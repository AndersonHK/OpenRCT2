/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanResources.h"

    #include "VulkanShader.h"

    #include <algorithm>
    #include <stdexcept>
    #include <string>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        uint32_t FindDeviceMemoryType(
            VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags requiredProperties)
        {
            VkPhysicalDeviceMemoryProperties properties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
            for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
            {
                if ((typeBits & (1u << i)) != 0
                    && (properties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties)
                {
                    return i;
                }
            }
            throw std::runtime_error("No device-local Vulkan memory type is available");
        }

        constexpr VkImageSubresourceRange ImageRange(
            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t layers = 1) noexcept
        {
            return { aspect, 0, 1, 0, layers };
        }

        template<typename Range>
        void DisposeImages(Range& images)
        {
            for (auto& image : images)
            {
                if constexpr (requires { image.Dispose(); })
                    image.Dispose();
                else
                    DisposeImages(image);
            }
        }
    } // namespace

    Buffer::~Buffer()
    {
        Dispose();
    }

    void Buffer::Initialise(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage)
    {
        Dispose();
        if (size == 0)
            throw std::invalid_argument("Vulkan device buffer requires a non-zero size");
        _device = device;
        _size = size;
        const VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        CheckVk(vkCreateBuffer(_device, &bufferInfo, nullptr, &_buffer), "vkCreateBuffer(device-local)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(_device, _buffer, &requirements);
        const VkMemoryAllocateInfo memoryInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = FindDeviceMemoryType(
                physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        try
        {
            CheckVk(vkAllocateMemory(_device, &memoryInfo, nullptr, &_memory), "vkAllocateMemory(device-local buffer)");
            CheckVk(vkBindBufferMemory(_device, _buffer, _memory, 0), "vkBindBufferMemory(device-local)");
        }
        catch (...)
        {
            Dispose();
            throw;
        }
    }

    void Buffer::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(_device, _buffer, nullptr);
            vkFreeMemory(_device, _memory, nullptr);
        }
        _device = VK_NULL_HANDLE;
        _buffer = VK_NULL_HANDLE;
        _memory = VK_NULL_HANDLE;
        _size = 0;
    }

    Image::~Image()
    {
        Dispose();
    }

    Image::Image(Image&& other) noexcept
    {
        *this = std::move(other);
    }

    Image& Image::operator=(Image&& other) noexcept
    {
        if (this != &other)
        {
            Dispose();
            _device = std::exchange(other._device, VK_NULL_HANDLE);
            _image = std::exchange(other._image, VK_NULL_HANDLE);
            _memory = std::exchange(other._memory, VK_NULL_HANDLE);
            _view = std::exchange(other._view, VK_NULL_HANDLE);
            _extent = std::exchange(other._extent, VkExtent3D{});
        }
        return *this;
    }

    void Image::Initialise(
        VkPhysicalDevice physicalDevice, VkDevice device, VkExtent3D extent, uint32_t layers, VkFormat format,
        VkImageUsageFlags usage, VkImageAspectFlags aspect, bool forceArrayView)
    {
        Dispose();
        _device = device;
        _extent = extent;

        const VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = extent,
            .mipLevels = 1,
            .arrayLayers = layers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        CheckVk(vkCreateImage(_device, &imageInfo, nullptr, &_image), "vkCreateImage");

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(_device, _image, &memoryRequirements);
        const VkMemoryAllocateInfo memoryInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = FindDeviceMemoryType(
                physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        CheckVk(vkAllocateMemory(_device, &memoryInfo, nullptr, &_memory), "vkAllocateMemory(image)");
        CheckVk(vkBindImageMemory(_device, _image, _memory, 0), "vkBindImageMemory");

        const VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = _image,
            .viewType = layers > 1 || forceArrayView ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .components = {},
            .subresourceRange = {
                .aspectMask = aspect,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = layers,
            },
        };
        CheckVk(vkCreateImageView(_device, &viewInfo, nullptr, &_view), "vkCreateImageView");
    }

    void Image::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            vkDestroyImageView(_device, _view, nullptr);
            vkDestroyImage(_device, _image, nullptr);
            vkFreeMemory(_device, _memory, nullptr);
        }
        _device = VK_NULL_HANDLE;
        _image = VK_NULL_HANDLE;
        _memory = VK_NULL_HANDLE;
        _view = VK_NULL_HANDLE;
        _extent = {};
    }

    IndexedResources::~IndexedResources()
    {
        Dispose();
    }

    void IndexedResources::Initialise(
        const DeviceContext& device, Gpu::Extent logicalExtent, bool createLightAccumulators, uint32_t frameCount,
        uint32_t atlasLayers)
    {
        Dispose();
        if (frameCount == 0 || frameCount > kFramesInFlight || atlasLayers == 0 || atlasLayers > Gpu::kAtlasLayers)
            throw std::invalid_argument("Invalid bounded Vulkan resource capacity");
        _frameCount = frameCount;
        _atlasLayers = atlasLayers;
        _physicalDevice = device.GetPhysicalDevice();
        _device = device.GetDevice();
        _hasLightAccumulators = createLightAccumulators;

        const auto initialise = [&](Image& image, VkExtent3D extent, uint32_t layers, VkFormat format,
                                    VkImageUsageFlags usage) {
            image.Initialise(_physicalDevice, _device, extent, layers, format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
        };
        // Atlas shader bindings are array images even when the bounded domain allocates just one layer.
        _spriteAtlas.Initialise(
            _physicalDevice, _device, { Gpu::kAtlasDimension, Gpu::kAtlasDimension, 1 }, _atlasLayers, VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, true);
        _spriteDescriptors.Initialise(
            _physicalDevice, _device,
            static_cast<VkDeviceSize>(_atlasLayers * Gpu::kAtlasSlotsPerLayer) * sizeof(Gpu::SpriteAssetDescriptor),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        for (auto& palette : _palettes)
        {
            initialise(
                palette, { 256, 1, 1 }, 1, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        }
        for (auto& lightPalette : _lightPalettes)
        {
            initialise(
                lightPalette, { 256, 1, 1 }, 1, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        }
        initialise(
            _remapPalette, { 256, 256, 1 }, 1, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        initialise(
            _blendPalette, { 256, 256, 1 }, 1, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        initialise(
            _lightFalloffs, { 256, 256, 1 }, 8, VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        const VkSamplerCreateInfo samplerInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        CheckVk(vkCreateSampler(_device, &samplerInfo, nullptr, &_nearestSampler), "vkCreateSampler(indexed)");
        CreateCanvases(logicalExtent);
    }

    void IndexedResources::Dispose()
    {
        DestroyCanvases();
        if (_device != VK_NULL_HANDLE)
        {
            vkDestroySampler(_device, _nearestSampler, nullptr);
        }
        _nearestSampler = VK_NULL_HANDLE;
        _blendPalette.Dispose();
        _lightFalloffs.Dispose();
        _remapPalette.Dispose();
        DisposeImages(_lightPalettes);
        DisposeImages(_palettes);
        _spriteAtlas.Dispose();
        _spriteDescriptors.Dispose();
        _physicalDevice = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE;
        _hasLightAccumulators = false;
        _atlasHasShaderLayout = false;
        _submittedAtlasHasShaderLayout = false;
        _submittedRemapPaletteHasShaderLayout = false;
        _submittedBlendPaletteHasShaderLayout = false;
        _submittedLightFalloffsHaveShaderLayout = false;
        _paletteHasShaderLayout.fill(false);
        _lightPaletteHasShaderLayout.fill(false);
        _remapPaletteHasShaderLayout = false;
        _blendPaletteHasShaderLayout = false;
        _lightFalloffsHaveShaderLayout = false;
    }

    void IndexedResources::Resize(Gpu::Extent logicalExtent, bool createLightAccumulators)
    {
        if (_device == VK_NULL_HANDLE)
        {
            return;
        }
        DestroyCanvases();
        _hasLightAccumulators = createLightAccumulators;
        CreateCanvases(logicalExtent);
    }

    void IndexedResources::DiscardFrameLayouts(uint32_t frameIndex)
    {
        if (frameIndex >= _frameCount)
        {
            throw std::out_of_range("Vulkan discarded frame index is out of range");
        }
        // Commands which updated these trackers were reset without submission.
        // Frame-local images are fully overwritten before their next read.
        // Shared images must retain the last submitted layout, including the
        // initial UNDEFINED state if their first transition was abandoned.
        _atlasHasShaderLayout = _submittedAtlasHasShaderLayout;
        _remapPaletteHasShaderLayout = _submittedRemapPaletteHasShaderLayout;
        _blendPaletteHasShaderLayout = _submittedBlendPaletteHasShaderLayout;
        _lightFalloffsHaveShaderLayout = _submittedLightFalloffsHaveShaderLayout;
        _paletteHasShaderLayout[frameIndex] = false;
        _lightPaletteHasShaderLayout[frameIndex] = false;
        _canvasHasShaderLayout[frameIndex] = false;
        _lightMapHasShaderLayout[frameIndex] = false;
        _lightAccumulatorHasShaderLayout[frameIndex] = false;
    }

    void IndexedResources::DiscardLightFalloffLayout() noexcept
    {
        _lightFalloffsHaveShaderLayout = _submittedLightFalloffsHaveShaderLayout;
    }

    void IndexedResources::CommitFrameLayouts() noexcept
    {
        _submittedAtlasHasShaderLayout = _atlasHasShaderLayout;
        _submittedRemapPaletteHasShaderLayout = _remapPaletteHasShaderLayout;
        _submittedBlendPaletteHasShaderLayout = _blendPaletteHasShaderLayout;
        _submittedLightFalloffsHaveShaderLayout = _lightFalloffsHaveShaderLayout;
    }

    void IndexedResources::EnsureAtlasShaderLayout(VkCommandBuffer commandBuffer)
    {
        if (_atlasHasShaderLayout)
            return;
        // Untextured commands still bind the shared atlas descriptor. Its
        // complete view must have the declared layout before the first draw,
        // even when no sprite upload has occurred yet.
        RecordImageBarrier(
            commandBuffer, _spriteAtlas.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, _atlasLayers), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT);
        _atlasHasShaderLayout = true;
    }

    void IndexedResources::BeginAtlasUploads(VkCommandBuffer commandBuffer)
    {
        // The descriptor table is shared across frame slots. A previous frame
        // may still read it, or have uploaded a descriptor without drawing it.
        // Its transfer-to-vertex barrier does not order our next transfer write.
        const VkBufferMemoryBarrier descriptorBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = _spriteDescriptors.GetBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &descriptorBarrier, 0, nullptr);
        const auto range = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, _atlasLayers);
        RecordImageBarrier(
            commandBuffer, _spriteAtlas.GetImage(),
            _atlasHasShaderLayout ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
            _atlasHasShaderLayout ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, _atlasHasShaderLayout ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    void IndexedResources::RecordAtlasUpload(
        VkCommandBuffer commandBuffer, const UploadAllocation& allocation, uint32_t atlasLayer,
        const Gpu::Int4& destinationBounds, uint32_t sourcePitchPixels)
    {
        if (atlasLayer >= _atlasLayers || destinationBounds.x < 0 || destinationBounds.y < 0
            || destinationBounds.z <= destinationBounds.x || destinationBounds.w <= destinationBounds.y)
        {
            throw std::invalid_argument("Invalid Vulkan sprite-atlas upload destination");
        }

        const auto width = static_cast<uint32_t>(destinationBounds.z - destinationBounds.x);
        const auto height = static_cast<uint32_t>(destinationBounds.w - destinationBounds.y);
        if (destinationBounds.z > Gpu::kAtlasDimension || destinationBounds.w > Gpu::kAtlasDimension
            || sourcePitchPixels < width)
        {
            throw std::invalid_argument("Vulkan sprite-atlas upload exceeds its source or destination bounds");
        }
        if (allocation.size < static_cast<VkDeviceSize>(sourcePitchPixels) * height)
        {
            throw std::invalid_argument("Vulkan sprite-atlas upload allocation is too small");
        }

        const VkBufferImageCopy copy = {
            .bufferOffset = allocation.offset,
            .bufferRowLength = sourcePitchPixels,
            .bufferImageHeight = height,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = atlasLayer,
                .layerCount = 1,
            },
            .imageOffset = { destinationBounds.x, destinationBounds.y, 0 },
            .imageExtent = { width, height, 1 },
        };
        vkCmdCopyBufferToImage(
            commandBuffer, allocation.buffer, _spriteAtlas.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        allocation.Record(Drawing::UploadMetric::imageTransfer, uint64_t{ width } * height);
    }

    void IndexedResources::RecordSpriteDescriptorUpload(
        VkCommandBuffer commandBuffer, const UploadAllocation& allocation, uint32_t descriptorIndex)
    {
        if (descriptorIndex >= _atlasLayers * Gpu::kAtlasSlotsPerLayer || allocation.size < sizeof(Gpu::SpriteAssetDescriptor))
        {
            throw std::invalid_argument("Invalid Vulkan sprite descriptor upload");
        }
        const VkBufferCopy copy = {
            .srcOffset = allocation.offset,
            .dstOffset = static_cast<VkDeviceSize>(descriptorIndex) * sizeof(Gpu::SpriteAssetDescriptor),
            .size = sizeof(Gpu::SpriteAssetDescriptor),
        };
        vkCmdCopyBuffer(commandBuffer, allocation.buffer, _spriteDescriptors.GetBuffer(), 1, &copy);
        allocation.Record(Drawing::UploadMetric::bufferTransfer, copy.size);
    }

    void IndexedResources::EndAtlasUploads(VkCommandBuffer commandBuffer)
    {
        const auto range = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, _atlasLayers);
        RecordImageBarrier(
            commandBuffer, _spriteAtlas.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        const VkBufferMemoryBarrier descriptorBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = _spriteDescriptors.GetBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1,
            &descriptorBarrier, 0, nullptr);
        _atlasHasShaderLayout = true;
    }

    void IndexedResources::RecordPaletteUpload(
        VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& allocation)
    {
        if (frameIndex >= _frameCount)
        {
            throw std::out_of_range("Vulkan palette frame index is out of range");
        }
        RecordRgbaPaletteUpload(
            commandBuffer, allocation, _palettes[frameIndex], _paletteHasShaderLayout[frameIndex], "palette");
    }

    void IndexedResources::RecordRgbaPaletteUpload(
        VkCommandBuffer commandBuffer, const UploadAllocation& allocation, Image& image, bool& hasShaderLayout,
        const char* description)
    {
        constexpr VkDeviceSize kPaletteBytes = 256 * 4;
        if (allocation.size < kPaletteBytes)
        {
            throw std::invalid_argument(std::string("Vulkan ") + description + " upload allocation is too small");
        }
        const VkBufferImageCopy copy = {
            .bufferOffset = allocation.offset,
            .bufferRowLength = 256,
            .bufferImageHeight = 1,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { 256, 1, 1 },
        };
        RecordImageUpload(commandBuffer, allocation, image, hasShaderLayout, copy, 4);
    }

    void IndexedResources::RecordImageUpload(
        VkCommandBuffer commandBuffer, const UploadAllocation& allocation, Image& image, bool& hasShaderLayout,
        const VkBufferImageCopy& copy, uint32_t bytesPerPixel)
    {
        const VkImageSubresourceRange range = {
            .aspectMask = copy.imageSubresource.aspectMask,
            .baseMipLevel = copy.imageSubresource.mipLevel,
            .levelCount = 1,
            .baseArrayLayer = copy.imageSubresource.baseArrayLayer,
            .layerCount = copy.imageSubresource.layerCount,
        };
        // Lookup images are sampled by both indexed fragments and the world filter compute resolve.
        // Include both consumers when replacing an image and when publishing its new texels.
        constexpr auto shaderStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        RecordImageBarrier(
            commandBuffer, image.GetImage(),
            hasShaderLayout ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range, hasShaderLayout ? shaderStages : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, hasShaderLayout ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdCopyBufferToImage(
            commandBuffer, allocation.buffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        allocation.Record(
            Drawing::UploadMetric::imageTransfer,
            uint64_t{ copy.imageExtent.width } * copy.imageExtent.height * copy.imageExtent.depth
                * copy.imageSubresource.layerCount * bytesPerPixel);
        RecordImageBarrier(
            commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            range, VK_PIPELINE_STAGE_TRANSFER_BIT, shaderStages, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        hasShaderLayout = true;
    }

    void IndexedResources::RecordLightFxUpload(
        VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& intensityAllocation,
        const UploadAllocation& paletteAllocation, uint32_t width, uint32_t height)
    {
        if (frameIndex >= _frameCount || width == 0 || height == 0)
        {
            throw std::invalid_argument("Invalid Vulkan LightFX upload destination");
        }
        auto& lightMap = _lightMaps[frameIndex];
        const auto extent = lightMap.GetExtent();
        const VkDeviceSize intensityBytes = static_cast<VkDeviceSize>(width) * height;
        if (width > extent.width || height > extent.height || intensityAllocation.size < intensityBytes)
        {
            throw std::invalid_argument("Vulkan LightFX intensity upload exceeds its source or destination");
        }

        const VkBufferImageCopy copy = {
            .bufferOffset = intensityAllocation.offset,
            .bufferRowLength = width,
            .bufferImageHeight = height,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { width, height, 1 },
        };
        RecordImageUpload(commandBuffer, intensityAllocation, lightMap, _lightMapHasShaderLayout[frameIndex], copy);

        RecordRgbaPaletteUpload(
            commandBuffer, paletteAllocation, _lightPalettes[frameIndex], _lightPaletteHasShaderLayout[frameIndex],
            "LightFX palette");
    }

    void IndexedResources::RecordLightPaletteUpload(
        VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& paletteAllocation)
    {
        if (frameIndex >= _frameCount)
            throw std::out_of_range("Vulkan LightFX palette frame index is out of range");
        RecordRgbaPaletteUpload(
            commandBuffer, paletteAllocation, _lightPalettes[frameIndex], _lightPaletteHasShaderLayout[frameIndex],
            "LightFX palette");
    }

    void IndexedResources::EnsureLightFxShaderLayouts(VkCommandBuffer commandBuffer, uint32_t frameIndex)
    {
        if (frameIndex >= _frameCount)
        {
            throw std::out_of_range("Vulkan LightFX frame index is out of range");
        }
        constexpr auto range = ImageRange();
        const auto ensureLayout = [&](Image& image, bool& hasShaderLayout) {
            if (!hasShaderLayout)
            {
                RecordImageBarrier(
                    commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT);
                hasShaderLayout = true;
            }
        };
        ensureLayout(_lightMaps[frameIndex], _lightMapHasShaderLayout[frameIndex]);
        ensureLayout(_lightPalettes[frameIndex], _lightPaletteHasShaderLayout[frameIndex]);
    }

    void IndexedResources::RecordLightFalloffUpload(VkCommandBuffer commandBuffer, const UploadAllocation& allocation)
    {
        constexpr VkDeviceSize byteSize = 8 * 256 * 256;
        if (allocation.size < byteSize)
        {
            throw std::invalid_argument("Vulkan LightFX falloff upload is too small");
        }
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 8 };
        RecordImageBarrier(
            commandBuffer, _lightFalloffs.GetImage(),
            _lightFalloffsHaveShaderLayout ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
            _lightFalloffsHaveShaderLayout ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, _lightFalloffsHaveShaderLayout ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT);
        std::array<VkBufferImageCopy, 8> copies{};
        for (uint32_t layer = 0; layer < copies.size(); layer++)
        {
            copies[layer] = {
                .bufferOffset = allocation.offset + layer * 256 * 256,
                .bufferRowLength = 256,
                .bufferImageHeight = 256,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1 },
                .imageExtent = { 256, 256, 1 },
            };
        }
        vkCmdCopyBufferToImage(
            commandBuffer, allocation.buffer, _lightFalloffs.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(copies.size()), copies.data());
        allocation.Record(Drawing::UploadMetric::imageTransfer, byteSize);
        RecordImageBarrier(
            commandBuffer, _lightFalloffs.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        _lightFalloffsHaveShaderLayout = true;
    }

    void IndexedResources::PrepareLightAccumulator(VkCommandBuffer commandBuffer, uint32_t frameIndex, bool prepareForCompute)
    {
        auto& image = _lightAccumulators.at(frameIndex);
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        RecordImageBarrier(
            commandBuffer, image.GetImage(),
            _lightAccumulatorHasShaderLayout[frameIndex] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
            _lightAccumulatorHasShaderLayout[frameIndex] ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                         : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, _lightAccumulatorHasShaderLayout[frameIndex] ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT);
        const VkClearColorValue clear = { .uint32 = { 0, 0, 0, 0 } };
        vkCmdClearColorImage(commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        if (prepareForCompute)
        {
            RecordImageBarrier(
                commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, range,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            _lightAccumulatorHasShaderLayout[frameIndex] = false;
        }
        else
        {
            RecordImageBarrier(
                commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                range, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT);
            _lightAccumulatorHasShaderLayout[frameIndex] = true;
        }
    }

    void IndexedResources::FinishLightAccumulator(VkCommandBuffer commandBuffer, uint32_t frameIndex)
    {
        auto& image = _lightAccumulators.at(frameIndex);
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        RecordImageBarrier(
            commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        _lightAccumulatorHasShaderLayout[frameIndex] = true;
    }

    void IndexedResources::RecordIndexTableUpload(VkCommandBuffer commandBuffer, const UploadAllocation& allocation, bool blend)
    {
        constexpr VkDeviceSize kIndexTableBytes = 256 * 256;
        if (allocation.size < kIndexTableBytes)
        {
            throw std::invalid_argument(
                blend ? "Vulkan blend-palette upload allocation is too small"
                      : "Vulkan remap-palette upload allocation is too small");
        }

        auto& image = blend ? _blendPalette : _remapPalette;
        auto& hasShaderLayout = blend ? _blendPaletteHasShaderLayout : _remapPaletteHasShaderLayout;

        const VkBufferImageCopy copy = {
            .bufferOffset = allocation.offset,
            .bufferRowLength = 256,
            .bufferImageHeight = 256,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { 256, 256, 1 },
        };
        RecordImageUpload(commandBuffer, allocation, image, hasShaderLayout, copy);
    }

    void IndexedResources::RecordInitialIndices(
        VkCommandBuffer commandBuffer, uint32_t frameIndex, const UploadAllocation& allocation)
    {
        if (frameIndex >= _frameCount)
            throw std::out_of_range("Initial indexed canvas slot is out of range");
        const auto extent = _indexedCanvases[frameIndex].GetExtent();
        const VkBufferImageCopy copy = {
            .bufferOffset = allocation.offset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = extent,
        };
        RecordImageUpload(commandBuffer, allocation, _indexedCanvases[frameIndex], _canvasHasShaderLayout[frameIndex], copy);
    }

    void IndexedResources::RecordCanvasClear(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint8_t paletteIndex)
    {
        if (frameIndex >= _frameCount)
        {
            throw std::out_of_range("Vulkan indexed canvas frame index is out of range");
        }

        auto& canvas = _indexedCanvases[frameIndex];
        constexpr auto range = ImageRange();
        RecordImageBarrier(
            commandBuffer, canvas.GetImage(),
            _canvasHasShaderLayout[frameIndex] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
            _canvasHasShaderLayout[frameIndex] ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, _canvasHasShaderLayout[frameIndex] ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT);

        const VkClearColorValue clear = { .uint32 = { paletteIndex, 0, 0, 0 } };
        vkCmdClearColorImage(commandBuffer, canvas.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

        RecordImageBarrier(
            commandBuffer, canvas.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            range, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        _canvasHasShaderLayout[frameIndex] = true;
    }

    void IndexedResources::RecordCanvasAndDepthClear(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint8_t paletteIndex)
    {
        RecordCanvasClear(commandBuffer, frameIndex, paletteIndex);

        RecordDepthClear(commandBuffer, frameIndex);
    }

    void IndexedResources::RecordDepthClear(VkCommandBuffer commandBuffer, uint32_t frameIndex)
    {
        if (frameIndex >= _frameCount)
        {
            throw std::out_of_range("Vulkan depth canvas frame index is out of range");
        }

        auto& depth = _depthCanvases.at(frameIndex);
        constexpr auto range = ImageRange(VK_IMAGE_ASPECT_DEPTH_BIT);
        RecordImageBarrier(
            commandBuffer, depth.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        constexpr VkClearDepthStencilValue clear = { 1.0f, 0 };
        vkCmdClearDepthStencilImage(commandBuffer, depth.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        RecordImageBarrier(
            commandBuffer, depth.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }

    void IndexedResources::CreateCanvases(Gpu::Extent logicalExtent)
    {
        _canvasHasShaderLayout.fill(false);
        _lightMapHasShaderLayout.fill(false);
        _lightAccumulatorHasShaderLayout.fill(false);
        const VkExtent3D extent = { std::max(logicalExtent.width, 1u), std::max(logicalExtent.height, 1u), 1 };
        const auto initialise = [&](Image& image, VkFormat format, VkImageUsageFlags usage,
                                    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
            image.Initialise(_physicalDevice, _device, extent, 1, format, usage, aspect);
        };
        for (uint32_t i = 0; i < _frameCount; i++)
        {
            initialise(
                _indexedCanvases[i], VK_FORMAT_R8_UINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            initialise(
                _depthCanvases[i], VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT);
            initialise(
                _compositeCanvases[i], VK_FORMAT_R8_UINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            initialise(
                _transparentCanvases[i], VK_FORMAT_R16_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            initialise(_lightMaps[i], VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            if (_hasLightAccumulators)
            {
                initialise(
                    _lightAccumulators[i], VK_FORMAT_R32_UINT,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            }
            for (auto& depth : _transparentDepthCanvases[i])
            {
                initialise(
                    depth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
            }
        }
    }

    void IndexedResources::DestroyCanvases()
    {
        _canvasHasShaderLayout.fill(false);
        _lightMapHasShaderLayout.fill(false);
        _lightAccumulatorHasShaderLayout.fill(false);
        DisposeImages(_indexedCanvases);
        DisposeImages(_depthCanvases);
        DisposeImages(_compositeCanvases);
        DisposeImages(_transparentCanvases);
        DisposeImages(_lightMaps);
        DisposeImages(_lightAccumulators);
        DisposeImages(_transparentDepthCanvases);
    }

    void RecordImageBarrier(
        VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkImageSubresourceRange range, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage,
        VkAccessFlags sourceAccess, VkAccessFlags destinationAccess)
    {
        const VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = sourceAccess,
            .dstAccessMask = destinationAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
