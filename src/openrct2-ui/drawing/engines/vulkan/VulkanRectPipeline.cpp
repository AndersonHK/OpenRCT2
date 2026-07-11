/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanRectPipeline.h"

    #include "VulkanCommandLayouts.h"
    #include "VulkanShader.h"

    #include <array>
    #include <cstddef>
    #include <cstring>
    #include <stdexcept>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    RectPipeline::~RectPipeline()
    {
        Dispose();
    }

    void RectPipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory)
    {
        Dispose();
        _device = device.GetDevice();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        const auto extent = resources.GetIndexedCanvas(0).GetExtent();
        _extent = { extent.width, extent.height };
        CreateDescriptors(resources);
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(resources);
    }

    void RectPipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            for (const auto framebuffer : _framebuffers)
            {
                vkDestroyFramebuffer(_device, framebuffer, nullptr);
            }
            vkDestroyPipeline(_device, _pipeline, nullptr);
            vkDestroyRenderPass(_device, _renderPass, nullptr);
            vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
        }
        _device = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _descriptorSetLayout = VK_NULL_HANDLE;
        _descriptorPool = VK_NULL_HANDLE;
        _descriptorSet = VK_NULL_HANDLE;
        _pipelineLayout = VK_NULL_HANDLE;
        _renderPass = VK_NULL_HANDLE;
        _pipeline = VK_NULL_HANDLE;
        _framebuffers = {};
        _extent = {};
        _shaderDirectory.clear();
    }

    void RectPipeline::Record(const FrameToken& frame, const Gpu::CommandBatch<Gpu::RectCommand>& commands) const
    {
        if (commands.empty())
        {
            return;
        }
        if (frame.frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan rectangle frame index is out of range");
        }

        const auto byteSize = static_cast<VkDeviceSize>(commands.size() * sizeof(Gpu::RectCommand));
        const auto allocation = frame.upload->Allocate(byteSize, alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for rectangle commands");
        }
        std::memcpy(allocation.data, commands.data(), static_cast<size_t>(byteSize));

        const VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = _renderPass,
            .framebuffer = _framebuffers[frame.frameIndex],
            .renderArea = { .offset = { 0, 0 }, .extent = _extent },
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
        const ScreenConstants screen = { static_cast<int32_t>(_extent.width), static_cast<int32_t>(_extent.height) };
        const VkDeviceSize vertexOffset = allocation.offset;
        vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        vkCmdBindDescriptorSets(
            frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
        vkCmdPushConstants(frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen), &screen);
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &allocation.buffer, &vertexOffset);
        vkCmdDraw(frame.commandBuffer, 4, static_cast<uint32_t>(commands.size()), 0, 0);
        vkCmdEndRenderPass(frame.commandBuffer);
    }

    void RectPipeline::CreateDescriptors(const IndexedResources& resources)
    {
        constexpr std::array bindings = {
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
            "vkCreateDescriptorSetLayout(rects)");
        constexpr VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 2,
        };
        const VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        CheckVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool), "vkCreateDescriptorPool(rects)");
        const VkDescriptorSetAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = _descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &_descriptorSetLayout,
        };
        CheckVk(vkAllocateDescriptorSets(_device, &allocateInfo, &_descriptorSet), "vkAllocateDescriptorSets(rects)");
        const VkDescriptorImageInfo atlasInfo = {
            .sampler = resources.GetNearestSampler(),
            .imageView = resources.GetSpriteAtlas().GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkDescriptorImageInfo remapInfo = {
            .sampler = resources.GetNearestSampler(),
            .imageView = resources.GetRemapPalette().GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const std::array writes = {
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = _descriptorSet,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &atlasInfo,
            },
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = _descriptorSet,
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &remapInfo,
            },
        };
        vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void RectPipeline::CreateRenderPass()
    {
        const std::array attachments = {
            VkAttachmentDescription{
                .format = VK_FORMAT_R8_UINT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            VkAttachmentDescription{
                .format = VK_FORMAT_D32_SFLOAT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
        };
        constexpr VkAttachmentReference colour = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        constexpr VkAttachmentReference depth = {
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        const VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colour,
            .pDepthStencilAttachment = &depth,
        };
        constexpr std::array dependencies = {
            VkSubpassDependency{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                    | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
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
        const VkRenderPassCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = static_cast<uint32_t>(dependencies.size()),
            .pDependencies = dependencies.data(),
        };
        CheckVk(vkCreateRenderPass(_device, &info, nullptr, &_renderPass), "vkCreateRenderPass(rects)");
    }

    void RectPipeline::CreatePipeline()
    {
        const VkPushConstantRange push = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(ScreenConstants),
        };
        const VkPipelineLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &_descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push,
        };
        CheckVk(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_pipelineLayout), "vkCreatePipelineLayout(rects)");
        constexpr VkPipelineDepthStencilStateCreateInfo depthState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        _pipeline = CreateGraphicsPipeline(
            _device, _pipelineCache,
            {
                .vertexShader = _shaderDirectory / "indexed_rect.vert.spv",
                .fragmentShader = _shaderDirectory / "indexed_rect.frag.spv",
                .vertexBindings = std::span{ &kRectCommandBinding, 1 },
                .vertexAttributes = kRectCommandAttributes,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                .depthStencil = &depthState,
                .layout = _pipelineLayout,
                .renderPass = _renderPass,
            },
            "vkCreateGraphicsPipelines(rects)");
    }

    void RectPipeline::CreateFramebuffers(const IndexedResources& resources)
    {
        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            const std::array attachments = { resources.GetIndexedCanvas(i).GetView(), resources.GetDepthCanvas(i).GetView() };
            const VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = _renderPass,
                .attachmentCount = static_cast<uint32_t>(attachments.size()),
                .pAttachments = attachments.data(),
                .width = _extent.width,
                .height = _extent.height,
                .layers = 1,
            };
            CheckVk(vkCreateFramebuffer(_device, &info, nullptr, &_framebuffers[i]), "vkCreateFramebuffer(rects)");
        }
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
