/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <cstring>
#include <future>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/core/JobPool.h>
#include <openrct2/drawing/PresentationScene.h>
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
        EXPECT_EQ(std::memcmp(retained->map->GetFirstElementAt({ 15, 15 }),
                              restored->map->GetFirstElementAt({ 15, 15 }), sizeof(TileElement)), 0);
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
    scene->Reset(jobs); // Waits and discards that pending publication.
    MutateMapAndEntity(64); // The fresh snapshot must include this change AND every unchanged tile.
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
