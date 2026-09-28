/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SdlCapture.h"

#include <SDL.h>
#include <optional>
#include <stdexcept>
#include <utility>

namespace
{
    // SDL presentation and arming belong to the one UI thread in this executable.
    std::optional<std::string> armed;
    std::optional<UiParity::SdlCapture> completed;
    std::string failure;
    uint64_t presentOrdinal{};

    void RequireSdl(bool success, const char* operation)
    {
        if (!success)
            throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
} // namespace

void UiParity::ArmSdlCapture(std::string name)
{
    if (armed.has_value() || completed.has_value() || !failure.empty())
        throw std::logic_error("A previous SDL capture has not been consumed");
    armed = std::move(name);
}

UiParity::SdlCapture UiParity::TakeSdlCapture()
{
    if (!failure.empty())
        throw std::runtime_error(std::exchange(failure, {}));
    if (!completed.has_value())
        throw std::runtime_error("The armed UI frame did not reach SDL_RenderPresent");
    auto result = std::move(*completed);
    completed.reset();
    return result;
}

extern "C" void SDLCALL OraclePresent(SDL_Renderer* renderer)
{
    ++presentOrdinal;
    if (armed.has_value())
    {
        try
        {
            if (SDL_GetRenderTarget(renderer) != nullptr)
                throw std::runtime_error("SDL oracle capture did not reach the default display target");
            int width{}, height{};
            RequireSdl(SDL_GetRendererOutputSize(renderer, &width, &height) == 0, "SDL_GetRendererOutputSize");
            if (width <= 0 || height <= 0)
                throw std::runtime_error("SDL oracle display is empty");
            SDL_RendererInfo info{};
            RequireSdl(SDL_GetRendererInfo(renderer, &info) == 0, "SDL_GetRendererInfo");
            UiParity::SdlCapture capture{
                .name = *armed,
                .rendererName = info.name == nullptr ? "" : info.name,
                .rendererFlags = info.flags,
                .width = static_cast<uint32_t>(width),
                .height = static_cast<uint32_t>(height),
                .presentOrdinal = presentOrdinal,
                .rgba = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4),
            };
            RequireSdl(
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, capture.rgba.data(), width * 4) == 0,
                "SDL_RenderReadPixels");
            completed = std::move(capture);
        }
        catch (const std::exception& error)
        {
            failure = error.what();
        }
        armed.reset();
    }
    // This translation unit is deliberately compiled without OraclePresentHook.h.
    SDL_RenderPresent(renderer);
}
