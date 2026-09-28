// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <array>
#include <gtest/gtest.h>
#include <openrct2/core/Numerics.hpp>
#include <openrct2/object/PathAdditionEntry.h>
#include <openrct2/world/tile_element/Slope.h>

namespace PathRules
{
#include "../../data/shaders/vulkan/world_path_order.glsl"
#include "../../data/shaders/vulkan/world_path_support_rules.glsl"
} // namespace PathRules
using namespace PathRules;

TEST(WorldPathRulesTest, ForegroundEdgeKeepsGuestsBehindRailAndOwnFixturesInFront)
{
    // A flat path owns one near contact, independent of the art's far-end
    // raster origin. Edge fixtures must not be cut by their own railing.
    for (int rotation = 0; rotation < 4; ++rotation)
        for (int edges : { 5, 10 })
        {
            const auto parts = worldPathFences(edges, 0, 0, rotation, false, true, true, false, false, false);
            ASSERT_EQ(parts.count, 2);
            int foreground = 0;
            for (int index = 0; index < parts.count; ++index)
            {
                const auto part = parts.parts[index];
                if (part.boundsX < 27 && part.boundsY < 27)
                    continue;
                ++foreground;
                const int contact = worldForegroundTileContact(80);
                EXPECT_GT(contact, 80 + 28 + 24);         // Near-side walking guest.
                EXPECT_LT(contact, 80 + 32 + 2 + 2 + 30); // Next tile's booth front.
                EXPECT_LT(part.x + part.y + part.z, 16 + 16);
                EXPECT_EQ(part.z, 0); // Original art remains at its unchanged offset.
            }
            EXPECT_EQ(foreground, 1);
            for (int type : { 0, 1, 2 })
                for (bool broken : { false, true })
                    for (int status : { 0, 255 })
                    {
                        const auto additions = worldPathAdditions(edges, rotation, false, type, broken, status, false, 0);
                        ASSERT_EQ(additions.count, 2);
                        int frontFixtures = 0;
                        for (int i = 0; i < additions.count; ++i)
                        {
                            const auto p = additions.parts[i];
                            const bool nearEdge = p.x > 16 || p.y > 16;
                            EXPECT_EQ(worldPathAdditionHasFrontContact(type, p.imageOffset), nearEdge);
                            frontFixtures += nearEdge;
                        }
                        EXPECT_EQ(frontFixtures, 1);
                    }
        }
    EXPECT_GT(WORLD_FOREGROUND_FIXTURE_LAYER, WORLD_FOREGROUND_RAIL_LAYER);
    EXPECT_LT(WORLD_FOREGROUND_FIXTURE_LAYER, 16); // Local roles cannot cross one scalar anchor.
    for (int image = 1; image <= 4; ++image)
        EXPECT_FALSE(worldPathAdditionHasFrontContact(3, image)); // Fountain effects have their own ownership.
}

TEST(WorldPathRulesTest, OwnDeckOccludesPoleFootingsAndSlopedSupportCapsWithoutMovingRasterAnchors)
{
    for (int edge = 0; edge < 4; edge++)
        for (bool sloped : { false, true })
        {
            auto cursor = worldPathPoleBegin(64, 0, 80, edge, sloped, true, true);
            int count = 0;
            while (cursor.phase >= 0)
            {
                const auto part = worldPathPoleNext(cursor);
                if (part.imageOffset < 0)
                    continue;
                EXPECT_EQ(part.x, worldPathPoleX(edge));
                EXPECT_EQ(part.y, worldPathPoleY(edge));
                const int authoredDepth = part.x + part.y + part.z;
                EXPECT_LT(worldPathSupportDepth(part, 80), 80);
                EXPECT_EQ(worldPathSupportDepth(part, 80), authoredDepth < 80 ? authoredDepth : 79);
                count++;
            }
            EXPECT_EQ(count, sloped ? 3 : 2); // footing, short shaft, optional top cap
        }
    // An actually lower support retains its own depth; this is not a global
    // support-family rank. The caller still emits the original image and XYZ.
    auto lower = worldPathPart(22, 0, 0, 64, 0, 0, 64, 32, 32, 28);
    EXPECT_EQ(worldPathSupportDepth(lower, 128), 64);
    auto box = worldPathBoxBegin(64, 0, 96, 0, 0, true, 0, true);
    EXPECT_EQ(worldPathSupportDepth(worldPathBoxNext(box), 96), 64);
    const auto transition = worldPathBoxNext(box);
    EXPECT_EQ(transition.z, 96);
    EXPECT_EQ(transition.imageOffset, 55);
    EXPECT_EQ(worldPathSupportDepth(transition, 96), 95);
}

TEST(WorldPathRulesTest, SupportSlopeTablesMatchOriginalWoodenAndMetalArt)
{
    constexpr std::array<int, 32> wood = { 0, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 0,
                                           0, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0, 17, 0,  16, 15, 0 };
    constexpr std::array<int, 32> metal = { 0, 1, 2, 3, 4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 0,
                                            0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0,  16, 0,  17, 18, 0 };
    for (int slope = 0; slope < 32; slope++)
    {
        EXPECT_EQ(worldPathWoodSlopeOffset(slope), wood[slope]);
        EXPECT_EQ(worldPathPoleSlopeOffset(slope), metal[slope]);
    }
}

TEST(WorldPathRulesTest, BoxSupportsSplitAtWaterAndRetainShortLastSegmentBounds)
{
    // Ground 64, water 80, path 128: split the first full section at water.
    auto cursor = worldPathBoxBegin(64, 0, 128, 80, 1, false, 0, true);
    constexpr std::array<int, 3> images = { 47, 47, 46 };
    constexpr std::array<int, 3> heights = { 64, 80, 96 };
    constexpr std::array<int, 3> boundsHeights = { 12, 12, 23 };
    for (size_t i = 0; i < images.size(); i++)
    {
        auto part = worldPathBoxNext(cursor);
        EXPECT_EQ(part.imageOffset, images[i]);
        EXPECT_EQ(part.z, heights[i]);
        EXPECT_EQ(part.sizeZ, boundsHeights[i]);
    }
    EXPECT_EQ(worldPathBoxNext(cursor).imageOffset, -1);
    cursor = worldPathBoxBegin(64, 32, 64, 0, 0, false, 0, true);
    auto cap = worldPathBoxNext(cursor);
    EXPECT_EQ(cap.imageOffset, 48);
    EXPECT_EQ(cap.z, 62);
    EXPECT_EQ(cap.sizeZ, 0);
    EXPECT_EQ(worldPathBoxNext(cursor).imageOffset, -1);
}

TEST(WorldPathRulesTest, BoxSupportsPreserveBothSteepFootingsAndSlopedPathTransition)
{
    for (int direction = 0; direction < 4; direction++)
    {
        auto cursor = worldPathBoxBegin(64, 23, 112, 0, 0, true, direction, true);
        constexpr std::array<int, 3> expectedImages = { 14, 18, 23 };
        for (int index = 0; index < 3; index++)
        {
            auto part = worldPathBoxNext(cursor);
            EXPECT_EQ(part.imageOffset, expectedImages[index]);
            EXPECT_EQ(part.z, 64 + index * 16);
            EXPECT_EQ(part.sizeZ, index < 2 ? 11 : 7);
        }
        const auto transition = worldPathBoxNext(cursor);
        EXPECT_EQ(transition.imageOffset, 55 + direction);
        EXPECT_EQ(transition.z, 112);
        EXPECT_EQ(transition.sizeX, 1);
        EXPECT_EQ(transition.sizeZ, 4);
        EXPECT_EQ(worldPathBoxNext(cursor).imageOffset, -1);
    }
    auto buried = worldPathBoxBegin(64, 23, 80, 0, 0, true, 0, true);
    EXPECT_EQ(worldPathBoxNext(buried).imageOffset, -1);
    auto beforeSurface = worldPathBoxBegin(64, 0, 128, 0, 0, false, 0, false);
    EXPECT_EQ(worldPathBoxNext(beforeSurface).imageOffset, -1);
}

TEST(WorldPathRulesTest, PoleSupportsPreserveBaseAlignmentFourthJointAndSlopedExtension)
{
    // The initial alignment segment is not one of the four repeated sections.
    auto cursor = worldPathPoleBegin(64, 30, 160, 2, true, true, true);
    constexpr std::array<int, 8> images = { 55, 29, 35, 35, 35, 36, 35, 27 };
    constexpr std::array<int, 8> heights = { 64, 70, 80, 96, 112, 128, 144, 160 };
    for (size_t i = 0; i < images.size(); i++)
    {
        auto part = worldPathPoleNext(cursor);
        EXPECT_EQ(part.imageOffset, images[i]);
        EXPECT_EQ(part.z, heights[i]);
        EXPECT_EQ(part.x, 28);
        EXPECT_EQ(part.y, 16);
        EXPECT_EQ(part.sizeZ, i == 0 ? 5 : (i == 1 ? 9 : (i == 7 ? 0 : 15)));
    }
    EXPECT_EQ(worldPathPoleNext(cursor).imageOffset, -1);
    auto blocked = worldPathPoleBegin(65535, 0, 160, 0, true, true, true);
    EXPECT_EQ(worldPathPoleNext(blocked).imageOffset, -1);
}

TEST(WorldPathRulesTest, PathSupportConsumptionDistinguishesQueueWideAndConnectedEdges)
{
    EXPECT_EQ(worldPathBlockedSupportSlots(0, 0, true, false), 511);
    EXPECT_EQ(worldPathBlockedSupportSlots(0, 0, false, true), 511);
    EXPECT_EQ(worldPathBlockedSupportSlots(255, 15, false, true), 480);
    constexpr std::array<int, 4> places = { 6, 8, 7, 5 };
    for (int edge = 0; edge < 4; edge++)
    {
        EXPECT_EQ(worldPathPolePlace(edge), places[edge]);
        EXPECT_EQ(worldPathBlockedSupportSlots(1 << edge, 1 << edge, false, false), 16 | (1 << places[edge]));
    }
}

TEST(WorldPathRulesTest, TallPathSupportsStreamEverySectionWithoutFixedComponentTruncation)
{
    auto pole = worldPathPoleBegin(0, 0, 4096, 0, false, false, true);
    int sections = 0;
    int joints = 0;
    while (pole.phase >= 0)
    {
        const auto part = worldPathPoleNext(pole);
        if (part.imageOffset < 0)
            break;
        ASSERT_LT(sections, 257);
        EXPECT_EQ(part.z, sections * 16);
        EXPECT_EQ(part.sizeZ, 15);
        joints += part.imageOffset == 36;
        sections++;
    }
    EXPECT_EQ(sections, 256);
    EXPECT_EQ(joints, 63);
    auto box = worldPathBoxBegin(0, 0, 4096, 0, 0, false, 0, true);
    sections = 0;
    while (box.phase >= 0)
    {
        const auto part = worldPathBoxNext(box);
        if (part.imageOffset < 0)
            break;
        ASSERT_LT(sections, 129);
        EXPECT_EQ(part.z, sections * 32);
        EXPECT_EQ(part.imageOffset, 22);
        EXPECT_EQ(part.sizeZ, sections == 127 ? 23 : 28);
        sections++;
    }
    EXPECT_EQ(sections, 128);
}

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
