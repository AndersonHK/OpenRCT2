/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Opt-in investigation: requires a locally owned park, never bundled with the tests.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/entity/EntityList.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/peep/GuestPathfinding.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPathRouteCache.h>
#include <openrct2/world/MapPathTopology.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <string_view>
#include <vector>

using namespace OpenRCT2;

TEST(TexasPathfindingInvestigation, BenchmarkLocalPark)
{
    const auto* parkFile = std::getenv("OPENRCT2_INVESTIGATION_PARK");
    const auto* benchmarkTicks = std::getenv("OPENRCT2_INVESTIGATION_BENCHMARK_TICKS");
    if (parkFile == nullptr || *parkFile == '\0' || benchmarkTicks == nullptr || *benchmarkTicks == '\0')
        GTEST_SKIP() << "Opt-in fixed-tick benchmark with isolated user data.";
    char* end = nullptr;
    const long ticks = std::strtol(benchmarkTicks, &end, 10);
    ASSERT_TRUE(end != benchmarkTicks && *end == '\0');
    ASSERT_GT(ticks, 0);
    ASSERT_LE(ticks, 1000000);
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    ASSERT_TRUE(context->LoadParkFromFile(parkFile));
    GameLoadInit();
    ScenarioRandSeed(0x12345678, 0x87654321);
    for (int t = 0; t < 2000; ++t)
        gameStateUpdateLogic(false);
    const auto initial = CaptureBenchmarkStateSnapshot();
    const auto start = std::chrono::steady_clock::now();
    for (long t = 0; t < ticks; ++t)
        gameStateUpdateLogic(false);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto final = CaptureBenchmarkStateSnapshot();
    std::cout << "BENCHMARK ticks=" << ticks << " seconds=" << seconds << " us_per_tick=" << seconds * 1000000 / ticks
              << " guests=" << initial.guestsInsidePark << " final_guests=" << final.guestsInsidePark
              << " route_nodes=" << final.routeNodes << " route_directions=" << final.routeDirectionEntries
              << " route_distances=" << final.routeDistanceEntries
              << " checksum=" << getGameState().entities.getAllEntitiesChecksum().toString() << '\n';
}

TEST(TexasPathfindingInvestigation, InspectAndRunLocalPark)
{
    const auto* parkFile = std::getenv("OPENRCT2_INVESTIGATION_PARK");
    const auto* output = std::getenv("OPENRCT2_INVESTIGATION_OUTPUT");
    if (parkFile == nullptr || *parkFile == '\0' || output == nullptr || *output == '\0')
        GTEST_SKIP() << "Set OPENRCT2_INVESTIGATION_PARK and OPENRCT2_INVESTIGATION_OUTPUT (filename prefix).";
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    ASSERT_TRUE(context->LoadParkFromFile(parkFile));
    GameLoadInit();
    ScenarioRandSeed(0x12345678, 0x87654321);
    PathFinding::PrepareSharedRouteFields();
    auto& state = getGameState();
    std::cout << "PARK " << state.park.name << " ticks=" << state.currentTicks << '\n';
    RideId texas = RideId::GetNull();
    for (auto& ride : RideManager(state))
    {
        std::cout << "RIDE " << ride.id.ToUnderlying() << ' ' << ride.getName();
        for (const auto& station : ride.getStations())
        {
            const auto e = station.getEntrance();
            if (!e.isNull())
                std::cout << " entrance=" << e.x << ',' << e.y << ',' << e.z;
        }
        std::cout << '\n';
        if (ride.getName() == "Texas Giant")
            texas = ride.id;
    }
    ASSERT_FALSE(texas.IsNull());
    const auto target = MapPathRouteCache::GetSingleTargetForRide(texas);
    ASSERT_TRUE(target.has_value());
    std::cout << "TARGET " << target->location.x << ',' << target->location.y << ',' << target->location.z << '\n';
    const auto inTrap = [](const TileCoordsXYZ& loc) {
        return loc == TileCoordsXYZ{ 55, 141, 14 } || loc == TileCoordsXYZ{ 55, 142, 14 }
        || loc == TileCoordsXYZ{ 56, 141, 14 };
    };
    struct TrackedGuest
    {
        EntityId id;
        std::string name;
        bool escaped{};
        bool reachedTarget{};
        bool changedTarget{};
    };
    std::vector<TrackedGuest> tracked;
    for (const auto* guest : EntityList<Guest>())
        if (guest->guestHeadingToRideId == texas && inTrap(TileCoordsXYZ{ guest->nextLoc }))
            tracked.push_back({ guest->id, guest->getName() });
    std::ofstream trackedTrace(std::string(output) + "-tracked.csv");
    ASSERT_TRUE(trackedTrace.good());
    trackedTrace << "tick,id,x,y,z,state,heading,current_ride\n";
    std::ofstream paths(std::string(output) + "-paths.csv");
    ASSERT_TRUE(paths.good());
    paths << "x,y,z,edges,permitted,wide,queue,slope,exact,distance,route,conn0,conn1,conn2,conn3\n";
    for (int y = 0; y < state.mapSize.y; ++y)
        for (int x = 0; x < state.mapSize.x; ++x)
        {
            const auto view = MapPathTopology::GetChunk({ x, y });
            auto* element = MapGetFirstElementAt(TileCoordsXY{ x, y });
            if (element == nullptr)
                continue;
            do
            {
                if (element->getType() != TileElementType::path || element->isGhost())
                    continue;
                const auto* path = element->asPath();
                const TileCoordsXYZ loc{ x, y, path->baseHeight };
                const auto distance = MapPathRouteCache::QueryDistanceToTarget(*target, loc);
                const auto step = MapPathRouteCache::GetNextStep(*target, loc);
                const auto* node = MapPathTopology::FindPath(view, loc);
                paths << x << ',' << y << ',' << loc.z << ',' << int(path->getEdges()) << ','
                      << (node ? int(node->permittedEdges) : -1) << ',' << path->isWide() << ','
                      << (path->isQueue() ? int(path->getRideIndex().ToUnderlying()) : -1) << ','
                      << (path->isSloped() ? int(path->getSlopeDirection()) : -1) << ',' << distance.isExact << ','
                      << (distance.pathTiles ? int(*distance.pathTiles) : -1) << ',' << (step ? int(step->direction) : -1);
                for (int d = 0; d < 4; ++d)
                    paths << ',' << (node && node->connections[d].IsConnected() ? int(node->connections[d].targetBaseZ) : -1);
                paths << '\n';
            } while (!(element++)->isLastForTile());
        }
    paths.close();
    for (const TileCoordsXYZ loc : { TileCoordsXYZ{ 55, 141, 14 }, TileCoordsXYZ{ 55, 142, 14 }, TileCoordsXYZ{ 56, 141, 14 } })
        for (Direction incoming = 0; incoming < 4; ++incoming)
        {
            Guest guest{};
            guest.type = EntityType::guest;
            guest.state = PeepState::walking;
            guest.outsideOfPark = false;
            guest.guestHeadingToRideId = texas;
            guest.nextLoc = loc.toCoordsXYZ();
            guest.x = guest.nextLoc.x + 16;
            guest.y = guest.nextLoc.y + 16;
            guest.z = guest.nextLoc.z;
            guest.setNextFlags(0, false, false);
            guest.peepDirection = incoming;
            guest.energy = 128;
            guest.transportRoutePlanningInitialised = true;
            guest.pathfindGoal = { target->location, 0 };
            for (auto& history : guest.pathfindHistory)
                history.setNull();
            auto solverGuest = guest;
            const auto solver = PathFinding::ChooseDirection(loc, target->location, solverGuest, true, texas);
            PathFinding::CalculateNextDestination(guest);
            std::cout << "DECISION " << loc.x << ',' << loc.y << ',' << loc.z << " incoming=" << int(incoming)
                      << " solver=" << int(solver) << " movement=" << int(guest.peepDirection) << '\n';
        }
    ScenarioRandSeed(0x12345678, 0x87654321);
    std::ofstream guests(std::string(output) + "-guests.csv");
    ASSERT_TRUE(guests.good());
    guests << "tick,id,name,x,y,z,direction,state,heading,goalx,goaly,goalz,transport,distance\n";
    const auto* ticksEnv = std::getenv("OPENRCT2_INVESTIGATION_TICKS");
    char* ticksEnd = nullptr;
    const long ticks = ticksEnv ? std::strtol(ticksEnv, &ticksEnd, 10) : 4096;
    ASSERT_TRUE(ticksEnv == nullptr || (ticksEnd != ticksEnv && *ticksEnd == '\0'));
    ASSERT_GE(ticks, 0);
    ASSERT_LE(ticks, 1000000);
    for (int t = 0; t <= ticks; ++t)
    {
        if (t % 16 == 0)
        {
            for (auto& entry : tracked)
            {
                const auto* guest = state.entities.tryGetEntity<Guest>(entry.id);
                if (guest == nullptr)
                    continue;
                const TileCoordsXYZ loc{ guest->nextLoc };
                entry.escaped |= !inTrap(loc);
                entry.reachedTarget |= loc == target->location
                    || (guest->currentRide == texas
                        && (guest->state == PeepState::queuing || guest->state == PeepState::onRide));
                entry.changedTarget |= guest->guestHeadingToRideId != texas;
                trackedTrace << t << ',' << entry.id.ToUnderlying() << ',' << loc.x << ',' << loc.y << ',' << loc.z << ','
                             << int(guest->state) << ',' << guest->guestHeadingToRideId.ToUnderlying() << ','
                             << guest->currentRide.ToUnderlying() << '\n';
            }
            for (auto* guest : EntityList<Guest>())
            {
                if (guest->guestHeadingToRideId != texas)
                    continue;
                const TileCoordsXYZ loc{ guest->nextLoc };
                const auto distance = MapPathRouteCache::QueryDistanceToTarget(*target, loc);
                guests << t << ',' << guest->id.ToUnderlying() << ',' << guest->getName() << ',' << loc.x << ',' << loc.y << ','
                       << loc.z << ',' << int(guest->peepDirection) << ',' << int(guest->state) << ','
                       << guest->guestHeadingToRideId.ToUnderlying() << ',' << guest->pathfindGoal.x << ','
                       << guest->pathfindGoal.y << ',' << guest->pathfindGoal.z << ',' << guest->hasTransportRoute() << ','
                       << (distance.pathTiles ? int(*distance.pathTiles) : -1) << '\n';
            }
        }
        if (t < ticks)
            gameStateUpdateLogic(false);
    }
    std::cout << "RAN " << ticks << " ticks, final=" << state.currentTicks << '\n';
    for (const auto& entry : tracked)
    {
        std::cout << "TRACKED_GUEST id=" << entry.id.ToUnderlying() << " name=" << entry.name << " escaped=" << entry.escaped
                  << " reached_target=" << entry.reachedTarget << " changed_target=" << entry.changedTarget << '\n';
        const auto* expectProgress = std::getenv("OPENRCT2_INVESTIGATION_EXPECT_PROGRESS");
        if (expectProgress != nullptr && std::string_view(expectProgress) == "1")
            EXPECT_TRUE(entry.escaped) << entry.name << " remained in the trap";
    }
}
