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

namespace ComponentDepthRules
{
#include "../../data/shaders/vulkan/world_component_depth.glsl"
}

namespace TrackDepthRules
{
#include "../../data/shaders/vulkan/world_track_depth.glsl"
}

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
                                    for (uint32_t tick = 0; tick < 128; ++tick)
                                        images.insert(OpenRCT2::Drawing::GetNativeTrackImageAtTick(recipe[part], tick));
                                else
                                    for (uint32_t image = SPR_STATION_PLATFORM_SW_NE; image <= SPR_STATION_BASE_BORDERLESS;
                                         ++image)
                                        images.insert(image);
                            }
                        }
        return { images.begin(), images.end() };
    }

    std::vector<uint32_t> WithMetalSupportImages(const std::vector<uint32_t>& rails)
    {
        // Independent union of original MetalSupports.cpp base/slope, beam/joint
        // and crossbeam banks. This must not add recipes to the rail sidecar or
        // replace any rail image: residency alone includes these shared assets.
        std::set<uint32_t> images(rails.begin(), rails.end());
        for (const auto range : { std::pair{ 3124u, 3241u }, std::pair{ 3243u, 3389u }, std::pair{ 3658u, 3674u } })
            for (uint32_t image = range.first; image <= range.second; ++image)
                images.insert(image);
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
    const auto railImages = StyleImages({ TrackStyle::loopingRollerCoaster });
    ASSERT_FALSE(railImages.empty());
    const auto expected = WithMetalSupportImages(railImages);
    EXPECT_EQ(resolved, expected);
    EXPECT_TRUE(std::includes(resolved.begin(), resolved.end(), railImages.begin(), railImages.end()));
    EXPECT_LT(resolved.size(), OpenRCT2::Drawing::GetNativeTrackRecipeImages().size());
    EXPECT_TRUE(std::binary_search(resolved.begin(), resolved.end(), 15004u));
    EXPECT_TRUE(std::binary_search(resolved.begin(), resolved.end(), 15009u));
    const auto& words = catalog.words;
    ASSERT_EQ(words[1] & 255u, 2u);
    const auto originalRails = OpenRCT2::Drawing::GetNativeTrackRecipeWords();
    ASSERT_LE(words[2] + originalRails.size(), words.size());
    EXPECT_TRUE(std::equal(originalRails.begin(), originalRails.end(), words.begin() + words[2]));
    const auto originalSupports = OpenRCT2::Drawing::GetNativeTrackSupportWords();
    ASSERT_EQ(words[13], originalSupports.size());
    ASSERT_LE(words[12] + originalSupports.size(), words.size());
    EXPECT_TRUE(std::equal(originalSupports.begin(), originalSupports.end(), words.begin() + words[12]));
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
    EXPECT_EQ(
        resolved,
        WithMetalSupportImages(StyleImages({ TrackStyle::flyingRollerCoaster, TrackStyle::flyingRollerCoasterInverted })));
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

TEST(WorldTrackRulesTest, OwnRailEnvelopeOccludesCentreSupportsWithoutMovingEitherRasterAnchor)
{
    namespace Support = OpenRCT2::Ui::Gpu::MetalSupportRules;
    constexpr int trackHeight = 128;
    for (uint32_t direction = 0; direction < 4; ++direction)
        for (uint32_t chain = 0; chain < 2; ++chain)
        {
            const auto rail = Recipe(
                static_cast<uint32_t>(TrackStyle::loopingRollerCoaster), static_cast<uint32_t>(OpenRCT2::TrackElemType::flat),
                0, direction, chain);
            ASSERT_EQ(rail.size(), 12u);
            // Original rail and support draw offsets remain unchanged. The
            // owner relation caps support depth, independent of image/style IDs.
            EXPECT_EQ(rail[1], 0u);
            EXPECT_EQ(rail[2], 0u);
            EXPECT_EQ(rail[3], 0u);
            for (int metal = 0; metal < 8; ++metal)
            {
                Support::WorldSupportState state;
                Support::worldSupportInitialise(state);
                Support::worldSupportSeedTerrain(state, 0, 0, 65535);
                auto cursor = Support::worldMetalBegin(state, metal, 4, 4, direction, trackHeight, 0, false, false);
                ASSERT_TRUE(cursor.accepted);
                int maximumColumnDepth = -1;
                int parts = 0;
                while (cursor.phase >= 0)
                {
                    ASSERT_LT(parts, 64);
                    const auto part = Support::worldMetalNext(cursor);
                    if (part.imageOffset < 0)
                        continue;
                    ++parts;
                    ASSERT_EQ(part.x, 16);
                    ASSERT_EQ(part.y, 16);
                    const int columnDepth = part.x + part.y + part.z;
                    const int supportedDepth = TrackDepthRules::worldTrackUnderRailDepth(columnDepth, trackHeight);
                    EXPECT_LT(supportedDepth, trackHeight);
                    EXPECT_EQ(supportedDepth, columnDepth < trackHeight ? columnDepth : trackHeight - 1);
                    maximumColumnDepth = std::max(maximumColumnDepth, columnDepth);
                }
                EXPECT_GT(parts, 0);
                // The former raster-origin scalar puts the top beam in front
                // of its own rail. This is the concrete regression covered.
                EXPECT_GT(maximumColumnDepth, trackHeight);
            }
        }
}

TEST(WorldTrackRulesTest, NamedStationFrontEaveIsBetweenOwnFenceAndAdjacentTallerBooth)
{
    // Original regular/inverted/tall shelter variants use roof heights22/30/46.
    // The front cover includes that roof; its art still draws from (0,0,height).
    constexpr int roofHeights[] = { 22, 30, 46 };
    for (int variant = 0; variant < 3; ++variant)
        for (int edge = 0; edge < 4; ++edge)
        {
            const auto marker = TrackDepthRules::worldTrackStationCoverMarker(edge, variant);
            const auto anchor = TrackDepthRules::worldTrackStationCoverAnchor(marker);
            if (edge == 0 || edge == 3)
            {
                EXPECT_EQ(marker, 0);
                EXPECT_FALSE(anchor.valid); // Do not move the independent rear wall.
                continue;
            }
            ASSERT_TRUE(anchor.valid);
            EXPECT_EQ(anchor.x, edge == 1 ? 0 : 31);
            EXPECT_EQ(anchor.y, edge == 1 ? 31 : 0);
            EXPECT_EQ(anchor.z, roofHeights[variant] + 1);
            const int eaveDepth = anchor.x + anchor.y + anchor.z;
            // Regular platform height5/fence7; inverted platform6/fence8.
            // The separately authored corner end-post (31,23) is not the
            // front fence origin and need not sit behind the complete roof.
            for (const int platformHeight : { 5, 6 })
                EXPECT_GT(eaveDepth, 24 + platformHeight);
            for (const int fenceHeight : { 7, 8 })
                EXPECT_GT(eaveDepth, 31 + fenceHeight);
            // The neighboring tile's front frame has explicit local(2,2).
            // All cover variants remain behind a frame reaching above them.
            const int tallerFrameHeight = roofHeights[variant] + 8;
            EXPECT_LT(eaveDepth, 32 + 2 + 2 + tallerFrameHeight);
            // The observed glass booth is30 units high: both ordinary and
            // inverted roofs must leave it visible. A46-unit tall shelter is
            // genuinely higher; do not invent a depth clamp for that case.
            if (variant < 2)
                EXPECT_LT(eaveDepth, 32 + 2 + 2 + 30);
            else
                EXPECT_GT(eaveDepth, 32 + 2 + 2 + 30);
            const auto glassAnchor = TrackDepthRules::worldTrackStationCoverAnchor(marker);
            EXPECT_EQ(glassAnchor.x, anchor.x);
            EXPECT_EQ(glassAnchor.y, anchor.y);
            EXPECT_EQ(glassAnchor.z, anchor.z);
        }
    for (const int ordinary : { 0, 15004, 22362, 22370, -2, -3, -4 })
        EXPECT_FALSE(TrackDepthRules::worldTrackStationCoverAnchor(ordinary).valid);
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
            if (words[p + 10] < 4 && words[p] != 0xfffffffcu && words[p] != 0xfffffffdu && words[p] != 0xfffffffeu)
            {
                const bool animated = OpenRCT2::Drawing::IsNativeTrackAnimatedImage(words[p]);
                ASSERT_TRUE(words[p] < 0x7ffffu || animated);
                const auto frames = OpenRCT2::Drawing::GetNativeTrackImageFrameCount(words[p]);
                const auto period = animated ? frames << ((words[p] >> 19) & 7u) : 1u;
                const auto firstImage = OpenRCT2::Drawing::GetNativeTrackImageAtTick(words[p], 0);
                uint32_t decodedFrames = 0;
                for (uint32_t tick = 0; tick < period; ++tick)
                {
                    const auto image = OpenRCT2::Drawing::GetNativeTrackImageAtTick(words[p], tick);
                    EXPECT_TRUE(std::binary_search(images.begin(), images.end(), image)) << image;
                    ASSERT_GE(image, firstImage);
                    ASSERT_LT(image - firstImage, frames);
                    decodedFrames |= 1u << (image - firstImage);
                }
                EXPECT_EQ(decodedFrames, (1u << frames) - 1u);
                if (animated)
                    EXPECT_FALSE(std::binary_search(images.begin(), images.end(), words[p]));
            }
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
                ASSERT_LE(words[p + 8], 7u);
                if (words[p + 8] == 7)
                {
                    EXPECT_LT(words[p + 1], 4u);
                    EXPECT_LT(words[p + 2], 3u);
                    ++expanded;
                }
                else
                {
                    expanded += 8;
                    parents += 6;
                }
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

TEST(WorldTrackRulesTest, ProductionSupportCatalogIsAdmittedAndMalformedSupportRangesAreRejected)
{
    OpenRCT2::WorldRidePresentationMaterials source;
    source.rides.resize(1);
    source.rides[0].present = true;
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_LOOPING_ROLLER_COASTER;
    auto catalog = OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [](uint32_t) { return 0u; });
    EXPECT_NO_THROW(OpenRCT2::Ui::Gpu::ValidateWorldTrackCatalog(catalog.words));
    const auto valid = catalog.words;
    catalog.words[12] = static_cast<uint32_t>(catalog.words.size());
    EXPECT_THROW(OpenRCT2::Ui::Gpu::ValidateWorldTrackCatalog(catalog.words), std::invalid_argument);
    catalog.words = valid;
    catalog.words[catalog.words[12] + 7]--;
    EXPECT_THROW(OpenRCT2::Ui::Gpu::ValidateWorldTrackCatalog(catalog.words), std::invalid_argument);
    catalog.words = valid;
    catalog.words[1] = 3;
    EXPECT_THROW(OpenRCT2::Ui::Gpu::ValidateWorldTrackCatalog(catalog.words), std::invalid_argument);
}

TEST(WorldTrackRulesTest, ConstantComponentLayersCannotCrossAnAdjacentAuthoredAnchorOrUi)
{
    using namespace ComponentDepthRules;
    const auto encoded = [](int depth, uint32_t layer, uint32_t base) {
        const float priority = static_cast<float>(base + worldComponentPriorityOffset(depth));
        const float value = 1.0f - (priority + 1.0f) / static_cast<float>(1u << 22);
        return std::bit_cast<float>(std::bit_cast<uint32_t>(value) - layer);
    };
    // Exercise every integer scalar, including every D32 exponent boundary, at
    // representative reserved world positions. Smaller hardware Z is nearer.
    for (uint32_t base : { 0u, 17u, 1048576u, 2097152u, 3145728u })
    {
        for (int depth = WORLD_COMPONENT_DEPTH_MIN; depth < WORLD_COMPONENT_DEPTH_MAX; ++depth)
        {
            ASSERT_GT(encoded(depth, WORLD_COMPONENT_LAYER_MAX, base), encoded(depth + 1, 0, base))
                << depth << " at world base " << base;
        }
        const float precedingUi = 1.0f - static_cast<float>(base) / static_cast<float>(1u << 22);
        const float followingUi = 1.0f - static_cast<float>(base + 1048576u + 1u) / static_cast<float>(1u << 22);
        EXPECT_LT(encoded(WORLD_COMPONENT_DEPTH_MIN, 0, base), precedingUi);
        EXPECT_GT(encoded(WORLD_COMPONENT_DEPTH_MAX, WORLD_COMPONENT_LAYER_MAX, base), followingUi);
        for (int depth : { WORLD_COMPONENT_DEPTH_MIN, -1, 0, 65536, WORLD_COMPONENT_DEPTH_MAX })
            for (uint32_t layer = 1; layer <= WORLD_COMPONENT_LAYER_MAX; ++layer)
                EXPECT_LT(encoded(depth, layer, base), encoded(depth, layer - 1, base));
    }
    EXPECT_FALSE(worldComponentDepthValid(WORLD_COMPONENT_DEPTH_MIN - 1, 0));
    EXPECT_FALSE(worldComponentDepthValid(WORLD_COMPONENT_DEPTH_MAX + 1, 0));
    EXPECT_FALSE(worldComponentDepthValid(0, WORLD_COMPONENT_LAYER_MAX + 1));
    EXPECT_FALSE(worldComponentDepthValid(0, -1));
    EXPECT_TRUE(worldComponentDepthValid(WORLD_COMPONENT_DEPTH_MIN, WORLD_COMPONENT_LAYER_MAX));
    EXPECT_TRUE(worldComponentDepthValid(WORLD_COMPONENT_DEPTH_MAX, 0));
}

TEST(WorldTrackRulesTest, SupportContactConstraintDoesNotOrderOtherElementsOrLowerColumnSections)
{
    // A column under a high rail may still stand in front of a different low
    // crossing rail: only its own element participates in the constraint.
    EXPECT_EQ(TrackDepthRules::worldTrackUnderRailDepth(180, 200), 180);
    EXPECT_GT(TrackDepthRules::worldTrackUnderRailDepth(180, 200), 100);
    EXPECT_EQ(TrackDepthRules::worldTrackUnderRailDepth(200, 200), 199);
    EXPECT_EQ(TrackDepthRules::worldTrackUnderRailDepth(-40, -20), -40);
    EXPECT_EQ(TrackDepthRules::worldTrackUnderRailDepth(-10, -20), -21);
}

TEST(WorldTrackRulesTest, RiverRapidsImageClockKeepsFixedComponentOwnershipAcrossCompletePeriods)
{
    using namespace OpenRCT2::Drawing;
    for (uint32_t direction = 0; direction < 4; ++direction)
        for (const uint32_t type : { 112u, 113u, 120u })
        {
            const auto parts = Recipe(58, type, 0, direction, 0);
            const uint32_t count = type == 112 ? 5 : type == 113 ? 2 : 3;
            ASSERT_EQ(parts.size(), count * 12);
            for (uint32_t tick = 0; tick < 128; ++tick)
            {
                const auto frame8 = (tick / 2) % 8, frame16 = (tick / 4) % 16;
                const std::vector<uint32_t> expected = type == 112
                    ? std::vector<uint32_t>{ 21204 + direction, (direction & 1 ? 21220u : 21212u) + frame8,
                                             (direction & 1 ? 21252u : 21244u) + frame8, 21208 + direction,
                                             (direction & 1 ? 21236u : 21228u) + frame8 }
                    : type == 113
                    ? std::vector<uint32_t>{ (direction & 1 ? 21269u : 21260u) + frame8, direction & 1 ? 21277u : 21268u }
                    : std::vector<uint32_t>{ 21132 + direction, 21278 + frame16, 21136 + direction };
                for (uint32_t i = 0; i < count; ++i)
                    EXPECT_EQ(GetNativeTrackImageAtTick(parts[i * 12], tick), expected[i]);
            }
            EXPECT_EQ(GetNativeTrackImageAtTick(parts[0], 0xffffffffu), GetNativeTrackImageAtTick(parts[0], 127u));
            if (type == 112 || type == 120)
                EXPECT_EQ(parts[12 + 11], 0u);
            if (type == 112)
                EXPECT_EQ(parts[48 + 11], 3u);
        }
    EXPECT_FALSE(IsNativeTrackAnimatedImage(0xffffffffu));
    EXPECT_FALSE(IsNativeTrackAnimatedImage(0x80000000u));                       // No frame count.
    EXPECT_FALSE(IsNativeTrackAnimatedImage(0x80200000u | (3u << 22) | 21212u)); // Period16 outside contract.
    EXPECT_FALSE(IsNativeTrackAnimatedImage(0x82000000u | (1u << 19) | (3u << 22) | 21212u));
    EXPECT_FALSE(IsNativeTrackAnimatedImage(0x80000000u | (1u << 19) | (4u << 22) | 0x7fff8u));
}

TEST(WorldTrackRulesTest, PresentRiverRapidsOwnsAllAnimationFramesWithoutResolvingEncodedIds)
{
    OpenRCT2::WorldRidePresentationMaterials source;
    source.rides.resize(1);
    source.rides[0].present = true;
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_RIVER_RAPIDS;
    std::set<uint32_t> images;
    const auto catalog = OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [&](uint32_t image) {
        EXPECT_LT(image, 0x7ffffu);
        images.insert(image);
        return image;
    });
    ASSERT_FALSE(catalog.words.empty());
    // Complete contiguous original waterfall, rapids and whirlpool art banks.
    for (uint32_t image = 21204; image < 21294; ++image)
        EXPECT_TRUE(images.contains(image)) << image;
    const auto supports = OpenRCT2::Drawing::GetNativeTrackSupportWords();
    for (const uint32_t type : { 112u, 113u, 120u })
        EXPECT_GT(supports[supports[4] + (58 * supports[3] + type) * 3 + 1], 0u);
}

TEST(WorldTrackRulesTest, MonorailEighthTurnsRetainAllAuthoredSequenceBounds)
{
    for (uint32_t type : { 133u, 134u, 135u, 136u })
        for (uint32_t direction = 0; direction < 4; ++direction)
        {
            uint32_t populated = 0;
            for (uint32_t sequence = 0; sequence < 5; ++sequence)
            {
                const auto parts = Recipe(50, type, sequence, direction, 0);
                EXPECT_LE(parts.size(), 12u);
                populated += !parts.empty();
            }
            EXPECT_EQ(populated, 4u);
        }
    const auto entry = Recipe(50, 134, 0, 0, 0);
    ASSERT_EQ(entry.size(), 12u);
    EXPECT_EQ((std::vector<uint32_t>(entry.begin() + 4, entry.begin() + 10)), (std::vector<uint32_t>{ 0, 6, 0, 32, 20, 2 }));
    const auto exit = Recipe(50, 134, 4, 0, 0);
    ASSERT_EQ(exit.size(), 12u);
    EXPECT_EQ((std::vector<uint32_t>(exit.begin() + 4, exit.begin() + 10)), (std::vector<uint32_t>{ 16, 0, 0, 16, 16, 2 }));
}

TEST(WorldTrackRulesTest, SpinningTunnelsKeepAnimatedBackChildFrontParentAndCompleteResidency)
{
    using namespace OpenRCT2::Drawing;
    for (uint32_t style : { 5u, 23u, 46u })
        for (uint32_t direction = 0; direction < 4; ++direction)
        {
            const auto parts = Recipe(style, 173, 0, direction, 0);
            ASSERT_EQ(parts.size(), (style == 46 ? 4u : 3u) * 12u);
            const auto back = parts.size() - 24, front = parts.size() - 12;
            EXPECT_EQ(parts[back + 11], 0u);
            EXPECT_EQ(parts[front + 11], UINT32_MAX);
            EXPECT_EQ(parts[back + 10], 1u);
            EXPECT_EQ(parts[front + 10], 1u);
            EXPECT_EQ(parts[back + 9], style == 23 ? 3u : 1u);
            for (uint32_t tick = 0; tick < 32; ++tick)
            {
                EXPECT_EQ(GetNativeTrackImageAtTick(parts[back], tick), 28865 + (direction & 1) * 4 + (tick / 4) % 4);
                EXPECT_EQ(GetNativeTrackImageAtTick(parts[front], tick), 28873 + (direction & 1) * 4 + (tick / 4) % 4);
            }
        }
    OpenRCT2::WorldRidePresentationMaterials source;
    source.rides.resize(1);
    source.rides[0].present = true;
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_CAR_RIDE;
    std::set<uint32_t> images;
    OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(source, [&](uint32_t image) {
        EXPECT_LT(image, 0x7ffffu);
        images.insert(image);
        return image;
    });
    for (uint32_t image = 28865; image <= 28880; ++image)
        EXPECT_TRUE(images.contains(image)) << image;
}

TEST(WorldTrackRulesTest, GoKartsStationsRetainGridSignalAndOrderedSingleCoverRequests)
{
    constexpr uint32_t red[4][2] = { { 20808, 20814 }, { 20810, 20816 }, { 20811, 20817 }, { 20812, 20818 } };
    constexpr uint32_t green[4][2] = { { 20809, 20815 }, { 20810, 20816 }, { 20811, 20817 }, { 20813, 20819 } };
    for (uint32_t type : { 1u, 2u, 3u })
        for (uint32_t direction = 0; direction < 4; ++direction)
            for (uint32_t light : { 0u, 32u })
            {
                const auto parts = Recipe(24, type, 0, direction, light);
                ASSERT_EQ(parts.size(), (type == 1 ? 6u : 4u) * 12u);
                EXPECT_EQ(parts[0], (type == 1 ? 20756u : 20764u) + direction);
                EXPECT_EQ(parts[24], (type == 1 ? 20760u : 20768u) + direction);
                for (uint32_t i : { 1u, 3u })
                {
                    EXPECT_EQ(parts[i * 12], 0xfffffffeu);
                    EXPECT_EQ(parts[i * 12 + 8], 7u);
                    EXPECT_EQ(parts[i * 12 + 1], (direction & 1u) ? (i == 1 ? 0u : 2u) : (i == 1 ? 3u : 1u));
                    EXPECT_EQ(parts[i * 12 + 2], 0u);
                    EXPECT_EQ(static_cast<int32_t>(parts[i * 12 + 11]), -1);
                }
                if (type == 1)
                    for (uint32_t i = 0; i < 2; ++i)
                        EXPECT_EQ(parts[(4 + i) * 12], (light ? green : red)[direction][i]);
                EXPECT_EQ(parts, Recipe(24, type, 0, direction, light | 64u));
            }
}
