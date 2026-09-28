// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#include <openrct2/interface/ScreenshotTiling.h>
#include <openrct2/interface/ViewportGeneration.h>
#include <array>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

TEST(ViewportGenerationTest, DefaultDomainPreservesExistingTargetAndMutations)
{
    std::array<uint8_t, 4> pixels{ 3, 7, 11, 13 };
    RenderTarget target{};
    target.bits = reinterpret_cast<PaletteIndex*>(pixels.data());
    target.x = -17; target.y = -1488; target.width = 2; target.height = 2;
    {
        ScopedViewportGenerationTarget scope(target, std::nullopt);
        EXPECT_EQ(target.bits, reinterpret_cast<PaletteIndex*>(pixels.data()));
        EXPECT_EQ(target.y, -1488); EXPECT_EQ(target.height, 2);
        target.lastStringPos = { 47, 53 }; // An inactive scope must not undo legacy changes.
    }
    EXPECT_EQ(target.lastStringPos.x, 47); EXPECT_EQ(target.lastStringPos.y, 53);
    EXPECT_EQ(pixels, (std::array<uint8_t, 4>{ 3, 7, 11, 13 }));
}

TEST(ViewportGenerationTest, WholeHeightUsesSameTileCameraAndRestoresRasterOnException)
{
    for (int8_t zoom = 0; zoom <= 3; zoom++)
    {
        Viewport whole{};
        whole.width = 6016; whole.height = 3488; whole.viewPos = { -3009, -3537 };
        whole.zoom = ZoomLevel{ zoom }; whole.rotation = 2;
        const auto tile = ScreenshotTiling::TileViewport(whole, { 2048, 2048, 2048, 1440 });
        std::array<uint8_t, 1> guard{ 97 };
        RenderTarget raster{};
        raster.bits = reinterpret_cast<PaletteIndex*>(guard.data());
        raster.x = tile.zoom.ApplyInversedTo(tile.viewPos.x) - tile.pos.x;
        raster.y = tile.zoom.ApplyInversedTo(tile.viewPos.y) - tile.pos.y;
        raster.width = 32; raster.height = 1440; raster.pitch = 2016;
        raster.cullingX = raster.x; raster.cullingY = -50000;
        raster.cullingWidth = 32; raster.cullingHeight = 100000; raster.zoom_level = tile.zoom;
        const auto saved = raster;
        const ViewportGenerationBounds domain{ tile.zoom.ApplyInversedTo(tile.viewPos.y), tile.height };
        try
        {
            ScopedViewportGenerationTarget scope(raster, domain);
            EXPECT_EQ(raster.y, whole.zoom.ApplyInversedTo(whole.viewPos.y));
            EXPECT_EQ(raster.height, whole.height);
            EXPECT_EQ(raster.x, saved.x); EXPECT_EQ(raster.width, saved.width);
            EXPECT_EQ(raster.cullingY, saved.cullingY); EXPECT_EQ(raster.cullingHeight, saved.cullingHeight);
            EXPECT_EQ(raster.bits, nullptr);
            throw std::runtime_error("Synthetic generation failure");
        }
        catch (const std::runtime_error&) {}
        EXPECT_EQ(raster.bits, saved.bits); EXPECT_EQ(raster.x, saved.x); EXPECT_EQ(raster.y, saved.y);
        EXPECT_EQ(raster.width, saved.width); EXPECT_EQ(raster.height, saved.height); EXPECT_EQ(raster.pitch, saved.pitch);
        EXPECT_EQ(raster.zoom_level, saved.zoom_level); EXPECT_EQ(guard[0], 97);
    }
}

TEST(ViewportGenerationTest, InvalidGenerationDomainRejectsBeforeMutatingRaster)
{
    RenderTarget raster{};
    raster.x = 8; raster.y = -1488; raster.width = 32; raster.height = 1440;
    for (const auto bounds : { ViewportGenerationBounds{ -1487, 3488 }, ViewportGenerationBounds{ -3536, 100 },
             ViewportGenerationBounds{ -3536, 0 }, ViewportGenerationBounds{ 1, INT32_MAX } })
    {
        EXPECT_THROW(ScopedViewportGenerationTarget scope(raster, bounds), std::invalid_argument);
        EXPECT_EQ(raster.y, -1488); EXPECT_EQ(raster.height, 1440);
    }
}
