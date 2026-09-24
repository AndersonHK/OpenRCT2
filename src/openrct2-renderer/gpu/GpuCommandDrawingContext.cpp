/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GpuCommandDrawingContext.h"

#include "GpuWorldPropCatalog.h"
#include "GpuWorldTrackCatalog.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <openrct2/Context.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/EnumUtils.hpp>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Drawing.String.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/G1Element.h>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/drawing/TTF.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/paint/tile_element/Paint.Surface.h>
#include <openrct2/world/Location.hpp>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <openrct2/world/PathPresentation.h>
#include <ranges>
#include <stdexcept>

namespace OpenRCT2::Ui::Gpu
{
    using namespace Drawing;

    namespace
    {
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

        [[nodiscard]] CompactSpriteGeometry CalculateSpriteGeometry(
            const RenderTarget& rt, int32_t width, int32_t height, int32_t xOffset, int32_t yOffset, bool rle, ZoomLevel zoom,
            int32_t x, int32_t y, const ScreenRect& clip)
        {
            if (zoom < ZoomLevel{ 0 })
            {
                const int32_t left = zoom.ApplyInversedTo(x + xOffset) + clip.getLeft() - rt.x;
                const int32_t top = zoom.ApplyInversedTo(y + yOffset) + clip.getTop() - rt.y;
                return {
                    .bounds = { left, top, left + zoom.ApplyInversedTo(width), top + zoom.ApplyInversedTo(height) },
                    .zoom = 1.0f / zoom.ApplyInversedTo(1),
                };
            }

            // Preserve the indexed asset format's sampling phase. Bitmap and RLE
            // minification have different origins; rounding every sprite to one
            // common grid changes fences, text and ride parts by a pixel.
            const int32_t step = zoom.ApplyTo(1);
            const int32_t lowMask = step - 1;
            if (rle)
            {
                x -= lowMask;
                y -= lowMask;
            }
            const int16_t top = static_cast<int16_t>(y + yOffset);
            int32_t destY = static_cast<int16_t>((rle ? top : top & ~lowMask) - zoom.ApplyTo(rt.y));
            int32_t sourceY = 0;
            if (destY < 0)
            {
                height += destY;
                sourceY = -destY;
                destY = 0;
            }
            else if (rle)
            {
                sourceY -= destY & lowMask;
                height += destY & lowMask;
            }
            height = std::min(height, zoom.ApplyTo(rt.height) - destY);
            destY = zoom.ApplyInversedTo(destY);
            if (rle && sourceY < 0)
            {
                sourceY += step;
                height -= step;
                destY++;
            }

            int32_t destX = static_cast<int16_t>(((x + xOffset + lowMask) & ~lowMask) - zoom.ApplyTo(rt.x));
            int32_t sourceX = 0;
            if (destX < 0)
            {
                width += destX;
                sourceX = -destX;
                destX = 0;
            }
            width = std::min(width, zoom.ApplyTo(rt.width) - destX);
            destX = zoom.ApplyInversedTo(destX);
            if (width <= 0 || height <= 0)
                return {};
            const int32_t left = clip.getLeft() + destX;
            const int32_t bottom = clip.getTop() + destY;
            return {
                .bounds = { left, bottom, left + (width + lowMask) / step, bottom + (height + lowMask) / step },
                .texelOffset = { sourceX, sourceY },
                .zoom = static_cast<float>(step),
            };
        }

        [[nodiscard]] SpriteGeometry CalculatePaletteSpriteGeometry(
            const RenderTarget& rt, const G1Element& element, int32_t x, int32_t y, TextureBinding texture,
            const ScreenRect& clip)
        {
            const auto geometry = CalculateSpriteGeometry(
                rt, element.width, element.height, element.xOffset, element.yOffset,
                element.flags.has(G1Flag::hasRLECompression), rt.zoom_level, x, y, clip);
            texture.coords.x += geometry.texelOffset.x;
            texture.coords.y += geometry.texelOffset.y;
            return { texture, geometry.bounds, geometry.zoom };
        }

        [[nodiscard]] CompactSpriteGeometry CalculateCompactSpriteGeometry(
            const RenderTarget& rt, const ResolvedSprite& sprite, int32_t x, int32_t y, const ScreenRect& clip)
        {
            for (uint8_t i = 0; i < sprite.coordinateShift; i++)
            {
                x /= 2;
                y /= 2;
            }
            return CalculateSpriteGeometry(
                rt, sprite.width, sprite.height, sprite.xOffset, sprite.yOffset, sprite.hasRleCompression, sprite.zoom, x, y,
                clip);
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
        // Native categories reserve intervals from this same painter sequence;
        // ordinary commands before/after those intervals retain their relative order.
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
        RenderTarget& rt, PaletteIndex paletteIndex, int32_t left, int32_t top, int32_t right, int32_t bottom, bool crossHatch)
    {
        assert(_inDraw);
        if (left > right || top > bottom)
            return;
        const ScreenRect clip = CalculateClipping(rt);
        left += clip.getLeft() - rt.x;
        top += clip.getTop() - rt.y;
        right += clip.getLeft() - rt.x;
        bottom += clip.getTop() - rt.y;
        const Int4 bounds{ left, top, right, bottom };
        const Int4 clipBounds{ clip.getLeft(), clip.getTop(), clip.getRight(), clip.getBottom() };
        if (!InclusiveRectIntersectsClip(bounds, clipBounds))
            return;

        auto& command = AppendRect(_commands->opaqueRects, clip, { bounds.x, bounds.y, bounds.z + 1, bounds.w + 1 });
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
        left += clip.getLeft() - rt.x;
        top += clip.getTop() - rt.y;
        right += clip.getLeft() - rt.x;
        bottom += clip.getTop() - rt.y;
        const Int4 bounds{ left, top, right, bottom };
        const Int4 clipBounds{ clip.getLeft(), clip.getTop(), clip.getRight(), clip.getBottom() };
        if (!InclusiveRectIntersectsClip(bounds, clipBounds))
            return;

        auto& command = AppendRect(_commands->transparentRects, clip, { bounds.x, bounds.y, bounds.z + 1, bounds.w + 1 });
        command.colour = TextureCache::PaletteToY(palette);
        command.flags = RectCommand::FLAG_NO_TEXTURE;
    }

    void CommandDrawingContext::DrawLine(RenderTarget& rt, PaletteIndex colour, const ScreenLine& line)
    {
        assert(_inDraw);
        int32_t x1 = rt.zoom_level.ApplyInversedTo(line.getX1());
        int32_t y1 = rt.zoom_level.ApplyInversedTo(line.getY1());
        int32_t x2 = rt.zoom_level.ApplyInversedTo(line.getX2());
        int32_t y2 = rt.zoom_level.ApplyInversedTo(line.getY2());
        if ((x1 < rt.x && x2 < rt.x) || (y1 < rt.y && y2 < rt.y) || (x1 > rt.x + rt.width && x2 > rt.x + rt.width)
            || (y1 > rt.y + rt.height && y2 > rt.y + rt.height))
            return;

        // Native GPU line coverage and clipping rules differ between devices and
        // from the indexed drawing contract. Prepare integer horizontal spans;
        // Vulkan still composites every pixel. Clip spans after determining the
        // original line's error phase, so offscreen endpoints cannot shift it.
        const bool steep = std::abs(y2 - y1) > std::abs(x2 - x1);
        if (steep)
        {
            std::swap(x1, y1);
            std::swap(x2, y2);
        }
        if (x1 > x2)
        {
            std::swap(x1, x2);
            std::swap(y1, y2);
        }
        const int32_t deltaX = x2 - x1;
        const int32_t deltaY = std::abs(y2 - y1);
        const int32_t step = y1 < y2 ? 1 : -1;
        int32_t error = deltaX / 2;
        int32_t y = y1;
        for (int32_t x = x1, start = x1, length = 1; x < x2; ++x, ++length)
        {
            if (steep)
                FillRect(rt, colour, y, x, y, x);
            error -= deltaY;
            if (error < 0)
            {
                if (!steep)
                    FillRect(rt, colour, start, y, start + std::max(1, length) - 1, y);
                start = x + 1;
                length = 0;
                y += step;
                error += deltaX;
            }
            // Preserve the indexed contract's final segment, including its
            // single-pixel segment after a last-step change in y. Degenerate
            // lines intentionally emit no geometry, as in the frozen reference.
            if (x + 1 == x2 && !steep)
                FillRect(rt, colour, start, y, start + std::max(1, length) - 1, y);
        }
    }
    void CommandDrawingContext::DrawSprite(RenderTarget& rt, ImageId imageId, int32_t x, int32_t y)
    {
        if (_nativeBalloonFixture && imageId.GetIndex() >= SPR_BALLOON && imageId.GetIndex() < SPR_BALLOON + 16)
            ++_commands->cpuBalloonSpriteCalls;
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
                primary = static_cast<uint8_t>(TextureCache::PaletteToY(static_cast<FilterPaletteID>(imageId.GetRemap())));
                paletteCount = 1;
            }

            auto& command = _commands->opaqueSprites.allocate();
            command = {
                .clip = { clip.getLeft(), clip.getTop(), clip.getRight(), clip.getBottom() },
                .bounds = geometry.bounds,
                .texelOffset = geometry.texelOffset,
                .asset = sprite->descriptorIndex,
                .palettes = SpriteCommand::PackPalettes(primary, secondary, tertiary, paletteCount),
                .effects = SpriteCommand::PackEffects(static_cast<uint32_t>(paletteCount), 0),
                .depth = _drawCount++,
                .zoom = geometry.zoom,
            };
            if (sprite->zeroCoverage.has_value())
            {
                auto& zero = AppendRect(_commands->opaqueRects, clip, geometry.bounds, geometry.zoom);
                zero.flags = RectCommand::FLAG_NO_TEXTURE | RectCommand::FLAG_ZERO_COVERAGE;
                zero.colour = 0;
                zero.texMaskAtlas = sprite->zeroCoverage->index;
                zero.texMaskBounds = sprite->zeroCoverage->coords;
                zero.texMaskBounds.x += geometry.texelOffset.x;
                zero.texMaskBounds.y += geometry.texelOffset.y;
            }
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
        auto geometry = CalculatePaletteSpriteGeometry(rt, *element, x, y, _textureCache.GetOrLoadImageTexture(imageId), clip);

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
        command.flags = legacyWater
            ? 0
            : (imageId.IsBlended() ? RectCommand::FLAG_NO_TEXTURE | RectCommand::FLAG_MASK : paletteCount);
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
        left += clip.getLeft() - rt.x;
        top += clip.getTop() - rt.y;
        right += clip.getLeft() - rt.x;
        bottom += clip.getTop() - rt.y;

        const float zoom = rt.zoom_level >= ZoomLevel{ 0 } ? static_cast<float>(rt.zoom_level.ApplyTo(1))
                                                           : 1.0f / rt.zoom_level.ApplyInversedTo(1);
        auto& command = AppendRect(_commands->opaqueRects, clip, { left, top, right, bottom }, zoom);
        command.texColourAtlas = colour.index;
        command.texColourBounds = colour.coords;
        command.texMaskAtlas = mask.index;
        command.texMaskBounds = mask.coords;
        command.flags = RectCommand::FLAG_MASK;
    }

    void CommandDrawingContext::DrawSpriteSolid(RenderTarget& rt, ImageId image, int32_t x, int32_t y, PaletteIndex colour)
    {
        assert(_inDraw);
        const auto sprite = _textureCache.GetOrLoadImageSprite(image, rt.zoom_level);
        if (!sprite.has_value())
            return;

        const ScreenRect clip = CalculateClipping(rt);
        const auto geometry = CalculateCompactSpriteGeometry(rt, *sprite, x, y, clip);
        auto& command = _commands->opaqueSprites.allocate();
        command = {
            .clip = { clip.getLeft(), clip.getTop(), clip.getRight(), clip.getBottom() },
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

    void CommandDrawingContext::DrawGlyph(RenderTarget& rt, ImageId image, int32_t x, int32_t y, const PaletteMap& palette)
    {
        assert(_inDraw);
        // An unremapped glyph ignores the supplied palette, including opaque zero coverage.
        if (!image.HasPrimary() && !image.HasSecondary() && !image.IsBlended())
        {
            DrawSprite(rt, image, x, y);
            return;
        }
        const auto* element = GfxGetG1Element(image);
        if (element == nullptr || element->width <= 0 || element->height <= 0)
            return;

        if (rt.zoom_level > ZoomLevel{ 0 })
        {
            if (element->flags.has(G1Flag::hasZoomSprite))
            {
                RenderTarget zoomed = rt;
                zoomed.zoom_level = rt.zoom_level - 1;
                DrawGlyph(zoomed, image.WithIndex(image.GetIndex() - element->zoomedOffset), x / 2, y / 2, palette);
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
        RenderTarget& rt, const TextDrawInfo& info, TTFSurface* surface, int32_t x, int32_t y, uint8_t hintingThreshold)
    {
        assert(_inDraw);
#ifndef DISABLE_TTF
        const auto texture = _textureCache.GetOrLoadTTFTexture(*surface);

        int32_t left = x;
        int32_t top = y;
        int32_t right = left + surface->w;
        int32_t bottom = top + surface->h;
        const ScreenRect clip = CalculateClipping(rt);
        left += clip.getLeft() - rt.x;
        top += clip.getTop() - rt.y;
        right += clip.getLeft() - rt.x;
        bottom += clip.getTop() - rt.y;

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
                appendText(_commands->opaqueRects, bounds, info.palette.shadowOutline, RectCommand::FLAG_TTF_TEXT);
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

    WorldSurfaceSpriteSet CommandDrawingContext::ResolveSurfaceSpriteSet(
        const ImageId image, std::vector<uint64_t>* residencies, std::vector<uint32_t>* dependencies)
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
                tertiary = static_cast<uint8_t>(TextureCache::PaletteToY(static_cast<FilterPaletteID>(image.GetTertiary())));
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
            std::optional<ResolvedSprite> resolved;
            if (dependencies != nullptr)
            {
                const auto asset = _textureCache.ResolveAssetSprite(
                    image, ZoomLevel{ static_cast<int8_t>(zoom) }, *dependencies);
                // Empty object slots still contribute their dependencies to the material lease. Replacing
                // one with a visible sprite invalidates that lease; missing/invalid metadata remains an error.
                if (!asset.sprite && !asset.noZoomDraw && !asset.empty)
                {
                    const auto terminalImage = dependencies->back();
                    const auto* metadata = GfxGetG1Element(terminalImage);
                    throw std::runtime_error(
                        "GPU world sprite is missing or unsupported: image=" + std::to_string(image.GetIndex())
                        + " terminal=" + std::to_string(terminalImage) + " zoom=" + std::to_string(zoom)
                        + " width=" + std::to_string(metadata ? metadata->width : -1)
                        + " height=" + std::to_string(metadata ? metadata->height : -1));
                }
                resolved = asset.sprite;
            }
            else
            {
                resolved = _textureCache.GetOrLoadImageSprite(image, ZoomLevel{ static_cast<int8_t>(zoom) });
            }
            if (!resolved.has_value())
                continue;
            if (residencies != nullptr)
                residencies->push_back(resolved->residencyRevision);
            _surfaceUsesZeroCoverage |= resolved->zeroCoverage.has_value();
            result.variants[zoom - static_cast<int32_t>(kWorldSurfaceMinimumZoom)] = {
                .spriteSize = { resolved->width, resolved->height },
                .spriteOffset = { resolved->xOffset, resolved->yOffset },
                .asset = resolved->descriptorIndex,
                .zoom = static_cast<int8_t>(resolved->zoom),
                .coordinateShift = resolved->coordinateShift,
                .valid = 1 | (resolved->hasRleCompression ? 2 : 0),
            };
        }
        return result;
    }

    bool CommandDrawingContext::IsBalloonFixtureEligible(const PresentationGeneration& generation)
    {
        if (generation.map == nullptr || generation.entities == nullptr || generation.balloons == nullptr
            || generation.entities->GetRetainedBalloons() != generation.balloons
            || !generation.map->CanDrawSurfaceBaseIndependently() || generation.map->GetSurfaceWidth() != 32
            || generation.map->GetSurfaceHeight() != 32 || generation.entities->GetCapturedEntityCount() != kBalloonFixtureCount
            || generation.balloons->count != kBalloonFixtureCount || generation.balloons->chunks[0] == nullptr)
            return false;
        // The published flat-map invariant already excludes interleaved sides/water/objects. Pin this fixture's elevation.
        const auto& surfaceChunks = generation.map->GetSurfaceChunks();
        if (surfaceChunks.empty() || surfaceChunks[0] == nullptr || surfaceChunks[0]->records[0].baseZ != 16)
            return false;
        const auto& snapshot = *generation.balloons;
        const auto& chunk = *snapshot.chunks[0];
        if (_balloonEligibilityEpoch == snapshot.epoch && _balloonEligibilityRevision == chunk.gpuRevision)
            return _balloonEligibility;
        _balloonEligibilityEpoch = snapshot.epoch;
        _balloonEligibilityRevision = chunk.gpuRevision;
        _balloonEligibility = AreBalloonRecordsSupported(snapshot);
        return _balloonEligibility;
    }

    bool CommandDrawingContext::AreBalloonRecordsSupported(const RetainedBalloonSnapshot& snapshot)
    {
        if (snapshot.epoch == 0 || snapshot.sequence == 0 || snapshot.count != kBalloonFixtureCount
            || snapshot.chunks[0] == nullptr || snapshot.chunks[0]->gpuRevision == 0)
            return false;
        const auto& chunk = *snapshot.chunks[0];
        // Structural validation runs only for a changed retained GPU revision, not for camera-only frames.
        for (size_t i = 0; i < chunk.records.size(); i++)
        {
            const auto& record = chunk.records[i];
            if (i >= kBalloonFixtureCount)
            {
                if (record.present != 0)
                    return false;
                continue;
            }
            if (record.present != 1 || record.id != i || record.generation == 0 || record.x < 400 || record.x > 624
                || record.y < 400 || record.y > 624 || record.z < 80 || record.z > 120 || record.colour >= 54
                || record.popped > 1 || record.frame >= 256 || (record.popped != 0 && record.frame >= 5) || record.width != 13
                || record.heightMin != 22 || record.heightMax != 11)
                return false;
        }
        // Count=24 plus exactly24 present records in chunk zero excludes any live record in other chunks.
        return true;
    }

    std::shared_ptr<const BalloonSpriteTable> CommandDrawingContext::ResolveBalloonSprites()
    {
        std::array<WorldSurfaceSpriteVariant, kBalloonSpriteCount> variants{};
        for (uint32_t variant = 0; variant < kBalloonSpriteCount; variant++)
        {
            // Asset residency is shared across all entities and refreshed once per viewport, independent of their frames.
            const auto sprite = _textureCache.GetOrLoadImageSprite(
                ImageId(SPR_BALLOON + variant, Colour::black), ZoomLevel{ 0 });
            if (!sprite.has_value() || sprite->zeroCoverage.has_value() || sprite->zoom != ZoomLevel{ 0 }
                || sprite->coordinateShift != 0 || sprite->width <= 0 || sprite->height <= 0 || sprite->width > 64
                || sprite->height > 64 || sprite->xOffset < -64 || sprite->xOffset > 64 || sprite->yOffset < -64
                || sprite->yOffset > 64)
                return {};
            variants[variant] = {
                { sprite->width, sprite->height }, { sprite->xOffset, sprite->yOffset }, sprite->descriptorIndex, 0, 0, 1
            };
        }
        if (_balloonSprites == nullptr || std::memcmp(variants.data(), _balloonSprites->records.data(), sizeof(variants)) != 0)
        {
            auto table = std::make_shared<BalloonSpriteTable>();
            table->records = variants;
            table->revision = ++_balloonSpriteRevision;
            _balloonSprites = std::move(table);
        }
        return _balloonSprites;
    }

    bool CommandDrawingContext::DrawCompleteTerrainScene(
        RenderTarget& rt, const PresentationGeneration& generation, const OrthographicCamera& camera)
    {
        const bool nativePeeps = generation.entities && generation.entities->IsNativeOnly();
        if ((!Terrain::kRuntimeAdmission && !_nativeTerrainFixture && !nativePeeps) || _admittingTerrainScene
            || !camera.nativeEntitiesAllowed || camera.landscapeSmoothing != 0 || camera.rotation > 3 || camera.zoom < 0
            || camera.zoom > 1 || generation.map == nullptr || generation.entities == nullptr
            || (nativePeeps ? generation.entities->GetUnsupportedEntityCount() != 0
                            : generation.entities->GetCapturedEntityCount() != 0)
            || (nativePeeps
                && (!generation.peeps || !generation.peepAnimations || LightFx::IsAvailable()
                    || !std::isfinite(camera.entityInterpolation) || camera.entityInterpolation < 0
                    || camera.entityInterpolation > 1))
            || rt.width < 1 || rt.width > 3840 || rt.height < 1 || rt.height > 2160 || camera.viewX < -1048576
            || camera.viewX > 1048576 || camera.viewY < -1048576 || camera.viewY > 1048576)
            return false;
        const ScreenRect clip = CalculateClipping(rt);
        // A later clipped viewport gets its own camera and depth interval. No partial CPU/native ownership.
        if (clip.getLeft() != camera.clipLeft || clip.getTop() != camera.clipTop || clip.getRight() != camera.clipRight
            || clip.getBottom() != camera.clipBottom)
            return false;
        _admittingTerrainScene = true;
        struct Reset
        {
            bool& flag;
            ~Reset()
            {
                flag = false;
            }
        } reset{ _admittingTerrainScene };
        if (!_terrainBridge.Update(*generation.map) || !_terrainBridge.ResolveAssets(_textureCache))
            return false;
        std::shared_ptr<const PeepAssetGeneration> peepAssets;
        if (nativePeeps)
        {
            peepAssets = _peepAssets.Resolve(
                _textureCache, generation.peepAnimations, GetContext()->GetObjectManager().GetPeepAnimationCatalog(),
                _terrainBridge.GetSprites());
            if (!peepAssets)
                return false;
        }
        const int32_t base = std::max(_drawCount, 1);
        if (base >= (1 << 22) - static_cast<int32_t>(Terrain::kDrawColumnCapacity))
            return false;
        _commands->terrainScenes.push_back(
            { _terrainBridge.GetSnapshot(),
              peepAssets ? peepAssets->sprites : _terrainBridge.GetSprites(),
              { camera.viewX, camera.viewY, rt.width, rt.height, clip.getLeft(), clip.getTop(), camera.rotation, camera.zoom, 0,
                0, base, Terrain::kDrawColumnCapacity, Terrain::kDrawColumnCapacity, camera.entityInterpolation,
                camera.entityInterpolationSourceTick },
              nativePeeps ? generation.peeps : nullptr,
              std::move(peepAssets) });
        _drawCount = base + static_cast<int32_t>(Terrain::kDrawColumnCapacity);
        return true;
    }

    NativeWorldCategories CommandDrawingContext::DrawWorldScene(
        RenderTarget& rt, std::shared_ptr<const PresentationGeneration> generation, const OrthographicCamera& camera)
    {
        if (generation && generation->entities && generation->entities->IsTerrainOnly())
        {
            if (!generation->map)
                throw std::runtime_error("Missing GPU-only terrain publication");
            // Window splits may revisit the main viewport after higher-depth UI was already recorded.
            // The first visit owns the full main target: never clear or reserve depth on a later visit.
            if (_commands->worldSurfaces.has_value() || generation->map->GetSurfaceRecordCount() == 0)
                return { false, false, 0, true };
            Clear(rt, PaletteIndex::pi10);
            // Partial GPU world ownership is intentional. Secondary viewports are left empty by ViewportPaint.
            const bool surfaces = DrawWorldSurfaceScene(rt, generation, camera);
            return { surfaces, false, 0, true };
        }
        if (generation != nullptr && DrawCompleteTerrainScene(rt, *generation, camera))
            return { true, false, static_cast<uint32_t>(_commands->terrainScenes.size() - 1), true };
        // Diagnostic native-only rejection must not record partial world categories; the viewport fails the frame.
        if (generation && generation->entities && generation->entities->IsNativeOnly())
            return {};
        if (!_nativeBalloonFixture || _admittingBalloonFixture || generation == nullptr || !camera.nativeEntitiesAllowed
            || camera.rotation != 0 || camera.zoom != 0 || camera.landscapeSmoothing != 0 || rt.width <= 0 || rt.height <= 0
            || rt.width > static_cast<int32_t>(kBalloonMaximumExtent) || rt.height > static_cast<int32_t>(kBalloonMaximumExtent)
            || rt.x < -8192 || rt.x > 8192 || rt.y < -8192 || rt.y > 8192 || !IsBalloonFixtureEligible(*generation))
            return { DrawWorldSurfaceScene(rt, std::move(generation), camera), false, 0 };
        _admittingBalloonFixture = true;
        try
        {
            const auto sprites = ResolveBalloonSprites();
            if (sprites == nullptr)
            {
                _admittingBalloonFixture = false;
                return {};
            }
            const ScreenRect clip = CalculateClipping(rt);
            const Int4 bounds{ std::max(clip.getLeft(), camera.clipLeft), std::max(clip.getTop(), camera.clipTop),
                               std::min(clip.getRight(), camera.clipRight), std::min(clip.getBottom(), camera.clipBottom) };
            if (bounds.x >= bounds.z || bounds.y >= bounds.w)
            {
                _admittingBalloonFixture = false;
                return {};
            }
            const bool surfaces = DrawWorldSurfaceScene(rt, generation, camera);
            // Later visible rectangles may use ordinary flat terrain, but all of their balloons still use the native pass.
            const auto index = static_cast<uint32_t>(_commands->balloons.size());
            _commands->balloons.push_back({ generation->balloons, sprites, bounds, { camera.viewX, camera.viewY } });
            _admittingBalloonFixture = false;
            return { surfaces, true, index };
        }
        catch (...)
        {
            _admittingBalloonFixture = false;
            throw;
        }
    }

    void CommandDrawingContext::SealWorldScene(const NativeWorldCategories& categories)
    {
        if (!categories.entities)
            return;
        auto& scene = _commands->balloons.at(categories.submission);
        if (scene.sealed)
            throw std::logic_error("Native balloon viewport was sealed twice");
        // Flat terrain lies below admitted balloons. UI commands recorded after this boundary lie above them.
        // This category interval contains no CPU-derived per-entity graphical decisions or ordering placeholders.
        const auto base = std::max(_drawCount, 1024);
        if (base > (1 << 22) - static_cast<int32_t>(kBalloonRecordCapacity) - 1)
            throw std::overflow_error("Native balloon painter depth capacity exceeded");
        scene.depthBase = static_cast<uint32_t>(base);
        scene.sealed = true;
        _drawCount = base + static_cast<int32_t>(kBalloonRecordCapacity);
    }

    bool CommandDrawingContext::DrawWorldSurfaceScene(
        RenderTarget& rt, std::shared_ptr<const PresentationGeneration> generation, const OrthographicCamera& camera)
    {
        assert(_inDraw);
        if (generation == nullptr || generation->map == nullptr || _commands->worldSurfaces.has_value())
            return false;
        const bool terrainOnly = generation->entities && generation->entities->IsTerrainOnly();
        if (generation->map->GetSurfaceRecordCount() == 0)
            return false; // An empty publication still belongs to the GPU-only viewport.
        if (!terrainOnly
            && (!generation->map->CanDrawSurfaceBaseIndependently()
                || (!_admittingBalloonFixture && generation->entities != nullptr
                    && generation->entities->GetCapturedEntityCount() != 0)
                || camera.landscapeSmoothing != 0))
            return false;
        if (terrainOnly
            && (camera.rotation > 3 || camera.zoom < kWorldSurfaceMinimumZoom || camera.zoom > kWorldSurfaceMaximumZoom
                || camera.viewX < -1048576 || camera.viewX > 1048576 || camera.viewY < -1048576 || camera.viewY > 1048576))
            throw std::invalid_argument("GPU-only terrain camera is outside the supported range");

        const ScreenRect clip = CalculateClipping(rt);
        const Int4 cameraClip{
            std::max(clip.getLeft(), camera.clipLeft),
            std::max(clip.getTop(), camera.clipTop),
            std::min(clip.getRight(), camera.clipRight),
            std::min(clip.getBottom(), camera.clipBottom),
        };
        if (cameraClip.x >= cameraClip.z || cameraClip.y >= cameraClip.w)
            return false;
        // Validate before claiming the slot. Commit only after atlas resolution,
        // which can re-enter painting and advance the ordinary command sequence.
        if (!GetWorldSurfaceDepthRange(_drawCount, generation->map->GetSurfaceRecordCount()).has_value())
        {
            if (terrainOnly)
                throw std::overflow_error("GPU-only terrain depth capacity exceeded");
            return false;
        }
        // Claim the slot before atlas resolution: first residency can invalidate and re-enter viewport painting.
        _surfaceUsesZeroCoverage = false;
        auto& scene = _commands->worldSurfaces.emplace(WorldSurfaceSceneCommand{
            .worldEpoch = generation->map->GetEpoch(),
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
            _publishedSurfaceSprites.reset();
        }

        const auto& sourceChunks = generation->map->GetSurfaceChunks();
        const auto& pathChunks = generation->map->GetPathChunks();
        const auto& objectChunks = generation->map->GetObjectChunks();
        if (!objectChunks.empty() && objectChunks.size() != sourceChunks.size())
            throw std::runtime_error("GPU object chunk directory differs from terrain");
        if (!pathChunks.empty() && pathChunks.size() != sourceChunks.size())
            throw std::runtime_error("GPU path chunk directory differs from terrain");
        _surfaceChunks.resize(sourceChunks.size());
        scene.chunks.resize(sourceChunks.size());
        for (size_t chunkIndex = 0; chunkIndex < sourceChunks.size(); chunkIndex++)
        {
            const auto& source = sourceChunks[chunkIndex];
            if (source == nullptr)
                throw std::runtime_error("GPU terrain publication has an absent surface chunk");
            const auto paths = pathChunks.empty() ? nullptr : pathChunks[chunkIndex];
            const auto pathRevision = paths ? paths->revision : 0;
            const auto objects = objectChunks.empty() ? nullptr : objectChunks[chunkIndex];
            const auto objectRevision = objects ? objects->revision : 0;
            auto& published = _surfaceChunks[chunkIndex];
            if (published.sourceRevision != source->revision || published.pathRevision != pathRevision
                || published.objectRevision != objectRevision || published.gpu == nullptr)
            {
                auto converted = std::make_shared<WorldSurfaceChunk>();
                converted->revision = ++_nextSurfaceChunkRevision;
                if (paths)
                {
                    converted->paths.reserve(paths->records.size());
                    for (const auto& raw : paths->records)
                        converted->paths.push_back({ raw.baseZ, raw.clearanceZ, raw.elementOrdinal, raw.flags, raw.surfaceSlot,
                                                     raw.railingsSlot, raw.additionSlot, raw.rideId, raw.edgesAndCorners,
                                                     raw.slopeDirection, raw.queueBannerDirection, raw.additionStatus });
                }
                if (objects)
                {
                    converted->objects.reserve(objects->records.size());
                    for (const auto& raw : objects->records)
                        converted->objects.push_back(
                            { raw.baseZ, raw.clearanceZ, raw.elementOrdinal, raw.flags, raw.objectSlot,
                              static_cast<uint32_t>(raw.kind), raw.direction, raw.sequence,
                              uint32_t(raw.primaryColour) | (uint32_t(raw.secondaryColour) << 8)
                                  | (uint32_t(raw.tertiaryColour) << 16),
                              uint32_t(raw.age) | (uint32_t(raw.quadrant) << 8) | (uint32_t(raw.slope) << 16)
                                  | (uint32_t(raw.position) << 24),
                              uint32_t(raw.animationFrame) | (uint32_t(raw.allowedEdges) << 8), 0,
                              uint32_t(raw.trackType) | (uint32_t(raw.rideType) << 16),
                              uint32_t(raw.rideId) | (uint32_t(raw.mazeEntry) << 16),
                              uint32_t(raw.colourScheme) | (uint32_t(raw.stationIndex) << 8)
                                  | (uint32_t(raw.brakeBoosterSpeed) << 16) | (uint32_t(raw.photoTimeout) << 24),
                              uint32_t(raw.seatRotation) | (uint32_t(raw.doorA) << 8) | (uint32_t(raw.doorB) << 16) });
                }
                for (size_t i = 0; i < source->records.size(); i++)
                {
                    const auto& raw = source->records[i].terrain;
                    auto& target = converted->records[i];
                    target = { raw.baseZ, raw.waterHeight, raw.surfaceSlot, raw.edgeSlot,
                               raw.slope, raw.grass,       raw.present,     raw.kind };
                    if (objects)
                    {
                        const auto range = objects->tiles[i];
                        if (range.first > converted->objects.size() || range.count > converted->objects.size() - range.first)
                            throw std::runtime_error("GPU object publication has an invalid tile range");
                        target.objectFirst = range.first;
                        target.objectCount = range.count;
                        for (uint32_t p = range.first; p < range.first + range.count; p++)
                            target.objectMaxZ = std::max(
                                target.objectMaxZ,
                                std::max(converted->objects[p].baseZ + 32, converted->objects[p].clearanceZ));
                    }
                    if (paths)
                    {
                        const auto range = paths->tiles[i];
                        if (range.first > converted->paths.size() || range.count > converted->paths.size() - range.first)
                            throw std::runtime_error("GPU path publication has an invalid tile range");
                        target.pathFirst = range.first;
                        target.pathCount = range.count;
                        for (uint32_t p = range.first; p < range.first + range.count; p++)
                            target.pathMaxZ = std::max(
                                target.pathMaxZ, std::max(converted->paths[p].baseZ + 32, converted->paths[p].clearanceZ));
                    }
                }
                // Only dirty publications are compared. Unrelated mutations need no world transfer.
                if (!published.gpu
                    || std::memcmp(
                           published.gpu->records.data(), converted->records.data(),
                           converted->records.size() * sizeof(WorldSurfaceSourceRecord))
                        != 0
                    || published.gpu->objects.size() != converted->objects.size()
                    || (!converted->objects.empty()
                        && std::memcmp(
                               published.gpu->objects.data(), converted->objects.data(),
                               converted->objects.size() * sizeof(WorldObjectSourceRecord))
                            != 0)
                    || published.gpu->paths.size() != converted->paths.size()
                    || (!converted->paths.empty()
                        && std::memcmp(
                               published.gpu->paths.data(), converted->paths.data(),
                               converted->paths.size() * sizeof(WorldPathSourceRecord))
                            != 0))
                    published.gpu = std::move(converted);
                published.sourceRevision = source->revision;
                published.pathRevision = pathRevision;
                published.objectRevision = objectRevision;
            }
            scene.chunks[chunkIndex] = published.gpu;
        }

        const auto materials = generation->map->GetTerrainMaterials();
        const auto pathMaterials = generation->map->GetPathMaterials();
        const auto objectMaterials = generation->map->GetObjectMaterials();
        const auto objectUsage = generation->map->GetObjectUsage();
        const auto rideMaterials = generation->map->GetRideMaterials();
        if (!materials)
            throw std::runtime_error("GPU terrain material generation is absent");
        if (!_publishedSurfaceSprites || _publishedSurfaceSprites->sourceMaterials != materials
            || _publishedSurfaceSprites->sourcePathMaterials != pathMaterials
            || _publishedSurfaceSprites->sourceObjectMaterials != objectMaterials
            || _publishedSurfaceSprites->sourceObjectUsage != objectUsage
            || _publishedSurfaceSprites->sourceRideMaterials != rideMaterials || !terrainOnly
            || !_textureCache.TryBindAssetLease(_publishedSurfaceSprites->residency))
        {
            if (materials->revision != GetTerrainObjectRevision()
                || (pathMaterials && pathMaterials->revision != GetPathObjectRevision())
                || (objectMaterials && objectMaterials->revision != GetWorldObjectRevision()))
                throw std::runtime_error("GPU terrain cannot resolve a retired material generation");
            auto table = std::make_shared<WorldSurfaceSpriteTable>();
            table->revision = ++_nextSurfaceSpriteRevision;
            table->sourceMaterials = materials;
            table->sourcePathMaterials = pathMaterials;
            table->sourceObjectMaterials = objectMaterials;
            table->sourceObjectUsage = objectUsage;
            table->sourceRideMaterials = rideMaterials;
            table->catalog.reserved = TextureCache::PaletteToY(FilterPaletteID::paletteGhost);
            std::vector<uint64_t> residencies;
            std::vector<uint32_t> dependencies;
            const auto append = [&](ImageId image) {
                if (table->records.size() >= kWorldSurfaceMaximumSpriteSetCount)
                    throw std::overflow_error("GPU terrain material sprite capacity exceeded");
                const auto index = static_cast<uint32_t>(table->records.size());
                table->records.push_back(ResolveSurfaceSpriteSet(
                    image, terrainOnly ? &residencies : nullptr, terrainOnly ? &dependencies : nullptr));
                for (size_t zoom = 0; zoom < kWorldSurfaceZoomCount; zoom++)
                {
                    const auto& sprite = table->records.back().variants[zoom];
                    if (!sprite.valid)
                        continue;
                    const auto scale = [z = sprite.zoom](int32_t value) { return z >= 0 ? value >> z : value * (1 << -z); };
                    auto& bounds = table->catalog.spriteEnvelope[zoom];
                    bounds.x = std::min(bounds.x, scale(sprite.spriteOffset.x) - 2);
                    bounds.y = std::min(bounds.y, scale(sprite.spriteOffset.y) - 2);
                    bounds.z = std::max(bounds.z, scale(sprite.spriteOffset.x + sprite.spriteSize.x) + 3);
                    bounds.w = std::max(bounds.w, scale(sprite.spriteOffset.y + sprite.spriteSize.y) + 3);
                }
                return index;
            };
            for (size_t slot = 0; slot < materials->surfaces.size(); slot++)
            {
                auto& target = table->catalog.materials[slot];
                const auto& surface = materials->surfaces[slot];
                if (surface.supported)
                {
                    target.surfaceBase = static_cast<uint32_t>(table->records.size());
                    target.surfaceCount = surface.imageCount;
                    target.selectors = surface.selectors;
                    for (uint32_t image = 0; image < surface.imageCount; image++)
                        append(ImageId(surface.imageBase + image));
                }
                const auto& edge = materials->edges[slot];
                if (edge.supported)
                {
                    target.edgeBase = static_cast<uint32_t>(table->records.size());
                    target.edgeCount = edge.imageCount;
                    for (uint32_t image = 0; image < edge.imageCount; image++)
                        append(ImageId(edge.imageBase + image));
                }
            }
            if (pathMaterials)
            {
                // Resolve whole immutable material ranges once per catalog/atlas generation, never per path instance.
                const auto appendRange = [&](uint32_t allocationBase, uint32_t allocationCount, uint32_t first, uint32_t limit,
                                             uint32_t& base, uint32_t& count) {
                    if (first < allocationBase || uint64_t(first) >= uint64_t(allocationBase) + allocationCount)
                        throw std::runtime_error("GPU path material range exceeds its owning object allocation");
                    base = static_cast<uint32_t>(table->records.size());
                    count = static_cast<uint32_t>(
                        std::min<uint64_t>(limit, uint64_t(allocationBase) + allocationCount - first));
                    for (uint32_t i = 0; i < count; i++)
                    {
                        append(ImageId(first + i));
                        for (auto& variant : table->records.back().variants)
                            if (variant.valid != 0)
                                variant.valid |= 4; // Path-only original bitmap/RLE sampling contract.
                    }
                };
                for (uint32_t legacy = 0; legacy < 2; legacy++)
                    for (size_t slot = 0; slot < 255; slot++)
                    {
                        auto& target = table->catalog.paths[legacy * 255 + slot];
                        const auto& surface = legacy ? pathMaterials->legacySurfaces[slot] : pathMaterials->surfaces[slot];
                        const auto& queue = legacy ? pathMaterials->legacyQueueSurfaces[slot] : surface;
                        const auto& railings = legacy ? pathMaterials->legacyRailings[slot] : pathMaterials->railings[slot];
                        if (surface.present)
                        {
                            appendRange(
                                surface.imageBase, surface.imageCount, surface.surfaceImage, 51, target.surfaceBase,
                                target.surfaceCount);
                            target.reserved = surface.flags;
                        }
                        if (queue.present)
                            appendRange(
                                queue.imageBase, queue.imageCount, queue.surfaceImage, 20, target.queueBase, target.queueCount);
                        if (railings.present)
                        {
                            appendRange(
                                railings.imageBase, railings.imageCount, railings.railingsImage, 36, target.railingsBase,
                                target.railingsCount);
                            appendRange(
                                railings.imageBase, railings.imageCount, railings.bridgeImage, 55, target.bridgeBase,
                                target.bridgeCount);
                            target.flags = railings.flags;
                            target.supportType = railings.supportType;
                            target.supportColour = railings.supportColour;
                        }
                    }
                for (size_t slot = 0; slot < 255; slot++)
                {
                    const auto& addition = pathMaterials->additions[slot];
                    if (!addition.present)
                        continue;
                    auto& target = table->catalog.additions[slot];
                    appendRange(addition.imageBase, addition.imageCount, addition.image, 16, target.base, target.count);
                    target.flags = addition.flags;
                    target.drawType = addition.drawType;
                }
            }
            if (objectMaterials)
                table->propCatalog = BuildWorldPropCatalog(
                                         *objectMaterials, TextureCache::PaletteToY(FilterPaletteID::paletteGlass),
                                         [&](uint32_t image) {
                                             const auto index = append(ImageId(image));
                                             for (auto& variant : table->records.back().variants)
                                                 if (variant.valid != 0)
                                                     variant.valid |= 4;
                                             return index;
                                         },
                                         objectUsage.get())
                                         .words;
            if (rideMaterials)
                table->trackCatalog = BuildWorldTrackCatalog(*rideMaterials, [&](uint32_t image) {
                                          const auto index = append(ImageId(image));
                                          for (auto& variant : table->records.back().variants)
                                              if (variant.valid != 0)
                                                  variant.valid |= 4;
                                          return index;
                                      }).words;
            for (uint32_t shape = 0; shape < 5; shape++)
            {
                // The water mask contains filter-row offsets, not ordinary remapped colours.
                table->catalog.waterMask[shape] = append(ImageId(SPR_WATER_MASK + shape));
                table->records.back().effects |= 1u << 8;
                table->records.back().palettes = TextureCache::PaletteToY(FilterPaletteID::paletteWater);
                table->catalog.waterOverlay[shape] = append(ImageId(SPR_WATER_OVERLAY + shape));
                table->records.back().effects |= 1u << 9;
                table->catalog.waterOpaque[shape] = append(ImageId(SPR_G2_OPAQUE_WATER_OVERLAY + shape));
                table->records.back().effects |= 1u << 9;
            }
            if (_surfaceUsesZeroCoverage)
                throw std::runtime_error("GPU terrain material has unsupported covered-zero pixels");
            if (terrainOnly)
            {
                table->residency = _textureCache.CreateAssetLease(residencies, dependencies);
                if (!_textureCache.TryBindAssetLease(table->residency))
                    throw std::runtime_error("GPU terrain atlas generation invalidated during preparation");
            }
            _publishedSurfaceSprites = std::move(table);
        }
        scene.sourceTick = generation->sourceTick;
        scene.clockMinute = generation->map->GetClockMinute();
        scene.clockHour = generation->map->GetClockHour();
        scene.transparentWater = Config::Get().general.transparentWater ? 1u : 0u;
        scene.sprites = _publishedSurfaceSprites;
        const auto depthRange = GetWorldSurfaceDepthRange(_drawCount, scene.recordCount);
        if (scene.sprites == nullptr || !depthRange.has_value())
        {
            _commands->worldSurfaces.reset();
            if (terrainOnly)
                throw std::runtime_error("GPU-only terrain publication lost its assets or depth range");
            return false;
        }
        // Native base sprites precede the ordinary blank-tile/overlay commands
        // and later translucent windows. Reserve even culled records so camera
        // movement cannot change their relationship to the UI painter sequence.
        scene.depthBase = depthRange->first;
        _drawCount = depthRange->next;
        return true;
    }

    RectCommand& CommandDrawingContext::AppendRect(
        CommandBatch<RectCommand>& batch, const ScreenRect& clip, Int4 bounds, float zoom)
    {
        auto& command = batch.allocate();
        command = {
            .clip = { clip.getLeft(), clip.getTop(), clip.getRight(), clip.getBottom() },
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
