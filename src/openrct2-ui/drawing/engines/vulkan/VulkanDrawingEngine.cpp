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
    #include "VulkanBackend.h"

    #include <SDL.h>
    #include <algorithm>
    #include <array>
    #include <cstring>
    #include <limits>
    #include <openrct2/Context.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/drawing/LightFX.h>
    #include <openrct2/drawing/BlendColourMap.h>
    #include <openrct2/drawing/Drawing.h>
    #include <openrct2/drawing/WeatherDrawer.h>
    #include <openrct2/drawing/X8DrawingEngine.h>
    #include <openrct2-ui/interface/Window.h>
    #include <openrct2/ui/UiContext.h>
    #include <stdexcept>
    #include <span>
    #include <vector>

namespace OpenRCT2::Ui
{
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
                Drawing::RenderTarget&, int32_t x, int32_t y, int32_t width, int32_t height, int32_t xStart,
                int32_t yStart, const uint8_t* weatherPattern) override
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
        Gpu::FrameCommandStream _commands;
        std::optional<Gpu::FrameHandle> _frame;
        std::vector<Drawing::PaletteIndex> _addressSpace;
        std::array<std::byte, 256 * 4> _paletteRgba{};
        uint32_t _width = 0;
        uint32_t _height = 0;
        uint64_t _frameNumber = 0;
        bool _initialised = false;
        bool _hasPalette = false;
        bool _vsync = true;

    public:
        explicit VulkanDirectDrawingEngine(IUiContext& uiContext)
            : _uiContext(uiContext)
            , _backend(Vulkan::CreateBackend())
            , _drawingContext(_mainTarget, _textureCache)
        {
            _mainTarget.DrawingEngine = this;
            _commands.reserveForParkView();
            Drawing::LightFx::SetAvailable(false);
        }

        void Initialise() override
        {
            auto& environment = GetContext()->GetPlatformEnvironment();
            const auto shaderRoot = environment.GetDirectoryPath(DirBase::openrct2, DirId::shaders);
            const auto shaderDirectory = Path::Combine(shaderRoot, "vulkan");
            const uint32_t width = static_cast<uint32_t>(std::max(1, _uiContext.GetWidth()));
            const uint32_t height = static_cast<uint32_t>(std::max(1, _uiContext.GetHeight()));
            _backend->Initialise({
                .nativeWindow = _uiContext.GetWindow(),
                .logicalExtent = { width, height },
                .presentMode = _vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate,
                .shaderDirectory = shaderDirectory,
            });
            _initialised = true;
            const auto remap = BuildRemapPalette();
            _backend->SetRemapPalette(remap);
            if (const auto* blend = Drawing::GetBlendColourMap(); blend != nullptr)
            {
                const std::span<const Drawing::BlendColourMapType> blendTable{ blend, 1 };
                _backend->SetBlendPalette(std::as_bytes(blendTable));
            }
            if (_hasPalette)
                _backend->SetPalette(_paletteRgba);
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
            if (_initialised && width != 0 && height != 0)
                _backend->Resize({ width, height });
        }

        void SetPalette(const Drawing::GamePalette& palette) override
        {
            for (size_t i = 0; i < 256; i++)
            {
                _paletteRgba[i * 4] = static_cast<std::byte>(palette[i].red);
                _paletteRgba[i * 4 + 1] = static_cast<std::byte>(palette[i].green);
                _paletteRgba[i * 4 + 2] = static_cast<std::byte>(palette[i].blue);
                _paletteRgba[i * 4 + 3] = i == 0 ? std::byte{ 0 } : std::byte{ 0xff };
            }
            _hasPalette = true;
            if (_initialised)
                _backend->SetPalette(_paletteRgba);
        }

        void SetVSync(bool vsync) override
        {
            _vsync = vsync;
            if (_initialised)
                _backend->SetPresentMode(vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate);
        }

        void Invalidate(int32_t, int32_t, int32_t, int32_t) override
        {
            // Direct activation deliberately redraws the complete command list.
        }

        void BeginDraw() override
        {
            _commands.clear();
            _frame = _initialised && _width != 0 && _height != 0
                ? _backend->BeginFrame(_frameNumber++)
                : std::nullopt;
            _weatherDrawer.SetCommands(_commands.weather);
            _textureCache.BeginFrame();
            _drawingContext.Begin(_commands);
        }

        void EndDraw() override
        {
            _drawingContext.End();
            if (!_frame.has_value())
            {
                _textureCache.EndFrame();
                return;
            }
            bool cacheFrameEnded = false;
            try
            {
                _textureCache.FlushPendingUploads(*_backend, _commands);
                _textureCache.EndFrame();
                cacheFrameEnded = true;
                _backend->Submit(*_frame, _commands);
                _backend->Present(*_frame);
                _frame.reset();
            }
            catch (...)
            {
                if (!cacheFrameEnded)
                {
                    _textureCache.EndFrame();
                }
                _frame.reset();
                throw;
            }
        }

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
            // Synchronous screenshot adaptation remains an activation gate;
            // returning no path is preferable to reading stale metadata bytes.
            return {};
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
            const uint64_t canvasBytes = static_cast<uint64_t>(initialWidth) * initialHeight;
            const Gpu::BackendConfig config = {
                .nativeWindow = _uiContext.GetWindow(),
                .logicalExtent = { initialWidth, initialHeight },
                .presentMode = _vsync ? Gpu::PresentMode::VSync : Gpu::PresentMode::Immediate,
                .uploadRingBytesPerFrame = std::max<uint64_t>(64 * 1024 * 1024, canvasBytes + 1024 * 1024),
                .shaderDirectory = shaderDirectory,
            };
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
            if (_initialised && width != 0 && height != 0)
            {
                _backend->Resize({ width, height });
            }
        }

        void SetPalette(const Drawing::GamePalette& palette) override
        {
            X8DrawingEngine::SetPalette(palette);
            for (size_t i = 0; i < 256; i++)
            {
                _paletteRgba[i * 4] = static_cast<std::byte>(palette[i].red);
                _paletteRgba[i * 4 + 1] = static_cast<std::byte>(palette[i].green);
                _paletteRgba[i * 4 + 2] = static_cast<std::byte>(palette[i].blue);
                _paletteRgba[i * 4 + 3] = i == 0 ? std::byte{ 0 } : std::byte{ 0xff };
            }
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
            if (!_initialised || _width == 0 || _height == 0)
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
            commands.canvasUpload = Gpu::CanvasUpload{
                static_cast<uint32_t>(upload.offset), _pitch, _width, _height
            };
            _backend->Submit(*frame, commands);
            _backend->Present(*frame);
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
