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

    #include "../gpu/GpuTransparencyDepth.h"

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

        template<size_t N>
        void CopyExact(std::span<const std::byte> source, std::array<std::byte, N>& destination, const char* error)
        {
            if (source.size() != destination.size())
                throw std::invalid_argument(error);
            std::copy(source.begin(), source.end(), destination.begin());
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
            config.presentMode == Gpu::PresentMode::VSync, static_cast<VkDeviceSize>(config.uploadRingBytesPerFrame),
            config.outputColorMode == Gpu::OutputColorMode::Hdr10IfAvailable, config.hdrPaperWhiteNits);
        try
        {
            const bool gpuLightFxSupported = LightFxPipeline::IsSupported(_device, config.logicalExtent);
            _resources.Initialise(_device, config.logicalExtent, gpuLightFxSupported);
            InitialiseDrawingPipelines(gpuLightFxSupported);
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
        }
        for (size_t i = 0; i < _pendingRemapPalette.size(); i++)
            _pendingRemapPalette[i] = static_cast<std::byte>(i & 0xff);
        _pendingBlendPalette = _pendingRemapPalette;
        _framePaletteVersions.fill(0);
        _remapPaletteDirty = true;
        _blendPaletteDirty = true;
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
        DisposeDrawingPipelines();
        _resources.Dispose();
        _device.Dispose();
        _config = {};
        ClearActiveFrame();
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
        _lastPresentedFrameIndex.reset();
        _lastPresentedCanvasComposite = false;
        _ready = false;
    }

    bool Backend::SupportsGpuLightFxRasterization() const noexcept
    {
        return _lightFxPipeline.IsAvailable();
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
        _config.logicalExtent = logicalExtent;
        _lastPresentedFrameIndex.reset();
        _lastPresentedCanvasComposite = false;
        _ready = false;
        DisposeDrawingPipelines();
        try
        {
            const bool gpuLightFxSupported = LightFxPipeline::IsSupported(_device, logicalExtent);
            _resources.Resize(logicalExtent, gpuLightFxSupported);
            InitialiseDrawingPipelines(gpuLightFxSupported);
            _palettePipeline.RefreshDescriptors(_resources);
            _device.RequestSwapchainRecreate();
            _ready = true;
        }
        catch (...)
        {
            DisposeDrawingPipelines();
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
        const auto frameIndex = _device.GetCurrentFrameIndex();
        _activeToken = _device.BeginFrame(waitForAvailability);
        HarvestGpuTimingsForFrame(frameIndex);
        if (!_activeToken.has_value())
        {
            return std::nullopt;
        }
        _palettePipeline.RefreshSwapchain(_device);

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

    void Backend::SetPalette(std::span<const std::byte> rgba)
    {
        CopyExact(rgba, _pendingPalette, "GPU palettes must contain exactly 256 RGBA8 entries");
        _paletteVersion++;
    }

    void Backend::SetRemapPalette(std::span<const std::byte> indices)
    {
        CopyExact(indices, _pendingRemapPalette, "GPU remap palettes must contain exactly 256 by 256 indices");
        _remapPaletteDirty = true;
    }

    void Backend::SetBlendPalette(std::span<const std::byte> indices)
    {
        CopyExact(indices, _pendingBlendPalette, "GPU blend palettes must contain exactly 256 by 256 indices");
        _blendPaletteDirty = true;
    }

    void Backend::SetLightFxFalloffs(std::span<const std::byte> layers)
    {
        if (layers.size() != 8 * 256 * 256)
            throw std::invalid_argument("GPU LightFX falloffs have an invalid size");
        _pendingLightFalloffs.assign(layers.begin(), layers.end());
        _lightFalloffsDirty = true;
    }

    void Backend::Submit(const Gpu::FrameHandle& frame, const Gpu::FrameCommandStream& commands)
    {
        ValidateActiveFrame(frame);
        const auto start = Clock::now();
        RecordPendingPalette();
        RecordPendingIndexTable(_pendingRemapPalette, _remapPaletteDirty, false);
        RecordPendingIndexTable(_pendingBlendPalette, _blendPaletteDirty, true);
        RecordPendingLightFalloffs();
        RecordTextureUploads(commands);
        _device.RecordGpuTimestamp(*_activeToken, GpuTimestampPoint::uploadsComplete);
        // Presented frames are complete generations. Clearing indexed colour and depth makes dropped generations harmless.
        _resources.RecordCanvasAndDepthClear(_activeToken->commandBuffer, _activeToken->frameIndex, 0);
        if (commands.worldSurfaces.has_value())
            _worldSurfacePipeline.Record(*_activeToken, *commands.worldSurfaces);
        _linePipeline.Record(*_activeToken, commands.lines);
        _rectPipeline.Record(*_activeToken, commands.opaqueRects, commands.opaqueSprites);
        bool finalComposite = false;
        if (!commands.transparentRects.empty())
        {
            const auto layers = Gpu::MaxTransparencyDepth(commands.transparentRects);
            finalComposite = _transparencyPipeline.Record(*_activeToken, commands.transparentRects, layers);
        }
        _weatherPipeline.Record(*_activeToken, commands.weather, finalComposite);
        _device.RecordGpuTimestamp(*_activeToken, GpuTimestampPoint::indexedDrawComplete);
        const bool lightFxEnabled = RecordLightFx(commands);
        _device.RecordGpuTimestamp(*_activeToken, GpuTimestampPoint::lightFxComplete);
        const auto& finalCanvas = finalComposite ? _resources.GetCompositeCanvas(_activeToken->frameIndex)
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
            ClearActiveFrame();
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
        ClearActiveFrame();
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

        ClearActiveFrame();
        _frameTimings[frame.frameSlot].reset();
        // Upload commands recorded into an abandoned command buffer never
        // reached the GPU. Queue all small lookup resources again next frame.
        _resources.DiscardFrameLayouts(frameIndex);
        _worldSurfacePipeline.DiscardPendingUploads();
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
            destination.first(static_cast<size_t>(byteSize)));
        return true;
    }

    void Backend::WaitIdle()
    {
        _device.WaitIdle();
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; frameIndex++)
        {
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

    void Backend::InitialiseDrawingPipelines(bool gpuLightFxSupported)
    {
        _linePipeline.Initialise(_device, _resources, _config.shaderDirectory);
        _worldSurfacePipeline.Initialise(_device, _resources, _config.shaderDirectory);
        _rectPipeline.Initialise(_device, _resources, _config.shaderDirectory);
        _transparencyPipeline.Initialise(_device, _resources, _config.shaderDirectory);
        _weatherPipeline.Initialise(_device, _resources, _config.shaderDirectory);
        _lightFxPipeline.Initialise(_device, _resources, _config.shaderDirectory, gpuLightFxSupported);
    }

    void Backend::DisposeDrawingPipelines()
    {
        _lightFxPipeline.Dispose();
        _weatherPipeline.Dispose();
        _transparencyPipeline.Dispose();
        _rectPipeline.Dispose();
        _worldSurfacePipeline.Dispose();
        _linePipeline.Dispose();
    }

    void Backend::ClearActiveFrame() noexcept
    {
        _activeToken.reset();
        _activeFrame.reset();
        _submitted = false;
        _finalCanvasComposite = false;
    }

    UploadAllocation Backend::StageUpload(std::span<const std::byte> source, const char* errorMessage)
    {
        auto allocation = _activeToken->upload->Allocate(source.size(), alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error(errorMessage);
        }
        std::memcpy(allocation.data, source.data(), source.size());
        return allocation;
    }

    void Backend::RecordPendingPalette()
    {
        const auto frameIndex = _activeToken->frameIndex;
        if (_framePaletteVersions[frameIndex] == _paletteVersion)
        {
            return;
        }
        const auto allocation = StageUpload(_pendingPalette, "Vulkan upload ring has no room for the palette");
        _resources.RecordPaletteUpload(_activeToken->commandBuffer, frameIndex, allocation);
        _framePaletteVersions[frameIndex] = _paletteVersion;
    }

    void Backend::RecordPendingIndexTable(std::span<const std::byte> indices, bool& dirty, bool blend)
    {
        if (!dirty)
        {
            return;
        }
        const auto allocation = StageUpload(
            indices, blend ? "Vulkan upload ring has no room for the blend palette"
                           : "Vulkan upload ring has no room for the remap palette");
        _resources.RecordIndexTableUpload(_activeToken->commandBuffer, allocation, blend);
        dirty = false;
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

        const auto palette = StageUpload(snapshot.lightPalette, "Vulkan upload ring has no room for the LightFX palette");
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
        const auto intensities = StageUpload(snapshot.intensities, "Vulkan upload ring has no room for the LightFX snapshot");
        _resources.RecordLightFxUpload(
            _activeToken->commandBuffer, _activeToken->frameIndex, intensities, palette, snapshot.width, snapshot.height);
        _palettePipeline.SetLightMapSource(_activeToken->frameIndex, _resources.GetLightMap(_activeToken->frameIndex));
        return true;
    }

    void Backend::RecordPendingLightFalloffs()
    {
        if (!_lightFxPipeline.IsAvailable() || !_lightFalloffsDirty || _pendingLightFalloffs.empty())
            return;
        const auto allocation = StageUpload(_pendingLightFalloffs, "Vulkan upload ring has no room for LightFX falloffs");
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

        _resources.BeginAtlasUploads(_activeToken->commandBuffer);
        for (const auto& upload : commands.textureUploads)
        {
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            if (size > std::numeric_limits<size_t>::max() || static_cast<size_t>(size) != upload.pixels.size())
            {
                throw std::invalid_argument("Vulkan texture upload payload does not match its bounds and pitch");
            }
            const auto allocation = StageUpload(upload.pixels, "Vulkan upload ring has no room for a sprite atlas upload");
            const std::span<const Gpu::SpriteAssetDescriptor> descriptor{ &upload.descriptor, 1 };
            const auto descriptorAllocation = StageUpload(
                std::as_bytes(descriptor), "Vulkan upload ring has no room for a sprite descriptor upload");
            _resources.RecordAtlasUpload(
                _activeToken->commandBuffer, allocation, upload.atlas, upload.bounds, upload.sourcePitch);
            _resources.RecordSpriteDescriptorUpload(
                _activeToken->commandBuffer, descriptorAllocation, upload.descriptorIndex);
        }
        _resources.EndAtlasUploads(_activeToken->commandBuffer);
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
