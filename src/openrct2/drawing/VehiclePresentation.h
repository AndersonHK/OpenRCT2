// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace OpenRCT2::Drawing
{
    constexpr uint32_t kVehiclePresentationGroups = 40;
    constexpr uint32_t kVehiclePresentationCarsPerObject = 4;
    struct VehiclePresentationCar
    {
        uint32_t imageBase{}, imageCount{}, baseImage{}, baseFrames{}, carImages{}, seatingRows{}, paintStyle{}, drawOrder{},
            spinningFrames{}, effectVisual{};
        uint64_t flags{};
        uint8_t riderImageBanks{}; // Car-owned complete row/animation domain, independent of the current pose.
        bool present{};
        // Original immutable image group starts/precision, never camera-selected images.
        std::array<std::array<uint32_t, 2>, kVehiclePresentationGroups> groups{};
    };
    struct VehiclePresentationCatalog
    {
        uint64_t revision{};
        uint32_t cableCar{ UINT32_MAX };
        std::vector<VehiclePresentationCar> cars;
    };
    struct VehiclePresentationRecord
    {
        int32_t x{}, y{}, z{};
        uint32_t entityId{}, generation{}, carSlot{}, flags{}, orientation{}, pitch{}, roll{}, animation{}, swing{}, spin{},
            restraints{}, colours{}, numPeeps{};
        std::array<uint32_t, 8> riderColours{};
        uint32_t trackType{}, trackProgress{};
        int32_t velocity{};
        uint32_t status{}, animationState{}, miniGolf{}, previousLink{}, nextLink{};
        bool operator==(const VehiclePresentationRecord&) const = default;
    };
    static_assert(sizeof(VehiclePresentationRecord) == 128);
    struct VehiclePresentationSnapshot
    {
        uint64_t worldEpoch{}, entityEpoch{}, revision{};
        uint32_t sourceTick{};
        std::shared_ptr<const VehiclePresentationCatalog> catalog;
        std::shared_ptr<const std::vector<VehiclePresentationRecord>> records;
        std::shared_ptr<const std::vector<uint32_t>> usedCars;
    };
    // Resolve residency by instantiated ride-object family, never current pose.
    // Reuses immutable membership until its owners or catalog generation change.
    void UpdateVehiclePresentationResidency(VehiclePresentationSnapshot& snapshot, const VehiclePresentationSnapshot* previous);
    // Simulation-owner boundary only. Copies graphical fields, never paint structs,
    // selected images, live object pointers, or a camera-dependent command list.
    std::shared_ptr<const VehiclePresentationSnapshot> CaptureVehiclePresentationSnapshot(uint32_t sourceTick);
} // namespace OpenRCT2::Drawing
