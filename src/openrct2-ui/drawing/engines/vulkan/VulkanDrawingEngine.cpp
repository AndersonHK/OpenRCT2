/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#if defined(ENABLE_VULKAN) && defined(ENABLE_VULKAN_DRAWING_ENGINE)

    #include "../DrawingEngineFactory.hpp"
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
    #include <openrct2/drawing/X8DrawingEngine.h>
    #include <openrct2/interface/Screenshot.h>
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

    #if defined(ENABLE_VULKAN_DIRECT_DRAWING_CONTEXT)
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

    /**
     * Direct indexed-command activation path. The CPU allocation behind the
     * render target carries pointer offsets for legacy clipping only; it is
     * never treated as a framebuffer or uploaded to Vulkan.
     */
    class VulkanDirectDrawingEngine final : public Drawing::IDrawingEngine
    {
    private:
        IUiContext& _uiContext;
        std::unique_ptr<Gpu::Backend> _backend;
        Gpu::TextureCache _textureCache;
        Drawing::RenderTarget _mainTarget{};
        Gpu::CommandDrawingContext _drawingContext;
        VulkanWeatherDrawer _weatherDrawer;
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
        Gpu::LatestFrameMailbox _frameMailbox;
        std::unique_ptr<Gpu::RecordedFramePacket> _recordingPacket;
        std::thread _renderWorker;
        std::mutex _workerErrorMutex;
        std::exception_ptr _workerError;
        uint64_t _resizeVersion = 0;
        uint64_t _surfaceFormatVersion = 0;
        uint64_t _presentModeVersion = 0;
        uint64_t _paletteVersion = 0;
        #else
        Gpu::FrameCommandStream _commands;
        std::optional<Gpu::FrameHandle> _frame;
        #endif
        std::vector<Drawing::PaletteIndex> _addressSpace;
        std::array<std::byte, 256 * 4> _paletteRgba{};
        Gpu::Extent _drawableExtent{};
        uint32_t _width = 0;
        uint32_t _height = 0;
        uint64_t _frameNumber = 0;
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
        uint64_t _graphicsLookupTablesVersion = 0;
        std::vector<std::byte> _lightFalloffs;
        std::array<std::byte, 256 * 256> _remapPalette{};
        std::array<std::byte, 256 * 256> _blendPalette{};
        bool _hasBlendPalette = false;
        #endif
        bool _initialised = false;
        bool _graphicsLookupTablesReady = false;
        bool _hasPalette = false;
        bool _vsync = true;
        std::atomic_bool _gpuLightFxRasterization{ false };

    public:
        explicit VulkanDirectDrawingEngine(IUiContext& uiContext)
            : _uiContext(uiContext)
            , _backend(Vulkan::CreateBackend())
            , _drawingContext(_mainTarget, _textureCache)
        {
            _mainTarget.DrawingEngine = this;
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            _recordingPacket = CreateRecordingPacket();
        #else
            _commands.reserveForParkView();
        #endif
            Drawing::LightFx::SetAvailable(true);
        }

        ~VulkanDirectDrawingEngine() override
        {
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            StopRenderWorker();
        #endif
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
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            _renderWorker = std::thread(&VulkanDirectDrawingEngine::RenderWorkerMain, this);
        #endif
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
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            {
                // Until the render worker has recreated extent-dependent
                // resources, publish a CPU intensity fallback with the resize
                // packet rather than assuming the new R32 image is supported.
                _gpuLightFxRasterization.store(false, std::memory_order_relaxed);
                _resizeVersion++;
            }
        #else
            {
                _backend->Resize({ width, height }, _drawableExtent);
                _gpuLightFxRasterization.store(
                    _backend->SupportsGpuLightFxRasterization(), std::memory_order_relaxed);
            }
        #endif
        }

        void NotifyDisplayChanged() override
        {
            if (!_initialised)
            {
                return;
            }
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            _surfaceFormatVersion++;
        #else
            _backend->RequestSurfaceFormatRefresh();
        #endif
        }

        void SetPalette(const Drawing::GamePalette& palette) override
        {
            _paletteRgba = ConvertPalette(palette);
            _hasPalette = true;
            if (_initialised)
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
                _paletteVersion++;
        #else
                _backend->SetPalette(_paletteRgba);
        #endif
        }

        void SetVSync(bool vsync) override
        {
            _vsync = vsync;
            if (_initialised)
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
                _presentModeVersion++;
        #else
                _backend->SetPresentMode(vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate);
        #endif
        }

        void Invalidate(int32_t, int32_t, int32_t, int32_t) override
        {
            // Direct activation deliberately redraws the complete command list.
        }

        void BeginDraw() override
        {
            EnsureGraphicsLookupTablesReady();
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            BeginDrawQueued();
        #else
            RefreshDrawableExtent();
            _commands.clear();
            _frame = _initialised && _width != 0 && _height != 0 && _drawableExtent.width != 0 && _drawableExtent.height != 0
                ? _backend->BeginFrame(_frameNumber++)
                : std::nullopt;
            _weatherDrawer.SetCommands(_commands.weather);
            _textureCache.BeginFrame();
            _drawingContext.Begin(_commands);
        #endif
        }

        void EndDraw() override
        {
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            EndDrawQueued();
        #else
            EndDrawSynchronous();
        #endif
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

        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            _lightFalloffs = std::move(lightFalloffs);
            _remapPalette = std::move(remapPalette);
            _blendPalette = std::move(blendPalette);
            _hasBlendPalette = hasBlendPalette;
            _graphicsLookupTablesVersion++;
        #else
            _backend->SetLightFxFalloffs(lightFalloffs);
            _backend->SetRemapPalette(remapPalette);
            if (hasBlendPalette)
            {
                _backend->SetBlendPalette(blendPalette);
            }
        #endif
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
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
            _resizeVersion++;
        #else
            _backend->Resize({ _width, _height }, _drawableExtent);
        #endif
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

        #if !defined(ENABLE_VULKAN_RENDER_THREAD)
        void EndDrawSynchronous()
        {
            _drawingContext.End();
            CaptureLightFx(_commands);
            std::optional<Gpu::AtlasResidencyToken> residency;
            try
            {
                residency = _textureCache.SealFrame(_commands);
                if (!_frame.has_value())
                {
                    _textureCache.RetireFrame(*residency, Gpu::FrameRetirement::Failed);
                    residency.reset();
                    return;
                }
                _backend->Submit(*_frame, _commands);
                _backend->Present(*_frame);
                _frame.reset();
                _textureCache.RetireFrame(*residency, Gpu::FrameRetirement::Presented);
                residency.reset();
            }
            catch (...)
            {
                if (_frame.has_value())
                {
                    try
                    {
                        _backend->AbandonFrame(*_frame);
                    }
                    catch (...)
                    {
                        // Preserve the rendering failure which triggered the
                        // cancellation. The backend disables itself if the
                        // cancellation could not restore frame invariants.
                    }
                }
                _frame.reset();
                try
                {
                    if (residency.has_value())
                    {
                        _textureCache.RetireFrame(*residency, Gpu::FrameRetirement::Failed);
                    }
                    else
                    {
                        _textureCache.AbortFrame();
                    }
                }
                catch (...)
                {
                    // Preserve the rendering failure. A cache-accounting
                    // failure is terminal for this direct renderer instance.
                }
                throw;
            }
        }
        #else
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
            try
            {
                std::scoped_lock lock(_workerErrorMutex);
                if (!_workerError)
                {
                    _workerError = std::move(error);
                }
            }
            catch (...)
            {
                // Nothing useful can be reported if even error publication fails.
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
            try
            {
                _renderWorker.join();
            }
            catch (...)
            {
                StoreWorkerError(std::current_exception());
            }
            (void)_frameMailbox.TakeRecycled();
        }
        #endif

    public:
        void PaintWindows() override
        {
            WindowUpdateAllViewports();
            WindowDrawAll(_mainTarget, 0, 0, static_cast<int32_t>(_width), static_cast<int32_t>(_height));
        }

        void PaintWeather() override
        {
            DrawWeather(_mainTarget, &_weatherDrawer);
        }

        void CopyRect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) override
        {
            // No dirty-region or viewport-copy optimisation is advertised.
        }

        std::string Screenshot() override
        {
            if (!_initialised || _width == 0 || _height == 0)
            {
                return {};
            }

        #if defined(ENABLE_VULKAN_RENDER_THREAD)
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
        #else
            std::vector<std::byte> pixels(static_cast<size_t>(_width) * _height);
            if (!_backend->ReadbackLatestIndexedCanvas({ _width, _height }, pixels))
            {
                return {};
            }
            return WriteIndexedScreenshot(pixels, _width, _height);
        #endif
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
            return {};
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
        #if defined(ENABLE_VULKAN_RENDER_THREAD)
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
        #else
            _backend->WaitIdle();
        #endif
            _backend->TakeCompletedTimings(samples);
        }

        void InvalidateImage(uint32_t image) override
        {
            _textureCache.InvalidateImage(image);
        }
    };
    #endif // ENABLE_VULKAN_DIRECT_DRAWING_CONTEXT

    /**
     * Opt-in integration bridge for exercising the Vulkan frame lifecycle.
     * Drawing still uses the established indexed X8 context and uploads that
     * canvas asynchronously. The direct Vulkan command recorder remains the
     * final activation gate; this bridge is intentionally not user-selectable
     * in normal builds.
     */
    class VulkanDrawingEngine final : public Drawing::X8DrawingEngine
    {
    private:
        IUiContext& _uiContext;
        std::unique_ptr<Gpu::Backend> _backend;
        uint64_t _frameNumber = 0;
        bool _initialised = false;
        bool _vsync = true;
        bool _hasPalette = false;
        std::array<std::byte, 256 * 4> _paletteRgba{};
        Gpu::Extent _drawableExtent{};

    public:
        explicit VulkanDrawingEngine(IUiContext& uiContext)
            : X8DrawingEngine(uiContext)
            , _uiContext(uiContext)
            , _backend(Vulkan::CreateBackend())
        {
            Drawing::LightFx::SetAvailable(false);
        }

        void Initialise() override
        {
            X8DrawingEngine::Initialise();
            auto& environment = GetContext()->GetPlatformEnvironment();
            const auto shaderRoot = environment.GetDirectoryPath(DirBase::openrct2, DirId::shaders);
            const auto shaderDirectory = Path::Combine(shaderRoot, "vulkan");
            const auto initialWidth = static_cast<uint32_t>(std::max(1, _uiContext.GetWidth()));
            const auto initialHeight = static_cast<uint32_t>(std::max(1, _uiContext.GetHeight()));
            _drawableExtent = QueryDrawableExtentOnUiThread(_uiContext);
            const uint64_t canvasBytes = static_cast<uint64_t>(initialWidth) * initialHeight;
            auto config = BuildBackendConfig(
                _uiContext, { initialWidth, initialHeight }, _drawableExtent, _vsync, shaderDirectory);
            config.uploadRingBytesPerFrame = std::max<uint64_t>(64 * 1024 * 1024, canvasBytes + 1024 * 1024);
            _backend->Initialise(config);
            _initialised = true;
            if (_hasPalette)
            {
                _backend->SetPalette(_paletteRgba);
            }
        }

        void Resize(uint32_t width, uint32_t height) override
        {
            X8DrawingEngine::Resize(width, height);
            _drawableExtent = QueryDrawableExtentOnUiThread(_uiContext);
            if (_initialised)
            {
                _backend->Resize({ width, height }, _drawableExtent);
            }
        }

        void NotifyDisplayChanged() override
        {
            if (_initialised)
            {
                _backend->RequestSurfaceFormatRefresh();
            }
        }

        void SetPalette(const Drawing::GamePalette& palette) override
        {
            X8DrawingEngine::SetPalette(palette);
            _paletteRgba = ConvertPalette(palette);
            _hasPalette = true;
            if (_initialised)
            {
                _backend->SetPalette(_paletteRgba);
            }
        }

        void SetVSync(bool vsync) override
        {
            _vsync = vsync;
            if (_initialised)
            {
                _backend->SetPresentMode(vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate);
            }
        }

        void EndDraw() override
        {
            X8DrawingEngine::EndDraw();
            RefreshDrawableExtent();
            if (!_initialised || _width == 0 || _height == 0 || _drawableExtent.width == 0 || _drawableExtent.height == 0)
            {
                return;
            }
            const auto frame = _backend->BeginFrame(_frameNumber++);
            if (!frame.has_value())
            {
                return;
            }
            const uint64_t byteSize = static_cast<uint64_t>(_pitch) * _height;
            if (byteSize > std::numeric_limits<uint32_t>::max())
            {
                throw std::overflow_error("Vulkan integration canvas exceeds command-stream limits");
            }
            try
            {
                auto upload = _backend->AllocateUpload(byteSize, alignof(uint32_t));
                if (!upload)
                {
                    throw std::runtime_error("Vulkan upload ring has no room for the indexed integration canvas");
                }
                if (upload.offset > std::numeric_limits<uint32_t>::max())
                {
                    throw std::overflow_error("Vulkan canvas upload offset exceeds command-stream limits");
                }
                std::memcpy(upload.bytes.data(), _bits, static_cast<size_t>(byteSize));
                Gpu::FrameCommandStream commands;
                commands.canvasUpload = Gpu::CanvasUpload{ static_cast<uint32_t>(upload.offset), _pitch, _width, _height };
                _backend->Submit(*frame, commands);
                _backend->Present(*frame);
            }
            catch (...)
            {
                try
                {
                    _backend->AbandonFrame(*frame);
                }
                catch (...)
                {
                    // Preserve the original rendering failure.
                }
                throw;
            }
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
            _backend->WaitIdle();
            _backend->TakeCompletedTimings(samples);
        }

    private:
        void RefreshDrawableExtent()
        {
            const auto drawableExtent = QueryDrawableExtentOnUiThread(_uiContext);
            if (drawableExtent == _drawableExtent)
            {
                return;
            }
            _drawableExtent = drawableExtent;
            if (_initialised)
            {
                _backend->Resize({ _width, _height }, _drawableExtent);
            }
        }
    };

    std::unique_ptr<Drawing::IDrawingEngine> CreateVulkanDrawingEngine(IUiContext& uiContext)
    {
    #if defined(ENABLE_VULKAN_DIRECT_DRAWING_CONTEXT)
        return std::make_unique<VulkanDirectDrawingEngine>(uiContext);
    #else
        return std::make_unique<VulkanDrawingEngine>(uiContext);
    #endif
    }
} // namespace OpenRCT2::Ui

#endif // ENABLE_VULKAN && ENABLE_VULKAN_DRAWING_ENGINE
