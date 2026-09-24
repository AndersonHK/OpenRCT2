/*****************************************************************************
 * Copyright (c) 2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace OpenRCT2::SimulationAttribution
{
    using Clock = std::chrono::steady_clock;
    using CycleReader = uint64_t (*)() noexcept;
    enum class Phase : uint8_t
    {
        tickPrelude,
        logicPrelude,
        map,
        routes,
        peeps,
        restoreProvisional,
        vehicles,
        miscEntities,
        rides,
        park,
        researchRatings,
        newsAnimations,
        spatialIndex,
        actionsNetworkScripts,
        tickTail,
        count
    };
    constexpr std::array<const char*, static_cast<size_t>(Phase::count)> kNames = {
        "tickPrelude",  "logicPrelude",          "map",     "routes", "peeps",           "restoreProvisional",
        "vehicles",     "miscEntities",          "rides",   "park",   "researchRatings", "newsAnimations",
        "spatialIndex", "actionsNetworkScripts", "tickTail"
    };
    struct Stamp
    {
        Clock::time_point time{};
        uint64_t cycles{};
    };
    inline Stamp Read(CycleReader reader) noexcept
    {
        const auto cycles = reader == nullptr ? 0 : reader();
        return { Clock::now(), cycles };
    }
    inline double Milliseconds(Clock::time_point begin, Clock::time_point end) noexcept
    {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }
    struct Event
    {
        uint32_t tick{};
        double offsetMs{}, wallMs{};
        uint64_t cycles{};
        bool cyclesAvailable{};
    };
    inline Event MakeEvent(uint32_t tick, Clock::time_point epoch, Stamp start, Stamp end) noexcept
    {
        return { tick, Milliseconds(epoch, start.time), Milliseconds(start.time, end.time),
                 end.cycles >= start.cycles ? end.cycles - start.cycles : 0, start.cycles != 0 && end.cycles >= start.cycles };
    }
    template<typename T>
    struct Samples
    {
        uint64_t count{}, cycles{}, cycleSamples{};
        double wallMs{};
        std::array<T, 16> worst{};
        size_t retained{}, minimum{};
        void Add(const T& event) noexcept
        {
            ++count;
            wallMs += event.wallMs;
            if (event.cyclesAvailable)
            {
                cycles += event.cycles;
                ++cycleSamples;
            }
            if (retained == worst.size() && event.wallMs <= worst[minimum].wallMs)
                return;
            const auto index = retained < worst.size() ? retained++ : minimum;
            worst[index] = event;
            minimum = 0;
            for (size_t i = 1; i < retained; ++i)
                if (worst[i].wallMs < worst[minimum].wallMs)
                    minimum = i;
        }
    };
    // Accessed only under the existing task-group mutex, except immutable reader/start fields set before enqueue.
    struct Workers
    {
        CycleReader reader{};
        Clock::time_point groupStart{}, ready{};
        size_t completed{}, retired{}, remainingAtWait{};
        double totalWorkMs{}, maximumWorkMs{}, maximumStartDelayMs{}, maximumCompletionLockMs{};
        uint64_t totalCycles{}, maximumWorkCycles{};
        size_t cycleSamples{};
        bool maximumWorkCyclesAvailable{};
        double acquireMutexMs{}, conditionWaitMs{}, readyToResumeMs{};
        void Complete(Stamp start, Stamp end, Clock::time_point acquired) noexcept
        {
            ++completed;
            const auto wall = Milliseconds(start.time, end.time);
            const bool available = start.cycles != 0 && end.cycles >= start.cycles;
            const auto cycles = available ? end.cycles - start.cycles : 0;
            totalWorkMs += wall;
            totalCycles += cycles;
            cycleSamples += available ? 1 : 0;
            maximumStartDelayMs = std::max(maximumStartDelayMs, Milliseconds(groupStart, start.time));
            maximumCompletionLockMs = std::max(maximumCompletionLockMs, Milliseconds(end.time, acquired));
            if (wall >= maximumWorkMs)
            {
                maximumWorkMs = wall;
                maximumWorkCycles = cycles;
                maximumWorkCyclesAvailable = available;
            }
        }
    };
    struct ParallelEvent : Event
    {
        size_t count{}, grain{}, workers{};
        double submitMs{}, callerMs{}, retireMs{}, waitMs{};
        uint64_t callerCycles{}, waitCycles{};
        bool callerCyclesAvailable{}, waitCyclesAvailable{};
        Workers worker{};
    };
    struct State
    {
        bool enabled{};
        Phase phase = Phase::count;
        uint32_t tick{};
        Clock::time_point epoch{};
        CycleReader reader{};
        std::array<Samples<Event>, static_cast<size_t>(Phase::count)> phases{};
        std::array<Samples<ParallelEvent>, 2> parallel{};
    };
    // No worker modifies this state. Worker results cross the existing group completion barrier.
    inline thread_local State state;
    inline void Begin(bool enabled, Clock::time_point epoch, CycleReader reader) noexcept
    {
        state = {};
        state.enabled = enabled;
        state.epoch = epoch;
        state.reader = reader;
    }
    class Sequence
    {
        bool _enabled;
        Phase _previous;
        uint32_t _previousTick;
        Stamp _start{};
        void Finish() noexcept
        {
            if (_enabled && state.phase != Phase::count)
                state.phases[static_cast<size_t>(state.phase)].Add(
                    MakeEvent(state.tick, state.epoch, _start, Read(state.reader)));
        }

    public:
        Sequence(Phase phase, uint32_t tick) noexcept
            : _enabled(state.enabled)
            , _previous(state.phase)
            , _previousTick(state.tick)
        {
            if (_enabled)
            {
                state.phase = phase;
                state.tick = tick;
                _start = Read(state.reader);
            }
        }
        ~Sequence()
        {
            Finish();
            if (_enabled)
            {
                state.phase = _previous;
                state.tick = _previousTick;
            }
        }
        void Next(Phase phase) noexcept
        {
            if (!_enabled)
                return;
            Finish();
            state.phase = phase;
            _start = Read(state.reader);
        }
    };
    inline bool TraceParallel() noexcept
    {
        return state.enabled && (state.phase == Phase::peeps || state.phase == Phase::vehicles);
    }
} // namespace OpenRCT2::SimulationAttribution
