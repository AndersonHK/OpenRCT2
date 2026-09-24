/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once
#include "../Limits.h"
#include "../object/ObjectLimits.h"

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace OpenRCT2
{
    enum class WorldObjectKind : uint8_t
    {
        smallScenery,
        largeScenery,
        wall,
        banner,
        track,
        entrance
    };
    namespace WorldObjectPresentationFlags
    {
        constexpr uint32_t ghost = 1 << 0, invisible = 1 << 1, needsSupports = 1 << 2, wallAnimating = 1 << 3,
                           wallBackwards = 1 << 4, wallAcrossTrack = 1 << 5, chain = 1 << 6, cable = 1 << 7, inverted = 1 << 8,
                           brakeClosed = 1 << 9, greenLight = 1 << 10, highlight = 1 << 11, legacyPath = 1 << 12,
                           nextElementAtClearance = 1 << 13, anyLaterElementAtClearance = 1 << 14;
    }
    // Immutable raw graphical state. Tile coordinates come from the owning tile range; no chosen image or quad.
    struct WorldObjectPresentationRecord
    {
        int32_t baseZ{}, clearanceZ{};
        uint32_t elementOrdinal{}, flags{};
        uint16_t objectSlot{ UINT16_MAX }, bannerId{ UINT16_MAX }, trackType{}, rideType{}, rideId{ UINT16_MAX }, mazeEntry{},
            sequence{}, pathSurfaceSlot{ UINT16_MAX };
        WorldObjectKind kind{};
        uint8_t direction{}, quadrant{}, age{}, primaryColour{}, secondaryColour{}, tertiaryColour{}, animationFrame{}, slope{},
            position{}, allowedEdges{}, colourScheme{}, stationIndex{}, brakeBoosterSpeed{}, photoTimeout{}, seatRotation{},
            doorA{}, doorB{}, entranceType{};
        bool operator==(const WorldObjectPresentationRecord&) const = default;
    };
    struct WorldObjectPresentationTileRange
    {
        uint32_t first{}, count{};
    };
    struct SmallSceneryPresentationMaterial
    {
        uint32_t imageBase{}, imageCount{}, image{}, flags{};
        uint8_t height{};
        uint16_t animationDelay{}, animationMask{}, numFrames{};
        bool present{};
        std::vector<uint8_t> frameOffsets;
    };
    struct LargeSceneryPresentationTile
    {
        int32_t x{}, y{}, z{}, zClearance{};
        uint8_t corners{}, walls{};
        bool hasSupports{}, allowSupportsAbove{};
    };
    struct LargeSceneryPresentationMaterial
    {
        uint32_t imageBase{}, imageCount{}, image{}, flags{};
        uint8_t scrollingMode{};
        bool present{};
        std::vector<LargeSceneryPresentationTile> tiles;
    };
    struct WallPresentationMaterial
    {
        uint32_t imageBase{}, imageCount{}, image{};
        uint8_t flags{}, flags2{}, height{}, scrollingMode{};
        bool present{};
    };
    struct BannerPresentationMaterial
    {
        uint32_t imageBase{}, imageCount{}, image{};
        uint8_t flags{}, scrollingMode{};
        bool present{};
    };
    struct StationPresentationMaterial
    {
        uint32_t imageBase{}, imageCount{}, image{ UINT32_MAX }, flags{};
        uint32_t entranceBack{ UINT32_MAX }, entranceFront{ UINT32_MAX }, exitBack{ UINT32_MAX }, exitFront{ UINT32_MAX };
        uint32_t entranceBackGlass{ UINT32_MAX }, entranceFrontGlass{ UINT32_MAX }, exitBackGlass{ UINT32_MAX },
            exitFrontGlass{ UINT32_MAX };
        uint32_t shelter{ UINT32_MAX }, shelterGlass{ UINT32_MAX };
        int32_t height{};
        uint8_t scrollingMode{};
        bool present{};
    };
    struct ParkEntrancePresentationMaterial
    {
        uint32_t imageBase{}, imageCount{}, image{};
        uint8_t scrollingMode{}, textHeight{};
        bool present{};
    };
    struct RideObjectPresentationMaterial
    {
        uint32_t imageBase{}, imageCount{}, carBaseImage{};
        bool present{};
    };
    struct WorldObjectPresentationUsage
    {
        static constexpr size_t kKinds = 7;
        static constexpr size_t kSlots = 2048;
        std::array<std::bitset<kSlots>, kKinds> slots{};
        [[nodiscard]] bool Contains(uint32_t kind, uint32_t slot) const noexcept
        {
            return kind < kKinds && slot < kSlots && slots[kind][slot];
        }
        // Families 4 and 6 are track and ride-entrance dependencies keyed by ride ID.
        [[nodiscard]] bool ContainsRide(uint32_t rideId) const noexcept
        {
            return Contains(4, rideId) || Contains(6, rideId);
        }
        static std::pair<uint32_t, uint32_t> Reference(const WorldObjectPresentationRecord& record) noexcept
        {
            if (record.kind == WorldObjectKind::track)
                return { 4, record.rideId };
            if (record.kind == WorldObjectKind::entrance && record.entranceType != 2)
                return { 6, record.rideId };
            return { static_cast<uint32_t>(record.kind), record.objectSlot };
        }
        bool operator==(const WorldObjectPresentationUsage&) const = default;
    };
    struct WorldObjectPresentationMaterials
    {
        uint64_t revision{};
        std::array<SmallSceneryPresentationMaterial, kMaxSmallSceneryObjects> smallScenery;
        std::array<LargeSceneryPresentationMaterial, kMaxLargeSceneryObjects> largeScenery;
        std::array<WallPresentationMaterial, kMaxWallSceneryObjects> walls;
        std::array<BannerPresentationMaterial, kMaxBannerObjects> banners;
        std::array<StationPresentationMaterial, kMaxStationObjects> stations;
        std::array<ParkEntrancePresentationMaterial, kMaxParkEntranceObjects> parkEntrances;
        std::array<RideObjectPresentationMaterial, kMaxRideObjects> rideObjects;
    };
    struct WorldTrackColour
    {
        uint8_t main{}, additional{}, supports{};
        bool operator==(const WorldTrackColour&) const = default;
    };
    struct WorldVehicleColour
    {
        uint8_t body{}, trim{}, tertiary{};
        bool operator==(const WorldVehicleColour&) const = default;
    };
    struct WorldRideStation
    {
        int32_t startX{}, startY{}, startZ{};                                     // world units
        int32_t entranceX{}, entranceY{}, entranceZ{}, exitX{}, exitY{}, exitZ{}; // tile units
        bool startValid{}, entranceValid{}, exitValid{};
        bool operator==(const WorldRideStation&) const = default;
    };
    struct WorldRidePresentationRecord
    {
        uint16_t rideType{}, objectSlot{ UINT16_MAX }, stationStyle{ UINT16_MAX };
        uint16_t regularStyle{}, invertedStyle{}, coveredStyle{}, coveredInvertedStyle{};
        bool present{};
        std::array<WorldTrackColour, 4> trackColours{};
        uint8_t vehicleColourSettings{}, numStations{}, numTrains{};
        // Static flat-ride bodies consume exactly four colour schemes (including Space Rings).
        // Moving vehicle colours belong to their future entity publication, not this world catalogue.
        std::array<WorldVehicleColour, 4> vehicleColours{};
        std::vector<WorldRideStation> stations;
        bool operator==(const WorldRidePresentationRecord&) const = default;
    };
    struct WorldRidePresentationMaterials
    {
        uint64_t revision{};
        std::vector<WorldRidePresentationRecord> rides;
    };
    inline std::atomic<uint64_t> gWorldObjectRevision{ 1 };
    inline uint64_t GetWorldObjectRevision() noexcept
    {
        return gWorldObjectRevision.load(std::memory_order_acquire);
    }
    inline void AdvanceWorldObjectRevision()
    {
        auto current = gWorldObjectRevision.load(std::memory_order_relaxed);
        do
        {
            if (current == UINT64_MAX)
                throw std::overflow_error("World object revision space exhausted");
        } while (!gWorldObjectRevision.compare_exchange_weak(current, current + 1, std::memory_order_release));
    }
} // namespace OpenRCT2
