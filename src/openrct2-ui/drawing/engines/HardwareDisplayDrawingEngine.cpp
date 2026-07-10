/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "DrawingEngineFactory.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <openrct2/Diagnostic.h>
#include <openrct2/Game.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Guard.hpp>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/interface/Window.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/world/Weather.h>
#include <vector>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
using namespace OpenRCT2::Ui;

class HardwareDisplayDrawingEngine final : public X8DrawingEngine
{
private:
    constexpr static uint32_t kDirtyVisualTime = 40;
    constexpr static uint32_t kDirtyRegionAlpha = 100;

    IUiContext& _uiContext;
    SDL_Window* _window = nullptr;
    SDL_Renderer* _sdlRenderer = nullptr;
    SDL_Texture* _screenTexture = nullptr;
    SDL_Texture* _scaledScreenTexture = nullptr;
    SDL_PixelFormat* _screenTextureFormat = nullptr;
    uint32_t _paletteHWMapped[256] = { 0 };
    uint32_t _lightPaletteHWMapped[256] = { 0 };
    std::vector<uint8_t> _convertedPixels;
    std::vector<SDL_Rect> _dirtyRegions;
    uint8_t _textureBytesPerPixel = 0;
    bool _fullUploadRequired = true;

    bool _useVsync = true;

    std::vector<uint32_t> _dirtyVisualsTime;

    bool smoothNN = false;

public:
    explicit HardwareDisplayDrawingEngine(IUiContext& uiContext)
        : X8DrawingEngine(uiContext)
        , _uiContext(uiContext)
    {
        _window = static_cast<SDL_Window*>(_uiContext.GetWindow());
    }

    ~HardwareDisplayDrawingEngine() override
    {
        if (_screenTexture != nullptr)
        {
            SDL_DestroyTexture(_screenTexture);
            _screenTexture = nullptr;
        }
        if (_scaledScreenTexture != nullptr)
        {
            SDL_DestroyTexture(_scaledScreenTexture);
            _scaledScreenTexture = nullptr;
        }
        SDL_FreeFormat(_screenTextureFormat);
        SDL_DestroyRenderer(_sdlRenderer);
    }

    void Initialise() override
    {
        _sdlRenderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | (_useVsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    }

    void SetVSync(bool vsync) override
    {
        if (_useVsync != vsync)
        {
            _useVsync = vsync;
#if SDL_VERSION_ATLEAST(2, 0, 18)
            SDL_RenderSetVSync(_sdlRenderer, vsync ? 1 : 0);
#else
            SDL_DestroyRenderer(_sdlRenderer);
            _screenTexture = nullptr;
            _scaledScreenTexture = nullptr;
            Initialise();
            Resize(_uiContext->GetWidth(), _uiContext->GetHeight());
#endif
        }
    }

    void Resize(uint32_t width, uint32_t height) override
    {
        if (width == 0 || height == 0)
        {
            return;
        }

        if (_screenTexture != nullptr)
        {
            SDL_DestroyTexture(_screenTexture);
            _screenTexture = nullptr;
        }
        if (_scaledScreenTexture != nullptr)
        {
            SDL_DestroyTexture(_scaledScreenTexture);
            _scaledScreenTexture = nullptr;
        }
        SDL_FreeFormat(_screenTextureFormat);
        _screenTextureFormat = nullptr;

        SDL_RendererInfo rendererInfo = {};
        int32_t result = SDL_GetRendererInfo(_sdlRenderer, &rendererInfo);
        if (result < 0)
        {
            LOG_WARNING("HWDisplayDrawingEngine::Resize error: %s", SDL_GetError());
            return;
        }
        uint32_t pixelFormat = SDL_PIXELFORMAT_UNKNOWN;
        uint32_t fallbackPixelFormat = SDL_PIXELFORMAT_UNKNOWN;
        for (uint32_t i = 0; i < rendererInfo.num_texture_formats; i++)
        {
            uint32_t format = rendererInfo.texture_formats[i];
            if (SDL_ISPIXELFORMAT_FOURCC(format) || SDL_ISPIXELFORMAT_INDEXED(format))
            {
                continue;
            }

            if (fallbackPixelFormat == SDL_PIXELFORMAT_UNKNOWN)
            {
                fallbackPixelFormat = format;
            }
            if (format == SDL_PIXELFORMAT_ARGB8888)
            {
                pixelFormat = format;
                break;
            }
            if (pixelFormat == SDL_PIXELFORMAT_UNKNOWN && SDL_BYTESPERPIXEL(format) == 4)
            {
                pixelFormat = format;
            }
        }
        if (pixelFormat == SDL_PIXELFORMAT_UNKNOWN)
        {
            pixelFormat = fallbackPixelFormat;
        }
        Guard::Assert(pixelFormat != SDL_PIXELFORMAT_UNKNOWN, "SDL renderer exposes no usable RGB texture format");

        ScaleQuality scaleQuality = GetContext()->GetUiContext().GetScaleQuality();
        if (scaleQuality == ScaleQuality::smoothNearestNeighbour)
        {
            scaleQuality = ScaleQuality::linear;
            smoothNN = true;
        }
        else
        {
            smoothNN = false;
        }

        if (smoothNN)
        {
            char scaleQualityBuffer[4];
            snprintf(scaleQualityBuffer, sizeof(scaleQualityBuffer), "%d", static_cast<int32_t>(scaleQuality));
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

            _screenTexture = SDL_CreateTexture(_sdlRenderer, pixelFormat, SDL_TEXTUREACCESS_STREAMING, width, height);
            Guard::Assert(
                _screenTexture != nullptr, "Failed to create unscaled screen texture (%ux%u, pixelFormat = %u): %s", width,
                height, pixelFormat, SDL_GetError());

            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scaleQualityBuffer);

            uint32_t scale = std::ceil(Config::Get().general.windowScale);
            _scaledScreenTexture = SDL_CreateTexture(
                _sdlRenderer, pixelFormat, SDL_TEXTUREACCESS_TARGET, width * scale, height * scale);

            Guard::Assert(
                _scaledScreenTexture != nullptr,
                "Failed to create scaled screen texture (%ux%u, scale = %u, pixelFormat = %u): %s", width, height, scale,
                pixelFormat, SDL_GetError());
        }
        else
        {
            _screenTexture = SDL_CreateTexture(_sdlRenderer, pixelFormat, SDL_TEXTUREACCESS_STREAMING, width, height);
            Guard::Assert(
                _screenTexture != nullptr, "Failed to create screen texture (%ux%u, pixelFormat = %u): %s", width, height,
                pixelFormat, SDL_GetError());
        }

        uint32_t format;
        SDL_QueryTexture(_screenTexture, &format, nullptr, nullptr, nullptr);
        _screenTextureFormat = SDL_AllocFormat(format);
        Guard::Assert(_screenTextureFormat != nullptr, "Failed to allocate SDL screen texture format: %s", SDL_GetError());
        _textureBytesPerPixel = _screenTextureFormat->BytesPerPixel;
        _convertedPixels.resize(static_cast<size_t>(width) * height * _textureBytesPerPixel);
        _dirtyRegions.clear();
        _fullUploadRequired = true;

        X8DrawingEngine::Resize(width, height);
    }

    void SetPalette(const GamePalette& palette) override
    {
        if (_screenTextureFormat != nullptr)
        {
            for (int32_t i = 0; i < 256; i++)
            {
                _paletteHWMapped[i] = SDL_MapRGB(_screenTextureFormat, palette[i].red, palette[i].green, palette[i].blue);
            }

            if (Config::Get().general.enableLightFx)
            {
                auto& lightPalette = LightFx::GetPalette();
                for (int32_t i = 0; i < 256; i++)
                {
                    const auto& src = lightPalette[i];
                    _lightPaletteHWMapped[i] = SDL_MapRGBA(_screenTextureFormat, src.red, src.green, src.blue, src.alpha);
                }
            }
        }
        _fullUploadRequired = true;
    }

    void BeginDraw() override
    {
        _dirtyRegions.clear();
        X8DrawingEngine::BeginDraw();
    }

    void CopyRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t dx, int32_t dy) override
    {
        X8DrawingEngine::CopyRect(x, y, width, height, dx, dy);
        AddDirtyRegion(x, y, x + width, y + height);
    }

    void EndDraw() override
    {
        X8DrawingEngine::EndDraw();

        Display();
    }

protected:
    void OnDrawDirtyBlock(int32_t left, int32_t top, int32_t right, int32_t bottom) override
    {
        AddDirtyRegion(left, top, right, bottom);

        if (gShowDirtyVisuals)
        {
            const auto columns = ((right - left) + (_invalidationGrid.getBlockWidth() - 1)) / _invalidationGrid.getBlockWidth();
            const auto rows = ((bottom - top) + (_invalidationGrid.getBlockHeight() - 1)) / _invalidationGrid.getBlockHeight();
            const auto firstRow = top / _invalidationGrid.getBlockHeight();
            const auto firstColumn = left / _invalidationGrid.getBlockWidth();

            for (uint32_t y = 0; y < rows; y++)
            {
                for (uint32_t x = 0; x < columns; x++)
                {
                    SetDirtyVisualTime(firstColumn + x, firstRow + y, gCurrentRealTimeTicks + kDirtyVisualTime);
                }
            }
        }
    }

private:
    void Display()
    {
        auto* viewport = WindowGetViewport(WindowGetMain());

        if (Config::Get().general.enableLightFx && viewport != nullptr)
        {
            void* pixels;
            int32_t pitch;
            if (SDL_LockTexture(_screenTexture, nullptr, &pixels, &pitch) == 0)
            {
                LightFx::RenderToTexture(
                    *viewport, pixels, pitch, _bits, _width, _height, _paletteHWMapped, _lightPaletteHWMapped);
                SDL_UnlockTexture(_screenTexture);
            }
        }
        else
        {
            const bool weatherTouchesUntrackedPixels = Weather::hasWeatherEffect();
            if (_fullUploadRequired || gPaintForceRedraw || weatherTouchesUntrackedPixels)
            {
                UploadFullTexture();
            }
            else
            {
                UploadDirtyRegions();
            }
        }
        _fullUploadRequired = false;
        if (smoothNN)
        {
            SDL_SetRenderTarget(_sdlRenderer, _scaledScreenTexture);
            SDL_RenderCopy(_sdlRenderer, _screenTexture, nullptr, nullptr);

            SDL_SetRenderTarget(_sdlRenderer, nullptr);
            SDL_RenderCopy(_sdlRenderer, _scaledScreenTexture, nullptr, nullptr);
        }
        else
        {
            SDL_RenderCopy(_sdlRenderer, _screenTexture, nullptr, nullptr);
        }

        if (gShowDirtyVisuals)
        {
            RenderDirtyVisuals();
        }

        SDL_RenderPresent(_sdlRenderer);
    }

    void AddDirtyRegion(int32_t left, int32_t top, int32_t right, int32_t bottom)
    {
        left = std::clamp(left, 0, static_cast<int32_t>(_width));
        right = std::clamp(right, 0, static_cast<int32_t>(_width));
        top = std::clamp(top, 0, static_cast<int32_t>(_height));
        bottom = std::clamp(bottom, 0, static_cast<int32_t>(_height));
        if (left >= right || top >= bottom)
        {
            return;
        }

        SDL_Rect merged = { left, top, right - left, bottom - top };
        for (size_t i = 0; i < _dirtyRegions.size();)
        {
            const auto& current = _dirtyRegions[i];
            const bool separated = merged.x + merged.w < current.x || current.x + current.w < merged.x
                || merged.y + merged.h < current.y || current.y + current.h < merged.y;
            if (separated)
            {
                i++;
                continue;
            }

            const int32_t mergedRight = std::max(merged.x + merged.w, current.x + current.w);
            const int32_t mergedBottom = std::max(merged.y + merged.h, current.y + current.h);
            merged.x = std::min(merged.x, current.x);
            merged.y = std::min(merged.y, current.y);
            merged.w = mergedRight - merged.x;
            merged.h = mergedBottom - merged.y;
            _dirtyRegions.erase(_dirtyRegions.begin() + i);
            i = 0;
        }
        _dirtyRegions.push_back(merged);
    }

    void ConvertRegion(const SDL_Rect& region)
    {
        const size_t destinationPitch = static_cast<size_t>(_width) * _textureBytesPerPixel;
        switch (_textureBytesPerPixel)
        {
            case 4:
            {
                for (int32_t y = region.y; y < region.y + region.h; y++)
                {
                    const PaletteIndex* src = _bits + static_cast<size_t>(y) * _pitch + region.x;
                    auto* dst = reinterpret_cast<uint32_t*>(
                        _convertedPixels.data() + static_cast<size_t>(y) * destinationPitch
                        + static_cast<size_t>(region.x) * 4);
                    for (int32_t x = 0; x < region.w; x++)
                    {
                        *dst++ = _paletteHWMapped[EnumValue(*src++)];
                    }
                }
                break;
            }
            case 3:
            {
                for (int32_t y = region.y; y < region.y + region.h; y++)
                {
                    const PaletteIndex* src = _bits + static_cast<size_t>(y) * _pitch + region.x;
                    uint8_t* dst = _convertedPixels.data() + static_cast<size_t>(y) * destinationPitch
                        + static_cast<size_t>(region.x) * 3;
                    for (int32_t x = 0; x < region.w; x++)
                    {
                        const uint32_t mapped = _paletteHWMapped[EnumValue(*src++)];
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        *dst++ = static_cast<uint8_t>(mapped >> 16);
                        *dst++ = static_cast<uint8_t>(mapped >> 8);
                        *dst++ = static_cast<uint8_t>(mapped);
#else
                        *dst++ = static_cast<uint8_t>(mapped);
                        *dst++ = static_cast<uint8_t>(mapped >> 8);
                        *dst++ = static_cast<uint8_t>(mapped >> 16);
#endif
                    }
                }
                break;
            }
            case 2:
            {
                for (int32_t y = region.y; y < region.y + region.h; y++)
                {
                    const PaletteIndex* src = _bits + static_cast<size_t>(y) * _pitch + region.x;
                    auto* dst = reinterpret_cast<uint16_t*>(
                        _convertedPixels.data() + static_cast<size_t>(y) * destinationPitch
                        + static_cast<size_t>(region.x) * 2);
                    for (int32_t x = 0; x < region.w; x++)
                    {
                        *dst++ = static_cast<uint16_t>(_paletteHWMapped[EnumValue(*src++)]);
                    }
                }
                break;
            }
            case 1:
            {
                for (int32_t y = region.y; y < region.y + region.h; y++)
                {
                    const PaletteIndex* src = _bits + static_cast<size_t>(y) * _pitch + region.x;
                    uint8_t* dst = _convertedPixels.data() + static_cast<size_t>(y) * destinationPitch + region.x;
                    for (int32_t x = 0; x < region.w; x++)
                    {
                        *dst++ = static_cast<uint8_t>(_paletteHWMapped[EnumValue(*src++)]);
                    }
                }
                break;
            }
            default:
                Guard::Fail("Unsupported SDL texture pixel size: %u", _textureBytesPerPixel);
                break;
        }
    }

    void UploadRegion(const SDL_Rect& region)
    {
        ConvertRegion(region);
        const size_t pitch = static_cast<size_t>(_width) * _textureBytesPerPixel;
        const uint8_t* pixels = _convertedPixels.data()
            + (static_cast<size_t>(region.y) * _width + region.x) * _textureBytesPerPixel;
        if (SDL_UpdateTexture(_screenTexture, &region, pixels, static_cast<int32_t>(pitch)) < 0)
        {
            LOG_WARNING("HWDisplayDrawingEngine texture upload failed: %s", SDL_GetError());
        }
    }

    void UploadFullTexture()
    {
        const SDL_Rect screen = { 0, 0, static_cast<int32_t>(_width), static_cast<int32_t>(_height) };
        UploadRegion(screen);
    }

    void UploadDirtyRegions()
    {
        for (const auto& region : _dirtyRegions)
        {
            UploadRegion(region);
        }
    }

    uint32_t GetDirtyVisualTime(uint32_t x, uint32_t y)
    {
        uint32_t result = 0;
        uint32_t i = y * _invalidationGrid.getColumnCount() + x;
        if (_dirtyVisualsTime.size() > i)
        {
            result = _dirtyVisualsTime[i];
        }
        return result;
    }

    void SetDirtyVisualTime(uint32_t x, uint32_t y, uint32_t value)
    {
        const auto rows = _invalidationGrid.getRowCount();
        const auto columns = _invalidationGrid.getColumnCount();

        _dirtyVisualsTime.resize(rows * columns);

        uint32_t i = y * _invalidationGrid.getColumnCount() + x;
        if (_dirtyVisualsTime.size() > i)
        {
            _dirtyVisualsTime[i] = value;
        }
    }

    void RenderDirtyVisuals()
    {
        int windowX, windowY, renderX, renderY;
        SDL_GetWindowSize(_window, &windowX, &windowY);
        SDL_GetRendererOutputSize(_sdlRenderer, &renderX, &renderY);

        float scaleX = Config::Get().general.windowScale * renderX / static_cast<float>(windowX);
        float scaleY = Config::Get().general.windowScale * renderY / static_cast<float>(windowY);

        SDL_SetRenderDrawBlendMode(_sdlRenderer, SDL_BLENDMODE_BLEND);
        for (uint32_t y = 0; y < _invalidationGrid.getRowCount(); y++)
        {
            for (uint32_t x = 0; x < _invalidationGrid.getColumnCount(); x++)
            {
                const auto timeEnd = GetDirtyVisualTime(x, y);
                const auto timeLeft = gCurrentRealTimeTicks < timeEnd ? timeEnd - gCurrentRealTimeTicks : 0;
                if (timeLeft > 0)
                {
                    uint8_t alpha = timeLeft * kDirtyRegionAlpha / kDirtyVisualTime;
                    SDL_Rect ddRect;
                    ddRect.x = static_cast<int32_t>(x * _invalidationGrid.getBlockWidth() * scaleX);
                    ddRect.y = static_cast<int32_t>(y * _invalidationGrid.getBlockHeight() * scaleY);
                    ddRect.w = static_cast<int32_t>(_invalidationGrid.getBlockWidth() * scaleX);
                    ddRect.h = static_cast<int32_t>(_invalidationGrid.getBlockHeight() * scaleY);

                    SDL_SetRenderDrawColor(_sdlRenderer, 255, 255, 255, alpha);
                    SDL_RenderFillRect(_sdlRenderer, &ddRect);
                }
            }
        }
    }
};

std::unique_ptr<IDrawingEngine> Ui::CreateHardwareDisplayDrawingEngine(IUiContext& uiContext)
{
    return std::make_unique<HardwareDisplayDrawingEngine>(uiContext);
}
