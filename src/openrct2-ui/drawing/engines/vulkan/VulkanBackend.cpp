/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanBackend.h"

    #include "../opengl/TransparencyDepth.h"

    #include <SDL.h>
    #include <algorithm>
    #include <chrono>
    #include <cstring>
    #include <limits>
    #include <stdexcept>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double ToMicroseconds(Clock::duration duration)
        {
            return std::chrono::duration<double, std::micro>(duration).count();
        }

    } // namespace

    Backend::~Backend()
    {
        Dispose();
    }

    void Backend::Initialise(const Gpu::BackendConfig& config)
    {
        Dispose();
        if (config.nativeWindow == nullptr)
        {
            throw std::invalid_argument("Vulkan backend requires an SDL window");
        }
        if (config.shaderDirectory.empty())
        {
            throw std::invalid_argument("Vulkan backend requires the installed SPIR-V shader directory");
        }
        if (config.logicalExtent.width == 0 || config.logicalExtent.height == 0)
        {
            throw std::invalid_argument("Vulkan backend requires a non-zero logical extent");
        }

        _config = config;
        auto* window = static_cast<SDL_Window*>(config.nativeWindow);
        _device.Initialise(
            window, config.presentMode == Gpu::PresentMode::VSync,
            static_cast<VkDeviceSize>(config.uploadRingBytesPerFrame),
            config.outputColorMode == Gpu::OutputColorMode::Hdr10IfAvailable);
        try
        {
            _resources.Initialise(_device, config.logicalExtent);
            _linePipeline.Initialise(_device, _resources, config.shaderDirectory);
            _rectPipeline.Initialise(_device, _resources, config.shaderDirectory);
            _transparencyPipeline.Initialise(_device, _resources, config.shaderDirectory);
            _weatherPipeline.Initialise(_device, _resources, config.shaderDirectory);
            _palettePipeline.Initialise(_device, _resources, config.shaderDirectory, config.hdrPaperWhiteNits);
        }
        catch (...)
        {
            Dispose();
            throw;
        }

        _pendingPalette.fill(std::byte{ 0 });
        for (size_t i = 0; i < 256; i++)
        {
            _pendingPalette[i * 4 + 3] = i == 0 ? std::byte{ 0 } : std::byte{ 0xff };
            for (size_t row = 0; row < 256; row++)
            {
                _pendingRemapPalette[row * 256 + i] = static_cast<std::byte>(i);
                _pendingBlendPalette[row * 256 + i] = static_cast<std::byte>(i);
            }
        }
        _paletteDirty = true;
        _remapPaletteDirty = true;
        _blendPaletteDirty = true;
        PopulateCapabilities();
        _ready = true;
    }

    void Backend::Dispose()
    {
        if (_device.GetDevice() != VK_NULL_HANDLE)
        {
            // Destruction must not throw. Waiting here keeps dependent image
            // and pipeline destruction ahead of logical-device destruction.
            (void)vkDeviceWaitIdle(_device.GetDevice());
        }
        _palettePipeline.Dispose();
        _weatherPipeline.Dispose();
        _transparencyPipeline.Dispose();
        _rectPipeline.Dispose();
        _linePipeline.Dispose();
        _resources.Dispose();
        _device.Dispose();
        _config = {};
        _capabilities = {};
        _activeToken.reset();
        _activeFrame.reset();
        _latestTimings.reset();
        _paletteDirty = true;
        _remapPaletteDirty = true;
        _blendPaletteDirty = true;
        _submitted = false;
        _finalCanvasComposite = false;
        _ready = false;
        {
            const std::lock_guard lock(_readbackMutex);
            _readbacks.clear();
        }
    }

    const Gpu::BackendCapabilities& Backend::GetCapabilities() const noexcept
    {
        return _capabilities;
    }

    void Backend::Resize(Gpu::Extent logicalExtent)
    {
        if (logicalExtent.width == 0 || logicalExtent.height == 0)
        {
            throw std::invalid_argument("Vulkan backend cannot create a zero-sized indexed canvas");
        }
        if (_activeToken.has_value())
        {
            throw std::logic_error("Cannot resize the Vulkan backend during a frame");
        }
        if (!_ready)
        {
            throw std::logic_error("Cannot resize a Vulkan backend that is not ready");
        }

        _device.WaitIdle();
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; frameIndex++)
        {
            HarvestReadbacksForFrame(frameIndex, false);
        }

        _config.logicalExtent = logicalExtent;
        _ready = false;
        _weatherPipeline.Dispose();
        _transparencyPipeline.Dispose();
        _rectPipeline.Dispose();
        _linePipeline.Dispose();
        try
        {
            _resources.Resize(logicalExtent);
            _linePipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _rectPipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _transparencyPipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _weatherPipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _palettePipeline.RefreshDescriptors(_resources);
            _device.RequestSwapchainRecreate();
            _ready = true;
        }
        catch (...)
        {
            _weatherPipeline.Dispose();
            _transparencyPipeline.Dispose();
            _rectPipeline.Dispose();
            _linePipeline.Dispose();
            throw;
        }
    }

    void Backend::SetPresentMode(Gpu::PresentMode mode)
    {
        _config.presentMode = mode;
        _device.SetVSync(mode == Gpu::PresentMode::VSync);
    }

    std::optional<Gpu::FrameHandle> Backend::BeginFrame(uint64_t frameNumber)
    {
        if (_activeToken.has_value())
        {
            throw std::logic_error("Vulkan backend already has an active frame");
        }
        if (!_ready)
        {
            throw std::logic_error("Vulkan backend is not ready to begin a frame");
        }

        HarvestReadbacksForFrame(_device.GetCurrentFrameIndex(), true);
        _activeToken = _device.BeginFrame();
        if (!_activeToken.has_value())
        {
            return std::nullopt;
        }
        _palettePipeline.RefreshSwapchain(_device);
        _capabilities.supportsHdr10Output = _device.IsHdr10Available();
        _capabilities.hdr10OutputActive = _device.IsHdr10Active();

        _activeFrame = Gpu::FrameHandle{
            .frameNumber = frameNumber,
            .frameSlot = _activeToken->frameIndex,
            .imageIndex = _activeToken->imageIndex,
            .drawableExtent = { _activeToken->extent.width, _activeToken->extent.height },
        };
        _submitted = false;
        _finalCanvasComposite = false;
        _latestTimings = Gpu::FrameTimings{ .frameNumber = frameNumber };
        return _activeFrame;
    }

    Gpu::UploadSlice Backend::AllocateUpload(uint64_t size, uint64_t alignment)
    {
        if (!_activeToken.has_value())
        {
            throw std::logic_error("Vulkan upload allocation requires an active frame");
        }
        const auto allocation = _activeToken->upload->Allocate(size, alignment);
        if (!allocation)
        {
            return {};
        }
        return {
            allocation.offset,
            std::span<std::byte>(allocation.data, static_cast<size_t>(allocation.size)),
        };
    }

    void Backend::SetPalette(std::span<const std::byte> rgba)
    {
        if (rgba.size() != _pendingPalette.size())
        {
            throw std::invalid_argument("GPU palettes must contain exactly 256 RGBA8 entries");
        }
        std::copy(rgba.begin(), rgba.end(), _pendingPalette.begin());
        _paletteDirty = true;
    }

    void Backend::SetRemapPalette(std::span<const std::byte> indices)
    {
        if (indices.size() != _pendingRemapPalette.size())
        {
            throw std::invalid_argument("GPU remap palettes must contain exactly 256 by 256 indices");
        }
        std::copy(indices.begin(), indices.end(), _pendingRemapPalette.begin());
        _remapPaletteDirty = true;
    }

    void Backend::SetBlendPalette(std::span<const std::byte> indices)
    {
        if (indices.size() != _pendingBlendPalette.size())
        {
            throw std::invalid_argument("GPU blend palettes must contain exactly 256 by 256 indices");
        }
        std::copy(indices.begin(), indices.end(), _pendingBlendPalette.begin());
        _blendPaletteDirty = true;
    }

    void Backend::Submit(const Gpu::FrameHandle& frame, const Gpu::FrameCommandStream& commands)
    {
        ValidateActiveFrame(frame);
        const auto start = Clock::now();
        RecordPendingPalette();
        RecordPendingRemapPalette();
        RecordPendingBlendPalette();
        RecordTextureUploads(commands);
        if (commands.canvasUpload.has_value())
        {
            if (!commands.lines.empty() || !commands.opaqueRects.empty() || !commands.transparentRects.empty())
            {
                throw std::invalid_argument("Indexed canvas upload cannot be mixed with recorded draw commands");
            }
            const auto& upload = *commands.canvasUpload;
            const uint64_t size = static_cast<uint64_t>(upload.sourcePitch) * upload.height;
            if (upload.sourceOffset > _activeToken->upload->GetUsed()
                || size > _activeToken->upload->GetUsed() - upload.sourceOffset)
            {
                throw std::out_of_range("Vulkan canvas upload references bytes outside the current upload ring");
            }
            const UploadAllocation allocation = {
                _activeToken->upload->GetBuffer(), upload.sourceOffset, size, nullptr
            };
            _resources.RecordCanvasUpload(
                _activeToken->commandBuffer, _activeToken->frameIndex, allocation, upload);
        }
        const bool requiresDepth = !commands.opaqueRects.empty() || !commands.transparentRects.empty();
        if (!commands.canvasUpload.has_value())
        {
            if (commands.lines.empty() && !requiresDepth)
            {
                _resources.RecordCanvasClear(_activeToken->commandBuffer, _activeToken->frameIndex, 0);
            }
            else
            {
                if (commands.lines.empty())
                {
                    _resources.RecordCanvasAndDepthClear(_activeToken->commandBuffer, _activeToken->frameIndex, 0);
                }
                else
                {
                    _linePipeline.Record(*_activeToken, commands.lines);
                }
                _rectPipeline.Record(*_activeToken, commands.opaqueRects);
            }
        }
        bool finalComposite = false;
        if (!commands.transparentRects.empty())
        {
            const auto layers = static_cast<uint32_t>(MaxTransparencyDepth(commands.transparentRects));
            finalComposite = _transparencyPipeline.Record(*_activeToken, commands.transparentRects, layers);
        }
        _weatherPipeline.Record(*_activeToken, commands.weather, finalComposite);
        const auto& finalCanvas = finalComposite
            ? _resources.GetCompositeCanvas(_activeToken->frameIndex)
            : _resources.GetIndexedCanvas(_activeToken->frameIndex);
        _palettePipeline.SetCanvasSource(_activeToken->frameIndex, finalCanvas);
        _palettePipeline.Record(*_activeToken);
        _finalCanvasComposite = finalComposite;
        _submitted = true;
        _latestTimings->cpuSubmitMicroseconds = ToMicroseconds(Clock::now() - start);
    }

    void Backend::Present(const Gpu::FrameHandle& frame)
    {
        ValidateActiveFrame(frame);
        if (!_submitted)
        {
            throw std::logic_error("Vulkan frame must be submitted before presentation");
        }

        const auto start = Clock::now();
        _device.EndFrame(*_activeToken);
        _latestTimings->cpuPresentMicroseconds = ToMicroseconds(Clock::now() - start);
        _activeToken.reset();
        _activeFrame.reset();
        _submitted = false;
    }

    std::optional<Gpu::FrameTimings> Backend::GetLatestTimings() const
    {
        return _latestTimings;
    }

    void Backend::RequestReadback(const Gpu::FrameHandle& frame, Gpu::ReadbackRequest request)
    {
        ValidateActiveFrame(frame);
        if (!_submitted)
        {
            throw std::logic_error("Vulkan readback requires a submitted frame");
        }
        if (!request.indexed)
        {
            throw std::invalid_argument("Vulkan readback currently exposes the indexed canvas only");
        }
        const auto& source = _finalCanvasComposite
            ? _resources.GetCompositeCanvas(_activeToken->frameIndex)
            : _resources.GetIndexedCanvas(_activeToken->frameIndex);
        const auto sourceExtent = source.GetExtent();
        if (request.extent.width == 0 || request.extent.height == 0 || request.extent.width > sourceExtent.width
            || request.extent.height > sourceExtent.height)
        {
            throw std::invalid_argument("Vulkan readback extent exceeds the indexed canvas");
        }
        const uint64_t byteSize64 = static_cast<uint64_t>(request.extent.width) * request.extent.height;
        if (byteSize64 > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Vulkan readback size exceeds addressable memory");
        }
        const auto byteSize = static_cast<size_t>(byteSize64);
        const auto allocation = _activeToken->upload->Allocate(byteSize, alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for indexed readback");
        }
        {
            const std::lock_guard lock(_readbackMutex);
            if (_readbacks.contains(request.id))
            {
                throw std::invalid_argument("Vulkan readback id is already pending");
            }
            _readbacks.emplace(
                request.id,
                PendingReadback{ _activeToken->frameIndex, allocation.offset, allocation.data, byteSize, {} });
        }

        const VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        RecordImageBarrier(
            _activeToken->commandBuffer, source.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
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
            .imageExtent = { request.extent.width, request.extent.height, 1 },
        };
        vkCmdCopyImageToBuffer(
            _activeToken->commandBuffer, source.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, allocation.buffer, 1,
            &copy);
        const VkBufferMemoryBarrier hostBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = allocation.buffer,
            .offset = allocation.offset,
            .size = byteSize,
        };
        vkCmdPipelineBarrier(
            _activeToken->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
            &hostBarrier, 0, nullptr);
        RecordImageBarrier(
            _activeToken->commandBuffer, source.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    bool Backend::TryTakeReadback(uint64_t requestId, std::span<std::byte> destination)
    {
        const std::lock_guard lock(_readbackMutex);
        const auto it = _readbacks.find(requestId);
        if (it == _readbacks.end())
        {
            return false;
        }
        auto& readback = it->second;
        if (destination.size() < readback.size)
        {
            throw std::invalid_argument("Vulkan readback destination is too small");
        }
        if (readback.readyData.empty())
        {
            if (!_device.IsFrameComplete(readback.frameIndex))
            {
                return false;
            }
            _device.InvalidateUpload(readback.frameIndex, readback.offset, readback.size);
            std::memcpy(destination.data(), readback.mappedData, readback.size);
        }
        else
        {
            std::memcpy(destination.data(), readback.readyData.data(), readback.size);
        }
        _readbacks.erase(it);
        return true;
    }

    void Backend::WaitIdle()
    {
        _device.WaitIdle();
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; frameIndex++)
        {
            HarvestReadbacksForFrame(frameIndex, false);
        }
    }

    void Backend::HarvestReadbacksForFrame(uint32_t frameIndex, bool wait)
    {
        const std::lock_guard lock(_readbackMutex);
        const bool hasPending = std::any_of(_readbacks.begin(), _readbacks.end(), [frameIndex](const auto& item) {
            return item.second.frameIndex == frameIndex && item.second.readyData.empty();
        });
        if (!hasPending)
        {
            return;
        }
        if (wait)
        {
            _device.WaitForFrame(frameIndex);
        }
        else if (!_device.IsFrameComplete(frameIndex))
        {
            return;
        }
        for (auto& item : _readbacks)
        {
            auto& readback = item.second;
            if (readback.frameIndex == frameIndex && readback.readyData.empty())
            {
                _device.InvalidateUpload(readback.frameIndex, readback.offset, readback.size);
                readback.readyData.assign(readback.mappedData, readback.mappedData + readback.size);
                readback.mappedData = nullptr;
            }
        }
    }

    void Backend::ValidateActiveFrame(const Gpu::FrameHandle& frame) const
    {
        if (!_activeFrame.has_value() || !_activeToken.has_value() || _activeFrame->frameNumber != frame.frameNumber
            || _activeFrame->frameSlot != frame.frameSlot || _activeFrame->imageIndex != frame.imageIndex)
        {
            throw std::logic_error("GPU frame handle does not match the active Vulkan frame");
        }
    }

    void Backend::RecordPendingPalette()
    {
        if (!_paletteDirty)
        {
            return;
        }
        auto allocation = _activeToken->upload->Allocate(_pendingPalette.size(), alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for the palette");
        }
        std::memcpy(allocation.data, _pendingPalette.data(), _pendingPalette.size());
        _resources.RecordPaletteUpload(_activeToken->commandBuffer, allocation);
        _paletteDirty = false;
    }

    void Backend::RecordPendingRemapPalette()
    {
        if (!_remapPaletteDirty)
        {
            return;
        }
        auto allocation = _activeToken->upload->Allocate(_pendingRemapPalette.size(), alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for the remap palette");
        }
        std::memcpy(allocation.data, _pendingRemapPalette.data(), _pendingRemapPalette.size());
        _resources.RecordRemapPaletteUpload(_activeToken->commandBuffer, allocation);
        _remapPaletteDirty = false;
    }

    void Backend::RecordPendingBlendPalette()
    {
        if (!_blendPaletteDirty)
        {
            return;
        }
        auto allocation = _activeToken->upload->Allocate(_pendingBlendPalette.size(), alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for the blend palette");
        }
        std::memcpy(allocation.data, _pendingBlendPalette.data(), _pendingBlendPalette.size());
        _resources.RecordBlendPaletteUpload(_activeToken->commandBuffer, allocation);
        _blendPaletteDirty = false;
    }

    void Backend::RecordTextureUploads(const Gpu::FrameCommandStream& commands)
    {
        if (commands.textureUploads.empty())
        {
            return;
        }

        for (const auto& upload : commands.textureUploads)
        {
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            if (upload.sourceOffset > _activeToken->upload->GetUsed()
                || size > _activeToken->upload->GetUsed() - upload.sourceOffset)
            {
                throw std::out_of_range("Vulkan texture upload references bytes outside the current upload ring");
            }
        }

        _resources.BeginAtlasUploads(_activeToken->commandBuffer);
        for (const auto& upload : commands.textureUploads)
        {
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            const UploadAllocation allocation = {
                _activeToken->upload->GetBuffer(), upload.sourceOffset, size, nullptr
            };
            _resources.RecordAtlasUpload(
                _activeToken->commandBuffer, allocation, upload.atlas, upload.bounds, upload.sourcePitch);
        }
        _resources.EndAtlasUploads(_activeToken->commandBuffer);
    }

    void Backend::PopulateCapabilities()
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &properties);
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(_device.GetPhysicalDevice(), &memory);
        uint64_t deviceLocalMemory = 0;
        for (uint32_t i = 0; i < memory.memoryHeapCount; i++)
        {
            if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                deviceLocalMemory += memory.memoryHeaps[i].size;
            }
        }

        _capabilities = {
            .api = Gpu::BackendApi::Vulkan,
            .framesInFlight = kFramesInFlight,
            .maxTextureDimension = properties.limits.maxImageDimension2D,
            .maxTextureArrayLayers = properties.limits.maxImageArrayLayers,
            .deviceLocalMemory = deviceLocalMemory,
            .uploadRingCapacity = std::max<uint64_t>(_config.uploadRingBytesPerFrame, 1024 * 1024),
            .supportsLineCommands = true,
            .supportsOpaqueRectCommands = true,
            .supportsTransparencyCommands = true,
            .supportsWeatherCommands = true,
            .supportsAsyncReadback = true,
            .supportsCanvasUpload = true,
            .supportsHdr10Output = _device.IsHdr10Available(),
            .hdr10OutputActive = _device.IsHdr10Active(),
            .supportsIndexedDrawCommands = true,
        };
    }

    std::unique_ptr<Gpu::Backend> CreateBackend()
    {
        return std::make_unique<Backend>();
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
