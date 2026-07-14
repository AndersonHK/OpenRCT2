/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "MapLimits.h"
#include "../drawing/ImageId.hpp"
#include "tile_element/TileElement.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

namespace OpenRCT2
{
    /** Pointer-free, object-resolved base terrain data captured at the map publication boundary. */
    struct SurfacePresentationRecord
    {
        static constexpr size_t kRotationCount = 4;

        uint16_t baseZ{};
        uint8_t valid{};
        uint8_t adapterRequired{};
        std::array<ImageId, kRotationCount> detailedImages{};
        std::array<ImageId, kRotationCount> distantImages{};
    };

    struct MapPresentationTileChange
    {
        uint32_t index{};
        std::vector<TileElement> elements;
        SurfacePresentationRecord surface;
        uint32_t surfaceIndex{ std::numeric_limits<uint32_t>::max() };
    };

    struct MapPresentationChangeBatch
    {
        uint64_t epoch{};
        uint32_t tick{};
        bool reset{};
        uint32_t surfaceWidth{};
        uint32_t surfaceHeight{};
        std::vector<MapPresentationTileChange> changes;
    };

    /** Owned tile storage and O(1) tile lookup for one presentation frame. */
    class MapPresentationSnapshot
    {
    public:
        static constexpr size_t kChunkWidth = 256;
        static constexpr size_t kTileCount = kMaximumMapSizeTechnical * kMaximumMapSizeTechnical;
        static constexpr size_t kChunkCount = (kTileCount + kChunkWidth - 1) / kChunkWidth;

        struct SurfaceChunk
        {
            uint64_t revision{};
            std::array<SurfacePresentationRecord, kChunkWidth> records{};
        };

        using SurfaceChunks = std::vector<std::shared_ptr<const SurfaceChunk>>;

    private:
        using Chunk = std::array<std::vector<TileElement>, kChunkWidth>;

        std::array<std::shared_ptr<const Chunk>, kChunkCount> _chunks;
        SurfaceChunks _surfaceChunks;
        uint64_t _epoch{};
        uint64_t _nextSurfaceRevision{};
        uint32_t _tick{};
        uint32_t _surfaceWidth{};
        uint32_t _surfaceHeight{};
        uint16_t _surfaceBaselineZ{};
        bool _surfaceBaselineSet{};
        bool _requiresLegacyPainterInterleave{};

    public:
        void Apply(const MapPresentationChangeBatch& batch);
        [[nodiscard]] TileElement* GetFirstElementAt(const TileCoordsXY& tilePos) const;
        [[nodiscard]] uint32_t GetTick() const noexcept
        {
            return _tick;
        }
        [[nodiscard]] uint64_t GetEpoch() const noexcept
        {
            return _epoch;
        }
        [[nodiscard]] const SurfaceChunks& GetSurfaceChunks() const noexcept
        {
            return _surfaceChunks;
        }
        [[nodiscard]] uint32_t GetSurfaceWidth() const noexcept
        {
            return _surfaceWidth;
        }
        [[nodiscard]] uint32_t GetSurfaceHeight() const noexcept
        {
            return _surfaceHeight;
        }
        [[nodiscard]] uint32_t GetSurfaceRecordCount() const noexcept
        {
            return _surfaceWidth * _surfaceHeight;
        }
        [[nodiscard]] bool RequiresLegacyPainterInterleave() const noexcept
        {
            return _requiresLegacyPainterInterleave;
        }
    };

    static_assert(std::is_trivially_copyable_v<SurfacePresentationRecord>);

    [[nodiscard]] MapPresentationChangeBatch ConsumeMapPresentationChanges(uint32_t tick);
    [[nodiscard]] uint64_t GetMapPresentationEpoch() noexcept;

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
