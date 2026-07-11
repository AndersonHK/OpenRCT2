/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../Context.h"
#include "../Game.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../config/ConfigTypes.h"
#include "../core/Console.hpp"
#include "../entity/EntityRegistry.h"
#include "../network/Network.h"
#include "../platform/Platform.h"
#include "../profiling/Profiling.h"
#include "CommandLine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

using namespace OpenRCT2::CommandLine;

namespace OpenRCT2
{
    namespace
    {
        using BenchmarkClock = std::chrono::steady_clock;

        constexpr double kTurboTargetTicksPerSecond = 320.0;

        int32_t _warmupTicks = -1;
        bool _benchmark = false;
        u8string _profilePath;

        struct BenchmarkStats
        {
            double elapsedSeconds{};
            double ticksPerSecond{};
            double meanMicroseconds{};
            double minMicroseconds{};
            double medianMicroseconds{};
            double p95Microseconds{};
            double p99Microseconds{};
            double maxMicroseconds{};
            double firstQuarterMeanMicroseconds{};
            double lastQuarterMeanMicroseconds{};
        };

        double GetPercentileNanoseconds(const std::vector<int64_t>& sortedSamples, double percentile)
        {
            const auto rank = percentile * static_cast<double>(sortedSamples.size() - 1);
            const auto lowerIndex = static_cast<size_t>(rank);
            const auto upperIndex = std::min(lowerIndex + 1, sortedSamples.size() - 1);
            const auto fraction = rank - static_cast<double>(lowerIndex);
            return static_cast<double>(sortedSamples[lowerIndex])
                + (static_cast<double>(sortedSamples[upperIndex] - sortedSamples[lowerIndex]) * fraction);
        }

        double GetMeanNanoseconds(const std::vector<int64_t>& samples, size_t first, size_t count)
        {
            long double total = 0;
            for (size_t i = first; i < first + count; i++)
            {
                total += samples[i];
            }
            return static_cast<double>(total / count);
        }

        BenchmarkStats GetBenchmarkStats(
            std::vector<int64_t> tickDurationsNanoseconds, BenchmarkClock::duration measurementDuration)
        {
            const auto trendSampleCount = std::max<size_t>(1, tickDurationsNanoseconds.size() / 4);
            const auto lastTrendSample = tickDurationsNanoseconds.size() - trendSampleCount;
            constexpr double nanosecondsPerMicrosecond = 1000.0;
            const auto firstQuarterMeanNanoseconds = GetMeanNanoseconds(tickDurationsNanoseconds, 0, trendSampleCount);
            const auto lastQuarterMeanNanoseconds = GetMeanNanoseconds(
                tickDurationsNanoseconds, lastTrendSample, trendSampleCount);
            std::sort(tickDurationsNanoseconds.begin(), tickDurationsNanoseconds.end());

            const auto elapsedSeconds = std::chrono::duration<double>(measurementDuration).count();
            const auto tickCount = static_cast<double>(tickDurationsNanoseconds.size());

            BenchmarkStats result;
            result.elapsedSeconds = elapsedSeconds;
            result.ticksPerSecond = tickCount / elapsedSeconds;
            result.meanMicroseconds = (elapsedSeconds * 1'000'000.0) / tickCount;
            result.minMicroseconds = static_cast<double>(tickDurationsNanoseconds.front()) / nanosecondsPerMicrosecond;
            result.medianMicroseconds = GetPercentileNanoseconds(tickDurationsNanoseconds, 0.50) / nanosecondsPerMicrosecond;
            result.p95Microseconds = GetPercentileNanoseconds(tickDurationsNanoseconds, 0.95) / nanosecondsPerMicrosecond;
            result.p99Microseconds = GetPercentileNanoseconds(tickDurationsNanoseconds, 0.99) / nanosecondsPerMicrosecond;
            result.maxMicroseconds = static_cast<double>(tickDurationsNanoseconds.back()) / nanosecondsPerMicrosecond;
            result.firstQuarterMeanMicroseconds = firstQuarterMeanNanoseconds / nanosecondsPerMicrosecond;
            result.lastQuarterMeanMicroseconds = lastQuarterMeanNanoseconds / nanosecondsPerMicrosecond;
            return result;
        }

        void PrintBenchmarkStats(const BenchmarkStats& stats, int32_t ticks)
        {
            constexpr double turboTickBudgetMicroseconds = 1'000'000.0 / kTurboTargetTicksPerSecond;
            const auto trendPercent = stats.firstQuarterMeanMicroseconds > 0.0
                ? ((stats.lastQuarterMeanMicroseconds / stats.firstQuarterMeanMicroseconds) - 1.0) * 100.0
                : 0.0;
            Console::WriteLine("Benchmark measurement:");
            Console::WriteLine("  ticks:             %d", ticks);
            Console::WriteLine("  elapsed:           %.6f s", stats.elapsedSeconds);
            Console::WriteLine("  actual TPS:        %.3f", stats.ticksPerSecond);
            Console::WriteLine(
                "  Turbo 320 target:  %.1f%% (%.3f TPS remaining)", (stats.ticksPerSecond / kTurboTargetTicksPerSecond) * 100.0,
                std::max(0.0, kTurboTargetTicksPerSecond - stats.ticksPerSecond));
            Console::WriteLine("  tick mean:         %.3f us", stats.meanMicroseconds);
            Console::WriteLine("  tick minimum:      %.3f us", stats.minMicroseconds);
            Console::WriteLine("  tick median:       %.3f us", stats.medianMicroseconds);
            Console::WriteLine("  tick p95:          %.3f us", stats.p95Microseconds);
            Console::WriteLine("  tick p99:          %.3f us", stats.p99Microseconds);
            Console::WriteLine("  tick maximum:      %.3f us", stats.maxMicroseconds);
            Console::WriteLine(
                "  tick trend:        %.3f -> %.3f us first/last quarter (%+.1f%%)", stats.firstQuarterMeanMicroseconds,
                stats.lastQuarterMeanMicroseconds, trendPercent);
            Console::WriteLine("  Turbo tick budget: %.3f us", turboTickBudgetMicroseconds);
        }

        void PrintBenchmarkStateSnapshot(const utf8* label, const BenchmarkStateSnapshot& snapshot)
        {
            const auto guestCount = snapshot.guestsInsidePark + snapshot.guestsOutsidePark;
            Console::WriteLine("%s simulation state:", label);
            Console::WriteLine("  simulation tick:    %u", snapshot.simulationTick);
            Console::WriteLine(
                "  guests:             %zu (%zu inside, %zu outside)", guestCount, snapshot.guestsInsidePark,
                snapshot.guestsOutsidePark);
            Console::WriteLine(
                "  guest states:       %zu walking, %zu queued, %zu on ride", snapshot.guestsWalking,
                snapshot.guestsQueuing, snapshot.guestsOnRide);
            Console::WriteLine("  transport routes:   %zu active", snapshot.activeTransportRoutes);
            Console::WriteLine("  staff / vehicles:   %zu / %zu", snapshot.staff, snapshot.vehicles);
            Console::WriteLine(
                "  shared route cache: %zu nodes, %zu targets, %zu direction / %zu distance entries, %zu single-ride targets (%s)",
                snapshot.routeNodes, snapshot.routeTargets, snapshot.routeDirectionEntries, snapshot.routeDistanceEntries,
                snapshot.singleRideTargets, snapshot.routeCacheCurrent ? "current" : "fallback or stale");
        }
    } // namespace

    // clang-format off
    static constexpr CommandLineOptionDefinition kSimulateOptions[]
    {
        { CMDLINE_TYPE_INTEGER, &_warmupTicks, kNAC, "warmup",   "number of unmeasured warm-up ticks (benchmark default: 2000)" },
        { CMDLINE_TYPE_SWITCH,  &_benchmark,   kNAC, "benchmark", "measure tick throughput and latency distribution" },
        { CMDLINE_TYPE_STRING,  &_profilePath, kNAC, "profile",   "profile measured ticks and export to a .csv or .json file" },
        kOptionTableEnd
    };

    static ExitCode HandleSimulate(CommandLineArgEnumerator* argEnumerator);

    const CommandLineCommand CommandLine::kSimulateCommands[]{
        // Main commands
        DefineCommand("", "<park file> <ticks>", kSimulateOptions, HandleSimulate),
        kCommandTableEnd
    };
    // clang-format on

    static ExitCode HandleSimulate(CommandLineArgEnumerator* argEnumerator)
    {
        const utf8* inputPath;
        if (!argEnumerator->TryPopString(&inputPath))
        {
            Console::Error::WriteLine("Expected a save file path");
            return ExitCode::fail;
        }

        int32_t ticks;
        if (!argEnumerator->TryPopInteger(&ticks))
        {
            Console::Error::WriteLine("Expected a number of ticks to simulate");
            return ExitCode::fail;
        }
        if (ticks <= 0)
        {
            Console::Error::WriteLine("The number of measured ticks must be greater than zero");
            return ExitCode::fail;
        }
        if (_warmupTicks < -1)
        {
            Console::Error::WriteLine("The number of warm-up ticks must not be negative");
            return ExitCode::fail;
        }

        const bool runBenchmark = _benchmark || !_profilePath.empty();
        const int32_t warmupTicks = _warmupTicks == -1 ? (runBenchmark ? 2000 : 0) : _warmupTicks;

        gOpenRCT2Headless = true;

#ifndef DISABLE_NETWORK
        gNetworkStart = Network::Mode::server;
#endif

        const auto initialiseStart = BenchmarkClock::now();
        std::unique_ptr<IContext> context(CreateContext());
        if (context->Initialise())
        {
            const auto initialiseDuration = BenchmarkClock::now() - initialiseStart;
            const auto loadStart = BenchmarkClock::now();
            if (!context->LoadParkFromFile(inputPath))
            {
                return ExitCode::fail;
            }
            const auto loadDuration = BenchmarkClock::now() - loadStart;

            if (runBenchmark)
            {
                Console::WriteLine("Context initialisation: %.6f s", std::chrono::duration<double>(initialiseDuration).count());
                Console::WriteLine("Park load:              %.6f s", std::chrono::duration<double>(loadDuration).count());
            }

            if (warmupTicks > 0)
            {
                Console::WriteLine("Warming up for %d ticks...", warmupTicks);
                const auto warmupStart = BenchmarkClock::now();
                for (int32_t i = 0; i < warmupTicks; i++)
                {
                    gameStateUpdateLogic();
                }
                const auto warmupDuration = BenchmarkClock::now() - warmupStart;
                if (runBenchmark)
                {
                    const auto warmupSeconds = std::chrono::duration<double>(warmupDuration).count();
                    Console::WriteLine(
                        "Warm-up:               %.6f s (%.3f TPS)", warmupSeconds,
                        static_cast<double>(warmupTicks) / warmupSeconds);
                }
            }

            if (!_profilePath.empty())
            {
                Profiling::resetData();
                Profiling::enable();
                Console::WriteLine(
                    "Profiler enabled for the measured interval; use --benchmark without --profile for clean TPS.");
            }

            Console::WriteLine("Running %d measured ticks...", ticks);
            BenchmarkStateSnapshot initialState;
            if (runBenchmark)
            {
                initialState = CaptureBenchmarkStateSnapshot();
            }
            std::vector<int64_t> tickDurationsNanoseconds;
            if (runBenchmark)
            {
                tickDurationsNanoseconds.reserve(ticks);
            }

            const auto measurementStart = BenchmarkClock::now();
            for (int32_t i = 0; i < ticks; i++)
            {
                if (runBenchmark)
                {
                    const auto tickStart = BenchmarkClock::now();
                    gameStateUpdateLogic();
                    tickDurationsNanoseconds.push_back(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(BenchmarkClock::now() - tickStart).count());
                }
                else
                {
                    gameStateUpdateLogic();
                }
            }
            const auto measurementDuration = BenchmarkClock::now() - measurementStart;

            if (!_profilePath.empty())
            {
                Profiling::disable();
                if (!Profiling::exportData(_profilePath))
                {
                    Console::Error::WriteLine("Unable to export profiler data to %s", _profilePath.c_str());
                    return ExitCode::fail;
                }
                Console::WriteLine("Profiler data:          %s", _profilePath.c_str());
            }

            if (runBenchmark)
            {
                PrintBenchmarkStats(GetBenchmarkStats(std::move(tickDurationsNanoseconds), measurementDuration), ticks);
                PrintBenchmarkStateSnapshot("Initial", initialState);
                PrintBenchmarkStateSnapshot("Final", CaptureBenchmarkStateSnapshot());
            }
            Console::WriteLine("Completed: %s", getGameState().entities.GetAllEntitiesChecksum().ToString().c_str());
        }
        else
        {
            Console::Error::WriteLine("Context initialization failed.");
            return ExitCode::fail;
        }

        return ExitCode::ok;
    }
} // namespace OpenRCT2
