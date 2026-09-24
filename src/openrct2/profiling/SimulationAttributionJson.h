/*****************************************************************************
 * Copyright (c) 2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../core/Json.hpp"
#include "SimulationAttribution.h"

namespace OpenRCT2::SimulationAttribution
{
    inline json_t EventJson(const Event& e)
    {
        return { { "simulationTick", e.tick },
                 { "offsetMs", e.offsetMs },
                 { "wallMs", e.wallMs },
                 { "threadCycles", e.cycles },
                 { "threadCyclesAvailable", e.cyclesAvailable } };
    }
    inline json_t EventJson(const ParallelEvent& e)
    {
        auto result = EventJson(static_cast<const Event&>(e));
        const auto& w = e.worker;
        result["parallel"] = { { "count", e.count },
                               { "grain", e.grain },
                               { "submittedWorkers", e.workers },
                               { "submitMs", e.submitMs },
                               { "callerMs", e.callerMs },
                               { "retireMs", e.retireMs },
                               { "waitMs", e.waitMs },
                               { "callerCycles", e.callerCycles },
                               { "callerCyclesAvailable", e.callerCyclesAvailable },
                               { "waitCycles", e.waitCycles },
                               { "waitCyclesAvailable", e.waitCyclesAvailable },
                               { "completedWorkers", w.completed },
                               { "retiredWorkers", w.retired },
                               { "remainingAtWait", w.remainingAtWait },
                               { "workerTotalWorkMs", w.totalWorkMs },
                               { "workerTotalCycles", w.totalCycles },
                               { "workerCycleSamples", w.cycleSamples },
                               { "workerMaxWorkMs", w.maximumWorkMs },
                               { "workerMaxWorkCycles", w.maximumWorkCycles },
                               { "workerMaxWorkCyclesAvailable", w.maximumWorkCyclesAvailable },
                               { "workerMaxStartDelayMs", w.maximumStartDelayMs },
                               { "workerMaxCompletionLockMs", w.maximumCompletionLockMs },
                               { "waitAcquireMutexMs", w.acquireMutexMs },
                               { "conditionWaitMs", w.conditionWaitMs },
                               { "readyToResumeMs", w.readyToResumeMs } };
        return result;
    }
    template<typename T>
    inline json_t SamplesJson(const Samples<T>& samples)
    {
        auto events = samples.worst;
        std::sort(events.begin(), events.begin() + samples.retained, [](const auto& a, const auto& b) {
            return a.wallMs > b.wallMs;
        });
        auto worst = json_t::array();
        for (size_t i = 0; i < samples.retained; ++i)
            worst.push_back(EventJson(events[i]));
        return { { "count", samples.count },
                 { "wallMs", samples.wallMs },
                 { "threadCycles", samples.cycles },
                 { "threadCycleSamples", samples.cycleSamples },
                 { "worst", std::move(worst) } };
    }
    // Called only after measurement. No JSON allocations or output occur in simulation/worker scopes.
    inline json_t Report()
    {
        auto phases = json_t::object();
        for (size_t i = 0; i < state.phases.size(); ++i)
            phases[kNames[i]] = SamplesJson(state.phases[i]);
        return {
            { "schema", 1 },
            { "capacityPerPhase", 16 },
            { "phases", std::move(phases) },
            { "parallel", { { "peeps", SamplesJson(state.parallel[0]) }, { "vehicles", SamplesJson(state.parallel[1]) } } },
            { "scope",
              "Opt-in measurement-only attribution, not clean acceptance. Main phase durations exclude nested logic phases; "
              "ParallelFor overlaps its containing phase. Worker totals overlap caller and other workers. Thread cycles are "
              "not CPU time. Start delay includes staggered submissions and queue waiting. Ready-to-resume includes condition "
              "wakeup, mutex reacquisition and main-thread descheduling; it does not identify OS cause." }
        };
    }
} // namespace OpenRCT2::SimulationAttribution
