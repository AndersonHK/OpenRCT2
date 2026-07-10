/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanPalettePipeline.h"

    #include "VulkanShader.h"

    #include <array>
    #include <stdexcept>
    #include <string>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        [[noreturn]] void ThrowVk(const char* operation, VkResult result)
        {
            throw std::runtime_error(std::string(operation) + " failed with Vulkan result " + std::to_string(result));
        }

        void CheckVk(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                ThrowVk(operation, result);
            }
        }
    } // namespace

    PalettePipeline::~PalettePipeline()
    {
        Dispose();
    }

    void PalettePipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory)
    {
        Dispose();
        _device = device.GetDevice();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        CreateDescriptorResources(resources);
        RefreshSwapchain(device);
    }

    void PalettePipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            DestroySwapchainResources();
            if (_pipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            }
            if (_descriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            }
            if (_descriptorSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
            }
        }

        _device = VK_NULL_HANDLE;
        _descriptorSetLayout = VK_NULL_HANDLE;
        _descriptorPool = VK_NULL_HANDLE;
        _descriptorSets = {};
        _pipelineLayout = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _shaderDirectory.clear();
        _swapchainFormat = VK_FORMAT_UNDEFINED;
        _swapchainExtent = {};
        _swapchainGeneration = 0;
    }

    void PalettePipeline::RefreshSwapchain(const Device& device)
    {
        if (_device != device.GetDevice())
        {
            throw std::logic_error("Palette pipeline belongs to a different Vulkan device");
        }
        if (_swapchainGeneration == device.GetSwapchainGeneration())
        {
            return;
        }

        DestroySwapchainResources();
        _swapchainFormat = device.GetSwapchainFormat();
        _swapchainExtent = device.GetSwapchainExtent();
        _swapchainGeneration = device.GetSwapchainGeneration();
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(device.GetSwapchainImageViews());
    }

    void PalettePipeline::RefreshDescriptors(const IndexedResources& resources)
    {
        const VkDescriptorImageInfo paletteInfo = {
            .sampler = resources.GetNearestSampler(),
            .imageView = resources.GetPalette().GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            const VkDescriptorImageInfo canvasInfo = {
                .sampler = resources.GetNearestSampler(),
                .imageView = resources.GetIndexedCanvas(i).GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            const std::array writes = {
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = _descriptorSets[i],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &canvasInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = _descriptorSets[i],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &paletteInfo,
                },
            };
            vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void PalettePipeline::Record(const FrameToken& frame) const
    {
        if (frame.imageIndex >= _framebuffers.size() || frame.frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan palette frame index is out of range");
        }

        const VkClearValue clear = { .color = { { 0.0f, 0.0f, 0.0f, 1.0f } } };
        const VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = _renderPass,
            .framebuffer = _framebuffers[frame.imageIndex],
            .renderArea = { .offset = { 0, 0 }, .extent = frame.extent },
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        vkCmdBeginRenderPass(frame.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(frame.extent.width),
            .height = static_cast<float>(frame.extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const VkRect2D scissor = { .offset = { 0, 0 }, .extent = frame.extent };
        vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        vkCmdBindDescriptorSets(
            frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1,
            &_descriptorSets[frame.frameIndex], 0, nullptr);
        vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(frame.commandBuffer);
    }

    void PalettePipeline::CreateDescriptorResources(const IndexedResources& resources)
    {
        const std::array bindings = {
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        CheckVk(
            vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout),
            "vkCreateDescriptorSetLayout(palette)");

        const VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = kFramesInFlight * 2,
        };
        const VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = kFramesInFlight,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        CheckVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool), "vkCreateDescriptorPool(palette)");

        std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
        layouts.fill(_descriptorSetLayout);
        const VkDescriptorSetAllocateInfo allocationInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = _descriptorPool,
            .descriptorSetCount = kFramesInFlight,
            .pSetLayouts = layouts.data(),
        };
        CheckVk(vkAllocateDescriptorSets(_device, &allocationInfo, _descriptorSets.data()), "vkAllocateDescriptorSets(palette)");

        const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &_descriptorSetLayout,
        };
        CheckVk(
            vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout),
            "vkCreatePipelineLayout(palette)");
        RefreshDescriptors(resources);
    }

    void PalettePipeline::CreateRenderPass()
    {
        const VkAttachmentDescription colourAttachment = {
            .format = _swapchainFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
        const VkAttachmentReference colourReference = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        const VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colourReference,
        };
        const VkSubpassDependency dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };
        const VkRenderPassCreateInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colourAttachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &dependency,
        };
        CheckVk(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderPass), "vkCreateRenderPass(palette)");
    }

    void PalettePipeline::CreatePipeline()
    {
        VkShaderModule vertexShader = VK_NULL_HANDLE;
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        try
        {
            vertexShader = LoadShaderModule(_device, _shaderDirectory / "indexed_palette.vert.spv");
            fragmentShader = LoadShaderModule(_device, _shaderDirectory / "indexed_palette.frag.spv");
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
            const VkPipelineVertexInputStateCreateInfo vertexInput = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            };
            const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            };
            const VkPipelineViewportStateCreateInfo viewportState = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            };
            const VkPipelineRasterizationStateCreateInfo rasterisation = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_NONE,
                .frontFace = VK_FRONT_FACE_CLOCKWISE,
                .lineWidth = 1.0f,
            };
            const VkPipelineMultisampleStateCreateInfo multisampling = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            };
            const VkPipelineColorBlendAttachmentState blendAttachment = {
                .blendEnable = VK_FALSE,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT,
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
                .pColorBlendState = &blending,
                .pDynamicState = &dynamicState,
                .layout = _pipelineLayout,
                .renderPass = _renderPass,
                .subpass = 0,
            };
            CheckVk(
                vkCreateGraphicsPipelines(_device, _pipelineCache, 1, &pipelineInfo, nullptr, &_pipeline),
                "vkCreateGraphicsPipelines(palette)");
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

    void PalettePipeline::CreateFramebuffers(std::span<const VkImageView> imageViews)
    {
        _framebuffers.resize(imageViews.size());
        for (size_t i = 0; i < imageViews.size(); i++)
        {
            const VkFramebufferCreateInfo framebufferInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = _renderPass,
                .attachmentCount = 1,
                .pAttachments = &imageViews[i],
                .width = _swapchainExtent.width,
                .height = _swapchainExtent.height,
                .layers = 1,
            };
            CheckVk(vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_framebuffers[i]), "vkCreateFramebuffer(palette)");
        }
    }

    void PalettePipeline::DestroySwapchainResources()
    {
        for (const auto framebuffer : _framebuffers)
        {
            vkDestroyFramebuffer(_device, framebuffer, nullptr);
        }
        _framebuffers.clear();
        if (_pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(_device, _pipeline, nullptr);
            _pipeline = VK_NULL_HANDLE;
        }
        if (_renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(_device, _renderPass, nullptr);
            _renderPass = VK_NULL_HANDLE;
        }
    }

} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
