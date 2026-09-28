// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "../core/StringTypes.h"
#include "../interface/ColourWithFlags.h"
#include "Font.h"

#include <cstdint>
#include <vector>

namespace OpenRCT2::Drawing
{
    struct TextGlyphPiece
    {
        int32_t x{}, y{}, width{}, height{};
        // Sprite characters advance the legacy wave cursor; TTF literal runs do not.
        uint32_t waveOrdinal{ UINT32_MAX };
        uint32_t kind{}, ink{}, hintThreshold{}; // kind0 indexed pixels, kind1 TTF coverage.
        std::vector<uint8_t> pixels;
        bool operator==(const TextGlyphPiece&) const = default;
    };
    struct TextGlyphRun
    {
        uint32_t waveCount{};
        std::vector<TextGlyphPiece> pieces;
        bool operator==(const TextGlyphRun&) const = default;
    };
    // Font/formatting compilation only. Owns asset pixels, never rasterises a world
    // target; callers retain the result until text, language or font assets change.
    TextGlyphRun CompileTextGlyphRun(u8string_view text, ColourWithFlags colour, FontStyle fontStyle, bool forceSpriteFont);
} // namespace OpenRCT2::Drawing
