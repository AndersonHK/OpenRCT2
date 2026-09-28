/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/actions/GameActionRunner.h>
#include <openrct2/actions/peep/StaffHireNewAction.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/peep/GuestPathfinding.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPathRouteCache.h>
#include <openrct2/world/MapPathTopology.h>
#include <openrct2/world/MapTopology.h>
#include <openrct2/world/Weather.h>
#include <openrct2/world/tile_element/BannerElement.h>
#include <openrct2/world/tile_element/EntranceElement.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>

using namespace OpenRCT2;

class GuestExactRoutingTest : public testing::Test
{
protected:
    static inline std::unique_ptr<IContext> context;
    Ride* ride{};
    static constexpr TileCoordsXYZ centre{ 10, 10, 14 };
    static constexpr TileCoordsXYZ goal{ 10, 7, 14 };

    static void SetUpTestSuite()
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        context = CreateContext();
        ASSERT_TRUE(context->Initialise());
        ASSERT_TRUE(context->LoadParkFromFile(TestData::GetParkPath("pathfinding-tests.sv6")));
        GameLoadInit();
    }

    static void TearDownTestSuite()
    {
        context.reset();
    }

    void SetUp() override
    {
        MapInit({ 30, 30 });
        ScenarioRandSeed(0x12345678, 0x87654321);
        ride = RideAllocateAtIndex(GetNextFreeRideId());
        ASSERT_NE(ride, nullptr);
        ride->type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
        ride->status = RideStatus::open;
        for (auto& station : ride->getStations())
        {
            station.clearEntrance();
            station.clearExit();
        }
        ride->getStation().setEntrance({ goal, 3 });

        // Two-wide avenue ending at a cross path: the west column is wide, the east column is not.
        // At centre, pruning its wide north neighbour and then the arrival edge creates the Texas loop.
        const auto exists = [](int x, int y) {
            return ((x == 10 || x == 11) && y >= 7 && y <= 11) || (y == 11 && x >= 8 && x <= 13);
        };
        for (int y = 7; y <= 11; ++y)
            for (int x = 8; x <= 13; ++x)
            {
                if (!exists(x, y))
                    continue;
                MapGetSurfaceElementAt(TileCoordsXY{ x, y })->setOwnership({ OwnershipFlag::landOwned });
                uint8_t edges = 0;
                for (Direction d : kAllDirections)
                    if (exists(x + TileDirectionDelta[d].x, y + TileDirectionDelta[d].y))
                        edges |= 1 << d;
                ASSERT_NE(
                    InsertTileElement<PathElement>(
                        { x * 32, y * 32, 14 * 8 }, 0,
                        [&](PathElement& path) {
                            path.setClearanceZ(18 * 8);
                            path.setEdges(edges);
                            path.setSloped(false);
                            path.setIsQueue(false);
                            path.setWide(x == 10 && y < 11);
                            path.setGhost(false);
                        }),
                    nullptr);
            }
        Prepare();
    }

    void TearDown() override
    {
        RideDelete(ride->id);
    }

    void Prepare()
    {
        MapPathRouteCache::Prepare(std::array{ MapPathRouteCache::RouteTarget{ goal, ride->id } });
    }

    void Configure(Guest& guest, Direction incoming, TileCoordsXYZ loc = centre)
    {
        guest.type = EntityType::guest;
        guest.state = PeepState::walking;
        guest.outsideOfPark = false;
        guest.guestHeadingToRideId = ride->id;
        guest.nextLoc = loc.toCoordsXYZ();
        guest.x = guest.nextLoc.x + 16;
        guest.y = guest.nextLoc.y + 16;
        guest.z = guest.nextLoc.z;
        guest.setNextFlags(0, false, false);
        guest.peepDirection = incoming;
        guest.energy = 128;
        guest.pathfindGoal = { goal, 0 };
        guest.transportRoutePlanningInitialised = true;
        guest.transportRoutePlannedInPrecipitation = Weather::isPrecipitating();
        guest.transportRouteCrowdingGeneration = RideGetTransportServiceCrowdingGeneration();
        for (auto& history : guest.pathfindHistory)
            history.setNull();
    }
};

TEST_F(GuestExactRoutingTest, BothLoopArrivalsFollowExactNorthStep)
{
    for (Direction arrival : { 0, 3 })
    {
        Guest guest{};
        Configure(guest, arrival);
        ASSERT_EQ(PathFinding::CalculateNextDestination(guest), 0);
        EXPECT_EQ(guest.peepDirection, 3);
    }
}

TEST_F(GuestExactRoutingTest, CrowdedTwoWideCorridorSplitsGuestsThenFinishesAtSingleWidthMerge)
{
    MapInit({ 30, 30 });
    const auto exists = [](int x, int y) { return (x == 10 && y >= 7 && y <= 20) || (x == 11 && y >= 9 && y <= 20); };
    for (int y = 7; y <= 20; ++y)
        for (int x = 10; x <= 11; ++x)
        {
            if (!exists(x, y))
                continue;
            uint8_t edges = 0;
            for (Direction d : kAllDirections)
                if (exists(x + TileDirectionDelta[d].x, y + TileDirectionDelta[d].y))
                    edges |= 1 << d;
            MapGetSurfaceElementAt(TileCoordsXY{ x, y })->setOwnership({ OwnershipFlag::landOwned });
            ASSERT_NE(
                InsertTileElement<PathElement>(
                    { x * 32, y * 32, 14 * 8 }, 0,
                    [&](PathElement& p) {
                        p.setClearanceZ(18 * 8);
                        p.setEdges(edges);
                    }),
                nullptr);
        }
    Prepare();
    const TileCoordsXYZ start{ 10, 17, 14 };
    std::vector<Guest*> guests;
    for (int i = 0; i < 16; ++i)
    {
        auto* guest = Guest::generate(start.toCoordsXYZ().toTileCentre());
        ASSERT_NE(guest, nullptr);
        Configure(*guest, 3, start);
        guests.push_back(guest);
    }
    unsigned changed = 0;
    getGameState().entities.updateEntitiesSpatialIndex();
    for (auto* guest : guests)
    {
        PathFinding::CalculateNextDestination(*guest);
        if (guest->laneForwardSteps != 0)
        {
            ++changed;
            EXPECT_EQ(guest->peepDirection, 2);
        }
    }
    EXPECT_GT(changed, 0u);
    EXPECT_LT(changed, guests.size());
    for (auto* guest : guests)
    {
        unsigned transitions = 0;
        auto previous = start;
        for (int action = 0; action < 2048 && TileCoordsXYZ{ guest->nextLoc } != goal; ++action)
        {
            guest->performNextAction();
            const TileCoordsXYZ loc{ guest->nextLoc };
            if (loc != previous)
            {
                EXPECT_LE(loc.y, previous.y); // Never turns back or oscillates at the merge.
                ++transitions;
                previous = loc;
            }
        }
        EXPECT_EQ(TileCoordsXYZ{ guest->nextLoc }, goal);
        EXPECT_LE(transitions, 14u);
        PeepEntityRemove(guest);
    }
}

TEST_F(GuestExactRoutingTest, StaffRepairBinsBenchesAndLampsWithRoleSpecificAnimations)
{
    struct Case
    {
        const char* object;
        StaffType type;
        PeepActionType action;
        uint16_t ticks;
    };
    for (const auto& test : { Case{ "rct2.footpath_item.litter1", StaffType::handyman, PeepActionType::staffEmptyBin, 200 },
                              Case{ "rct2.footpath_item.bench1", StaffType::mechanic, PeepActionType::staffFixGround, 320 },
                              Case{ "rct2.footpath_item.lamp1", StaffType::mechanic, PeepActionType::staffFix, 320 } })
    {
        SCOPED_TRACE(test.object);
        auto& objects = context->GetObjectManager();
        auto* addition = objects.LoadObject(test.object);
        ASSERT_NE(addition, nullptr);
        auto* path = MapGetPathElementAt(centre);
        path->setAdditionEntryIndex(objects.GetLoadedObjectEntryIndex(addition));
        path->setIsBroken(true);
        path->setWide(false);
        path->setEdges(0xA); // Furniture is visible on the west/east edges.
        auto action = GameActions::StaffHireNewAction(
            false, test.type, kObjectEntryIndexNull, STAFF_ORDERS_EMPTY_BINS | STAFF_ORDERS_FIX_RIDES);
        const auto hire = GameActions::ExecuteNested(&action, getGameState());
        ASSERT_EQ(hire.error, GameActions::Status::ok);
        auto* worker = getGameState().entities.getEntity<Staff>(
            hire.getData<GameActions::StaffHireNewActionResult>().StaffEntityId);
        ASSERT_NE(worker, nullptr);
        worker->moveTo(centre.toCoordsXYZ().toTileCentre());
        worker->nextLoc = centre.toCoordsXYZ();
        worker->setNextFlags(0, false, false);
        worker->setDestination(worker->getLocation(), 2);
        worker->state = PeepState::patrolling;
        worker->stepProgress = 255;
        PrepareHandymanServiceReservations();
        worker->update();
        ASSERT_EQ(worker->state, PeepState::repairingPathAddition);
        EXPECT_EQ(worker->repairTicksRemaining, test.ticks);
        bool sawAnimation = false;
        unsigned workTicks = 0;
        for (int tick = 0; tick < 1800 && path->isBroken(); ++tick)
        {
            PrepareHandymanServiceReservations();
            if (worker->subState == 1)
                ++workTicks;
            worker->update();
            sawAnimation |= worker->action == test.action;
        }
        EXPECT_TRUE(sawAnimation);
        EXPECT_FALSE(path->isBroken());
        EXPECT_GE(workTicks, test.ticks);
        EXPECT_NE(worker->state, PeepState::repairingPathAddition);
        PeepEntityRemove(worker);
    }
}

TEST_F(GuestExactRoutingTest, FurnitureRepairClaimsAreExclusiveAndRemovedFurnitureCancelsWork)
{
    auto& objects = context->GetObjectManager();
    auto* addition = objects.LoadObject("rct2.footpath_item.bench1");
    ASSERT_NE(addition, nullptr);
    auto* path = MapGetPathElementAt(centre);
    path->setAdditionEntryIndex(objects.GetLoadedObjectEntryIndex(addition));
    path->setIsBroken(true);
    path->setEdges(0xA);
    Staff* workers[2]{};
    for (auto& worker : workers)
    {
        auto hire = GameActions::StaffHireNewAction(false, StaffType::mechanic, kObjectEntryIndexNull, STAFF_ORDERS_FIX_RIDES);
        const auto result = GameActions::ExecuteNested(&hire, getGameState());
        ASSERT_EQ(result.error, GameActions::Status::ok);
        worker = getGameState().entities.getEntity<Staff>(
            result.getData<GameActions::StaffHireNewActionResult>().StaffEntityId);
        ASSERT_NE(worker, nullptr);
        worker->moveTo(centre.toCoordsXYZ().toTileCentre());
        worker->nextLoc = centre.toCoordsXYZ();
        worker->setNextFlags(0, false, false);
        worker->setDestination(worker->getLocation(), 2);
        worker->state = PeepState::patrolling;
        worker->stepProgress = 255;
    }
    PrepareHandymanServiceReservations();
    workers[0]->update();
    workers[1]->update();
    ASSERT_EQ(workers[0]->state, PeepState::repairingPathAddition);
    EXPECT_NE(workers[1]->state, PeepState::repairingPathAddition);
    path->setAddition(0);
    workers[0]->stepProgress = 255;
    workers[0]->update();
    EXPECT_NE(workers[0]->state, PeepState::repairingPathAddition);
    for (auto* worker : workers)
        PeepEntityRemove(worker);
}

TEST_F(GuestExactRoutingTest, CrowdedHorizontalLaneCanTurnOntoSingleWidthPathWithoutLooping)
{
    MapInit({ 30, 30 });
    const auto exists = [](int x, int y) {
        return (x == 10 && y >= 7 && y <= 10) || (x >= 10 && x <= 22 && (y == 9 || y == 10));
    };
    for (int y = 7; y <= 10; ++y)
        for (int x = 10; x <= 22; ++x)
        {
            if (!exists(x, y))
                continue;
            uint8_t edges = 0;
            for (Direction d : kAllDirections)
                if (exists(x + TileDirectionDelta[d].x, y + TileDirectionDelta[d].y))
                    edges |= 1 << d;
            MapGetSurfaceElementAt(TileCoordsXY{ x, y })->setOwnership({ OwnershipFlag::landOwned });
            ASSERT_NE(
                InsertTileElement<PathElement>(
                    { x * 32, y * 32, 14 * 8 }, 0,
                    [&](PathElement& p) {
                        p.setClearanceZ(18 * 8);
                        p.setEdges(edges);
                    }),
                nullptr);
        }
    Prepare();
    const TileCoordsXYZ start{ 19, 9, 14 };
    std::vector<Guest*> guests;
    for (int i = 0; i < 16; ++i)
    {
        auto* guest = Guest::generate(start.toCoordsXYZ().toTileCentre());
        ASSERT_NE(guest, nullptr);
        Configure(*guest, 0, start);
        guests.push_back(guest);
    }
    getGameState().entities.updateEntitiesSpatialIndex();
    unsigned changed = 0;
    for (auto* guest : guests)
    {
        PathFinding::CalculateNextDestination(*guest);
        changed += guest->laneForwardSteps != 0;
    }
    EXPECT_GT(changed, 0u);
    EXPECT_LT(changed, guests.size());
    for (auto* guest : guests)
    {
        unsigned transitions = 0;
        auto previous = start;
        for (int action = 0; action < 2048 && TileCoordsXYZ{ guest->nextLoc } != goal; ++action)
        {
            guest->performNextAction();
            const TileCoordsXYZ loc{ guest->nextLoc };
            if (loc != previous)
            {
                EXPECT_LE(loc.x, previous.x);
                ++transitions;
                previous = loc;
            }
        }
        EXPECT_EQ(TileCoordsXYZ{ guest->nextLoc }, goal);
        EXPECT_LE(transitions, 15u);
        PeepEntityRemove(guest);
    }
}

TEST_F(GuestExactRoutingTest, ExactRouteCanReverseTheArrivalDirection)
{
    Guest guest{};
    Configure(guest, 1);
    ASSERT_EQ(PathFinding::CalculateNextDestination(guest), 0);
    EXPECT_EQ(guest.peepDirection, 3);
}

TEST_F(GuestExactRoutingTest, ActualGuestMovementEscapesTheThreeTileLoop)
{
    for (Direction arrival : { 0, 3 })
    {
        auto* guest = Guest::generate(centre.toCoordsXYZ().toTileCentre());
        ASSERT_NE(guest, nullptr);
        Configure(*guest, arrival);
        PathFinding::CalculateNextDestination(*guest);
        auto previous = centre;
        uint32_t previousDistance = 3;
        for (int action = 0; action < 512 && TileCoordsXYZ{ guest->nextLoc } != goal; ++action)
        {
            guest->performNextAction();
            const TileCoordsXYZ current{ guest->nextLoc };
            if (current != previous)
            {
                const auto distance = MapPathRouteCache::QueryDistanceToTarget({ goal, ride->id }, current);
                EXPECT_TRUE(distance.pathTiles.has_value());
                if (distance.pathTiles)
                {
                    EXPECT_LT(*distance.pathTiles, previousDistance);
                    previousDistance = *distance.pathTiles;
                }
                previous = current;
            }
        }
        EXPECT_EQ(TileCoordsXYZ{ guest->nextLoc }, goal);
        PeepEntityRemove(guest);
    }
}

TEST_F(GuestExactRoutingTest, BannerStillForbidsTheDirectStep)
{
    ASSERT_NE(
        InsertTileElement<BannerElement>(
            { centre.toCoordsXY(), 16 * 8 }, 0,
            [](BannerElement& banner) {
                banner.setClearanceZ(18 * 8);
                banner.setAllowedEdges(0x7); // No north exit.
                banner.setGhost(false);
            }),
        nullptr);
    Prepare();
    Guest guest{};
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_NE(guest.peepDirection, 3);
}

TEST_F(GuestExactRoutingTest, ForeignQueueStillForbidsTheDirectStep)
{
    const TileCoordsXYZ north{ 10, 9, 14 };
    auto* path = MapGetPathElementAt(north);
    path->setIsQueue(true);
    path->setRideIndex(RideId::FromUnderlying(200));
    MapTopology::InvalidateTileAndNeighbours(north);
    Prepare();
    Guest guest{};
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_NE(guest.peepDirection, 3);
}

TEST_F(GuestExactRoutingTest, StaleFieldsRetainTheLegacyShortcutUntilRebuilt)
{
    MapTopology::InvalidateTileAndNeighbours(centre);
    Guest guest{};
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 1);
    Prepare();
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 3);
}

TEST_F(GuestExactRoutingTest, AimlessAndClosedRideGuestsDoNotTakeTheExactFastPath)
{
    Guest guest{};
    Configure(guest, 0);
    guest.guestHeadingToRideId = RideId::GetNull();
    const auto before = guest.pathfindGoal;
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.pathfindGoal, before);
    ride->status = RideStatus::closed;
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 1);
}

TEST_F(GuestExactRoutingTest, PublishedDestinationChangeSupersedesOldGoal)
{
    const TileCoordsXYZ changedGoal{ 13, 11, 14 };
    MapPathRouteCache::Prepare(std::array{ MapPathRouteCache::RouteTarget{ changedGoal, ride->id } });
    Guest guest{};
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(TileCoordsXYZ{ guest.pathfindGoal }, changedGoal);
    const auto expected = MapPathRouteCache::GetNextStep({ changedGoal, ride->id }, centre);
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(guest.peepDirection, expected->direction);
}

TEST_F(GuestExactRoutingTest, PlannedTransportBoardingTakesPriorityOverTheFinalWalkingTarget)
{
    auto cleanup = [](Ride* value) {
        if (value != nullptr)
            RideDelete(value->id);
    };
    std::unique_ptr<Ride, decltype(cleanup)> transport(RideAllocateAtIndex(GetNextFreeRideId()), cleanup);
    ASSERT_NE(transport, nullptr);
    transport->type = RIDE_TYPE_MONORAIL;
    transport->status = RideStatus::open;
    transport->numStations = 2;
    const TileCoordsXYZ boarding{ 11, 10, 14 };
    transport->getStation().setEntrance({ boarding, 0 });
    MapPathRouteCache::Prepare(std::array{ MapPathRouteCache::RouteTarget{ goal, ride->id },
                                           MapPathRouteCache::RouteTarget{ boarding, transport->id } });
    Guest guest{};
    Configure(guest, 0);
    guest.setTransportRoute(transport->id, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1));
    PathFinding::CalculateNextDestination(guest);
    EXPECT_TRUE(guest.hasTransportRoute());
    EXPECT_EQ(guest.peepDirection, 2);
    EXPECT_EQ(TileCoordsXYZ{ guest.pathfindGoal }, boarding);

    // Service changes still invalidate a route before a published walking step is consumed.
    transport->status = RideStatus::closed;
    PathFinding::CalculateNextDestination(guest);
    EXPECT_FALSE(guest.hasTransportRoute());
    EXPECT_EQ(guest.peepDirection, 3);
}

TEST_F(GuestExactRoutingTest, UnavailableTargetKeepsTheBoundedFallback)
{
    const MapPathRouteCache::RouteTarget unavailable{ { 20, 20, 14 }, ride->id };
    MapPathRouteCache::Prepare(std::array{ unavailable });
    ASSERT_FALSE(MapPathRouteCache::GetNextStep(unavailable, centre).has_value());
    Guest guest{};
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 1);
}

TEST_F(GuestExactRoutingTest, MultipleStationsRetainNearestAndSynchronizedSelection)
{
    const TileCoordsXYZ second{ 11, 10, 14 };
    ride->numStations = 2;
    ride->getStation(StationIndex::FromUnderlying(1)).setEntrance({ second, 0 });
    MapPathRouteCache::Prepare(
        std::array{ MapPathRouteCache::RouteTarget{ goal, ride->id }, MapPathRouteCache::RouteTarget{ second, ride->id } });
    Guest guest{};
    Configure(guest, 0);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 2);
    ride->departFlags |= RIDE_DEPART_SYNCHRONISE_WITH_ADJACENT_STATIONS;
    Configure(guest, 0);
    guest.guestNumRides = 0;
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 3);
    Configure(guest, 0);
    guest.guestNumRides = 1;
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 2);
}

TEST_F(GuestExactRoutingTest, ParkExitUsesTheExactRouteBeforeWidePathPreferences)
{
    ASSERT_NE(
        InsertTileElement<EntranceElement>(
            goal.toCoordsXYZ(), 0,
            [](EntranceElement& entrance) {
                entrance.setClearanceZ(18 * 8);
                entrance.setEntranceType(EntranceType::parkEntrance);
                entrance.setSequenceIndex(ParkEntranceSequence::centre);
                entrance.setDirection(3);
                entrance.setGhost(false);
            }),
        nullptr);
    auto& entrances = getGameState().park.entrances;
    const auto originalEntrances = entrances;
    entrances = { CoordsXYZD{ goal.toCoordsXYZ(), 3 } };
    MapPathRouteCache::Prepare(std::array{ MapPathRouteCache::RouteTarget{ goal, RideId::GetNull() } });
    Guest guest{};
    Configure(guest, 0);
    guest.guestHeadingToRideId = RideId::GetNull();
    guest.peepFlags.set(PeepFlag::leavingPark);
    PathFinding::CalculateNextDestination(guest);
    EXPECT_EQ(guest.peepDirection, 3);
    EXPECT_TRUE(guest.peepFlags.has(PeepFlag::parkEntranceChosen));
    EXPECT_EQ(TileCoordsXYZ{ guest.pathfindGoal }, goal);
    entrances = originalEntrances;
}
