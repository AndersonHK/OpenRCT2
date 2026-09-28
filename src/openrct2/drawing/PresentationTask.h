/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once

#include "../core/JobPool.h"

#include <optional>
#include <utility>

namespace OpenRCT2::Detail
{
    // Wait still reports the worker error, but that completed task must not poison
    // the publisher's next reset/retry. The task owns its captured snapshot until completion.
    inline void WaitAndReleasePresentationTask(JobPool& jobs, std::optional<JobPool::TaskGroup>& pending)
    {
        auto task = std::exchange(pending, std::nullopt);
        if (task.has_value())
            jobs.Wait(*task);
    }
} // namespace OpenRCT2::Detail
