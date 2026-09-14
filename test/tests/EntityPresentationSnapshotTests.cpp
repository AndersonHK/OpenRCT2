/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Litter.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/paint/vehicle/VehiclePaint.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideEntry.h>
#include <openrct2/ride/Vehicle.h>

using namespace OpenRCT2;

class EntityPresentationSnapshotTests : public testing::Test
{
protected:
    std::unique_ptr<IContext> context;

    void SetUp() override
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        context = CreateContext();
        ASSERT_NE(context, nullptr);
        ASSERT_TRUE(context->Initialise());
        getGameState().entities.resetAllEntities();
    }

    void TearDown() override
    {
        context.reset();
    }
};

TEST_F(EntityPresentationSnapshotTests, SplashBoatPaintRejectsEmptyTrainsAndCapturedRecursion)
{
    auto& objectManager = context->GetObjectManager();
    ASSERT_NE(objectManager.LoadObject("rct2.ride.spboat"), nullptr);
    auto* ride = RideAllocateAtIndex(RideId::FromUnderlying(0));
    ASSERT_NE(ride, nullptr);
    ride->type = RIDE_TYPE_SPLASH_BOATS;
    ride->subtype = objectManager.GetLoadedObjectEntryIndex("rct2.ride.spboat");
    const auto* entry = ride->getRideEntry();
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->zero_cars, 2);
    ASSERT_EQ(entry->Cars[1].paintStyle, VehiclePaintStyle::splashBoatsOrWaterCoaster);
    auto& entities = getGameState().entities;
    auto* head = entities.createEntity<Vehicle>();
    auto* tail = entities.createEntity<Vehicle>();
    ASSERT_NE(head, nullptr);
    ASSERT_NE(tail, nullptr);
    for (auto* vehicle : { head, tail })
    {
        vehicle->ride = ride->id;
        vehicle->ride_subtype = ride->subtype;
        vehicle->vehicle_type = 1;
        vehicle->flags = {};
    }
    head->SubType = Vehicle::Type::head;
    tail->SubType = Vehicle::Type::tail;
    head->next_vehicle_on_ride = tail->id;
    tail->prev_vehicle_on_ride = head->id;
    const std::array<CoordsXY, 0> noTiles{};
    const auto snapshot = EntityPresentationSnapshot::Capture(entities, noTiles);
    ScopedEntityPresentationSnapshot scope(snapshot.get());
    auto* capturedHead = GetEntityForPresentation<Vehicle>(head->id);
    ASSERT_NE(capturedHead, nullptr);
    Drawing::RenderTarget rt{};
    std::unique_ptr<PaintSession, decltype(&PaintSessionFree)> session(PaintSessionAlloc(rt, 0, 0), PaintSessionFree);
    ASSERT_NE(session, nullptr);
    for (uint8_t count : { 1, 2 })
    {
        ride->numCarsPerTrain = count;
        session->CurrentlyDrawnEntity = nullptr;
        VehicleVisualSplashBoatsOrWaterCoaster(*session, 0, 0, 0, 0, capturedHead, &entry->Cars[1]);
        EXPECT_FALSE(static_cast<bool>(session->CurrentlyDrawnEntity));
    }
    // Live ride size now permits painting, but the captured two-proxy chain still loops.
    ride->numCarsPerTrain = 3;
    head->next_vehicle_on_ride = EntityId::GetNull();
    for (int repeat = 0; repeat < 2; repeat++)
    {
        session->CurrentlyDrawnEntity = nullptr;
        VehicleVisualSplashBoatsOrWaterCoaster(*session, 0, 0, 0, 0, capturedHead, &entry->Cars[1]);
        EXPECT_EQ(session->CurrentlyDrawnEntity.id, head->id);
        EXPECT_EQ(session->LastPS, nullptr);
    }
    EXPECT_TRUE(head->next_vehicle_on_ride.IsNull());
    EXPECT_EQ(capturedHead->next_vehicle_on_ride, tail->id);
}

TEST_F(EntityPresentationSnapshotTests, CapturesOnlyRequestedSpatialBucketsAndKeepsOwnedState)
{
    auto& entities = getGameState().entities;
    auto* visible = entities.createEntity<Litter>();
    auto* hidden = entities.createEntity<Litter>();
    ASSERT_NE(visible, nullptr);
    ASSERT_NE(hidden, nullptr);
    visible->moveTo({ 32, 64, 8 });
    hidden->moveTo({ 320, 640, 16 });
    entities.updateEntitiesSpatialIndex();

    const std::array tiles{ CoordsXY{ 32, 64 } };
    const auto snapshot = EntityPresentationSnapshot::Capture(entities, tiles);
    ASSERT_NE(snapshot->TryGetEntity(visible->id), nullptr);
    EXPECT_EQ(snapshot->TryGetEntity(hidden->id), nullptr);
    ASSERT_EQ(snapshot->GetEntityTileList({ 32, 64 }).size(), 1);
    EXPECT_EQ(snapshot->GetEntityTileList({ 32, 64 })[0], visible->id);
    EXPECT_TRUE(snapshot->GetEntityTileList({ 320, 640 }).empty());

    const auto originalLocation = snapshot->TryGetEntity(visible->id)->getLocation();
    visible->moveTo({ 96, 128, 24 });
    entities.updateEntitiesSpatialIndex();
    EXPECT_EQ(snapshot->TryGetEntity(visible->id)->getLocation(), originalLocation);
}

TEST_F(EntityPresentationSnapshotTests, CapturesVehicleAndRiderLookupClosureOutsideVisibleBuckets)
{
    auto& entities = getGameState().entities;
    auto* vehicle = entities.createEntity<Vehicle>();
    auto* rider = entities.createEntity<Guest>();
    ASSERT_NE(vehicle, nullptr);
    ASSERT_NE(rider, nullptr);
    vehicle->peep[0] = rider->id;

    const std::array<CoordsXY, 0> noTiles{};
    const auto snapshot = EntityPresentationSnapshot::Capture(entities, noTiles);
    EXPECT_NE(snapshot->TryGetEntity(vehicle->id), nullptr);
    EXPECT_NE(snapshot->TryGetEntity(rider->id), nullptr);
    EXPECT_EQ(snapshot->GetCapturedEntityCount(), 2);

    EXPECT_EQ(GetCurrentEntityPresentationSnapshot(), nullptr);
    {
        ScopedEntityPresentationSnapshot scope(snapshot.get());
        EXPECT_EQ(GetEntityForPresentation<Vehicle>(vehicle->id), snapshot->TryGetEntity(vehicle->id)->cast<Vehicle>());
        EXPECT_EQ(GetEntityForPresentation<Guest>(rider->id), snapshot->TryGetEntity(rider->id)->cast<Guest>());
    }
    EXPECT_EQ(GetCurrentEntityPresentationSnapshot(), nullptr);
}
