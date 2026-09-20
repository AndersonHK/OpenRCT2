// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#ifdef ENABLE_VULKAN
#include "VulkanRectPipeline.h"
#include "VulkanTerrainEmissionPipeline.h"
#include <openrct2-renderer/gpu/RetainedTerrainDrawing.h>

namespace OpenRCT2::Ui::Vulkan
{
    // Bounded, unqualified drawing component. Does not enable runtime admission.
    // Device/resources/atlas generation must outlive this component. One queue,
    // serialized Record/submit order; consume diagnostic outputs before reuse.
    class TerrainDrawPipeline final
    {
        VkDevice _device = VK_NULL_HANDLE;
        VkDescriptorSetLayout _descriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool _pool = VK_NULL_HANDLE;
        VkDescriptorSet _set = VK_NULL_HANDLE;
        VkPipelineLayout _layout = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        TerrainEmissionPipeline _emission;
        RectPipeline _rect;
        std::array<Buffer, 4> _buffers;
        VkExtent3D _extent{};
        uint32_t _frameCount{}, _assetCapacity{};
        uint64_t _epoch{}, _spriteRevision{};
        uint32_t _assetCount{}, _spriteCount{};
        uint64_t _lastSpriteUploadBytes{};

    public:
        TerrainDrawPipeline() = default;
        ~TerrainDrawPipeline();
        TerrainDrawPipeline(const TerrainDrawPipeline&) = delete;
        TerrainDrawPipeline& operator=(const TerrainDrawPipeline&) = delete;
        void Initialise(const DeviceContext& device, const IndexedResources& resources,
            const std::filesystem::path& shaderDirectory, const std::filesystem::path& emissionShader,
            const std::filesystem::path& columnShader);
        void Dispose(); // Retire submissions first.
        // resources canvas/depth must already be cleared/prepared, atlas uploaded
        // and in shader layout. Sprite records describe the same pinned atlas.
        void Record(const SubmissionToken& frame, const Gpu::Terrain::RetainedTerrainSnapshot& snapshot,
            const Gpu::Terrain::DrawSpriteTable& sprites, const Gpu::Terrain::DrawCamera& camera, uint32_t assetCount);
        void DiscardPendingUploads() noexcept; // Mandatory for abandoned recordings.
        uint64_t GetLastSpriteUploadBytes() const noexcept { return _lastSpriteUploadBytes; }
        const Buffer& GetParentBuffer() const noexcept { return _buffers[1]; }
        const Buffer& GetCommandBuffer() const noexcept { return _buffers[2]; }
        const Buffer& GetColumnBuffer() const noexcept { return _buffers[3]; }
        const TerrainEmissionPipeline& GetEmission() const noexcept { return _emission; }
    };
}
#endif
