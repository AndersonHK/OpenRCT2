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
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>

class JobPool
{
private:
    struct TaskGroupState;

    struct TaskData
    {
        std::function<void()> WorkFn;
        std::function<void()> CompletionFn;
        std::exception_ptr Error{};
        std::shared_ptr<TaskGroupState> Group;
    };

    struct TaskGroupState
    {
        std::mutex Mutex;
        std::condition_variable Complete;
        size_t Remaining = 0;
        std::exception_ptr FirstError{};
    };

    bool _shouldStop = false;
    size_t _processing = 0;
    std::vector<std::thread> _threads;
    static constexpr size_t kPriorityCount = 3;
    std::array<std::deque<TaskData>, kPriorityCount> _pending;
    std::deque<TaskData> _completed;
    std::condition_variable _condPending;
    std::condition_variable _condComplete;
    std::mutex _mutex;
    // Legacy AddTask/Join remains one exclusive completion-callback batch.
    std::mutex _batchMutex;
    static thread_local JobPool* _currentPool;

public:
    enum class TaskPriority : uint8_t
    {
        foreground,
        normal,
        background,
    };

    /**
     * Ownership token for one independently waitable set of tasks. Unlike the legacy
     * AddTask/Join queue, task groups from unrelated producers may be in flight at
     * the same time. Callers must keep captured data alive until Wait returns.
     */
    class TaskGroup
    {
        friend class JobPool;

    private:
        JobPool* _owner = nullptr;
        std::shared_ptr<TaskGroupState> _state;

        TaskGroup(JobPool* owner, std::shared_ptr<TaskGroupState> state)
            : _owner(owner)
            , _state(std::move(state))
        {
        }

    public:
        TaskGroup() = default;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsComplete() const;
    };

    explicit JobPool(size_t maxThreads = 255);
    ~JobPool();

    // Completion callbacks run synchronously on the thread that calls Join, never on a worker.
    void AddTask(
        std::function<void()> workFn, std::function<void()> completionFn = nullptr,
        TaskPriority priority = TaskPriority::normal);
    // Join is the ownership barrier for queued captures and must not be called by this pool's worker or completion callback.
    void Join(std::function<void()> reportFn = nullptr);
    // ParallelFor waits on its own task group; unrelated producers remain in flight. Nested worker calls execute serially.
    void ParallelFor(
        size_t count, const std::function<void(size_t)>& workFn, size_t grainSize = 1,
        std::function<void()> reportFn = nullptr, TaskPriority priority = TaskPriority::normal);
    [[nodiscard]] TaskGroup CreateTaskGroup();
    void AddTask(TaskGroup& group, std::function<void()> workFn, TaskPriority priority = TaskPriority::normal);
    void Wait(TaskGroup& group);
    bool IsBusy();

private:
    void EnqueueTask(
        std::function<void()> workFn, std::function<void()> completionFn,
        std::shared_ptr<TaskGroupState> group = nullptr, TaskPriority priority = TaskPriority::normal);
    [[nodiscard]] bool HasPendingTasks() const noexcept;
    TaskData TakeNextTask();
    void JoinInternal(std::function<void()> reportFn);
    void ProcessQueue();
};
