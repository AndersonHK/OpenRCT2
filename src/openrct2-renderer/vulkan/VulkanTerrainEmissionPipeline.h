/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once
#ifdef ENABLE_VULKAN
#include "VulkanResources.h"
#include <filesystem>
#include <openrct2-renderer/gpu/RetainedTerrain.h>

namespace OpenRCT2::Ui::Vulkan
{
    // Shared-device compute component, independent of windows/FrameExecutor.
    // This produces UNSORTED descriptors, never enables world admission or draws.
    // Caller serializes Record on one graphics queue; consume/copy results in
    // the same submission before another Record overwrites transient outputs.
    class TerrainEmissionPipeline final
    {
        VkDevice _device = VK_NULL_HANDLE;
        VkDescriptorSetLayout _descriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool _pool = VK_NULL_HANDLE;
        VkDescriptorSet _set = VK_NULL_HANDLE;
        VkPipelineLayout _layout = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        std::array<Buffer, 5> _buffers;
        uint64_t _epoch{};
        std::array<uint64_t, Gpu::Terrain::kRetainedChunkCount> _chunkRevisions{};
        uint64_t _materialRevision{};
        uint32_t _primitiveCapacity{};
        uint64_t _lastTileUploadBytes{}, _lastMaterialUploadBytes{};

    public:
        TerrainEmissionPipeline() = default;
        ~TerrainEmissionPipeline();
        TerrainEmissionPipeline(const TerrainEmissionPipeline&) = delete;
        TerrainEmissionPipeline& operator=(const TerrainEmissionPipeline&) = delete;

        // Explicit shader path: qualification must pin the actual compiled binary.
        // No main renderer path currently requires or instantiates this component.
        void Initialise(const DeviceContext& device, const std::filesystem::path& shader,
            uint32_t storageCapacity = Gpu::Terrain::kRetainedEmissionCapacity);
        void Dispose(); // Caller must retire submissions first.
        void Record(const SubmissionToken& frame, const Gpu::Terrain::RetainedTerrainSnapshot& snapshot,
            uint32_t rotation, int32_t zoom, bool transparentBackground,
            uint32_t outputCapacity = Gpu::Terrain::kRetainedEmissionCapacity);
        // Mandatory if a recorded upload is abandoned before queue submission.
        void DiscardPendingUploads() noexcept;

        uint64_t GetLastTileUploadBytes() const noexcept { return _lastTileUploadBytes; }
        uint64_t GetLastMaterialUploadBytes() const noexcept { return _lastMaterialUploadBytes; }
        const Buffer& GetPrimitiveBuffer() const noexcept { return _buffers[3]; }
        const Buffer& GetStatusBuffer() const noexcept { return _buffers[4]; }
        const Buffer& GetTileCountBuffer() const noexcept { return _buffers[2]; }
        const Buffer& GetTileBuffer() const noexcept { return _buffers[0]; }
        const Buffer& GetMaterialBuffer() const noexcept { return _buffers[1]; }
    };
} // namespace OpenRCT2::Ui::Vulkan
#endif
