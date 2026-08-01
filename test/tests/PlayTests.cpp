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
#include <openrct2/actions/park/ParkMarketingAction.h>
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
        .simulationBatches = 80,
        .draws = 30,
        .simulationSeconds = 1.6,
        .drawSeconds = 0.2,
        .longestSimulationBatchSeconds = 0.04,
        .longestSimulationSliceSeconds = 0.006,
    };

    const auto metrics = CalculateIntegratedBenchmarkMetrics(totals);
    EXPECT_DOUBLE_EQ(metrics.logicalTicksPerSecond, 320.0);
    EXPECT_DOUBLE_EQ(metrics.framesPerSecond, 15.0);
    EXPECT_DOUBLE_EQ(metrics.simulationUtilisationPercent, 80.0);
    EXPECT_DOUBLE_EQ(metrics.drawUtilisationPercent, 10.0);
    EXPECT_DOUBLE_EQ(metrics.meanSimulationMicrosecondsPerLogicalTick, 2500.0);
    EXPECT_DOUBLE_EQ(metrics.meanSimulationMicrosecondsPerBatch, 20000.0);
    EXPECT_NEAR(metrics.meanDrawMicroseconds, 6666.6666667, 0.0001);
    EXPECT_DOUBLE_EQ(metrics.longestSimulationBatchMilliseconds, 40.0);
    EXPECT_DOUBLE_EQ(metrics.longestSimulationSliceMilliseconds, 6.0);
}

TEST_F(PlayTests, IntegratedBenchmarkMetricsHandleEmptyMeasurement)
{
    const auto metrics = CalculateIntegratedBenchmarkMetrics({});
    EXPECT_DOUBLE_EQ(metrics.logicalTicksPerSecond, 0.0);
    EXPECT_DOUBLE_EQ(metrics.framesPerSecond, 0.0);
    EXPECT_DOUBLE_EQ(metrics.meanSimulationMicrosecondsPerLogicalTick, 0.0);
    EXPECT_DOUBLE_EQ(metrics.meanSimulationMicrosecondsPerBatch, 0.0);
    EXPECT_DOUBLE_EQ(metrics.meanDrawMicroseconds, 0.0);
    EXPECT_DOUBLE_EQ(metrics.longestSimulationBatchMilliseconds, 0.0);
    EXPECT_DOUBLE_EQ(metrics.longestSimulationSliceMilliseconds, 0.0);
}

TEST_F(PlayTests, GameSpeedsSelectTickRatesWithoutChangingTheLogicalUpdateShape)
{
    EXPECT_EQ(GetGameSpeedMultiplier(1), 1u);
    EXPECT_EQ(GetGameSpeedMultiplier(2), 2u);
    EXPECT_EQ(GetGameSpeedMultiplier(3), 4u);
    EXPECT_EQ(GetGameSpeedMultiplier(kGameSpeedTurbo), 9u);
    EXPECT_EQ(GetGameSpeedMultiplier(kGameSpeedTurbo + 1), 16u);
    EXPECT_EQ(GetGameSpeedTargetTicksPerSecond(1), 40u);
    EXPECT_EQ(GetGameSpeedTargetTicksPerSecond(2), 80u);
    EXPECT_EQ(GetGameSpeedTargetTicksPerSecond(3), 160u);
    EXPECT_EQ(kTurboTargetTicksPerSecond, 360u);
    EXPECT_EQ(GetGameSpeedTargetTicksPerSecond(kGameSpeedTurbo), 360u);
    EXPECT_FLOAT_EQ(GetGameSpeedUpdateTime(kGameSpeedTurbo), 1.0f / 360.0f);
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

static std::unique_ptr<IContext> LoadEverythingPark()
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    if (context != nullptr && context->Initialise())
    {
        context->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));
        RideClearAllStationPlatformPreQueues();
        return context;
    }
    return {};
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

TEST_F(PlayTests, InvalidMarketingCampaignDurationsAreRejected)
{
    auto context = localStartGame(TestData::GetParkPath("small_park_with_ferris_wheel.sv6"));
    ASSERT_NE(context.get(), nullptr);

    for (const auto duration : { -1, 0, 256 })
    {
        GameActions::ParkMarketingAction action(ADVERTISING_CAMPAIGN_PARK, 0, duration);
        auto& gameState = getGameState();
        const auto result = action.Query(gameState, gameState.park);
        EXPECT_EQ(result.error, GameActions::Status::invalidParameters) << "duration=" << duration;
    }

    for (const auto duration : { 1, 255 })
    {
        GameActions::ParkMarketingAction action(ADVERTISING_CAMPAIGN_PARK, 0, duration);
        auto& gameState = getGameState();
        const auto result = action.Query(gameState, gameState.park);
        EXPECT_EQ(result.error, GameActions::Status::ok) << "duration=" << duration;
    }
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

static void StagePlatformGuest(
    Guest& guest, const Ride& ride, StationIndex station, const RideStationPlatformReservation& reservation,
    PeepRideSubState subState = PeepRideSubState::waitingOnPlatform)
{
    guest.CurrentRide = ride.id;
    guest.CurrentRideStation = station;
    guest.CurrentTrain = RideStation::kNoTrain;
    guest.CurrentCar = reservation.carIndex;
    guest.CurrentSeat = reservation.seatIndex;
    guest.State = PeepState::enteringRide;
    guest.RideSubState = subState;
}

struct CapturedPlatformTrain
{
    Ride* ride{};
    Vehicle* train{};
    StationIndex station{ StationIndex::GetNull() };
    uint8_t trainIndex{};
};

template<typename Accept>
static CapturedPlatformTrain FindCapturedPlatformTrain(GameState_t& gameState, Accept&& accept)
{
    for (auto& ride : RideManager(gameState))
    {
        if (!RideSupportsStationPlatformPreQueue(ride))
            continue;
        for (uint8_t trainIndex = 0; trainIndex < ride.numTrains; trainIndex++)
        {
            auto* train = gameState.entities.GetEntity<Vehicle>(ride.vehicles[trainIndex]);
            if (train == nullptr)
                continue;
            for (uint8_t stationIndex = 0; stationIndex < ride.numStations; stationIndex++)
            {
                const auto station = StationIndex::FromUnderlying(stationIndex);
                if (accept(ride, *train, trainIndex, station)
                    && RideCaptureStationPlatformTemplate(ride, station, *train))
                    return { &ride, train, station, trainIndex };
            }
        }
    }
    return {};
}

static void OpenPlatformTestRide(Ride& ride)
{
    ride.status = RideStatus::open;
    ride.flags.unset(RideFlag::brokenDown, RideFlag::breakdownPending);
    ride.vehicleChangeTimeout = 0;
}

static void ClearTrain(GameState_t& gameState, Vehicle& head, bool clearPairFlag = false)
{
    for (auto* car = &head; car != nullptr; car = gameState.entities.GetEntity<Vehicle>(car->next_vehicle_on_train))
    {
        if (clearPairFlag)
            car->num_seats &= kVehicleSeatNumMask;
        car->num_peeps = 0;
        car->next_free_seat = 0;
        car->restraints_position = 255;
        std::fill(std::begin(car->peep), std::end(car->peep), EntityId::GetNull());
    }
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

TEST_F(PlayTests, GuestPaysAtEntranceBeforeBoarding)
{
    auto context = localStartGame(TestData::GetParkPath("small_park_with_ferris_wheel.sv6"));
    ASSERT_NE(context, nullptr);

    auto& gameState = getGameState();
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;
    gameState.cheats.ignorePrice = true;

    auto* ride = FindFerrisWheel(gameState);
    ASSERT_NE(ride, nullptr);
    constexpr auto stationIndex = StationIndex::FromUnderlying(0);
    auto& station = ride->getStation(stationIndex);
    ASSERT_FALSE(station.Entrance.IsNull());

    auto openResult = executeImmediate<GameActions::RideSetStatusAction>(ride->id, RideStatus::open);
    ASSERT_EQ(openResult.error, GameActions::Status::ok);

    constexpr money64 admission = 1.23_GBP;
    auto priceResult = executeImmediate<GameActions::RideSetPriceAction>(ride->id, admission, true);
    ASSERT_EQ(priceResult.error, GameActions::Status::ok);
    ASSERT_EQ(RideGetPrice(*ride), admission);

    auto* guest = Guest::generate(station.Entrance.ToCoordsXYZ());
    ASSERT_NE(guest, nullptr);
    guest->cashInPocket = 10.00_GBP;
    guest->CurrentRide = ride->id;
    guest->CurrentRideStation = stationIndex;
    guest->SetState(PeepState::queuingFront);
    guest->RideSubState = PeepRideSubState::atEntrance;
    guest->DestinationTolerance = 0;
    guest->guestNextInQueue = EntityId::GetNull();
    station.LastPeepInQueue = guest->id;
    station.QueueLength = 1;

    const auto profitBefore = ride->totalProfit;
    const bool paidAtEntrance = updateUntil(10000, [&]() { return guest->paidOnRides != 0.00_GBP; });

    ASSERT_TRUE(paidAtEntrance);
    const auto paidAdmission = guest->paidOnRides;
    EXPECT_EQ(guest->cashInPocket, 10.00_GBP - paidAdmission);
    EXPECT_GE(ride->totalProfit, profitBefore + paidAdmission);
    EXPECT_EQ(guest->State, PeepState::enteringRide);
    EXPECT_NE(guest->State, PeepState::onRide);

    // Progressing from the paid station area into a vehicle must not charge admission again.
    for (int32_t tick = 0; tick < 64; tick++)
    {
        gameStateUpdateLogic();
    }
    EXPECT_EQ(guest->cashInPocket, 10.00_GBP - paidAdmission);
    EXPECT_EQ(guest->paidOnRides, paidAdmission);
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
    auto context = LoadEverythingPark();
    ASSERT_NE(context, nullptr);
    auto& gameState = getGameState();

    TrackElement* coasterOrigin = nullptr;
    const auto coasterTarget = FindCapturedPlatformTrain(
        gameState, [&](Ride& ride, Vehicle&, uint8_t, StationIndex stationIndex) {
            if (ride.getRideTypeDescriptor().Category != RideCategory::rollerCoaster)
                return false;
            auto& station = ride.getStation(stationIndex);
            coasterOrigin = ride.getOriginElement(stationIndex);
            if (station.Entrance.IsNull() || station.Exit.IsNull() || coasterOrigin == nullptr)
                return false;
            const auto stationDirection = coasterOrigin->getDirection();
            station.Entrance.direction = (stationDirection + 1) & kTileElementDirectionMask;
            station.Exit.direction = (stationDirection + 3) & kTileElementDirectionMask;
            return true;
        });
    auto* coaster = coasterTarget.ride;
    auto* coasterTrain = coasterTarget.train;
    const auto coasterStation = coasterTarget.station;

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

    const auto transportTarget = FindCapturedPlatformTrain(
        gameState, [](Ride& ride, Vehicle&, uint8_t, StationIndex stationIndex) {
            if (!ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
                return false;
            auto& station = ride.getStation(stationIndex);
            if (station.Entrance.IsNull() || station.Exit.IsNull())
                return false;
            station.Exit.direction = station.Entrance.direction;
            return true;
        });
    EXPECT_NE(transportTarget.ride, nullptr);
}

TEST_F(PlayTests, SameSideCoasterGuestContinuesOrdinaryBoarding)
{
    auto context = LoadEverythingPark();
    ASSERT_NE(context, nullptr);
    auto& gameState = getGameState();

    TrackElement* coasterOrigin = nullptr;
    const auto target = FindCapturedPlatformTrain(
        gameState, [&](Ride& ride, Vehicle& train, uint8_t, StationIndex stationIndex) {
            if (ride.getRideTypeDescriptor().Category != RideCategory::rollerCoaster
                || (train.num_seats & kVehicleSeatNumMask) == 0)
                return false;
            auto& station = ride.getStation(stationIndex);
            coasterOrigin = ride.getOriginElement(stationIndex);
            if (station.Entrance.IsNull() || station.Exit.IsNull() || coasterOrigin == nullptr)
                return false;
            const auto stationDirection = coasterOrigin->getDirection();
            station.Entrance.direction = (stationDirection + 1) & kTileElementDirectionMask;
            station.Exit.direction = (stationDirection + 3) & kTileElementDirectionMask;
            return true;
        });

    ASSERT_NE(target.ride, nullptr);
    ASSERT_NE(target.train, nullptr);
    ASSERT_NE(coasterOrigin, nullptr);

    auto& station = target.ride->getStation(target.station);
    const auto stationDirection = coasterOrigin->getDirection();
    station.Entrance.direction = (stationDirection + 1) & kTileElementDirectionMask;
    station.Exit.direction = station.Entrance.direction;
    RideClearStationPlatformPreQueue(*target.ride);
    EXPECT_FALSE(RideCaptureStationPlatformTemplate(*target.ride, target.station, *target.train));
    RideActivateStationPlatformPreQueue(*target.ride, target.station);
    ASSERT_FALSE(RideStationPlatformPreQueueIsActive(*target.ride, target.station));

    OpenPlatformTestRide(*target.ride);
    gameState.park.flags |= PARK_FLAGS_NO_MONEY;
    ClearTrain(gameState, *target.train, true);
    target.train->current_station = target.station;
    target.train->status = Vehicle::Status::waitingForPassengers;
    target.train->sub_state = 1;
    target.train->flags.unset(VehicleFlag::readyToDepart, VehicleFlag::waitingOnAdjacentStation);
    station.TrainAtStation = target.trainIndex;

    auto* guest = Guest::generate(station.Entrance.ToCoordsXYZ());
    ASSERT_NE(guest, nullptr);
    guest->CurrentRide = target.ride->id;
    guest->CurrentRideStation = target.station;
    guest->CurrentTrain = target.trainIndex;
    guest->CurrentCar = 0;
    guest->CurrentSeat = 0;
    guest->State = PeepState::enteringRide;
    guest->RideSubState = PeepRideSubState::inEntrance;
    guest->SetDestination(guest->getLocation(), 2);
    guest->StepProgress = std::numeric_limits<uint8_t>::max();
    target.train->next_free_seat = 1;
    target.train->peep[0] = guest->id;

    guest->update();

    EXPECT_EQ(guest->State, PeepState::enteringRide);
    EXPECT_NE(guest->RideSubState, PeepRideSubState::approachExit);
    EXPECT_EQ(guest->CurrentTrain, target.trainIndex);
    EXPECT_EQ(target.train->next_free_seat, 1);
    EXPECT_EQ(target.train->peep[0], guest->id);
}

TEST_F(PlayTests, StationLoadingDecisionHasCompletePriorityOrder)
{
    using namespace RideVehicle::StationDetail;

    TrainSeatSummary train{};
    train.capacity = 8;
    train.currentPeeps = 3;
    train.reservedSeats = 5;

    StationLoadingPolicy policy{};
    EXPECT_TRUE(ShouldStopBoarding(train, policy));

    policy.waitForLoad = true;
    policy.loadTarget = 4;
    EXPECT_FALSE(ShouldStopBoarding(train, policy));
    train.currentPeeps = 4;
    EXPECT_TRUE(ShouldStopBoarding(train, policy));

    train.currentPeeps = 3;
    policy.maximumWaitElapsed = true;
    EXPECT_TRUE(ShouldStopBoarding(train, policy));

    policy.minimumWaitPending = true;
    EXPECT_FALSE(ShouldStopBoarding(train, policy));
    policy.incomingTrain = true;
    EXPECT_TRUE(ShouldStopBoarding(train, policy));

    policy.incomingTrain = false;
    policy.minimumWaitPending = false;
    policy.initialDwellPending = true;
    EXPECT_FALSE(ShouldStopBoarding(train, policy));
    policy.initialDwellPending = false;
    policy.emptyTrainMustWait = true;
    EXPECT_FALSE(ShouldStopBoarding(train, policy));
}




TEST_F(PlayTests, TrainCannotPublishStationWhileZeroPrefixPassengersAreStillAlighting)
{
    auto context = LoadEverythingPark();
    ASSERT_NE(context, nullptr);
    auto& gameState = getGameState();
    const auto target = FindCapturedPlatformTrain(
        gameState, [](const Ride& ride, const Vehicle&, uint8_t trainIndex, StationIndex stationIndex) {
            return trainIndex == 0 && ride.getRideTypeDescriptor().Category == RideCategory::rollerCoaster
                && !ride.getStation(stationIndex).Exit.IsNull();
        });
    ASSERT_NE(target.ride, nullptr);
    ASSERT_NE(target.train, nullptr);

    ClearTrain(gameState, *target.train);
    target.train->current_station = target.station;
    target.train->status = Vehicle::Status::unloadingPassengers;
    target.train->sub_state = 1;
    target.train->num_peeps = 1;
    target.train->next_free_seat = 0;

    target.train->Update();
    EXPECT_EQ(target.train->status, Vehicle::Status::unloadingPassengers);

    target.train->num_peeps = 0;
    target.train->Update();
    EXPECT_EQ(target.train->status, Vehicle::Status::movingToEndOfStation);
}

TEST_F(PlayTests, NaturallyArrivingTrainPreservesStagedSeatsThroughUnloadAndBoarding)
{
    auto context = LoadEverythingPark();
    ASSERT_NE(context, nullptr);
    auto& gameState = getGameState();

    Ride* targetRide = nullptr;
    Vehicle* targetTrain = nullptr;
    int32_t shortestTrack = std::numeric_limits<int32_t>::max();
    for (auto& ride : RideManager(gameState))
    {
        if (!RideSupportsStationPlatformPreQueue(ride) || ride.numTrains == 0 || ride.numStations == 0
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
            || (train->num_seats & kVehicleSeatNumMask) == 0
            || trackLength <= 0 || trackLength >= shortestTrack
            || RideVehicle::StationDetail::BuildTrainSeatSummary(*train).capacity < 4)
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
    OpenPlatformTestRide(*targetRide);
    targetRide->departFlags = 0;
    gameState.park.flags |= PARK_FLAGS_NO_MONEY;
    ClearTrain(gameState, *targetTrain, true);

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

    // Put a real rider on the travelling train so the return trip necessarily exercises unloading before the station is
    // published for boarding. The staged guests below reserve the same complete, empty-train plan in advance.
    auto* alightingGuest = Guest::generate(targetTrain->getLocation());
    ASSERT_NE(alightingGuest, nullptr);
    alightingGuest->CurrentRide = targetRide->id;
    alightingGuest->CurrentRideStation = targetStation;
    alightingGuest->CurrentTrain = 0;
    alightingGuest->CurrentCar = 0;
    alightingGuest->CurrentSeat = 0;
    alightingGuest->State = PeepState::onRide;
    alightingGuest->RideSubState = PeepRideSubState::onRide;
    targetTrain->num_peeps = 1;
    targetTrain->next_free_seat = 1;
    targetTrain->peep[0] = alightingGuest->id;
    targetTrain->peep_tshirt_colours[0] = alightingGuest->TshirtColour;

    constexpr size_t kStagedGuestCount = 4;
    std::array<Guest*, kStagedGuestCount> guests{};
    std::array<RideStationPlatformReservation, kStagedGuestCount> reservations{};
    for (size_t guestIndex = 0; guestIndex < guests.size(); guestIndex++)
    {
        auto* guest = Guest::generate(targetRide->getStation(targetStation).Entrance.ToCoordsXYZ());
        ASSERT_NE(guest, nullptr);
        const auto reservation = RideReserveStationPlatformSlot(*targetRide, targetStation, guest->id);
        ASSERT_TRUE(reservation.has_value());
        guests[guestIndex] = guest;
        reservations[guestIndex] = reservation.value();
        guest->moveTo(reservation->waitPosition);
        guest->SetDestination(reservation->waitPosition, 2);
        StagePlatformGuest(*guest, *targetRide, targetStation, reservation.value());
        guest->DestinationTolerance = 0;
        guest->StepProgress = std::numeric_limits<uint8_t>::max();
    }

    bool sawUnloading = false;
    bool arrivedAndStopped = false;
    std::array<bool, kStagedGuestCount> seatBound{};
    bool departedEmpty = false;
    bool allBoarded = false;
    std::ostringstream trace;
    for (int32_t tick = 0; tick < 12000; tick++)
    {
        sawUnloading = sawUnloading || targetTrain->status == Vehicle::Status::unloadingPassengers;
        gameStateUpdateLogic();
        sawUnloading = sawUnloading || targetTrain->status == Vehicle::Status::unloadingPassengers;
        if (targetTrain->current_station == targetStation
            && targetTrain->status == Vehicle::Status::waitingForPassengers)
        {
            arrivedAndStopped = true;
        }

        allBoarded = true;
        for (size_t guestIndex = 0; guestIndex < guests.size(); guestIndex++)
        {
            const auto* guest = guests[guestIndex];
            const auto& reservation = reservations[guestIndex];
            EXPECT_EQ(guest->CurrentCar, reservation.carIndex);
            EXPECT_EQ(guest->CurrentSeat, reservation.seatIndex);

            if (guest->CurrentTrain == RideStation::kNoTrain)
            {
                const auto currentReservation = RideGetStationPlatformReservation(*targetRide, targetStation, guest->id);
                ASSERT_TRUE(currentReservation.has_value());
                EXPECT_EQ(currentReservation->slotIndex, reservation.slotIndex);
                EXPECT_EQ(currentReservation->carIndex, reservation.carIndex);
                EXPECT_EQ(currentReservation->seatIndex, reservation.seatIndex);
                EXPECT_EQ(currentReservation->waitPosition, reservation.waitPosition);
                const CoordsXY expectedDestination{ reservation.waitPosition.x, reservation.waitPosition.y };
                EXPECT_EQ(guest->GetDestination(), expectedDestination);
            }
            else
            {
                seatBound[guestIndex] = true;
                const auto* car = targetTrain->GetCar(reservation.carIndex);
                ASSERT_NE(car, nullptr);
                EXPECT_EQ(car->peep[reservation.seatIndex], guest->id);
            }
            allBoarded = allBoarded && guest->State == PeepState::onRide;
        }
        if (allBoarded)
            break;
        if (arrivedAndStopped && std::ranges::none_of(seatBound, [](bool value) { return value; })
            && targetTrain->status == Vehicle::Status::travelling)
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
                   << " firstGuestState=" << static_cast<int32_t>(guests.front()->State)
                   << " firstGuestSubState=" << static_cast<int32_t>(guests.front()->RideSubState)
                   << " firstGuestTrain=" << static_cast<int32_t>(guests.front()->CurrentTrain) << '\n';
        }
    }

    EXPECT_TRUE(sawUnloading) << trace.str();
    EXPECT_TRUE(arrivedAndStopped) << trace.str();
    EXPECT_TRUE(std::ranges::all_of(seatBound, [](bool value) { return value; })) << trace.str();
    EXPECT_FALSE(departedEmpty) << trace.str();
    EXPECT_TRUE(allBoarded) << trace.str();
    EXPECT_NE(alightingGuest->State, PeepState::onRide);
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

    auto result = executeImmediate<GameActions::ParkSetEntranceFeeAction>(1.00_GBP);
    ASSERT_EQ(result.error, GameActions::Status::ok);
    EXPECT_EQ(gameState.park.entranceFeeTarget, Park::ParkEntranceFeeTarget::custom);
    EXPECT_EQ(Park::GetEntranceFee(gameState.park), 1.00_GBP);

    result = executeImmediate<GameActions::ParkSetEntranceFeeAction>(Park::ParkEntranceFeeTarget::incomePerGuest);
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

    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::overcrowded), 0);
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
    EXPECT_EQ(maze.getMazeCapacityForMode(MazeCapacityMode::overcrowded), Limits::kCheatsMaxOperatingLimit);
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
