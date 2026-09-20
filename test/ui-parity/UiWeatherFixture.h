/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include "UiFixtures.h"
#include "UiLightFixture.h"

#include <openrct2/Context.h>
#include <openrct2/config/Config.h>
#include <openrct2/object/ClimateObject.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/WaterEntry.h>
#include <openrct2/object/WaterObject.h>
#include <openrct2/scenario/Scenario.h>

namespace OpenRCT2::UiParityWeather
{
    inline bool IsFamily(const std::string& family)
    {
        return family == "weather-precipitation" || family == "weather-palette";
    }

    inline std::vector<std::string> Steps(const std::string& family)
    {
        if (family == "weather-precipitation")
            return { "weather-clear",         "weather-rain-light", "weather-rain-heavy", "weather-rain-occluded",
                     "weather-rain-restored", "weather-snow-light", "weather-snow-heavy", "weather-cleared" };
        return { "palette-clear",   "palette-gloom-1", "palette-gloom-2", "palette-lightning", "palette-lightning-recovered",
                 "palette-restored" };
    }

    inline json_t State(const Weather::State& state)
    {
        return { { "type", static_cast<uint8_t>(state.weatherType) },
                 { "temperature", state.temperature },
                 { "effect", static_cast<uint8_t>(state.weatherEffect) },
                 { "gloom", state.weatherGloom },
                 { "level", static_cast<int>(state.level) } };
    }

    inline json_t Metadata()
    {
        const auto& state = getGameState();
        const auto& config = Config::Get().general;
        const auto& water = getActiveWaterEntry();
        auto& objects = GetContext()->GetObjectManager();
        const auto* climate = objects.GetLoadedObject<ClimateObject>(0);
        const auto* waterObject = objects.GetLoadedObject<WaterObject>(0);
        UiParityFixtures::Require(
            climate != nullptr && waterObject != nullptr, "Weather fixture requires loaded climate and water objects");
        const auto& rng = ScenarioRandState();
        return { { "current", State(state.weatherCurrent) },
                 { "next", State(state.weatherNext) },
                 { "timer", state.weatherUpdateTimer },
                 { "rng", { rng.s0, rng.s1 } },
                 { "ticks", state.currentTicks },
                 { "night", gDayNightCycle },
                 { "paletteEffectFrame", Drawing::gPaletteEffectFrame },
                 { "lightningFlash", Weather::gLightningFlash },
                 { "effectsEnabled", config.renderWeatherEffects },
                 { "gloomEnabled", config.renderWeatherGloom },
                 { "lightFxEnabled", config.enableLightFx },
                 { "snowSelected", Weather::isSnowing() || Weather::isTransitioningToSnow() },
                 { "gamePaletteFnv1a64", UiParityLight::HashBytes(std::as_bytes(std::span(Drawing::gGamePalette))) },
                 { "displayPaletteFnv1a64", UiParityLight::HashBytes(std::as_bytes(std::span(Drawing::gPalette))) },
                 { "climateObject", std::string(climate->GetIdentifier()) },
                 { "waterObject", std::string(waterObject->GetIdentifier()) },
                 { "waterPaletteImages", { water.mainPalette, water.waterWavesPalette, water.waterSparklesPalette } },
                 { "normalization",
                   "Public forceWeather then next=current; fixed paused tick; no weather update or simulation step" } };
    }

    inline void SetWeather(Weather::Type type)
    {
        Weather::forceWeather(type);
        auto& state = getGameState();
        UiParityFixtures::Require(
            state.weatherCurrent.weatherType == type, "Public weather setter did not apply requested type");
        state.weatherNext = state.weatherCurrent;
    }

    inline void Initialise(const std::string& family)
    {
        auto& config = Config::Get().general;
        config.enableLightFx = false;
        config.renderWeatherEffects = family == "weather-precipitation";
        config.renderWeatherGloom = family == "weather-palette";
        ScenarioRandSeed(0x12345678, 0x9abcdef0);
        static_cast<void>(Metadata()); // Fail before a no-op forceWeather without a climate object.
        SetWeather(Weather::Type::sunny);
        gDayNightCycle = 0;
        Drawing::gPaletteEffectFrame = 0;
        Weather::gLightningFlash = 0;
        Drawing::LoadPalette();
    }

    template<typename Capture>
    void Run(const std::string& family, Capture&& capture)
    {
        const auto emit = [&](const char* step) {
            auto input = UiParityFixtures::WindowInputState();
            input["weatherBeforePaint"] = Metadata();
            capture(step, input);
        };
        if (family == "weather-precipitation")
        {
            emit("weather-clear");
            SetWeather(Weather::Type::rain);
            emit("weather-rain-light");
            SetWeather(Weather::Type::heavyRain);
            emit("weather-rain-heavy");
            auto* research = Ui::Windows::ResearchOpen();
            UiParityFixtures::Require(research != nullptr, "Weather clipping fixture requires Research window");
            Ui::Windows::WindowSetPosition(*research, { 260, 132 });
            emit("weather-rain-occluded");
            Ui::GetWindowManager()->Close(*research);
            WindowCullDead();
            emit("weather-rain-restored");
            SetWeather(Weather::Type::snow);
            emit("weather-snow-light");
            SetWeather(Weather::Type::heavySnow);
            emit("weather-snow-heavy");
            SetWeather(Weather::Type::sunny);
            emit("weather-cleared");
        }
        else
        {
            emit("palette-clear");
            SetWeather(Weather::Type::rain);
            emit("palette-gloom-1");
            SetWeather(Weather::Type::heavyRain);
            emit("palette-gloom-2");
            Weather::gLightningFlash = 1;
            emit("palette-lightning");
            UiParityFixtures::Require(Weather::gLightningFlash == 2, "Lightning paint did not advance flash 1 to 2");
            emit("palette-lightning-recovered");
            UiParityFixtures::Require(Weather::gLightningFlash == 0, "Lightning recovery did not advance flash 2 to 0");
            SetWeather(Weather::Type::sunny);
            emit("palette-restored");
        }
    }
} // namespace OpenRCT2::UiParityWeather
