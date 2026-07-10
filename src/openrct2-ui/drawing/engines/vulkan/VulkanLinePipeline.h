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
    class LinePipeline final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkRenderPass _renderPass = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        std::array<VkFramebuffer, kFramesInFlight> _framebuffers{};
        VkExtent2D _extent{};
        std::filesystem::path _shaderDirectory;

    public:
        LinePipeline() = default;
        ~LinePipeline();

        LinePipeline(const LinePipeline&) = delete;
        LinePipeline& operator=(const LinePipeline&) = delete;

        void Initialise(
            const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory);
        void Dispose();
        void Refresh(const IndexedResources& resources);
        void Record(const FrameToken& frame, const Gpu::CommandBatch<Gpu::LineCommand>& commands) const;

    private:
        void CreateRenderPass();
        void CreatePipeline();
        void CreateFramebuffers(const IndexedResources& resources);
        void DestroyCanvasResources();
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
