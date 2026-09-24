// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <array>
#include <gtest/gtest.h>
#include <openrct2/core/Numerics.hpp>
#include <openrct2/object/PathAdditionEntry.h>
#include <openrct2/world/tile_element/Slope.h>

namespace PathRules
{
#include "../../data/shaders/vulkan/world_path_order.glsl"
}
using namespace PathRules;

TEST(WorldPathRulesTest, BoundedComponentOrderingPreservesEveryParentAcrossDegenerateAndOverlappingBounds)
{
    uint32_t state = 0x72ad3019;
    const auto next = [&]() { return state = state * 1664525u + 1013904223u; };
    for (int sample = 0; sample < 10000; ++sample)
    {
        WorldPathPart parts[12]{};
        const int count = 1 + next() % 12;
        for (int i = 0; i < count; ++i)
            parts[i] = worldPathPart(
                i, 0, 0, 0, next() % 33, next() % 33, static_cast<int>(next() % 257) - 64, next() % 65, next() % 65,
                next() % 257);
        for (int rotation = 0; rotation < 4; ++rotation)
        {
            const auto order = worldPathOrder(parts, count, rotation);
            ASSERT_EQ(order.count, count) << "sample=" << sample << " rotation=" << rotation;
            uint32_t seen = 0;
            for (int i = 0; i < count; ++i)
            {
                ASSERT_GE(order.indices[i], 0);
                ASSERT_LT(order.indices[i], count);
                EXPECT_EQ(seen & (1u << order.indices[i]), 0u);
                seen |= 1u << order.indices[i];
            }
            EXPECT_EQ(seen, (1u << count) - 1u);
        }
    }
}

// Independently retained CPU painter table. oracle-ui-source-02 Paint.Path.cpp
// SHA256 5ba44b2df3d5dd87262a4d50672c6065817026366fb5a54af06248acd40859a1.
// These are rule tests, not an original-art raster qualification.
TEST(WorldPathRulesTest, SurfaceSelectionMatchesFrozenPainterAtEveryRotation)
{
    constexpr std::array<int, 256> expected = {
        0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 14, 15, 0, 1, 2, 20, 4, 5, 6, 22, 8, 9, 10, 26, 12, 13, 14, 36,
        0, 1, 2, 3, 4, 5, 21, 23, 8, 9,  10, 11, 12, 13, 33, 37, 0, 1, 2, 3,  4, 5, 6, 24, 8, 9, 10, 11, 12, 13, 14, 38,
        0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 29, 30, 34, 39, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 40,
        0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 35, 41, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 42,
        0, 1, 2, 3, 4, 5, 6,  7,  8, 25, 10, 27, 12, 31, 14, 43, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 28, 12, 13, 14, 44,
        0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 14, 45, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 46,
        0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 32, 14, 47, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 48,
        0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 14, 49, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 50
    };
    for (int key = 0; key < 256; key++)
        for (int rotation = 0; rotation < 4; rotation++)
        {
            // Rotate individual bits independently of the shared rotate helper.
            int rotated = 0;
            for (int bit = 0; bit < 8; bit++)
                if ((key & (1 << bit)) != 0)
                    rotated |= 1 << ((bit & 4) + ((bit + rotation) & 3));
            EXPECT_EQ(worldPathSurfaceOffset(key & 15, key >> 4, 0, false, false, rotation), expected[rotated]);
            EXPECT_EQ(worldPathSurfaceOffset(key & 15, key >> 4, 0, false, true, rotation), rotated & 15);
            for (int slope = 0; slope < 4; slope++)
                EXPECT_EQ(
                    worldPathSurfaceOffset(key & 15, key >> 4, slope, true, false, rotation), 16 + ((slope + rotation) % 4));
        }
}

TEST(WorldPathRulesTest, SupportsRespectTerrainSlopeAndUndergroundMaterialPolicy)
{
    using namespace OpenRCT2;
    constexpr std::array<int, 4> slope = { kTileSlopeSWSideUp, kTileSlopeNWSideUp, kTileSlopeNESideUp, kTileSlopeSESideUp };
    for (int direction = 0; direction < 4; direction++)
        for (int terrainSlope = 0; terrainSlope < 32; terrainSlope++)
        {
            EXPECT_EQ(
                worldPathNeedsSupports(64, true, direction, true, 64, terrainSlope, false), terrainSlope != slope[direction]);
            EXPECT_EQ(worldPathNeedsSupports(64, false, direction, true, 64, terrainSlope, true), terrainSlope != 0);
        }
    EXPECT_TRUE(worldPathNeedsSupports(64, false, 0, false, 0, 0, true));
    EXPECT_TRUE(worldPathNeedsSupports(64, false, 0, true, 32, 0, true));
    EXPECT_TRUE(worldPathNeedsSupports(64, false, 0, true, 96, 0, false));
    EXPECT_FALSE(worldPathNeedsSupports(64, false, 0, true, 96, 0, true));
}

TEST(WorldPathRulesTest, SurfaceBoundsAndBridgeImagesPreserveTraversalDependencies)
{
    constexpr int bounds[16][4] = { { 3, 3, 26, 26 }, { 0, 3, 29, 26 }, { 3, 3, 26, 29 }, { 0, 3, 29, 29 },
                                    { 3, 3, 29, 26 }, { 0, 3, 32, 26 }, { 3, 3, 29, 29 }, { 0, 3, 32, 29 },
                                    { 3, 0, 26, 29 }, { 0, 0, 29, 29 }, { 3, 0, 26, 32 }, { 0, 0, 29, 32 },
                                    { 3, 0, 29, 29 }, { 0, 0, 32, 29 }, { 3, 0, 29, 32 }, { 0, 0, 32, 32 } };
    constexpr int boxOrientation[16] = { 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0 };
    for (int e = 0; e < 16; e++)
    {
        auto p = worldPathSurfacePart(7, e, true, false);
        EXPECT_EQ(p.boundsX, bounds[e][0]);
        EXPECT_EQ(p.boundsY, bounds[e][1]);
        EXPECT_EQ(p.sizeX, bounds[e][2]);
        EXPECT_EQ(p.sizeY, bounds[e][3]);
        EXPECT_EQ(p.boundsZ, 1);
        EXPECT_EQ(p.sizeZ, 0);
        p = worldPathSurfacePart(7, e, false, true);
        EXPECT_EQ(p.boundsX, 3);
        EXPECT_EQ(p.boundsY, 3);
        EXPECT_EQ(p.sizeX, 26);
        EXPECT_EQ(p.sizeY, 26);
        EXPECT_EQ(p.boundsZ, 2);
        EXPECT_EQ(worldPathBridgeOffset(e, 0, false, false), 49 + boxOrientation[e]);
        EXPECT_EQ(worldPathBridgeOffset(e, 0, false, true), e);
    }
    for (int d = 0; d < 4; d++)
    {
        EXPECT_EQ(worldPathBridgeOffset(0, d, true, false), 51 + d);
        EXPECT_EQ(worldPathBridgeOffset(0, d, true, true), 16 + d);
    }
}

TEST(WorldPathRulesTest, FlatFenceParentOrderAndCornerSuppressionMatchPainter)
{
    constexpr int expected[16][4] = { { -1, -1, -1, -1 }, { 3, 3, -1, -1 },  { 4, 4, -1, -1 },  { 3, 4, 11, -1 },
                                      { 5, 5, -1, -1 },   { 1, 1, -1, -1 },  { 4, 5, 12, -1 },  { 1, 11, 12, -1 },
                                      { 2, 2, -1, -1 },   { 2, 3, 10, -1 },  { 0, 0, -1, -1 },  { 0, 11, 10, -1 },
                                      { 2, 5, 13, -1 },   { 1, 13, 10, -1 }, { 0, 12, 13, -1 }, { 11, 12, 13, 10 } };
    constexpr int queueExpected[16][4] = { { -1, -1, -1, -1 }, { 17, 17, -1, -1 }, { 18, 18, -1, -1 }, { 17, 18, 25, -1 },
                                           { 19, 19, -1, -1 }, { 15, 15, -1, -1 }, { 18, 19, 26, -1 }, { 15, 25, 26, -1 },
                                           { 16, 16, -1, -1 }, { 16, 17, 24, -1 }, { 14, 14, -1, -1 }, { 14, 24, 25, -1 },
                                           { 16, 19, 27, -1 }, { 15, 24, 27, -1 }, { 14, 26, 27, -1 }, { 24, 25, 26, 27 } };
    for (int e = 0; e < 16; e++)
        for (int corners = 0; corners < 16; corners++)
        {
            const auto p = worldPathFences(e, corners, 0, 0, false, false, true, false, true, true);
            int count = 0;
            for (int i = 0; i < 4; i++)
            {
                int image = expected[e][i];
                if (image < 0)
                    continue;
                int corner = image == 10 ? 3 : image - 11;
                if (image >= 10 && (corners & (1 << corner)) != 0)
                    continue;
                ASSERT_LT(count, p.count);
                EXPECT_EQ(p.parts[count++].imageOffset, image);
            }
            EXPECT_EQ(p.count, count);
            const auto q = worldPathFences(e, corners, 0, 0, false, true, false, true, true, true);
            for (int i = 0; i < 4; i++)
                EXPECT_EQ(q.parts[i].imageOffset, queueExpected[e][i]);
            EXPECT_EQ(worldPathFences(e, corners, 0, 0, false, false, false, false, true, true).count, 0);
            if (e == 7 || e == 11 || e == 13 || e == 14 || e == 15)
                EXPECT_EQ(worldPathFences(e, corners, 0, 0, false, true, false, false, true, false).count, 0);
        }
    auto corner = worldPathFences(3, 0, 0, 0, false, false, true, false, false, false);
    ASSERT_EQ(corner.count, 3);
    EXPECT_EQ(corner.parts[0].sizeX, 26);
    EXPECT_EQ(corner.parts[1].boundsY, 4);
    EXPECT_EQ(corner.parts[2].boundsX, 0);
    EXPECT_EQ(corner.parts[2].boundsY, 27);
}

TEST(WorldPathRulesTest, SlopedFencePairsPreserveDirectionBoundsAndVisibility)
{
    constexpr int images[4] = { 8, 7, 9, 6 };
    for (int d = 0; d < 4; d++)
        for (int r = 0; r < 4; r++)
            for (bool queue : { false, true })
            {
                const auto p = worldPathFences(0, 15, d, r, true, queue, false, false, false, false);
                ASSERT_EQ(p.count, 2);
                const auto direction = (d + r) % 4;
                for (int i = 0; i < 2; i++)
                {
                    EXPECT_EQ(p.parts[i].imageOffset, images[direction] + (queue ? 14 : 0));
                    EXPECT_EQ(p.parts[i].boundsZ, 2);
                    EXPECT_EQ(p.parts[i].sizeZ, 23);
                    EXPECT_EQ(p.parts[i].sizeX, (direction & 1) ? 1 : 32);
                    EXPECT_EQ(p.parts[i].sizeY, (direction & 1) ? 32 : 1);
                }
                EXPECT_EQ(worldPathFences(0, 0, d, r, true, queue, false, true, false, false).count, queue ? 2 : 0);
            }
}

TEST(WorldPathRulesTest, BinStatusUsesInverseRotationAndBrokenPrecedesFull)
{
    for (int status = 0; status < 256; status++)
        for (int rotation = 0; rotation < 4; rotation++)
            for (int edge = 0; edge < 4; edge++)
            {
                const auto mask = OpenRCT2::Numerics::ror8(static_cast<uint8_t>(3 << (2 * edge)), 2 * rotation);
                bool full = (status & mask) == 0;
                EXPECT_EQ(worldPathBinFull(status, edge, rotation), full);
                auto p = worldPathAdditions(0, rotation, false, 1, false, status, false, 0);
                ASSERT_EQ(p.count, 4);
                EXPECT_EQ(p.parts[edge].imageOffset, edge + 1 + (full ? 8 : 0));
                p = worldPathAdditions(0, rotation, false, 1, true, status, false, 0);
                EXPECT_EQ(p.parts[edge].imageOffset, edge + 5);
            }
}

TEST(WorldPathRulesTest, AdditionsPreserveOpenEdgesHeightZoomAndIssueVisibility)
{
    static_assert(static_cast<int>(OpenRCT2::PathAdditionDrawType::light) == 0);
    static_assert(static_cast<int>(OpenRCT2::PathAdditionDrawType::bin) == 1);
    static_assert(static_cast<int>(OpenRCT2::PathAdditionDrawType::bench) == 2);
    static_assert(static_cast<int>(OpenRCT2::PathAdditionDrawType::jumpingFountain) == 3);
    for (int type = 0; type < 4; type++)
    {
        auto p = worldPathAdditions(0, 0, true, type, false, 255, false, 0);
        ASSERT_EQ(p.count, 4);
        EXPECT_EQ(p.parts[0].z, type < 2 ? 8 : 0);
        EXPECT_EQ(p.parts[0].boundsZ, type < 2 ? 10 : 2);
        EXPECT_EQ(worldPathAdditions(0, 0, false, type, false, 255, false, 2).count, 0);
        EXPECT_EQ(worldPathAdditions(0, 0, false, type, false, 255, false, 1).count, type == 3 ? 0 : 4);
        EXPECT_EQ(worldPathAdditions(0, 0, false, type, false, 255, true, 0).count, 0);
        EXPECT_EQ(worldPathAdditions(0, 0, false, type, true, 255, true, 0).count, 4);
        EXPECT_EQ(worldPathAdditions(15, 0, false, type, false, 255, false, 0).count, type == 3 ? 4 : 0);
    }
    for (int rotation = 0; rotation < 4; rotation++)
    {
        const auto p = worldPathAdditions(14, rotation, false, 2, false, 255, false, 0);
        ASSERT_EQ(p.count, 1);
        EXPECT_EQ(p.parts[0].imageOffset, rotation + 1);
    }
}
