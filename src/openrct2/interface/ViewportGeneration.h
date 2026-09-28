// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "../drawing/RenderTarget.h"
#include <limits>
#include <optional>
#include <stdexcept>

namespace OpenRCT2
{
    enum class ViewportGenerationDomain
    {
        targetClip,
        fullViewportHeight,
    };

    // Zoomed world-plane pixels, independent of the writable target's clip/stride.
    struct ViewportGenerationBounds
    {
        int32_t top{}, height{};
    };

    // Generate/Arrange append and order paint structures; they do not rasterize.
    // Restore the actual tile target before any drawing, also when generation throws.
    class ScopedViewportGenerationTarget final
    {
        Drawing::RenderTarget& _target;
        Drawing::RenderTarget _saved;
        bool _active;

    public:
        ScopedViewportGenerationTarget(
            Drawing::RenderTarget& target, const std::optional<ViewportGenerationBounds>& bounds)
            : _target(target), _saved(target), _active(bounds.has_value())
        {
            if (!_active) return;
            const auto bottom = int64_t{ bounds->top } + bounds->height;
            if (bounds->height <= 0 || bottom > std::numeric_limits<int32_t>::max()
                || bounds->top > target.y || bottom < int64_t{ target.y } + target.height)
                throw std::invalid_argument("Viewport generation bounds must contain the raster target");
            _target.y = bounds->top;
            _target.height = bounds->height;
            _target.bits = nullptr; // No writable pixel storage in the generation-only view.
        }
        ~ScopedViewportGenerationTarget()
        {
            if (_active) _target = _saved;
        }
        ScopedViewportGenerationTarget(const ScopedViewportGenerationTarget&) = delete;
        ScopedViewportGenerationTarget& operator=(const ScopedViewportGenerationTarget&) = delete;
    };
}
