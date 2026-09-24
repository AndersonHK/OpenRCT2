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

    #include "VulkanPlatform.h"
    #include "VulkanScreenPalette.h"

    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2-renderer/gpu/GpuFrameMailbox.h>
    #include <openrct2-renderer/gpu/GpuGraphicsLookupTables.h>
    #include <openrct2-renderer/gpu/GpuWeatherDrawer.h>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>

    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
        #include "VulkanDiagnosticCapture.h"

        #include <openrct2/drawing/PresentationGeneration.h>
        #include <openrct2/entity/EntityPresentationSnapshot.h>
    #endif

    #include <SDL.h>
    #include <algorithm>
    #include <array>
    #include <atomic>
    #include <chrono>
    #include <cmath>
    #include <cstring>
    #include <exception>
    #include <limits>
    #include <mutex>
    #include <openrct2-ui/interface/Window.h>
    #include <openrct2/Context.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/config/Config.h>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/drawing/BlendColourMap.h>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/Drawing.h>
    #include <openrct2/drawing/G1Element.h>
    #include <openrct2/drawing/LightFX.h>
    #include <openrct2/drawing/RenderTarget.h>
    #include <openrct2/drawing/SpriteAssetDecoder.h>
    #include <openrct2/drawing/WeatherDrawer.h>
    #include <openrct2/interface/Screenshot.h>
    #include <openrct2/interface/Viewport.h>
    #include <openrct2/profiling/Profiling.h>
    #include <openrct2/ui/UiContext.h>
    #include <span>
    #include <stdexcept>
    #include <thread>
    #include <vector>

namespace OpenRCT2::Ui
{
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
    namespace Vulkan::Diagnostic
    {
        static std::atomic_bool CaptureEnabled = false;
        static std::atomic_bool RetainedBalloonPublicationEnabled = false;
        static std::atomic_bool RetainedPeepPublicationEnabled = false;
        static std::atomic_bool NativeBalloonFixtureEnabled = false;
        static std::atomic_bool NativeTerrainFixtureEnabled = false;

        void SetNativeTerrainFixtureForTesting(bool enabled)
        {
            NativeTerrainFixtureEnabled.store(enabled);
        }

        void SetNativeBalloonFixtureForTesting(bool enabled)
        {
            NativeBalloonFixtureEnabled.store(enabled);
        }

        void SetRetainedBalloonPublicationForTesting(bool enabled)
        {
            RetainedBalloonPublicationEnabled.store(enabled);
        }
        void SetRetainedPeepPublicationForTesting(bool enabled)
        {
            RetainedPeepPublicationEnabled.store(enabled);
        }

        void EnableCaptureForTesting()
        {
            CaptureEnabled.store(true);
        }
    } // namespace Vulkan::Diagnostic
    #endif
    namespace
    {
        [[nodiscard]] Gpu::Extent QueryDrawableExtentOnUiThread(IUiContext& uiContext)
        {
            const auto extent = Vulkan::Platform::GetDrawableExtent(static_cast<SDL_Window*>(uiContext.GetWindow()));
            return { extent.width, extent.height };
        }

        [[nodiscard]] Gpu::ScaleSettings CaptureScaleSettings(IUiContext& uiContext)
        {
            const auto scale = std::ceil(static_cast<double>(Config::Get().general.windowScale));
            if (!std::isfinite(scale) || scale < 1 || scale > std::numeric_limits<uint32_t>::max())
                throw std::invalid_argument("Invalid Vulkan window scale");
            Gpu::ScaleMode mode = Gpu::ScaleMode::Nearest;
            switch (uiContext.GetScaleQuality())
            {
                case ScaleQuality::linear:
                    mode = Gpu::ScaleMode::Linear;
                    break;
                case ScaleQuality::smoothNearestNeighbour:
                    mode = Gpu::ScaleMode::SmoothNearest;
                    break;
                default:
                    break;
            }
            return { mode, static_cast<uint32_t>(scale) };
        }

        [[nodiscard]] Gpu::BackendConfig BuildBackendConfig(
            IUiContext& uiContext, Gpu::Extent logicalExtent, Gpu::Extent drawableExtent, bool vsync,
            std::string shaderDirectory)
        {
            return {
                .logicalExtent = logicalExtent,
                .drawableExtent = drawableExtent,
                .presentMode = vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate,
                .frameAcquireMode = Gpu::FrameAcquireMode::SkipIfBusy,
                .outputColorMode = Config::Get().general.enableHdr10Output ? Gpu::OutputColorMode::Hdr10IfAvailable
                                                                           : Gpu::OutputColorMode::Sdr,
                .shaderDirectory = std::move(shaderDirectory),
                .scaleSettings = CaptureScaleSettings(uiContext),
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

    } // namespace

    /** The CPU render-target allocation carries legacy pointer offsets only; it is never uploaded as a framebuffer. */
    class VulkanDrawingEngine final : public Drawing::IDrawingEngine
    {
    private:
        IUiContext& _uiContext;
        std::unique_ptr<Gpu::Backend> _backend;
        std::shared_ptr<Gpu::TextureCache> _textureCache = std::make_shared<Gpu::TextureCache>();
        Drawing::RenderTarget _mainTarget{};
        Gpu::CommandDrawingContext _drawingContext;
        Gpu::WeatherDrawer _weatherDrawer;
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
        bool _hdrOutputRequested = false;
        float _hdrPaperWhiteNits = 203.0f;
        std::chrono::steady_clock::time_point _nextHdrWhiteRefresh{};
        std::atomic_bool _gpuLightFxRasterization{ false };
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
        const bool _diagnosticCaptureEnabled = Vulkan::Diagnostic::CaptureEnabled.load();
        std::shared_ptr<Vulkan::Diagnostic::CaptureRequest> _armedCapture;
        std::mutex _diagnosticMutex;
        std::weak_ptr<Vulkan::Diagnostic::CaptureRequest> _outstandingCapture;
    #endif

    public:
        explicit VulkanDrawingEngine(IUiContext& uiContext, std::shared_ptr<Vulkan::DeviceContextOwner> owner)
            : _uiContext(uiContext)
            , _backend(Vulkan::CreateBackend(
                  Vulkan::Platform::CreatePresentationHost(static_cast<SDL_Window*>(uiContext.GetWindow())), std::move(owner)))
            , _drawingContext(_mainTarget, *_textureCache)
        {
            _mainTarget.DrawingEngine = this;
            _recordingPacket = CreateRecordingPacket();
            Drawing::LightFx::SetAvailable(true);
        }

        ~VulkanDrawingEngine() override
        {
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            const auto error = std::make_exception_ptr(std::runtime_error("Vulkan diagnostic renderer shut down"));
            if (_armedCapture != nullptr)
                _armedCapture->Fail(error);
            if (_recordingPacket != nullptr && _recordingPacket->diagnosticCapture != nullptr)
                _recordingPacket->diagnosticCapture->Fail(error);
    #endif
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
            auto config = BuildBackendConfig(_uiContext, { width, height }, _drawableExtent, _vsync, shaderDirectory);
            _hdrOutputRequested = config.outputColorMode == Gpu::OutputColorMode::Hdr10IfAvailable;
            RefreshHdrWhiteOnUiThread();
            config.hdrPaperWhiteNits = _hdrPaperWhiteNits;
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            config.enableDiagnosticCapture = _diagnosticCaptureEnabled;
    #endif
            config.enableUploadTelemetry = gIntegratedBenchmark.enabled && gIntegratedBenchmark.uploadTelemetry;
            _backend->Initialise(config);
            _initialised = true;
            _gpuLightFxRasterization.store(_backend->SupportsGpuLightFxRasterization(), std::memory_order_relaxed);
            if (_hasPalette)
                _backend->SetPalette(_paletteRgba);
            _renderWorker = std::thread(&VulkanDrawingEngine::RenderWorkerMain, this);
        }

        void Resize(uint32_t width, uint32_t height) override
        {
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            // An already published packet owns its old extent and is still
            // captured before the worker applies any later resize packet.
            const auto error = std::make_exception_ptr(std::runtime_error("Vulkan diagnostic capture cancelled by resize"));
            if (_armedCapture != nullptr)
                std::exchange(_armedCapture, nullptr)->Fail(error);
            if (_recordingPacket != nullptr && _recordingPacket->diagnosticCapture != nullptr)
                _recordingPacket->diagnosticCapture->Fail(error);
    #endif
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
            // Monitor moves/HDR mode changes must not inherit another display's white.
            _nextHdrWhiteRefresh = {};
        }

        void SetPalette(const Drawing::GamePalette& palette) override
        {
            _paletteRgba = Vulkan::ConvertScreenPalette(palette);
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
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            try
            {
    #endif
                EnsureGraphicsLookupTablesReady();
                BeginDrawQueued();
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
                _recordingPacket->diagnosticCapture = std::exchange(_armedCapture, nullptr);
            }
            catch (...)
            {
                if (_armedCapture != nullptr)
                    std::exchange(_armedCapture, nullptr)->Fail(std::current_exception());
                throw;
            }
    #endif
        }

        void EndDraw() override
        {
            EndDrawQueued();
        }

    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
        std::shared_ptr<Vulkan::Diagnostic::CaptureRequest> ArmDiagnosticCapture(std::string name)
        {
            if (!_diagnosticCaptureEnabled || !_initialised)
                throw std::logic_error("Vulkan diagnostic capture was not enabled before engine creation");
            if (_drawingContext.IsActive())
                throw std::logic_error("Arm Vulkan diagnostic capture before BeginDraw");
            const std::scoped_lock lock(_diagnosticMutex);
            RethrowWorkerError();
            if (auto previous = _outstandingCapture.lock(); previous != nullptr && previous->IsPending())
                throw std::logic_error("A Vulkan diagnostic capture is already pending");
            _armedCapture = std::make_shared<Vulkan::Diagnostic::CaptureRequest>(std::move(name));
            _outstandingCapture = _armedCapture;
            return _armedCapture;
        }
    #endif

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
            auto remapPalette = Gpu::BuildRemapPalette();
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
            PROFILED_FUNCTION();

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

        void RefreshHdrWhiteOnUiThread()
        {
            if (!_hdrOutputRequested)
                return;
            const auto now = std::chrono::steady_clock::now();
            if (now < _nextHdrWhiteRefresh)
                return;
            _hdrPaperWhiteNits = Gpu::NormaliseHdrPaperWhiteNits(
                Vulkan::Platform::GetSdrWhiteNits(static_cast<SDL_Window*>(_uiContext.GetWindow())).value_or(203.0f));
            // The Windows SDR-content slider has no SDL event. Poll at most once
            // per second, never per pixel and never from the render worker.
            _nextHdrWhiteRefresh = now + std::chrono::seconds(1);
        }

        void BeginDrawQueued()
        {
            PROFILED_FUNCTION();

            RethrowWorkerError();
            RefreshDrawableExtent();
            RefreshHdrWhiteOnUiThread();
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
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            if (_recordingPacket->diagnosticCapture != nullptr)
            {
                _recordingPacket->diagnosticCapture->Fail(
                    std::make_exception_ptr(std::runtime_error("Vulkan diagnostic draw restarted before publication")));
                _recordingPacket->diagnosticCapture.reset();
            }
    #endif
            _recordingPacket->hasVisualFrame = false;
            _weatherDrawer.SetCommands(_recordingPacket->commands.weather);
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            _drawingContext.SetNativeBalloonFixture(Vulkan::Diagnostic::NativeBalloonFixtureEnabled.load());
            _drawingContext.SetNativeTerrainFixtureForTesting(Vulkan::Diagnostic::NativeTerrainFixtureEnabled.load());
    #endif
            _textureCache->BeginFrame();
            _drawingContext.Begin(_recordingPacket->commands);
        }

        void EndDrawQueued()
        {
            PROFILED_FUNCTION();

            bool sealed = false;
            try
            {
                _drawingContext.End();
                CaptureLightFx(_recordingPacket->commands);
                _recordingPacket->residency = _textureCache->SealFrame(_recordingPacket->commands);
                sealed = true;
                _recordingPacket->hasVisualFrame = true;
                _recordingPacket->frameNumber = _frameNumber++;
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
                if (_recordingPacket->diagnosticCapture != nullptr)
                    _recordingPacket->diagnosticCapture->BindFrame(
                        _recordingPacket->frameNumber, _recordingPacket->residency.value,
                        _drawingContext.GetTerrainPreparationForTesting());
    #endif
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
                    .scaleSettings = CaptureScaleSettings(_uiContext),
                    .hdrPaperWhiteNits = _hdrPaperWhiteNits,
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
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
                if (_recordingPacket != nullptr && _recordingPacket->diagnosticCapture != nullptr)
                    _recordingPacket->diagnosticCapture->Fail(std::current_exception());
    #endif
                try
                {
                    if (!sealed)
                    {
                        _textureCache->AbortFrame();
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
                        _backend->SetScaleSettings(presentation.scaleSettings);
                        _backend->SetHdrPaperWhiteNits(presentation.hdrPaperWhiteNits);
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
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
                        if (packet->diagnosticCapture != nullptr && !_backend->RequestFrameCapture(*frame))
                            throw std::runtime_error("Vulkan final SDR diagnostic capture is unsupported for this frame");
    #endif
                        _backend->Submit(*frame, packet->commands);
                        _backend->Present(*frame);
                        frame.reset();
                        RetirePacket(*packet, Gpu::FrameRetirement::Presented);
                        CompleteTimingBoundary(*packet);
                        CompleteReadback(*packet);
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
                        CompleteDiagnosticCapture(*packet);
    #endif
                        RecyclePacket(std::move(packet));
                    }
                    catch (...)
                    {
                        const auto error = std::current_exception();
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
                        if (packet->diagnosticCapture != nullptr)
                            packet->diagnosticCapture->Fail(error);
    #endif
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
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            if (packet.diagnosticCapture != nullptr && retirement != Gpu::FrameRetirement::Presented)
            {
                packet.diagnosticCapture->Fail(std::make_exception_ptr(std::runtime_error(
                    retirement == Gpu::FrameRetirement::Superseded ? "Vulkan diagnostic frame was superseded"
                        : retirement == Gpu::FrameRetirement::Busy ? "Vulkan diagnostic frame acquisition was skipped"
                                                                   : "Vulkan diagnostic frame retired without presentation")));
            }
    #endif
            if (packet.readback != nullptr && retirement != Gpu::FrameRetirement::Presented)
            {
                packet.readback->Fail(std::make_exception_ptr(std::runtime_error(
                    retirement == Gpu::FrameRetirement::Superseded ? "Vulkan screenshot request was superseded"
                                                                   : "Vulkan screenshot request retired before readback")));
            }
            if (packet.residency)
            {
                _textureCache->RetireFrame(packet.residency, retirement);
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
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            packet->diagnosticCapture.reset();
    #endif
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

    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
        void CompleteDiagnosticCapture(Gpu::RecordedFramePacket& packet)
        {
            if (packet.diagnosticCapture == nullptr || !packet.diagnosticCapture->IsPending())
                return;
            auto output = _backend->ReadbackFrameRgba(packet.frameNumber);
            if (!output.has_value())
                throw std::runtime_error("Vulkan diagnostic frame is unavailable after presentation");
            Vulkan::Diagnostic::CaptureResult result;
            result.output = std::move(*output);
            const auto extent = packet.presentation.logicalExtent;
            result.indexed.resize(static_cast<size_t>(extent.width) * extent.height);
            // The worker has not taken another packet: latest indexed is still
            // this exact successfully presented frame, not a later screenshot.
            if (!_backend->ReadbackLatestIndexedCanvas(extent, result.indexed))
                throw std::runtime_error("Vulkan diagnostic indexed canvas is unavailable");
            const auto& commands = packet.commands;
            result.terrainScenes = commands.terrainScenes;
            result.coverage = {
                .frameNumber = packet.frameNumber,
                .resizeVersion = packet.presentation.resizeVersion,
                .surfaceFormatVersion = packet.presentation.surfaceFormatVersion,
                .paletteVersion = packet.presentation.paletteVersion,
                .graphicsLookupTablesVersion = packet.presentation.graphicsLookupTablesVersion,
                .lineCount = commands.lines.size(),
                .opaqueRectCount = commands.opaqueRects.size(),
                .opaqueSpriteCount = commands.opaqueSprites.size(),
                .transparentRectCount = commands.transparentRects.size(),
                .weatherCount = commands.weather.size(),
                .nativeBalloonViewports = commands.balloons.size(),
                .cpuBalloonSpriteCalls = commands.cpuBalloonSpriteCalls,
                .worldSurfaces = commands.worldSurfaces.has_value(),
                .worldEpoch = commands.worldSurfaces.has_value() ? commands.worldSurfaces->worldEpoch : 0,
                .worldSurfaceRecordCount = commands.worldSurfaces.has_value() ? commands.worldSurfaces->recordCount : 0,
            };
            // Opt-in diagnostics inspect the captured packet; no glyph raster or
            // ordinary frame state is altered. Bounds are clipped, right/bottom exclusive.
            const auto inspectTtf = [&](const auto& batch, size_t& count) {
                for (const auto& command : batch)
                {
                    if ((static_cast<uint32_t>(command.flags) & Gpu::RectCommand::FLAG_TTF_TEXT) == 0)
                        continue;
                    const auto left = std::max(command.bounds.x, command.clip.x);
                    const auto top = std::max(command.bounds.y, command.clip.y);
                    const auto right = std::min(command.bounds.z, command.clip.z);
                    const auto bottom = std::min(command.bounds.w, command.clip.w);
                    if (left >= right || top >= bottom)
                        continue;
                    count++;
                    result.coverage.ttfClippedBoundsAndThreshold.push_back(
                        { left, top, right, bottom,
                          static_cast<int32_t>(
                              (static_cast<uint32_t>(command.flags) & Gpu::RectCommand::FLAG_TTF_HINTING_THRESHOLD_MASK)
                              >> 8) });
                }
            };
            inspectTtf(commands.opaqueRects, result.coverage.ttfOpaqueCount);
            inspectTtf(commands.transparentRects, result.coverage.ttfTransparentCount);
            // Read the already-resolved packet only. Capturing another LightFX
            // snapshot here would advance the legacy temporal resolver twice.
            if (commands.lightFx.has_value())
            {
                const auto& lightFx = *commands.lightFx;
                auto& coverage = result.coverage;
                coverage.lightFxCommandCount = lightFx.lights.size();
                coverage.lightFxCpuIntensityAttached = lightFx.HasCpuIntensity();
                const auto hashBytes = [](std::span<const std::byte> bytes) {
                    uint64_t hash = 14695981039346656037ULL;
                    for (const auto byte : bytes)
                        hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1099511628211ULL;
                    return hash;
                };
                coverage.lightFxCommandHash = hashBytes(std::as_bytes(std::span(lightFx.lights)));
                coverage.lightFxPaletteHash = hashBytes(lightFx.lightPalette);
                for (const auto& light : lightFx.lights)
                    if (light.type < coverage.lightFxTypeHistogram.size())
                        coverage.lightFxTypeHistogram[light.type]++;
            }
            packet.diagnosticCapture->Complete(std::move(result));
        }
    #endif

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
            {
                std::scoped_lock lock(_workerErrorMutex);
                if (!_workerError)
                    _workerError = error;
            }
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            {
                const std::scoped_lock lock(_diagnosticMutex);
                if (auto request = _outstandingCapture.lock())
                    request->Fail(error);
            }
    #endif
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
                _textureCache->DrainFrameRetirements();
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
            PROFILED_FUNCTION();

            WindowUpdateAllViewports();
            // Publish the immutable scene before traversing any window or viewport. The backend clears its indexed and depth
            // canvases for every admitted packet, and this traversal therefore records one complete replacement frame.
            ViewportBeginPresentationFrame();
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            if (_recordingPacket->diagnosticCapture != nullptr)
            {
                // Latch from the immutable generation used below, never the worker's later live registry state.
                const auto generation = ViewportGetPresentationGeneration();
                if (generation == nullptr || generation->entities == nullptr)
                    throw std::runtime_error("Named Vulkan paint has no entity publication");
                Vulkan::Diagnostic::BalloonPublication publication;
                publication.retained = generation->balloons != nullptr;
                publication.entityCount = generation->entities->GetCapturedEntityCount();
                publication.metrics = generation->entities->GetBalloonMetrics();
                publication.producerTotals = ViewportGetBalloonPublicationCopyTotals();
                if (publication.retained)
                {
                    const auto& balloons = *generation->balloons;
                    publication.epoch = balloons.epoch;
                    publication.sequence = balloons.sequence;
                    publication.count = balloons.count;
                    for (const auto& chunk : balloons.chunks)
                    {
                        if (chunk == nullptr)
                            continue;
                        for (size_t i = 0; i < chunk->records.size(); i++)
                        {
                            if (chunk->records[i].present == 0)
                                continue;
                            const auto& balloon = chunk->compatibility[i];
                            publication.records.push_back(
                                { balloon.id.ToUnderlying(), balloon.x, balloon.y, balloon.z, balloon.frame, balloon.popped,
                                  balloon.timeToMove, static_cast<uint8_t>(balloon.colour), balloon.spriteData.width,
                                  balloon.spriteData.heightMin, balloon.spriteData.heightMax, balloon.orientation });
                        }
                    }
                }
                _recordingPacket->diagnosticCapture->BindBalloonPublication(std::move(publication));
                if (Vulkan::Diagnostic::NativeTerrainFixtureEnabled.load())
                    _recordingPacket->diagnosticCapture->BindTerrainPublication(generation->map);
            }
    #endif
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

        EntityPublicationProfile GetEntityPublicationProfile() const override
        {
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
            if (Vulkan::Diagnostic::RetainedPeepPublicationEnabled.load())
                return EntityPublicationProfile::nativePeeps;
            if (Vulkan::Diagnostic::RetainedBalloonPublicationEnabled.load())
                return EntityPublicationProfile::retainedBalloons;
    #endif
            return EntityPublicationProfile::gpuTerrainOnly;
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
            _textureCache->InvalidateImage(image);
        }
    };
    std::unique_ptr<Drawing::IDrawingEngine> CreateVulkanDrawingEngine(
        IUiContext& uiContext, std::shared_ptr<Vulkan::DeviceContextOwner> owner)
    {
        return std::make_unique<VulkanDrawingEngine>(uiContext, std::move(owner));
    }
    #ifdef OPENRCT2_VULKAN_DIAGNOSTICS
    std::shared_ptr<Vulkan::Diagnostic::CaptureRequest> Vulkan::Diagnostic::ArmNextCapture(
        Drawing::IDrawingEngine& engine, std::string name)
    {
        auto* vulkan = dynamic_cast<VulkanDrawingEngine*>(&engine);
        if (vulkan == nullptr)
            throw std::invalid_argument("Vulkan diagnostic capture requires the Vulkan drawing engine");
        return vulkan->ArmDiagnosticCapture(std::move(name));
    }
    #endif
} // namespace OpenRCT2::Ui

#endif // ENABLE_VULKAN
