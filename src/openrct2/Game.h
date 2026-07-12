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

#include <cstddef>
#include <cstdint>
#include <memory>

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
    constexpr uint32_t kTurboTargetTicksPerSecond = kGameUpdateFPS * (1u << (kGameSpeedTurbo - 1));

    // Fixed-rate simulation deadline which deliberately never carries lateness into a later batch.
    template<typename Clock>
    class TurboSimulationPacer
    {
        using TimePoint = typename Clock::time_point;
        using Duration = typename Clock::duration;

        TimePoint _deadline{};
        bool _initialised{};

    public:
        void Reset() noexcept
        {
            _initialised = false;
        }

        [[nodiscard]] Duration TimeUntilDue(TimePoint now) const noexcept
        {
            return !_initialised || now >= _deadline ? Duration::zero() : _deadline - now;
        }

        void BeginBatch(TimePoint now) noexcept
        {
            if (!_initialised || now > _deadline)
            {
                // Presentation and OS delays are not simulation work; discard them before starting the next batch.
                _deadline = now;
                _initialised = true;
            }
        }

        void CompleteBatch(TimePoint completedAt, Duration interval) noexcept
        {
            _deadline += interval;
            if (_deadline < completedAt)
            {
                // An over-budget batch remains throughput-limited without manufacturing catch-up debt.
                _deadline = completedAt;
            }
        }
    };

    // The network update runs at a different rate to the game update.
    constexpr uint32_t kNetworkUpdateFPS = 140;
    // The network update interval in milliseconds, (1000 / 140fps) = ~7.14ms
    constexpr float kNetworkUpdateTimeMS = 1.0f / kNetworkUpdateFPS;

    constexpr float kGameMinTimeScale = 0.1f;
    constexpr float kGameMaxTimeScale = 5.0f;

    constexpr uint32_t kDefaultDisplayRefreshRate = 60;

    [[nodiscard]] constexpr uint32_t NormaliseDisplayRefreshRate(uint32_t refreshRate) noexcept
    {
        return refreshRate >= 30 && refreshRate <= 1000 ? refreshRate : kDefaultDisplayRefreshRate;
    }

    [[nodiscard]] constexpr double GetDisplayRefreshIntervalSeconds(uint32_t refreshRate) noexcept
    {
        return 1.0 / static_cast<double>(NormaliseDisplayRefreshRate(refreshRate));
    }

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

    [[nodiscard]] constexpr IntegratedBenchmarkMetrics CalculateIntegratedBenchmarkMetrics(
        const IntegratedBenchmarkTotals& totals) noexcept
    {
        const auto ratio = [](double value, double divisor) { return divisor > 0.0 ? value / divisor : 0.0; };
        return {
            .logicalTicksPerSecond = ratio(static_cast<double>(totals.logicalTicks), totals.elapsedSeconds),
            .framesPerSecond = ratio(static_cast<double>(totals.draws), totals.elapsedSeconds),
            .simulationUtilisationPercent = ratio(totals.simulationSeconds * 100.0, totals.elapsedSeconds),
            .drawUtilisationPercent = ratio(totals.drawSeconds * 100.0, totals.elapsedSeconds),
            .meanSimulationMicrosecondsPerLogicalTick =
                ratio(totals.simulationSeconds * 1'000'000.0, static_cast<double>(totals.logicalTicks)),
            .meanSimulationMicrosecondsPerBatch =
                ratio(totals.simulationSeconds * 1'000'000.0, static_cast<double>(totals.simulationBatches)),
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
