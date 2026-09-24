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
#include <openrct2/ride/ted/TrackElemType.h>
#include <openrct2/ride/ted/TrackElementDescriptor.h>
#include <openrct2/world/Banner.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapAnimation.h>
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

    void TrackSpecials(IContext& context, json_t& manifest)
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
            bool chain{}, station{};
        };
        auto& state = getGameState();
        auto& manager = context.GetObjectManager();
        std::vector<SeedRide> seeds;
        for (int style : { 39, 27, 1, 56, 65, 3, 69, 31, 78 })
        {
            bool found = false;
            for (const auto& ride : RideManager(state))
                if (static_cast<int>(getTrackDrawerEntry(GetRideTypeDescriptor(ride.type)).trackStyle) == style)
                {
                    seeds.push_back({ style, ride.type, ride.subtype, ride.entranceStyle });
                    found = true;
                    break;
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
        Require(specimens.size() <= 49, "Track-specials grid capacity exceeded");
        const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        Require(grass != kObjectEntryIndexNull && rock != kObjectEntryIndexNull, "Seed lacks grass/rock");
        gameStateInitAll(state, TileCoordsXY{ 64, 64 });
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
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
        manifest["grid"] = { { "mapSize", 64 }, { "columns", 7 }, { "spacing", 8 }, { "count", specimens.size() } };
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
            const int cellX = 3 + static_cast<int>(id % 7) * 8, cellY = 3 + static_cast<int>(id / 7) * 8;
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
            manifest["objects"].push_back({ { "kind", spec.station ? "station" : "trackSpecial" },
                                            { "specimen", id },
                                            { "ride", id },
                                            { "style", spec.seed.style },
                                            { "rideType", spec.seed.type },
                                            { "object", spec.seed.object },
                                            { "station", spec.seed.station },
                                            { "trackType", static_cast<uint16_t>(spec.type) },
                                            { "chain", spec.chain },
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
        manifest["fixture"] = "original-track-specials-v1";
        manifest["scope"] = "Looping quarter-helix types102..109; six narrow/pier station families; Junior/Water flat, chain, "
                            "slopes, curves, S-bends, steep transitions and stations; no vehicles or simulation ticks";
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
    if (argc != 3
        && (argc != 4
            || (std::string_view(argv[3]) != "--static-buildings" && std::string_view(argv[3]) != "--track-specials"
                && std::string_view(argv[3]) != "--underground")))
    {
        std::cerr
            << "Usage: object-fixture <new-output-directory> <seed-park> [--static-buildings|--track-specials|--underground]\n";
        return EXIT_FAILURE;
    }
    const fs::path output = fs::absolute(argv[1]);
    if (fs::exists(output))
    {
        std::cerr << "Output already exists\n";
        return EXIT_FAILURE;
    }
    fs::create_directories(output);
    const bool buildings = argc == 4 && std::string_view(argv[3]) == "--static-buildings";
    const bool specials = argc == 4 && std::string_view(argv[3]) == "--track-specials";
    const bool underground = argc == 4 && std::string_view(argv[3]) == "--underground";
    const bool largeFixture = buildings || specials || underground;
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
        Require(Config::SaveToPath(environment->GetFilePath(PathId::config)), "Could not seed isolated configuration");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        auto context = CreateContext();
        Require(context->Initialise() && context->LoadParkFromFile(argv[2]), "Could not initialise/load seed park");
        gLegacyScene = LegacyScene::playing;
        auto& manager = context->GetObjectManager();
        auto& state = getGameState();
        manifest["objectSources"] = ObjectSources(*context);
        if (underground)
            Underground(*context, manifest);
        else if (specials)
            TrackSpecials(*context, manifest);
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
        for (int y = 0; y < (largeFixture ? 64 : 32); ++y)
            for (int x = 0; x < (largeFixture ? 64 : 32); ++x)
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
                viewport.width = largeFixture ? 3840 : 1664;
                viewport.height = largeFixture ? 2160 : 1024;
                viewport.zoom = ZoomLevel{ static_cast<int8_t>(zoom) };
                viewport.rotation = rotation;
                const auto centre = Translate3DTo2DWithZ(
                    rotation,
                    (specials || underground) ? CoordsXYZ{ 1024, 1024, 128 }
                                              : (buildings ? CoordsXYZ{ 896, 896, 64 } : CoordsXYZ{ 512, 512, 64 }));
                viewport.viewPos = { centre.x - viewport.ViewWidth() / 2, centre.y - viewport.ViewHeight() / 2 };
                std::vector<PaletteIndex> pixels(static_cast<size_t>(viewport.width) * viewport.height);
                RenderTarget target{};
                target.bits = pixels.data();
                target.width = viewport.width;
                target.height = viewport.height;
                target.DrawingEngine = &engine;
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
                                              { "viewX", viewport.zoom.ApplyInversedTo(viewport.viewPos.x) },
                                              { "viewY", viewport.zoom.ApplyInversedTo(viewport.viewPos.y) },
                                              { "viewPosition", { viewport.viewPos.x, viewport.viewPos.y } },
                                              { "referenceIndexed", raw },
                                              { "referencePng", png } });
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
