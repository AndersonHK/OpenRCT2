// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// External original-art oracle: compile ONLY against the pristine upstream core.
// No current prop/track rules, shaders, renderer, or precomputed paint stream are inputs.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openrct2/Context.h>
#include <openrct2/Date.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Imaging.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/interface/ViewportFlags.h>
#include <openrct2/localisation/Language.h>
#include <openrct2/object/BannerObject.h>
#include <openrct2/object/EntranceObject.h>
#include <openrct2/object/FootpathEntry.h>
#include <openrct2/object/FootpathRailingsObject.h>
#include <openrct2/object/FootpathSurfaceObject.h>
#include <openrct2/object/LargeSceneryObject.h>
#include <openrct2/object/ObjectLimits.h>
#include <openrct2/object/ObjectList.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/ObjectRepository.h>
#include <openrct2/object/PathAdditionObject.h>
#include <openrct2/object/SmallSceneryObject.h>
#include <openrct2/object/WallObject.h>
#include <openrct2/park/ParkFile.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/TrackData.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/ride/ted/TrackElemType.h>
#include <openrct2/ride/ted/TrackElementDescriptor.h>
#include <openrct2/world/Banner.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/MapSelection.h>
#include <openrct2/world/tile_element/BannerElement.h>
#include <openrct2/world/tile_element/EntranceElement.h>
#include <openrct2/world/tile_element/LargeSceneryElement.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <openrct2/world/tile_element/TrackElement.h>
#include <openrct2/world/tile_element/WallElement.h>
#include <stdexcept>
#include <string>
#include <vector>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
namespace fs = std::filesystem;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    std::string Environment(const char* key)
    {
        const auto* value = std::getenv(key);
        Require(value && *value, "Missing explicit OPENRCT2_ORACLE isolation path");
        return value;
    }
    void Bytes(const fs::path& path, const void* data, size_t size)
    {
        Require(!fs::exists(path), "Refusing to replace output artifact");
        std::ofstream stream(path, std::ios::binary);
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        Require(stream.good(), "Could not write artifact");
    }
    json_t ObjectSources(IContext& context)
    {
        json_t result = json_t::array();
        auto& manager = context.GetObjectManager();
        const auto loaded = manager.GetLoadedObjects();
        for (const auto type : getAllObjectTypes())
        {
            const auto& list = loaded.GetList(type);
            for (size_t slot = 0; slot < list.size(); slot++)
            {
                if (!list[slot].HasValue())
                    continue;
                const auto* source = context.GetObjectRepository().FindObject(list[slot]);
                Require(source != nullptr, "Loaded object is missing repository provenance");
                result.push_back({ { "type", static_cast<int>(type) },
                                   { "slot", slot },
                                   { "identifier", source->Identifier },
                                   { "path", source->Path } });
            }
        }
        return result;
    }

    void ConstructionOverlays(IContext& context, json_t& manifest)
    {
        auto& manager = context.GetObjectManager();
        ObjectEntryIndex pathSurface = kObjectEntryIndexNull, railings = kObjectEntryIndexNull;
        for (ObjectEntryIndex i = 0; i < 255; ++i)
        {
            if (auto* surface = manager.GetLoadedObject<FootpathSurfaceObject>(i); surface != nullptr
                && !(surface->Flags & FOOTPATH_ENTRY_FLAG_IS_QUEUE)
                && !(surface->Flags & FOOTPATH_ENTRY_FLAG_SHOW_ONLY_IN_SCENARIO_EDITOR) && pathSurface == kObjectEntryIndexNull)
                pathSurface = i;
            if (railings == kObjectEntryIndexNull && manager.GetLoadedObject<FootpathRailingsObject>(i))
                railings = i;
        }
        const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        Require(
            grass != kObjectEntryIndexNull && rock != kObjectEntryIndexNull && pathSurface != kObjectEntryIndexNull
                && railings != kObjectEntryIndexNull,
            "Seed lacks construction overlay terrain/path materials");
        gameStateInitAll(getGameState(), TileCoordsXY{ 64, 64 });
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
                Require(surface != nullptr, "Construction fixture lacks surface");
                const int z = x >= 38 && x <= 40 && y >= 27 && y <= 37 ? 128 : 64;
                surface->setBaseZ(z);
                surface->setClearanceZ(z);
                surface->setSlope(x == 30 && y >= 30 && y <= 34 ? 12 : 0);
                surface->setWaterHeight(x >= 31 && x <= 33 && y >= 31 && y <= 33 ? 96 : 0);
                surface->setGrassLength(0);
                surface->setOwnership(kUnowned);
                surface->setParkFences(0);
                surface->setSurfaceObjectIndex(grass);
                surface->setEdgeObjectIndex(rock);
            }
        const auto path = [&](int x, int y, int z, uint8_t edges, bool ghost, bool sloped = false, uint8_t direction = 0) {
            auto* element = TileElementInsert<PathElement>({ x * 32, y * 32, z }, 15);
            Require(element != nullptr, "Construction path insertion failed");
            element->setClearanceZ(z + (sloped ? 32 : 16));
            element->setSurfaceEntryIndex(pathSurface);
            element->setRailingsEntryIndex(railings);
            element->setEdgesAndCorners(edges);
            element->setSloped(sloped);
            element->setSlopeDirection(direction);
            element->setHasQueueBanner(false);
            element->setAddition(0);
            element->setGhost(ghost);
            manifest["objects"].push_back({ { "kind", "constructionPath" },
                                            { "x", x },
                                            { "y", y },
                                            { "baseZ", z },
                                            { "edges", edges },
                                            { "ghost", ghost },
                                            { "sloped", sloped },
                                            { "direction", direction } });
        };
        for (int x = 23; x <= 28; ++x)
            path(x, 28, 128, 5, true);
        path(22, 28, 112, 5, true, true, 2);
        for (int y = 29; y <= 35; ++y)
            path(27, y, 128, 10, true);
        for (int x = 35; x <= 43; ++x)
            path(x, 32, 64, 5, x >= 39);
        path(37, 35, 64, 5, true, true, 0);
        manifest["fixture"] = "original-construction-overlays-v1";
        manifest["scope"] = "raw rectangular/corner/quarter/edge/water selections, irregular construction footprint and eight "
                            "arrow directions across eight cameras; original ghost bridge/ramp and tunnel paths; no simulation "
                            "ticks";
    }
    json_t ApplyConstructionSelection(uint8_t rotation, int zoom)
    {
        constexpr std::array<MapSelectType, 8> types{ MapSelectType::full,     MapSelectType::fullTerrainAndWater,
                                                      MapSelectType::quarter0, MapSelectType::edge0,
                                                      MapSelectType::corner0,  MapSelectType::fullWater,
                                                      MapSelectType::quarter2, MapSelectType::edge2 };
        const auto index = static_cast<size_t>(zoom * 4 + rotation);
        gMapSelectFlags.clearAll();
        gMapSelectFlags.set(MapSelectFlag::enable, MapSelectFlag::enableConstruct, MapSelectFlag::enableArrow);
        if (zoom == 1)
            gMapSelectFlags.set(MapSelectFlag::green);
        gMapSelectType = types[index];
        gMapSelectPositionA = { 30 * 32, 30 * 32 };
        gMapSelectPositionB = { 34 * 32, 34 * 32 };
        gMapSelectArrowPosition = { 27 * 32, 32 * 32, 128 };
        gMapSelectArrowDirection = static_cast<uint8_t>(index);
        MapSelection::clearSelectedTiles();
        json_t tiles = json_t::array();
        constexpr std::array<CoordsXY, 6> footprint{ { { 25 * 32, 28 * 32 },
                                                       { 26 * 32, 28 * 32 },
                                                       { 27 * 32, 28 * 32 },
                                                       { 27 * 32, 29 * 32 },
                                                       { 27 * 32, 30 * 32 },
                                                       { 28 * 32, 30 * 32 } } };
        for (const auto& tile : footprint)
        {
            MapSelection::addSelectedTile(tile);
            tiles.push_back({ tile.x, tile.y });
        }
        return { { "flags", gMapSelectFlags.holder },
                 { "type", EnumValue(gMapSelectType) },
                 { "first", { gMapSelectPositionA.x, gMapSelectPositionA.y } },
                 { "last", { gMapSelectPositionB.x, gMapSelectPositionB.y } },
                 { "arrow", { gMapSelectArrowPosition.x, gMapSelectArrowPosition.y, gMapSelectArrowPosition.z } },
                 { "direction", gMapSelectArrowDirection },
                 { "tiles", std::move(tiles) } };
    }

    void TrackSpecials(IContext& context, json_t& manifest, bool regressions = false)
    {
        struct SeedRide
        {
            int style;
            ride_type_t type;
            ObjectEntryIndex object, station;
        };
        struct Specimen
        {
            SeedRide seed;
            TrackElemType type;
            bool chain{}, station{}, inverted{}, brakeClosed{};
        };
        auto& state = getGameState();
        auto& manager = context.GetObjectManager();
        std::vector<SeedRide> seeds;
        const std::vector<int> requiredStyles = regressions ? std::vector<int>{ 79, 9, 31, 78, 53, 67, 11, 30, 8, 38, 10 }
                                                            : std::vector<int>{ 39, 27, 1, 56, 65, 3, 69, 31, 78, 53, 24 };
        for (int style : requiredStyles)
        {
            bool found = false;
            for (const auto& ride : RideManager(state))
                if (static_cast<int>(getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle) == style)
                {
                    seeds.push_back({ style, ride.type, ride.subtype, ride.entranceStyle });
                    found = true;
                    break;
                }
            if (!found && regressions && (style == 8 || style == 9 || style == 10))
            {
                // Older EverythingPark seeds predate the separate classic ride
                // types. Their original painters accept the corresponding held assets.
                const auto assetStyle = style == 8 ? TrackStyle::standUpRollerCoaster : TrackStyle::woodenRollerCoaster;
                for (const auto& ride : RideManager(state))
                {
                    if (getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle != assetStyle)
                        continue;
                    for (uint32_t type = 0; type < static_cast<uint32_t>(RIDE_TYPE_COUNT); ++type)
                        if (static_cast<int>(
                                getTrackDrawerEntry(GetRideTypeDescriptor(static_cast<ride_type_t>(type))).trackStyle)
                            == style)
                        {
                            seeds.push_back({ style, static_cast<ride_type_t>(type), ride.subtype, ride.entranceStyle });
                            manifest["seedRideFallbacks"].push_back(
                                { { "style", style },
                                  { "rideType", type },
                                  { "assetRideType", ride.type },
                                  { "object", ride.subtype },
                                  { "reason",
                                    "classic original painter with corresponding existing vehicle/station assets" } });
                            found = true;
                            break;
                        }
                    if (found)
                        break;
                }
            }
            Require(found, "Seed lacks a required track-specials ride style");
        }
        const auto seed = [&](int style) -> SeedRide {
            for (const auto& item : seeds)
                if (item.style == style)
                    return item;
            throw std::runtime_error("Missing track-specials seed metadata");
        };
        std::vector<Specimen> specimens;
        if (regressions)
        {
            // Complete TED placements for 49 curated source-admission regressions.
            for (const auto type : { 16, 17, 18, 21, 22, 23, 44, 45, 87, 90, 91, 94, 137, 158, 178 })
                specimens.push_back({ seed(79), static_cast<TrackElemType>(type) });
            for (const auto type : { 16, 18 })
                specimens.push_back({ seed(9), static_cast<TrackElemType>(type) });
            for (int style : { 31, 78 })
                for (const auto type : { 46, 47, 48, 49, 133, 134, 137, 138 })
                    specimens.push_back({ seed(style), static_cast<TrackElemType>(type) });
            for (int style : { 53, 67 })
                specimens.push_back({ seed(style), TrackElemType::endStation, false, true });
            for (const auto type : { 42, 46 })
                specimens.push_back({ seed(67), static_cast<TrackElemType>(type) });
            for (int style : { 11, 53 })
            {
                for (const auto type : { 4, 6, 9, 24, 26 })
                    specimens.push_back({ seed(style), static_cast<TrackElemType>(type), false, false, style == 53 });
                specimens.push_back({ seed(style), TrackElemType::up25, true, false, style == 53 });
            }
            Require(specimens.size() == 49, "Original track-regression prefix changed");
            // Append only: the original49 identities and tile placements remain fixed.
            for (int type : { 22, 44 })
                specimens.push_back({ seed(9), static_cast<TrackElemType>(type) });
            for (int style : { 11, 30 })
                for (int type : { 141, 337, 338 })
                    specimens.push_back({ seed(style), static_cast<TrackElemType>(type), style == 11 && type == 141, false,
                                          false, type == 338 });
            for (int type : { 158, 171 })
                specimens.push_back({ seed(8), static_cast<TrackElemType>(type) });
            for (int type : { 42, 43 })
                specimens.push_back({ seed(38), static_cast<TrackElemType>(type) });
            for (int style : { 9, 10, 79 })
                specimens.push_back({ seed(style), TrackElemType::waterSplash });
        }
        else
        {
            for (uint16_t type = 102; type <= 109; ++type)
                specimens.push_back({ seed(39), static_cast<TrackElemType>(type) });
            for (int style : { 27, 1, 56, 65, 3, 69 })
                specimens.push_back({ seed(style), TrackElemType::endStation, false, true });
            for (int style : { 31, 78 })
            {
                for (const auto type : { TrackElemType::flat, TrackElemType::up25 })
                {
                    specimens.push_back({ seed(style), type, false });
                    specimens.push_back({ seed(style), type, true });
                }
                for (const auto type :
                     { TrackElemType::down25, TrackElemType::flatToUp25, TrackElemType::up25ToFlat,
                       TrackElemType::leftQuarterTurn3Tiles, TrackElemType::rightQuarterTurn3Tiles, TrackElemType::sBendLeft,
                       TrackElemType::sBendRight, TrackElemType::flatToUp60, TrackElemType::up60ToFlat })
                    specimens.push_back({ seed(style), type });
                specimens.push_back({ seed(style), TrackElemType::endStation, false, true });
            }
            // Append after the original42 specimens so their identity and positions stay stable.
            for (const auto type :
                 { TrackElemType::flat, TrackElemType::up25, TrackElemType::leftQuarterTurn5Tiles, TrackElemType::sBendLeft })
                specimens.push_back({ seed(53), type, false, false, true });
            for (const auto type : { TrackElemType::up60, TrackElemType::up25ToUp60, TrackElemType::up60ToUp25 })
                specimens.push_back({ seed(24), type });
        }
        Require(specimens.size() <= (regressions ? 64u : 49u), "Track-specials grid capacity exceeded");
        if (regressions)
            Require(specimens.size() == 64, "Track-regression coverage changed unexpectedly");
        const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        Require(grass != kObjectEntryIndexNull && rock != kObjectEntryIndexNull, "Seed lacks grass/rock");
        const int mapSize = regressions ? 72 : 64;
        gameStateInitAll(state, TileCoordsXY{ mapSize, mapSize });
        for (int y = 0; y < mapSize; ++y)
            for (int x = 0; x < mapSize; ++x)
            {
                auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
                Require(surface != nullptr, "Missing track-specials surface");
                surface->setBaseZ(64);
                surface->setClearanceZ(64);
                surface->setSlope(0);
                surface->setWaterHeight(0);
                surface->setGrassLength(0);
                surface->setOwnership(kUnowned);
                surface->setParkFences(0);
                surface->setSurfaceObjectIndex(grass);
                surface->setEdgeObjectIndex(rock);
            }
        manifest["grid"] = { { "mapSize", mapSize },
                             { "columns", regressions ? 8 : 7 },
                             { "spacing", 8 },
                             { "preservedPrefix", regressions ? 49 : 0 },
                             { "count", specimens.size() } };
        for (size_t id = 0; id < specimens.size(); ++id)
        {
            const auto& spec = specimens[id];
            const auto& data = TrackMetadata::GetTrackElementDescriptor(spec.type).sequenceData;
            Require(data.numSequences > 0, "Track-specials TED has no sequences");
            int minX = 0, minY = 0, maxX = 0, maxY = 0, minZ = 0;
            for (uint8_t sequence = 0; sequence < data.numSequences; ++sequence)
            {
                const auto& c = data.sequences[sequence].clearance;
                Require(c.x % 32 == 0 && c.y % 32 == 0, "Track-specials TED is not tile aligned");
                minX = std::min(minX, int(c.x));
                minY = std::min(minY, int(c.y));
                minZ = std::min(minZ, int(c.z));
                maxX = std::max(maxX, int(c.x));
                maxY = std::max(maxY, int(c.y));
            }
            Require(maxX - minX <= 5 * 32 && maxY - minY <= 5 * 32, "Track-specials specimen exceeds separated grid cell");
            const int column = id < 49 ? static_cast<int>(id % 7) : (id < 56 ? 7 : static_cast<int>(id - 56));
            const int row = id < 49 ? static_cast<int>(id / 7) : (id < 56 ? static_cast<int>(id - 49) : 7);
            const int cellX = 3 + column * 8, cellY = 3 + row * 8;
            Require(
                cellX + (maxX - minX) / 32 < mapSize - 1 && cellY + (maxY - minY) / 32 < mapSize - 1,
                "Track-specials specimen reaches technical map boundary");
            const int x = cellX * 32 - minX, y = cellY * 32 - minY, baseZ = 128 - minZ;
            auto& ride = *RideAllocateAtIndex(RideId::FromUnderlying(static_cast<uint16_t>(id)));
            ride.type = spec.seed.type;
            ride.subtype = spec.seed.object;
            ride.entranceStyle = spec.seed.station;
            ride.status = RideStatus::closed;
            ride.customName = "Track special " + std::to_string(id);
            ride.numStations = spec.station ? 1 : 0;
            for (auto& colour : ride.trackColours)
                colour = { Colour::brightRed, Colour::yellow, Colour::white };
            for (auto& colour : ride.vehicleColours)
                colour = { Colour::lightBlue, Colour::yellow, Colour::white };
            auto& station = ride.getStation();
            station.start = { x, y };
            station.setBaseZ(baseZ);
            station.entrance = { cellX + 1, cellY - 1, baseZ / 8, 0 };
            station.exit = { cellX + 1, cellY + 1, baseZ / 8, 0 };
            json_t placements = json_t::array();
            const auto count = spec.station ? 3 : data.numSequences;
            for (int sequence = 0; sequence < count; ++sequence)
            {
                const auto piece = spec.station
                    ? (sequence == 0 ? TrackElemType::endStation
                                     : (sequence == 1 ? TrackElemType::middleStation : TrackElemType::beginStation))
                    : spec.type;
                const auto& c = TrackMetadata::GetTrackElementDescriptor(piece)
                                    .sequenceData.sequences[spec.station ? 0 : sequence]
                                    .clearance;
                CoordsXYZ pos{ x + c.x + (spec.station ? sequence * 32 : 0), y + c.y, baseZ + c.z };
                auto* track = TileElementInsert<TrackElement>(pos, 15);
                Require(track != nullptr, "Track-specials insertion failed");
                track->setRideIndex(ride.id);
                track->setRideType(spec.seed.type);
                track->setTrackType(piece);
                track->setDirection(0);
                track->setSequenceIndex(static_cast<uint8_t>(spec.station ? 0 : sequence));
                track->setStationIndex(StationIndex::FromUnderlying(0));
                track->setHasChain(spec.chain);
                track->setInverted(spec.inverted);
                track->setBrakeClosed(spec.brakeClosed);
                track->setClearanceZ(pos.z + std::max(32, int(c.clearanceZ)));
                track->setColourScheme(RideColourScheme::main);
                placements.push_back({ { "x", pos.x / 32 },
                                       { "y", pos.y / 32 },
                                       { "baseZ", pos.z },
                                       { "clearanceZ", track->getClearanceZ() },
                                       { "trackType", static_cast<uint16_t>(piece) },
                                       { "sequence", spec.station ? 0 : sequence } });
            }
            if (spec.station)
                for (int exit = 0; exit < 2; ++exit)
                {
                    auto* portal = TileElementInsert<EntranceElement>(
                        { (cellX + 1) * 32, (cellY + (exit ? 1 : -1)) * 32, baseZ }, 15);
                    Require(portal != nullptr, "Track-specials portal insertion failed");
                    portal->setEntranceType(exit ? EntranceType::rideExit : EntranceType::rideEntrance);
                    portal->setRideIndex(ride.id);
                    portal->setStationIndex(StationIndex::FromUnderlying(0));
                    portal->setDirection(exit ? 1 : 3);
                    portal->setClearanceZ(baseZ + 64);
                }
            manifest["objects"].push_back(
                { { "kind", spec.station ? "station" : "trackSpecial" },
                  { "specimen", id },
                  { "ride", id },
                  { "style", spec.seed.style },
                  { "effectiveStyle",
                    static_cast<uint32_t>(
                        getTrackDrawerEntry(GetRideTypeDescriptor(spec.seed.type), spec.inverted, false).trackStyle) },
                  { "rideType", spec.seed.type },
                  { "object", spec.seed.object },
                  { "station", spec.seed.station },
                  { "trackType", static_cast<uint16_t>(spec.type) },
                  { "chain", spec.chain },
                  { "inverted", spec.inverted },
                  { "brakeClosed", spec.brakeClosed },
                  { "direction", 0 },
                  { "x", x / 32 },
                  { "y", y / 32 },
                  { "baseZ", baseZ },
                  { "cellX", cellX },
                  { "cellY", cellY },
                  { "sequences", count },
                  { "placements", placements } });
        }
        state.ridesEndOfUsedRange = static_cast<uint16_t>(specimens.size());
        manifest["fixture"] = regressions ? "original-track-regressions-v2" : "original-track-specials-v1";
        manifest["scope"] = regressions
            ? "Wooden/classic ordinary and banked turns, helixes and diagonals; Junior/Water sloped three-tile turns "
              "and eighths; MultiDimension/WildMouse stations and Mouse curves; Compact/MultiInverted25-degree "
              "support-predicate "
              "regressions; appended classic banks, inverted diagonal chain/brakes, classic stand-up diagonals, LogFlume "
              "curves "
              "and three wooden waterSplash families; complete TED sequences, no vehicles or simulation ticks"
            : "Looping quarter-helix types102..109; six narrow/pier station families; Junior/Water flat, chain, "
              "slopes, curves, S-bends, steep transitions and stations; no vehicles or simulation ticks";
    }

    void PhotoStates(IContext& context, json_t& manifest)
    {
        struct Seed
        {
            ride_type_t type;
            ObjectEntryIndex object, station;
        };
        std::array<Seed, 2> seeds{};
        auto& state = getGameState();
        for (size_t family = 0; family < seeds.size(); ++family)
        {
            const auto wanted = family == 0 ? TrackStyle::loopingRollerCoaster : TrackStyle::woodenRollerCoaster;
            bool found = false;
            for (const auto& ride : RideManager(state))
                if (getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle == wanted)
                {
                    seeds[family] = { ride.type, ride.subtype, ride.entranceStyle };
                    found = true;
                    break;
                }
            Require(found, "Seed lacks normal/small photo track family");
        }
        auto& manager = context.GetObjectManager();
        const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        Require(grass != kObjectEntryIndexNull && rock != kObjectEntryIndexNull, "Photo fixture lacks terrain");
        gameStateInitAll(state, TileCoordsXY{ 64, 64 });
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
                Require(surface != nullptr, "Photo fixture lacks surface");
                surface->setBaseZ(64);
                surface->setClearanceZ(64);
                surface->setSlope(0);
                surface->setWaterHeight(0);
                surface->setGrassLength(0);
                surface->setOwnership(kUnowned);
                surface->setParkFences(0);
                surface->setSurfaceObjectIndex(grass);
                surface->setEdgeObjectIndex(rock);
            }
        manifest["photoStates"] = json_t::array();
        uint16_t id = 0;
        for (size_t family = 0; family < seeds.size(); ++family)
            for (uint8_t timeout : { uint8_t(0), uint8_t(1), uint8_t(3) })
                for (bool ghost : { false, true })
                {
                    const int x = 19 + int(id % 4) * 8, y = 23 + int(id / 4) * 8;
                    const auto seed = seeds[family];
                    auto* ride = RideAllocateAtIndex(RideId::FromUnderlying(id));
                    Require(ride != nullptr, "Photo ride allocation failed");
                    ride->type = seed.type;
                    ride->subtype = seed.object;
                    ride->entranceStyle = seed.station;
                    ride->status = RideStatus::closed;
                    ride->customName = "Photo state " + std::to_string(id);
                    for (auto& colour : ride->trackColours)
                        colour = { Colour::brightRed, Colour::yellow, Colour::white };
                    auto* track = TileElementInsert<TrackElement>({ x * 32, y * 32, 64 }, 15);
                    Require(track != nullptr, "Photo track insertion failed");
                    track->setRideIndex(ride->id);
                    track->setRideType(seed.type);
                    track->setTrackType(TrackElemType::onRidePhoto);
                    track->setDirection(0);
                    track->setSequenceIndex(0);
                    track->setClearanceZ(112);
                    track->setColourScheme(RideColourScheme::main);
                    track->setPhotoTimeout(timeout);
                    track->setGhost(ghost);
                    const json_t identity = { { "specimen", id },
                                              { "x", x },
                                              { "y", y },
                                              { "baseZ", 64 },
                                              { "clearanceZ", 112 },
                                              { "ride", id },
                                              { "rideType", seed.type },
                                              { "trackType", static_cast<uint16_t>(TrackElemType::onRidePhoto) },
                                              { "sequence", 0 },
                                              { "direction", 0 },
                                              { "photoTimeout", timeout },
                                              { "small", family != 0 },
                                              { "ghost", ghost } };
                    manifest["photoStates"].push_back(identity);
                    auto specimen = identity;
                    specimen["kind"] = "photo";
                    manifest["objects"].push_back(std::move(specimen));
                    ++id;
                }
        state.ridesEndOfUsedRange = id;
        manifest["fixture"] = "original-onride-photo-v1";
        manifest["grid"] = { { "mapSize", 64 }, { "columns", 4 }, { "spacing", 8 }, { "count", id } };
        manifest["scope"] = "Normal Looping and small Wooden camera/sign art, timeout0/1/3, ordinary/ghost, "
                            "all four camera rotations and zoom0/1; no simulation ticks";
    }

    void Underground(IContext& context, json_t& manifest)
    {
        struct Seed
        {
            ride_type_t type = kRideTypeNull;
            ObjectEntryIndex object = kObjectEntryIndexNull, station = kObjectEntryIndexNull;
            TrackElemType piece{};
        };
        auto& state = getGameState();
        auto& manager = context.GetObjectManager();
        std::array<Seed, 3> seeds{};
        constexpr std::array styles{ TrackStyle::loopingRollerCoaster, TrackStyle::_3DCinema, TrackStyle::shop };
        for (size_t i = 0; i < styles.size(); ++i)
            for (const auto& ride : RideManager(state))
            {
                const auto& descriptor = GetRideTypeDescriptor(ride.type);
                if (getTrackDrawerEntry(descriptor).trackStyle == styles[i])
                {
                    seeds[i] = { ride.type, ride.subtype, ride.entranceStyle, descriptor.StartTrackPiece };
                    break;
                }
            }
        Require(
            std::all_of(seeds.begin(), seeds.end(), [](const auto& seed) { return seed.type != kRideTypeNull; }),
            "Seed lacks underground coaster/cinema/shop families");
        ObjectEntryIndex pathSurface = kObjectEntryIndexNull, railings = kObjectEntryIndexNull;
        for (ObjectEntryIndex i = 0; i < 255; ++i)
        {
            if (auto* surface = manager.GetLoadedObject<FootpathSurfaceObject>(i); surface != nullptr
                && !(surface->Flags & FOOTPATH_ENTRY_FLAG_IS_QUEUE)
                && !(surface->Flags & FOOTPATH_ENTRY_FLAG_SHOW_ONLY_IN_SCENARIO_EDITOR) && pathSurface == kObjectEntryIndexNull)
                pathSurface = i;
            if (railings == kObjectEntryIndexNull && manager.GetLoadedObject<FootpathRailingsObject>(i))
                railings = i;
        }
        const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        Require(
            grass != kObjectEntryIndexNull && rock != kObjectEntryIndexNull && pathSurface != kObjectEntryIndexNull
                && railings != kObjectEntryIndexNull,
            "Seed lacks underground terrain/path materials");
        gameStateInitAll(state, TileCoordsXY{ 64, 64 });
        const auto terrain = [&](int x, int y, int z, uint8_t slope = 0) {
            auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
            Require(surface != nullptr, "Underground fixture lacks surface");
            surface->setBaseZ(z);
            surface->setClearanceZ(z);
            surface->setSlope(slope);
            surface->setWaterHeight(0);
            surface->setGrassLength(0);
            surface->setOwnership(kUnowned);
            surface->setParkFences(0);
            surface->setSurfaceObjectIndex(grass);
            surface->setEdgeObjectIndex(rock);
        };
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                terrain(x, y, 64);
        constexpr std::array labels{ "path-through-flat-terrace-x",
                                     "path-through-flat-terrace-y",
                                     "path-ramp-matching-surface",
                                     "path-flat-beneath-sloped-surface",
                                     "coaster-flat-through-terrace",
                                     "coaster-up25-through-slope",
                                     "station-partly-buried",
                                     "station-fully-buried",
                                     "cinema-partly-buried",
                                     "cinema-fully-buried",
                                     "shop-partly-buried",
                                     "shop-fully-buried",
                                     "equal-base-path-on-raised-corners",
                                     "equal-base-track-on-raised-corners",
                                     "path-at-terrace-top",
                                     "stacked-buried-and-exposed-paths" };
        for (int id = 0; id < static_cast<int>(labels.size()); ++id)
        {
            const int x = 8 + (id % 4) * 13, y = 8 + (id / 4) * 13;
            int plateau = id == 7 ? 256 : (id == 9 ? 320 : (id == 11 ? 192 : (id == 10 ? 80 : 128)));
            if (id == 2 || id == 3 || id == 12 || id == 13)
                plateau = 64;
            const uint8_t terrainSlope = (id == 2 || id == 3 || id == 12 || id == 13) ? 12 : 0;
            // A separated 3x5 raised terrace exposes both faces in opposite cameras.
            // Author all terrain before inserting elements to preserve ordinary height ordering.
            for (int dy = (id == 9 || id == 11 ? -4 : -2); dy <= (id == 9 || id == 11 ? 4 : 2); ++dy)
                for (int dx = (id == 9 || id == 11 ? -2 : 0); dx <= (id == 9 || id == 11 ? 4 : 2); ++dx)
                    terrain(x + dx, y + dy, plateau, terrainSlope);
            if (id == 2)
                for (int dy = -2; dy <= 2; ++dy)
                {
                    terrain(x, y + dy, 80);
                    terrain(x + 2, y + dy, 64);
                }
            if (id == 5)
            {
                terrain(x, y, 64, 12);
                terrain(x - 1, y, 80);
            }
            json_t placements = json_t::array();
            const auto path = [&](int px, int py, int z, bool sloped, uint8_t edges) {
                auto* element = TileElementInsert<PathElement>({ px * 32, py * 32, z }, 15);
                Require(element != nullptr, "Underground path insertion failed");
                element->setClearanceZ(z + (sloped ? 32 : 16));
                element->setSurfaceEntryIndex(pathSurface);
                element->setRailingsEntryIndex(railings);
                element->setEdgesAndCorners(edges);
                element->setSloped(sloped);
                element->setSlopeDirection(0);
                element->setHasQueueBanner(false);
                element->setAddition(0);
                placements.push_back({ { "kind", "path" },
                                       { "x", px },
                                       { "y", py },
                                       { "baseZ", z },
                                       { "slopeDirection", 0 },
                                       { "sloped", sloped },
                                       { "edges", edges } });
            };
            if (id < 4 || id == 12 || id >= 14)
            {
                if (id == 1)
                    for (int offset = -4; offset <= 4; ++offset)
                        path(x + 1, y + offset, 64, false, 10);
                else if (id == 2)
                {
                    path(x + 1, y, 64, true, 5);
                    path(x, y, 80, false, 5);
                    path(x + 2, y, 64, false, 5);
                }
                else
                    for (int offset = -2; offset <= 4; ++offset)
                    {
                        path(x + offset, y, id == 14 ? 128 : (id == 3 ? 48 : 64), false, 5);
                        if (id == 15)
                            path(x + offset, y, 128, false, 5);
                    }
            }
            else
            {
                const int family = id == 8 || id == 9 ? 1 : (id == 10 || id == 11 ? 2 : 0);
                const auto& seed = seeds[family];
                auto* allocated = RideAllocateAtIndex(RideId::FromUnderlying(static_cast<uint16_t>(id)));
                Require(allocated != nullptr, "Underground ride allocation failed");
                auto& ride = *allocated;
                ride.type = seed.type;
                ride.subtype = seed.object;
                ride.entranceStyle = seed.station;
                ride.status = RideStatus::closed;
                ride.customName = labels[id];
                ride.numStations = 1;
                ride.getStation().start = { x * 32, y * 32 };
                ride.getStation().setBaseZ(64);
                for (auto& colour : ride.trackColours)
                    colour = { Colour::brightRed, Colour::yellow, Colour::white };
                for (auto& colour : ride.vehicleColours)
                    colour = { Colour::lightBlue, Colour::yellow, Colour::white };
                const auto insertTrack = [&](int px, int py, int z, TrackElemType piece, uint8_t sequence) {
                    auto* element = TileElementInsert<TrackElement>({ px * 32, py * 32, z }, 15);
                    Require(element != nullptr, "Underground track insertion failed");
                    element->setRideIndex(ride.id);
                    element->setRideType(ride.type);
                    element->setTrackType(piece);
                    element->setDirection(0);
                    element->setSequenceIndex(sequence);
                    element->setStationIndex(StationIndex::FromUnderlying(0));
                    element->setClearanceZ(z + (family == 1 ? 160 : 64));
                    element->setColourScheme(RideColourScheme::main);
                    placements.push_back({ { "kind", "track" },
                                           { "x", px },
                                           { "y", py },
                                           { "baseZ", z },
                                           { "trackType", static_cast<uint16_t>(piece) },
                                           { "sequence", sequence },
                                           { "ride", id } });
                };
                if (family != 0)
                {
                    const auto& data = TrackMetadata::GetTrackElementDescriptor(seed.piece).sequenceData;
                    for (uint8_t seq = 0; seq < data.numSequences; ++seq)
                    {
                        const auto& c = data.sequences[seq].clearance;
                        Require(c.x % 32 == 0 && c.y % 32 == 0, "Underground building is not tile aligned");
                        insertTrack(x + c.x / 32, y + c.y / 32, 64 + c.z, seed.piece, seq);
                    }
                }
                else if (id == 6 || id == 7)
                    for (int offset = 0; offset < 3; ++offset)
                        insertTrack(
                            x + offset, y, 64,
                            offset == 0 ? TrackElemType::endStation
                                        : (offset == 1 ? TrackElemType::middleStation : TrackElemType::beginStation),
                            0);
                else if (id == 5)
                    insertTrack(x, y, 64, TrackElemType::up25, 0);
                else
                    for (int offset = -2; offset <= 4; ++offset)
                        insertTrack(x + offset, y, 64, TrackElemType::flat, 0);
            }
            json_t tileFacts = json_t::array();
            for (int dy = -4; dy <= 4; ++dy)
                for (int dx = -2; dx <= 4; ++dx)
                {
                    auto* element = MapGetFirstElementAt(TileCoordsXY{ x + dx, y + dy });
                    Require(element != nullptr, "Underground tile fact missing");
                    json_t elements = json_t::array();
                    uint32_t ordinal = 0;
                    do
                    {
                        json_t fact = { { "ordinal", ordinal++ },
                                        { "type", static_cast<uint8_t>(element->getType()) },
                                        { "baseZ", element->getBaseZ() },
                                        { "clearanceZ", element->getClearanceZ() } };
                        if (element->getType() == TileElementType::surface)
                            fact["slope"] = element->asSurface()->getSlope();
                        elements.push_back(fact);
                    } while (!(element++)->isLastForTile());
                    tileFacts.push_back({ { "x", x + dx }, { "y", y + dy }, { "elements", elements } });
                }
            manifest["objects"].push_back({ { "kind", "underground" },
                                            { "label", labels[id] },
                                            { "x", x },
                                            { "y", y },
                                            { "baseZ", 64 },
                                            { "terrainBaseZ", plateau },
                                            { "terrainSlope", terrainSlope },
                                            { "placements", placements },
                                            { "tiles", tileFacts } });
        }
        state.ridesEndOfUsedRange = static_cast<uint16_t>(labels.size());
        manifest["fixture"] = "original-underground-v1";
        manifest["scope"] = "16 isolated normal-view terrain occlusion/tunnel cases: flat and sloped paths/coaster, stations, "
                            "partly/fully buried Cinema and shop, equal-base raised corners and stacked paths; no "
                            "vehicles/ticks";
        manifest["grid"] = { { "mapSize", 64 }, { "columns", 4 }, { "spacing", 13 }, { "count", labels.size() } };
    }

    void UndergroundView(IContext& context, json_t& manifest)
    {
        auto& state = getGameState();
        ride_type_t towerType = kRideTypeNull;
        ObjectEntryIndex towerObject = kObjectEntryIndexNull, stationStyle = kObjectEntryIndexNull;
        for (const auto& ride : RideManager(state))
            if (getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle == TrackStyle::observationTower)
            {
                towerType = ride.type;
                towerObject = ride.subtype;
                stationStyle = ride.entranceStyle;
                break;
            }
        Require(towerType != kRideTypeNull, "Seed lacks underground-view tower");
        Underground(context, manifest);
        for (int specimen = 0; specimen < 2; ++specimen)
        {
            const int x = 58, y = 8 + specimen * 18;
            const int terrainZ = specimen == 0 ? 128 : 256;
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                {
                    auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ x + dx, y + dy });
                    Require(surface != nullptr, "Missing underground-view tower terrain");
                    surface->setBaseZ(terrainZ);
                    surface->setClearanceZ(terrainZ);
                }
            const auto rideId = RideId::FromUnderlying(static_cast<uint16_t>(16 + specimen));
            auto* allocated = RideAllocateAtIndex(rideId);
            Require(allocated != nullptr, "Underground-view tower allocation failed");
            auto& ride = *allocated;
            ride.type = towerType;
            ride.subtype = towerObject;
            ride.entranceStyle = stationStyle;
            ride.status = RideStatus::closed;
            ride.customName = "Buried tower " + std::to_string(specimen);
            ride.numStations = 1;
            ride.getStation().start = { x * 32, y * 32 };
            ride.getStation().setBaseZ(64);
            for (auto& colour : ride.trackColours)
                colour = { Colour::brightRed, Colour::yellow, Colour::white };
            json_t placements = json_t::array();
            const auto track = [&](CoordsXYZ position, TrackElemType type, uint8_t sequence, int clearance) {
                auto* element = TileElementInsert<TrackElement>(position, 15);
                Require(element != nullptr, "Underground-view tower track insertion failed");
                element->setRideIndex(rideId);
                element->setRideType(towerType);
                element->setTrackType(type);
                element->setSequenceIndex(sequence);
                element->setDirection(0);
                element->setStationIndex(StationIndex::FromUnderlying(0));
                element->setColourScheme(RideColourScheme::main);
                element->setClearanceZ(clearance);
                placements.push_back({ { "x", position.x / 32 },
                                       { "y", position.y / 32 },
                                       { "baseZ", position.z },
                                       { "clearanceZ", clearance },
                                       { "trackType", static_cast<uint16_t>(type) },
                                       { "sequence", sequence } });
            };
            const auto& base = TrackMetadata::GetTrackElementDescriptor(TrackElemType::towerBase).sequenceData;
            for (uint8_t sequence = 0; sequence < base.numSequences; ++sequence)
            {
                const auto& c = base.sequences[sequence].clearance;
                const CoordsXYZ position{ x * 32 + c.x, y * 32 + c.y, 64 + c.z };
                track(position, TrackElemType::towerBase, sequence, position.z + 96);
            }
            for (int z = 160; z <= 256; z += 32)
                track({ x * 32, y * 32, z }, TrackElemType::towerSection, 0, z + 32);
            manifest["objects"].push_back({ { "kind", "undergroundTower" },
                                            { "label", specimen == 0 ? "shallow-tower" : "deep-tower" },
                                            { "x", x },
                                            { "y", y },
                                            { "baseZ", 64 },
                                            { "terrainZ", terrainZ },
                                            { "style", static_cast<int>(TrackStyle::observationTower) },
                                            { "ride", rideId.ToUnderlying() },
                                            { "placements", std::move(placements) } });
        }
        state.ridesEndOfUsedRange = 18;
        manifest["fixture"] = "original-underground-view-v1";
        manifest["scope"] = "Actual underground/inside viewport mode: buried paths, coaster stations, flat buildings and tower "
                            "bases/sections under darkened transparent terrain; all four rotations and zoom0/1, no simulation "
                            "ticks";
    }

    void AnimatedPoses(json_t& manifest, int phase)
    {
        Require(phase >= 0 && phase < 3, "Animated pose must be0,1 or2");
        auto& state = getGameState();
        manifest["mechanismPoses"] = json_t::array();
        for (auto& ride : RideManager(state))
        {
            const auto style = getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle;
            int frame = 0, secondary = 0, restraints = 0;
            bool mechanism = true;
            switch (style)
            {
                case TrackStyle::hauntedHouse:
                    frame = phase == 0 ? 1 : (phase == 1 ? 9 : 18);
                    break;
                case TrackStyle::merryGoRound:
                    frame = phase == 0 ? 13 : (phase == 1 ? 31 : 7);
                    break;
                case TrackStyle::ferrisWheel:
                    frame = phase == 0 ? 13 : (phase == 1 ? 64 : 127);
                    break;
                case TrackStyle::spaceRings:
                    frame = 11;
                    break;
                case TrackStyle::twist:
                    frame = phase == 0 ? 3 : (phase == 1 ? 11 : 23);
                    break;
                case TrackStyle::enterprise:
                    frame = phase == 0 ? 12 : (phase == 1 ? 36 : 48);
                    break;
                case TrackStyle::swingingShip:
                    frame = phase == 0 ? -3 : (phase == 1 ? 6 : -9);
                    break;
                case TrackStyle::swingingInverterShip:
                    frame = phase == 0 ? -12 : (phase == 1 ? 24 : 36);
                    break;
                case TrackStyle::magicCarpet:
                    frame = phase == 0 ? 8 : (phase == 1 ? 16 : 24);
                    break;
                case TrackStyle::topSpin:
                    frame = phase == 0 ? 12 : (phase == 1 ? 24 : 0);
                    secondary = phase == 0 ? 5 : (phase == 1 ? 11 : 0);
                    restraints = phase == 2 ? 255 : 0;
                    break;
                case TrackStyle::motionSimulator:
                    frame = phase == 0 ? 12 : (phase == 1 ? 34 : 0);
                    restraints = phase == 2 ? 192 : 0;
                    break;
                case TrackStyle::spiralSlide:
                    ride.slideInUse = 1;
                    ride.spiralSlideProgress = static_cast<uint8_t>(phase == 0 ? 10 : (phase == 1 ? 30 : 47));
                    ride.slidePeepTShirtColour = Colour::brightRed;
                    mechanism = false;
                    break;
                default:
                    continue;
            }
            ride.flags.set(RideFlag::onTrack, mechanism);
            if (style == TrackStyle::merryGoRound && phase == 2)
            {
                ride.flags.set(RideFlag::breakdownPending);
                ride.breakdownReasonPending = Breakdown::controlFailure;
                ride.breakdownSoundModifier = 128;
            }
            json_t vehicles = json_t::array();
            if (mechanism)
            {
                const int count = style == TrackStyle::spaceRings ? 4 : 1;
                ride.numTrains = static_cast<uint8_t>(count);
                ride.numCarsPerTrain = 1;
                for (int slot = 0; slot < count; ++slot)
                {
                    auto* vehicle = state.entities.createEntity<Vehicle>();
                    Require(vehicle != nullptr, "Mechanism vehicle allocation failed");
                    ride.vehicles[slot] = vehicle->id;
                    vehicle->ride = ride.id;
                    vehicle->ride_subtype = ride.subtype;
                    vehicle->vehicle_type = 0;
                    vehicle->SubType = Vehicle::Type::head;
                    vehicle->next_vehicle_on_train = EntityId::GetNull();
                    vehicle->prev_vehicle_on_ride = vehicle->next_vehicle_on_ride = vehicle->id;
                    vehicle->status = Vehicle::Status::waitingForPassengers;
                    vehicle->num_peeps = vehicle->num_seats = 0;
                    std::fill(std::begin(vehicle->peep), std::end(vehicle->peep), EntityId::GetNull());
                    const int ringFrames[4] = { 0, 11, 41, 87 };
                    vehicle->flatRideAnimationFrame = static_cast<uint8_t>(
                        style == TrackStyle::spaceRings ? ringFrames[(slot + phase) % 4] : frame);
                    vehicle->flatRideSecondaryAnimationFrame = static_cast<uint8_t>(secondary);
                    vehicle->orientation = static_cast<uint8_t>((phase + 1) * 8);
                    vehicle->restraints_position = static_cast<uint8_t>(restraints);
                    vehicle->current_time = 8;
                    // Null-position mechanism entities are intentionally rendered by their original
                    // tile painter, not by the separate travelling-vehicle painter.
                    vehicles.push_back({ { "slot", slot },
                                         { "entityId", vehicle->id.ToUnderlying() },
                                         { "frame", vehicle->flatRideAnimationFrame },
                                         { "secondary", secondary },
                                         { "orientation", vehicle->orientation },
                                         { "restraints", restraints },
                                         { "currentTime", 8 } });
                }
            }
            manifest["mechanismPoses"].push_back({ { "ride", ride.id.ToUnderlying() },
                                                   { "rideType", ride.type },
                                                   { "objectSlot", ride.subtype },
                                                   { "style", static_cast<int>(style) },
                                                   { "onTrack", mechanism },
                                                   { "breakdownPending", ride.flags.has(RideFlag::breakdownPending) },
                                                   { "breakdownReason", static_cast<uint8_t>(ride.breakdownReasonPending) },
                                                   { "breakdownModifier", ride.breakdownSoundModifier },
                                                   { "slideInUse", ride.slideInUse },
                                                   { "slideProgress", ride.spiralSlideProgress },
                                                   { "slideColour", static_cast<uint8_t>(ride.slidePeepTShirtColour) },
                                                   { "vehicles", vehicles } });
        }
        manifest["fixture"] = "original-animated-buildings-v1";
        manifest["posePhase"] = phase;
        manifest["scope"] = "Original upstream flat mechanism body painters with explicit simulation-owned pose fields; "
                            "three separately saved poses, no wall-clock extrapolation, no simulation advance, no riders or "
                            "travelling cars";
    }

    void StaticBuildings(IContext& context, json_t& manifest)
    {
        struct Specimen
        {
            ride_type_t type;
            ObjectEntryIndex object, station;
            TrackElemType piece;
            int style;
            bool stationOnly{};
        };
        auto& state = getGameState();
        auto& manager = context.GetObjectManager();
        std::vector<Specimen> specimens;
        constexpr std::array styles{ 0, 7, 13, 25, 64, 16, 22, 42, 19, 63, 75, 17, 73, 72, 40, 74, 52, 60, 18, 55, 34, 59, 41 };
        for (const auto style : styles)
            for (const auto& ride : RideManager(state))
            {
                const auto& descriptor = GetRideTypeDescriptor(ride.type);
                if (static_cast<int>(getTrackDrawerEntry(descriptor).trackStyle) != style)
                    continue;
                specimens.push_back({ ride.type, ride.subtype, ride.entranceStyle, descriptor.StartTrackPiece, style });
                break;
            }
        Require(specimens.size() >= 12, "Seed lacks the expected static ride families");
        for (const auto style :
             { TrackStyle::loopingRollerCoaster, TrackStyle::latticeTriangle, TrackStyle::woodenRollerCoaster })
            for (const auto& ride : RideManager(state))
                if (getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle == style)
                {
                    specimens.push_back({ ride.type, ride.subtype, ride.entranceStyle, TrackElemType::endStation,
                                          static_cast<int>(style), true });
                    break;
                }
        const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        gameStateInitAll(state, TileCoordsXY{ 64, 64 });
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
                Require(surface != nullptr, "Missing building fixture surface");
                surface->setBaseZ(64);
                surface->setClearanceZ(64);
                surface->setSlope(0);
                surface->setWaterHeight(0);
                surface->setGrassLength(0);
                surface->setOwnership(kUnowned);
                surface->setParkFences(0);
                surface->setSurfaceObjectIndex(grass);
                surface->setEdgeObjectIndex(rock);
            }
        for (size_t i = 0; i < specimens.size(); ++i)
        {
            const auto& spec = specimens[i];
            const int x = 12 + int(i % 5) * 8, y = 12 + int(i / 5) * 8;
            // RideInitAll marks slots free but deliberately retains unrelated old fields.
            // Allocate through the normal owner API to clear vehicle IDs, flags and pose state.
            auto& ride = *RideAllocateAtIndex(RideId::FromUnderlying(static_cast<uint16_t>(i)));
            ride.type = spec.type;
            ride.subtype = spec.object;
            ride.entranceStyle = spec.station;
            ride.status = RideStatus::closed;
            ride.customName = "Static family " + std::to_string(spec.style);
            ride.numStations = 1;
            auto& station = ride.getStation();
            station.start = { x * 32, y * 32 };
            station.setBaseZ(64);
            station.entrance = { x + 3, y, 8, 0 };
            station.exit = { x + 3, y + 2, 8, 0 };
            for (auto& colour : ride.trackColours)
                colour = { Colour::brightRed, Colour::yellow, Colour::white };
            for (auto& colour : ride.vehicleColours)
                colour = { Colour::lightBlue, Colour::yellow, Colour::white };
            const auto& sequence = TrackMetadata::GetTrackElementDescriptor(spec.piece).sequenceData;
            for (uint8_t j = 0; j < (spec.stationOnly ? 3 : sequence.numSequences); ++j)
            {
                const auto& clearance = sequence.sequences[spec.stationOnly ? 0 : j].clearance;
                const CoordsXYZ pos{ x * 32 + clearance.x + (spec.stationOnly ? j * 32 : 0), y * 32 + clearance.y,
                                     64 + clearance.z };
                auto* track = TileElementInsert<TrackElement>(pos, 15);
                Require(track != nullptr, "Static ride insertion failed");
                track->setRideIndex(ride.id);
                track->setRideType(spec.type);
                const auto piece = spec.stationOnly && j != 0
                    ? (j == 1 ? TrackElemType::middleStation : TrackElemType::beginStation)
                    : spec.piece;
                track->setTrackType(piece);
                track->setDirection(0);
                track->setSequenceIndex(spec.stationOnly ? 0 : j);
                track->setStationIndex(StationIndex::FromUnderlying(0));
                track->setClearanceZ(pos.z + 128);
                track->setColourScheme(RideColourScheme::main);
                if (spec.piece == TrackElemType::maze)
                    track->setMazeEntry(0x5AA5);
            }
            if (spec.piece == TrackElemType::towerBase)
                for (int z = 160; z <= 256; z += 32)
                {
                    auto* track = TileElementInsert<TrackElement>({ x * 32, y * 32, z }, 15);
                    Require(track != nullptr, "Tower section insertion failed");
                    track->setRideIndex(ride.id);
                    track->setRideType(spec.type);
                    track->setTrackType(TrackElemType::towerSection);
                    track->setDirection(0);
                    track->setSequenceIndex(0);
                    track->setClearanceZ(z + 32);
                    track->setColourScheme(RideColourScheme::main);
                }
            for (int exit = 0; exit < 2; ++exit)
            {
                auto* entrance = TileElementInsert<EntranceElement>({ (x + 3) * 32, (y + exit * 2) * 32, 64 }, 15);
                Require(entrance != nullptr, "Ride portal insertion failed");
                entrance->setEntranceType(exit ? EntranceType::rideExit : EntranceType::rideEntrance);
                entrance->setRideIndex(ride.id);
                entrance->setStationIndex(StationIndex::FromUnderlying(0));
                entrance->setDirection(0);
                entrance->setClearanceZ(128);
            }
            manifest["objects"].push_back({ { "kind", spec.stationOnly ? "station" : "staticRide" },
                                            { "style", spec.style },
                                            { "ride", i },
                                            { "x", x },
                                            { "y", y },
                                            { "object", spec.object },
                                            { "station", spec.station },
                                            { "trackType", static_cast<uint16_t>(spec.piece) },
                                            { "sequences", sequence.numSequences } });
        }
        state.ridesEndOfUsedRange = static_cast<uint16_t>(specimens.size());
        for (ObjectEntryIndex slot = 0; slot < kMaxParkEntranceObjects; ++slot)
            if (manager.GetLoadedObject<EntranceObject>(slot))
            {
                for (int i = 0; i < 3; ++i)
                {
                    const int dy = i == 1 ? -1 : (i == 2 ? 1 : 0);
                    auto* entrance = TileElementInsert<EntranceElement>({ 36 * 32, (54 + dy) * 32, 64 }, 15);
                    Require(entrance != nullptr, "Park portal insertion failed");
                    entrance->setEntranceType(EntranceType::parkEntrance);
                    entrance->setEntryIndex(slot);
                    entrance->setSequenceIndex(static_cast<ParkEntranceSequence>(i));
                    entrance->setDirection(0);
                    entrance->setClearanceZ(160);
                    entrance->setSurfaceEntryIndex(kObjectEntryIndexNull);
                }
                manifest["objects"].push_back({ { "kind", "parkEntrance" }, { "x", 36 }, { "y", 54 }, { "slot", slot } });
                break;
            }
        manifest["fixture"] = "original-static-buildings-v1";
        manifest["scope"] = "closed static flat rides, shops, facilities and portals; no vehicles/riders; eight original-art "
                            "views";
    }
} // namespace

int main(int argc, char** argv)
{
    const bool animated = argc >= 4 && std::string_view(argv[3]) == "--animated-buildings";
    if ((argc != 3 && argc != 4 && !(argc == 5 && animated))
        || (argc >= 4 && !animated && std::string_view(argv[3]) != "--static-buildings"
            && std::string_view(argv[3]) != "--track-specials" && std::string_view(argv[3]) != "--track-regressions"
            && std::string_view(argv[3]) != "--track-regressions-opaque"
            && std::string_view(argv[3]) != "--track-regressions-inside" && std::string_view(argv[3]) != "--underground"
            && std::string_view(argv[3]) != "--construction-overlays" && std::string_view(argv[3]) != "--underground-view"
            && std::string_view(argv[3]) != "--underground-view-control" && std::string_view(argv[3]) != "--photo-states"))
    {
        std::cerr << "Usage: object-fixture <new-output-directory> <seed-park> "
                     "[--static-buildings|--track-specials|--track-regressions|--track-regressions-opaque|--track-regressions-"
                     "inside|--underground|--underground-view|--underground-"
                     "view-control|--"
                     "construction-overlays|--photo-states|--animated-buildings [0|1|2]]\n";
        return EXIT_FAILURE;
    }
    int posePhase = 0;
    if (argc == 5)
    {
        const std::string_view value(argv[4]);
        if (value != "0" && value != "1" && value != "2")
        {
            std::cerr << "Animated pose must be0,1 or2\n";
            return EXIT_FAILURE;
        }
        posePhase = value[0] - '0';
    }
    const fs::path output = fs::absolute(argv[1]);
    if (fs::exists(output))
    {
        std::cerr << "Output already exists\n";
        return EXIT_FAILURE;
    }
    fs::create_directories(output);
    const bool buildings = animated || (argc == 4 && std::string_view(argv[3]) == "--static-buildings");
    const bool regressionOpaque = argc == 4 && std::string_view(argv[3]) == "--track-regressions-opaque";
    const bool regressionInside = argc == 4 && std::string_view(argv[3]) == "--track-regressions-inside";
    const bool regressions = regressionOpaque || regressionInside
        || (argc == 4 && std::string_view(argv[3]) == "--track-regressions");
    const bool specials = regressions || (argc == 4 && std::string_view(argv[3]) == "--track-specials");
    const bool undergroundView = argc == 4 && std::string_view(argv[3]) == "--underground-view";
    const bool undergroundControl = argc == 4 && std::string_view(argv[3]) == "--underground-view-control";
    const bool underground = undergroundView || undergroundControl
        || (argc == 4 && std::string_view(argv[3]) == "--underground");
    const bool overlays = argc == 4 && std::string_view(argv[3]) == "--construction-overlays";
    const bool photos = argc == 4 && std::string_view(argv[3]) == "--photo-states";
    const bool largeFixture = buildings || specials || underground || overlays || photos;
    json_t manifest = {
        { "schema", 1 },
        { "fixture", "original-world-object-art-v1" },
        { "status", "incomplete" },
        { "cases", json_t::array() },
        { "objects", json_t::array() },
        { "noSimulationTicks", true },
        { "renderer", "pristine-upstream-software" },
        { "landscapeSmoothing", false },
        { "scope",
          "original small/large scenery, walls, empty-text banners, flat rails behind trees, ghosts; no simulation ticks" }
    };
    try
    {
        gCustomUserDataPath = Environment("OPENRCT2_ORACLE_USER_PATH");
        gCustomOpenRCT2DataPath = Environment("OPENRCT2_ORACLE_DATA_PATH");
        gCustomRCT1DataPath = Environment("OPENRCT2_ORACLE_RCT1_PATH");
        gCustomRCT2DataPath = Environment("OPENRCT2_ORACLE_RCT2_PATH");
        fs::create_directories(gCustomUserDataPath);
        // Environment creation loads/resets Config; set and save explicit paths afterwards.
        auto environment = CreatePlatformEnvironment();
        Require(Config::SetDefaults(), "Could not set defaults");
        Config::Get().general.rct1Path = gCustomRCT1DataPath;
        Config::Get().general.rct2Path = gCustomRCT2DataPath;
        Config::Get().general.language = LANGUAGE_ENGLISH_UK;
        Config::Get().general.dayNightCycle = false;
        Config::Get().general.enableLightFx = false;
        Config::Get().general.landscapeSmoothing = false;
        if (regressions)
        {
            Config::Get().general.transparentWater = !regressionOpaque;
            manifest["transparentWater"] = !regressionOpaque;
        }
        Require(Config::SaveToPath(environment->GetFilePath(PathId::config)), "Could not seed isolated configuration");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        auto context = CreateContext();
        Require(context->Initialise() && context->LoadParkFromFile(argv[2]), "Could not initialise/load seed park");
        gLegacyScene = LegacyScene::playing;
        auto& manager = context->GetObjectManager();
        auto& state = getGameState();
        manifest["objectSources"] = ObjectSources(*context);
        if (photos)
            PhotoStates(*context, manifest);
        else if (overlays)
            ConstructionOverlays(*context, manifest);
        else if (undergroundView || undergroundControl)
        {
            UndergroundView(*context, manifest);
            manifest["viewMode"] = undergroundView ? "underground-inside" : "normal-control";
        }
        else if (underground)
            Underground(*context, manifest);
        else if (specials)
            TrackSpecials(*context, manifest, regressions);
        else if (buildings)
            StaticBuildings(*context, manifest);
        else
        {
            ObjectEntryIndex treeSlot = kObjectEntryIndexNull, largeSlot = kObjectEntryIndexNull,
                             wallSlot = kObjectEntryIndexNull, bannerSlot = kObjectEntryIndexNull;
            for (ObjectEntryIndex i = 0; i < kMaxSmallSceneryObjects; ++i)
                if (auto* object = manager.GetLoadedObject<SmallSceneryObject>(i))
                {
                    const auto& entry = *static_cast<const SmallSceneryEntry*>(object->GetLegacyData());
                    if (entry.flags.has(SmallSceneryFlag::isTree) && !entry.flags.has(SmallSceneryFlag::isAnimated))
                    {
                        treeSlot = i;
                        break;
                    }
                }
            for (ObjectEntryIndex i = 0; i < kMaxLargeSceneryObjects; ++i)
                if (auto* object = manager.GetLoadedObject<LargeSceneryObject>(i))
                {
                    const auto& entry = *static_cast<const LargeSceneryEntry*>(object->GetLegacyData());
                    if (!entry.flags.has(LargeSceneryFlag::is3DText) && !entry.tiles.empty() && entry.tiles.size() <= 8
                        && std::all_of(entry.tiles.begin(), entry.tiles.end(), [](auto& tile) {
                               return std::abs(tile.offset.x) <= 64 && std::abs(tile.offset.y) <= 64;
                           }))
                    {
                        largeSlot = i;
                        break;
                    }
                }
            for (ObjectEntryIndex i = 0; i < kMaxWallSceneryObjects; ++i)
                if (auto* object = manager.GetLoadedObject<WallObject>(i))
                {
                    const auto& entry = *static_cast<const WallSceneryEntry*>(object->GetLegacyData());
                    if (entry.scrolling_mode == 255)
                    {
                        wallSlot = i;
                        break;
                    }
                }
            for (ObjectEntryIndex i = 0; i < kMaxBannerObjects; ++i)
                if (manager.GetLoadedObject<BannerObject>(i) != nullptr)
                {
                    bannerSlot = i;
                    break;
                }
            Require(
                treeSlot != kObjectEntryIndexNull && largeSlot != kObjectEntryIndexNull && wallSlot != kObjectEntryIndexNull
                    && bannerSlot != kObjectEntryIndexNull,
                "Missing required scenery families");
            ride_type_t rideType = kRideTypeNull;
            ObjectEntryIndex rideSlot = kObjectEntryIndexNull;
            for (const auto& ride : RideManager(getGameState()))
                if (getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle == TrackStyle::loopingRollerCoaster)
                {
                    rideType = ride.type;
                    rideSlot = ride.subtype;
                    break;
                }
            Require(rideType != kRideTypeNull, "Seed park lacks looping-coaster original track object");
            const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
            const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
            Require(grass != kObjectEntryIndexNull && rock != kObjectEntryIndexNull, "Seed park lacks grass/rock");
            manifest["objectSources"] = ObjectSources(*context);
            manifest["materials"] = { { "tree", treeSlot },     { "large", largeSlot },   { "wall", wallSlot },
                                      { "banner", bannerSlot }, { "rideType", rideType }, { "rideSlot", rideSlot },
                                      { "grass", grass },       { "rock", rock } };
            ObjectEntryIndex pathSurface = kObjectEntryIndexNull, pathRailings = kObjectEntryIndexNull;
            std::array<ObjectEntryIndex, 3> additions{ kObjectEntryIndexNull, kObjectEntryIndexNull, kObjectEntryIndexNull };
            for (ObjectEntryIndex i = 0; i < 255; ++i)
            {
                if (auto* surface = manager.GetLoadedObject<FootpathSurfaceObject>(i); surface != nullptr
                    && !(surface->Flags & FOOTPATH_ENTRY_FLAG_IS_QUEUE)
                    && !(surface->Flags & FOOTPATH_ENTRY_FLAG_SHOW_ONLY_IN_SCENARIO_EDITOR)
                    && pathSurface == kObjectEntryIndexNull)
                    pathSurface = i;
                if (pathRailings == kObjectEntryIndexNull && manager.GetLoadedObject<FootpathRailingsObject>(i) != nullptr)
                    pathRailings = i;
                if (auto* object = manager.GetLoadedObject<PathAdditionObject>(i))
                {
                    const auto* entry = static_cast<const PathAdditionEntry*>(object->GetLegacyData());
                    const auto type = static_cast<size_t>(entry->draw_type);
                    if (type < additions.size() && additions[type] == kObjectEntryIndexNull)
                        additions[type] = i;
                }
            }
            Require(
                pathSurface != kObjectEntryIndexNull && pathRailings != kObjectEntryIndexNull
                    && std::all_of(additions.begin(), additions.end(), [](auto slot) { return slot != kObjectEntryIndexNull; }),
                "Seed park lacks original lamps/bins/benches path materials");
            manifest["materials"]["pathSurface"] = pathSurface;
            manifest["materials"]["pathRailings"] = pathRailings;
            manifest["materials"]["additions"] = additions;
            gameStateInitAll(state, TileCoordsXY{ 32, 32 });
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++)
                {
                    auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
                    Require(surface != nullptr, "Missing reset surface");
                    surface->setBaseZ(64);
                    surface->setClearanceZ(64);
                    surface->setSlope(0);
                    surface->setWaterHeight(0);
                    surface->setGrassLength(0);
                    surface->setOwnership(kUnowned);
                    surface->setParkFences(0);
                    surface->setSurfaceObjectIndex(grass);
                    surface->setEdgeObjectIndex(rock);
                }
            auto& ride = state.rides[0];
            ride.id = RideId::FromUnderlying(0);
            ride.type = rideType;
            ride.subtype = rideSlot;
            ride.status = RideStatus::closed;
            state.ridesEndOfUsedRange = 1;
            ride.customName = "Static original track sample";
            for (auto& colour : ride.trackColours)
                colour = { Colour::brightRed, Colour::yellow, Colour::white };
            const auto addTree = [&](int x, int y, bool ghost) {
                auto* tree = TileElementInsert<SmallSceneryElement>({ x * 32, y * 32, 64 }, 15);
                Require(tree != nullptr, "Tree insertion failed");
                tree->setEntryIndex(treeSlot);
                tree->setGhost(ghost);
                const auto* entry = tree->getEntry();
                Require(entry != nullptr, "Tree metadata missing");
                tree->setClearanceZ(64 + entry->height);
                tree->setAge(0);
                tree->setSceneryQuadrant(0);
                tree->setPrimaryColour(Colour::brightRed);
                tree->setSecondaryColour(Colour::yellow);
                tree->setTertiaryColour(Colour::white);
                manifest["objects"].push_back(
                    { { "kind", "small" }, { "x", x }, { "y", y }, { "slot", treeSlot }, { "ghost", ghost } });
            };
            addTree(9, 9, false);
            addTree(12, 9, true);
            const auto& large = *static_cast<const LargeSceneryEntry*>(
                manager.GetLoadedObject<LargeSceneryObject>(largeSlot)->GetLegacyData());
            for (int ghost = 0; ghost < 2; ++ghost)
                for (size_t sequence = 0; sequence < large.tiles.size(); ++sequence)
                {
                    const auto& tile = large.tiles[sequence];
                    const CoordsXYZ position{ (18 + ghost * 6) * 32 + tile.offset.x, 9 * 32 + tile.offset.y,
                                              64 + tile.offset.z };
                    auto* element = TileElementInsert<LargeSceneryElement>(position, 15);
                    Require(element != nullptr, "Large scenery insertion failed");
                    element->setEntryIndex(largeSlot);
                    element->setSequenceIndex(static_cast<uint8_t>(sequence));
                    element->setDirection(0);
                    element->setClearanceZ(position.z + tile.zClearance);
                    element->setGhost(ghost != 0);
                    element->setPrimaryColour(Colour::brightRed);
                    element->setSecondaryColour(Colour::yellow);
                    element->setTertiaryColour(Colour::white);
                    element->setBannerIndex(BannerIndex::GetNull());
                    manifest["objects"].push_back({ { "kind", "large" },
                                                    { "x", position.x / 32 },
                                                    { "y", position.y / 32 },
                                                    { "baseZ", position.z },
                                                    { "slot", largeSlot },
                                                    { "sequence", sequence },
                                                    { "ghost", ghost != 0 } });
                }
            for (uint8_t direction = 0; direction < 4; ++direction)
            {
                const int x = 8 + direction * 4;
                auto* wall = TileElementInsert<WallElement>({ x * 32, 15 * 32, 64 }, 15);
                Require(wall != nullptr, "Wall insertion failed");
                wall->setEntryIndex(wallSlot);
                wall->setDirection(direction);
                wall->setSlope(0);
                wall->setGhost(direction == 3);
                wall->setClearanceZ(64 + wall->getEntry()->height * 8);
                wall->setBannerIndex(BannerIndex::GetNull());
                wall->setPrimaryColour(Colour::brightRed);
                wall->setSecondaryColour(Colour::yellow);
                wall->setTertiaryColour(Colour::white);
                manifest["objects"].push_back(
                    { { "kind", "wall" }, { "x", x }, { "y", 15 }, { "direction", direction }, { "ghost", direction == 3 } });
                auto* banner = CreateBanner();
                Require(banner != nullptr, "Banner allocation failed");
                banner->type = bannerSlot;
                banner->position = { x, 19 };
                banner->colour = Colour::brightRed;
                banner->text.clear(); // Upstream displays default "Sign" text; native dynamic glyphs remain outside scope.
                auto* element = TileElementInsert<BannerElement>({ x * 32, 19 * 32, 64 }, 15);
                Require(element != nullptr, "Banner insertion failed");
                element->setIndex(banner->id);
                element->setPosition(direction);
                element->setAllowedEdges(15);
                element->setClearanceZ(96);
                element->setGhost(direction == 3);
                manifest["objects"].push_back(
                    { { "kind", "banner" }, { "x", x }, { "y", 19 }, { "position", direction }, { "ghost", direction == 3 } });
            }
            for (int x = 9; x <= 21; ++x)
            {
                auto* track = TileElementInsert<TrackElement>({ x * 32, 24 * 32, 64 }, 15);
                Require(track != nullptr, "Track insertion failed");
                track->setRideIndex(ride.id);
                track->setRideType(rideType);
                track->setTrackType(TrackElemType::flat);
                track->setDirection(0);
                track->setSequenceIndex(0);
                track->setHasChain(x >= 16 && x <= 18);
                track->setClearanceZ(96);
                track->setColourScheme(RideColourScheme::main);
                track->setGhost(x >= 19);
                manifest["objects"].push_back(
                    { { "kind", "track" }, { "x", x }, { "y", 24 }, { "chain", x >= 16 && x <= 18 }, { "ghost", x >= 19 } });
            }
            addTree(13, 24, false);
            addTree(16, 23, false);
            addTree(20, 24, true);
            gRealTimeOfDay = { 0, 23, 7 };
            manifest["clockHour"] = 7;
            manifest["clockMinute"] = 23;
            for (int type = 0; type < 3; ++type)
                for (int condition = 0; condition < 3; ++condition)
                {
                    const int x = 4 + (type * 3 + condition) * 3;
                    auto* path = TileElementInsert<PathElement>({ x * 32, 28 * 32, 64 }, 15);
                    Require(path != nullptr, "Path addition insertion failed");
                    path->setClearanceZ(80);
                    path->setSurfaceEntryIndex(pathSurface);
                    path->setRailingsEntryIndex(pathRailings);
                    path->setEdgesAndCorners(0);
                    path->setAdditionEntryIndex(additions[type]);
                    path->setAdditionStatus(condition == 1 ? 0 : 255);
                    path->setIsBroken(condition == 2);
                    manifest["objects"].push_back({ { "kind", "pathAddition" },
                                                    { "x", x },
                                                    { "y", 28 },
                                                    { "additionType", type },
                                                    { "condition", condition },
                                                    { "additionSlot", additions[type] },
                                                    { "additionStatus", path->getAdditionStatus() },
                                                    { "broken", condition == 2 } });
                }
        }
        if (animated)
            AnimatedPoses(manifest, posePhase);
        gRealTimeOfDay = { 0, 23, 7 };
        manifest["clockHour"] = 7;
        manifest["clockMinute"] = 23;
        EntityTweener::get().reset();
        MapAnimations::ClearAll();
        state.entities.resetEntitySpatialIndices();
        state.currentTicks = 0;
        state.date = {};
        state.scenarioRand.seed(1);
        state.weatherCurrent = { Weather::Type::sunny, 20, Weather::EffectType::none, 0, Weather::Level::none };
        state.weatherNext = state.weatherCurrent;
        state.weatherUpdateTimer = 1920;
        state.park.name = "Original world object art v1";
        state.savedView = { 0, 448 };
        state.savedViewZoom = ZoomLevel{ 0 };
        state.savedViewRotation = 0;
        ParkFileExporter exporter;
        exporter.ExportObjectsList = manager.GetPackableObjects();
        // The ordinary park writer deliberately drops ghosts. Preserve the instances in the saved
        // fixture, and give the candidate loader exact identity-checked flag patches before capture.
        std::vector<TileElement*> ghosts;
        manifest["ghostPatches"] = json_t::array();
        for (int y = 0; y < (regressions ? 72 : (largeFixture ? 64 : 32)); ++y)
            for (int x = 0; x < (regressions ? 72 : (largeFixture ? 64 : 32)); ++x)
            {
                auto* element = MapGetFirstElementAt(TileCoordsXY{ x, y });
                if (element == nullptr)
                    continue;
                uint32_t ordinal = 0;
                do
                {
                    if (element->isGhost())
                    {
                        manifest["ghostPatches"].push_back({ { "x", x },
                                                             { "y", y },
                                                             { "elementOrdinal", ordinal },
                                                             { "type", static_cast<uint8_t>(element->getType()) },
                                                             { "baseZ", element->getBaseZ() },
                                                             { "clearanceZ", element->getClearanceZ() } });
                        ghosts.push_back(element);
                        element->setGhost(false);
                    }
                    ++ordinal;
                } while (!(element++)->isLastForTile());
            }
        exporter.Export(state, (output / "objects.park").string(), kParkFileSaveCompressionLevel);
        for (auto* element : ghosts)
            element->setGhost(true);
        manifest["ghostSavePolicy"] = "instances saved without ghost bit; apply all identity-checked ghostPatches before "
                                      "native capture";
        LoadPalette();
        Require(
            std::any_of(gPalette.begin(), gPalette.end(), [](auto c) { return c.red || c.green || c.blue; }), "Empty palette");
        Bytes(output / "palette.bgra", gPalette.data(), sizeof(gPalette));
        manifest["palette"] = "palette.bgra";
        X8DrawingEngine engine(context->GetUiContext());
        for (int zoom = 0; zoom <= 1; zoom++)
            for (uint8_t rotation = 0; rotation < 4; rotation++)
            {
                const std::string name = "objects-r" + std::to_string(rotation) + "-z" + std::to_string(zoom);
                Viewport viewport{};
                viewport.width = regressions ? 4096 : (largeFixture ? 3840 : 1664);
                viewport.height = regressions ? 2304 : (largeFixture ? 2160 : 1024);
                viewport.zoom = ZoomLevel{ static_cast<int8_t>(zoom) };
                viewport.rotation = rotation;
                if (undergroundView || regressionInside)
                    viewport.flags.set(ViewportFlag::undergroundInside);
                const auto centre = Translate3DTo2DWithZ(
                    rotation,
                    (specials || underground || overlays || photos)
                        ? CoordsXYZ{ 1024, 1024, 128 }
                        : (buildings ? CoordsXYZ{ 896, 896, 64 } : CoordsXYZ{ 512, 512, 64 }));
                viewport.viewPos = { centre.x - viewport.ViewWidth() / 2, centre.y - viewport.ViewHeight() / 2 };
                std::vector<PaletteIndex> pixels(static_cast<size_t>(viewport.width) * viewport.height);
                RenderTarget target{};
                target.bits = pixels.data();
                target.width = viewport.width;
                target.height = viewport.height;
                target.DrawingEngine = &engine;
                json_t selection;
                if (overlays)
                    selection = ApplyConstructionSelection(rotation, zoom);
                ResetAllSpriteQuadrantPlacements();
                engine.BeginDraw();
                ViewportRender(target, &viewport);
                engine.EndDraw();
                const auto raw = name + ".indexed";
                const auto png = name + ".png";
                Bytes(output / raw, pixels.data(), pixels.size());
                Image image;
                image.Width = static_cast<uint32_t>(viewport.width);
                image.Height = static_cast<uint32_t>(viewport.height);
                image.Depth = 8;
                image.Stride = image.Width;
                image.Palette = gPalette;
                const auto* bytes = reinterpret_cast<const uint8_t*>(pixels.data());
                image.Pixels.assign(bytes, bytes + pixels.size());
                Imaging::WriteToFile((output / png).string(), image, ImageFormat::png);
                manifest["cases"].push_back({ { "name", name },
                                              { "park", "objects.park" },
                                              { "width", viewport.width },
                                              { "height", viewport.height },
                                              { "rotation", rotation },
                                              { "zoom", zoom },
                                              { "viewFlags", viewport.flags.holder },
                                              { "viewX", viewport.zoom.ApplyInversedTo(viewport.viewPos.x) },
                                              { "viewY", viewport.zoom.ApplyInversedTo(viewport.viewPos.y) },
                                              { "viewPosition", { viewport.viewPos.x, viewport.viewPos.y } },
                                              { "referenceIndexed", raw },
                                              { "referencePng", png } });
                if (regressions)
                    manifest["cases"].back()["transparentWater"] = !regressionOpaque;
                if (overlays)
                {
                    manifest["cases"].back()["selection"] = std::move(selection);
                    manifest["cases"].back()["viewFlags"] = 0;
                }
            }
        Require(state.currentTicks == 0, "Reference rendering advanced simulation");
        manifest["status"] = "pass";
    }
    catch (const std::exception& error)
    {
        manifest["status"] = "fail";
        manifest["error"] = error.what();
        std::cerr << error.what() << '\n';
    }
    std::ofstream report(output / "manifest.json");
    report << manifest.dump(2) << '\n';
    return manifest["status"] == "pass" && report.good() ? EXIT_SUCCESS : EXIT_FAILURE;
}
