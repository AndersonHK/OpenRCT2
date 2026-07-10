/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanWeatherPipeline.h"

    #include "VulkanCommandLayouts.h"
    #include "VulkanShader.h"

    #include <array>
    #include <cstring>
    #include <stdexcept>
    #include <string>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        void CheckVk(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(
                    std::string(operation) + " failed with Vulkan result " + std::to_string(result));
            }
        }
    } // namespace

    WeatherPipeline::~WeatherPipeline()
    {
        Dispose();
    }

    void WeatherPipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory)
    {
        Dispose();
        _device = device.GetDevice();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        const auto extent = resources.GetIndexedCanvas(0).GetExtent();
        _extent = { extent.width, extent.height };
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(resources);
    }

    void WeatherPipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            for (auto& frame : _framebuffers)
            {
                for (auto& framebuffer : frame)
                {
                    if (framebuffer != VK_NULL_HANDLE)
                    {
                        vkDestroyFramebuffer(_device, framebuffer, nullptr);
                    }
                }
            }
            if (_pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(_device, _pipeline, nullptr);
            }
            if (_renderPass != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(_device, _renderPass, nullptr);
            }
            if (_pipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            }
        }
        _device = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _pipelineLayout = VK_NULL_HANDLE;
        _renderPass = VK_NULL_HANDLE;
        _pipeline = VK_NULL_HANDLE;
        _extent = {};
        _shaderDirectory.clear();
        _framebuffers = {};
    }

    void WeatherPipeline::Record(
        const FrameToken& frame, const Gpu::CommandBatch<Gpu::WeatherCommand>& commands, bool targetComposite) const
    {
        if (commands.empty())
        {
            return;
        }
        const auto byteSize = static_cast<VkDeviceSize>(commands.size() * sizeof(Gpu::WeatherCommand));
        const auto allocation = frame.upload->Allocate(byteSize, alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for weather commands");
        }
        std::memcpy(allocation.data, commands.data(), static_cast<size_t>(byteSize));

        const VkRenderPassBeginInfo pass = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = _renderPass,
            .framebuffer = _framebuffers.at(frame.frameIndex)[targetComposite ? 1 : 0],
            .renderArea = { { 0, 0 }, _extent },
        };
        const VkViewport viewport = {
            0.0f, 0.0f, static_cast<float>(_extent.width), static_cast<float>(_extent.height), 0.0f, 1.0f
        };
        const VkRect2D scissor = { { 0, 0 }, _extent };
        const ScreenConstants screen = { static_cast<int32_t>(_extent.width), static_cast<int32_t>(_extent.height) };
        const VkDeviceSize offset = allocation.offset;
        vkCmdBeginRenderPass(frame.commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        vkCmdPushConstants(
            frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen), &screen);
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &allocation.buffer, &offset);
        vkCmdDraw(frame.commandBuffer, 4, static_cast<uint32_t>(commands.size()), 0, 0);
        vkCmdEndRenderPass(frame.commandBuffer);
    }

    void WeatherPipeline::CreateRenderPass()
    {
        const VkAttachmentDescription attachment = {
            0, VK_FORMAT_R8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        const VkAttachmentReference colour = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        const VkSubpassDescription subpass = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1, &colour };
        const std::array dependencies = {
            VkSubpassDependency{ VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
            VkSubpassDependency{ 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT },
        };
        const VkRenderPassCreateInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 1, &attachment,
                                               1, &subpass, static_cast<uint32_t>(dependencies.size()),
                                               dependencies.data() };
        CheckVk(vkCreateRenderPass(_device, &info, nullptr, &_renderPass), "create weather render pass");
    }

    void WeatherPipeline::CreatePipeline()
    {
        const VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ScreenConstants) };
        const VkPipelineLayoutCreateInfo layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 0,
                                                     nullptr, 1, &push };
        CheckVk(vkCreatePipelineLayout(_device, &layout, nullptr, &_pipelineLayout), "create weather layout");
        const auto vertex = LoadShaderModule(_device, _shaderDirectory / "indexed_weather.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try
        {
            fragment = LoadShaderModule(_device, _shaderDirectory / "indexed_weather.frag.spv");
        }
        catch (...)
        {
            vkDestroyShaderModule(_device, vertex, nullptr);
            throw;
        }
        const std::array stages = {
            VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                             VK_SHADER_STAGE_VERTEX_BIT, vertex, "main" },
            VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                             VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main" },
        };
        const VkPipelineVertexInputStateCreateInfo vertexInput = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 1, &kWeatherCommandBinding,
            static_cast<uint32_t>(kWeatherCommandAttributes.size()), kWeatherCommandAttributes.data()
        };
        const VkPipelineInputAssemblyStateCreateInfo assembly = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
        };
        const VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                              nullptr, 0, 1, nullptr, 1, nullptr };
        const VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                                nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL,
                                                                VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, VK_FALSE,
                                                                0, 0, 0, 1.0f };
        const VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                                   nullptr, 0, VK_SAMPLE_COUNT_1_BIT };
        const VkPipelineColorBlendAttachmentState attachment = { VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                                  VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE,
                                                                  VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                                                                  VK_COLOR_COMPONENT_R_BIT };
        const VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                             nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &attachment };
        constexpr std::array dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        const VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                            nullptr, 0, static_cast<uint32_t>(dynamicStates.size()),
                                                            dynamicStates.data() };
        const VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0,
                                                     static_cast<uint32_t>(stages.size()), stages.data(), &vertexInput,
                                                     &assembly, nullptr, &viewport, &raster, &multisample, nullptr,
                                                     &blend, &dynamic, _pipelineLayout, _renderPass, 0 };
        const auto status = vkCreateGraphicsPipelines(_device, _pipelineCache, 1, &info, nullptr, &_pipeline);
        vkDestroyShaderModule(_device, vertex, nullptr);
        vkDestroyShaderModule(_device, fragment, nullptr);
        CheckVk(status, "create weather pipeline");
    }

    void WeatherPipeline::CreateFramebuffers(const IndexedResources& resources)
    {
        for (uint32_t frame = 0; frame < kFramesInFlight; frame++)
        {
            const std::array views = { resources.GetIndexedCanvas(frame).GetView(),
                                       resources.GetCompositeCanvas(frame).GetView() };
            for (uint32_t target = 0; target < views.size(); target++)
            {
                const VkFramebufferCreateInfo info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0,
                                                        _renderPass, 1, &views[target], _extent.width, _extent.height, 1 };
                CheckVk(vkCreateFramebuffer(_device, &info, nullptr, &_framebuffers[frame][target]), "create weather framebuffer");
            }
        }
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
