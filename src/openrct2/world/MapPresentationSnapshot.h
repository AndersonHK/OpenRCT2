/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "MapLimits.h"
#include "tile_element/TileElement.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace OpenRCT2
{
    struct MapPresentationTileChange
    {
        uint32_t index{};
        std::vector<TileElement> elements;
    };

    struct MapPresentationChangeBatch
    {
        uint64_t epoch{};
        uint32_t tick{};
        bool reset{};
        std::vector<MapPresentationTileChange> changes;
    };

    /** Owned tile storage and O(1) tile lookup for one presentation frame. */
    class MapPresentationSnapshot
    {
    private:
        static constexpr size_t kChunkWidth = 256;
        static constexpr size_t kTileCount = kMaximumMapSizeTechnical * kMaximumMapSizeTechnical;
        static constexpr size_t kChunkCount = (kTileCount + kChunkWidth - 1) / kChunkWidth;
        using Chunk = std::array<std::vector<TileElement>, kChunkWidth>;

        std::array<std::shared_ptr<const Chunk>, kChunkCount> _chunks;
        uint64_t _epoch{};
        uint32_t _tick{};

    public:
        void Apply(const MapPresentationChangeBatch& batch);
        [[nodiscard]] TileElement* GetFirstElementAt(const TileCoordsXY& tilePos) const;
        [[nodiscard]] uint32_t GetTick() const noexcept
        {
            return _tick;
        }
    };

    [[nodiscard]] MapPresentationChangeBatch ConsumeMapPresentationChanges(uint32_t tick);

    /** Installs a snapshot only for map reads made by the current paint worker. */
    class ScopedMapPresentationSnapshot
    {
    private:
        const MapPresentationSnapshot* _previous{};

    public:
        explicit ScopedMapPresentationSnapshot(const MapPresentationSnapshot* snapshot) noexcept;
        ~ScopedMapPresentationSnapshot();

        ScopedMapPresentationSnapshot(const ScopedMapPresentationSnapshot&) = delete;
        ScopedMapPresentationSnapshot& operator=(const ScopedMapPresentationSnapshot&) = delete;
    };
} // namespace OpenRCT2
