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
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    /**
     * Final indexed-canvas presentation pass. Sprite pipelines render palette
     * indices into an R8_UINT canvas; this pass performs the only palette-to-
     * RGBA conversion and writes directly to the acquired swapchain image.
     */
    class PalettePipeline final
    {
    private:
        const DeviceContext* _context = nullptr;
        uint32_t _frameCount = kFramesInFlight;
        VkImageLayout _finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkDevice _device = VK_NULL_HANDLE;
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        uint32_t _maxImageDimension = 0;
        VkSampler _nearestSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kFramesInFlight> _descriptorSets{};
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkRenderPass _renderPass = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> _framebuffers;
        std::filesystem::path _shaderDirectory;
        VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D _swapchainExtent{};
        uint64_t _swapchainGeneration = 0;
        float _paperWhiteNits = 203.0f;
        int32_t _outputEncoding = 0;
        VkSampler _linearSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout _scaleDescriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool _scaleDescriptorPool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kFramesInFlight> _scaleDescriptorSets{};
        VkPipelineLayout _scalePipelineLayout = VK_NULL_HANDLE;
        VkPipeline _scalePipeline = VK_NULL_HANDLE;
        VkRenderPass _rgbaRenderPass = VK_NULL_HANDLE;
        VkPipeline _rgbaPipeline = VK_NULL_HANDLE;
        std::array<Image, kFramesInFlight> _rgbaImages;
        std::array<VkFramebuffer, kFramesInFlight> _rgbaFramebuffers{};

    public:
        PalettePipeline() = default;
        ~PalettePipeline();

        PalettePipeline(const PalettePipeline&) = delete;
        PalettePipeline& operator=(const PalettePipeline&) = delete;

        void Initialise(
            const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory,
            float paperWhiteNits);
        void Initialise(
            const DeviceContext& device, const IndexedResources& resources, std::filesystem::path shaderDirectory,
            float paperWhiteNits = 203.0f);
        // The caller owns all views through completion and retires old targets before refresh.
        void RefreshOutput(
            VkFormat format, VkExtent2D extent, std::span<const VkImageView> views, uint64_t generation, int32_t encoding,
            VkImageLayout finalLayout);
        void Record(
            const SubmissionToken& frame, uint32_t imageIndex, VkExtent2D extent, bool lightFxEnabled,
            Gpu::Extent logicalExtent, Gpu::ScaleSettings scaleSettings);
        void Dispose();
        void ReleaseSwapchainResources();
        void RefreshSwapchain(const Device& device);
        void RefreshDescriptors(const IndexedResources& resources);
        void SetCanvasSource(uint32_t frameIndex, const Image& canvas);
        void SetLightMapSource(uint32_t frameIndex, const Image& lightMap);
        void SetHdrPaperWhiteNits(float nits) noexcept;
        void Record(const FrameToken& frame, bool lightFxEnabled, Gpu::Extent logicalExtent, Gpu::ScaleSettings scaleSettings);

    private:
        void CreateDescriptorResources(const IndexedResources& resources);
        void CreateScaleResources();
        void EnsureScaleTarget(uint32_t frameIndex, VkExtent2D extent);
        void CreateRenderPass();
        void CreatePipeline();
        void CreateFramebuffers(std::span<const VkImageView> imageViews);
        void DestroySwapchainResources();
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
