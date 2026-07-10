/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../gpu/GpuCommandStream.h"

namespace OpenRCT2::Ui
{
    // Compatibility aliases while OpenGL and Vulkan converge on the shared
    // backend-neutral command stream.
    using DrawLineCommand = Gpu::LineCommand;
    using DrawRectCommand = Gpu::RectCommand;
    using DrawWeatherCommand = Gpu::WeatherCommand;

    using LineCommandBatch = Gpu::CommandBatch<DrawLineCommand>;
    using RectCommandBatch = Gpu::CommandBatch<DrawRectCommand>;
    using WeatherCommandBatch = Gpu::CommandBatch<DrawWeatherCommand>;
} // namespace OpenRCT2::Ui
