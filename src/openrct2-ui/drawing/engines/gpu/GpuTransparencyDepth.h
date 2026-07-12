/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuCommandStream.h"

#include <cstdint>

namespace OpenRCT2::Ui::Gpu
{
    /** Exact maximum overlap of clipped, half-open transparent rectangles. */
    [[nodiscard]] uint32_t MaxTransparencyDepth(const CommandBatch<RectCommand>& commands);
} // namespace OpenRCT2::Ui::Gpu
