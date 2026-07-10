#include "TestData.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/core/String.hpp>
#include <openrct2/entity/Guest.h>
#include <openrct2/peep/GuestPathfinding.h>
#include <openrct2/platform/Platform.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/Wall.h>
#include <openrct2/world/Weather.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <string>

using namespace OpenRCT2;

static std::ostream& operator<<(std::ostream& os, const TileCoordsXYZ& coords)
{
    return os << "(" << coords.x << ", " << coords.y << ", " << coords.z << ")";
}

class PathfindingTestBase : public testing::Test
{
public:
    static void SetUpTestCase()
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        _context = CreateContext();
        const bool initialised = _context->Initialise();
        ASSERT_TRUE(initialised);

        std::string parkPath = TestData::GetParkPath("pathfinding-tests.sv6");
        GetContext()->LoadParkFromFile(parkPath);
        GameLoadInit(); // NB: calls `setActiveScene`
    }

    void SetUp() override
    {
        // Use a consistent random seed in every test
        ScenarioRandSeed(0x12345678, 0x87654321);
    }

    static void TearDownTestCase()
    {
        _context = nullptr;
    }

protected:
    struct SurfaceRejoinCandidate
    {
        CoordsXY loc;
        int32_t baseZ;
        int32_t walkZ;
        Direction pathDirection;
    };

    static Ride* FindRideByName(const char* name)
    {
        auto& gameState = getGameState();
        for (auto& ride : RideManager(gameState))
        {
            auto thisName = ride.getName();
            if (String::startsWith(thisName, u8string{ name }, true))
            {
                return &ride;
            }
        }
        return nullptr;
    }

    static bool TileHasReachablePathAtHeight(const CoordsXY& loc, int32_t walkZ)
    {
        const auto* tileElement = MapGetFirstElementAt(loc);
        if (tileElement == nullptr)
            return false;

        const int32_t baseZ = std::max(0, (walkZ / kCoordsZStep) - 2);
        const int32_t topZ = (walkZ / kCoordsZStep) + 1;

        for (;;)
        {
            if (baseZ <= tileElement->baseHeight && topZ >= tileElement->baseHeight && !tileElement->isGhost()
                && tileElement->getType() == TileElementType::Path)
            {
                return true;
            }

            if (tileElement->isLastForTile())
                return false;

            tileElement++;
        }
    }

    static bool SurfaceStepIsClear(const CoordsXY& loc, int32_t baseZ, Direction direction)
    {
        auto pathPos = CoordsXYRangedZ{ loc, baseZ, baseZ + kPathClearance };
        if (WallInTheWay(pathPos, direction))
            return false;

        auto nextLoc = (loc + CoordsDirectionDelta[direction]).ToTileStart();
        pathPos = CoordsXYRangedZ{ nextLoc, baseZ, baseZ + kPathClearance };
        return !WallInTheWay(pathPos, DirectionReverse(direction));
    }

    static bool FindSurfaceRejoinCandidate(SurfaceRejoinCandidate& result)
    {
        const auto& gameState = getGameState();
        for (int32_t y = 1; y < gameState.mapSize.y - 1; y++)
        {
            for (int32_t x = 1; x < gameState.mapSize.x - 1; x++)
            {
                const auto loc = TileCoordsXY{ x, y }.ToCoordsXY();
                const auto* surfaceElement = MapGetSurfaceElementAt(loc);
                if (surfaceElement == nullptr || !MapIsLocationInPark(loc) || MapSurfaceIsBlocked(loc))
                    continue;

                const int32_t baseZ = surfaceElement->getBaseZ();
                const int32_t walkZ = TileElementHeight(loc.ToTileCentre());
                if (TileHasReachablePathAtHeight(loc, walkZ))
                    continue;

                uint8_t pathDirectionCount = 0;
                Direction pathDirection = kInvalidDirection;
                for (Direction direction : kAllDirections)
                {
                    if (!SurfaceStepIsClear(loc, baseZ, direction))
                        continue;

                    const auto nextLoc = (loc + CoordsDirectionDelta[direction]).ToTileStart();
                    if (TileHasReachablePathAtHeight(nextLoc, walkZ))
                    {
                        pathDirection = direction;
                        pathDirectionCount++;
                    }
                }

                if (pathDirectionCount == 1)
                {
                    result = SurfaceRejoinCandidate{ loc, baseZ, walkZ, pathDirection };
                    return true;
                }
            }
        }

        return false;
    }

    static bool FindPath(TileCoordsXYZ* pos, const TileCoordsXYZ& goal, int expectedSteps, RideId targetRideID)
    {
        // Our start position is in tile coordinates, but we need to give the peep spawn
        // position in actual world coords (32 units per tile X/Y, 8 per Z level).
        // Add 16 so the peep spawns in the centre of the tile.
        auto* peep = Guest::generate(pos->ToCoordsXYZ().ToTileCentre());

        // Peeps that are outside of the park use specialized pathfinding which we don't want to
        // use here
        peep->outsideOfPark = false;

        // An earlier iteration of this code just gave peeps a target position to walk to, but it turns out
        // that with no actual ride to head towards, when a peep reaches a junction they use the 'aimless'
        // pathfinder instead of pursuing their original pathfinding target. So, we always need to give them
        // an actual ride to walk to the entrance of.
        peep->guestHeadingToRideId = targetRideID;

        // Pick the direction the peep should initially move in, given the goal position.
        // This will also store the goal position and initialize pathfinding data for the peep.
        const Direction moveDir = PathFinding::ChooseDirection(*pos, goal, *peep, false, RideId::GetNull());
        if (moveDir == kInvalidDirection)
        {
            // Couldn't determine a direction to move off in
            return false;
        }

        // We have already set up the peep's overall pathfinding goal, but we also have to set their initial
        // 'destination' which is a close position that they will walk towards in a straight line - in this case, one
        // tile away. Stepping the peep will move them towards their destination, and once they reach it, a new
        // destination will be picked, to try and get the peep towards the overall pathfinding goal.
        peep->PeepDirection = moveDir;
        auto destination = CoordsDirectionDelta[moveDir] + peep->getLocation();
        peep->SetDestination(destination, 2);

        // Repeatedly step the peep, until they reach the target position or until the expected number of steps have
        // elapsed. Each step, check that the tile they are standing on is not marked as forbidden in the test data
        // (red neon ground type).
        int step = 0;
        while (*pos != goal && step < expectedSteps)
        {
            peep->PerformNextAction();
            ++step;

            *pos = TileCoordsXYZ(peep->getLocation());

            EXPECT_PRED_FORMAT1(AssertIsNotForbiddenPosition, *pos);

            // Check that the peep is still on a footpath. Use next_z instead of pos->z here because pos->z will change
            // when the peep is halfway up a slope, but next_z will not change until they move to the next tile.
            EXPECT_NE(MapGetFootpathElement({ pos->ToCoordsXY(), peep->NextLoc.z }), nullptr);
        }

        // Clean up the peep, because we're reusing this loaded context for all tests.
        PeepEntityRemove(peep);

        // Require that the number of steps taken is exactly what we expected. The pathfinder is supposed to be
        // deterministic, and we reset the RNG seed for each test, everything should be entirely repeatable; as
        // such a change in the number of steps taken on one of these paths needs to be reviewed. For the negative
        // tests, we will not have reached the goal but we still expect the loop to have run for the total number
        // of steps requested before giving up.
        EXPECT_EQ(step, expectedSteps);

        return *pos == goal;
    }

    static testing::AssertionResult AssertIsStartPosition(const char*, const TileCoordsXYZ& location)
    {
        const uint32_t expectedSurfaceStyle = 11u;
        const uint32_t style = MapGetSurfaceElementAt(location.ToCoordsXYZ())->GetSurfaceObjectIndex();

        if (style != expectedSurfaceStyle)
            return testing::AssertionFailure()
                << "Start location " << location << " should have surface style " << expectedSurfaceStyle
                << " but actually has style " << style
                << ". Either the test map is not set up correctly, or you got the coordinates wrong.";

        return testing::AssertionSuccess();
    }

    static testing::AssertionResult AssertIsNotForbiddenPosition(const char*, const TileCoordsXYZ& location)
    {
        const uint32_t forbiddenSurfaceStyle = 8u;

        const uint32_t style = MapGetSurfaceElementAt(location.ToCoordsXYZ())->GetSurfaceObjectIndex();

        if (style == forbiddenSurfaceStyle)
            return testing::AssertionFailure()
                << "Path traversed location " << location << ", but it is marked as a forbidden location (surface style "
                << forbiddenSurfaceStyle << "). Either the map is set up incorrectly, or the pathfinder went the wrong way.";

        return testing::AssertionSuccess();
    }

private:
    static std::shared_ptr<IContext> _context;
};

std::shared_ptr<IContext> PathfindingTestBase::_context;

struct SimplePathfindingScenario
{
    const char* name;
    TileCoordsXYZ start;
    uint32_t steps;

    SimplePathfindingScenario(const char* _name, const TileCoordsXYZ& _start, int _steps)
        : name(_name)
        , start(_start)
        , steps(_steps)
    {
    }

    static std::string ToName(const testing::TestParamInfo<SimplePathfindingScenario>& param_info)
    {
        return param_info.param.name;
    }
};

class SimplePathfindingTest : public PathfindingTestBase, public testing::WithParamInterface<SimplePathfindingScenario>
{
};

TEST_P(SimplePathfindingTest, CanFindPathFromStartToGoal)
{
    const SimplePathfindingScenario& scenario = GetParam();

    ASSERT_PRED_FORMAT1(AssertIsStartPosition, scenario.start);
    TileCoordsXYZ pos = scenario.start;

    auto ride = FindRideByName(scenario.name);
    ASSERT_NE(ride, nullptr);

    auto entrancePos = ride->getStation().Entrance;
    TileCoordsXYZ goal = TileCoordsXYZ(
        entrancePos.x - TileDirectionDelta[entrancePos.direction].x,
        entrancePos.y - TileDirectionDelta[entrancePos.direction].y, entrancePos.z);

    const auto succeeded = FindPath(&pos, goal, scenario.steps, ride->id) ? testing::AssertionSuccess()
                                                                          : testing::AssertionFailure()
            << "Failed to find path from " << scenario.start << " to " << goal << " in " << scenario.steps << " steps; reached "
            << pos << " before giving up.";

    EXPECT_TRUE(succeeded);
}

TEST_F(PathfindingTestBase, SurfaceGuestsStepTowardAdjacentPath)
{
    SurfaceRejoinCandidate candidate{};
    ASSERT_TRUE(FindSurfaceRejoinCandidate(candidate));

    auto* peep = Guest::generate({ candidate.loc.ToTileCentre(), candidate.walkZ });
    ASSERT_NE(peep, nullptr);

    peep->outsideOfPark = false;
    peep->SetState(PeepState::walking);
    peep->NextLoc = { candidate.loc, candidate.baseZ };
    peep->SetNextFlags(0, false, true);
    peep->SetDestination(candidate.loc.ToTileCentre(), 2);

    EXPECT_EQ(PathFinding::CalculateNextDestination(*peep), 0);
    EXPECT_EQ(peep->PeepDirection, candidate.pathDirection);

    PeepEntityRemove(peep);
}

TEST_F(PathfindingTestBase, TransportCandidateRadiusConservativelyCoversWalkingTime)
{
    EXPECT_EQ(PathFinding::CalculateTransportCandidateRadiusTiles(1341, 0), 1);
    EXPECT_EQ(PathFinding::CalculateTransportCandidateRadiusTiles(1341, 60'000), 21);
    EXPECT_EQ(PathFinding::CalculateTransportCandidateRadiusTiles(670, 60'000), 11);
    EXPECT_EQ(
        PathFinding::CalculateTransportCandidateRadiusTiles(
            std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max()),
        kMaximumMapSizeTechnical);
}

TEST_F(PathfindingTestBase, ReasonableMonorailIsChosenOverLongWalk)
{
    const auto rideId = GetNextFreeRideId();
    ASSERT_FALSE(rideId.IsNull());
    auto* monorail = RideAllocateAtIndex(rideId);
    ASSERT_NE(monorail, nullptr);
    monorail->type = RIDE_TYPE_MONORAIL;
    monorail->status = RideStatus::open;
    monorail->numStations = 3;
    monorail->stableStats.valid = true;
    monorail->stableStats.averageSpeed = 22 * 29127;
    monorail->stableStats.maxSpeed = monorail->stableStats.averageSpeed;
    monorail->stableStats.stations[0].SegmentLength = 600 << 16;
    monorail->stableStats.stations[1].SegmentLength = 600 << 16;
    for (auto& station : monorail->getStations())
    {
        station.Start.SetNull();
        station.Entrance.SetNull();
        station.Exit.SetNull();
    }
    monorail->getStation(StationIndex::FromUnderlying(0)).Entrance = { 1, 0, 0, 0 };
    monorail->getStation(StationIndex::FromUnderlying(2)).Exit = { 299, 0, 0, 0 };
    monorail->priceTarget = RidePriceTarget::neutral;

    auto& gameState = getGameState();
    const auto originalParkFlags = gameState.park.flags;
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    Guest guest{};
    guest.NextLoc = { 0, 0, 0 };
    guest.Energy = 96;
    guest.outsideOfPark = false;
    guest.guestHeadingToRideId = RideId::GetNull();
    guest.previousRide = RideId::GetNull();
    guest.cashInPocket = 100.00_GBP;
    guest.clearTransportRoute();

    EXPECT_TRUE(PathFinding::PlanTransportRoute(guest, { 300, 0, 0 }));
    EXPECT_EQ(guest.previousRide, monorail->id);
    EXPECT_EQ(guest.CurrentRideStation, StationIndex::FromUnderlying(0));
    EXPECT_EQ(guest.transportDestinationStation, StationIndex::FromUnderlying(2));

    guest.clearTransportRoute();
    guest.previousRide = RideId::GetNull();
    const auto journey = RideGetTransportJourney(*monorail, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(2));
    guest.cashInPocket = RideGetTransportFare(*monorail, journey) - 0.01_GBP;
    EXPECT_FALSE(PathFinding::PlanTransportRoute(guest, { 300, 0, 0 }));

    guest.giveItem(ShopItem::voucher);
    guest.voucherType = VOUCHER_TYPE_RIDE_FREE;
    guest.voucherRideId = monorail->id;
    EXPECT_TRUE(PathFinding::PlanTransportRoute(guest, { 300, 0, 0 }));

    guest.clearTransportRoute();
    gameState.park.flags = originalParkFlags;
    RideDelete(rideId);
}

TEST_F(PathfindingTestBase, RainRelaxesTheTransportTimeSavingThreshold)
{
    const auto rideId = GetNextFreeRideId();
    ASSERT_FALSE(rideId.IsNull());
    auto* monorail = RideAllocateAtIndex(rideId);
    ASSERT_NE(monorail, nullptr);
    monorail->type = RIDE_TYPE_MONORAIL;
    monorail->status = RideStatus::open;
    monorail->numStations = 2;
    monorail->stableStats.valid = true;
    monorail->stableStats.averageSpeed = 18 * 29127;
    monorail->stableStats.maxSpeed = monorail->stableStats.averageSpeed;
    monorail->stableStats.stations[0].SegmentLength = 1950 << 16;
    for (auto& station : monorail->getStations())
    {
        station.Start.SetNull();
        station.Entrance.SetNull();
        station.Exit.SetNull();
    }
    monorail->getStation(StationIndex::FromUnderlying(0)).Entrance = { 1, 0, 0, 0 };
    monorail->getStation(StationIndex::FromUnderlying(1)).Exit = { 99, 0, 0, 0 };
    monorail->price[0] = 0.00_GBP;

    auto& gameState = getGameState();
    const auto originalParkFlags = gameState.park.flags;
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;
    monorail->priceTarget = RidePriceTarget::neutral;
    const auto originalWeatherCurrent = gameState.weatherCurrent;
    const auto originalWeatherNext = gameState.weatherNext;
    const auto originalWeatherUpdateTimer = gameState.weatherUpdateTimer;

    Guest guest{};
    guest.NextLoc = { 0, 0, 0 };
    guest.Energy = 96;
    guest.cashInPocket = 100.00_GBP;
    guest.outsideOfPark = false;
    guest.guestHeadingToRideId = RideId::GetNull();
    guest.previousRide = RideId::GetNull();
    guest.clearTransportRoute();

    Weather::forceWeather(Weather::Type::Sunny);
    EXPECT_FALSE(PathFinding::PlanTransportRoute(guest, { 100, 0, 0 }));

    Weather::forceWeather(Weather::Type::Rain);
    EXPECT_TRUE(PathFinding::PlanTransportRoute(guest, { 100, 0, 0 }));

    gameState.weatherCurrent = originalWeatherCurrent;
    gameState.weatherNext = originalWeatherNext;
    gameState.weatherUpdateTimer = originalWeatherUpdateTimer;
    gameState.park.flags = originalParkFlags;
    RideDelete(rideId);
}

TEST_F(PathfindingTestBase, FreeTransportMayWinAReasonableTimeTie)
{
    const auto rideId = GetNextFreeRideId();
    ASSERT_FALSE(rideId.IsNull());
    auto* monorail = RideAllocateAtIndex(rideId);
    ASSERT_NE(monorail, nullptr);
    monorail->type = RIDE_TYPE_MONORAIL;
    monorail->status = RideStatus::open;
    monorail->numStations = 2;
    monorail->stableStats.valid = true;
    monorail->stableStats.averageSpeed = 18 * 29127;
    monorail->stableStats.maxSpeed = monorail->stableStats.averageSpeed;
    monorail->stableStats.stations[0].SegmentLength = 2200 << 16;
    monorail->stableStats.stations[0].SegmentTime = 305;
    for (auto& station : monorail->getStations())
    {
        station.Start.SetNull();
        station.Entrance.SetNull();
        station.Exit.SetNull();
    }
    monorail->getStation(StationIndex::FromUnderlying(0)).Entrance = { 1, 0, 0, 0 };
    monorail->getStation(StationIndex::FromUnderlying(1)).Exit = { 99, 0, 0, 0 };

    auto& gameState = getGameState();
    const auto originalParkFlags = gameState.park.flags;
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    Guest guest{};
    guest.NextLoc = { 0, 0, 0 };
    guest.Energy = 96;
    guest.outsideOfPark = false;
    guest.guestHeadingToRideId = RideId::GetNull();
    guest.previousRide = RideId::GetNull();
    guest.cashInPocket = 100.00_GBP;

    monorail->priceTarget = RidePriceTarget::free;
    EXPECT_TRUE(PathFinding::PlanTransportRoute(guest, { 100, 0, 0 }));

    guest.clearTransportRoute();
    guest.previousRide = RideId::GetNull();
    monorail->priceTarget = RidePriceTarget::goodValue;
    EXPECT_FALSE(PathFinding::PlanTransportRoute(guest, { 100, 0, 0 }));

    gameState.park.flags = originalParkFlags;
    RideDelete(rideId);
}

TEST_F(PathfindingTestBase, ExtortiveTransportRequiresNoWalkingOrNonExtortiveAlternative)
{
    const auto extortiveRideId = GetNextFreeRideId();
    ASSERT_FALSE(extortiveRideId.IsNull());
    auto* extortive = RideAllocateAtIndex(extortiveRideId);
    ASSERT_NE(extortive, nullptr);
    extortive->type = RIDE_TYPE_MONORAIL;
    extortive->status = RideStatus::open;
    extortive->numStations = 2;
    extortive->priceTarget = RidePriceTarget::badValue;
    extortive->stableStats.valid = true;
    extortive->stableStats.averageSpeed = 22 * 29127;
    extortive->stableStats.maxSpeed = extortive->stableStats.averageSpeed;
    extortive->stableStats.stations[0].SegmentLength = 1200 << 16;
    extortive->getStation(StationIndex::FromUnderlying(0)).Entrance = { 1, 0, 0, 0 };
    extortive->getStation(StationIndex::FromUnderlying(1)).Exit = { 299, 0, 0, 0 };

    auto& gameState = getGameState();
    const auto originalParkFlags = gameState.park.flags;
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    Guest guest{};
    guest.NextLoc = { 0, 0, 0 };
    guest.Energy = 96;
    guest.outsideOfPark = false;
    guest.guestHeadingToRideId = RideId::GetNull();
    guest.previousRide = RideId::GetNull();
    guest.cashInPocket = 100.00_GBP;

    EXPECT_FALSE(PathFinding::PlanTransportRoute(guest, { 300, 0, 0 }, true));
    EXPECT_TRUE(PathFinding::PlanTransportRoute(guest, { 300, 0, 0 }, false));
    EXPECT_EQ(guest.previousRide, extortiveRideId);
    EXPECT_TRUE(guest.transportRouteWasExtortive);

    guest.clearTransportRoute();
    guest.previousRide = RideId::GetNull();
    const auto discountRideId = GetNextFreeRideId();
    ASSERT_FALSE(discountRideId.IsNull());
    auto* discount = RideAllocateAtIndex(discountRideId);
    ASSERT_NE(discount, nullptr);
    discount->type = RIDE_TYPE_MONORAIL;
    discount->status = RideStatus::open;
    discount->numStations = 2;
    discount->priceTarget = RidePriceTarget::goodValue;
    discount->stableStats.valid = true;
    discount->stableStats.averageSpeed = extortive->stableStats.averageSpeed;
    discount->stableStats.maxSpeed = extortive->stableStats.maxSpeed;
    discount->stableStats.stations[0].SegmentLength = extortive->stableStats.stations[0].SegmentLength;
    discount->getStation(StationIndex::FromUnderlying(0)).Entrance = { 1, 0, 0, 0 };
    discount->getStation(StationIndex::FromUnderlying(1)).Exit = { 299, 0, 0, 0 };

    EXPECT_TRUE(PathFinding::PlanTransportRoute(guest, { 300, 0, 0 }, false));
    EXPECT_EQ(guest.previousRide, discountRideId);
    EXPECT_FALSE(guest.transportRouteWasExtortive);

    guest.clearTransportRoute();
    gameState.park.flags = originalParkFlags;
    RideDelete(discountRideId);
    RideDelete(extortiveRideId);
}

TEST_F(PathfindingTestBase, DisconnectedTransportSearchRetainsFullSpatialFallback)
{
    const auto rideId = GetNextFreeRideId();
    ASSERT_FALSE(rideId.IsNull());
    auto* monorail = RideAllocateAtIndex(rideId);
    ASSERT_NE(monorail, nullptr);
    monorail->type = RIDE_TYPE_MONORAIL;
    monorail->status = RideStatus::open;
    monorail->numStations = 2;
    monorail->priceTarget = RidePriceTarget::badValue;
    monorail->stableStats.valid = true;
    monorail->stableStats.averageSpeed = 18 * 29127;
    monorail->stableStats.maxSpeed = monorail->stableStats.averageSpeed;
    monorail->stableStats.stations[0].SegmentLength = 400 << 16;
    for (auto& station : monorail->getStations())
    {
        station.Start.SetNull();
        station.Entrance.SetNull();
        station.Exit.SetNull();
    }
    monorail->getStation(StationIndex::FromUnderlying(0)).Entrance = { 200, 200, 0, 0 };
    monorail->getStation(StationIndex::FromUnderlying(1)).Exit = { 100, 200, 0, 0 };

    auto& gameState = getGameState();
    const auto originalParkFlags = gameState.park.flags;
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    Guest guest{};
    guest.NextLoc = { 0, 0, 0 };
    guest.Energy = 96;
    guest.cashInPocket = 100.00_GBP;
    guest.outsideOfPark = false;
    guest.guestHeadingToRideId = RideId::GetNull();
    guest.previousRide = RideId::GetNull();

    EXPECT_TRUE(PathFinding::PlanTransportRoute(guest, { 300, 0, 0 }, false));
    EXPECT_EQ(guest.previousRide, rideId);
    EXPECT_TRUE(guest.transportRouteWasExtortive);

    gameState.park.flags = originalParkFlags;
    RideDelete(rideId);
}

TEST_F(PathfindingTestBase, TransportIsBoardedOnlyAsAPlannedRouteLeg)
{
    Ride monorail{};
    monorail.id = RideId::FromUnderlying(100);
    monorail.type = RIDE_TYPE_MONORAIL;
    monorail.status = RideStatus::open;
    monorail.numStations = 2;
    monorail.stableStats.valid = true;
    monorail.stableStats.averageSpeed = 18 * 29127;
    monorail.stableStats.maxSpeed = monorail.stableStats.averageSpeed;
    monorail.stableStats.stations[0].SegmentLength = 400 << 16;

    auto& gameState = getGameState();
    const auto originalParkFlags = gameState.park.flags;
    gameState.park.flags &= ~PARK_FLAGS_NO_MONEY;
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;
    monorail.price[0] = RideGetTransportSegment(monorail, StationIndex::FromUnderlying(0)).fareValue;

    Guest guest{};
    guest.cashInPocket = 100.00_GBP;
    guest.happiness = 200;
    guest.guestHeadingToRideId = RideId::GetNull();
    guest.clearTransportRoute();

    EXPECT_FALSE(guest.shouldGoOnRide(monorail, StationIndex::FromUnderlying(0), false, true));

    guest.setTransportRoute(monorail.id, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1));
    monorail.priceTarget = RidePriceTarget::badValue;
    EXPECT_FALSE(guest.shouldGoOnRide(monorail, StationIndex::FromUnderlying(0), false, true));
    EXPECT_FALSE(guest.hasTransportRoute());

    monorail.priceTarget = RidePriceTarget::neutral;
    guest.setTransportRoute(monorail.id, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1));
    guest.PeepFlags |= PEEP_FLAGS_LEAVING_PARK;
    EXPECT_TRUE(guest.shouldGoOnRide(monorail, StationIndex::FromUnderlying(0), false, true));

    const auto happinessBefore = guest.happiness;
    guest.onEnterRide(monorail);
    EXPECT_EQ(guest.guestNumRides, 0);
    EXPECT_EQ(guest.happiness, happinessBefore);

    guest.onExitRide(monorail);
    EXPECT_FALSE(guest.hasTransportRoute());

    gameState.park.flags = originalParkFlags;
}

TEST_F(PathfindingTestBase, PayingExtortiveTransportReducesHappinessAndCreatesThought)
{
    Guest guest{};
    for (auto& thought : guest.thoughts)
    {
        thought.type = PeepThoughtType::none;
    }
    guest.happiness = 200;
    guest.happinessTarget = 200;

    GuestApplyPaidExtortiveTransportPenalty(guest, RideId::FromUnderlying(100));

    EXPECT_EQ(guest.happiness, 176);
    EXPECT_EQ(guest.happinessTarget, 176);
    EXPECT_TRUE(std::any_of(guest.thoughts.begin(), guest.thoughts.end(), [](const PeepThought& thought) {
        return thought.type == PeepThoughtType::extortiveTransport;
    }));
}

TEST_F(PathfindingTestBase, PlannedTransportRouteIsIndependentOfRideInteractionState)
{
    Guest guest{};
    const auto transportRide = RideId::FromUnderlying(10);
    guest.setTransportRoute(transportRide, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1));

    guest.InteractionRideIndex = RideId::FromUnderlying(12);

    EXPECT_TRUE(guest.hasTransportRoute());
    EXPECT_EQ(guest.previousRide, transportRide);
    EXPECT_EQ(guest.CurrentRideStation, StationIndex::FromUnderlying(0));
}

TEST_F(PathfindingTestBase, ChangingConcreteTargetInvalidatesPlannedTransportLeg)
{
    Guest guest{};
    const auto transportRide = RideId::FromUnderlying(10);
    guest.setTransportRoute(transportRide, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1));

    const auto firstAid = RideId::FromUnderlying(11);
    guest.setPathfindingTargetRide(firstAid);

    EXPECT_EQ(guest.guestHeadingToRideId, firstAid);
    EXPECT_FALSE(guest.hasTransportRoute());
    EXPECT_EQ(guest.previousRide, transportRide);
    EXPECT_EQ(guest.previousRideTimeOut, 0);
    EXPECT_FALSE(DirectionValid(guest.PathfindGoal.direction));
}

INSTANTIATE_TEST_SUITE_P(
    ForScenario, SimplePathfindingTest,
    ::testing::Values(
        SimplePathfindingScenario("StraightFlat", { 19, 15, 14 }, 24), SimplePathfindingScenario("SBend", { 15, 12, 14 }, 87),
        SimplePathfindingScenario("UBend", { 17, 9, 14 }, 87), SimplePathfindingScenario("CBend", { 14, 5, 14 }, 164),
        SimplePathfindingScenario("TwoEqualRoutes", { 9, 13, 14 }, 89),
        SimplePathfindingScenario("TwoUnequalRoutes", { 3, 13, 14 }, 89),
        SimplePathfindingScenario("StraightUpBridge", { 12, 15, 14 }, 24),
        SimplePathfindingScenario("StraightUpSlope", { 14, 15, 14 }, 24),
        SimplePathfindingScenario("SelfCrossingPath", { 6, 5, 14 }, 211)),
    SimplePathfindingScenario::ToName);

class ImpossiblePathfindingTest : public PathfindingTestBase, public testing::WithParamInterface<SimplePathfindingScenario>
{
};

TEST_P(ImpossiblePathfindingTest, CannotFindPathFromStartToGoal)
{
    const SimplePathfindingScenario& scenario = GetParam();
    TileCoordsXYZ pos = scenario.start;
    ASSERT_PRED_FORMAT1(AssertIsStartPosition, scenario.start);

    auto ride = FindRideByName(scenario.name);
    ASSERT_NE(ride, nullptr);

    auto entrancePos = ride->getStation().Entrance;
    TileCoordsXYZ goal = TileCoordsXYZ(
        entrancePos.x + TileDirectionDelta[entrancePos.direction].x,
        entrancePos.y + TileDirectionDelta[entrancePos.direction].y, entrancePos.z);

    EXPECT_FALSE(FindPath(&pos, goal, 10000, ride->id));
}

INSTANTIATE_TEST_SUITE_P(
    ForScenario, ImpossiblePathfindingTest,
    ::testing::Values(
        SimplePathfindingScenario("PathWithGap", { 1, 6, 14 }, 10000),
        SimplePathfindingScenario("PathWithFences", { 11, 6, 14 }, 10000),
        SimplePathfindingScenario("PathWithCliff", { 7, 17, 14 }, 10000)),
    SimplePathfindingScenario::ToName);
