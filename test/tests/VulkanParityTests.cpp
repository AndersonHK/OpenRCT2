/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <cstdlib>
#include <gtest/gtest.h>
#include <string_view>

namespace
{
    bool RequiredVulkanParity()
    {
        const auto* value = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        return value != nullptr && std::string_view(value) == "1";
    }
} // namespace

#ifdef ENABLE_VULKAN

    #include "VulkanParityTestSupport.h"

    #include <SDL.h>
    #include <algorithm>
    #include <array>
    #include <filesystem>
    #include <fstream>
    #include <memory>
    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2-renderer/gpu/GpuTransparencyDepth.h>
    #include <openrct2-renderer/gpu/GpuWeatherDrawer.h>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanPlatform.h>
    #include <openrct2/Context.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/SpriteIds.h>
    #include <openrct2/core/Imaging.h>
    #include <openrct2/core/Json.hpp>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/Drawing.h>
    #include <openrct2/drawing/FilterPaletteIds.h>
    #include <openrct2/drawing/WeatherDrawer.h>
    #include <openrct2/drawing/X8DrawingEngine.h>
    #ifdef ENABLE_SCRIPTING
        #include <openrct2/scripting/ScriptEngine.h>
    #endif
    #include <span>
    #include <vector>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    using namespace OpenRCT2::Drawing;
    using namespace VulkanParitySupport;

    constexpr Gpu::Extent kExtent{ 96, 80 };
    constexpr size_t kPixelCount = kExtent.width * kExtent.height;
    constexpr uint64_t kFrameNumber = 271;

    struct PaletteAssets
    {
        std::unique_ptr<OpenRCT2::IContext> context;
        bool ownsAssets = false;
        ~PaletteAssets()
        {
            if (ownsAssets)
                GfxUnloadG2PalettesFontsTracks();
        }
    };

    std::array<std::byte, 1024> MakePalette()
    {
        std::array<std::byte, 1024> result{};
        for (size_t i = 0; i < 256; i++)
        {
            result[i * 4] = static_cast<std::byte>(i);
            result[i * 4 + 1] = static_cast<std::byte>(255 - i);
            result[i * 4 + 2] = static_cast<std::byte>((i * 37) & 255);
            result[i * 4 + 3] = std::byte{ 255 };
        }
        return result;
    }

    // These assets belong to the fixture, require no installed game data, and are
    // consumed independently by the frozen software renderer and GPU asset path.
    struct SyntheticSprites
    {
        std::array<OpenRCT2::G1Element, 5> saved{};
        std::vector<uint8_t> bitmap;
        std::vector<uint8_t> rle;
        std::vector<uint8_t> glyph;
        std::vector<uint8_t> water;
        std::vector<uint8_t> mask;
        bool noGraphics = gOpenRCT2NoGraphics;

        explicit SyntheticSprites(std::string_view fixture)
        {
            gOpenRCT2NoGraphics = false;
            for (size_t i = 0; i < saved.size(); i++)
                saved[i] = *GfxGetG1Element(SPR_TEMP_BEGIN + static_cast<uint32_t>(i));
            bitmap.resize(17 * 13);
            for (size_t i = 0; i < bitmap.size(); i++)
                bitmap[i] = i % 5 == 0 ? 0 : static_cast<uint8_t>(1 + (i * 17) % 254);
            OpenRCT2::G1Element element{ .offset = bitmap.data(),
                                         .width = 17,
                                         .height = 13,
                                         .xOffset = -3,
                                         .yOffset = -2,
                                         .flags = { OpenRCT2::G1Flag::hasTransparency } };
            if (fixture.starts_with("OpaqueZeroBitmap"))
                element.flags = {};
            GfxSetG1Element(SPR_TEMP_BEGIN, &element);
            rle.resize(13 * 2);
            for (size_t y = 0; y < 13; y++)
            {
                const auto offset = rle.size();
                rle[y * 2] = static_cast<uint8_t>(offset & 255);
                rle[y * 2 + 1] = static_cast<uint8_t>(offset >> 8);
                rle.push_back(0x80 | 13);
                rle.push_back(2);
                for (size_t x = 0; x < 13; x++)
                    rle.push_back(
                        fixture.starts_with("CoveredZeroRle") && (x + y) % 3 == 0
                            ? 0
                            : static_cast<uint8_t>(1 + (x * 11 + y * 23) % 254));
            }
            element.offset = rle.data();
            element.flags = { OpenRCT2::G1Flag::hasRLECompression };
            GfxSetG1Element(SPR_TEMP_BEGIN + 1, &element);
            glyph.resize(17 * 13);
            for (size_t i = 0; i < glyph.size(); i++)
                glyph[i] = static_cast<uint8_t>(i % 8);
            element.offset = glyph.data();
            element.flags = { OpenRCT2::G1Flag::hasTransparency };
            GfxSetG1Element(SPR_TEMP_BEGIN + 2, &element);
            water.resize(17 * 13);
            mask.resize(17 * 13);
            constexpr std::array<uint8_t, 5> maskValues{ 0, 255, 15, 240, 85 };
            for (size_t i = 0; i < water.size(); i++)
            {
                water[i] = static_cast<uint8_t>(i % 6);
                mask[i] = maskValues[i % maskValues.size()];
            }
            element.offset = water.data();
            GfxSetG1Element(SPR_TEMP_BEGIN + 3, &element);
            element.offset = mask.data();
            GfxSetG1Element(SPR_TEMP_BEGIN + 4, &element);
        }

        ~SyntheticSprites()
        {
            for (size_t i = 0; i < saved.size(); i++)
                GfxSetG1Element(SPR_TEMP_BEGIN + static_cast<uint32_t>(i), &saved[i]);
            gOpenRCT2NoGraphics = noGraphics;
        }
    };

    // Saturating darkening maps can conceal missing early peels. These fixture
    // inputs are noncommuting permutations of 1..255 (zero stays zero), read
    // independently by X8 and the GPU lookup upload, then restored on exit.
    struct SyntheticOrderingMaps
    {
        std::array<PaletteMap, 2> maps;
        std::array<std::array<PaletteIndex, 256>, 2> saved{};

        SyntheticOrderingMaps()
        {
            maps = { GetPaletteMapForColour(FilterPaletteID::paletteDarken1).value(),
                     GetPaletteMapForColour(FilterPaletteID::paletteDarken2).value() };
            for (size_t row = 0; row < maps.size(); row++)
                for (size_t i = 0; i < 256; i++)
                {
                    saved[row][i] = maps[row][i];
                    const auto mapped = i == 0 ? 0 : 1 + ((i - 1) * (row == 0 ? 2 : 7) + (row == 0 ? 17 : 59)) % 255;
                    maps[row][i] = static_cast<PaletteIndex>(mapped);
                }
        }

        ~SyntheticOrderingMaps()
        {
            for (size_t row = 0; row < maps.size(); row++)
                for (size_t i = 0; i < 256; i++)
                    maps[row][i] = saved[row][i];
        }
    };

    void DrawTransparencyOrdering(IDrawingContext& dc, RenderTarget& rt)
    {
        for (int32_t x = 0; x < static_cast<int32_t>(kExtent.width); x++)
            dc.FillRect(rt, static_cast<PaletteIndex>(1 + x * 2), x, 0, x, kExtent.height - 1);
        const auto glass = ImageId(SPR_TEMP_BEGIN + 1).WithTransparency(Colour::brightRed);
        const auto water = ImageId(SPR_TEMP_BEGIN + 3).WithRemap(FilterPaletteID::paletteWater).WithBlended(true);
        std::array<PaletteIndex, 8> glyphEntries{};
        for (size_t i = 1; i < glyphEntries.size(); i++)
            glyphEntries[i] = static_cast<PaletteIndex>(200 + i);
        const PaletteMap glyphMap(glyphEntries);
        for (int32_t i = 0; i < 4; i++)
        {
            const int32_t x = 9 + i * 17;
            const int32_t y = 7 + i * 13;
            dc.DrawGlyph(rt, ImageId(SPR_TEMP_BEGIN + 2).WithPrimary(Colour::black), x, y, glyphMap);
            dc.DrawSprite(rt, water, x + 3, y + 2);
            dc.FilterRect(rt, FilterPaletteID::paletteDarken1, x - 7, y - 3, x + 24, y + 18);
            dc.DrawSprite(rt, glass, x + 5, y + 5);
            dc.DrawSprite(rt, ImageId(SPR_TEMP_BEGIN), x + 6, y + 4);
            dc.DrawLine(rt, static_cast<PaletteIndex>(230 - i), { { x - 5, y + 11 }, { x + 25, y + 11 } });
            dc.FillRect(rt, static_cast<PaletteIndex>(155 + i), x + 8, y + 4, x + 12, y + 20);
            dc.DrawSprite(rt, water, x + 1, y + 8);
            dc.DrawGlyph(rt, ImageId(SPR_TEMP_BEGIN + 2).WithPrimary(Colour::black), x + 9, y + 11, glyphMap);
            dc.FilterRect(rt, FilterPaletteID::paletteDarken2, x + 4, y + 7, x + 26, y + 24);
            dc.DrawSprite(rt, glass, x + 12, y + 13);
        }
        auto clipped = rt.Crop({ 17, 13 }, { 53, 49 });
        clipped.x = -11;
        clipped.y = -9;
        dc.FilterRect(clipped, FilterPaletteID::paletteDarken1, -20, -14, 47, 42);
        dc.DrawSprite(clipped, water, -12, -10);
        dc.DrawGlyph(clipped, ImageId(SPR_TEMP_BEGIN + 2).WithPrimary(Colour::black), 27, 28, glyphMap);
    }

    void DrawFixture(std::string_view name, IDrawingContext& dc, RenderTarget& rt)
    {
        dc.Clear(rt, static_cast<PaletteIndex>(7));
        if (name == "TransparencyOrdering" || name == "WeatherTransparencyOrdering")
        {
            DrawTransparencyOrdering(dc, rt);
        }
        else if (name.starts_with("TransparencyDeep"))
        {
            for (int32_t x = 0; x < static_cast<int32_t>(kExtent.width); x++)
                dc.FillRect(rt, static_cast<PaletteIndex>(1 + x * 2), x, 0, x, kExtent.height - 1);
            const int32_t layers = name == "TransparencyDeep32" ? 32 : 33;
            for (int32_t i = 0; i < layers; i++)
            {
                if (i == 11 || i == 23)
                {
                    dc.FillRect(rt, static_cast<PaletteIndex>(81 + i), 48, 10, 55, 69);
                    dc.DrawSprite(rt, ImageId(SPR_TEMP_BEGIN + 1), 31, 34);
                }
                dc.FilterRect(
                    rt, i % 3 == 0 ? FilterPaletteID::paletteDarken2 : FilterPaletteID::paletteDarken1, 8 + i % 5, 8 + i % 7,
                    87 - i % 9, 71 - i % 11);
            }
        }
        else if (name.starts_with("Weather"))
        {
            for (int32_t x = 0; x < static_cast<int32_t>(kExtent.width); x++)
                dc.FillRect(rt, static_cast<PaletteIndex>(30 + x), x, 0, x, kExtent.height - 1);
        }
        else if (name == "Rectangles" || name == "Hatches")
        {
            const bool hatch = name == "Hatches";
            dc.FillRect(rt, static_cast<PaletteIndex>(21), -5, -3, 15, 14, hatch);
            dc.FillRect(rt, static_cast<PaletteIndex>(63), 20, 7, 42, 19, hatch);
            dc.FillRect(rt, static_cast<PaletteIndex>(147), 95, 79, 105, 90, hatch);
            dc.FillRect(rt, static_cast<PaletteIndex>(199), 46, 0, 46, 0, hatch);
            dc.FillRect(rt, static_cast<PaletteIndex>(99), 30, 25, 29, 40, hatch);
            if (!hatch)
            {
                dc.FillRect(rt, PaletteIndex::transparent, 65, 12, 87, 25);
                auto cleared = rt.Crop({ 65, 34 }, { 17, 13 });
                dc.Clear(cleared, PaletteIndex::transparent);
            }
            auto clipped = rt.Crop({ 13, 29 }, { 47, 31 });
            dc.FillRect(clipped, static_cast<PaletteIndex>(112), 10, 26, 68, 51, hatch);
            auto nested = clipped.Crop({ 7, 5 }, { 21, 17 });
            nested.x = 20;
            nested.y = 34;
            dc.FillRect(nested, static_cast<PaletteIndex>(213), 16, 31, 36, 46, hatch);
        }
        else if (name == "Filters")
        {
            for (int32_t x = 0; x < static_cast<int32_t>(kExtent.width); x++)
                dc.FillRect(rt, static_cast<PaletteIndex>(1 + x * 2), x, 0, x, kExtent.height - 1);
            dc.FilterRect(rt, FilterPaletteID::paletteDarken1, -7, -3, 55, 49);
            dc.FillRect(rt, static_cast<PaletteIndex>(172), 26, 24, 64, 43);
            dc.FilterRect(rt, FilterPaletteID::paletteDarken2, 35, 17, 91, 68);
            dc.FilterRect(rt, FilterPaletteID::paletteDarken1, 41, 25, 74, 58);
            auto clipped = rt.Crop({ 13, 51 }, { 47, 21 });
            dc.FilterRect(clipped, FilterPaletteID::paletteDarken2, 5, 43, 79, 78);
        }
        else if (name == "Lines")
        {
            constexpr std::array<ScreenLine, 12> lines = { {
                { { 1, 1 }, { 33, 1 } },
                { { 3, 3 }, { 3, 31 } },
                { { 5, 5 }, { 31, 19 } },
                { { 31, 23 }, { 5, 9 } },
                { { 9, 35 }, { 21, 5 } },
                { { 24, 5 }, { 12, 35 } },
                { { -13, 9 }, { 39, 27 } },
                { { 42, -11 }, { 51, 40 } },
                { { 78, 20 }, { 103, 69 } },
                { { 55, 68 }, { 87, 93 } },
                { { 70, 10 }, { 70, 10 } },
                { { 55, 3 }, { 56, 3 } },
            } };
            for (size_t i = 0; i < lines.size(); i++)
                dc.DrawLine(rt, static_cast<PaletteIndex>(30 + i * 15), lines[i]);
            auto clipped = rt.Crop({ 17, 43 }, { 41, 27 });
            dc.DrawLine(clipped, static_cast<PaletteIndex>(244), { { 8, 45 }, { 74, 68 } });
        }
        else if (name.starts_with("OpaqueZeroBitmap") || name.starts_with("CoveredZeroRle"))
        {
            const ImageId image(SPR_TEMP_BEGIN + (name.starts_with("CoveredZeroRle") ? 1 : 0));
            if (name.ends_with("Ordering"))
            {
                dc.FillRect(rt, static_cast<PaletteIndex>(181), 0, 0, 95, 79);
                dc.FilterRect(rt, FilterPaletteID::paletteDarken1, 0, 0, 47, 79);
                dc.DrawSprite(rt, image, 17, 13);
                dc.DrawSprite(rt, image, 57, 13);
                dc.FilterRect(rt, FilterPaletteID::paletteDarken1, 48, 0, 95, 30);
                dc.FillRect(rt, static_cast<PaletteIndex>(211), 21, 9, 24, 19);
                auto clipped = rt.Crop({ 11, 31 }, { 68, 43 }).Crop({ 6, 6 }, { 51, 31 });
                clipped.x = 17;
                clipped.y = 37;
                dc.DrawSprite(clipped, image, 16, 37);
                clipped.zoom_level = ZoomLevel{ 1 };
                dc.DrawSprite(clipped, image, 49, 43);
                clipped.zoom_level = ZoomLevel{ -1 };
                dc.DrawSprite(clipped, image, 38, 27);
                return;
            }
            for (int32_t y = 0; y < 3; y++)
                for (int32_t x = 0; x < 4; x++)
                {
                    if (name.ends_with("Solid"))
                        dc.DrawSpriteSolid(rt, image, x * 27, y * 29, static_cast<PaletteIndex>(x == 0 ? 0 : 203));
                    else
                        dc.DrawSprite(
                            rt, name.ends_with("Remapped") ? image.WithPrimary(Colour::brightRed) : image, x * 27, y * 29);
                }
        }
        else if (name == "SolidSprites")
        {
            for (int32_t y = 0; y < 2; y++)
                for (int32_t x = 0; x < 4; x++)
                    dc.DrawSpriteSolid(
                        rt, ImageId(SPR_TEMP_BEGIN + y), x * 27, 9 + y * 27, static_cast<PaletteIndex>(203 + y * 12));
            auto clipped = rt.Crop({ 17, 48 }, { 39, 25 });
            clipped.x = -17;
            clipped.y = -11;
            dc.DrawSpriteSolid(clipped, ImageId(SPR_TEMP_BEGIN + 1), -21, -15, static_cast<PaletteIndex>(181));
        }
        else if (name == "Glyphs")
        {
            const ImageId image(SPR_TEMP_BEGIN + 2);
            std::array<PaletteIndex, 8> entries{};
            for (size_t i = 1; i < entries.size(); i++)
                entries[i] = static_cast<PaletteIndex>(200 + i);
            const PaletteMap map(entries);
            for (int32_t x = 0; x < 4; x++)
            {
                dc.DrawGlyph(rt, image, x * 27, 7, map);
                dc.DrawGlyph(rt, image.WithPrimary(Colour::black), x * 27, 29, map);
            }
            entries[1] = PaletteIndex::transparent;
            for (size_t i = 2; i < entries.size(); i++)
                entries[i] = static_cast<PaletteIndex>(130 + i);
            for (int32_t x = 0; x < 4; x++)
                dc.DrawGlyph(rt, image.WithPrimary(Colour::black), x * 27, 54, map);
            auto clipped = rt.Crop({ 23, 59 }, { 41, 17 });
            clipped.x = -7;
            clipped.y = -3;
            dc.DrawGlyph(clipped, image.WithPrimary(Colour::black), -9, -5, map);
        }
        else if (name == "NestedTargets")
        {
            auto first = rt.Crop({ 11, 13 }, { 63, 55 });
            first.x = -17;
            first.y = -23;
            dc.FillRect(first, static_cast<PaletteIndex>(43), -21, -29, 11, 9, true);
            auto second = first.Crop({ 9, 7 }, { 31, 29 });
            second.x = 42;
            second.y = -39;
            dc.Clear(second, static_cast<PaletteIndex>(81));
            dc.DrawSprite(second, ImageId(SPR_TEMP_BEGIN), 43, -37);
            dc.DrawLine(second, static_cast<PaletteIndex>(111), { { 33, -43 }, { 89, -12 } });
            dc.FilterRect(second, FilterPaletteID::paletteDarken1, 39, -45, 77, -21);
            auto third = second.Crop({ 3, 4 }, { 17, 19 });
            third.zoom_level = ZoomLevel{ 1 };
            third.x = -3;
            third.y = -7;
            dc.DrawSprite(third, ImageId(SPR_TEMP_BEGIN + 1), -9, -17);
        }
        else if (name == "Remaps")
        {
            const ImageId bitmap(SPR_TEMP_BEGIN);
            const ImageId rle(SPR_TEMP_BEGIN + 1);
            for (int32_t y = 0; y < 3; y++)
            {
                const auto image = y == 1 ? bitmap : rle;
                dc.DrawSprite(rt, image.WithPrimary(Colour::brightRed), 9, 7 + y * 23);
                dc.DrawSprite(rt, image.WithPrimary(Colour::brightRed).WithSecondary(Colour::lightBlue), 35, 7 + y * 23);
                dc.DrawSprite(
                    rt, image.WithPrimary(Colour::brightRed).WithSecondary(Colour::lightBlue).WithTertiary(Colour::yellow), 61,
                    7 + y * 23);
            }
            auto clipped = rt.Crop({ 74, 31 }, { 19, 31 });
            clipped.x = -13;
            clipped.y = -9;
            dc.DrawSprite(clipped, rle.WithPrimary(Colour::lightBlue), -16, -12);
        }
        else if (name == "Masks" || name == "MasksZoomIn")
        {
            auto target = rt;
            target.zoom_level = name == "MasksZoomIn" ? ZoomLevel{ -1 } : ZoomLevel{ 0 };
            for (int32_t y = 0; y < 3; y++)
                for (int32_t x = 0; x < 4; x++)
                    dc.DrawSpriteRawMasked(target, x * 19, y * 17, ImageId(SPR_TEMP_BEGIN + 4), ImageId(SPR_TEMP_BEGIN));
            auto clipped = target.Crop({ 17, 23 }, { 47, 29 });
            clipped.x = -13;
            clipped.y = -9;
            dc.DrawSpriteRawMasked(clipped, -17, -13, ImageId(SPR_TEMP_BEGIN + 4), ImageId(SPR_TEMP_BEGIN));
        }
        else if (name == "Transparency")
        {
            for (int32_t x = 0; x < static_cast<int32_t>(kExtent.width); x++)
                dc.FillRect(rt, static_cast<PaletteIndex>(1 + x * 2), x, 0, x, kExtent.height - 1);
            const auto glass = ImageId(SPR_TEMP_BEGIN + 1).WithTransparency(Colour::brightRed);
            const auto water = ImageId(SPR_TEMP_BEGIN + 3).WithRemap(FilterPaletteID::paletteWater).WithBlended(true);
            for (int32_t i = 0; i < 12; i++)
            {
                dc.DrawSprite(rt, glass, 9 + i * 4, 8 + i * 3);
                dc.DrawSprite(rt, water, 14 + i * 4, 13 + i * 3);
                if (i % 3 == 0)
                    dc.FillRect(rt, static_cast<PaletteIndex>(101 + i), 22 + i * 3, 18 + i * 2, 36 + i * 3, 25 + i * 2);
            }
            auto clipped = rt.Crop({ 59, 37 }, { 29, 31 });
            clipped.x = -13;
            clipped.y = -9;
            dc.DrawSprite(clipped, glass, -15, -11);
            dc.FilterRect(clipped, FilterPaletteID::paletteDarken1, -20, -17, 20, 16);
            dc.DrawSprite(clipped, water, -8, -2);
        }
        else
        {
            if (name.ends_with("ZoomIn"))
                rt.zoom_level = ZoomLevel{ -1 };
            else if (name.ends_with("ZoomOut"))
                rt.zoom_level = ZoomLevel{ 1 };
            const ImageId image(SPR_TEMP_BEGIN + (name.starts_with("Rle") ? 1 : 0));
            for (int32_t y = 0; y < 3; y++)
                for (int32_t x = 0; x < 4; x++)
                    dc.DrawSprite(rt, image, x * 27, y * 29);
            auto clipped = rt.Crop({ 15, 16 }, { 46, 39 });
            dc.DrawSprite(clipped, image, 14, 17);
            dc.DrawSpriteSolid(clipped, image, 40, 35, static_cast<PaletteIndex>(203));
            std::array<PaletteIndex, 8> glyphPalette{};
            for (size_t i = 1; i < glyphPalette.size(); i++)
                glyphPalette[i] = static_cast<PaletteIndex>(255 - i);
            const PaletteMap map(glyphPalette);
            const ImageId glyph(SPR_TEMP_BEGIN + 2);
            dc.DrawGlyph(rt, glyph, 70, 41, map);
            dc.DrawGlyph(rt, glyph.WithPrimary(Colour::black), 70, 58, map);
            if (name.ends_with("ZoomIn") || name.ends_with("ZoomOut"))
            {
                auto negative = rt.Crop({ 41, 27 }, { 31, 29 });
                negative.x = -13;
                negative.y = -9;
                dc.DrawSprite(negative, image, -17, -13);
                dc.DrawSpriteSolid(negative, image, -2, 4, static_cast<PaletteIndex>(197));
            }
        }
    }

    void DrawWeatherFixture(std::string_view name, IWeatherDrawer& drawer, RenderTarget& rt)
    {
        const auto* pattern = name == "WeatherSnow" ? kSnowPattern : kRainPattern;
        drawer.Draw(rt, 0, 0, kExtent.width, kExtent.height, -113, -227, pattern);
        drawer.Draw(rt, 13, 17, 67, 41, 37, -65, pattern);
        drawer.Draw(rt, 95, 79, 1, 1, 0, 0, pattern);
    }

    class VulkanParityTest : public testing::TestWithParam<const char*>
    {
    protected:
        bool ownsVideo = false;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{ nullptr, SDL_DestroyWindow };
        std::unique_ptr<Gpu::Backend> backend;
        std::filesystem::path shaders;

        std::string Initialise()
        {
            ownsVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
            if (ownsVideo && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                return SDL_GetError();
            window.reset(SDL_CreateWindow(
                "OpenRCT2 frozen software parity", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kExtent.width,
                kExtent.height, SDL_WINDOW_HIDDEN | Vulkan::Platform::GetRequiredSdlWindowFlags()));
            if (!window)
                return SDL_GetError();
            std::vector<std::filesystem::path> candidates{ std::filesystem::current_path() / "data/shaders/vulkan" };
            if (char* base = SDL_GetBasePath())
            {
                candidates.emplace_back(std::filesystem::path(base) / "data/shaders/vulkan");
                SDL_free(base);
            }
            if (const auto* overridePath = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                candidates.insert(candidates.begin(), overridePath);
            for (const auto& path : candidates)
                if (std::filesystem::is_regular_file(path / "indexed_palette.frag.spv"))
                {
                    shaders = path;
                    break;
                }
            if (shaders.empty())
                return "Compiled Vulkan shaders unavailable";
            const auto drawable = Vulkan::Platform::GetDrawableExtent(window.get());
            if (drawable.width != kExtent.width || drawable.height != kExtent.height)
                return "The primitive fixture requires an unscaled 96x80 drawable";
            Gpu::BackendConfig config{ .logicalExtent = kExtent,
                                       .drawableExtent = kExtent,
                                       .presentMode = Gpu::PresentMode::Immediate,
                                       .frameAcquireMode = Gpu::FrameAcquireMode::Wait,
                                       .shaderDirectory = shaders.string() };
            config.enableDiagnosticCapture = true;
            backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(window.get()));
            try
            {
                backend->Initialise(config);
            }
            catch (const std::exception& error)
            {
                return error.what();
            }
            return {};
        }

        void TearDown() override
        {
            backend.reset();
            window.reset();
            if (ownsVideo)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    };
} // namespace

TEST_P(VulkanParityTest, FrozenSoftwarePixels)
{
    const auto unavailable = Initialise();
    if (!unavailable.empty())
    {
        if (RequiredVulkanParity())
            FAIL() << "Required Vulkan parity unavailable: " << unavailable;
        GTEST_SKIP() << unavailable;
    }
    SyntheticSprites sprites(GetParam());
    PaletteAssets assets;
    std::filesystem::path palettePath;
    {
        const auto dataDirectory = shaders.parent_path().parent_path();
        for (const auto* filename : { "g2.dat", "palettes.dat", "fonts.dat", "tracks.dat" })
        {
            if (!std::filesystem::is_regular_file(dataDirectory / filename))
            {
                if (RequiredVulkanParity())
                    FAIL() << "Required primitive fixture needs bundled asset " << (dataDirectory / filename);
                GTEST_SKIP() << "Primitive fixture needs bundled asset " << (dataDirectory / filename);
            }
        }
        ASSERT_EQ(OpenRCT2::GetContext(), nullptr) << "Do not overwrite another fixture's resource context";
        assets.context = OpenRCT2::CreateContext();
    #ifdef ENABLE_SCRIPTING
        // Context teardown unregisters the static scripting classes. Give this
        // lightweight resource context its own class/runtime lifecycle too,
        // including when a preceding scene fixture initialised scripting.
        assets.context->GetScriptEngine().Initialise();
    #endif
        assets.context->GetPlatformEnvironment().SetBasePath(OpenRCT2::DirBase::openrct2, dataDirectory.string());
        palettePath = dataDirectory / "palettes.dat";
        GfxLoadG2PalettesFontsTracks();
        assets.ownsAssets = true;
        ASSERT_TRUE(GetPaletteMapForColour(FilterPaletteID::paletteNull).has_value());
        ASSERT_TRUE(GetPaletteMapForColour(FilterPaletteID::paletteDarken1).has_value());
    }

    std::unique_ptr<SyntheticOrderingMaps> orderingMaps;
    if (std::string_view(GetParam()).starts_with("TransparencyDeep"))
        orderingMaps = std::make_unique<SyntheticOrderingMaps>();

    const auto palette = MakePalette();
    backend->SetPalette(palette);
    std::vector<std::byte> remap(256 * 256);
    for (size_t i = 0; i < 256; i++)
        remap[i] = static_cast<std::byte>(i);
    for (int32_t i = 0; i < kPaletteTotalOffsets; i++)
    {
        const auto id = static_cast<FilterPaletteID>(i);
        const auto index = GetPaletteG1Index(id);
        ASSERT_TRUE(index.has_value());
        const auto* element = GfxGetG1Element(*index);
        ASSERT_NE(element, nullptr);
        ASSERT_FALSE(element->flags.has(OpenRCT2::G1Flag::hasRLECompression));
        const auto row = Gpu::TextureCache::PaletteToY(id);
        for (int32_t y = 0; y < std::min<int32_t>(element->height, 256 - row); y++)
            for (int32_t x = 0; x < std::min<int32_t>(element->width, 256); x++)
                remap[(row + y) * 256 + x] = static_cast<std::byte>(element->offset[y * element->width + x]);
    }
    backend->SetRemapPalette(remap);
    backend->SetBlendPalette(remap);

    std::vector<PaletteIndex> reference(kPixelCount);
    std::vector<PaletteIndex> addressSpace(kPixelCount);
    RenderTarget softwareTarget{ .bits = reference.data(), .width = kExtent.width, .height = kExtent.height };
    RenderTarget gpuTarget{ .bits = addressSpace.data(), .width = kExtent.width, .height = kExtent.height };
    X8DrawingContext software(nullptr);
    software.BeginDraw();
    DrawFixture(GetParam(), software, softwareTarget);
    software.EndDraw();
    if (std::string_view(GetParam()).starts_with("Weather"))
    {
        X8WeatherDrawer weather;
        DrawWeatherFixture(GetParam(), weather, softwareTarget);
        EXPECT_EQ(reference.back(), static_cast<PaletteIndex>(std::string_view(GetParam()) == "WeatherSnow" ? 32 : 12));
        EXPECT_GT(std::count(reference.begin(), reference.end(), static_cast<PaletteIndex>(16)), 0)
            << "Weather must paint its third pattern row over the background";
    }
    EXPECT_GT(
        std::count_if(
            reference.begin(), reference.end(), [](PaletteIndex value) { return value != static_cast<PaletteIndex>(7); }),
        0)
        << "A parity fixture must draw observable pixels";
    if (std::string_view(GetParam()) == "Glyphs")
    {
        for (const auto value : { 1, 201, 132 })
            EXPECT_GT(std::count(reference.begin(), reference.end(), static_cast<PaletteIndex>(value)), 0)
                << "Glyph fixture must exercise raw, remapped and changed-palette pixels";
    }
    if (std::string_view(GetParam()).ends_with("Raw"))
        EXPECT_GT(std::count(reference.begin(), reference.end(), PaletteIndex::transparent), 0)
            << "Opaque zero fixture must overwrite background with index zero";

    Gpu::TextureCache textures;
    Gpu::FrameCommandStream commands;
    Gpu::CommandDrawingContext recorder(gpuTarget, textures);
    textures.BeginFrame();
    recorder.Begin(commands);
    DrawFixture(GetParam(), recorder, gpuTarget);
    recorder.End();
    if (std::string_view(GetParam()).starts_with("Weather"))
    {
        Gpu::WeatherDrawer weather;
        weather.SetCommands(commands.weather);
        DrawWeatherFixture(GetParam(), weather, gpuTarget);
        ASSERT_GT(commands.weather.size(), 0u);
    }
    const auto lease = textures.SealFrame(commands);
    const auto transparencyDepth = Gpu::MaxTransparencyDepth(commands.transparentRects);
    if (std::string_view(GetParam()).starts_with("TransparencyDeep"))
    {
        const auto expectedDepth = std::string_view(GetParam()) == "TransparencyDeep32" ? 32u : 33u;
        EXPECT_EQ(commands.transparentRects.size(), expectedDepth);
        EXPECT_EQ(transparencyDepth, expectedDepth);
    }
    size_t orderingLineSpanCount = 0;
    if (std::string_view(GetParam()).ends_with("TransparencyOrdering"))
    {
        const auto rectangles = std::span(commands.opaqueRects.data(), commands.opaqueRects.size());
        for (int32_t i = 0; i < 4; i++)
        {
            // DrawLine preserves the X8 endpoint contract through untextured
            // horizontal spans. Verify each actual span, not a native-line batch.
            const auto isLineSpan = [i](const Gpu::RectCommand& command) {
                return command.flags == Gpu::RectCommand::FLAG_NO_TEXTURE && command.colour == static_cast<uint32_t>(230 - i);
            };
            const auto count = std::count_if(rectangles.begin(), rectangles.end(), isLineSpan);
            EXPECT_EQ(count, 1) << "Missing/duplicate line span " << i;
            orderingLineSpanCount += static_cast<size_t>(count);
            const auto span = std::find_if(rectangles.begin(), rectangles.end(), isLineSpan);
            if (span == rectangles.end())
                continue;
            const int32_t x = 9 + i * 17;
            const int32_t y = 7 + i * 13;
            EXPECT_EQ(span->bounds.x, x - 5);
            EXPECT_EQ(span->bounds.y, y + 11);
            EXPECT_EQ(span->bounds.z, x + 25);
            EXPECT_EQ(span->bounds.w, y + 12);
            const auto occluder = std::find_if(rectangles.begin(), rectangles.end(), [i](const Gpu::RectCommand& command) {
                return command.flags == Gpu::RectCommand::FLAG_NO_TEXTURE && command.colour == static_cast<uint32_t>(155 + i)
                    && command.bounds.x == 17 + i * 17 && command.bounds.y == 11 + i * 13;
            });
            ASSERT_NE(occluder, rectangles.end());
            EXPECT_GT(occluder->depth, span->depth) << "The next opaque rectangle must cover the earlier line";
            const auto transparent = std::span(commands.transparentRects.data(), commands.transparentRects.size());
            for (const bool earlier : { true, false })
                EXPECT_TRUE(std::any_of(transparent.begin(), transparent.end(), [&](const Gpu::RectCommand& command) {
                    return (earlier ? command.depth < span->depth : command.depth > span->depth)
                        && command.bounds.x < span->bounds.z && command.bounds.z > span->bounds.x
                        && command.bounds.y < span->bounds.w && command.bounds.w > span->bounds.y;
                })) << "Each line must overlap transparent work both before and after it";
        }
        EXPECT_EQ(orderingLineSpanCount, 4u);
        EXPECT_GT(commands.opaqueSprites.size(), 0u);
        EXPECT_GT(commands.transparentRects.size(), 16u);
        EXPECT_GT(transparencyDepth, 4u);
    }
    const auto frame = backend->BeginFrame(kFrameNumber);
    ASSERT_TRUE(frame.has_value()) << "Required fixture frame acquisition failed";
    ASSERT_TRUE(backend->RequestFrameCapture(*frame));
    backend->Submit(*frame, commands);
    backend->Present(*frame);
    backend->WaitIdle();
    textures.RetireFrame(lease, Gpu::FrameRetirement::Presented);
    textures.DrainFrameRetirements();

    std::filesystem::path artifacts;
    if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
        artifacts = path;
    json_t metadata{ { "frameNumber", kFrameNumber },
                     { "softwareReference", "frozen source; see renderer freeze manifest" },
                     { "shaderDirectory", shaders.string() },
                     { "sdlVideoDriver", SDL_GetCurrentVideoDriver() },
                     { "lighting", "disabled" },
                     { "scale", "1:1 nearest" },
                     { "fixtureState", "synthetic immutable inputs; no simulation or wall clock" } };
    metadata["inputHashAlgorithm"] = "FNV-1a-64";
    metadata["transparentCommandCount"] = commands.transparentRects.size();
    metadata["maximumTransparentOverlap"] = transparencyDepth;
    metadata["lineCommandCount"] = commands.lines.size();
    metadata["orderingLineSpanCount"] = orderingLineSpanCount;
    metadata["opaqueSpriteCommandCount"] = commands.opaqueSprites.size();
    metadata["weatherCommandCount"] = commands.weather.size();
    if (orderingMaps != nullptr)
        metadata["syntheticOrderingMaps"] = "index0->0; others:1+((i-1)*2+17)%255 and 1+((i-1)*7+59)%255";
    metadata["shaderHashes"] = json_t::object();
    for (const auto& entry : std::filesystem::directory_iterator(shaders))
        if (entry.path().extension() == ".spv")
            metadata["shaderHashes"][entry.path().filename().string()] = HashFile(entry.path());
    if (!palettePath.empty())
        metadata["paletteAssetHash"] = HashFile(palettePath);
    std::vector<std::byte> indexed(kPixelCount);
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(kExtent, indexed));
    const auto expected = std::as_bytes(std::span(reference));
    EXPECT_EQ(CompareAndReport(artifacts, GetParam(), "indexed", expected, indexed, 1, palette, metadata, kExtent), 0u)
        << "Indexed divergence: inspect software.png, vulkan.png, diff.png and report.json";

    const auto capture = backend->ReadbackFrameRgba(kFrameNumber);
    ASSERT_TRUE(capture.has_value());
    ASSERT_EQ(capture->frameNumber, kFrameNumber);
    ASSERT_EQ(capture->logicalExtent, kExtent);
    ASSERT_EQ(capture->drawableExtent, kExtent);
    ASSERT_EQ(capture->rgba.size(), kPixelCount * 4);
    metadata["paletteVersion"] = capture->paletteVersion;
    metadata["swapchainGeneration"] = capture->swapchainGeneration;
    metadata["sourceFormat"] = capture->sourceFormat;
    metadata["deviceName"] = capture->deviceName;
    metadata["vendorId"] = capture->vendorId;
    metadata["deviceId"] = capture->deviceId;
    metadata["driverVersion"] = capture->driverVersion;
    EXPECT_EQ(
        CompareAndReport(
            artifacts, GetParam(), "rgba", Expand(expected, palette), capture->rgba, 4, palette, metadata, kExtent),
        0u)
        << "Final output divergence: inspect software.png, vulkan.png, diff.png and report.json";
}

TEST(VulkanCoverageCacheTest, MaskAllocationFailureRollsBackTheColourAllocation)
{
    SyntheticSprites sprites("OpaqueZeroBitmapRaw");
    std::vector<uint8_t> pixels(2048 * 2048, 0);
    OpenRCT2::G1Element element{ .offset = pixels.data(), .width = 2048, .height = 2048 };
    GfxSetG1Element(SPR_TEMP_BEGIN, &element);
    Gpu::TextureCache textures(1);
    textures.BeginFrame();
    EXPECT_THROW((void)textures.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{}), std::runtime_error);
    std::fill(pixels.begin(), pixels.end(), 31);
    const auto retry = textures.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{});
    ASSERT_TRUE(retry.has_value());
    EXPECT_FALSE(retry->zeroCoverage.has_value());
    Gpu::FrameCommandStream commands;
    const auto lease = textures.SealFrame(commands);
    ASSERT_EQ(commands.textureUploads.size(), 1u);
    EXPECT_EQ(commands.textureUploads.front().pixels.size(), pixels.size());
    textures.RetireFrame(lease, Gpu::FrameRetirement::Presented);
    textures.DrainFrameRetirements();
}

TEST(VulkanCoverageCacheTest, BothAllocationsRemainPinnedAcrossInvalidation)
{
    SyntheticSprites sprites("OpaqueZeroBitmapRaw");
    std::vector<uint8_t> pixels(1024 * 1024, 31);
    pixels[0] = 0;
    OpenRCT2::G1Element element{ .offset = pixels.data(), .width = 1024, .height = 1024 };
    for (uint32_t i = 0; i < 3; i++)
        GfxSetG1Element(SPR_TEMP_BEGIN + i, &element);
    Gpu::TextureCache textures(1);
    std::array<Gpu::AtlasResidencyToken, 2> leases{};
    for (uint32_t i = 0; i < 2; i++)
    {
        textures.BeginFrame();
        const auto sprite = textures.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN + i), ZoomLevel{});
        ASSERT_TRUE(sprite.has_value());
        ASSERT_TRUE(sprite->zeroCoverage.has_value());
        Gpu::FrameCommandStream commands;
        leases[i] = textures.SealFrame(commands);
        ASSERT_EQ(commands.textureUploads.size(), 2u);
        EXPECT_EQ(commands.textureUploads[1].pixels[0], std::byte{ 255 });
        EXPECT_EQ(commands.textureUploads[1].pixels[1], std::byte{ 0 });
        textures.InvalidateImage(SPR_TEMP_BEGIN + i);
    }
    textures.BeginFrame();
    EXPECT_THROW((void)textures.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN + 2), ZoomLevel{}), std::runtime_error);
    textures.AbortFrame();
    textures.RetireFrame(leases[0], Gpu::FrameRetirement::Failed);
    textures.DrainFrameRetirements();
    textures.BeginFrame();
    const auto retry = textures.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN + 2), ZoomLevel{});
    ASSERT_TRUE(retry.has_value());
    ASSERT_TRUE(retry->zeroCoverage.has_value());
    Gpu::FrameCommandStream commands;
    const auto lease = textures.SealFrame(commands);
    EXPECT_EQ(commands.textureUploads.size(), 2u);
    textures.RetireFrame(lease, Gpu::FrameRetirement::Presented);
    textures.RetireFrame(leases[1], Gpu::FrameRetirement::Superseded);
    textures.DrainFrameRetirements();
}

TEST(VulkanCoverageCacheTest, FailedFramesRetryBothUploadsAndRemappedFramesOmitCoverage)
{
    SyntheticSprites sprites("CoveredZeroRleRaw");
    Gpu::TextureCache textures;
    const ImageId image(SPR_TEMP_BEGIN + 1);
    for (uint32_t attempt = 0; attempt < 3; attempt++)
    {
        textures.BeginFrame();
        const auto sprite = textures.GetOrLoadImageSprite(image, ZoomLevel{});
        ASSERT_TRUE(sprite.has_value());
        ASSERT_TRUE(sprite->zeroCoverage.has_value());
        const auto remapped = textures.GetOrLoadImageSprite(image.WithPrimary(Colour::brightRed), ZoomLevel{});
        ASSERT_TRUE(remapped.has_value());
        EXPECT_FALSE(remapped->zeroCoverage.has_value());
        const auto zoomed = textures.GetOrLoadImageSprite(image, ZoomLevel{ 1 });
        ASSERT_TRUE(zoomed.has_value());
        EXPECT_FALSE(zoomed->zeroCoverage.has_value());
        Gpu::FrameCommandStream commands;
        const auto lease = textures.SealFrame(commands);
        EXPECT_EQ(commands.textureUploads.size(), attempt == 2 ? 0u : 2u);
        textures.RetireFrame(lease, attempt == 0 ? Gpu::FrameRetirement::Failed : Gpu::FrameRetirement::Presented);
        textures.DrainFrameRetirements();
    }
}

INSTANTIATE_TEST_SUITE_P(
    Primitives, VulkanParityTest,
    testing::Values(
        "Rectangles", "Hatches", "Lines", "BitmapSprites", "RleSprites", "Filters", "BitmapZoomIn", "BitmapZoomOut",
        "RleZoomIn", "RleZoomOut", "Remaps", "Masks", "MasksZoomIn", "Transparency", "SolidSprites", "Glyphs", "NestedTargets",
        "OpaqueZeroBitmapRaw", "OpaqueZeroBitmapRemapped", "OpaqueZeroBitmapSolid", "CoveredZeroRleRaw",
        "CoveredZeroRleRemapped", "CoveredZeroRleSolid", "WeatherRain", "WeatherSnow", "OpaqueZeroBitmapOrdering",
        "CoveredZeroRleOrdering", "TransparencyOrdering", "WeatherTransparencyOrdering", "TransparencyDeep32",
        "TransparencyDeep33"),
    [](const testing::TestParamInfo<const char*>& info) { return info.param; });

#else

TEST(VulkanParityTest, RequiredBackendAvailability)
{
    if (RequiredVulkanParity())
        FAIL() << "OPENRCT2_REQUIRE_VULKAN_TESTS=1 requires an ENABLE_VULKAN build";
    GTEST_SKIP() << "Vulkan was not compiled";
}

#endif
