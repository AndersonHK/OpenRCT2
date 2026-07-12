/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "EntityRegistry.h"

#include "../Diagnostic.h"
#include "../Game.h"
#include "../GameState.h"
#include "../core/Algorithm.hpp"
#include "../core/ChecksumStream.h"
#include "../core/Crypt.h"
#include "../core/DataSerialiser.h"
#include "../core/Guard.hpp"
#include "../core/MemoryStream.h"
#include "../core/String.hpp"
#include "../entity/EntityList.h"
#include "../entity/Peep.h"
#include "../entity/Staff.h"
#include "../interface/Viewport.h"
#include "../peep/RideUseSystem.h"
#include "../profiling/Profiling.h"
#include "../ride/Vehicle.h"
#include "../world/Map.h"
#include "Balloon.h"
#include "Duck.h"
#include "EntityTweener.h"
#include "JumpingFountain.h"
#include "MoneyEffect.h"
#include "Particle.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <numeric>
#include <vector>

namespace OpenRCT2
{
    using namespace OpenRCT2::Core;

    static constexpr uint32_t ComputeSpatialIndex(const CoordsXY& loc)
    {
        if (loc.IsNull())
            return kSpatialIndexNullBucket;

        // NOTE: The input coordinate is rotated and can have negative components.
        const auto tileX = std::abs(loc.x) / kCoordsXYStep;
        const auto tileY = std::abs(loc.y) / kCoordsXYStep;

        if (tileX >= kMaximumMapSizeTechnical || tileY >= kMaximumMapSizeTechnical)
            return kSpatialIndexNullBucket;

        return tileX * kMaximumMapSizeTechnical + tileY;
    }

    static constexpr uint32_t GetSpatialIndex(EntityBase& entity)
    {
        return entity.spatialIndex & ~kSpatialIndexDirtyMask;
    }

    static constexpr bool EntityTypeIsMiscEntity(const EntityType type)
    {
        switch (type)
        {
            case EntityType::steamParticle:
            case EntityType::moneyEffect:
            case EntityType::crashedVehicleParticle:
            case EntityType::explosionCloud:
            case EntityType::crashSplash:
            case EntityType::explosionFlare:
            case EntityType::jumpingFountain:
            case EntityType::balloon:
            case EntityType::duck:
                return true;
            default:
                return false;
        }
    }

    // TODO: make part of EntityList unit?
    uint16_t EntityRegistry::GetEntityListCount(EntityType type)
    {
        return static_cast<uint16_t>(gEntityLists[EnumValue(type)].size());
    }

    // TODO: make part of EntityList unit?
    uint16_t EntityRegistry::GetNumFreeEntities()
    {
        return static_cast<uint16_t>(_freeIds.size());
    }

    std::string EntitiesChecksum::ToString() const
    {
        return String::StringFromHex(raw);
    }

    EntityBase* EntityRegistry::GetEntity(EntityId entityIndex)
    {
        if (entityIndex.IsNull())
        {
            return nullptr;
        }
        Guard::Assert(entityIndex.ToUnderlying() < kMaxEntities, "Tried getting entity %u", entityIndex.ToUnderlying());
        return TryGetEntity(entityIndex);
    }

    const std::vector<EntityId>& EntityRegistry::GetEntityTileList(const CoordsXY& spritePos)
    {
        return gEntitySpatialIndex[ComputeSpatialIndex(spritePos)];
    }

    const EntityIdList& EntityRegistry::GetEntityList(const EntityType id)
    {
        return gEntityLists[EnumValue(id)];
    }

    const std::vector<EntityId>& EntityRegistry::GetVehicleHeadEntityList()
    {
        if (!_vehicleHeadEntityListDirty)
        {
            return _vehicleHeadEntityList;
        }

        const auto& vehicles = gEntityLists[EnumValue(EntityType::vehicle)];
        _vehicleHeadEntityList.clear();
        _vehicleHeadEntityList.reserve(vehicles.size());
        for (const auto entityId : vehicles)
        {
            const auto* vehicle = GetEntity<Vehicle>(entityId);
            if (vehicle->IsHead())
                _vehicleHeadEntityList.push_back(entityId);
        }
        _vehicleHeadEntityListDirty = false;
        return _vehicleHeadEntityList;
    }

    /**
     *
     *  rct2: 0x0069EB13
     */
    void EntityRegistry::ResetAllEntities()
    {
        // Free live entities before zeroing storage. The typed membership is complete, so avoid scanning every empty slot.
        for (const auto& list : gEntityLists)
        {
            for (const auto entityId : list)
            {
                FreeEntity(entities[entityId.ToUnderlying()].base);
            }
        }

        std::fill(std::begin(entities), std::end(entities), Entity_t());
        RideUse::GetHistory().Clear();
        RideUse::GetTypeHistory().Clear();
        for (uint32_t i = 0; i < kMaxEntities; ++i)
        {
            auto& entity = entities[i].base;
            entity.type = EntityType::null;
            entity.id = EntityId::FromUnderlying(static_cast<EntityId::UnderlyingType>(i));

            _entityFlashingList[i] = false;
        }
        for (auto& list : gEntityLists)
            list.clear();
        _vehicleHeadEntityList.clear();
        _vehicleHeadEntityListDirty = true;
        _freeIds.fill();
        ResetEntitySpatialIndices();
    }

    /**
     *
     *  rct2: 0x0069EBE4
     * This function looks as though it sets some sort of order for sprites.
     * Sprites can share their position if this is the case.
     */
    void EntityRegistry::ResetEntitySpatialIndices()
    {
        ClearSpatialIndexDirtyWorklist();
        for (auto& vec : gEntitySpatialIndex)
        {
            vec.clear();
        }
        // Membership is rebuilt alongside entity creation/import. Iterate live ids rather than rescanning every registry
        // slot, which is especially wasteful immediately after ResetAllEntities() has cleared every list.
        for (const auto& list : gEntityLists)
        {
            for (const auto entityId : list)
            {
                auto& entity = entities[entityId.ToUnderlying()].base;
                EntitySpatialInsert(entity, { entity.x, entity.y });
            }
        }
    }

#ifndef DISABLE_NETWORK
    EntitiesChecksum EntityRegistry::GetAllEntitiesChecksum()
    {
        EntitiesChecksum checksum{};

        ChecksumStream ms(checksum.raw);
        DataSerialiser ds(true, ms);
        NetworkSerialiseEntityTypes<Guest, Staff, Vehicle, Litter>(ds);

        return checksum;
    }
#else
    EntitiesChecksum EntityRegistry::GetAllEntitiesChecksum()
    {
        return EntitiesChecksum{};
    }
#endif // DISABLE_NETWORK

    void EntityRegistry::EntityReset(EntityBase& entity)
    {
        // Retain the registry slot id while resetting entity storage.
        auto entityIndex = entity.id;
        _entityFlashingList[entityIndex.ToUnderlying()] = false;

        Entity_t* tempEntity = reinterpret_cast<Entity_t*>(&entity);
        *tempEntity = Entity_t();

        entity.id = entityIndex;
        entity.type = EntityType::null;
    }

    uint16_t EntityRegistry::GetMiscEntityCount()
    {
        uint16_t count = 0;
        for (auto id : { EntityType::steamParticle, EntityType::moneyEffect, EntityType::crashedVehicleParticle,
                         EntityType::explosionCloud, EntityType::crashSplash, EntityType::explosionFlare,
                         EntityType::jumpingFountain, EntityType::balloon, EntityType::duck })
        {
            count += GetEntityListCount(id);
        }
        return count;
    }

    void EntityRegistry::PrepareNewEntity(EntityBase& base, const EntityType type)
    {
        // Need to reset all sprite data, as the uninitialised values
        // may contain garbage and cause a desync later on.
        EntityReset(base);

        base.type = type;
        auto& list = gEntityLists[EnumValue(type)];
        Guard::Assert(list.insert(base.id), "Entity %u is already in its typed list", base.id.ToUnderlying());
        if (type == EntityType::vehicle)
            _vehicleHeadEntityListDirty = true;

        base.x = kLocationNull;
        base.y = kLocationNull;
        base.z = 0;
        base.spriteData.width = 0x10;
        base.spriteData.heightMin = 0x14;
        base.spriteData.heightMax = 0x8;
        base.spriteData.spriteRect = {};
        base.spatialIndex = kInvalidSpatialIndex;

        EntitySpatialInsert(base, { kLocationNull, 0 });
    }

    EntityBase* EntityRegistry::CreateEntity(EntityType type)
    {
        if (_freeIds.empty())
        {
            // No free sprites.
            return nullptr;
        }

        if (EntityTypeIsMiscEntity(type))
        {
            // Misc sprites are commonly used for effects, give other entity types higher priority.
            if (GetMiscEntityCount() >= kMaxMiscEntities)
            {
                return nullptr;
            }

            // If there are less than kMaxMiscEntities free slots, ensure other entities can be created.
            if (_freeIds.size() < kMaxMiscEntities)
            {
                return nullptr;
            }
        }

        const auto entityId = *_freeIds.begin();
        Guard::Assert(_freeIds.erase(entityId), "Entity %u was not free", entityId.ToUnderlying());
        auto& entity = entities[entityId.ToUnderlying()].base;
        PrepareNewEntity(entity, type);
        return &entity;
    }

    EntityBase* EntityRegistry::CreateEntityAt(const EntityId index, const EntityType type)
    {
        if (!_freeIds.erase(index))
        {
            return nullptr;
        }

        auto& entity = entities[index.ToUnderlying()].base;
        PrepareNewEntity(entity, type);
        return &entity;
    }

    /**
     *
     *  rct2: 0x00672AA4
     */
    void EntityRegistry::UpdateAllMiscEntities()
    {
        PROFILED_FUNCTION();

        MiscUpdateAllTypes<
            SteamParticle, MoneyEffect, VehicleCrashParticle, ExplosionCloud, CrashSplashParticle, ExplosionFlare,
            JumpingFountain, Balloon, Duck>();
    }

    void EntityRegistry::UpdateMoneyEffect()
    {
        MiscUpdateAllTypes<MoneyEffect>();
    }

    // Performs a search to ensure that insert keeps next_in_quadrant in sprite_index order
    void EntityRegistry::EntitySpatialInsert(EntityBase& entity, const CoordsXY& newLoc)
    {
        const auto newIndex = ComputeSpatialIndex(newLoc);

        auto& spatialVector = gEntitySpatialIndex[newIndex];

        Algorithm::sortedInsert(spatialVector, entity.id);

        entity.spatialIndex = newIndex;
    }

    void EntityRegistry::EntitySpatialRemove(EntityBase& entity)
    {
        const auto currentIndex = GetSpatialIndex(entity);

        auto& spatialVector = gEntitySpatialIndex[currentIndex];
        auto index = Algorithm::binaryFind(std::begin(spatialVector), std::end(spatialVector), entity.id);
        if (index != std::end(spatialVector))
        {
            spatialVector.erase(index, index + 1);
        }
        else
        {
            LOG_WARNING("Bad sprite spatial index. Rebuilding the spatial index...");
            ResetEntitySpatialIndices();
        }

        entity.spatialIndex = kInvalidSpatialIndex;
    }

    void EntityRegistry::UpdateEntitySpatialIndex(EntityBase& entity)
    {
        if (entity.spatialIndex & kSpatialIndexDirtyMask)
        {
            if (entity.spatialIndex != kInvalidSpatialIndex)
            {
                EntitySpatialRemove(entity);
            }
            EntitySpatialInsert(entity, { entity.x, entity.y });
        }
    }

    void EntityRegistry::QueueEntitySpatialIndexUpdate(EntityBase& entity)
    {
        const auto entityIndex = entity.id.ToUnderlying();
        if (entityIndex >= kMaxEntities || entity.type == EntityType::null || TryGetEntity(entity.id) != &entity
            || _spatialIndexDirtyQueued.test(entityIndex))
        {
            return;
        }

        // Keep the id queued through immediate updates so another move in this tick still reaches the final bucket.
        _spatialIndexDirtyQueued.set(entityIndex);
        _spatialIndexDirtyEntities.push_back(entity.id);
    }

    void EntityRegistry::UpdateEntitiesSpatialIndex()
    {
        if (_spatialIndexDirtyEntities.empty())
        {
            return;
        }

        PROFILED_FUNCTION();

        // Process detached storage so a defensive full rebuild can safely clear the member worklist.
        auto pending = std::move(_spatialIndexDirtyEntities);
        _spatialIndexDirtyEntities.clear();

        for (const auto entityId : pending)
        {
            const auto entityIndex = entityId.ToUnderlying();
            _spatialIndexDirtyQueued.reset(entityIndex);

            auto* entity = TryGetEntity(entityId);
            if (entity->type == EntityType::null || !(entity->spatialIndex & kSpatialIndexDirtyMask))
                continue;

            UpdateEntitySpatialIndex(*entity);
        }

        pending.clear();
        if (_spatialIndexDirtyEntities.empty())
        {
            _spatialIndexDirtyEntities.swap(pending);
        }
    }

    void EntityRegistry::ClearSpatialIndexDirtyWorklist() noexcept
    {
        _spatialIndexDirtyEntities.clear();
        _spatialIndexDirtyQueued.reset();
    }

    /**
     * Frees any dynamically attached memory to the entity, such as peep name.
     */
    void EntityRegistry::FreeEntity(EntityBase& entity)
    {
        if (auto* staff = entity.as<Staff>(); staff != nullptr)
        {
            staff->SetName({});
            staff->clearPatrolArea();
        }
        else if (auto* guest = entity.as<Guest>(); guest != nullptr)
        {
            guest->SetName({});
            guest->guestNextInQueue = EntityId::GetNull();

            RideUse::GetHistory().RemoveHandle(guest->id);
            RideUse::GetTypeHistory().RemoveHandle(guest->id);
        }
    }

    /**
     *
     *  rct2: 0x0069EDB6
     */
    void EntityRegistry::EntityRemove(EntityBase* entity)
    {
        FreeEntity(*entity);

        EntityTweener::Get().RemoveEntity(entity);
        auto& list = gEntityLists[EnumValue(entity->type)];
        Guard::Assert(list.erase(entity->id), "Entity %u was not in its typed list", entity->id.ToUnderlying());
        Guard::Assert(_freeIds.insert(entity->id), "Entity %u is already free", entity->id.ToUnderlying());
        if (entity->type == EntityType::vehicle)
            _vehicleHeadEntityListDirty = true;

        EntitySpatialRemove(*entity);
        EntityReset(*entity);
    }

    /**
     * Loops through all floating entities and removes them.
     * Returns the amount of removed objects as feedback.
     */
    uint16_t EntityRegistry::RemoveFloatingEntities()
    {
        uint16_t removed = 0;
        for (auto* balloon : EntityList<Balloon>())
        {
            EntityRemove(balloon);
            removed++;
        }
        for (auto* duck : EntityList<Duck>())
        {
            if (duck->IsFlying())
            {
                EntityRemove(duck);
                removed++;
            }
        }
        for (auto* money : EntityList<MoneyEffect>())
        {
            EntityRemove(money);
            removed++;
        }
        return removed;
    }

    void EntityRegistry::EntitySetFlashing(EntityBase* entity, bool flashing)
    {
        assert(entity->id.ToUnderlying() < kMaxEntities);
        _entityFlashingList[entity->id.ToUnderlying()] = flashing;
    }

    bool EntityRegistry::EntityGetFlashing(EntityBase* entity)
    {
        assert(entity->id.ToUnderlying() < kMaxEntities);
        return _entityFlashingList[entity->id.ToUnderlying()];
    }
} // namespace OpenRCT2

using namespace OpenRCT2;

CoordsXYZ EntityBase::getLocation() const
{
    return { x, y, z };
}

void EntityBase::setLocation(const CoordsXYZ& newLocation)
{
    if (getLocation() == newLocation)
    {
        // No change, this can happen quite often when the entity is interpolated.
        return;
    }

    x = newLocation.x;
    y = newLocation.y;
    z = newLocation.z;

    if (spatialIndex & kSpatialIndexDirtyMask)
    {
        // Already marked as dirty.
        return;
    }

    const auto newSpatialIndex = ComputeSpatialIndex({ x, y });
    if (newSpatialIndex == GetSpatialIndex(*this))
    {
        // Avoid marking it dirty when we don't leave the current tile.
        return;
    }

    spatialIndex |= kSpatialIndexDirtyMask;

    // Transient entities are ignored by the registry-side identity check.
    getGameState().entities.QueueEntitySpatialIndexUpdate(*this);
}

static void EntitySetLocation(EntityBase& entity, const CoordsXYZ& location, bool updateSpatialIndex)
{
    if (updateSpatialIndex)
    {
        entity.setLocation(location);
    }
    else
    {
        entity.x = location.x;
        entity.y = location.y;
        entity.z = location.z;
    }
}

static void EntitySetCoordinates(const CoordsXYZ& entityPos, EntityBase* entity, bool updateSpatialIndex)
{
    auto screenCoords = Translate3DTo2DWithZ(GetCurrentRotation(), entityPos);

    entity->spriteData.spriteRect = ScreenRect(
        screenCoords - ScreenCoordsXY{ entity->spriteData.width, entity->spriteData.heightMin },
        screenCoords + ScreenCoordsXY{ entity->spriteData.width, entity->spriteData.heightMax });
    EntitySetLocation(*entity, entityPos, updateSpatialIndex);
}

static void EntityMoveTo(EntityBase& entity, const CoordsXYZ& newLocation, bool updateSpatialIndex)
{
    if (entity.x != kLocationNull)
    {
        // Invalidate old position.
        entity.invalidate();
    }

    auto loc = newLocation;
    if (!MapIsLocationValid(loc))
    {
        loc.x = kLocationNull;
    }

    if (loc.x == kLocationNull)
    {
        EntitySetLocation(entity, loc, updateSpatialIndex);
    }
    else
    {
        EntitySetCoordinates(loc, &entity, updateSpatialIndex);
        entity.invalidate(); // Invalidate new position.
    }
}

void EntityBase::moveTo(const CoordsXYZ& newLocation)
{
    EntityMoveTo(*this, newLocation, true);
}

void EntityBase::moveToForTween(const CoordsXYZ& newLocation)
{
    EntityMoveTo(*this, newLocation, false);
}

void EntityBase::moveToAndUpdateSpatialIndex(const CoordsXYZ& newLocation)
{
    moveTo(newLocation);

    // TODO: pass as param instead of relying on global game state
    auto& gameState = getGameState();

    gameState.entities.UpdateEntitySpatialIndex(*this);
}
