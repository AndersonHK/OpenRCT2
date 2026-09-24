/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanWorldSurfacePipeline.h"

    #include "../gpu/GpuSelectedVehiclePaint.h"
    #include "../gpu/GpuWorldEntranceCatalog.h"
    #include "../gpu/GpuWorldFlatRideCatalog.h"
    #include "VulkanShader.h"

    #include <array>
    #include <bit>
    #include <cstdlib>
    #include <cstring>
    #include <limits>
    #include <locale>
    #include <openrct2/core/Console.hpp>
    #include <ranges>
    #include <span>
    #include <sstream>
    #include <stdexcept>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        struct WorldSurfaceConstants
        {
            Gpu::Int2 screen;
            Gpu::Int2 view;
            Gpu::Int4 clip;
            uint32_t width;
            uint32_t height;
            uint32_t recordCount;
            int32_t zoom;
            uint32_t rotation;
            uint32_t spriteSetCount;
            int32_t depthBase;
            uint32_t phase;
            uint32_t transparentWater;
            uint32_t outputCapacity;
            uint32_t sourceTick, clockMinute, clockHour;
            uint32_t viewFlags;
        };
        static_assert(sizeof(WorldSurfaceConstants) == 88);
        static_assert(offsetof(WorldSurfaceConstants, width) == 32);
        static_assert(offsetof(WorldSurfaceConstants, zoom) == 44);
        static_assert(offsetof(WorldSurfaceConstants, spriteSetCount) == 52);
        static_assert(offsetof(WorldSurfaceConstants, depthBase) == 56);
        static_assert(sizeof(VkDrawIndirectCommand) == 16);

        constexpr uint32_t kMaximumSelectedComponents = 16384;
        constexpr VkDeviceSize kSelectedVehicleBytes = (16ull + 255ull * 12ull + kMaximumSelectedComponents * 28ull)
            * sizeof(uint32_t);
        constexpr VkVertexInputBindingDescription kBinding = {
            .binding = 0,
            .stride = sizeof(Gpu::WorldSurfaceRecord),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        };
        constexpr std::array kAttributes = {
            VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SINT, offsetof(Gpu::WorldSurfaceRecord, world) },
            VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, valid) },
            VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32_SINT, offsetof(Gpu::WorldSurfaceRecord, spriteSize) },
            VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32G32_SINT, offsetof(Gpu::WorldSurfaceRecord, spriteOffset) },
            VkVertexInputAttributeDescription{ 4, 0, VK_FORMAT_R32_UINT, offsetof(Gpu::WorldSurfaceRecord, asset) },
            VkVertexInputAttributeDescription{ 5, 0, VK_FORMAT_R32_UINT, offsetof(Gpu::WorldSurfaceRecord, palettes) },
            VkVertexInputAttributeDescription{ 6, 0, VK_FORMAT_R32_UINT, offsetof(Gpu::WorldSurfaceRecord, effects) },
            VkVertexInputAttributeDescription{ 7, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, depth) },
            VkVertexInputAttributeDescription{ 8, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, zoom) },
            VkVertexInputAttributeDescription{ 9, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, coordinateShift) },
            VkVertexInputAttributeDescription{ 10, 0, VK_FORMAT_R32G32_SINT, offsetof(Gpu::WorldSurfaceRecord, reserved) },
        };
    } // namespace

    WorldSurfacePipeline::~WorldSurfacePipeline()
    {
        Dispose();
    }

    void WorldSurfacePipeline::Initialise(
        const DeviceContext& device, const IndexedResources& resources, std::filesystem::path shaderDirectory)
    {
        Dispose();
        _device = device.GetDevice();
        _frameCount = resources.GetFrameCount();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        const auto extent = resources.GetIndexedCanvas(0).GetExtent();
        _extent = { extent.width, extent.height };
        constexpr VkDeviceSize sourceBytes = Gpu::kWorldSurfaceMaximumChunkCount * Gpu::kWorldSurfaceChunkWidth
            * sizeof(Gpu::WorldSurfaceSourceRecord);
        constexpr VkDeviceSize pathBytes = Gpu::kWorldPathSourceCapacity * sizeof(Gpu::WorldPathSourceRecord);
        constexpr VkDeviceSize objectBytes = Gpu::kWorldObjectSourceCapacity * sizeof(Gpu::WorldObjectSourceRecord);
        constexpr VkDeviceSize spriteBytes = Gpu::kWorldSurfaceMaximumSpriteSetCount * sizeof(Gpu::WorldSurfaceSpriteSet);
        constexpr VkDeviceSize visibleBytes = Gpu::kWorldSurfaceOutputCapacity * sizeof(Gpu::WorldSurfaceRecord);
        constexpr VkDeviceSize indirectBytes = Gpu::kWorldSurfaceMaximumDrawCount * sizeof(VkDrawIndirectCommand);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
        if (properties.limits.maxPerStageDescriptorStorageBuffers < 17 || properties.limits.maxDescriptorSetStorageBuffers < 17)
            throw std::runtime_error("GPU world presentation requires seventeen storage-buffer descriptors");
        if (properties.limits.maxPushConstantsSize < sizeof(WorldSurfaceConstants))
            throw std::runtime_error("GPU world presentation constants exceed the device push-constant range");
        if (properties.limits.maxComputeSharedMemorySize < 1025 * sizeof(uint32_t))
            throw std::runtime_error("GPU world materialization exceeds the device compute shared memory");
        if (std::max({ sourceBytes, pathBytes, objectBytes, spriteBytes, visibleBytes })
            > properties.limits.maxStorageBufferRange)
            throw std::runtime_error("GPU terrain storage exceeds the device buffer range");
        _catalog.Initialise(
            device.GetPhysicalDevice(), _device, sizeof(Gpu::WorldSurfaceCatalog),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _prefixes.Initialise(
            device.GetPhysicalDevice(), _device, Gpu::kWorldSurfaceMaximumRecordCount * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _status.Initialise(
            device.GetPhysicalDevice(), _device, sizeof(Gpu::WorldSurfaceStatus),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        _filters.Initialise(device, resources, _status, _shaderDirectory);
        _sourceRecords.Initialise(
            device.GetPhysicalDevice(), _device, sourceBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _pathRecords.Initialise(
            device.GetPhysicalDevice(), _device, pathBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _objectRecords.Initialise(
            device.GetPhysicalDevice(), _device, objectBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _propCatalog.Initialise(
            device.GetPhysicalDevice(), _device, Gpu::kWorldPropCatalogCapacity * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _trackCatalog.Initialise(
            device.GetPhysicalDevice(), _device, Gpu::kWorldTrackCatalogCapacity * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        for (auto* catalog : { &_flatRideCatalog, &_entranceCatalog })
            catalog->Initialise(
                device.GetPhysicalDevice(), _device, Gpu::kWorldBuildingCatalogCapacity * sizeof(uint32_t),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _spriteSets.Initialise(
            device.GetPhysicalDevice(), _device, spriteBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _ridePoses.Initialise(
            device.GetPhysicalDevice(), _device, 16 + Limits::kMaxRidesInPark * sizeof(WorldRidePoseRecord),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _selection.Initialise(
            device.GetPhysicalDevice(), _device, (16 + 262144) * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _selectedVehicle.Initialise(
            device.GetPhysicalDevice(), _device, kSelectedVehicleBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _visibleRecords.Initialise(
            device.GetPhysicalDevice(), _device, visibleBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        _indirectCommands.Initialise(
            device.GetPhysicalDevice(), _device, indirectBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        CreateDescriptors(resources);
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(resources);
        InitialiseProfile(device);
    }

    void WorldSurfacePipeline::Dispose()
    {
        DisposeProfile();
        _filters.Dispose();
        _catalog.Dispose();
        _prefixes.Dispose();
        _status.Dispose();
        _indirectCommands.Dispose();
        _visibleRecords.Dispose();
        _selectedVehicle.Dispose();
        _uploadedSelectedVehicle.reset();
        _selectedVehicleInitialised = false;
        _spriteSets.Dispose();
        _sourceRecords.Dispose();
        _pathRecords.Dispose();
        _objectRecords.Dispose();
        _propCatalog.Dispose();
        _trackCatalog.Dispose();
        _flatRideCatalog.Dispose();
        _entranceCatalog.Dispose();
        _ridePoses.Dispose();
        _selection.Dispose();
        _uploadedSelection.reset();
        _selectionInitialised = false;
        _uploadedRidePoses.reset();
        _ridePosesInitialised = false;
        _objectOffsets.clear();
        _objectCapacities.clear();
        _objectArenaEnd = 0;
        _pathOffsets.clear();
        _pathCapacities.clear();
        _pathArenaEnd = 0;
        if (_device != VK_NULL_HANDLE)
        {
            for (const auto framebuffer : _framebuffers)
                vkDestroyFramebuffer(_device, framebuffer, nullptr);
            vkDestroyPipeline(_device, _computePipeline, nullptr);
            vkDestroyPipeline(_device, _pipeline, nullptr);
            vkDestroyPipeline(_device, _filterPipeline, nullptr);
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
        _computePipeline = VK_NULL_HANDLE;
        _pipeline = VK_NULL_HANDLE;
        _filterPipeline = VK_NULL_HANDLE;
        _framebuffers = {};
        _uploadedRevisions.clear();
        _uploadedSpriteRevision = 0;
        _uploadedEpoch = 0;
        _uploadedWidth = 0;
        _uploadedHeight = 0;
        _extent = {};
        _shaderDirectory.clear();
    }

    void WorldSurfacePipeline::Record(const SubmissionToken& frame, const Gpu::WorldSurfaceSceneCommand& scene)
    {
        if (frame.frameIndex >= _frameCount)
            throw std::out_of_range("Vulkan world-surface frame index is out of range");
        const uint64_t expectedRecordCount = static_cast<uint64_t>(scene.width) * scene.height;
        const size_t expectedChunkCount = (scene.recordCount + Gpu::kWorldSurfaceChunkWidth - 1) / Gpu::kWorldSurfaceChunkWidth;
        if (expectedRecordCount != scene.recordCount || scene.recordCount > Gpu::kWorldSurfaceMaximumRecordCount
            || scene.chunks.size() != expectedChunkCount || scene.sprites == nullptr || scene.outputCapacity == 0
            || scene.outputCapacity > Gpu::kWorldSurfaceOutputCapacity
            || scene.sprites->records.size() > Gpu::kWorldSurfaceMaximumSpriteSetCount
            || std::ranges::any_of(scene.chunks, [](const auto& chunk) { return chunk == nullptr; }))
            throw std::invalid_argument("Vulkan world-surface scene dimensions are inconsistent");
        if (scene.recordCount == 0)
            return;
        if (scene.ridePoses != nullptr
            && (scene.ridePoses->epoch != scene.worldEpoch || scene.ridePoses->sourceTick != scene.sourceTick
                || scene.ridePoses->records == nullptr || scene.ridePoses->records->size() > Limits::kMaxRidesInPark))
            throw std::invalid_argument("GPU ride poses do not belong to the submitted map boundary");
        if (!Gpu::GetWorldSurfaceDepthRange(scene.depthBase, scene.recordCount).has_value())
            throw std::invalid_argument("Vulkan world-surface painter depth interval is invalid");
        if (scene.selectedVehicle != nullptr)
        {
            Gpu::ValidateSelectedVehiclePaintPacket(*scene.selectedVehicle);
            const auto& words = scene.selectedVehicle->words;
            for (uint32_t car = 0; car < words[2]; car++)
                if (words[16 + car * 12 + 4] >= scene.recordCount)
                    throw std::invalid_argument("Selected vehicle car tile is outside the submitted map");
            if (scene.selectedVehicle->source->sourceTick != scene.sourceTick
                || scene.selectedVehicle->source->worldEpoch != scene.worldEpoch)
                throw std::invalid_argument("Selected vehicle packet is not the submitted scene boundary");
        }
        const auto& props = scene.sprites->propCatalog;
        const auto& tracks = scene.sprites->trackCatalog;
        if (props.size() > Gpu::kWorldPropCatalogCapacity || tracks.size() > Gpu::kWorldTrackCatalogCapacity)
            throw std::overflow_error("GPU world rule catalog capacity exceeded");
        if (scene.sprites->flatRideCatalog.size() > Gpu::kWorldBuildingCatalogCapacity
            || scene.sprites->entranceCatalog.size() > Gpu::kWorldBuildingCatalogCapacity)
            throw std::overflow_error("GPU building catalog capacity exceeded");
        // Shared tables are immutable. Validate their variable ranges on admission, not once
        // per draw while the same resident generation is reused.
        if (_uploadedSpriteRevision != scene.sprites->revision || _uploadedEpoch != scene.worldEpoch
            || _uploadedWidth != scene.width || _uploadedHeight != scene.height)
        {
            Gpu::ValidateWorldFlatRideCatalog(scene.sprites->flatRideCatalog, scene.sprites->records.size());
            if (!scene.sprites->entranceCatalog.empty())
                Gpu::ValidateWorldEntranceCatalog(
                    scene.sprites->entranceCatalog, static_cast<uint32_t>(scene.sprites->records.size()));
        }
        if (!props.empty())
        {
            if (props.size() < Gpu::kWorldPropCatalogHeaderWords)
                throw std::invalid_argument("GPU prop catalog header is incomplete");
            for (size_t family = 0; family < 4; family++)
                if (props[family] < Gpu::kWorldPropCatalogHeaderWords
                    || uint64_t(props[family]) + uint64_t(props[family + 4]) * Gpu::kWorldPropMaterialWords > props.size())
                    throw std::invalid_argument("GPU prop catalog family range is invalid");
        }
        if (!tracks.empty()
            && (tracks.size() < 12 || tracks[0] != 0x5754524b || (tracks[1] & 255u) != 1 || tracks[11] != tracks.size()))
            throw std::invalid_argument("GPU track catalog header is invalid");
        if (_uploadedEpoch != scene.worldEpoch || _uploadedWidth != scene.width || _uploadedHeight != scene.height
            || _uploadedRevisions.size() != scene.chunks.size())
        {
            _uploadedEpoch = scene.worldEpoch;
            _uploadedWidth = scene.width;
            _uploadedHeight = scene.height;
            _uploadedRevisions.assign(scene.chunks.size(), 0);
            _pathOffsets.assign(scene.chunks.size(), 0);
            _pathCapacities.assign(scene.chunks.size(), 0);
            _objectOffsets.assign(scene.chunks.size(), 0);
            _objectCapacities.assign(scene.chunks.size(), 0);
            _objectArenaEnd = 0;
            _pathArenaEnd = 0;
            _uploadedSpriteRevision = 0;
        }

        constexpr VkDeviceSize chunkBytes = Gpu::kWorldSurfaceChunkWidth * sizeof(Gpu::WorldSurfaceSourceRecord);
        std::vector<size_t> dirtyChunks;
        for (size_t i = 0; i < scene.chunks.size(); i++)
            if (_uploadedRevisions[i] != scene.chunks[i]->revision)
                dirtyChunks.push_back(i);
        // An absent table is legal only while no instance can reference it. Otherwise a reused
        // device buffer could expose an earlier generation's metadata after a malformed submission.
        const auto hasFlatRide = [&](const Gpu::WorldObjectSourceRecord& object) {
            const auto& flat = scene.sprites->flatRideCatalog;
            const auto ride = object.rideIdAndMazeEntry & 65535u;
            if (flat.empty() || ride >= flat[3])
                return false;
            const auto entry = flat[2] + ride * Gpu::kWorldFlatRideWords;
            const auto type = object.trackTypeAndRideType & 65535u;
            return flat[entry] != 0 && (type == flat[entry + 2] || type == flat[entry + 3]);
        };
        if (props.empty() || tracks.empty() || scene.sprites->entranceCatalog.empty())
            for (size_t i = 0; i < scene.chunks.size(); i++)
                if (_uploadedRevisions[i] != scene.chunks[i]->revision || _uploadedSpriteRevision != scene.sprites->revision)
                    for (const auto& object : scene.chunks[i]->objects)
                        if ((object.kind < 4 && props.empty()) || (object.kind == 4 && tracks.empty() && !hasFlatRide(object))
                            || (object.kind == 5 && scene.sprites->entranceCatalog.empty()))
                            throw std::invalid_argument("GPU world instance has no matching immutable catalog");
        // Validate every changed range before recording transfers. Admission never truncates a tile.
        for (const auto i : dirtyChunks)
        {
            const auto& chunk = *scene.chunks[i];
            for (const auto& record : chunk.records)
            {
                if (record.pathCount > Gpu::kWorldPathMaximumTileWork || record.objectCount > Gpu::kWorldObjectMaximumTileWork)
                    throw std::overflow_error("GPU world tile dispatch budget exceeded");
                if (record.pathFirst > chunk.paths.size() || record.pathCount > chunk.paths.size() - record.pathFirst
                    || record.objectFirst > chunk.objects.size()
                    || record.objectCount > chunk.objects.size() - record.objectFirst)
                    throw std::invalid_argument("GPU world tile range exceeds its owned chunk");
            }
        }
        // Both immutable families share the same bounded allocation policy; fragmentation alone triggers repacking.
        const auto reserveArena = [&](auto member, uint32_t limit, auto& offsets, auto& capacities, uint32_t& end) {
            bool repack = false;
            for (const auto i : dirtyChunks)
            {
                const auto count = (scene.chunks[i].get()->*member).size();
                if (count > limit)
                    throw std::overflow_error("GPU world source capacity exceeded");
                if (count <= capacities[i])
                    continue;
                const auto capacity = std::min<uint64_t>(limit, std::bit_ceil(uint64_t(count)));
                if (capacity > limit - end)
                {
                    repack = true;
                    break;
                }
                offsets[i] = end;
                capacities[i] = static_cast<uint32_t>(capacity);
                end += static_cast<uint32_t>(capacity);
            }
            if (!repack)
                return false;
            uint64_t count = 0;
            for (const auto& chunk : scene.chunks)
                count += (chunk.get()->*member).size();
            if (count > limit)
                throw std::overflow_error("GPU world source capacity exceeded");
            end = 0;
            for (size_t i = 0; i < scene.chunks.size(); i++)
            {
                offsets[i] = end;
                capacities[i] = static_cast<uint32_t>((scene.chunks[i].get()->*member).size());
                end += capacities[i];
            }
            return true;
        };
        const bool pathsRepacked = reserveArena(
            &Gpu::WorldSurfaceChunk::paths, Gpu::kWorldPathSourceCapacity, _pathOffsets, _pathCapacities, _pathArenaEnd);
        const bool objectsRepacked = reserveArena(
            &Gpu::WorldSurfaceChunk::objects, Gpu::kWorldObjectSourceCapacity, _objectOffsets, _objectCapacities,
            _objectArenaEnd);
        if (pathsRepacked || objectsRepacked)
        {
            dirtyChunks.clear();
            for (size_t i = 0; i < scene.chunks.size(); i++)
                dirtyChunks.push_back(i);
        }
        const bool hasChangedChunks = !dirtyChunks.empty();
        const bool spritesChanged = _uploadedSpriteRevision != scene.sprites->revision;
        if (hasChangedChunks || spritesChanged)
        {
            std::array<VkBufferMemoryBarrier, 9> barriers{};
            uint32_t barrierCount = 0;
            const auto append = [&barriers, &barrierCount](const VkBuffer buffer) {
                barriers[barrierCount++] = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = buffer,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE,
                };
            };
            if (hasChangedChunks)
            {
                append(_sourceRecords.GetBuffer());
                append(_pathRecords.GetBuffer());
                append(_objectRecords.GetBuffer());
            }
            if (spritesChanged)
            {
                append(_spriteSets.GetBuffer());
                append(_catalog.GetBuffer());
                append(_propCatalog.GetBuffer());
                append(_trackCatalog.GetBuffer());
                append(_flatRideCatalog.GetBuffer());
                append(_entranceCatalog.GetBuffer());
            }
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                barrierCount, barriers.data(), 0, nullptr);
        }

        if (hasChangedChunks)
        {
            VkDeviceSize pathBytes = 0, objectBytes = 0;
            for (const auto i : dirtyChunks)
            {
                pathBytes += scene.chunks[i]->paths.size() * sizeof(Gpu::WorldPathSourceRecord);
                objectBytes += scene.chunks[i]->objects.size() * sizeof(Gpu::WorldObjectSourceRecord);
            }
            const auto sourceBytes = chunkBytes * dirtyChunks.size();
            const auto totalBytes = sourceBytes + pathBytes + objectBytes;
            auto allocation = frame.upload->Allocate(totalBytes, alignof(uint32_t), Drawing::UploadCategory::world);
            if (!allocation)
                throw std::runtime_error("Vulkan upload ring has no room for terrain/path deltas");
            std::vector<VkBufferCopy> regions, pathRegions, objectRegions;
            regions.reserve(dirtyChunks.size());
            pathRegions.reserve(dirtyChunks.size());
            objectRegions.reserve(dirtyChunks.size());
            VkDeviceSize pathCursor = sourceBytes;
            VkDeviceSize objectCursor = sourceBytes + pathBytes;
            for (size_t i = 0; i < dirtyChunks.size(); i++)
            {
                const auto index = dirtyChunks[i];
                const auto& chunk = *scene.chunks[index];
                auto records = chunk.records;
                for (auto& record : records)
                {
                    if (record.pathFirst > chunk.paths.size() || record.pathCount > chunk.paths.size() - record.pathFirst)
                        throw std::invalid_argument("GPU world path tile range exceeds its owned chunk");
                    record.pathFirst += _pathOffsets[index];
                    record.objectFirst += _objectOffsets[index];
                }
                std::memcpy(allocation.data + i * chunkBytes, records.data(), chunkBytes);
                regions.push_back({ allocation.offset + i * chunkBytes, index * chunkBytes, chunkBytes });
                const auto bytes = chunk.paths.size() * sizeof(Gpu::WorldPathSourceRecord);
                if (bytes != 0)
                {
                    std::memcpy(allocation.data + pathCursor, chunk.paths.data(), bytes);
                    pathRegions.push_back(
                        { allocation.offset + pathCursor, _pathOffsets[index] * sizeof(Gpu::WorldPathSourceRecord), bytes });
                    pathCursor += bytes;
                }
                const auto objectSize = chunk.objects.size() * sizeof(Gpu::WorldObjectSourceRecord);
                if (objectSize != 0)
                {
                    std::memcpy(allocation.data + objectCursor, chunk.objects.data(), objectSize);
                    objectRegions.push_back({ allocation.offset + objectCursor,
                                              _objectOffsets[index] * sizeof(Gpu::WorldObjectSourceRecord), objectSize });
                    objectCursor += objectSize;
                }
                _uploadedRevisions[index] = chunk.revision;
            }
            allocation.RecordHostWrite();
            vkCmdCopyBuffer(
                frame.commandBuffer, allocation.buffer, _sourceRecords.GetBuffer(), static_cast<uint32_t>(regions.size()),
                regions.data());
            if (!pathRegions.empty())
                vkCmdCopyBuffer(
                    frame.commandBuffer, allocation.buffer, _pathRecords.GetBuffer(), static_cast<uint32_t>(pathRegions.size()),
                    pathRegions.data());
            if (!objectRegions.empty())
                vkCmdCopyBuffer(
                    frame.commandBuffer, allocation.buffer, _objectRecords.GetBuffer(),
                    static_cast<uint32_t>(objectRegions.size()), objectRegions.data());
            if (frame.telemetry)
                frame.telemetry->Add(frame.telemetry->worldBufferCopyCalls, 1 + !pathRegions.empty() + !objectRegions.empty());
            allocation.Record(Drawing::UploadMetric::bufferTransfer, totalBytes);
        }
        if (spritesChanged)
        {
            const VkDeviceSize spriteBytes = scene.sprites->records.size() * sizeof(Gpu::WorldSurfaceSpriteSet);
            if (spriteBytes != 0)
            {
                auto allocation = frame.upload->Allocate(spriteBytes, alignof(uint32_t), Drawing::UploadCategory::world);
                if (!allocation)
                    throw std::runtime_error("Vulkan upload ring has no room for world-surface sprite sets");
                std::memcpy(allocation.data, scene.sprites->records.data(), static_cast<size_t>(spriteBytes));
                allocation.RecordHostWrite();
                const VkBufferCopy copy = { .srcOffset = allocation.offset, .dstOffset = 0, .size = spriteBytes };
                vkCmdCopyBuffer(frame.commandBuffer, allocation.buffer, _spriteSets.GetBuffer(), 1, &copy);
                if (frame.telemetry)
                    frame.telemetry->Add(frame.telemetry->worldBufferCopyCalls, 1);
                allocation.Record(Drawing::UploadMetric::bufferTransfer, copy.size);
            }
            auto catalogUpload = frame.upload->Allocate(
                sizeof(Gpu::WorldSurfaceCatalog), alignof(uint32_t), Drawing::UploadCategory::world);
            if (!catalogUpload)
                throw std::runtime_error("Vulkan upload ring has no room for terrain materials");
            std::memcpy(catalogUpload.data, &scene.sprites->catalog, sizeof(Gpu::WorldSurfaceCatalog));
            catalogUpload.RecordHostWrite();
            const VkBufferCopy catalogCopy{ catalogUpload.offset, 0, sizeof(Gpu::WorldSurfaceCatalog) };
            vkCmdCopyBuffer(frame.commandBuffer, catalogUpload.buffer, _catalog.GetBuffer(), 1, &catalogCopy);
            if (frame.telemetry)
                frame.telemetry->Add(frame.telemetry->worldBufferCopyCalls, 1);
            catalogUpload.Record(Drawing::UploadMetric::bufferTransfer, catalogCopy.size);
            const auto uploadWords = [&](const std::vector<uint32_t>& words, const Buffer& target, bool clearAbsent = false) {
                if (words.empty() && !clearAbsent)
                    return;
                // Clear absent headers too: a later generation must never consult a previous
                // scene's shared catalog merely because this generation has no such family.
                constexpr std::array<uint32_t, 16> emptyHeader{};
                const auto source = words.empty() ? std::span<const uint32_t>(emptyHeader) : std::span<const uint32_t>(words);
                const VkDeviceSize bytes = source.size_bytes();
                if (bytes > target.GetSize())
                    throw std::overflow_error("GPU world rule catalog capacity exceeded");
                auto upload = frame.upload->Allocate(bytes, alignof(uint32_t), Drawing::UploadCategory::world);
                if (!upload)
                    throw std::runtime_error("Vulkan upload ring has no room for world rules");
                std::memcpy(upload.data, source.data(), static_cast<size_t>(bytes));
                upload.RecordHostWrite();
                const VkBufferCopy copy{ upload.offset, 0, bytes };
                vkCmdCopyBuffer(frame.commandBuffer, upload.buffer, target.GetBuffer(), 1, &copy);
                upload.Record(Drawing::UploadMetric::bufferTransfer, bytes);
                if (frame.telemetry)
                    frame.telemetry->Add(frame.telemetry->worldBufferCopyCalls, 1);
            };
            uploadWords(scene.sprites->propCatalog, _propCatalog);
            uploadWords(scene.sprites->trackCatalog, _trackCatalog);
            uploadWords(scene.sprites->flatRideCatalog, _flatRideCatalog, true);
            uploadWords(scene.sprites->entranceCatalog, _entranceCatalog, true);
            _uploadedSpriteRevision = scene.sprites->revision;
        }
        if (hasChangedChunks || spritesChanged)
        {
            std::array<VkBufferMemoryBarrier, 9> barriers{};
            uint32_t barrierCount = 0;
            const auto append = [&barriers, &barrierCount](const VkBuffer buffer) {
                barriers[barrierCount++] = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = buffer,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE,
                };
            };
            if (hasChangedChunks)
            {
                append(_sourceRecords.GetBuffer());
                append(_pathRecords.GetBuffer());
                append(_objectRecords.GetBuffer());
            }
            if (spritesChanged)
            {
                append(_spriteSets.GetBuffer());
                append(_catalog.GetBuffer());
                append(_propCatalog.GetBuffer());
                append(_trackCatalog.GetBuffer());
                append(_flatRideCatalog.GetBuffer());
                append(_entranceCatalog.GetBuffer());
            }
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                barrierCount, barriers.data(), 0, nullptr);
        }

        const auto poseRecords = scene.ridePoses != nullptr ? scene.ridePoses->records : nullptr;
        const auto oldPoseRecords = _uploadedRidePoses != nullptr ? _uploadedRidePoses->records : nullptr;
        if (!_ridePosesInitialised || poseRecords != oldPoseRecords)
        {
            const bool reset = !_ridePosesInitialised || _uploadedRidePoses == nullptr || scene.ridePoses == nullptr
                || _uploadedRidePoses->epoch != scene.ridePoses->epoch
                || _uploadedRidePoses->entityEpoch != scene.ridePoses->entityEpoch;
            const size_t poseCount = poseRecords != nullptr ? poseRecords->size() : 0;
            std::vector<std::pair<size_t, size_t>> ranges;
            size_t changedCount = 0;
            for (size_t i = 0; i < poseCount;)
            {
                const auto changed = [&](size_t index) {
                    return reset || oldPoseRecords == nullptr || index >= oldPoseRecords->size()
                        || (*poseRecords)[index] != (*oldPoseRecords)[index];
                };
                if (!changed(i))
                {
                    ++i;
                    continue;
                }
                const auto first = i++;
                while (i < poseCount && changed(i))
                    ++i;
                ranges.emplace_back(first, i - first);
                changedCount += i - first;
            }
            // SourceTick already travels in the scene constants. The resident header is purely
            // structural, so a paused/unchanged mechanism incurs no hot-buffer upload.
            const std::array<uint32_t, 4> header{ static_cast<uint32_t>(poseCount), 20, 0, 0 };
            const auto bytes = sizeof(header) + changedCount * sizeof(WorldRidePoseRecord);
            auto upload = frame.upload->Allocate(bytes, alignof(uint32_t), Drawing::UploadCategory::world);
            if (!upload)
                throw std::runtime_error("Vulkan upload ring has no room for ride pose deltas");
            std::memcpy(upload.data, header.data(), sizeof(header));
            std::vector<VkBufferCopy> copies{ { upload.offset, 0, sizeof(header) } };
            copies.reserve(ranges.size() + 1);
            VkDeviceSize cursor = sizeof(header);
            for (const auto& [first, count] : ranges)
            {
                const VkDeviceSize rangeBytes = count * sizeof(WorldRidePoseRecord);
                std::memcpy(upload.data + cursor, poseRecords->data() + first, static_cast<size_t>(rangeBytes));
                copies.push_back({ upload.offset + cursor, sizeof(header) + first * sizeof(WorldRidePoseRecord), rangeBytes });
                cursor += rangeBytes;
            }
            upload.RecordHostWrite();
            VkBufferMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _ridePoses.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            vkCmdCopyBuffer(
                frame.commandBuffer, upload.buffer, _ridePoses.GetBuffer(), static_cast<uint32_t>(copies.size()),
                copies.data());
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            upload.Record(Drawing::UploadMetric::bufferTransfer, bytes);
            if (frame.telemetry)
                frame.telemetry->Add(frame.telemetry->worldBufferCopyCalls, 1);
            _ridePosesInitialised = true;
        }
        _uploadedRidePoses = scene.ridePoses;
        if (!_selectionInitialised || _uploadedSelection != scene.selection)
        {
            constexpr std::array<uint32_t, 16> emptySelection{};
            const auto source = scene.selection != nullptr ? std::span<const uint32_t>(*scene.selection)
                                                           : std::span<const uint32_t>(emptySelection);
            if (source.size() < emptySelection.size() || source.size_bytes() > _selection.GetSize())
                throw std::invalid_argument("GPU selection state exceeds its bounded buffer");
            auto upload = frame.upload->Allocate(source.size_bytes(), alignof(uint32_t), Drawing::UploadCategory::world);
            if (!upload)
                throw std::runtime_error("Vulkan upload ring has no room for selection state");
            std::memcpy(upload.data, source.data(), source.size_bytes());
            upload.RecordHostWrite();
            VkBufferMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _selection.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            const VkBufferCopy copy{ upload.offset, 0, source.size_bytes() };
            vkCmdCopyBuffer(frame.commandBuffer, upload.buffer, _selection.GetBuffer(), 1, &copy);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            upload.Record(Drawing::UploadMetric::bufferTransfer, source.size_bytes());
            if (frame.telemetry)
                frame.telemetry->Add(frame.telemetry->worldBufferCopyCalls, 1);
            _uploadedSelection = scene.selection;
            _selectionInitialised = true;
        }

        if (!_selectedVehicleInitialised || _uploadedSelectedVehicle != scene.selectedVehicle)
        {
            constexpr std::array<uint32_t, 16> emptySelection{};
            const auto source = scene.selectedVehicle != nullptr ? std::span<const uint32_t>(scene.selectedVehicle->words)
                                                                 : std::span<const uint32_t>(emptySelection);
            if (source.size() < emptySelection.size() || source.size_bytes() > _selectedVehicle.GetSize())
                throw std::invalid_argument("GPU selected-vehicle recipes exceeds its bounded buffer");
            auto upload = frame.upload->Allocate(source.size_bytes(), alignof(uint32_t), Drawing::UploadCategory::world);
            if (!upload)
                throw std::runtime_error("Vulkan upload ring has no room for selected-vehicle recipes");
            std::memcpy(upload.data, source.data(), source.size_bytes());
            upload.RecordHostWrite();
            VkBufferMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _selectedVehicle.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            const VkBufferCopy copy{ upload.offset, 0, source.size_bytes() };
            vkCmdCopyBuffer(frame.commandBuffer, upload.buffer, _selectedVehicle.GetBuffer(), 1, &copy);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            upload.Record(Drawing::UploadMetric::bufferTransfer, source.size_bytes());
            if (frame.telemetry)
                frame.telemetry->Add(frame.telemetry->worldBufferCopyCalls, 1);
            _uploadedSelectedVehicle = scene.selectedVehicle;
            _selectedVehicleInitialised = true;
        }

        const std::array outputToCompute = {
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _visibleRecords.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _indirectCommands.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, static_cast<uint32_t>(outputToCompute.size()),
            outputToCompute.data(), 0, nullptr);

        WorldSurfaceConstants constants{
            .screen = { static_cast<int32_t>(_extent.width), static_cast<int32_t>(_extent.height) },
            .view = scene.view,
            .clip = scene.clip,
            .width = scene.width,
            .height = scene.height,
            .recordCount = scene.recordCount,
            .zoom = scene.zoom,
            .rotation = static_cast<uint32_t>(scene.rotation),
            .spriteSetCount = static_cast<uint32_t>(scene.sprites->records.size()),
            .depthBase = scene.depthBase,
            .phase = 0,
            .transparentWater = scene.transparentWater,
            .outputCapacity = scene.outputCapacity,
            .sourceTick = scene.sourceTick,
            .clockMinute = scene.clockMinute,
            .clockHour = scene.clockHour,
            .viewFlags = scene.viewFlags,
        };
        const uint32_t drawCount = Gpu::GetWorldSurfaceDrawCount(scene.recordCount);
        if (scene.zoom < -2 || scene.zoom > 3)
            throw std::invalid_argument("GPU world column zoom is outside the resident variant range");
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _computePipeline);
        vkCmdBindDescriptorSets(
            frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
        // Retire earlier compute reads/writes and asynchronous status transfer before reusing the persistent arena.
        const VkMemoryBarrier previousCompute{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                                   | VK_ACCESS_TRANSFER_READ_BIT,
                                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &previousCompute, 0, nullptr, 0, nullptr);
        if (_profileQueries != VK_NULL_HANDLE)
        {
            if (_profilePending.at(frame.frameIndex))
                throw std::logic_error("World GPU profile slot reused before fence retirement");
            vkCmdResetQueryPool(
                frame.commandBuffer, _profileQueries, frame.frameIndex * kProfilePointCount, kProfilePointCount);
            _profilePending[frame.frameIndex] = true;
            ProfilePoint(frame, 0);
        }
        for (uint32_t phase = 0; phase < 3; phase++)
        {
            constants.phase = phase;
            vkCmdPushConstants(
                frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0,
                sizeof(constants), &constants);
            vkCmdDispatch(frame.commandBuffer, phase == 1 ? 1 : drawCount, 1, 1);
            const VkMemoryBarrier between{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between,
                0, nullptr, 0, nullptr);
        }

        ProfilePoint(frame, 1);

        const std::array computeToDraw = {
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _visibleRecords.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _indirectCommands.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr,
            static_cast<uint32_t>(computeToDraw.size()), computeToDraw.data(), 0, nullptr);

        _filters.Begin(frame.commandBuffer);
        const VkRenderPassBeginInfo pass{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                          nullptr,
                                          _renderPass,
                                          _framebuffers[frame.frameIndex],
                                          { { 0, 0 }, _extent } };
        const VkViewport viewport{
            0.0f, 0.0f, static_cast<float>(_extent.width), static_cast<float>(_extent.height), 0.0f, 1.0f
        };
        const VkRect2D scissor{ { 0, 0 }, _extent };
        for (uint32_t phase = 3; phase <= 4; phase++)
        {
            if (phase == 4)
            {
                const VkMemoryBarrier depth{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                                 | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
                vkCmdPipelineBarrier(
                    frame.commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 1, &depth, 0,
                    nullptr, 0, nullptr);
            }
            constants.phase = phase;
            vkCmdPushConstants(
                frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0,
                sizeof(constants), &constants);
            vkCmdBeginRenderPass(frame.commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, phase == 3 ? _pipeline : _filterPipeline);
            vkCmdBindDescriptorSets(
                frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
            const auto filterSet = _filters.GetSet(frame.frameIndex);
            vkCmdBindDescriptorSets(
                frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 1, 1, &filterSet, 0, nullptr);
            const VkDeviceSize offset = 0;
            const auto buffer = _visibleRecords.GetBuffer();
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &buffer, &offset);
            vkCmdDrawIndirect(frame.commandBuffer, _indirectCommands.GetBuffer(), 0, drawCount, sizeof(VkDrawIndirectCommand));
            vkCmdEndRenderPass(frame.commandBuffer);
        }
        _filters.Resolve(frame.commandBuffer, frame.frameIndex);
        ProfilePoint(frame, 2);
    }

    void WorldSurfacePipeline::InitialiseProfile(const DeviceContext& device)
    {
        const auto* requested = std::getenv("OPENRCT2_VULKAN_PROFILE_WORLD");
        _profileRequested = requested != nullptr && std::string_view(requested) == "1";
        if (!_profileRequested)
            return;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device.GetPhysicalDevice(), &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device.GetPhysicalDevice(), &count, families.data());
        const auto family = device.GetQueueFamilies().graphics;
        if (!family || *family >= count)
            return;
        _profileValidBits = families[*family].timestampValidBits;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
        _profilePeriodNs = properties.limits.timestampPeriod;
        if (!properties.limits.timestampComputeAndGraphics || _profileValidBits == 0 || _profileValidBits > 64
            || _profilePeriodNs <= 0)
            return;
        const VkQueryPoolCreateInfo query{ .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                           .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                           .queryCount = _frameCount * kProfilePointCount };
        CheckVk(vkCreateQueryPool(_device, &query, nullptr, &_profileQueries), "world stage timestamp pool");
    }

    void WorldSurfacePipeline::ProfilePoint(const SubmissionToken& frame, uint32_t point) const
    {
        if (_profileQueries != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(
                frame.commandBuffer, point == 2 ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                _profileQueries, frame.frameIndex * kProfilePointCount + point);
    }

    void WorldSurfacePipeline::CompleteProfile(uint32_t frameIndex)
    {
        if (_profileQueries == VK_NULL_HANDLE || frameIndex >= _profilePending.size() || !_profilePending[frameIndex])
            return;
        // The caller already retired this slot. Query availability without WAIT.
        std::array<std::array<uint64_t, 2>, kProfilePointCount> values{};
        const auto result = vkGetQueryPoolResults(
            _device, _profileQueries, frameIndex * kProfilePointCount, kProfilePointCount, sizeof(values), values.data(),
            sizeof(values[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        _profilePending[frameIndex] = false;
        if (result != VK_SUCCESS && result != VK_NOT_READY)
            CheckVk(result, "completed world stage timestamp results");
        if (result == VK_NOT_READY || std::ranges::any_of(values, [](const auto& value) { return value[1] == 0; }))
        {
            ++_profileUnavailable;
            return;
        }
        const uint64_t mask = _profileValidBits == 64 ? std::numeric_limits<uint64_t>::max()
                                                      : (uint64_t{ 1 } << _profileValidBits) - 1;
        for (size_t i = 0; i < _profileTotalUs.size(); i++)
        {
            const auto ticks = (values[i + 1][0] - values[i][0]) & mask;
            const double microseconds = static_cast<double>(ticks) * _profilePeriodNs / 1000.0;
            _profileTotalUs[i] += microseconds;
            _profileMaxUs[i] = std::max(_profileMaxUs[i], microseconds);
        }
        ++_profileSamples;
    }

    void WorldSurfacePipeline::DisposeProfile()
    {
        if (_profileRequested)
        {
            // One summary per pipeline lifetime. Outstanding slots are reported,
            // never waited on or mistaken for completed samples during teardown.
            constexpr std::array names{ "materialize", "raster" };
            const auto pending = std::count(_profilePending.begin(), _profilePending.end(), true);
            std::ostringstream json;
            json.imbue(std::locale::classic());
            json << "{\"schema\":2,\"supported\":" << (_profileQueries != VK_NULL_HANDLE ? "true" : "false")
                 << ",\"timestampValidBits\":" << _profileValidBits << ",\"timestampPeriodNs\":" << _profilePeriodNs
                 << ",\"samples\":" << _profileSamples << ",\"unavailable\":" << _profileUnavailable
                 << ",\"discarded\":" << _profileDiscarded << ",\"pending\":" << pending << ",\"stages\":{";
            for (size_t i = 0; i < names.size(); i++)
            {
                if (i != 0)
                    json << ',';
                json << '"' << names[i] << "\":{\"meanUs\":"
                     << (_profileSamples != 0 ? _profileTotalUs[i] / static_cast<double>(_profileSamples) : 0.0)
                     << ",\"maxUs\":" << _profileMaxUs[i] << '}';
            }
            json << "}}";
            Console::WriteLine("VULKAN_WORLD_PROFILE %s", json.str().c_str());
        }
        if (_device != VK_NULL_HANDLE && _profileQueries != VK_NULL_HANDLE)
            vkDestroyQueryPool(_device, _profileQueries, nullptr);
        _profileQueries = VK_NULL_HANDLE;
        _profileRequested = false;
        _profileValidBits = 0;
        _profilePeriodNs = 0;
        _profilePending = {};
        _profileSamples = _profileUnavailable = _profileDiscarded = 0;
        _profileTotalUs = _profileMaxUs = {};
    }

    void WorldSurfacePipeline::DiscardPendingUploads(uint32_t frameIndex) noexcept
    {
        if (frameIndex < _profilePending.size())
        {
            _profileDiscarded += _profilePending[frameIndex];
            _profilePending[frameIndex] = false;
        }
        else
        {
            for (auto& pending : _profilePending)
            {
                _profileDiscarded += pending;
                pending = false;
            }
        }
        // Revisions advance while transfer commands are recorded. An abandoned command buffer never executes those copies,
        // so the next accepted frame must republish every source buffer rather than trusting the recorded revisions.
        std::ranges::fill(_uploadedRevisions, 0);
        _uploadedSpriteRevision = 0;
        _uploadedRidePoses.reset();
        _ridePosesInitialised = false;
        _uploadedSelection.reset();
        _selectionInitialised = false;
        _uploadedSelectedVehicle.reset();
        _selectedVehicleInitialised = false;
    }

    void WorldSurfacePipeline::CreateDescriptors(const IndexedResources& resources)
    {
        constexpr std::array bindings = {
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
            VkDescriptorSetLayoutBinding{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
            VkDescriptorSetLayoutBinding{ 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 22, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        };
        const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        CheckVk(vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout), "world surfaces layout");
        constexpr std::array poolSizes = {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 17 },
        };
        const VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };
        CheckVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool), "world surfaces pool");
        const VkDescriptorSetAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = _descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &_descriptorSetLayout,
        };
        CheckVk(vkAllocateDescriptorSets(_device, &allocation, &_descriptorSet), "world surfaces descriptors");
        const VkDescriptorImageInfo atlas{ resources.GetNearestSampler(), resources.GetSpriteAtlas().GetView(),
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkDescriptorImageInfo remap{ resources.GetNearestSampler(), resources.GetRemapPalette().GetView(),
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkDescriptorBufferInfo sprites{ resources.GetSpriteDescriptors().GetBuffer(), 0,
                                              resources.GetSpriteDescriptors().GetSize() };
        const VkDescriptorBufferInfo objects{ _objectRecords.GetBuffer(), 0, _objectRecords.GetSize() };
        const VkDescriptorBufferInfo props{ _propCatalog.GetBuffer(), 0, _propCatalog.GetSize() };
        const VkDescriptorBufferInfo tracks{ _trackCatalog.GetBuffer(), 0, _trackCatalog.GetSize() };
        const VkDescriptorBufferInfo flatRides{ _flatRideCatalog.GetBuffer(), 0, _flatRideCatalog.GetSize() };
        const VkDescriptorBufferInfo entrances{ _entranceCatalog.GetBuffer(), 0, _entranceCatalog.GetSize() };
        const VkDescriptorBufferInfo poses{ _ridePoses.GetBuffer(), 0, _ridePoses.GetSize() };
        const VkDescriptorBufferInfo selection{ _selection.GetBuffer(), 0, _selection.GetSize() };
        const VkDescriptorBufferInfo paths{ _pathRecords.GetBuffer(), 0, _pathRecords.GetSize() };
        const VkDescriptorBufferInfo sources{ _sourceRecords.GetBuffer(), 0, _sourceRecords.GetSize() };
        const VkDescriptorBufferInfo spriteSets{ _spriteSets.GetBuffer(), 0, _spriteSets.GetSize() };
        const VkDescriptorBufferInfo outputs{ _visibleRecords.GetBuffer(), 0, _visibleRecords.GetSize() };
        const VkDescriptorBufferInfo selectedVehicle{ _selectedVehicle.GetBuffer(), 0, _selectedVehicle.GetSize() };
        const VkDescriptorBufferInfo commands{ _indirectCommands.GetBuffer(), 0, _indirectCommands.GetSize() };
        const VkDescriptorBufferInfo catalog{ _catalog.GetBuffer(), 0, _catalog.GetSize() };
        const VkDescriptorBufferInfo prefixes{ _prefixes.GetBuffer(), 0, _prefixes.GetSize() };
        const VkDescriptorBufferInfo status{ _status.GetBuffer(), 0, _status.GetSize() };
        const std::array writes = {
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 0, 0, 1,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &atlas },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 1, 0, 1,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &remap },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 2, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &sprites },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 3, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &sources },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 4, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &spriteSets },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 5, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputs },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 6, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &commands },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 7, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &catalog },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 8, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &prefixes },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 9, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &status },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 11, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &paths },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 12, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &objects },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 13, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &props },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 14, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &tracks },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 15, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &flatRides },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 16, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &entrances },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 17, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &poses },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 18, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &selection },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 22, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &selectedVehicle },
        };
        vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void WorldSurfacePipeline::CreateRenderPass()
    {
        const std::array attachments = {
            VkAttachmentDescription{ 0, VK_FORMAT_R8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
                                     VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                     VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            VkAttachmentDescription{ 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
                                     VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                     VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
        };
        constexpr VkAttachmentReference colour{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        constexpr VkAttachmentReference depth{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        const VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colour,
            .pDepthStencilAttachment = &depth,
        };
        constexpr std::array dependencies = {
            VkSubpassDependency{ VK_SUBPASS_EXTERNAL, 0,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                     | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                     | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                     | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT },
            VkSubpassDependency{ 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT },
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
        CheckVk(vkCreateRenderPass(_device, &info, nullptr, &_renderPass), "world surfaces render pass");
    }

    void WorldSurfacePipeline::CreatePipeline()
    {
        const std::array layouts{ _descriptorSetLayout, _filters.GetLayout() };
        const VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(WorldSurfaceConstants) };
        const VkPipelineLayoutCreateInfo layout = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts = layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push,
        };
        CheckVk(vkCreatePipelineLayout(_device, &layout, nullptr, &_pipelineLayout), "world surfaces pipeline layout");
        const auto computeShader = LoadShaderModule(_device, _shaderDirectory / "world_surface_compact.comp.spv");
        const VkPipelineShaderStageCreateInfo computeStage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = computeShader,
            .pName = "main",
        };
        VkPipelineCreateFlags computeFlags = 0;
        // Explicit diagnostic A/B only; ordinary application builds keep driver optimization enabled.
        const auto* quickCompile = std::getenv("OPENRCT2_VULKAN_QUICK_COMPILE");
        if (quickCompile != nullptr && std::string_view(quickCompile) == "1")
        {
            computeFlags = VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
            Console::WriteLine("Vulkan diagnostic: native world pipeline optimization disabled");
        }
        const VkComputePipelineCreateInfo computeInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .flags = computeFlags,
            .stage = computeStage,
            .layout = _pipelineLayout,
        };
        const auto computeResult = vkCreateComputePipelines(
            _device, _pipelineCache, 1, &computeInfo, nullptr, &_computePipeline);
        vkDestroyShaderModule(_device, computeShader, nullptr);
        CheckVk(computeResult, "world surfaces compute pipeline");
        constexpr VkPipelineDepthStencilStateCreateInfo depth = {
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
                .vertexShader = _shaderDirectory / "world_surface.vert.spv",
                .fragmentShader = _shaderDirectory / "world_surface.frag.spv",
                .vertexBindings = std::span{ &kBinding, 1 },
                .vertexAttributes = kAttributes,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                .depthStencil = &depth,
                .layout = _pipelineLayout,
                .renderPass = _renderPass,
            },
            "world surfaces graphics pipeline");
        auto filterDepth = depth;
        filterDepth.depthWriteEnable = VK_FALSE;
        _filterPipeline = CreateGraphicsPipeline(
            _device, _pipelineCache,
            { .vertexShader = _shaderDirectory / "world_surface.vert.spv",
              .fragmentShader = _shaderDirectory / "world_filter_collect.frag.spv",
              .vertexBindings = std::span{ &kBinding, 1 },
              .vertexAttributes = kAttributes,
              .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
              .depthStencil = &filterDepth,
              .colorWriteMask = 0,
              .layout = _pipelineLayout,
              .renderPass = _renderPass },
            "world ordered filter collection pipeline");
    }

    void WorldSurfacePipeline::CreateFramebuffers(const IndexedResources& resources)
    {
        for (uint32_t i = 0; i < _frameCount; i++)
        {
            const std::array attachments{ resources.GetIndexedCanvas(i).GetView(), resources.GetDepthCanvas(i).GetView() };
            const VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = _renderPass,
                .attachmentCount = static_cast<uint32_t>(attachments.size()),
                .pAttachments = attachments.data(),
                .width = _extent.width,
                .height = _extent.height,
                .layers = 1,
            };
            CheckVk(vkCreateFramebuffer(_device, &info, nullptr, &_framebuffers[i]), "world surfaces framebuffer");
        }
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
