/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>
#include <openrct2/core/JobPool.h>
#include <stdexcept>
#include <thread>
#include <vector>

TEST(JobPoolTest, ParallelForDoesNotWaitForQueuedEmptyConsumers)
{
    JobPool pool(1);
    std::promise<void> started, release;
    auto released = release.get_future().share();
    pool.AddTask([&]() {
        started.set_value();
        released.wait();
    });
    started.get_future().wait();
    std::array<std::atomic_uint32_t, 64> visits{};
    auto done = std::async(std::launch::async, [&]() {
        namespace Attribution = OpenRCT2::SimulationAttribution;
        Attribution::Begin(true, Attribution::Clock::now(), nullptr);
        Attribution::Sequence phase(Attribution::Phase::peeps, 42);
        pool.ParallelFor(visits.size(), [&](size_t i) { ++visits[i]; }, 4);
        return Attribution::state.parallel[0];
    });
    const auto status = done.wait_for(std::chrono::seconds(2));
    release.set_value();
    const auto samples = done.get();
    pool.Join();
    EXPECT_EQ(samples.count, 1u);
    EXPECT_EQ(samples.worst[0].worker.completed, 0u);
    EXPECT_EQ(samples.worst[0].worker.retired, 1u);
    EXPECT_EQ(samples.worst[0].worker.remainingAtWait, 0u);
    EXPECT_EQ(status, std::future_status::ready);
    for (const auto& count : visits)
        EXPECT_EQ(count.load(), 1u);
}

TEST(JobPoolTest, ParallelForVisitsEveryIndexExactlyOnce)
{
    constexpr size_t count = 257;
    JobPool pool(4);
    std::vector<std::atomic_uint32_t> visits(count);
    std::vector<size_t> output(count);
    for (auto& visit : visits)
    {
        visit.store(0, std::memory_order_relaxed);
    }

    pool.ParallelFor(
        count,
        [&](size_t index) {
            visits[index].fetch_add(1, std::memory_order_relaxed);
            output[index] = index * index;
        },
        8);

    for (size_t i = 0; i < count; i++)
    {
        EXPECT_EQ(visits[i].load(std::memory_order_relaxed), 1u);
        EXPECT_EQ(output[i], i * i);
    }
}

TEST(JobPoolTest, NestedParallelForUsesTheExistingOuterBarrier)
{
    JobPool pool(4);
    std::atomic_size_t visits{ 0 };

    pool.ParallelFor(8, [&](size_t) { pool.ParallelFor(5, [&](size_t) { visits.fetch_add(1, std::memory_order_relaxed); }); });

    EXPECT_EQ(visits.load(std::memory_order_relaxed), 40u);
}

TEST(JobPoolTest, ReportCallbackCanRunANestedParallelBatch)
{
    JobPool pool(4);
    std::atomic_size_t nestedVisits{ 0 };

    pool.ParallelFor(
        64, [](size_t) {}, 4,
        [&]() { pool.ParallelFor(3, [&](size_t) { nestedVisits.fetch_add(1, std::memory_order_relaxed); }); });

    EXPECT_GE(nestedVisits.load(std::memory_order_relaxed), 3u);
    EXPECT_EQ(nestedVisits.load(std::memory_order_relaxed) % 3, 0u);
}

TEST(JobPoolTest, ParallelForDrainsRemainingWorkBeforeRethrowing)
{
    JobPool pool(4);
    std::atomic_size_t completed{ 0 };

    EXPECT_THROW(
        pool.ParallelFor(
            64,
            [&](size_t index) {
                if (index == 17)
                {
                    throw std::runtime_error("expected worker failure");
                }
                completed.fetch_add(1, std::memory_order_relaxed);
            }),
        std::runtime_error);

    EXPECT_EQ(completed.load(std::memory_order_relaxed), 63u);
}

TEST(JobPoolTest, JoinDrainsTasksAfterACompletionCallbackThrows)
{
    JobPool pool(2);
    std::atomic_size_t completed{ 0 };
    pool.AddTask(
        [&]() { completed.fetch_add(1, std::memory_order_relaxed); },
        []() { throw std::runtime_error("expected completion failure"); });
    pool.AddTask([&]() { completed.fetch_add(1, std::memory_order_relaxed); });

    EXPECT_THROW(pool.Join(), std::runtime_error);
    EXPECT_EQ(completed.load(std::memory_order_relaxed), 2u);
}

TEST(JobPoolTest, CompletionCallbackCanQueueWorkForTheSameBarrier)
{
    JobPool pool(2);
    std::atomic_size_t completed{ 0 };
    pool.AddTask(
        [&]() { completed.fetch_add(1, std::memory_order_relaxed); },
        [&]() { pool.AddTask([&]() { completed.fetch_add(1, std::memory_order_relaxed); }); });

    pool.Join();
    EXPECT_EQ(completed.load(std::memory_order_relaxed), 2u);
}

TEST(JobPoolTest, DestructorDrainsQueuedTasks)
{
    std::atomic_size_t completed{ 0 };
    {
        JobPool pool(2);
        for (size_t i = 0; i < 16; i++)
        {
            pool.AddTask([&]() { completed.fetch_add(1, std::memory_order_relaxed); });
        }
    }

    EXPECT_EQ(completed.load(std::memory_order_relaxed), 16u);
}

TEST(JobPoolTest, IndependentTaskGroupsMayOverlapAndWaitSeparately)
{
    JobPool pool(4);
    auto first = pool.CreateTaskGroup();
    auto second = pool.CreateTaskGroup();
    std::atomic_bool releaseFirst{ false };
    std::atomic_bool secondCompleted{ false };

    pool.AddTask(first, [&]() {
        while (!releaseFirst.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    });
    pool.AddTask(second, [&]() { secondCompleted.store(true, std::memory_order_release); });

    pool.Wait(second);
    EXPECT_TRUE(secondCompleted.load(std::memory_order_acquire));
    EXPECT_FALSE(first.IsComplete());

    releaseFirst.store(true, std::memory_order_release);
    pool.Wait(first);
    EXPECT_TRUE(first.IsComplete());
}

TEST(JobPoolTest, TaskGroupDrainsAllWorkBeforeRethrowing)
{
    JobPool pool(4);
    auto group = pool.CreateTaskGroup();
    std::atomic_size_t completed{ 0 };
    for (size_t i = 0; i < 32; i++)
    {
        pool.AddTask(group, [&, i]() {
            if (i == 11)
                throw std::runtime_error("expected grouped worker failure");
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    EXPECT_THROW(pool.Wait(group), std::runtime_error);
    EXPECT_EQ(completed.load(std::memory_order_relaxed), 31u);
}

TEST(JobPoolTest, ForegroundTasksPreemptQueuedBackgroundWork)
{
    JobPool pool(1);
    auto blocker = pool.CreateTaskGroup();
    auto background = pool.CreateTaskGroup();
    auto foreground = pool.CreateTaskGroup();
    std::atomic_bool blockerStarted{ false };
    std::atomic_bool releaseBlocker{ false };
    std::vector<int32_t> completionOrder;

    pool.AddTask(blocker, [&]() {
        blockerStarted.store(true, std::memory_order_release);
        while (!releaseBlocker.load(std::memory_order_acquire))
            std::this_thread::yield();
    });
    while (!blockerStarted.load(std::memory_order_acquire))
        std::this_thread::yield();

    pool.AddTask(background, [&]() { completionOrder.push_back(2); }, JobPool::TaskPriority::background);
    pool.AddTask(foreground, [&]() { completionOrder.push_back(1); }, JobPool::TaskPriority::foreground);
    releaseBlocker.store(true, std::memory_order_release);

    pool.Wait(blocker);
    pool.Wait(foreground);
    pool.Wait(background);
    EXPECT_EQ(completionOrder, (std::vector<int32_t>{ 1, 2 }));
}

TEST(JobPoolTest, SimulationAttributionRetainsLargestSixteenWithoutClockAssumptions)
{
    using namespace OpenRCT2::SimulationAttribution;
    Samples<Event> samples;
    for (uint32_t i = 0; i < 40; ++i)
        samples.Add(Event{ i, static_cast<double>(i), static_cast<double>(i), i, true });
    EXPECT_EQ(samples.count, 40u);
    EXPECT_EQ(samples.retained, 16u);
    EXPECT_EQ(samples.wallMs, 780.0);
    EXPECT_EQ(samples.cycles, 780u);
    EXPECT_EQ(samples.cycleSamples, 40u);
    for (const auto& event : samples.worst)
        EXPECT_GE(event.tick, 24u);
}

TEST(JobPoolTest, SimulationAttributionPreservesWorkerExceptionAndBarrierOwnership)
{
    namespace Attribution = OpenRCT2::SimulationAttribution;
    struct Reset
    {
        ~Reset()
        {
            Attribution::Begin(false, {}, nullptr);
        }
    } reset;
    Attribution::Begin(true, Attribution::Clock::now(), nullptr);
    JobPool pool(1);
    std::promise<void> workerStarted, release;
    auto started = workerStarted.get_future();
    auto released = release.get_future();
    const auto caller = std::this_thread::get_id();
    std::atomic<size_t> visits{};
    {
        Attribution::Sequence phase(Attribution::Phase::vehicles, 1234);
        EXPECT_THROW(
            pool.ParallelFor(
                2,
                [&](size_t) {
                    ++visits;
                    if (std::this_thread::get_id() == caller)
                    {
                        started.wait();
                        release.set_value();
                    }
                    else
                    {
                        workerStarted.set_value();
                        released.wait();
                        throw std::runtime_error("worker exception retained");
                    }
                }),
            std::runtime_error);
    }
    EXPECT_EQ(visits.load(), 2u);
    const auto& samples = Attribution::state.parallel[1];
    EXPECT_EQ(samples.count, 1u);
    EXPECT_EQ(samples.worst[0].tick, 1234u);
    EXPECT_EQ(samples.worst[0].worker.completed, 1u);
    EXPECT_EQ(samples.worst[0].worker.retired, 0u);
    EXPECT_EQ(samples.worst[0].worker.cycleSamples, 0u);
    EXPECT_FALSE(samples.worst[0].cyclesAvailable);
    EXPECT_EQ(Attribution::state.phase, Attribution::Phase::count);
    EXPECT_EQ(Attribution::state.phases[static_cast<size_t>(Attribution::Phase::vehicles)].count, 1u);
    Attribution::Begin(false, {}, nullptr);
    pool.ParallelFor(8, [&](size_t) { ++visits; });
    EXPECT_EQ(visits.load(), 10u);
    EXPECT_EQ(Attribution::state.parallel[1].count, 0u);
}

TEST(JobPoolTest, SimulationAttributionSeparatesNestedPhasesAndSkippedScope)
{
    namespace Attribution = OpenRCT2::SimulationAttribution;
    Attribution::Begin(true, Attribution::Clock::now(), nullptr);
    {
        Attribution::Sequence outer(Attribution::Phase::tickPrelude, 900);
        outer.Next(Attribution::Phase::count);
        {
            Attribution::Sequence logic(Attribution::Phase::logicPrelude, 900);
            logic.Next(Attribution::Phase::peeps);
            EXPECT_EQ(Attribution::state.tick, 900u);
        }
        EXPECT_EQ(Attribution::state.phase, Attribution::Phase::count);
        outer.Next(Attribution::Phase::tickTail);
    }
    for (auto phase : { Attribution::Phase::tickPrelude, Attribution::Phase::logicPrelude, Attribution::Phase::peeps,
                        Attribution::Phase::tickTail })
        EXPECT_EQ(Attribution::state.phases[static_cast<size_t>(phase)].count, 1u);
    EXPECT_EQ(Attribution::state.phase, Attribution::Phase::count);
    Attribution::Begin(false, {}, nullptr);
}
