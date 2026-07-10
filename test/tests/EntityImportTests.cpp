/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <array>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/core/FileStream.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Vehicle.Station.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/world/MapAnimation.h>

using namespace OpenRCT2;

class EntityImportTests : public testing::Test
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
    }

    void TearDown() override
    {
        context.reset();
    }
};

// This here tests that CreateEntityAt returns nullptr when trying to create an entity at an index that's already occupied.
// This behavior is what caused crashes before, corrupted saves had duplicate EntityIndex values that caused CreateEntityAt to
// return nullptr, which was then dereferenced.
TEST_F(EntityImportTests, CreateEntityAtDuplicateIndexReturnsNull)
{
    auto& gameState = getGameState();
    gameState.entities.ResetAllEntities();

    // Create an entity at index 100
    auto* entity1 = gameState.entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(100));
    ASSERT_NE(entity1, nullptr);
    EXPECT_EQ(entity1->id.ToUnderlying(), 100u);

    // Try to create another entity at the same index, which should return nullptr
    auto* entity2 = gameState.entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(100));
    EXPECT_EQ(entity2, nullptr);
}

TEST_F(EntityImportTests, VehicleHeadEntityListTracksAddsRemovalsAndReset)
{
    auto& entities = getGameState().entities;
    entities.ResetAllEntities();

    auto* tail = entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(10));
    auto* secondHead = entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(40));
    auto* firstHead = entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(20));
    ASSERT_NE(tail, nullptr);
    ASSERT_NE(firstHead, nullptr);
    ASSERT_NE(secondHead, nullptr);
    tail->SubType = Vehicle::Type::tail;
    firstHead->SubType = Vehicle::Type::head;
    secondHead->SubType = Vehicle::Type::head;

    const auto& initialHeads = entities.GetVehicleHeadEntityList();
    ASSERT_EQ(initialHeads.size(), 2u);
    EXPECT_EQ(initialHeads[0].ToUnderlying(), 20u);
    EXPECT_EQ(initialHeads[1].ToUnderlying(), 40u);

    auto* middleHead = entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(30));
    ASSERT_NE(middleHead, nullptr);
    middleHead->SubType = Vehicle::Type::head;

    const auto& headsAfterAdd = entities.GetVehicleHeadEntityList();
    ASSERT_EQ(headsAfterAdd.size(), 3u);
    EXPECT_EQ(headsAfterAdd[0].ToUnderlying(), 20u);
    EXPECT_EQ(headsAfterAdd[1].ToUnderlying(), 30u);
    EXPECT_EQ(headsAfterAdd[2].ToUnderlying(), 40u);

    entities.EntityRemove(firstHead);

    const auto& headsAfterRemove = entities.GetVehicleHeadEntityList();
    ASSERT_EQ(headsAfterRemove.size(), 2u);
    EXPECT_EQ(headsAfterRemove[0].ToUnderlying(), 30u);
    EXPECT_EQ(headsAfterRemove[1].ToUnderlying(), 40u);

    entities.ResetAllEntities();
    EXPECT_TRUE(entities.GetVehicleHeadEntityList().empty());
}

TEST_F(EntityImportTests, PassengerUnloadPlanPreservesThroughRidersAndOrdinaryUnloadRemovesEveryone)
{
    constexpr uint8_t passengerCount = 5;
    constexpr std::array<bool, passengerCount> shouldAlight{ false, true, false, true, false };
    constexpr std::array<uint8_t, passengerCount> expectedSourceIndices{ 0, 2, 4, 1, 3 };

    auto& entities = getGameState().entities;
    entities.ResetAllEntities();

    Vehicle vehicle{};
    vehicle.num_peeps = passengerCount;
    vehicle.next_free_seat = passengerCount;

    std::array<Guest*, passengerCount> passengers{};
    std::array<EntityId, passengerCount> passengerIds{};
    std::array<Drawing::Colour, passengerCount> passengerColours{};
    for (size_t index = 0; index < passengerCount; index++)
    {
        const auto entityId = EntityId::FromUnderlying(static_cast<uint16_t>(100 + index));
        auto* guest = entities.CreateEntityAt<Guest>(entityId);
        ASSERT_NE(guest, nullptr);
        guest->State = PeepState::walking;
        guest->RideSubState = PeepRideSubState::onRide;
        guest->CurrentSeat = static_cast<uint8_t>(index);

        const auto colour = static_cast<Drawing::Colour>(index);
        passengers[index] = guest;
        passengerIds[index] = entityId;
        passengerColours[index] = colour;
        vehicle.peep[index] = entityId;
        vehicle.peep_tshirt_colours[index] = colour;
    }

    const auto plan = RideVehicle::StationDetail::BuildPassengerUnloadPlan(shouldAlight);
    EXPECT_EQ(plan.passengerCount, passengerCount);
    EXPECT_EQ(plan.continuingCount, 3u);
    for (size_t index = 0; index < passengerCount; index++)
    {
        EXPECT_EQ(plan.sourceIndices[index], expectedSourceIndices[index]);
    }

    RideVehicle::StationDetail::ApplyTransportPassengerUnload(vehicle, passengers, plan);

    EXPECT_EQ(vehicle.next_free_seat, 3u);
    for (size_t destinationIndex = 0; destinationIndex < passengerCount; destinationIndex++)
    {
        const auto sourceIndex = expectedSourceIndices[destinationIndex];
        EXPECT_EQ(vehicle.peep[destinationIndex], passengerIds[sourceIndex]);
        EXPECT_EQ(vehicle.peep_tshirt_colours[destinationIndex], passengerColours[sourceIndex]);
        EXPECT_EQ(passengers[sourceIndex]->CurrentSeat, destinationIndex);
    }
    for (const auto throughRiderIndex : { 0u, 2u, 4u })
    {
        EXPECT_EQ(passengers[throughRiderIndex]->State, PeepState::walking);
        EXPECT_EQ(passengers[throughRiderIndex]->RideSubState, PeepRideSubState::onRide);
    }
    for (const auto alightingIndex : { 1u, 3u })
    {
        EXPECT_EQ(passengers[alightingIndex]->State, PeepState::leavingRide);
        EXPECT_EQ(passengers[alightingIndex]->RideSubState, PeepRideSubState::leaveVehicle);
    }

    for (size_t index = 0; index < passengerCount; index++)
    {
        passengers[index]->State = PeepState::walking;
        passengers[index]->RideSubState = PeepRideSubState::onRide;
        passengers[index]->CurrentSeat = static_cast<uint8_t>(index);
        vehicle.peep[index] = passengerIds[index];
        vehicle.peep_tshirt_colours[index] = passengerColours[index];
    }
    vehicle.next_free_seat = passengerCount;

    RideVehicle::StationDetail::ApplyOrdinaryPassengerUnload(vehicle, entities);

    EXPECT_EQ(vehicle.next_free_seat, 0u);
    for (size_t index = 0; index < passengerCount; index++)
    {
        EXPECT_EQ(vehicle.peep[index], passengerIds[index]);
        EXPECT_EQ(vehicle.peep_tshirt_colours[index], passengerColours[index]);
        EXPECT_EQ(passengers[index]->State, PeepState::leavingRide);
        EXPECT_EQ(passengers[index]->RideSubState, PeepRideSubState::leaveVehicle);
    }
}

TEST_F(EntityImportTests, TrainSeatSummaryUsesExactWideCapacityAndReservationCounts)
{
    auto& entities = getGameState().entities;
    entities.ResetAllEntities();

    auto* head = entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(200));
    auto* middle = entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(201));
    auto* tail = entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(202));
    ASSERT_NE(head, nullptr);
    ASSERT_NE(middle, nullptr);
    ASSERT_NE(tail, nullptr);
    head->SubType = Vehicle::Type::head;
    middle->SubType = Vehicle::Type::tail;
    tail->SubType = Vehicle::Type::tail;
    head->next_vehicle_on_train = middle->id;
    middle->next_vehicle_on_train = tail->id;

    head->num_seats = 127;
    middle->num_seats = kVehicleSeatPairFlag | 10;
    tail->num_seats = 127;
    head->num_peeps = 3;
    middle->num_peeps = 4;
    tail->num_peeps = 5;
    head->next_free_seat = 4;
    middle->next_free_seat = 6;
    tail->next_free_seat = 8;

    const Vehicle& constHead = *head;
    const auto summary = RideVehicle::StationDetail::BuildTrainSeatSummary(constHead);
    EXPECT_EQ(summary.carCount, 3u);
    EXPECT_EQ(summary.capacity, 264u);
    EXPECT_EQ(summary.currentPeeps, 12u);
    EXPECT_EQ(summary.reservedSeats, 18u);
    EXPECT_TRUE(summary.HasRiders());
    EXPECT_EQ(summary.cars[0], head);
    EXPECT_EQ(summary.cars[1], middle);
    EXPECT_EQ(summary.cars[2], tail);
}

TEST_F(EntityImportTests, SpatialIndexDirtyWorklistCoalescesMovesAndPreservesSortedBuckets)
{
    auto& entities = getGameState().entities;
    entities.ResetAllEntities();

    auto* last = entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(40));
    auto* first = entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(10));
    auto* middle = entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(30));
    ASSERT_NE(last, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(middle, nullptr);

    constexpr CoordsXYZ intermediate{ 5 * kCoordsXYStep, 6 * kCoordsXYStep, 0 };
    constexpr CoordsXYZ destination{ 10 * kCoordsXYStep, 11 * kCoordsXYStep, 0 };
    last->setLocation(destination);
    first->setLocation(destination);
    middle->setLocation(intermediate);
    middle->setLocation(destination);

    entities.UpdateEntitiesSpatialIndex();

    EXPECT_TRUE(entities.GetEntityTileList(intermediate).empty());
    const auto& destinationEntities = entities.GetEntityTileList(destination);
    ASSERT_EQ(destinationEntities.size(), 3u);
    EXPECT_EQ(destinationEntities[0].ToUnderlying(), 10u);
    EXPECT_EQ(destinationEntities[1].ToUnderlying(), 30u);
    EXPECT_EQ(destinationEntities[2].ToUnderlying(), 40u);
}

TEST_F(EntityImportTests, SpatialIndexDirtyWorklistCoversImmediateUpdatesAndEntityIdReuse)
{
    auto& entities = getGameState().entities;
    entities.ResetAllEntities();

    constexpr auto immediateId = EntityId::FromUnderlying(20);
    constexpr auto reusedId = EntityId::FromUnderlying(30);
    constexpr CoordsXYZ firstLocation{ 12 * kCoordsXYStep, 13 * kCoordsXYStep, 0 };
    constexpr CoordsXYZ secondLocation{ 14 * kCoordsXYStep, 15 * kCoordsXYStep, 0 };
    constexpr CoordsXYZ removedLocation{ 16 * kCoordsXYStep, 17 * kCoordsXYStep, 0 };
    constexpr CoordsXYZ replacementLocation{ 18 * kCoordsXYStep, 19 * kCoordsXYStep, 0 };

    auto* immediate = entities.CreateEntityAt<Guest>(immediateId);
    ASSERT_NE(immediate, nullptr);
    immediate->setLocation(firstLocation);
    entities.UpdateEntitySpatialIndex(*immediate);
    immediate->setLocation(secondLocation);

    auto* removed = entities.CreateEntityAt<Guest>(reusedId);
    ASSERT_NE(removed, nullptr);
    removed->setLocation(removedLocation);
    entities.EntityRemove(removed);

    auto* replacement = entities.CreateEntityAt<Guest>(reusedId);
    ASSERT_NE(replacement, nullptr);
    replacement->setLocation(replacementLocation);

    entities.UpdateEntitiesSpatialIndex();

    EXPECT_TRUE(entities.GetEntityTileList(firstLocation).empty());
    const auto& immediateEntities = entities.GetEntityTileList(secondLocation);
    ASSERT_EQ(immediateEntities.size(), 1u);
    EXPECT_EQ(immediateEntities.front(), immediateId);
    EXPECT_TRUE(entities.GetEntityTileList(removedLocation).empty());
    const auto& replacementEntities = entities.GetEntityTileList(replacementLocation);
    ASSERT_EQ(replacementEntities.size(), 1u);
    EXPECT_EQ(replacementEntities.front(), reusedId);
}

TEST_F(EntityImportTests, SpatialIndexResetRebuildsDirectImportCoordinatesAndClearsPendingWork)
{
    auto& entities = getGameState().entities;
    entities.ResetAllEntities();

    constexpr CoordsXYZ queuedLocation{ 18 * kCoordsXYStep, 19 * kCoordsXYStep, 0 };
    constexpr CoordsXYZ importedLocation{ 20 * kCoordsXYStep, 21 * kCoordsXYStep, 8 };

    auto* queued = entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(60));
    auto* imported = entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(70));
    ASSERT_NE(queued, nullptr);
    ASSERT_NE(imported, nullptr);
    queued->setLocation(queuedLocation);
    imported->x = importedLocation.x;
    imported->y = importedLocation.y;
    imported->z = importedLocation.z;

    entities.ResetEntitySpatialIndices();

    entities.UpdateEntitiesSpatialIndex();
    const auto& queuedEntities = entities.GetEntityTileList(queuedLocation);
    ASSERT_EQ(queuedEntities.size(), 1u);
    EXPECT_EQ(queuedEntities.front(), queued->id);
    const auto& importedEntities = entities.GetEntityTileList(importedLocation);
    ASSERT_EQ(importedEntities.size(), 1u);
    EXPECT_EQ(importedEntities.front(), imported->id);
}

// This test verifies that corrupted S6 files with duplicate EntityIndex values can be loaded without crashing.
TEST_F(EntityImportTests, S6ImportCorruptedDuplicateEntityIndicesDoesNotCrash)
{
    std::string testParkPath = TestData::GetParkPath("corrupted_duplicate_entity_indices.sv6");
    auto fs = FileStream(testParkPath, FileMode::open);

    auto& objManager = context->GetObjectManager();
    auto importer = ParkImporter::CreateS6(context->GetObjectRepository());
    auto loadResult = importer->LoadFromStream(&fs, false);
    objManager.LoadObjects(loadResult.RequiredObjects);

    MapAnimations::ClearAll();
    auto& gameState = getGameState();

    // This should not crash because the code should sanitize EntityIndex values before CreateEntityAt is called
    EXPECT_NO_THROW(importer->Import(gameState));
}
