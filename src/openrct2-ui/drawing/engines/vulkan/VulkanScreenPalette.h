/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <array>
#include <cstddef>
#include <openrct2/drawing/PaletteType.h>

namespace OpenRCT2::Ui::Vulkan
{
    // Main-window palette conversion, separated from auxiliary-image palette
    // contracts so final display bytes can be compared directly with SDL.
    [[nodiscard]] inline std::array<std::byte, 256 * 4> ConvertScreenPalette(const Drawing::GamePalette& palette)
    {
        std::array<std::byte, 256 * 4> result{};
        for (size_t i = 0; i < palette.size(); i++)
        {
            result[i * 4] = static_cast<std::byte>(palette[i].red);
            result[i * 4 + 1] = static_cast<std::byte>(palette[i].green);
            result[i * 4 + 2] = static_cast<std::byte>(palette[i].blue);
            // SDL_MapRGB makes the displayed screen opaque, including index zero.
            // Indexed screenshot/export transparency is a separate contract.
            result[i * 4 + 3] = std::byte{ 255 };
        }
        return result;
    }
} // namespace OpenRCT2::Ui::Vulkan
