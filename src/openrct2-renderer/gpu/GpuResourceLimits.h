// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <cstdint>

namespace OpenRCT2::Ui::Gpu
{
    // Bootstrap batches resident world art and state; ordinary frames reuse residency.
    inline constexpr uint64_t kDefaultUploadRingBytes = 128 * 1024 * 1024;
}
