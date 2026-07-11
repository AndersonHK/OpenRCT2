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

#include <chrono>
#include <cstdlib>
#include <memory>

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

        BenchmarkClock::duration RunSimulationTicks(int32_t ticks)
        {
            const auto start = BenchmarkClock::now();
            for (int32_t i = 0; i < ticks; i++)
            {
                gameStateUpdateLogic();
            }
            return BenchmarkClock::now() - start;
        }

        void PrintBenchmarkStats(BenchmarkClock::duration duration, int32_t ticks)
        {
            const auto elapsedSeconds = std::chrono::duration<double>(duration).count();
            const auto ticksPerSecond = static_cast<double>(ticks) / elapsedSeconds;
            constexpr double turboTickBudgetMicroseconds = 1'000'000.0 / kTurboTargetTicksPerSecond;
            Console::WriteLine("Benchmark measurement (headless, offline, uncapped logical ticks):");
            Console::WriteLine("  ticks:             %d", ticks);
            Console::WriteLine("  elapsed:           %.6f s", elapsedSeconds);
            Console::WriteLine("  actual TPS:        %.3f", ticksPerSecond);
            Console::WriteLine(
                "  Turbo 320 target:  %.1f%%", (ticksPerSecond / kTurboTargetTicksPerSecond) * 100.0);
            Console::WriteLine("  tick mean:         %.3f us", elapsedSeconds * 1'000'000.0 / ticks);
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
        { CMDLINE_TYPE_SWITCH,  &_benchmark,   kNAC, "benchmark", "measure logical simulation tick throughput" },
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
        if (!runBenchmark)
        {
            gNetworkStart = Network::Mode::server;
        }
#endif

        std::unique_ptr<IContext> context(CreateContext());
        if (context->Initialise())
        {
            if (!context->LoadParkFromFile(inputPath))
            {
                return ExitCode::fail;
            }

            if (warmupTicks > 0)
            {
                Console::WriteLine("Warming up for %d ticks...", warmupTicks);
                const auto warmupDuration = RunSimulationTicks(warmupTicks);
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
            const auto measurementDuration = RunSimulationTicks(ticks);

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
                PrintBenchmarkStats(measurementDuration, ticks);
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
