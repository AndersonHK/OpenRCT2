// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once

#include <openrct2/drawing/RetainedPeepState.h>
#include <initializer_list>

namespace OpenRCT2::Drawing::Test
{
    // Diagnostic/test adapter only. Production publishers construct notified groups directly.
    inline void AppendFullPeep(RetainedPeepBatch& batch, const RetainedPeepRecord& record)
    {
        const auto f = SplitRetainedPeepRecord(record);
        batch.lifecycle.push_back({ f.id, f.lifecycle });
        if (f.lifecycle.flags & kRetainedPeepPresent)
        {
            batch.motion.push_back({ f.id, f.lifecycle.generation, f.motion });
            batch.appearance.push_back({ f.id, f.lifecycle.generation, f.appearance });
            batch.animation.push_back({ f.id, f.lifecycle.generation, f.animation });
        }
    }

    inline RetainedPeepBatch FullPeepBatch(uint64_t epoch, bool reset, std::initializer_list<RetainedPeepRecord> records)
    {
        RetainedPeepBatch batch{ .epoch = epoch, .reset = reset };
        for (const auto& record : records) AppendFullPeep(batch, record);
        return batch;
    }
}
