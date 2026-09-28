/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "PaletteIndex.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include <vector>

namespace OpenRCT2
{
    struct G1Element;
}

namespace OpenRCT2::Drawing
{
    struct DecodedSpriteAsset
    {
        // Asset-local row-major indices. Offsets, zoom, clipping and destination effects are not applied.
        std::vector<PaletteIndex> pixels;
        // Distinguishes encoded index zero from an RLE hole or a transparent/remapped-away bitmap pixel.
        std::vector<uint8_t> coverage;
    };

    // Expands bitmap/RLE asset bytes; throws invalid_argument for malformed data or unsupported RGB palettes.
    // A nonempty remap maps source indices, preserving transparent zero and transparent map entries.
    [[nodiscard]] DecodedSpriteAsset DecodeSpriteAsset(
        const G1Element& element, std::span<const uint8_t> data, std::span<const PaletteIndex> remap = {});

    // For resident G1 assets only. G1Element does not carry an allocation length, so the caller must guarantee
    // that offset addresses a complete, valid asset. This overload cannot detect truncated backing storage.
    // Use the bounded overload when the owning byte span is available (e.g. imports and tests).
    [[nodiscard]] DecodedSpriteAsset DecodeTrustedSpriteAsset(
        const G1Element& element, std::span<const PaletteIndex> remap = {});

    struct InferredSpriteAssetBounds
    {
        uint8_t width{};
        uint8_t heightNegative{};
        uint8_t heightPositive{};
    };

    // Legacy vehicle/peep metadata inference uses an ordered sequence of assets in a fixed 200x200
    // region about the origin. Occupancy retains zero-clearing semantics without a colour framebuffer.
    class SpriteAssetBoundsAccumulator
    {
        std::bitset<200 * 200> _occupied;

    public:
        void Add(const G1Element& element);
        [[nodiscard]] InferredSpriteAssetBounds GetBounds() const;
    };

    // Tiny scrolling-text font mask: eight columns, bit y is the font-fill sample at screen (x,y)
    // when the glyph origin is (-1,0). Resident asset storage has the same trust contract as above.
    [[nodiscard]] std::array<uint8_t, 8> ExtractScrollingGlyphMask(const G1Element& element);
} // namespace OpenRCT2::Drawing
