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
    class Buffer final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        VkBuffer _buffer = VK_NULL_HANDLE;
        VkDeviceMemory _memory = VK_NULL_HANDLE;
        VkDeviceSize _size = 0;

    public:
        Buffer() = default;
        ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        void Initialise(
            VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage);
        void Dispose();

        [[nodiscard]] VkBuffer GetBuffer() const noexcept
        {
            return _buffer;
        }
        [[nodiscard]] VkDeviceSize GetSize() const noexcept
        {
            return _size;
        }
    };

    class Image final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        VkImage _image = VK_NULL_HANDLE;
        VkDeviceMemory _memory = VK_NULL_HANDLE;
        VkImageView _view = VK_NULL_HANDLE;
        VkExtent3D _extent{};

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
        [[nodiscard]] VkExtent3D GetExtent() const noexcept
        {
            return _extent;
        }
    };

    class IndexedResources final
    {
    private:
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkDevice _device = VK_NULL_HANDLE;
        Image _spriteAtlas;
        Buffer _spriteDescriptors;
        std::array<Image, kFramesInFlight> _palettes;
        std::array<Image, kFramesInFlight> _lightPalettes;
        Image _remapPalette;
        Image _blendPalette;
        Image _lightFalloffs;
        VkSampler _nearestSampler = VK_NULL_HANDLE;
        std::array<Image, kFramesInFlight> _indexedCanvases;
        std::array<Image, kFramesInFlight> _depthCanvases;
        std::array<Image, kFramesInFlight> _compositeCanvases;
        std::array<Image, kFramesInFlight> _transparentCanvases;
        std::array<Image, kFramesInFlight> _lightMaps;
        std::array<Image, kFramesInFlight> _lightAccumulators;
        std::array<std::array<Image, 2>, kFramesInFlight> _transparentDepthCanvases;
        bool _atlasHasShaderLayout = false;
        std::array<bool, kFramesInFlight> _paletteHasShaderLayout{};
        std::array<bool, kFramesInFlight> _lightPaletteHasShaderLayout{};
        bool _remapPaletteHasShaderLayout = false;
        bool _blendPaletteHasShaderLayout = false;
        bool _lightFalloffsHaveShaderLayout = false;
        std::array<bool, kFramesInFlight> _canvasHasShaderLayout{};
        std::array<bool, kFramesInFlight> _lightMapHasShaderLayout{};
        std::array<bool, kFramesInFlight> _lightAccumulatorHasShaderLayout{};
        bool _hasLightAccumulators = false;

    public:
        IndexedResources() = default;
        ~IndexedResources();

        IndexedResources(const IndexedResources&) = delete;
        IndexedResources& operator=(const IndexedResources&) = delete;

        void Initialise(const Device& device, Gpu::Extent logicalExtent, bool createLightAccumulators);
        void Dispose();
        void Resize(Gpu::Extent logicalExtent, bool createLightAccumulators);
        void DiscardFrameLayouts(uint32_t frameIndex);

        void BeginAtlasUploads(VkCommandBuffer commandBuffer);
        void RecordAtlasUpload(
            VkCommandBuffer commandBuffer, const UploadAllocation& allocation, uint32_t atlasLayer,
            const Gpu::Int4& destinationBounds, uint32_t sourcePitchPixels);
        void RecordSpriteDescriptorUpload(
            VkCommandBuffer commandBuffer, const UploadAllocation& allocation, uint32_t descriptorIndex);
        void EndAtlasUploads(VkCommandBuffer commandBuffer);
        void RecordPaletteUpload(
            VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& allocation);
        void RecordIndexTableUpload(
            VkCommandBuffer commandBuffer, const UploadAllocation& allocation, bool blend);
        void RecordLightFxUpload(
            VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& intensityAllocation,
            const UploadAllocation& paletteAllocation, uint32_t width, uint32_t height);
        void RecordLightPaletteUpload(
            VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& paletteAllocation);
        void EnsureLightFxShaderLayouts(VkCommandBuffer commandBuffer, uint32_t frameIndex);
        void RecordLightFalloffUpload(VkCommandBuffer commandBuffer, const UploadAllocation& allocation);
        void PrepareLightAccumulator(VkCommandBuffer commandBuffer, uint32_t frameIndex, bool prepareForCompute);
        void FinishLightAccumulator(VkCommandBuffer commandBuffer, uint32_t frameIndex);
        void DiscardLightFalloffLayout() noexcept;
        void RecordCanvasClear(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint8_t paletteIndex);
        void RecordCanvasAndDepthClear(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint8_t paletteIndex);

        [[nodiscard]] const Image& GetSpriteAtlas() const noexcept
        {
            return _spriteAtlas;
        }
        [[nodiscard]] const Buffer& GetSpriteDescriptors() const noexcept
        {
            return _spriteDescriptors;
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
        [[nodiscard]] const Image& GetPalette(uint32_t frameIndex) const
        {
            return _palettes.at(frameIndex);
        }
        [[nodiscard]] const Image& GetLightPalette(uint32_t frameIndex) const
        {
            return _lightPalettes.at(frameIndex);
        }
        [[nodiscard]] const Image& GetLightMap(uint32_t frameIndex) const
        {
            return _lightMaps.at(frameIndex);
        }
        [[nodiscard]] const Image& GetLightAccumulator(uint32_t frameIndex) const
        {
            return _lightAccumulators.at(frameIndex);
        }
        [[nodiscard]] bool HasLightAccumulators() const noexcept
        {
            return _hasLightAccumulators;
        }
        [[nodiscard]] const Image& GetLightFalloffs() const noexcept
        {
            return _lightFalloffs;
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
        void RecordRgbaPaletteUpload(
            VkCommandBuffer commandBuffer, const UploadAllocation& allocation, Image& image, bool& hasShaderLayout,
            const char* description);
        void RecordImageUpload(
            VkCommandBuffer commandBuffer, const UploadAllocation& allocation, Image& image, bool& hasShaderLayout,
            const VkBufferImageCopy& copy);
    };

    void RecordImageBarrier(
        VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkImageSubresourceRange range, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage,
        VkAccessFlags sourceAccess, VkAccessFlags destinationAccess);
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
