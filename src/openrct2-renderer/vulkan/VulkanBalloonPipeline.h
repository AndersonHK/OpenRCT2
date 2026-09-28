/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
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
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    class BalloonPipeline final
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
        VkPipeline _computePipeline = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        std::array<VkFramebuffer, kFramesInFlight> _framebuffers{};
        Buffer _sourceRecords;
        Buffer _spriteSets;
        Buffer _visibleRecords;
        Buffer _indirectCommands;
        std::vector<uint64_t> _uploadedRevisions;
        uint64_t _uploadedSpriteRevision{};
        uint64_t _uploadedEpoch{};
        uint64_t _lastSequence{};
        Gpu::BalloonUploadStats _stats{};
        VkExtent2D _extent{};
        std::filesystem::path _shaderDirectory;

    public:
        BalloonPipeline() = default;
        ~BalloonPipeline();

        BalloonPipeline(const BalloonPipeline&) = delete;
        BalloonPipeline& operator=(const BalloonPipeline&) = delete;

        void Initialise(const DeviceContext& device, const IndexedResources& resources, std::filesystem::path shaderDirectory);
        void Dispose();
        void Record(const SubmissionToken& frame, const Gpu::BalloonSceneCommand& scene);
        void DiscardPendingUploads() noexcept;
        void BeginFrame() noexcept
        {
            _stats = {};
        }
        [[nodiscard]] Gpu::BalloonUploadStats GetUploadStats() const noexcept
        {
            return _stats;
        }
        // Explicit diagnostic tests may copy these device-local outputs into their submission's readback allocation.
        [[nodiscard]] VkBuffer GetDiagnosticOutputBuffer() const noexcept
        {
            return _visibleRecords.GetBuffer();
        }
        [[nodiscard]] VkBuffer GetDiagnosticIndirectBuffer() const noexcept
        {
            return _indirectCommands.GetBuffer();
        }

    private:
        void CreateDescriptors(const IndexedResources& resources);
        void CreateRenderPass();
        void CreatePipeline();
        void CreateFramebuffers(const IndexedResources& resources);
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
