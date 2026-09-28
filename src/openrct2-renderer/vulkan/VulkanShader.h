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

    #include <filesystem>
    #include <span>
    #include <string_view>
    #include <vulkan/vulkan.h>

namespace OpenRCT2::Ui::Vulkan
{
    [[noreturn]] void ThrowVk(std::string_view operation, VkResult result);
    void CheckVk(VkResult result, std::string_view operation);

    [[nodiscard]] VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path);

    struct GraphicsPipelineConfig
    {
        std::filesystem::path vertexShader;
        std::filesystem::path fragmentShader;
        std::span<const VkVertexInputBindingDescription> vertexBindings;
        std::span<const VkVertexInputAttributeDescription> vertexAttributes;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        const VkPipelineDepthStencilStateCreateInfo* depthStencil = nullptr;
        VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
    };

    [[nodiscard]] VkPipeline CreateGraphicsPipeline(
        VkDevice device, VkPipelineCache cache, const GraphicsPipelineConfig& config, std::string_view operation);
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
