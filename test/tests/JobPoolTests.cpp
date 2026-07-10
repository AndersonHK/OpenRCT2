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
#include <gtest/gtest.h>
#include <openrct2/core/JobPool.h>
#include <stdexcept>
#include <vector>

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
