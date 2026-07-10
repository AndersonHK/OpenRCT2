/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../Identifiers.h"
#include "Location.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace OpenRCT2::MapPathTopology
{
    enum class ConnectionFlag : uint8_t
    {
        connected = 1 << 0,
        ambiguousTarget = 1 << 1,
        targetWide = 1 << 2,
        targetRideQueue = 1 << 3,
    };

    struct PathConnection
    {
        uint8_t targetBaseZ{};
        uint8_t flags{};

        [[nodiscard]] bool IsConnected() const noexcept;
        [[nodiscard]] bool HasAmbiguousTarget() const noexcept;
        [[nodiscard]] bool HasFlag(ConnectionFlag flag) const noexcept;
    };

    enum class PathNodeFlag : uint8_t
    {
        sloped = 1 << 0,
        queue = 1 << 1,
        wide = 1 << 2,
        queueBanner = 1 << 3,
        banner = 1 << 4,
        thinJunction = 1 << 5,
    };

    struct PathNode
    {
        std::array<PathConnection, kNumOrthogonalDirections> connections{};
        RideId queueRide{ RideId::GetNull() };
        uint8_t localX{};
        uint8_t localY{};
        uint8_t baseZ{};
        uint8_t edges{};
        uint8_t permittedEdges{};
        uint8_t slopeDirection{};
        uint8_t queueBannerDirection{};
        uint8_t flags{};
        StationIndex queueStation{ StationIndex::GetNull() };

        [[nodiscard]] bool HasFlag(PathNodeFlag flag) const noexcept;
        [[nodiscard]] TileCoordsXYZ GetLocation(const TileCoordsXY& chunkOrigin) const noexcept;
    };

    struct EntranceNode
    {
        std::array<PathConnection, kNumOrthogonalDirections> connections{};
        RideId ride{ RideId::GetNull() };
        uint8_t localX{};
        uint8_t localY{};
        uint8_t baseZ{};
        uint8_t direction{};
        uint8_t entranceType{};
        uint8_t sequence{};
        uint8_t connectionEdges{};
        StationIndex station{ StationIndex::GetNull() };

        [[nodiscard]] TileCoordsXYZ GetLocation(const TileCoordsXY& chunkOrigin) const noexcept;
    };

    static_assert(sizeof(PathNode) <= 24);
    static_assert(sizeof(EntranceNode) <= 24);

    struct ChunkView
    {
        TileCoordsXY origin{};
        std::span<const PathNode> paths{};
        std::span<const EntranceNode> entrances{};
        std::span<const uint32_t> pathTileOffsets{};
        uint64_t buildSerial{};
        bool isExact{};

        [[nodiscard]] explicit operator bool() const noexcept;
    };

    // Lazily rebuilds the requested chunk if its own or a cardinal neighbour's topology generation changed.
    // Warm hits perform no allocation. Returned spans remain valid until that chunk is rebuilt or Reset() is called.
    [[nodiscard]] ChunkView GetChunk(const TileCoordsXY& tile);
    [[nodiscard]] ChunkView GetChunk(const CoordsXY& coords);

    [[nodiscard]] const PathNode* FindPath(const ChunkView& view, const TileCoordsXYZ& location) noexcept;
    [[nodiscard]] const EntranceNode* FindEntrance(
        const ChunkView& view, const TileCoordsXYZ& location, uint8_t entranceType) noexcept;

    // Releases all warmed chunk storage. MapTopology::Reset invokes this for map replacement, load, resize, and stash swaps.
    void Reset() noexcept;
} // namespace OpenRCT2::MapPathTopology
