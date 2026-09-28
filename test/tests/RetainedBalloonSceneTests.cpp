/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include <cstring>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/drawing/RetainedBalloonScene.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/entity/Litter.h>
#include <openrct2/paint/Paint.SessionFlags.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/paint/tile_element/Paint.TileElement.h>
#include <openrct2/world/Map.h>
#include <stdexcept>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

namespace
{
    Balloon MakeBalloon(uint16_t id, Colour colour = Colour::brightRed)
    {
        Balloon value{};
        value.id = EntityId::FromUnderlying(id);
        value.type = EntityType::balloon;
        value.x = 64;
        value.y = 96;
        value.z = 200;
        value.spriteData.width = 13;
        value.spriteData.heightMin = 22;
        value.spriteData.heightMax = 11;
        value.colour = colour;
        return value;
    }

    void Append(EntityVisualChangeBatch& batch, const Balloon& balloon, uint32_t generation = 1)
    {
        EntityVisualChange change{};
        change.handle = { batch.epoch, balloon.id, generation };
        change.type = EntityType::balloon;
        change.present = true;
        change.payloadOffset = static_cast<uint32_t>(batch.payload.size());
        change.payloadSize = static_cast<uint16_t>(sizeof(balloon));
        const auto* data = reinterpret_cast<const std::byte*>(&balloon);
        batch.payload.insert(batch.payload.end(), data, data + sizeof(balloon));
        batch.changes.push_back(change);
    }

    void Remove(EntityVisualChangeBatch& batch, EntityId id, uint32_t generation = 1)
    {
        EntityVisualChange change{};
        change.handle = { batch.epoch, id, generation };
        batch.changes.push_back(change);
    }
} // namespace

TEST(RetainedBalloonSceneTest, LatestSnapshotKeepsSkippedRemovalAndUnchangedChunks)
{
    RetainedBalloonScene scene;
    auto first = MakeBalloon(0);
    auto second = MakeBalloon(64);
    auto third = MakeBalloon(128);
    EntityVisualChangeBatch bootstrap{ .epoch = 7, .reset = true };
    Append(bootstrap, first);
    Append(bootstrap, second);
    Append(bootstrap, third);
    ASSERT_TRUE(scene.Apply(bootstrap, 1));
    const auto before = scene.GetSnapshot();
    ASSERT_EQ(before->count, 3u);
    // Renderer skips publication 2 entirely, but publication 3 contains the cumulative deletion.
    EntityVisualChangeBatch deletion{ .epoch = 7 };
    Remove(deletion, first.id);
    ASSERT_TRUE(scene.Apply(deletion, 2));
    second.colour = Colour::darkBlue;
    EntityVisualChangeBatch appearance{ .epoch = 7 };
    Append(appearance, second);
    ASSERT_TRUE(scene.Apply(appearance, 3));
    const auto latest = scene.GetSnapshot();
    ASSERT_EQ(latest->count, 2u);
    EXPECT_EQ(latest->TryGet(first.id), nullptr);
    ASSERT_NE(before->TryGet(first.id), nullptr);
    EXPECT_EQ(latest->TryGet(second.id)->colour, Colour::darkBlue);
    EXPECT_EQ(before->TryGet(second.id)->colour, Colour::brightRed);
    EXPECT_EQ(latest->chunks[2], before->chunks[2]);
    EXPECT_EQ(latest->chunks[0]->gpuRevision, 2u);
    EXPECT_EQ(latest->chunks[1]->gpuRevision, 3u);
    // Ownership does not depend on the source packet remaining alive or unchanged.
    bootstrap.payload.clear();
    appearance.payload.assign(appearance.payload.size(), std::byte{ 0 });
    EXPECT_EQ(latest->TryGet(second.id)->z, 200);
    EXPECT_FALSE(scene.Apply(deletion, 2));
    EXPECT_EQ(scene.GetSnapshot(), latest);
}

TEST(RetainedBalloonSceneTest, CompatibilityOnlyChangesDoNotDirtyGpuRecords)
{
    RetainedBalloonScene scene;
    auto value = MakeBalloon(4);
    EntityVisualChangeBatch initial{ .epoch = 1, .reset = true };
    Append(initial, value);
    ASSERT_TRUE(scene.Apply(initial, 1));
    const auto before = scene.GetSnapshot();
    value.timeToMove = 2;
    value.orientation = 19;
    EntityVisualChangeBatch change{ .epoch = 1 };
    Append(change, value);
    ASSERT_TRUE(scene.Apply(change, 2));
    const auto after = scene.GetSnapshot();
    EXPECT_EQ(after->chunks[0]->revision, 2u);
    EXPECT_EQ(after->chunks[0]->gpuRevision, 1u);
    EXPECT_EQ(after->chunks[0]->records[4], before->chunks[0]->records[4]);
    EXPECT_EQ(after->TryGet(value.id)->timeToMove, 2u);
    EXPECT_EQ(after->TryGet(value.id)->orientation, 19u);
    EXPECT_EQ(before->TryGet(value.id)->timeToMove, 0u);
    EXPECT_EQ(std::memcmp(after->TryGet(value.id), &value, sizeof(value)), 0);
    EntityVisualChangeBatch idle{ .epoch = 1 };
    ASSERT_TRUE(scene.Apply(idle, 3));
    EXPECT_EQ(scene.GetSnapshot()->chunks[0], after->chunks[0]);
}

TEST(RetainedBalloonSceneTest, SlotReuseAcrossFamiliesRejectsOldGeneration)
{
    RetainedBalloonScene scene;
    auto value = MakeBalloon(3);
    EntityVisualChangeBatch initial{ .epoch = 4, .reset = true };
    Append(initial, value);
    ASSERT_TRUE(scene.Apply(initial, 1));
    EntityVisualChangeBatch replacement{ .epoch = 4 };
    EntityVisualChange other{};
    other.handle = { 4, value.id, 2 };
    other.present = true;
    other.type = EntityType::litter;
    replacement.changes.push_back(other);
    ASSERT_TRUE(scene.Apply(replacement, 2));
    EXPECT_EQ(scene.GetSnapshot()->TryGet(value.id), nullptr);
    EXPECT_EQ(scene.GetSnapshot()->count, 0u);
    const auto removed = scene.GetSnapshot();
    EntityVisualChangeBatch stale{ .epoch = 4 };
    Append(stale, value, 1);
    EXPECT_THROW(scene.Apply(stale, 3), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot(), removed);
    EntityVisualChangeBatch recreated{ .epoch = 4 };
    value.colour = Colour::yellow;
    Append(recreated, value, 3);
    ASSERT_TRUE(scene.Apply(recreated, 3));
    EXPECT_EQ(scene.GetSnapshot()->count, 1u);
    EXPECT_EQ(scene.GetSnapshot()->chunks[0]->records[3].generation, 3u);
    EXPECT_EQ(removed->TryGet(value.id), nullptr);
}

TEST(RetainedBalloonSceneTest, CoalescedSlotGenerationWrapDoesNotResurrectOldHandles)
{
    RetainedBalloonScene scene;
    auto value = MakeBalloon(8);
    EntityVisualChangeBatch initial{ .epoch = 3, .reset = true };
    Append(initial, value, UINT32_MAX);
    ASSERT_TRUE(scene.Apply(initial, 1));
    EntityVisualChangeBatch reuse{ .epoch = 3 };
    value.colour = Colour::yellow;
    Append(reuse, value, 1);
    ASSERT_TRUE(scene.Apply(reuse, 2));
    EXPECT_EQ(scene.GetSnapshot()->chunks[0]->records[8].generation, 1u);
    EntityVisualChangeBatch stale{ .epoch = 3 };
    Remove(stale, value.id, UINT32_MAX);
    EXPECT_THROW(scene.Apply(stale, 3), std::invalid_argument);
    ASSERT_NE(scene.GetSnapshot()->TryGet(value.id), nullptr);
    EXPECT_EQ(scene.GetSnapshot()->TryGet(value.id)->colour, Colour::yellow);
}

TEST(RetainedBalloonSceneTest, InvalidBatchAndMissingDeltaCannotPartiallyPublish)
{
    RetainedBalloonScene scene;
    auto value = MakeBalloon(1);
    EntityVisualChangeBatch initial{ .epoch = 5, .reset = true };
    Append(initial, value);
    ASSERT_TRUE(scene.Apply(initial, 1));
    const auto before = scene.GetSnapshot();
    EntityVisualChangeBatch gap{ .epoch = 5 };
    EXPECT_THROW(scene.Apply(gap, 3), std::invalid_argument);
    EntityVisualChangeBatch malformed{ .epoch = 5 };
    value.colour = Colour::yellow;
    Append(malformed, value);
    auto other = MakeBalloon(65);
    Append(malformed, other);
    malformed.changes.back().payloadOffset = UINT32_MAX;
    EXPECT_THROW(scene.Apply(malformed, 2), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot(), before);
    EXPECT_EQ(before->TryGet(value.id)->colour, Colour::brightRed);
    malformed.changes.back() = malformed.changes.front();
    EXPECT_THROW(scene.Apply(malformed, 2), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot(), before);
    EntityVisualChangeBatch reset{ .epoch = 6, .reset = true };
    Append(reset, other);
    ASSERT_TRUE(scene.Apply(reset, 10));
    EXPECT_EQ(scene.GetSnapshot()->TryGet(value.id), nullptr);
    ASSERT_NE(before->TryGet(value.id), nullptr);
    EntityVisualChangeBatch oldEpoch{ .epoch = 5, .reset = true };
    EXPECT_THROW(scene.Apply(oldEpoch, 11), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot()->epoch, 6u);
}

class RetainedBalloonHookTest : public testing::Test
{
protected:
    bool oldHeadless = gOpenRCT2Headless;
    bool oldNoGraphics = gOpenRCT2NoGraphics;
    std::unique_ptr<IContext> context;
    RetainedBalloonScene scene;
    std::unique_ptr<PresentationScene> publication;
    uint64_t sequence{};

    void SetUp() override
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        context = CreateContext();
        ASSERT_NE(context, nullptr);
        ASSERT_TRUE(context->Initialise());
        MapInit({ 16, 16 });
        getGameState().entities.resetAllEntities();
        publication = std::make_unique<PresentationScene>();
    }
    void TearDown() override
    {
        if (publication != nullptr)
        {
            publication->Reset(context->GetJobPool());
            publication.reset();
        }
        context.reset();
        gOpenRCT2Headless = oldHeadless;
        gOpenRCT2NoGraphics = oldNoGraphics;
    }
    void Publish()
    {
        const auto batch = getGameState().entities.ConsumeEntityVisualChanges();
        ASSERT_EQ(batch.changes.size(), 1u);
        ASSERT_TRUE(scene.Apply(batch, ++sequence));
    }
};

TEST_F(RetainedBalloonHookTest, NativeCategoriesSurviveTileResetButNotSessionReuse)
{
    // Cull tile rasterization after the real per-tile reset, avoiding any synthetic renderer or asset oracle.
    RenderTarget target{ .x = 0, .y = 100000, .width = 32, .height = 32 };
    auto session = std::make_unique<PaintSession>();
    const uint8_t nativeMasks[] = { 0, PaintSessionFlags::SurfaceBaseDrawn, PaintSessionFlags::EntitiesDrawn,
                                    PaintSessionFlags::SurfaceBaseDrawn | PaintSessionFlags::EntitiesDrawn };
    for (const auto native : nativeMasks)
    {
        for (const bool preview : { false, true })
        {
            PaintSessionInitialise(*session, target, 0, 0);
            session->Flags = static_cast<uint8_t>(
                native | PaintSessionFlags::PassedSurface | PaintSessionFlags::IsTrackPiecePreview);
            TileElementPaintSetup(*session, { 64, 64 }, preview);
            const auto expected = native | (preview ? PaintSessionFlags::IsTrackPiecePreview : 0);
            EXPECT_EQ(session->Flags, expected);
            // A later tile must not restore stale tile-local flags or lose either native category.
            session->Flags |= PaintSessionFlags::PassedSurface;
            TileElementPaintSetup(*session, { 96, 96 }, preview);
            EXPECT_EQ(session->Flags, expected);
            EXPECT_TRUE(session->paintEntries.fixedPaintEntries.empty());
            EXPECT_FALSE(session->paintEntries.dynamicPaintEntries.has_value());
            PaintSessionInitialise(*session, target, 0, 0);
            EXPECT_EQ(session->Flags, 0);
        }
    }
}

TEST_F(RetainedBalloonHookTest, CreateMovePopAppearanceAndRemovalPublishFinalState)
{
    auto& registry = getGameState().entities;
    Balloon::create({ 64, 64, 200 }, Colour::brightRed, false);
    ASSERT_EQ(registry.getEntityListCount(EntityType::balloon), 1u);
    auto* balloon = registry.GetEntityExecutionList(EntityType::balloon).front()->cast<Balloon>();
    const auto id = balloon->id;
    Publish();
    ASSERT_NE(scene.GetSnapshot(), nullptr);
    ASSERT_NE(scene.GetSnapshot()->TryGet(id), nullptr);
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->spriteData.heightMin, 22u);
    const auto initialGpuRevision = scene.GetSnapshot()->chunks[id.ToUnderlying() / kRetainedBalloonChunkWidth]->gpuRevision;
    for (uint8_t tick = 1; tick <= 2; ++tick)
    {
        balloon->update();
        Publish();
        EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->timeToMove, tick);
        EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->frame, 0u);
        EXPECT_EQ(scene.GetSnapshot()->chunks[id.ToUnderlying() / kRetainedBalloonChunkWidth]->gpuRevision, initialGpuRevision);
    }
    balloon->update();
    Publish();
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->z, 201);
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->frame, 1u);
    balloon->setColour(Colour::yellow);
    Publish();
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->colour, Colour::yellow);
    balloon->pop(false);
    Publish();
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->popped, 1u);
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->frame, 0u);
    for (uint16_t frame = 1; frame < 5; ++frame)
    {
        balloon->update();
        Publish();
        EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->frame, frame);
        EXPECT_EQ(scene.GetSnapshot()->TryGet(id)->z, 201);
    }
    const auto beforeRemoval = scene.GetSnapshot();
    balloon->update();
    Publish();
    EXPECT_EQ(registry.tryGetEntity(id), nullptr);
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id), nullptr);
    EXPECT_EQ(scene.GetSnapshot()->count, 0u);
    EXPECT_EQ(beforeRemoval->TryGet(id)->frame, 4u);
}

TEST_F(RetainedBalloonHookTest, PublicationProfileBootstrapsWithoutChangingSoftwareCapture)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    Balloon::create({ 64, 64, 200 }, Colour::brightRed, false);
    auto* balloon = registry.GetEntityExecutionList(EntityType::balloon).front()->cast<Balloon>();
    const auto id = balloon->id;
    auto* litter = registry.createEntity<Litter>();
    ASSERT_NE(litter, nullptr);
    litter->moveTo({ 64, 64, 16 });
    registry.updateEntitiesSpatialIndex();
    std::array<std::byte, sizeof(Balloon)> balloonBefore{};
    std::memcpy(balloonBefore.data(), balloon, sizeof(Balloon));
    const auto checksumBefore = registry.getAllEntitiesChecksum().raw;
    const auto tickBefore = getGameState().currentTicks;
    // The software owner neither opts in nor consumes pending visual notifications.
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true));
    const auto software = publication->GetGeneration();
    EXPECT_EQ(software->balloons, nullptr);
    EXPECT_EQ(software->entities->GetRetainedBalloons(), nullptr);
    EXPECT_GT(software->entities->GetBalloonMetrics().bulkCopiedBytes, 0u);
    const auto outstanding = registry.ConsumeEntityVisualChanges();
    EXPECT_EQ(outstanding.changes.size(), 2u);
    // Bootstrap must rebuild from finalized registry even though the dirty worklist was just cleared.
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, false, EntityPublicationProfile::retainedBalloons));
    const auto retained = publication->GetGeneration();
    ASSERT_NE(retained->balloons, nullptr);
    EXPECT_EQ(retained->balloons, retained->entities->GetRetainedBalloons());
    EXPECT_EQ(retained->entities->TryGetEntity(id), retained->balloons->TryGet(id));
    EXPECT_EQ(std::memcmp(retained->entities->TryGetEntity(id), balloon, sizeof(Balloon)), 0);
    EXPECT_NE(retained->entities->TryGetEntity(litter->id), nullptr);
    EXPECT_EQ(retained->entities->GetEntityTileList({ 64, 64 }), registry.getEntityTileList({ 64, 64 }));
    const auto& metrics = retained->entities->GetBalloonMetrics();
    EXPECT_EQ(metrics.bulkCopiedBytes, 0u);
    EXPECT_EQ(metrics.worklistVisits, 0u);
    EXPECT_EQ(metrics.payloadCopiedBytes, sizeof(Balloon));
    EXPECT_EQ(metrics.compatibilityCopiedBytes, 2 * sizeof(Balloon));
    EXPECT_EQ(metrics.recordCopiedBytes, sizeof(RetainedBalloonRecord));
    EXPECT_EQ(metrics.fallbackIndexedBalloons, 1u);
    EXPECT_EQ(metrics.fallbackSlotVisits, kRetainedBalloonChunkWidth);
    EXPECT_EQ(metrics.GetCopiedBalloonBytes(), 3 * sizeof(Balloon) + sizeof(RetainedBalloonRecord));
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 3, true, EntityPublicationProfile::retainedBalloons));
    const auto idle = publication->GetGeneration();
    EXPECT_EQ(
        idle->balloons->chunks[id.ToUnderlying() / kRetainedBalloonChunkWidth],
        retained->balloons->chunks[id.ToUnderlying() / kRetainedBalloonChunkWidth]);
    EXPECT_EQ(idle->entities->GetBalloonMetrics().payloadCopiedBytes, 0u);
    EXPECT_EQ(idle->entities->GetBalloonMetrics().compatibilityCopiedBytes, 0u);
    EXPECT_EQ(idle->entities->GetBalloonMetrics().recordCopiedBytes, 0u);
    EXPECT_EQ(idle->entities->GetBalloonMetrics().bulkCopiedBytes, 0u);
    EXPECT_EQ(idle->entities->GetBalloonMetrics().fallbackIndexedBalloons, 1u);
    EXPECT_NE(software->entities->TryGetEntity(id), idle->entities->TryGetEntity(id));
    EXPECT_EQ(getGameState().currentTicks, tickBefore);
    EXPECT_EQ(registry.getAllEntitiesChecksum().raw, checksumBefore);
    // The ordinary entity checksum omits miscellaneous balloons, so compare their authoritative bytes too.
    EXPECT_EQ(std::memcmp(balloonBefore.data(), balloon, sizeof(Balloon)), 0);
}

TEST_F(RetainedBalloonHookTest, PausedMutationAndDiscardedPreparedGenerationStayCoherent)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    Balloon::create({ 64, 64, 200 }, Colour::brightRed, false);
    auto* balloon = registry.GetEntityExecutionList(EntityType::balloon).front()->cast<Balloon>();
    const auto id = balloon->id;
    auto* litter = registry.createEntity<Litter>();
    ASSERT_NE(litter, nullptr);
    litter->moveTo({ 64, 64, 16 });
    registry.updateEntitiesSpatialIndex();
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, EntityPublicationProfile::retainedBalloons));
    const auto original = publication->GetGeneration();
    const auto sourceTick = getGameState().currentTicks;
    balloon->setColour(Colour::yellow);
    publication->ScheduleNext(jobs, registry);
    // Same logical tick: a script/edit changes state after the prepared snapshot was captured.
    balloon->setColour(Colour::darkBlue);
    balloon->moveTo({ 96, 64, 211 });
    litter->moveTo({ 96, 64, 16 });
    registry.updateEntitiesSpatialIndex();
    ASSERT_EQ(getGameState().currentTicks, sourceTick);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, true, EntityPublicationProfile::retainedBalloons));
    const auto changed = publication->GetGeneration();
    ASSERT_NE(changed->balloons->TryGet(id), nullptr);
    EXPECT_EQ(changed->balloons->TryGet(id)->colour, Colour::darkBlue);
    EXPECT_EQ(changed->balloons->TryGet(id)->x, 96);
    EXPECT_EQ(changed->entities->TryGetEntity(litter->id)->x, 96);
    EXPECT_EQ(changed->entities->GetEntityTileList({ 96, 64 }), registry.getEntityTileList({ 96, 64 }));
    EXPECT_EQ(changed->entities->GetBalloonMetrics().sourceTick, sourceTick);
    EXPECT_EQ(changed->entities->GetBalloonMetrics().payloadCopiedBytes, sizeof(Balloon));
    EXPECT_EQ(changed->entities->GetBalloonMetrics().worklistVisits, 4u);
    EXPECT_EQ(original->balloons->TryGet(id)->colour, Colour::brightRed);
    EXPECT_EQ(original->entities->TryGetEntity(litter->id)->x, 64);
    const auto copied = publication->GetBalloonPublicationCopyTotals();
    EXPECT_EQ(copied.captures, 3u);
    EXPECT_EQ(copied.payloadCopiedBytes, 4 * sizeof(Balloon));
    EXPECT_EQ(copied.bulkCopiedBytes, 0u);
    // The yellow prepared state was never admitted, but its producer copies are included.
    EXPECT_GT(copied.GetCopiedBalloonBytes(), changed->entities->GetBalloonMetrics().GetCopiedBalloonBytes());
    // Drop a visual generation containing the only tombstone; the latest complete snapshot still removes it.
    registry.entityRemove(balloon);
    publication->ScheduleNext(jobs, registry);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 3, true, EntityPublicationProfile::retainedBalloons));
    EXPECT_EQ(publication->GetGeneration()->balloons->TryGet(id), nullptr);
    EXPECT_EQ(publication->GetGeneration()->entities->TryGetEntity(id), nullptr);
    EXPECT_NE(changed->entities->TryGetEntity(id), nullptr);
}

TEST_F(RetainedBalloonHookTest, ProfileSwitchAndEntityEpochResetCannotRetainOldSlots)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    Balloon::create({ 64, 64, 200 }, Colour::brightRed, false);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, EntityPublicationProfile::retainedBalloons));
    const auto original = publication->GetGeneration();
    const auto originalEpoch = original->balloons->epoch;
    publication->ScheduleNext(jobs, registry);
    registry.resetAllEntities();
    Balloon::create({ 96, 64, 300 }, Colour::yellow, false);
    auto* balloon = registry.GetEntityExecutionList(EntityType::balloon).front()->cast<Balloon>();
    const auto id = balloon->id;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, false, EntityPublicationProfile::retainedBalloons));
    const auto reset = publication->GetGeneration();
    EXPECT_NE(reset->balloons->epoch, originalEpoch);
    EXPECT_EQ(reset->balloons->TryGet(id)->x, 96);
    EXPECT_EQ(original->balloons->TryGet(id)->x, 64);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 3, false));
    EXPECT_EQ(publication->GetGeneration()->balloons, nullptr);
    balloon->setColour(Colour::darkBlue);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 4, true));
    EXPECT_EQ(registry.ConsumeEntityVisualChanges().changes.size(), 1u);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 5, false, EntityPublicationProfile::retainedBalloons));
    EXPECT_EQ(publication->GetGeneration()->balloons->TryGet(id)->colour, Colour::darkBlue);
    EXPECT_GT(publication->GetGeneration()->balloons->sequence, reset->balloons->sequence);
    EXPECT_EQ(reset->balloons->TryGet(id)->colour, Colour::yellow);
}
