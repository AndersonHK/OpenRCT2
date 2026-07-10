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

    #include <algorithm>
    #include <stdexcept>
    #include <string>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        [[noreturn]] void ThrowVk(const char* operation, VkResult result)
        {
            throw std::runtime_error(std::string(operation) + " failed with Vulkan result " + std::to_string(result));
        }

        void CheckVk(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                ThrowVk(operation, result);
            }
        }

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
    } // namespace

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
            _format = std::exchange(other._format, VK_FORMAT_UNDEFINED);
            _extent = std::exchange(other._extent, VkExtent3D{});
            _layers = std::exchange(other._layers, 0);
        }
        return *this;
    }

    void Image::Initialise(
        VkPhysicalDevice physicalDevice, VkDevice device, VkExtent3D extent, uint32_t layers, VkFormat format,
        VkImageUsageFlags usage, VkImageAspectFlags aspect)
    {
        Dispose();
        _device = device;
        _format = format;
        _extent = extent;
        _layers = layers;

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
            .viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
            },
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
            if (_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(_device, _view, nullptr);
            }
            if (_image != VK_NULL_HANDLE)
            {
                vkDestroyImage(_device, _image, nullptr);
            }
            if (_memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(_device, _memory, nullptr);
            }
        }
        _device = VK_NULL_HANDLE;
        _image = VK_NULL_HANDLE;
        _memory = VK_NULL_HANDLE;
        _view = VK_NULL_HANDLE;
        _format = VK_FORMAT_UNDEFINED;
        _extent = {};
        _layers = 0;
    }

    IndexedResources::~IndexedResources()
    {
        Dispose();
    }

    void IndexedResources::Initialise(const Device& device, Gpu::Extent logicalExtent)
    {
        Dispose();
        _physicalDevice = device.GetPhysicalDevice();
        _device = device.GetDevice();

        _spriteAtlas.Initialise(
            _physicalDevice, _device, { Gpu::kAtlasDimension, Gpu::kAtlasDimension, 1 }, kSpriteAtlasLayers,
            VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        _palette.Initialise(
            _physicalDevice, _device, { 256, 1, 1 }, 1, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

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
        if (_nearestSampler != VK_NULL_HANDLE && _device != VK_NULL_HANDLE)
        {
            vkDestroySampler(_device, _nearestSampler, nullptr);
        }
        _nearestSampler = VK_NULL_HANDLE;
        _palette.Dispose();
        _spriteAtlas.Dispose();
        _physicalDevice = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE;
        _atlasHasShaderLayout = false;
        _paletteHasShaderLayout = false;
        _canvasHasShaderLayout.fill(false);
    }

    void IndexedResources::Resize(Gpu::Extent logicalExtent)
    {
        if (_device == VK_NULL_HANDLE)
        {
            return;
        }
        CheckVk(vkDeviceWaitIdle(_device), "vkDeviceWaitIdle(indexed resize)");
        DestroyCanvases();
        CreateCanvases(logicalExtent);
    }

    void IndexedResources::BeginAtlasUploads(VkCommandBuffer commandBuffer)
    {
        const VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = kSpriteAtlasLayers,
        };
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
        if (atlasLayer >= kSpriteAtlasLayers || destinationBounds.x < 0 || destinationBounds.y < 0
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
    }

    void IndexedResources::EndAtlasUploads(VkCommandBuffer commandBuffer)
    {
        const VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = kSpriteAtlasLayers,
        };
        RecordImageBarrier(
            commandBuffer, _spriteAtlas.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        _atlasHasShaderLayout = true;
    }

    void IndexedResources::RecordPaletteUpload(VkCommandBuffer commandBuffer, const UploadAllocation& allocation)
    {
        constexpr VkDeviceSize kPaletteBytes = 256 * 4;
        if (allocation.size < kPaletteBytes)
        {
            throw std::invalid_argument("Vulkan palette upload allocation is too small");
        }

        const VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        RecordImageBarrier(
            commandBuffer, _palette.GetImage(),
            _paletteHasShaderLayout ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
            _paletteHasShaderLayout ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, _paletteHasShaderLayout ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT);

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
        vkCmdCopyBufferToImage(
            commandBuffer, allocation.buffer, _palette.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        RecordImageBarrier(
            commandBuffer, _palette.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        _paletteHasShaderLayout = true;
    }

    void IndexedResources::RecordCanvasClear(
        VkCommandBuffer commandBuffer, uint32_t frameIndex, uint8_t paletteIndex)
    {
        if (frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan indexed canvas frame index is out of range");
        }

        auto& canvas = _indexedCanvases[frameIndex];
        const VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        RecordImageBarrier(
            commandBuffer, canvas.GetImage(),
            _canvasHasShaderLayout[frameIndex] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range,
            _canvasHasShaderLayout[frameIndex] ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, _canvasHasShaderLayout[frameIndex] ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT);

        const VkClearColorValue clear = { .uint32 = { paletteIndex, 0, 0, 0 } };
        vkCmdClearColorImage(
            commandBuffer, canvas.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

        RecordImageBarrier(
            commandBuffer, canvas.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        _canvasHasShaderLayout[frameIndex] = true;
    }

    void IndexedResources::CreateCanvases(Gpu::Extent logicalExtent)
    {
        _canvasHasShaderLayout.fill(false);
        const VkExtent3D extent = { std::max(logicalExtent.width, 1u), std::max(logicalExtent.height, 1u), 1 };
        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            _indexedCanvases[i].Initialise(
                _physicalDevice, _device, extent, 1, VK_FORMAT_R8_UINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            _depthCanvases[i].Initialise(
                _physicalDevice, _device, extent, 1, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT);
        }
    }

    void IndexedResources::DestroyCanvases()
    {
        _canvasHasShaderLayout.fill(false);
        for (auto& image : _indexedCanvases)
        {
            image.Dispose();
        }
        for (auto& image : _depthCanvases)
        {
            image.Dispose();
        }
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
        vkCmdPipelineBarrier(
            commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
