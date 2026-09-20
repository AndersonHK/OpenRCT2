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

    #include "VulkanShader.h"
    #include "VulkanSubmissionSlots.h"
    #include "VulkanSurfaceFormat.h"

    #include <algorithm>
    #include <array>
    #include <chrono>
    #include <cmath>
    #include <cstring>
    #include <limits>
    #include <openrct2-renderer/gpu/GpuAtlas.h>
    #include <openrct2-renderer/gpu/GpuBackend.h>
    #include <openrct2-renderer/gpu/GpuCommandStream.h>
    #include <openrct2/core/Console.hpp>
    #include <stdexcept>
    #include <string>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
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
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
            vkDestroyBuffer(_device, _buffer, nullptr);
            vkFreeMemory(_device, _memory, nullptr);
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
        _telemetry = nullptr;
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
            .size = AlignUp(_cursor, _nonCoherentAtomSize) >= _allocationSize ? VK_WHOLE_SIZE
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

    UploadAllocation UploadRing::Allocate(VkDeviceSize size, VkDeviceSize alignment, Drawing::UploadCategory category)
    {
        const VkDeviceSize offset = AlignUp(_cursor, alignment);
        if (offset > _capacity || size > _capacity - offset)
        {
            if (_telemetry != nullptr)
                _telemetry->Add(_telemetry->allocationFailures, 1);
            return {};
        }

        if (_telemetry != nullptr)
        {
            _telemetry->Add(_telemetry->allocatedBytes, size);
            _telemetry->Add(_telemetry->alignmentBytes, offset - _cursor);
        }
        _cursor = offset + size;
        return { _buffer, offset, size, _mapped + offset, _telemetry, category };
    }

    Device::Device(std::shared_ptr<DeviceContextOwner> owner)
        : _owner(std::move(owner))
    {
    }

    Device::~Device()
    {
        Dispose();
    }

    void Device::Initialise(
        PresentationHost& presentationHost, VkExtent2D drawableExtent, bool vsync, VkDeviceSize uploadRingCapacity,
        bool preferHdr10, float hdrPaperWhiteNits, bool enableDiagnosticCapture)
    {
        if (_instance != VK_NULL_HANDLE)
        {
            throw std::logic_error("Vulkan device is already initialised");
        }
        if (drawableExtent.width == 0 || drawableExtent.height == 0)
        {
            throw std::invalid_argument("Vulkan device requires a non-zero initial drawable extent");
        }

        _presentationHost = &presentationHost;
        _drawableExtent = drawableExtent;
        _vsync = vsync;
        _preferHdr10 = preferHdr10;
        _enableDiagnosticCapture = enableDiagnosticCapture;
        _hdrPaperWhiteNits = Gpu::NormaliseHdrPaperWhiteNits(hdrPaperWhiteNits);
        uploadRingCapacity = std::max<VkDeviceSize>(uploadRingCapacity, 1024 * 1024);

        try
        {
            _context = _owner ? _owner->AcquireWindowed(presentationHost, _surface)
                              : DeviceContext::CreateWindowed(presentationHost, _surface);
            _instance = _context->GetInstance();
            _physicalDevice = _context->GetPhysicalDevice();
            _device = _context->GetDevice();
            _pipelineCache = _context->GetPipelineCache();
            _queueFamilies = _context->GetQueueFamilies();
            _hdrMetadataAvailable = _context->HasHdrMetadata();
    #ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
            _setHdrMetadata = _context->GetSetHdrMetadata();
    #endif
            CreateCommandResources(uploadRingCapacity);
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
            _context->WaitIdle();
        }

        DestroySwapchain();
        if (_device != VK_NULL_HANDLE)
        {
            for (auto& semaphore : _imageAvailable)
            {
                vkDestroySemaphore(_device, semaphore, nullptr);
                semaphore = VK_NULL_HANDLE;
            }
        }
        _slots.reset();
        if (_instance != VK_NULL_HANDLE)
        {
            _presentationHost->DestroySurface(_instance, _surface);
        }
        _context.reset();

        _presentationHost = nullptr;
        _instance = VK_NULL_HANDLE;
        _surface = VK_NULL_HANDLE;
        _physicalDevice = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _queueFamilies = {};
        _currentFrame = 0;
        _swapchainGeneration = 0;
        _swapchainInvalid = false;
        _drawableExtent = {};
        _timestampValidBits = 0;
        _timestampPeriodNanoseconds = 0.0;
        _gpuTimestampsSupported = false;
        _preferHdr10 = false;
        _enableDiagnosticCapture = false;
        _hdrPaperWhiteNits = 203.0f;
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
            CheckVk(_context->WaitIdle(), "vkDeviceWaitIdle");
        }
    }

    void Device::SetVSync(bool enabled)
    {
        if (_vsync != enabled)
        {
            _vsync = enabled;
            RequestSwapchainRecreate();
        }
    }

    void Device::SetHdrPaperWhiteNits(float nits)
    {
        const std::lock_guard lock(_hostMutex);
        nits = Gpu::NormaliseHdrPaperWhiteNits(nits);
        if (_hdrPaperWhiteNits == nits)
            return;
        _hdrPaperWhiteNits = nits;
        PublishHdrMetadata();
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

    std::optional<FrameToken> Device::BeginFrame(
        bool waitForAvailability, const std::function<void(uint32_t)>& onSlotComplete)
    {
        const std::lock_guard lock(_hostMutex);
        if (_swapchainInvalid)
        {
            return std::nullopt;
        }

        auto& frame = _slots->_frames[_currentFrame];
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
        if (onSlotComplete)
            onSlotComplete(_currentFrame);
        _slots->HarvestGpuTimestamps(frame);

        uint32_t imageIndex = 0;
        const auto timeout = waitForAvailability ? UINT64_MAX : uint64_t{ 0 };
        const auto acquireResult = vkAcquireNextImageKHR(
            _device, _swapchain, timeout, _imageAvailable[_currentFrame], VK_NULL_HANDLE, &imageIndex);
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
            vkCmdResetQueryPool(frame.commandBuffer, frame.timestampQueryPool, 0, static_cast<uint32_t>(kGpuTimestampCount));
            vkCmdWriteTimestamp(
                frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.timestampQueryPool,
                static_cast<uint32_t>(GpuTimestampPoint::frameStart));
        }

        return FrameToken{
            .submission = {
                .commandBuffer = frame.commandBuffer,
                .frameIndex = _currentFrame,
                .upload = &frame.upload,
            },
            .extent = _swapchainExtent,
            .imageIndex = imageIndex,
        };
    }

    double Device::EndFrame(const FrameToken& token)
    {
        const std::lock_guard lock(_hostMutex);
        if (token.submission.frameIndex != _currentFrame || token.imageIndex >= _presentReady.size())
        {
            throw std::logic_error("Vulkan frame token does not belong to the active frame");
        }

        auto& frame = _slots->_frames[_currentFrame];
        const auto presentReady = _presentReady[token.imageIndex];
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
            .pWaitSemaphores = &_imageAvailable[_currentFrame],
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &frame.commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &presentReady,
        };
        CheckVk(_context->Submit(submitInfo, frame.available), "vkQueueSubmit(frame)");
        if (token.submission.telemetry != nullptr)
            token.submission.telemetry->submitted = true;
        frame.timestampPending = frame.timestampQueryPool != VK_NULL_HANDLE;

        const VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentReady,
            .swapchainCount = 1,
            .pSwapchains = &_swapchain,
            .pImageIndices = &token.imageIndex,
        };
        const auto presentStart = std::chrono::steady_clock::now();
        const auto presentResult = _context->Present(presentInfo);
        const auto presentCallMicroseconds = std::chrono::duration<double, std::micro>(
                                                 std::chrono::steady_clock::now() - presentStart)
                                                 .count();
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
        if (token.submission.frameIndex != _currentFrame)
        {
            throw std::logic_error("Vulkan frame token does not belong to the active frame");
        }

        auto& frame = _slots->_frames[_currentFrame];
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
            .pWaitSemaphores = &_imageAvailable[_currentFrame],
            .pWaitDstStageMask = &waitStage,
        };
        CheckVk(_context->Submit(submitInfo, frame.available), "vkQueueSubmit(abandoned frame)");

        _swapchainInvalid = true;
        _currentFrame = (_currentFrame + 1) % kFramesInFlight;
    }

    void Device::RecordGpuTimestamp(const SubmissionToken& token, GpuTimestampPoint point) const
    {
        if (token.frameIndex != _currentFrame || point == GpuTimestampPoint::frameStart
            || point == GpuTimestampPoint::frameComplete || point == GpuTimestampPoint::count)
        {
            throw std::logic_error("Invalid Vulkan GPU timestamp point for the active frame");
        }
        const auto queryPool = _slots->_frames[token.frameIndex].timestampQueryPool;
        if (queryPool != VK_NULL_HANDLE)
        {
            vkCmdWriteTimestamp(
                token.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, static_cast<uint32_t>(point));
        }
    }

    std::optional<GpuTimestampDurations> Device::TakeCompletedGpuTimings(uint32_t frameIndex)
    {
        if (frameIndex >= kFramesInFlight)
        {
            throw std::out_of_range("Vulkan frame index is out of range");
        }
        auto& frame = _slots->_frames[frameIndex];
        if (frame.timestampPending)
        {
            const auto fenceStatus = vkGetFenceStatus(_device, frame.available);
            if (fenceStatus == VK_SUCCESS)
            {
                _slots->HarvestGpuTimestamps(frame);
            }
            else if (fenceStatus != VK_NOT_READY)
            {
                ThrowVk("vkGetFenceStatus(frame timestamps)", fenceStatus);
            }
        }
        return std::exchange(frame.completedGpuTimings, std::nullopt);
    }

    void Device::ReadbackImage(
        uint32_t frameIndex, VkImage image, VkImageLayout layout, VkExtent2D extent, std::span<std::byte> destination,
        uint32_t bytesPerPixel, const std::function<void(uint32_t)>& onSlotComplete)
    {
        const std::lock_guard lock(_hostMutex);
        if (frameIndex >= kFramesInFlight || image == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0)
        {
            throw std::invalid_argument("Invalid Vulkan synchronous readback source");
        }
        const uint64_t pixelCount = static_cast<uint64_t>(extent.width) * extent.height;
        if ((bytesPerPixel != 1 && bytesPerPixel != 4) || pixelCount > destination.size() / bytesPerPixel)
        {
            throw std::invalid_argument("Vulkan synchronous readback destination is too small");
        }
        const uint64_t byteSize64 = pixelCount * bytesPerPixel;

        auto& frame = _slots->_frames[frameIndex];
        CheckVk(vkWaitForFences(_device, 1, &frame.available, VK_TRUE, UINT64_MAX), "wait for readback frame");
        if (onSlotComplete)
            onSlotComplete(frameIndex);
        CheckVk(vkResetCommandBuffer(frame.commandBuffer, 0), "reset readback command buffer");
        frame.upload.Reset();
        const auto allocation = frame.upload.Allocate(byteSize64, alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for synchronous image readback");
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
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = layout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
            1, &toTransfer);

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
        vkCmdCopyImageToBuffer(frame.commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, allocation.buffer, 1, &copy);

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
            .dstAccessMask = static_cast<VkAccessFlags>(
                layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT),
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
            1, &hostBarrier, 1, &restoreLayout);
        CheckVk(vkEndCommandBuffer(frame.commandBuffer), "end readback command buffer");

        CheckVk(vkResetFences(_device, 1, &frame.available), "reset readback fence");
        const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &frame.commandBuffer,
        };
        CheckVk(_context->Submit(submitInfo, frame.available), "submit readback command buffer");
        CheckVk(vkWaitForFences(_device, 1, &frame.available, VK_TRUE, UINT64_MAX), "wait for synchronous readback");
        frame.upload.Invalidate(allocation.offset, byteSize64);
        std::memcpy(destination.data(), allocation.data, static_cast<size_t>(byteSize64));
    }

    void Device::CreateCommandResources(VkDeviceSize uploadRingCapacity)
    {
        _slots = std::make_unique<SubmissionSlots>(_context, uploadRingCapacity);
        const VkSemaphoreCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (auto& semaphore : _imageAvailable)
            CheckVk(vkCreateSemaphore(_device, &info, nullptr, &semaphore), "create image semaphore");
        _timestampValidBits = _slots->_timestampValidBits;
        _timestampPeriodNanoseconds = _slots->_timestampPeriodNanoseconds;
        _gpuTimestampsSupported = _slots->_gpuTimestampsSupported;
    }

    bool Device::CreateSwapchain()
    {
        const auto support = QuerySwapchainSupport(_physicalDevice);
        const auto surfaceFormatSelection = SelectSurfaceFormat(support.formats, _preferHdr10);
        const auto surfaceFormat = surfaceFormatSelection.surfaceFormat;
        if (!IsSupportedOutputSurfaceFormat(surfaceFormatSelection))
        {
            throw std::runtime_error("Vulkan surface exposes neither a supported SDR format nor an active exact HDR10 format");
        }
        const auto presentMode = SelectPresentMode(support.presentModes, _vsync);
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
        _swapchainCaptureSupported = _enableDiagnosticCapture
            && (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (_swapchainCaptureSupported)
        {
            imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
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
            // Diagnostics must also render obscured/hidden window pixels.
            .clipped = _enableDiagnosticCapture ? VK_FALSE : VK_TRUE,
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
        _swapchainImages.resize(imageCount);
        CheckVk(vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, _swapchainImages.data()), "get swapchain images");

        _swapchainImageViews.resize(imageCount);
        _presentReady.resize(imageCount, VK_NULL_HANDLE);
        const VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (size_t i = 0; i < _swapchainImages.size(); i++)
        {
            CheckVk(vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_presentReady[i]), "create present semaphore");
            const VkImageViewCreateInfo viewInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = _swapchainImages[i],
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
            for (const auto semaphore : _presentReady)
                vkDestroySemaphore(_device, semaphore, nullptr);
            for (const auto imageView : _swapchainImageViews)
            {
                vkDestroyImageView(_device, imageView, nullptr);
            }
            vkDestroySwapchainKHR(_device, _swapchain, nullptr);
        }
        _swapchainImageViews.clear();
        _presentReady.clear();
        _swapchainImages.clear();
        _swapchainCaptureSupported = false;
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

    Device::SwapchainSupport Device::QuerySwapchainSupport(VkPhysicalDevice physicalDevice) const
    {
        return DeviceContext::QuerySwapchainSupport(physicalDevice, _surface);
    }

    VkExtent2D Device::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        return {
            std::clamp(_drawableExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(_drawableExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
