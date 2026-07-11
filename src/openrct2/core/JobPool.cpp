/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "JobPool.h"

#include <algorithm>
#include <cassert>
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
        assert(th.joinable() != false);
        th.join();
    }
}

void JobPool::AddTask(std::function<void()> workFn, std::function<void()> completionFn)
{
    // Work spawned by this pool is already covered by the active outer barrier.
    if (_currentPool == this)
    {
        EnqueueTask(std::move(workFn), std::move(completionFn));
        return;
    }

    std::scoped_lock batchLock(_batchMutex);
    EnqueueTask(std::move(workFn), std::move(completionFn));
}

void JobPool::EnqueueTask(std::function<void()> workFn, std::function<void()> completionFn)
{
    {
        std::lock_guard lock(_mutex);
        if (_shouldStop)
        {
            throw std::logic_error("Cannot submit work to a stopped JobPool");
        }
        _pending.push_back({ std::move(workFn), std::move(completionFn) });
    }
    _condPending.notify_one();
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
        _condComplete.wait(lock, [this]() { return (_pending.empty() && _processing == 0) || !_completed.empty(); });

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
        if (_completed.empty() && _pending.empty() && _processing == 0)
            break;
    }

    lock.unlock();
    if (firstError != nullptr)
        std::rethrow_exception(firstError);
}

void JobPool::ParallelFor(
    size_t count, const std::function<void(size_t)>& workFn, size_t grainSize, std::function<void()> reportFn)
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

    std::scoped_lock batchLock(_batchMutex);
    CurrentPoolScope currentPool(_currentPool, this);
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
    for (size_t i = 0; i < workerTasks; i++)
    {
        EnqueueTask(processNext, nullptr);
    }

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

    try
    {
        JoinInternal(std::move(reportFn));
    }
    catch (...)
    {
        if (firstError == nullptr)
            firstError = std::current_exception();
    }

    if (firstError != nullptr)
        std::rethrow_exception(firstError);
}

bool JobPool::IsBusy()
{
    std::lock_guard lock(_mutex);
    return _processing != 0 || !_pending.empty();
}

void JobPool::ProcessQueue()
{
    CurrentPoolScope currentPool(_currentPool, this);
    std::unique_lock lock(_mutex);
    do
    {
        // Wait for work or cancellation.
        _condPending.wait(lock, [this]() { return _shouldStop || !_pending.empty(); });

        if (!_pending.empty())
        {
            _processing++;

            auto taskData = std::move(_pending.front());
            _pending.pop_front();

            lock.unlock();

            try
            {
                taskData.WorkFn();
            }
            catch (...)
            {
                taskData.Error = std::current_exception();
            }

            lock.lock();

            _completed.push_back(std::move(taskData));

            _processing--;
            _condComplete.notify_one();
        }
    } while (!_shouldStop);
}
