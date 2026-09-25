/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "EntityPresentationSnapshot.h"

#include "../GameState.h"
#include "../profiling/Profiling.h"
#include "../ride/Vehicle.h"
#include "Balloon.h"
#include "Duck.h"
#include "Guest.h"
#include "JumpingFountain.h"
#include "Litter.h"
#include "MoneyEffect.h"
#include "Particle.h"
#include "Staff.h"

#include <algorithm>
#include <bit>
#include <bitset>
#include <cstring>
#include <utility>

namespace OpenRCT2
{
    namespace
    {
        thread_local const EntityPresentationSnapshot* _currentSnapshot{};
        const std::vector<EntityId> _emptySpatialList;

        [[nodiscard]] size_t GetConcreteEntitySize(const EntityType type) noexcept
        {
            switch (type)
            {
                case EntityType::vehicle:
                    return sizeof(Vehicle);
                case EntityType::guest:
                    return sizeof(Guest);
                case EntityType::staff:
                    return sizeof(Staff);
                case EntityType::steamParticle:
                    return sizeof(SteamParticle);
                case EntityType::moneyEffect:
                    return sizeof(MoneyEffect);
                case EntityType::crashedVehicleParticle:
                    return sizeof(VehicleCrashParticle);
                case EntityType::explosionCloud:
                    return sizeof(ExplosionCloud);
                case EntityType::crashSplash:
                    return sizeof(CrashSplashParticle);
                case EntityType::explosionFlare:
                    return sizeof(ExplosionFlare);
                case EntityType::jumpingFountain:
                    return sizeof(JumpingFountain);
                case EntityType::balloon:
                    return sizeof(Balloon);
                case EntityType::duck:
                    return sizeof(Duck);
                case EntityType::litter:
                    return sizeof(Litter);
                default:
                    return sizeof(EntityBase);
            }
        }

        [[nodiscard]] constexpr size_t AlignEntityOffset(const size_t offset) noexcept
        {
            constexpr auto alignment = alignof(std::max_align_t);
            return (offset + alignment - 1) & ~(alignment - 1);
        }
    } // namespace

    EntityPresentationSnapshot::EntityPresentationSnapshot() = default;

    std::shared_ptr<const EntityPresentationSnapshot> EntityPresentationSnapshot::Capture(
        EntityRegistry& registry, const std::span<const CoordsXY> tileLocations, const std::span<const EntityId> lookupRoots)
    {
        PROFILED_FUNCTION();

        auto snapshot = std::make_shared<EntityPresentationSnapshot>();
        std::bitset<kMaxEntities> captureIds;
        std::vector<EntityId> closureQueue;

        const auto enqueue = [&captureIds, &closureQueue](const EntityId id) {
            const auto index = id.ToUnderlying();
            if (index < kMaxEntities && !captureIds.test(index))
            {
                captureIds.set(index);
                closureQueue.push_back(id);
            }
        };

        for (const auto& location : tileLocations)
        {
            const auto bucketIndex = EntityRegistry::ComputeSpatialIndex(location);
            const auto& liveBucket = registry.gEntitySpatialIndex[bucketIndex];
            for (const auto id : liveBucket)
                enqueue(id);
        }

        if (lookupRoots.empty())
        {
            for (const auto id : registry.getEntityList(EntityType::vehicle))
                enqueue(id);
        }
        else
        {
            for (const auto id : lookupRoots)
                enqueue(id);
        }

        // Track painters can resolve a visible ride's representative vehicle even when that vehicle is outside the viewport.
        // Vehicle painters also walk train links and read rider colours, so close those exact relationships transitively.
        for (size_t queueIndex = 0; queueIndex < closureQueue.size(); queueIndex++)
        {
            const auto id = closureQueue[queueIndex];
            const auto* vehicle = registry.getEntity<Vehicle>(id);
            if (vehicle == nullptr)
                continue;
            enqueue(vehicle->next_vehicle_on_train);
            enqueue(vehicle->prev_vehicle_on_ride);
            enqueue(vehicle->next_vehicle_on_ride);
            enqueue(vehicle->cable_lift_target);
            for (const auto peepId : vehicle->peep)
                enqueue(peepId);
        }

        EntityVisualChangeBatch batch{ registry._entityVisualEpoch, true, {}, {} };
        batch.changes.reserve(captureIds.count());
        size_t payloadSize = 0;
        for (uint32_t index = 0; index < kMaxEntities; index++)
        {
            const auto* entity = registry.entities[index];
            if (captureIds.test(index) && entity != nullptr)
                payloadSize += GetConcreteEntitySize(entity->type);
        }
        batch.payload.reserve(payloadSize);
        for (uint32_t index = 0; index < kMaxEntities; index++)
        {
            const auto* entity = registry.entities[index];
            if (!captureIds.test(index) || entity == nullptr)
                continue;

            auto& change = batch.changes.emplace_back();
            change.handle = registry.GetEntityVisualHandle(EntityId::FromUnderlying(index));
            change.type = entity->type;
            change.location = entity->getLocation();
            change.spriteData = entity->spriteData;
            change.orientation = entity->orientation;
            change.dirty = EntityVisualDirty::full;
            change.present = true;
            const auto entitySize = GetConcreteEntitySize(entity->type);
            change.payloadOffset = static_cast<uint32_t>(batch.payload.size());
            change.payloadSize = static_cast<uint16_t>(entitySize);
            const auto* first = reinterpret_cast<const std::byte*>(entity);
            batch.payload.insert(batch.payload.end(), first, first + entitySize);
        }
        snapshot->Apply(batch);

        return snapshot;
    }

    void EntityPresentationSnapshot::Apply(const EntityVisualChangeBatch& batch)
    {
        PROFILED_FUNCTION();
        _bulkMode = false;
        _nativeOnly = false;
        _unsupportedEntityCount = 0;
        _retainedBalloons.reset();
        _retainedPeeps.reset();
        _peepAnimations.reset();
        _balloonMetrics = {};

        if (batch.reset || _epoch != batch.epoch || _entityChunks.empty())
        {
            _entityChunks.assign(kEntityChunkCount, nullptr);
            _spatialChunks.assign(kSpatialChunkCount, nullptr);
            _entityCount = 0;
        }

        std::vector<std::shared_ptr<SpatialChunk>> mutableSpatialChunks(kSpatialChunkCount);
        const auto getMutableSpatialBucket = [this, &mutableSpatialChunks](const uint32_t bucket) -> std::vector<EntityId>& {
            const auto chunkIndex = bucket / kSpatialChunkWidth;
            auto& mutableChunk = mutableSpatialChunks[chunkIndex];
            if (mutableChunk == nullptr)
            {
                mutableChunk = _spatialChunks[chunkIndex] == nullptr
                    ? std::make_shared<SpatialChunk>()
                    : std::make_shared<SpatialChunk>(*_spatialChunks[chunkIndex]);
                _spatialChunks[chunkIndex] = mutableChunk;
            }
            return (*mutableChunk)[bucket % kSpatialChunkWidth];
        };

        size_t activeEntityChunkIndex = kEntityChunkCount;
        std::shared_ptr<EntityChunk> activeEntityChunk;
        for (const auto& change : batch.changes)
        {
            const auto idIndex = change.handle.id.ToUnderlying();
            if (idIndex >= kMaxEntities)
                continue;

            const auto* oldEntity = TryGetEntity(change.handle.id);
            const bool wasPresent = oldEntity != nullptr;
            const auto oldBucket = wasPresent ? EntityRegistry::ComputeSpatialIndex(oldEntity->getLocation())
                                              : kInvalidSpatialIndex;

            const auto entityChunkIndex = idIndex / kEntityChunkWidth;
            if (entityChunkIndex != activeEntityChunkIndex)
            {
                activeEntityChunkIndex = entityChunkIndex;
                activeEntityChunk = _entityChunks[entityChunkIndex] == nullptr
                    ? std::make_shared<EntityChunk>()
                    : std::make_shared<EntityChunk>(*_entityChunks[entityChunkIndex]);
                _entityChunks[entityChunkIndex] = activeEntityChunk;
            }

            const auto entityOffset = idIndex % kEntityChunkWidth;
            if (change.present)
            {
                const auto* payload = batch.payload.data() + change.payloadOffset;
                std::memcpy(activeEntityChunk->entities[entityOffset].pad00, payload, change.payloadSize);
                activeEntityChunk->present.set(entityOffset);
                if (!wasPresent)
                    _entityCount++;
            }
            else
            {
                activeEntityChunk->present.reset(entityOffset);
                if (wasPresent)
                    _entityCount--;
            }

            const auto* newEntity = change.present ? &activeEntityChunk->entities[entityOffset].base : nullptr;
            const auto newBucket = newEntity != nullptr ? EntityRegistry::ComputeSpatialIndex(newEntity->getLocation())
                                                        : kInvalidSpatialIndex;
            if (oldBucket == newBucket)
                continue;

            if (wasPresent)
            {
                auto& oldList = getMutableSpatialBucket(oldBucket);
                const auto position = std::ranges::lower_bound(oldList, change.handle.id);
                if (position != oldList.end() && *position == change.handle.id)
                    oldList.erase(position);
            }
            if (newEntity != nullptr)
            {
                auto& newList = getMutableSpatialBucket(newBucket);
                const auto position = std::ranges::lower_bound(newList, change.handle.id);
                if (position == newList.end() || *position != change.handle.id)
                    newList.insert(position, change.handle.id);
            }
        }
        _epoch = batch.epoch;
    }

    void EntityPresentationSnapshot::CaptureStorage(
        EntityRegistry& registry, std::shared_ptr<const Drawing::RetainedBalloonSnapshot> balloons,
        Drawing::BalloonPublicationMetrics metrics, std::shared_ptr<const Drawing::RetainedPeepSnapshot> peeps,
        std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> peepAnimations)
    {
        PROFILED_FUNCTION();
        _nativeOnly = false;
        _unsupportedEntityCount = 0;
        _vehicles.reset();
        _effects.reset();
        _money.reset();
        _retainedBalloons = std::move(balloons);
        _balloonMetrics = metrics;
        _retainedPeeps = std::move(peeps);
        _peepAnimations = std::move(peepAnimations);
        _epoch = registry.GetEntityVisualEpoch();
        registry.CaptureEntityPresentationStorage(*this);
        _sourceTick = getGameState().currentTicks;
    }

    void EntityPresentationSnapshot::CaptureNativeStorage(
        EntityRegistry& registry, std::shared_ptr<const Drawing::RetainedPeepSnapshot> peeps,
        std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> catalog,
        std::shared_ptr<const Drawing::RetainedBalloonSnapshot> balloons,
        std::shared_ptr<const Drawing::VehiclePresentationSnapshot> vehicles,
        std::shared_ptr<const Drawing::WorldEffectSnapshot> effects,
        std::shared_ptr<const Drawing::MoneyPresentationSnapshot> money)
    {
        if (!_nativeOnly)
        {
            // A recycled legacy snapshot must relinquish concrete object clones and CPU indexing storage.
            decltype(_entityChunks){}.swap(_entityChunks);
            decltype(_spatialChunks){}.swap(_spatialChunks);
            decltype(_bulkPages){}.swap(_bulkPages);
            decltype(_bulkEntityIndex){}.swap(_bulkEntityIndex);
            decltype(_bulkSpatialBucketIndex){}.swap(_bulkSpatialBucketIndex);
            decltype(_bulkSpatialBuckets){}.swap(_bulkSpatialBuckets);
            _bulkPageCount = _bulkSpatialBucketCount = 0;
        }
        _nativeOnly = true;
        _bulkMode = false;
        _vehicles = std::move(vehicles);
        _effects = std::move(effects);
        _money = std::move(money);
        _retainedBalloons = std::move(balloons);
        _balloonMetrics = {};
        _retainedPeeps = std::move(peeps);
        _peepAnimations = std::move(catalog);
        _epoch = registry.GetEntityVisualEpoch();
        _sourceTick = getGameState().currentTicks;
        _unsupportedEntityCount = 0;
        for (uint8_t index = 0; index < static_cast<uint8_t>(EntityType::count); ++index)
        {
            const auto type = static_cast<EntityType>(index);
            if (type != EntityType::guest && type != EntityType::staff)
                _unsupportedEntityCount += registry.GetEntityExecutionList(type).size();
        }
        _entityCount = _unsupportedEntityCount;
    }

    void EntityPresentationSnapshot::BuildCapturedStorage()
    {
        PROFILED_FUNCTION();

        if (_nativeOnly)
            return;
        _bulkEntityIndex.assign(kMaxEntities, nullptr);
        _bulkSpatialBucketIndex.resize(kSpatialIndexSize);
        std::ranges::fill(_bulkSpatialBucketIndex, UINT16_MAX);
        for (size_t index = 0; index < _bulkSpatialBucketCount; index++)
            _bulkSpatialBuckets[index].entities.clear();
        _bulkSpatialBucketCount = 0;
        _entityCount = 0;

        for (size_t pageIndex = 0; pageIndex < _bulkPageCount; pageIndex++)
        {
            auto& page = _bulkPages[pageIndex];
            for (size_t wordIndex = 0; wordIndex < page.occupied.size(); wordIndex++)
            {
                auto word = page.occupied[wordIndex];
                while (word != 0)
                {
                    const auto bit = static_cast<size_t>(std::countr_zero(word));
                    const auto slot = wordIndex * 64 + bit;
                    const auto* entity = reinterpret_cast<const EntityBase*>(page.storage.data() + slot * page.stride);
                    const auto idIndex = entity->id.ToUnderlying();
                    if (idIndex < kMaxEntities && entity->type == page.type)
                    {
                        _bulkEntityIndex[idIndex] = entity;
                        _entityCount++;

                        const auto bucketIndex = EntityRegistry::ComputeSpatialIndex(entity->getLocation());
                        auto bucketSlot = _bulkSpatialBucketIndex[bucketIndex];
                        if (bucketSlot == UINT16_MAX)
                        {
                            bucketSlot = static_cast<uint16_t>(_bulkSpatialBucketCount++);
                            _bulkSpatialBucketIndex[bucketIndex] = bucketSlot;
                            if (bucketSlot == _bulkSpatialBuckets.size())
                                _bulkSpatialBuckets.emplace_back();
                            auto& bucket = _bulkSpatialBuckets[bucketSlot];
                            bucket.index = bucketIndex;
                            bucket.entities.clear();
                        }
                        _bulkSpatialBuckets[bucketSlot].entities.push_back(entity->id);
                    }
                    word &= word - 1;
                }
            }
        }

        if (_retainedBalloons != nullptr)
        {
            for (const auto& chunk : _retainedBalloons->chunks)
            {
                if (chunk == nullptr)
                    continue;
                for (size_t offset = 0; offset < Drawing::kRetainedBalloonChunkWidth; ++offset)
                {
                    ++_balloonMetrics.fallbackSlotVisits;
                    if (chunk->records[offset].present == 0)
                        continue;
                    const auto* entity = &chunk->compatibility[offset];
                    _bulkEntityIndex[entity->id.ToUnderlying()] = entity;
                    ++_entityCount;
                    ++_balloonMetrics.fallbackIndexedBalloons;
                    const auto bucketIndex = EntityRegistry::ComputeSpatialIndex(entity->getLocation());
                    auto bucketSlot = _bulkSpatialBucketIndex[bucketIndex];
                    if (bucketSlot == UINT16_MAX)
                    {
                        bucketSlot = static_cast<uint16_t>(_bulkSpatialBucketCount++);
                        _bulkSpatialBucketIndex[bucketIndex] = bucketSlot;
                        if (bucketSlot == _bulkSpatialBuckets.size())
                            _bulkSpatialBuckets.emplace_back();
                        _bulkSpatialBuckets[bucketSlot].index = bucketIndex;
                        _bulkSpatialBuckets[bucketSlot].entities.clear();
                    }
                    _bulkSpatialBuckets[bucketSlot].entities.push_back(entity->id);
                }
            }
        }
        for (size_t index = 0; index < _bulkSpatialBucketCount; index++)
            std::ranges::sort(_bulkSpatialBuckets[index].entities);
        _bulkMode = true;
    }

    const EntityBase* EntityPresentationSnapshot::TryGetEntity(const EntityId id) const noexcept
    {
        if (_nativeOnly)
            return nullptr;
        const auto idIndex = id.ToUnderlying();
        if (idIndex >= kMaxEntities)
            return nullptr;
        if (_bulkMode)
            return _bulkEntityIndex[idIndex];
        if (idIndex / kEntityChunkWidth >= _entityChunks.size())
            return nullptr;
        const auto& chunk = _entityChunks[idIndex / kEntityChunkWidth];
        const auto offset = idIndex % kEntityChunkWidth;
        return chunk != nullptr && chunk->present.test(offset) ? &chunk->entities[offset].base : nullptr;
    }

    const std::vector<EntityId>& EntityPresentationSnapshot::GetEntityTileList(const CoordsXY& location) const noexcept
    {
        if (_nativeOnly)
            return _emptySpatialList;
        const auto bucket = EntityRegistry::ComputeSpatialIndex(location);
        if (_bulkMode)
        {
            const auto index = _bulkSpatialBucketIndex[bucket];
            return index == UINT16_MAX ? _emptySpatialList : _bulkSpatialBuckets[index].entities;
        }
        if (bucket / kSpatialChunkWidth >= _spatialChunks.size())
            return _emptySpatialList;
        const auto& chunk = _spatialChunks[bucket / kSpatialChunkWidth];
        return chunk == nullptr ? _emptySpatialList : (*chunk)[bucket % kSpatialChunkWidth];
    }

    std::optional<GuestPresentationFacts> GetGuestPresentationFacts(EntityId id) noexcept
    {
        const auto* snapshot = GetCurrentEntityPresentationSnapshot();
        if (snapshot != nullptr && snapshot->GetRetainedPeeps() != nullptr)
        {
            const size_t index = id.ToUnderlying();
            if (index >= kMaxEntities)
                return std::nullopt;
            const auto& chunk = snapshot->GetRetainedPeeps()->chunks[index / Drawing::kRetainedPeepChunkWidth];
            const auto offset = index % Drawing::kRetainedPeepChunkWidth;
            if (!chunk || !chunk->lifecycle)
                return std::nullopt;
            const auto flags = chunk->lifecycle->values[offset].flags;
            if (!(flags & Drawing::kRetainedPeepPresent) || (flags & Drawing::kRetainedPeepStaff))
                return std::nullopt;
            // Ride paint needs colours/state only; it never reconstructs motion or borrows simulation storage.
            return GuestPresentationFacts{ chunk->appearance->values[offset].colours,
                                           static_cast<uint8_t>(
                                               (chunk->animation->values[offset].flags & Drawing::kRetainedPeepStateMask)
                                               >> Drawing::kRetainedPeepStateShift) };
        }
        const auto* guest = GetEntityForPresentation<Guest>(id);
        if (guest == nullptr)
            return std::nullopt;
        return GuestPresentationFacts{ static_cast<uint32_t>(guest->getTShirtColour())
                                           | (static_cast<uint32_t>(guest->getTrousersColour()) << 8),
                                       static_cast<uint8_t>(guest->state) };
    }

    ScopedEntityPresentationSnapshot::ScopedEntityPresentationSnapshot(const EntityPresentationSnapshot* snapshot) noexcept
        : _previous(std::exchange(_currentSnapshot, snapshot))
    {
    }

    ScopedEntityPresentationSnapshot::~ScopedEntityPresentationSnapshot()
    {
        _currentSnapshot = _previous;
    }

    const EntityPresentationSnapshot* GetCurrentEntityPresentationSnapshot() noexcept
    {
        return _currentSnapshot;
    }

    EntityBase* GetEntityForPresentation(const EntityId id) noexcept
    {
        if (_currentSnapshot != nullptr)
            return const_cast<EntityBase*>(_currentSnapshot->TryGetEntity(id));
        return getGameState().entities.getEntity(id);
    }
} // namespace OpenRCT2
