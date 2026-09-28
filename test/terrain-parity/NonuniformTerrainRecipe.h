/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace NonuniformTerrainFixture
{
    constexpr uint32_t kRecipeVersion = 1;
    constexpr uint32_t kWidth = 32;
    constexpr uint32_t kHeight = 32;
    constexpr std::array<std::string_view, 2> kSurfaceIdentifiers = { "rct2.terrain_surface.grass",
                                                                      "rct2.terrain_surface.sand" };
    constexpr std::array<std::string_view, 2> kEdgeIdentifiers = { "rct2.terrain_edge.rock", "rct2.terrain_edge.wood_red" };

    struct Tile
    {
        uint16_t baseZ = 16;
        uint8_t slope{};
        uint8_t grassLength{};
        uint8_t surfaceVariant{};
        uint8_t edgeVariant{};
    };

    // Pure preparation recipe, not called by a renderer or per-frame publisher.
    // All 1,024 coordinates remain one surface each. A 16x16 central region mixes
    // adjacent elevations, every canonical non-steep slope, two surfaces and two edges.
    constexpr std::array<Tile, kWidth * kHeight> MakeTiles()
    {
        std::array<Tile, kWidth * kHeight> result{};
        for (uint32_t y = 8; y < 24; y++)
        {
            for (uint32_t x = 8; x < 24; x++)
            {
                result[y * kWidth + x] = {
                    static_cast<uint16_t>(16 * (1 + ((x / 4 + 2 * (y / 4)) % 4))),
                    static_cast<uint8_t>(((x % 4) + 4 * (y % 4)) % 15),
                    static_cast<uint8_t>((x + 3 * y) % 7),
                    static_cast<uint8_t>((x / 2 + y / 2) % 2),
                    static_cast<uint8_t>((x + y / 2) % 2),
                };
            }
        }
        return result;
    }
} // namespace NonuniformTerrainFixture
