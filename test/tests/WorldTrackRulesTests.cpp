// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <algorithm>
#include <bit>
#include <gtest/gtest.h>
#include <openrct2-renderer/gpu/GpuWorldTrackCatalog.h>
#include <openrct2/drawing/NativeTrackRecipes.h>
#include <openrct2/ride/TrackStyle.h>
#include <openrct2/ride/ted/TrackElemType.h>
#include <set>
#include <span>

namespace
{
    std::span<const uint32_t> RawRecipe(uint32_t style, uint32_t type, uint32_t sequence, uint32_t direction, uint32_t state)
    {
        const auto words = OpenRCT2::Drawing::GetNativeTrackRecipeWords();
        if (style >= words[2] || type >= words[3] || direction >= 4 || state >= 128)
            return {};
        const auto descriptor = words[4] + (style * words[3] + type) * 3;
        const auto sequences = words[descriptor + 1], mask = words[descriptor + 2];
        if (sequence >= sequences)
            return {};
        uint32_t variant = 0, bitPosition = 0;
        for (uint32_t bit = 0; bit < 7; ++bit)
            if (mask & (1u << bit))
                variant |= ((state >> bit) & 1u) << bitPosition++;
        const auto row = words[5] + (words[descriptor] + (variant * sequences + sequence) * 4 + direction) * 2;
        return words.subspan(words[6] + words[row] * 12, words[row + 1] * 12);
    }

    // Existing rail regressions compare drawable parts; tunnel requests are
    // independently checked below and must never become resident sprite IDs.
    std::vector<uint32_t> Recipe(uint32_t style, uint32_t type, uint32_t sequence, uint32_t direction, uint32_t state)
    {
        const auto raw = RawRecipe(style, type, sequence, direction, state);
        std::vector<uint32_t> result;
        for (size_t p = 0; p < raw.size(); p += 12)
            if (raw[p] != 0xfffffffdu)
                result.insert(result.end(), raw.begin() + p, raw.begin() + p + 12);
        return result;
    }

    std::vector<uint32_t> StyleImages(std::initializer_list<TrackStyle> styles)
    {
        // Independently enumerate the public selection contract, including all
        // aliased raw states, rather than mirroring catalog descriptor traversal.
        std::set<uint32_t> images;
        for (const auto style : styles)
            for (uint32_t type = 0; type < static_cast<uint32_t>(OpenRCT2::TrackElemType::count); ++type)
                for (uint32_t sequence = 0; sequence < 16; ++sequence)
                    for (uint32_t direction = 0; direction < 4; ++direction)
                        for (uint32_t state = 0; state < 128; ++state)
                        {
                            if (bool(state & 16u) != IsCsgLoaded())
                                continue;
                            const auto recipe = RawRecipe(static_cast<uint32_t>(style), type, sequence, direction, state);
                            for (size_t part = 0; part < recipe.size(); part += 12)
                            {
                                if (recipe[part + 10] >= 4)
                                    continue; // Water uses shared terrain banks, not image0.
                                if (recipe[part] == 0xfffffffdu)
                                {
                                    if (recipe[part + 1] == 2)
                                        for (uint32_t image = 1575; image <= 1578; ++image)
                                            images.insert(image);
                                    continue;
                                }
                                if (recipe[part] == 0xfffffffcu)
                                {
                                    const uint32_t first = recipe[part + 2] != 0 ? 23485u : 25615u;
                                    for (uint32_t image = first; image < first + 12; ++image)
                                        images.insert(image);
                                    continue;
                                }
                                if (recipe[part] != 0xfffffffeu)
                                    images.insert(recipe[part]);
                                else
                                    for (uint32_t image = SPR_STATION_PLATFORM_SW_NE; image <= SPR_STATION_BASE_BORDERLESS;
                                         ++image)
                                        images.insert(image);
                            }
                        }
        return { images.begin(), images.end() };
    }
} // namespace

TEST(WorldTrackRulesTest, PresentLoopingRidesResolveOnlyTheirCompleteSharedStyle)
{
    OpenRCT2::WorldRidePresentationMaterials source;
    source.rides.resize(3);
    source.rides[0].present = source.rides[2].present = true;
    source.rides[0].rideType = source.rides[2].rideType = OpenRCT2::RIDE_TYPE_LOOPING_ROLLER_COASTER;
    // Stale cached style fields and an absent ride must not expand residency.
    source.rides[0].regularStyle = static_cast<uint16_t>(TrackStyle::corkscrewRollerCoaster);
    source.rides[1].rideType = OpenRCT2::RIDE_TYPE_FLYING_ROLLER_COASTER;
    std::vector<uint32_t> resolved;
    const auto catalog = OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [&](uint32_t image) {
        resolved.push_back(image);
        return image + 100000;
    });
    const auto expected = StyleImages({ TrackStyle::loopingRollerCoaster });
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(resolved, expected);
    EXPECT_LT(resolved.size(), OpenRCT2::Drawing::GetNativeTrackRecipeImages().size());
    EXPECT_TRUE(std::binary_search(resolved.begin(), resolved.end(), 15004u));
    EXPECT_TRUE(std::binary_search(resolved.begin(), resolved.end(), 15009u));
    const auto& words = catalog.words;
    ASSERT_EQ(words[10], expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(words[words[9] + i * 2], expected[i]);
        EXPECT_EQ(words[words[9] + i * 2 + 1], expected[i] + 100000);
    }
    EXPECT_THROW(
        OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(
            source, [](uint32_t) -> uint32_t { throw std::runtime_error("required atlas image unavailable"); }),
        std::runtime_error);
}

TEST(WorldTrackRulesTest, MissingOrUnsupportedRidesResolveNoImages)
{
    OpenRCT2::WorldRidePresentationMaterials source;
    const auto assertEmpty = [&] {
        uint32_t calls = 0;
        const auto catalog = OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [&](uint32_t) { return ++calls; });
        EXPECT_EQ(calls, 0u);
        EXPECT_EQ(catalog.words[10], 0u);
    };
    assertEmpty();
    source.rides.resize(1);
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_LOOPING_ROLLER_COASTER;
    assertEmpty();
    source.rides[0].present = true;
    source.rides[0].rideType = UINT16_MAX;
    assertEmpty();
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_MAZE;
    assertEmpty();
}

TEST(WorldTrackRulesTest, InvertedRideResolvesBothSharedVariants)
{
    OpenRCT2::WorldRidePresentationMaterials source;
    source.rides.resize(1);
    source.rides[0].present = true;
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_FLYING_ROLLER_COASTER;
    std::vector<uint32_t> resolved;
    OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [&](uint32_t image) {
        resolved.push_back(image);
        return image;
    });
    EXPECT_EQ(resolved, StyleImages({ TrackStyle::flyingRollerCoaster, TrackStyle::flyingRollerCoasterInverted }));
}

TEST(WorldTrackRulesTest, LoopingFlatOriginalIdsChainAndRotatedBounds)
{
    // Independent original LoopingRCTrackFlat rule: numeric G1 IDs and odd
    // direction XY swap, from the CPU painter source, not generated expectations.
    for (uint32_t direction = 0; direction < 4; ++direction)
        for (uint32_t state = 0; state < 8; ++state)
        {
            const auto recipe = Recipe(
                static_cast<uint32_t>(TrackStyle::loopingRollerCoaster), static_cast<uint32_t>(OpenRCT2::TrackElemType::flat),
                0, direction, state);
            ASSERT_EQ(recipe.size(), 12u);
            EXPECT_EQ(recipe[0], (state & 1) ? 15006u + direction : 15004u + (direction & 1));
            EXPECT_EQ(recipe[1], 0u);
            EXPECT_EQ(recipe[2], 0u);
            EXPECT_EQ(recipe[3], 0u);
            EXPECT_EQ(recipe[4], (direction & 1) ? 6u : 0u);
            EXPECT_EQ(recipe[5], (direction & 1) ? 0u : 6u);
            EXPECT_EQ(recipe[6], 0u);
            EXPECT_EQ(recipe[7], (direction & 1) ? 20u : 32u);
            EXPECT_EQ(recipe[8], (direction & 1) ? 32u : 20u);
            EXPECT_EQ(recipe[9], 3u);
            EXPECT_EQ(recipe[10], 0u);
            EXPECT_EQ(recipe[11], UINT32_MAX);
        }
}

TEST(WorldTrackRulesTest, EveryAuthoredRowStaysWithinItsImmutableCatalog)
{
    const auto words = OpenRCT2::Drawing::GetNativeTrackRecipeWords();
    ASSERT_GE(words.size(), 8u);
    ASSERT_EQ(words[0], 0x5452434bu);
    ASSERT_EQ(words[1], 1u);
    ASSERT_EQ(words[7], words.size());
    ASSERT_EQ(words[4], 8u);
    ASSERT_EQ(words[5], words[4] + words[2] * words[3] * 3);
    ASSERT_LE(words[5], words[6]);
    ASSERT_LE(words[6], words.size());
    ASSERT_EQ((words.size() - words[6]) % 12, 0u);
    const auto rows = (words[6] - words[5]) / 2;
    const auto parts = (words.size() - words[6]) / 12;
    uint32_t supported = 0, styles = 0;
    for (uint32_t style = 0; style < words[2]; ++style)
    {
        bool hasRecipes = false;
        for (uint32_t type = 0; type < words[3]; ++type)
        {
            const auto d = words[4] + (style * words[3] + type) * 3;
            const auto sequences = words[d + 1], mask = words[d + 2];
            if (sequences == 0)
                continue;
            hasRecipes = true;
            ++supported;
            ASSERT_LE(sequences, 16u);
            ASSERT_LT(mask, 128u);
            const auto count = (1u << std::popcount(mask)) * sequences * 4;
            ASSERT_LE(uint64_t(words[d]) + count, rows);
        }
        styles += hasRecipes;
    }
    // A broad-body checkpoint must not silently collapse into a single-style demo.
    EXPECT_GE(styles, 56u);
    EXPECT_GE(supported, 4800u);
    const auto images = OpenRCT2::Drawing::GetNativeTrackRecipeImages();
    EXPECT_TRUE(std::is_sorted(images.begin(), images.end()));
    EXPECT_EQ(std::adjacent_find(images.begin(), images.end()), images.end());
    for (size_t row = words[5]; row < words[6]; row += 2)
    {
        const auto first = words[row], count = words[row + 1];
        ASSERT_LE(uint64_t(first) + count, parts);
        ASSERT_LE(count, 16u);
        uint32_t expanded = count, parents = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto p = words[6] + (first + i) * 12;
            EXPECT_TRUE(
                words[p + 10] >= 4 || words[p] == 0xfffffffcu || words[p] == 0xfffffffdu || words[p] == 0xfffffffeu
                || std::binary_search(images.begin(), images.end(), words[p]));
            EXPECT_LE(words[p + 10], 5u);
            if (words[p + 10] >= 4)
            {
                EXPECT_EQ(words[p], 0u);
                EXPECT_GE(static_cast<int32_t>(words[p + 11]), 0);
            }
            const auto parent = static_cast<int32_t>(words[p + 11]);
            EXPECT_TRUE(parent == -1 || (parent >= 0 && static_cast<uint32_t>(parent) < i));
            if (words[p] == 0xfffffffdu)
            {
                EXPECT_LE(words[p + 1], 2u);
                EXPECT_TRUE(words[p + 2] < 26u || (words[p + 2] >= 256u && words[p + 2] < 264u));
                EXPECT_EQ(parent, -1);
                EXPECT_FALSE(std::binary_search(images.begin(), images.end(), words[p]));
                --expanded;
                continue;
            }
            if (words[p] == 0xfffffffcu)
            {
                EXPECT_LT(words[p + 1], 4u);
                EXPECT_LE(words[p + 2], 1u);
                EXPECT_EQ(words[p + 10], 2u);
                EXPECT_EQ(parent, -1);
                EXPECT_FALSE(std::binary_search(images.begin(), images.end(), words[p]));
                expanded += 2;
                parents += 2;
            }
            parents += parent == -1;
            if (words[p] == 0xfffffffeu)
            {
                expanded += 8;
                parents += 6;
            }
        }
        EXPECT_LE(expanded, 16u);
        EXPECT_LE(parents, 12u);
    }
}

TEST(WorldTrackRulesTest, UnknownStyleTypeAndSequenceNeverAliasSupportedRecipes)
{
    EXPECT_TRUE(Recipe(255, 0, 0, 0, 0).empty());
    EXPECT_TRUE(Recipe(39, 65535, 0, 0, 0).empty());
    EXPECT_TRUE(Recipe(39, 0, 16, 0, 0).empty());
    EXPECT_TRUE(Recipe(39, 0, 0, 4, 0).empty());
    EXPECT_TRUE(Recipe(39, 0, 0, 0, 128).empty());
}

TEST(WorldTrackRulesTest, SharedQuarterHelixRetainsOriginalImagesAndReversedBounds)
{
    // Original LIM-launched shared artwork used by the Looping source getter.
    const auto up = Recipe(39, 102, 0, 0, 0);
    ASSERT_EQ(up.size(), 24u);
    EXPECT_EQ(up[0], 35301u);
    EXPECT_EQ(up[12], 35302u);
    EXPECT_EQ(up[4], 0u);
    EXPECT_EQ(up[5], 6u);
    EXPECT_EQ(up[6], 1u);
    const auto down = Recipe(39, 104, 0, 0, 0);
    ASSERT_EQ(down.size(), 12u);
    EXPECT_EQ(down[0], 35330u);
    EXPECT_EQ(down[4], 0u);
    EXPECT_EQ(down[5], 25u);
    EXPECT_EQ(down[6], 11u);
    for (const auto direction : { 0u, 1u, 2u, 3u })
    {
        constexpr uint32_t reverse[] = { 6, 4, 5, 3, 1, 2, 0 };
        for (uint32_t sequence = 0; sequence < 7; ++sequence)
        {
            const auto actual = Recipe(39, 104, sequence, direction, 0);
            const auto expected = Recipe(39, 103, reverse[sequence], (direction + 1) & 3, 0);
            EXPECT_TRUE(std::ranges::equal(actual, expected));
        }
    }
}

TEST(WorldTrackRulesTest, JuniorWaterSubtypeAndNarrowPierStationRecipesStayDistinct)
{
    for (uint32_t direction = 0; direction < 4; ++direction)
    {
        const auto junior = Recipe(31, 0, 0, direction, 1);
        const auto water = Recipe(78, 0, 0, direction, 1);
        ASSERT_EQ(junior.size(), 12u);
        ASSERT_EQ(water.size(), 12u);
        EXPECT_EQ(junior[0], 27913u + (direction & 1));
        EXPECT_EQ(water[0], 27983u + (direction & 1));
    }
    for (const auto style : { 1u, 3u, 27u, 56u, 65u, 69u })
        for (const auto type : { 1u, 2u, 3u })
        {
            const auto recipe = Recipe(style, type, 0, 0, 64);
            uint32_t markers = 0;
            for (size_t i = 0; i < recipe.size(); i += 12)
                if (recipe[i] == 0xfffffffeu)
                {
                    ++markers;
                    EXPECT_EQ(recipe[i + 8], style == 3 || style == 69 ? 5u : 4u);
                }
            EXPECT_EQ(markers, 1u);
        }
}

TEST(WorldTrackRulesTest, JuniorWaterSBendsAndTransitionsKeepCompleteOriginalRailRows)
{
    for (const auto style : { 31u, 78u })
    {
        for (const auto type : { 38u, 39u })
        {
            for (uint32_t direction = 0; direction < 4; ++direction)
            {
                for (uint32_t sequence = 0; sequence < 4; ++sequence)
                {
                    const auto mapped = direction >= 2 ? 3 - sequence : sequence;
                    const auto expected = (direction & 1) ? (type == 38 ? 27908 : 27912) - mapped
                                                          : (type == 38 ? 27901 : 27897) + mapped;
                    const auto rail = Recipe(style, type, sequence, direction, 0);
                    ASSERT_EQ(rail.size(), 12u);
                    EXPECT_EQ(rail[0], expected);
                }
                // C++ source remaps invalid sequence4 to -1; it must never be
                // admitted as a Python-style wraparound row in the GPU catalog.
                EXPECT_TRUE(Recipe(style, type, 4, direction, 0).empty());
            }
        }
        for (const auto type : { 6u, 9u, 12u, 15u })
            for (uint32_t direction = 0; direction < 4; ++direction)
            {
                const auto rail = Recipe(style, type, 0, direction, 0);
                const auto chain = Recipe(style, type, 0, direction, 1);
                ASSERT_EQ(rail.size(), 12u);
                ASSERT_EQ(chain.size(), 12u);
                EXPECT_NE(rail[0], chain[0]);
            }
    }
}

#include "../../data/shaders/vulkan/world_tunnel_rules.glsl"

TEST(WorldTrackRulesTest, TunnelApertureUsesLowClearanceReplacementWithoutChangingDoorHeight)
{
    EXPECT_EQ(worldTunnelResolveType(2, 4, 6, 6), 0);
    EXPECT_EQ(worldTunnelResolveType(2, 4, 7, 7), 2);
    EXPECT_EQ(worldTunnelResolveType(5, 4, 7, 8), 3);
    EXPECT_EQ(worldTunnelResolveType(9, 4, 6, 6), 6);
    EXPECT_EQ(worldTunnelHeight(6), 2);
    EXPECT_EQ(worldTunnelImageOffset(25, 1, false), 38);
    EXPECT_EQ(worldTunnelImageOffset(25, 1, true), 102);
    EXPECT_EQ(worldTunnelTinyZ(-8), 255);
    EXPECT_EQ(worldTunnelTinyZ(80), 5);
}

TEST(WorldTrackRulesTest, ConnectedPathsSelectOriginalTunnelSidesAndSlopeHeights)
{
    // Paint.Path.cpp uses EDGE_SW=4, EDGE_SE=2, EDGE_NE=1, EDGE_NW=8,
    // including its original direction comparisons against those constants.
    for (int edges = 0; edges < 16; ++edges)
        for (int direction = 0; direction < 4; ++direction)
            for (bool sloped : { false, true })
            {
                const auto left = !(edges & 4) ? -1 : (sloped && direction == 2 ? 10 : (edges & 8 ? 11 : 10));
                const auto right = !(edges & 2) ? -1 : (sloped && direction == 1 ? 10 : (edges & 1 ? 11 : 10));
                EXPECT_EQ(worldPathTunnelType(edges, direction, sloped, 0), left);
                EXPECT_EQ(worldPathTunnelType(edges, direction, sloped, 1), right);
                EXPECT_EQ(worldPathTunnelZOffset(direction, sloped, 0), sloped && direction == 2 ? 16 : 0);
                EXPECT_EQ(worldPathTunnelZOffset(direction, sloped, 1), sloped && direction == 1 ? 16 : 0);
            }
}

TEST(WorldTrackRulesTest, FlatLoopingTunnelRequestsAreMetadataAndNeverImages)
{
    for (uint32_t direction = 0; direction < 4; ++direction)
    {
        const auto raw = RawRecipe(39, 0, 0, direction, 0);
        uint32_t requests = 0;
        for (size_t p = 0; p < raw.size(); p += 12)
            if (raw[p] == 0xfffffffdu)
            {
                ++requests;
                EXPECT_EQ(raw[p + 1], direction & 1);
                EXPECT_EQ(raw[p + 2], 0u);
                EXPECT_EQ(raw[p + 3], 0u);
            }
        EXPECT_EQ(requests, 1u);
        EXPECT_EQ(Recipe(39, 0, 0, direction, 0).size(), 12u);
    }
}

TEST(WorldTrackRulesTest, GhostTrainDoorSelectorsUseLiveRawDoorFrames)
{
    constexpr int outward[8] = { 16, 17, 17, 18, 17, 17, 16, 16 };
    constexpr int inward[8] = { 16, 19, 19, 20, 19, 19, 16, 16 };
    for (int a = 0; a < 8; ++a)
        for (int b = 0; b < 8; ++b)
        {
            EXPECT_EQ(worldTunnelDoorType(256, a, b), outward[a]);
            EXPECT_EQ(worldTunnelDoorType(257, a, b), outward[b]);
            EXPECT_EQ(worldTunnelDoorType(258, a, b), inward[a]);
            EXPECT_EQ(worldTunnelDoorType(263, a, b), inward[b] + 5);
        }
}

TEST(WorldTrackRulesTest, ExpandedInvertedAndGoKartsPiecesRetainSourceImages)
{
    const auto invertedFlat = Recipe(54, 0, 0, 0, 0);
    ASSERT_FALSE(invertedFlat.empty());
    EXPECT_EQ(invertedFlat[0], 26227u);
    const auto invertedCurve = Recipe(54, 16, 0, 0, 0);
    ASSERT_FALSE(invertedCurve.empty());
    EXPECT_EQ(invertedCurve[0], 26310u);
    for (const auto& [type, firstImage] : { std::pair{ 5u, 35621u }, std::pair{ 7u, 35605u }, std::pair{ 8u, 35613u } })
    {
        const auto parts = Recipe(24, type, 0, 0, 0);
        ASSERT_EQ(parts.size(), 24u);
        EXPECT_EQ(parts[0], firstImage);
        EXPECT_EQ(parts[12], firstImage + 1);
    }
}

TEST(WorldTrackRulesTest, StationTunnelHelpersRetainSquareAndTallRequests)
{
    for (const auto style : { TrackStyle::loopingRollerCoaster, TrackStyle::invertedRollerCoaster })
        for (uint32_t direction = 0; direction < 4; ++direction)
        {
            const auto raw = RawRecipe(static_cast<uint32_t>(style), 1, 0, direction, 0);
            uint32_t requests = 0;
            for (size_t p = 0; p < raw.size(); p += 12)
                if (raw[p] == 0xfffffffdu)
                {
                    ++requests;
                    EXPECT_EQ(raw[p + 1], direction & 1);
                    EXPECT_EQ(raw[p + 2], style == TrackStyle::loopingRollerCoaster ? 6u : 9u);
                    EXPECT_EQ(raw[p + 3], 0u);
                }
            EXPECT_EQ(requests, 1u);
        }
}

TEST(WorldTrackRulesTest, StaticRideTunnelRequestsFollowOriginalDoorAndTowerBranches)
{
    for (int family = 1; family <= 23; ++family)
        for (int direction = 0; direction < 4; ++direction)
            for (int side = 0; side < 2; ++side)
                EXPECT_EQ(
                    worldStaticRideTunnelType(family, direction, side),
                    (family == 18 || family == 19) && (direction == 1 || direction == 2) && side == (direction & 1) ? 6 : -1);
    for (int family : { 20, 21, 22 })
        for (int sequence = 0; sequence < 9; ++sequence)
        {
            EXPECT_EQ(worldStaticRideVerticalTunnelOffset(family, sequence, false), sequence == 0 ? 96 : -1);
            EXPECT_EQ(worldStaticRideVerticalTunnelOffset(family, sequence, true), sequence == 1 ? -1 : 32);
        }
    EXPECT_EQ(worldStaticRideVerticalTunnelOffset(18, 0, false), -1);
    OpenRCT2::WorldRidePresentationMaterials source;
    source.rides.resize(1);
    source.rides[0].present = true;
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_OBSERVATION_TOWER;
    std::set<uint32_t> images;
    OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [&](uint32_t image) {
        images.insert(image);
        return image;
    });
    for (uint32_t image = 1575; image <= 1578; ++image)
        EXPECT_TRUE(images.contains(image));
}

namespace PhotoRules
{
    using uint = uint32_t;
#include "../../data/shaders/vulkan/world_track_photo.glsl"
} // namespace PhotoRules

TEST(WorldTrackRulesTest, PhotoTimeoutSelectsOriginalCameraFlashWithUnchangedThreeParentGeometry)
{
    // Frozen tables from TrackPaintUtilOnridePhoto{Small,}Paint. In particular,
    // the second sign is lower and only north/east cameras inherit that height.
    constexpr int offsets[4][3][3] = {
        { { 26, 0, 0 }, { 26, 28, -3 }, { 6, 0, 0 } },
        { { 0, 6, 0 }, { 28, 6, -3 }, { 0, 26, 0 } },
        { { 6, 0, 0 }, { 6, 28, -3 }, { 26, 28, -3 } },
        { { 0, 26, 0 }, { 28, 26, -3 }, { 28, 6, -3 } },
    };
    constexpr uint32_t normal[4][3] = {
        { 25623, 25617, 25621 },
        { 25624, 25618, 25622 },
        { 25625, 25615, 25619 },
        { 25626, 25616, 25620 },
    };
    constexpr uint32_t small[4][3] = {
        { 23493, 23487, 23491 },
        { 23494, 23488, 23492 },
        { 23495, 23485, 23489 },
        { 23496, 23486, 23490 },
    };
    for (uint32_t direction = 0; direction < 4; ++direction)
        for (uint32_t part = 0; part < 3; ++part)
        {
            EXPECT_EQ(PhotoRules::worldPhotoX(direction, part), offsets[direction][part][0]);
            EXPECT_EQ(PhotoRules::worldPhotoY(direction, part), offsets[direction][part][1]);
            EXPECT_EQ(PhotoRules::worldPhotoZ(direction, part), offsets[direction][part][2]);
            for (uint32_t timeout : { 0u, 1u, 2u, 3u, 255u })
            {
                const auto column = part < 2 ? 0 : (timeout == 0 ? 1 : 2);
                EXPECT_EQ(PhotoRules::worldPhotoImage(direction, false, timeout, part), normal[direction][column]);
                EXPECT_EQ(PhotoRules::worldPhotoImage(direction, true, timeout, part), small[direction][column]);
            }
        }
}

TEST(WorldTrackRulesTest, AuthoredPhotoRecipesKeepLiveSelectionAndOwnEveryCameraState)
{
    const auto type = static_cast<uint32_t>(OpenRCT2::TrackElemType::onRidePhoto);
    for (uint32_t direction = 0; direction < 4; ++direction)
    {
        const auto raw = RawRecipe(static_cast<uint32_t>(TrackStyle::loopingRollerCoaster), type, 0, direction, 0);
        uint32_t photos = 0;
        for (size_t p = 0; p < raw.size(); p += 12)
            if (raw[p] == 0xfffffffcu)
            {
                ++photos;
                EXPECT_EQ(raw[p + 1], direction);
                EXPECT_EQ(raw[p + 2], 0u);
                EXPECT_EQ(raw[p + 3], 3u);
            }
        EXPECT_EQ(photos, 1u);
    }
    OpenRCT2::WorldRidePresentationMaterials source;
    source.rides.resize(1);
    source.rides[0].present = true;
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_LOOPING_ROLLER_COASTER;
    std::set<uint32_t> images;
    OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [&](uint32_t image) {
        images.insert(image);
        return image;
    });
    for (uint32_t image = 25615; image <= 25626; ++image)
        EXPECT_TRUE(images.contains(image));
    EXPECT_FALSE(images.contains(0xfffffffcu));
}

TEST(WorldTrackRulesTest, RestoredOrdinaryCurvesAndStationsKeepTheirFullSequenceTails)
{
    for (const uint32_t style : { 9u, 10u, 79u })
        for (const uint32_t type : { 16u, 17u, 22u, 23u })
            for (uint32_t direction = 0; direction < 4; ++direction)
                EXPECT_FALSE(Recipe(style, type, 6, direction, 0).empty());
    for (const uint32_t type : { 119u, 121u })
        for (uint32_t sequence = 0; sequence < 4; ++sequence)
            for (uint32_t direction = 0; direction < 4; ++direction)
                EXPECT_FALSE(Recipe(30, type, sequence, direction, 0).empty());
    for (uint32_t direction = 0; direction < 4; ++direction)
    {
        const auto regular = Recipe(53, 1, 0, direction, 64);
        const auto noPlatforms = Recipe(53, 1, 0, direction, 0);
        ASSERT_EQ(regular.size(), 24u);
        ASSERT_EQ(noPlatforms.size(), 12u);
        EXPECT_EQ(regular[0], 15812u + (direction & 1u));
        EXPECT_EQ(noPlatforms[0], 16220u + (direction & 1u));
        EXPECT_EQ(regular[12], 0xfffffffeu);
        EXPECT_EQ(regular[20], 6u); // Covers-only, no fabricated platform.
    }
}

TEST(WorldTrackRulesTest, WaterSplashFiltersRemainOrderedChildrenAndNeverBecomeImageZero)
{
    const auto images = OpenRCT2::Drawing::GetNativeTrackRecipeImages();
    EXPECT_FALSE(std::binary_search(images.begin(), images.end(), 0u));
    for (const uint32_t style : { 9u, 10u, 79u })
        for (uint32_t direction = 0; direction < 4; ++direction)
        {
            const auto parts = Recipe(style, 117, 0, direction, 0);
            ASSERT_GE(parts.size(), 48u);
            EXPECT_EQ(parts[24], 0u);
            EXPECT_EQ(parts[34], 4u);
            EXPECT_EQ(parts[36], 0u);
            EXPECT_EQ(parts[46], 5u);
            EXPECT_EQ(parts[35], 0u);
            EXPECT_EQ(parts[47], 0u);
        }
}
