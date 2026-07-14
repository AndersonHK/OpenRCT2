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
#include <cstring>
#include <openrct2/core/EnumUtils.hpp>
#include <openrct2/drawing/Drawing.String.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/world/Location.hpp>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <ranges>

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

        struct CompactSpriteGeometry
        {
            Int4 bounds{};
            Int2 texelOffset{};
            float zoom = 1.0f;
        };

        [[nodiscard]] SpriteGeometry CalculatePaletteSpriteGeometry(
            const RenderTarget& rt, const G1Element& element, int32_t x, int32_t y, TextureBinding texture,
            const ScreenRect& clip)
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
            const int32_t clipX = clip.GetLeft() - rt.x;
            const int32_t clipY = clip.GetTop() - rt.y;
            return { texture, { left + clipX, top + clipY, right + clipX, bottom + clipY }, zoom };
        }

        [[nodiscard]] CompactSpriteGeometry CalculateCompactSpriteGeometry(
            const RenderTarget& rt, const ResolvedSprite& sprite, int32_t x, int32_t y, const ScreenRect& clip)
        {
            for (uint8_t i = 0; i < sprite.coordinateShift; i++)
            {
                x /= 2;
                y /= 2;
            }

            int32_t left = x + sprite.xOffset;
            int32_t top = y + sprite.yOffset;
            int32_t xModifier = 0;
            int32_t yModifier = 0;
            int32_t widthModifier = 0;
            if (sprite.zoom > ZoomLevel{ 0 })
            {
                const int32_t interval = sprite.zoom.ApplyTo(1);
                xModifier = EuclideanRemainder(left, interval);
                xModifier = xModifier ? interval - xModifier : 0;
                yModifier = EuclideanRemainder(top, interval);
                widthModifier = EuclideanRemainder(left + sprite.width, interval);
                widthModifier = widthModifier ? interval - widthModifier : 0;
            }

            left = sprite.zoom.ApplyInversedTo(left + xModifier);
            top = sprite.zoom.ApplyInversedTo(top);
            const int32_t right = left + sprite.zoom.ApplyInversedTo(sprite.width + widthModifier);
            const int32_t bottom = top + sprite.zoom.ApplyInversedTo(sprite.height + yModifier);
            const int32_t clipX = clip.GetLeft() - rt.x;
            const int32_t clipY = clip.GetTop() - rt.y;
            const float zoom = sprite.zoom >= ZoomLevel{ 0 } ? static_cast<float>(sprite.zoom.ApplyTo(1))
                                                             : 1.0f / sprite.zoom.ApplyInversedTo(1);
            const int32_t texelY = sprite.zoom > ZoomLevel{ 0 } ? sprite.zoom.ApplyTo(1) - 1 - yModifier : 0;
            return {
                .bounds = { left + clipX, top + clipY, right + clipX, bottom + clipY },
                .texelOffset = { xModifier, texelY },
                .zoom = zoom,
            };
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
        // Mixed legacy scenes conservatively reject direct terrain until both paths publish one shared painter key.
        // Eligible terrain-only scenes can therefore use their native deterministic order without a category partition.
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
        for (auto& entry : _clippingCache)
            entry.bits = nullptr;
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

        auto& command = AppendRect(
            _commands->opaqueRects, clip, { bounds.x, bounds.y, bounds.z + 1, bounds.w + 1 });
        command.colour = EnumValue(paletteIndex);
        command.flags = RectCommand::FLAG_NO_TEXTURE | (crossHatch ? RectCommand::FLAG_CROSS_HATCH : 0);
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

        auto& command = AppendRect(
            _commands->transparentRects, clip, { bounds.x, bounds.y, bounds.z + 1, bounds.w + 1 });
        command.colour = TextureCache::PaletteToY(palette);
        command.flags = RectCommand::FLAG_NO_TEXTURE;
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

        const bool water = imageId.IsRemap()
            && static_cast<FilterPaletteID>(imageId.GetRemap()) == FilterPaletteID::paletteWater;
        if (!water && !imageId.IsBlended())
        {
            const auto sprite = _textureCache.GetOrLoadImageSprite(imageId, rt.zoom_level);
            if (!sprite.has_value())
                return;

            const ScreenRect clip = CalculateClipping(rt);
            const auto geometry = CalculateCompactSpriteGeometry(rt, *sprite, x, y, clip);
            int32_t paletteCount = 0;
            uint8_t primary = 0;
            uint8_t secondary = 0;
            uint8_t tertiary = 0;
            if (imageId.HasSecondary())
            {
                primary = static_cast<uint8_t>(TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetPrimary())));
                secondary = static_cast<uint8_t>(
                    TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetSecondary())));
                paletteCount = 2;
                if (imageId.HasTertiary())
                {
                    tertiary = static_cast<uint8_t>(
                        TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetTertiary())));
                    paletteCount = 3;
                }
            }
            else if (imageId.IsRemap())
            {
                primary = static_cast<uint8_t>(
                    TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetRemap())));
                paletteCount = 1;
            }

            auto& command = _commands->opaqueSprites.allocate();
            command = {
                .clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() },
                .bounds = geometry.bounds,
                .texelOffset = geometry.texelOffset,
                .asset = sprite->descriptorIndex,
                .palettes = SpriteCommand::PackPalettes(primary, secondary, tertiary, paletteCount),
                .effects = SpriteCommand::PackEffects(static_cast<uint32_t>(paletteCount), 0),
                .depth = _drawCount++,
                .zoom = geometry.zoom,
            };
            return;
        }

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

        const ScreenRect clip = CalculateClipping(rt);
        auto geometry = CalculatePaletteSpriteGeometry(
            rt, *element, x, y, _textureCache.GetOrLoadImageTexture(imageId), clip);

        int32_t paletteCount = 0;
        Int3 palettes{};
        bool legacyWater = false;
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
            legacyWater = palette == FilterPaletteID::paletteWater;
        }

        auto& batch = (legacyWater || imageId.IsBlended()) ? _commands->transparentRects : _commands->opaqueRects;
        auto& command = AppendRect(batch, clip, geometry.bounds, geometry.zoom);
        command.texColourAtlas = geometry.texture.index;
        command.texColourBounds = geometry.texture.coords;
        command.texMaskAtlas = (legacyWater || imageId.IsBlended()) ? geometry.texture.index : 0;
        command.texMaskBounds = (legacyWater || imageId.IsBlended())
            ? geometry.texture.coords
            : Float4{ 0, 0, geometry.texture.coords.z, geometry.texture.coords.w };
        command.palettes = palettes;
        command.colour = (legacyWater || imageId.IsBlended()) ? palettes.x - (legacyWater ? 1 : 0) : 0;
        command.flags = legacyWater ? 0
                              : (imageId.IsBlended() ? RectCommand::FLAG_NO_TEXTURE | RectCommand::FLAG_MASK
                                                     : paletteCount);
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
        left = rt.zoom_level.ApplyInversedTo(left);
        top = rt.zoom_level.ApplyInversedTo(top);
        right = rt.zoom_level.ApplyInversedTo(right);
        bottom = rt.zoom_level.ApplyInversedTo(bottom);
        const ScreenRect clip = CalculateClipping(rt);
        left += clip.GetLeft() - rt.x;
        top += clip.GetTop() - rt.y;
        right += clip.GetLeft() - rt.x;
        bottom += clip.GetTop() - rt.y;

        const float zoom = rt.zoom_level >= ZoomLevel{ 0 } ? static_cast<float>(rt.zoom_level.ApplyTo(1))
                                                           : 1.0f / rt.zoom_level.ApplyInversedTo(1);
        auto& command = AppendRect(_commands->opaqueRects, clip, { left, top, right, bottom }, zoom);
        command.texColourAtlas = colour.index;
        command.texColourBounds = colour.coords;
        command.texMaskAtlas = mask.index;
        command.texMaskBounds = mask.coords;
        command.flags = RectCommand::FLAG_MASK;
    }

    void CommandDrawingContext::DrawSpriteSolid(
        RenderTarget& rt, ImageId image, int32_t x, int32_t y, PaletteIndex colour)
    {
        assert(_inDraw);
        const auto sprite = _textureCache.GetOrLoadImageSprite(image, rt.zoom_level);
        if (!sprite.has_value())
            return;

        const ScreenRect clip = CalculateClipping(rt);
        const auto geometry = CalculateCompactSpriteGeometry(rt, *sprite, x, y, clip);
        auto& command = _commands->opaqueSprites.allocate();
        command = {
            .clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() },
            .bounds = geometry.bounds,
            .texelOffset = geometry.texelOffset,
            .asset = sprite->descriptorIndex,
            .palettes = 0,
            .effects = SpriteCommand::PackEffects(
                RectCommand::FLAG_NO_TEXTURE | RectCommand::FLAG_MASK, static_cast<uint8_t>(EnumValue(colour))),
            .depth = _drawCount++,
            .zoom = geometry.zoom,
        };
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

        const ScreenRect clip = CalculateClipping(rt);
        auto geometry = CalculatePaletteSpriteGeometry(
            rt, *element, x, y, _textureCache.GetOrLoadGlyphTexture(image, palette), clip);

        auto& command = AppendRect(_commands->opaqueRects, clip, geometry.bounds, geometry.zoom);
        command.texColourAtlas = geometry.texture.index;
        command.texColourBounds = geometry.texture.coords;
    }

    void CommandDrawingContext::DrawTTFBitmap(
        RenderTarget& rt, const TextDrawInfo& info, TTFSurface* surface, int32_t x, int32_t y,
        uint8_t hintingThreshold)
    {
        assert(_inDraw);
#ifndef DISABLE_TTF
        const auto texture = _textureCache.GetOrLoadTTFTexture(*surface);

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
            auto& command = AppendRect(batch, clip, bounds);
            command.texColourAtlas = texture.index;
            command.texColourBounds = texture.coords;
            command.flags = flags;
            command.colour = EnumValue(colour);
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

    size_t CommandDrawingContext::ImageIdHash::operator()(const ImageId& image) const noexcept
    {
        size_t result = image.GetIndex();
        const auto combine = [&result](const size_t value) {
            result ^= value + 0x9e3779b9 + (result << 6) + (result >> 2);
        };
        combine(static_cast<uint8_t>(image.GetPrimary()));
        combine(static_cast<uint8_t>(image.GetSecondary()));
        combine(static_cast<uint8_t>(image.GetTertiary()));
        combine(image.HasPrimary());
        combine(image.HasSecondary());
        combine(image.IsBlended());
        return result;
    }

    uint32_t CommandDrawingContext::GetOrCreateSurfaceSpriteSet(const ImageId image)
    {
        if (const auto existing = _surfaceSpriteLookup.find(image); existing != _surfaceSpriteLookup.end())
            return existing->second;
        if (_surfaceSpriteCache.size() >= kWorldSurfaceMaximumSpriteSetCount)
            return std::numeric_limits<uint32_t>::max();
        const auto index = static_cast<uint32_t>(_surfaceSpriteCache.size());
        _surfaceSpriteCache.push_back({ .image = image });
        _surfaceSpriteLookup.emplace(image, index);
        return index;
    }

    WorldSurfaceSpriteSet CommandDrawingContext::ResolveSurfaceSpriteSet(const ImageId image)
    {
        uint8_t primary = 0;
        uint8_t secondary = 0;
        uint8_t tertiary = 0;
        uint8_t count = 0;
        if (image.HasSecondary())
        {
            primary = static_cast<uint8_t>(TextureCache::PaletteToY(static_cast<FilterPaletteID>(image.GetPrimary())));
            secondary = static_cast<uint8_t>(TextureCache::PaletteToY(static_cast<FilterPaletteID>(image.GetSecondary())));
            count = 2;
            if (image.HasTertiary())
            {
                tertiary = static_cast<uint8_t>(
                    TextureCache::PaletteToY(static_cast<FilterPaletteID>(image.GetTertiary())));
                count = 3;
            }
        }
        else if (image.IsRemap())
        {
            primary = static_cast<uint8_t>(TextureCache::PaletteToY(static_cast<FilterPaletteID>(image.GetRemap())));
            count = 1;
        }

        WorldSurfaceSpriteSet result{};
        result.palettes = SpriteCommand::PackPalettes(primary, secondary, tertiary, count);
        result.effects = SpriteCommand::PackEffects(count, 0);
        for (int32_t zoom = static_cast<int32_t>(kWorldSurfaceMinimumZoom);
             zoom <= static_cast<int32_t>(kWorldSurfaceMaximumZoom); zoom++)
        {
            const auto resolved = _textureCache.GetOrLoadImageSprite(image, ZoomLevel{ static_cast<int8_t>(zoom) });
            if (!resolved.has_value())
                continue;
            result.variants[zoom - static_cast<int32_t>(kWorldSurfaceMinimumZoom)] = {
                .spriteSize = { resolved->width, resolved->height },
                .spriteOffset = { resolved->xOffset, resolved->yOffset },
                .asset = resolved->descriptorIndex,
                .zoom = static_cast<int8_t>(resolved->zoom),
                .coordinateShift = resolved->coordinateShift,
                .valid = 1,
            };
        }
        return result;
    }

    bool CommandDrawingContext::DrawWorldSurfaceScene(
        RenderTarget& rt, std::shared_ptr<const PresentationGeneration> generation, const OrthographicCamera& camera)
    {
        assert(_inDraw);
        if (generation == nullptr || generation->map == nullptr || _commands->worldSurfaces.has_value())
            return false;
        const auto fallbackReason = GetWorldSurfaceFallbackReason(
            generation->map->RequiresLegacyPainterInterleave(),
            generation->entities != nullptr && generation->entities->GetCapturedEntityCount() != 0,
            camera.landscapeSmoothing != 0);
        if (fallbackReason != WorldSurfaceFallbackReason::none)
            return false;

        const ScreenRect clip = CalculateClipping(rt);
        const Int4 cameraClip{
            std::max(clip.GetLeft(), camera.clipLeft),
            std::max(clip.GetTop(), camera.clipTop),
            std::min(clip.GetRight(), camera.clipRight),
            std::min(clip.GetBottom(), camera.clipBottom),
        };
        // Claim the slot before atlas resolution: first residency can invalidate and re-enter viewport painting.
        auto& scene = _commands->worldSurfaces.emplace(WorldSurfaceSceneCommand{
            .generation = generation->id,
            .worldEpoch = generation->worldEpoch,
            .width = generation->map->GetSurfaceWidth(),
            .height = generation->map->GetSurfaceHeight(),
            .recordCount = generation->map->GetSurfaceRecordCount(),
            .clip = cameraClip,
            .view = { camera.viewX, camera.viewY },
            .zoom = camera.zoom,
            .rotation = camera.rotation & 3,
        });

        if (_surfaceWorldEpoch != scene.worldEpoch || _surfaceWidth != scene.width || _surfaceHeight != scene.height)
        {
            _surfaceWorldEpoch = scene.worldEpoch;
            _surfaceWidth = scene.width;
            _surfaceHeight = scene.height;
            _surfaceChunks.clear();
            _surfaceSpriteCache.clear();
            _surfaceSpriteLookup.clear();
            _publishedSurfaceSprites.reset();
        }

        const auto& sourceChunks = generation->map->GetSurfaceChunks();
        _surfaceChunks.resize(sourceChunks.size());
        scene.chunks.resize(sourceChunks.size());
        for (size_t chunkIndex = 0; chunkIndex < sourceChunks.size(); chunkIndex++)
        {
            const auto& source = sourceChunks[chunkIndex];
            if (source == nullptr)
                continue;
            auto& publishedChunk = _surfaceChunks[chunkIndex];
            if (publishedChunk.source.get() != source.get() || publishedChunk.gpu == nullptr)
            {
                auto converted = std::make_shared<WorldSurfaceChunk>();
                converted->revision = ++_nextSurfaceChunkRevision;
                publishedChunk.spriteSets.clear();
                for (size_t localIndex = 0; localIndex < source->records.size(); localIndex++)
                {
                    const size_t tileIndex = chunkIndex * kWorldSurfaceChunkWidth + localIndex;
                    if (tileIndex >= scene.recordCount)
                        break;
                    const auto& input = source->records[localIndex];
                    auto& output = converted->records[localIndex];
                    output.baseZ = input.baseZ;
                    output.valid = input.valid;
                    if (!input.valid)
                        continue;
                    for (size_t rotation = 0; rotation < SurfacePresentationRecord::kRotationCount; rotation++)
                    {
                        output.detailedSprites[rotation] = GetOrCreateSurfaceSpriteSet(input.detailedImages[rotation]);
                        output.distantSprites[rotation] = GetOrCreateSurfaceSpriteSet(input.distantImages[rotation]);
                        if (output.detailedSprites[rotation] == std::numeric_limits<uint32_t>::max()
                            || output.distantSprites[rotation] == std::numeric_limits<uint32_t>::max())
                        {
                            _commands->worldSurfaces.reset();
                            return false;
                        }
                        publishedChunk.spriteSets.push_back(output.detailedSprites[rotation]);
                        publishedChunk.spriteSets.push_back(output.distantSprites[rotation]);
                    }
                }
                std::ranges::sort(publishedChunk.spriteSets);
                publishedChunk.spriteSets.erase(
                    std::unique(publishedChunk.spriteSets.begin(), publishedChunk.spriteSets.end()),
                    publishedChunk.spriteSets.end());
                publishedChunk.source = source;
                publishedChunk.gpu = std::move(converted);
            }
            scene.chunks[chunkIndex] = publishedChunk.gpu;
        }

        std::vector<bool> activeSpriteSets(_surfaceSpriteCache.size());
        for (const auto& chunk : _surfaceChunks)
        {
            for (const auto spriteSet : chunk.spriteSets)
                activeSpriteSets[spriteSet] = true;
        }
        bool spriteTableChanged = _publishedSurfaceSprites == nullptr
            || _publishedSurfaceSprites->records.size() != _surfaceSpriteCache.size();
        for (size_t index = 0; index < _surfaceSpriteCache.size(); index++)
        {
            if (!activeSpriteSets[index])
                continue;
            const auto resolved = ResolveSurfaceSpriteSet(_surfaceSpriteCache[index].image);
            if (std::memcmp(&resolved, &_surfaceSpriteCache[index].record, sizeof(resolved)) != 0)
            {
                _surfaceSpriteCache[index].record = resolved;
                spriteTableChanged = true;
            }
        }
        if (spriteTableChanged)
        {
            auto table = std::make_shared<WorldSurfaceSpriteTable>();
            table->revision = ++_nextSurfaceSpriteRevision;
            table->records.reserve(_surfaceSpriteCache.size());
            for (const auto& entry : _surfaceSpriteCache)
                table->records.push_back(entry.record);
            _publishedSurfaceSprites = std::move(table);
        }
        scene.sprites = _publishedSurfaceSprites;
        return scene.sprites != nullptr;
    }

    RectCommand& CommandDrawingContext::AppendRect(
        CommandBatch<RectCommand>& batch, const ScreenRect& clip, Int4 bounds, float zoom)
    {
        auto& command = batch.allocate();
        command = {
            .clip = { clip.GetLeft(), clip.GetTop(), clip.GetRight(), clip.GetBottom() },
            .bounds = bounds,
            .depth = _drawCount++,
            .zoom = zoom,
        };
        return command;
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
} // namespace OpenRCT2::Ui::Gpu
