/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <vector>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/core/FileStream.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/entity/EntityList.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Ride.h>
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
        getGameState().entities.ResetAllEntities();
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

    // Create an entity at index 100
    auto* entity1 = gameState.entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(100));
    ASSERT_NE(entity1, nullptr);
    EXPECT_EQ(entity1->id.ToUnderlying(), 100u);

    // Try to create another entity at the same index, which should return nullptr
    auto* entity2 = gameState.entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(100));
    EXPECT_EQ(entity2, nullptr);
}

TEST_F(EntityImportTests, FreeEntityIdsPreserveLowestIdAllocationAndExactClaims)
{
    auto& entities = getGameState().entities;
    EXPECT_EQ(entities.GetNumFreeEntities(), kMaxEntities);

    constexpr auto exactId = EntityId::FromUnderlying(100);
    auto* exact = entities.CreateEntityAt<Guest>(exactId);
    auto* first = entities.CreateEntity<Guest>();
    auto* second = entities.CreateEntity<Guest>();
    ASSERT_NE(exact, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->id.ToUnderlying(), 0u);
    EXPECT_EQ(second->id.ToUnderlying(), 1u);
    EXPECT_EQ(entities.GetNumFreeEntities(), kMaxEntities - 3);

    entities.EntityRemove(first);
    auto* reusedLowest = entities.CreateEntity<Guest>();
    ASSERT_NE(reusedLowest, nullptr);
    EXPECT_EQ(reusedLowest->id.ToUnderlying(), 0u);

    entities.EntityRemove(exact);
    EXPECT_NE(entities.CreateEntityAt<Guest>(exactId), nullptr);
    EXPECT_EQ(entities.GetNumFreeEntities(), kMaxEntities - 3);
}

TEST_F(EntityImportTests, TypedEntityIterationSkipsSparseWordRangesInIdOrder)
{
    auto& entities = getGameState().entities;

    constexpr std::array ids{ 0u, 4096u, static_cast<uint32_t>(kMaxEntities - 1) };
    for (const auto id : ids)
        ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(static_cast<uint16_t>(id))), nullptr);

    std::vector<uint32_t> iteratedIds;
    for (const auto* guest : EntityList<Guest>())
        iteratedIds.push_back(guest->id.ToUnderlying());
    EXPECT_EQ(iteratedIds, std::vector<uint32_t>(ids.begin(), ids.end()));
}

TEST_F(EntityImportTests, VehicleHeadEntityListTracksAddsRemovalsAndReset)
{
    auto& entities = getGameState().entities;

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

TEST_F(EntityImportTests, TypedEntityIterationPreservesOrderAndMutationVisibility)
{
    auto& entities = getGameState().entities;

    ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(50)), nullptr);
    ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(10)), nullptr);
    ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(30)), nullptr);

    std::vector<uint16_t> visited;
    for (auto* guest : EntityList<Guest>())
    {
        const auto id = guest->id.ToUnderlying();
        visited.push_back(id);
        if (id == 10)
        {
            entities.EntityRemove(guest);
            ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(20)), nullptr);
            ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(5)), nullptr);
        }
        else if (id == 30)
        {
            auto* future = entities.GetEntity<Guest>(EntityId::FromUnderlying(50));
            ASSERT_NE(future, nullptr);
            entities.EntityRemove(future);
            ASSERT_NE(entities.CreateEntityAt<Vehicle>(EntityId::FromUnderlying(50)), nullptr);
            ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(40)), nullptr);
        }
    }

    // The old list iterator preselected its next node before exposing the current entity. Preserve that ordering contract:
    // insertions before the preselected next id are not visited during the active traversal.
    EXPECT_EQ(visited, (std::vector<uint16_t>{ 10, 30 }));

    std::vector<uint16_t> remaining;
    for (const auto id : entities.GetEntityList(EntityType::guest))
    {
        remaining.push_back(id.ToUnderlying());
    }
    EXPECT_EQ(remaining, (std::vector<uint16_t>{ 5, 20, 30, 40 }));
    const auto& vehicles = entities.GetEntityList(EntityType::vehicle);
    ASSERT_EQ(vehicles.size(), 1u);
    EXPECT_EQ((*vehicles.begin()).ToUnderlying(), 50u);
}

TEST_F(EntityImportTests, TypedEntityIteratorEqualityIgnoresMutationLookahead)
{
    auto& entities = getGameState().entities;

    ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(10)), nullptr);
    ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(30)), nullptr);

    const auto& guestIds = entities.GetEntityList(EntityType::guest);
    auto beforeInsertion = guestIds.begin();
    ASSERT_NE(entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(20)), nullptr);
    auto afterInsertion = guestIds.begin();

    EXPECT_EQ(beforeInsertion, afterInsertion);
    EXPECT_EQ((*beforeInsertion).ToUnderlying(), 10u);
    EXPECT_EQ((*afterInsertion).ToUnderlying(), 10u);

    ++beforeInsertion;
    ++afterInsertion;
    EXPECT_EQ((*beforeInsertion).ToUnderlying(), 30u);
    EXPECT_EQ((*afterInsertion).ToUnderlying(), 20u);
}

TEST_F(EntityImportTests, TypedEntityMembershipClearsAndRebuildsAtBoundaryIds)
{
    auto& entities = getGameState().entities;

    constexpr auto firstId = EntityId::FromUnderlying(0);
    constexpr auto middleId = EntityId::FromUnderlying(123);
    constexpr auto lastId = EntityId::FromUnderlying(kMaxEntities - 1);
    ASSERT_NE(entities.CreateEntityAt<Guest>(firstId), nullptr);
    ASSERT_NE(entities.CreateEntityAt<Vehicle>(middleId), nullptr);
    ASSERT_NE(entities.CreateEntityAt<Guest>(lastId), nullptr);
    EXPECT_EQ(entities.GetEntityList(EntityType::guest).size(), 2u);
    EXPECT_EQ(entities.GetEntityList(EntityType::vehicle).size(), 1u);

    entities.ResetAllEntities();
    EXPECT_TRUE(entities.GetEntityList(EntityType::guest).empty());
    EXPECT_TRUE(entities.GetEntityList(EntityType::vehicle).empty());

    auto* lastVehicle = entities.CreateEntityAt<Vehicle>(lastId);
    auto* middleGuest = entities.CreateEntityAt<Guest>(middleId);
    ASSERT_NE(lastVehicle, nullptr);
    ASSERT_NE(middleGuest, nullptr);

    const auto& guestIds = entities.GetEntityList(EntityType::guest);
    const auto& vehicleIds = entities.GetEntityList(EntityType::vehicle);
    ASSERT_EQ(guestIds.size(), 1u);
    ASSERT_EQ(vehicleIds.size(), 1u);
    EXPECT_EQ(*guestIds.begin(), middleId);
    EXPECT_EQ(*vehicleIds.begin(), lastId);
    const auto guests = EntityList<Guest>();
    const auto vehicles = EntityList<Vehicle>();
    EXPECT_EQ((*guests.begin())->type, EntityType::guest);
    EXPECT_EQ((*vehicles.begin())->type, EntityType::vehicle);
}

TEST_F(EntityImportTests, PassengerUnloadPlanPreservesThroughRidersAndOrdinaryUnloadRemovesEveryone)
{
    constexpr uint8_t passengerCount = 5;
    constexpr std::array<bool, passengerCount> shouldAlight{ false, true, false, true, false };
    constexpr std::array<uint8_t, passengerCount> expectedSourceIndices{ 0, 2, 4, 1, 3 };

    auto& entities = getGameState().entities;

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

TEST_F(EntityImportTests, TransportPassengerStaysAboardIntermediateStationAndAlightsAtDestination)
{
    constexpr auto origin = StationIndex::FromUnderlying(0);
    constexpr auto intermediate = StationIndex::FromUnderlying(1);
    constexpr auto destination = StationIndex::FromUnderlying(2);

    Ride ride{};
    ride.id = RideId::FromUnderlying(123);
    ride.type = RIDE_TYPE_MONORAIL;
    ride.numStations = 3;

    Guest throughRider{};
    Guest intermediateRider{};
    Guest ordinaryRider{};
    throughRider.id = EntityId::FromUnderlying(10);
    intermediateRider.id = EntityId::FromUnderlying(11);
    ordinaryRider.id = EntityId::FromUnderlying(12);
    throughRider.setTransportRoute(ride.id, origin, destination);
    intermediateRider.setTransportRoute(ride.id, origin, intermediate);

    Vehicle vehicle{};
    vehicle.num_peeps = 3;
    vehicle.next_free_seat = 3;
    vehicle.peep[0] = throughRider.id;
    vehicle.peep[1] = intermediateRider.id;
    vehicle.peep[2] = ordinaryRider.id;

    std::array<Guest*, 3> passengers{ &throughRider, &intermediateRider, &ordinaryRider };
    const auto intermediatePlan = RideVehicle::StationDetail::BuildTransportPassengerUnloadPlan(
        ride, intermediate, passengers);
    EXPECT_EQ(intermediatePlan.continuingCount, 1);
    EXPECT_EQ(intermediatePlan.sourceIndices[0], 0);
    RideVehicle::StationDetail::ApplyTransportPassengerUnload(vehicle, passengers, intermediatePlan);

    EXPECT_EQ(vehicle.next_free_seat, 1);
    EXPECT_EQ(vehicle.peep[0], throughRider.id);
    EXPECT_NE(throughRider.State, PeepState::leavingRide);
    EXPECT_EQ(intermediateRider.State, PeepState::leavingRide);
    EXPECT_EQ(ordinaryRider.State, PeepState::leavingRide);

    vehicle.num_peeps = 1;
    vehicle.next_free_seat = 1;
    std::array<Guest*, 1> remainingPassenger{ &throughRider };
    const auto destinationPlan = RideVehicle::StationDetail::BuildTransportPassengerUnloadPlan(
        ride, destination, remainingPassenger);
    EXPECT_EQ(destinationPlan.continuingCount, 0);
    RideVehicle::StationDetail::ApplyTransportPassengerUnload(vehicle, remainingPassenger, destinationPlan);
    EXPECT_EQ(vehicle.next_free_seat, 0);
    EXPECT_EQ(throughRider.State, PeepState::leavingRide);
}

TEST_F(EntityImportTests, PlatformSeatBindingPreservesThroughRidersAndBindsFifoToExactSeats)
{
    Vehicle vehicle{};
    std::fill(std::begin(vehicle.peep), std::end(vehicle.peep), EntityId::GetNull());
    vehicle.num_seats = 3;
    vehicle.next_free_seat = 1;
    const auto throughRider = EntityId::FromUnderlying(100);
    vehicle.peep[0] = throughRider;

    Guest first{};
    first.id = EntityId::FromUnderlying(101);
    first.TshirtColour = Drawing::Colour::brightRed;
    Guest second{};
    second.id = EntityId::FromUnderlying(102);
    second.TshirtColour = Drawing::Colour::brightGreen;

    using RideVehicle::StationDetail::BindPlatformGuestToSeat;
    EXPECT_TRUE(BindPlatformGuestToSeat(first, vehicle, 1));
    EXPECT_TRUE(BindPlatformGuestToSeat(second, vehicle, 2));

    EXPECT_EQ(vehicle.next_free_seat, 3u);
    EXPECT_EQ(vehicle.peep[0], throughRider);
    EXPECT_EQ(vehicle.peep[1], first.id);
    EXPECT_EQ(vehicle.peep[2], second.id);
    EXPECT_EQ(first.CurrentSeat, 1u);
    EXPECT_EQ(second.CurrentSeat, 2u);
    EXPECT_EQ(vehicle.peep_tshirt_colours[1], first.TshirtColour);
    EXPECT_EQ(vehicle.peep_tshirt_colours[2], second.TshirtColour);
}

TEST_F(EntityImportTests, PlatformSeatBindingUsesReservedCountAndRejectsActiveDuplicate)
{
    Vehicle vehicle{};
    std::fill(std::begin(vehicle.peep), std::end(vehicle.peep), EntityId::GetNull());
    vehicle.num_seats = 2;
    vehicle.next_free_seat = 1;
    vehicle.peep[0] = EntityId::FromUnderlying(100);

    Guest guest{};
    guest.id = EntityId::FromUnderlying(101);
    guest.CurrentSeat = 7;

    using RideVehicle::StationDetail::BindPlatformGuestToSeat;
    EXPECT_FALSE(BindPlatformGuestToSeat(guest, vehicle, 0));

    // Guests leave from the end of the active passenger range. The fixed seat array deliberately retains their ids after
    // next_free_seat is reduced, so an inactive value must not make an otherwise empty seat unavailable.
    vehicle.peep[1] = guest.id;
    EXPECT_TRUE(BindPlatformGuestToSeat(guest, vehicle, 1));
    EXPECT_EQ(vehicle.next_free_seat, 2u);
    EXPECT_EQ(guest.CurrentSeat, 1u);

    vehicle.next_free_seat = 1;
    vehicle.peep[0] = guest.id;
    vehicle.peep[1] = EntityId::GetNull();
    EXPECT_FALSE(BindPlatformGuestToSeat(guest, vehicle, 1));
    EXPECT_EQ(vehicle.next_free_seat, 1u);
    EXPECT_EQ(std::count(std::begin(vehicle.peep), std::end(vehicle.peep), guest.id), 1);
}

TEST_F(EntityImportTests, TrainSeatSummaryUsesExactWideCapacityAndReservationCounts)
{
    auto& entities = getGameState().entities;

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
    EXPECT_NE(summary.currentPeeps, 0u);
    EXPECT_EQ(summary.cars[0], head);
    EXPECT_EQ(summary.cars[1], middle);
    EXPECT_EQ(summary.cars[2], tail);

    head->num_peeps = 0;
    middle->num_peeps = 0;
    tail->num_peeps = 0;
    EXPECT_EQ(RideVehicle::StationDetail::BuildTrainSeatSummary(constHead).currentPeeps, 0u);
}

TEST_F(EntityImportTests, SpatialIndexDirtyWorklistCoalescesMovesAndPreservesSortedBuckets)
{
    auto& entities = getGameState().entities;

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

TEST_F(EntityImportTests, TweenMovementPreservesAuthoritativeSpatialIndex)
{
    auto& entities = getGameState().entities;

    constexpr CoordsXYZ authoritativeLocation{ 10 * kCoordsXYStep, 11 * kCoordsXYStep, 0 };
    constexpr CoordsXYZ tweenLocation{ 5 * kCoordsXYStep, 6 * kCoordsXYStep, 0 };

    auto* guest = entities.CreateEntityAt<Guest>(EntityId::FromUnderlying(10));
    ASSERT_NE(guest, nullptr);
    guest->setLocation(authoritativeLocation);
    entities.UpdateEntitySpatialIndex(*guest);

    guest->moveToForTween(tweenLocation);
    EXPECT_EQ(guest->getLocation(), tweenLocation);
    EXPECT_EQ(guest->spatialIndex & kSpatialIndexDirtyMask, 0u);

    entities.UpdateEntitiesSpatialIndex();
    EXPECT_TRUE(entities.GetEntityTileList(tweenLocation).empty());
    const auto& authoritativeEntities = entities.GetEntityTileList(authoritativeLocation);
    ASSERT_EQ(authoritativeEntities.size(), 1u);
    EXPECT_EQ(authoritativeEntities.front(), guest->id);

    guest->moveToForTween(authoritativeLocation);
    EXPECT_EQ(guest->getLocation(), authoritativeLocation);
    EXPECT_EQ(guest->spatialIndex & kSpatialIndexDirtyMask, 0u);
}

TEST_F(EntityImportTests, SpatialIndexDirtyWorklistCoversImmediateUpdatesAndEntityIdReuse)
{
    auto& entities = getGameState().entities;

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
