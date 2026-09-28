/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

namespace OpenRCT2::Ui::Gpu::Terrain
{
    // Single implementation for device compute and host differential tests.
    // Does not enable native terrain or generate CPU frame commands.
#include "../../../data/shaders/vulkan/terrain_surface_rules.glsl"
    static_assert(sizeof(int) == 4);
    static_assert(sizeof(TerrainEdgePlan) == 32);
    static_assert(sizeof(TerrainEdgeEmission) == 32);
}
