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
#include <limits>
#include <utility>
#include <vector>

namespace OpenRCT2::MapPathRouteCache
{
    namespace
    {
        using NodeIndex = uint32_t;
        constexpr auto kInvalidNodeIndex = std::numeric_limits<NodeIndex>::max();
        constexpr auto kUnreachableDistance = std::numeric_limits<uint32_t>::max();

        struct FrozenNode
        {
            TileCoordsXYZ location{};
            std::array<uint8_t, kNumOrthogonalDirections> targetBaseZ{};
            std::array<NodeIndex, kNumOrthogonalDirections> connections{ kInvalidNodeIndex, kInvalidNodeIndex,
                                                                         kInvalidNodeIndex, kInvalidNodeIndex };
            RideId queueRide{ RideId::GetNull() };
            uint8_t permittedEdges{};
            uint8_t connectionEdges{};
            Direction slopeDirection{};
            bool isQueue{};
            bool isSloped{};
        };

        struct FrozenEntrance
        {
            TileCoordsXYZ location{};
            std::array<uint8_t, kNumOrthogonalDirections> targetBaseZ{};
            std::array<NodeIndex, kNumOrthogonalDirections> connections{ kInvalidNodeIndex, kInvalidNodeIndex,
                                                                         kInvalidNodeIndex, kInvalidNodeIndex };
            RideId ride{ RideId::GetNull() };
            uint8_t entranceType{};
            uint8_t connectionEdges{};
        };

        static_assert(sizeof(FrozenNode) <= 40);
        static_assert(sizeof(FrozenEntrance) <= 40);

        struct FrozenGraph
        {
            MapTopology::Generation epoch{};
            std::vector<FrozenNode> nodes;
            std::vector<FrozenEntrance> entrances;
            std::vector<std::pair<uint64_t, NodeIndex>> nodeIndex;
            std::vector<std::pair<uint64_t, NodeIndex>> entranceIndex;
            std::vector<uint32_t> incomingOffsets;
            std::vector<NodeIndex> incomingEdges;
        };

        struct RouteField
        {
            RouteTarget target{};
            std::vector<uint8_t> directions;
            std::vector<uint32_t> distances;
        };

        struct SingleRideTarget
        {
            RideId ride{ RideId::GetNull() };
            RouteTarget target{};
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
                    Reset();
                    return;
                }

                size_t capacity = 1;
                while (capacity < sortedNodes.size() * 2)
                {
                    capacity <<= 1;
                }
                _entries.assign(capacity, Entry{});
                _count = 0;
                const auto mask = capacity - 1;
                for (const auto& [key, node] : sortedNodes)
                {
                    auto index = HashLocationKey(key) & mask;
                    while (_entries[index].node != kInvalidNodeIndex && _entries[index].key != key)
                    {
                        index = (index + 1) & mask;
                    }
                    if (_entries[index].node == kInvalidNodeIndex)
                    {
                        _entries[index] = { key, node };
                        _count++;
                    }
                }
            }

            [[nodiscard]] NodeIndex Find(const TileCoordsXYZ& location) const noexcept
            {
                if (_entries.empty())
                {
                    return kInvalidNodeIndex;
                }

                const auto key = GetLocationKey(location);
                const auto mask = _entries.size() - 1;
                auto index = HashLocationKey(key) & mask;
                while (_entries[index].node != kInvalidNodeIndex)
                {
                    if (_entries[index].key == key)
                    {
                        return _entries[index].node;
                    }
                    index = (index + 1) & mask;
                }
                return kInvalidNodeIndex;
            }

            [[nodiscard]] size_t size() const noexcept
            {
                return _count;
            }

            void Reset() noexcept
            {
                std::vector<Entry>().swap(_entries);
                _count = 0;
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
        std::vector<SingleRideTarget> _singleRideTargets;
        std::vector<RideTargetFieldRef> _rideTargetFields;
        std::vector<FrozenEntrance> _entrances;
        std::vector<std::pair<uint64_t, NodeIndex>> _entranceIndex;
        MapTopology::Generation _preparedEpoch{};

        [[nodiscard]] bool TargetLess(const RouteTarget& lhs, const RouteTarget& rhs) noexcept
        {
            if (lhs.location.x != rhs.location.x)
                return lhs.location.x < rhs.location.x;
            if (lhs.location.y != rhs.location.y)
                return lhs.location.y < rhs.location.y;
            if (lhs.location.z != rhs.location.z)
                return lhs.location.z < rhs.location.z;
            if (lhs.queueRide != rhs.queueRide)
                return lhs.queueRide.ToUnderlying() < rhs.queueRide.ToUnderlying();
            return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
        }

        [[nodiscard]] bool IsSameDestination(const RouteTarget& lhs, const RouteTarget& rhs) noexcept
        {
            return lhs.location == rhs.location && lhs.queueRide == rhs.queueRide;
        }

        [[nodiscard]] const RouteField* FindField(const RouteTarget& target) noexcept
        {
            auto firstTargetKind = target;
            firstTargetKind.kind = RouteTargetKind::pathOrEntrance;
            const auto fieldIterator = std::lower_bound(
                _fields.begin(), _fields.end(), firstTargetKind,
                [](const RouteField& field, const RouteTarget& value) { return TargetLess(field.target, value); });
            return fieldIterator == _fields.end() || !IsSameDestination(fieldIterator->target, target) ? nullptr
                                                                                                       : &*fieldIterator;
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
            if (iterator == nodeIndex.end() || iterator->first != key)
                return kInvalidNodeIndex;
            return iterator->second;
        }

        [[nodiscard]] bool NodeIsAllowed(const FrozenNode& node, RideId targetQueueRide) noexcept
        {
            return !node.isQueue || node.queueRide.IsNull() || node.queueRide == targetQueueRide;
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
                        node.isQueue = path.HasFlag(MapPathTopology::PathNodeFlag::queue);
                        node.isSloped = path.HasFlag(MapPathTopology::PathNodeFlag::sloped);
                        node.slopeDirection = path.slopeDirection;
                        for (Direction direction : kAllDirections)
                        {
                            const auto& connection = path.connections[direction];
                            if (!connection.IsConnected())
                                continue;
                            node.connectionEdges |= 1 << direction;
                            node.targetBaseZ[direction] = connection.targetBaseZ;
                        }
                        graph.nodes.push_back(node);
                    }

                    for (const auto& entrance : view.entrances)
                    {
                        auto node = FrozenEntrance{};
                        node.location = entrance.GetLocation(view.origin);
                        node.ride = entrance.ride;
                        node.entranceType = entrance.entranceType;
                        node.connectionEdges = entrance.connectionEdges;
                        for (Direction direction : kAllDirections)
                        {
                            const auto& connection = entrance.connections[direction];
                            if (!connection.IsConnected())
                                continue;
                            node.targetBaseZ[direction] = connection.targetBaseZ;
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

            for (auto& node : graph.nodes)
            {
                for (Direction direction : kAllDirections)
                {
                    if ((node.permittedEdges & node.connectionEdges) & (1 << direction))
                    {
                        const auto targetTile = TileCoordsXY{ node.location } + TileDirectionDelta[direction];
                        node.connections[direction] = FindNode(
                            graph.nodeIndex, TileCoordsXYZ{ targetTile, node.targetBaseZ[direction] });
                    }
                }
            }
            for (auto& entrance : graph.entrances)
            {
                for (Direction direction : kAllDirections)
                {
                    if (entrance.connectionEdges & (1 << direction))
                    {
                        const auto targetTile = TileCoordsXY{ entrance.location } + TileDirectionDelta[direction];
                        entrance.connections[direction] = FindNode(
                            graph.nodeIndex, TileCoordsXYZ{ targetTile, entrance.targetBaseZ[direction] });
                    }
                }
            }

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
                        graph.incomingEdges[cursors[target]++] = source;
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
            if (target.kind != RouteTargetKind::shopOrFacilityTrack)
                return;

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
                    if (path.isSloped && path.slopeDirection == direction)
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

            const auto targetPath = FindNode(graph.nodeIndex, target.location);
            if (target.kind == RouteTargetKind::pathOrEntrance && targetPath != kInvalidNodeIndex
                && NodeIsAllowed(graph.nodes[targetPath], target.queueRide))
            {
                distances[targetPath] = 0;
                queue.push_back(targetPath);
            }
            if (target.kind == RouteTargetKind::pathOrEntrance)
                AddEntranceSeeds(graph, target, distances, field.directions, queue);
            else
                AddShopFacilitySeeds(graph, target, distances, field.directions, queue);
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
                    const auto source = graph.incomingEdges[edgeIndex];
                    if (distances[source] != kUnreachableDistance
                        || !NodeIsAllowed(graph.nodes[source], target.queueRide))
                    {
                        continue;
                    }
                    distances[source] = nextDistance;
                    queue.push_back(source);
                }
            }

            for (NodeIndex source = 0; source < graph.nodes.size(); source++)
            {
                if (DirectionValid(field.directions[source]) || distances[source] == kUnreachableDistance)
                    continue;

                auto bestDistance = distances[source];
                for (Direction direction : kAllDirections)
                {
                    const auto targetNode = graph.nodes[source].connections[direction];
                    if (targetNode == kInvalidNodeIndex || !NodeIsAllowed(graph.nodes[targetNode], target.queueRide))
                        continue;
                    if (distances[targetNode] < bestDistance)
                    {
                        bestDistance = distances[targetNode];
                        field.directions[source] = direction;
                    }
                }
            }
            field.distances = std::move(distances);
            return field;
        }

        [[nodiscard]] std::vector<SingleRideTarget> BuildSingleRideTargets(const std::vector<RouteField>& fields)
        {
            std::vector<RouteTarget> rideTargets;
            rideTargets.reserve(fields.size());
            for (const auto& field : fields)
            {
                if (!field.target.queueRide.IsNull())
                    rideTargets.push_back(field.target);
            }
            std::sort(rideTargets.begin(), rideTargets.end(), [](const RouteTarget& lhs, const RouteTarget& rhs) {
                if (lhs.queueRide != rhs.queueRide)
                    return lhs.queueRide.ToUnderlying() < rhs.queueRide.ToUnderlying();
                return TargetLess(lhs, rhs);
            });

            std::vector<SingleRideTarget> result;
            for (size_t first = 0; first < rideTargets.size();)
            {
                auto last = first + 1;
                while (last < rideTargets.size() && rideTargets[last].queueRide == rideTargets[first].queueRide)
                {
                    last++;
                }
                if (last == first + 1)
                {
                    const auto field = std::lower_bound(
                        fields.begin(), fields.end(), rideTargets[first],
                        [](const RouteField& entry, const RouteTarget& target) {
                            return TargetLess(entry.target, target);
                        });
                    if (field != fields.end() && field->target == rideTargets[first] && !field->directions.empty())
                        result.push_back({ rideTargets[first].queueRide, rideTargets[first] });
                }
                first = last;
            }
            return result;
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

    bool RouteTarget::operator==(const RouteTarget& other) const noexcept
    {
        return location == other.location && queueRide == other.queueRide && kind == other.kind;
    }

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
        auto singleRideTargets = BuildSingleRideTargets(fields);
        auto rideTargetFields = BuildRideTargetFieldRefs(fields);
        PublishedNodeIndex nodeIndex;
        nodeIndex.Build(graph->nodeIndex);
        _nodeIndex = std::move(nodeIndex);
        _fields = std::move(fields);
        _singleRideTargets = std::move(singleRideTargets);
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
        if (!IsPreparedForCurrentTopology() || sourceRide.IsNull())
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
        if (!IsPreparedForCurrentTopology() || candidates.empty())
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
            auto fieldRef = std::lower_bound(
                _rideTargetFields.begin(), _rideTargetFields.end(), ride,
                [](const RideTargetFieldRef& entry, RideId value) {
                    return entry.ride.ToUnderlying() < value.ToUnderlying();
                });
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
        const auto iterator = std::lower_bound(
            _singleRideTargets.begin(), _singleRideTargets.end(), ride,
            [](const SingleRideTarget& entry, RideId value) {
                return entry.ride.ToUnderlying() < value.ToUnderlying();
            });
        if (iterator == _singleRideTargets.end() || iterator->ride != ride)
            return std::nullopt;
        return iterator->target;
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
        return {
            .nodeCount = _nodeIndex.size(),
            .targetCount = _fields.size(),
            .directionEntryCount = directionEntryCount,
            .distanceEntryCount = distanceEntryCount,
            .singleRideTargetCount = _singleRideTargets.size(),
            .preparedForCurrentTopology = IsPreparedForCurrentTopology(),
        };
    }

    void Reset() noexcept
    {
        _nodeIndex.Reset();
        std::vector<RouteField>().swap(_fields);
        std::vector<SingleRideTarget>().swap(_singleRideTargets);
        std::vector<RideTargetFieldRef>().swap(_rideTargetFields);
        std::vector<FrozenEntrance>().swap(_entrances);
        std::vector<std::pair<uint64_t, NodeIndex>>().swap(_entranceIndex);
        _preparedEpoch = 0;
    }
} // namespace OpenRCT2::MapPathRouteCache
