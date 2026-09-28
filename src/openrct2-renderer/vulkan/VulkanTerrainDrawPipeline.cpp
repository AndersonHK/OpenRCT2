// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifdef ENABLE_VULKAN
    #include "VulkanTerrainDrawPipeline.h"

    #include "VulkanShader.h"

    #include <algorithm>
    #include <cmath>
    #include <cstring>
    #include <stdexcept>

namespace OpenRCT2::Ui::Vulkan
{
    namespace Terrain = Gpu::Terrain;
    namespace
    {
        struct Parameters
        {
            int32_t x, y, width, height, clipX, clipY;
            uint32_t rotation;
            int32_t zoom;
            uint32_t transparent, stable;
            int32_t depthBase;
            uint32_t spriteCount, columnCount, pass, assetCount, limits, peepEnabled;
            float entityInterpolation;
            uint32_t sourceTick;
        };
        static_assert(sizeof(Parameters) == 76 && offsetof(Parameters, columnCount) == 48);
        static_assert(sizeof(Gpu::SpriteCommand) == 60);
        constexpr VkDeviceSize kSpritesPerColumn = Terrain::kDrawColumnCapacity * sizeof(Gpu::SpriteCommand);
        int32_t FloorTo(int32_t value, int32_t interval)
        {
            const int32_t remainder = value % interval;
            return value - remainder - (remainder < 0 ? interval : 0);
        }
        void Barrier(
            VkCommandBuffer command, VkPipelineStageFlags fromStage, VkAccessFlags fromAccess, VkPipelineStageFlags toStage,
            VkAccessFlags toAccess)
        {
            const VkMemoryBarrier barrier{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                           .srcAccessMask = fromAccess,
                                           .dstAccessMask = toAccess };
            vkCmdPipelineBarrier(command, fromStage, toStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
        void ValidateTable(const Terrain::DrawSpriteTable& table, uint32_t assetCount)
        {
            uint32_t previous = 0;
            bool first = true;
            for (const auto& sprite : table.records)
            {
                if ((!first && sprite.imageIndex <= previous) || sprite.width < 1 || sprite.width > 2048 || sprite.height < 1
                    || sprite.height > 2048 || sprite.xOffset < -32768 || sprite.xOffset > 32767 || sprite.yOffset < -32768
                    || sprite.yOffset > 32767 || sprite.flags != 0 || sprite.reserved0 != 0 || sprite.reserved1 != 0)
                    throw std::invalid_argument("Unsupported terrain sprite metadata");
                first = false;
                previous = sprite.imageIndex;
                for (int32_t zoom = 0; zoom < 2; zoom++)
                {
                    const auto& variant = sprite.variants[zoom];
                    if ((variant.flags & 2u) != 0)
                    {
                        if (variant.flags != 2u)
                            throw std::invalid_argument("Invalid noZoomDraw variant");
                        continue;
                    }
                    if (variant.width < 1 || variant.width > 2048 || variant.height < 1 || variant.height > 2048
                        || variant.xOffset < -32768 || variant.xOffset > 32767 || variant.yOffset < -32768
                        || variant.yOffset > 32767 || variant.flags > 1 || variant.asset >= assetCount
                        || variant.effectiveZoom < 0 || variant.effectiveZoom > 1 || variant.coordinateShift < 0
                        || variant.coordinateShift > 1 || variant.effectiveZoom + variant.coordinateShift != zoom)
                        throw std::invalid_argument("Unsupported terrain sprite zoom variant");
                }
            }
        }
    } // namespace

    TerrainDrawPipeline::~TerrainDrawPipeline()
    {
        Dispose();
    }

    void TerrainDrawPipeline::Initialise(
        const DeviceContext& device, const IndexedResources& resources, const std::filesystem::path& shaderDirectory,
        const std::filesystem::path& emissionShader, const std::filesystem::path& columnShader)
    {
        Dispose();
        _device = device.GetDevice();
        _extent = resources.GetIndexedCanvas(0).GetExtent();
        _frameCount = resources.GetFrameCount();
        _assetCapacity = resources.GetAtlasLayers() * Gpu::kAtlasSlotsPerLayer;
        _columnCapacity = std::min(Terrain::kDrawMaximumColumns, (_extent.width + 15u) / 16u + 1u);
        _bufferSizes = { VkDeviceSize{ Terrain::kDrawSpriteCapacity } * sizeof(Terrain::DrawSpriteMetadata),
                         VkDeviceSize{ _columnCapacity } * Terrain::kDrawColumnCapacity * sizeof(Terrain::DrawParent),
                         VkDeviceSize{ _columnCapacity } * kSpritesPerColumn,
                         VkDeviceSize{ _columnCapacity } * sizeof(Terrain::DrawColumnStatus) };
        try
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
            const auto& limits = properties.limits;
            if (limits.maxPerStageDescriptorStorageBuffers < 11 || limits.maxDescriptorSetStorageBuffers < 11
                || limits.maxComputeWorkGroupCount[0] < _columnCapacity
                || limits.maxComputeSharedMemorySize < 2002 * sizeof(int32_t)
                || limits.maxStorageBufferRange < *std::max_element(_bufferSizes.begin(), _bufferSizes.end())
                || limits.maxPushConstantsSize < sizeof(Parameters))
                throw std::runtime_error("Device limits do not support retained terrain drawing");
            _emission.Initialise(device, emissionShader);
            _peeps.Initialise(device, shaderDirectory / "peep_fields.comp.spv");
            for (size_t i = 0; i < _buffers.size(); i++)
            {
                VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                if (i == 2)
                    usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                if (i == 3)
                    usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                _buffers[i].Initialise(device.GetPhysicalDevice(), _device, _bufferSizes[i], usage);
            }
            std::array<VkDescriptorSetLayoutBinding, 11> bindings{};
            for (uint32_t i = 0; i < bindings.size(); i++)
                bindings[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT };
            const VkDescriptorSetLayoutCreateInfo descriptors{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                               .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                               .pBindings = bindings.data() };
            CheckVk(
                vkCreateDescriptorSetLayout(_device, &descriptors, nullptr, &_descriptorLayout), "terrain draw descriptors");
            const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11 };
            const VkDescriptorPoolCreateInfo pool{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size
            };
            CheckVk(vkCreateDescriptorPool(_device, &pool, nullptr, &_pool), "terrain draw descriptor pool");
            const VkDescriptorSetAllocateInfo allocation{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                          .descriptorPool = _pool,
                                                          .descriptorSetCount = 1,
                                                          .pSetLayouts = &_descriptorLayout };
            CheckVk(vkAllocateDescriptorSets(_device, &allocation, &_set), "terrain draw descriptor set");
            const std::array<const Buffer*, 11> buffers = { &_emission.GetTileBuffer(),
                                                            &_emission.GetTileCountBuffer(),
                                                            &_emission.GetPrimitiveBuffer(),
                                                            &_emission.GetStatusBuffer(),
                                                            &_buffers[0],
                                                            &_buffers[1],
                                                            &_buffers[2],
                                                            &_buffers[3],
                                                            &_peeps.GetFields(),
                                                            &_peeps.GetBins(),
                                                            &_peeps.GetCatalog() };
            std::array<VkDescriptorBufferInfo, 11> infos{};
            std::array<VkWriteDescriptorSet, 11> writes{};
            for (uint32_t i = 0; i < buffers.size(); i++)
            {
                infos[i] = { buffers[i]->GetBuffer(), 0, buffers[i]->GetSize() };
                writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstSet = _set,
                              .dstBinding = i,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                              .pBufferInfo = &infos[i] };
            }
            vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            const VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters) };
            const VkPipelineLayoutCreateInfo layout{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .setLayoutCount = 1,
                                                     .pSetLayouts = &_descriptorLayout,
                                                     .pushConstantRangeCount = 1,
                                                     .pPushConstantRanges = &push };
            CheckVk(vkCreatePipelineLayout(_device, &layout, nullptr, &_layout), "terrain draw pipeline layout");
            auto cacheLock = device.LockPipelineCache();
            const auto module = LoadShaderModule(_device, columnShader);
            const VkComputePipelineCreateInfo pipeline{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                                   .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                                   .module = module,
                                                                   .pName = "main" },
                                                        .layout = _layout };
            const auto result = vkCreateComputePipelines(_device, device.GetPipelineCache(), 1, &pipeline, nullptr, &_pipeline);
            vkDestroyShaderModule(_device, module, nullptr);
            CheckVk(result, "terrain column compute pipeline");
            _rect.Initialise(device, resources, shaderDirectory);
        }
        catch (...)
        {
            Dispose();
            throw;
        }
    }

    void TerrainDrawPipeline::Dispose()
    {
        _rect.Dispose();
        if (_device != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(_device, _pipeline, nullptr);
            vkDestroyPipelineLayout(_device, _layout, nullptr);
            vkDestroyDescriptorPool(_device, _pool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _descriptorLayout, nullptr);
        }
        for (auto& buffer : _buffers)
            buffer.Dispose();
        _emission.Dispose();
        _peeps.Dispose();
        _device = VK_NULL_HANDLE;
        _pipeline = VK_NULL_HANDLE;
        _layout = VK_NULL_HANDLE;
        _pool = VK_NULL_HANDLE;
        _descriptorLayout = VK_NULL_HANDLE;
        _set = VK_NULL_HANDLE;
        _extent = {};
        _frameCount = 0;
        _assetCapacity = 0;
        _epoch = 0;
        DiscardPendingUploads();
    }

    void TerrainDrawPipeline::DiscardPendingUploads() noexcept
    {
        _emission.DiscardPendingUploads();
        _peeps.Discard();
        _spriteRevision = 0;
        _assetCount = 0;
        _spriteCount = 0;
    }

    void TerrainDrawPipeline::Record(
        const SubmissionToken& frame, const Terrain::RetainedTerrainSnapshot& snapshot, const Terrain::DrawSpriteTable& sprites,
        const Terrain::DrawCamera& camera, uint32_t assetCount, std::shared_ptr<const Drawing::RetainedPeepSnapshot> peeps,
        std::shared_ptr<const Gpu::PeepAssetGeneration> peepAssets)
    {
        if (_pipeline == VK_NULL_HANDLE || frame.commandBuffer == VK_NULL_HANDLE || frame.upload == nullptr
            || frame.frameIndex >= _frameCount || camera.width < 1 || camera.width > 3840 || camera.height < 1
            || camera.height > 2160 || camera.x < -1048576 || camera.x > 1048576 || camera.y < -1048576 || camera.y > 1048576
            || camera.rotation >= 4 || camera.zoom < 0 || camera.zoom > 1 || !std::isfinite(camera.entityInterpolation)
            || camera.entityInterpolation < 0 || camera.entityInterpolation > 1 || camera.transparent > 1
            || camera.stableSort > 1 || camera.clipX < 0 || camera.clipY < 0
            || camera.parentCapacity > Terrain::kDrawColumnCapacity || camera.commandCapacity > Terrain::kDrawColumnCapacity
            || static_cast<uint64_t>(camera.clipX) + camera.width > _extent.width
            || static_cast<uint64_t>(camera.clipY) + camera.height > _extent.height || camera.depthBase < 1
            || static_cast<uint32_t>(camera.depthBase) >= (1u << 22) - Terrain::kDrawColumnCapacity || sprites.revision == 0
            || sprites.records.empty() || sprites.records.size() > Terrain::kDrawSpriteCapacity || assetCount == 0
            || assetCount > _assetCapacity)
            throw std::invalid_argument("Unsupported retained terrain drawing parameters");
        const int32_t columnWidth = 32 >> camera.zoom;
        const auto columnCount = static_cast<uint32_t>(
            (camera.x + camera.width - 1 - FloorTo(camera.x, columnWidth)) / columnWidth + 1);
        if (columnCount > _columnCapacity)
            throw std::invalid_argument("Terrain draw column capacity exceeded");
        if (_epoch != snapshot.worldEpoch)
        {
            _epoch = snapshot.worldEpoch;
            DiscardPendingUploads();
        }
        _lastSpriteUploadBytes = 0;
        const bool uploadSprites = _spriteRevision != sprites.revision;
        if (uploadSprites)
            ValidateTable(sprites, assetCount);
        else if (_assetCount != assetCount || _spriteCount != sprites.records.size())
            throw std::invalid_argument("Terrain sprite generation changed without a new revision");
        if (static_cast<bool>(peeps) != static_cast<bool>(peepAssets))
            throw std::invalid_argument("Native peep state and asset ownership must arrive together");
        try
        {
            if (peeps)
                _peeps.Record(frame, peeps, peepAssets);
            _emission.Record(frame, snapshot, camera.rotation, camera.zoom, camera.transparent != 0);
            // Retire prior graphics and diagnostic reads before reusing the drawing buffers.
            Barrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            if (uploadSprites)
            {
                const VkDeviceSize bytes = sprites.records.size() * sizeof(Terrain::DrawSpriteMetadata);
                auto allocation = frame.upload->Allocate(bytes, alignof(uint32_t), Drawing::UploadCategory::world);
                if (!allocation)
                    throw std::runtime_error("Upload ring cannot hold terrain sprite metadata generation");
                std::memcpy(allocation.data, sprites.records.data(), static_cast<size_t>(bytes));
                allocation.RecordHostWrite();
                const VkBufferCopy copy{ allocation.offset, 0, bytes };
                vkCmdCopyBuffer(frame.commandBuffer, allocation.buffer, _buffers[0].GetBuffer(), 1, &copy);
                allocation.Record(Drawing::UploadMetric::bufferTransfer, bytes);
                _lastSpriteUploadBytes += bytes;
                _spriteRevision = sprites.revision;
                _assetCount = assetCount;
                _spriteCount = static_cast<uint32_t>(sprites.records.size());
            }
            Barrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            Parameters parameters{ camera.x,           camera.y,
                                   camera.width,       camera.height,
                                   camera.clipX,       camera.clipY,
                                   camera.rotation,    camera.zoom,
                                   camera.transparent, camera.stableSort,
                                   camera.depthBase,   _spriteCount,
                                   columnCount,        0,
                                   assetCount,         camera.parentCapacity | (camera.commandCapacity << 16),
                                   peeps ? 1u : 0u,    camera.entityInterpolation,
                                   camera.sourceTick };
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
            vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _layout, 0, 1, &_set, 0, nullptr);
            for (uint32_t pass = 0; pass < 3; pass++)
            {
                parameters.pass = pass;
                vkCmdPushConstants(
                    frame.commandBuffer, _layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
                vkCmdDispatch(frame.commandBuffer, pass == 2 ? 1 : columnCount, 1, 1);
                Barrier(
                    frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            }
            Barrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
            _rect.RecordSpritesIndirect(
                frame, _buffers[2], _buffers[3], columnCount, kSpritesPerColumn,
                offsetof(Terrain::DrawColumnStatus, vertexCount), sizeof(Terrain::DrawColumnStatus));
        }
        catch (...)
        {
            DiscardPendingUploads();
            throw;
        }
    }
} // namespace OpenRCT2::Ui::Vulkan
#endif
