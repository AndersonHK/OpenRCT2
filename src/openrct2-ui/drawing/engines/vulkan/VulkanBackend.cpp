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
    #include <exception>
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
        if (config.drawableExtent.width == 0 || config.drawableExtent.height == 0)
        {
            throw std::invalid_argument("Vulkan backend requires a non-zero initial drawable extent");
        }

        _config = config;
        auto* window = static_cast<SDL_Window*>(config.nativeWindow);
        _device.Initialise(
            window, { config.drawableExtent.width, config.drawableExtent.height },
            config.presentMode == Gpu::PresentMode::VSync,
            static_cast<VkDeviceSize>(config.uploadRingBytesPerFrame),
            config.outputColorMode == Gpu::OutputColorMode::Hdr10IfAvailable, config.hdrPaperWhiteNits);
        try
        {
            const bool gpuLightFxSupported = LightFxPipeline::IsSupported(_device, config.logicalExtent);
            _resources.Initialise(_device, config.logicalExtent, gpuLightFxSupported);
            _linePipeline.Initialise(_device, _resources, config.shaderDirectory);
            _rectPipeline.Initialise(_device, _resources, config.shaderDirectory);
            _transparencyPipeline.Initialise(_device, _resources, config.shaderDirectory);
            _weatherPipeline.Initialise(_device, _resources, config.shaderDirectory);
            _lightFxPipeline.Initialise(_device, _resources, config.shaderDirectory, gpuLightFxSupported);
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
        _framePaletteVersions.fill(0);
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
        _lightFxPipeline.Dispose();
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
        _frameTimings.fill(std::nullopt);
        {
            const std::lock_guard lock(_timingsMutex);
            _latestTimings.reset();
            _completedTimingStart = 0;
            _completedTimingCount = 0;
        }
        _paletteVersion = 1;
        _framePaletteVersions.fill(0);
        _remapPaletteDirty = true;
        _blendPaletteDirty = true;
        _pendingLightFalloffs.clear();
        _lightFalloffsDirty = false;
        _lightFalloffsRecorded = false;
        _submitted = false;
        _finalCanvasComposite = false;
        _lastPresentedFrameIndex.reset();
        _lastPresentedCanvasComposite = false;
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

    void Backend::Resize(Gpu::Extent logicalExtent, Gpu::Extent drawableExtent)
    {
        if (_activeToken.has_value())
        {
            throw std::logic_error("Cannot resize the Vulkan backend during a frame");
        }
        if (!_ready)
        {
            throw std::logic_error("Cannot resize a Vulkan backend that is not ready");
        }

        const bool drawableChanged = _config.drawableExtent != drawableExtent;
        if (drawableChanged)
        {
            _config.drawableExtent = drawableExtent;
            _device.SetDrawableExtent({ drawableExtent.width, drawableExtent.height });
        }

        const bool hasLogicalExtent = logicalExtent.width != 0 && logicalExtent.height != 0;
        const bool logicalChanged = hasLogicalExtent && _config.logicalExtent != logicalExtent;
        if (!logicalChanged)
        {
            return;
        }

        _device.WaitIdle();
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; frameIndex++)
        {
            HarvestReadbacksForFrame(frameIndex, false);
        }

        _config.logicalExtent = logicalExtent;
        _lastPresentedFrameIndex.reset();
        _lastPresentedCanvasComposite = false;
        _ready = false;
        _weatherPipeline.Dispose();
        _lightFxPipeline.Dispose();
        _transparencyPipeline.Dispose();
        _rectPipeline.Dispose();
        _linePipeline.Dispose();
        try
        {
            const bool gpuLightFxSupported = LightFxPipeline::IsSupported(_device, logicalExtent);
            _resources.Resize(logicalExtent, gpuLightFxSupported);
            _linePipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _rectPipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _transparencyPipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _weatherPipeline.Initialise(_device, _resources, _config.shaderDirectory);
            _lightFxPipeline.Initialise(
                _device, _resources, _config.shaderDirectory, gpuLightFxSupported);
            _palettePipeline.RefreshDescriptors(_resources);
            _device.RequestSwapchainRecreate();
            PopulateCapabilities();
            _ready = true;
        }
        catch (...)
        {
            _lightFxPipeline.Dispose();
            _weatherPipeline.Dispose();
            _transparencyPipeline.Dispose();
            _rectPipeline.Dispose();
            _linePipeline.Dispose();
            throw;
        }
    }

    void Backend::RequestSurfaceFormatRefresh()
    {
        if (!_ready)
        {
            throw std::logic_error("Cannot refresh the surface format of a Vulkan backend that is not ready");
        }
        _device.RequestSwapchainRecreate();
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

        if (_device.IsSwapchainInvalid())
        {
            // Framebuffers must be destroyed before their old swapchain image
            // views, but only after their queued work completes. RefreshSwapchain
            // rebuilds this final pass against the replacement views below.
            _device.WaitIdle();
            _palettePipeline.ReleaseSwapchainResources();
            if (!_device.RecreateSwapchain())
            {
                return std::nullopt;
            }
        }

        const bool waitForAvailability = _config.frameAcquireMode == Gpu::FrameAcquireMode::Wait;
        // Explicit readback is one of the few consumers allowed to wait. Its
        // bytes live in this frame slot's upload ring and must be harvested
        // before a successful BeginFrame resets that ring.
        const auto frameIndex = _device.GetCurrentFrameIndex();
        HarvestReadbacksForFrame(frameIndex, true);
        _activeToken = _device.BeginFrame(waitForAvailability);
        HarvestGpuTimingsForFrame(frameIndex);
        if (!_activeToken.has_value())
        {
            return std::nullopt;
        }
        _palettePipeline.RefreshSwapchain(_device);
        _capabilities.supportsHdrMetadata = _device.IsHdrMetadataAvailable();
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
        _frameTimings[_activeToken->frameIndex] = Gpu::FrameTimings{ .frameNumber = frameNumber };
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
        _paletteVersion++;
        if (_paletteVersion == 0)
        {
            _paletteVersion = 1;
            _framePaletteVersions.fill(0);
        }
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

    void Backend::SetLightFxFalloffs(std::span<const std::byte> layers)
    {
        constexpr size_t expected = 8 * 256 * 256;
        if (layers.size() != expected) throw std::invalid_argument("GPU LightFX falloffs have an invalid size");
        _pendingLightFalloffs.assign(layers.begin(), layers.end());
        _lightFalloffsDirty = true;
    }

    void Backend::Submit(const Gpu::FrameHandle& frame, const Gpu::FrameCommandStream& commands)
    {
        ValidateActiveFrame(frame);
        const auto start = Clock::now();
        RecordPendingPalette();
        RecordPendingRemapPalette();
        RecordPendingBlendPalette();
        RecordPendingLightFalloffs();
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
        _device.RecordGpuTimestamp(*_activeToken, GpuTimestampPoint::uploadsComplete);
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
        _device.RecordGpuTimestamp(*_activeToken, GpuTimestampPoint::indexedDrawComplete);
        const bool lightFxEnabled = RecordLightFx(commands);
        _device.RecordGpuTimestamp(*_activeToken, GpuTimestampPoint::lightFxComplete);
        const auto& finalCanvas = finalComposite
            ? _resources.GetCompositeCanvas(_activeToken->frameIndex)
            : _resources.GetIndexedCanvas(_activeToken->frameIndex);
        _palettePipeline.SetCanvasSource(_activeToken->frameIndex, finalCanvas);
        _palettePipeline.Record(*_activeToken, lightFxEnabled);
        _finalCanvasComposite = finalComposite;
        _submitted = true;
        _frameTimings[frame.frameSlot]->cpuSubmitMicroseconds = ToMicroseconds(Clock::now() - start);
    }

    void Backend::Present(const Gpu::FrameHandle& frame)
    {
        ValidateActiveFrame(frame);
        if (!_submitted)
        {
            throw std::logic_error("Vulkan frame must be submitted before presentation");
        }

        const auto start = Clock::now();
        try
        {
            auto& timings = _frameTimings[frame.frameSlot];
            timings->presentCallMicroseconds = _device.EndFrame(*_activeToken);
            timings->hasPresentCallMeasurement = true;
        }
        catch (...)
        {
            // Queue submission may already have happened. Do not attempt to
            // reset that command buffer through AbandonFrame; disable this
            // backend instance and let renderer recovery rebuild it.
            _ready = false;
            _activeToken.reset();
            _activeFrame.reset();
            _submitted = false;
            _finalCanvasComposite = false;
            throw;
        }
        auto& timings = _frameTimings[frame.frameSlot];
        timings->cpuPresentMicroseconds = ToMicroseconds(Clock::now() - start);
        if (!_device.SupportsGpuTimestamps())
        {
            PublishTimings(*timings);
        }
        _lastPresentedFrameIndex = _activeToken->frameIndex;
        _lastPresentedCanvasComposite = _finalCanvasComposite;
        _lightFalloffsRecorded = false;
        _activeToken.reset();
        _activeFrame.reset();
        _submitted = false;
    }

    void Backend::AbandonFrame(const Gpu::FrameHandle& frame)
    {
        ValidateActiveFrame(frame);
        const auto frameIndex = _activeToken->frameIndex;
        std::exception_ptr failure;
        try
        {
            _device.AbandonFrame(*_activeToken);
        }
        catch (...)
        {
            _ready = false;
            failure = std::current_exception();
        }

        _activeToken.reset();
        _activeFrame.reset();
        _submitted = false;
        _finalCanvasComposite = false;
        _frameTimings[frame.frameSlot].reset();
        // Upload commands recorded into an abandoned command buffer never
        // reached the GPU. Queue all small lookup resources again next frame.
        _resources.DiscardFrameLayouts(frameIndex);
        if (_lightFalloffsRecorded)
        {
            _lightFalloffsDirty = true;
            _resources.DiscardLightFalloffLayout();
        }
        _lightFalloffsRecorded = false;
        _framePaletteVersions[frameIndex] = 0;
        _remapPaletteDirty = true;
        _blendPaletteDirty = true;
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }

    std::optional<Gpu::FrameTimings> Backend::GetLatestTimings() const
    {
        const std::lock_guard lock(_timingsMutex);
        return _latestTimings;
    }

    void Backend::TakeCompletedTimings(std::vector<Gpu::FrameTimings>& samples)
    {
        const std::lock_guard lock(_timingsMutex);
        samples.resize(_completedTimingCount);
        for (size_t i = 0; i < _completedTimingCount; i++)
        {
            samples[i] = _completedTimings[(_completedTimingStart + i) % kCompletedTimingCapacity];
        }
        std::ranges::sort(samples, {}, &Gpu::FrameTimings::frameNumber);
        _completedTimingStart = 0;
        _completedTimingCount = 0;
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

    bool Backend::ReadbackLatestIndexedCanvas(Gpu::Extent extent, std::span<std::byte> destination)
    {
        if (_activeToken.has_value())
        {
            throw std::logic_error("Cannot synchronously read back Vulkan canvas during a frame");
        }
        if (!_ready || !_lastPresentedFrameIndex.has_value())
        {
            return false;
        }

        const auto frameIndex = *_lastPresentedFrameIndex;
        const auto& source = _lastPresentedCanvasComposite ? _resources.GetCompositeCanvas(frameIndex)
                                                          : _resources.GetIndexedCanvas(frameIndex);
        const auto sourceExtent = source.GetExtent();
        if (extent.width == 0 || extent.height == 0 || extent.width > sourceExtent.width
            || extent.height > sourceExtent.height)
        {
            throw std::invalid_argument("Vulkan screenshot extent exceeds the latest indexed canvas");
        }
        const uint64_t byteSize = static_cast<uint64_t>(extent.width) * extent.height;
        if (byteSize > destination.size())
        {
            throw std::invalid_argument("Vulkan screenshot destination is too small");
        }

        HarvestReadbacksForFrame(frameIndex, true);
        _device.ReadbackImage(
            frameIndex, source.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, { extent.width, extent.height },
            destination.first(static_cast<size_t>(byteSize)));
        return true;
    }

    void Backend::WaitIdle()
    {
        _device.WaitIdle();
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; frameIndex++)
        {
            HarvestReadbacksForFrame(frameIndex, false);
            HarvestGpuTimingsForFrame(frameIndex);
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
        const auto frameIndex = _activeToken->frameIndex;
        if (_framePaletteVersions[frameIndex] == _paletteVersion)
        {
            return;
        }
        auto allocation = _activeToken->upload->Allocate(_pendingPalette.size(), alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for the palette");
        }
        std::memcpy(allocation.data, _pendingPalette.data(), _pendingPalette.size());
        _resources.RecordPaletteUpload(_activeToken->commandBuffer, frameIndex, allocation);
        _framePaletteVersions[frameIndex] = _paletteVersion;
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

    bool Backend::RecordLightFx(const Gpu::FrameCommandStream& commands)
    {
        if (!commands.lightFx.has_value())
        {
            _resources.EnsureLightFxShaderLayouts(_activeToken->commandBuffer, _activeToken->frameIndex);
            return false;
        }
        const auto& snapshot = *commands.lightFx;
        if (!snapshot.IsValid() || snapshot.width != _config.logicalExtent.width
            || snapshot.height != _config.logicalExtent.height)
        {
            throw std::invalid_argument("Vulkan LightFX snapshot does not match the logical canvas");
        }

        auto palette = _activeToken->upload->Allocate(snapshot.lightPalette.size(), alignof(uint32_t));
        if (!palette) throw std::runtime_error("Vulkan upload ring has no room for the LightFX palette");
        std::memcpy(palette.data, snapshot.lightPalette.data(), snapshot.lightPalette.size());
        if (_lightFxPipeline.IsAvailable() && !_pendingLightFalloffs.empty() && !_lightFalloffsDirty)
        {
            _resources.RecordLightPaletteUpload(_activeToken->commandBuffer, _activeToken->frameIndex, palette);
            if (_lightFxPipeline.Record(*_activeToken, snapshot, _resources))
            {
                _palettePipeline.SetLightMapSource(
                    _activeToken->frameIndex, _resources.GetLightAccumulator(_activeToken->frameIndex));
                return true;
            }
        }
        if (!snapshot.HasCpuIntensity())
        {
            throw std::runtime_error("Vulkan LightFX compute path could not consume a command-only snapshot");
        }
        auto intensities = _activeToken->upload->Allocate(snapshot.intensities.size(), alignof(uint32_t));
        if (!intensities) throw std::runtime_error("Vulkan upload ring has no room for the LightFX snapshot");
        std::memcpy(intensities.data, snapshot.intensities.data(), snapshot.intensities.size());
        _resources.RecordLightFxUpload(
            _activeToken->commandBuffer, _activeToken->frameIndex, intensities, palette, snapshot.width,
            snapshot.height);
        _palettePipeline.SetLightMapSource(
            _activeToken->frameIndex, _resources.GetLightMap(_activeToken->frameIndex));
        return true;
    }

    void Backend::RecordPendingLightFalloffs()
    {
        _lightFalloffsRecorded = false;
        if (!_lightFxPipeline.IsAvailable() || !_lightFalloffsDirty || _pendingLightFalloffs.empty()) return;
        auto allocation = _activeToken->upload->Allocate(_pendingLightFalloffs.size(), alignof(uint32_t));
        if (!allocation) throw std::runtime_error("Vulkan upload ring has no room for LightFX falloffs");
        std::memcpy(allocation.data, _pendingLightFalloffs.data(), _pendingLightFalloffs.size());
        _resources.RecordLightFalloffUpload(_activeToken->commandBuffer, allocation);
        _lightFalloffsDirty = false;
        _lightFalloffsRecorded = true;
    }

    void Backend::RecordTextureUploads(const Gpu::FrameCommandStream& commands)
    {
        if (commands.textureUploads.empty())
        {
            return;
        }

        std::vector<UploadAllocation> allocations;
        allocations.reserve(commands.textureUploads.size());
        for (const auto& upload : commands.textureUploads)
        {
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            if (size > std::numeric_limits<size_t>::max() || static_cast<size_t>(size) != upload.pixels.size())
            {
                throw std::invalid_argument("Vulkan texture upload payload does not match its bounds and pitch");
            }
            auto allocation = _activeToken->upload->Allocate(size, alignof(uint32_t));
            if (!allocation)
            {
                throw std::runtime_error("Vulkan upload ring has no room for a sprite atlas upload");
            }
            std::memcpy(allocation.data, upload.pixels.data(), upload.pixels.size());
            allocations.push_back(allocation);
        }

        _resources.BeginAtlasUploads(_activeToken->commandBuffer);
        for (size_t i = 0; i < commands.textureUploads.size(); i++)
        {
            const auto& upload = commands.textureUploads[i];
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            const auto& staged = allocations[i];
            const UploadAllocation allocation = { staged.buffer, staged.offset, size, nullptr };
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
            .supportsNonBlockingFrameAcquire = true,
            .supportsLineCommands = true,
            .supportsOpaqueRectCommands = true,
            .supportsTransparencyCommands = true,
            .supportsWeatherCommands = true,
            .supportsLightFxComposition = true,
            .supportsGpuLightFxRasterization = _lightFxPipeline.IsAvailable(),
            .supportsAsyncReadback = true,
            .supportsCanvasUpload = true,
            .supportsGpuTimestamps = _device.SupportsGpuTimestamps(),
            .supportsHdrMetadata = _device.IsHdrMetadataAvailable(),
            .supportsHdr10Output = _device.IsHdr10Available(),
            .hdr10OutputActive = _device.IsHdr10Active(),
            .supportsIndexedDrawCommands = true,
        };
    }

    void Backend::HarvestGpuTimingsForFrame(uint32_t frameIndex)
    {
        const auto gpu = _device.TakeCompletedGpuTimings(frameIndex);
        if (!gpu.has_value() || !_frameTimings[frameIndex].has_value())
        {
            return;
        }
        auto& timings = *_frameTimings[frameIndex];
        timings.gpuMicroseconds = gpu->totalMicroseconds;
        timings.gpuUploadMicroseconds = gpu->uploadMicroseconds;
        timings.gpuDrawMicroseconds = gpu->drawMicroseconds;
        timings.gpuLightFxMicroseconds = gpu->lightFxMicroseconds;
        timings.gpuCompositeMicroseconds = gpu->compositeMicroseconds;
        timings.hasGpuTimestamp = true;
        timings.hasGpuPassTimestamps = true;
        PublishTimings(timings);
        _frameTimings[frameIndex].reset();
    }

    void Backend::PublishTimings(const Gpu::FrameTimings& timings)
    {
        const std::lock_guard lock(_timingsMutex);
        size_t writeIndex;
        if (_completedTimingCount < kCompletedTimingCapacity)
        {
            writeIndex = (_completedTimingStart + _completedTimingCount) % kCompletedTimingCapacity;
            _completedTimingCount++;
        }
        else
        {
            writeIndex = _completedTimingStart;
            _completedTimingStart = (_completedTimingStart + 1) % kCompletedTimingCapacity;
        }
        _completedTimings[writeIndex] = timings;
        if (!_latestTimings.has_value() || timings.frameNumber >= _latestTimings->frameNumber)
        {
            _latestTimings = timings;
        }
    }

    std::unique_ptr<Gpu::Backend> CreateBackend()
    {
        return std::make_unique<Backend>();
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
