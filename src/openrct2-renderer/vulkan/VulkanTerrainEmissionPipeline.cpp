/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#ifdef ENABLE_VULKAN
#include "VulkanTerrainEmissionPipeline.h"
#include "VulkanShader.h"
#include <cstring>
#include <stdexcept>

namespace OpenRCT2::Ui::Vulkan
{
    namespace Terrain = Gpu::Terrain;
    namespace
    {
        struct Parameters
        {
            uint32_t pass;
            uint32_t rotation;
            int32_t zoom;
            uint32_t materialCount;
            uint32_t capacity;
            uint32_t transparentBackground;
            uint32_t blankImage;
            uint32_t reserved;
        };
        static_assert(sizeof(Parameters) == 32 && offsetof(Parameters, capacity) == 16);
        constexpr std::array<VkDeviceSize, 5> kBufferSizes = {
            Terrain::kRetainedTileCount * sizeof(Terrain::RetainedTile),
            Terrain::kRetainedMaterialCapacity * sizeof(Terrain::RetainedMaterial),
            Terrain::kRetainedTileCount * 32, // Eight scalar uint fields per tile count record.
            Terrain::kRetainedEmissionCapacity * sizeof(Terrain::RetainedTerrainPrimitive),
            sizeof(Terrain::RetainedTerrainStatus),
        };

        void MemoryBarrier(VkCommandBuffer command, VkPipelineStageFlags sourceStage, VkAccessFlags sourceAccess,
            VkPipelineStageFlags destinationStage, VkAccessFlags destinationAccess)
        {
            const VkMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = sourceAccess,
                .dstAccessMask = destinationAccess,
            };
            vkCmdPipelineBarrier(command, sourceStage, destinationStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
    }

    TerrainEmissionPipeline::~TerrainEmissionPipeline() { Dispose(); }

    void TerrainEmissionPipeline::Initialise(const DeviceContext& device, const std::filesystem::path& shader, uint32_t storageCapacity)
    {
        Dispose();
        if (storageCapacity == 0 || storageCapacity > Terrain::kRetainedEmissionCapacity)
            throw std::invalid_argument("Terrain emission storage capacity is invalid");
        _device = device.GetDevice();
        _primitiveCapacity = storageCapacity;
        try
        {
            auto bufferSizes = kBufferSizes;
            bufferSizes[3] = VkDeviceSize{ storageCapacity } * sizeof(Terrain::RetainedTerrainPrimitive);
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
            const auto& limits = properties.limits;
            if (limits.maxPerStageDescriptorStorageBuffers < 5 || limits.maxDescriptorSetStorageBuffers < 5
                || limits.maxComputeWorkGroupInvocations < 64 || limits.maxComputeWorkGroupSize[0] < 64
                || limits.maxComputeWorkGroupCount[0] < Terrain::kRetainedTileCount / 64
                || limits.maxStorageBufferRange < bufferSizes[3] || limits.maxPushConstantsSize < sizeof(Parameters))
                throw std::runtime_error("Device limits do not support retained terrain emission");
            for (size_t i = 0; i < _buffers.size(); i++)
                _buffers[i].Initialise(device.GetPhysicalDevice(), _device, bufferSizes[i],
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
            for (uint32_t i = 0; i < bindings.size(); i++)
                bindings[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT };
            const VkDescriptorSetLayoutCreateInfo descriptors{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data(),
            };
            CheckVk(vkCreateDescriptorSetLayout(_device, &descriptors, nullptr, &_descriptorLayout), "terrain emission descriptors");
            const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 };
            const VkDescriptorPoolCreateInfo pool{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size,
            };
            CheckVk(vkCreateDescriptorPool(_device, &pool, nullptr, &_pool), "terrain emission descriptor pool");
            const VkDescriptorSetAllocateInfo allocation{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = _pool, .descriptorSetCount = 1, .pSetLayouts = &_descriptorLayout,
            };
            CheckVk(vkAllocateDescriptorSets(_device, &allocation, &_set), "terrain emission descriptor set");
            std::array<VkDescriptorBufferInfo, 5> infos{};
            std::array<VkWriteDescriptorSet, 5> writes{};
            for (uint32_t i = 0; i < infos.size(); i++)
            {
                infos[i] = { _buffers[i].GetBuffer(), 0, _buffers[i].GetSize() };
                writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = _set, .dstBinding = i,
                    .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &infos[i] };
            }
            vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            const VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters) };
            const VkPipelineLayoutCreateInfo layout{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1, .pSetLayouts = &_descriptorLayout,
                .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
            };
            CheckVk(vkCreatePipelineLayout(_device, &layout, nullptr, &_layout), "terrain emission pipeline layout");
            // The cache is shared with the window and other auxiliary domains.
            auto cacheLock = device.LockPipelineCache();
            const auto module = LoadShaderModule(_device, shader);
            const VkComputePipelineCreateInfo pipeline{
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" },
                .layout = _layout,
            };
            const auto result = vkCreateComputePipelines(_device, device.GetPipelineCache(), 1, &pipeline, nullptr, &_pipeline);
            vkDestroyShaderModule(_device, module, nullptr);
            cacheLock.unlock();
            CheckVk(result, "terrain emission compute pipeline");
        }
        catch (...) { Dispose(); throw; }
    }

    void TerrainEmissionPipeline::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(_device, _pipeline, nullptr);
            vkDestroyPipelineLayout(_device, _layout, nullptr);
            vkDestroyDescriptorPool(_device, _pool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _descriptorLayout, nullptr);
        }
        for (auto& buffer : _buffers) buffer.Dispose();
        _device = VK_NULL_HANDLE; _pipeline = VK_NULL_HANDLE; _layout = VK_NULL_HANDLE;
        _pool = VK_NULL_HANDLE; _descriptorLayout = VK_NULL_HANDLE; _set = VK_NULL_HANDLE;
        _epoch = 0; _primitiveCapacity = 0; DiscardPendingUploads();
    }

    void TerrainEmissionPipeline::DiscardPendingUploads() noexcept
    {
        _chunkRevisions.fill(0);
        _materialRevision = 0;
    }

    void TerrainEmissionPipeline::Record(const SubmissionToken& frame, const Terrain::RetainedTerrainSnapshot& snapshot,
        uint32_t rotation, int32_t zoom, bool transparentBackground, uint32_t outputCapacity)
    {
        if (_pipeline == VK_NULL_HANDLE || frame.commandBuffer == VK_NULL_HANDLE || frame.upload == nullptr)
            throw std::invalid_argument("Terrain emission requires an initialized pipeline and active submission");
        if (rotation >= 4 || zoom < 0 || zoom > 1 || outputCapacity > _primitiveCapacity
            || snapshot.worldEpoch == 0 || snapshot.materials == nullptr || snapshot.materials->revision == 0
            || snapshot.materials->records.empty() || snapshot.materials->records.size() > Terrain::kRetainedMaterialCapacity)
            throw std::invalid_argument("Terrain emission scene or capacity is invalid");
        for (const auto& chunk : snapshot.chunks)
            if (chunk == nullptr || chunk->revision == 0)
                throw std::invalid_argument("Terrain emission requires every immutable tile chunk");
        if (_epoch != snapshot.worldEpoch)
        {
            _epoch = snapshot.worldEpoch;
            DiscardPendingUploads();
        }

        // Queue serialization and this dependency retire previous shader consumers
        // and optional diagnostic transfer reads before reusing retained/transient buffers.
        MemoryBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        _lastTileUploadBytes = _lastMaterialUploadBytes = 0;
        const auto upload = [&](size_t buffer, VkDeviceSize offset, const void* data, VkDeviceSize bytes) {
            auto allocation = frame.upload->Allocate(bytes, alignof(uint32_t), Drawing::UploadCategory::world);
            if (!allocation) throw std::runtime_error("Upload ring cannot hold terrain state delta");
            std::memcpy(allocation.data, data, static_cast<size_t>(bytes));
            allocation.RecordHostWrite();
            const VkBufferCopy copy{ allocation.offset, offset, bytes };
            vkCmdCopyBuffer(frame.commandBuffer, allocation.buffer, _buffers[buffer].GetBuffer(), 1, &copy);
            allocation.Record(Drawing::UploadMetric::bufferTransfer, bytes);
            (buffer == 0 ? _lastTileUploadBytes : _lastMaterialUploadBytes) += bytes;
        };
        // Only chunk metadata is visited on an unchanged frame. No CPU tile walk.
        try
        {
            constexpr VkDeviceSize chunkBytes = Terrain::kRetainedChunkSize * sizeof(Terrain::RetainedTile);
            for (size_t i = 0; i < snapshot.chunks.size(); i++)
            {
                const auto& chunk = *snapshot.chunks[i];
                if (_chunkRevisions[i] == chunk.revision) continue;
                upload(0, i * chunkBytes, chunk.records.data(), chunkBytes);
                _chunkRevisions[i] = chunk.revision;
            }
            if (_materialRevision != snapshot.materials->revision)
            {
                upload(1, 0, snapshot.materials->records.data(), snapshot.materials->records.size() * sizeof(Terrain::RetainedMaterial));
                _materialRevision = snapshot.materials->revision;
            }
        }
        catch (...) { DiscardPendingUploads(); throw; }
        MemoryBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        Parameters parameters{ 0, rotation, zoom, static_cast<uint32_t>(snapshot.materials->records.size()),
            outputCapacity, transparentBackground ? 1u : 0u, 3123, 0 };
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _layout, 0, 1, &_set, 0, nullptr);
        for (uint32_t pass = 0; pass < 3; pass++)
        {
            parameters.pass = pass;
            vkCmdPushConstants(frame.commandBuffer, _layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
            vkCmdDispatch(frame.commandBuffer, pass == 1 ? 1 : Terrain::kRetainedTileCount / 64, 1, 1);
            MemoryBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
        }
    }
} // namespace OpenRCT2::Ui::Vulkan
#endif
