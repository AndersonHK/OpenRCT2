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

    #include <algorithm>
    #include <chrono>
    #include <cstring>
    #include <exception>
    #include <limits>
    #include <openrct2-renderer/gpu/GpuTransparencyDepth.h>
    #include <stdexcept>
    #include <utility>

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

    Backend::Backend(std::unique_ptr<PresentationHost> presentationHost, std::shared_ptr<DeviceContextOwner> owner)
        : _presentationHost(std::move(presentationHost))
        , _device(std::move(owner))
    {
        if (_presentationHost == nullptr)
            throw std::invalid_argument("Vulkan backend requires a presentation host");
    }

    Backend::~Backend()
    {
        Dispose();
    }

    void Backend::Initialise(const Gpu::BackendConfig& config)
    {
        Dispose();
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
        SetScaleSettings(config.scaleSettings);
        _device.Initialise(
            *_presentationHost, { config.drawableExtent.width, config.drawableExtent.height },
            config.presentMode == Gpu::PresentMode::VSync, static_cast<VkDeviceSize>(config.uploadRingBytesPerFrame),
            config.outputColorMode == Gpu::OutputColorMode::Hdr10IfAvailable, config.hdrPaperWhiteNits,
            config.enableDiagnosticCapture);
        try
        {
            _executor.Initialise(_device.GetContext(), config.logicalExtent, config.shaderDirectory);
            _palettePipeline.Initialise(_device, _executor.GetResources(), config.shaderDirectory, config.hdrPaperWhiteNits);
        }
        catch (...)
        {
            Dispose();
            throw;
        }

        _ready = true;
    }

    void Backend::Dispose()
    {
        if (_device.GetDevice() != VK_NULL_HANDLE)
        {
            // Destruction must not throw. Waiting here keeps dependent image
            // and pipeline destruction ahead of logical-device destruction.
            (void)_device.GetContext()->WaitIdle();
        }
        _palettePipeline.Dispose();
        _executor.Dispose();
        for (auto& image : _captureImages)
            image.Dispose();
        _captures.fill(std::nullopt);
        _device.Dispose();
        _config = {};
        ClearActiveFrame();
        _frameTimings.fill(std::nullopt);
        {
            const std::lock_guard lock(_timingsMutex);
            _latestTimings.reset();
            _lostTelemetrySamples = 0;
            _completedTimingStart = 0;
            _completedTimingCount = 0;
        }
        _lastPresentedFrameIndex.reset();
        _lastPresentedCanvasComposite = false;
        _ready = false;
    }

    bool Backend::SupportsGpuLightFxRasterization() const noexcept
    {
        return _executor.SupportsGpuLightFxRasterization();
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
        if (drawableChanged || logicalChanged)
            _captures.fill(std::nullopt);
        if (!logicalChanged)
        {
            return;
        }

        WaitIdle();
        _config.logicalExtent = logicalExtent;
        _lastPresentedFrameIndex.reset();
        _lastPresentedCanvasComposite = false;
        _ready = false;
        try
        {
            _executor.Resize(logicalExtent);
            _palettePipeline.RefreshDescriptors(_executor.GetResources());
            _device.RequestSwapchainRecreate();
            _ready = true;
        }
        catch (...)
        {
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

    void Backend::SetScaleSettings(Gpu::ScaleSettings settings)
    {
        if (_activeToken.has_value())
            throw std::logic_error("Cannot change Vulkan scale settings during a frame");
        if (settings.integerScale == 0 || settings.mode > Gpu::ScaleMode::SmoothNearest)
            throw std::invalid_argument("Invalid Vulkan scale settings");
        _config.scaleSettings = settings;
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
            WaitIdle();
            _captures.fill(std::nullopt);
            _palettePipeline.ReleaseSwapchainResources();
            if (!_device.RecreateSwapchain())
            {
                return std::nullopt;
            }
        }

        const bool waitForAvailability = _config.frameAcquireMode == Gpu::FrameAcquireMode::Wait;
        const auto frameIndex = _device.GetCurrentFrameIndex();
        _activeToken = _device.BeginFrame(waitForAvailability,
            [this](uint32_t slot) { _executor.CompleteTerrainStatus(slot); });
        HarvestGpuTimingsForFrame(frameIndex);
        if (!_activeToken.has_value())
        {
            return std::nullopt;
        }
        _palettePipeline.RefreshSwapchain(_device);
        _captures[_activeToken->submission.frameIndex].reset();
        for (auto& capture : _captures)
        {
            if (capture.has_value() && capture->frameNumber == frameNumber)
                capture.reset();
        }

        _activeFrame = Gpu::FrameHandle{
            .frameNumber = frameNumber,
            .frameSlot = _activeToken->submission.frameIndex,
            .imageIndex = _activeToken->imageIndex,
            .drawableExtent = { _activeToken->extent.width, _activeToken->extent.height },
        };
        _submitted = false;
        _finalCanvasComposite = false;
        _frameTimings[_activeToken->submission.frameIndex] = Gpu::FrameTimings{ .frameNumber = frameNumber };
        if (_config.enableUploadTelemetry)
        {
            _activeTelemetry = &_frameTimings[_activeToken->submission.frameIndex]->uploadTelemetry.emplace();
            _activeToken->submission.telemetry = _activeTelemetry;
            _activeToken->submission.upload->SetTelemetry(_activeTelemetry);
        }
        return _activeFrame;
    }

    void Backend::SetPalette(std::span<const std::byte> rgba)
    {
        _executor.SetPalette(rgba);
    }

    void Backend::SetRemapPalette(std::span<const std::byte> indices)
    {
        _executor.SetRemapPalette(indices);
    }

    void Backend::SetBlendPalette(std::span<const std::byte> indices)
    {
        _executor.SetBlendPalette(indices);
    }

    void Backend::SetLightFxFalloffs(std::span<const std::byte> layers)
    {
        _executor.SetLightFxFalloffs(layers);
    }

    void Backend::Submit(const Gpu::FrameHandle& frame, const Gpu::FrameCommandStream& commands)
    {
        ValidateActiveFrame(frame);
        const auto start = Clock::now();
        const auto output = _executor.Record(_activeToken->submission, commands, 0, {}, [this](GpuTimestampPoint point) {
            _device.RecordGpuTimestamp(_activeToken->submission, point);
        });
        const bool lightFxEnabled = output.lightMap != nullptr;
        const bool finalComposite = output.composite;
        _palettePipeline.SetCanvasSource(_activeToken->submission.frameIndex, *output.canvas);
        if (output.lightMap)
            _palettePipeline.SetLightMapSource(_activeToken->submission.frameIndex, *output.lightMap);
        _palettePipeline.Record(*_activeToken, lightFxEnabled, _config.logicalExtent, _config.scaleSettings);
        if (_captureRequested)
            RecordFrameCapture(lightFxEnabled);
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
            _executor.Commit();
            timings->hasPresentCallMeasurement = true;
        }
        catch (...)
        {
            // Queue submission may already have happened. Do not attempt to
            // reset that command buffer through AbandonFrame; disable this
            // backend instance and let renderer recovery rebuild it.
            _ready = false;
            if (_activeTelemetry != nullptr)
            {
                auto sample = *_frameTimings[frame.frameSlot];
                sample.telemetryOnly = true;
                PublishTimings(sample);
                _frameTimings[frame.frameSlot].reset();
            }
            ClearActiveFrame();
            throw;
        }
        auto& timings = _frameTimings[frame.frameSlot];
        timings->cpuPresentMicroseconds = ToMicroseconds(Clock::now() - start);
        if (!_device.SupportsGpuTimestamps())
        {
            PublishTimings(*timings);
        }
        _lastPresentedFrameIndex = _activeToken->submission.frameIndex;
        _lastPresentedCanvasComposite = _finalCanvasComposite;
        ClearActiveFrame();
    }

    void Backend::AbandonFrame(const Gpu::FrameHandle& frame)
    {
        ValidateActiveFrame(frame);
        const auto frameIndex = _activeToken->submission.frameIndex;
        _captures[frameIndex].reset();
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

        if (_activeTelemetry != nullptr)
        {
            auto sample = *_frameTimings[frame.frameSlot];
            sample.telemetryOnly = true;
            PublishTimings(sample);
        }
        ClearActiveFrame();
        _frameTimings[frame.frameSlot].reset();
        // Upload commands recorded into an abandoned command buffer never
        // reached the GPU. Queue all small lookup resources again next frame.
        _executor.Discard(frameIndex);
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
        if (_lostTelemetrySamples != 0)
        {
            Drawing::RenderUploadTelemetry telemetry;
            telemetry.auxiliary = true;
            telemetry.lostSamples = std::exchange(_lostTelemetrySamples, 0);
            samples.push_back(Gpu::FrameTimings{ .uploadTelemetry = telemetry, .telemetryOnly = true });
        }
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
        const auto& source = _lastPresentedCanvasComposite ? _executor.GetResources().GetCompositeCanvas(frameIndex)
                                                           : _executor.GetResources().GetIndexedCanvas(frameIndex);
        const auto sourceExtent = source.GetExtent();
        if (extent.width == 0 || extent.height == 0 || extent.width > sourceExtent.width || extent.height > sourceExtent.height)
        {
            throw std::invalid_argument("Vulkan screenshot extent exceeds the latest indexed canvas");
        }
        const uint64_t byteSize = static_cast<uint64_t>(extent.width) * extent.height;
        if (byteSize > destination.size())
        {
            throw std::invalid_argument("Vulkan screenshot destination is too small");
        }

        _device.ReadbackImage(
            frameIndex, source.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, { extent.width, extent.height },
            destination.first(static_cast<size_t>(byteSize)), 1,
            [this](uint32_t slot) { _executor.CompleteTerrainStatus(slot); });
        PublishReadbackTelemetry(byteSize);
        return true;
    }

    bool Backend::RequestFrameCapture(const Gpu::FrameHandle& frame)
    {
        ValidateActiveFrame(frame);
        if (_submitted)
            throw std::logic_error("Request Vulkan diagnostic capture before submitting the frame");
        if (!_config.enableDiagnosticCapture || !_device.SupportsSwapchainCapture())
            return false;
        const auto format = _device.GetSwapchainFormat();
        if (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB
            && format != VK_FORMAT_B8G8R8A8_SRGB)
            return false;

        // BeginFrame already waited for this slot. Its previous image can be
        // replaced without stalling any other frame or the presentation queue.
        _captureImages[frame.frameSlot].Initialise(
            _device.GetPhysicalDevice(), _device.GetDevice(), { frame.drawableExtent.width, frame.drawableExtent.height, 1 }, 1,
            format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        _captureRequested = true;
        return true;
    }

    void Backend::RecordFrameCapture(bool lightFxEnabled)
    {
        const auto& frame = *_activeToken;
        if (_activeTelemetry != nullptr)
            _activeTelemetry->Add(_activeTelemetry->captureRequests, 1);
        const auto source = _device.GetSwapchainImage(frame.imageIndex);
        const auto destination = _captureImages[frame.submission.frameIndex].GetImage();
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        const std::array barriers = {
            VkImageMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = source,
                .subresourceRange = range,
            },
            VkImageMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = destination,
                .subresourceRange = range,
            },
        };
        vkCmdPipelineBarrier(
            frame.submission.commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
            nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
        const VkImageCopy copy = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { frame.extent.width, frame.extent.height, 1 },
        };
        vkCmdCopyImage(
            frame.submission.commandBuffer, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        auto restore = barriers;
        restore[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        restore[0].dstAccessMask = 0;
        restore[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        restore[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        restore[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        restore[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        restore[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        restore[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(
            frame.submission.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
            nullptr, static_cast<uint32_t>(restore.size()), restore.data());
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &deviceProperties);
        _captures[frame.submission.frameIndex] = Gpu::FrameRgbaCapture{
            .frameNumber = _activeFrame->frameNumber,
            .logicalExtent = _config.logicalExtent,
            .drawableExtent = _activeFrame->drawableExtent,
            .paletteVersion = _executor.GetPaletteVersion(frame.submission.frameIndex),
            .swapchainGeneration = _device.GetSwapchainGeneration(),
            .sourceFormat = static_cast<uint32_t>(_device.GetSwapchainFormat()),
            .deviceName = deviceProperties.deviceName,
            .vendorId = deviceProperties.vendorID,
            .deviceId = deviceProperties.deviceID,
            .driverVersion = deviceProperties.driverVersion,
            .lightFxEnabled = lightFxEnabled,
            .scaleSettings = _config.scaleSettings,
            .balloonUploads = _executor.GetBalloonUploadStats(),
            .terrainUploads = _executor.GetTerrainUploadStats(),
        };
    }

    std::optional<Gpu::FrameRgbaCapture> Backend::ReadbackFrameRgba(uint64_t frameNumber)
    {
        if (_activeToken.has_value())
            throw std::logic_error("Cannot read back Vulkan diagnostic output during a frame");
        if (!_ready)
            return std::nullopt;
        for (uint32_t slot = 0; slot < kFramesInFlight; slot++)
        {
            if (!_captures[slot].has_value() || _captures[slot]->frameNumber != frameNumber)
                continue;
            auto result = *_captures[slot];
            const auto extent = result.drawableExtent;
            const uint64_t pixelCount = static_cast<uint64_t>(extent.width) * extent.height;
            if (pixelCount > result.rgba.max_size() / 4)
                throw std::length_error("Vulkan diagnostic capture exceeds host memory limits");
            result.rgba.resize(static_cast<size_t>(pixelCount) * 4);
            _device.ReadbackImage(
                slot, _captureImages[slot].GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, { extent.width, extent.height },
                result.rgba, 4, [this](uint32_t completedSlot) { _executor.CompleteTerrainStatus(completedSlot); });
            PublishReadbackTelemetry(result.rgba.size());
            if (result.sourceFormat == VK_FORMAT_B8G8R8A8_UNORM || result.sourceFormat == VK_FORMAT_B8G8R8A8_SRGB)
            {
                for (size_t i = 0; i < result.rgba.size(); i += 4)
                    std::swap(result.rgba[i], result.rgba[i + 2]);
            }
            return result;
        }
        return std::nullopt;
    }

    void Backend::PublishReadbackTelemetry(uint64_t bytes)
    {
        if (!_config.enableUploadTelemetry)
            return;
        Drawing::RenderUploadTelemetry telemetry;
        telemetry.auxiliary = true;
        telemetry.readbackRequests = 1;
        telemetry.readbackBytes = bytes;
        PublishTimings(Gpu::FrameTimings{ .uploadTelemetry = telemetry, .telemetryOnly = true });
    }

    void Backend::WaitIdle()
    {
        _device.WaitIdle();
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; frameIndex++)
        {
            _executor.CompleteTerrainStatus(frameIndex);
            HarvestGpuTimingsForFrame(frameIndex);
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

    void Backend::ClearActiveFrame() noexcept
    {
        if (_activeToken.has_value())
            _activeToken->submission.upload->SetTelemetry(nullptr);
        _activeTelemetry = nullptr;
        _activeToken.reset();
        _activeFrame.reset();
        _submitted = false;
        _finalCanvasComposite = false;
        _captureRequested = false;
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
            if (_config.enableUploadTelemetry && _lostTelemetrySamples != std::numeric_limits<uint64_t>::max())
                ++_lostTelemetrySamples;
            writeIndex = _completedTimingStart;
            _completedTimingStart = (_completedTimingStart + 1) % kCompletedTimingCapacity;
        }
        _completedTimings[writeIndex] = timings;
        if (!timings.telemetryOnly && (!_latestTimings.has_value() || timings.frameNumber >= _latestTimings->frameNumber))
        {
            _latestTimings = timings;
        }
    }

    std::unique_ptr<Gpu::Backend> CreateBackend(
        std::unique_ptr<PresentationHost> presentationHost, std::shared_ptr<DeviceContextOwner> owner)
    {
        return std::make_unique<Backend>(std::move(presentationHost), std::move(owner));
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
