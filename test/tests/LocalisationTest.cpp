/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "helpers/StringHelpers.hpp"
#include "openrct2/localisation/Language.h"
#include "openrct2/rct12/CSStringConverter.h"

#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/core/UnicodeChar.h>
#include <openrct2/drawing/Font.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/localisation/Currency.h>
#include <openrct2/platform/Platform.h>

using namespace OpenRCT2;

class Localisation : public testing::Test
{
};

TEST_F(Localisation, DollarLocaleDefaultsPreserveDedicatedCurrenciesAndInvalidFallback)
{
    for (const char* code : { "CAD", "AUD", "NZD", "SGD", "XCD" })
        EXPECT_EQ(Platform::GetCurrencyValue(code), CurrencyType::dollars);
    for (int32_t i = 0; i < EnumValue(CurrencyType::count); ++i)
        EXPECT_EQ(Platform::GetCurrencyValue(CurrencyDescriptors[i].isoCode), static_cast<CurrencyType>(i));
    constexpr const char* invalidCodes[] = { nullptr, "", "CA", "ZZZ", "cad" };
    for (const char* code : invalidCodes)
        EXPECT_EQ(Platform::GetCurrencyValue(code), CurrencyType::pounds);
}

TEST_F(Localisation, SpriteFontsHaveDistinctLowercaseHardSignAndCurrencyGlyphsInEveryStyle)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    // Enable resource lookup after creating the headless context, without creating a native window.
    struct RestoreGraphics
    {
        ~RestoreGraphics() { gOpenRCT2NoGraphics = true; }
    } restoreGraphics;
    gOpenRCT2NoGraphics = false;
    GfxLoadG2PalettesFontsTracks();
    FontSpriteInitialiseCharacters();
    EXPECT_TRUE(FontSupportsStringSprite(u8"ъ₩₴"));
    EXPECT_TRUE(FontSupportsStringSprite(u8"✕❌"));
    EXPECT_EQ(FontSpriteGetCodepointOffset(U'✕'), 0xAD - 32);
    EXPECT_EQ(FontSpriteGetCodepointOffset(U'❌'), FontSpriteGetCodepointOffset(U'X'));
    EXPECT_NE(FontSpriteGetCodepointOffset(U'✕'), FontSpriteGetCodepointOffset(U'❌'));
    EXPECT_EQ(FontSpriteGetCodepointOffset(U'ъ'), static_cast<int32_t>(SPR_FONTS_CYRILLIC_HARD_SIGN_LOWER - SPR_FONTS_BEGIN));
    EXPECT_EQ(FontSpriteGetCodepointOffset(U'₩'), static_cast<int32_t>(SPR_FONTS_WON_SIGN - SPR_FONTS_BEGIN));
    EXPECT_EQ(FontSpriteGetCodepointOffset(U'₴'), static_cast<int32_t>(SPR_FONTS_HRYVNIA_SIGN - SPR_FONTS_BEGIN));
    for (const auto style : kFontStyles)
    {
        const auto smallClose = FontSpriteGetCodepointSprite(style, U'✕').GetIndex();
        const auto largeClose = FontSpriteGetCodepointSprite(style, U'❌').GetIndex();
        EXPECT_EQ(largeClose, FontSpriteGetCodepointSprite(style, U'X').GetIndex());
        EXPECT_NE(smallClose, largeClose);
        ASSERT_NE(GfxGetG1Element(smallClose), nullptr);
        ASSERT_NE(GfxGetG1Element(largeClose), nullptr);
        EXPECT_GT(GfxGetG1Element(smallClose)->width, 0);
        EXPECT_GT(GfxGetG1Element(largeClose)->width, 0);
        const auto lower = FontSpriteGetCodepointSprite(style, U'ъ').GetIndex();
        const auto upper = FontSpriteGetCodepointSprite(style, U'Ъ').GetIndex();
        const auto won = FontSpriteGetCodepointSprite(style, U'₩').GetIndex();
        const auto hryvnia = FontSpriteGetCodepointSprite(style, U'₴').GetIndex();
        const auto fallback = FontSpriteGetCodepointSprite(style, U'?').GetIndex();
        EXPECT_NE(lower, upper);
        EXPECT_NE(won, fallback);
        EXPECT_NE(hryvnia, fallback);
        EXPECT_NE(hryvnia, won);
        EXPECT_GE(lower, SPR_FONTS_BEGIN);
        EXPECT_LT(lower, SPR_FONTS_END);
        EXPECT_GE(won, SPR_FONTS_BEGIN);
        EXPECT_LT(won, SPR_FONTS_END);
        ASSERT_NE(GfxGetG1Element(lower), nullptr);
        ASSERT_NE(GfxGetG1Element(won), nullptr);
        EXPECT_GT(GfxGetG1Element(lower)->width, 0);
        EXPECT_GT(GfxGetG1Element(won)->width, 0);
        ASSERT_NE(GfxGetG1Element(hryvnia), nullptr);
        EXPECT_GT(GfxGetG1Element(hryvnia)->width, 0);
    }
    const auto* guilderBold = GfxGetG1Element(FontSpriteGetCodepointSprite(FontStyle::medium, U'ƒ'));
    const auto* guilderTiny = GfxGetG1Element(FontSpriteGetCodepointSprite(FontStyle::tiny, U'ƒ'));
    ASSERT_NE(guilderBold, nullptr);
    ASSERT_NE(guilderTiny, nullptr);
    EXPECT_EQ(guilderBold->xOffset, -1);
    EXPECT_EQ(guilderTiny->yOffset, 0);
    // Unloading releases the vectors even though the old file headers retain their entry counts.
    GfxUnloadG2PalettesFontsTracks();
    EXPECT_EQ(GfxGetG1Element(SPR_G2_BEGIN + 1), nullptr);
    EXPECT_EQ(GfxGetG1Element(SPR_PALETTE_START + 1), nullptr);
    EXPECT_EQ(GfxGetG1Element(SPR_FONTS_BEGIN + 1), nullptr);
    EXPECT_EQ(GfxGetG1Element(SPR_TRACKS_BEGIN + 1), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
// Tests for RCT2StringToUTF8
///////////////////////////////////////////////////////////////////////////////

TEST_F(Localisation, LegacyCloseGlyphDecodesToDingbatMultiply)
{
    EXPECT_EQ(RCT2StringToUTF8(StringFromHex("41AD42"), RCT2LanguageId::englishUK), u8"A✕B");
}

TEST_F(Localisation, RCT2_to_UTF8_UK)
{
    auto input = "The quick brown fox";
    auto expected = u8"The quick brown fox";
    auto actual = RCT2StringToUTF8(input, RCT2LanguageId::englishUK);
    ASSERT_EQ(expected, actual);
}

TEST_F(Localisation, RCT2_to_UTF8_JP)
{
    auto input = StringFromHex("ff8374ff8340ff8358ff8367ff8375ff8389ff8345ff8393ff8374ff8348ff8362ff834eff8358");
    auto expected = u8"ファストブラウンフォックス";
    auto actual = RCT2StringToUTF8(input, RCT2LanguageId::japanese);
    ASSERT_EQ(expected, actual);
}

TEST_F(Localisation, RCT2_to_UTF8_ZH_TW)
{
    auto input = StringFromHex("ffa7d6ffb374ffaabaffb4c4ffa6e2ffaab0ffaf57");
    auto expected = u8"快速的棕色狐狸";
    auto actual = RCT2StringToUTF8(input, RCT2LanguageId::chineseTraditional);
    ASSERT_EQ(expected, actual);
}

TEST_F(Localisation, RCT2_to_UTF8_PL)
{
    auto input = StringFromHex("47F372736b6120446ff76b692054e6637a6f7779");
    auto expected = u8"Górska Dołki Tęczowy";
    auto actual = RCT2StringToUTF8(input, RCT2LanguageId::englishUK);
    ASSERT_EQ(expected, actual);
}

TEST_F(Localisation, RCT2_to_UTF8_ZH_TW_PREMATURE_END)
{
    // This string can be found in BATFL.DAT, the last double byte character is missing its second byte.
    auto input = StringFromHex("ffa470ffabacffa8aeffbdf8ffa662ffc54bffb944ffa457ffaeb6ffb0caffb76effc2");
    auto expected = u8"小型車輛在鐵道上振動搖";
    auto actual = RCT2StringToUTF8(input, RCT2LanguageId::chineseTraditional);
    ASSERT_EQ(expected, actual);
}
