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
