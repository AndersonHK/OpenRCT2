/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_VULKAN

    #include <algorithm>
    #include <array>
    #include <optional>
    #include <span>
    #include <vulkan/vulkan.h>

namespace OpenRCT2::Ui::Vulkan
{
    struct SurfaceFormatSelection
    {
        VkSurfaceFormatKHR surfaceFormat{};
        bool hdr10Available = false;
        bool hdr10Active = false;
    };

    [[nodiscard]] constexpr bool IsHdr10SurfaceFormat(VkSurfaceFormatKHR surfaceFormat) noexcept
    {
        const bool tenBit = surfaceFormat.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32
            || surfaceFormat.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        return tenBit && surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT;
    }

    [[nodiscard]] constexpr bool IsSdrSurfaceFormat(VkSurfaceFormatKHR surfaceFormat) noexcept
    {
        const bool supportedFormat = surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM
            || surfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM || surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB
            || surfaceFormat.format == VK_FORMAT_R8G8B8A8_SRGB;
        return supportedFormat && surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }

    [[nodiscard]] constexpr bool IsSupportedOutputSurfaceFormat(const SurfaceFormatSelection& selection) noexcept
    {
        return selection.hdr10Active ? IsHdr10SurfaceFormat(selection.surfaceFormat)
                                     : IsSdrSurfaceFormat(selection.surfaceFormat);
    }

    [[nodiscard]] constexpr std::optional<VkCompositeAlphaFlagBitsKHR> SelectStraightAlphaCompositeMode(
        VkCompositeAlphaFlagsKHR supportedModes) noexcept
    {
        constexpr std::array compatibleModes = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (const auto mode : compatibleModes)
        {
            if ((supportedModes & mode) != 0)
            {
                return mode;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline SurfaceFormatSelection SelectSurfaceFormat(
        std::span<const VkSurfaceFormatKHR> formats, bool preferHdr10) noexcept
    {
        if (formats.empty())
        {
            return {};
        }

        const bool hdr10Available = std::any_of(formats.begin(), formats.end(), IsHdr10SurfaceFormat);
        const auto makeSelection = [=](VkSurfaceFormatKHR surfaceFormat) {
            return SurfaceFormatSelection{
                .surfaceFormat = surfaceFormat,
                .hdr10Available = hdr10Available,
                .hdr10Active = preferHdr10 && IsHdr10SurfaceFormat(surfaceFormat),
            };
        };

        if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED)
        {
            return makeSelection({ VK_FORMAT_B8G8R8A8_UNORM, formats.front().colorSpace });
        }
        if (preferHdr10)
        {
            const auto hdr = std::find_if(formats.begin(), formats.end(), IsHdr10SurfaceFormat);
            if (hdr != formats.end())
            {
                return makeSelection(*hdr);
            }
        }

        // A UNORM attachment preserves the palette's established encoded byte
        // values directly. If a platform exposes only an sRGB attachment, the
        // palette pass compensates by decoding to linear before the fixed-
        // function sRGB conversion.
        const auto preferred = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
            return format.format == VK_FORMAT_B8G8R8A8_UNORM
                && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (preferred != formats.end())
        {
            return makeSelection(*preferred);
        }
        const auto alternateUnorm = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
            return format.format == VK_FORMAT_R8G8B8A8_UNORM
                && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (alternateUnorm != formats.end())
        {
            return makeSelection(*alternateUnorm);
        }
        const auto srgb = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
            const bool srgbFormat = format.format == VK_FORMAT_B8G8R8A8_SRGB
                || format.format == VK_FORMAT_R8G8B8A8_SRGB;
            return srgbFormat && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        return makeSelection(srgb != formats.end() ? *srgb : formats.front());
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
