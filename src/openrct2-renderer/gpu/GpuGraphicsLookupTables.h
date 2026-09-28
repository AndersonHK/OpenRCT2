/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once
#include <array>
#include <cstddef>

namespace OpenRCT2::Ui::Gpu
{
    // Capture on the graphics owner thread after loading assets. Returned bytes have no live asset dependencies.
    std::array<std::byte, 256 * 256> BuildRemapPalette();
    struct GraphicsLookupTables
    {
        std::array<std::byte, 256 * 256> remap{};
        std::array<std::byte, 256 * 256> blend{};
        bool hasBlend{};
    };
    GraphicsLookupTables CaptureGraphicsLookupTables();
} // namespace OpenRCT2::Ui::Gpu
