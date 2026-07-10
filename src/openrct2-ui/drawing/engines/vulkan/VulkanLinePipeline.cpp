/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanLinePipeline.h"

    #include "VulkanShader.h"

    #include <array>
    #include <cstddef>
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

        struct ScreenConstants
        {
            int32_t width;
            int32_t height;
        };
    } // namespace

    LinePipeline::~LinePipeline()
    {
        Dispose();
    }

    void LinePipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory)
    {
        Dispose();
        _device = device.GetDevice();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        Refresh(resources);
    }

    void LinePipeline::Dispose()
    {
        DestroyCanvasResources();
        _device = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _shaderDirectory.clear();
        _extent = {};
    }

    void LinePipeline::Refresh(const IndexedResources& resources)
    {
        DestroyCanvasResources();
        const auto canvasExtent = resources.GetIndexedCanvas(0).GetExtent();
        _extent = { canvasExtent.width, canvasExtent.height };
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(resources);
    }

    void LinePipeline::Record(
        const FrameToken& frame, const Gpu::CommandBatch<Gpu::LineCommand>& commands) const
    {
        if (commands.empty())
        {
            return;
        }
        if (frame.frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan line frame index is out of range");
        }

        const auto byteSize = static_cast<VkDeviceSize>(commands.size() * sizeof(Gpu::LineCommand));
        const auto allocation = frame.upload->Allocate(byteSize, alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for line commands");
        }
        std::memcpy(allocation.data, commands.data(), static_cast<size_t>(byteSize));

        std::array<VkClearValue, 2> clears{};
        clears[0].color.uint32[0] = 0;
        clears[1].depthStencil = { 1.0f, 0 };
        const VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = _renderPass,
            .framebuffer = _framebuffers[frame.frameIndex],
            .renderArea = { .offset = { 0, 0 }, .extent = _extent },
            .clearValueCount = static_cast<uint32_t>(clears.size()),
            .pClearValues = clears.data(),
        };
        vkCmdBeginRenderPass(frame.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(_extent.width),
            .height = static_cast<float>(_extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const VkRect2D scissor = { .offset = { 0, 0 }, .extent = _extent };
        const ScreenConstants screen = {
            static_cast<int32_t>(_extent.width), static_cast<int32_t>(_extent.height)
        };
        const VkDeviceSize vertexOffset = allocation.offset;

        vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        vkCmdPushConstants(
            frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen), &screen);
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &allocation.buffer, &vertexOffset);
        vkCmdDraw(frame.commandBuffer, 2, static_cast<uint32_t>(commands.size()), 0, 0);
        vkCmdEndRenderPass(frame.commandBuffer);
    }

    void LinePipeline::CreateRenderPass()
    {
        const std::array attachments = {
            VkAttachmentDescription{
                .format = VK_FORMAT_R8_UINT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            VkAttachmentDescription{
                .format = VK_FORMAT_D32_SFLOAT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
        };
        constexpr VkAttachmentReference colourReference = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        constexpr VkAttachmentReference depthReference = {
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        const VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colourReference,
            .pDepthStencilAttachment = &depthReference,
        };
        constexpr std::array dependencies = {
            VkSubpassDependency{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            },
            VkSubpassDependency{
                .srcSubpass = 0,
                .dstSubpass = VK_SUBPASS_EXTERNAL,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            },
        };
        const VkRenderPassCreateInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = static_cast<uint32_t>(dependencies.size()),
            .pDependencies = dependencies.data(),
        };
        CheckVk(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderPass), "vkCreateRenderPass(lines)");
    }

    void LinePipeline::CreatePipeline()
    {
        const VkPushConstantRange pushConstant = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(ScreenConstants),
        };
        const VkPipelineLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstant,
        };
        CheckVk(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_pipelineLayout), "vkCreatePipelineLayout(lines)");

        VkShaderModule vertexShader = VK_NULL_HANDLE;
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        try
        {
            vertexShader = LoadShaderModule(_device, _shaderDirectory / "indexed_line.vert.spv");
            fragmentShader = LoadShaderModule(_device, _shaderDirectory / "indexed_line.frag.spv");
            const std::array stages = {
                VkPipelineShaderStageCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vertexShader,
                    .pName = "main",
                },
                VkPipelineShaderStageCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fragmentShader,
                    .pName = "main",
                },
            };
            constexpr VkVertexInputBindingDescription binding = {
                .binding = 0,
                .stride = sizeof(Gpu::LineCommand),
                .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
            };
            constexpr std::array attributes = {
                VkVertexInputAttributeDescription{
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32A32_SINT,
                    .offset = offsetof(Gpu::LineCommand, bounds),
                },
                VkVertexInputAttributeDescription{
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32_UINT,
                    .offset = offsetof(Gpu::LineCommand, colour),
                },
                VkVertexInputAttributeDescription{
                    .location = 2,
                    .binding = 0,
                    .format = VK_FORMAT_R32_SINT,
                    .offset = offsetof(Gpu::LineCommand, depth),
                },
            };
            const VkPipelineVertexInputStateCreateInfo vertexInput = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &binding,
                .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
                .pVertexAttributeDescriptions = attributes.data(),
            };
            constexpr VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            };
            constexpr VkPipelineViewportStateCreateInfo viewportState = {
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
            constexpr VkPipelineDepthStencilStateCreateInfo depth = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = VK_COMPARE_OP_LESS,
                .minDepthBounds = 0.0f,
                .maxDepthBounds = 1.0f,
            };
            constexpr VkPipelineColorBlendAttachmentState blendAttachment = {
                .blendEnable = VK_FALSE,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT,
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
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterisation,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depth,
                .pColorBlendState = &blending,
                .pDynamicState = &dynamicState,
                .layout = _pipelineLayout,
                .renderPass = _renderPass,
                .subpass = 0,
            };
            CheckVk(
                vkCreateGraphicsPipelines(_device, _pipelineCache, 1, &pipelineInfo, nullptr, &_pipeline),
                "vkCreateGraphicsPipelines(lines)");
        }
        catch (...)
        {
            if (vertexShader != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(_device, vertexShader, nullptr);
            }
            if (fragmentShader != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(_device, fragmentShader, nullptr);
            }
            throw;
        }
        vkDestroyShaderModule(_device, vertexShader, nullptr);
        vkDestroyShaderModule(_device, fragmentShader, nullptr);
    }

    void LinePipeline::CreateFramebuffers(const IndexedResources& resources)
    {
        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            const std::array attachments = {
                resources.GetIndexedCanvas(i).GetView(), resources.GetDepthCanvas(i).GetView()
            };
            const VkFramebufferCreateInfo framebufferInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = _renderPass,
                .attachmentCount = static_cast<uint32_t>(attachments.size()),
                .pAttachments = attachments.data(),
                .width = _extent.width,
                .height = _extent.height,
                .layers = 1,
            };
            CheckVk(vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_framebuffers[i]), "vkCreateFramebuffer(lines)");
        }
    }

    void LinePipeline::DestroyCanvasResources()
    {
        if (_device == VK_NULL_HANDLE)
        {
            return;
        }
        for (auto& framebuffer : _framebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(_device, framebuffer, nullptr);
                framebuffer = VK_NULL_HANDLE;
            }
        }
        if (_pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(_device, _pipeline, nullptr);
            _pipeline = VK_NULL_HANDLE;
        }
        if (_pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            _pipelineLayout = VK_NULL_HANDLE;
        }
        if (_renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(_device, _renderPass, nullptr);
            _renderPass = VK_NULL_HANDLE;
        }
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
