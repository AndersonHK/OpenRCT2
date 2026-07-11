/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GpuCommandDrawingContext.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <openrct2/core/EnumUtils.hpp>
#include <openrct2/drawing/Drawing.String.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/world/Location.hpp>

namespace OpenRCT2::Ui::Gpu
{
    using namespace Drawing;

    namespace
    {
        constexpr uint8_t kCSInside = 0b0000;
        constexpr uint8_t kCSLeft = 0b0001;
        constexpr uint8_t kCSRight = 0b0010;
        constexpr uint8_t kCSTop = 0b0100;
        constexpr uint8_t kCSBottom = 0b1000;

        template<typename T>
        [[nodiscard]] T EuclideanRemainder(T a, T b)
        {
            const T remainder = a % b;
            return remainder >= 0 ? remainder : remainder + b;
        }

        struct SpriteGeometry
        {
            TextureBinding texture;
            Int4 bounds;
            float zoom;
        };

        [[nodiscard]] SpriteGeometry CalculatePaletteSpriteGeometry(
            const RenderTarget& rt, const G1Element& element, int32_t x, int32_t y, TextureBinding texture)
        {
            int32_t left = x + element.xOffset;
            int32_t top = y + element.yOffset;
            int32_t xModifier = 0;
            int32_t yModifier = 0;
            int32_t widthModifier = 0;
            if (rt.zoom_level > ZoomLevel{ 0 })
            {
                const int32_t interval = rt.zoom_level.ApplyTo(1);
                xModifier = EuclideanRemainder(left, interval);
                xModifier = xModifier ? interval - xModifier : 0;
                yModifier = EuclideanRemainder(top, interval);
                widthModifier = EuclideanRemainder(left + element.width, interval);
                widthModifier = widthModifier ? interval - widthModifier : 0;
                texture.coords.x += xModifier;
                texture.coords.y += (interval - 1) - yModifier;
            }

            left = rt.zoom_level.ApplyInversedTo(left + xModifier);
            top = rt.zoom_level.ApplyInversedTo(top);
            const int32_t right = left + rt.zoom_level.ApplyInversedTo(element.width + widthModifier);
            const int32_t bottom = top + rt.zoom_level.ApplyInversedTo(element.height + yModifier);
            const float zoom = rt.zoom_level >= ZoomLevel{ 0 } ? static_cast<float>(rt.zoom_level.ApplyTo(1))
                                                               : 1.0f / rt.zoom_level.ApplyInversedTo(1);
            return { texture, { left, top, right, bottom }, zoom };
        }
    } // namespace

    CommandDrawingContext::CommandDrawingContext(RenderTarget& mainTarget, TextureCache& textureCache)
        : _mainTarget(mainTarget)
        , _textureCache(textureCache)
    {
    }

    void CommandDrawingContext::Begin(FrameCommandStream& commands)
    {
        assert(!_inDraw);
        _commands = &commands;
        _drawCount = 0;
        _inDraw = true;
    }

    void CommandDrawingContext::End()
    {
        assert(_inDraw);
        _commands = nullptr;
        _inDraw = false;
    }

    void CommandDrawingContext::Resize()
    {
        ResetClippingCache();
    }

    bool CommandDrawingContext::IsActive() const noexcept
    {
        return _inDraw;
    }

    void CommandDrawingContext::Clear(RenderTarget& rt, PaletteIndex paletteIndex)
    {
        assert(_inDraw);
        FillRect(rt, paletteIndex, rt.x, rt.y, rt.x + rt.width, rt.y + rt.height);
    }

    void CommandDrawingContext::FillRect(
        RenderTarget& rt, PaletteIndex paletteIndex, int32_t left, int32_t top, int32_t right, int32_t bottom,
        bool crossHatch)
    {
        assert(_inDraw);
        if (left > right || top > bottom)
            return;
        const ScreenRect clip = CalculateClipping(rt);
        left += clip.GetLeft() - rt.x;
        top += clip.GetTop() - rt.y;
        right += clip.GetLeft() - rt.x;
        bottom += clip.GetTop() - rt.y;
        const Int4 bounds{ left, top, right, bottom };
        const Int4 clipBounds{ clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        if (!InclusiveRectIntersectsClip(bounds, clipBounds))
            return;

        auto& command = _commands->opaqueRects.allocate();
        command.clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        command.texColourAtlas = 0;
        command.texColourBounds = {};
        command.texMaskAtlas = 0;
        command.texMaskBounds = {};
        command.palettes = {};
        command.colour = EnumValue(paletteIndex);
        command.bounds = { bounds.x, bounds.y, bounds.z + 1, bounds.w + 1 };
        command.flags = RectCommand::FLAG_NO_TEXTURE | (crossHatch ? RectCommand::FLAG_CROSS_HATCH : 0);
        command.depth = _drawCount++;
        command.zoom = 1.0f;
    }

    void CommandDrawingContext::FilterRect(
        RenderTarget& rt, FilterPaletteID palette, int32_t left, int32_t top, int32_t right, int32_t bottom)
    {
        assert(_inDraw);
        if (left > right || top > bottom || !GetPaletteMapForColour(palette).has_value())
            return;
        const ScreenRect clip = CalculateClipping(rt);
        left += clip.GetLeft() - rt.x;
        top += clip.GetTop() - rt.y;
        right += clip.GetLeft() - rt.x;
        bottom += clip.GetTop() - rt.y;
        const Int4 bounds{ left, top, right, bottom };
        const Int4 clipBounds{ clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        if (!InclusiveRectIntersectsClip(bounds, clipBounds))
            return;

        auto& command = _commands->transparentRects.allocate();
        command.clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        command.texColourAtlas = 0;
        command.texColourBounds = {};
        command.texMaskAtlas = 0;
        command.texMaskBounds = {};
        command.palettes = {};
        command.colour = TextureCache::PaletteToY(palette);
        command.bounds = { bounds.x, bounds.y, bounds.z + 1, bounds.w + 1 };
        command.flags = RectCommand::FLAG_NO_TEXTURE;
        command.depth = _drawCount++;
        command.zoom = 1.0f;
    }

    uint8_t CommandDrawingContext::ComputeOutCode(
        ScreenCoordsXY point, ScreenCoordsXY topLeft, ScreenCoordsXY bottomRight)
    {
        uint8_t code = kCSInside;
        if (point.x < topLeft.x)
            code |= kCSLeft;
        else if (point.x > bottomRight.x)
            code |= kCSRight;
        if (point.y < topLeft.y)
            code |= kCSTop;
        else if (point.y > bottomRight.y)
            code |= kCSBottom;
        return code;
    }

    bool CommandDrawingContext::CohenSutherlandLineClip(ScreenLine& line, const RenderTarget& rt)
    {
        const ScreenCoordsXY topLeft = { rt.x, rt.y };
        const ScreenCoordsXY bottomRight = { rt.x + rt.width - 1, rt.y + rt.height - 1 };
        uint8_t outcode1 = ComputeOutCode(line.Point1, topLeft, bottomRight);
        uint8_t outcode2 = ComputeOutCode(line.Point2, topLeft, bottomRight);

        while (true)
        {
            if (outcode1 == kCSInside && outcode2 == kCSInside)
                return true;
            if (outcode1 & outcode2)
                return false;

            const uint8_t outside = outcode2 > outcode1 ? outcode2 : outcode1;
            ScreenCoordsXY clipped{};
            if (outside & kCSBottom)
            {
                clipped.x = line.Point1.x + (line.Point2.x - line.Point1.x) * (bottomRight.y - line.Point1.y)
                    / (line.Point2.y - line.Point1.y);
                clipped.y = bottomRight.y;
            }
            else if (outside & kCSTop)
            {
                clipped.x = line.Point1.x + (line.Point2.x - line.Point1.x) * (topLeft.y - line.Point1.y)
                    / (line.Point2.y - line.Point1.y);
                clipped.y = topLeft.y;
            }
            else if (outside & kCSRight)
            {
                clipped.y = line.Point1.y + (line.Point2.y - line.Point1.y) * (bottomRight.x - line.Point1.x)
                    / (line.Point2.x - line.Point1.x);
                clipped.x = bottomRight.x;
            }
            else
            {
                clipped.y = line.Point1.y + (line.Point2.y - line.Point1.y) * (topLeft.x - line.Point1.x)
                    / (line.Point2.x - line.Point1.x);
                clipped.x = topLeft.x;
            }

            if (outside == outcode1)
            {
                line.Point1 = clipped;
                outcode1 = ComputeOutCode(line.Point1, topLeft, bottomRight);
            }
            else
            {
                line.Point2 = clipped;
                outcode2 = ComputeOutCode(line.Point2, topLeft, bottomRight);
            }
        }
    }

    void CommandDrawingContext::DrawLine(RenderTarget& rt, PaletteIndex colour, const ScreenLine& line)
    {
        assert(_inDraw);
        const ZoomLevel zoom = rt.zoom_level;
        ScreenLine trimmed = { { zoom.ApplyInversedTo(line.GetX1()), zoom.ApplyInversedTo(line.GetY1()) },
                               { zoom.ApplyInversedTo(line.GetX2()), zoom.ApplyInversedTo(line.GetY2()) } };
        if (!CohenSutherlandLineClip(trimmed, rt))
            return;

        const ScreenRect clip = CalculateClipping(rt);
        auto& command = _commands->lines.allocate();
        command.bounds = {
            trimmed.GetX1() - rt.x + clip.GetLeft(),
            trimmed.GetY1() - rt.y + clip.GetTop(),
            trimmed.GetX2() - rt.x + clip.GetLeft(),
            trimmed.GetY2() - rt.y + clip.GetTop(),
        };
        command.colour = EnumValue(colour);
        command.depth = _drawCount++;
    }

    void CommandDrawingContext::DrawSprite(RenderTarget& rt, ImageId imageId, int32_t x, int32_t y)
    {
        assert(_inDraw);
        const auto* element = GfxGetG1Element(imageId);
        if (element == nullptr || element->width <= 0 || element->height <= 0)
            return;

        if (rt.zoom_level > ZoomLevel{ 0 })
        {
            if (element->flags.has(G1Flag::hasZoomSprite))
            {
                RenderTarget zoomed = rt;
                zoomed.zoom_level = rt.zoom_level - 1;
                DrawSprite(zoomed, imageId.WithIndex(imageId.GetIndex() - element->zoomedOffset), x / 2, y / 2);
                return;
            }
            if (element->flags.has(G1Flag::noZoomDraw))
                return;
        }

        auto geometry = CalculatePaletteSpriteGeometry(
            rt, *element, x, y, _textureCache.GetOrLoadImageTexture(imageId));
        const ScreenRect clip = CalculateClipping(rt);
        geometry.bounds.x += clip.GetLeft() - rt.x;
        geometry.bounds.y += clip.GetTop() - rt.y;
        geometry.bounds.z += clip.GetLeft() - rt.x;
        geometry.bounds.w += clip.GetTop() - rt.y;

        int32_t paletteCount = 0;
        Int3 palettes{};
        bool water = false;
        if (imageId.HasSecondary())
        {
            palettes.x = TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetPrimary()));
            palettes.y = TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetSecondary()));
            paletteCount = 2;
            if (imageId.HasTertiary())
            {
                palettes.z = TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetTertiary()));
                paletteCount = 3;
            }
        }
        else if (imageId.IsRemap() || imageId.IsBlended())
        {
            paletteCount = 1;
            const auto palette = static_cast<FilterPaletteID>(imageId.GetRemap());
            palettes.x = TextureCache::PaletteToY(palette);
            water = palette == FilterPaletteID::paletteWater;
        }

        auto& batch = (water || imageId.IsBlended()) ? _commands->transparentRects : _commands->opaqueRects;
        auto& command = batch.allocate();
        command.clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        command.texColourAtlas = geometry.texture.index;
        command.texColourBounds = geometry.texture.coords;
        command.texMaskAtlas = (water || imageId.IsBlended()) ? geometry.texture.index : 0;
        command.texMaskBounds = (water || imageId.IsBlended())
            ? geometry.texture.coords
            : Float4{ 0, 0, geometry.texture.coords.z, geometry.texture.coords.w };
        command.palettes = palettes;
        command.colour = (water || imageId.IsBlended()) ? palettes.x - (water ? 1 : 0) : 0;
        command.bounds = geometry.bounds;
        command.flags = water ? 0
                              : (imageId.IsBlended() ? RectCommand::FLAG_NO_TEXTURE | RectCommand::FLAG_MASK
                                                     : paletteCount);
        command.depth = _drawCount++;
        command.zoom = geometry.zoom;
    }

    void CommandDrawingContext::DrawSpriteRawMasked(
        RenderTarget& rt, int32_t x, int32_t y, ImageId maskImage, ImageId colourImage)
    {
        assert(_inDraw);
        const auto* maskElement = GfxGetG1Element(maskImage);
        const auto* colourElement = GfxGetG1Element(colourImage);
        if (maskElement == nullptr || colourElement == nullptr || maskElement->width <= 0 || maskElement->height <= 0
            || colourElement->width <= 0 || colourElement->height <= 0)
            return;

        const auto mask = _textureCache.GetOrLoadImageTexture(maskImage);
        const auto colour = _textureCache.GetOrLoadImageTexture(colourImage);
        int32_t left = x + maskElement->xOffset;
        int32_t top = y + maskElement->yOffset;
        int32_t right = left + std::min(maskElement->width, colourElement->width);
        int32_t bottom = top + std::min(maskElement->height, colourElement->height);
        if (left > right)
            std::swap(left, right);
        if (top > bottom)
            std::swap(top, bottom);

        left = rt.zoom_level.ApplyInversedTo(left);
        top = rt.zoom_level.ApplyInversedTo(top);
        right = rt.zoom_level.ApplyInversedTo(right);
        bottom = rt.zoom_level.ApplyInversedTo(bottom);
        const ScreenRect clip = CalculateClipping(rt);
        left += clip.GetLeft() - rt.x;
        top += clip.GetTop() - rt.y;
        right += clip.GetLeft() - rt.x;
        bottom += clip.GetTop() - rt.y;

        auto& command = _commands->opaqueRects.allocate();
        command.clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        command.texColourAtlas = colour.index;
        command.texColourBounds = colour.coords;
        command.texMaskAtlas = mask.index;
        command.texMaskBounds = mask.coords;
        command.palettes = {};
        command.flags = RectCommand::FLAG_MASK;
        command.colour = 0;
        command.bounds = { left, top, right, bottom };
        command.depth = _drawCount++;
        command.zoom = rt.zoom_level >= ZoomLevel{ 0 } ? static_cast<float>(rt.zoom_level.ApplyTo(1))
                                                       : 1.0f / rt.zoom_level.ApplyInversedTo(1);
    }

    void CommandDrawingContext::DrawSpriteSolid(
        RenderTarget& rt, ImageId image, int32_t x, int32_t y, PaletteIndex colour)
    {
        assert(_inDraw);
        const auto* element = GfxGetG1Element(image);
        if (element == nullptr || element->width <= 0 || element->height <= 0)
            return;

        if (rt.zoom_level > ZoomLevel{ 0 })
        {
            if (element->flags.has(G1Flag::hasZoomSprite))
            {
                RenderTarget zoomed = rt;
                zoomed.zoom_level = rt.zoom_level - 1;
                DrawSpriteSolid(
                    zoomed, image.WithIndex(image.GetIndex() - element->zoomedOffset), x / 2, y / 2, colour);
                return;
            }
            if (element->flags.has(G1Flag::noZoomDraw))
                return;
        }

        auto geometry = CalculatePaletteSpriteGeometry(
            rt, *element, x, y, _textureCache.GetOrLoadImageTexture(image));

        const ScreenRect clip = CalculateClipping(rt);
        geometry.bounds.x += clip.GetLeft() - rt.x;
        geometry.bounds.y += clip.GetTop() - rt.y;
        geometry.bounds.z += clip.GetLeft() - rt.x;
        geometry.bounds.w += clip.GetTop() - rt.y;

        auto& command = _commands->opaqueRects.allocate();
        command.clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        command.texColourAtlas = 0;
        command.texColourBounds = {};
        command.texMaskAtlas = geometry.texture.index;
        command.texMaskBounds = geometry.texture.coords;
        command.palettes = {};
        command.flags = RectCommand::FLAG_NO_TEXTURE | RectCommand::FLAG_MASK;
        command.colour = EnumValue(colour);
        command.bounds = geometry.bounds;
        command.depth = _drawCount++;
        command.zoom = geometry.zoom;
    }

    void CommandDrawingContext::DrawGlyph(
        RenderTarget& rt, ImageId image, int32_t x, int32_t y, const PaletteMap& palette)
    {
        assert(_inDraw);
        const auto* element = GfxGetG1Element(image);
        if (element == nullptr || element->width <= 0 || element->height <= 0)
            return;

        if (rt.zoom_level > ZoomLevel{ 0 })
        {
            if (element->flags.has(G1Flag::hasZoomSprite))
            {
                RenderTarget zoomed = rt;
                zoomed.zoom_level = rt.zoom_level - 1;
                DrawGlyph(
                    zoomed, image.WithIndex(image.GetIndex() - element->zoomedOffset), x / 2, y / 2, palette);
                return;
            }
            if (element->flags.has(G1Flag::noZoomDraw))
                return;
        }

        auto geometry = CalculatePaletteSpriteGeometry(
            rt, *element, x, y, _textureCache.GetOrLoadGlyphTexture(image, palette));

        const ScreenRect clip = CalculateClipping(rt);
        geometry.bounds.x += clip.GetLeft() - rt.x;
        geometry.bounds.y += clip.GetTop() - rt.y;
        geometry.bounds.z += clip.GetLeft() - rt.x;
        geometry.bounds.w += clip.GetTop() - rt.y;

        auto& command = _commands->opaqueRects.allocate();
        command.clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
        command.texColourAtlas = geometry.texture.index;
        command.texColourBounds = geometry.texture.coords;
        command.texMaskAtlas = 0;
        command.texMaskBounds = {};
        command.palettes = {};
        command.flags = 0;
        command.colour = 0;
        command.bounds = geometry.bounds;
        command.depth = _drawCount++;
        command.zoom = geometry.zoom;
    }

    void CommandDrawingContext::DrawTTFBitmap(
        RenderTarget& rt, const TextDrawInfo& info, TTFSurface* surface, int32_t x, int32_t y,
        uint8_t hintingThreshold)
    {
        assert(_inDraw);
#ifndef DISABLE_TTF
        const auto texture = _textureCache.LoadTransientBitmapTexture(surface->pixels, surface->w, surface->h);

        int32_t left = x;
        int32_t top = y;
        int32_t right = left + surface->w;
        int32_t bottom = top + surface->h;
        const ScreenRect clip = CalculateClipping(rt);
        left += clip.GetLeft() - rt.x;
        top += clip.GetTop() - rt.y;
        right += clip.GetLeft() - rt.x;
        bottom += clip.GetTop() - rt.y;

        const auto appendText = [&](CommandBatch<RectCommand>& batch, Int4 bounds, PaletteIndex colour, uint32_t flags) {
            auto& command = batch.allocate();
            command.clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() };
            command.texColourAtlas = texture.index;
            command.texColourBounds = texture.coords;
            command.texMaskAtlas = 0;
            command.texMaskBounds = {};
            command.palettes = {};
            command.flags = flags;
            command.colour = EnumValue(colour);
            command.bounds = bounds;
            command.depth = _drawCount++;
            command.zoom = 1.0f;
        };

        if (info.colourFlags.has(ColourFlag::withOutline))
        {
            const std::array<Int4, 4> outlines = { {
                { left + 1, top, right + 1, bottom },
                { left - 1, top, right - 1, bottom },
                { left, top + 1, right, bottom + 1 },
                { left, top - 1, right, bottom - 1 },
            } };
            for (const auto& bounds : outlines)
            {
                appendText(
                    _commands->opaqueRects, bounds, info.palette.shadowOutline, RectCommand::FLAG_TTF_TEXT);
            }
        }
        if (info.colourFlags.has(ColourFlag::inset))
        {
            appendText(
                _commands->opaqueRects, { left + 1, top + 1, right + 1, bottom + 1 }, info.palette.shadowOutline,
                RectCommand::FLAG_TTF_TEXT);
        }

        auto& batch = hintingThreshold > 0 ? _commands->transparentRects : _commands->opaqueRects;
        appendText(
            batch, { left, top, right, bottom }, info.palette.fill,
            RectCommand::FLAG_TTF_TEXT | (static_cast<uint32_t>(hintingThreshold) << 8));
#else
        static_cast<void>(rt);
        static_cast<void>(info);
        static_cast<void>(surface);
        static_cast<void>(x);
        static_cast<void>(y);
        static_cast<void>(hintingThreshold);
#endif
    }

    ScreenRect CommandDrawingContext::CalculateClipping(const RenderTarget& rt) const
    {
        const int32_t stride = _mainTarget.LineStride();
        const auto key = (reinterpret_cast<uintptr_t>(rt.bits) >> 4) ^ static_cast<uintptr_t>(rt.width)
            ^ (static_cast<uintptr_t>(rt.height) << 8);
        auto& cached = _clippingCache[key & (kClippingCacheSize - 1)];
        if (cached.bits == rt.bits && cached.width == rt.width && cached.height == rt.height && cached.stride == stride)
            return cached.clip;

        const ptrdiff_t offset = rt.bits - _mainTarget.bits;
#ifndef NDEBUG
        const ptrdiff_t size = static_cast<ptrdiff_t>(_mainTarget.height) * stride;
        assert(offset >= 0 && offset < size);
#endif
        const int32_t left = static_cast<int32_t>(offset % stride);
        const int32_t top = static_cast<int32_t>(offset / stride);
        cached.bits = rt.bits;
        cached.width = rt.width;
        cached.height = rt.height;
        cached.stride = stride;
        cached.clip = { { left, top }, { left + rt.width, top + rt.height } };
        return cached.clip;
    }

    void CommandDrawingContext::ResetClippingCache()
    {
        for (auto& entry : _clippingCache)
            entry.bits = nullptr;
    }
} // namespace OpenRCT2::Ui::Gpu
