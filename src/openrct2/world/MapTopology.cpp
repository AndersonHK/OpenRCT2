/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "MapTopology.h"

#include "MapPathTopology.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace OpenRCT2::MapTopology
{
    namespace
    {
        constexpr std::size_t kTotalChunks = static_cast<std::size_t>(kChunkCount) * kChunkCount;

        struct TopologyGenerationState
        {
            std::atomic<Generation> epoch{ 1 };
            std::array<std::atomic<Generation>, kTotalChunks> chunks{};

            TopologyGenerationState() noexcept
            {
                for (auto& chunk : chunks)
                {
                    chunk.store(1, std::memory_order_relaxed);
                }
            }
        };

        TopologyGenerationState _state;

        [[nodiscard]] bool IsValidTile(const TileCoordsXY& tile) noexcept
        {
            return tile.x >= 0 && tile.y >= 0 && tile.x < kMaximumMapSizeTechnical && tile.y < kMaximumMapSizeTechnical;
        }

        [[nodiscard]] bool IsValidCoords(const CoordsXY& coords) noexcept
        {
            return coords.x >= 0 && coords.y >= 0 && coords.x < kMaximumMapSizeBig && coords.y < kMaximumMapSizeBig;
        }

        [[nodiscard]] std::size_t GetChunkIndex(int32_t chunkX, int32_t chunkY) noexcept
        {
            return static_cast<std::size_t>(chunkY) * kChunkCount + chunkX;
        }

        void StoreChunk(int32_t chunkX, int32_t chunkY, Generation generation) noexcept
        {
            if (chunkX < 0 || chunkY < 0 || chunkX >= kChunkCount || chunkY >= kChunkCount)
                return;
            _state.chunks[GetChunkIndex(chunkX, chunkY)].store(generation, std::memory_order_release);
        }

        [[nodiscard]] Generation NextGeneration() noexcept
        {
            auto generation = _state.epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (generation == 0)
            {
                // Practically unreachable, but never expose zero because it is reserved as an invalid generation.
                generation = 1;
                _state.epoch.store(generation, std::memory_order_release);
            }
            return generation;
        }
    } // namespace

    Generation GetEpoch() noexcept
    {
        return _state.epoch.load(std::memory_order_acquire);
    }

    Generation GetChunkGeneration(const TileCoordsXY& tile) noexcept
    {
        if (!IsValidTile(tile))
            return GetEpoch();

        const auto chunkX = tile.x / kChunkSize;
        const auto chunkY = tile.y / kChunkSize;
        return _state.chunks[GetChunkIndex(chunkX, chunkY)].load(std::memory_order_acquire);
    }

    Generation GetChunkGeneration(const CoordsXY& coords) noexcept
    {
        if (!IsValidCoords(coords))
            return GetEpoch();

        return GetChunkGeneration(TileCoordsXY(coords));
    }

    void Reset() noexcept
    {
        const auto generation = NextGeneration();
        for (auto& chunk : _state.chunks)
        {
            chunk.store(generation, std::memory_order_release);
        }
        MapPathTopology::Reset();
    }

    void InvalidateTileAndNeighbours(const TileCoordsXY& tile) noexcept
    {
        if (!IsValidTile(tile))
            return;

        const auto generation = NextGeneration();
        const auto chunkX = tile.x / kChunkSize;
        const auto chunkY = tile.y / kChunkSize;
        StoreChunk(chunkX, chunkY, generation);

        const auto localX = tile.x % kChunkSize;
        const auto localY = tile.y % kChunkSize;
        if (localX == 0)
            StoreChunk(chunkX - 1, chunkY, generation);
        if (localX == kChunkSize - 1)
            StoreChunk(chunkX + 1, chunkY, generation);
        if (localY == 0)
            StoreChunk(chunkX, chunkY - 1, generation);
        if (localY == kChunkSize - 1)
            StoreChunk(chunkX, chunkY + 1, generation);
    }

    void InvalidateTileAndNeighbours(const CoordsXY& coords) noexcept
    {
        if (!IsValidCoords(coords))
            return;

        InvalidateTileAndNeighbours(TileCoordsXY(coords));
    }
} // namespace OpenRCT2::MapTopology
