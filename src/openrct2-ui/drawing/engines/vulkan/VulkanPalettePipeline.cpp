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

    #include <algorithm>
    #include <array>
    #include <cmath>
    #include <stdexcept>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        enum class OutputEncoding : int32_t
        {
            LegacyBytes,
            SrgbAttachment,
            Hdr10Pq,
        };

        struct OutputConstants
        {
            int32_t encoding;
            float paperWhiteNits;
            int32_t lightFxEnabled;
        };
        static_assert(sizeof(OutputConstants) == 12);
    } // namespace

    PalettePipeline::~PalettePipeline()
    {
        Dispose();
    }

    void PalettePipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory, float paperWhiteNits)
    {
        Dispose();
        _device = device.GetDevice();
        _nearestSampler = resources.GetNearestSampler();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        _paperWhiteNits = std::isfinite(paperWhiteNits) ? std::clamp(paperWhiteNits, 80.0f, 1000.0f) : 203.0f;
        CreateDescriptorResources(resources);
        RefreshSwapchain(device);
    }

    void PalettePipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            DestroySwapchainResources();
            vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
        }

        _device = VK_NULL_HANDLE;
        _nearestSampler = VK_NULL_HANDLE;
        _descriptorSetLayout = VK_NULL_HANDLE;
        _descriptorPool = VK_NULL_HANDLE;
        _descriptorSets = {};
        _pipelineLayout = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _shaderDirectory.clear();
        _swapchainFormat = VK_FORMAT_UNDEFINED;
        _swapchainExtent = {};
        _swapchainGeneration = 0;
        _paperWhiteNits = 203.0f;
        _outputEncoding = static_cast<int32_t>(OutputEncoding::LegacyBytes);
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
        if (device.IsHdr10Active())
        {
            _outputEncoding = static_cast<int32_t>(OutputEncoding::Hdr10Pq);
        }
        else if (_swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB || _swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB)
        {
            _outputEncoding = static_cast<int32_t>(OutputEncoding::SrgbAttachment);
        }
        else
        {
            _outputEncoding = static_cast<int32_t>(OutputEncoding::LegacyBytes);
        }
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(device.GetSwapchainImageViews());
    }

    void PalettePipeline::ReleaseSwapchainResources()
    {
        DestroySwapchainResources();
    }

    void PalettePipeline::RefreshDescriptors(const IndexedResources& resources)
    {
        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            const VkDescriptorImageInfo paletteInfo = {
                .sampler = resources.GetNearestSampler(),
                .imageView = resources.GetPalette(i).GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            const VkDescriptorImageInfo canvasInfo = {
                .sampler = resources.GetNearestSampler(),
                .imageView = resources.GetIndexedCanvas(i).GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            const VkDescriptorImageInfo lightMapInfo = {
                .sampler = resources.GetNearestSampler(),
                .imageView = resources.GetLightMap(i).GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            const VkDescriptorImageInfo lightPaletteInfo = {
                .sampler = resources.GetNearestSampler(),
                .imageView = resources.GetLightPalette(i).GetView(),
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
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = _descriptorSets[i],
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &lightMapInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = _descriptorSets[i],
                    .dstBinding = 3,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &lightPaletteInfo,
                },
            };
            vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void PalettePipeline::SetCanvasSource(uint32_t frameIndex, const Image& canvas)
    {
        if (frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan palette canvas frame index is out of range");
        }
        const VkDescriptorImageInfo canvasInfo = {
            .sampler = _nearestSampler,
            .imageView = canvas.GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = _descriptorSets[frameIndex],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &canvasInfo,
        };
        vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
    }

    void PalettePipeline::SetLightMapSource(uint32_t frameIndex, const Image& lightMap)
    {
        if (frameIndex >= kFramesInFlight)
            throw std::out_of_range("Vulkan LightFX frame index is out of range");
        const VkDescriptorImageInfo info = { _nearestSampler, lightMap.GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = _descriptorSets[frameIndex],
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &info,
        };
        vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
    }

    void PalettePipeline::Record(const FrameToken& frame, bool lightFxEnabled) const
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
            frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSets[frame.frameIndex], 0,
            nullptr);
        const OutputConstants output = { _outputEncoding, _paperWhiteNits, lightFxEnabled ? 1 : 0 };
        vkCmdPushConstants(frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(output), &output);
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
            VkDescriptorSetLayoutBinding{
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 3,
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
            .descriptorCount = kFramesInFlight * 4,
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
        CheckVk(
            vkAllocateDescriptorSets(_device, &allocationInfo, _descriptorSets.data()), "vkAllocateDescriptorSets(palette)");

        const VkPushConstantRange outputConstants = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(OutputConstants),
        };
        const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &_descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &outputConstants,
        };
        CheckVk(
            vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout), "vkCreatePipelineLayout(palette)");
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
        _pipeline = CreateGraphicsPipeline(
            _device, _pipelineCache,
            {
                .vertexShader = _shaderDirectory / "indexed_palette.vert.spv",
                .fragmentShader = _shaderDirectory / "indexed_palette.frag.spv",
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT,
                .layout = _pipelineLayout,
                .renderPass = _renderPass,
            },
            "vkCreateGraphicsPipelines(palette)");
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
        vkDestroyPipeline(_device, _pipeline, nullptr);
        vkDestroyRenderPass(_device, _renderPass, nullptr);
        _pipeline = VK_NULL_HANDLE;
        _renderPass = VK_NULL_HANDLE;
    }

} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
