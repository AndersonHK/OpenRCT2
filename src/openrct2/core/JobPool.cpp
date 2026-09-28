/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "JobPool.h"

#include "../profiling/Profiling.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <utility>

thread_local JobPool* JobPool::_currentPool = nullptr;

namespace
{
    class CurrentPoolScope
    {
    public:
        CurrentPoolScope(JobPool*& slot, JobPool* pool)
            : _slot(slot)
            , _previous(std::exchange(slot, pool))
        {
        }

        ~CurrentPoolScope()
        {
            _slot = _previous;
        }

    private:
        JobPool*& _slot;
        JobPool* _previous;
    };
} // namespace

JobPool::JobPool(size_t maxThreads)
{
    const auto hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    // ParallelFor also uses its calling thread, so leave one hardware thread available to that caller.
    const auto availableWorkers = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
    maxThreads = std::min<size_t>(maxThreads, availableWorkers);
    for (size_t n = 0; n < maxThreads; n++)
    {
        _threads.emplace_back(&JobPool::ProcessQueue, this);
    }
}

JobPool::~JobPool()
{
    // Context-owned pools normally reach destruction behind an explicit barrier. Joining here makes other owners safe when
    // their lifetime ends while work is still in flight.
    try
    {
        Join();
    }
    catch (...)
    {
        // Destructors cannot surface worker exceptions. Normal barriers rethrow them on the submitting thread.
    }

    {
        std::lock_guard lock(_mutex);
        _shouldStop = true;
    }
    _condPending.notify_all();

    for (auto& th : _threads)
    {
        assert(th.joinable());
        th.join();
    }
}

void JobPool::AddTask(std::function<void()> workFn, std::function<void()> completionFn, TaskPriority priority)
{
    // Work spawned by this pool is already covered by the active outer barrier.
    if (_currentPool == this)
    {
        EnqueueTask(std::move(workFn), std::move(completionFn), nullptr, priority);
        return;
    }

    std::scoped_lock batchLock(_batchMutex);
    EnqueueTask(std::move(workFn), std::move(completionFn), nullptr, priority);
}

void JobPool::EnqueueTask(
    std::function<void()> workFn, std::function<void()> completionFn, std::shared_ptr<TaskGroupState> group,
    TaskPriority priority)
{
    {
        std::lock_guard lock(_mutex);
        if (_shouldStop)
        {
            throw std::logic_error("Cannot submit work to a stopped JobPool");
        }
        _pending[static_cast<size_t>(priority)].push_back({ std::move(workFn), std::move(completionFn), {}, std::move(group) });
    }
    _condPending.notify_one();
}

bool JobPool::TaskGroup::IsValid() const noexcept
{
    return _owner != nullptr && _state != nullptr;
}

bool JobPool::TaskGroup::IsComplete() const
{
    if (_state == nullptr)
        return true;

    std::scoped_lock lock(_state->Mutex);
    return _state->Remaining == 0;
}

JobPool::TaskGroup JobPool::CreateTaskGroup()
{
    return TaskGroup(this, std::make_shared<TaskGroupState>());
}

void JobPool::AddTask(TaskGroup& group, std::function<void()> workFn, TaskPriority priority)
{
    if (group._owner != this || group._state == nullptr)
    {
        throw std::invalid_argument("Task group does not belong to this JobPool");
    }

    {
        std::scoped_lock groupLock(group._state->Mutex);
        group._state->Remaining++;
    }
    try
    {
        EnqueueTask(std::move(workFn), nullptr, group._state, priority);
    }
    catch (...)
    {
        std::scoped_lock groupLock(group._state->Mutex);
        group._state->Remaining--;
        group._state->Complete.notify_all();
        throw;
    }
}

void JobPool::Wait(TaskGroup& group)
{
    PROFILED_FUNCTION();
    if (group._owner != this || group._state == nullptr)
    {
        throw std::invalid_argument("Task group does not belong to this JobPool");
    }
    if (_currentPool == this)
    {
        throw std::logic_error("A JobPool worker cannot wait on its own task group");
    }

    auto* attribution = group._state->Attribution.get();
    const auto beforeLock = attribution == nullptr ? OpenRCT2::SimulationAttribution::Clock::time_point{}
                                                   : OpenRCT2::SimulationAttribution::Clock::now();
    std::unique_lock lock(group._state->Mutex);
    const auto acquired = attribution == nullptr ? OpenRCT2::SimulationAttribution::Clock::time_point{}
                                                 : OpenRCT2::SimulationAttribution::Clock::now();
    if (attribution != nullptr)
    {
        attribution->acquireMutexMs = OpenRCT2::SimulationAttribution::Milliseconds(beforeLock, acquired);
        attribution->remainingAtWait = group._state->Remaining;
    }
    group._state->Complete.wait(lock, [&group] { return group._state->Remaining == 0; });
    if (attribution != nullptr)
    {
        const auto resumed = OpenRCT2::SimulationAttribution::Clock::now();
        attribution->conditionWaitMs = OpenRCT2::SimulationAttribution::Milliseconds(acquired, resumed);
        // If ready preceded Wait, this interval includes useful caller work, not wakeup latency.
        // Clamp to wait entry so only completion-to-resumption within the actual wait is reported.
        if (attribution->ready != OpenRCT2::SimulationAttribution::Clock::time_point{})
            attribution->readyToResumeMs = OpenRCT2::SimulationAttribution::Milliseconds(
                std::max(acquired, attribution->ready), resumed);
    }
    const auto error = group._state->FirstError;
    lock.unlock();
    if (error != nullptr)
    {
        std::rethrow_exception(error);
    }
}

void JobPool::Join(std::function<void()> reportFn)
{
    if (_currentPool == this)
    {
        throw std::logic_error("A JobPool worker or callback cannot wait on its own outer task");
    }

    std::scoped_lock batchLock(_batchMutex);
    CurrentPoolScope currentPool(_currentPool, this);
    JoinInternal(std::move(reportFn));
}

void JobPool::JoinInternal(std::function<void()> reportFn)
{
    std::unique_lock lock(_mutex);
    std::exception_ptr firstError;
    const auto invoke = [&](const auto& callback) {
        if (!callback)
            return;
        lock.unlock();
        try
        {
            callback();
        }
        catch (...)
        {
            if (firstError == nullptr)
                firstError = std::current_exception();
        }
        lock.lock();
    };
    while (true)
    {
        // Wait for the queue to become empty or having completed tasks.
        _condComplete.wait(lock, [this]() { return (!HasPendingTasks() && _processing == 0) || !_completed.empty(); });

        // Dispatch all completion callbacks if there are any.
        while (!_completed.empty())
        {
            auto taskData = std::move(_completed.front());
            _completed.pop_front();

            if (taskData.Error != nullptr)
            {
                if (firstError == nullptr)
                    firstError = taskData.Error;
            }
            else
                invoke(taskData.CompletionFn);
        }

        invoke(reportFn);

        // If everything is empty and no more work has to be done we can stop waiting.
        if (_completed.empty() && !HasPendingTasks() && _processing == 0)
            break;
    }

    lock.unlock();
    if (firstError != nullptr)
        std::rethrow_exception(firstError);
}

void JobPool::ParallelFor(
    size_t count, const std::function<void(size_t)>& workFn, size_t grainSize, std::function<void()> reportFn,
    TaskPriority priority)
{
    if (count == 0)
        return;

    // A worker cannot wait for its own outer task to leave _processing. Execute nested work serially on that worker instead.
    if (_currentPool == this)
    {
        for (size_t i = 0; i < count; i++)
        {
            workFn(i);
        }
        if (reportFn)
        {
            reportFn();
        }
        return;
    }

    namespace Attribution = OpenRCT2::SimulationAttribution;
    const bool trace = Attribution::TraceParallel();
    const auto traceStart = trace ? Attribution::Read(Attribution::state.reader) : Attribution::Stamp{};
    std::optional<Attribution::ParallelEvent> traceEvent;
    grainSize = std::max<size_t>(grainSize, 1);
    std::atomic_size_t nextIndex{ 0 };
    const auto processNext = [&]() {
        while (true)
        {
            const auto first = nextIndex.fetch_add(grainSize, std::memory_order_relaxed);
            if (first >= count)
                return;
            const auto last = first + std::min(grainSize, count - first);
            for (auto index = first; index < last; index++)
            {
                workFn(index);
            }
        }
    };

    const auto batchCount = 1 + ((count - 1) / grainSize);
    const auto workerTasks = std::min(batchCount, _threads.size());
    auto group = CreateTaskGroup();
    if (trace)
    {
        group._state->Attribution = std::make_unique<Attribution::Workers>();
        group._state->Attribution->reader = Attribution::state.reader;
        group._state->Attribution->groupStart = traceStart.time;
        auto& event = traceEvent.emplace();
        event.count = count;
        event.grain = grainSize;
        event.workers = workerTasks;
    }
    for (size_t i = 0; i < workerTasks; i++)
    {
        AddTask(group, processNext, priority);
    }

    const auto submitted = trace ? Attribution::Read(Attribution::state.reader) : Attribution::Stamp{};
    // The submitting thread participates instead of blocking while workers consume the queue. Both paths still reach the
    // barrier after an exception so worker lambdas never outlive references captured from this stack frame.
    std::exception_ptr firstError;
    try
    {
        processNext();
    }
    catch (...)
    {
        firstError = std::current_exception();
    }

    const auto callerEnd = trace ? Attribution::Read(Attribution::state.reader) : Attribution::Stamp{};
    if (nextIndex.load(std::memory_order_relaxed) >= count)
    {
        // All indices are claimed. Consumers still in the queue can only discover
        // that there is no work. Retire them here instead of waiting for unrelated
        // busy workers to wake just to run these empty closures. Already running
        // consumers remain in the group and must complete before stack captures die.
        size_t removed = 0;
        {
            std::scoped_lock lock(_mutex);
            for (auto& queue : _pending)
                removed += std::erase_if(queue, [&](const auto& task) { return task.Group == group._state; });
            if (removed != 0)
                _condComplete.notify_all();
        }
        if (removed != 0)
        {
            std::scoped_lock lock(group._state->Mutex);
            assert(group._state->Remaining >= removed);
            group._state->Remaining -= removed;
            if (trace)
                group._state->Attribution->retired += removed;
            if (group._state->Remaining == 0)
            {
                if (trace)
                    group._state->Attribution->ready = Attribution::Clock::now();
                group._state->Complete.notify_all();
            }
        }
    }

    const auto waitStart = trace ? Attribution::Read(Attribution::state.reader) : Attribution::Stamp{};
    try
    {
        Wait(group);
    }
    catch (...)
    {
        if (firstError == nullptr)
            firstError = std::current_exception();
    }

    if (trace)
    {
        const auto end = Attribution::Read(Attribution::state.reader);
        auto& event = *traceEvent;
        static_cast<Attribution::Event&>(event) = Attribution::MakeEvent(
            Attribution::state.tick, Attribution::state.epoch, traceStart, end);
        event.submitMs = Attribution::Milliseconds(traceStart.time, submitted.time);
        event.callerMs = Attribution::Milliseconds(submitted.time, callerEnd.time);
        event.retireMs = Attribution::Milliseconds(callerEnd.time, waitStart.time);
        event.waitMs = Attribution::Milliseconds(waitStart.time, end.time);
        const auto caller = Attribution::MakeEvent(0, traceStart.time, submitted, callerEnd);
        const auto waiting = Attribution::MakeEvent(0, traceStart.time, waitStart, end);
        event.callerCycles = caller.cycles;
        event.callerCyclesAvailable = caller.cyclesAvailable;
        event.waitCycles = waiting.cycles;
        event.waitCyclesAvailable = waiting.cyclesAvailable;
        // Wait observed Remaining == 0 under this same mutex; no group workers can mutate the result afterward.
        {
            std::scoped_lock lock(group._state->Mutex);
            event.worker = *group._state->Attribution;
        }
        Attribution::state.parallel[Attribution::state.phase == Attribution::Phase::peeps ? 0 : 1].Add(event);
    }
    if (reportFn)
    {
        try
        {
            reportFn();
        }
        catch (...)
        {
            if (firstError == nullptr)
                firstError = std::current_exception();
        }
    }

    if (firstError != nullptr)
        std::rethrow_exception(firstError);
}

bool JobPool::IsBusy()
{
    std::lock_guard lock(_mutex);
    return _processing != 0 || HasPendingTasks();
}

bool JobPool::HasPendingTasks() const noexcept
{
    return std::ranges::any_of(_pending, [](const auto& queue) { return !queue.empty(); });
}

JobPool::TaskData JobPool::TakeNextTask()
{
    for (auto& queue : _pending)
    {
        if (!queue.empty())
        {
            auto task = std::move(queue.front());
            queue.pop_front();
            return task;
        }
    }
    throw std::logic_error("No pending JobPool task");
}

void JobPool::ProcessQueue()
{
    CurrentPoolScope currentPool(_currentPool, this);
    std::unique_lock lock(_mutex);
    do
    {
        // Wait for work or cancellation.
        _condPending.wait(lock, [this]() { return _shouldStop || HasPendingTasks(); });

        if (HasPendingTasks())
        {
            _processing++;

            auto taskData = TakeNextTask();

            lock.unlock();

            auto* attribution = taskData.Group == nullptr ? nullptr : taskData.Group->Attribution.get();
            const auto workerStart = attribution == nullptr ? OpenRCT2::SimulationAttribution::Stamp{}
                                                            : OpenRCT2::SimulationAttribution::Read(attribution->reader);
            try
            {
                taskData.WorkFn();
            }
            catch (...)
            {
                taskData.Error = std::current_exception();
            }

            const auto workerEnd = attribution == nullptr ? OpenRCT2::SimulationAttribution::Stamp{}
                                                          : OpenRCT2::SimulationAttribution::Read(attribution->reader);
            if (taskData.Group != nullptr)
            {
                std::scoped_lock groupLock(taskData.Group->Mutex);
                if (attribution != nullptr)
                    attribution->Complete(workerStart, workerEnd, OpenRCT2::SimulationAttribution::Clock::now());
                if (taskData.Error != nullptr && taskData.Group->FirstError == nullptr)
                {
                    taskData.Group->FirstError = taskData.Error;
                }
                assert(taskData.Group->Remaining > 0);
                taskData.Group->Remaining--;
                if (taskData.Group->Remaining == 0)
                {
                    if (attribution != nullptr)
                        attribution->ready = OpenRCT2::SimulationAttribution::Clock::now();
                    taskData.Group->Complete.notify_all();
                }
            }

            lock.lock();

            // Independently waitable groups publish completion directly to their
            // group state. The legacy completion queue is reserved for AddTask/Join
            // so one producer cannot consume or rethrow another producer's result.
            if (taskData.Group == nullptr)
            {
                _completed.push_back(std::move(taskData));
            }

            _processing--;
            _condComplete.notify_one();
        }
    } while (!_shouldStop);
}
