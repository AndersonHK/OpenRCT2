/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/peep/GuestPathfinding.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPathTopology.h>
#include <openrct2/world/MapPathRouteCache.h>
#include <openrct2/world/MapTopology.h>
#include <openrct2/world/tile_element/BannerElement.h>
#include <openrct2/world/tile_element/EntranceElement.h>
#include <openrct2/world/tile_element/PathElement.h>

using namespace OpenRCT2;

class MapPathTopologyTest : public testing::Test
{
public:
    static void SetUpTestCase()
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        _context = CreateContext();
        ASSERT_TRUE(_context->Initialise());
    }

    static void TearDownTestCase()
    {
        _context = nullptr;
    }

    void SetUp() override
    {
        MapInit({ 70, 40 });
    }

protected:
    static PathElement* AddPath(
        const TileCoordsXY& tile, uint8_t baseZ, uint8_t edges, bool sloped = false, Direction slopeDirection = 0)
    {
        auto* path = TileElementInsert<PathElement>({ tile.ToCoordsXY(), baseZ * kCoordsZStep }, 0);
        if (path == nullptr)
        {
            ADD_FAILURE() << "Unable to insert path element";
            return nullptr;
        }

        path->setClearanceZ((baseZ + 4) * kCoordsZStep);
        path->SetEdges(edges);
        path->SetSloped(sloped);
        path->SetSlopeDirection(slopeDirection);
        path->SetIsQueue(false);
        path->SetWide(false);
        path->SetHasQueueBanner(false);
        path->setGhost(false);
        MapTopology::InvalidateTileAndNeighbours(tile);
        return path;
    }

    static BannerElement* AddBanner(const TileCoordsXY& tile, uint8_t baseZ, uint8_t allowedEdges)
    {
        auto* banner = TileElementInsert<BannerElement>({ tile.ToCoordsXY(), baseZ * kCoordsZStep }, 0);
        if (banner == nullptr)
        {
            ADD_FAILURE() << "Unable to insert banner element";
            return nullptr;
        }

        banner->setClearanceZ((baseZ + 2) * kCoordsZStep);
        banner->SetAllowedEdges(allowedEdges);
        banner->setGhost(false);
        MapTopology::InvalidateTileAndNeighbours(tile);
        return banner;
    }

    static EntranceElement* AddEntrance(
        const TileCoordsXY& tile, uint8_t baseZ, uint8_t entranceType, Direction direction, RideId ride, StationIndex station)
    {
        auto* entrance = TileElementInsert<EntranceElement>({ tile.ToCoordsXY(), baseZ * kCoordsZStep }, 0);
        if (entrance == nullptr)
        {
            ADD_FAILURE() << "Unable to insert entrance element";
            return nullptr;
        }

        entrance->setClearanceZ((baseZ + 4) * kCoordsZStep);
        entrance->SetEntranceType(entranceType);
        entrance->SetSequenceIndex(EntranceSequence::Centre);
        entrance->setDirection(direction);
        entrance->SetRideIndex(ride);
        entrance->SetStationIndex(station);
        entrance->setGhost(false);
        MapTopology::InvalidateTileAndNeighbours(tile);
        return entrance;
    }

private:
    static std::shared_ptr<IContext> _context;
};

std::shared_ptr<IContext> MapPathTopologyTest::_context;

TEST_F(MapPathTopologyTest, SlopeAndHeightDeterminePathAdjacency)
{
    constexpr Direction east = 2;
    constexpr Direction west = 0;
    constexpr Direction south = 1;
    const auto sourceTile = TileCoordsXY{ 10, 10 };
    const auto highTile = sourceTile + TileDirectionDelta[east];
    const auto wrongHeightTile = sourceTile + TileDirectionDelta[south];

    ASSERT_NE(AddPath(sourceTile, 10, (1 << east) | (1 << south), true, east), nullptr);
    ASSERT_NE(AddPath(highTile, 12, 1 << west), nullptr);
    ASSERT_NE(AddPath(wrongHeightTile, 11, 1 << DirectionReverse(south)), nullptr);

    const auto view = MapPathTopology::GetChunk(sourceTile);
    ASSERT_TRUE(view);
    ASSERT_TRUE(view.isExact);

    const auto* source = MapPathTopology::FindPath(view, { sourceTile, 10 });
    ASSERT_NE(source, nullptr);
    EXPECT_TRUE(source->HasFlag(MapPathTopology::PathNodeFlag::sloped));
    EXPECT_EQ(source->slopeDirection, east);
    EXPECT_TRUE(source->connections[east].IsConnected());
    EXPECT_EQ(source->connections[east].targetBaseZ, 12);
    EXPECT_FALSE(source->connections[south].IsConnected());

    const auto* high = MapPathTopology::FindPath(view, { highTile, 12 });
    ASSERT_NE(high, nullptr);
    EXPECT_TRUE(high->connections[west].IsConnected());
    EXPECT_EQ(high->connections[west].targetBaseZ, 10);
}

TEST_F(MapPathTopologyTest, BannerDirectionQueueOwnershipWideFlagAndEntranceConnectionsArePreserved)
{
    constexpr Direction east = 2;
    constexpr Direction south = 1;
    const auto queueTile = TileCoordsXY{ 10, 10 };
    auto* queue = AddPath(queueTile, 10, (1 << east) | (1 << south));
    ASSERT_NE(queue, nullptr);
    ASSERT_NE(AddPath(queueTile + TileDirectionDelta[east], 10, 1 << DirectionReverse(east)), nullptr);
    ASSERT_NE(AddPath(queueTile + TileDirectionDelta[south], 10, 1 << DirectionReverse(south)), nullptr);

    const auto queueRide = RideId::FromUnderlying(42);
    const auto queueStation = StationIndex::FromUnderlying(3);
    queue->SetIsQueue(true);
    queue->SetRideIndex(queueRide);
    queue->SetStationIndex(queueStation);
    queue->SetWide(true);
    queue->SetHasQueueBanner(true);
    queue->SetQueueBannerDirection(east);
    ASSERT_NE(AddBanner(queueTile, 12, 1 << south), nullptr);
    MapTopology::InvalidateTileAndNeighbours(queueTile);

    constexpr Direction entranceDirection = east;
    const auto entranceTile = TileCoordsXY{ 12, 12 };
    const auto entrancePathDirection = DirectionReverse(entranceDirection);
    ASSERT_NE(AddPath(entranceTile + TileDirectionDelta[entrancePathDirection], 10, 1 << entranceDirection), nullptr);
    const auto entranceRide = RideId::FromUnderlying(55);
    const auto entranceStation = StationIndex::FromUnderlying(1);
    ASSERT_NE(
        AddEntrance(entranceTile, 10, ENTRANCE_TYPE_RIDE_ENTRANCE, entranceDirection, entranceRide, entranceStation), nullptr);

    const auto view = MapPathTopology::GetChunk(queueTile);
    ASSERT_TRUE(view);
    ASSERT_TRUE(view.isExact);

    const auto* queueNode = MapPathTopology::FindPath(view, { queueTile, 10 });
    ASSERT_NE(queueNode, nullptr);
    EXPECT_EQ(queueNode->edges, (1 << east) | (1 << south));
    EXPECT_EQ(queueNode->permittedEdges, 1 << south);
    EXPECT_TRUE(queueNode->connections[east].IsConnected());
    EXPECT_TRUE(queueNode->HasFlag(MapPathTopology::PathNodeFlag::queue));
    EXPECT_TRUE(queueNode->HasFlag(MapPathTopology::PathNodeFlag::wide));
    EXPECT_TRUE(queueNode->HasFlag(MapPathTopology::PathNodeFlag::queueBanner));
    EXPECT_EQ(queueNode->queueBannerDirection, east);
    EXPECT_TRUE(queueNode->HasFlag(MapPathTopology::PathNodeFlag::banner));
    EXPECT_EQ(queueNode->queueRide, queueRide);
    EXPECT_EQ(queueNode->queueStation, queueStation);

    const auto* entrance = MapPathTopology::FindEntrance(view, { entranceTile, 10 }, ENTRANCE_TYPE_RIDE_ENTRANCE);
    ASSERT_NE(entrance, nullptr);
    EXPECT_EQ(entrance->ride, entranceRide);
    EXPECT_EQ(entrance->station, entranceStation);
    EXPECT_EQ(entrance->connectionEdges, 1 << entrancePathDirection);
    EXPECT_TRUE(entrance->connections[entrancePathDirection].IsConnected());
    EXPECT_EQ(entrance->connections[entrancePathDirection].targetBaseZ, 10);
}

TEST_F(MapPathTopologyTest, BoundaryAndNeighbourGenerationChangesRebuildTheDependentChunk)
{
    constexpr Direction east = 2;
    const auto sourceTile = TileCoordsXY{ 15, 10 };
    const auto targetTile = TileCoordsXY{ 16, 10 };
    ASSERT_NE(AddPath(sourceTile, 10, 1 << east), nullptr);
    auto* target = AddPath(targetTile, 10, 1 << DirectionReverse(east));
    ASSERT_NE(target, nullptr);

    const auto initial = MapPathTopology::GetChunk(sourceTile);
    const auto* initialSource = MapPathTopology::FindPath(initial, { sourceTile, 10 });
    ASSERT_NE(initialSource, nullptr);
    ASSERT_TRUE(initialSource->connections[east].IsConnected());

    // The source chunk records the east chunk generation because its adjacency can cross the boundary.
    MapTopology::InvalidateTileAndNeighbours(TileCoordsXY{ 20, 10 });
    const auto neighbourChanged = MapPathTopology::GetChunk(sourceTile);
    EXPECT_NE(neighbourChanged.buildSerial, initial.buildSerial);

    target->setBaseZ(12 * kCoordsZStep);
    target->setClearanceZ(16 * kCoordsZStep);
    MapTopology::InvalidateTileAndNeighbours(targetTile);
    const auto heightChanged = MapPathTopology::GetChunk(sourceTile);
    EXPECT_NE(heightChanged.buildSerial, neighbourChanged.buildSerial);
    const auto* changedSource = MapPathTopology::FindPath(heightChanged, { sourceTile, 10 });
    ASSERT_NE(changedSource, nullptr);
    EXPECT_FALSE(changedSource->connections[east].IsConnected());
}

TEST_F(MapPathTopologyTest, ThinJunctionClassificationExcludesWidePathsAndOwnedQueues)
{
    constexpr Direction north = 0;
    constexpr Direction east = 1;
    constexpr Direction south = 2;
    constexpr Direction west = 3;
    const auto centre = TileCoordsXY{ 10, 10 };
    auto* source = AddPath(centre, 10, 0x0F);
    ASSERT_NE(source, nullptr);

    ASSERT_NE(AddPath(centre + TileDirectionDelta[north], 10, 1 << DirectionReverse(north)), nullptr);
    ASSERT_NE(AddPath(centre + TileDirectionDelta[east], 10, 1 << DirectionReverse(east)), nullptr);
    ASSERT_NE(AddPath(centre + TileDirectionDelta[south], 10, 1 << DirectionReverse(south)), nullptr);
    auto* westPath = AddPath(centre + TileDirectionDelta[west], 10, 1 << DirectionReverse(west));
    ASSERT_NE(westPath, nullptr);

    auto view = MapPathTopology::GetChunk(centre);
    auto* sourceNode = MapPathTopology::FindPath(view, { centre, 10 });
    ASSERT_NE(sourceNode, nullptr);
    EXPECT_TRUE(sourceNode->HasFlag(MapPathTopology::PathNodeFlag::thinJunction));
    EXPECT_FALSE(sourceNode->connections[west].HasFlag(MapPathTopology::ConnectionFlag::targetWide));

    westPath->SetWide(true);
    MapTopology::InvalidateTileAndNeighbours(centre + TileDirectionDelta[west]);
    view = MapPathTopology::GetChunk(centre);
    sourceNode = MapPathTopology::FindPath(view, { centre, 10 });
    ASSERT_NE(sourceNode, nullptr);
    EXPECT_TRUE(sourceNode->HasFlag(MapPathTopology::PathNodeFlag::thinJunction));
    EXPECT_TRUE(sourceNode->connections[west].HasFlag(MapPathTopology::ConnectionFlag::targetWide));

    auto* southPath = MapGetPathElementAt({ centre + TileDirectionDelta[south], 10 });
    ASSERT_NE(southPath, nullptr);
    southPath->SetIsQueue(true);
    southPath->SetRideIndex(RideId::FromUnderlying(42));
    MapTopology::InvalidateTileAndNeighbours(centre + TileDirectionDelta[south]);
    view = MapPathTopology::GetChunk(centre);
    sourceNode = MapPathTopology::FindPath(view, { centre, 10 });
    ASSERT_NE(sourceNode, nullptr);
    EXPECT_FALSE(sourceNode->HasFlag(MapPathTopology::PathNodeFlag::thinJunction));
    EXPECT_TRUE(sourceNode->connections[south].HasFlag(MapPathTopology::ConnectionFlag::targetRideQueue));

    southPath->SetRideIndex(RideId::GetNull());
    MapTopology::InvalidateTileAndNeighbours(centre + TileDirectionDelta[south]);
    view = MapPathTopology::GetChunk(centre);
    sourceNode = MapPathTopology::FindPath(view, { centre, 10 });
    ASSERT_NE(sourceNode, nullptr);
    EXPECT_TRUE(sourceNode->HasFlag(MapPathTopology::PathNodeFlag::thinJunction));
    EXPECT_FALSE(sourceNode->connections[south].HasFlag(MapPathTopology::ConnectionFlag::targetRideQueue));
}

TEST_F(MapPathTopologyTest, SharedRouteFieldsRespectDirectedEdgesInvalidateAndIgnoreTargetInputOrder)
{
    constexpr Direction east = 2;
    constexpr Direction south = 1;
    const auto start = TileCoordsXY{ 10, 10 };
    const auto eastPath = start + TileDirectionDelta[east];
    const auto southPath = start + TileDirectionDelta[south];
    const auto mergePath = eastPath + TileDirectionDelta[south];
    const auto entranceTile = mergePath + TileDirectionDelta[east];

    ASSERT_NE(AddPath(start, 10, (1 << east) | (1 << south)), nullptr);
    ASSERT_NE(AddPath(eastPath, 10, (1 << DirectionReverse(east)) | (1 << south)), nullptr);
    ASSERT_NE(AddPath(southPath, 10, (1 << DirectionReverse(south)) | (1 << east)), nullptr);
    ASSERT_NE(
        AddPath(mergePath, 10, (1 << DirectionReverse(south)) | (1 << DirectionReverse(east)) | (1 << east)), nullptr);
    const auto ride = RideId::FromUnderlying(42);
    ASSERT_NE(
        AddEntrance(entranceTile, 10, ENTRANCE_TYPE_RIDE_ENTRANCE, east, ride, StationIndex::FromUnderlying(0)), nullptr);
    auto* banner = AddBanner(start, 12, 1 << east);
    ASSERT_NE(banner, nullptr);

    const auto target = MapPathRouteCache::RouteTarget{ { entranceTile, 10 }, ride };
    const auto unreachable = MapPathRouteCache::RouteTarget{ { 30, 30, 10 }, RideId::GetNull() };
    MapPathRouteCache::Prepare(std::array{ unreachable, target });
    const auto first = MapPathRouteCache::GetNextStep(target, { start, 10 });
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->direction, east);
    const auto singleTarget = MapPathRouteCache::GetSingleTargetForRide(ride);
    ASSERT_TRUE(singleTarget.has_value());
    EXPECT_EQ(singleTarget->location, target.location);

    MapPathRouteCache::Reset();
    MapPathRouteCache::Prepare(std::array{ target, unreachable });
    const auto reordered = MapPathRouteCache::GetNextStep(target, { start, 10 });
    ASSERT_TRUE(reordered.has_value());
    EXPECT_EQ(reordered->direction, first->direction);

    banner->SetAllowedEdges(1 << south);
    MapTopology::InvalidateTileAndNeighbours(start);
    EXPECT_FALSE(MapPathRouteCache::IsPreparedForCurrentTopology());
    EXPECT_FALSE(MapPathRouteCache::GetNextStep(target, { start, 10 }).has_value());

    MapPathRouteCache::Prepare(std::array{ target });
    const auto changed = MapPathRouteCache::GetNextStep(target, { start, 10 });
    ASSERT_TRUE(changed.has_value());
    EXPECT_EQ(changed->direction, south);
}

TEST_F(MapPathTopologyTest, SharedRouteFieldsFallBackGloballyWhenAnIntermediateChunkIsInexact)
{
    constexpr Direction east = 2;
    constexpr Direction west = 0;
    constexpr uint8_t baseZ = 10;
    constexpr int32_t firstPathX = 10;
    constexpr int32_t lastPathX = 34;
    constexpr int32_t pathY = 10;
    const auto ride = RideId::FromUnderlying(42);

    for (int32_t x = firstPathX; x <= lastPathX; x++)
    {
        uint8_t edges = 1 << east;
        if (x != firstPathX)
            edges |= 1 << west;
        ASSERT_NE(AddPath({ x, pathY }, baseZ, edges), nullptr);
    }

    // Duplicate stable paths at one tile/Z make the middle chunk unsupported. Publishing the two exact outer chunks as
    // a disconnected graph would produce a deterministic detour or dead end instead of retaining live pathfinding globally.
    ASSERT_NE(AddPath({ 20, pathY }, baseZ, (1 << east) | (1 << west)), nullptr);
    const auto entranceTile = TileCoordsXY{ lastPathX + 1, pathY };
    ASSERT_NE(
        AddEntrance(entranceTile, baseZ, ENTRANCE_TYPE_RIDE_ENTRANCE, east, ride, StationIndex::FromUnderlying(0)),
        nullptr);

    const auto middleView = MapPathTopology::GetChunk(TileCoordsXY{ 20, pathY });
    ASSERT_TRUE(middleView);
    ASSERT_FALSE(middleView.isExact);

    const auto target = MapPathRouteCache::RouteTarget{ { entranceTile, baseZ }, ride };
    MapPathRouteCache::Prepare(std::array{ target });

    // The epoch is deliberately remembered as fallback-only so gameplay does not retry the same full build every tick.
    EXPECT_TRUE(MapPathRouteCache::IsPreparedForCurrentTopology());
    EXPECT_FALSE(MapPathRouteCache::GetNextStep(target, { firstPathX, pathY, baseZ }).has_value());
    EXPECT_FALSE(MapPathRouteCache::GetSingleTargetForRide(ride).has_value());
}

TEST_F(MapPathTopologyTest, SharedRouteFieldsExcludeForeignQueuesAndAdmitTheTargetQueue)
{
    constexpr Direction east = 2;
    constexpr Direction south = 1;
    const auto start = TileCoordsXY{ 20, 20 };
    const auto eastPath = start + TileDirectionDelta[east];
    const auto southPath = start + TileDirectionDelta[south];
    const auto targetPath = eastPath + TileDirectionDelta[south];

    ASSERT_NE(AddPath(start, 10, (1 << east) | (1 << south)), nullptr);
    ASSERT_NE(AddPath(eastPath, 10, (1 << DirectionReverse(east)) | (1 << south)), nullptr);
    auto* foreignQueue = AddPath(southPath, 10, (1 << DirectionReverse(south)) | (1 << east));
    ASSERT_NE(foreignQueue, nullptr);
    const auto targetRide = RideId::FromUnderlying(42);
    const auto foreignRide = RideId::FromUnderlying(99);
    foreignQueue->SetIsQueue(true);
    foreignQueue->SetRideIndex(foreignRide);
    auto* targetQueue = AddPath(
        targetPath, 10, (1 << DirectionReverse(south)) | (1 << DirectionReverse(east)));
    ASSERT_NE(targetQueue, nullptr);
    targetQueue->SetIsQueue(true);
    targetQueue->SetRideIndex(targetRide);
    MapTopology::InvalidateTileAndNeighbours(southPath);
    MapTopology::InvalidateTileAndNeighbours(targetPath);

    const auto target = MapPathRouteCache::RouteTarget{ { targetPath, 10 }, targetRide };
    MapPathRouteCache::Prepare(std::array{ target });
    const auto avoidsForeignQueue = MapPathRouteCache::GetNextStep(target, { start, 10 });
    ASSERT_TRUE(avoidsForeignQueue.has_value());
    EXPECT_EQ(avoidsForeignQueue->direction, east);

    foreignQueue->SetRideIndex(targetRide);
    MapTopology::InvalidateTileAndNeighbours(southPath);
    MapPathRouteCache::Prepare(std::array{ target });
    const auto admitsTargetQueue = MapPathRouteCache::GetNextStep(target, { start, 10 });
    ASSERT_TRUE(admitsTargetQueue.has_value());
    EXPECT_EQ(admitsTargetQueue->direction, south);
}

TEST_F(MapPathTopologyTest, SharedRouteProposalCannotBypassGuestJunctionHistory)
{
    constexpr Direction east = 2;
    constexpr Direction south = 1;
    constexpr Direction west = 0;
    const auto start = TileCoordsXY{ 10, 10 };
    const auto directPath = start + TileDirectionDelta[east];
    const auto detourStart = start + TileDirectionDelta[south];
    const auto detourCorner = detourStart + TileDirectionDelta[east];
    const auto deadEnd = start + TileDirectionDelta[west];
    const auto entranceTile = directPath + TileDirectionDelta[east];

    ASSERT_NE(AddPath(start, 10, (1 << east) | (1 << south) | (1 << west)), nullptr);
    ASSERT_NE(
        AddPath(
            directPath, 10,
            (1 << DirectionReverse(east)) | (1 << east) | (1 << south)),
        nullptr);
    ASSERT_NE(AddPath(detourStart, 10, (1 << DirectionReverse(south)) | (1 << east)), nullptr);
    ASSERT_NE(
        AddPath(detourCorner, 10, (1 << DirectionReverse(east)) | (1 << DirectionReverse(south))), nullptr);
    ASSERT_NE(AddPath(deadEnd, 10, 1 << DirectionReverse(west)), nullptr);
    const auto ride = RideId::FromUnderlying(42);
    ASSERT_NE(
        AddEntrance(entranceTile, 10, ENTRANCE_TYPE_RIDE_ENTRANCE, east, ride, StationIndex::FromUnderlying(0)), nullptr);

    const auto target = MapPathRouteCache::RouteTarget{ { entranceTile, 10 }, ride };
    MapPathRouteCache::Prepare(std::array{ target });
    const auto sharedStep = MapPathRouteCache::GetNextStep(target, { start, 10 });
    ASSERT_TRUE(sharedStep.has_value());
    ASSERT_EQ(sharedStep->direction, east);

    Guest guest{};
    guest.type = EntityType::guest;
    guest.PathfindGoal = { target.location, 0 };
    for (auto& history : guest.PathfindHistory)
    {
        history.SetNull();
    }
    guest.PathfindHistory[0] = { { start, 10 }, 1 << south };

    const auto chosen = PathFinding::ChooseDirection({ start, 10 }, target.location, guest, true, ride);
    EXPECT_EQ(chosen, south);
}

TEST_F(MapPathTopologyTest, ExplicitShopFacilityTargetsRespectSlopedTerminalHeight)
{
    constexpr Direction east = 2;
    const auto source = TileCoordsXY{ 10, 10 };
    const auto terminal = source + TileDirectionDelta[east];
    const auto ride = RideId::FromUnderlying(42);
    ASSERT_NE(AddPath(source, 10, 1 << east, true, east), nullptr);

    const auto ordinaryTarget = MapPathRouteCache::RouteTarget{ { terminal, 12 }, ride };
    MapPathRouteCache::Prepare(std::array{ ordinaryTarget });
    EXPECT_FALSE(MapPathRouteCache::GetNextStep(ordinaryTarget, { source, 10 }).has_value());

    const auto shopTarget = MapPathRouteCache::RouteTarget{
        { terminal, 12 }, ride, MapPathRouteCache::RouteTargetKind::shopOrFacilityTrack
    };
    MapPathRouteCache::Prepare(std::array{ shopTarget });
    const auto route = MapPathRouteCache::GetNextStep(ordinaryTarget, { source, 10 });
    ASSERT_TRUE(route.has_value());
    EXPECT_EQ(route->direction, east);
    const auto singleTarget = MapPathRouteCache::GetSingleTargetForRide(ride);
    ASSERT_TRUE(singleTarget.has_value());
    EXPECT_EQ(singleTarget->kind, MapPathRouteCache::RouteTargetKind::shopOrFacilityTrack);

    const auto wrongHeight = MapPathRouteCache::RouteTarget{
        { terminal, 10 }, ride, MapPathRouteCache::RouteTargetKind::shopOrFacilityTrack
    };
    MapPathRouteCache::Prepare(std::array{ wrongHeight });
    EXPECT_FALSE(MapPathRouteCache::GetNextStep(wrongHeight, { source, 10 }).has_value());

    const auto flatSource = TileCoordsXY{ 10, 14 };
    const auto flatTerminal = flatSource + TileDirectionDelta[east];
    ASSERT_NE(AddPath(flatSource, 10, 1 << east), nullptr);
    const auto flatTarget = MapPathRouteCache::RouteTarget{
        { flatTerminal, 10 }, ride, MapPathRouteCache::RouteTargetKind::shopOrFacilityTrack
    };
    MapPathRouteCache::Prepare(std::array{ flatTarget });
    const auto flatRoute = MapPathRouteCache::GetNextStep(flatTarget, { flatSource, 10 });
    ASSERT_TRUE(flatRoute.has_value());
    EXPECT_EQ(flatRoute->direction, east);
}

TEST_F(MapPathTopologyTest, SingleRideTargetIndexRejectsMultipleDistinctDestinations)
{
    const auto ride = RideId::FromUnderlying(42);
    const auto first = MapPathRouteCache::RouteTarget{ { 10, 10, 10 }, ride };
    const auto second = MapPathRouteCache::RouteTarget{ { 20, 20, 10 }, ride };
    ASSERT_NE(AddPath({ 10, 10 }, 10, 0), nullptr);
    ASSERT_NE(AddPath({ 20, 20 }, 10, 0), nullptr);

    MapPathRouteCache::Prepare(std::array{ first, first });
    const auto deduplicated = MapPathRouteCache::GetSingleTargetForRide(ride);
    ASSERT_TRUE(deduplicated.has_value());
    EXPECT_EQ(deduplicated->location, first.location);

    MapPathRouteCache::Prepare(std::array{ first, second });
    EXPECT_FALSE(MapPathRouteCache::GetSingleTargetForRide(ride).has_value());

    const auto unseededRide = RideId::FromUnderlying(99);
    const auto unseeded = MapPathRouteCache::RouteTarget{ { 30, 30, 10 }, unseededRide };
    MapPathRouteCache::Prepare(std::array{ unseeded });
    EXPECT_FALSE(MapPathRouteCache::GetSingleTargetForRide(unseededRide).has_value());
}

TEST_F(MapPathTopologyTest, UnchangedChunksReuseStorageAndMapResetDropsWarmData)
{
    const auto sourceTile = TileCoordsXY{ 4, 4 };
    ASSERT_NE(AddPath(sourceTile, 10, 0), nullptr);

    const auto initial = MapPathTopology::GetChunk(sourceTile);
    const auto warmHit = MapPathTopology::GetChunk(sourceTile);
    EXPECT_EQ(warmHit.buildSerial, initial.buildSerial);
    EXPECT_EQ(warmHit.paths.data(), initial.paths.data());

    ASSERT_NE(AddPath({ 40, 10 }, 10, 0), nullptr);
    const auto unrelatedEdit = MapPathTopology::GetChunk(sourceTile);
    EXPECT_EQ(unrelatedEdit.buildSerial, initial.buildSerial);
    EXPECT_EQ(unrelatedEdit.paths.data(), initial.paths.data());

    MapInit({ 70, 40 });
    const auto afterReset = MapPathTopology::GetChunk(sourceTile);
    EXPECT_NE(afterReset.buildSerial, initial.buildSerial);
    EXPECT_TRUE(afterReset.paths.empty());
}
