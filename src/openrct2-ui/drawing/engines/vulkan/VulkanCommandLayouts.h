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

    #include "../gpu/GpuCommandStream.h"

    #include <array>
    #include <cstddef>
    #include <vulkan/vulkan.h>

namespace OpenRCT2::Ui::Vulkan
{
    struct ScreenConstants
    {
        int32_t width;
        int32_t height;
    };

    struct TransparencyConstants
    {
        int32_t width;
        int32_t height;
        int32_t peeling;
    };

    inline constexpr VkVertexInputBindingDescription kRectCommandBinding = {
        .binding = 0,
        .stride = sizeof(Gpu::RectCommand),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    inline constexpr std::array kRectCommandAttributes = {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(Gpu::RectCommand, clip) },
        VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::RectCommand, texColourAtlas) },
        VkVertexInputAttributeDescription{
            2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Gpu::RectCommand, texColourBounds) },
        VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::RectCommand, texMaskAtlas) },
        VkVertexInputAttributeDescription{
            4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Gpu::RectCommand, texMaskBounds) },
        VkVertexInputAttributeDescription{ 5, 0, VK_FORMAT_R32G32B32_SINT, offsetof(Gpu::RectCommand, palettes) },
        VkVertexInputAttributeDescription{ 6, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::RectCommand, flags) },
        VkVertexInputAttributeDescription{ 7, 0, VK_FORMAT_R32_UINT, offsetof(Gpu::RectCommand, colour) },
        VkVertexInputAttributeDescription{ 8, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(Gpu::RectCommand, bounds) },
        VkVertexInputAttributeDescription{ 9, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::RectCommand, depth) },
        VkVertexInputAttributeDescription{ 10, 0, VK_FORMAT_R32_SFLOAT, offsetof(Gpu::RectCommand, zoom) },
    };

    inline constexpr VkVertexInputBindingDescription kWeatherCommandBinding = {
        .binding = 0,
        .stride = sizeof(Gpu::WeatherCommand),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    inline constexpr std::array kWeatherCommandAttributes = {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(Gpu::WeatherCommand, bounds) },
        VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32_SINT, offsetof(Gpu::WeatherCommand, offset) },
        VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WeatherCommand, pattern) },
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
