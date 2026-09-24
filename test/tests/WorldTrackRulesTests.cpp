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
    std::span<const uint32_t> Recipe(uint32_t style, uint32_t type, uint32_t sequence, uint32_t direction, uint32_t state)
    {
        const auto words = OpenRCT2::Drawing::GetNativeTrackRecipeWords();
        if (style >= words[2] || type >= words[3] || direction >= 4 || state >= 8)
            return {};
        const auto descriptor = words[4] + (style * words[3] + type) * 3;
        const auto sequences = words[descriptor + 1], mask = words[descriptor + 2];
        if (sequence >= sequences)
            return {};
        uint32_t variant = 0, bitPosition = 0;
        for (uint32_t bit = 0; bit < 3; ++bit)
            if (mask & (1u << bit))
                variant |= ((state >> bit) & 1u) << bitPosition++;
        const auto row = words[5] + (words[descriptor] + (variant * sequences + sequence) * 4 + direction) * 2;
        return words.subspan(words[6] + words[row] * 12, words[row + 1] * 12);
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
                        for (uint32_t state = 0; state < 8; ++state)
                        {
                            const auto recipe = Recipe(static_cast<uint32_t>(style), type, sequence, direction, state);
                            for (size_t part = 0; part < recipe.size(); part += 12)
                                images.insert(recipe[part]);
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
    source.rides[0].rideType = OpenRCT2::RIDE_TYPE_HYBRID_COASTER;
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
            ASSERT_LT(mask, 8u);
            const auto count = (1u << std::popcount(mask)) * sequences * 4;
            ASSERT_LE(uint64_t(words[d]) + count, rows);
        }
        styles += hasRecipes;
    }
    // A broad-body checkpoint must not silently collapse into a single-style demo.
    EXPECT_GE(styles, 20u);
    EXPECT_GE(supported, 2000u);
    const auto images = OpenRCT2::Drawing::GetNativeTrackRecipeImages();
    EXPECT_TRUE(std::is_sorted(images.begin(), images.end()));
    EXPECT_EQ(std::adjacent_find(images.begin(), images.end()), images.end());
    for (size_t row = words[5]; row < words[6]; row += 2)
    {
        const auto first = words[row], count = words[row + 1];
        ASSERT_LE(uint64_t(first) + count, parts);
        ASSERT_LE(count, 64u);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto p = words[6] + (first + i) * 12;
            EXPECT_TRUE(std::binary_search(images.begin(), images.end(), words[p]));
            EXPECT_LE(words[p + 10], 1u);
            const auto parent = static_cast<int32_t>(words[p + 11]);
            EXPECT_TRUE(parent == -1 || (parent >= 0 && static_cast<uint32_t>(parent) < i));
        }
    }
}

TEST(WorldTrackRulesTest, UnknownStyleTypeAndSequenceNeverAliasSupportedRecipes)
{
    EXPECT_TRUE(Recipe(255, 0, 0, 0, 0).empty());
    EXPECT_TRUE(Recipe(39, 65535, 0, 0, 0).empty());
    EXPECT_TRUE(Recipe(39, 0, 16, 0, 0).empty());
    EXPECT_TRUE(Recipe(39, 0, 0, 4, 0).empty());
    EXPECT_TRUE(Recipe(39, 0, 0, 0, 8).empty());
}
