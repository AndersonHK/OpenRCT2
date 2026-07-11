/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/Limits.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/actions/GameActionRunner.h>
#include <openrct2/actions/park/ParkSetEntranceFeeAction.h>
#include <openrct2/actions/park/ParkSetParameterAction.h>
#include <openrct2/actions/ride/RideCreateAction.h>
#include <openrct2/actions/ride/RideSetPriceAction.h>
#include <openrct2/actions/ride/RideSetStatusAction.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Peep.h>
#include <openrct2/object/ObjectLimits.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/TrackDesign.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/ride/Vehicle.Station.h>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/Park.h>
#include <sstream>
#include <string>
#include <utility>

using namespace OpenRCT2;

class PlayTests : public testing::Test
{
};

TEST_F(PlayTests, CalculatesIntegratedBenchmarkMetricsFromIndependentCounters)
{
    const IntegratedBenchmarkTotals totals{
        .elapsedSeconds = 2.0,
        .logicalTicks = 640,
        .draws = 30,
        .simulationSeconds = 1.6,
        .drawSeconds = 0.2,
    };

    const auto metrics = CalculateIntegratedBenchmarkMetrics(totals);
    EXPECT_DOUBLE_EQ(metrics.logicalTicksPerSecond, 320.0);
    EXPECT_DOUBLE_EQ(metrics.framesPerSecond, 15.0);
    EXPECT_DOUBLE_EQ(metrics.simulationUtilisationPercent, 80.0);
    EXPECT_DOUBLE_EQ(metrics.drawUtilisationPercent, 10.0);
    EXPECT_DOUBLE_EQ(metrics.meanSimulationMicrosecondsPerLogicalTick, 2500.0);
    EXPECT_NEAR(metrics.meanDrawMicroseconds, 6666.6666667, 0.0001);
}

TEST_F(PlayTests, IntegratedBenchmarkMetricsHandleEmptyMeasurement)
{
    const auto metrics = CalculateIntegratedBenchmarkMetrics({});
    EXPECT_DOUBLE_EQ(metrics.logicalTicksPerSecond, 0.0);
    EXPECT_DOUBLE_EQ(metrics.framesPerSecond, 0.0);
    EXPECT_DOUBLE_EQ(metrics.meanSimulationMicrosecondsPerLogicalTick, 0.0);
    EXPECT_DOUBLE_EQ(metrics.meanDrawMicroseconds, 0.0);
}

static std::unique_ptr<IContext> localStartGame(const std::string& parkPath)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    if (!context->Initialise())
        return {};

    auto importer = ParkImporter::CreateS6(context->GetObjectRepository());
    auto loadResult = importer->LoadSavedGame(parkPath.c_str(), false);
    context->GetObjectManager().LoadObjects(loadResult.RequiredObjects);

    MapAnimations::ClearAll();
    // TODO: Have a separate GameState and exchange once loaded.
    auto& gameState = getGameState();
    importer->Import(gameState);

    gameState.entities.ResetEntitySpatialIndices();

    ResetAllSpriteQuadrantPlacements();
    LoadPalette();
    EntityTweener::Get().Reset();
    MapAnimations::MarkAllTiles();
    FixInvalidVehicleSpriteSizes();

    gGameSpeed = 1;

    return context;
}

template<class Fn>
static bool updateUntil(int maxSteps, Fn&& fn)
{
    while (maxSteps-- && !fn())
    {
        gameStateUpdateLogic();
    }
    return maxSteps > 0;
}

template<class GA, class... Args>
static void execute(Args&&... args)
{
    GA ga(std::forward<Args>(args)...);
    GameActions::Execute(&ga, getGameState());
}

template<class GA, class... Args>
static GameActions::Result executeImmediate(Args&&... args)
{
    GA ga(std::forward<Args>(args)...);
    return GameActions::ExecuteNested(&ga, getGameState());
}

static void InsertGuestAtBackOfQueue(Guest& guest, Ride& ride, StationIndex stationIndex, const Peep& queueAnchor)
{
    auto& station = ride.getStation(stationIndex);
    ASSERT_FALSE(station.LastPeepInQueue.IsNull());

    guest.moveTo(queueAnchor.getLocation());
    guest.NextLoc = queueAnchor.NextLoc;
    guest.PeepDirection = queueAnchor.PeepDirection;
    guest.InteractionRideIndex = ride.id;
    guest.guestNextInQueue = station.LastPeepInQueue;
    station.LastPeepInQueue = guest.id;
    station.QueueLength++;

    guest.CurrentRide = ride.id;
    guest.CurrentRideStation = stationIndex;
    guest.State = PeepState::queuing;
    guest.daysInQueue = 0;
    guest.RideSubState = PeepRideSubState::inQueue;
    guest.DestinationTolerance = 2;
    guest.timeInQueue = 0;
}

static Ride* FindFerrisWheel(GameState_t& gameState)
{
    auto rideManager = RideManager(gameState);
    auto it = std::find_if(
        rideManager.begin(), rideManager.end(), [](const auto& ride) { return ride.type == RIDE_TYPE_FERRIS_WHEEL; });
    return it == rideManager.end() ? nullptr : &*it;
}

static bool GuestHasRideThought(const Guest& guest, PeepThoughtType thoughtType, RideId rideId)
{
    return std::any_of(guest.thoughts.begin(), guest.thoughts.end(), [thoughtType, rideId](const auto& thought) {
        return thought.type == thoughtType && thought.rideId == rideId;
    });
}

TEST_F(PlayTests, SecondGuestInQueueShouldNotRideIfNoFunds)
{
    /* This test verifies that a guest, when second in queue, won't be forced to enter
     * the ride if it has not enough money to pay for it.
     * To simulate this scenario, two guests (a rich and a poor) are encouraged to enter
     * the ride queue, and then the price is raised such that the second guest in line
     * (the poor one) cannot pay. The poor guest should not enter the ride.
     */
    std::string initStateFile = TestData::GetParkPath("small_park_with_ferris_wheel.sv6");

    auto context = localStartGame(initStateFile);
    ASSERT_NE(context.get(), nullptr);

    auto& gameState = getGameState();

    // Open park for free but charging for rides
    execute<GameActions::ParkSetParameterAction>(GameActions::ParkParameter::open);
    execute<GameActions::ParkSetEntranceFeeAction>(0);
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    // Find ferris wheel
    auto rideManager = RideManager(gameState);
    auto it = std::find_if(
        rideManager.begin(), rideManager.end(), [](auto& ride) { return ride.type == RIDE_TYPE_FERRIS_WHEEL; });
    ASSERT_NE(it, rideManager.end());
    Ride& ferrisWheel = *it;

    // Open it for free
    execute<GameActions::RideSetStatusAction>(ferrisWheel.id, RideStatus::open);
    execute<GameActions::RideSetPriceAction>(ferrisWheel.id, 0, true);

    // Ignore intensity to stimulate peeps to queue into ferris wheel
    gameState.cheats.ignoreRideIntensity = true;

    // Insert a rich guest
    auto richGuest = Park::GenerateGuest();
    richGuest->cashInPocket = 300.00_GBP;

    // Wait for rich guest to get in queue
    bool matched = updateUntil(1000, [&]() { return richGuest->State == PeepState::queuing; });
    ASSERT_TRUE(matched);

    // Insert poor guest
    auto poorGuest = Park::GenerateGuest();
    poorGuest->cashInPocket = 0.49_GBP;
    InsertGuestAtBackOfQueue(*poorGuest, ferrisWheel, richGuest->CurrentRideStation, *richGuest);

    // Raise the price of the ride to a value poor guest can't pay.
    poorGuest->cashInPocket = 0.49_GBP;
    auto raisePriceResult = executeImmediate<GameActions::RideSetPriceAction>(ferrisWheel.id, 1.00_GBP, true);
    ASSERT_EQ(raisePriceResult.error, GameActions::Status::ok);
    ASSERT_GT(RideGetPrice(ferrisWheel), poorGuest->cashInPocket);

    const auto cashBeforeDecision = poorGuest->cashInPocket;
    EXPECT_FALSE(poorGuest->shouldGoOnRide(ferrisWheel, poorGuest->CurrentRideStation, true, false));
    EXPECT_NE(poorGuest->State, PeepState::onRide);
    EXPECT_EQ(poorGuest->cashInPocket, cashBeforeDecision);
}

TEST_F(PlayTests, CarRideWithOneCarOnlyAcceptsTwoGuests)
{
    // This test verifies that a car ride with one car will accept at most two guests
    std::string initStateFile = TestData::GetParkPath("small_park_car_ride_one_car.sv6");

    auto context = localStartGame(initStateFile);
    ASSERT_NE(context.get(), nullptr);

    auto& gameState = getGameState();

    // Open park for free but charging for rides
    execute<GameActions::ParkSetParameterAction>(GameActions::ParkParameter::open);
    execute<GameActions::ParkSetEntranceFeeAction>(0);
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    // Find car ride
    auto rideManager = RideManager(gameState);
    auto it = std::find_if(rideManager.begin(), rideManager.end(), [](auto& ride) { return ride.type == RIDE_TYPE_CAR_RIDE; });
    ASSERT_NE(it, rideManager.end());
    Ride& carRide = *it;

    // Open it for free
    execute<GameActions::RideSetStatusAction>(carRide.id, RideStatus::open);
    execute<GameActions::RideSetPriceAction>(carRide.id, 0, true);

    // Ignore intensity to stimulate peeps to queue into the ride
    gameState.cheats.ignoreRideIntensity = true;

    // Create some guests
    std::vector<Peep*> guests;
    for (int i = 0; i < 25; i++)
    {
        guests.push_back(Park::GenerateGuest());
    }

    // Wait until one of them is riding
    auto guestIsOnRide = [](auto* g) { return g->State == PeepState::onRide; };
    bool matched = updateUntil(10000, [&]() { return std::any_of(guests.begin(), guests.end(), guestIsOnRide); });
    ASSERT_TRUE(matched);

    // For the next few ticks at most two guests can be on the ride
    for (int i = 0; i < 100; i++)
    {
        int numRiding = std::count_if(guests.begin(), guests.end(), guestIsOnRide);
        ASSERT_LE(numRiding, 2);
        gameStateUpdateLogic();
    }
}

TEST_F(PlayTests, CoasterPlatformPreQueueRequiresOppositeLateralStationSides)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));

    auto& gameState = getGameState();
    RideClearAllStationPlatformPreQueues();

    Ride* coaster = nullptr;
    Vehicle* coasterTrain = nullptr;
    TrackElement* coasterOrigin = nullptr;
    StationIndex coasterStation = StationIndex::GetNull();
    for (auto& ride : RideManager(gameState))
    {
        if (!RideSupportsStationPlatformPreQueue(ride)
            || ride.getRideTypeDescriptor().Category != RideCategory::rollerCoaster)
        {
            continue;
        }
        for (uint8_t stationIndex = 0; stationIndex < ride.numStations && coaster == nullptr; stationIndex++)
        {
            const auto candidateStation = StationIndex::FromUnderlying(stationIndex);
            auto& station = ride.getStation(candidateStation);
            auto* origin = ride.getOriginElement(candidateStation);
            if (station.Entrance.IsNull() || station.Exit.IsNull() || origin == nullptr)
            {
                continue;
            }
            for (uint8_t trainIndex = 0; trainIndex < ride.numTrains; trainIndex++)
            {
                auto* train = gameState.entities.GetEntity<Vehicle>(ride.vehicles[trainIndex]);
                if (train == nullptr)
                {
                    continue;
                }
                const auto stationDirection = origin->getDirection();
                station.Entrance.direction = (stationDirection + 1) & kTileElementDirectionMask;
                station.Exit.direction = (stationDirection + 3) & kTileElementDirectionMask;
                if (RideCaptureStationPlatformTemplate(ride, candidateStation, *train))
                {
                    coaster = &ride;
                    coasterTrain = train;
                    coasterOrigin = origin;
                    coasterStation = candidateStation;
                    break;
                }
                RideClearAllStationPlatformPreQueues();
            }
        }
    }

    ASSERT_NE(coaster, nullptr);
    ASSERT_NE(coasterTrain, nullptr);
    ASSERT_NE(coasterOrigin, nullptr);
    ASSERT_FALSE(coasterStation.IsNull());
    auto& coasterStationData = coaster->getStation(coasterStation);
    const auto coasterDirection = coasterOrigin->getDirection();

    RideClearAllStationPlatformPreQueues();
    coasterStationData.Entrance.direction = (coasterDirection + 1) & kTileElementDirectionMask;
    coasterStationData.Exit.direction = coasterStationData.Entrance.direction;
    EXPECT_FALSE(RideCaptureStationPlatformTemplate(*coaster, coasterStation, *coasterTrain));
    RideActivateStationPlatformPreQueue(*coaster, coasterStation);
    EXPECT_FALSE(RideStationPlatformPreQueueIsActive(*coaster, coasterStation));
    EXPECT_FALSE(
        RideReserveStationPlatformSlot(*coaster, coasterStation, EntityId::FromUnderlying(65000)).has_value());

    auto* stagedGuest = Guest::generate(coasterStationData.Entrance.ToCoordsXYZ());
    ASSERT_NE(stagedGuest, nullptr);
    stagedGuest->CurrentRide = coaster->id;
    stagedGuest->CurrentRideStation = coasterStation;
    stagedGuest->CurrentTrain = RideStation::kNoTrain;
    stagedGuest->State = PeepState::enteringRide;
    stagedGuest->RideSubState = PeepRideSubState::waitingOnPlatform;
    RideRebuildStationPlatformPreQueues();
    EXPECT_EQ(stagedGuest->State, PeepState::leavingRide);
    EXPECT_EQ(stagedGuest->RideSubState, PeepRideSubState::approachExit);

    coasterStationData.Entrance.direction = coasterDirection;
    coasterStationData.Exit.direction = DirectionReverse(coasterDirection);
    EXPECT_FALSE(RideCaptureStationPlatformTemplate(*coaster, coasterStation, *coasterTrain));

    coasterStationData.Entrance.direction = (coasterDirection + 1) & kTileElementDirectionMask;
    coasterStationData.Exit.direction = (coasterDirection + 3) & kTileElementDirectionMask;
    EXPECT_TRUE(RideCaptureStationPlatformTemplate(*coaster, coasterStation, *coasterTrain));

    bool sameSideTransportCaptured = false;
    for (auto& ride : RideManager(gameState))
    {
        if (!RideSupportsStationPlatformPreQueue(ride)
            || !ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
        {
            continue;
        }
        for (uint8_t stationIndex = 0; stationIndex < ride.numStations && !sameSideTransportCaptured; stationIndex++)
        {
            const auto candidateStation = StationIndex::FromUnderlying(stationIndex);
            auto& station = ride.getStation(candidateStation);
            if (station.Entrance.IsNull() || station.Exit.IsNull())
            {
                continue;
            }
            station.Exit.direction = station.Entrance.direction;
            for (uint8_t trainIndex = 0; trainIndex < ride.numTrains; trainIndex++)
            {
                auto* train = gameState.entities.GetEntity<Vehicle>(ride.vehicles[trainIndex]);
                if (train != nullptr && RideCaptureStationPlatformTemplate(ride, candidateStation, *train))
                {
                    sameSideTransportCaptured = true;
                    break;
                }
            }
            if (!sameSideTransportCaptured)
            {
                RideClearAllStationPlatformPreQueues();
            }
        }
    }
    EXPECT_TRUE(sameSideTransportCaptured);
}

TEST_F(PlayTests, StoppedTrainBoardsGuestWaitingOnStationPlatformBeforeDeparting)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));

    auto& gameState = getGameState();
    RideClearAllStationPlatformPreQueues();

    Ride* targetRide = nullptr;
    Vehicle* targetTrain = nullptr;
    StationIndex targetStation = StationIndex::GetNull();
    for (auto& ride : RideManager(gameState))
    {
        // Keep this simulation focused on the guest/train handshake. A second train at the same station can
        // legitimately replace RideStation::TrainAtStation later in the vehicle-update pass.
        if (!RideSupportsStationPlatformPreQueue(ride) || ride.numTrains != 1)
            continue;

        for (uint8_t trainIndex = 0; trainIndex < ride.numTrains && targetRide == nullptr; trainIndex++)
        {
            auto* train = gameState.entities.GetEntity<Vehicle>(ride.vehicles[trainIndex]);
            if (train == nullptr)
                continue;

            bool trainIsEmpty = true;
            for (auto* car = train; car != nullptr; car = gameState.entities.GetEntity<Vehicle>(car->next_vehicle_on_train))
            {
                trainIsEmpty = trainIsEmpty && car->num_peeps == 0 && car->next_free_seat == 0;
            }
            if (!trainIsEmpty)
                continue;

            for (uint8_t stationIndex = 0; stationIndex < ride.numStations; stationIndex++)
            {
                const auto candidate = StationIndex::FromUnderlying(stationIndex);
                const auto& station = ride.getStation(candidate);
                if (station.Entrance.IsNull() || station.Exit.IsNull())
                    continue;
                if (!RideCaptureStationPlatformTemplate(ride, candidate, *train))
                    continue;

                targetRide = &ride;
                targetTrain = train;
                targetStation = candidate;
                break;
            }
        }
        if (targetRide != nullptr)
            break;
    }

    ASSERT_NE(targetRide, nullptr);
    ASSERT_NE(targetTrain, nullptr);
    ASSERT_FALSE(targetStation.IsNull());

    targetRide->status = RideStatus::open;
    targetRide->flags.unset(RideFlag::brokenDown, RideFlag::breakdownPending);
    targetRide->vehicleChangeTimeout = 0;
    targetRide->departFlags = 0;
    gameState.park.flags |= PARK_FLAGS_NO_MONEY;

    for (auto* car = targetTrain; car != nullptr;
         car = gameState.entities.GetEntity<Vehicle>(car->next_vehicle_on_train))
    {
        car->num_peeps = 0;
        car->next_free_seat = 0;
        car->restraints_position = 255;
        std::fill(std::begin(car->peep), std::end(car->peep), EntityId::GetNull());
    }
    targetTrain->current_station = targetStation;
    targetTrain->status = Vehicle::Status::waitingForPassengers;
    targetTrain->sub_state = 0;
    targetTrain->time_waiting = 0;
    targetTrain->flags.unset(VehicleFlag::readyToDepart, VehicleFlag::waitingOnAdjacentStation);
    targetRide->getStation(targetStation).TrainAtStation = RideStation::kNoTrain;

    auto* guest = Guest::generate(targetRide->getStation(targetStation).Entrance.ToCoordsXYZ());
    ASSERT_NE(guest, nullptr);
    RideActivateStationPlatformPreQueue(*targetRide, targetStation);

    // Put the real guest in the final platform slot, then remove the synthetic occupants ahead of them. An arriving
    // empty train compacts that guest to its first free seat, reproducing the in-game remap that previously restarted
    // the platform walk and allowed the train to depart empty.
    std::vector<EntityId> placeholders;
    for (uint16_t rawId = 60000;; rawId++)
    {
        const auto placeholder = EntityId::FromUnderlying(rawId);
        if (!RideReserveStationPlatformSlot(*targetRide, targetStation, placeholder).has_value())
            break;
        placeholders.push_back(placeholder);
    }
    ASSERT_GT(placeholders.size(), 1u);
    RideReleaseStationPlatformSlot(*targetRide, targetStation, placeholders.back());
    placeholders.pop_back();
    const auto reservation = RideReserveStationPlatformSlot(*targetRide, targetStation, guest->id);
    ASSERT_TRUE(reservation.has_value());
    for (const auto placeholder : placeholders)
    {
        RideReleaseStationPlatformSlot(*targetRide, targetStation, placeholder);
    }

    guest->moveTo(reservation->waitPosition);
    guest->SetDestination(reservation->waitPosition, 2);
    guest->CurrentRide = targetRide->id;
    guest->CurrentRideStation = targetStation;
    guest->CurrentTrain = RideStation::kNoTrain;
    guest->CurrentCar = reservation->carIndex;
    guest->CurrentSeat = reservation->seatIndex;
    guest->State = PeepState::enteringRide;
    guest->RideSubState = PeepRideSubState::waitingOnPlatform;
    guest->DestinationTolerance = 0;
    guest->StepProgress = std::numeric_limits<uint8_t>::max();

    // Drive the production arrival preparation once. It remaps the staged guest, after which the normal whole-game
    // update loop publishes the stopped train and must reserve a seat before the empty dwell expires.
    ASSERT_TRUE(RidePrepareStationPlatformBoarding(*targetRide, targetStation, *targetTrain));
    const auto remappedReservation = RideGetStationPlatformReservation(*targetRide, targetStation, guest->id);
    ASSERT_TRUE(remappedReservation.has_value());
    ASSERT_NE(remappedReservation->slotIndex, reservation->slotIndex);
    ASSERT_EQ(guest->RideSubState, PeepRideSubState::approachPlatformSlot);

    bool departedEmpty = false;
    bool boarded = false;
    std::ostringstream trace;
    for (int32_t tick = 0; tick < 512; tick++)
    {
        gameStateUpdateLogic();
        if (tick < 64)
        {
            trace << "tick=" << tick << " guestState=" << static_cast<int32_t>(guest->State)
                  << " guestSubState=" << static_cast<int32_t>(guest->RideSubState)
                  << " guestTrain=" << static_cast<int32_t>(guest->CurrentTrain)
                  << " guestCar=" << static_cast<int32_t>(guest->CurrentCar)
                  << " guestSeat=" << static_cast<int32_t>(guest->CurrentSeat)
                  << " reservation="
                  << RideGetStationPlatformReservation(*targetRide, targetStation, guest->id).has_value()
                  << " first=" << RideStationPlatformGuestIsFirst(*targetRide, targetStation, guest->id)
                  << " trainAtStation=" << static_cast<int32_t>(targetRide->getStation(targetStation).TrainAtStation)
                  << " vehicleStatus=" << static_cast<int32_t>(targetTrain->status)
                  << " vehicleSubState=" << static_cast<int32_t>(targetTrain->sub_state)
                  << " reservedSeats=" << static_cast<int32_t>(targetTrain->next_free_seat)
                  << " riders=" << static_cast<int32_t>(targetTrain->num_peeps) << '\n';
        }
        boarded = targetTrain->num_peeps != 0 && guest->State == PeepState::onRide;
        if (boarded)
            break;
        if (targetTrain->num_peeps == 0
            && targetTrain->status != Vehicle::Status::waitingForPassengers)
        {
            departedEmpty = true;
            break;
        }
    }

    EXPECT_FALSE(departedEmpty) << trace.str();
    EXPECT_TRUE(boarded) << trace.str();
    EXPECT_EQ(targetTrain->peep[guest->CurrentSeat], guest->id) << trace.str();
}

TEST_F(PlayTests, ArrivingTrainCannotStealStationPlatformBeforePublishedHandoff)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));

    auto& gameState = getGameState();
    RideClearAllStationPlatformPreQueues();

    Ride* targetRide = nullptr;
    Vehicle* firstTrain = nullptr;
    Vehicle* arrivingTrain = nullptr;
    StationIndex targetStation = StationIndex::GetNull();
    for (auto& ride : RideManager(gameState))
    {
        if (!RideSupportsStationPlatformPreQueue(ride) || ride.numTrains < 2
            || !ride.getRideTypeDescriptor().flags.has(RtdFlag::hasLoadOptions))
            continue;

        auto* candidateFirstTrain = gameState.entities.GetEntity<Vehicle>(ride.vehicles[0]);
        auto* candidateArrivingTrain = gameState.entities.GetEntity<Vehicle>(ride.vehicles[1]);
        if (candidateFirstTrain == nullptr || candidateArrivingTrain == nullptr || !candidateFirstTrain->IsUsedInPairs()
            || RideVehicle::StationDetail::BuildTrainSeatSummary(*candidateFirstTrain).capacity < 4)
        {
            continue;
        }

        for (uint8_t stationIndex = 0; stationIndex < ride.numStations; stationIndex++)
        {
            const auto candidateStation = StationIndex::FromUnderlying(stationIndex);
            auto& station = ride.getStation(candidateStation);
            if (station.Entrance.IsNull() || station.Exit.IsNull())
                continue;

            if (ride.getRideTypeDescriptor().Category == RideCategory::rollerCoaster)
            {
                const auto* origin = ride.getOriginElement(candidateStation);
                if (origin == nullptr)
                    continue;
                const auto direction = origin->getDirection();
                station.Entrance.direction = (direction + 1) & kTileElementDirectionMask;
                station.Exit.direction = (direction + 3) & kTileElementDirectionMask;
            }
            if (!RideCaptureStationPlatformTemplate(ride, candidateStation, *candidateFirstTrain))
                continue;

            targetRide = &ride;
            firstTrain = candidateFirstTrain;
            arrivingTrain = candidateArrivingTrain;
            targetStation = candidateStation;
            break;
        }
        if (targetRide != nullptr)
            break;
    }

    ASSERT_NE(targetRide, nullptr);
    ASSERT_NE(firstTrain, nullptr);
    ASSERT_NE(arrivingTrain, nullptr);
    ASSERT_FALSE(targetStation.IsNull());

    const auto clearTrain = [&gameState](Vehicle& head) {
        for (auto* car = &head; car != nullptr; car = gameState.entities.GetEntity<Vehicle>(car->next_vehicle_on_train))
        {
            car->num_peeps = 0;
            car->next_free_seat = 0;
            car->restraints_position = 255;
            std::fill(std::begin(car->peep), std::end(car->peep), EntityId::GetNull());
        }
    };
    clearTrain(*firstTrain);
    clearTrain(*arrivingTrain);
    firstTrain->flags.unset(VehicleFlag::readyToDepart);
    arrivingTrain->flags.unset(VehicleFlag::readyToDepart);

    targetRide->status = RideStatus::open;
    targetRide->flags.unset(RideFlag::brokenDown, RideFlag::breakdownPending);
    targetRide->vehicleChangeTimeout = 0;

    firstTrain->num_peeps = 1;
    firstTrain->next_free_seat = 1;
    firstTrain->peep[0] = EntityId::FromUnderlying(65000);
    firstTrain->current_station = targetStation;
    arrivingTrain->current_station = targetStation;

    auto& station = targetRide->getStation(targetStation);
    station.TrainAtStation = 0;
    RideActivateStationPlatformPreQueue(*targetRide, targetStation);
    ASSERT_TRUE(RidePrepareStationPlatformBoarding(*targetRide, targetStation, *firstTrain));

    auto* firstGuest = Guest::generate(station.Entrance.ToCoordsXYZ());
    ASSERT_NE(firstGuest, nullptr);
    const auto firstReservation = RideReserveStationPlatformSlot(*targetRide, targetStation, firstGuest->id);
    ASSERT_TRUE(firstReservation.has_value());
    EXPECT_NE(firstReservation->seatIndex, 0);
    firstGuest->CurrentRide = targetRide->id;
    firstGuest->CurrentRideStation = targetStation;
    firstGuest->CurrentTrain = RideStation::kNoTrain;
    firstGuest->CurrentCar = firstReservation->carIndex;
    firstGuest->CurrentSeat = firstReservation->seatIndex;
    firstGuest->State = PeepState::enteringRide;
    firstGuest->RideSubState = PeepRideSubState::inEntrance;

    const auto* firstRideEntry = firstTrain->GetRideEntry();
    ASSERT_NE(firstRideEntry, nullptr);
    const auto& firstCarEntry = firstRideEntry->Cars[firstTrain->vehicle_type];
    const auto entranceOffset = firstCarEntry.flags.hasAny(
                                    CarEntryFlag::isMiniGolf, CarEntryFlag::isChairlift, CarEntryFlag::isGoKart)
        ? 32
        : 21;
    auto entranceWaypoint = station.Entrance.ToCoordsXYZD().ToTileCentre();
    const auto entranceDirection = station.Entrance.direction;
    ASSERT_LT(entranceDirection, kNumOrthogonalDirections);
    const auto entranceNormal = DirectionOffsets[entranceDirection];
    entranceWaypoint.x += entranceNormal.x * entranceOffset;
    entranceWaypoint.y += entranceNormal.y * entranceOffset;

    firstGuest->moveTo(
        { entranceWaypoint.x - entranceNormal.x * 8, entranceWaypoint.y - entranceNormal.y * 8, station.GetBaseZ() });
    firstGuest->SetDestination(entranceWaypoint, 2);
    firstGuest->Action = PeepActionType::walking;
    firstGuest->StepProgress = std::numeric_limits<uint8_t>::max();
    station.TrainAtStation = RideStation::kNoTrain;

    firstGuest->update();
    EXPECT_EQ(firstGuest->RideSubState, PeepRideSubState::inEntrance);
    EXPECT_EQ(firstGuest->GetDestination().x, entranceWaypoint.x);
    EXPECT_EQ(firstGuest->GetDestination().y, entranceWaypoint.y);
    for (int32_t tick = 0; tick < 32 && firstGuest->RideSubState == PeepRideSubState::inEntrance; tick++)
    {
        firstGuest->StepProgress = std::numeric_limits<uint8_t>::max();
        firstGuest->update();
    }
    ASSERT_EQ(firstGuest->RideSubState, PeepRideSubState::approachPlatformSlot);
    EXPECT_LE(std::abs(firstGuest->x - entranceWaypoint.x) + std::abs(firstGuest->y - entranceWaypoint.y), 2);
    EXPECT_EQ(firstGuest->GetDestination().x, firstReservation->waitPosition.x);
    EXPECT_EQ(firstGuest->GetDestination().y, firstReservation->waitPosition.y);
    if (entranceNormal.x != 0)
    {
        EXPECT_EQ(firstReservation->waitPosition.x, entranceWaypoint.x);
    }
    else
    {
        EXPECT_EQ(firstReservation->waitPosition.y, entranceWaypoint.y);
    }

    station.TrainAtStation = 0;

    arrivingTrain->status = Vehicle::Status::movingToEndOfStation;
    EXPECT_FALSE(RidePrepareStationPlatformBoarding(*targetRide, targetStation, *arrivingTrain));
    const auto unchangedReservation = RideGetStationPlatformReservation(*targetRide, targetStation, firstGuest->id);
    ASSERT_TRUE(unchangedReservation.has_value());
    EXPECT_EQ(unchangedReservation->slotIndex, firstReservation->slotIndex);
    EXPECT_EQ(unchangedReservation->carIndex, firstReservation->carIndex);
    EXPECT_EQ(unchangedReservation->seatIndex, firstReservation->seatIndex);
    EXPECT_EQ(
        RideBindStationPlatformGuestToSeat(*targetRide, targetStation, 0, *firstGuest),
        RideStationPlatformSeatBindingResult::success);
    RideReleaseStationPlatformSlot(*targetRide, targetStation, firstGuest->id);

    auto* handoffGuest = Guest::generate(station.Entrance.ToCoordsXYZ());
    ASSERT_NE(handoffGuest, nullptr);
    const auto oldReservation = RideReserveStationPlatformSlot(*targetRide, targetStation, handoffGuest->id);
    ASSERT_TRUE(oldReservation.has_value());
    handoffGuest->CurrentRide = targetRide->id;
    handoffGuest->CurrentRideStation = targetStation;
    handoffGuest->CurrentTrain = RideStation::kNoTrain;
    handoffGuest->CurrentCar = oldReservation->carIndex;
    handoffGuest->CurrentSeat = oldReservation->seatIndex;
    handoffGuest->State = PeepState::enteringRide;
    handoffGuest->RideSubState = PeepRideSubState::waitingOnPlatform;

    station.TrainAtStation = RideStation::kNoTrain;
    EXPECT_EQ(
        RideBindStationPlatformGuestToSeat(*targetRide, targetStation, 0, *handoffGuest),
        RideStationPlatformSeatBindingResult::seatUnavailable);

    station.TrainAtStation = 1;
    ASSERT_TRUE(RidePrepareStationPlatformBoarding(*targetRide, targetStation, *arrivingTrain));

    const auto newReservation = RideGetStationPlatformReservation(*targetRide, targetStation, handoffGuest->id);
    ASSERT_TRUE(newReservation.has_value());
    EXPECT_EQ(newReservation->seatIndex, 0);
    EXPECT_EQ(
        RideBindStationPlatformGuestToSeat(*targetRide, targetStation, 1, *handoffGuest),
        RideStationPlatformSeatBindingResult::success);
    EXPECT_EQ(handoffGuest->CurrentTrain, 1);

    // Recreate the reported overlap through the production guest and vehicle updates. A lone guest reserves the first
    // half of a paired car while the front train waits below half load. The blocked follower remains moving-to-end; its
    // arrival must make the front train ready, stop later platform bindings, let the already bound guest finish boarding,
    // and release the station instead of leaving num_peeps and next_free_seat permanently mismatched.
    RideClearAllStationPlatformPreQueues();
    clearTrain(*firstTrain);
    clearTrain(*arrivingTrain);
    firstTrain->peep[1] = handoffGuest->id;
    ASSERT_TRUE(RideCaptureStationPlatformTemplate(*targetRide, targetStation, *firstTrain));
    RideActivateStationPlatformPreQueue(*targetRide, targetStation);

    targetRide->departFlags = static_cast<uint8_t>(
                                  RIDE_DEPART_WAIT_FOR_LOAD | RIDE_DEPART_LEAVE_WHEN_ANOTHER_ARRIVES
                                  | RIDE_DEPART_WAIT_FOR_MINIMUM_LENGTH)
        | static_cast<uint8_t>(WAIT_FOR_LOAD_HALF);
    targetRide->minWaitingTime = 10;
    firstTrain->current_station = targetStation;
    firstTrain->status = Vehicle::Status::waitingForPassengers;
    firstTrain->sub_state = 1;
    firstTrain->time_waiting = 0;
    firstTrain->flags.unset(VehicleFlag::readyToDepart, VehicleFlag::waitingOnAdjacentStation);
    arrivingTrain->current_station = targetStation;
    arrivingTrain->status = Vehicle::Status::movingToEndOfStation;
    arrivingTrain->velocity = 0;
    station.TrainAtStation = 0;
    station.Depart = targetRide->minWaitingTime;
    ASSERT_TRUE(RidePrepareStationPlatformBoarding(*targetRide, targetStation, *firstTrain));

    auto* unmatchedGuest = Guest::generate(station.Entrance.ToCoordsXYZ());
    ASSERT_NE(unmatchedGuest, nullptr);
    const auto unmatchedReservation = RideReserveStationPlatformSlot(*targetRide, targetStation, unmatchedGuest->id);
    ASSERT_TRUE(unmatchedReservation.has_value());
    ASSERT_EQ(unmatchedReservation->seatIndex, 0);
    unmatchedGuest->moveTo(unmatchedReservation->waitPosition);
    unmatchedGuest->SetDestination(unmatchedReservation->waitPosition, 2);
    unmatchedGuest->CurrentRide = targetRide->id;
    unmatchedGuest->CurrentRideStation = targetStation;
    unmatchedGuest->CurrentTrain = RideStation::kNoTrain;
    unmatchedGuest->CurrentCar = unmatchedReservation->carIndex;
    unmatchedGuest->CurrentSeat = unmatchedReservation->seatIndex;
    unmatchedGuest->State = PeepState::enteringRide;
    ASSERT_EQ(
        RideBindStationPlatformGuestToSeat(*targetRide, targetStation, 0, *unmatchedGuest),
        RideStationPlatformSeatBindingResult::success);
    RideReleaseStationPlatformSlot(*targetRide, targetStation, unmatchedGuest->id);
    unmatchedGuest->RideSubState = PeepRideSubState::approachVehicle;

    ASSERT_EQ(unmatchedGuest->CurrentTrain, 0);
    ASSERT_EQ(unmatchedGuest->RideSubState, PeepRideSubState::approachVehicle);
    ASSERT_EQ(firstTrain->num_peeps, 0);
    ASSERT_EQ(firstTrain->next_free_seat, 1);
    ASSERT_FALSE(RideGetStationPlatformReservation(*targetRide, targetStation, unmatchedGuest->id).has_value());

    firstTrain->Update();
    ASSERT_TRUE(firstTrain->flags.has(VehicleFlag::readyToDepart));
    ASSERT_EQ(station.TrainAtStation, 0);

    auto* followingGuest = Guest::generate(station.Entrance.ToCoordsXYZ());
    ASSERT_NE(followingGuest, nullptr);
    const auto followingReservation = RideReserveStationPlatformSlot(*targetRide, targetStation, followingGuest->id);
    ASSERT_TRUE(followingReservation.has_value());
    followingGuest->moveTo(followingReservation->waitPosition);
    followingGuest->SetDestination(followingReservation->waitPosition, 2);
    followingGuest->CurrentRide = targetRide->id;
    followingGuest->CurrentRideStation = targetStation;
    followingGuest->CurrentTrain = RideStation::kNoTrain;
    followingGuest->CurrentCar = followingReservation->carIndex;
    followingGuest->CurrentSeat = followingReservation->seatIndex;
    followingGuest->State = PeepState::enteringRide;
    followingGuest->RideSubState = PeepRideSubState::waitingOnPlatform;
    EXPECT_EQ(
        RideBindStationPlatformGuestToSeat(*targetRide, targetStation, 0, *followingGuest),
        RideStationPlatformSeatBindingResult::seatUnavailable);
    EXPECT_EQ(followingGuest->CurrentTrain, RideStation::kNoTrain);
    EXPECT_EQ(firstTrain->next_free_seat, 1);
    EXPECT_TRUE(RideGetStationPlatformReservation(*targetRide, targetStation, followingGuest->id).has_value());

    for (int32_t tick = 0; tick < 64 && unmatchedGuest->State != PeepState::onRide; tick++)
    {
        unmatchedGuest->StepProgress = std::numeric_limits<uint8_t>::max();
        unmatchedGuest->update();
    }
    ASSERT_EQ(unmatchedGuest->State, PeepState::onRide);
    ASSERT_EQ(firstTrain->num_peeps, firstTrain->next_free_seat);

    firstTrain->Update();
    ASSERT_EQ(station.TrainAtStation, RideStation::kNoTrain);
    ASSERT_EQ(firstTrain->status, Vehicle::Status::waitingForPassengers);
    ASSERT_EQ(firstTrain->sub_state, 2);

    for (int32_t tick = 0; tick < 32 && firstTrain->status != Vehicle::Status::departing; tick++)
    {
        firstTrain->Update();
    }
    EXPECT_EQ(firstTrain->status, Vehicle::Status::departing);
}

TEST_F(PlayTests, NaturallyArrivingTrainBoardsAnAlreadyStagedGuest)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));

    auto& gameState = getGameState();
    RideClearAllStationPlatformPreQueues();

    Ride* targetRide = nullptr;
    Vehicle* targetTrain = nullptr;
    int32_t shortestTrack = std::numeric_limits<int32_t>::max();
    for (auto& ride : RideManager(gameState))
    {
        if (!RideSupportsStationPlatformPreQueue(ride) || ride.numTrains != 1 || ride.numStations != 1
            || ride.mode != RideMode::continuousCircuit
            || ride.getRideTypeDescriptor().Category != RideCategory::rollerCoaster)
        {
            continue;
        }
        auto& station = ride.getStation(StationIndex::FromUnderlying(0));
        auto* train = gameState.entities.GetEntity<Vehicle>(ride.vehicles[0]);
        auto* origin = ride.getOriginElement(StationIndex::FromUnderlying(0));
        const auto trackLength = ride.getTotalLength();
        if (station.Entrance.IsNull() || station.Exit.IsNull() || train == nullptr || origin == nullptr
            || train->IsUsedInPairs()
            || trackLength <= 0 || trackLength >= shortestTrack
            || RideVehicle::StationDetail::BuildTrainSeatSummary(*train).capacity == 0)
        {
            continue;
        }
        const auto stationDirection = origin->getDirection();
        station.Entrance.direction = (stationDirection + 1) & kTileElementDirectionMask;
        station.Exit.direction = (stationDirection + 3) & kTileElementDirectionMask;
        if (!RideCaptureStationPlatformTemplate(ride, StationIndex::FromUnderlying(0), *train))
        {
            continue;
        }
        targetRide = &ride;
        targetTrain = train;
        shortestTrack = trackLength;
    }

    ASSERT_NE(targetRide, nullptr);
    ASSERT_NE(targetTrain, nullptr);
    RideClearAllStationPlatformPreQueues();
    constexpr auto targetStation = StationIndex::FromUnderlying(0);
    targetRide->status = RideStatus::open;
    targetRide->flags.unset(RideFlag::brokenDown, RideFlag::breakdownPending);
    targetRide->vehicleChangeTimeout = 0;
    targetRide->departFlags = 0;
    gameState.park.flags |= PARK_FLAGS_NO_MONEY;

    bool departedNaturally = false;
    for (int32_t tick = 0; tick < 6000; tick++)
    {
        gameStateUpdateLogic();
        departedNaturally = RideStationPlatformPreQueueIsActive(*targetRide, targetStation)
            && targetTrain->status == Vehicle::Status::travelling;
        if (departedNaturally)
            break;
    }
    ASSERT_TRUE(departedNaturally);

    auto* guest = Guest::generate(targetRide->getStation(targetStation).Entrance.ToCoordsXYZ());
    ASSERT_NE(guest, nullptr);
    const auto reservation = RideReserveStationPlatformSlot(*targetRide, targetStation, guest->id);
    ASSERT_TRUE(reservation.has_value());
    guest->moveTo(reservation->waitPosition);
    guest->SetDestination(reservation->waitPosition, 2);
    guest->CurrentRide = targetRide->id;
    guest->CurrentRideStation = targetStation;
    guest->CurrentTrain = RideStation::kNoTrain;
    guest->CurrentCar = reservation->carIndex;
    guest->CurrentSeat = reservation->seatIndex;
    guest->State = PeepState::enteringRide;
    guest->RideSubState = PeepRideSubState::waitingOnPlatform;
    guest->DestinationTolerance = 0;
    guest->StepProgress = std::numeric_limits<uint8_t>::max();

    // Alighting guests normally leave these inactive array entries behind. Seed the same persisted state explicitly so
    // the regression remains deterministic even if this particular train happened to depart empty on the first circuit.
    for (auto* car = targetTrain; car != nullptr;
         car = gameState.entities.GetEntity<Vehicle>(car->next_vehicle_on_train))
    {
        const auto seatCount = static_cast<uint8_t>(
            std::min<size_t>(car->num_seats & kVehicleSeatNumMask, std::size(car->peep)));
        const auto activeSeats = static_cast<uint8_t>(
            std::min<size_t>(std::max(car->num_peeps, car->next_free_seat), seatCount));
        std::fill(std::begin(car->peep) + activeSeats, std::begin(car->peep) + seatCount, guest->id);
    }

    bool arrivedAndStopped = false;
    bool seatBound = false;
    bool departedEmpty = false;
    bool boarded = false;
    std::ostringstream trace;
    for (int32_t tick = 0; tick < 12000; tick++)
    {
        gameStateUpdateLogic();
        if (targetTrain->current_station == targetStation
            && targetTrain->status == Vehicle::Status::waitingForPassengers)
        {
            arrivedAndStopped = true;
        }
        seatBound = seatBound || guest->CurrentTrain == 0;
        if (guest->State == PeepState::onRide)
        {
            const auto* car = targetTrain->GetCar(guest->CurrentCar);
            boarded = car != nullptr && car->peep[guest->CurrentSeat] == guest->id;
            if (boarded)
                break;
        }
        if (arrivedAndStopped && !seatBound && targetTrain->status == Vehicle::Status::travelling)
        {
            departedEmpty = true;
            break;
        }
        if (tick % 256 == 0)
        {
            trace << "tick=" << tick << " trainStatus=" << static_cast<int32_t>(targetTrain->status)
                  << " trainSubState=" << static_cast<int32_t>(targetTrain->sub_state)
                  << " trainStation=" << static_cast<int32_t>(targetTrain->current_station.ToUnderlying())
                  << " trainAtStation=" << static_cast<int32_t>(targetRide->getStation(targetStation).TrainAtStation)
                  << " guestState=" << static_cast<int32_t>(guest->State)
                  << " guestSubState=" << static_cast<int32_t>(guest->RideSubState)
                  << " guestTrain=" << static_cast<int32_t>(guest->CurrentTrain) << '\n';
        }
    }

    EXPECT_TRUE(arrivedAndStopped) << trace.str();
    EXPECT_TRUE(seatBound) << trace.str();
    EXPECT_FALSE(departedEmpty) << trace.str();
    EXPECT_TRUE(boarded) << trace.str();
}

TEST_F(PlayTests, ParkEntranceFeeTargetsUseGuestCashAndDebuffedParkValue)
{
    std::string initStateFile = TestData::GetParkPath("small_park_with_ferris_wheel.sv6");

    auto context = localStartGame(initStateFile);
    ASSERT_NE(context.get(), nullptr);

    auto& gameState = getGameState();
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags &= ~PARK_FLAGS_PARK_FREE_ENTRY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;
    gameState.park.entranceFeeTarget = Park::ParkEntranceFeeTarget::affordable;

    gameState.scenarioOptions.guestInitialCash = 10.00_GBP;
    gameState.park.totalRideValueForMoney = 100.00_GBP;

    EXPECT_EQ(Park::GetEntranceFeeForTarget(gameState.park, Park::ParkEntranceFeeTarget::incomePerGuest), 30.00_GBP);
    EXPECT_EQ(Park::GetEntranceFeeForTarget(gameState.park, Park::ParkEntranceFeeTarget::profit), 20.00_GBP);
    EXPECT_EQ(Park::GetEntranceFeeForTarget(gameState.park, Park::ParkEntranceFeeTarget::affordable), 0.00_GBP);

    gameState.scenarioOptions.guestInitialCash = 1000.00_GBP;
    EXPECT_EQ(Park::GetEntranceFeeForTarget(gameState.park, Park::ParkEntranceFeeTarget::incomePerGuest), 70.00_GBP);

    auto result = executeImmediate<GameActions::ParkSetEntranceFeeAction>(Park::ParkEntranceFeeTarget::incomePerGuest);
    ASSERT_EQ(result.error, GameActions::Status::ok);
    EXPECT_EQ(gameState.park.entranceFeeTarget, Park::ParkEntranceFeeTarget::incomePerGuest);
    EXPECT_EQ(Park::GetEntranceFee(gameState.park), 70.00_GBP);
}

TEST_F(PlayTests, RideCreateConvertsLegacyDefaultPricesToCentMoney)
{
    std::string initStateFile = TestData::GetParkPath("small_park_with_ferris_wheel.sv6");

    auto context = localStartGame(initStateFile);
    ASSERT_NE(context.get(), nullptr);

    auto& gameState = getGameState();
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;
    gameState.park.entranceFee = 0.00_GBP;

    const auto& rtd = GetRideTypeDescriptor(RIDE_TYPE_FERRIS_WHEEL);
    const auto defaultPrice = rtd.DefaultPrices[0];
    const auto defaultPriceCentMoney = ToMoney64(static_cast<money32>(defaultPrice));
    ASSERT_EQ(defaultPrice, 10);
    ASSERT_EQ(defaultPriceCentMoney, 1.00_GBP);

    auto result = executeImmediate<GameActions::RideCreateAction>(
        RIDE_TYPE_FERRIS_WHEEL, kObjectEntryIndexNull, 0, 0, kObjectEntryIndexNull, RideInspection::every30Minutes);
    ASSERT_EQ(result.error, GameActions::Status::ok);

    const auto rideId = result.getData<RideId>();
    const auto* ride = GetRide(rideId);
    ASSERT_NE(ride, nullptr);
    EXPECT_EQ(ride->price[0], 1.00_GBP);
    EXPECT_EQ(ride->price[1], 0.00_GBP);
}

TEST_F(PlayTests, RideSetPriceActionPreservesCentPrices)
{
    std::string initStateFile = TestData::GetParkPath("small_park_with_ferris_wheel.sv6");

    auto context = localStartGame(initStateFile);
    ASSERT_NE(context.get(), nullptr);

    auto& gameState = getGameState();
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    auto rideManager = RideManager(gameState);
    auto it = std::find_if(
        rideManager.begin(), rideManager.end(), [](const auto& ride) { return ride.type == RIDE_TYPE_FERRIS_WHEEL; });
    ASSERT_NE(it, rideManager.end());
    Ride& ferrisWheel = *it;

    auto result = executeImmediate<GameActions::RideSetPriceAction>(ferrisWheel.id, 1.23_GBP, true);
    ASSERT_EQ(result.error, GameActions::Status::ok);
    EXPECT_EQ(ferrisWheel.price[0], 1.23_GBP);
}

TEST_F(PlayTests, MazeCapacityModesDeriveCapacityFromTileCount)
{
    Ride maze{};
    maze.type = RIDE_TYPE_MAZE;

    EXPECT_EQ(maze.getMazeMaximumCapacity(), 0);
    EXPECT_EQ(maze.getOperationOptionMinimum(false), 0);
    EXPECT_EQ(maze.getOperationOptionMaximum(false), 2);
    EXPECT_EQ(maze.getDefaultOperationOption(), static_cast<uint8_t>(MazeCapacityMode::normal));
    EXPECT_EQ(maze.getEffectiveOperationOption(), 0);

    maze.mazeTiles = 5;
    maze.operationOption = 64;
    EXPECT_EQ(maze.getMazeCapacityMode(), MazeCapacityMode::normal);
    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::sparse), 2);
    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::normal), 5);
    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::overcrowded), 10);
    EXPECT_EQ(maze.getMazeMaximumCapacity(), 10);
    EXPECT_EQ(maze.getStoredOperationOption(), static_cast<uint8_t>(MazeCapacityMode::normal));
    EXPECT_EQ(maze.getEffectiveOperationOption(), 5);

    maze.updateMazeCapacityForConstruction();
    EXPECT_EQ(maze.operationOption, static_cast<uint8_t>(MazeCapacityMode::normal));

    maze.operationOption = static_cast<uint8_t>(MazeCapacityMode::sparse);
    EXPECT_EQ(maze.getEffectiveOperationOption(), 2);
    EXPECT_EQ(maze.getMazeRatingAccumulatorScale(), std::make_pair(2, 1));

    maze.mazeTiles = 1;
    EXPECT_EQ(maze.getEffectiveOperationOption(), 1);

    maze.mazeTiles = 100;
    maze.operationOption = static_cast<uint8_t>(MazeCapacityMode::overcrowded);
    maze.updateMazeCapacityForConstruction();
    EXPECT_EQ(maze.operationOption, static_cast<uint8_t>(MazeCapacityMode::overcrowded));
    EXPECT_EQ(maze.getEffectiveOperationOption(), 200);
    EXPECT_EQ(maze.getMazeRatingAccumulatorScale(), std::make_pair(1, 2));

    maze.mazeTiles = 300;
    EXPECT_EQ(maze.getMazeMaximumCapacity(), Limits::kCheatsMaxOperatingLimit);
}

TEST_F(PlayTests, LegacyMazeCapacityMapsToClosestCapacityMode)
{
    Ride maze{};
    maze.type = RIDE_TYPE_MAZE;
    maze.mazeTiles = 4;

    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::sparse), 2);
    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::normal), 4);
    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::overcrowded), 8);

    EXPECT_EQ(maze.getClosestMazeCapacityModeForCapacity(1), MazeCapacityMode::sparse);
    EXPECT_EQ(maze.getClosestMazeCapacityModeForCapacity(4), MazeCapacityMode::normal);
    EXPECT_EQ(maze.getClosestMazeCapacityModeForCapacity(7), MazeCapacityMode::overcrowded);

    // Ties favour normal so old saves do not become sparse or overcrowded unless they are closer.
    EXPECT_EQ(maze.getClosestMazeCapacityModeForCapacity(3), MazeCapacityMode::normal);

    maze.mazeTiles = 1;
    EXPECT_EQ(maze.getClosestMazeCapacityModeForCapacity(1), MazeCapacityMode::normal);
    EXPECT_EQ(maze.getClosestMazeCapacityModeForCapacity(4), MazeCapacityMode::overcrowded);

    maze.operationOption = 64;
    maze.normaliseMazeCapacityMode();
    EXPECT_EQ(maze.operationOption, static_cast<uint8_t>(MazeCapacityMode::normal));
}

TEST_F(PlayTests, GameFixRideNumRidersRebuildsCountsFromGuestStates)
{
    auto& gameState = getGameState();
    gameState.entities.ResetAllEntities();
    for (auto& ride : gameState.rides)
    {
        ride.id = RideId::GetNull();
        ride.type = kRideTypeNull;
        ride.numRiders = 0;
    }
    gameState.ridesEndOfUsedRange = 2;

    auto& maze = gameState.rides[0];
    maze.id = RideId::FromUnderlying(0);
    maze.type = RIDE_TYPE_MAZE;
    maze.mazeTiles = 1;
    maze.operationOption = static_cast<uint8_t>(MazeCapacityMode::normal);
    maze.numRiders = 64;

    auto& ferrisWheel = gameState.rides[1];
    ferrisWheel.id = RideId::FromUnderlying(1);
    ferrisWheel.type = RIDE_TYPE_FERRIS_WHEEL;
    ferrisWheel.numRiders = 64;

    auto addGuest = [&gameState](RideId rideId, PeepState state) {
        auto* guest = gameState.entities.CreateEntity<Guest>();
        EXPECT_NE(guest, nullptr);
        guest->CurrentRide = rideId;
        guest->State = state;
        return guest;
    };

    addGuest(maze.id, PeepState::onRide);
    addGuest(maze.id, PeepState::enteringRide);
    addGuest(maze.id, PeepState::walking);
    addGuest(ferrisWheel.id, PeepState::onRide);
    addGuest(RideId::FromUnderlying(99), PeepState::onRide);

    GameFixRideNumRiders();

    EXPECT_EQ(maze.getEffectiveOperationOption(), 1);
    EXPECT_EQ(maze.numRiders, 2);
    EXPECT_GT(maze.numRiders, maze.getEffectiveOperationOption());
    EXPECT_EQ(ferrisWheel.numRiders, 1);
}

TEST_F(PlayTests, ImportedMazeTrackDesignCapacityMapsToClosestMode)
{
    TrackDesign mazeDesign{};
    mazeDesign.trackAndVehicle.rtdIndex = RIDE_TYPE_MAZE;

    mazeDesign.operation.operationSetting = 1;
    mazeDesign.mazeElements.resize(4);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::sparse));

    mazeDesign.operation.operationSetting = 3;
    mazeDesign.mazeElements.resize(4);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::normal));

    mazeDesign.operation.operationSetting = 7;
    mazeDesign.mazeElements.resize(4);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::overcrowded));

    mazeDesign.operation.operationSetting = 4;
    mazeDesign.mazeElements.resize(37);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::sparse));

    mazeDesign.operation.operationSetting = 4;
    mazeDesign.mazeElements.resize(1);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::overcrowded));
}

static Park::ParkData MakeGuestGenerationPark(uint16_t rating, money64 value)
{
    Park::ParkData park{};
    park.flags = PARK_FLAGS_NO_MONEY;
    park.rating = rating;
    park.value = value;
    return park;
}

TEST_F(PlayTests, GuestGenerationRatingScaleDoublesEveryHundredRatingPoints)
{
    EXPECT_EQ(Park::CalculateGuestGenerationProbability(MakeGuestGenerationPark(500, 50000.00_GBP)), 213u);
    EXPECT_EQ(Park::CalculateGuestGenerationProbability(MakeGuestGenerationPark(600, 50000.00_GBP)), 425u);
    EXPECT_EQ(Park::CalculateGuestGenerationProbability(MakeGuestGenerationPark(700, 50000.00_GBP)), 850u);
    EXPECT_EQ(Park::CalculateGuestGenerationProbability(MakeGuestGenerationPark(800, 50000.00_GBP)), 1700u);
}

TEST_F(PlayTests, GuestGenerationKeepsSmallPositiveParkValueChance)
{
    EXPECT_EQ(Park::CalculateGuestGenerationProbability(MakeGuestGenerationPark(600, 0.00_GBP)), 0u);
    EXPECT_EQ(Park::CalculateGuestGenerationProbability(MakeGuestGenerationPark(600, 0.01_GBP)), 1u);
}

TEST_F(PlayTests, RideTargetPriceUsesIncomeDebuff)
{
    std::string initStateFile = TestData::GetParkPath("small_park_with_ferris_wheel.sv6");

    auto context = localStartGame(initStateFile);
    ASSERT_NE(context.get(), nullptr);

    auto& gameState = getGameState();
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;
    gameState.park.entranceFeeTarget = Park::ParkEntranceFeeTarget::custom;
    gameState.park.entranceFee = 0.00_GBP;

    auto rideManager = RideManager(gameState);
    auto it = std::find_if(
        rideManager.begin(), rideManager.end(), [](const auto& ride) { return ride.type == RIDE_TYPE_FERRIS_WHEEL; });
    ASSERT_NE(it, rideManager.end());
    Ride& ferrisWheel = *it;
    ferrisWheel.value = 10.00_GBP;

    EXPECT_EQ(RideGetTargetPrice(ferrisWheel, RidePriceTarget::neutral), 6.30_GBP);
}

TEST_F(PlayTests, GuestRideValueThresholdsUseIncomeDebuff)
{
    std::string initStateFile = TestData::GetParkPath("small_park_with_ferris_wheel.sv6");

    auto context = localStartGame(initStateFile);
    ASSERT_NE(context.get(), nullptr);

    auto& gameState = getGameState();
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;
    gameState.park.entranceFeeTarget = Park::ParkEntranceFeeTarget::custom;
    gameState.park.entranceFee = 0.00_GBP;
    gameState.cheats.ignoreRideIntensity = true;

    Ride* ferrisWheel = FindFerrisWheel(gameState);
    ASSERT_NE(ferrisWheel, nullptr);
    auto openResult = executeImmediate<GameActions::RideSetStatusAction>(ferrisWheel->id, RideStatus::open);
    ASSERT_EQ(openResult.error, GameActions::Status::ok);
    ferrisWheel->value = 10.00_GBP;
    auto& station = ferrisWheel->getStation(StationIndex::FromUnderlying(0));
    station.LastPeepInQueue = EntityId::GetNull();
    station.QueueLength = 0;

    auto* badValueGuest = Park::GenerateGuest();
    badValueGuest->cashInPocket = 100.00_GBP;
    badValueGuest->guestHeadingToRideId = ferrisWheel->id;

    auto result = executeImmediate<GameActions::RideSetPriceAction>(ferrisWheel->id, 14.01_GBP, true);
    ASSERT_EQ(result.error, GameActions::Status::ok);
    EXPECT_FALSE(badValueGuest->shouldGoOnRide(*ferrisWheel, StationIndex::FromUnderlying(0), false, false));
    EXPECT_TRUE(GuestHasRideThought(*badValueGuest, PeepThoughtType::badValue, ferrisWheel->id));

    auto* expensiveGuest = Park::GenerateGuest();
    expensiveGuest->cashInPocket = 100.00_GBP;
    expensiveGuest->guestHeadingToRideId = ferrisWheel->id;

    result = executeImmediate<GameActions::RideSetPriceAction>(ferrisWheel->id, 10.51_GBP, true);
    ASSERT_EQ(result.error, GameActions::Status::ok);
    EXPECT_TRUE(expensiveGuest->shouldGoOnRide(*ferrisWheel, StationIndex::FromUnderlying(0), false, false));
    EXPECT_TRUE(GuestHasRideThought(*expensiveGuest, PeepThoughtType::expensiveRide, ferrisWheel->id));
    EXPECT_FALSE(GuestHasRideThought(*expensiveGuest, PeepThoughtType::badValue, ferrisWheel->id));

    auto* goodValueGuest = Park::GenerateGuest();
    goodValueGuest->cashInPocket = 100.00_GBP;
    goodValueGuest->guestHeadingToRideId = ferrisWheel->id;

    result = executeImmediate<GameActions::RideSetPriceAction>(ferrisWheel->id, 3.50_GBP, true);
    ASSERT_EQ(result.error, GameActions::Status::ok);
    EXPECT_TRUE(goodValueGuest->shouldGoOnRide(*ferrisWheel, StationIndex::FromUnderlying(0), false, false));
    EXPECT_TRUE(GuestHasRideThought(*goodValueGuest, PeepThoughtType::goodValue, ferrisWheel->id));
}

TEST_F(PlayTests, NiceRidePhoenixThoughtIsARareFallback)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    Ride ride{};
    ride.id = RideId::FromUnderlying(5);
    ride.type = RIDE_TYPE_SPIRAL_ROLLER_COASTER;

    Guest guest{};
    guest.happiness = 100;
    guest.happinessTarget = 100;
    guest.Energy = 0;
    for (auto& thought : guest.thoughts)
    {
        thought.type = PeepThoughtType::none;
    }

    // A zero first RNG result hits the 1-in-2048 branch.
    ScenarioRandSeed(0, 0);
    guest.onExitRide(ride);
    EXPECT_EQ(guest.thoughts[0].type, PeepThoughtType::niceRideDeprecated);

    for (auto& thought : guest.thoughts)
    {
        thought.type = PeepThoughtType::none;
    }
    ScenarioRandSeed(8, 0);
    guest.onExitRide(ride);
    EXPECT_EQ(guest.thoughts[0].type, PeepThoughtType::none);

    guest.insertNewThought(PeepThoughtType::badValue, ride.id);
    ScenarioRandSeed(0, 0);
    guest.onExitRide(ride);
    EXPECT_EQ(guest.thoughts[0].type, PeepThoughtType::badValue);
}
