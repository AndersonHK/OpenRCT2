/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/core/JobPool.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/drawing/PresentationTask.h>
#include <openrct2/entity/Balloon.h>
#include <openrct2/entity/Duck.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/JumpingFountain.h>
#include <openrct2/entity/Litter.h>
#include <openrct2/entity/MoneyEffect.h>
#include <openrct2/entity/Particle.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <openrct2/world/tile_element/SurfaceElement.h>

using namespace OpenRCT2;

namespace
{
    size_t ConcreteEntitySize(EntityType type)
    {
        switch (type)
        {
            case EntityType::vehicle:
                return sizeof(Vehicle);
            case EntityType::guest:
                return sizeof(Guest);
            case EntityType::staff:
                return sizeof(Staff);
            case EntityType::litter:
                return sizeof(Litter);
            case EntityType::steamParticle:
                return sizeof(SteamParticle);
            case EntityType::moneyEffect:
                return sizeof(MoneyEffect);
            case EntityType::crashedVehicleParticle:
                return sizeof(VehicleCrashParticle);
            case EntityType::explosionCloud:
                return sizeof(ExplosionCloud);
            case EntityType::crashSplash:
                return sizeof(CrashSplashParticle);
            case EntityType::explosionFlare:
                return sizeof(ExplosionFlare);
            case EntityType::jumpingFountain:
                return sizeof(JumpingFountain);
            case EntityType::balloon:
                return sizeof(Balloon);
            case EntityType::duck:
                return sizeof(Duck);
            default:
                return sizeof(EntityBase);
        }
    }

    void ExpectSnapshotEqualsLive(const PresentationGeneration& generation)
    {
        ASSERT_NE(generation.map, nullptr);
        ASSERT_NE(generation.entities, nullptr);
        auto& state = getGameState();
        ASSERT_EQ(generation.map->GetEpoch(), GetMapPresentationEpoch());
        ASSERT_EQ(generation.map->GetSurfaceWidth(), static_cast<uint32_t>(state.mapSize.x));
        ASSERT_EQ(generation.map->GetSurfaceHeight(), static_cast<uint32_t>(state.mapSize.y));
        for (int32_t y = 0; y < state.mapSize.y; y++)
        {
            for (int32_t x = 0; x < state.mapSize.x; x++)
            {
                SCOPED_TRACE(::testing::Message() << "tile " << x << ',' << y);
                const TileCoordsXY tile{ x, y };
                auto* live = MapGetFirstElementAt(tile);
                auto* captured = generation.map->GetFirstElementAt(tile);
                ASSERT_EQ(live == nullptr, captured == nullptr);
                if (live != nullptr)
                {
                    for (;; live++, captured++)
                    {
                        ASSERT_EQ(std::memcmp(live, captured, sizeof(TileElement)), 0);
                        if (live->isLastForTile())
                            break;
                    }
                }
                const CoordsXY position{ x * 32, y * 32 };
                EXPECT_EQ(generation.entities->GetEntityTileList(position), state.entities.getEntityTileList(position));
            }
        }
        size_t count = 0;
        for (uint32_t index = 0; index < kMaxEntities; index++)
        {
            const auto id = EntityId::FromUnderlying(static_cast<uint16_t>(index));
            const auto* live = state.entities.tryGetEntity(id);
            const auto* captured = generation.entities->TryGetEntity(id);
            SCOPED_TRACE(::testing::Message() << "entity " << index);
            ASSERT_EQ(live == nullptr, captured == nullptr);
            if (live == nullptr)
                continue;
            count++;
            ASSERT_EQ(live->type, captured->type);
            // Entity publication promises exact owned concrete bytes. Compare the actual type size, never a
            // whole legacy union past the live pool allocation. This checks appearance/topology as well as position.
            EXPECT_EQ(std::memcmp(live, captured, ConcreteEntitySize(live->type)), 0);
        }
        EXPECT_EQ(generation.entities->GetCapturedEntityCount(), count);
    }

    class PublicationSnapshotParityTest : public testing::Test
    {
    protected:
        bool oldHeadless = gOpenRCT2Headless;
        bool oldNoGraphics = gOpenRCT2NoGraphics;
        std::unique_ptr<IContext> context;
        std::unique_ptr<PresentationScene> scene;
        EntityId movingEntity;

        void SetUp() override
        {
            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = true;
            context = CreateContext();
            ASSERT_TRUE(context->Initialise());
            MapInit({ 16, 16 });
            auto& entities = getGameState().entities;
            entities.resetAllEntities();
            for (uint8_t type = 0; type < static_cast<uint8_t>(EntityType::count); type++)
            {
                auto* entity = entities.createEntity(static_cast<EntityType>(type));
                ASSERT_NE(entity, nullptr);
                entity->moveTo({ 64 + type * 32, 64, 16 + type });
                entity->orientation = type;
                if (entity->type == EntityType::litter)
                    movingEntity = entity->id;
            }
            entities.updateEntitiesSpatialIndex();
            scene = std::make_unique<PresentationScene>();
        }

        void TearDown() override
        {
            if (scene != nullptr)
                scene->Reset(context->GetJobPool());
            scene.reset();
            context.reset();
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
        }

        void MutateMapAndEntity(int32_t height)
        {
            auto element = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
            element.setBaseZ(height);
            ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { element }), TileMutationStatus::ok);
            auto& entities = getGameState().entities;
            auto* entity = entities.getEntity(movingEntity);
            ASSERT_NE(entity, nullptr);
            entity->moveTo({ 160, 96, height });
            entity->orientation++;
            entities.updateEntitiesSpatialIndex();
        }

        void PublishCompleted(uint32_t frame)
        {
            scene->ScheduleNext(context->GetJobPool(), getGameState().entities);
            context->GetJobPool().Join();
            ASSERT_TRUE(scene->BeginFrame(context->GetJobPool(), getGameState().entities, frame, false));
        }
    };
} // namespace

TEST_F(PublicationSnapshotParityTest, InitialSyntheticPublicationEqualsAllLiveTilesAndEntityTypes)
{
    ASSERT_TRUE(scene->BeginFrame(context->GetJobPool(), getGameState().entities, 1, true));
    ExpectSnapshotEqualsLive(*scene->GetGeneration());
}

TEST_F(PublicationSnapshotParityTest, InitialImportedParkPublicationEqualsLiveWorld)
{
    auto importer = ParkImporter::CreateS6(context->GetObjectRepository());
    const auto load = importer->LoadSavedGame(TestData::GetParkPath("small_park_with_ferris_wheel.sv6"), false);
    context->GetObjectManager().LoadObjects(load.RequiredObjects);
    MapAnimations::ClearAll();
    importer->Import(getGameState());
    getGameState().entities.resetEntitySpatialIndices();
    ResetAllSpriteQuadrantPlacements();
    ASSERT_TRUE(scene->BeginFrame(context->GetJobPool(), getGameState().entities, 1, true));
    ExpectSnapshotEqualsLive(*scene->GetGeneration());

    const auto* artifactRoot = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS");
    if (artifactRoot == nullptr || *artifactRoot == '\0')
        return;

    // The ordinary regression above is the small Ferris-wheel park, not EverythingPark.
    // Use a fresh headless graphics-capable context for this opt-in metadata diagnostic:
    // no window/device is created, but object image ranges must not be suppressed by NoGraphics.
    scene->Reset(context->GetJobPool());
    scene.reset();
    importer.reset();
    context.reset();
    gOpenRCT2NoGraphics = false;
    context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    scene = std::make_unique<PresentationScene>();
    const auto parkPath = TestData::GetParkPath("EverythingPark.park");
    importer = ParkImporter::CreateParkFile(context->GetObjectRepository());
    const auto everythingLoad = importer->LoadSavedGame(parkPath, false);
    context->GetObjectManager().LoadObjects(everythingLoad.RequiredObjects);
    MapAnimations::ClearAll();
    importer->Import(getGameState());
    getGameState().entities.resetEntitySpatialIndices();
    ResetAllSpriteQuadrantPlacements();
    ASSERT_TRUE(
        scene->BeginFrame(context->GetJobPool(), getGameState().entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto generation = scene->GetGeneration();
    const auto& map = *generation->map;
    ASSERT_TRUE(map.IsRawTerrainOnly());
    ASSERT_FALSE(map.HasLegacyTileStorage());
    const auto materials = map.GetTerrainMaterials();
    ASSERT_NE(materials, nullptr);

    const auto directory = std::filesystem::path(artifactRoot) / "terrain-publication" / "EverythingPark";
    std::filesystem::create_directories(directory);
    std::ofstream surfaces(directory / "surfaces.csv");
    ASSERT_TRUE(surfaces.good());
    surfaces << "tileX,tileY,worldX,worldY,baseZ,waterHeight,surfaceSlot,edgeSlot,slope,grass,kind\n";
    size_t presentCount = 0;
    for (uint32_t index = 0; index < map.GetSurfaceRecordCount(); ++index)
    {
        const auto& chunk = map.GetSurfaceChunks()[index / MapPresentationSnapshot::kChunkWidth];
        ASSERT_NE(chunk, nullptr);
        const auto& raw = chunk->records[index % MapPresentationSnapshot::kChunkWidth].terrain;
        if (!raw.present)
            continue;
        const auto x = index % map.GetSurfaceWidth();
        const auto y = index / map.GetSurfaceWidth();
        surfaces << x << ',' << y << ',' << x * 32 << ',' << y * 32 << ',' << raw.baseZ << ',' << raw.waterHeight << ','
                 << raw.surfaceSlot << ',' << raw.edgeSlot << ',' << static_cast<uint32_t>(raw.slope) << ','
                 << static_cast<uint32_t>(raw.grass) << ',' << static_cast<uint32_t>(raw.kind) << '\n';
        ++presentCount;
    }
    surfaces.close();
    ASSERT_FALSE(surfaces.fail());
    std::ofstream ranges(directory / "materials.csv");
    ASSERT_TRUE(ranges.good());
    ranges << "type,slot,imageBase,imageCount,supported\n";
    for (size_t slot = 0; slot < materials->surfaces.size(); ++slot)
    {
        const auto& surface = materials->surfaces[slot];
        const auto& edge = materials->edges[slot];
        ranges << "surface," << slot << ',' << surface.imageBase << ',' << surface.imageCount << ',' << surface.supported
               << '\n';
        ranges << "edge," << slot << ',' << edge.imageBase << ',' << edge.imageCount << ',' << edge.supported << '\n';
    }
    ranges.close();
    ASSERT_FALSE(ranges.fail());
    auto cornerProbe = json_t::array();
    for (uint32_t y = 0; y < 3 && y < map.GetSurfaceHeight(); ++y)
        for (uint32_t x = 253; x < 256 && x < map.GetSurfaceWidth(); ++x)
        {
            const auto index = y * map.GetSurfaceWidth() + x;
            const auto& raw = map.GetSurfaceChunks()[index / MapPresentationSnapshot::kChunkWidth]
                                  ->records[index % MapPresentationSnapshot::kChunkWidth]
                                  .terrain;
            cornerProbe.push_back({ { "tileX", x },
                                    { "tileY", y },
                                    { "present", raw.present },
                                    { "baseZ", raw.baseZ },
                                    { "waterHeight", raw.waterHeight },
                                    { "slope", raw.slope },
                                    { "kind", raw.kind },
                                    { "surfaceSlot", raw.surfaceSlot },
                                    { "edgeSlot", raw.edgeSlot } });
        }
    std::ofstream report(directory / "report.json");
    ASSERT_TRUE(report.good());
    report << json_t{
        { "schemaVersion", 1 },
        { "fixture", "EverythingPark.park" },
        { "fixturePath", std::filesystem::absolute(parkPath).generic_string() },
        { "fixtureBytes", std::filesystem::file_size(parkPath) },
        { "scope", "Imported raw publication; no simulation advances or GPU execution" },
        { "width", map.GetSurfaceWidth() },
        { "height", map.GetSurfaceHeight() },
        { "presentSurfaces", presentCount },
        { "sourceTick", generation->sourceTick },
        { "mapEpoch", map.GetEpoch() },
        { "materialRevision", materials->revision },
        { "surfacesFile", "surfaces.csv" },
        { "materialsFile", "materials.csv" },
        { "coordinateUnits", "tileX/Y are tile indices; worldX/Y, baseZ and waterHeight are world units" },
        { "materialIdentityScope", "Image indices belong to this loaded object generation" },
        { "cornerProbe253To255By0To2", std::move(cornerProbe) },
        { "hardwareReadback", false }
    }.dump(2);
    report.close();
    ASSERT_FALSE(report.fail());
}

TEST_F(PublicationSnapshotParityTest, CompletedScheduledPublicationEqualsChangedLiveWorld)
{
    ASSERT_TRUE(scene->BeginFrame(context->GetJobPool(), getGameState().entities, 1, true));
    MutateMapAndEntity(48);
    PublishCompleted(2);
    ExpectSnapshotEqualsLive(*scene->GetGeneration());
}

TEST_F(PublicationSnapshotParityTest, SynchronousPublicationMustNotPairNewMapWithStaleEntities)
{
    ASSERT_TRUE(scene->BeginFrame(context->GetJobPool(), getGameState().entities, 1, true));
    MutateMapAndEntity(48);
    // Construction/ghost paths can demand synchronous map publication without a completed entity job.
    // A coherent live result must refresh both sources. This is a parity requirement, not a tolerated lag.
    ASSERT_TRUE(scene->BeginFrame(context->GetJobPool(), getGameState().entities, 2, true));
    ExpectSnapshotEqualsLive(*scene->GetGeneration());
}

TEST_F(PublicationSnapshotParityTest, RetainedGenerationMustRemainImmutableAcrossSnapshotRecycling)
{
    ASSERT_TRUE(scene->BeginFrame(context->GetJobPool(), getGameState().entities, 1, true));
    const auto retained = scene->GetGeneration();
    const auto* oldEntity = retained->entities->TryGetEntity(movingEntity);
    ASSERT_NE(oldEntity, nullptr);
    const auto oldLocation = oldEntity->getLocation();
    const auto oldOrientation = oldEntity->orientation;
    const auto oldHeight = retained->map->GetFirstElementAt({ 2, 2 })->getBaseZ();
    MutateMapAndEntity(48);
    PublishCompleted(2);
    MutateMapAndEntity(64);
    PublishCompleted(3);
    ExpectSnapshotEqualsLive(*scene->GetGeneration());
    ASSERT_NE(retained->entities->TryGetEntity(movingEntity), nullptr);
    EXPECT_EQ(retained->entities->TryGetEntity(movingEntity)->getLocation(), oldLocation);
    EXPECT_EQ(retained->entities->TryGetEntity(movingEntity)->orientation, oldOrientation);
    EXPECT_EQ(retained->map->GetFirstElementAt({ 2, 2 })->getBaseZ(), oldHeight);
}

TEST_F(PublicationSnapshotParityTest, PendingEntityCaptureCannotBePairedWithLaterMapCapture)
{
    getGameState().currentTicks = 100;
    JobPool jobs(1);
    PresentationScene publication;
    struct ResetPublication
    {
        PresentationScene& scene;
        JobPool& jobs;
        ~ResetPublication()
        {
            scene.Reset(jobs);
        }
    } reset{ publication, jobs };
    ASSERT_TRUE(publication.BeginFrame(jobs, getGameState().entities, 1, true));
    const auto initial = publication.GetGeneration();
    EXPECT_EQ(initial->sourceTick, 100u);
    const auto initialHeight = initial->map->GetFirstElementAt({ 2, 2 })->getBaseZ();
    std::promise<void> started;
    std::promise<void> release;
    auto released = release.get_future();
    struct ReleaseWorker
    {
        std::promise<void>& release;
        JobPool& jobs;
        bool done{};
        void Complete()
        {
            if (!done)
            {
                release.set_value();
                done = true;
                jobs.Join();
            }
        }
        ~ReleaseWorker()
        {
            Complete();
        }
    } gate{ release, jobs };
    jobs.AddTask([&]() {
        started.set_value();
        released.wait();
    });
    started.get_future().wait();
    auto& entities = getGameState().entities;
    auto* entity = entities.getEntity(movingEntity);
    ASSERT_NE(entity, nullptr);
    entity->moveTo({ 160, 96, 48 });
    entities.updateEntitiesSpatialIndex();
    getGameState().currentTicks = 101;
    publication.ScheduleNext(jobs, entities); // Only entities changed; the one worker is deterministically held.
    EXPECT_FALSE(publication.BeginFrame(jobs, entities, 2, false));
    EXPECT_EQ(publication.GetGeneration(), initial);
    MutateMapAndEntity(64);
    getGameState().currentTicks = 102;
    publication.ScheduleNext(jobs, entities); // Must not queue this newer map beside the already queued older entities.
    gate.Complete();
    ASSERT_TRUE(publication.BeginFrame(jobs, entities, 3, false));
    const auto captured = publication.GetGeneration();
    EXPECT_EQ(captured->sourceTick, 101u);
    EXPECT_EQ(initial->sourceTick, 100u);
    EXPECT_EQ(captured->map->GetFirstElementAt({ 2, 2 })->getBaseZ(), initialHeight);
    ASSERT_NE(captured->entities->TryGetEntity(movingEntity), nullptr);
    EXPECT_EQ(captured->entities->TryGetEntity(movingEntity)->z, 48);
    publication.ScheduleNext(jobs, entities);
    jobs.Join();
    ASSERT_TRUE(publication.BeginFrame(jobs, entities, 4, false));
    EXPECT_EQ(publication.GetGeneration()->sourceTick, 102u);
    ExpectSnapshotEqualsLive(*publication.GetGeneration());
}

TEST_F(PublicationSnapshotParityTest, ResetBootstrapsAllTilesAndFreshEpochOnlyOnce)
{
    auto& jobs = context->GetJobPool();
    auto& entities = getGameState().entities;
    for (const bool synchronous : { false, true })
    {
        SCOPED_TRACE(synchronous);
        ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, true));
        const auto retained = scene->GetGeneration();
        scene->Reset(jobs);
        ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, synchronous));
        const auto restored = scene->GetGeneration();
        EXPECT_NE(restored->map->GetEpoch(), retained->map->GetEpoch());
        ExpectSnapshotEqualsLive(*restored);
        // Reset must not mutate generations still held by an older submission.
        ASSERT_NE(retained->map->GetFirstElementAt({ 15, 15 }), nullptr);
        EXPECT_EQ(
            std::memcmp(
                retained->map->GetFirstElementAt({ 15, 15 }), restored->map->GetFirstElementAt({ 15, 15 }),
                sizeof(TileElement)),
            0);
        // A normal empty frame must retain the map and must not request another full bootstrap.
        ASSERT_TRUE(scene->BeginFrame(jobs, entities, 2, synchronous));
        EXPECT_EQ(scene->GetGeneration()->map, restored->map);
        EXPECT_EQ(GetMapPresentationEpoch(), restored->map->GetEpoch());
        const auto remaining = ConsumeMapPresentationChanges();
        EXPECT_FALSE(remaining.reset);
        EXPECT_TRUE(remaining.changes.empty());
        scene->Reset(jobs);
    }
}

TEST_F(PublicationSnapshotParityTest, ResetAfterConsumedPendingDeltaRecapturesCompleteLatestMap)
{
    auto& jobs = context->GetJobPool();
    auto& entities = getGameState().entities;
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, true));
    const auto retained = scene->GetGeneration();
    const auto initialHeight = retained->map->GetFirstElementAt({ 2, 2 })->getBaseZ();
    MutateMapAndEntity(48);
    scene->ScheduleNext(jobs, entities); // Consumes the tile delta, possibly still applying it in the background.
    scene->Reset(jobs);                  // Waits and discards that pending publication.
    MutateMapAndEntity(64);              // The fresh snapshot must include this change AND every unchanged tile.
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, false));
    ExpectSnapshotEqualsLive(*scene->GetGeneration());
    EXPECT_NE(scene->GetGeneration()->map->GetEpoch(), retained->map->GetEpoch());
    EXPECT_EQ(retained->map->GetFirstElementAt({ 2, 2 })->getBaseZ(), initialHeight);
}

TEST_F(PublicationSnapshotParityTest, RecreatedOwnerBootstrapsAfterPreviousOwnerConsumedMapReset)
{
    auto& jobs = context->GetJobPool();
    auto& entities = getGameState().entities;
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, true));
    const auto retained = scene->GetGeneration();
    scene->Reset(jobs);
    scene = std::make_unique<PresentationScene>();
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, false));
    ExpectSnapshotEqualsLive(*scene->GetGeneration());
    EXPECT_NE(scene->GetGeneration()->map->GetEpoch(), retained->map->GetEpoch());
}

TEST_F(PublicationSnapshotParityTest, NativeTerrainCapturesRawSteepWetMixedTileWithoutLegacyCopies)
{
    auto surface = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    surface.setBaseZ(96);
    surface.asSurface()->setSlope(23); // Complete steep-slope bits, outside the bounded diagnostic recipe.
    surface.asSurface()->setWaterHeight(128);
    surface.asSurface()->setGrassLength(6);
    surface.asSurface()->setSurfaceObjectIndex(3);
    surface.asSurface()->setEdgeObjectIndex(4);
    surface.setLastForTile(false);
    TileElement other{};
    other.clearAs(TileElementType::path);
    other.setLastForTile(true);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { surface, other }), TileMutationStatus::ok);
    getGameState().currentTicks = 321;

    const auto changes = ConsumeMapPresentationChanges(true, MapPublicationProfile::rawTerrain);
    ASSERT_EQ(changes.changes.size(), 256u);
    EXPECT_EQ(changes.sourceTick, 321u);
    for (const auto& change : changes.changes)
    {
        EXPECT_TRUE(change.elements.empty());
        EXPECT_EQ(change.elements.capacity(), 0u);
    }
    MapPresentationSnapshot snapshot;
    snapshot.Apply(changes);
    EXPECT_TRUE(snapshot.IsRawTerrainOnly());
    EXPECT_FALSE(snapshot.HasLegacyTileStorage());
    EXPECT_EQ(snapshot.GetFirstElementAt({ 2, 2 }), nullptr);
    EXPECT_EQ(snapshot.GetSourceTick(), 321u);
    const auto& raw = snapshot.GetSurfaceChunks()[0]->records[34].terrain;
    EXPECT_EQ(raw.baseZ, 96);
    EXPECT_EQ(raw.waterHeight, 128);
    EXPECT_EQ(raw.slope, 23);
    EXPECT_EQ(raw.grass, 6);
    EXPECT_EQ(raw.surfaceSlot, 3);
    EXPECT_EQ(raw.edgeSlot, 4);
    EXPECT_EQ(raw.present, 1);
    EXPECT_EQ(raw.kind, 1);
    EXPECT_EQ(raw.bounded, 0);
    EXPECT_FALSE(snapshot.HasBoundedTerrainFacts());
}

TEST_F(PublicationSnapshotParityTest, NativeTerrainReleasesLegacyStorageAndMetadataOnlyTickSharesDirectory)
{
    auto& jobs = context->GetJobPool();
    auto& entities = getGameState().entities;
    getGameState().currentTicks = 400;
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, true));
    const auto heldLegacy = scene->GetGeneration();
    ASSERT_TRUE(heldLegacy->map->HasLegacyTileStorage());

    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 2, true, EntityPublicationProfile::gpuTerrainOnly));
    const auto raw = scene->GetGeneration();
    EXPECT_TRUE(raw->map->IsRawTerrainOnly());
    EXPECT_FALSE(raw->map->HasLegacyTileStorage());
    EXPECT_FALSE(raw->entities->HasLegacyStorage());
    EXPECT_NE(raw->map->GetEpoch(), heldLegacy->map->GetEpoch());
    ASSERT_NE(heldLegacy->map->GetFirstElementAt({ 15, 15 }), nullptr);
    EXPECT_EQ(raw->map->GetFirstElementAt({ 15, 15 }), nullptr);

    getGameState().currentTicks = 401;
    scene->ScheduleNext(jobs, entities);
    jobs.Join();
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 3, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto next = scene->GetGeneration();
    EXPECT_EQ(next->sourceTick, 401u);
    EXPECT_EQ(next->map->GetSourceTick(), 401u);
    EXPECT_EQ(next->entities->GetSourceTick(), 401u);
    EXPECT_EQ(&next->map->GetSurfaceChunks(), &raw->map->GetSurfaceChunks());
    EXPECT_EQ(next->map->GetTerrainMaterials(), raw->map->GetTerrainMaterials());
    EXPECT_FALSE(next->map->HasLegacyTileStorage());
    EXPECT_FALSE(next->entities->HasLegacyStorage());
    EXPECT_EQ(raw->sourceTick, 400u);

    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 4, true, EntityPublicationProfile::legacyBulk));
    EXPECT_FALSE(scene->GetGeneration()->map->IsRawTerrainOnly());
    EXPECT_TRUE(scene->GetGeneration()->map->HasLegacyTileStorage());
    ExpectSnapshotEqualsLive(*scene->GetGeneration());
    EXPECT_FALSE(raw->map->HasLegacyTileStorage());
}

TEST_F(PublicationSnapshotParityTest, NativeTerrainPendingMapKeepsMatchingCapturedTickAndNeverWaitsForInteractiveEdits)
{
    JobPool jobs(1);
    PresentationScene publication;
    auto& entities = getGameState().entities;
    getGameState().currentTicks = 500;
    ASSERT_TRUE(publication.BeginFrame(jobs, entities, 1, true, EntityPublicationProfile::gpuTerrainOnly));
    const auto initial = publication.GetGeneration();

    std::promise<void> started;
    std::promise<void> release;
    auto released = release.get_future();
    struct ReleaseWorker
    {
        std::promise<void>& release;
        JobPool& jobs;
        bool done{};
        void Complete()
        {
            if (!done)
            {
                release.set_value();
                done = true;
                jobs.Join();
            }
        }
        ~ReleaseWorker()
        {
            Complete();
        }
    } gate{ release, jobs };
    jobs.AddTask([&]() {
        started.set_value();
        released.wait();
    });
    started.get_future().wait();

    MutateMapAndEntity(48);
    getGameState().currentTicks = 501;
    publication.ScheduleNext(jobs, entities);
    MutateMapAndEntity(64);
    getGameState().currentTicks = 502;
    // A synchronous-map hint from interactive editing must not wait for the blocked native worker.
    EXPECT_FALSE(publication.BeginFrame(jobs, entities, 2, true, EntityPublicationProfile::gpuTerrainOnly));
    EXPECT_EQ(publication.GetGeneration(), initial);
    publication.ScheduleNext(jobs, entities); // Must not drain the newer tick alongside the pending older map.
    gate.Complete();

    ASSERT_TRUE(publication.BeginFrame(jobs, entities, 3, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto prior = publication.GetGeneration();
    EXPECT_EQ(prior->sourceTick, 501u);
    EXPECT_EQ(prior->map->GetSourceTick(), 501u);
    EXPECT_EQ(prior->entities->GetSourceTick(), 501u);
    EXPECT_EQ(prior->map->GetSurfaceChunks()[0]->records[34].terrain.baseZ, 48);
    EXPECT_FALSE(prior->map->HasLegacyTileStorage());
    EXPECT_FALSE(prior->entities->HasLegacyStorage());

    publication.ScheduleNext(jobs, entities);
    jobs.Join();
    ASSERT_TRUE(publication.BeginFrame(jobs, entities, 4, false, EntityPublicationProfile::gpuTerrainOnly));
    EXPECT_EQ(publication.GetGeneration()->sourceTick, 502u);
    EXPECT_EQ(publication.GetGeneration()->map->GetSurfaceChunks()[0]->records[34].terrain.baseZ, 64);
    EXPECT_EQ(prior->map->GetSurfaceChunks()[0]->records[34].terrain.baseZ, 48);
    publication.Reset(jobs);
}

TEST_F(PublicationSnapshotParityTest, FailedWorkerIsReportedOnceAndCannotPoisonRetryOrReset)
{
    JobPool jobs(1);
    std::optional<JobPool::TaskGroup> pending{ jobs.CreateTaskGroup() };
    std::promise<void> executed;
    jobs.AddTask(*pending, [&]() {
        executed.set_value();
        throw std::runtime_error("publication worker failure");
    });
    executed.get_future().wait();
    EXPECT_THROW(Detail::WaitAndReleasePresentationTask(jobs, pending), std::runtime_error);
    EXPECT_FALSE(pending.has_value());
    EXPECT_NO_THROW(Detail::WaitAndReleasePresentationTask(jobs, pending));

    bool rebuilt = false;
    pending.emplace(jobs.CreateTaskGroup());
    jobs.AddTask(*pending, [&]() { rebuilt = true; });
    EXPECT_NO_THROW(Detail::WaitAndReleasePresentationTask(jobs, pending));
    EXPECT_TRUE(rebuilt);
    EXPECT_FALSE(pending.has_value());
}

TEST_F(PublicationSnapshotParityTest, NativeTerrainCatalogReplacementRefreshesPendingSourcesAtSameDrawBoundary)
{
    auto& jobs = context->GetJobPool();
    auto& entities = getGameState().entities;
    getGameState().currentTicks = 600;
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto held = scene->GetGeneration();
    const auto heldMaterials = held->map->GetTerrainMaterials();
    ASSERT_NE(heldMaterials, nullptr);
    const auto heldHeight = held->map->GetSurfaceChunks()[0]->records[34].terrain.baseZ;

    MutateMapAndEntity(48);
    getGameState().currentTicks = 601;
    scene->ScheduleNext(jobs, entities);
    MutateMapAndEntity(64);
    getGameState().currentTicks = 602;
    // This is the production object load/unload identity operation. No graphics/G1 access is required
    // to exercise publication's catalog lifetime barrier or its already queued older map capture.
    AdvanceTerrainObjectRevision();
    ASSERT_TRUE(scene->BeginFrame(jobs, entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto current = scene->GetGeneration();
    ASSERT_NE(current->map->GetTerrainMaterials(), nullptr);
    EXPECT_EQ(current->map->GetTerrainMaterials()->revision, GetTerrainObjectRevision());
    EXPECT_NE(current->map->GetTerrainMaterials(), heldMaterials);
    EXPECT_EQ(current->sourceTick, 602u);
    EXPECT_EQ(current->map->GetSourceTick(), 602u);
    EXPECT_EQ(current->entities->GetSourceTick(), 602u);
    EXPECT_EQ(current->map->GetSurfaceChunks()[0]->records[34].terrain.baseZ, 64);
    EXPECT_FALSE(current->map->HasLegacyTileStorage());
    EXPECT_FALSE(current->entities->HasLegacyStorage());
    EXPECT_EQ(held->sourceTick, 600u);
    EXPECT_EQ(held->map->GetSurfaceChunks()[0]->records[34].terrain.baseZ, heldHeight);
    EXPECT_LT(heldMaterials->revision, current->map->GetTerrainMaterials()->revision);
    EXPECT_FALSE(scene->BeginFrame(jobs, entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
}
