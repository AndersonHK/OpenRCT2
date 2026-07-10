/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "MapPathTopology.h"

#include "../GameState.h"
#include "Footpath.h"
#include "Map.h"
#include "MapPathRouteCache.h"
#include "MapTopology.h"
#include "tile_element/BannerElement.h"
#include "tile_element/EntranceElement.h"
#include "tile_element/PathElement.h"
#include "tile_element/TileElement.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace OpenRCT2::MapPathTopology
{
    namespace
    {
        constexpr std::size_t kTotalChunks = static_cast<std::size_t>(MapTopology::kChunkCount) * MapTopology::kChunkCount;
        constexpr std::size_t kDependencyCount = 5;

        struct CachedChunk
        {
            std::vector<PathNode> paths;
            std::vector<EntranceNode> entrances;
            std::vector<uint32_t> pathTileOffsets;
            std::array<MapTopology::Generation, kDependencyCount> dependencies{};
            uint64_t buildSerial{};
            bool isExact{};
            bool isInitialised{};
        };

        std::array<CachedChunk, kTotalChunks> _chunks;
        uint64_t _nextBuildSerial{};

        [[nodiscard]] bool IsValidTile(const TileCoordsXY& tile) noexcept
        {
            return tile.x >= 0 && tile.y >= 0 && tile.x < kMaximumMapSizeTechnical && tile.y < kMaximumMapSizeTechnical;
        }

        [[nodiscard]] bool IsValidChunk(int32_t chunkX, int32_t chunkY) noexcept
        {
            return chunkX >= 0 && chunkY >= 0 && chunkX < MapTopology::kChunkCount && chunkY < MapTopology::kChunkCount;
        }

        [[nodiscard]] std::size_t GetChunkIndex(int32_t chunkX, int32_t chunkY) noexcept
        {
            return static_cast<std::size_t>(chunkY) * MapTopology::kChunkCount + chunkX;
        }

        [[nodiscard]] MapTopology::Generation GetChunkGeneration(int32_t chunkX, int32_t chunkY) noexcept
        {
            if (!IsValidChunk(chunkX, chunkY))
                return 0;

            return MapTopology::GetChunkGeneration(
                TileCoordsXY{ chunkX * MapTopology::kChunkSize, chunkY * MapTopology::kChunkSize });
        }

        [[nodiscard]] std::array<MapTopology::Generation, kDependencyCount> GetDependencies(
            int32_t chunkX, int32_t chunkY) noexcept
        {
            return {
                GetChunkGeneration(chunkX, chunkY),     GetChunkGeneration(chunkX - 1, chunkY),
                GetChunkGeneration(chunkX, chunkY - 1), GetChunkGeneration(chunkX + 1, chunkY),
                GetChunkGeneration(chunkX, chunkY + 1),
            };
        }

        [[nodiscard]] PathConnection FindAdjacentPath(
            const TileCoordsXY& sourceTile, uint8_t sourceBaseZ, bool isSloped, Direction slopeDirection, Direction direction,
            bool& isExact)
        {
            auto currentZ = static_cast<int32_t>(sourceBaseZ);
            if (isSloped && slopeDirection == direction)
            {
                currentZ += 2;
            }

            const auto targetTile = sourceTile + TileDirectionDelta[direction];
            if (!IsValidTile(targetTile))
                return {};

            const auto* tileElement = MapGetFirstElementAt(targetTile);
            if (tileElement == nullptr)
                return {};

            PathConnection result{};
            do
            {
                if (tileElement->isGhost() || tileElement->getType() != TileElementType::Path)
                    continue;

                const auto* path = tileElement->asPath();
                if (!FootpathIsZAndDirectionValid(*path, currentZ, direction))
                    continue;

                if (!result.IsConnected())
                {
                    result.targetBaseZ = tileElement->baseHeight;
                    result.flags = static_cast<uint8_t>(ConnectionFlag::connected);
                    if (path->IsWide())
                        result.flags |= static_cast<uint8_t>(ConnectionFlag::targetWide);
                    if (path->IsQueue() && !path->GetRideIndex().IsNull())
                        result.flags |= static_cast<uint8_t>(ConnectionFlag::targetRideQueue);
                }
                else if (result.targetBaseZ == tileElement->baseHeight)
                {
                    result.flags |= static_cast<uint8_t>(ConnectionFlag::ambiguousTarget);
                    isExact = false;
                }
            } while (!(tileElement++)->isLastForTile());

            return result;
        }

        [[nodiscard]] uint8_t GetPermittedEdges(const PathElement& path, bool& hasBanner)
        {
            auto edges = path.GetEdges();
            const auto* tileElement = reinterpret_cast<const TileElement*>(&path);
            if (tileElement->isLastForTile())
                return edges;

            do
            {
                tileElement++;
                if (tileElement->getType() == TileElementType::Path)
                {
                    if (!tileElement->isGhost())
                        break;
                    continue;
                }
                if (tileElement->getType() != TileElementType::Banner)
                    continue;

                // Stable topology behaves as if construction previews are absent. Existing live pathfinding has a ghost-banner
                // quirk, documented as outside this cache's scope.
                if (tileElement->isGhost())
                    continue;

                hasBanner = true;
                edges &= tileElement->asBanner()->GetAllowedEdges();
            } while (!tileElement->isLastForTile());

            return edges;
        }

        [[nodiscard]] uint8_t GetAbsoluteEntranceEdges(const EntranceElement& entrance, bool& isExact)
        {
            const auto entranceType = entrance.GetEntranceType();
            const auto sequence = entrance.GetSequenceIndex();
            if (entranceType > ENTRANCE_TYPE_PARK_ENTRANCE || sequence >= 8)
            {
                isExact = false;
                return 0;
            }

            uint8_t result = 0;
            const auto relativeEdges = static_cast<uint8_t>(entrance.GetDirections());
            for (Direction relativeDirection : kAllDirections)
            {
                if (relativeEdges & (1 << relativeDirection))
                {
                    const auto absoluteDirection = static_cast<Direction>(
                        (entrance.getDirection() + relativeDirection) & kTileElementDirectionMask);
                    result |= 1 << absoluteDirection;
                }
            }
            return result;
        }

        void BuildChunk(CachedChunk& cache, int32_t chunkX, int32_t chunkY)
        {
            cache.paths.clear();
            cache.entrances.clear();
            cache.isExact = true;

            const auto origin = TileCoordsXY{ chunkX * MapTopology::kChunkSize, chunkY * MapTopology::kChunkSize };
            const auto& mapSize = getGameState().mapSize;
            const auto endX = std::min(origin.x + MapTopology::kChunkSize, mapSize.x);
            const auto endY = std::min(origin.y + MapTopology::kChunkSize, mapSize.y);

            if (origin.x < mapSize.x && origin.y < mapSize.y)
            {
                for (int32_t y = origin.y; y < endY; y++)
                {
                    for (int32_t x = origin.x; x < endX; x++)
                    {
                        const auto tile = TileCoordsXY{ x, y };
                        const auto* tileElement = MapGetFirstElementAt(tile);
                        if (tileElement == nullptr)
                            continue;

                        const auto firstPathOnTile = cache.paths.size();
                        do
                        {
                            if (tileElement->isGhost())
                                continue;

                            if (tileElement->getType() == TileElementType::Path)
                            {
                                const auto* path = tileElement->asPath();
                                const auto duplicate = std::find_if(
                                    cache.paths.begin() + firstPathOnTile, cache.paths.end(),
                                    [&](const PathNode& existing) { return existing.baseZ == tileElement->baseHeight; });
                                if (duplicate != cache.paths.end())
                                {
                                    cache.isExact = false;
                                }

                                bool hasBanner = false;
                                auto node = PathNode{};
                                node.localX = static_cast<uint8_t>(x - origin.x);
                                node.localY = static_cast<uint8_t>(y - origin.y);
                                node.baseZ = tileElement->baseHeight;
                                node.edges = path->GetEdges();
                                node.permittedEdges = GetPermittedEdges(*path, hasBanner);
                                if (path->IsSloped())
                                {
                                    node.flags |= static_cast<uint8_t>(PathNodeFlag::sloped);
                                    node.slopeDirection = path->GetSlopeDirection();
                                }
                                if (path->IsQueue())
                                {
                                    node.flags |= static_cast<uint8_t>(PathNodeFlag::queue);
                                    node.queueRide = path->GetRideIndex();
                                    node.queueStation = path->GetStationIndex();
                                }
                                if (path->IsWide())
                                    node.flags |= static_cast<uint8_t>(PathNodeFlag::wide);
                                if (path->HasQueueBanner())
                                {
                                    node.flags |= static_cast<uint8_t>(PathNodeFlag::queueBanner);
                                    node.queueBannerDirection = path->GetQueueBannerDirection();
                                }
                                if (hasBanner)
                                    node.flags |= static_cast<uint8_t>(PathNodeFlag::banner);

                                constexpr auto excludedThinNeighbourFlags = static_cast<uint8_t>(
                                    ConnectionFlag::targetWide) | static_cast<uint8_t>(ConnectionFlag::targetRideQueue);
                                uint8_t thinNeighbourCount = 0;
                                for (Direction direction : kAllDirections)
                                {
                                    if (node.edges & (1 << direction))
                                    {
                                        node.connections[direction] = FindAdjacentPath(
                                            tile, node.baseZ, path->IsSloped(), path->GetSlopeDirection(), direction,
                                            cache.isExact);
                                        const auto connectionFlags = node.connections[direction].flags;
                                        if (node.connections[direction].IsConnected()
                                            && (connectionFlags & excludedThinNeighbourFlags) == 0)
                                        {
                                            thinNeighbourCount++;
                                        }
                                    }
                                }
                                if (thinNeighbourCount > 2)
                                    node.flags |= static_cast<uint8_t>(PathNodeFlag::thinJunction);
                                cache.paths.push_back(node);
                            }
                            else if (tileElement->getType() == TileElementType::Entrance)
                            {
                                const auto* entrance = tileElement->asEntrance();
                                auto node = EntranceNode{};
                                node.localX = static_cast<uint8_t>(x - origin.x);
                                node.localY = static_cast<uint8_t>(y - origin.y);
                                node.baseZ = tileElement->baseHeight;
                                node.direction = entrance->getDirection();
                                node.entranceType = entrance->GetEntranceType();
                                node.sequence = entrance->GetSequenceIndex();
                                node.ride = entrance->GetRideIndex();
                                node.station = entrance->GetStationIndex();
                                node.connectionEdges = GetAbsoluteEntranceEdges(*entrance, cache.isExact);
                                for (Direction direction : kAllDirections)
                                {
                                    if (node.connectionEdges & (1 << direction))
                                    {
                                        node.connections[direction] = FindAdjacentPath(
                                            tile, node.baseZ, false, 0, direction, cache.isExact);
                                    }
                                }
                                cache.entrances.push_back(node);
                            }
                        } while (!(tileElement++)->isLastForTile());
                    }
                }
            }

            cache.dependencies = GetDependencies(chunkX, chunkY);
            cache.pathTileOffsets.resize((MapTopology::kChunkSize * MapTopology::kChunkSize) + 1);
            std::size_t pathIndex = 0;
            for (std::size_t tileIndex = 0; tileIndex < cache.pathTileOffsets.size(); tileIndex++)
            {
                while (pathIndex < cache.paths.size())
                {
                    const auto& path = cache.paths[pathIndex];
                    const auto pathTileIndex = static_cast<std::size_t>(path.localY) * MapTopology::kChunkSize + path.localX;
                    if (pathTileIndex >= tileIndex)
                        break;
                    pathIndex++;
                }
                cache.pathTileOffsets[tileIndex] = static_cast<uint32_t>(pathIndex);
            }
            cache.buildSerial = ++_nextBuildSerial;
            cache.isInitialised = true;
        }

        [[nodiscard]] ChunkView MakeView(const CachedChunk& cache, int32_t chunkX, int32_t chunkY) noexcept
        {
            return {
                TileCoordsXY{ chunkX * MapTopology::kChunkSize, chunkY * MapTopology::kChunkSize },
                std::span<const PathNode>{ cache.paths },
                std::span<const EntranceNode>{ cache.entrances },
                std::span<const uint32_t>{ cache.pathTileOffsets },
                cache.buildSerial,
                cache.isExact,
            };
        }
    } // namespace

    bool PathConnection::IsConnected() const noexcept
    {
        return HasFlag(ConnectionFlag::connected);
    }

    bool PathConnection::HasAmbiguousTarget() const noexcept
    {
        return HasFlag(ConnectionFlag::ambiguousTarget);
    }

    bool PathConnection::HasFlag(ConnectionFlag flag) const noexcept
    {
        return (flags & static_cast<uint8_t>(flag)) != 0;
    }

    bool PathNode::HasFlag(PathNodeFlag flag) const noexcept
    {
        return (flags & static_cast<uint8_t>(flag)) != 0;
    }

    TileCoordsXYZ PathNode::GetLocation(const TileCoordsXY& chunkOrigin) const noexcept
    {
        return { chunkOrigin.x + localX, chunkOrigin.y + localY, baseZ };
    }

    TileCoordsXYZ EntranceNode::GetLocation(const TileCoordsXY& chunkOrigin) const noexcept
    {
        return { chunkOrigin.x + localX, chunkOrigin.y + localY, baseZ };
    }

    ChunkView::operator bool() const noexcept
    {
        return buildSerial != 0;
    }

    ChunkView GetChunk(const TileCoordsXY& tile)
    {
        if (!IsValidTile(tile))
            return {};

        const auto chunkX = tile.x / MapTopology::kChunkSize;
        const auto chunkY = tile.y / MapTopology::kChunkSize;
        auto& cache = _chunks[GetChunkIndex(chunkX, chunkY)];
        const auto dependencies = GetDependencies(chunkX, chunkY);
        if (!cache.isInitialised || cache.dependencies != dependencies)
        {
            BuildChunk(cache, chunkX, chunkY);
        }
        return MakeView(cache, chunkX, chunkY);
    }

    ChunkView GetChunk(const CoordsXY& coords)
    {
        if (coords.x < 0 || coords.y < 0 || coords.x >= kMaximumMapSizeBig || coords.y >= kMaximumMapSizeBig)
            return {};
        return GetChunk(TileCoordsXY{ coords });
    }

    const PathNode* FindPath(const ChunkView& view, const TileCoordsXYZ& location) noexcept
    {
        if (location.x < view.origin.x || location.y < view.origin.y || location.x >= view.origin.x + MapTopology::kChunkSize
            || location.y >= view.origin.y + MapTopology::kChunkSize)
        {
            return nullptr;
        }

        const auto localX = static_cast<uint8_t>(location.x - view.origin.x);
        const auto localY = static_cast<uint8_t>(location.y - view.origin.y);
        const auto tileIndex = static_cast<std::size_t>(localY) * MapTopology::kChunkSize + localX;
        if (view.pathTileOffsets.size() <= tileIndex + 1)
            return nullptr;

        const auto first = view.pathTileOffsets[tileIndex];
        const auto last = view.pathTileOffsets[tileIndex + 1];
        for (auto index = first; index < last; index++)
        {
            const auto& path = view.paths[index];
            if (path.localX == location.x - view.origin.x && path.localY == location.y - view.origin.y
                && path.baseZ == location.z)
            {
                return &path;
            }
        }
        return nullptr;
    }

    const EntranceNode* FindEntrance(const ChunkView& view, const TileCoordsXYZ& location, uint8_t entranceType) noexcept
    {
        if (location.x < view.origin.x || location.y < view.origin.y || location.x >= view.origin.x + MapTopology::kChunkSize
            || location.y >= view.origin.y + MapTopology::kChunkSize)
        {
            return nullptr;
        }

        for (const auto& entrance : view.entrances)
        {
            if (entrance.localX == location.x - view.origin.x && entrance.localY == location.y - view.origin.y
                && entrance.baseZ == location.z && entrance.entranceType == entranceType)
            {
                return &entrance;
            }
        }
        return nullptr;
    }

    void Reset() noexcept
    {
        MapPathRouteCache::Reset();
        for (auto& chunk : _chunks)
        {
            std::vector<PathNode>().swap(chunk.paths);
            std::vector<EntranceNode>().swap(chunk.entrances);
            std::vector<uint32_t>().swap(chunk.pathTileOffsets);
            chunk.dependencies = {};
            chunk.buildSerial = 0;
            chunk.isExact = false;
            chunk.isInitialised = false;
        }
    }
} // namespace OpenRCT2::MapPathTopology
