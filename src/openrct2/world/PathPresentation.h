/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace OpenRCT2
{
    namespace PathPresentationFlags
    {
        constexpr uint32_t sloped = 1 << 0, queue = 1 << 1, wide = 1 << 2, queueBanner = 1 << 3, ghost = 1 << 4,
                           additionGhost = 1 << 5, broken = 1 << 6, legacy = 1 << 7, junctionRailings = 1 << 8,
                           invisible = 1 << 9, blockedByVehicle = 1 << 10;
    }
    // Raw fields, in original tile-element order. No image selection or per-tile stack limit.
    struct PathPresentationRecord
    {
        int32_t baseZ{}, clearanceZ{};
        uint32_t elementOrdinal{}, flags{};
        uint16_t surfaceSlot{}, railingsSlot{}, additionSlot{ UINT16_MAX }, rideId{ UINT16_MAX };
        uint8_t edgesAndCorners{}, slopeDirection{}, queueBannerDirection{}, additionStatus{};
        bool operator==(const PathPresentationRecord&) const = default;
    };
    struct PathPresentationTileRange
    {
        uint32_t first{}, count{};
    };
    struct PathPresentationSurfaceMaterial
    {
        uint32_t imageBase{}, imageCount{}, surfaceImage{};
        uint8_t flags{};
        bool present{};
    };
    struct PathPresentationRailingsMaterial
    {
        uint32_t imageBase{}, imageCount{}, bridgeImage{}, railingsImage{};
        uint8_t supportType{}, supportColour{}, flags{}, scrollingMode{};
        bool present{};
    };
    struct PathPresentationAdditionMaterial
    {
        uint32_t imageBase{}, imageCount{}, image{};
        uint16_t flags{};
        uint8_t drawType{};
        bool present{};
    };
    // Owns value facts, not borrowed Object/G1 pointers. Atlas resolution separately checks revision and holds leases.
    struct PathPresentationMaterials
    {
        uint64_t revision{};
        std::array<PathPresentationSurfaceMaterial, 255> surfaces{}, legacySurfaces{}, legacyQueueSurfaces{};
        std::array<PathPresentationRailingsMaterial, 255> railings{}, legacyRailings{};
        std::array<PathPresentationAdditionMaterial, 255> additions{};
    };
    inline std::atomic<uint64_t> gPathObjectRevision{ 1 };
    inline std::atomic<uint32_t> gPathObjectMutationDepth{};
    inline uint64_t GetPathObjectRevision() noexcept
    {
        return gPathObjectRevision.load(std::memory_order_acquire);
    }
    inline void AdvancePathObjectRevision()
    {
        auto current = gPathObjectRevision.load(std::memory_order_relaxed);
        do
        {
            if (current == UINT64_MAX)
                throw std::overflow_error("Path object revision space exhausted");
        } while (!gPathObjectRevision.compare_exchange_weak(current, current + 1, std::memory_order_release));
    }
} // namespace OpenRCT2
