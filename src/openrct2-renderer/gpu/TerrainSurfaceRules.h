/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace OpenRCT2::Ui::Gpu::Terrain
{
    // Static object metadata for a future shader lookup. These helpers do not
    // paint tiles or build frame commands. Compile only on object revision.
    constexpr uint32_t kAny = 255;
    constexpr uint32_t kImagesPerSurfaceEntry = 19;
    constexpr size_t kMaterialSelectorCount = 9 * 4 * 4;

    struct MaterialSpecial
    {
        uint32_t entry{};
        uint32_t length = kAny;
        uint32_t rotation = kAny;
        uint32_t variation = kAny;
    };

    struct MaterialLookup
    {
        // grass 0..7, distant 8; then rotation; then tile parity x | (y << 1).
        std::array<uint32_t, kMaterialSelectorCount> entries{};
    };

    constexpr size_t MaterialSelector(uint32_t length, uint32_t rotation, uint32_t variation)
    {
        return ((length == kAny ? 8 : length) * 4 + rotation) * 4 + variation;
    }

    constexpr MaterialLookup CompileMaterialLookup(uint32_t defaultEntry, std::span<const MaterialSpecial> specials)
    {
        MaterialLookup result;
        for (uint32_t lengthIndex = 0; lengthIndex < 9; lengthIndex++)
        {
            const auto length = lengthIndex == 8 ? kAny : lengthIndex;
            for (uint32_t rotation = 0; rotation < 4; rotation++)
            {
                for (uint32_t variation = 0; variation < 4; variation++)
                {
                    auto entry = defaultEntry;
                    for (const auto& special : specials)
                    {
                        if ((special.length == kAny || special.length == length)
                            && (special.rotation == kAny || special.rotation == rotation)
                            && (special.variation == kAny || special.variation == variation))
                        {
                            entry = special.entry;
                            break; // Object file order is significant.
                        }
                    }
                    result.entries[MaterialSelector(length, rotation, variation)] = entry;
                }
            }
        }
        return result;
    }

    struct ShapeRule
    {
        uint32_t imageOffset;
        // Screen-relative corners: top, right, bottom, left; units of 16 world Z.
        std::array<uint32_t, 4> cornerHeights;
    };

    // Initial native scope excludes diagonal/steep slopes. The GPU rotates the
    // four raw slope bits, then indexes this immutable table.
    constexpr std::array<ShapeRule, 16> kShapeRules = { {
        { 0, { 0, 0, 0, 0 } },
        { 2, { 0, 0, 1, 0 } },
        { 1, { 0, 0, 0, 1 } },
        { 3, { 0, 0, 1, 1 } },
        { 8, { 1, 0, 0, 0 } },
        { 10, { 1, 0, 1, 0 } },
        { 9, { 1, 0, 0, 1 } },
        { 11, { 1, 0, 1, 1 } },
        { 4, { 0, 1, 0, 0 } },
        { 6, { 0, 1, 1, 0 } },
        { 5, { 0, 1, 0, 1 } },
        { 7, { 0, 1, 1, 1 } },
        { 12, { 1, 1, 0, 0 } },
        { 14, { 1, 1, 1, 0 } },
        { 13, { 1, 1, 0, 1 } },
        { 15, { 1, 1, 1, 1 } },
    } };

    constexpr uint32_t RelativeSlope(uint32_t slope, uint32_t rotation)
    {
        const auto corners = (slope & 15u) << (rotation & 3u);
        return (corners | (corners >> 4u)) & 15u;
    }

    struct EdgeRule
    {
        std::array<uint32_t, 2> ownCorners;
        std::array<uint32_t, 2> neighbourCorners;
        // World neighbour offset at rotation 0, rotated on the GPU.
        std::array<int32_t, 2> neighbourTile;
        // Bottom faces are parent sprites at these camera-relative world offsets.
        std::array<int32_t, 2> faceOffset;
        uint32_t imageOffset;
        uint32_t attachedToSurface;
    };

    // Bottom-left/right use edge entry offsets 0/5 plus filler 1..4.
    // Top-left/right use 33/30 plus corner incline+1, attached to their surface.
    constexpr std::array<EdgeRule, 4> kEdgeRules = { {
        { { 3, 2 }, { 0, 1 }, { 1, 0 }, { 30, 0 }, 0, 0 },
        { { 1, 2 }, { 0, 3 }, { 0, 1 }, { 0, 30 }, 5, 0 },
        { { 0, 3 }, { 1, 2 }, { 0, -1 }, { 0, 0 }, 33, 1 },
        { { 0, 1 }, { 3, 2 }, { -1, 0 }, { 0, 0 }, 30, 1 },
    } };

    static_assert(sizeof(ShapeRule) == 20);
    static_assert(sizeof(EdgeRule) == 40);
    static_assert(sizeof(MaterialLookup) == 576);
} // namespace OpenRCT2::Ui::Gpu::Terrain
