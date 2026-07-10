/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPathTopology.h>
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
