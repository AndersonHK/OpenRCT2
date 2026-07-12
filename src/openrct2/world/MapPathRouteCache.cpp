/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "MapPathRouteCache.h"

#include "../Context.h"
#include "../GameState.h"
#include "../core/JobPool.h"
#include "../profiling/Profiling.h"
#include "Map.h"
#include "MapPathTopology.h"
#include "MapTopology.h"
#include "tile_element/EntranceElement.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

namespace OpenRCT2::MapPathRouteCache
{
    namespace
    {
        using NodeIndex = uint32_t;
        using PackedIncomingEdge = uint32_t;
        constexpr auto kInvalidNodeIndex = std::numeric_limits<NodeIndex>::max();
        constexpr auto kUnreachableDistance = std::numeric_limits<uint32_t>::max();
        constexpr auto kIncomingEdgeDirectionBits = 2;

        static_assert(kNumOrthogonalDirections == (1 << kIncomingEdgeDirectionBits));
        static_assert(kMaxTileElements < (1u << (32 - kIncomingEdgeDirectionBits)));

        struct FrozenNode
        {
            TileCoordsXYZ location{};
            std::array<uint8_t, kNumOrthogonalDirections> targetBaseZ{};
            std::array<NodeIndex, kNumOrthogonalDirections> connections{ kInvalidNodeIndex, kInvalidNodeIndex,
                                                                         kInvalidNodeIndex, kInvalidNodeIndex };
            RideId queueRide{ RideId::GetNull() };
            uint8_t permittedEdges{};
            Direction slopeDirection{ kInvalidDirection };
        };

        struct FrozenEntrance
        {
            TileCoordsXYZ location{};
            std::array<uint8_t, kNumOrthogonalDirections> targetBaseZ{};
            std::array<NodeIndex, kNumOrthogonalDirections> connections{ kInvalidNodeIndex, kInvalidNodeIndex,
                                                                         kInvalidNodeIndex, kInvalidNodeIndex };
            RideId ride{ RideId::GetNull() };
            uint8_t entranceType{};
        };

        static_assert(sizeof(FrozenNode) <= 36);
        static_assert(sizeof(FrozenEntrance) <= 36);

        struct FrozenGraph
        {
            MapTopology::Generation epoch{};
            std::vector<FrozenNode> nodes;
            std::vector<FrozenEntrance> entrances;
            std::vector<std::pair<uint64_t, NodeIndex>> nodeIndex;
            std::vector<std::pair<uint64_t, NodeIndex>> entranceIndex;
            std::vector<uint32_t> incomingOffsets;
            std::vector<PackedIncomingEdge> incomingEdges;
        };

        struct RouteField
        {
            RouteTarget target{};
            std::vector<uint8_t> directions;
            std::vector<uint32_t> distances;
        };

        struct RideTargetFieldRef
        {
            RideId ride{ RideId::GetNull() };
            size_t fieldIndex{};
        };

        [[nodiscard]] uint64_t GetLocationKey(const TileCoordsXYZ& location) noexcept
        {
            return (static_cast<uint64_t>(static_cast<uint16_t>(location.z)) << 32)
                | (static_cast<uint64_t>(static_cast<uint16_t>(location.y)) << 16)
                | static_cast<uint16_t>(location.x);
        }

        [[nodiscard]] size_t HashLocationKey(uint64_t key) noexcept
        {
            key ^= key >> 30;
            key *= 0xBF58476D1CE4E5B9ULL;
            key ^= key >> 27;
            key *= 0x94D049BB133111EBULL;
            key ^= key >> 31;
            return static_cast<size_t>(key);
        }

        class PublishedNodeIndex
        {
        public:
            void Build(std::span<const std::pair<uint64_t, NodeIndex>> sortedNodes)
            {
                if (sortedNodes.empty())
                {
                    *this = {};
                    return;
                }

                const auto capacity = std::bit_ceil(sortedNodes.size() * 2);
                _entries.assign(capacity, Entry{});
                _count = sortedNodes.size();
                const auto mask = capacity - 1;
                for (const auto& [key, node] : sortedNodes)
                {
                    auto index = HashLocationKey(key) & mask;
                    while (_entries[index].node != kInvalidNodeIndex)
                        index = (index + 1) & mask;
                    _entries[index] = { key, node };
                }
            }

            [[nodiscard]] NodeIndex Find(const TileCoordsXYZ& location) const noexcept
            {
                if (_entries.empty())
                    return kInvalidNodeIndex;

                const auto key = GetLocationKey(location);
                const auto mask = _entries.size() - 1;
                auto index = HashLocationKey(key) & mask;
                while (_entries[index].node != kInvalidNodeIndex)
                {
                    if (_entries[index].key == key)
                        return _entries[index].node;
                    index = (index + 1) & mask;
                }
                return kInvalidNodeIndex;
            }

            [[nodiscard]] size_t size() const noexcept
            {
                return _count;
            }

        private:
            struct Entry
            {
                uint64_t key{};
                NodeIndex node{ kInvalidNodeIndex };
            };
            static_assert(sizeof(Entry) <= 16);

            std::vector<Entry> _entries;
            size_t _count{};
        };

        PublishedNodeIndex _nodeIndex;
        std::vector<RouteField> _fields;
        std::vector<RideTargetFieldRef> _rideTargetFields;
        std::vector<FrozenEntrance> _entrances;
        std::vector<std::pair<uint64_t, NodeIndex>> _entranceIndex;
        MapTopology::Generation _preparedEpoch{};

        [[nodiscard]] bool TargetLess(const RouteTarget& lhs, const RouteTarget& rhs) noexcept
        {
            return std::tuple{ lhs.location.x, lhs.location.y, lhs.location.z, lhs.queueRide.ToUnderlying(), lhs.kind }
                < std::tuple{ rhs.location.x, rhs.location.y, rhs.location.z, rhs.queueRide.ToUnderlying(), rhs.kind };
        }

        [[nodiscard]] const RouteField* FindField(const RouteTarget& target) noexcept
        {
            // Target kind controls seed construction, but published lookups intentionally key fields by location and
            // queue ownership so callers do not need to retain how that terminal was discovered.
            auto firstTargetKind = target;
            firstTargetKind.kind = RouteTargetKind::pathOrEntrance;
            const auto fieldIterator = std::lower_bound(
                _fields.begin(), _fields.end(), firstTargetKind,
                [](const RouteField& field, const RouteTarget& value) { return TargetLess(field.target, value); });
            return fieldIterator == _fields.end() || fieldIterator->target.location != target.location
                    || fieldIterator->target.queueRide != target.queueRide
                ? nullptr
                : &*fieldIterator;
        }

        [[nodiscard]] auto FindFirstRideTargetField(RideId ride) noexcept
        {
            return std::lower_bound(
                _rideTargetFields.begin(), _rideTargetFields.end(), ride,
                [](const RideTargetFieldRef& entry, RideId value) {
                    return entry.ride.ToUnderlying() < value.ToUnderlying();
                });
        }

        [[nodiscard]] std::optional<uint32_t> GetFieldDistance(const RouteField& field, NodeIndex sourceNode) noexcept
        {
            if (sourceNode == kInvalidNodeIndex || sourceNode >= field.distances.size())
                return std::nullopt;
            const auto distance = field.distances[sourceNode];
            return distance == kUnreachableDistance ? std::nullopt : std::optional<uint32_t>{ distance };
        }

        [[nodiscard]] NodeIndex FindNode(
            std::span<const std::pair<uint64_t, NodeIndex>> nodeIndex, const TileCoordsXYZ& location) noexcept
        {
            const auto key = GetLocationKey(location);
            const auto iterator = std::lower_bound(
                nodeIndex.begin(), nodeIndex.end(), key,
                [](const auto& entry, uint64_t value) { return entry.first < value; });
            return iterator == nodeIndex.end() || iterator->first != key ? kInvalidNodeIndex : iterator->second;
        }

        [[nodiscard]] bool NodeIsAllowed(const FrozenNode& node, RideId targetQueueRide) noexcept
        {
            return node.queueRide.IsNull() || node.queueRide == targetQueueRide;
        }

        [[nodiscard]] std::optional<FrozenGraph> FreezeGraph()
        {
            PROFILED_FUNCTION();

            FrozenGraph graph{};
            graph.epoch = MapTopology::GetPathConnectivityEpoch();

            const auto& mapSize = getGameState().mapSize;
            const auto chunkCountX = (mapSize.x + MapTopology::kChunkSize - 1) / MapTopology::kChunkSize;
            const auto chunkCountY = (mapSize.y + MapTopology::kChunkSize - 1) / MapTopology::kChunkSize;
            for (int32_t chunkY = 0; chunkY < chunkCountY; chunkY++)
            {
                for (int32_t chunkX = 0; chunkX < chunkCountX; chunkX++)
                {
                    const auto chunkOrigin = TileCoordsXY{ chunkX * MapTopology::kChunkSize,
                                                           chunkY * MapTopology::kChunkSize };
                    const auto view = MapPathTopology::GetChunk(chunkOrigin);
                    if (!view.isExact)
                        return std::nullopt;

                    for (const auto& path : view.paths)
                    {
                        auto node = FrozenNode{};
                        node.location = path.GetLocation(view.origin);
                        node.queueRide = path.queueRide;
                        node.permittedEdges = path.permittedEdges;
                        node.slopeDirection = path.slopeDirection;
                        for (Direction direction : kAllDirections)
                        {
                            const auto& connection = path.connections[direction];
                            if (!connection.IsConnected() || !(node.permittedEdges & (1 << direction)))
                                continue;
                            node.targetBaseZ[direction] = connection.targetBaseZ;
                            node.connections[direction] = 0;
                        }
                        graph.nodes.push_back(node);
                    }

                    for (const auto& entrance : view.entrances)
                    {
                        auto node = FrozenEntrance{};
                        node.location = entrance.GetLocation(view.origin);
                        node.ride = entrance.ride;
                        node.entranceType = entrance.entranceType;
                        for (Direction direction : kAllDirections)
                        {
                            const auto& connection = entrance.connections[direction];
                            if (!connection.IsConnected())
                                continue;
                            node.targetBaseZ[direction] = connection.targetBaseZ;
                            node.connections[direction] = 0;
                        }
                        graph.entrances.push_back(node);
                    }
                }
            }

            graph.nodeIndex.reserve(graph.nodes.size());
            for (NodeIndex index = 0; index < graph.nodes.size(); index++)
            {
                graph.nodeIndex.emplace_back(GetLocationKey(graph.nodes[index].location), index);
            }
            std::sort(graph.nodeIndex.begin(), graph.nodeIndex.end());

            graph.entranceIndex.reserve(graph.entrances.size());
            for (NodeIndex index = 0; index < graph.entrances.size(); index++)
            {
                graph.entranceIndex.emplace_back(GetLocationKey(graph.entrances[index].location), index);
            }
            std::sort(graph.entranceIndex.begin(), graph.entranceIndex.end());

            const auto resolveConnections = [&](auto& entry) {
                // Before publication, a non-sentinel connection is only a presence marker; every marker is replaced by
                // the immutable node index resolved from the sorted location table below.
                for (Direction direction : kAllDirections)
                {
                    if (entry.connections[direction] != kInvalidNodeIndex)
                    {
                        const auto targetTile = TileCoordsXY{ entry.location } + TileDirectionDelta[direction];
                        entry.connections[direction] = FindNode(
                            graph.nodeIndex, TileCoordsXYZ{ targetTile, entry.targetBaseZ[direction] });
                    }
                }
            };
            for (auto& node : graph.nodes)
                resolveConnections(node);
            for (auto& entrance : graph.entrances)
                resolveConnections(entrance);

            graph.incomingOffsets.assign(graph.nodes.size() + 1, 0);
            for (const auto& node : graph.nodes)
            {
                for (Direction direction : kAllDirections)
                {
                    const auto target = node.connections[direction];
                    if (target != kInvalidNodeIndex)
                        graph.incomingOffsets[target + 1]++;
                }
            }
            for (size_t index = 1; index < graph.incomingOffsets.size(); index++)
            {
                graph.incomingOffsets[index] += graph.incomingOffsets[index - 1];
            }

            graph.incomingEdges.resize(graph.incomingOffsets.back());
            auto cursors = graph.incomingOffsets;
            for (NodeIndex source = 0; source < graph.nodes.size(); source++)
            {
                for (Direction direction : kAllDirections)
                {
                    const auto target = graph.nodes[source].connections[direction];
                    if (target != kInvalidNodeIndex)
                    {
                        // The low bits carry the source-to-target direction, allowing reverse BFS to publish directions
                        // without rescanning every node's four outgoing edges after calculating distances.
                        graph.incomingEdges[cursors[target]++] = (source << kIncomingEdgeDirectionBits) | direction;
                    }
                }
            }
            return std::optional<FrozenGraph>{ std::move(graph) };
        }

        void AddTerminalSeed(
            NodeIndex pathIndex, Direction direction, std::vector<uint32_t>& distances,
            std::vector<uint8_t>& directions, std::vector<NodeIndex>& queue)
        {
            if (distances[pathIndex] == kUnreachableDistance)
            {
                distances[pathIndex] = 0;
                queue.push_back(pathIndex);
            }
            if (!DirectionValid(directions[pathIndex]) || direction < directions[pathIndex])
                directions[pathIndex] = direction;
        }

        void AddEntranceSeeds(
            const FrozenGraph& graph, const RouteTarget& target, std::vector<uint32_t>& distances,
            std::vector<uint8_t>& directions, std::vector<NodeIndex>& queue)
        {
            const auto locationKey = GetLocationKey(target.location);
            auto entranceIndex = std::lower_bound(
                graph.entranceIndex.begin(), graph.entranceIndex.end(), locationKey,
                [](const auto& entry, uint64_t value) { return entry.first < value; });
            for (; entranceIndex != graph.entranceIndex.end() && entranceIndex->first == locationKey; entranceIndex++)
            {
                const auto& entrance = graph.entrances[entranceIndex->second];
                if (target.queueRide.IsNull())
                {
                    if (entrance.entranceType != ENTRANCE_TYPE_PARK_ENTRANCE)
                        continue;
                }
                else if (entrance.entranceType != ENTRANCE_TYPE_RIDE_ENTRANCE || entrance.ride != target.queueRide)
                {
                    continue;
                }

                for (Direction entranceDirection : kAllDirections)
                {
                    const auto pathIndex = entrance.connections[entranceDirection];
                    if (pathIndex == kInvalidNodeIndex || !NodeIsAllowed(graph.nodes[pathIndex], target.queueRide))
                        continue;

                    const auto pathDirection = DirectionReverse(entranceDirection);
                    if (!(graph.nodes[pathIndex].permittedEdges & (1 << pathDirection)))
                        continue;
                    AddTerminalSeed(pathIndex, pathDirection, distances, directions, queue);
                }
            }
        }

        void AddShopFacilitySeeds(
            const FrozenGraph& graph, const RouteTarget& target, std::vector<uint32_t>& distances,
            std::vector<uint8_t>& directions, std::vector<NodeIndex>& queue)
        {
            const auto targetTile = TileCoordsXY{ target.location };
            for (Direction direction : kAllDirections)
            {
                const auto& delta = TileDirectionDelta[direction];
                const auto sourceTile = TileCoordsXY{ targetTile.x - delta.x, targetTile.y - delta.y };
                const std::array<int32_t, 2> candidateBaseZ{ target.location.z, target.location.z - 2 };
                for (const auto baseZ : candidateBaseZ)
                {
                    if (baseZ < 0)
                        continue;
                    const auto pathIndex = FindNode(graph.nodeIndex, TileCoordsXYZ{ sourceTile, baseZ });
                    if (pathIndex == kInvalidNodeIndex)
                        continue;

                    const auto& path = graph.nodes[pathIndex];
                    if (!NodeIsAllowed(path, target.queueRide) || !(path.permittedEdges & (1 << direction)))
                        continue;
                    auto targetZ = path.location.z;
                    if (path.slopeDirection == direction)
                        targetZ += 2;
                    if (targetZ != target.location.z)
                        continue;
                    AddTerminalSeed(pathIndex, direction, distances, directions, queue);
                }
            }
        }

        [[nodiscard]] RouteField BuildField(const FrozenGraph& graph, const RouteTarget& target)
        {
            PROFILED_FUNCTION();

            RouteField field{};
            field.target = target;
            field.directions.assign(graph.nodes.size(), kInvalidDirection);
            std::vector<uint32_t> distances(graph.nodes.size(), kUnreachableDistance);
            std::vector<NodeIndex> queue;
            queue.reserve(graph.nodes.size());

            if (target.kind == RouteTargetKind::pathOrEntrance)
            {
                const auto targetPath = FindNode(graph.nodeIndex, target.location);
                if (targetPath != kInvalidNodeIndex && NodeIsAllowed(graph.nodes[targetPath], target.queueRide))
                {
                    distances[targetPath] = 0;
                    queue.push_back(targetPath);
                }
                AddEntranceSeeds(graph, target, distances, field.directions, queue);
            }
            else
            {
                AddShopFacilitySeeds(graph, target, distances, field.directions, queue);
            }
            if (queue.empty())
            {
                std::vector<uint8_t>().swap(field.directions);
                return field;
            }

            for (size_t queueIndex = 0; queueIndex < queue.size(); queueIndex++)
            {
                const auto current = queue[queueIndex];
                const auto nextDistance = distances[current] + 1;
                for (auto edgeIndex = graph.incomingOffsets[current]; edgeIndex < graph.incomingOffsets[current + 1];
                     edgeIndex++)
                {
                    const auto incomingEdge = graph.incomingEdges[edgeIndex];
                    const auto source = incomingEdge >> kIncomingEdgeDirectionBits;
                    if (!NodeIsAllowed(graph.nodes[source], target.queueRide))
                    {
                        continue;
                    }
                    const auto direction = static_cast<Direction>(incomingEdge & (kNumOrthogonalDirections - 1));
                    if (distances[source] == kUnreachableDistance)
                    {
                        distances[source] = nextDistance;
                        field.directions[source] = direction;
                        queue.push_back(source);
                    }
                    else if (distances[source] == nextDistance
                        && (!DirectionValid(field.directions[source]) || direction < field.directions[source]))
                    {
                        // The old post-BFS scan visited directions from 0 to 3 and therefore chose the lowest direction
                        // among equal shortest paths. Preserve that deterministic tie break independent of queue order.
                        field.directions[source] = direction;
                    }
                }
            }
            field.distances = std::move(distances);
            return field;
        }

        [[nodiscard]] std::vector<RideTargetFieldRef> BuildRideTargetFieldRefs(const std::vector<RouteField>& fields)
        {
            std::vector<RideTargetFieldRef> result;
            result.reserve(fields.size());
            for (size_t fieldIndex = 0; fieldIndex < fields.size(); fieldIndex++)
            {
                if (!fields[fieldIndex].target.queueRide.IsNull())
                    result.push_back({ fields[fieldIndex].target.queueRide, fieldIndex });
            }
            std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.ride != rhs.ride)
                    return lhs.ride.ToUnderlying() < rhs.ride.ToUnderlying();
                return lhs.fieldIndex < rhs.fieldIndex;
            });
            return result;
        }
    } // namespace

    bool IsPreparedForCurrentTopology() noexcept
    {
        return _preparedEpoch != 0 && _preparedEpoch == MapTopology::GetPathConnectivityEpoch();
    }

    void Prepare(std::span<const RouteTarget> targets)
    {
        PROFILED_FUNCTION();

        const auto epoch = MapTopology::GetPathConnectivityEpoch();
        std::vector<RouteTarget> sortedTargets(targets.begin(), targets.end());
        std::sort(sortedTargets.begin(), sortedTargets.end(), TargetLess);
        sortedTargets.erase(std::unique(sortedTargets.begin(), sortedTargets.end()), sortedTargets.end());

        const bool sameTargets = _fields.size() == sortedTargets.size()
            && std::equal(
                _fields.begin(), _fields.end(), sortedTargets.begin(),
                [](const auto& field, const auto& target) { return field.target == target; });
        if (_preparedEpoch == epoch && sameTargets)
            return;

        if (sortedTargets.empty())
        {
            Reset();
            _preparedEpoch = epoch;
            return;
        }

        auto graph = FreezeGraph();
        if (!graph.has_value())
        {
            // One unsupported chunk can change the shortest route from any other chunk. Publish an empty state for this
            // epoch rather than a graph with holes, so every caller consistently retains the live legacy fallback.
            Reset();
            _preparedEpoch = epoch;
            return;
        }
        std::vector<RouteField> fields(sortedTargets.size());
        const auto build = [&](size_t index) { fields[index] = BuildField(*graph, sortedTargets[index]); };
        if (auto* context = GetContext(); context != nullptr && sortedTargets.size() > 1)
        {
            context->GetJobPool().ParallelFor(sortedTargets.size(), build, 1);
        }
        else
        {
            for (size_t index = 0; index < sortedTargets.size(); index++)
            {
                build(index);
            }
        }

        if (MapTopology::GetPathConnectivityEpoch() != graph->epoch)
            return;
        auto rideTargetFields = BuildRideTargetFieldRefs(fields);
        PublishedNodeIndex nodeIndex;
        nodeIndex.Build(graph->nodeIndex);
        _nodeIndex = std::move(nodeIndex);
        _fields = std::move(fields);
        _rideTargetFields = std::move(rideTargetFields);
        _entrances = std::move(graph->entrances);
        _entranceIndex = std::move(graph->entranceIndex);
        _preparedEpoch = graph->epoch;
    }

    std::optional<RouteStep> GetNextStep(const RouteTarget& target, const TileCoordsXYZ& source) noexcept
    {
        if (!IsPreparedForCurrentTopology())
            return std::nullopt;
        const auto* field = FindField(target);
        if (field == nullptr)
            return std::nullopt;

        const auto sourceNode = _nodeIndex.Find(source);
        if (sourceNode == kInvalidNodeIndex || sourceNode >= field->directions.size())
            return std::nullopt;
        const auto direction = field->directions[sourceNode];
        if (!DirectionValid(direction))
            return std::nullopt;
        return RouteStep{ direction };
    }

    RouteDistance QueryDistanceToTarget(const RouteTarget& target, const TileCoordsXYZ& source) noexcept
    {
        if (!IsPreparedForCurrentTopology())
            return {};
        const auto* field = FindField(target);
        if (field == nullptr || field->distances.empty())
            return {};

        const auto sourceNode = _nodeIndex.Find(source);
        if (sourceNode == kInvalidNodeIndex)
            return {};
        return { GetFieldDistance(*field, sourceNode), true };
    }

    RouteDistance QueryDistanceFromRideExitToTarget(
        const RouteTarget& target, const TileCoordsXYZ& sourceExit, RideId sourceRide) noexcept
    {
        if (sourceRide.IsNull() || !IsPreparedForCurrentTopology())
            return {};
        const auto* field = FindField(target);
        if (field == nullptr || field->distances.empty())
            return {};

        const auto key = GetLocationKey(sourceExit);
        auto entranceIterator = std::lower_bound(
            _entranceIndex.begin(), _entranceIndex.end(), key,
            [](const auto& entry, uint64_t value) { return entry.first < value; });
        auto bestDistance = kUnreachableDistance;
        bool foundExit = false;
        for (; entranceIterator != _entranceIndex.end() && entranceIterator->first == key; entranceIterator++)
        {
            const auto& entrance = _entrances[entranceIterator->second];
            if (entrance.entranceType != ENTRANCE_TYPE_RIDE_EXIT || entrance.ride != sourceRide)
                continue;
            foundExit = true;
            for (const auto pathNode : entrance.connections)
            {
                const auto distance = GetFieldDistance(*field, pathNode);
                if (distance.has_value())
                    bestDistance = std::min(bestDistance, *distance);
            }
        }
        if (!foundExit)
            return {};
        return { bestDistance == kUnreachableDistance ? std::nullopt : std::optional<uint32_t>{ bestDistance }, true };
    }

    std::optional<size_t> GetClosestReachableTargetIndex(
        std::span<const RouteTarget> candidates, const TileCoordsXYZ& source) noexcept
    {
        if (!IsPreparedForCurrentTopology())
            return std::nullopt;
        const auto sourceNode = _nodeIndex.Find(source);
        if (sourceNode == kInvalidNodeIndex)
            return std::nullopt;

        std::optional<size_t> result;
        auto shortestDistance = kUnreachableDistance;
        for (size_t index = 0; index < candidates.size(); index++)
        {
            const auto* field = FindField(candidates[index]);
            if (field == nullptr)
                continue;
            const auto distance = GetFieldDistance(*field, sourceNode);
            if (distance.has_value() && *distance < shortestDistance)
            {
                shortestDistance = *distance;
                result = index;
            }
        }
        return result;
    }

    ReachableRideTargetResult GetClosestReachableRideTarget(
        std::span<const RideId> candidates, const TileCoordsXYZ& source) noexcept
    {
        if (!IsPreparedForCurrentTopology() || candidates.empty())
            return {};
        const auto sourceNode = _nodeIndex.Find(source);
        if (sourceNode == kInvalidNodeIndex)
            return {};

        ReachableRideTargetResult result{ .isExact = true };
        auto shortestDistance = kUnreachableDistance;
        for (size_t candidateIndex = 0; candidateIndex < candidates.size(); candidateIndex++)
        {
            const auto ride = candidates[candidateIndex];
            auto fieldRef = FindFirstRideTargetField(ride);
            for (; fieldRef != _rideTargetFields.end() && fieldRef->ride == ride; fieldRef++)
            {
                const auto& field = _fields[fieldRef->fieldIndex];
                const auto distance = GetFieldDistance(field, sourceNode);
                if (distance.has_value() && *distance < shortestDistance)
                {
                    shortestDistance = *distance;
                    result.selection = ReachableRideTarget{ candidateIndex, field.target, *distance };
                }
            }
        }
        return result;
    }

    std::optional<RouteTarget> GetSingleTargetForRide(RideId ride) noexcept
    {
        if (ride.IsNull() || !IsPreparedForCurrentTopology())
            return std::nullopt;
        const auto first = FindFirstRideTargetField(ride);
        if (first == _rideTargetFields.end() || first->ride != ride
            || (first + 1 != _rideTargetFields.end() && (first + 1)->ride == ride))
            return std::nullopt;
        const auto& field = _fields[first->fieldIndex];
        return field.directions.empty() ? std::nullopt : std::optional<RouteTarget>{ field.target };
    }

    Statistics GetStatistics() noexcept
    {
        size_t directionEntryCount = 0;
        size_t distanceEntryCount = 0;
        for (const auto& field : _fields)
        {
            directionEntryCount += field.directions.size();
            distanceEntryCount += field.distances.size();
        }
        size_t singleRideTargetCount = 0;
        for (size_t first = 0; first < _rideTargetFields.size();)
        {
            auto last = first + 1;
            while (last < _rideTargetFields.size() && _rideTargetFields[last].ride == _rideTargetFields[first].ride)
                last++;
            singleRideTargetCount += last == first + 1 && !_fields[_rideTargetFields[first].fieldIndex].directions.empty();
            first = last;
        }
        return {
            .nodeCount = _nodeIndex.size(),
            .targetCount = _fields.size(),
            .directionEntryCount = directionEntryCount,
            .distanceEntryCount = distanceEntryCount,
            .singleRideTargetCount = singleRideTargetCount,
            .preparedForCurrentTopology = IsPreparedForCurrentTopology(),
        };
    }

    void Reset() noexcept
    {
        _nodeIndex = {};
        _fields.clear();
        _rideTargetFields.clear();
        _entrances.clear();
        _entranceIndex.clear();
        _preparedEpoch = 0;
    }
} // namespace OpenRCT2::MapPathRouteCache
