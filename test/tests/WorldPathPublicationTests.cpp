/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include <gtest/gtest.h>
#include <openrct2/Cheats.h>
#include <openrct2/Context.h>
#include <openrct2/Date.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/actions/cheats/CheatSetAction.h>
#include <openrct2/core/JobPool.h>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/object/FootpathSurfaceObject.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/SmallSceneryObject.h>
#include <openrct2/object/StationObject.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <openrct2/world/Park.h>
#include <openrct2/world/tile_element/EntranceElement.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <openrct2/world/tile_element/TrackElement.h>
#include <openrct2/world/tile_element/WallElement.h>

using namespace OpenRCT2;

namespace
{
    class WorldPathPublicationTest : public testing::Test
    {
    protected:
        bool oldHeadless = gOpenRCT2Headless;
        bool oldNoGraphics = gOpenRCT2NoGraphics;
        std::unique_ptr<IContext> context;
        void SetUp() override
        {
            gOpenRCT2Headless = true;
            // Catalog lifetime test needs real object image allocations, but creates no drawing engine/device.
            gOpenRCT2NoGraphics = false;
            context = CreateContext();
            ASSERT_TRUE(context->Initialise());
            MapInit({ 16, 16 });
        }
        void TearDown() override
        {
            context.reset();
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
        }
        MapPresentationChangeBatch Capture(bool complete = false)
        {
            return ConsumeMapPresentationChanges(complete, MapPublicationProfile::rawTerrain);
        }
    };
} // namespace

TEST_F(WorldPathPublicationTest, NativeCapturePreservesStackOrderAllFieldsAndNoLegacyTiles)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    std::vector<TileElement> elements{ surface };
    // More paths than a small fixed per-tile GPU/producer cap could admit, deliberately unsorted heights.
    for (uint32_t i = 0; i < 40; ++i)
    {
        TileElement element{};
        element.clearAs(TileElementType::path);
        element.setBaseZ(32 + (39 - i) * 8);
        element.setClearanceZ(40 + (39 - i) * 8);
        element.setLastForTile(i == 39);
        auto& path = *element.asPath();
        path.setSurfaceEntryIndex(4);
        path.setRailingsEntryIndex(7);
        path.setAdditionEntryIndex(2);
        path.setEdgesAndCorners(static_cast<uint8_t>(i));
        path.setAdditionStatus(static_cast<uint8_t>(255 - i));
        path.setSloped(true);
        path.setSlopeDirection(3);
        path.setIsBroken(true);
        path.setAdditionIsGhost(true);
        path.setJunctionRailings(true);
        elements.push_back(element);
    }
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, elements), TileMutationStatus::ok);
    const auto batch = Capture(true);
    for (const auto& change : batch.changes)
        EXPECT_EQ(change.elements.capacity(), 0u);
    MapPresentationSnapshot snapshot;
    snapshot.Apply(batch);
    EXPECT_FALSE(snapshot.HasLegacyTileStorage());
    ASSERT_EQ(snapshot.GetPathChunks().size(), 1u);
    const auto& chunk = snapshot.GetPathChunks()[0];
    ASSERT_NE(chunk, nullptr);
    const auto range = chunk->tiles[34];
    ASSERT_EQ(range.count, 40u);
    for (uint32_t i = 0; i < range.count; ++i)
    {
        const auto& path = chunk->records[range.first + i];
        EXPECT_EQ(path.elementOrdinal, i + 1);
        EXPECT_EQ(path.baseZ, static_cast<int32_t>(32 + (39 - i) * 8));
        EXPECT_EQ(path.clearanceZ, path.baseZ + 8);
        EXPECT_EQ(path.surfaceSlot, 4);
        EXPECT_EQ(path.railingsSlot, 7);
        EXPECT_EQ(path.additionSlot, 2);
        EXPECT_EQ(path.edgesAndCorners, i);
        EXPECT_EQ(path.additionStatus, 255 - i);
        EXPECT_EQ(path.slopeDirection, 3);
        EXPECT_EQ(
            path.flags,
            PathPresentationFlags::sloped | PathPresentationFlags::broken | PathPresentationFlags::additionGhost
                | PathPresentationFlags::junctionRailings);
    }
}

TEST_F(WorldPathPublicationTest, ChangedListsShrinkDeleteReuseWhileHeldChunksAndUnchangedTilesStayValid)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement path{};
    path.clearAs(TileElementType::path);
    path.setBaseZ(32);
    path.setClearanceZ(40);
    path.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, path }), TileMutationStatus::ok);
    MapPresentationSnapshot current;
    current.Apply(Capture(true));
    const auto held = current;
    const auto oldChunk = held.GetPathChunks()[0];
    ASSERT_NE(oldChunk, nullptr);
    // Surface-only dirty notifications do not allocate or advance a path chunk.
    MarkMapTilePresentationDirty({ 64, 64 });
    current.Apply(Capture());
    EXPECT_EQ(current.GetPathChunks()[0], oldChunk);
    auto* live = (MapGetFirstElementAt(TileCoordsXY{ 2, 2 }) + 1)->asPath();
    live->setAdditionEntryIndex(3);
    live->setAdditionStatus(0x51);
    live->setIsBroken(true);
    MapInvalidateTileFull({ 64, 64 });
    current.Apply(Capture());
    const auto changed = current.GetPathChunks()[0];
    ASSERT_NE(changed, oldChunk);
    EXPECT_GT(changed->revision, oldChunk->revision);
    EXPECT_EQ(changed->records[changed->tiles[34].first].additionStatus, 0x51);
    EXPECT_EQ(oldChunk->records[oldChunk->tiles[34].first].additionSlot, UINT16_MAX);
    surface.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface }), TileMutationStatus::ok);
    current.Apply(Capture());
    const auto empty = current.GetPathChunks()[0];
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->tiles[34].count, 0u);
    EXPECT_TRUE(empty->records.empty());
    EXPECT_GT(empty->revision, changed->revision);
    surface.setLastForTile(false);
    path.setBaseZ(160);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, path }), TileMutationStatus::ok);
    current.Apply(Capture());
    const auto reused = current.GetPathChunks()[0];
    EXPECT_GT(reused->revision, empty->revision);
    EXPECT_EQ(reused->records[reused->tiles[34].first].baseZ, 160);
    EXPECT_EQ(oldChunk->records[oldChunk->tiles[34].first].baseZ, 32);
    ++getGameState().currentTicks;
    current.Apply(Capture());
    EXPECT_EQ(current.GetPathChunks()[0], reused);
}

TEST_F(WorldPathPublicationTest, CatalogHeldAcrossUnloadReloadDoesNotAliasAllocationLifetime)
{
    auto& manager = context->GetObjectManager();
    const auto before = GetPathObjectRevision();
    auto* object = manager.LoadObject("rct2.footpath_surface.tarmac");
    ASSERT_NE(object, nullptr);
    EXPECT_GT(GetPathObjectRevision(), before);
    const auto slot = manager.GetLoadedObjectEntryIndex(object);
    const auto descriptor = object->GetDescriptor();
    auto first = Capture(true);
    const auto held = first.pathMaterials;
    ASSERT_NE(held, nullptr);
    ASSERT_LT(slot, held->surfaces.size());
    const auto old = held->surfaces[slot];
    ASSERT_TRUE(old.present);
    ASSERT_GT(old.imageCount, 0u);
    EXPECT_EQ(Capture().pathMaterials, held);
    manager.UnloadObjects({ descriptor });
    const auto unloaded = Capture().pathMaterials;
    EXPECT_GT(unloaded->revision, held->revision);
    EXPECT_FALSE(unloaded->surfaces[slot].present);
    ASSERT_NE(manager.LoadObject(descriptor, slot), nullptr);
    const auto replacement = Capture().pathMaterials;
    EXPECT_GT(replacement->revision, unloaded->revision);
    EXPECT_TRUE(replacement->surfaces[slot].present);
    // The allocator may reuse the numeric image range. Catalog lifetime is the revision, never that address.
    EXPECT_EQ(held->surfaces[slot].imageBase, old.imageBase);
    EXPECT_EQ(held->surfaces[slot].surfaceImage, old.surfaceImage);
    EXPECT_EQ(held->surfaces[slot].imageCount, old.imageCount);
    const auto priorReset = replacement->revision;
    manager.ResetObjects();
    EXPECT_GT(Capture().pathMaterials->revision, priorReset);
}

TEST_F(WorldPathPublicationTest, AsyncPathsUseCapturedTickAndProfileSwitchPreservesHeldGeneration)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement path{};
    path.clearAs(TileElementType::path);
    path.setBaseZ(32);
    path.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, path }), TileMutationStatus::ok);
    auto& jobs = context->GetJobPool();
    auto& state = getGameState();
    PresentationScene scene;
    state.currentTicks = 700;
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto held = scene.GetGeneration();
    auto* live = (MapGetFirstElementAt(TileCoordsXY{ 2, 2 }) + 1)->asPath();
    live->setEdgesAndCorners(0xAF);
    MapInvalidateTileFull({ 64, 64 });
    state.currentTicks = 701;
    scene.ScheduleNext(jobs, state.entities);
    live->setEdgesAndCorners(0x31);
    MapInvalidateTileFull({ 64, 64 });
    state.currentTicks = 702;
    jobs.Join();
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 2, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto captured = scene.GetGeneration();
    EXPECT_EQ(captured->sourceTick, 701u);
    EXPECT_EQ(captured->map->GetSourceTick(), 701u);
    EXPECT_EQ(captured->entities->GetSourceTick(), 701u);
    const auto& chunk = captured->map->GetPathChunks()[0];
    EXPECT_EQ(chunk->records[chunk->tiles[34].first].edgesAndCorners, 0xAF);
    const auto& old = held->map->GetPathChunks()[0];
    EXPECT_EQ(old->records[old->tiles[34].first].edgesAndCorners, 0);
    EXPECT_FALSE(captured->map->HasLegacyTileStorage());
    EXPECT_FALSE(captured->entities->HasLegacyStorage());
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 3, true, EntityPublicationProfile::legacyBulk));
    EXPECT_TRUE(scene.GetGeneration()->map->HasLegacyTileStorage());
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 4, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto& latest = scene.GetGeneration()->map->GetPathChunks()[0];
    EXPECT_EQ(latest->records[latest->tiles[34].first].edgesAndCorners, 0x31);
    EXPECT_FALSE(scene.GetGeneration()->map->HasLegacyTileStorage());
    EXPECT_EQ(old->records[old->tiles[34].first].edgesAndCorners, 0);
    scene.Reset(jobs);
}

TEST_F(WorldPathPublicationTest, RepairVandalismNotifiesOnlyMutatedTiles)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement path{};
    path.clearAs(TileElementType::path);
    path.setLastForTile(true);
    path.asPath()->setAdditionEntryIndex(0);
    path.asPath()->setIsBroken(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, path }), TileMutationStatus::ok);
    static_cast<void>(Capture(true));
    auto& state = getGameState();
    const GameActions::CheatSetAction repair(CheatType::fixVandalism);
    static_cast<void>(repair.Execute(state, state.park));
    const auto changes = Capture();
    ASSERT_EQ(changes.changes.size(), 1u);
    ASSERT_EQ(changes.changes[0].paths.size(), 1u);
    EXPECT_EQ(changes.changes[0].paths[0].flags & PathPresentationFlags::broken, 0u);
    static_cast<void>(repair.Execute(state, state.park));
    EXPECT_TRUE(Capture().changes.empty());
}

TEST_F(WorldPathPublicationTest, ProgressDrawDuringObjectMutationDefersCaptureAndSameDrawCanResume)
{
    auto& jobs = context->GetJobPool();
    auto& state = getGameState();
    PresentationScene scene;
    PresentationScene cold;
    state.currentTicks = 900;
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 5, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto held = scene.GetGeneration();
    const auto heldCatalog = held->map->GetPathMaterials();
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement path{};
    path.clearAs(TileElementType::path);
    path.setBaseZ(64);
    path.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, path }), TileMutationStatus::ok);
    state.currentTicks = 901;
    {
        // The same scope state used by ObjectManager::WorldMaterialMutation while SetProgress can draw.
        struct ObjectMutationScope
        {
            ObjectMutationScope()
            {
                AdvancePathObjectRevision();
                gPathObjectMutationDepth.fetch_add(1, std::memory_order_acq_rel);
            }
            ~ObjectMutationScope()
            {
                AdvancePathObjectRevision();
                gPathObjectMutationDepth.fetch_sub(1, std::memory_order_release);
            }
        } mutation;
        EXPECT_EQ(scene.GetGeneration(), nullptr);
        EXPECT_EQ(cold.GetGeneration(), nullptr);
        EXPECT_FALSE(scene.BeginFrame(jobs, state.entities, 5, true, EntityPublicationProfile::gpuTerrainOnly));
        EXPECT_FALSE(cold.BeginFrame(jobs, state.entities, 5, true, EntityPublicationProfile::gpuTerrainOnly));
        EXPECT_NO_THROW(scene.ScheduleNext(jobs, state.entities));
        EXPECT_NO_THROW(cold.ScheduleNext(jobs, state.entities));
        EXPECT_THROW(static_cast<void>(Capture()), std::logic_error);
        EXPECT_EQ(held->sourceTick, 900u);
        EXPECT_EQ(held->map->GetPathMaterials(), heldCatalog);
    }
    // Capture after the guard proves progress drawing did not acknowledge the authoritative tile worklist.
    const auto pending = Capture();
    ASSERT_EQ(pending.changes.size(), 1u);
    ASSERT_EQ(pending.changes[0].paths.size(), 1u);
    EXPECT_EQ(pending.changes[0].paths[0].baseZ, 64);
    MarkMapTilePresentationDirty({ 64, 64 });
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 5, false, EntityPublicationProfile::gpuTerrainOnly));
    EXPECT_EQ(scene.GetGeneration()->sourceTick, 901u);
    EXPECT_EQ(scene.GetGeneration()->map->GetPathMaterials()->revision, GetPathObjectRevision());
    ASSERT_NE(scene.GetGeneration()->map->GetPathChunks()[0], nullptr);
    EXPECT_EQ(scene.GetGeneration()->map->GetPathChunks()[0]->tiles[34].count, 1u);
    EXPECT_EQ(held->map->GetPathMaterials(), heldCatalog);
    ASSERT_TRUE(cold.BeginFrame(jobs, state.entities, 5, false, EntityPublicationProfile::gpuTerrainOnly));
    EXPECT_EQ(cold.GetGeneration()->sourceTick, 901u);
    EXPECT_EQ(cold.GetGeneration()->map->GetPathChunks()[0]->tiles[34].count, 1u);
    scene.Reset(jobs);
    cold.Reset(jobs);
}

TEST_F(WorldPathPublicationTest, RawWorldObjectsPreserveOrderGhostsAndHeldDeletionWithoutLegacyTiles)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement tree{};
    tree.clearAs(TileElementType::smallScenery);
    tree.setBaseZ(32);
    tree.setClearanceZ(96);
    tree.setGhost(true);
    tree.asSmallScenery()->setEntryIndex(7);
    tree.asSmallScenery()->setAge(42);
    tree.asSmallScenery()->setPrimaryColour(static_cast<Drawing::Colour>(12));
    TileElement track{};
    track.clearAs(TileElementType::track);
    track.setBaseZ(32);
    track.setClearanceZ(64);
    track.setLastForTile(true);
    track.asTrack()->setRideIndex(RideId::FromUnderlying(2));
    track.asTrack()->setHasChain(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, tree, track }), TileMutationStatus::ok);
    MapPresentationSnapshot current;
    current.Apply(Capture(true));
    ASSERT_FALSE(current.HasLegacyTileStorage());
    ASSERT_EQ(current.GetObjectChunks().size(), 1u);
    const auto held = current.GetObjectChunks()[0];
    ASSERT_NE(held, nullptr);
    const auto range = held->tiles[34];
    ASSERT_EQ(range.count, 2u);
    const auto& rawTree = held->records[range.first];
    EXPECT_EQ(rawTree.kind, WorldObjectKind::smallScenery);
    EXPECT_EQ(rawTree.elementOrdinal, 1u);
    EXPECT_EQ(rawTree.objectSlot, 7);
    EXPECT_EQ(rawTree.age, 42);
    EXPECT_EQ(rawTree.primaryColour, 12);
    EXPECT_NE(rawTree.flags & WorldObjectPresentationFlags::ghost, 0u);
    const auto& rawTrack = held->records[range.first + 1];
    EXPECT_EQ(rawTrack.kind, WorldObjectKind::track);
    EXPECT_EQ(rawTrack.elementOrdinal, 2u);
    EXPECT_EQ(rawTrack.rideId, 2);
    EXPECT_NE(rawTrack.flags & WorldObjectPresentationFlags::chain, 0u);
    MarkMapTilePresentationDirty({ 64, 64 });
    current.Apply(Capture());
    EXPECT_EQ(current.GetObjectChunks()[0], held);
    surface.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface }), TileMutationStatus::ok);
    current.Apply(Capture());
    EXPECT_EQ(current.GetObjectChunks()[0]->tiles[34].count, 0u);
    EXPECT_GT(current.GetObjectChunks()[0]->revision, held->revision);
    EXPECT_EQ(held->records.size(), 2u);
    surface.setLastForTile(false);
    tree.setGhost(false);
    tree.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, tree }), TileMutationStatus::ok);
    current.Apply(Capture());
    EXPECT_EQ(current.GetObjectChunks()[0]->tiles[34].count, 1u);
    EXPECT_EQ(current.GetObjectChunks()[0]->records[0].flags & WorldObjectPresentationFlags::ghost, 0u);
}

TEST_F(WorldPathPublicationTest, WorldCatalogAndRideFactsOwnOldValuesAcrossReplacement)
{
    auto& manager = context->GetObjectManager();
    auto* tree = manager.LoadObject("rct2.scenery_small.tl0");
    ASSERT_NE(tree, nullptr);
    const auto slot = manager.GetLoadedObjectEntryIndex(tree);
    auto initial = Capture(true);
    ASSERT_TRUE(initial.objectMaterials->smallScenery[slot].present);
    const auto held = initial.objectMaterials;
    const auto base = held->smallScenery[slot].imageBase;
    auto& state = getGameState();
    state.ridesEndOfUsedRange = 1;
    auto& ride = state.rides[0];
    ride.id = RideId::FromUnderlying(0);
    ride.type = 0;
    ride.trackColours[0].main = static_cast<Drawing::Colour>(12);
    ride.vehicleColours[3].Body = static_cast<Drawing::Colour>(23);
    auto& highStation = ride.getStation(StationIndex::FromUnderlying(200));
    highStation.setEntrance(TileCoordsXYZD{ 8, 9, 10, 3 });
    auto first = Capture();
    ASSERT_EQ(first.rideMaterials->rides.size(), 1u);
    EXPECT_EQ(Capture().rideMaterials, first.rideMaterials);
    ASSERT_GT(first.rideMaterials->rides[0].stations.size(), 200u);
    EXPECT_EQ(first.rideMaterials->rides[0].vehicleColours[3].body, 23);
    EXPECT_TRUE(first.rideMaterials->rides[0].stations[200].entranceValid);
    EXPECT_EQ(first.rideMaterials->rides[0].stations[200].entranceX, 8);
    // Simulation ticks and colour schemes unused by static bodies must not republish the catalogue.
    ++state.currentTicks;
    ride.vehicleColours[200].Body = static_cast<Drawing::Colour>(25);
    EXPECT_EQ(Capture().rideMaterials, first.rideMaterials);
    auto movedEntrance = highStation.getEntrance();
    movedEntrance.x = 11;
    highStation.setEntrance(movedEntrance);
    ride.vehicleColours[3].Body = static_cast<Drawing::Colour>(24);
    ride.trackColours[0].main = static_cast<Drawing::Colour>(14);
    auto recoloured = Capture();
    EXPECT_NE(recoloured.rideMaterials, first.rideMaterials);
    EXPECT_EQ(first.rideMaterials->rides[0].trackColours[0].main, 12);
    EXPECT_EQ(recoloured.rideMaterials->rides[0].trackColours[0].main, 14);
    EXPECT_EQ(recoloured.rideMaterials->rides[0].stations[200].entranceX, 11);
    EXPECT_EQ(recoloured.rideMaterials->rides[0].vehicleColours[3].body, 24);
    EXPECT_EQ(first.rideMaterials->rides[0].stations[200].entranceX, 8);
    EXPECT_EQ(first.rideMaterials->rides[0].vehicleColours[3].body, 23);
    manager.UnloadObjects({ tree->GetDescriptor() });
    auto removed = Capture();
    EXPECT_GT(removed.objectMaterials->revision, held->revision);
    EXPECT_FALSE(removed.objectMaterials->smallScenery[slot].present);
    EXPECT_TRUE(held->smallScenery[slot].present);
    EXPECT_EQ(held->smallScenery[slot].imageBase, base);
}

TEST_F(WorldPathPublicationTest, RideFactsFastPathDetectsHighStationChangesRemovalAndReuse)
{
    auto& state = getGameState();
    state.ridesEndOfUsedRange = 1;
    auto& ride = state.rides[0];
    ride.id = RideId::FromUnderlying(0);
    ride.type = 0;
    auto& station = ride.getStation(StationIndex::FromUnderlying(254));
    station.setStart({ 64, 96 });
    station.setBaseZ(80);
    station.setEntrance(TileCoordsXYZD{ 2, 3, 10, 0 });
    station.setExit(TileCoordsXYZD{ 4, 5, 10, 0 });
    auto first = Capture(true).rideMaterials;
    ASSERT_EQ(first->rides[0].stations.size(), 255u);
    EXPECT_EQ(Capture().rideMaterials, first);
    auto previous = first;
    const auto changed = [&]() {
        auto next = Capture().rideMaterials;
        EXPECT_NE(next, previous);
        EXPECT_EQ(Capture().rideMaterials, next);
        previous = std::move(next);
    };
    station.setStart(station.getStartXY() + CoordsXY{ 0, 32 });
    changed();
    station.setBaseZ(88);
    changed();
    auto raisedEntrance = station.getEntrance();
    ++raisedEntrance.z;
    station.setEntrance(raisedEntrance);
    changed();
    auto shiftedExit = station.getExit();
    ++shiftedExit.x;
    station.setExit(shiftedExit);
    changed();
    ride.vehicleColours[3].Trim = static_cast<Drawing::Colour>(7);
    changed();
    ++ride.numTrains;
    changed();
    station.clearStart();
    station.clearEntrance();
    station.clearExit();
    changed();
    EXPECT_LT(previous->rides[0].stations.size(), 255u);
    station.setExit(TileCoordsXYZD{ 6, 7, 12, 0 });
    changed();
    ASSERT_EQ(previous->rides[0].stations.size(), 255u);
    EXPECT_EQ(previous->rides[0].stations[254].exitX, 6);
    EXPECT_EQ(first->rides[0].stations[254].startY, 96);
    EXPECT_EQ(first->rides[0].stations[254].startZ, 80);
    EXPECT_EQ(first->rides[0].stations[254].entranceZ, 10);
    EXPECT_EQ(first->rides[0].stations[254].exitX, 4);
    ride.id = RideId::GetNull();
    changed();
    EXPECT_FALSE(previous->rides[0].present);
    ride.id = RideId::FromUnderlying(0);
    changed();
    EXPECT_TRUE(previous->rides[0].present);
}

TEST_F(WorldPathPublicationTest, ClockMetadataChangesWithoutDirtyTilesOrChunkCopies)
{
    const auto saved = gRealTimeOfDay;
    MapPresentationSnapshot snapshot;
    snapshot.Apply(Capture(true));
    const auto chunks = snapshot.GetSurfaceChunks();
    gRealTimeOfDay.hour = 7;
    gRealTimeOfDay.minute = 23;
    auto batch = Capture();
    gRealTimeOfDay = saved;
    EXPECT_TRUE(batch.changes.empty());
    snapshot.Apply(batch);
    EXPECT_EQ(snapshot.GetClockHour(), 7);
    EXPECT_EQ(snapshot.GetClockMinute(), 23);
    EXPECT_EQ(snapshot.GetSurfaceChunks(), chunks);
}

TEST_F(WorldPathPublicationTest, StationGraphicalOwnerTracksAssignmentHeightNullAndNoOpWrites)
{
    auto& state = getGameState();
    state.ridesEndOfUsedRange = 1;
    auto& ride = state.rides[0];
    ride.id = RideId::FromUnderlying(0);
    ride.type = 0;
    auto& station = ride.getStation(StationIndex::FromUnderlying(254));
    station.setStart({ 64, 96 });
    station.setBaseZ(80);
    station.setEntrance({ 2, 3, 10, 0 });
    station.setExit({ 4, 5, 10, 1 });
    const auto original = Capture(true).rideMaterials;
    auto revision = GetRideStationGraphicalRevision();
    station.setStart(station.getStartXY());
    station.setHeight(station.getHeight());
    station.setEntrance(station.getEntrance());
    station.setExit(station.getExit());
    station.depart ^= 1; // Simulation-only updates never dirty the graphical owner.
    EXPECT_EQ(GetRideStationGraphicalRevision(), revision);
    EXPECT_EQ(Capture().rideMaterials, original);

    station.setEntranceDirection(2);
    EXPECT_GT(GetRideStationGraphicalRevision(), revision);
    // Direction is not used by the current raw station geometry; the owned table stays identical.
    EXPECT_EQ(Capture().rideMaterials, original);
    revision = GetRideStationGraphicalRevision();
    RideStation replacement = station;
    replacement.setBaseZ(88);
    replacement.clearEntrance();
    revision = GetRideStationGraphicalRevision();
    station = replacement;
    EXPECT_GT(GetRideStationGraphicalRevision(), revision);
    const auto copied = Capture().rideMaterials;
    EXPECT_NE(copied, original);
    EXPECT_EQ(copied->rides[0].stations[254].startZ, 88);
    EXPECT_FALSE(copied->rides[0].stations[254].entranceValid);
    revision = GetRideStationGraphicalRevision();
    station = replacement;
    EXPECT_EQ(GetRideStationGraphicalRevision(), revision);
    replacement.clearStart();
    replacement.clearExit();
    revision = GetRideStationGraphicalRevision();
    station = std::move(replacement);
    EXPECT_GT(GetRideStationGraphicalRevision(), revision);
    const auto removed = Capture().rideMaterials;
    EXPECT_NE(removed, copied);
    EXPECT_LT(removed->rides[0].stations.size(), 255u);
    EXPECT_EQ(original->rides[0].stations[254].startZ, 80);
    EXPECT_TRUE(original->rides[0].stations[254].entranceValid);
}

TEST_F(WorldPathPublicationTest, WaterPlantsPublishesOnlyChangedRawAges)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement tree{};
    tree.clearAs(TileElementType::smallScenery);
    tree.setBaseZ(32);
    tree.setLastForTile(true);
    tree.asSmallScenery()->setAge(40);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, tree }), TileMutationStatus::ok);
    static_cast<void>(Capture(true));
    auto& state = getGameState();
    GameActions::CheatSetAction water(CheatType::waterPlants);
    static_cast<void>(water.Execute(state, state.park));
    const auto changed = Capture();
    ASSERT_EQ(changed.changes.size(), 1u);
    ASSERT_EQ(changed.changes[0].objects.size(), 1u);
    EXPECT_EQ(changed.changes[0].objects[0].age, 0);
    static_cast<void>(water.Execute(state, state.park));
    EXPECT_TRUE(Capture().changes.empty());
}

TEST_F(WorldPathPublicationTest, WorldCatalogReloadAtSameDrawRefreshesWholeAsyncGeneration)
{
    auto& state = getGameState();
    auto& jobs = context->GetJobPool();
    PresentationScene scene;
    state.currentTicks = 80;
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto held = scene.GetGeneration();
    const auto old = held->map->GetObjectMaterials();
    ++state.currentTicks;
    scene.ScheduleNext(jobs, state.entities);
    ASSERT_NE(context->GetObjectManager().LoadObject("rct2.scenery_small.tl0"), nullptr);
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto current = scene.GetGeneration();
    ASSERT_NE(current, held);
    EXPECT_EQ(current->map->GetObjectMaterials()->revision, GetWorldObjectRevision());
    EXPECT_NE(current->map->GetObjectMaterials(), old);
    EXPECT_EQ(current->map->GetSourceTick(), current->entities->GetSourceTick());
    EXPECT_EQ(held->map->GetObjectMaterials(), old);
    scene.Reset(jobs);
}

TEST(WorldObjectUsageTest, TracksMembershipAcrossMultiplicityMutationRemovalResetAndHeldSnapshots)
{
    MapPresentationSnapshot current;
    MapPresentationChangeBatch batch;
    batch.epoch = 1;
    batch.reset = true;
    batch.surfaceWidth = 32;
    batch.surfaceHeight = 32;
    batch.profile = MapPublicationProfile::rawTerrain;
    current.Apply(batch);
    const auto empty = current.GetObjectUsage();
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->Contains(0, 7));
    EXPECT_FALSE(empty->Contains(4, 7));
    EXPECT_FALSE(empty->Contains(0, UINT16_MAX));
    WorldObjectPresentationRecord tree;
    tree.kind = WorldObjectKind::smallScenery;
    tree.objectSlot = 7;
    WorldObjectPresentationRecord wall;
    wall.kind = WorldObjectKind::wall;
    wall.objectSlot = 2047;
    WorldObjectPresentationRecord track;
    track.kind = WorldObjectKind::track;
    track.objectSlot = 7;
    batch.reset = false;
    batch.changes.resize(1);
    auto& change = batch.changes[0];
    change.index = 34;
    change.surfaceIndex = 34;
    change.objects = { tree, tree, wall, track };
    current.Apply(batch);
    const auto used = current.GetObjectUsage();
    ASSERT_NE(used, empty);
    EXPECT_TRUE(used->Contains(0, 7));
    EXPECT_TRUE(used->Contains(2, 2047));
    EXPECT_FALSE(used->Contains(4, 7));
    const auto held = current;
    change.objects[0].primaryColour = 12;
    change.objects[0].flags = WorldObjectPresentationFlags::ghost;
    current.Apply(batch);
    EXPECT_EQ(current.GetObjectUsage(), used);
    // Removing one of two instances changes counts, but not the resident asset inventory.
    change.objects.erase(change.objects.begin());
    current.Apply(batch);
    EXPECT_EQ(current.GetObjectUsage(), used);
    // A sibling generation still owns independent counts: removing one there also leaves the asset resident.
    auto sibling = held;
    sibling.Apply(batch);
    EXPECT_EQ(sibling.GetObjectUsage(), used);
    change.objects.erase(change.objects.begin());
    current.Apply(batch);
    const auto wallOnly = current.GetObjectUsage();
    ASSERT_NE(wallOnly, used);
    EXPECT_FALSE(wallOnly->Contains(0, 7));
    EXPECT_TRUE(wallOnly->Contains(2, 2047));
    EXPECT_TRUE(held.GetObjectUsage()->Contains(0, 7));
    EXPECT_TRUE(sibling.GetObjectUsage()->Contains(0, 7));
    // Membership unchanged across a reset keeps the inventory identity, but counts must be rebuilt.
    batch.reset = true;
    batch.epoch = 2;
    current.Apply(batch);
    EXPECT_EQ(current.GetObjectUsage(), wallOnly);
    batch.reset = false;
    change.objects.clear();
    current.Apply(batch);
    EXPECT_FALSE(current.GetObjectUsage()->Contains(2, 2047));
    EXPECT_TRUE(wallOnly->Contains(2, 2047));
    const auto cleared = current.GetObjectUsage();
    batch.changes.clear();
    current.Apply(batch);
    EXPECT_EQ(current.GetObjectUsage(), cleared);
    // Full reset with no tile work retires every old membership, including across a smaller layout.
    auto resetHeld = held;
    batch.reset = true;
    batch.epoch = 3;
    batch.surfaceWidth = 16;
    batch.surfaceHeight = 16;
    resetHeld.Apply(batch);
    EXPECT_FALSE(resetHeld.GetObjectUsage()->Contains(0, 7));
    EXPECT_FALSE(resetHeld.GetObjectUsage()->Contains(2, 2047));
    EXPECT_TRUE(held.GetObjectUsage()->Contains(0, 7));
}

TEST_F(WorldPathPublicationTest, EntrancesPreserveRawFieldsAndResidencyDependencies)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement park{};
    park.clearAs(TileElementType::entrance);
    park.setBaseZ(64);
    park.setClearanceZ(144);
    park.setDirection(3);
    park.setGhost(true);
    auto& entry = *park.asEntrance();
    entry.setEntranceType(EntranceType::parkEntrance);
    entry.setEntryIndex(7);
    entry.setSequenceIndex(ParkEntranceSequence::right);
    entry.setLegacyPathEntryIndex(9);
    TileElement ride = park;
    auto& exit = *ride.asEntrance();
    exit.setEntranceType(EntranceType::rideExit);
    exit.setRideIndex(RideId::FromUnderlying(11));
    exit.setStationIndex(StationIndex::FromUnderlying(200));
    ride.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, park, ride }), TileMutationStatus::ok);
    auto batch = Capture(true);
    MapPresentationSnapshot current;
    current.Apply(batch);
    ASSERT_FALSE(current.HasLegacyTileStorage());
    const auto held = current.GetObjectChunks()[0];
    const auto range = held->tiles[34];
    ASSERT_EQ(range.count, 2u);
    const auto& raw = held->records[range.first];
    EXPECT_EQ(raw.kind, WorldObjectKind::entrance);
    EXPECT_EQ(raw.entranceType, 2);
    EXPECT_EQ(raw.sequence, 2);
    EXPECT_EQ(raw.direction, 3);
    EXPECT_EQ(raw.elementOrdinal, 1u);
    EXPECT_EQ(raw.pathSurfaceSlot, 9);
    EXPECT_EQ(raw.objectSlot, 7);
    EXPECT_NE(raw.flags & WorldObjectPresentationFlags::legacyPath, 0u);
    EXPECT_NE(raw.flags & WorldObjectPresentationFlags::ghost, 0u);
    EXPECT_EQ(held->records[range.first + 1].rideId, 11);
    EXPECT_EQ(held->records[range.first + 1].stationIndex, 200);
    const auto used = current.GetObjectUsage();
    EXPECT_TRUE(used->Contains(5, 7));
    EXPECT_TRUE(used->ContainsRide(11));
    EXPECT_FALSE(used->ContainsRide(7));
    // A same-slot park entrance edit preserves residency; changing a ride reference must replace it.
    entry.setSequenceIndex(ParkEntranceSequence::left);
    exit.setRideIndex(RideId::FromUnderlying(12));
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, park, ride }), TileMutationStatus::ok);
    current.Apply(Capture());
    EXPECT_NE(current.GetObjectUsage(), used);
    EXPECT_FALSE(current.GetObjectUsage()->ContainsRide(11));
    EXPECT_TRUE(current.GetObjectUsage()->ContainsRide(12));
    EXPECT_TRUE(used->ContainsRide(11));
    surface.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface }), TileMutationStatus::ok);
    current.Apply(Capture());
    EXPECT_FALSE(current.GetObjectUsage()->Contains(5, 7));
    EXPECT_FALSE(current.GetObjectUsage()->ContainsRide(12));
    EXPECT_EQ(held->records[range.first].sequence, 2);
}

TEST_F(WorldPathPublicationTest, StationCatalogOwnsFactsAcrossUnloadAndReload)
{
    auto& manager = context->GetObjectManager();
    auto* object = manager.LoadObject("rct2.station.plain");
    ASSERT_NE(object, nullptr);
    const auto slot = manager.GetLoadedObjectEntryIndex(object);
    const auto descriptor = object->GetDescriptor();
    const auto held = Capture(true).objectMaterials;
    ASSERT_TRUE(held->stations[slot].present);
    const auto old = held->stations[slot];
    EXPECT_EQ(Capture().objectMaterials, held);
    manager.UnloadObjects({ descriptor });
    const auto unloaded = Capture().objectMaterials;
    EXPECT_FALSE(unloaded->stations[slot].present);
    EXPECT_GT(unloaded->revision, held->revision);
    ASSERT_NE(manager.LoadObject(descriptor, slot), nullptr);
    const auto replaced = Capture().objectMaterials;
    EXPECT_TRUE(replaced->stations[slot].present);
    EXPECT_GT(replaced->revision, unloaded->revision);
    EXPECT_EQ(held->stations[slot].entranceBack, old.entranceBack);
    EXPECT_EQ(held->stations[slot].imageBase, old.imageBase);
}

#ifdef ENABLE_VULKAN
    #include <openrct2-renderer/gpu/GpuWorldEntranceCatalog.h>
namespace EntranceRulesTest
{
    #include "../../data/shaders/vulkan/world_entrance_rules.glsl"
}
TEST(WorldEntranceCatalogTest, UsedDependenciesOnlyAndOwnedRangeValidation)
{
    namespace G = OpenRCT2::Ui::Gpu;
    auto objects = std::make_unique<WorldObjectPresentationMaterials>();
    auto& station = objects->stations[2];
    station.present = true;
    station.imageBase = 100;
    station.imageCount = 16;
    station.entranceBack = 100;
    station.entranceFront = 104;
    station.exitBack = 108;
    station.exitFront = 112;
    objects->stations[3] = station; // Loaded, but never referenced.
    objects->parkEntrances[4] = { 200, 12, 200, 0, 0, true };
    objects->parkEntrances[5] = { 300, 12, 300, 0, 0, true };
    WorldRidePresentationMaterials rides;
    rides.rides.resize(8);
    rides.rides[7].present = true;
    rides.rides[7].stationStyle = 2;
    rides.rides[7].stations.resize(201);
    rides.rides[7].stations[200].entranceValid = true;
    rides.rides[7].stations[200].entranceX = 5;
    WorldObjectPresentationUsage usage;
    usage.slots[6].set(7);
    usage.slots[5].set(4);
    std::vector<uint32_t> images;
    const auto append = [&](uint32_t image) {
        images.push_back(image);
        return static_cast<uint32_t>(images.size() - 1);
    };
    auto catalog = G::BuildWorldEntranceCatalog(*objects, rides, &usage, 40, append);
    ASSERT_EQ(images.size(), 28u);
    EXPECT_EQ(images.front(), 100u);
    EXPECT_EQ(images.back(), 211u);
    EXPECT_NO_THROW(G::ValidateWorldEntranceCatalog(catalog.words, static_cast<uint32_t>(images.size())));
    auto bad = catalog.words;
    bad[bad[0] + 2 * 16 + 4] = 25;
    EXPECT_THROW(G::ValidateWorldEntranceCatalog(bad, 28), std::invalid_argument);
    bad = catalog.words;
    bad[bad[4] + 7 * 8 + 5] = UINT32_MAX;
    EXPECT_THROW(G::ValidateWorldEntranceCatalog(bad, 28), std::invalid_argument);
    objects->parkEntrances[4].imageCount = 11;
    EXPECT_THROW(static_cast<void>(G::BuildWorldEntranceCatalog(*objects, rides, &usage, 40, append)), std::runtime_error);
}
TEST(WorldEntranceRulesTest, OriginalParentBoundsAndGlassRemainAttached)
{
    using namespace EntranceRulesTest;
    const auto entrance = worldRideEntranceParts(1, false, 7);
    ASSERT_EQ(entrance.count, 4);
    EXPECT_EQ(entrance.parts[0].sizeX, 8);
    EXPECT_EQ(entrance.parts[0].sizeY, 28);
    EXPECT_EQ(entrance.parts[1].child, 1);
    EXPECT_EQ(entrance.parts[1].colourMode, 4);
    EXPECT_EQ(entrance.parts[2].boundsZ, 30);
    EXPECT_EQ(entrance.parts[2].sizeZ, 17);
    const auto exit = worldRideEntranceParts(0, true, 0);
    ASSERT_EQ(exit.count, 2);
    EXPECT_EQ(exit.parts[0].imageOffset, 2);
    EXPECT_EQ(exit.parts[1].sizeZ, 1);
    const auto park = worldParkEntranceParts(3, 0);
    ASSERT_EQ(park.count, 2);
    EXPECT_EQ(park.parts[0].imageOffset, -1);
    EXPECT_EQ(park.parts[1].imageOffset, 9);
    EXPECT_EQ(worldParkEntranceParts(0, 3).count, 0);
}
TEST(WorldEntranceRulesTest, TwoParentOrderingMatchesGeneralRulesAndKeepsGlassAttached)
{
    using namespace EntranceRulesTest;
    const auto verify = [](const WorldPropParts& parts, int rotation) {
        WorldPathPart parents[12]{};
        int recipes[2]{};
        int count = 0;
        for (int i = 0; i < parts.count; ++i)
        {
            const auto& p = parts.parts[i];
            if (p.child != 0)
                continue;
            ASSERT_LT(count, 2);
            recipes[count] = i;
            parents[count++] = worldPathPart(
                p.imageOffset, p.x, p.y, p.z, p.boundsX, p.boundsY, p.boundsZ, p.sizeX, p.sizeY, p.sizeZ);
        }
        ASSERT_GT(count, 0);
        const auto general = worldPathOrder(parents, count, rotation);
        ASSERT_EQ(general.count, count);
        const int first = count == 2 ? worldEntranceFirstParent(parts.parts[recipes[0]], parts.parts[recipes[1]], rotation) : 0;
        std::vector<int> expected;
        std::vector<int> actual;
        const auto appendFamily = [&](std::vector<int>& target, int parent) {
            const int begin = recipes[parent];
            target.push_back(begin);
            for (int i = begin + 1; i < parts.count && parts.parts[i].child != 0; ++i)
                target.push_back(i);
        };
        for (int ordinal = 0; ordinal < count; ++ordinal)
        {
            appendFamily(expected, general.indices[ordinal]);
            appendFamily(actual, count == 2 && first == 1 ? 1 - ordinal : ordinal);
        }
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(actual.size(), static_cast<size_t>(parts.count));
    };
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        SCOPED_TRACE(rotation);
        for (int direction = 0; direction < 4; ++direction)
        {
            SCOPED_TRACE(direction);
            for (int flags = 0; flags < 256; ++flags)
            {
                SCOPED_TRACE(flags);
                verify(worldRideEntranceParts(direction, false, flags), rotation);
                verify(worldRideEntranceParts(direction, true, flags), rotation);
            }
            for (int sequence = 0; sequence < 3; ++sequence)
                verify(worldParkEntranceParts(direction, sequence), rotation);
        }
    }
}
#endif

TEST_F(WorldPathPublicationTest, TowerTopologyIncludesHiddenUnrenderedSuccessorsAndUpdatesOnRemoval)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setLastForTile(false);
    TileElement tower{};
    tower.clearAs(TileElementType::track);
    tower.setBaseZ(64);
    tower.setClearanceZ(96);
    TileElement gap{};
    gap.clearAs(TileElementType::path);
    gap.setBaseZ(80);
    gap.setClearanceZ(88);
    TileElement upper{};
    upper.clearAs(TileElementType::path);
    upper.setBaseZ(96);
    upper.setClearanceZ(104);
    upper.setGhost(true);
    upper.setInvisible(true);
    upper.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, tower, gap, upper }), TileMutationStatus::ok);
    MapPresentationSnapshot snapshot;
    snapshot.Apply(Capture(true));
    const auto held = snapshot.GetObjectChunks()[0];
    const auto index = held->tiles[34].first;
    const auto hasNext = WorldObjectPresentationFlags::nextElementAtClearance;
    const auto hasLater = WorldObjectPresentationFlags::anyLaterElementAtClearance;
    EXPECT_EQ(held->records[index].flags & hasNext, 0u);
    EXPECT_NE(held->records[index].flags & hasLater, 0u);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, tower, upper }), TileMutationStatus::ok);
    snapshot.Apply(Capture());
    auto chunk = snapshot.GetObjectChunks()[0];
    auto raw = chunk->records[chunk->tiles[34].first];
    EXPECT_NE(raw.flags & hasNext, 0u);
    EXPECT_NE(raw.flags & hasLater, 0u);
    tower.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, tower }), TileMutationStatus::ok);
    snapshot.Apply(Capture());
    chunk = snapshot.GetObjectChunks()[0];
    raw = chunk->records[chunk->tiles[34].first];
    EXPECT_EQ(raw.flags & (hasNext | hasLater), 0u);
    EXPECT_NE(held->records[index].flags & hasLater, 0u);
}
