/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <gtest/gtest.h>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <openrct2/world/Scenery.h>
#include <openrct2/world/tile_element/EntranceElement.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <openrct2/world/tile_element/TrackElement.h>

using namespace OpenRCT2;

TEST(MapPresentationSnapshotTest, CopyOnWriteUpdateDoesNotMutatePublishedSnapshot)
{
    TileElement first;
    first.ClearAs(TileElementType::surface);
    first.baseHeight = 10;
    first.clearanceHeight = 10;
    first.setLastForTile(true);

    MapPresentationChangeBatch initial{ .epoch = 1, .reset = true };
    initial.changes.push_back({ 0, { first } });
    MapPresentationSnapshot published;
    published.Apply(initial);

    auto next = published;
    auto second = first;
    second.baseHeight = 20;
    second.clearanceHeight = 20;
    MapPresentationChangeBatch update{ .epoch = 1, .reset = false };
    update.changes.push_back({ 0, { second } });
    next.Apply(update);

    ASSERT_NE(published.GetFirstElementAt({ 0, 0 }), nullptr);
    ASSERT_NE(next.GetFirstElementAt({ 0, 0 }), nullptr);
    EXPECT_EQ(published.GetFirstElementAt({ 0, 0 })->baseHeight, 10);
    EXPECT_EQ(next.GetFirstElementAt({ 0, 0 })->baseHeight, 20);
}

TEST(MapPresentationSnapshotTest, StoresTheFinalPartialTechnicalMapChunk)
{
    TileElement element;
    element.ClearAs(TileElementType::surface);
    element.baseHeight = 12;
    element.clearanceHeight = 12;
    element.setLastForTile(true);

    constexpr uint32_t finalIndex = kMaximumMapSizeTechnical * kMaximumMapSizeTechnical - 1;
    MapPresentationChangeBatch batch{ .epoch = 1, .reset = true };
    batch.changes.push_back({ finalIndex, { element } });

    MapPresentationSnapshot snapshot;
    snapshot.Apply(batch);

    const TileCoordsXY finalTile{ kMaximumMapSizeTechnical - 1, kMaximumMapSizeTechnical - 1 };
    ASSERT_NE(snapshot.GetFirstElementAt(finalTile), nullptr);
    EXPECT_EQ(snapshot.GetFirstElementAt(finalTile)->baseHeight, 12);
}

TEST(MapPresentationSnapshotTest, SurfaceChunksAreImmutableAndRevisioned)
{
    TileElement element;
    element.ClearAs(TileElementType::surface);
    element.setLastForTile(true);
    SurfacePresentationRecord surface;
    surface.valid = 1;
    surface.baseZ = 48;
    surface.detailedImages[0] = ImageId(1234);

    MapPresentationChangeBatch initial{
        .epoch = 7,
        .reset = true,
        .surfaceWidth = 1,
        .surfaceHeight = 1,
    };
    initial.changes.push_back({ 0, { element }, surface, 0 });
    MapPresentationSnapshot published;
    published.Apply(initial);

    auto next = published;
    surface.baseZ = 64;
    surface.detailedImages[0] = ImageId(5678);
    MapPresentationChangeBatch update{ .epoch = 7, .surfaceWidth = 1, .surfaceHeight = 1 };
    update.changes.push_back({ 0, { element }, surface, 0 });
    next.Apply(update);

    const auto& oldChunk = published.GetSurfaceChunks()[0];
    const auto& newChunk = next.GetSurfaceChunks()[0];
    ASSERT_NE(oldChunk, nullptr);
    ASSERT_NE(newChunk, nullptr);
    EXPECT_NE(oldChunk, newChunk);
    EXPECT_NE(oldChunk->revision, newChunk->revision);
    EXPECT_EQ(oldChunk->records[0].baseZ, 48);
    EXPECT_EQ(oldChunk->records[0].detailedImages[0].GetIndex(), 1234u);
    EXPECT_EQ(newChunk->records[0].baseZ, 64);
    EXPECT_EQ(newChunk->records[0].detailedImages[0].GetIndex(), 5678u);
}

TEST(MapPresentationSnapshotTest, SurfacePublicationUsesDenseActiveMapIndicesAndDynamicChunks)
{
    TileElement element;
    element.ClearAs(TileElementType::surface);
    element.setLastForTile(true);
    SurfacePresentationRecord surface;
    surface.valid = 1;
    surface.baseZ = 72;

    constexpr uint32_t width = 257;
    constexpr uint32_t height = 2;
    constexpr uint32_t technicalIndex = kMaximumMapSizeTechnical;
    constexpr uint32_t denseIndex = width;
    MapPresentationChangeBatch batch{
        .epoch = 9,
        .reset = true,
        .surfaceWidth = width,
        .surfaceHeight = height,
    };
    batch.changes.push_back({ technicalIndex, { element }, surface, denseIndex });

    MapPresentationSnapshot snapshot;
    snapshot.Apply(batch);

    EXPECT_EQ(snapshot.GetSurfaceWidth(), width);
    EXPECT_EQ(snapshot.GetSurfaceHeight(), height);
    EXPECT_EQ(snapshot.GetSurfaceRecordCount(), width * height);
    ASSERT_EQ(snapshot.GetSurfaceChunks().size(), 3u);
    ASSERT_NE(snapshot.GetSurfaceChunks()[1], nullptr);
    EXPECT_EQ(snapshot.GetSurfaceChunks()[1]->records[1].baseZ, 72);
    ASSERT_NE(snapshot.GetFirstElementAt({ 0, 1 }), nullptr);
}

TEST(MapPresentationSnapshotTest, MixedOrNonUniformSurfacePublicationCannotDrawSurfaceBaseIndependently)
{
    TileElement element;
    element.ClearAs(TileElementType::surface);
    element.setLastForTile(true);
    SurfacePresentationRecord surface;
    surface.valid = 1;
    surface.baseZ = 48;

    MapPresentationChangeBatch flat{
        .epoch = 11,
        .reset = true,
        .surfaceWidth = 2,
        .surfaceHeight = 1,
    };
    flat.changes.push_back({ 0, { element }, surface, 0 });
    flat.changes.push_back({ 1, { element }, surface, 1 });
    MapPresentationSnapshot snapshot;
    snapshot.Apply(flat);
    EXPECT_TRUE(snapshot.CanDrawSurfaceBaseIndependently());

    surface.baseZ = 64;
    MapPresentationChangeBatch heightChange{
        .epoch = 11,
        .surfaceWidth = 2,
        .surfaceHeight = 1,
    };
    heightChange.changes.push_back({ 1, { element }, surface, 1 });
    snapshot.Apply(heightChange);
    EXPECT_FALSE(snapshot.CanDrawSurfaceBaseIndependently());

    surface.baseZ = 48;
    MapPresentationChangeBatch heightRestored{
        .epoch = 11,
        .surfaceWidth = 2,
        .surfaceHeight = 1,
    };
    heightRestored.changes.push_back({ 1, { element }, surface, 1 });
    snapshot.Apply(heightRestored);
    EXPECT_TRUE(snapshot.CanDrawSurfaceBaseIndependently());

    surface.requiresCategoryInterleaving = 1;
    MapPresentationChangeBatch mixed{
        .epoch = 12,
        .reset = true,
        .surfaceWidth = 1,
        .surfaceHeight = 1,
    };
    mixed.changes.push_back({ 0, { element }, surface, 0 });
    snapshot.Apply(mixed);
    EXPECT_FALSE(snapshot.CanDrawSurfaceBaseIndependently());
}

class TileElementWantsFootpathConnection : public testing::Test
{
protected:
    static void SetUpTestCase()
    {
        std::string parkPath = TestData::GetParkPath("tile-element-tests.sv6");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        _context = CreateContext();
        bool initialised = _context->Initialise();
        ASSERT_TRUE(initialised);

        GetContext()->LoadParkFromFile(parkPath);
        GameLoadInit(); // NB: calls `setActiveScene`

        // Changed in some tests. Store to restore its value
        _gLegacyScene = gLegacyScene;
        SUCCEED();
    }

    static void TearDownTestCase()
    {
        if (_context)
            _context.reset();

        gLegacyScene = _gLegacyScene;
    }

private:
    static std::shared_ptr<IContext> _context;
    static LegacyScene _gLegacyScene;
};

std::shared_ptr<IContext> TileElementWantsFootpathConnection::_context;
LegacyScene TileElementWantsFootpathConnection::_gLegacyScene;

TEST_F(TileElementWantsFootpathConnection, TemporaryMapStashRestoresPresentationEpoch)
{
    const auto publishedEpoch = GetMapPresentationEpoch();
    auto temporaryMap = GetTileElements();

    StashMap();
    auto& gameState = getGameState();
    SetTileElements(gameState, std::move(temporaryMap));
    EXPECT_NE(GetMapPresentationEpoch(), publishedEpoch);
    UnstashMap();

    EXPECT_EQ(GetMapPresentationEpoch(), publishedEpoch);
}

TEST_F(TileElementWantsFootpathConnection, FlatPath)
{
    // Flat paths want to connect to other paths in any direction
    const auto* pathElement = MapGetFootpathElement(TileCoordsXYZ{ 19, 18, 14 }.ToCoordsXYZ());
    ASSERT_NE(pathElement, nullptr);
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 19, 18, 14, 0 }, nullptr));
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 19, 18, 14, 1 }, nullptr));
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 19, 18, 14, 2 }, nullptr));
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 19, 18, 14, 3 }, nullptr));
    SUCCEED();
}

TEST_F(TileElementWantsFootpathConnection, SlopedPath)
{
    // Sloped paths only want to connect in two directions, of which is one at a higher offset
    const auto* slopedPathElement = MapGetFootpathElement(TileCoordsXYZ{ 18, 18, 14 }.ToCoordsXYZ());
    ASSERT_NE(slopedPathElement, nullptr);
    ASSERT_TRUE(slopedPathElement->asPath()->IsSloped());
    // Bottom and top of sloped path want a path connection
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 18, 18, 14, 2 }, nullptr));
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 18, 18, 16, 0 }, nullptr));
    // Other directions at both heights do not want a connection
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 14, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 14, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 14, 3 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 16, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 16, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 16, 3 }, nullptr));
    SUCCEED();
}

TEST_F(TileElementWantsFootpathConnection, Stall)
{
    // Stalls usually have one path direction flag, but can have multiple (info kiosk for example)
    auto tileCoords = TileCoordsXYZ{ 19, 15, 14 };
    const TrackElement* const stallElement = MapGetTrackElementAt(tileCoords.ToCoordsXYZ());
    ASSERT_NE(stallElement, nullptr);
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 19, 15, 14, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 15, 14, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 15, 14, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 15, 14, 3 }, nullptr));
    SUCCEED();
}

TEST_F(TileElementWantsFootpathConnection, RideEntrance)
{
    // Ride entrances and exits want a connection in one direction
    const EntranceElement* const entranceElement = MapGetRideEntranceElementAt(TileCoordsXYZ{ 18, 8, 14 }.ToCoordsXYZ(), false);
    ASSERT_NE(entranceElement, nullptr);
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 18, 8, 14, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 8, 14, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 8, 14, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 8, 14, 3 }, nullptr));
    SUCCEED();
}

TEST_F(TileElementWantsFootpathConnection, RideExit)
{
    // The exit has been rotated; it wants a path connection in direction 1, but not 0 like the entrance
    const EntranceElement* const exitElement = MapGetRideExitElementAt(TileCoordsXYZ{ 18, 10, 14 }.ToCoordsXYZ(), false);
    ASSERT_NE(exitElement, nullptr);
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 10, 14, 0 }, nullptr));
    EXPECT_TRUE(TileElementWantsPathConnectionTowards({ 18, 10, 14, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 10, 14, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 10, 14, 3 }, nullptr));
    SUCCEED();
}

TEST_F(TileElementWantsFootpathConnection, DifferentHeight)
{
    // Test at different heights, all of these should fail
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 4, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 4, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 4, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 4, 3 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 4, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 6, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 15, 4, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 8, 4, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 10, 4, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 24, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 24, 1 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 24, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 18, 24, 3 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 24, 2 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 18, 26, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 19, 15, 24, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 8, 24, 0 }, nullptr));
    EXPECT_FALSE(TileElementWantsPathConnectionTowards({ 18, 10, 24, 1 }, nullptr));
    SUCCEED();
}

TEST_F(TileElementWantsFootpathConnection, MapEdge)
{
    // Paths at the map edge should have edge flags turned on when placed in scenario editor
    // This tile is a single, unconnected footpath on the map edge - on load, GetEdges() returns 0
    auto* pathElement = MapGetFootpathElement(TileCoordsXYZ{ 1, 4, 14 }.ToCoordsXYZ());

    gLegacyScene = LegacyScene::scenarioEditor;

    // Calculate the connected edges and set the appropriate edge flags
    // FIXME: The footpath functions should only take PathElement and not TileElement.
    FootpathConnectEdges({ 16, 64 }, pathElement->as<TileElement>(), {});
    auto edges = pathElement->GetEdges();

    // The tiles alongside in the Y direction are both on the map edge so should be marked as an edge
    EXPECT_TRUE(edges & (1 << 1));
    EXPECT_TRUE(edges & (1 << 3));

    // The tile in the -X direction is off the map so should be marked as an edge
    EXPECT_TRUE(edges & (1 << 0));

    // The tile in the -X direction is a normal tile and should not be marked as an edge
    EXPECT_FALSE(edges & (1 << 2));
}

TEST_F(TileElementWantsFootpathConnection, MowedGrassCountsAsDecoration)
{
    SurfaceElement* surfaceElement = nullptr;
    const auto& gameState = getGameState();
    for (int32_t y = 1; y < gameState.mapSize.y - 1 && surfaceElement == nullptr; y++)
    {
        for (int32_t x = 1; x < gameState.mapSize.x - 1; x++)
        {
            auto* candidate = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
            if (candidate != nullptr && candidate->CanGrassGrow())
            {
                surfaceElement = candidate;
                break;
            }
        }
    }

    ASSERT_NE(surfaceElement, nullptr);

    const auto originalWaterHeight = surfaceElement->GetWaterHeight();
    const auto originalGrassLength = surfaceElement->GetGrassLength();
    const auto& tileElement = *surfaceElement->as<TileElement>();

    surfaceElement->SetWaterHeight(0);
    surfaceElement->SetGrassLength(GRASS_LENGTH_CLEAR_1);
    EXPECT_FALSE(TileElementCountsAsDecoration(tileElement));

    surfaceElement->SetGrassLength(GRASS_LENGTH_MOWED);
    EXPECT_TRUE(TileElementCountsAsDecoration(tileElement));

    surfaceElement->SetGrassLength(GRASS_LENGTH_CLEAR_0);
    EXPECT_FALSE(TileElementCountsAsDecoration(tileElement));

    surfaceElement->SetGrassLength(originalGrassLength);
    surfaceElement->SetWaterHeight(originalWaterHeight);
}

TEST_F(TileElementWantsFootpathConnection, WaterCountsAsDecoration)
{
    SurfaceElement* surfaceElement = nullptr;
    const auto& gameState = getGameState();
    for (int32_t y = 1; y < gameState.mapSize.y - 1 && surfaceElement == nullptr; y++)
    {
        for (int32_t x = 1; x < gameState.mapSize.x - 1; x++)
        {
            auto* candidate = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
            if (candidate != nullptr)
            {
                surfaceElement = candidate;
                break;
            }
        }
    }

    ASSERT_NE(surfaceElement, nullptr);

    const auto originalWaterHeight = surfaceElement->GetWaterHeight();
    const auto originalGrassLength = surfaceElement->GetGrassLength();
    const auto& tileElement = *surfaceElement->as<TileElement>();

    surfaceElement->SetGrassLength(GRASS_LENGTH_CLEAR_0);
    surfaceElement->SetWaterHeight(0);
    EXPECT_FALSE(TileElementCountsAsDecoration(tileElement));

    surfaceElement->SetWaterHeight(surfaceElement->getBaseZ() + (2 * kCoordsZStep));
    EXPECT_TRUE(TileElementCountsAsDecoration(tileElement));
    EXPECT_EQ(TileElementGetDecorationScore(tileElement), 18);

    surfaceElement->SetWaterHeight(originalWaterHeight);
    surfaceElement->SetGrassLength(originalGrassLength);
}
