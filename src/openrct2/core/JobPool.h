/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class JobPool
{
private:
    struct TaskData
    {
        std::function<void()> WorkFn;
        std::function<void()> CompletionFn;
        std::exception_ptr Error{};
    };

    bool _shouldStop = false;
    size_t _processing = 0;
    std::vector<std::thread> _threads;
    std::deque<TaskData> _pending;
    std::deque<TaskData> _completed;
    std::condition_variable _condPending;
    std::condition_variable _condComplete;
    std::mutex _mutex;
    // ParallelFor is an exclusive submit-and-barrier batch. This prevents one caller from dispatching another caller's
    // completion functions or returning while jobs which reference its stack are still queued.
    std::mutex _batchMutex;
    static thread_local JobPool* _currentPool;

public:
    explicit JobPool(size_t maxThreads = 255);
    ~JobPool();

    // Completion callbacks run synchronously on the thread that calls Join, never on a worker.
    void AddTask(std::function<void()> workFn, std::function<void()> completionFn = nullptr);
    // Join is the ownership barrier for queued captures and must not be called by this pool's worker or completion callback.
    void Join(std::function<void()> reportFn = nullptr);
    // ParallelFor is one exclusive submit/barrier operation. Nested calls execute serially to avoid waiting on their own worker.
    void ParallelFor(
        size_t count, const std::function<void(size_t)>& workFn, size_t grainSize = 1,
        std::function<void()> reportFn = nullptr);
    bool IsBusy();

private:
    void EnqueueTask(std::function<void()> workFn, std::function<void()> completionFn);
    void JoinInternal(std::function<void()> reportFn);
    void ProcessQueue();
};
