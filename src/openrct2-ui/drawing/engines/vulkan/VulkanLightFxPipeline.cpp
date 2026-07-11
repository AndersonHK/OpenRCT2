#ifdef ENABLE_VULKAN

    #include "VulkanLightFxPipeline.h"

    #include "VulkanShader.h"

    #include <algorithm>
    #include <cstring>
    #include <stdexcept>
    #include <string>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        static_assert(
            Gpu::kMaximumLightFxCommandCount <= 65535, "LightFX dispatch Z exceeds Vulkan's required minimum");

        void CheckVk(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
        }
    } // namespace

    LightFxPipeline::~LightFxPipeline()
    {
        Dispose();
    }

    void LightFxPipeline::Initialise(
        const Device& device, const IndexedResources& resources, const std::filesystem::path& shaderDirectory,
        bool enableGpuRasterization)
    {
        Dispose();
        _device = device.GetDevice();
        _pipelineCache = device.GetPipelineCache();
        if (!enableGpuRasterization || !resources.HasLightAccumulators())
            return;
        const auto extent = resources.GetLightAccumulator(0).GetExtent();
        _extent = { extent.width, extent.height };

        const std::array bindings = {
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        };
        const VkDescriptorSetLayoutCreateInfo setInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, static_cast<uint32_t>(bindings.size()),
            bindings.data()
        };
        CheckVk(vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_descriptorSetLayout), "create LightFX set layout");
        const std::array poolSizes = {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kFramesInFlight },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight },
        };
        const VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
                                                       kFramesInFlight, static_cast<uint32_t>(poolSizes.size()),
                                                       poolSizes.data() };
        CheckVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool), "create LightFX descriptor pool");
        std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
        layouts.fill(_descriptorSetLayout);
        const VkDescriptorSetAllocateInfo allocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                                          _descriptorPool, kFramesInFlight, layouts.data() };
        CheckVk(vkAllocateDescriptorSets(_device, &allocation, _descriptorSets.data()), "allocate LightFX descriptors");
        const VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) };
        const VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1,
                                                        &_descriptorSetLayout, 1, &push };
        CheckVk(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_pipelineLayout), "create LightFX pipeline layout");
        const auto shader = LoadShaderModule(_device, shaderDirectory / "lightfx_accumulate.comp.spv");
        const VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                                        VK_SHADER_STAGE_COMPUTE_BIT, shader, "main" };
        const VkComputePipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
                                                           stage, _pipelineLayout };
        const auto result = vkCreateComputePipelines(_device, _pipelineCache, 1, &pipelineInfo, nullptr, &_pipeline);
        vkDestroyShaderModule(_device, shader, nullptr);
        CheckVk(result, "create LightFX compute pipeline");
        RefreshDescriptors(resources);
        _available = true;
    }

    bool LightFxPipeline::IsSupported(const Device& device, Gpu::Extent logicalExtent) noexcept
    {
        if (logicalExtent.width == 0 || logicalExtent.height == 0)
            return false;

        const auto physicalDevice = device.GetPhysicalDevice();
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R32_UINT, &formatProperties);
        constexpr VkFormatFeatureFlags requiredFormatFeatures =
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT
            | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((formatProperties.optimalTilingFeatures & requiredFormatFeatures) != requiredFormatFeatures)
            return false;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        if (!Gpu::AreLightFxComputeLimitsSufficient(
                properties.limits.maxComputeWorkGroupInvocations, properties.limits.maxComputeWorkGroupSize[0],
                properties.limits.maxComputeWorkGroupSize[1], properties.limits.maxComputeWorkGroupCount[2]))
        {
            return false;
        }

        constexpr VkImageUsageFlags usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageFormatProperties imageProperties{};
        if (vkGetPhysicalDeviceImageFormatProperties(
                physicalDevice, VK_FORMAT_R32_UINT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                &imageProperties)
            != VK_SUCCESS)
        {
            return false;
        }
        const uint64_t requiredBytes = static_cast<uint64_t>(logicalExtent.width) * logicalExtent.height * sizeof(uint32_t);
        return logicalExtent.width <= imageProperties.maxExtent.width
            && logicalExtent.height <= imageProperties.maxExtent.height && imageProperties.maxExtent.depth >= 1
            && imageProperties.maxMipLevels >= 1 && imageProperties.maxArrayLayers >= 1
            && (imageProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0
            && requiredBytes <= imageProperties.maxResourceSize;
    }

    void LightFxPipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            if (_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(_device, _pipeline, nullptr);
            if (_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            if (_descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            if (_descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
        }
        _device = VK_NULL_HANDLE;
        _pipeline = VK_NULL_HANDLE;
        _pipelineLayout = VK_NULL_HANDLE;
        _descriptorPool = VK_NULL_HANDLE;
        _descriptorSetLayout = VK_NULL_HANDLE;
        _descriptorSets = {};
        _pipelineCache = VK_NULL_HANDLE;
        _extent = {};
        _available = false;
    }

    void LightFxPipeline::RefreshDescriptors(const IndexedResources& resources)
    {
        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            const VkDescriptorImageInfo accumulator = { VK_NULL_HANDLE, resources.GetLightAccumulator(i).GetView(),
                                                        VK_IMAGE_LAYOUT_GENERAL };
            const VkDescriptorImageInfo falloffs = { resources.GetNearestSampler(), resources.GetLightFalloffs().GetView(),
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            const std::array writes = {
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = _descriptorSets[i],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = &accumulator,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = _descriptorSets[i],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &falloffs,
                },
            };
            vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    bool LightFxPipeline::Record(
        const FrameToken& frame, const Gpu::LightFxFrameSnapshot& snapshot, IndexedResources& resources) const
    {
        if (!_available || snapshot.lights.size() > Gpu::kMaximumLightFxCommandCount) return false;
        UploadAllocation allocation{};
        if (!snapshot.lights.empty())
        {
            const VkDeviceSize byteSize = snapshot.lights.size() * sizeof(Gpu::LightFxCommand);
            allocation = frame.upload->Allocate(byteSize, alignof(uint32_t));
            if (!allocation) return false;
            std::memcpy(allocation.data, snapshot.lights.data(), static_cast<size_t>(byteSize));
        }
        const bool hasLights = !snapshot.lights.empty();
        resources.PrepareLightAccumulator(frame.commandBuffer, frame.frameIndex, hasLights);
        if (!hasLights)
            return true;

        const VkDescriptorBufferInfo commands = { allocation.buffer, 0, VK_WHOLE_SIZE };
        const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = _descriptorSets[frame.frameIndex],
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &commands,
        };
        vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
        const uint32_t wordOffset = static_cast<uint32_t>(allocation.offset / sizeof(uint32_t));
        uint32_t maxWidth = 0;
        uint32_t maxHeight = 0;
        for (const auto& light : snapshot.lights)
        {
            maxWidth = std::max(maxWidth, light.width);
            maxHeight = std::max(maxHeight, light.height);
        }
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1,
                                &_descriptorSets[frame.frameIndex], 0, nullptr);
        vkCmdPushConstants(frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(wordOffset),
                           &wordOffset);
        vkCmdDispatch(
            frame.commandBuffer,
            (maxWidth + Gpu::kLightFxComputeLocalSizeX - 1) / Gpu::kLightFxComputeLocalSizeX,
            (maxHeight + Gpu::kLightFxComputeLocalSizeY - 1) / Gpu::kLightFxComputeLocalSizeY,
            static_cast<uint32_t>(snapshot.lights.size()));
        resources.FinishLightAccumulator(frame.commandBuffer, frame.frameIndex);
        return true;
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif
