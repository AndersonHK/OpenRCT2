/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "Location.hpp"
#include "MapLimits.h"

#include <cstdint>

namespace OpenRCT2::MapTopology
{
    using Generation = uint64_t;

    // Keeps the generation table near 32 KiB while local edits touch few cache lines.
    constexpr int32_t kChunkSize = 16;
    constexpr int32_t kChunkCount = (kMaximumMapSizeTechnical + kChunkSize - 1) / kChunkSize;

    [[nodiscard]] Generation GetEpoch() noexcept;
    // Excludes dynamic wide-path changes that do not affect connectivity.
    [[nodiscard]] Generation GetPathConnectivityEpoch() noexcept;
    [[nodiscard]] Generation GetChunkGeneration(const TileCoordsXY& tile) noexcept;
    [[nodiscard]] Generation GetChunkGeneration(const CoordsXY& coords) noexcept;

    // Resets every chunk to one new generation. Use for map replacement, load, resize, shift, or stash swaps.
    void Reset() noexcept;
    // Invalidates the edited tile's chunk and any cardinally adjacent chunk whose connectivity can cross a chunk boundary.
    void InvalidateTileAndNeighbours(const TileCoordsXY& tile) noexcept;
    void InvalidateTileAndNeighbours(const CoordsXY& coords) noexcept;
    // Wide flags affect thin-junction classification, but not path connectivity. Keep the local topology chunks current
    // without invalidating connectivity-only consumers such as shared reverse route fields.
    void InvalidatePathWideTileAndNeighbours(const CoordsXY& coords) noexcept;
} // namespace OpenRCT2::MapTopology
