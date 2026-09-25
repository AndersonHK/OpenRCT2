// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <algorithm>
#include <array>
#include <cassert>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2/SpriteIds.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/paint/Boundbox.h>
#include <openrct2/paint/Paint.SessionFlags.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/paint/support/WoodenSupports.h>
#include <openrct2/ride/TrackData.h>
#include <openrct2/ride/ted/TrackElementDescriptor.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/tile_element/Slope.h>
#include <vector>
namespace WoodenRules
{
#include "../../data/shaders/vulkan/world_wooden_support_rules.glsl"
}
using namespace WoodenRules;
using WoodenPart = std::array<int, 11>;
namespace WoodenOracle
{
    static std::vector<WoodenPart> parts;
    static PaintStruct orphan;
    static PaintStruct* CaptureParent(PaintSession&, ImageId image, const CoordsXYZ& offset, const BoundBoxXYZ& bounds)
    {
        parts.push_back({ int(image.GetIndex()), offset.x, offset.y, offset.z, bounds.offset.x, bounds.offset.y,
                          bounds.offset.z, bounds.length.x, bounds.length.y, bounds.length.z, 0 });
        return nullptr;
    }
    static PaintStruct* CaptureParent(PaintSession& session, ImageId image, const CoordsXYZ& offset, const CoordsXYZ& size)
    {
        return CaptureParent(session, image, offset, BoundBoxXYZ{ offset, size });
    }
    static PaintStruct* CaptureOrphan(PaintSession& session, ImageId image, const CoordsXYZ& offset, const BoundBoxXYZ& bounds)
    {
        CaptureParent(session, image, offset, bounds);
        parts.back()[10] = 1;
        return &orphan;
    }
// All original branches/tables execute unchanged; replace only the sprite sink.
// Public function aliases avoid ADL finding the real global declarations when
// the original rotated/sequence wrappers call the corresponding A/B helper.
#define PaintAddImageAsParent CaptureParent
#define PaintAddImageAsOrphan CaptureOrphan
#define WoodenASupportsPaintSetup OriginalA
#define WoodenASupportsPaintSetupRotated OriginalARotated
#define WoodenBSupportsPaintSetup OriginalB
#define WoodenBSupportsPaintSetupRotated OriginalBRotated
#define PathBoxSupportsPaintSetup OriginalPathBox
#define DrawSupportForSequenceA OriginalSequenceA
#define DrawSupportForSequenceB OriginalSequenceB
#include "../../src/openrct2/paint/support/WoodenSupports.cpp"
#undef DrawSupportForSequenceB
#undef DrawSupportForSequenceA
#undef PathBoxSupportsPaintSetup
#undef WoodenBSupportsPaintSetupRotated
#undef WoodenBSupportsPaintSetup
#undef WoodenASupportsPaintSetupRotated
#undef WoodenASupportsPaintSetup
#undef PaintAddImageAsOrphan
#undef PaintAddImageAsParent
} // namespace WoodenOracle
namespace
{
    void CompareWooden(
        PaintSession& session, const WorldSupportState& state, int type, int subtype, int direction, int height, int transition,
        bool typeB, bool rotate, bool prepend)
    {
        session.Flags = state.passedSurface ? OpenRCT2::PaintSessionFlags::PassedSurface : 0;
        session.ViewFlags = 0;
        session.Support.height = static_cast<uint16_t>(state.generalHeight);
        session.Support.slope = static_cast<uint8_t>(state.generalSlope);
        session.WaterHeight = static_cast<uint16_t>(state.waterHeight);
        PaintStruct owner{};
        session.WoodenSupportsPrependTo = prepend ? &owner : nullptr;
        WoodenOracle::parts.clear();
        bool returned;
        auto t = static_cast<WoodenSupportType>(type);
        auto sub = static_cast<WoodenSupportSubType>(subtype);
        auto trans = static_cast<WoodenSupportTransitionType>(transition);
        auto dir = static_cast<uint8_t>(direction);
        if (rotate)
            returned = typeB ? WoodenOracle::OriginalBRotated(session, t, sub, dir, height, ImageId(0), trans)
                             : WoodenOracle::OriginalARotated(session, t, sub, dir, height, ImageId(0), trans);
        else
            returned = typeB ? WoodenOracle::OriginalB(session, t, sub, height, ImageId(0), trans, dir)
                             : WoodenOracle::OriginalA(session, t, sub, height, ImageId(0), trans, dir);
        auto cursor = worldWoodenBegin(state, type, subtype, direction, height, transition, typeB, rotate, prepend);
        std::vector<WoodenPart> actual;
        for (int guard = 0; guard < 8192; guard++)
        {
            auto p = worldWoodenNext(cursor);
            if (p.imageOffset < 0)
                break;
            actual.push_back(
                { p.imageOffset, p.x, p.y, p.z, p.boundsX, p.boundsY, p.boundsZ, p.sizeX, p.sizeY, p.sizeZ, p.orphan });
        }
        ASSERT_EQ(cursor.phase, -1);
        ASSERT_EQ(actual, WoodenOracle::parts)
            << "type=" << type << " subtype=" << subtype << " direction=" << direction << " height=" << height
            << " slope=" << state.generalSlope << " transition=" << transition << " B=" << typeB << " rotate=" << rotate
            << " prepend=" << prepend;
        ASSERT_EQ(cursor.hasSupports, returned);
        ASSERT_EQ(session.Support.height, state.generalHeight);
        ASSERT_EQ(session.Support.slope, state.generalSlope);
        for (const auto& p : actual)
            ASSERT_TRUE(std::binary_search(std::begin(worldWoodenAssets), std::end(worldWoodenAssets), p[0]));
    }
} // namespace
TEST(WorldWoodenSupportRulesTest, AllTypesSubtypesRotationsSlopesAndTransitionsMatchOriginalPainter)
{
    static_assert(worldWoodenTransitions[6 * 4] == SPR_TRACKS_SUPPORT_WOODEN_TRUSS_UP_25_EVEN);
    static_assert(worldWoodenTransitions[13 * 4] == SPR_TRACKS_SUPPORT_WOODEN_TRUSS_LONG_FLAT_TO_STEEP);
    static_assert(worldWoodenTransitions[17 * 4] == SPR_TRACKS_SUPPORT_WOODEN_TRUSS_LONG_STEEP_TO_FLAT);
    static_assert(worldWoodenTransitions[(21 + 6) * 4] == SPR_TRACKS_SUPPORT_WOODEN_MINE_UP_25_EVEN);
    static_assert(worldWoodenTransitions[(21 + 13) * 4] == SPR_TRACKS_SUPPORT_WOODEN_MINE_LONG_FLAT_TO_STEEP);
    static_assert(worldWoodenTransitions[(21 + 17) * 4] == SPR_TRACKS_SUPPORT_WOODEN_MINE_LONG_STEEP_TO_FLAT);
    auto session = std::make_unique<PaintSession>();
    for (int type = 0; type < 2; type++)
        for (int sub = 0; sub < 6; sub++)
            for (int direction = 0; direction < 4; direction++)
                for (bool rotate : { false, true })
                    for (bool typeB : { false, true })
                        for (int slope = 0; slope < 33; slope++)
                            for (int transition = 0; transition < 22; transition++)
                            {
                                WorldSupportState state;
                                worldSupportInitialise(state);
                                state.passedSurface = 1;
                                state.generalHeight = 64;
                                state.generalSlope = slope;
                                state.waterHeight = (direction & 1) ? 80 : 65535;
                                CompareWooden(
                                    *session, state, type, sub, direction, 176 + (transition % 3) * 16,
                                    transition == 21 ? 255 : transition, typeB, rotate, (transition & 1) != 0);
                            }
}
TEST(WorldWoodenSupportRulesTest, ShortFootingsZeroHeightAndAboveSceneryRespectABDifferences)
{
    auto session = std::make_unique<PaintSession>();
    for (int slope : { 0, 1, 23, 32 })
        for (int height : { 63, 64, 65, 79, 80, 95, 96, 111, 112 })
            for (bool typeB : { false, true })
            {
                WorldSupportState state;
                worldSupportInitialise(state);
                state.passedSurface = 1;
                state.generalHeight = 64;
                state.generalSlope = slope;
                CompareWooden(*session, state, 0, 0, 0, height, 255, typeB, false, false);
                CompareWooden(*session, state, 1, 1, 2, height, 5, typeB, true, true);
            }
}
TEST(WorldWoodenSupportRulesTest, WaterSplitsFullColumnsAndTallSupportsAreNotTruncated)
{
    auto session = std::make_unique<PaintSession>();
    WorldSupportState state;
    worldSupportInitialise(state);
    state.passedSurface = 1;
    state.generalHeight = 64;
    state.generalSlope = 0;
    for (int water : { 80, 96, 112, 65535 })
    {
        state.waterHeight = water;
        CompareWooden(*session, state, 0, 0, 0, 192, 255, false, false, false);
    }
    state.waterHeight = 65535;
    CompareWooden(*session, state, 1, 4, 3, 16464, 255, false, true, false);
    ASSERT_GT(WoodenOracle::parts.size(), 512u);
}
TEST(WorldWoodenSupportRulesTest, UnavailableCornerTransitionCanReturnFalseAfterEmittingBase)
{
    auto session = std::make_unique<PaintSession>();
    WorldSupportState state;
    worldSupportInitialise(state);
    CompareWooden(*session, state, 0, 0, 0, 160, 5, false, false, false);
    ASSERT_TRUE(WoodenOracle::parts.empty());
    worldSupportSeedTerrain(state, 64, 0, 65535);
    CompareWooden(*session, state, 0, 2, 0, 160, 5, false, false, true);
    ASSERT_FALSE(WoodenOracle::parts.empty());
    auto cursor = worldWoodenBegin(state, 0, 2, 0, 160, 5, false, false, true);
    ASSERT_TRUE(cursor.accepted);
    while (cursor.phase >= 0)
        worldWoodenNext(cursor);
    EXPECT_FALSE(cursor.hasSupports);
}

TEST(WorldWoodenSupportRulesTest, EntranceColumnsMatchOriginalAndStayInsideTheirIndependentArtDomain)
{
    auto session = std::make_unique<PaintSession>();
    std::vector<int> art;
    for (int i = 0; i < worldEntranceSupportAssetCount(); ++i)
        art.push_back(worldEntranceSupportAssetImage(i));
    for (int direction = 0; direction < 4; ++direction)
        for (int slope = 0; slope <= 32; ++slope)
            for (int height : { 64, 80, 96, 176, 4080 })
            {
                WorldSupportState state;
                worldSupportInitialise(state);
                worldSupportSeedTerrain(state, 64, slope, 96);
                CompareWooden(*session, state, 0, 0, direction, height, 255, false, true, false);
                auto cursor = worldEntranceWoodenBegin(state, direction, height);
                std::vector<WoodenPart> actual;
                while (cursor.phase >= 0)
                {
                    const auto p = worldWoodenNext(cursor);
                    if (p.imageOffset < 0)
                        continue;
                    EXPECT_TRUE(std::binary_search(art.begin(), art.end(), p.imageOffset));
                    EXPECT_EQ(p.orphan, 0);
                    actual.push_back(
                        { p.imageOffset, p.x, p.y, p.z, p.boundsX, p.boundsY, p.boundsZ, p.sizeX, p.sizeY, p.sizeZ, p.orphan });
                }
                EXPECT_EQ(actual, WoodenOracle::parts);
            }
}
