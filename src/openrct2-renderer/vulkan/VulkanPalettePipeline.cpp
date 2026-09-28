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

        void RecordPass(
            const SubmissionToken& frame, VkRenderPass renderPass, VkFramebuffer framebuffer, VkExtent2D extent,
            VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptor, const OutputConstants& output)
        {
            const VkRenderPassBeginInfo begin = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = renderPass,
                .framebuffer = framebuffer,
                .renderArea = { .offset = { 0, 0 }, .extent = extent },
            };
            vkCmdBeginRenderPass(frame.commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
            const VkViewport viewport = {
                .width = static_cast<float>(extent.width),
                .height = static_cast<float>(extent.height),
                .maxDepth = 1.0f,
            };
            const VkRect2D scissor = { .offset = { 0, 0 }, .extent = extent };
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(
                frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
            vkCmdPushConstants(frame.commandBuffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(output), &output);
            vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(frame.commandBuffer);
        }
    } // namespace

    void PalettePipeline::SetHdrPaperWhiteNits(float nits) noexcept
    {
        _paperWhiteNits = Gpu::NormaliseHdrPaperWhiteNits(nits);
    }

    PalettePipeline::~PalettePipeline()
    {
        Dispose();
    }

    void PalettePipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory, float paperWhiteNits)
    {
        Initialise(*device.GetContext(), resources, std::move(shaderDirectory), paperWhiteNits);
        RefreshSwapchain(device);
    }

    void PalettePipeline::Initialise(
        const DeviceContext& device, const IndexedResources& resources, std::filesystem::path shaderDirectory,
        float paperWhiteNits)
    {
        Dispose();
        _context = &device;
        const auto cacheLock = device.LockPipelineCache();
        _frameCount = resources.GetFrameCount();
        _device = device.GetDevice();
        _physicalDevice = device.GetPhysicalDevice();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(_physicalDevice, &properties);
        _maxImageDimension = properties.limits.maxImageDimension2D;
        _nearestSampler = resources.GetNearestSampler();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        _paperWhiteNits = Gpu::NormaliseHdrPaperWhiteNits(paperWhiteNits);
        CreateDescriptorResources(resources);
        CreateScaleResources();
    }

    void PalettePipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            DestroySwapchainResources();
            for (uint32_t i = 0; i < kFramesInFlight; i++)
            {
                vkDestroyFramebuffer(_device, _rgbaFramebuffers[i], nullptr);
                _rgbaImages[i].Dispose();
            }
            vkDestroyPipeline(_device, _rgbaPipeline, nullptr);
            vkDestroyRenderPass(_device, _rgbaRenderPass, nullptr);
            vkDestroyPipelineLayout(_device, _scalePipelineLayout, nullptr);
            vkDestroyDescriptorPool(_device, _scaleDescriptorPool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _scaleDescriptorLayout, nullptr);
            vkDestroySampler(_device, _linearSampler, nullptr);
            vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
        }

        _device = VK_NULL_HANDLE;
        _physicalDevice = VK_NULL_HANDLE;
        _maxImageDimension = 0;
        _rgbaFramebuffers = {};
        _rgbaPipeline = VK_NULL_HANDLE;
        _rgbaRenderPass = VK_NULL_HANDLE;
        _scalePipelineLayout = VK_NULL_HANDLE;
        _scaleDescriptorPool = VK_NULL_HANDLE;
        _scaleDescriptorLayout = VK_NULL_HANDLE;
        _scaleDescriptorSets = {};
        _linearSampler = VK_NULL_HANDLE;
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
            throw std::logic_error("Palette pipeline belongs to a different Vulkan device");
        const auto format = device.GetSwapchainFormat();
        const int32_t encoding = device.IsHdr10Active() ? static_cast<int32_t>(OutputEncoding::Hdr10Pq)
            : (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB)
            ? static_cast<int32_t>(OutputEncoding::SrgbAttachment)
            : static_cast<int32_t>(OutputEncoding::LegacyBytes);
        RefreshOutput(
            format, device.GetSwapchainExtent(), device.GetSwapchainImageViews(), device.GetSwapchainGeneration(), encoding,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    void PalettePipeline::RefreshOutput(
        VkFormat format, VkExtent2D extent, std::span<const VkImageView> views, uint64_t generation, int32_t encoding,
        VkImageLayout finalLayout)
    {
        if (generation == 0 || extent.width == 0 || extent.height == 0 || views.empty()
            || (finalLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && finalLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            || encoding < 0 || encoding > 2)
            throw std::invalid_argument("Invalid Vulkan palette output target");
        if (_swapchainGeneration == generation)
            return;
        const auto cacheLock = _context->LockPipelineCache();
        DestroySwapchainResources();
        _swapchainFormat = format;
        _swapchainExtent = extent;
        _swapchainGeneration = generation;
        _outputEncoding = encoding;
        _finalLayout = finalLayout;
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(views);
    }

    void PalettePipeline::ReleaseSwapchainResources()
    {
        DestroySwapchainResources();
    }

    void PalettePipeline::RefreshDescriptors(const IndexedResources& resources)
    {
        const auto imageInfo = [sampler = resources.GetNearestSampler()](const Image& image) {
            return VkDescriptorImageInfo{ sampler, image.GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        };
        for (uint32_t i = 0; i < resources.GetFrameCount(); i++)
        {
            const std::array infos = {
                imageInfo(resources.GetIndexedCanvas(i)),
                imageInfo(resources.GetPalette(i)),
                imageInfo(resources.GetLightMap(i)),
                imageInfo(resources.GetLightPalette(i)),
            };
            std::array<VkWriteDescriptorSet, infos.size()> writes{};
            for (uint32_t binding = 0; binding < writes.size(); binding++)
            {
                writes[binding] = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = _descriptorSets[i],
                    .dstBinding = binding,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &infos[binding],
                };
            }
            vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void PalettePipeline::SetCanvasSource(uint32_t frameIndex, const Image& canvas)
    {
        if (frameIndex >= _frameCount)
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
        if (frameIndex >= _frameCount)
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

    void PalettePipeline::Record(
        const FrameToken& frame, bool lightFxEnabled, Gpu::Extent logicalExtent, Gpu::ScaleSettings scaleSettings)
    {
        Record(frame.submission, frame.imageIndex, frame.extent, lightFxEnabled, logicalExtent, scaleSettings);
    }

    void PalettePipeline::Record(
        const SubmissionToken& frame, uint32_t imageIndex, VkExtent2D extent, bool lightFxEnabled, Gpu::Extent logicalExtent,
        Gpu::ScaleSettings scaleSettings)
    {
        if (imageIndex >= _framebuffers.size() || frame.frameIndex >= _frameCount)
        {
            throw std::out_of_range("Vulkan palette frame index is out of range");
        }

        const OutputConstants output = { _outputEncoding, _paperWhiteNits, lightFxEnabled ? 1 : 0 };
        if (scaleSettings.mode == Gpu::ScaleMode::Nearest)
        {
            RecordPass(
                frame, _renderPass, _framebuffers[imageIndex], extent, _pipeline, _pipelineLayout,
                _descriptorSets[frame.frameIndex], output);
            return;
        }
        const uint32_t factor = scaleSettings.mode == Gpu::ScaleMode::SmoothNearest ? scaleSettings.integerScale : 1;
        if (factor == 0 || logicalExtent.width > _maxImageDimension / factor
            || logicalExtent.height > _maxImageDimension / factor)
            throw std::length_error("Vulkan RGBA scaling target exceeds device image limits");
        const VkExtent2D rgbaExtent{ logicalExtent.width * factor, logicalExtent.height * factor };
        EnsureScaleTarget(frame.frameIndex, rgbaExtent);
        const OutputConstants expansion = { static_cast<int32_t>(OutputEncoding::LegacyBytes), _paperWhiteNits,
                                            lightFxEnabled ? 1 : 0 };
        RecordPass(
            frame, _rgbaRenderPass, _rgbaFramebuffers[frame.frameIndex], rgbaExtent, _rgbaPipeline, _pipelineLayout,
            _descriptorSets[frame.frameIndex], expansion);
        RecordPass(
            frame, _renderPass, _framebuffers[imageIndex], extent, _scalePipeline, _scalePipelineLayout,
            _scaleDescriptorSets[frame.frameIndex], output);
    }

    void PalettePipeline::CreateDescriptorResources(const IndexedResources& resources)
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        for (uint32_t binding = 0; binding < bindings.size(); binding++)
        {
            bindings[binding] = {
                .binding = binding,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
        }
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

    void PalettePipeline::CreateScaleResources()
    {
        const VkSamplerCreateInfo sampler = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        };
        CheckVk(vkCreateSampler(_device, &sampler, nullptr, &_linearSampler), "create RGBA scaling sampler");
        const VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        const VkDescriptorSetLayoutCreateInfo layout = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &binding,
        };
        CheckVk(
            vkCreateDescriptorSetLayout(_device, &layout, nullptr, &_scaleDescriptorLayout), "create scale descriptor layout");
        const VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight };
        const VkDescriptorPoolCreateInfo pool = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = kFramesInFlight,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        CheckVk(vkCreateDescriptorPool(_device, &pool, nullptr, &_scaleDescriptorPool), "create scale descriptor pool");
        std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
        layouts.fill(_scaleDescriptorLayout);
        const VkDescriptorSetAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = _scaleDescriptorPool,
            .descriptorSetCount = kFramesInFlight,
            .pSetLayouts = layouts.data(),
        };
        CheckVk(vkAllocateDescriptorSets(_device, &allocation, _scaleDescriptorSets.data()), "allocate scale descriptors");
        const VkPushConstantRange constants = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutputConstants) };
        const VkPipelineLayoutCreateInfo pipelineLayout = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &_scaleDescriptorLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &constants,
        };
        CheckVk(
            vkCreatePipelineLayout(_device, &pipelineLayout, nullptr, &_scalePipelineLayout), "create scale pipeline layout");
        const VkAttachmentDescription attachment = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkAttachmentReference reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        const VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &reference,
        };
        const std::array dependencies = {
            VkSubpassDependency{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
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
        const VkRenderPassCreateInfo renderPass = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &attachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = static_cast<uint32_t>(dependencies.size()),
            .pDependencies = dependencies.data(),
        };
        CheckVk(vkCreateRenderPass(_device, &renderPass, nullptr, &_rgbaRenderPass), "create RGBA expansion pass");
        _rgbaPipeline = CreateGraphicsPipeline(
            _device, _pipelineCache,
            {
                .vertexShader = _shaderDirectory / "indexed_palette.vert.spv",
                .fragmentShader = _shaderDirectory / "indexed_palette.frag.spv",
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT,
                .layout = _pipelineLayout,
                .renderPass = _rgbaRenderPass,
            },
            "create RGBA expansion pipeline");
    }

    void PalettePipeline::EnsureScaleTarget(uint32_t frameIndex, VkExtent2D extent)
    {
        const auto previous = _rgbaImages[frameIndex].GetExtent();
        if (previous.width == extent.width && previous.height == extent.height
            && _rgbaFramebuffers[frameIndex] != VK_NULL_HANDLE)
            return;
        // The acquired slot's fence protects both its old target and descriptor.
        vkDestroyFramebuffer(_device, _rgbaFramebuffers[frameIndex], nullptr);
        _rgbaFramebuffers[frameIndex] = VK_NULL_HANDLE;
        _rgbaImages[frameIndex].Initialise(
            _physicalDevice, _device, { extent.width, extent.height, 1 }, 1, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        const auto view = _rgbaImages[frameIndex].GetView();
        const VkFramebufferCreateInfo framebuffer = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = _rgbaRenderPass,
            .attachmentCount = 1,
            .pAttachments = &view,
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };
        CheckVk(vkCreateFramebuffer(_device, &framebuffer, nullptr, &_rgbaFramebuffers[frameIndex]), "create RGBA framebuffer");
        const VkDescriptorImageInfo image = { _linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = _scaleDescriptorSets[frameIndex],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image,
        };
        vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
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
            .finalLayout = _finalLayout,
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
        _scalePipeline = CreateGraphicsPipeline(
            _device, _pipelineCache,
            {
                .vertexShader = _shaderDirectory / "indexed_palette.vert.spv",
                .fragmentShader = _shaderDirectory / "rgba_scale.frag.spv",
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT,
                .layout = _scalePipelineLayout,
                .renderPass = _renderPass,
            },
            "create RGBA scale pipeline");
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
        vkDestroyPipeline(_device, _scalePipeline, nullptr);
        vkDestroyRenderPass(_device, _renderPass, nullptr);
        _pipeline = VK_NULL_HANDLE;
        _scalePipeline = VK_NULL_HANDLE;
        _renderPass = VK_NULL_HANDLE;
    }

} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
