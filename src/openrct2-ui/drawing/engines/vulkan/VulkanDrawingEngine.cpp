/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanDrawingEngine.h"

    #include "../gpu/GpuCommandDrawingContext.h"
    #include "../gpu/GpuFrameMailbox.h"
    #include "VulkanBackend.h"
    #include "VulkanPlatform.h"

    #include <SDL.h>
    #include <algorithm>
    #include <array>
    #include <atomic>
    #include <cstring>
    #include <exception>
    #include <limits>
    #include <mutex>
    #include <openrct2-ui/interface/Window.h>
    #include <openrct2/Context.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/config/Config.h>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/drawing/BlendColourMap.h>
    #include <openrct2/drawing/Drawing.h>
    #include <openrct2/drawing/LightFX.h>
    #include <openrct2/drawing/WeatherDrawer.h>
    #include <openrct2/interface/Screenshot.h>
    #include <openrct2/interface/Viewport.h>
    #include <openrct2/ui/UiContext.h>
    #include <span>
    #include <stdexcept>
    #include <thread>
    #include <vector>

namespace OpenRCT2::Ui
{
    namespace
    {
        [[nodiscard]] Gpu::Extent QueryDrawableExtentOnUiThread(IUiContext& uiContext)
        {
            const auto extent = Vulkan::Platform::GetDrawableExtent(static_cast<SDL_Window*>(uiContext.GetWindow()));
            return { extent.width, extent.height };
        }

        [[nodiscard]] Gpu::BackendConfig BuildBackendConfig(
            IUiContext& uiContext, Gpu::Extent logicalExtent, Gpu::Extent drawableExtent, bool vsync,
            std::string shaderDirectory)
        {
            return {
                .nativeWindow = uiContext.GetWindow(),
                .logicalExtent = logicalExtent,
                .drawableExtent = drawableExtent,
                .presentMode = vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate,
                .frameAcquireMode = Gpu::FrameAcquireMode::SkipIfBusy,
                .outputColorMode = Config::Get().general.enableHdr10Output ? Gpu::OutputColorMode::Hdr10IfAvailable
                                                                           : Gpu::OutputColorMode::Sdr,
                .shaderDirectory = std::move(shaderDirectory),
            };
        }

        [[nodiscard]] std::string WriteIndexedScreenshot(std::span<std::byte> pixels, uint32_t width, uint32_t height)
        {
            Drawing::RenderTarget target{};
            target.bits = reinterpret_cast<Drawing::PaletteIndex*>(pixels.data());
            target.width = static_cast<int32_t>(width);
            target.height = static_cast<int32_t>(height);
            target.pitch = 0;
            target.zoom_level = ZoomLevel{ 0 };
            return ScreenshotDumpPNG(target);
        }

        [[nodiscard]] std::array<std::byte, 256 * 4> ConvertPalette(const Drawing::GamePalette& palette)
        {
            std::array<std::byte, 256 * 4> result{};
            for (size_t i = 0; i < 256; i++)
            {
                const auto& colour = palette[i];
                result[i * 4] = static_cast<std::byte>(colour.red);
                result[i * 4 + 1] = static_cast<std::byte>(colour.green);
                result[i * 4 + 2] = static_cast<std::byte>(colour.blue);
                result[i * 4 + 3] = i == 0 ? std::byte{ 0 } : std::byte{ 0xff };
            }
            return result;
        }
    } // namespace

    namespace
    {
        class VulkanWeatherDrawer final : public Drawing::IWeatherDrawer
        {
        private:
            Gpu::CommandBatch<Gpu::WeatherCommand>* _commands = nullptr;

        public:
            void SetCommands(Gpu::CommandBatch<Gpu::WeatherCommand>& commands)
            {
                _commands = &commands;
            }

            void Draw(
                Drawing::RenderTarget&, int32_t x, int32_t y, int32_t width, int32_t height, int32_t xStart, int32_t yStart,
                const uint8_t* weatherPattern) override
            {
                if (_commands == nullptr || width <= 0 || height <= 0)
                    return;
                auto& command = _commands->allocate();
                command.bounds = { x, y, x + width, y + height };
                command.offset = { xStart, yStart };
                command.pattern = weatherPattern[3] == 32 ? 1 : 0;
            }
        };

        [[nodiscard]] std::array<std::byte, 256 * 256> BuildRemapPalette()
        {
            std::array<Drawing::PaletteIndex, 256 * 256> indices{};
            auto target = Drawing::RenderTarget{};
            target.bits = indices.data();
            target.width = 256;
            target.height = 256;
            target.pitch = 0;
            target.zoom_level = ZoomLevel{ 0 };
            for (int32_t i = 0; i < 256; i++)
                target.bits[i] = static_cast<Drawing::PaletteIndex>(i);

            for (int32_t i = 0; i < kPaletteTotalOffsets; i++)
            {
                const auto palette = static_cast<Drawing::FilterPaletteID>(i);
                const auto image = GetPaletteG1Index(palette);
                if (!image.has_value())
                    continue;
                const auto* element = GfxGetG1Element(*image);
                if (element == nullptr)
                    continue;
                const int32_t row = Gpu::TextureCache::PaletteToY(palette);
                GfxDrawSpriteSoftware(target, ImageId(*image), { -element->xOffset, row - element->yOffset });
            }
            std::array<std::byte, 256 * 256> pixels{};
            std::memcpy(pixels.data(), indices.data(), pixels.size());
            return pixels;
        }
    } // namespace

    /** The CPU render-target allocation carries legacy pointer offsets only; it is never uploaded as a framebuffer. */
    class VulkanDrawingEngine final : public Drawing::IDrawingEngine
    {
    private:
        IUiContext& _uiContext;
        std::unique_ptr<Gpu::Backend> _backend;
        Gpu::TextureCache _textureCache;
        Drawing::RenderTarget _mainTarget{};
        Gpu::CommandDrawingContext _drawingContext;
        VulkanWeatherDrawer _weatherDrawer;
        Gpu::LatestFrameMailbox _frameMailbox;
        std::unique_ptr<Gpu::RecordedFramePacket> _recordingPacket;
        std::thread _renderWorker;
        std::mutex _workerErrorMutex;
        std::exception_ptr _workerError;
        uint64_t _resizeVersion = 0;
        uint64_t _surfaceFormatVersion = 0;
        uint64_t _presentModeVersion = 0;
        uint64_t _paletteVersion = 0;
        std::vector<Drawing::PaletteIndex> _addressSpace;
        std::array<std::byte, 256 * 4> _paletteRgba{};
        Gpu::Extent _drawableExtent{};
        uint32_t _width = 0;
        uint32_t _height = 0;
        uint64_t _frameNumber = 0;
        uint64_t _graphicsLookupTablesVersion = 0;
        std::vector<std::byte> _lightFalloffs;
        std::array<std::byte, 256 * 256> _remapPalette{};
        std::array<std::byte, 256 * 256> _blendPalette{};
        bool _hasBlendPalette = false;
        bool _initialised = false;
        bool _graphicsLookupTablesReady = false;
        bool _hasPalette = false;
        bool _vsync = true;
        std::atomic_bool _gpuLightFxRasterization{ false };

    public:
        explicit VulkanDrawingEngine(IUiContext& uiContext)
            : _uiContext(uiContext)
            , _backend(Vulkan::CreateBackend())
            , _drawingContext(_mainTarget, _textureCache)
        {
            _mainTarget.DrawingEngine = this;
            _recordingPacket = CreateRecordingPacket();
            Drawing::LightFx::SetAvailable(true);
        }

        ~VulkanDrawingEngine() override
        {
            StopRenderWorker();
        }

        void Initialise() override
        {
            auto& environment = GetContext()->GetPlatformEnvironment();
            const auto shaderRoot = environment.GetDirectoryPath(DirBase::openrct2, DirId::shaders);
            const auto shaderDirectory = Path::Combine(shaderRoot, "vulkan");
            const uint32_t width = static_cast<uint32_t>(std::max(1, _uiContext.GetWidth()));
            const uint32_t height = static_cast<uint32_t>(std::max(1, _uiContext.GetHeight()));
            _drawableExtent = QueryDrawableExtentOnUiThread(_uiContext);
            _backend->Initialise(BuildBackendConfig(_uiContext, { width, height }, _drawableExtent, _vsync, shaderDirectory));
            _initialised = true;
            _gpuLightFxRasterization.store(
                _backend->SupportsGpuLightFxRasterization(), std::memory_order_relaxed);
            if (_hasPalette)
                _backend->SetPalette(_paletteRgba);
            _renderWorker = std::thread(&VulkanDrawingEngine::RenderWorkerMain, this);
        }

        void Resize(uint32_t width, uint32_t height) override
        {
            if (width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                || height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
            {
                throw std::overflow_error("Vulkan command target dimensions exceed the legacy render-target range");
            }
            if (height != 0 && width > std::numeric_limits<size_t>::max() / height)
            {
                throw std::overflow_error("Vulkan command target dimensions overflow address space");
            }
            _width = width;
            _height = height;
            _addressSpace.resize(static_cast<size_t>(width) * height);
            _mainTarget.bits = _addressSpace.data();
            _mainTarget.x = 0;
            _mainTarget.y = 0;
            _mainTarget.width = static_cast<int32_t>(width);
            _mainTarget.height = static_cast<int32_t>(height);
            _mainTarget.pitch = 0;
            _mainTarget.zoom_level = ZoomLevel{ 0 };
            _drawingContext.Resize();
            _drawableExtent = QueryDrawableExtentOnUiThread(_uiContext);
            if (_initialised)
            {
                // Until the render worker has recreated extent-dependent
                // resources, publish a CPU intensity fallback with the resize
                // packet rather than assuming the new R32 image is supported.
                _gpuLightFxRasterization.store(false, std::memory_order_relaxed);
                _resizeVersion++;
            }
        }

        void NotifyDisplayChanged() override
        {
            if (!_initialised)
            {
                return;
            }
            _surfaceFormatVersion++;
        }

        void SetPalette(const Drawing::GamePalette& palette) override
        {
            _paletteRgba = ConvertPalette(palette);
            _hasPalette = true;
            if (_initialised)
                _paletteVersion++;
        }

        void SetVSync(bool vsync) override
        {
            _vsync = vsync;
            if (_initialised)
                _presentModeVersion++;
        }

        void Invalidate(int32_t, int32_t, int32_t, int32_t) override
        {
            // Vulkan records a complete frame from one presentation generation.
        }

        [[nodiscard]] bool CanBeginFrame() override
        {
            return _frameMailbox.CanPublishVisualFrame();
        }

        bool CanSkipViewportInvalidation() const override
        {
            return true;
        }

        void BeginDraw() override
        {
            EnsureGraphicsLookupTablesReady();
            BeginDrawQueued();
        }

        void EndDraw() override
        {
            EndDrawQueued();
        }

    private:
        void EnsureGraphicsLookupTablesReady()
        {
            if (_graphicsLookupTablesReady)
            {
                return;
            }

            // Drawing-engine initialisation precedes LoadBaseGraphics and
            // LightFx::Init. Capture into temporaries on the first real draw,
            // then publish readiness only after every source table succeeded.
            auto lightFalloffs = Drawing::LightFx::CaptureBakedFalloffs();
            auto remapPalette = BuildRemapPalette();
            std::array<std::byte, 256 * 256> blendPalette{};
            bool hasBlendPalette = false;
            if (const auto* blend = Drawing::GetBlendColourMap(); blend != nullptr)
            {
                const std::span<const Drawing::BlendColourMapType> blendTable{ blend, 1 };
                std::ranges::copy(std::as_bytes(blendTable), blendPalette.begin());
                hasBlendPalette = true;
            }

            _lightFalloffs = std::move(lightFalloffs);
            _remapPalette = std::move(remapPalette);
            _blendPalette = std::move(blendPalette);
            _hasBlendPalette = hasBlendPalette;
            _graphicsLookupTablesVersion++;
            _graphicsLookupTablesReady = true;
        }

        void RefreshDrawableExtent()
        {
            const auto drawableExtent = QueryDrawableExtentOnUiThread(_uiContext);
            if (drawableExtent == _drawableExtent)
            {
                return;
            }
            _drawableExtent = drawableExtent;
            if (!_initialised)
            {
                return;
            }
            _resizeVersion++;
        }

        void CaptureLightFx(Gpu::FrameCommandStream& commands)
        {
            if (!Drawing::LightFx::IsAvailable())
            {
                commands.lightFx.reset();
                return;
            }
            const auto* viewport = WindowGetViewport(WindowGetMain());
            if (viewport == nullptr)
            {
                commands.lightFx.reset();
                return;
            }

            Drawing::LightFx::FrameSnapshot resolved;
            std::vector<Gpu::LightFxCommand> recycledLights;
            if (commands.lightFx.has_value())
            {
                resolved.intensities = std::move(commands.lightFx->intensities);
                resolved.lights.reserve(commands.lightFx->lights.capacity());
                recycledLights = std::move(commands.lightFx->lights);
            }
            if (!Drawing::LightFx::CaptureFrameSnapshot(
                    *viewport, _width, _height, resolved, !_gpuLightFxRasterization.load(std::memory_order_relaxed))
                || !resolved.IsValid())
            {
                commands.lightFx.reset();
                return;
            }
            Gpu::LightFxFrameSnapshot snapshot;
            snapshot.width = resolved.width;
            snapshot.height = resolved.height;
            for (size_t i = 0; i < resolved.lightPalette.size(); i++)
            {
                const auto& colour = resolved.lightPalette[i];
                snapshot.lightPalette[i * 4] = static_cast<std::byte>(colour.red);
                snapshot.lightPalette[i * 4 + 1] = static_cast<std::byte>(colour.green);
                snapshot.lightPalette[i * 4 + 2] = static_cast<std::byte>(colour.blue);
                snapshot.lightPalette[i * 4 + 3] = static_cast<std::byte>(colour.alpha);
            }
            snapshot.intensities = std::move(resolved.intensities);
            snapshot.lights = std::move(recycledLights);
            snapshot.lights.clear();
            snapshot.lights.reserve(resolved.lights.size());
            for (const auto& light : resolved.lights)
            {
                snapshot.lights.push_back(Gpu::MakeLightFxCommand(light));
            }
            commands.lightFx = std::move(snapshot);
        }

        [[nodiscard]] static std::unique_ptr<Gpu::RecordedFramePacket> CreateRecordingPacket()
        {
            auto packet = std::make_unique<Gpu::RecordedFramePacket>();
            packet->commands.reserveForParkView();
            return packet;
        }

        void BeginDrawQueued()
        {
            RethrowWorkerError();
            RefreshDrawableExtent();
            if (_recordingPacket == nullptr)
            {
                _recordingPacket = _frameMailbox.TakeRecycled();
                if (_recordingPacket == nullptr)
                {
                    _recordingPacket = CreateRecordingPacket();
                }
            }
            _recordingPacket->commands.clear();
            _recordingPacket->residency = {};
            _recordingPacket->readback.reset();
            _recordingPacket->timingBoundary.reset();
            _recordingPacket->hasVisualFrame = false;
            _weatherDrawer.SetCommands(_recordingPacket->commands.weather);
            _textureCache.BeginFrame();
            _drawingContext.Begin(_recordingPacket->commands);
        }

        void EndDrawQueued()
        {
            bool sealed = false;
            try
            {
                _drawingContext.End();
                CaptureLightFx(_recordingPacket->commands);
                _recordingPacket->residency = _textureCache.SealFrame(_recordingPacket->commands);
                sealed = true;
                _recordingPacket->hasVisualFrame = true;
                _recordingPacket->frameNumber = _frameNumber++;
                _recordingPacket->presentation = {
                    .logicalExtent = { _width, _height },
                    .drawableExtent = _drawableExtent,
                    .presentMode = _vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate,
                    .palette = _paletteRgba,
                    .resizeVersion = _resizeVersion,
                    .surfaceFormatVersion = _surfaceFormatVersion,
                    .presentModeVersion = _presentModeVersion,
                    .paletteVersion = _paletteVersion,
                    .graphicsLookupTablesVersion = _graphicsLookupTablesVersion,
                    .hasPalette = _hasPalette,
                };

                auto result = _frameMailbox.Publish(std::move(_recordingPacket));
                if (result.released != nullptr)
                {
                    RetirePacket(
                        *result.released, result.accepted ? Gpu::FrameRetirement::Superseded : Gpu::FrameRetirement::Shutdown);
                    result.released->commands.clear();
                    _recordingPacket = std::move(result.released);
                }
                if (!result.accepted)
                {
                    RethrowWorkerError();
                    throw std::runtime_error("Vulkan render worker is not accepting frame packets");
                }
            }
            catch (...)
            {
                try
                {
                    if (!sealed)
                    {
                        _textureCache.AbortFrame();
                    }
                    else if (_recordingPacket != nullptr && _recordingPacket->residency)
                    {
                        RetirePacket(*_recordingPacket, Gpu::FrameRetirement::Failed);
                    }
                }
                catch (...)
                {
                    // Preserve the recording or worker failure.
                }
                throw;
            }
        }

        void RenderWorkerMain() noexcept
        {
            uint64_t appliedResizeVersion = 0;
            uint64_t appliedSurfaceFormatVersion = 0;
            uint64_t appliedPresentModeVersion = 0;
            uint64_t appliedPaletteVersion = 0;
            uint64_t appliedGraphicsLookupTablesVersion = 0;

            try
            {
                while (auto packet = _frameMailbox.WaitTakeNewest())
                {
                    std::optional<Gpu::FrameHandle> frame;
                    try
                    {
                        if (!packet->hasVisualFrame)
                        {
                            CompleteTimingBoundary(*packet);
                            CompleteReadback(*packet);
                            RecyclePacket(std::move(packet));
                            continue;
                        }

                        const auto& presentation = packet->presentation;
                        if (presentation.resizeVersion != appliedResizeVersion)
                        {
                            if (presentation.logicalExtent.width != 0 && presentation.logicalExtent.height != 0)
                            {
                                _backend->Resize(presentation.logicalExtent, presentation.drawableExtent);
                                _gpuLightFxRasterization.store(
                                    _backend->SupportsGpuLightFxRasterization(), std::memory_order_relaxed);
                            }
                            appliedResizeVersion = presentation.resizeVersion;
                        }
                        if (presentation.surfaceFormatVersion != appliedSurfaceFormatVersion)
                        {
                            _backend->RequestSurfaceFormatRefresh();
                            appliedSurfaceFormatVersion = presentation.surfaceFormatVersion;
                        }
                        if (presentation.presentModeVersion != appliedPresentModeVersion)
                        {
                            _backend->SetPresentMode(presentation.presentMode);
                            appliedPresentModeVersion = presentation.presentModeVersion;
                        }
                        if (presentation.hasPalette && presentation.paletteVersion != appliedPaletteVersion)
                        {
                            _backend->SetPalette(presentation.palette);
                            appliedPaletteVersion = presentation.paletteVersion;
                        }
                        if (presentation.graphicsLookupTablesVersion != appliedGraphicsLookupTablesVersion)
                        {
                            _backend->SetLightFxFalloffs(_lightFalloffs);
                            _backend->SetRemapPalette(_remapPalette);
                            if (_hasBlendPalette)
                            {
                                _backend->SetBlendPalette(_blendPalette);
                            }
                            appliedGraphicsLookupTablesVersion = presentation.graphicsLookupTablesVersion;
                        }

                        if (presentation.logicalExtent.width != 0 && presentation.logicalExtent.height != 0
                            && presentation.drawableExtent.width != 0 && presentation.drawableExtent.height != 0)
                        {
                            if (packet->readback != nullptr)
                            {
                                // Screenshot capture is an explicit boundary at
                                // which blocking for current visual state is
                                // permitted. Routine visual packets never wait.
                                _backend->WaitIdle();
                            }
                            frame = _backend->BeginFrame(packet->frameNumber);
                        }
                        if (!frame.has_value())
                        {
                            if (packet->readback != nullptr)
                            {
                                // Never capture an older frame when the visual
                                // attached to this request was not presented.
                                packet->readback->Complete(false);
                            }
                            RetirePacket(*packet, Gpu::FrameRetirement::Busy);
                            CompleteTimingBoundary(*packet);
                            RecyclePacket(std::move(packet));
                            continue;
                        }
                        _backend->Submit(*frame, packet->commands);
                        _backend->Present(*frame);
                        frame.reset();
                        RetirePacket(*packet, Gpu::FrameRetirement::Presented);
                        CompleteTimingBoundary(*packet);
                        CompleteReadback(*packet);
                        RecyclePacket(std::move(packet));
                    }
                    catch (...)
                    {
                        const auto error = std::current_exception();
                        if (packet->readback != nullptr)
                        {
                            packet->readback->Fail(error);
                        }
                        if (packet->timingBoundary != nullptr)
                        {
                            packet->timingBoundary->Fail(error);
                        }
                        if (frame.has_value())
                        {
                            try
                            {
                                _backend->AbandonFrame(*frame);
                            }
                            catch (...)
                            {
                                // Preserve the first render-worker failure.
                            }
                        }
                        try
                        {
                            RetirePacket(*packet, Gpu::FrameRetirement::Failed);
                        }
                        catch (...)
                        {
                            // Cache-accounting failure is terminal for this worker.
                        }
                        StoreWorkerError(error);
                        RetireStoppedPacket(_frameMailbox.Stop());
                        return;
                    }
                }
            }
            catch (...)
            {
                StoreWorkerError(std::current_exception());
                RetireStoppedPacket(_frameMailbox.Stop());
            }
        }

        void RetirePacket(Gpu::RecordedFramePacket& packet, Gpu::FrameRetirement retirement)
        {
            if (packet.readback != nullptr && retirement != Gpu::FrameRetirement::Presented)
            {
                packet.readback->Fail(std::make_exception_ptr(std::runtime_error(
                    retirement == Gpu::FrameRetirement::Superseded ? "Vulkan screenshot request was superseded"
                                                                   : "Vulkan screenshot request retired before readback")));
            }
            if (packet.residency)
            {
                _textureCache.RetireFrame(packet.residency, retirement);
                packet.residency = {};
            }
        }

        void RecyclePacket(std::unique_ptr<Gpu::RecordedFramePacket> packet)
        {
            packet->commands.clear();
            packet->presentation = {};
            packet->frameNumber = 0;
            packet->readback.reset();
            packet->timingBoundary.reset();
            packet->hasVisualFrame = false;
            _frameMailbox.Recycle(std::move(packet));
        }

        void CompleteReadback(Gpu::RecordedFramePacket& packet)
        {
            if (packet.readback == nullptr)
            {
                return;
            }
            const auto extent = packet.readback->GetExtent();
            const bool available = _backend->ReadbackLatestIndexedCanvas(extent, packet.readback->GetPixels());
            packet.readback->Complete(available);
        }

        void CompleteTimingBoundary(Gpu::RecordedFramePacket& packet)
        {
            if (packet.timingBoundary == nullptr)
            {
                return;
            }
            _backend->WaitIdle();
            packet.timingBoundary->Complete();
        }

        void RetireStoppedPacket(std::unique_ptr<Gpu::RecordedFramePacket> packet) noexcept
        {
            if (packet == nullptr)
            {
                return;
            }
            try
            {
                if (packet->readback != nullptr)
                {
                    packet->readback->Fail(
                        std::make_exception_ptr(std::runtime_error("Vulkan render worker stopped before readback")));
                }
                if (packet->timingBoundary != nullptr)
                {
                    packet->timingBoundary->Fail(
                        std::make_exception_ptr(std::runtime_error("Vulkan render worker stopped before timing drain")));
                }
                RetirePacket(*packet, Gpu::FrameRetirement::Shutdown);
            }
            catch (...)
            {
                StoreWorkerError(std::current_exception());
            }
        }

        void StoreWorkerError(std::exception_ptr error) noexcept
        {
            std::scoped_lock lock(_workerErrorMutex);
            if (!_workerError)
            {
                _workerError = std::move(error);
            }
        }

        void RethrowWorkerError()
        {
            std::exception_ptr error;
            {
                std::scoped_lock lock(_workerErrorMutex);
                error = _workerError;
            }
            if (error)
            {
                std::rethrow_exception(error);
            }
        }

        void StopRenderWorker() noexcept
        {
            if (!_renderWorker.joinable())
            {
                return;
            }
            RetireStoppedPacket(_frameMailbox.Stop());
            // Only the owning UI thread destroys the engine; joining from the worker would be a lifecycle violation.
            _renderWorker.join();
            // The worker owns completion timing, but atlas maps remain single-writer state of this recording thread.
            try
            {
                _textureCache.DrainFrameRetirements();
            }
            catch (...)
            {
                StoreWorkerError(std::current_exception());
            }
            (void)_frameMailbox.TakeRecycled();
        }

    public:
        void PaintWindows() override
        {
            auto& commands = _recordingPacket->commands;
            const auto commandCount = [&commands] {
                return commands.lines.size() + commands.opaqueRects.size() + commands.opaqueSprites.size()
                    + commands.transparentRects.size();
            };
            const auto beforeViewportUpdate = commandCount();
            WindowUpdateAllViewports();
            if (commandCount() != beforeViewportUpdate)
            {
                // Shift helpers can emit provisional strip redraws. This backend always follows with the authoritative full
                // scene, so retaining those batches would apply transparent sprites twice.
                _drawingContext.End();
                commands.lines.clear();
                commands.opaqueRects.clear();
                commands.opaqueSprites.clear();
                commands.transparentRects.clear();
                _drawingContext.Begin(commands);
            }
            // Publish the immutable scene before traversing any window or viewport. The backend clears its indexed and depth
            // canvases for every admitted packet, and this traversal therefore records one complete replacement frame.
            ViewportBeginPresentationFrame();
            WindowDrawAll(_mainTarget, 0, 0, static_cast<int32_t>(_width), static_cast<int32_t>(_height));
        }

        void PaintWeather() override
        {
            DrawWeather(_mainTarget, &_weatherDrawer);
        }

        void CopyRect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) override
        {
            // Viewport shifts need no retained-canvas blit because this backend records a complete replacement frame.
        }

        std::string Screenshot() override
        {
            if (!_initialised || _width == 0 || _height == 0)
            {
                return {};
            }

            auto readback = std::make_shared<Gpu::SynchronousReadback>(Gpu::Extent{ _width, _height });
            auto result = _frameMailbox.PublishReadback(readback);
            if (result.released != nullptr)
            {
                result.released->Fail(std::make_exception_ptr(std::runtime_error(
                    result.accepted ? "Vulkan screenshot request was superseded"
                                    : "Vulkan render worker rejected a screenshot request")));
            }
            if (!result.accepted)
            {
                try
                {
                    RethrowWorkerError();
                    throw std::runtime_error("Vulkan render worker rejected a screenshot request");
                }
                catch (...)
                {
                    readback->Fail(std::current_exception());
                }
            }
            if (!readback->Wait())
            {
                return {};
            }
            return WriteIndexedScreenshot(readback->GetPixels(), _width, _height);
        }

        Drawing::IDrawingContext* GetDrawingContext() override
        {
            return _drawingContext.IsActive() ? &_drawingContext : nullptr;
        }

        Drawing::RenderTarget* getRT() override
        {
            return &_mainTarget;
        }

        DrawingEngineFlags GetFlags() override
        {
            return { DrawingEngineFlag::dirtyOptimisations };
        }

        std::optional<Drawing::FrameTimings> GetLatestFrameTimings() const override
        {
            return _backend->GetLatestTimings();
        }

        void TakeCompletedFrameTimings(std::vector<Drawing::FrameTimings>& samples) override
        {
            _backend->TakeCompletedTimings(samples);
        }

        void DrainFrameTimings(std::vector<Drawing::FrameTimings>& samples) override
        {
            auto boundary = std::make_shared<Gpu::SynchronousFrameBoundary>();
            auto result = _frameMailbox.PublishTimingBoundary(boundary);
            if (result.released != nullptr)
            {
                result.released->Fail(std::make_exception_ptr(std::runtime_error("Vulkan timing boundary was superseded")));
            }
            if (!result.accepted)
            {
                RethrowWorkerError();
                throw std::runtime_error("Vulkan render worker rejected a timing boundary");
            }
            boundary->Wait();
            RethrowWorkerError();
            _backend->TakeCompletedTimings(samples);
        }

        void InvalidateImage(uint32_t image) override
        {
            _textureCache.InvalidateImage(image);
        }
    };
    std::unique_ptr<Drawing::IDrawingEngine> CreateVulkanDrawingEngine(IUiContext& uiContext)
    {
        return std::make_unique<VulkanDrawingEngine>(uiContext);
    }
} // namespace OpenRCT2::Ui

#endif // ENABLE_VULKAN
