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
        targetWide = 1 << 2,
        targetRideQueue = 1 << 3,
    };

    struct PathConnection
    {
        uint8_t targetBaseZ{};
        uint8_t flags{};

        [[nodiscard]] bool HasFlag(ConnectionFlag flag) const noexcept
        {
            return (flags & static_cast<uint8_t>(flag)) != 0;
        }
        [[nodiscard]] bool IsConnected() const noexcept { return HasFlag(ConnectionFlag::connected); }
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
        Direction slopeDirection{ kInvalidDirection };
        bool isThinJunction{};
        [[nodiscard]] TileCoordsXYZ GetLocation(const TileCoordsXY& origin) const noexcept
        {
            return { origin.x + localX, origin.y + localY, baseZ };
        }
    };

    struct EntranceNode
    {
        std::array<PathConnection, kNumOrthogonalDirections> connections{};
        RideId ride{ RideId::GetNull() };
        uint8_t localX{};
        uint8_t localY{};
        uint8_t baseZ{};
        uint8_t entranceType{};

        [[nodiscard]] TileCoordsXYZ GetLocation(const TileCoordsXY& origin) const noexcept
        {
            return { origin.x + localX, origin.y + localY, baseZ };
        }
    };

    // These arrays scale with every warmed path tile; keep diagnostic metadata out of the routing snapshot.
    static_assert(sizeof(PathNode) <= 20);
    static_assert(sizeof(EntranceNode) <= 16);

    struct ChunkView
    {
        TileCoordsXY origin{};
        std::span<const PathNode> paths{};
        std::span<const EntranceNode> entrances{};
        std::span<const uint32_t> pathTileOffsets{};
        uint64_t buildSerial{};
        bool isExact{};

        [[nodiscard]] explicit operator bool() const noexcept { return buildSerial != 0; }
    };

    // Lazily rebuilds the requested chunk if its own or a cardinal neighbour's topology generation changed.
    // Warm hits perform no allocation. Returned spans remain valid until that chunk is rebuilt or Reset() is called.
    [[nodiscard]] ChunkView GetChunk(const TileCoordsXY& tile);

    [[nodiscard]] const PathNode* FindPath(const ChunkView& view, const TileCoordsXYZ& location) noexcept;

    // Releases all warmed chunk storage. MapTopology::Reset invokes this for map replacement, load, resize, and stash swaps.
    void Reset() noexcept;
} // namespace OpenRCT2::MapPathTopology
