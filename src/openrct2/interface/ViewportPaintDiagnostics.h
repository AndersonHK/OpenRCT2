// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#define OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS_VERSION 1

namespace OpenRCT2::Drawing::Diagnostic
{
    // No shared class layout depends on the diagnostic compile option.
    struct ViewportPaintCounts
    {
        uint64_t generate{}, arrange{}, draw{};
    };
#ifdef OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS
    // UI owner resets before a named synchronous paint and reads after its scene jobs join.
    void ResetViewportPaintCounts() noexcept;
    ViewportPaintCounts ReadViewportPaintCounts() noexcept;
#endif
}
