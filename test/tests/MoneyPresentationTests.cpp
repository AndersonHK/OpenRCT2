// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "../../src/openrct2-renderer/gpu/GpuWorldMoney.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/ScrollingText.h>
#include <openrct2/drawing/SpriteAssetDecoder.h>
#include <openrct2/drawing/TextColour.h>
#include <openrct2/entity/MoneyEffect.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
namespace
{
#include "../../data/shaders/vulkan/world_text_operation.glsl"
    class MoneyPresentationTest : public testing::Test
    {
    protected:
        bool oldHeadless = gOpenRCT2Headless, oldNoGraphics = gOpenRCT2NoGraphics;
        bool oldPurchases = Config::Get().general.showGuestPurchases;
        LegacyScene oldScene = gLegacyScene;
        std::unique_ptr<IContext> context;
        void SetUp() override
        {
            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = false;
            context = CreateContext();
            ASSERT_TRUE(context->Initialise());
            gameStateInitAll(getGameState(), { 16, 16 });
            gLegacyScene = LegacyScene::playing;
            Config::Get().general.showGuestPurchases = true;
        }
        void TearDown() override
        {
            context.reset();
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
            gLegacyScene = oldScene;
            Config::Get().general.showGuestPurchases = oldPurchases;
        }
        std::shared_ptr<const MoneyPresentationSnapshot> Capture()
        {
            return CaptureMoneyPresentationSnapshot(getGameState().currentTicks);
        }
    };
} // namespace

TEST_F(MoneyPresentationTest, MediumGlyphsPreserveFormattingWidthsOutlinesAndWaveOrdinals)
{
    const auto run = CompileTextGlyphRun("{OUTLINE}{GREEN}A {RED}B", { Colour::black }, FontStyle::medium, true);
    ASSERT_EQ(run.pieces.size(), 3u);
    EXPECT_EQ(run.waveCount, 3u);
    int32_t x = 0;
    for (uint32_t i = 0; i < 3; i++)
    {
        const int codepoint = "A B"[i];
        const auto* source = GfxGetG1Element(FontSpriteGetCodepointSprite(FontStyle::medium, codepoint));
        ASSERT_NE(source, nullptr);
        const auto& p = run.pieces[i];
        EXPECT_EQ(p.waveOrdinal, i);
        EXPECT_EQ(p.x, x + source->xOffset);
        EXPECT_EQ(p.y, source->yOffset);
        const auto palette = getTextColourMapping(i < 2 ? TextColour::green : TextColour::red);
        const auto raw = DecodeTrustedSpriteAsset(*source);
        ASSERT_EQ(p.pixels.size(), raw.pixels.size());
        const std::array<PaletteIndex, 4> expected{ PaletteIndex::transparent, palette.fill, palette.sunnyOutline,
                                                    palette.shadowOutline };
        for (size_t n = 0; n < raw.pixels.size(); n++)
        {
            const auto value = EnumValue(raw.pixels[n]);
            ASSERT_LT(value, expected.size());
            EXPECT_EQ(p.pixels[n], EnumValue(expected[value]));
        }
        x += FontSpriteGetCodepointWidth(FontStyle::medium, codepoint);
    }
}

TEST(MoneyTextOperationTest, HintedCoverageUsesBlendInkInsteadOfPaletteFilterRows)
{
    // Frozen indexed_transparency_compose contract: low byte zero blends,
    // nonzero writes solid ink; 0x0102/3 represent the otherwise ambiguous zero ink.
    for (int ink = 1; ink <= 255; ++ink)
    {
        EXPECT_EQ(worldTextOperationKind(ink << 8), 1);
        EXPECT_EQ(worldTextOperationValue(ink << 8), ink);
        EXPECT_EQ(worldTextOperationKind((ink << 8) | 1), 2);
        EXPECT_EQ(worldTextOperationValue((ink << 8) | 1), ink);
    }
    EXPECT_EQ(worldTextOperationKind(0x0102), 1);
    EXPECT_EQ(worldTextOperationValue(0x0102), 0);
    EXPECT_EQ(worldTextOperationKind(0x0103), 2);
    EXPECT_EQ(worldTextOperationValue(0x0103), 0);
    for (int ink = 0; ink <= 255; ++ink)
    {
        EXPECT_EQ(worldTextOperationKind(ink), 2);
        EXPECT_EQ(worldTextOperationValue(ink), ink);
    }
}

TEST_F(MoneyPresentationTest, MotionAndWiggleDoNotRecompileGlyphsAndHeldSnapshotSurvivesReuse)
{
    auto& state = getGameState();
    auto* money = state.entities.createEntity<MoneyEffect>();
    ASSERT_NE(money, nullptr);
    money->value = 12345;
    money->guestPurchase = 0;
    money->offsetX = -12;
    money->wiggle = 0;
    money->moveTo({ 64, 96, 48 });
    const auto first = Capture();
    ASSERT_EQ(first->records->size(), 1u);
    const auto held = *first->catalog->runs[0];
    const auto packedCatalog = Ui::Gpu::BuildWorldMoneyCatalog(first->catalog);
    const auto packed = Ui::Gpu::PackWorldMoneyRecords(*first, *packedCatalog);
    EXPECT_EQ(packed.size(), 28u);
    EXPECT_EQ(packed[16], 64u);
    EXPECT_EQ(packed[24], packedCatalog->runs[0].first);
    money->wiggle = 17;
    money->moveTo({ 66, 98, 49 });
    ++state.currentTicks;
    const auto moved = Capture();
    EXPECT_EQ(moved->catalog, first->catalog);
    EXPECT_NE(moved->records, first->records);
    EXPECT_EQ((*moved->records)[0].wiggle, 17u);
    EXPECT_EQ((*first->records)[0].wiggle, 0u);
    EXPECT_EQ(Capture()->records, moved->records);
    const auto id = money->id;
    const auto generation = (*moved->records)[0].generation;
    state.entities.entityRemove(money);
    EXPECT_TRUE(Capture()->records->empty());
    money = state.entities.createEntityAt<MoneyEffect>(id);
    ASSERT_NE(money, nullptr);
    money->value = -456;
    money->guestPurchase = 0;
    money->moveTo({ 64, 64, 32 });
    const auto reused = Capture();
    EXPECT_NE((*reused->records)[0].generation, generation);
    EXPECT_NE(reused->catalog->runs[0], first->catalog->runs[0]);
    EXPECT_EQ(*first->catalog->runs[0], held);
    EXPECT_THROW(static_cast<void>(CaptureMoneyPresentationSnapshot(state.currentTicks + 1)), std::invalid_argument);
}

TEST_F(MoneyPresentationTest, VisibilityAndFontInvalidationKeepOldRunsOwned)
{
    auto* money = getGameState().entities.createEntity<MoneyEffect>();
    ASSERT_NE(money, nullptr);
    money->value = 123;
    money->guestPurchase = 1;
    money->moveTo({ 64, 64, 32 });
    const auto first = Capture();
    ASSERT_EQ(first->records->size(), 1u);
    const auto held = first->catalog->runs[0];
    ScrollingText::invalidate();
    const auto changed = Capture();
    EXPECT_NE(changed->catalog->runs[0], held);
    EXPECT_EQ(*changed->catalog->runs[0], *held);
    Config::Get().general.showGuestPurchases = false;
    EXPECT_TRUE(Capture()->records->empty());
    Config::Get().general.showGuestPurchases = true;
    gLegacyScene = LegacyScene::titleSequence;
    EXPECT_TRUE(Capture()->records->empty());
    EXPECT_FALSE(held->pieces.empty());
}
