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

    #include "../gpu/GpuCommandStream.h"

    #include <array>
    #include <filesystem>

namespace OpenRCT2::Ui::Vulkan
{
    class TransparencyPipeline final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        const IndexedResources* _resources = nullptr;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        VkExtent2D _extent{};
        std::filesystem::path _shaderDirectory;
        VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout _peelSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout _composeSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout _peelPipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayout _composePipelineLayout = VK_NULL_HANDLE;
        VkRenderPass _peelRenderPass = VK_NULL_HANDLE;
        VkRenderPass _composeRenderPass = VK_NULL_HANDLE;
        VkPipeline _peelPipeline = VK_NULL_HANDLE;
        VkPipeline _composePipeline = VK_NULL_HANDLE;
        std::array<std::array<VkDescriptorSet, 2>, kFramesInFlight> _peelSets{};
        std::array<std::array<std::array<VkDescriptorSet, 2>, 2>, kFramesInFlight> _composeSets{};
        std::array<std::array<VkFramebuffer, 2>, kFramesInFlight> _peelFramebuffers{};
        std::array<std::array<VkFramebuffer, 2>, kFramesInFlight> _composeFramebuffers{};

    public:
        TransparencyPipeline() = default;
        ~TransparencyPipeline();
        TransparencyPipeline(const TransparencyPipeline&) = delete;
        TransparencyPipeline& operator=(const TransparencyPipeline&) = delete;

        void Initialise(
            const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory);
        void Dispose();
        [[nodiscard]] bool Record(
            const FrameToken& frame, const Gpu::CommandBatch<Gpu::RectCommand>& commands,
            uint32_t layerCount) const;

    private:
        void CreateDescriptors(const IndexedResources& resources);
        void CreateRenderPasses();
        void CreatePipelines();
        void CreateFramebuffers(const IndexedResources& resources);
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
