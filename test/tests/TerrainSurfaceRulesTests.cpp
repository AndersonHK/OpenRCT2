/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "../terrain-parity/NonuniformTerrainRecipe.h"

#include <gtest/gtest.h>
#include <openrct2-renderer/gpu/TerrainSurfaceRules.h>
#include <openrct2/drawing/ImageId.hpp>
#include <openrct2/object/TerrainSurfaceObject.h>
#include <openrct2/world/Location.hpp>
#include <openrct2/world/tile_element/Slope.h>
#include <set>

namespace Rules = OpenRCT2::Ui::Gpu::Terrain;

// All-raised slope15 is raw representation coverage; the gameplay fixture uses only slopes0..14.
TEST(TerrainSurfaceRulesTest, NonSteepCornersAgreeWithFrozenPublicSlopeRulesAtEveryRotation)
{
    for (uint32_t slope = 0; slope < 16; slope++)
    {
        for (uint32_t rotation = 0; rotation < 4; rotation++)
        {
            const auto relative = Rules::RelativeSlope(slope, rotation);
            const auto expected = OpenRCT2::GetSlopeRelativeCornerHeights(static_cast<uint8_t>(relative));
            const std::array<uint32_t, 4> corners = { expected.top, expected.right, expected.bottom, expected.left };
            EXPECT_EQ(Rules::kShapeRules[relative].cornerHeights, corners);
            EXPECT_EQ(Rules::RelativeSlope(relative, 4 - rotation), slope);
        }
    }
}

TEST(TerrainSurfaceRulesTest, CompiledSelectorsAgreeWithObjectRulesIncludingOrderedWildcardsAndDistantGrass)
{
    OpenRCT2::TerrainSurfaceObject object;
    object.EntryBaseImageId = 1000;
    object.DefaultEntry = 3;
    object.Colour = OpenRCT2::Drawing::kColourNull;
    const std::array specials = {
        Rules::MaterialSpecial{ 5, 2, Rules::kAny, 1 },
        Rules::MaterialSpecial{ 6, Rules::kAny, 1, 1 },
        Rules::MaterialSpecial{ 7, 4, 3, Rules::kAny },
        Rules::MaterialSpecial{ 8, Rules::kAny, 3, 2 },
    };
    for (const auto& entry : specials)
        object.SpecialEntries.push_back({ static_cast<uint8_t>(entry.entry), static_cast<uint8_t>(entry.length),
                                          static_cast<uint8_t>(entry.rotation), static_cast<uint8_t>(entry.variation) });
    const auto compiled = Rules::CompileMaterialLookup(object.DefaultEntry, specials);
    // Raw selector7 is representation coverage; the gameplay fixture uses named lengths0..6.
    for (uint32_t lengthIndex = 0; lengthIndex < 9; lengthIndex++)
    {
        const auto length = lengthIndex == 8 ? Rules::kAny : lengthIndex;
        for (uint32_t rotation = 0; rotation < 4; rotation++)
        {
            for (uint32_t variation = 0; variation < 4; variation++)
            {
                const CoordsXY position{ static_cast<int32_t>((variation & 1) * 32),
                                         static_cast<int32_t>((variation >> 1) * 32) };
                for (uint8_t offset = 0; offset < Rules::kImagesPerSurfaceEntry; offset++)
                {
                    const auto actual = object.GetImageId(
                        position, static_cast<uint8_t>(length), static_cast<uint8_t>(rotation), offset, false, false);
                    const auto selected = compiled.entries[Rules::MaterialSelector(length, rotation, variation)];
                    EXPECT_EQ(actual.GetIndex(), object.EntryBaseImageId + selected * Rules::kImagesPerSurfaceEntry + offset);
                }
            }
        }
    }
}

TEST(TerrainSurfaceRulesTest, NonuniformRecipeContainsAdjacentDiscontinuitiesAndPreservesReservedBorder)
{
    const auto tiles = NonuniformTerrainFixture::MakeTiles();
    std::set<uint16_t> heights;
    std::set<uint8_t> slopes, surfaces, edges, grass;
    uint32_t unequalNeighbours = 0;
    for (uint32_t y = 0; y < NonuniformTerrainFixture::kHeight; y++)
    {
        for (uint32_t x = 0; x < NonuniformTerrainFixture::kWidth; x++)
        {
            const auto& tile = tiles[y * NonuniformTerrainFixture::kWidth + x];
            heights.insert(tile.baseZ);
            slopes.insert(tile.slope);
            surfaces.insert(tile.surfaceVariant);
            edges.insert(tile.edgeVariant);
            grass.insert(tile.grassLength);
            if (x == 0 || y == 0 || x == 31 || y == 31)
            {
                EXPECT_EQ(tile.baseZ, 16);
                EXPECT_EQ(tile.slope, 0);
            }
            if (x < 31 && tile.baseZ != tiles[y * NonuniformTerrainFixture::kWidth + x + 1].baseZ)
                unequalNeighbours++;
        }
    }
    EXPECT_EQ(heights.size(), 4u);
    EXPECT_EQ(slopes.size(), 15u);
    EXPECT_EQ(surfaces.size(), 2u);
    EXPECT_EQ(edges.size(), 2u);
    EXPECT_EQ(grass.size(), 7u);
    EXPECT_GT(unequalNeighbours, 0u);
}
