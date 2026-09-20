/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace OpenRCT2
{
    // Raw immutable world facts; no selected tile images, projection or ordering.
    struct TerrainPresentationRecord
    {
        int32_t baseZ{};
        uint16_t surfaceSlot{}, edgeSlot{};
        uint8_t slope{}, grass{}, kind{}; // 0 unsupported/missing, 1 surface, 2 border
    };
    struct TerrainPresentationMaterial
    {
        uint32_t imageBase{}, imageCount{};
        bool supported{};
        std::array<uint32_t, 9 * 4 * 4> selectors{};
    };
    struct TerrainPresentationMaterials
    {
        uint64_t revision{};
        std::array<TerrainPresentationMaterial, 255> surfaces{}, edges{};
    };
    // Atomic because object loading can use workers. Publication remains at the existing owner barrier.
    inline std::atomic<uint64_t> gTerrainObjectRevision{ 1 };
    inline uint64_t GetTerrainObjectRevision() noexcept
    {
        return gTerrainObjectRevision.load(std::memory_order_acquire);
    }
    inline void AdvanceTerrainObjectRevision()
    {
        auto current = gTerrainObjectRevision.load(std::memory_order_relaxed);
        do
        {
            if (current == UINT64_MAX)
                throw std::overflow_error("Terrain object revision space exhausted");
        } while (!gTerrainObjectRevision.compare_exchange_weak(current, current + 1, std::memory_order_release));
    }
}
