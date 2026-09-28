/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "core/StringTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace OpenRCT2
{
    class Intent;

    // The number of logical update / ticks per second.
    constexpr uint32_t kGameUpdateFPS = 40;
    // The maximum amount of updates in case rendering is slower
    constexpr uint32_t kGameMaxUpdates = 4;
    // The game update interval in milliseconds, (1000 / 40fps) = 25ms
    constexpr float kGameUpdateTimeMS = 1.0f / kGameUpdateFPS;
    // The maximum threshold to advance.
    constexpr float kGameUpdateMaxThreshold = kGameUpdateTimeMS * kGameMaxUpdates;

    constexpr uint8_t kGameSpeedTurbo = 4;
    constexpr uint32_t kTurboTargetTicksPerSecond = 360;

    [[nodiscard]] constexpr uint32_t GetGameSpeedMultiplier(uint8_t speed) noexcept
    {
        if (speed <= 1)
            return 1;
        if (speed == kGameSpeedTurbo)
            return kTurboTargetTicksPerSecond / kGameUpdateFPS;
        return 1u << (speed - 1);
    }

    [[nodiscard]] constexpr uint32_t GetGameSpeedTargetTicksPerSecond(uint8_t speed) noexcept
    {
        return kGameUpdateFPS * GetGameSpeedMultiplier(speed);
    }

    [[nodiscard]] constexpr float GetGameSpeedUpdateTime(uint8_t speed) noexcept
    {
        return 1.0f / GetGameSpeedTargetTicksPerSecond(speed);
    }

    // Accumulator debt was sampled at the outer-frame boundary. Work since then already moves the next tick closer;
    // waiting must not count that work a second time, and simulation seconds must be converted back to wall seconds.
    [[nodiscard]] constexpr float GetSimulationWaitSeconds(
        float updateTime, float accumulator, float elapsedSinceSample, float timeScale) noexcept
    {
        if (timeScale <= 0)
            return 0;
        const float remaining = (updateTime - accumulator) / timeScale - elapsedSinceSample;
        return remaining > 0 ? remaining : 0;
    }

    // The network update runs at a different rate to the game update.
    constexpr uint32_t kNetworkUpdateFPS = 140;
    // The network update interval in milliseconds, (1000 / 140fps) = ~7.14ms
    constexpr float kNetworkUpdateTimeMS = 1.0f / kNetworkUpdateFPS;

    // Keep fractional idle budgets and service input/network work at least once per network interval.
    [[nodiscard]] constexpr float GetSchedulerWaitSeconds(float simulationWait, float presentationWait) noexcept
    {
        const float wait = simulationWait < presentationWait ? simulationWait : presentationWait;
        return wait <= 0 ? 0 : (wait < kNetworkUpdateTimeMS ? wait : kNetworkUpdateTimeMS);
    }

    constexpr float kGameMinTimeScale = 0.1f;
    constexpr float kGameMaxTimeScale = 5.0f;

    struct BenchmarkStateSnapshot
    {
        uint32_t simulationTick{};
        size_t guestsInsidePark{};
        size_t guestsOutsidePark{};
        size_t guestsWalking{};
        size_t guestsQueuing{};
        size_t guestsOnRide{};
        size_t activeTransportRoutes{};
        size_t staff{};
        size_t vehicles{};
        size_t routeNodes{};
        size_t routeTargets{};
        size_t routeDirectionEntries{};
        size_t routeDistanceEntries{};
        size_t singleRideTargets{};
        bool routeCacheCurrent{};
    };

    struct IntegratedBenchmarkTotals
    {
        double elapsedSeconds{};
        uint64_t logicalTicks{};
        uint64_t simulationBatches{};
        uint64_t draws{};
        double simulationSeconds{};
        double drawSeconds{};
        double longestSimulationBatchSeconds{};
        double longestSimulationSliceSeconds{};
    };

    struct IntegratedBenchmarkMetrics
    {
        double logicalTicksPerSecond{};
        double framesPerSecond{};
        double simulationUtilisationPercent{};
        double drawUtilisationPercent{};
        double meanSimulationMicrosecondsPerLogicalTick{};
        double meanSimulationMicrosecondsPerBatch{};
        double meanDrawMicroseconds{};
        double longestSimulationBatchMilliseconds{};
        double longestSimulationSliceMilliseconds{};
    };

    // Benchmark-only bounded storage. Timestamps are accepted queue presents,
    // not UI draw attempts or fence-harvest times. Percentiles are upper bounds
    // in 0.1 ms bins; intervals above 1000 ms have an explicit overflow bin.
    class BenchmarkPresentationPacing
    {
        static constexpr uint64_t kBinNanoseconds = 100'000;
        static constexpr size_t kLastFiniteBin = 10'000;
        std::array<uint64_t, kLastFiniteBin + 2> _bins{};
        uint64_t _begin = 0;
        uint64_t _end = std::numeric_limits<uint64_t>::max();
        uint64_t _last = 0;
        uint64_t _samples = 0;
        uint64_t _intervals = 0;
        uint64_t _maximum = 0;
        uint64_t _outOfOrder = 0;

    public:
        void Reset(uint64_t begin) noexcept
        {
            _bins.fill(0);
            _begin = begin;
            _end = std::numeric_limits<uint64_t>::max();
            _last = _samples = _intervals = _maximum = _outOfOrder = 0;
        }

        void SetEnd(uint64_t end) noexcept
        {
            _end = end;
        }

        void Include(uint64_t timestamp) noexcept
        {
            if (timestamp < _begin || timestamp > _end)
                return;
            if (_samples != 0 && timestamp < _last)
            {
                ++_outOfOrder;
                return;
            }
            if (_samples != 0)
            {
                const auto interval = timestamp - _last;
                // Divide before rounding up, avoiding an addition overflow.
                const auto bin = interval / kBinNanoseconds + (interval % kBinNanoseconds != 0);
                ++_bins[bin <= kLastFiniteBin ? static_cast<size_t>(bin) : kLastFiniteBin + 1];
                ++_intervals;
                if (interval > _maximum)
                    _maximum = interval;
            }
            _last = timestamp;
            ++_samples;
        }

        uint64_t Samples() const noexcept
        {
            return _samples;
        }
        uint64_t Intervals() const noexcept
        {
            return _intervals;
        }
        uint64_t OverflowIntervals() const noexcept
        {
            return _bins.back();
        }
        uint64_t OutOfOrderSamples() const noexcept
        {
            return _outOfOrder;
        }
        double MaximumMilliseconds() const noexcept
        {
            return static_cast<double>(_maximum) / 1'000'000.0;
        }

        std::optional<double> PercentileUpperMilliseconds(uint32_t percent) const noexcept
        {
            if (_intervals == 0 || percent == 0 || percent > 100)
                return std::nullopt;
            const auto rank = (_intervals / 100) * percent + ((_intervals % 100) * percent + 99) / 100;
            uint64_t cumulative = 0;
            for (size_t bin = 0; bin <= kLastFiniteBin; ++bin)
            {
                cumulative += _bins[bin];
                if (cumulative >= rank)
                    return static_cast<double>(bin * kBinNanoseconds) / 1'000'000.0;
            }
            return std::nullopt;
        }
    };

    [[nodiscard]] constexpr IntegratedBenchmarkMetrics CalculateIntegratedBenchmarkMetrics(
        const IntegratedBenchmarkTotals& totals) noexcept
    {
        const auto ratio = [](double value, double divisor) { return divisor > 0.0 ? value / divisor : 0.0; };
        return {
            .logicalTicksPerSecond = ratio(static_cast<double>(totals.logicalTicks), totals.elapsedSeconds),
            .framesPerSecond = ratio(static_cast<double>(totals.draws), totals.elapsedSeconds),
            .simulationUtilisationPercent = ratio(totals.simulationSeconds * 100.0, totals.elapsedSeconds),
            .drawUtilisationPercent = ratio(totals.drawSeconds * 100.0, totals.elapsedSeconds),
            .meanSimulationMicrosecondsPerLogicalTick = ratio(
                totals.simulationSeconds * 1'000'000.0, static_cast<double>(totals.logicalTicks)),
            .meanSimulationMicrosecondsPerBatch = ratio(
                totals.simulationSeconds * 1'000'000.0, static_cast<double>(totals.simulationBatches)),
            .meanDrawMicroseconds = ratio(totals.drawSeconds * 1'000'000.0, static_cast<double>(totals.draws)),
            .longestSimulationBatchMilliseconds = totals.longestSimulationBatchSeconds * 1'000.0,
            .longestSimulationSliceMilliseconds = totals.longestSimulationSliceSeconds * 1'000.0,
        };
    }

    [[nodiscard]] BenchmarkStateSnapshot CaptureBenchmarkStateSnapshot();
} // namespace OpenRCT2

enum
{
    GAME_PAUSED_NORMAL = 1 << 0,
    GAME_PAUSED_MODAL = 1 << 1,
    GAME_PAUSED_SAVING_TRACK = 1 << 2,
};

enum
{
    ERROR_TYPE_NONE = 0,
    ERROR_TYPE_GENERIC = 254,
    ERROR_TYPE_FILE_LOAD = 255
};

extern uint32_t gCurrentRealTimeTicks;
// Monotonic process-local count of completed logical simulation ticks, used only for performance telemetry.
extern uint64_t gTotalSimulationTicks;
// Measured logical simulation ticks per wall-clock second. This is independent from render FPS.
extern float gActualSimulationTPS;

extern uint16_t gCurrentDeltaTime;
extern uint8_t gGamePaused;
extern uint8_t gGameSpeed;
extern bool gDoSingleUpdate;
extern float gDayNightCycle;
extern bool gInUpdateCode;
extern bool gInMapInitCode;
extern std::string gCurrentLoadedPath;
extern bool gIsAutosave;
extern bool gIsAutosaveLoaded;

extern bool gLoadKeepWindowsOpen;

void GameResetSpeed();
void GameIncreaseGameSpeed();
void GameReduceGameSpeed();

void GameCreateWindows();
void ResetAllSpriteQuadrantPlacements();

void GameLoadOrQuitNoSavePrompt();
void GameLoadInit();
void GameLoadScripts();
void GameUnloadScripts();
void GameNotifyMapChange();
void GameNotifyMapChanged();
void PauseToggle();
bool GameIsPaused();
bool GameIsNotPaused();
void SaveGame();
std::unique_ptr<OpenRCT2::Intent> CreateSaveGameAsIntent();
void SaveGameAs();
void SaveGameCmd(u8string_view name = {});
void SaveGameWithName(u8string_view name);
void GameAutosave();
void RCT2StringToUTF8Self(char* buffer, size_t length);
void GameFixRideNumRiders();
void GameFixSaveVars();
void StartSilentRecord();
bool StopSilentRecord();
void PrepareMapForSave();
