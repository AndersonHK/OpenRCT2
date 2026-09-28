// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#include <openrct2/drawing/WorldSelection.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

TEST(WorldSelectionTest, ConstructionTilesAreOwnedSortedUniqueAndBounded)
{
    std::vector<CoordsXY> tiles{ { 64, 32 }, { 32, 32 }, { 64, 32 }, { -32, 0 }, { 16384, 0 } };
    const auto words = MakeWorldSelectionWords(2, 4, {}, {}, {}, 0, tiles);
    ASSERT_EQ(words.size(), 18u);
    EXPECT_EQ(words[10], 2u);
    EXPECT_EQ(words[16], 65537u);
    EXPECT_EQ(words[17], 65538u);
    tiles.clear();
    EXPECT_NO_THROW(ValidateWorldSelectionWords(words));
    EXPECT_EQ(words[17], 65538u);
}

TEST(WorldSelectionTest, RectangleAndArrowPreserveSignedWorldCoordinatesWithoutSelectingSprites)
{
    const auto words = MakeWorldSelectionWords(5, 14, { -32, 64 }, { 96, 128 }, { 32, 64, -8 }, 7, {});
    EXPECT_EQ(static_cast<int32_t>(words[2]), -32);
    EXPECT_EQ(static_cast<int32_t>(words[8]), -8);
    EXPECT_EQ(words[9], 7u);
    EXPECT_EQ(words.size(), kWorldSelectionHeaderWords);
    EXPECT_NO_THROW(ValidateWorldSelectionWords(words));
}

TEST(WorldSelectionTest, InvalidPayloadCannotReachShaderBinarySearch)
{
    auto words = MakeWorldSelectionWords(2, 4, {}, {}, {}, 0, {});
    words[10] = 1;
    EXPECT_THROW(ValidateWorldSelectionWords(words), std::invalid_argument);
    words.push_back(512);
    EXPECT_THROW(ValidateWorldSelectionWords(words), std::invalid_argument);
    words[16] = 1;
    EXPECT_NO_THROW(ValidateWorldSelectionWords(words));
    words.push_back(1);
    words[10] = 2;
    EXPECT_THROW(ValidateWorldSelectionWords(words), std::invalid_argument);
    EXPECT_THROW(MakeWorldSelectionWords(4, 0, {}, {}, {}, 8, {}), std::invalid_argument);
}

TEST(WorldSelectionTest, CutawayStateIsOwnedWithoutActiveConstructionSelection)
{
    auto first = CoordsXY{ 32, 64 };
    auto last = CoordsXY{ 320, 640 };
    const auto words = MakeWorldSelectionWords(0, 0, {}, {}, {}, 0, {}, 42, first, last);
    first.x = 999;
    last.y = 999;
    EXPECT_EQ(words.size(), kWorldSelectionHeaderWords);
    EXPECT_EQ(words[0], 0u);
    EXPECT_EQ(words[11], 42u * kCoordsZStep);
    EXPECT_EQ(words[12], 32u);
    EXPECT_EQ(words[13], 64u);
    EXPECT_EQ(words[14], 320u);
    EXPECT_EQ(words[15], 640u);
    EXPECT_NO_THROW(ValidateWorldSelectionWords(words));
    auto invalid = words;
    invalid[14] = 0;
    EXPECT_THROW(ValidateWorldSelectionWords(invalid), std::invalid_argument);
}
