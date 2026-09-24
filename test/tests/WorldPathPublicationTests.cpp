/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include <gtest/gtest.h>
#include <openrct2/Cheats.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/actions/cheats/CheatSetAction.h>
#include <openrct2/core/JobPool.h>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/object/FootpathSurfaceObject.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <openrct2/world/Park.h>
#include <openrct2/world/tile_element/PathElement.h>

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
        // The same scope state used by ObjectManager::PathMaterialMutation while SetProgress can draw.
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
