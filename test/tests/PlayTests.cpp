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
#include <openrct2/entity/Peep.h>
#include <openrct2/object/ObjectLimits.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/TrackDesign.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/Park.h>
#include <string>
#include <utility>

using namespace OpenRCT2;

class PlayTests : public testing::Test
{
};

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

TEST_F(PlayTests, ImportedMazeTrackDesignCapacityDefaultsToNormalMode)
{
    TrackDesign mazeDesign{};
    mazeDesign.trackAndVehicle.rtdIndex = RIDE_TYPE_MAZE;
    mazeDesign.operation.operationSetting = 4;

    mazeDesign.mazeElements.resize(37);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::normal));

    mazeDesign.operation.operationSetting = static_cast<uint8_t>(MazeCapacityMode::sparse);
    mazeDesign.mazeElements.resize(37);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::sparse));

    mazeDesign.operation.operationSetting = static_cast<uint8_t>(MazeCapacityMode::overcrowded);
    mazeDesign.mazeElements.resize(37);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::overcrowded));

    mazeDesign.operation.operationSetting = 50;
    mazeDesign.mazeElements.resize(37);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::normal));

    mazeDesign.operation.operationSetting = 4;
    mazeDesign.mazeElements.resize(300);
    mazeDesign.NormaliseMazeOperationSetting();
    EXPECT_EQ(mazeDesign.operation.operationSetting, static_cast<uint8_t>(MazeCapacityMode::normal));
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
