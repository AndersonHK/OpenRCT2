/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../core/StringTypes.h"

#include <array>
#include <cstdint>
#include <vector>

struct ImageId;
struct PaintSession;
using StringId = uint16_t;

constexpr uint8_t kScrollingModeNone = 255;

namespace OpenRCT2::Drawing
{
    enum class PaletteIndex : uint8_t;
}

namespace OpenRCT2::Drawing::ScrollingText
{
    static auto constexpr kMaxLegacyEntries = 32;
    static auto constexpr kMaxEntries = 256;
    constexpr int8_t kMaxModes = 38;
    constexpr auto kParkBannerColourPrefix = "{WHITE}";
    constexpr auto kRideBannerColourPrefix = "{YELLOW}";

    // Immutable glyph columns, compiled only when text or font assets change. Scroll phase
    // and placement remain GPU work. Sprite fonts retain the original four-repeat bound;
    // TrueType columns wrap independently of the formatted string's phase width.
    struct TextColumns
    {
        uint32_t phaseWidth{};
        bool repeat{};
        std::vector<std::array<uint8_t, 8>> columns;
        bool operator==(const TextColumns&) const = default;
    };
    struct ModeColumn
    {
        uint16_t sourceColumn{ UINT16_MAX };
        uint8_t y{};
    };
    using ModeColumns = std::array<std::array<ModeColumn, 64>, kMaxModes>;

    TextColumns compileTextColumns(u8string_view string, PaletteIndex colour);
    const ModeColumns& getModeColumns();
    uint64_t getAssetRevision() noexcept;

    void initialiseBitmaps();
    void invalidate();
    ImageId setup(PaintSession& session, u8string_view string, uint16_t scrollingMode, PaletteIndex colour);
} // namespace OpenRCT2::Drawing::ScrollingText
