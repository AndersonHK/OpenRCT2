/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once
#include "../core/Imaging.h"
#include "../drawing/RenderService.h"
#include "Viewport.h"
#include <functional>

namespace OpenRCT2::ScreenshotTiling
{
    // This is a client-side subdivision of the existing bounded auxiliary pool,
    // not a larger GPU allocation or a second renderer/device.
    constexpr int32_t kTileSide = 2048;
    struct Tile
    {
        int32_t x{}, y{}, width{}, height{};
    };
    Viewport TileViewport(const Viewport& whole, const Tile& tile);
    using PaintTile = std::function<void(Drawing::RenderTarget&, const Viewport&)>;
    // Own the assembled indexed image on the CPU; screenshot readback is intentional.
    // No image is returned if any recording, submission, readback or validation fails.
    Image Render(Drawing::IRenderService& service, const Viewport& whole,
                 const Drawing::GamePalette& palette, const PaintTile& paint);
}
