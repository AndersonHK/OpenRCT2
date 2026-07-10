/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanTransparencyPipeline.h"

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

        VkDescriptorSet AllocateSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout)
        {
            VkDescriptorSet result = VK_NULL_HANDLE;
            const VkDescriptorSetAllocateInfo info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &layout,
            };
            CheckVk(vkAllocateDescriptorSets(device, &info, &result), "vkAllocateDescriptorSets(transparency)");
            return result;
        }

        VkDescriptorImageInfo ImageInfo(const IndexedResources& resources, const Image& image, VkImageLayout layout)
        {
            return { resources.GetNearestSampler(), image.GetView(), layout };
        }
    } // namespace

    TransparencyPipeline::~TransparencyPipeline()
    {
        Dispose();
    }

    void TransparencyPipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory)
    {
        Dispose();
        _device = device.GetDevice();
        _resources = &resources;
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        const auto extent = resources.GetIndexedCanvas(0).GetExtent();
        _extent = { extent.width, extent.height };
        CreateDescriptors(resources);
        CreateRenderPasses();
        CreatePipelines();
        CreateFramebuffers(resources);
    }

    void TransparencyPipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            for (auto& frame : _peelFramebuffers)
            {
                for (auto& framebuffer : frame)
                {
                    if (framebuffer != VK_NULL_HANDLE)
                    {
                        vkDestroyFramebuffer(_device, framebuffer, nullptr);
                    }
                }
            }
            for (auto& frame : _composeFramebuffers)
            {
                for (auto& framebuffer : frame)
                {
                    if (framebuffer != VK_NULL_HANDLE)
                    {
                        vkDestroyFramebuffer(_device, framebuffer, nullptr);
                    }
                }
            }
            if (_peelPipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(_device, _peelPipeline, nullptr);
            }
            if (_composePipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(_device, _composePipeline, nullptr);
            }
            if (_peelRenderPass != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(_device, _peelRenderPass, nullptr);
            }
            if (_composeRenderPass != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(_device, _composeRenderPass, nullptr);
            }
            if (_peelPipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(_device, _peelPipelineLayout, nullptr);
            }
            if (_composePipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(_device, _composePipelineLayout, nullptr);
            }
            if (_descriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            }
            if (_peelSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(_device, _peelSetLayout, nullptr);
            }
            if (_composeSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(_device, _composeSetLayout, nullptr);
            }
        }
        _device = VK_NULL_HANDLE;
        _resources = nullptr;
        _pipelineCache = VK_NULL_HANDLE;
        _extent = {};
        _shaderDirectory.clear();
        _descriptorPool = VK_NULL_HANDLE;
        _peelSetLayout = VK_NULL_HANDLE;
        _composeSetLayout = VK_NULL_HANDLE;
        _peelPipelineLayout = VK_NULL_HANDLE;
        _composePipelineLayout = VK_NULL_HANDLE;
        _peelRenderPass = VK_NULL_HANDLE;
        _composeRenderPass = VK_NULL_HANDLE;
        _peelPipeline = VK_NULL_HANDLE;
        _composePipeline = VK_NULL_HANDLE;
        _peelSets = {};
        _composeSets = {};
        _peelFramebuffers = {};
        _composeFramebuffers = {};
    }

    bool TransparencyPipeline::Record(
        const FrameToken& frame, const Gpu::CommandBatch<Gpu::RectCommand>& commands, uint32_t layerCount) const
    {
        if (commands.empty())
        {
            return false;
        }
        if (frame.frameIndex >= kFramesInFlight || layerCount == 0)
        {
            throw std::out_of_range("Invalid Vulkan transparency frame or layer count");
        }

        const auto byteSize = static_cast<VkDeviceSize>(commands.size() * sizeof(Gpu::RectCommand));
        const auto allocation = frame.upload->Allocate(byteSize, alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for transparent rectangle commands");
        }
        std::memcpy(allocation.data, commands.data(), static_cast<size_t>(byteSize));

        const VkImageSubresourceRange depthRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        // The first peel does not sample its previous-depth image, but putting
        // it in the declared descriptor layout keeps validation deterministic.
        RecordImageBarrier(
            frame.commandBuffer, _resources->GetTransparentDepthCanvas(frame.frameIndex, 1).GetImage(),
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, depthRange,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            VK_ACCESS_SHADER_READ_BIT);
        RecordImageBarrier(
            frame.commandBuffer, _resources->GetDepthCanvas(frame.frameIndex).GetImage(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            depthRange, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);

        bool inputComposite = false;
        uint32_t previousDepth = 1;
        uint32_t currentDepth = 0;
        const VkDeviceSize vertexOffset = allocation.offset;
        const VkViewport viewport = {
            0.0f, 0.0f, static_cast<float>(_extent.width), static_cast<float>(_extent.height), 0.0f, 1.0f
        };
        const VkRect2D scissor = { { 0, 0 }, _extent };

        for (uint32_t layer = 0; layer < layerCount; layer++)
        {
            std::array<VkClearValue, 2> clearValues{};
            clearValues[0].color.uint32[0] = 0;
            clearValues[1].depthStencil = { 0.0f, 0 };
            const VkRenderPassBeginInfo peelInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = _peelRenderPass,
                .framebuffer = _peelFramebuffers[frame.frameIndex][currentDepth],
                .renderArea = { { 0, 0 }, _extent },
                .clearValueCount = static_cast<uint32_t>(clearValues.size()),
                .pClearValues = clearValues.data(),
            };
            vkCmdBeginRenderPass(frame.commandBuffer, &peelInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _peelPipeline);
            vkCmdBindDescriptorSets(
                frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _peelPipelineLayout, 0, 1,
                &_peelSets[frame.frameIndex][previousDepth], 0, nullptr);
            const TransparencyConstants constants = {
                static_cast<int32_t>(_extent.width), static_cast<int32_t>(_extent.height), layer == 0 ? 0 : 1
            };
            vkCmdPushConstants(
                frame.commandBuffer, _peelPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &allocation.buffer, &vertexOffset);
            vkCmdDraw(frame.commandBuffer, 4, static_cast<uint32_t>(commands.size()), 0, 0);
            vkCmdEndRenderPass(frame.commandBuffer);

            const bool outputComposite = !inputComposite;
            const VkRenderPassBeginInfo composeInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = _composeRenderPass,
                .framebuffer = _composeFramebuffers[frame.frameIndex][outputComposite ? 1 : 0],
                .renderArea = { { 0, 0 }, _extent },
            };
            vkCmdBeginRenderPass(frame.commandBuffer, &composeInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _composePipeline);
            vkCmdBindDescriptorSets(
                frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _composePipelineLayout, 0, 1,
                &_composeSets[frame.frameIndex][inputComposite ? 1 : 0][currentDepth], 0, nullptr);
            vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(frame.commandBuffer);

            inputComposite = outputComposite;
            previousDepth = currentDepth;
            currentDepth = 1 - currentDepth;
        }
        return inputComposite;
    }

    void TransparencyPipeline::CreateDescriptors(const IndexedResources& resources)
    {
        std::array<VkDescriptorSetLayoutBinding, 3> peelBindings{};
        for (uint32_t i = 0; i < peelBindings.size(); i++)
        {
            peelBindings[i] = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        }
        const VkDescriptorSetLayoutCreateInfo peelLayoutInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            static_cast<uint32_t>(peelBindings.size()), peelBindings.data()
        };
        CheckVk(vkCreateDescriptorSetLayout(_device, &peelLayoutInfo, nullptr, &_peelSetLayout), "create peel layout");

        std::array<VkDescriptorSetLayoutBinding, 6> composeBindings{};
        for (uint32_t i = 0; i < composeBindings.size(); i++)
        {
            composeBindings[i] = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        }
        const VkDescriptorSetLayoutCreateInfo composeLayoutInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            static_cast<uint32_t>(composeBindings.size()), composeBindings.data()
        };
        CheckVk(
            vkCreateDescriptorSetLayout(_device, &composeLayoutInfo, nullptr, &_composeSetLayout),
            "create transparency compose layout");

        const VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 90 };
        const VkDescriptorPoolCreateInfo poolInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 18, 1, &poolSize
        };
        CheckVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool), "create transparency pool");

        for (uint32_t frame = 0; frame < kFramesInFlight; frame++)
        {
            for (uint32_t depth = 0; depth < 2; depth++)
            {
                _peelSets[frame][depth] = AllocateSet(_device, _descriptorPool, _peelSetLayout);
                const std::array infos = {
                    ImageInfo(resources, resources.GetSpriteAtlas(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                    ImageInfo(resources, resources.GetRemapPalette(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                    ImageInfo(
                        resources, resources.GetTransparentDepthCanvas(frame, depth),
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
                };
                std::array<VkWriteDescriptorSet, 3> writes{};
                for (uint32_t i = 0; i < writes.size(); i++)
                {
                    writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _peelSets[frame][depth], i, 0, 1,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &infos[i], nullptr, nullptr };
                }
                vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }

            for (uint32_t input = 0; input < 2; input++)
            {
                const auto& inputImage = input == 0 ? resources.GetIndexedCanvas(frame) : resources.GetCompositeCanvas(frame);
                for (uint32_t depth = 0; depth < 2; depth++)
                {
                    auto& set = _composeSets[frame][input][depth];
                    set = AllocateSet(_device, _descriptorPool, _composeSetLayout);
                    const std::array infos = {
                        ImageInfo(resources, inputImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                        ImageInfo(resources, resources.GetDepthCanvas(frame), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
                        ImageInfo(resources, resources.GetTransparentCanvas(frame), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                        ImageInfo(
                            resources, resources.GetTransparentDepthCanvas(frame, depth),
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
                        ImageInfo(resources, resources.GetRemapPalette(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                        ImageInfo(resources, resources.GetBlendPalette(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                    };
                    std::array<VkWriteDescriptorSet, 6> writes{};
                    for (uint32_t i = 0; i < writes.size(); i++)
                    {
                        writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, i, 0, 1,
                                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &infos[i], nullptr, nullptr };
                    }
                    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                }
            }
        }
    }

    void TransparencyPipeline::CreateRenderPasses()
    {
        const std::array peelAttachments = {
            VkAttachmentDescription{ 0, VK_FORMAT_R16_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                     VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            VkAttachmentDescription{ 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                     VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
        };
        const VkAttachmentReference colour = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        const VkAttachmentReference depth = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        const VkSubpassDescription subpass = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1, &colour, nullptr, &depth };
        const std::array dependencies = {
            VkSubpassDependency{ VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_SHADER_READ_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT },
            VkSubpassDependency{ 0, VK_SUBPASS_EXTERNAL,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT },
        };
        const VkRenderPassCreateInfo peelInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0,
                                                   static_cast<uint32_t>(peelAttachments.size()), peelAttachments.data(), 1,
                                                   &subpass, static_cast<uint32_t>(dependencies.size()), dependencies.data() };
        CheckVk(vkCreateRenderPass(_device, &peelInfo, nullptr, &_peelRenderPass), "create transparency peel pass");

        const VkAttachmentDescription composeAttachment = { 0, VK_FORMAT_R8_UINT, VK_SAMPLE_COUNT_1_BIT,
                                                              VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                              VK_ATTACHMENT_STORE_OP_STORE,
                                                              VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                              VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                              VK_IMAGE_LAYOUT_UNDEFINED,
                                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkAttachmentReference composeColour = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        const VkSubpassDescription composeSubpass = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1,
                                                       &composeColour };
        const std::array composeDependencies = {
            VkSubpassDependency{ VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
            VkSubpassDependency{ 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT },
        };
        const VkRenderPassCreateInfo composeInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 1,
                                                      &composeAttachment, 1, &composeSubpass,
                                                      static_cast<uint32_t>(composeDependencies.size()),
                                                      composeDependencies.data() };
        CheckVk(vkCreateRenderPass(_device, &composeInfo, nullptr, &_composeRenderPass), "create transparency compose pass");
    }

    void TransparencyPipeline::CreatePipelines()
    {
        const VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                           sizeof(TransparencyConstants) };
        const VkPipelineLayoutCreateInfo peelLayout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1,
                                                         &_peelSetLayout, 1, &push };
        CheckVk(vkCreatePipelineLayout(_device, &peelLayout, nullptr, &_peelPipelineLayout), "create peel pipeline layout");
        const VkPipelineLayoutCreateInfo composeLayout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1,
                                                            &_composeSetLayout };
        CheckVk(
            vkCreatePipelineLayout(_device, &composeLayout, nullptr, &_composePipelineLayout),
            "create compose pipeline layout");

        const auto create = [&](const char* vertexName, const char* fragmentName, VkPipelineLayout layout,
                                VkRenderPass renderPass, bool rectangles) {
            const auto vertex = LoadShaderModule(_device, _shaderDirectory / vertexName);
            VkShaderModule fragment = VK_NULL_HANDLE;
            try
            {
                fragment = LoadShaderModule(_device, _shaderDirectory / fragmentName);
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
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0,
                rectangles ? 1u : 0u, rectangles ? &kRectCommandBinding : nullptr,
                rectangles ? static_cast<uint32_t>(kRectCommandAttributes.size()) : 0u,
                rectangles ? kRectCommandAttributes.data() : nullptr
            };
            const VkPipelineInputAssemblyStateCreateInfo assembly = {
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
                rectangles ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
            };
            const VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                                  nullptr, 0, 1, nullptr, 1, nullptr };
            const VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                                    nullptr, 0, VK_FALSE, VK_FALSE,
                                                                    VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                                                                    VK_FRONT_FACE_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
            const VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                                       nullptr, 0, VK_SAMPLE_COUNT_1_BIT };
            const VkPipelineDepthStencilStateCreateInfo depthState = {
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0,
                rectangles ? VK_TRUE : VK_FALSE, rectangles ? VK_TRUE : VK_FALSE, VK_COMPARE_OP_GREATER
            };
            const VkPipelineColorBlendAttachmentState attachment = { VK_FALSE, VK_BLEND_FACTOR_ONE,
                                                                      VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                                                                      VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                                      VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT };
            const VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                                 nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &attachment };
            constexpr std::array dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            const VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                                nullptr, 0,
                                                                static_cast<uint32_t>(dynamicStates.size()),
                                                                dynamicStates.data() };
            const VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0,
                                                         static_cast<uint32_t>(stages.size()), stages.data(), &vertexInput,
                                                         &assembly, nullptr, &viewport, &raster, &multisample,
                                                         rectangles ? &depthState : nullptr, &blend, &dynamic, layout,
                                                         renderPass, 0 };
            VkPipeline result = VK_NULL_HANDLE;
            const auto status = vkCreateGraphicsPipelines(_device, _pipelineCache, 1, &info, nullptr, &result);
            vkDestroyShaderModule(_device, vertex, nullptr);
            vkDestroyShaderModule(_device, fragment, nullptr);
            CheckVk(status, "create transparency pipeline");
            return result;
        };
        _peelPipeline = create(
            "indexed_transparent_rect.vert.spv", "indexed_transparent_rect.frag.spv", _peelPipelineLayout,
            _peelRenderPass, true);
        _composePipeline = create(
            "indexed_transparency_compose.vert.spv", "indexed_transparency_compose.frag.spv",
            _composePipelineLayout, _composeRenderPass, false);
    }

    void TransparencyPipeline::CreateFramebuffers(const IndexedResources& resources)
    {
        for (uint32_t frame = 0; frame < kFramesInFlight; frame++)
        {
            for (uint32_t depth = 0; depth < 2; depth++)
            {
                const std::array attachments = { resources.GetTransparentCanvas(frame).GetView(),
                                                 resources.GetTransparentDepthCanvas(frame, depth).GetView() };
                const VkFramebufferCreateInfo info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0,
                                                        _peelRenderPass, static_cast<uint32_t>(attachments.size()),
                                                        attachments.data(), _extent.width, _extent.height, 1 };
                CheckVk(
                    vkCreateFramebuffer(_device, &info, nullptr, &_peelFramebuffers[frame][depth]),
                    "create transparency peel framebuffer");
            }
            const std::array outputs = { resources.GetIndexedCanvas(frame).GetView(),
                                         resources.GetCompositeCanvas(frame).GetView() };
            for (uint32_t output = 0; output < outputs.size(); output++)
            {
                const VkFramebufferCreateInfo info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0,
                                                        _composeRenderPass, 1, &outputs[output], _extent.width,
                                                        _extent.height, 1 };
                CheckVk(
                    vkCreateFramebuffer(_device, &info, nullptr, &_composeFramebuffers[frame][output]),
                    "create transparency compose framebuffer");
            }
        }
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
