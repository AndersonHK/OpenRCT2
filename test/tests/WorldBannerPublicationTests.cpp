/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/ImageId.hpp>
#include <openrct2/drawing/PaletteIndex.h>
#include <openrct2/drawing/ScrollingText.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/world/Banner.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>

using namespace OpenRCT2;

namespace
{
    class WorldBannerPublicationTest : public testing::Test
    {
    protected:
        bool oldHeadless = gOpenRCT2Headless;
        bool oldNoGraphics = gOpenRCT2NoGraphics;
        bool oldUppercase = Config::Get().general.upperCaseBanners;
        std::unique_ptr<IContext> context;
        void SetUp() override
        {
            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = false;
            context = CreateContext();
            ASSERT_TRUE(context->Initialise());
            MapInit({ 16, 16 });
            BannerInit(getGameState());
            Config::Get().general.upperCaseBanners = false;
            Drawing::ScrollingText::invalidate();
        }
        void TearDown() override
        {
            Config::Get().general.upperCaseBanners = oldUppercase;
            context.reset();
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
        }
        MapPresentationChangeBatch Capture(bool complete = false)
        {
            return ConsumeMapPresentationChanges(complete, MapPublicationProfile::rawTerrain);
        }
    };

    std::array<uint8_t, 64 * 40> RasterColumns(const Drawing::ScrollingText::TextColumns& text, uint16_t mode, uint32_t tick)
    {
        std::array<uint8_t, 64 * 40> pixels{};
        if (text.columns.empty())
            return pixels;
        const auto phase = text.phaseWidth == 0 ? 0 : (tick / 2) % text.phaseWidth;
        const auto& placement = Drawing::ScrollingText::getModeColumns()[mode];
        for (size_t x = 0; x < 64; ++x)
        {
            if (placement[x].sourceColumn == UINT16_MAX)
                continue;
            size_t column = phase + placement[x].sourceColumn;
            if (text.repeat)
                column %= text.columns.size();
            else if (column >= text.columns.size())
                continue;
            for (size_t y = 0; y < 8; ++y)
                pixels[x + (placement[x].y + y) * 64] = text.columns[column][y];
        }
        return pixels;
    }
} // namespace

TEST_F(WorldBannerPublicationTest, ImmutableColumnsMatchOriginalBitmapForEveryModeAndScrollPhase)
{
    const std::array<std::string, 4> strings{ "{YELLOW}Queue Name", "{RED}A{GREEN}b", "i", "" };
    PaintSession session{};
    session.rt.zoom_level = ZoomLevel{ 0 };
    size_t nonzero{};
    for (const auto& string : strings)
    {
        const auto columns = Drawing::ScrollingText::compileTextColumns(string, Drawing::PaletteIndex::transparent);
        for (uint16_t mode = 0; mode < Drawing::ScrollingText::kMaxModes; ++mode)
        {
            for (const uint32_t tick : { 0u, 1u, 2u, 23u, 119u, UINT32_MAX })
            {
                SCOPED_TRACE(string + " mode=" + std::to_string(mode) + " tick=" + std::to_string(tick));
                getGameState().currentTicks = tick;
                const auto image = Drawing::ScrollingText::setup(session, string, mode, Drawing::PaletteIndex::transparent);
                const auto* source = GfxGetG1Element(image);
                ASSERT_NE(source, nullptr);
                ASSERT_EQ(source->width, 64);
                ASSERT_EQ(source->height, 40);
                const auto actual = RasterColumns(columns, mode, tick);
                EXPECT_TRUE(std::equal(actual.begin(), actual.end(), source->offset));
                nonzero += std::count_if(actual.begin(), actual.end(), [](uint8_t pixel) { return pixel != 0; });
            }
        }
    }
    EXPECT_GT(nonzero, 0u);
}

TEST_F(WorldBannerPublicationTest, BannerEditsDeletionAndSlotReuseKeepHeldTextGeneration)
{
    auto* banner = CreateBanner();
    ASSERT_NE(banner, nullptr);
    const auto id = banner->id;
    banner->setText("First generation");
    auto first = Capture(true);
    ASSERT_NE(first.bannerTexts, nullptr);
    const auto held = first.bannerTexts->banners[id.ToUnderlying()];
    ASSERT_NE(held, nullptr);
    const auto heldColumns = *held;
    EXPECT_EQ(Capture().bannerTexts, first.bannerTexts);
    banner->setText("First generation");
    EXPECT_EQ(Capture().bannerTexts, first.bannerTexts);
    banner->setText("Replacement");
    const auto changed = Capture();
    EXPECT_NE(changed.bannerTexts, first.bannerTexts);
    EXPECT_NE(*changed.bannerTexts->banners[id.ToUnderlying()], heldColumns);
    banner->setFlag(BannerFlag::noEntry, true);
    const auto noEntry = Capture();
    EXPECT_NE(*noEntry.bannerTexts->banners[id.ToUnderlying()], *changed.bannerTexts->banners[id.ToUnderlying()]);
    DeleteBanner(id);
    const auto removed = Capture();
    EXPECT_EQ(removed.bannerTexts->banners[id.ToUnderlying()], nullptr);
    banner = CreateBanner();
    ASSERT_EQ(banner->id, id);
    banner->setText("Reused slot");
    const auto reused = Capture();
    EXPECT_NE(reused.bannerTexts->banners[id.ToUnderlying()], held);
    EXPECT_EQ(*held, heldColumns);
    MapPresentationSnapshot snapshot;
    snapshot.Apply(first);
    EXPECT_EQ(snapshot.GetBannerTexts(), first.bannerTexts);
    snapshot.Apply(reused);
    EXPECT_EQ(snapshot.GetBannerTexts(), reused.bannerTexts);
    EXPECT_EQ(*held, heldColumns);
}

TEST_F(WorldBannerPublicationTest, CopyAssignmentAndFontInvalidationRefreshTextWithoutBorrowing)
{
    auto* banner = GetOrCreateBanner(BannerIndex::FromUnderlying(8191));
    ASSERT_NE(banner, nullptr);
    banner->setType(0);
    banner->setText("lowercase");
    const auto first = Capture(true).bannerTexts;
    Banner copied = *banner;
    copied.setText("COPY");
    *banner = copied;
    const auto changed = Capture().bannerTexts;
    EXPECT_NE(*changed->banners[8191], *first->banners[8191]);
    const auto held = *changed->banners[8191];
    Config::Get().general.upperCaseBanners = true;
    Drawing::ScrollingText::invalidate();
    const auto invalidated = Capture().bannerTexts;
    EXPECT_NE(invalidated, changed);
    EXPECT_EQ(*changed->banners[8191], held);
    BannerInit(getGameState());
    EXPECT_TRUE(Capture().bannerTexts->banners.empty());
    EXPECT_EQ(*changed->banners[8191], held);
}

TEST_F(WorldBannerPublicationTest, QueueStatusIsRawForTrackedRidesAndDoesNotRebuildTextOrMechanismPresence)
{
    auto& state = getGameState();
    state.ridesEndOfUsedRange = 1;
    auto& ride = state.rides[0];
    ride.id = RideId::FromUnderlying(0);
    ride.type = RIDE_TYPE_LOOPING_ROLLER_COASTER;
    ride.customName = "Queue source";
    ride.status = RideStatus::closed;
    ride.flags.clearAll();
    const auto closed = Capture(true);
    ASSERT_NE(closed.bannerTexts->queueNames[0], nullptr);
    ASSERT_NE(closed.bannerTexts->queueClosed, nullptr);
    EXPECT_EQ((*closed.ridePoses->records)[0].words[0], 0u);
    ride.status = RideStatus::open;
    state.currentTicks += 2;
    const auto open = Capture();
    EXPECT_EQ((*open.ridePoses->records)[0].words[0], 16u);
    EXPECT_EQ(open.bannerTexts, closed.bannerTexts);
    ride.flags.set(RideFlag::brokenDown);
    const auto broken = Capture();
    EXPECT_EQ((*broken.ridePoses->records)[0].words[0], 24u);
    EXPECT_EQ(broken.bannerTexts, closed.bannerTexts);
    EXPECT_EQ((*closed.ridePoses->records)[0].words[0], 0u);
    ride.customName = "Renamed queue";
    Drawing::ScrollingText::invalidate();
    const auto renamed = Capture();
    EXPECT_NE(*renamed.bannerTexts->queueNames[0], *closed.bannerTexts->queueNames[0]);
}
