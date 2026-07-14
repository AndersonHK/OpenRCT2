/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_VULKAN

    #include "../gpu/GpuCommandStream.h"
    #include "VulkanDevice.h"
    #include "VulkanResources.h"

    #include <array>
    #include <filesystem>
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    class WorldSurfacePipeline final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
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
        uint32_t _uploadedWidth{};
        uint32_t _uploadedHeight{};
        VkExtent2D _extent{};
        std::filesystem::path _shaderDirectory;

    public:
        WorldSurfacePipeline() = default;
        ~WorldSurfacePipeline();

        WorldSurfacePipeline(const WorldSurfacePipeline&) = delete;
        WorldSurfacePipeline& operator=(const WorldSurfacePipeline&) = delete;

        void Initialise(const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory);
        void Dispose();
        void Record(const FrameToken& frame, const Gpu::WorldSurfaceSceneCommand& scene);
        void DiscardPendingUploads() noexcept;

    private:
        void CreateDescriptors(const IndexedResources& resources);
        void CreateRenderPass();
        void CreatePipeline();
        void CreateFramebuffers(const IndexedResources& resources);
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
