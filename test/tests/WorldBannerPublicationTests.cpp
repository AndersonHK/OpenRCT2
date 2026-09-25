/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include "../../src/openrct2-renderer/gpu/GpuWorldBannerText.h"
#include "../../src/openrct2-renderer/gpu/GpuWorldObject.h"
#include "../../src/openrct2-renderer/gpu/GpuWorldPropCatalog.h"

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/JobPool.h>
#include <openrct2/drawing/BlendColourMap.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/ColourMap.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/ImageId.hpp>
#include <openrct2/drawing/PaletteIndex.h>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/drawing/ScrollingText.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/world/Banner.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>

using namespace OpenRCT2;

TEST(WorldBannerMetadataTest, MissingPathMaterialCannotOverwriteBannerIdentity)
{
    for (const uint16_t banner : { uint16_t(0), uint16_t(9), uint16_t(255), uint16_t(8191), uint16_t(UINT16_MAX) })
    {
        const auto metadata = Ui::Gpu::PackWorldObjectMetadata(2, UINT16_MAX, banner);
        EXPECT_EQ(metadata >> 16, banner);
        EXPECT_EQ((metadata >> 8) & 255u, 255u);
        EXPECT_EQ(metadata & 255u, 2u);
    }
    const auto entrance = Ui::Gpu::PackWorldObjectMetadata(1, 22, UINT16_MAX);
    EXPECT_EQ(entrance, 0xFFFF1601u);
}

namespace
{
#include "../../data/shaders/vulkan/world_prop_rules.glsl"
    std::array<int, 256> objectFontGlyphs{};
    std::vector<uint32_t> objectFontText;
    int worldLargeCodepoint(int, int index)
    {
        return static_cast<int>(objectFontText.at(index));
    }
    int worldLargeGlyph(int, int codepoint)
    {
        return objectFontGlyphs.at(codepoint >= 0 && codepoint < 256 ? codepoint : 32);
    }
#include "../../data/shaders/vulkan/world_large_text_rules.glsl"
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

    std::array<uint8_t, 64 * 40> RasterColumns(
        const Drawing::ScrollingText::TextColumns& text, uint16_t mode, uint32_t tick,
        Drawing::PaletteIndex ink = Drawing::PaletteIndex::transparent)
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
            {
                auto pixel = text.columns[column][y];
                if (!text.initialInk.empty())
                {
                    const auto mask = text.initialInk[column];
                    if ((mask & (1u << y)) != 0)
                        pixel = EnumValue(ink);
#ifndef DISABLE_TTF
                    else if ((mask & (256u << y)) != 0)
                        pixel = EnumValue(Drawing::BlendColours(ink, Drawing::PaletteIndex::transparent));
#endif
                }
                pixels[x + (placement[x].y + y) * 64] = pixel;
            }
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

TEST_F(WorldBannerPublicationTest, InitialInkMatchesOriginalSignBitmapWithoutReplacingInlineColours)
{
    PaintSession session{};
    session.rt.zoom_level = ZoomLevel{ 0 };
    for (const std::string text : { "Gallery sign", "A{RED}B{GREEN}C", "{YELLOW}fixed colour" })
    {
        const auto columns = Drawing::ScrollingText::compileTextColumns(text, Drawing::PaletteIndex::transparent, true);
        ASSERT_EQ(columns.initialInk.size(), columns.columns.size());
        for (const auto colour : { Drawing::Colour::grey, Drawing::Colour::brightRed, Drawing::Colour::darkGreen })
        {
            const auto shades = Drawing::getColourMap(colour);
            for (const auto ink : { shades.midDark, shades.light })
                for (const uint16_t mode : { uint16_t(0), uint16_t(1), uint16_t(22), uint16_t(37) })
                    for (const uint32_t tick : { 0u, 119u, UINT32_MAX })
                    {
                        SCOPED_TRACE(text + " mode=" + std::to_string(mode) + " tick=" + std::to_string(tick));
                        getGameState().currentTicks = tick;
                        const auto image = Drawing::ScrollingText::setup(session, text, mode, ink);
                        const auto* source = GfxGetG1Element(image);
                        ASSERT_NE(source, nullptr);
                        const auto actual = RasterColumns(columns, mode, tick, ink);
                        EXPECT_TRUE(std::equal(actual.begin(), actual.end(), source->offset));
                    }
        }
    }
}

TEST_F(WorldBannerPublicationTest, PlainSignsAndParkNameRetainOwnedGenerations)
{
    auto* banner = CreateBanner();
    ASSERT_NE(banner, nullptr);
    banner->setText("Gallery");
    auto& park = getGameState().park;
    park.name = "First park";
    park.flags.set(ParkFlag::parkOpen);
    const auto first = Capture(true).bannerTexts;
    const auto id = banner->id.ToUnderlying();
    ASSERT_NE(first->plainBanners[id], nullptr);
    ASSERT_NE(first->parkEntrance, nullptr);
    const auto held = *first->parkEntrance;
    const auto packed = Ui::Gpu::BuildWorldBannerTextData(first);
    const auto plainDescriptor = packed->words[10] + id * 4;
    EXPECT_EQ(packed->words[plainDescriptor + 2], first->plainBanners[id]->columns.size());
    EXPECT_NE(packed->words[plainDescriptor + 3] >> 1, 0u);
    EXPECT_EQ(packed->words[packed->words[12] + 1], first->parkEntrance->phaseWidth);
    EXPECT_EQ(Capture().bannerTexts, first);
    park.name = "Second park";
    const auto renamed = Capture().bannerTexts;
    EXPECT_NE(*renamed->parkEntrance, held);
    EXPECT_EQ(renamed->plainBanners[id], first->plainBanners[id]);
    park.flags.unset(ParkFlag::parkOpen);
    const auto closed = Capture().bannerTexts;
    EXPECT_NE(*closed->parkEntrance, *renamed->parkEntrance);
    EXPECT_EQ(*first->parkEntrance, held);
    DeleteBanner(banner->id);
    EXPECT_EQ(Capture().bannerTexts->plainBanners[id], nullptr);
    EXPECT_FALSE(first->plainBanners[id]->initialInk.empty());
}

TEST(WorldBannerRulesTest, SignsRespectOriginalFaceSequenceDoorAndZoomContracts)
{
    for (int direction = 0; direction < 4; ++direction)
    {
        const auto visible = direction == 0 || direction == 3;
        EXPECT_EQ(worldPropScrollingMode(2, 0, 0, direction, 10, 0), visible ? 10 + ((direction + 1) & 3) : -1);
        EXPECT_EQ(worldPropScrollingMode(2, 1 << 4, 0, direction, 10, 0), -1);
        for (int sequence = 0; sequence < 8; ++sequence)
        {
            const auto expected = visible && ((sequence - 1) & 3) == direction ? 10 + ((direction + 1) & 3) : -1;
            EXPECT_EQ(worldPropScrollingMode(1, 0, sequence, direction, 10, 0), expected);
            EXPECT_EQ(worldPropScrollingMode(1, 0, sequence, direction, 10, 1), -1);
            EXPECT_EQ(worldPropScrollingMode(1, 1 << 2, sequence, direction, 10, 0), -1);
        }
        EXPECT_EQ(worldParkEntranceScrollingMode(direction, 0, false, 10), visible ? 10 + direction / 2 : -1);
        EXPECT_EQ(worldParkEntranceScrollingMode(direction, 0, true, 10), -1);
        EXPECT_EQ(worldParkEntranceScrollingMode(direction, 1, false, 10), -1);
        EXPECT_EQ(worldParkEntranceScrollingMode(direction, 0, false, 255), -1);
    }
}

TEST(WorldLargeTextRulesTest, OriginalOverflowGlyphAndNegativeHalfPixelPhaseArePreserved)
{
    objectFontGlyphs.fill(3 | (4 << 8) | (6 << 16));
    objectFontText = { 'A', 'B', 'C' };
    const auto layout = worldLargeLayout(0, 0, 3, 0, 5, 0, 0);
    EXPECT_EQ(layout.end0, 2); // The original display prefix includes the overshooting B.
    EXPECT_EQ(layout.total, 2);
    const auto left = worldLargeGlyphPosition(objectFontGlyphs['A'], 0, false, 10, -1, 8, 0);
    EXPECT_EQ(left.image, 14); // Original half-pixel alias variant, not a UI font glyph.
    EXPECT_EQ(left.x, 6);
    EXPECT_EQ(left.y, -3); // Floor(-5/2), not C++/GLSL truncation toward zero.
    const auto right = worldLargeGlyphPosition(objectFontGlyphs['A'], 3, false, 10, -1, 8, 0);
    EXPECT_EQ(right.image, 13);
    EXPECT_EQ(right.x, 6);
    EXPECT_EQ(right.y, 2);
    objectFontGlyphs[32] = 7 | (2 << 8) | (8 << 16);
    objectFontText = { 0x1234 };
    EXPECT_EQ(worldLargeMeasure(0, 0, 0, 1, false), 2); // Non-object codepoints use its space glyph.
}

TEST(WorldLargeTextRulesTest, TwoLinesAndVerticalAttachmentOrderMatchOriginalFontLayout)
{
    objectFontGlyphs.fill(3 | (4 << 8) | (6 << 16));
    objectFontText = { 'A', ' ', 'B', ' ', 'C' };
    const auto lines = worldLargeLayout(0, 0, 5, 2, 12, 5, 0);
    EXPECT_EQ(lines.first0, 0);
    EXPECT_EQ(lines.end0, 3);
    EXPECT_EQ(lines.first1, 4);
    EXPECT_EQ(lines.end1, 5);
    EXPECT_EQ(lines.y0, 3);
    EXPECT_EQ(lines.y1, 17);
    EXPECT_EQ(lines.total, 4);
    EXPECT_EQ(worldLargeLayout(0, 0, 5, 2, 2, 5, 0).total, 0); // No glyph fits the original two-line splitter.
    objectFontText = { 'A', 'B', 'C' };
    const auto vertical = worldLargeLayout(0, 0, 3, 1, 6, 2, 0);
    EXPECT_EQ(vertical.end0, 2);
    EXPECT_EQ(vertical.y0, -7);
    const auto glyph = worldLargeGlyphPosition(objectFontGlyphs['A'], 0, true, 10, vertical.y0, 8, 0);
    EXPECT_EQ(glyph.image, 6);
    EXPECT_EQ(glyph.x, 10);
    EXPECT_EQ(glyph.y, -4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(worldLargeTextOrdinal(i, 4, 0, false), i);
        EXPECT_EQ(worldLargeTextOrdinal(i, 4, 3, false), 3 - i);
        EXPECT_EQ(worldLargeTextOrdinal(i, 4, 0, true), 3 - i);
    }
    EXPECT_TRUE(worldLargeTextVisible(0, 0, 1, 1, 0));
    EXPECT_FALSE(worldLargeTextVisible(0, 0, 1, 2, 0));
    EXPECT_FALSE(worldLargeTextVisible(1, 0, 1, 0, 0));
    EXPECT_FALSE(worldLargeTextVisible(0, 0, 1, 0, 255));
    EXPECT_TRUE(worldLargeTextVisible(3, 4, 8, 0, 0));
    EXPECT_FALSE(worldLargeTextVisible(3, 1, 8, 0, 0));
}

TEST(WorldLargeTextCatalogTest, RetainsObjectGlyphAllocationAndRejectsUnownedImages)
{
    auto objects = std::make_unique<WorldObjectPresentationMaterials>();
    auto& material = objects->largeScenery[0];
    material.present = true;
    material.imageBase = 100;
    material.imageCount = 16;
    material.image = 108;
    material.tiles.resize(1);
    auto font = std::make_shared<LargeSceneryPresentationFont>();
    font->image = 100;
    font->numImages = 2;
    font->maxWidth = 27;
    font->offsets = { -7, 11, 13, -17 };
    font->glyphs['A'] = 1 | (5 << 8) | (9 << 16);
    material.font = font;
    std::vector<uint32_t> images;
    const auto catalog = Ui::Gpu::BuildWorldPropCatalog(*objects, 0, [&](uint32_t image) {
        images.push_back(image);
        return static_cast<uint32_t>(images.size() - 1);
    });
    const auto descriptor = catalog.words[catalog.words[1] + 12];
    ASSERT_NE(descriptor, 0u);
    EXPECT_EQ(catalog.words[descriptor], 8u);
    EXPECT_EQ(catalog.words[descriptor + 1], 8u);
    EXPECT_EQ(catalog.words[descriptor + 3], 27u);
    EXPECT_EQ(static_cast<int32_t>(catalog.words[descriptor + 4]), -7);
    EXPECT_EQ(catalog.words[descriptor + 8 + 'A'], font->glyphs['A']);
    ASSERT_EQ(images.size(), 16u);
    EXPECT_EQ(images[0], 108u);
    EXPECT_EQ(images[8], 100u);
    auto replacement = std::make_shared<LargeSceneryPresentationFont>(*font);
    replacement->image = 109;
    material.font = replacement;
    EXPECT_THROW(
        static_cast<void>(Ui::Gpu::BuildWorldPropCatalog(*objects, 0, [](uint32_t image) { return image; })),
        std::runtime_error);
    EXPECT_EQ(catalog.words[descriptor + 8 + 'A'], 1u | (5u << 8) | (9u << 16));
}

TEST_F(WorldBannerPublicationTest, ObjectFontTextKeepsOriginalFormattingLimitAndHeldGeneration)
{
    auto* banner = CreateBanner();
    ASSERT_NE(banner, nullptr);
    banner->setText("Mixed case");
    const auto id = banner->id.ToUnderlying();
    const auto first = Capture(true).bannerTexts;
    ASSERT_NE(first->objectFontText[id], nullptr);
    EXPECT_EQ(*first->objectFontText[id], (std::vector<uint32_t>{ 'M', 'i', 'x', 'e', 'd', ' ', 'c', 'a', 's', 'e' }));
    const auto packed = Ui::Gpu::BuildWorldBannerTextData(first);
    const auto descriptor = packed->words[15] + uint32_t(id) * 2;
    EXPECT_EQ(packed->words[descriptor + 1], 10u);
    EXPECT_EQ(packed->words[packed->words[descriptor]], uint32_t('M'));
    banner->setText(std::string(400, 'A'));
    const auto longText = Capture().bannerTexts;
    EXPECT_EQ(longText->objectFontText[id]->size(), 255u);
    EXPECT_EQ(first->objectFontText[id]->size(), 10u);
    DeleteBanner(banner->id);
    EXPECT_EQ(Capture().bannerTexts->objectFontText[id], nullptr);
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

TEST_F(WorldBannerPublicationTest, PausedScenePublishesCaptionAndQueueStatusWithoutTileOrTickChanges)
{
    auto& state = getGameState();
    auto& jobs = context->GetJobPool();
    state.currentTicks = 700;
    state.ridesEndOfUsedRange = 1;
    auto& ride = state.rides[0];
    ride.id = RideId::FromUnderlying(0);
    ride.type = RIDE_TYPE_LOOPING_ROLLER_COASTER;
    ride.customName = "Paused queue";
    state.park.flags.set(ParkFlag::parkOpen);
    // Exercise both the asynchronous native owner and the explicit synchronous
    // diagnostic owner; neither may discard acknowledged metadata at a held tick.
    for (const auto profile : { EntityPublicationProfile::gpuTerrainOnly, EntityPublicationProfile::legacyBulk })
    {
        SCOPED_TRACE(static_cast<int>(profile));
        const bool synchronous = profile == EntityPublicationProfile::legacyBulk;
        ride.status = RideStatus::closed;
        ride.flags.clearAll();
        state.park.name = "Before paused rename";
        PresentationScene scene;
        uint32_t draw = 1;
        ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, draw++, synchronous, profile));
        const auto held = scene.GetGeneration()->map;
        const auto heldText = *held->GetBannerTexts()->parkEntrance;
        ASSERT_EQ((*held->GetRidePoses()->records)[0].words[0], 0u);
        const auto advance = [&]() {
            if (!synchronous)
            {
                scene.ScheduleNext(jobs, state.entities);
                jobs.Join();
            }
            EXPECT_TRUE(scene.BeginFrame(jobs, state.entities, draw++, synchronous, profile));
            EXPECT_EQ(scene.GetGeneration()->sourceTick, 700u);
            return scene.GetGeneration()->map;
        };
        state.park.name = "After paused rename";
        const auto renamed = advance();
        EXPECT_NE(renamed, held);
        EXPECT_NE(*renamed->GetBannerTexts()->parkEntrance, heldText);
        EXPECT_EQ(renamed->GetSurfaceChunks(), held->GetSurfaceChunks());
        EXPECT_EQ(renamed->GetRideMaterials(), held->GetRideMaterials());
        ride.status = RideStatus::open;
        const auto opened = advance();
        EXPECT_EQ((*opened->GetRidePoses()->records)[0].words[0], 16u);
        EXPECT_EQ(opened->GetBannerTexts(), renamed->GetBannerTexts());
        ride.flags.set(RideFlag::brokenDown);
        const auto broken = advance();
        EXPECT_EQ((*broken->GetRidePoses()->records)[0].words[0], 24u);
        EXPECT_EQ(advance(), broken); // A fresh unchanged pose wrapper must not republish the map.
        EXPECT_EQ((*held->GetRidePoses()->records)[0].words[0], 0u);
        EXPECT_EQ(*held->GetBannerTexts()->parkEntrance, heldText);
        scene.Reset(jobs);
    }
}

TEST(WorldWallContactRulesTest, BuriedLatticeEdgesStayBetweenTheirAdjacentRoadFloors)
{
    // Kahuna Point parking bays: WALLLT32 atZ224, flat tarmac atZ256.
    // The white top edge must beat the floor behind it while the floor ahead
    // still hides its buried lattice. These are the original Paint.Wall bounds.
    constexpr std::array<CoordsXYZ, 4> originalContacts = { CoordsXYZ{ 1, 1, 1 }, { 2, 30, 1 }, { 30, 2, 1 }, { 1, 1, 1 } };
    for (int rotation = 0; rotation < 4; ++rotation)
        for (int worldDirection = 0; worldDirection < 4; ++worldDirection)
        {
            const int direction = (worldDirection + rotation) & 3;
            const auto parts = worldWallPropParts(1, 0, 4, direction, 0, 0, false, 0);
            ASSERT_EQ(parts.count, 1);
            const auto& wall = parts.parts[0];
            const auto& original = originalContacts[direction];
            EXPECT_EQ(wall.boundsX, original.x);
            EXPECT_EQ(wall.boundsY, original.y);
            EXPECT_EQ(wall.boundsZ, original.z);
            const int contact = 224 + wall.boundsX + wall.boundsY + wall.boundsZ;
            const bool nearEdge = direction == 1 || direction == 2;
            const int rearFloor = nearEdge ? 256 : 224;
            EXPECT_GT(contact, rearFloor);
            EXPECT_LT(contact, rearFloor + 32);
            // The fix preserves source imagery and height, rather than raising
            // all walls by their32-unit visible height.
            EXPECT_EQ(wall.z, 0);
            EXPECT_EQ(wall.imageOffset, (direction & 1) == 0 ? 1 : 0);
            const int oldRasterContact = 224 + wall.x + wall.y;
            EXPECT_GE(contact - oldRasterContact, 0);
            EXPECT_LE(contact - oldRasterContact, 3);
        }
    EXPECT_TRUE(worldPropHasEdgeContact(2));
    EXPECT_TRUE(worldPropHasEdgeContact(3));
    EXPECT_FALSE(worldPropHasEdgeContact(0));
    EXPECT_FALSE(worldPropHasEdgeContact(1));
}
