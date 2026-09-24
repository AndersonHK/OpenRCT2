// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <array>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/core/JobPool.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/drawing/RetainedPeepState.h>
#include <openrct2/entity/Balloon.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Litter.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/interface/WindowBase.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/world/Map.h>
#include <stdexcept>
#include <utility>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

class NativePeepPublicationTest : public testing::Test
{
protected:
    bool oldHeadless = gOpenRCT2Headless;
    bool oldNoGraphics = gOpenRCT2NoGraphics;
    uint32_t oldTick = getGameState().currentTicks;
    std::unique_ptr<IContext> context;
    std::unique_ptr<PresentationScene> publication;
    std::vector<std::unique_ptr<WindowBase>> savedWindows;
    Viewport viewport;
    bool ownsWindowList{};
    const std::array<uint32_t, 1> objectGenerations{ 1 };
    std::shared_ptr<const RetainedPeepAnimationCatalog> catalog;

    void SetUp() override
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        context = CreateContext();
        ASSERT_NE(context, nullptr);
        ASSERT_TRUE(context->Initialise());
        EntityTweener::get().reset();
        MapInit({ 16, 16 });
        getGameState().entities.resetAllEntities();
        getGameState().currentTicks = 10;
        publication = std::make_unique<PresentationScene>();
        const auto object = BuildRetainedPeepAnimationObject(
            { 0, 1, 1, 0, 100, 64, 0, 0 }, std::array{ RetainedPeepAnimationSource{ 0, 0, 100 } });
        catalog = PublishRetainedPeepAnimationCatalog(
            nullptr, 1, 1, true, std::array{ RetainedPeepAnimationChange{ 0, 1, object } });

        // Register a real visible viewport in the same list traversed by production preTick.
        // No native window, drawing engine, UI library or GPU is acquired.
        savedWindows.swap(gWindowList);
        ownsWindowList = true;
        viewport.viewPos = { -1024, -1024 };
        viewport.width = viewport.height = 2048;
        viewport.zoom = ZoomLevel{ 0 };
        viewport.rotation = 0;
        viewport.isVisible = true;
        auto window = std::make_unique<WindowBase>();
        window->viewport = &viewport;
        window->isVisible = true;
        gWindowList.push_back(std::move(window));
    }

    void TearDown() override
    {
        EntityTweener::get().restore();
        EntityTweener::get().reset();
        if (publication)
            publication->Reset(context->GetJobPool());
        publication.reset();
        if (ownsWindowList)
            gWindowList.clear();
        context.reset();
        if (ownsWindowList)
            savedWindows.swap(gWindowList);
        getGameState().currentTicks = oldTick;
        gOpenRCT2Headless = oldHeadless;
        gOpenRCT2NoGraphics = oldNoGraphics;
    }

    template<typename T = Guest>
    T* NewPeep(const CoordsXYZ& position = { 64, 96, 16 })
    {
        auto* peep = getGameState().entities.createEntity<T>();
        if (peep == nullptr)
            throw std::runtime_error("Unable to allocate native publication fixture peep");
        peep->animationObjectIndex = 0;
        peep->state = PeepState::walking;
        peep->spriteData.width = 8;
        peep->spriteData.heightMin = 5;
        peep->spriteData.heightMax = 24;
        peep->moveTo(position);
        return peep;
    }

    std::shared_ptr<const RetainedPeepSnapshot> CapturePeeps()
    {
        const auto input = getGameState().entities.CaptureRetainedEntityPublication(
            getGameState().currentTicks, objectGenerations, true, false);
        RetainedPeepScene scene;
        if (!scene.Apply(input.peeps, 1))
            throw std::runtime_error("Native fixture bootstrap did not publish");
        return scene.GetSnapshot();
    }

    void MoveAcrossTick(
        Peep& peep, uint32_t previousTick, uint32_t sourceTick, CoordsXYZ previous = { 64, 96, 16 },
        CoordsXYZ current = { 96, 128, 32 })
    {
        auto& tweener = EntityTweener::get();
        tweener.reset();
        peep.moveTo(previous);
        getGameState().currentTicks = previousTick;
        tweener.preTick();
        peep.moveTo(current);
        getGameState().currentTicks = sourceTick;
        tweener.postTick();
    }
};

TEST_F(NativePeepPublicationTest, NativeCaptureReleasesLegacyStorageAndNeverRebuildsLegacyIndexes)
{
    auto& registry = getGameState().entities;
    auto* guest = NewPeep();
    const auto* staff = NewPeep<Staff>({ 96, 96, 16 });
    registry.updateEntitiesSpatialIndex();
    EntityPresentationSnapshot captured;
    captured.CaptureStorage(registry);
    captured.BuildCapturedStorage();
    ASSERT_TRUE(captured.HasLegacyStorage());
    ASSERT_NE(captured.TryGetEntity(guest->id), nullptr);
    const auto peeps = CapturePeeps();
    for (size_t repetition = 0; repetition < 3; ++repetition)
    {
        captured.CaptureNativeStorage(registry, peeps, catalog);
        captured.BuildCapturedStorage();
        EXPECT_TRUE(captured.IsNativeOnly());
        EXPECT_FALSE(captured.HasLegacyStorage());
        EXPECT_EQ(captured.TryGetEntity(guest->id), nullptr);
        EXPECT_EQ(captured.TryGetEntity(staff->id), nullptr);
        EXPECT_TRUE(captured.GetEntityTileList({ 64, 96 }).empty());
        EXPECT_TRUE(captured.GetEntityTileList({ 96, 96 }).empty());
        EXPECT_EQ(captured.GetRetainedPeeps(), peeps);
        EXPECT_EQ(captured.GetPeepAnimations(), catalog);
        EXPECT_EQ(captured.GetCapturedEntityCount(), 2u);
        EXPECT_EQ(captured.GetUnsupportedEntityCount(), 0u);
        EXPECT_EQ(captured.GetRetainedBalloons(), nullptr);
    }
    // Legacy delta storage must be reinitialised even when native capture kept the same source epoch.
    registry.AcknowledgeRetainedEntityPublication();
    registry.PublishEntityVisualState(*guest);
    const auto delta = registry.ConsumeEntityVisualChanges();
    ASSERT_FALSE(delta.reset);
    EXPECT_EQ(delta.epoch, captured.GetSourceEpoch());
    captured.Apply(delta);
    EXPECT_FALSE(captured.IsNativeOnly());
    EXPECT_TRUE(captured.HasLegacyStorage());
    ASSERT_NE(captured.TryGetEntity(guest->id), nullptr);
    captured.CaptureNativeStorage(registry, peeps, catalog);
    EXPECT_FALSE(captured.HasLegacyStorage());
    // Reuse the same object in the bulk legacy direction too.
    captured.CaptureStorage(registry);
    captured.BuildCapturedStorage();
    EXPECT_FALSE(captured.IsNativeOnly());
    EXPECT_TRUE(captured.HasLegacyStorage());
    ASSERT_NE(captured.TryGetEntity(guest->id), nullptr);
    EXPECT_EQ(captured.TryGetEntity(guest->id)->getLocation(), guest->getLocation());
}

TEST_F(NativePeepPublicationTest, UnsupportedFamiliesAreCountedWithoutCompatibilityPayloadOrClones)
{
    auto& registry = getGameState().entities;
    const auto* guest = NewPeep();
    static_cast<void>(NewPeep<Staff>());
    auto* balloon = registry.createEntity<Balloon>();
    auto* litter = registry.createEntity<Litter>();
    auto* vehicle = registry.createEntity<Vehicle>();
    ASSERT_NE(balloon, nullptr);
    ASSERT_NE(litter, nullptr);
    ASSERT_NE(vehicle, nullptr);
    const auto input = registry.CaptureRetainedEntityPublication(10, objectGenerations, true, false);
    EXPECT_TRUE(input.balloons.changes.empty());
    EXPECT_TRUE(input.balloons.payload.empty());
    EXPECT_EQ(input.balloons.changes.capacity(), 0u);
    EXPECT_EQ(input.balloons.payload.capacity(), 0u);
    EXPECT_EQ(input.bootstrapVisits, 2u); // Supported typed lists only, no whole-population extra traversal.
    EntityPresentationSnapshot captured;
    captured.CaptureNativeStorage(registry, CapturePeeps(), catalog);
    captured.BuildCapturedStorage();
    EXPECT_EQ(captured.GetUnsupportedEntityCount(), 3u);
    EXPECT_EQ(captured.GetCapturedEntityCount(), 5u);
    EXPECT_FALSE(captured.HasLegacyStorage());
    for (const auto id : { guest->id, balloon->id, litter->id, vehicle->id })
        EXPECT_EQ(captured.TryGetEntity(id), nullptr);
    registry.entityRemove(balloon);
    registry.entityRemove(litter);
    registry.entityRemove(vehicle);
    captured.CaptureNativeStorage(registry, CapturePeeps(), catalog);
    EXPECT_EQ(captured.GetUnsupportedEntityCount(), 0u);
    EXPECT_EQ(captured.GetCapturedEntityCount(), 2u);
}

TEST_F(NativePeepPublicationTest, PresentationOwnerPublishesHeldIndependentGroupsWithoutLegacyCopies)
{
    auto& registry = getGameState().entities;
    auto* guest = NewPeep();
    auto& jobs = context->GetJobPool();
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, EntityPublicationProfile::nativePeeps, catalog));
    const auto held = publication->GetGeneration();
    ASSERT_NE(held->peeps, nullptr);
    EXPECT_TRUE(held->entities->IsNativeOnly());
    EXPECT_FALSE(held->entities->HasLegacyStorage());
    EXPECT_EQ(held->entities->GetRetainedPeeps(), held->peeps);
    EXPECT_EQ(held->peepAnimations, catalog);
    const auto chunk = guest->id.ToUnderlying() / kRetainedPeepChunkWidth;
    const auto old = held->peeps->chunks[chunk];
    guest->setTShirtColour(Colour::brightRed);
    getGameState().currentTicks = 11;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, true, EntityPublicationProfile::nativePeeps, catalog));
    const auto current = publication->GetGeneration();
    EXPECT_EQ(current->sourceTick, 11u);
    EXPECT_EQ(held->sourceTick, 10u);
    EXPECT_EQ(current->peepAnimations, held->peepAnimations);
    const auto now = current->peeps->chunks[chunk];
    EXPECT_EQ(now->lifecycle, old->lifecycle);
    EXPECT_EQ(now->motion, old->motion);
    EXPECT_EQ(now->animation, old->animation);
    EXPECT_NE(now->appearance, old->appearance);
    EXPECT_NE(held->peeps->TryGet(guest->id)->colours, current->peeps->TryGet(guest->id)->colours);
    EXPECT_EQ(current->entities->TryGetEntity(guest->id), nullptr);
    EXPECT_FALSE(current->entities->HasLegacyStorage());
    const auto totals = publication->GetBalloonPublicationCopyTotals();
    EXPECT_EQ(totals.payloadCopiedBytes, 0u);
    EXPECT_EQ(totals.compatibilityCopiedBytes, 0u);
    EXPECT_EQ(totals.bulkCopiedBytes, 0u);
}

TEST_F(NativePeepPublicationTest, RealTweenPublishesEndpointsInsteadOfTemporaryLivePoseAndRestores)
{
    auto* guest = NewPeep();
    MoveAcrossTick(*guest, 10, 11);
    auto& tweener = EntityTweener::get();
    tweener.tween(0.25f);
    EXPECT_EQ(guest->getLocation(), (CoordsXYZ{ 72, 104, 20 }));
    EXPECT_FLOAT_EQ(tweener.GetRenderAlpha(), 0.25f);
    const auto history = tweener.GetMotion(*guest);
    ASSERT_TRUE(history);
    EXPECT_EQ(history->previous, (CoordsXYZ{ 64, 96, 16 }));
    EXPECT_EQ(history->current, (CoordsXYZ{ 96, 128, 32 }));
    ASSERT_TRUE(publication->BeginFrame(
        context->GetJobPool(), getGameState().entities, 1, true, EntityPublicationProfile::nativePeeps, catalog));
    const auto held = publication->GetGeneration();
    const auto record = held->peeps->TryGet(guest->id);
    ASSERT_TRUE(record);
    EXPECT_EQ(record->x, 96);
    EXPECT_EQ(record->y, 128);
    EXPECT_EQ(record->z, 32);
    EXPECT_EQ(record->previousX, 64);
    EXPECT_EQ(record->previousY, 96);
    EXPECT_EQ(record->previousZ, 16);
    EXPECT_EQ(record->sourceTick, 11u);
    EXPECT_EQ(record->previousTick, 10u);
    EXPECT_NE(record->flags & kRetainedPeepInterpolate, 0u);
    EXPECT_EQ(held->sourceTick, 11u);
    EXPECT_EQ(guest->getLocation(), (CoordsXYZ{ 72, 104, 20 })); // Capturing must not alter presentation pose.
    tweener.restore();
    EXPECT_EQ(guest->getLocation(), (CoordsXYZ{ 96, 128, 32 }));
    EXPECT_FLOAT_EQ(tweener.GetRenderAlpha(), 1.0f);
    ASSERT_TRUE(tweener.GetMotion(*guest));
    EXPECT_EQ(held->peeps->TryGet(guest->id), record);
}

TEST_F(NativePeepPublicationTest, NonAdjacentAndWrappedTicksKeepHonestEndpointHistory)
{
    auto* guest = NewPeep();
    for (const auto [previous, current] :
         std::array{ std::pair{ 10u, 14u }, std::pair{ UINT32_MAX, 0u }, std::pair{ UINT32_MAX - 1, 1u } })
    {
        SCOPED_TRACE(previous);
        SCOPED_TRACE(current);
        MoveAcrossTick(*guest, previous, current);
        EntityTweener::get().tween(0.5f);
        const auto history = EntityTweener::get().GetMotion(*guest);
        ASSERT_TRUE(history);
        EXPECT_EQ(history->previousTick, previous);
        EXPECT_EQ(history->sourceTick, current);
        const auto owned = CapturePeeps(); // Runs the actual field validator/publication, not just GetMotion.
        const auto record = owned->TryGet(guest->id);
        ASSERT_TRUE(record);
        EXPECT_EQ(record->previousTick, previous);
        EXPECT_EQ(record->sourceTick, current);
        EXPECT_NE(record->flags & kRetainedPeepInterpolate, 0u);
        EXPECT_EQ(record->x, 96);
        EXPECT_EQ(record->previousX, 64);
        EntityTweener::get().restore();
    }
}

TEST_F(NativePeepPublicationTest, StaleTickCopiedIdentityTeleportAndSlotReuseCannotBorrowHistory)
{
    auto& registry = getGameState().entities;
    auto& tweener = EntityTweener::get();
    auto* guest = NewPeep();
    MoveAcrossTick(*guest, 10, 11);
    tweener.tween(0.5f);
    const EntityBase copied = *guest;
    EXPECT_FALSE(tweener.GetMotion(copied));
    EXPECT_THROW(
        static_cast<void>(registry.CaptureRetainedEntityPublication(10, objectGenerations, false, false)),
        std::invalid_argument);
    // Failed stale-tick capture is non-consuming; the exact queued mutation is still available.
    const auto pending = registry.CaptureRetainedEntityPublication(11, objectGenerations, false, false);
    EXPECT_GT(pending.bootstrapVisits + pending.dirtyVisits, 0u);
    ASSERT_FALSE(pending.peeps.motion.empty());
    EXPECT_EQ(pending.peeps.motion.front().value.x, 96);
    tweener.restore();
    getGameState().currentTicks = 12;
    EXPECT_FALSE(tweener.GetMotion(*guest));
    const auto staleFallback = CapturePeeps()->TryGet(guest->id);
    ASSERT_TRUE(staleFallback);
    EXPECT_EQ(staleFallback->flags & kRetainedPeepInterpolate, 0u);
    EXPECT_EQ(staleFallback->sourceTick, 12u);
    EXPECT_EQ(staleFallback->x, 96);

    MoveAcrossTick(*guest, 12, 13);
    tweener.tween(0.25f);
    guest->moveTo({ 160, 192, 40 });
    EXPECT_FALSE(tweener.GetMotion(*guest));
    const auto teleport = CapturePeeps()->TryGet(guest->id);
    ASSERT_TRUE(teleport);
    EXPECT_EQ(teleport->x, 160);
    EXPECT_EQ(teleport->previousX, 160);
    EXPECT_EQ(teleport->flags & kRetainedPeepInterpolate, 0u);

    const auto id = guest->id;
    const auto handle = registry.GetEntityVisualHandle(id);
    const auto held = CapturePeeps();
    registry.entityRemove(guest);
    auto* replacement = NewPeep();
    ASSERT_EQ(replacement->id, id);
    EXPECT_NE(registry.GetEntityVisualHandle(id), handle);
    EXPECT_FALSE(tweener.GetMotion(*replacement));
    EXPECT_FALSE(tweener.GetMotion(copied));
    EXPECT_FLOAT_EQ(tweener.GetRenderAlpha(), 1.0f);
    const auto record = CapturePeeps()->TryGet(id);
    ASSERT_TRUE(record);
    EXPECT_EQ(record->flags & kRetainedPeepInterpolate, 0u);
    EXPECT_EQ(record->generation, registry.GetEntityVisualHandle(id).generation);
    EXPECT_EQ(held->TryGet(id)->generation, handle.generation);
    EXPECT_EQ(held->TryGet(id)->x, 160);
}

TEST_F(NativePeepPublicationTest, ZeroAndAmbiguousTickSpansCaptureAuthoritativePoseWithoutHistory)
{
    auto* guest = NewPeep();
    for (const auto [previous, current] :
         std::array{ std::pair{ 10u, 10u }, std::pair{ 10u, 10u + (uint32_t{ 1 } << 31) }, std::pair{ 10u, 9u } })
    {
        SCOPED_TRACE(previous);
        SCOPED_TRACE(current);
        MoveAcrossTick(*guest, previous, current);
        EntityTweener::get().tween(0.25f);
        ASSERT_TRUE(EntityTweener::get().GetMotion(*guest));
        const auto record = CapturePeeps()->TryGet(guest->id);
        ASSERT_TRUE(record);
        EXPECT_EQ(record->x, 96);
        EXPECT_EQ(record->y, 128);
        EXPECT_EQ(record->z, 32);
        EXPECT_EQ(record->previousX, record->x);
        EXPECT_EQ(record->previousY, record->y);
        EXPECT_EQ(record->previousZ, record->z);
        EXPECT_EQ(record->sourceTick, current);
        EXPECT_EQ(record->previousTick, current);
        EXPECT_EQ(record->flags & kRetainedPeepInterpolate, 0u);
        EntityTweener::get().restore();
    }
}
