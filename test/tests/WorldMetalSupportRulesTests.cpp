// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2/core/EnumUtils.hpp>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/SmallSceneryEntry.h>
#include <openrct2/paint/Paint.SessionFlags.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/paint/support/MetalSupports.h>
#include <openrct2/paint/tile_element/Paint.TileElement.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/tile_element/Slope.h>
#include <vector>

namespace MetalRules
{
#include "../../data/shaders/vulkan/world_metal_support_rules.glsl"
#include "../../data/shaders/vulkan/world_object_support_state.glsl"
} // namespace MetalRules
using namespace MetalRules;
using Part = std::array<int, 10>;

// Compile the actual original painter in an isolated oracle namespace. Only its
// output sink is replaced: all original tables, branches, rotations, integer
// conversions and mutable support-state rules execute unchanged. No G1/device
// setup, bitmap rasterization, duplicated expected implementation, or CPU world
// rendering path is introduced by this test.
namespace MetalOracle
{
    static std::vector<Part> parts;
    static PaintStruct* CaptureParent(PaintSession&, ImageId image, const CoordsXYZ& offset, const BoundBoxXYZ& bounds)
    {
        parts.push_back({ static_cast<int>(image.GetIndex()), offset.x, offset.y, offset.z, bounds.offset.x, bounds.offset.y,
                          bounds.offset.z, bounds.length.x, bounds.length.y, bounds.length.z });
        return nullptr;
    }
    static PaintStruct* CaptureParent(PaintSession& session, ImageId image, const CoordsXYZ& offset, const CoordsXYZ& size)
    {
        return CaptureParent(session, image, offset, BoundBoxXYZ{ offset, size });
    }
#define PaintAddImageAsParent CaptureParent
#include "../../src/openrct2/paint/support/MetalSupports.cpp"
#undef PaintAddImageAsParent
} // namespace MetalOracle

namespace
{
    Part AsPart(const WorldMetalPart& p)
    {
        return { p.imageOffset, p.x, p.y, p.z, p.boundsX, p.boundsY, p.boundsZ, p.sizeX, p.sizeY, p.sizeZ };
    }
    void CopyState(PaintSession& session, const WorldSupportState& state, int rotation)
    {
        session.CurrentRotation = static_cast<uint8_t>(rotation);
        session.Flags = state.passedSurface ? OpenRCT2::PaintSessionFlags::PassedSurface : 0;
        session.ViewFlags = 0;
        session.Support.height = static_cast<uint16_t>(state.generalHeight);
        session.Support.slope = static_cast<uint8_t>(state.generalSlope);
        for (int i = 0; i < 9; i++)
        {
            session.SupportSegments[i].height = static_cast<uint16_t>(state.heights[i]);
            session.SupportSegments[i].slope = static_cast<uint8_t>(state.slopes[i]);
        }
    }
    void ExpectState(const PaintSession& session, const WorldSupportState& state)
    {
        for (int i = 0; i < 9; i++)
        {
            ASSERT_EQ(session.SupportSegments[i].height, state.heights[i]) << "slot " << i;
            ASSERT_EQ(session.SupportSegments[i].slope, state.slopes[i]) << "slot " << i;
        }
        ASSERT_EQ(session.Support.height, state.generalHeight);
        ASSERT_EQ(session.Support.slope, state.generalSlope);
    }
    void CompareCase(
        PaintSession& session, WorldSupportState state, int type, int place, int helperRotation, int camera, int height,
        int extra, bool typeB)
    {
        CopyState(session, state, camera);
        MetalOracle::parts.clear();
        const auto support = static_cast<MetalSupportType>(type);
        const auto placement = static_cast<MetalSupportPlace>(place);
        bool accepted;
        if (helperRotation == 4)
            accepted = typeB ? MetalOracle::MetalBSupportsPaintSetup(session, support, placement, extra, height, ImageId(0))
                             : MetalOracle::MetalASupportsPaintSetup(session, support, placement, extra, height, ImageId(0));
        else
            accepted = typeB
                ? MetalOracle::MetalBSupportsPaintSetupRotated(
                      session, support, placement, static_cast<uint8_t>(helperRotation), extra, height, ImageId(0))
                : MetalOracle::MetalASupportsPaintSetupRotated(
                      session, support, placement, static_cast<uint8_t>(helperRotation), extra, height, ImageId(0));
        auto cursor = worldMetalBegin(state, type, place, helperRotation, camera, height, extra, typeB, false);
        ASSERT_EQ(cursor.accepted, accepted);
        std::vector<Part> actual;
        for (int guard = 0; guard < 8192; guard++)
        {
            const auto p = worldMetalNext(cursor);
            if (p.imageOffset < 0)
                break;
            actual.push_back(AsPart(p));
        }
        ASSERT_EQ(cursor.phase, -1) << "nonterminating support cursor";
        ASSERT_EQ(actual, MetalOracle::parts)
            << "type=" << type << " place=" << place << " helper=" << helperRotation << " camera=" << camera
            << " height=" << height << " extra=" << extra << " B=" << typeB;
        ExpectState(session, state);
        for (const auto& p : actual)
        {
            ASSERT_TRUE(std::binary_search(std::begin(worldMetalAssets), std::end(worldMetalAssets), p[0]))
                << "unadmitted image " << p[0];
        }
    }
} // namespace

TEST(WorldMetalSupportRulesTest, AllTypesPlacementsRotationsSlopesAndExtraSignsMatchOriginalPainter)
{
    auto session = std::make_unique<PaintSession>();
    for (int type = 0; type < 8; type++)
        for (int place = 0; place < 9; place++)
            for (int helper = 0; helper < 5; helper++)
                for (int slope = 0; slope < 32; slope++)
                    for (int extra : { -17, 0, 23 })
                        for (bool typeB : { false, true })
                        {
                            WorldSupportState state;
                            worldSupportInitialise(state);
                            worldSupportSeedTerrain(state, 64, slope, 80);
                            // Vary camera independently from helper rotation across the matrix.
                            CompareCase(*session, state, type, place, helper, (slope + place) & 3, 208, extra, typeB);
                        }
}

TEST(WorldMetalSupportRulesTest, EveryRepositionAttemptRejectionAndBlockedSegmentMatchesOriginalPainter)
{
    auto session = std::make_unique<PaintSession>();
    for (int type = 0; type < 8; type++)
        for (int place = 0; place < 9; place++)
            for (int camera = 0; camera < 4; camera++)
                for (int attempt = 0; attempt < 5; attempt++)
                    for (bool typeB : { false, true })
                    {
                        WorldSupportState state;
                        worldSupportInitialise(state);
                        state.passedSurface = 1;
                        for (int i = 0; i < 9; i++)
                        {
                            state.heights[i] = 65535;
                            state.slopes[i] = 32;
                        }
                        if (attempt < 4)
                        {
                            const auto destination = MetalOracle::kMetalSupportSegmentOffsets[attempt][place]
                                                                                             [static_cast<uint8_t>(camera)]
                                                                                                 .place;
                            state.heights[static_cast<int>(destination)] = 40;
                        }
                        CompareCase(*session, state, type, place, 4, camera, 112, -33, typeB);
                    }
}

TEST(WorldMetalSupportRulesTest, SideBySideRotatesGraphicsWithoutRotatingExplicitSlotsTwice)
{
    auto session = std::make_unique<PaintSession>();
    for (int type = 0; type < 8; type++)
        for (int direction = 0; direction < 4; direction++)
        {
            WorldSupportState state;
            worldSupportInitialise(state);
            worldSupportSeedTerrain(state, 64, 23, 80);
            CopyState(*session, state, 3);
            MetalOracle::parts.clear();
            MetalOracle::DrawSupportsSideBySide(
                *session, static_cast<uint8_t>(direction), 160, ImageId(0), static_cast<MetalSupportType>(type), 17);
            std::vector<Part> actual;
            for (int place : { (direction & 1) ? 6 : 5, (direction & 1) ? 7 : 8 })
            {
                auto cursor = worldMetalBegin(state, type, place, direction, 3, 160, 17, false, true);
                for (int guard = 0; guard < 8192; guard++)
                {
                    const auto p = worldMetalNext(cursor);
                    if (p.imageOffset < 0)
                        break;
                    actual.push_back(AsPart(p));
                }
                ASSERT_EQ(cursor.phase, -1);
            }
            EXPECT_EQ(actual, MetalOracle::parts);
            ExpectState(*session, state);
        }
}

TEST(WorldMetalSupportRulesTest, TallSupportsStreamWithoutPartTruncationAndBeforeSurfaceIsNoOp)
{
    auto session = std::make_unique<PaintSession>();
    WorldSupportState state;
    worldSupportInitialise(state);
    CompareCase(*session, state, 0, 4, 4, 0, 4096, 1024, false);
    worldSupportSeedTerrain(state, 16, 0, 65535);
    CompareCase(*session, state, 0, 4, 4, 0, 8192, -1025, false);
    ASSERT_GT(MetalOracle::parts.size(), 512u);
}

TEST(WorldMetalSupportRulesTest, SegmentSentinelRotationAndGeneralRaiseMatchOriginalUtilities)
{
    auto session = std::make_unique<PaintSession>();
    for (int mask = 0; mask < 512; mask++)
        for (int rotation = 0; rotation < 4; rotation++)
        {
            EXPECT_EQ(worldSupportRotatedMask(mask, rotation), PaintUtilRotateSegments(mask, static_cast<uint8_t>(rotation)));
            for (int height : { 96, 65535 })
            {
                WorldSupportState state;
                worldSupportSeedTerrain(state, 64, 23, 80);
                CopyState(*session, state, rotation);
                PaintUtilSetSegmentSupportHeight(*session, mask, static_cast<uint16_t>(height), 17);
                worldSupportSetSegments(state, mask, height, 17);
                PaintUtilSetGeneralSupportHeight(*session, 112);
                worldSupportRaiseGeneralState(state, 112);
                PaintUtilSetGeneralSupportHeight(*session, 96);
                worldSupportRaiseGeneralState(state, 96);
                ExpectState(*session, state);
            }
        }
}

TEST(WorldMetalSupportRulesTest, TerrainSeedsPreserveAuthoredIntermediateHeightsAndFlatInvalidCases)
{
    WorldSupportState state;
    worldSupportInitialise(state);
    EXPECT_EQ(state.passedSurface, 0);
    EXPECT_EQ(state.generalHeight, 65535);
    EXPECT_EQ(state.waterHeight, 65535);
    worldSupportSeedTerrain(state, 64, 23, 80);
    constexpr std::array<int, 9> offsets = { 16, 28, 4, 16, 16, 22, 10, 22, 10 };
    for (int i = 0; i < 9; i++)
    {
        EXPECT_EQ(state.heights[i], 64 + offsets[i]);
        EXPECT_EQ(state.slopes[i], 23);
    }
    EXPECT_EQ(state.generalHeight, 64);
    EXPECT_EQ(state.generalSlope, 23);
    EXPECT_EQ(state.waterHeight, 80);
    for (int shape : { 0, 15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 26, 28, 31 })
    {
        worldSupportSeedTerrain(state, 96, shape, 65535);
        for (int i = 0; i < 9; i++)
        {
            EXPECT_EQ(state.heights[i], 96);
            EXPECT_EQ(state.slopes[i], 0);
        }
    }
}

TEST(WorldMetalSupportRulesTest, SceneryAndEntrancePostStatePreservesAuthoredFootprintsAndClearance)
{
    static_assert(static_cast<int>(OpenRCT2::SmallSceneryFlag::allowSupportsAbove) == 23);
    static_assert(static_cast<int>(OpenRCT2::SmallSceneryFlag::flag27) == 27);
    WorldSupportState state;
    worldSupportSeedTerrain(state, 64, 23, 80);
    worldSupportSmallScenery(state, 80, 17, 1 | 2 | (1 << 23), 0, 0);
    EXPECT_EQ(state.generalHeight, 104);
    for (int i = 0; i < 9; i++)
    {
        EXPECT_EQ(state.heights[i], 97);
        EXPECT_EQ(state.slopes[i], 32);
    }

    for (int quadrant = 0; quadrant < 4; quadrant++)
        for (int rotation = 0; rotation < 4; rotation++)
        {
            worldSupportSeedTerrain(state, 64, 23, 80);
            const auto original = state;
            worldSupportSmallScenery(state, 80, 16, 2 | (1 << 23), quadrant, rotation);
            const auto mask = PaintUtilRotateSegments(131, static_cast<uint8_t>((quadrant + rotation) & 3));
            for (int i = 0; i < 9; i++)
            {
                EXPECT_EQ(state.heights[i], (mask & kSegmentOffsets[i]) ? 96 : original.heights[i]);
                EXPECT_EQ(state.slopes[i], (mask & kSegmentOffsets[i]) ? 32 : original.slopes[i]);
            }
        }
    worldSupportSeedTerrain(state, 64, 23, 80);
    worldSupportSmallScenery(state, 80, 16, 1 | 2, 0, 0);
    for (int i = 0; i < 9; i++)
    {
        EXPECT_EQ(state.heights[i], 65535);
        EXPECT_EQ(state.slopes[i], 23);
    }

    worldSupportSeedTerrain(state, 64, 23, 80);
    const auto before = state;
    worldSupportLargeScenery(state, 128, false, true);
    for (int i = 0; i < 9; i++)
        EXPECT_EQ(state.heights[i], before.heights[i]);
    EXPECT_EQ(state.generalHeight, before.generalHeight);
    worldSupportLargeScenery(state, 64, true, true);
    EXPECT_EQ(state.generalHeight, 80); // Original adds15 before rounding up16.
    for (int i = 0; i < 9; i++)
    {
        EXPECT_EQ(state.heights[i], 80);
        EXPECT_EQ(state.slopes[i], 32);
    }
    worldSupportLargeScenery(state, 96, true, false);
    EXPECT_EQ(state.generalHeight, 112);
    for (int i = 0; i < 9; i++)
    {
        EXPECT_EQ(state.heights[i], 65535);
        EXPECT_EQ(state.slopes[i], 32);
    }

    constexpr int heights[3] = { 120, 104, 144 };
    for (int type = 0; type < 3; type++)
    {
        worldSupportSeedTerrain(state, 64, 23, 80);
        worldSupportEntrance(state, 64, type);
        EXPECT_EQ(state.generalHeight, heights[type]);
        for (int i = 0; i < 9; i++)
        {
            EXPECT_EQ(state.heights[i], 65535);
            EXPECT_EQ(state.slopes[i], 23);
        }
    }
}
