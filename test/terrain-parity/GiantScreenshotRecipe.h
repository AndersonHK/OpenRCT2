// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace GiantScreenshotFixture
{
    constexpr uint32_t kRecipeVersion = 1;
    constexpr uint32_t kWidth = 96, kHeight = 96, kBalloonCount = 800;
    constexpr std::array<std::string_view, 2> kSurfaceIdentifiers = {
        "rct2.terrain_surface.grass", "rct2.terrain_surface.sand" };
    constexpr std::array<std::string_view, 2> kEdgeIdentifiers = {
        "rct2.terrain_edge.rock", "rct2.terrain_edge.wood_red" };
    struct Tile
    {
        uint16_t baseZ = 16;
        uint8_t slope{}, grassLength{}, surfaceVariant{}, edgeVariant{};
        uint16_t water{};
    };
    constexpr auto MakeTiles()
    {
        std::array<Tile, kWidth * kHeight> result{};
        for (uint32_t y = 1; y < kHeight - 1; ++y)
            for (uint32_t x = 1; x < kWidth - 1; ++x)
            {
                const bool tower = (x == 3 || x == 92) && (y == 3 || y == 92);
                const uint16_t z = tower ? 512 : static_cast<uint16_t>(16 * (1 + (x / 3 + 2 * (y / 5)) % 12));
                result[y * kWidth + x] = { z,
                    static_cast<uint8_t>(tower ? 0 : ((x % 4) + 4 * (y % 4)) % 15),
                    static_cast<uint8_t>((x + 3 * y) % 7),
                    static_cast<uint8_t>((x / 2 + y / 3) % 2),
                    static_cast<uint8_t>((x + y / 2) % 2),
                    static_cast<uint16_t>(z <= 96 && (x / 7 + y / 9) % 3 == 0 ? 128 : 0) };
            }
        return result;
    }
    struct BalloonSpec { int32_t x, y, z; uint8_t colour, frame, popped, timeToMove; };
    constexpr BalloonSpec BalloonAt(uint32_t ordinal)
    {
        const uint32_t cell = ordinal / 2, gx = cell % 20, gy = cell / 20, overlap = ordinal % 2;
        const bool popped = ordinal % 7 == 0;
        return { static_cast<int32_t>((5 + 4 * gx) * 32 + 15 + 3 * overlap),
            static_cast<int32_t>((5 + 4 * gy) * 32 + 17 + 3 * overlap),
            static_cast<int32_t>(256 + (gx + 2 * gy) % 5 * 24 + 2 * overlap),
            static_cast<uint8_t>(ordinal % 54), static_cast<uint8_t>(ordinal % (popped ? 5 : 8)),
            static_cast<uint8_t>(popped), static_cast<uint8_t>(ordinal % 3) };
    }
}
