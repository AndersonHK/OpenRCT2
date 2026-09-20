/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/world/Weather.h>
#include <span>
#include <string>
#include <vector>

namespace OpenRCT2::UiParityLight
{
    inline constexpr uint32_t kWarmupPaints = 2;
    inline std::vector<std::string> Steps()
    {
        return { "light-paint-3", "light-paint-4", "light-paint-5", "light-paint-6" };
    }

    inline uint64_t HashBytes(std::span<const std::byte> bytes)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (const auto byte : bytes)
            hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1099511628211ULL;
        return hash;
    }

    inline void PinInputs()
    {
        gDayNightCycle = 1.0f;
        Drawing::gPaletteEffectFrame = 0;
        Weather::gLightningFlash = 0;
        auto& weather = getGameState().weatherCurrent;
        weather.weatherType = Weather::Type::sunny;
        weather.temperature = 20;
        weather.weatherEffect = Weather::EffectType::none;
        weather.weatherGloom = 0;
        weather.level = Weather::Level::none;
        // Exactly one full refresh after inputs are pinned, before the first
        // explicitly counted paint. Subsequent adaptation uses ordinary Paint.
        Drawing::LoadPalette();
    }

    inline json_t Metadata(uint32_t paintOrdinal)
    {
        const auto& lightPalette = Drawing::LightFx::GetPalette();
        const auto& weather = getGameState().weatherCurrent;
        return { { "paintOrdinal", paintOrdinal },
                 { "initialization",
                   "startup-disabled; enable-after-load; explicit-same-size-resize; pin-inputs; full-palette-refresh; warmup" },
                 { "warmupPaints", kWarmupPaints },
                 { "night", gDayNightCycle },
                 { "paletteEffectFrame", Drawing::gPaletteEffectFrame },
                 { "weatherType", static_cast<uint8_t>(weather.weatherType) },
                 { "temperature", weather.temperature },
                 { "weatherLevel", static_cast<int>(weather.level) },
                 { "lightningFlash", Weather::gLightningFlash },
                 { "darkPaletteFnv1a64", HashBytes(std::as_bytes(std::span(Drawing::gPalette))) },
                 { "lightPaletteFnv1a64", HashBytes(std::as_bytes(std::span(lightPalette))) },
                 { "source", "natural park paint sources; no harness light injection" },
                 { "temporalContract", "Compare this paint ordinal across fresh processes; adjacent paints need not match" } };
    }
} // namespace OpenRCT2::UiParityLight
