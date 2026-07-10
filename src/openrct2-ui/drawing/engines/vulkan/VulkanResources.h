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

    #include "../gpu/GpuAtlas.h"
    #include "../gpu/GpuBackend.h"

    #include <array>

namespace OpenRCT2::Ui::Vulkan
{
    class Image final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        VkImage _image = VK_NULL_HANDLE;
        VkDeviceMemory _memory = VK_NULL_HANDLE;
        VkImageView _view = VK_NULL_HANDLE;
        VkFormat _format = VK_FORMAT_UNDEFINED;
        VkExtent3D _extent{};
        uint32_t _layers = 0;

    public:
        Image() = default;
        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;

        void Initialise(
            VkPhysicalDevice physicalDevice, VkDevice device, VkExtent3D extent, uint32_t layers, VkFormat format,
            VkImageUsageFlags usage, VkImageAspectFlags aspect);
        void Dispose();

        [[nodiscard]] VkImage GetImage() const noexcept
        {
            return _image;
        }
        [[nodiscard]] VkImageView GetView() const noexcept
        {
            return _view;
        }
        [[nodiscard]] VkFormat GetFormat() const noexcept
        {
            return _format;
        }
        [[nodiscard]] VkExtent3D GetExtent() const noexcept
        {
            return _extent;
        }
        [[nodiscard]] uint32_t GetLayers() const noexcept
        {
            return _layers;
        }
    };

    class IndexedResources final
    {
    private:
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkDevice _device = VK_NULL_HANDLE;
        Image _spriteAtlas;
        Image _palette;
        Image _remapPalette;
        Image _blendPalette;
        VkSampler _nearestSampler = VK_NULL_HANDLE;
        std::array<Image, kFramesInFlight> _indexedCanvases;
        std::array<Image, kFramesInFlight> _depthCanvases;
        std::array<Image, kFramesInFlight> _compositeCanvases;
        std::array<Image, kFramesInFlight> _transparentCanvases;
        std::array<std::array<Image, 2>, kFramesInFlight> _transparentDepthCanvases;
        bool _atlasHasShaderLayout = false;
        bool _paletteHasShaderLayout = false;
        bool _remapPaletteHasShaderLayout = false;
        bool _blendPaletteHasShaderLayout = false;
        std::array<bool, kFramesInFlight> _canvasHasShaderLayout{};

    public:
        IndexedResources() = default;
        ~IndexedResources();

        IndexedResources(const IndexedResources&) = delete;
        IndexedResources& operator=(const IndexedResources&) = delete;

        void Initialise(const Device& device, Gpu::Extent logicalExtent);
        void Dispose();
        void Resize(Gpu::Extent logicalExtent);

        void BeginAtlasUploads(VkCommandBuffer commandBuffer);
        void RecordAtlasUpload(
            VkCommandBuffer commandBuffer, const UploadAllocation& allocation, uint32_t atlasLayer,
            const Gpu::Int4& destinationBounds, uint32_t sourcePitchPixels);
        void EndAtlasUploads(VkCommandBuffer commandBuffer);
        void RecordPaletteUpload(VkCommandBuffer commandBuffer, const UploadAllocation& allocation);
        void RecordRemapPaletteUpload(VkCommandBuffer commandBuffer, const UploadAllocation& allocation);
        void RecordBlendPaletteUpload(VkCommandBuffer commandBuffer, const UploadAllocation& allocation);
        void RecordCanvasClear(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint8_t paletteIndex);
        void RecordCanvasUpload(
            VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& allocation,
            const Gpu::CanvasUpload& upload);
        void RecordCanvasAndDepthClear(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint8_t paletteIndex);

        [[nodiscard]] const Image& GetSpriteAtlas() const noexcept
        {
            return _spriteAtlas;
        }
        [[nodiscard]] const Image& GetIndexedCanvas(uint32_t frameIndex) const
        {
            return _indexedCanvases.at(frameIndex);
        }
        [[nodiscard]] const Image& GetDepthCanvas(uint32_t frameIndex) const
        {
            return _depthCanvases.at(frameIndex);
        }
        [[nodiscard]] const Image& GetCompositeCanvas(uint32_t frameIndex) const
        {
            return _compositeCanvases.at(frameIndex);
        }
        [[nodiscard]] const Image& GetTransparentCanvas(uint32_t frameIndex) const
        {
            return _transparentCanvases.at(frameIndex);
        }
        [[nodiscard]] const Image& GetTransparentDepthCanvas(uint32_t frameIndex, uint32_t index) const
        {
            return _transparentDepthCanvases.at(frameIndex).at(index);
        }
        [[nodiscard]] const Image& GetPalette() const noexcept
        {
            return _palette;
        }
        [[nodiscard]] const Image& GetRemapPalette() const noexcept
        {
            return _remapPalette;
        }
        [[nodiscard]] const Image& GetBlendPalette() const noexcept
        {
            return _blendPalette;
        }
        [[nodiscard]] VkSampler GetNearestSampler() const noexcept
        {
            return _nearestSampler;
        }

    private:
        void CreateCanvases(Gpu::Extent logicalExtent);
        void DestroyCanvases();
        void RecordIndexTableUpload(
            VkCommandBuffer commandBuffer, const UploadAllocation& allocation, Image& image, bool& hasShaderLayout,
            const char* description);
    };

    void RecordImageBarrier(
        VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkImageSubresourceRange range, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage,
        VkAccessFlags sourceAccess, VkAccessFlags destinationAccess);
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
