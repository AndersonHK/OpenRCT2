/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuCommandStream.h"
#include "GpuTextureCache.h"

#include <array>
#include <cstddef>
#include <openrct2/drawing/IDrawingContext.h>

namespace OpenRCT2::Drawing
{
    struct RenderTarget;
}

namespace OpenRCT2::Ui::Gpu
{
    /** Records the legacy indexed drawing contract without rasterising a canvas. */
    class CommandDrawingContext final : public Drawing::IDrawingContext
    {
    private:
        Drawing::RenderTarget& _mainTarget;
        TextureCache& _textureCache;
        FrameCommandStream* _commands = nullptr;
        int32_t _drawCount = 0;
        bool _inDraw = false;

        struct ClippingCacheEntry
        {
            const Drawing::PaletteIndex* bits = nullptr;
            int32_t width = 0;
            int32_t height = 0;
            int32_t stride = 0;
            ScreenRect clip{ 0, 0, 0, 0 };
        };

        static constexpr size_t kClippingCacheSize = 16;
        mutable std::array<ClippingCacheEntry, kClippingCacheSize> _clippingCache{};

    public:
        CommandDrawingContext(Drawing::RenderTarget& mainTarget, TextureCache& textureCache);

        void Begin(FrameCommandStream& commands);
        void End();
        void Resize();
        [[nodiscard]] bool IsActive() const noexcept;

        void Clear(Drawing::RenderTarget& rt, Drawing::PaletteIndex paletteIndex) override;
        void FillRect(
            Drawing::RenderTarget& rt, Drawing::PaletteIndex paletteIndex, int32_t left, int32_t top, int32_t right,
            int32_t bottom, bool crossHatch = false) override;
        void FilterRect(
            Drawing::RenderTarget& rt, Drawing::FilterPaletteID palette, int32_t left, int32_t top, int32_t right,
            int32_t bottom) override;
        void DrawLine(Drawing::RenderTarget& rt, Drawing::PaletteIndex colour, const ScreenLine& line) override;
        void DrawSprite(Drawing::RenderTarget& rt, ImageId imageId, int32_t x, int32_t y) override;
        void DrawSpriteRawMasked(
            Drawing::RenderTarget& rt, int32_t x, int32_t y, ImageId maskImage, ImageId colourImage) override;
        void DrawSpriteSolid(
            Drawing::RenderTarget& rt, ImageId image, int32_t x, int32_t y, Drawing::PaletteIndex colour) override;
        void DrawGlyph(
            Drawing::RenderTarget& rt, ImageId image, int32_t x, int32_t y, const Drawing::PaletteMap& palette) override;
        void DrawTTFBitmap(
            Drawing::RenderTarget& rt, const Drawing::TextDrawInfo& info, TTFSurface* surface, int32_t x,
            int32_t y, uint8_t hintingThreshold) override;

    private:
        static uint8_t ComputeOutCode(ScreenCoordsXY point, ScreenCoordsXY topLeft, ScreenCoordsXY bottomRight);
        static bool CohenSutherlandLineClip(ScreenLine& line, const Drawing::RenderTarget& rt);
        RectCommand& AppendRect(
            CommandBatch<RectCommand>& batch, const ScreenRect& clip, Int4 bounds, float zoom = 1.0f);
        [[nodiscard]] ScreenRect CalculateClipping(const Drawing::RenderTarget& rt) const;
    };
} // namespace OpenRCT2::Ui::Gpu
