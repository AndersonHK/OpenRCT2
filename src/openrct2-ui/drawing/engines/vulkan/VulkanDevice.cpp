/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanDevice.h"
    #include "VulkanPlatform.h"
    #include "VulkanSurfaceFormat.h"

    #include "../gpu/GpuAtlas.h"

    #include <algorithm>
    #include <array>
    #include <chrono>
    #include <cmath>
    #include <cstring>
    #include <limits>
    #include <openrct2/core/Console.hpp>
    #include <set>
    #include <stdexcept>
    #include <string>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        constexpr std::array<const char*, 1> kRequiredDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";

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

        bool HasExtension(std::span<const VkExtensionProperties> extensions, const char* name)
        {
            return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        }

        void AppendExtensionIfMissing(std::vector<const char*>& extensions, const char* name)
        {
            const bool present = std::any_of(extensions.begin(), extensions.end(), [name](const char* extension) {
                return std::strcmp(extension, name) == 0;
            });
            if (!present)
            {
                extensions.push_back(name);
            }
        }

        VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
        {
            if (alignment <= 1)
            {
                return value;
            }
            return (value + alignment - 1) & ~(alignment - 1);
        }

        struct MemoryTypeSelection
        {
            uint32_t index;
            VkMemoryPropertyFlags properties;
        };

        MemoryTypeSelection FindMemoryType(
            VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags requiredProperties,
            VkMemoryPropertyFlags preferredProperties = 0)
        {
            VkPhysicalDeviceMemoryProperties properties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
            std::optional<MemoryTypeSelection> fallback;
            for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
            {
                const auto flags = properties.memoryTypes[i].propertyFlags;
                if ((typeBits & (1u << i)) != 0 && (flags & requiredProperties) == requiredProperties)
                {
                    const MemoryTypeSelection candidate{ i, flags };
                    if ((flags & preferredProperties) == preferredProperties)
                    {
                        return candidate;
                    }
                    if (!fallback.has_value())
                    {
                        fallback = candidate;
                    }
                }
            }
            if (fallback.has_value())
            {
                return *fallback;
            }
            throw std::runtime_error("No Vulkan memory type satisfies the upload-ring requirements");
        }

        VkDeviceSize AlignDown(VkDeviceSize value, VkDeviceSize alignment)
        {
            return alignment <= 1 ? value : value & ~(alignment - 1);
        }
    } // namespace

    UploadRing::~UploadRing()
    {
        Dispose();
    }

    void UploadRing::Initialise(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize capacity)
    {
        Dispose();
        _device = device;
        _capacity = capacity;

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        _nonCoherentAtomSize = std::max<VkDeviceSize>(1, deviceProperties.limits.nonCoherentAtomSize);

        const VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = capacity,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        CheckVk(vkCreateBuffer(_device, &bufferInfo, nullptr, &_buffer), "vkCreateBuffer(upload ring)");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(_device, _buffer, &requirements);
        const auto memoryType = FindMemoryType(
            physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        _hostCoherent = (memoryType.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        _allocationSize = requirements.size;
        const VkMemoryAllocateInfo allocationInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = _allocationSize,
            .memoryTypeIndex = memoryType.index,
        };
        CheckVk(vkAllocateMemory(_device, &allocationInfo, nullptr, &_memory), "vkAllocateMemory(upload ring)");
        CheckVk(vkBindBufferMemory(_device, _buffer, _memory, 0), "vkBindBufferMemory(upload ring)");

        void* mapped = nullptr;
        CheckVk(vkMapMemory(_device, _memory, 0, _allocationSize, 0, &mapped), "vkMapMemory(upload ring)");
        _mapped = static_cast<std::byte*>(mapped);
    }

    void UploadRing::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            if (_mapped != nullptr)
            {
                vkUnmapMemory(_device, _memory);
            }
            if (_buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(_device, _buffer, nullptr);
            }
            if (_memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(_device, _memory, nullptr);
            }
        }

        _device = VK_NULL_HANDLE;
        _buffer = VK_NULL_HANDLE;
        _memory = VK_NULL_HANDLE;
        _mapped = nullptr;
        _capacity = 0;
        _allocationSize = 0;
        _nonCoherentAtomSize = 1;
        _cursor = 0;
        _hostCoherent = false;
    }

    void UploadRing::Reset() noexcept
    {
        _cursor = 0;
    }

    void UploadRing::FlushWritten()
    {
        if (_hostCoherent || _cursor == 0)
        {
            return;
        }

        const VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = _memory,
            .offset = 0,
            .size = AlignUp(_cursor, _nonCoherentAtomSize) >= _allocationSize
                ? VK_WHOLE_SIZE
                : AlignUp(_cursor, _nonCoherentAtomSize),
        };
        CheckVk(vkFlushMappedMemoryRanges(_device, 1, &range), "vkFlushMappedMemoryRanges(upload ring)");
    }

    void UploadRing::Invalidate(VkDeviceSize offset, VkDeviceSize size)
    {
        if (_hostCoherent || size == 0)
        {
            return;
        }
        if (offset > _capacity || size > _capacity - offset)
        {
            throw std::out_of_range("Vulkan upload-ring invalidation is outside the mapped allocation");
        }

        const auto alignedOffset = AlignDown(offset, _nonCoherentAtomSize);
        const auto alignedEnd = AlignUp(offset + size, _nonCoherentAtomSize);
        const VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = _memory,
            .offset = alignedOffset,
            .size = alignedEnd >= _allocationSize ? VK_WHOLE_SIZE : alignedEnd - alignedOffset,
        };
        CheckVk(vkInvalidateMappedMemoryRanges(_device, 1, &range), "vkInvalidateMappedMemoryRanges(upload ring)");
    }

    UploadAllocation UploadRing::Allocate(VkDeviceSize size, VkDeviceSize alignment)
    {
        const VkDeviceSize offset = AlignUp(_cursor, alignment);
        if (offset > _capacity || size > _capacity - offset)
        {
            return {};
        }

        _cursor = offset + size;
        return { _buffer, offset, size, _mapped + offset };
    }

    Device::~Device()
    {
        Dispose();
    }

    void Device::Initialise(
        SDL_Window* window, VkExtent2D drawableExtent, bool vsync, VkDeviceSize uploadRingCapacity, bool preferHdr10,
        float hdrPaperWhiteNits)
    {
        if (_instance != VK_NULL_HANDLE)
        {
            throw std::logic_error("Vulkan device is already initialised");
        }
        if (drawableExtent.width == 0 || drawableExtent.height == 0)
        {
            throw std::invalid_argument("Vulkan device requires a non-zero initial drawable extent");
        }

        _window = window;
        _drawableExtent = drawableExtent;
        _vsync = vsync;
        _preferHdr10 = preferHdr10;
        _hdrPaperWhiteNits = std::isfinite(hdrPaperWhiteNits) ? std::clamp(hdrPaperWhiteNits, 80.0f, 1000.0f) : 203.0f;
        _uploadRingCapacity = std::max<VkDeviceSize>(uploadRingCapacity, 1024 * 1024);
        Platform::LoadVulkanLibrary();
        _loaderLoaded = true;

        try
        {
            CreateInstance();
            CreateSurface();
            SelectPhysicalDevice();
            CreateLogicalDevice();
            CreateCommandResources();
            if (!CreateSwapchain())
            {
                throw std::runtime_error("Vulkan surface has no drawable extent during initialisation");
            }
        }
        catch (...)
        {
            Dispose();
            throw;
        }
    }

    void Device::Dispose()
    {
        if (_device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(_device);
        }

        DestroySwapchain();
        if (_device != VK_NULL_HANDLE)
        {
            for (auto& frame : _frames)
            {
                frame.upload.Dispose();
                if (frame.timestampQueryPool != VK_NULL_HANDLE)
                {
                    vkDestroyQueryPool(_device, frame.timestampQueryPool, nullptr);
                }
                if (frame.imageAvailable != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(_device, frame.imageAvailable, nullptr);
                }
                if (frame.renderFinished != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(_device, frame.renderFinished, nullptr);
                }
                if (frame.available != VK_NULL_HANDLE)
                {
                    vkDestroyFence(_device, frame.available, nullptr);
                }
                frame.commandBuffer = VK_NULL_HANDLE;
                frame.imageAvailable = VK_NULL_HANDLE;
                frame.renderFinished = VK_NULL_HANDLE;
                frame.available = VK_NULL_HANDLE;
                frame.timestampQueryPool = VK_NULL_HANDLE;
                frame.timestampPending = false;
                frame.completedGpuTimings.reset();
            }
            if (_commandPool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(_device, _commandPool, nullptr);
            }
            if (_pipelineCache != VK_NULL_HANDLE)
            {
                vkDestroyPipelineCache(_device, _pipelineCache, nullptr);
            }
            vkDestroyDevice(_device, nullptr);
        }
        if (_surface != VK_NULL_HANDLE && _instance != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(_instance, _surface, nullptr);
        }
        if (_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(_instance, nullptr);
        }
        if (_loaderLoaded)
        {
            Platform::UnloadVulkanLibrary();
        }

        _window = nullptr;
        _loaderLoaded = false;
        _instance = VK_NULL_HANDLE;
        _surface = VK_NULL_HANDLE;
        _physicalDevice = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE;
        _graphicsQueue = VK_NULL_HANDLE;
        _presentQueue = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _queueFamilies = {};
        _commandPool = VK_NULL_HANDLE;
        _currentFrame = 0;
        _swapchainGeneration = 0;
        _swapchainInvalid = false;
        _drawableExtent = {};
        _uploadRingCapacity = kDefaultUploadRingSize;
        _timestampValidBits = 0;
        _timestampPeriodNanoseconds = 0.0;
        _gpuTimestampsSupported = false;
        _preferHdr10 = false;
        _hdrPaperWhiteNits = 203.0f;
        _hdr10Available = false;
        _hdr10Active = false;
        _hdrMetadataAvailable = false;
#ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        _setHdrMetadata = nullptr;
#endif
    }

    void Device::WaitIdle() const
    {
        const std::lock_guard lock(_hostMutex);
        if (_device != VK_NULL_HANDLE)
        {
            CheckVk(vkDeviceWaitIdle(_device), "vkDeviceWaitIdle");
        }
    }

    bool Device::IsFrameComplete(uint32_t frameIndex) const
    {
        const std::lock_guard lock(_hostMutex);
        if (frameIndex >= kFramesInFlight || _device == VK_NULL_HANDLE)
        {
            return false;
        }
        const auto result = vkGetFenceStatus(_device, _frames[frameIndex].available);
        if (result == VK_SUCCESS)
        {
            return true;
        }
        if (result == VK_NOT_READY)
        {
            return false;
        }
        ThrowVk("vkGetFenceStatus", result);
    }

    void Device::WaitForFrame(uint32_t frameIndex) const
    {
        const std::lock_guard lock(_hostMutex);
        if (frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan frame index is out of range");
        }
        CheckVk(vkWaitForFences(_device, 1, &_frames[frameIndex].available, VK_TRUE, UINT64_MAX), "wait for frame");
    }

    void Device::SetVSync(bool enabled)
    {
        if (_vsync != enabled)
        {
            _vsync = enabled;
            RequestSwapchainRecreate();
        }
    }

    void Device::SetDrawableExtent(VkExtent2D drawableExtent) noexcept
    {
        if (_drawableExtent.width != drawableExtent.width || _drawableExtent.height != drawableExtent.height)
        {
            _drawableExtent = drawableExtent;
            RequestSwapchainRecreate();
        }
    }

    void Device::RequestSwapchainRecreate() noexcept
    {
        _swapchainInvalid = true;
    }

    std::optional<FrameToken> Device::BeginFrame(bool waitForAvailability)
    {
        const std::lock_guard lock(_hostMutex);
        if (_swapchainInvalid)
        {
            return std::nullopt;
        }

        auto& frame = _frames[_currentFrame];
        if (waitForAvailability)
        {
            CheckVk(vkWaitForFences(_device, 1, &frame.available, VK_TRUE, UINT64_MAX), "vkWaitForFences(frame)");
        }
        else
        {
            const auto fenceStatus = vkGetFenceStatus(_device, frame.available);
            if (fenceStatus == VK_NOT_READY)
            {
                return std::nullopt;
            }
            CheckVk(fenceStatus, "vkGetFenceStatus(frame)");
        }
        HarvestGpuTimestamps(frame);

        uint32_t imageIndex = 0;
        const auto timeout = waitForAvailability ? UINT64_MAX : uint64_t{ 0 };
        const auto acquireResult =
            vkAcquireNextImageKHR(_device, _swapchain, timeout, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_NOT_READY || acquireResult == VK_TIMEOUT)
        {
            return std::nullopt;
        }
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            _swapchainInvalid = true;
            return std::nullopt;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        {
            ThrowVk("vkAcquireNextImageKHR", acquireResult);
        }
        if (acquireResult == VK_SUBOPTIMAL_KHR)
        {
            _swapchainInvalid = true;
        }

        CheckVk(vkResetFences(_device, 1, &frame.available), "vkResetFences(frame)");
        CheckVk(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer(frame)");
        frame.upload.Reset();

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        CheckVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer(frame)");
        if (frame.timestampQueryPool != VK_NULL_HANDLE)
        {
            vkCmdResetQueryPool(
                frame.commandBuffer, frame.timestampQueryPool, 0, static_cast<uint32_t>(kGpuTimestampCount));
            vkCmdWriteTimestamp(
                frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.timestampQueryPool,
                static_cast<uint32_t>(GpuTimestampPoint::frameStart));
        }

        return FrameToken{
            .commandBuffer = frame.commandBuffer,
            .extent = _swapchainExtent,
            .imageIndex = imageIndex,
            .frameIndex = _currentFrame,
            .upload = &frame.upload,
        };
    }

    double Device::EndFrame(const FrameToken& token)
    {
        const std::lock_guard lock(_hostMutex);
        if (token.frameIndex != _currentFrame)
        {
            throw std::logic_error("Vulkan frame token does not belong to the active frame");
        }

        auto& frame = _frames[_currentFrame];
        if (frame.timestampQueryPool != VK_NULL_HANDLE)
        {
            vkCmdWriteTimestamp(
                frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.timestampQueryPool,
                static_cast<uint32_t>(GpuTimestampPoint::frameComplete));
        }
        CheckVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer(frame)");
        frame.upload.FlushWritten();

        // The command executor may first touch the swapchain image in a
        // transfer, compute, or colour-attachment pass.
        constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.imageAvailable,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &frame.commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &frame.renderFinished,
        };
        CheckVk(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, frame.available), "vkQueueSubmit(frame)");
        frame.timestampPending = frame.timestampQueryPool != VK_NULL_HANDLE;

        const VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.renderFinished,
            .swapchainCount = 1,
            .pSwapchains = &_swapchain,
            .pImageIndices = &token.imageIndex,
        };
        const auto presentStart = std::chrono::steady_clock::now();
        const auto presentResult = vkQueuePresentKHR(_presentQueue, &presentInfo);
        const auto presentCallMicroseconds =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - presentStart).count();
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        {
            _swapchainInvalid = true;
        }
        else if (presentResult != VK_SUCCESS)
        {
            ThrowVk("vkQueuePresentKHR", presentResult);
        }

        _currentFrame = (_currentFrame + 1) % kFramesInFlight;
        return presentCallMicroseconds;
    }

    void Device::AbandonFrame(const FrameToken& token)
    {
        const std::lock_guard lock(_hostMutex);
        if (token.frameIndex != _currentFrame)
        {
            throw std::logic_error("Vulkan frame token does not belong to the active frame");
        }

        auto& frame = _frames[_currentFrame];
        CheckVk(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer(abandoned frame)");
        frame.timestampPending = false;

        // Acquiring a swapchain image signals this binary semaphore. Consume
        // it before the frame slot can be reused, but do not present an image
        // whose command buffer may be incomplete. Swapchain recreation below
        // releases the acquired image.
        constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.imageAvailable,
            .pWaitDstStageMask = &waitStage,
        };
        CheckVk(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, frame.available), "vkQueueSubmit(abandoned frame)");

        _swapchainInvalid = true;
        _currentFrame = (_currentFrame + 1) % kFramesInFlight;
    }

    void Device::RecordGpuTimestamp(const FrameToken& token, GpuTimestampPoint point) const
    {
        const std::lock_guard lock(_hostMutex);
        if (token.frameIndex != _currentFrame || point == GpuTimestampPoint::frameStart
            || point == GpuTimestampPoint::frameComplete || point == GpuTimestampPoint::count)
        {
            throw std::logic_error("Invalid Vulkan GPU timestamp point for the active frame");
        }
        const auto queryPool = _frames[token.frameIndex].timestampQueryPool;
        if (queryPool != VK_NULL_HANDLE)
        {
            vkCmdWriteTimestamp(
                token.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, static_cast<uint32_t>(point));
        }
    }

    std::optional<GpuTimestampDurations> Device::TakeCompletedGpuTimings(uint32_t frameIndex)
    {
        const std::lock_guard lock(_hostMutex);
        if (frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan frame index is out of range");
        }
        auto& frame = _frames[frameIndex];
        if (frame.timestampPending)
        {
            const auto fenceStatus = vkGetFenceStatus(_device, frame.available);
            if (fenceStatus == VK_SUCCESS)
            {
                HarvestGpuTimestamps(frame);
            }
            else if (fenceStatus != VK_NOT_READY)
            {
                ThrowVk("vkGetFenceStatus(frame timestamps)", fenceStatus);
            }
        }
        return std::exchange(frame.completedGpuTimings, std::nullopt);
    }

    void Device::InvalidateUpload(uint32_t frameIndex, VkDeviceSize offset, VkDeviceSize size)
    {
        const std::lock_guard lock(_hostMutex);
        if (frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan frame index is outside the upload-ring set");
        }
        _frames[frameIndex].upload.Invalidate(offset, size);
    }

    void Device::ReadbackImage(
        uint32_t frameIndex, VkImage image, VkImageLayout layout, VkExtent2D extent,
        std::span<std::byte> destination)
    {
        const std::lock_guard lock(_hostMutex);
        if (frameIndex >= kFramesInFlight || image == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0)
        {
            throw std::invalid_argument("Invalid Vulkan synchronous readback source");
        }
        const uint64_t byteSize64 = static_cast<uint64_t>(extent.width) * extent.height;
        if (byteSize64 > destination.size())
        {
            throw std::invalid_argument("Vulkan synchronous readback destination is too small");
        }

        auto& frame = _frames[frameIndex];
        CheckVk(vkWaitForFences(_device, 1, &frame.available, VK_TRUE, UINT64_MAX), "wait for readback frame");
        CheckVk(vkResetCommandBuffer(frame.commandBuffer, 0), "reset readback command buffer");
        frame.upload.Reset();
        const auto allocation = frame.upload.Allocate(byteSize64, alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for synchronous indexed readback");
        }

        const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        CheckVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "begin readback command buffer");

        const VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        const VkImageMemoryBarrier toTransfer = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = layout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &toTransfer);

        const VkBufferImageCopy copy = {
            .bufferOffset = allocation.offset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { extent.width, extent.height, 1 },
        };
        vkCmdCopyImageToBuffer(
            frame.commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, allocation.buffer, 1, &copy);

        const VkBufferMemoryBarrier hostBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = allocation.buffer,
            .offset = allocation.offset,
            .size = byteSize64,
        };
        const VkImageMemoryBarrier restoreLayout = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &hostBarrier, 1,
            &restoreLayout);
        CheckVk(vkEndCommandBuffer(frame.commandBuffer), "end readback command buffer");

        CheckVk(vkResetFences(_device, 1, &frame.available), "reset readback fence");
        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &frame.commandBuffer,
        };
        CheckVk(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, frame.available), "submit readback command buffer");
        CheckVk(vkWaitForFences(_device, 1, &frame.available, VK_TRUE, UINT64_MAX), "wait for synchronous readback");
        frame.upload.Invalidate(allocation.offset, byteSize64);
        std::memcpy(destination.data(), allocation.data, static_cast<size_t>(byteSize64));
    }

    void Device::CreateInstance()
    {
        auto extensions = GetInstanceExtensions();
        VkInstanceCreateFlags flags = 0;

        uint32_t extensionCount = 0;
        CheckVk(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr), "enumerate instance extensions");
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        CheckVk(
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()),
            "enumerate instance extensions");

    #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (HasExtension(availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            AppendExtensionIfMissing(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    #endif
    #ifdef VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME
        if (HasExtension(availableExtensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
        {
            AppendExtensionIfMissing(extensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
        }
    #endif

        const VkApplicationInfo applicationInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "OpenRCT2",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "OpenRCT2 GPU renderer",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_1,
        };
        const VkInstanceCreateInfo instanceInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = flags,
            .pApplicationInfo = &applicationInfo,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };
        CheckVk(vkCreateInstance(&instanceInfo, nullptr, &_instance), "vkCreateInstance");
    }

    void Device::CreateSurface()
    {
        _surface = Platform::CreateSurface(_window, _instance);
    }

    void Device::SelectPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        CheckVk(vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices(count)");
        if (deviceCount == 0)
        {
            throw std::runtime_error("No Vulkan physical devices are available");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        CheckVk(vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");

        int32_t bestScore = -1;
        for (const auto device : devices)
        {
            const int32_t score = ScorePhysicalDevice(device);
            if (score > bestScore)
            {
                bestScore = score;
                _physicalDevice = device;
            }
        }
        if (_physicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error(
                "No Vulkan 1.1 device satisfies the renderer's queue, surface, format, and atlas requirements");
        }
        _queueFamilies = FindQueueFamilies(_physicalDevice);
    }

    void Device::CreateLogicalDevice()
    {
        const std::set<uint32_t> uniqueFamilies = { _queueFamilies.graphics.value(), _queueFamilies.present.value() };
        constexpr float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        queueInfos.reserve(uniqueFamilies.size());
        for (const auto family : uniqueFamilies)
        {
            queueInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = family,
                .queueCount = 1,
                .pQueuePriorities = &priority,
            });
        }

        const auto extensions = GetDeviceExtensions(_physicalDevice);
        const VkPhysicalDeviceFeatures features{};
        const VkDeviceCreateInfo deviceInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size()),
            .pQueueCreateInfos = queueInfos.data(),
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures = &features,
        };
        CheckVk(vkCreateDevice(_physicalDevice, &deviceInfo, nullptr, &_device), "vkCreateDevice");
#ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        const bool enabledHdrMetadata = std::any_of(extensions.begin(), extensions.end(), [](const char* extension) {
            return std::strcmp(extension, VK_EXT_HDR_METADATA_EXTENSION_NAME) == 0;
        });
        if (enabledHdrMetadata)
        {
            _setHdrMetadata = reinterpret_cast<PFN_vkSetHdrMetadataEXT>(
                vkGetDeviceProcAddr(_device, "vkSetHdrMetadataEXT"));
            _hdrMetadataAvailable = _setHdrMetadata != nullptr;
        }
#endif
        vkGetDeviceQueue(_device, _queueFamilies.graphics.value(), 0, &_graphicsQueue);
        vkGetDeviceQueue(_device, _queueFamilies.present.value(), 0, &_presentQueue);

        const VkPipelineCacheCreateInfo pipelineCacheInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        CheckVk(vkCreatePipelineCache(_device, &pipelineCacheInfo, nullptr, &_pipelineCache), "vkCreatePipelineCache");
    }

    void Device::CreateCommandResources()
    {
        const VkCommandPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = _queueFamilies.graphics.value(),
        };
        CheckVk(vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool), "vkCreateCommandPool");

        std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
        const VkCommandBufferAllocateInfo commandInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = _commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = kFramesInFlight,
        };
        CheckVk(vkAllocateCommandBuffers(_device, &commandInfo, commandBuffers.data()), "vkAllocateCommandBuffers");

        const VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        const VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            auto& frame = _frames[i];
            frame.commandBuffer = commandBuffers[i];
            CheckVk(vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &frame.imageAvailable), "create image semaphore");
            CheckVk(vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &frame.renderFinished), "create render semaphore");
            CheckVk(vkCreateFence(_device, &fenceInfo, nullptr, &frame.available), "create frame fence");
            frame.upload.Initialise(_physicalDevice, _device, _uploadRingCapacity);
        }

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueFamilyCount, queueFamilies.data());
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(_physicalDevice, &deviceProperties);
        _timestampValidBits = queueFamilies[_queueFamilies.graphics.value()].timestampValidBits;
        _timestampPeriodNanoseconds = deviceProperties.limits.timestampPeriod;
        _gpuTimestampsSupported = _timestampValidBits != 0 && _timestampPeriodNanoseconds > 0.0;
        if (_gpuTimestampsSupported)
        {
            const VkQueryPoolCreateInfo queryPoolInfo = {
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = static_cast<uint32_t>(kGpuTimestampCount),
            };
            for (auto& frame : _frames)
            {
                const auto result = vkCreateQueryPool(_device, &queryPoolInfo, nullptr, &frame.timestampQueryPool);
                if (result != VK_SUCCESS)
                {
                    _gpuTimestampsSupported = false;
                    break;
                }
            }
            if (!_gpuTimestampsSupported)
            {
                for (auto& frame : _frames)
                {
                    if (frame.timestampQueryPool != VK_NULL_HANDLE)
                    {
                        vkDestroyQueryPool(_device, frame.timestampQueryPool, nullptr);
                        frame.timestampQueryPool = VK_NULL_HANDLE;
                    }
                }
            }
        }
    }

    void Device::HarvestGpuTimestamps(FrameResources& frame)
    {
        if (!frame.timestampPending || frame.timestampQueryPool == VK_NULL_HANDLE)
        {
            return;
        }

        struct TimestampQueryResult
        {
            uint64_t value;
            uint64_t available;
        };
        std::array<TimestampQueryResult, kGpuTimestampCount> results{};
        const auto result = vkGetQueryPoolResults(
            _device, frame.timestampQueryPool, 0, static_cast<uint32_t>(results.size()), sizeof(results),
            results.data(), sizeof(TimestampQueryResult),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        frame.timestampPending = false;
        if (result == VK_NOT_READY)
        {
            return;
        }
        CheckVk(result, "vkGetQueryPoolResults(frame timestamps)");
        if (!std::all_of(results.begin(), results.end(), [](const auto& query) { return query.available != 0; }))
        {
            return;
        }
        std::array<uint64_t, kGpuTimestampCount> timestamps{};
        std::transform(
            results.begin(), results.end(), timestamps.begin(), [](const auto& query) { return query.value; });
        frame.completedGpuTimings =
            CalculateGpuTimestampDurations(timestamps, _timestampValidBits, _timestampPeriodNanoseconds);
    }

    bool Device::CreateSwapchain()
    {
        const auto support = QuerySwapchainSupport(_physicalDevice);
        const auto surfaceFormatSelection = SelectSurfaceFormat(support.formats, _preferHdr10);
        const auto surfaceFormat = surfaceFormatSelection.surfaceFormat;
        if (!IsSupportedOutputSurfaceFormat(surfaceFormatSelection))
        {
            throw std::runtime_error(
                "Vulkan surface exposes neither a supported SDR format nor an active exact HDR10 format");
        }
        _hdr10Available = surfaceFormatSelection.hdr10Available;
        const auto presentMode = ChoosePresentMode(support.presentModes);
        const auto extent = ChooseExtent(support.capabilities);
        if (extent.width == 0 || extent.height == 0)
        {
            return false;
        }
        const auto compositeAlpha = SelectStraightAlphaCompositeMode(support.capabilities.supportedCompositeAlpha);
        if (!compositeAlpha.has_value())
        {
            throw std::runtime_error("Vulkan surface has no composite-alpha mode compatible with straight-alpha output");
        }

        DestroySwapchain();

        VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if ((support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0)
        {
            imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0)
        {
            imageCount = std::min(imageCount, support.capabilities.maxImageCount);
        }

        const std::array queueFamilyIndices = { _queueFamilies.graphics.value(), _queueFamilies.present.value() };
        VkSwapchainCreateInfoKHR swapchainInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = _surface,
            .minImageCount = imageCount,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = imageUsage,
            .preTransform = support.capabilities.currentTransform,
            .compositeAlpha = *compositeAlpha,
            .presentMode = presentMode,
            .clipped = VK_TRUE,
        };
        if (_queueFamilies.graphics != _queueFamilies.present)
        {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
            swapchainInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        }
        else
        {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        CheckVk(vkCreateSwapchainKHR(_device, &swapchainInfo, nullptr, &_swapchain), "vkCreateSwapchainKHR");
        _swapchainFormat = surfaceFormat.format;
        _hdr10Active = surfaceFormatSelection.hdr10Active;
        _swapchainExtent = extent;

        CheckVk(vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, nullptr), "get swapchain image count");
        std::vector<VkImage> swapchainImages(imageCount);
        CheckVk(vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, swapchainImages.data()), "get swapchain images");

        _swapchainImageViews.resize(imageCount);
        for (size_t i = 0; i < swapchainImages.size(); i++)
        {
            const VkImageViewCreateInfo viewInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapchainImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = _swapchainFormat,
                .components = {
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };
            CheckVk(vkCreateImageView(_device, &viewInfo, nullptr, &_swapchainImageViews[i]), "create swapchain view");
        }
        PublishHdrMetadata();
        _swapchainGeneration++;
        _swapchainInvalid = false;
        return true;
    }

    void Device::DestroySwapchain()
    {
        if (_device != VK_NULL_HANDLE)
        {
            for (const auto imageView : _swapchainImageViews)
            {
                vkDestroyImageView(_device, imageView, nullptr);
            }
            if (_swapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(_device, _swapchain, nullptr);
            }
        }
        _swapchainImageViews.clear();
        _swapchain = VK_NULL_HANDLE;
        _swapchainFormat = VK_FORMAT_UNDEFINED;
        _hdr10Active = false;
        _swapchainExtent = {};
    }

    void Device::PublishHdrMetadata() const noexcept
    {
#ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        if (!_hdr10Active || !_hdrMetadataAvailable || _setHdrMetadata == nullptr || _swapchain == VK_NULL_HANDLE)
        {
            return;
        }

        // The HDR palette pass maps the brightest indexed colour to paper white and emits no separate highlight range yet.
        // Describe that content truthfully; the display remains responsible for adapting it to its own peak capability.
        const VkHdrMetadataEXT metadata = {
            .sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT,
            .displayPrimaryRed = { 0.708f, 0.292f },
            .displayPrimaryGreen = { 0.170f, 0.797f },
            .displayPrimaryBlue = { 0.131f, 0.046f },
            .whitePoint = { 0.3127f, 0.3290f },
            .maxLuminance = _hdrPaperWhiteNits,
            .minLuminance = 0.0f,
            .maxContentLightLevel = _hdrPaperWhiteNits,
            .maxFrameAverageLightLevel = _hdrPaperWhiteNits,
        };
        _setHdrMetadata(_device, 1, &_swapchain, &metadata);
#endif
    }

    bool Device::RecreateSwapchain()
    {
        if (_drawableExtent.width == 0 || _drawableExtent.height == 0)
        {
            return false;
        }

        WaitIdle();
        return CreateSwapchain();
    }

    Device::QueueFamilies Device::FindQueueFamilies(VkPhysicalDevice device) const
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

        QueueFamilies result;
        for (uint32_t i = 0; i < count; i++)
        {
            if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                result.graphics = i;
            }
            VkBool32 supportsPresent = VK_FALSE;
            CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &supportsPresent), "query present support");
            if (supportsPresent == VK_TRUE)
            {
                result.present = i;
            }
            if (result.IsComplete())
            {
                break;
            }
        }
        return result;
    }

    Device::SwapchainSupport Device::QuerySwapchainSupport(VkPhysicalDevice device) const
    {
        SwapchainSupport result;
        CheckVk(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &result.capabilities),
            "query surface capabilities");

        uint32_t formatCount = 0;
        CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr), "query surface formats");
        result.formats.resize(formatCount);
        if (formatCount > 0)
        {
            CheckVk(
                vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, result.formats.data()),
                "query surface formats");
        }

        uint32_t modeCount = 0;
        CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &modeCount, nullptr), "query present modes");
        result.presentModes.resize(modeCount);
        if (modeCount > 0)
        {
            CheckVk(
                vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &modeCount, result.presentModes.data()),
                "query present modes");
        }
        return result;
    }

    bool Device::SupportsDeviceExtensions(VkPhysicalDevice device) const
    {
        uint32_t count = 0;
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "enumerate device extensions");
        std::vector<VkExtensionProperties> available(count);
        CheckVk(
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data()), "enumerate device extensions");
        return std::all_of(kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end(), [&](const char* required) {
            return HasExtension(available, required);
        });
    }

    int32_t Device::ScorePhysicalDevice(VkPhysicalDevice device) const
    {
        const auto families = FindQueueFamilies(device);
        const auto swapchain = QuerySwapchainSupport(device);
        if (!families.IsComplete() || !SupportsDeviceExtensions(device) || !swapchain.IsUsable()
            || (swapchain.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0
            || !SupportsRequiredRenderingFormats(device))
        {
            return -1;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_1
            || properties.limits.maxImageDimension2D < static_cast<uint32_t>(Gpu::kAtlasDimension)
            || properties.limits.maxImageArrayLayers < Gpu::kAtlasLayers)
        {
            return -1;
        }
        int32_t score = static_cast<int32_t>(properties.limits.maxImageDimension2D);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score += 100000;
        }
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            score += 50000;
        }
        return score;
    }

    bool Device::SupportsRequiredRenderingFormats(VkPhysicalDevice device)
    {
        const auto supports = [device](VkFormat format, VkFormatFeatureFlags required) {
            VkFormatProperties2 properties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
            vkGetPhysicalDeviceFormatProperties2(device, format, &properties);
            return (properties.formatProperties.optimalTilingFeatures & required) == required;
        };

        constexpr VkFormatFeatureFlags transferAndSampling = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
            | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        return supports(VK_FORMAT_R8_UINT, transferAndSampling | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
            && supports(VK_FORMAT_R16_UINT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
            && supports(
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
                    | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
            && supports(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
    }

    std::vector<const char*> Device::GetInstanceExtensions() const
    {
        return Platform::GetInstanceExtensions(_window);
    }

    std::vector<const char*> Device::GetDeviceExtensions(VkPhysicalDevice device) const
    {
        std::vector<const char*> result(kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end());

        uint32_t count = 0;
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "enumerate portability extensions");
        std::vector<VkExtensionProperties> available(count);
        CheckVk(
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data()),
            "enumerate portability extensions");
        if (HasExtension(available, kPortabilitySubsetExtension))
        {
            result.push_back(kPortabilitySubsetExtension);
        }
#ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        if (HasExtension(available, VK_EXT_HDR_METADATA_EXTENSION_NAME))
        {
            result.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
        }
#endif

        return result;
    }

    VkPresentModeKHR Device::ChoosePresentMode(std::span<const VkPresentModeKHR> modes) const
    {
        if (_vsync)
        {
            return VK_PRESENT_MODE_FIFO_KHR;
        }
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
        {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
        {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D Device::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        return {
            std::clamp(_drawableExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(
                _drawableExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
