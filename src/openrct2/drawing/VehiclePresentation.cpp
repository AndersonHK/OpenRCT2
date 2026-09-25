// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "VehiclePresentation.h"

#include "../Context.h"
#include "../GameState.h"
#include "../core/Console.hpp"
#include "../entity/EntityList.h"
#include "../entity/EntityTweener.h"
#include "../entity/Guest.h"
#include "../object/ObjectManager.h"
#include "../object/RideObject.h"
#include "../profiling/Profiling.h"
#include "../ride/CarEntry.h"
#include "../ride/RideData.h"
#include "../ride/Vehicle.h"
#include "../world/Map.h"
#include "../world/MapPresentationSnapshot.h"
#include "../world/WorldObjectPresentation.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace OpenRCT2::Drawing
{
    namespace
    {
        static_assert(EnumValue(SpriteGroupType::count) == kVehiclePresentationGroups);
        VehiclePresentationCar CaptureCar(const CarEntry& source, uint32_t imageBase, uint32_t imageCount)
        {
            VehiclePresentationCar out;
            out.imageBase = imageBase;
            out.imageCount = imageCount;
            out.baseImage = source.baseImageId;
            out.baseFrames = source.baseNumFrames;
            out.carImages = source.numCarImages;
            out.seatingRows = source.numSeatingRows;
            out.riderImageBanks = source.getNumRiderImageBanks();
            out.paintStyle = EnumValue(source.paintStyle);
            out.drawOrder = source.drawOrder;
            out.spinningFrames = source.spinningNumFrames;
            out.effectVisual = EnumValue(source.effectVisual);
            out.flags = source.flags.holder;
            out.present = source.isVisible();
            if (std::getenv("OPENRCT2_VEHICLE_CATALOG_REPORT") != nullptr && source.flags.has(CarEntryFlag::hasRiderAnimation))
                Console::WriteLine(
                    "Vehicle rider animation: objectBase=%u base=%u animation=%u frames=%u rows=%u riderBanks=%u", imageBase,
                    source.baseImageId, EnumValue(source.animation), source.animationFrames, source.numSeatingRows,
                    out.riderImageBanks);
            for (size_t i = 0; i < out.groups.size(); ++i)
                out.groups[i] = { source.spriteGroups[i].imageId, EnumValue(source.spriteGroups[i].spritePrecision) };
            return out;
        }
        std::shared_ptr<const VehiclePresentationCatalog> CaptureCatalog()
        {
            static std::shared_ptr<const VehiclePresentationCatalog> held;
            const auto revision = GetWorldObjectRevision();
            if (gPathObjectMutationDepth.load(std::memory_order_acquire) != 0)
                throw std::logic_error("Vehicle catalog requested during object mutation");
            if (held && held->revision == revision)
                return held;
            auto result = std::make_shared<VehiclePresentationCatalog>();
            result->revision = revision;
            result->cableCar = kMaxRideObjects * kVehiclePresentationCarsPerObject;
            result->cars.resize(result->cableCar + 1);
            auto& manager = GetContext()->GetObjectManager();
            for (uint16_t slot = 0; slot < kMaxRideObjects; ++slot)
                if (const auto* object = manager.GetLoadedObject<RideObject>(slot))
                {
                    if (std::getenv("OPENRCT2_VEHICLE_CATALOG_REPORT") != nullptr)
                        Console::WriteLine(
                            "Vehicle object bank: slot=%u id=%.*s base=%u count=%u", slot,
                            static_cast<int>(object->GetIdentifier().size()), object->GetIdentifier().data(),
                            object->GetBaseImageId(), object->GetNumImages());
                    const auto& entry = object->GetEntry();
                    static_assert(std::size(entry.Cars) == kVehiclePresentationCarsPerObject);
                    for (uint32_t car = 0; car < kVehiclePresentationCarsPerObject; ++car)
                        result->cars[slot * kVehiclePresentationCarsPerObject + car] = CaptureCar(
                            entry.Cars[car], object->GetBaseImageId(), object->GetNumImages());
                }
            // Cable-lift sprites belong to the immutable original G1 bank.
            result->cars[result->cableCar] = CaptureCar(
                // The original G1 cable car has no generic numCarImages stride.
                // Its final slopes60 group is 32 rotations in each direction.
                kCableLiftVehicle, kCableLiftVehicle.baseImageId,
                kCableLiftVehicle.groupImageId(SpriteGroupType::slopes60)
                    + kCableLiftVehicle.numRotationSprites(SpriteGroupType::slopes60) * 2 - kCableLiftVehicle.baseImageId);
            held = result;
            return result;
        }
    } // namespace
    void UpdateVehiclePresentationResidency(VehiclePresentationSnapshot& snapshot, const VehiclePresentationSnapshot* previous)
    {
        const auto& catalog = *snapshot.catalog;
        std::vector<bool> resident(catalog.cars.size());
        for (const auto& record : *snapshot.records)
        {
            if (record.carSlot >= catalog.cars.size())
                throw std::invalid_argument("Vehicle references an invalid residency car slot");
            if (record.carSlot == catalog.cableCar)
            {
                resident[record.carSlot] = true;
                continue;
            }
            const auto first = record.carSlot / kVehiclePresentationCarsPerObject * kVehiclePresentationCarsPerObject;
            const auto end = std::min<size_t>(first + kVehiclePresentationCarsPerObject, catalog.cars.size());
            // Inversion and pitch/roll fallback select sibling cars. Their artwork
            // belongs to the same cold object family and must remain resident even
            // when no car happens to select that variant in this particular tick.
            for (auto slot = first; slot < end; ++slot)
                if (slot != catalog.cableCar && catalog.cars[slot].present)
                    resident[slot] = true;
        }
        auto used = std::make_shared<std::vector<uint32_t>>();
        for (uint32_t slot = 0; slot < resident.size(); ++slot)
            if (resident[slot])
                used->push_back(slot);
        // Membership selects cold artwork, not entity identity. Equal car slots
        // in the same object generation remain valid across a new map/entity epoch.
        const bool compatible = previous && previous->catalog == snapshot.catalog;
        snapshot.usedCars = compatible && previous->usedCars && *previous->usedCars == *used ? previous->usedCars : used;
    }

    std::shared_ptr<const VehiclePresentationSnapshot> CaptureVehiclePresentationSnapshot(uint32_t sourceTick)
    {
        PROFILED_FUNCTION();
        static std::shared_ptr<const VehiclePresentationSnapshot> held;
        static uint64_t revision = 0;
        auto& state = getGameState();
        auto result = std::make_shared<VehiclePresentationSnapshot>();
        result->worldEpoch = GetMapPresentationEpoch();
        result->entityEpoch = state.entities.GetEntityVisualEpoch();
        result->sourceTick = sourceTick;
        result->catalog = CaptureCatalog();
        auto records = std::make_shared<std::vector<VehiclePresentationRecord>>();
        records->reserve(state.entities.getEntityListCount(EntityType::vehicle));
        for (auto* car : EntityList<Vehicle>())
        {
            if (!car->IsCableLift()
                && (car->ride_subtype >= kMaxRideObjects || car->vehicle_type >= kVehiclePresentationCarsPerObject))
                throw std::invalid_argument("Vehicle references an invalid ride-object car slot");
            VehiclePresentationRecord r;
            auto position = car->getLocation();
            if (const auto motion = EntityTweener::get().GetMotion(*car))
                position = motion->current;
            r.x = position.x;
            r.y = position.y;
            r.z = position.z;
            const auto handle = state.entities.GetEntityVisualHandle(car->id);
            r.entityId = car->id.ToUnderlying();
            r.generation = handle.generation;
            r.carSlot = car->IsCableLift()
                ? result->catalog->cableCar
                : uint32_t(car->ride_subtype) * kVehiclePresentationCarsPerObject + car->vehicle_type;
            if (r.carSlot >= result->catalog->cars.size())
                continue;
            r.flags = car->flags.holder | (car->isGhost() ? 1u << 31 : 0u) | (car->IsOnCoveredTrack() ? 1u << 30 : 0u)
                | (car->IsHead() ? 1u << 29 : 0u);
            r.orientation = car->orientation;
            r.pitch = EnumValue(car->pitch);
            r.roll = EnumValue(car->roll);
            r.animation = car->animation_frame;
            r.swing = car->SwingSprite;
            r.spin = car->spin_sprite;
            r.restraints = car->restraints_position;
            r.colours = EnumValue(car->colours.Body) | (uint32_t(EnumValue(car->colours.Trim)) << 8)
                | (uint32_t(EnumValue(car->colours.Tertiary)) << 16);
            r.numPeeps = car->num_peeps;
            if (r.numPeeps > std::size(car->peep_tshirt_colours))
                throw std::invalid_argument("Vehicle passenger count exceeds owned colour slots");
            // Authored rider sprites can remap a pair, including the partner of
            // an odd final rider. Preserve that source lookup, omit unused rows.
            const auto riderColourCount = (r.numPeeps + 1u) & ~1u;
            for (uint32_t i = 0; i < riderColourCount; ++i)
                r.riderColours[i / 4] |= uint32_t(EnumValue(car->peep_tshirt_colours[i])) << ((i % 4) * 8);
            r.trackType = EnumValue(car->GetTrackType());
            r.trackProgress = car->track_progress;
            r.velocity = car->velocity;
            r.SetStatusAndRide(EnumValue(car->status), car->ride.ToUnderlying());
            r.animationState = car->animationState;
            r.miniGolf = EnumValue(car->mini_golf_current_animation);
            if (car->num_peeps != 0)
                if (const auto* guest = state.entities.getEntity<Guest>(car->peep[0]))
                    r.miniGolf |= uint32_t(EnumValue(guest->getTShirtColour())) << 8
                        | uint32_t(EnumValue(guest->getTrousersColour())) << 16 | (1u << 24);
            r.previousLink = car->prev_vehicle_on_ride.ToUnderlying();
            r.nextLink = car->next_vehicle_on_ride.ToUnderlying();
            records->push_back(r);
            if ((r.flags & (1u << 11)) != 0)
            {
                if (car->IsCableLift() || car->vehicle_type + 1 >= kVehiclePresentationCarsPerObject)
                    throw std::invalid_argument("Inverted vehicle references an invalid adjacent car slot");
            }
        }
        // EntityList's ascending membership already gives the GPU lookup its canonical ID order.
        const bool compatible = held && held->worldEpoch == result->worldEpoch && held->entityEpoch == result->entityEpoch;
        result->records = compatible && *held->records == *records ? held->records : records;
        UpdateVehiclePresentationResidency(*result, held.get());
        result->revision = compatible && result->records == held->records ? held->revision : ++revision;
        held = result;
        return result;
    }
} // namespace OpenRCT2::Drawing
