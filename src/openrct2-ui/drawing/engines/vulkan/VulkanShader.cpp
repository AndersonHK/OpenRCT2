/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanShader.h"

    #include <array>
    #include <fstream>
    #include <stdexcept>
    #include <string>
    #include <utility>
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        class ScopedShaderModule
        {
        public:
            ScopedShaderModule(VkDevice device, const std::filesystem::path& path)
                : _device(device)
                , _module(LoadShaderModule(device, path))
            {
            }

            ~ScopedShaderModule()
            {
                vkDestroyShaderModule(_device, _module, nullptr);
            }

            ScopedShaderModule(const ScopedShaderModule&) = delete;
            ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;

            [[nodiscard]] VkShaderModule Get() const noexcept
            {
                return _module;
            }

        private:
            VkDevice _device;
            VkShaderModule _module;
        };
    } // namespace

    [[noreturn]] void ThrowVk(std::string_view operation, VkResult result)
    {
        throw std::runtime_error(std::string(operation) + " failed with Vulkan result " + std::to_string(result));
    }

    void CheckVk(VkResult result, std::string_view operation)
    {
        if (result != VK_SUCCESS)
        {
            ThrowVk(operation, result);
        }
    }

    VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            throw std::runtime_error("Unable to open Vulkan shader: " + path.string());
        }

        const auto length = stream.tellg();
        if (length <= 0 || (length % static_cast<std::streamoff>(sizeof(uint32_t))) != 0)
        {
            throw std::runtime_error("Invalid Vulkan shader length: " + path.string());
        }
        std::vector<uint32_t> code(static_cast<size_t>(length) / sizeof(uint32_t));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(length));
        if (!stream)
        {
            throw std::runtime_error("Unable to read Vulkan shader: " + path.string());
        }

        const VkShaderModuleCreateInfo moduleInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = static_cast<size_t>(length),
            .pCode = code.data(),
        };
        VkShaderModule result = VK_NULL_HANDLE;
        CheckVk(vkCreateShaderModule(device, &moduleInfo, nullptr, &result), "vkCreateShaderModule");
        return result;
    }

    VkPipeline CreateGraphicsPipeline(
        VkDevice device, VkPipelineCache cache, const GraphicsPipelineConfig& config, std::string_view operation)
    {
        const ScopedShaderModule vertexShader(device, config.vertexShader);
        const ScopedShaderModule fragmentShader(device, config.fragmentShader);
        const std::array stages = {
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertexShader.Get(),
                .pName = "main",
            },
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragmentShader.Get(),
                .pName = "main",
            },
        };
        const VkPipelineVertexInputStateCreateInfo vertexInput = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexBindings.size()),
            .pVertexBindingDescriptions = config.vertexBindings.data(),
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributes.size()),
            .pVertexAttributeDescriptions = config.vertexAttributes.data(),
        };
        const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = config.topology,
        };
        constexpr VkPipelineViewportStateCreateInfo viewport = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };
        constexpr VkPipelineRasterizationStateCreateInfo rasterisation = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .lineWidth = 1.0f,
        };
        constexpr VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        const VkPipelineColorBlendAttachmentState blendAttachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = config.colorWriteMask,
        };
        const VkPipelineColorBlendStateCreateInfo blending = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &blendAttachment,
        };
        constexpr std::array dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        const VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data(),
        };
        const VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewport,
            .pRasterizationState = &rasterisation,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = config.depthStencil,
            .pColorBlendState = &blending,
            .pDynamicState = &dynamicState,
            .layout = config.layout,
            .renderPass = config.renderPass,
            .subpass = 0,
        };
        VkPipeline pipeline = VK_NULL_HANDLE;
        CheckVk(vkCreateGraphicsPipelines(device, cache, 1, &pipelineInfo, nullptr, &pipeline), operation);
        return pipeline;
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
