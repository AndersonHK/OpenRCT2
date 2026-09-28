/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_VULKAN

    #include "VulkanDevice.h"
    #include "VulkanResources.h"

    #include <array>
    #include <filesystem>
    #include <openrct2-renderer/gpu/GpuCommandStream.h>
    #include <span>

namespace OpenRCT2::Ui::Vulkan
{
    class RectPipeline final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        uint32_t _frameCount = kFramesInFlight;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet _descriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkRenderPass _renderPass = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        VkPipeline _spritePipeline = VK_NULL_HANDLE;
        std::array<VkFramebuffer, kFramesInFlight> _framebuffers{};
        VkExtent2D _extent{};
        std::filesystem::path _shaderDirectory;

    public:
        RectPipeline() = default;
        ~RectPipeline();

        RectPipeline(const RectPipeline&) = delete;
        RectPipeline& operator=(const RectPipeline&) = delete;

        void Initialise(const DeviceContext& device, const IndexedResources& resources, std::filesystem::path shaderDirectory);
        void Dispose();
        void Record(
            const SubmissionToken& frame, const Gpu::CommandBatch<Gpu::RectCommand>& commands,
            const Gpu::CommandBatch<Gpu::SpriteCommand>& sprites) const;
        void RecordDamageClear(const SubmissionToken& frame, std::span<const Gpu::Int4> rectangles) const;
        // Device-generated compact SpriteCommand stream. Caller supplies the
        // compute->vertex/indirect dependency and pins both buffers until retirement.
        // One indirect command is issued per column, without multiDrawIndirect.
        void RecordSpritesIndirect(const SubmissionToken& frame, const Buffer& sprites,
            const Buffer& indirect, uint32_t drawCount, VkDeviceSize spriteStride, VkDeviceSize indirectOffset, uint32_t indirectStride) const;

    private:
        void CreateDescriptors(const IndexedResources& resources);
        void CreateRenderPass();
        void CreatePipeline();
        void CreateFramebuffers(const IndexedResources& resources);
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
