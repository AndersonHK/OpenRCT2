/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include "ScreenshotTiling.h"
#include "../drawing/RenderTarget.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace OpenRCT2::ScreenshotTiling
{
    using namespace Drawing;
    namespace
    {
        [[noreturn]] void Invalid(const char* message)
        {
            throw RenderServiceException({ RenderErrorCode::invalidRequest, message });
        }
        void ValidateViewport(const Viewport& viewport)
        {
            if (viewport.width <= 0 || viewport.height <= 0 || viewport.pos.x != 0 || viewport.pos.y != 0
                || viewport.zoom < ZoomLevel::min() || viewport.zoom > ZoomLevel::max())
                Invalid("Tiled screenshot needs a positive, origin-zero viewport and supported zoom");
        }
    }

    Viewport TileViewport(const Viewport& whole, const Tile& tile)
    {
        ValidateViewport(whole);
        if (tile.x < 0 || tile.y < 0 || tile.width <= 0 || tile.height <= 0 || tile.width > kTileSide
            || tile.height > kTileSide || static_cast<int64_t>(tile.x) + tile.width > whole.width
            || static_cast<int64_t>(tile.y) + tile.height > whole.height)
            Invalid("Screenshot tile lies outside its bounded full viewport");
        auto viewport = whole;
        // Keep the original camera and full dimensions. ViewportPaint first floors
        // viewPos to zoomed pixels, then adds rt.pos - viewport.pos. Moving the camera
        // instead would lose the raster phase at negative zooms or odd origins.
        viewport.pos = { -tile.x, -tile.y };
        return viewport;
    }

    Image Render(IRenderService& service, const Viewport& whole, const GamePalette& palette, const PaintTile& paint)
    {
        ValidateViewport(whole);
        if (!paint)
            Invalid("Tiled screenshot needs a viewport recorder");
        const auto count = static_cast<uint64_t>(whole.width) * static_cast<uint64_t>(whole.height);
        Image image;
        if (count > std::numeric_limits<size_t>::max() || count > image.Pixels.max_size())
            Invalid("Giant screenshot dimensions exceed addressable indexed image storage");
        image.Width = static_cast<uint32_t>(whole.width);
        image.Height = static_cast<uint32_t>(whole.height);
        image.Depth = 8;
        image.Stride = image.Width;
        image.Palette = palette;
        // Allocate the final owned CPU result before creating any auxiliary session.
        image.Pixels.resize(static_cast<size_t>(count));
        RenderPalette renderPalette{};
        for (size_t i = 0; i < palette.size(); ++i)
            renderPalette[i] = { palette[i].red, palette[i].green, palette[i].blue, palette[i].alpha };
        uint64_t ordinal = 0;
        for (int32_t y = 0; y < whole.height;)
        {
            const auto height = std::min(kTileSide, whole.height - y);
            for (int32_t x = 0; x < whole.width;)
            {
                const auto width = std::min(kTileSide, whole.width - x);
                const Tile tile{ x, y, width, height };
                const auto viewport = TileViewport(whole, tile);
                OffscreenRenderRequest request;
                request.name = "screenshot-cli-giant-tile-" + std::to_string(ordinal++);
                request.logicalExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
                request.outputExtent = request.logicalExtent;
                request.clearIndex = 0;
                request.palette = renderPalette;
                request.alphaPolicy = RenderAlphaPolicy::transparentIndexZero;
                request.indexedOutput = true;
                request.rgbaOutput = false;
                const auto extent = request.logicalExtent;
                auto session = service.BeginOffscreen(std::move(request));
                if (!session)
                    throw RenderServiceException({ RenderErrorCode::creationFailed, "Giant screenshot has no tile session" });
                auto& target = session->GetRenderTarget();
                if (target.x != 0 || target.y != 0 || target.width != width || target.height != height)
                    throw RenderServiceException({ RenderErrorCode::executionFailed, "Giant screenshot tile target differs" });
                paint(target, viewport);
                auto completion = session->Submit();
                if (!completion)
                    throw RenderServiceException({ RenderErrorCode::executionFailed, "Giant screenshot tile has no completion" });
                const auto outcome = completion->Wait(std::chrono::seconds(120));
                if (outcome.error)
                    throw RenderServiceException(*outcome.error);
                if (!outcome.result)
                    throw RenderServiceException({ RenderErrorCode::executionFailed, "Giant screenshot tile has no owned output" });
                const auto& result = *outcome.result;
                const auto pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
                if (outcome.identity != completion->GetIdentity() || result.identity != outcome.identity
                    || result.identity.submissionId == 0 || result.identity.targetId == 0 || result.identity.targetGeneration == 0
                    || result.logicalExtent != extent || result.outputExtent != extent || result.palette != renderPalette
                    || result.indexed.size() != pixels || !result.rgba.empty())
                    throw RenderServiceException({ RenderErrorCode::executionFailed, "Giant screenshot tile readback contract differs" });
                for (int32_t row = 0; row < height; ++row)
                {
                    const auto destination = static_cast<size_t>(y + row) * image.Stride + static_cast<size_t>(x);
                    const auto source = static_cast<size_t>(row) * static_cast<size_t>(width);
                    std::memcpy(image.Pixels.data() + destination, result.indexed.data() + source, static_cast<size_t>(width));
                }
                // The session/result die before the next BeginOffscreen: at most one
                // tile is recorded/submitted, with no full-sized GPU target or upload.
                x += width;
            }
            y += height;
        }
        return image;
    }
}
