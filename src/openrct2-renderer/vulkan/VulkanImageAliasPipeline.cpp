/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#ifdef ENABLE_VULKAN
    #include "VulkanImageAliasPipeline.h"
    #include "VulkanShader.h"
    #include <algorithm>
    #include <array>
    #include <cstring>
    #include <stdexcept>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        struct AliasPush
        {
            uint32_t stride, sourceX, sourceY, destinationX, destinationY, width, first, count, flags;
        };
        static_assert(sizeof(AliasPush) == 36);
    }

    ImageAliasPipeline::~ImageAliasPipeline() { Dispose(); }

    void ImageAliasPipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(_device, _pipeline, nullptr);
            vkDestroyPipelineLayout(_device, _layout, nullptr);
            vkDestroyDescriptorPool(_device, _pool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _setLayout, nullptr);
        }
        _device = VK_NULL_HANDLE;
        _pipeline = VK_NULL_HANDLE;
        _layout = VK_NULL_HANDLE;
        _pool = VK_NULL_HANDLE;
        _setLayout = VK_NULL_HANDLE;
        _set = VK_NULL_HANDLE;
    }

    void ImageAliasPipeline::Initialise(const DeviceContext& device, const std::filesystem::path& shaders)
    {
        if (_pipeline != VK_NULL_HANDLE)
            return;
        Dispose();
        _device = device.GetDevice();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
        if (properties.limits.maxStorageBufferRange < Drawing::kOrderedImageAliasMaxPixels)
            throw std::runtime_error("Ordered bitmap alias exceeds storage buffer device limit");
        _alignment = std::max<VkDeviceSize>(4, properties.limits.minStorageBufferOffsetAlignment);
        const std::array bindings = {
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        };
        const VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            static_cast<uint32_t>(bindings.size()), bindings.data() };
        CheckVk(vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_setLayout), "create bitmap alias set layout");
        const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &size };
        CheckVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_pool), "create bitmap alias descriptor pool");
        const VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
            _pool, 1, &_setLayout };
        CheckVk(vkAllocateDescriptorSets(_device, &allocation, &_set), "allocate bitmap alias descriptor set");
        const VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(AliasPush) };
        const VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0,
            1, &_setLayout, 1, &push };
        CheckVk(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_layout), "create bitmap alias pipeline layout");
        const auto shader = LoadShaderModule(_device, shaders / "image_alias.comp.spv");
        const VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
            VK_SHADER_STAGE_COMPUTE_BIT, shader, "main" };
        const VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stage, _layout };
        VkResult result;
        {
            const auto cacheLock = device.LockPipelineCache();
            result = vkCreateComputePipelines(_device, device.GetPipelineCache(), 1, &info, nullptr, &_pipeline);
        }
        vkDestroyShaderModule(_device, shader, nullptr);
        CheckVk(result, "create ordered bitmap alias pipeline");
    }

    UploadAllocation ImageAliasPipeline::Record(
        const SubmissionToken& token, const Drawing::OffscreenRenderRequest& request)
    {
        Drawing::ValidateOffscreenRenderRequest(request);
        if (!request.orderedAlias || _pipeline == VK_NULL_HANDLE)
            throw std::runtime_error("Ordered bitmap alias pipeline is not ready");
        const auto& alias = *request.orderedAlias;
        const auto byteCount = request.initialIndices.size();
        const auto pixels = token.upload->Allocate((byteCount + 3) & ~size_t{ 3 }, _alignment);
        const auto remap = token.upload->Allocate(256, _alignment);
        if (!pixels || !remap)
            throw std::runtime_error("Ordered bitmap alias exceeds bounded upload ring");
        std::memset(pixels.data, 0, static_cast<size_t>(pixels.size));
        std::memcpy(pixels.data, request.initialIndices.data(), byteCount);
        std::memcpy(remap.data, alias.remap.data(), alias.remap.size());
        const std::array buffers = {
            VkDescriptorBufferInfo{ pixels.buffer, pixels.offset, pixels.size },
            VkDescriptorBufferInfo{ remap.buffer, remap.offset, remap.size },
        };
        std::array<VkWriteDescriptorSet, 2> writes{};
        for (uint32_t i = 0; i < writes.size(); ++i)
        {
            writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = _set, .dstBinding = i,
                .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffers[i] };
        }
        vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
        vkCmdBindDescriptorSets(token.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _layout, 0, 1, &_set, 0, nullptr);
        VkBufferMemoryBarrier barrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = pixels.buffer, .offset = pixels.offset, .size = pixels.size };
        // SubmissionSlots flushes the mapped upload range before queue submission.
        vkCmdPipelineBarrier(token.commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
        const uint32_t total = alias.width * alias.height;
        for (uint32_t first = 0; first < total; first += Drawing::kOrderedImageAliasSlicePixels)
        {
            const AliasPush push{ request.logicalExtent.width, alias.sourceX, alias.sourceY, alias.destinationX,
                alias.destinationY, alias.width, first, std::min(Drawing::kOrderedImageAliasSlicePixels, total - first),
                (alias.skipSourceZero ? 1u : 0u) | (alias.skipMappedZero ? 2u : 0u) };
            vkCmdPushConstants(token.commandBuffer, _layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            vkCmdDispatch(token.commandBuffer, 1, 1, 1);
            // The next slice must read the preceding slice's writes, including a shared packed uint word.
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(token.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
        }
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(token.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
        return pixels;
    }
}
#endif
