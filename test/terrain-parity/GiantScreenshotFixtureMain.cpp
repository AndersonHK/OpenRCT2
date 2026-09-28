/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Public-API-only fixture preparation. Compile the identical file against both
// cores; only the frozen executable may prepare, and neither mode advances ticks.
#include "../ui-parity/BalloonFixtureState.h"
#include "GiantScreenshotRecipe.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/localisation/Language.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/ObjectRepository.h>
#include <openrct2/object/TerrainEdgeObject.h>
#include <openrct2/object/TerrainSurfaceObject.h>
#include <openrct2/park/ParkFile.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <sstream>
#include <stdexcept>

using namespace OpenRCT2;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    json_t Objects(IContext& context, json_t& sources)
    {
        json_t result = json_t::array();
        auto& manager = context.GetObjectManager();
        const auto loaded = manager.GetLoadedObjects();
        for (const auto type : getAllObjectTypes())
        {
            const auto& list = loaded.GetList(type);
            json_t slots = json_t::array();
            for (size_t slot = 0; slot < list.size(); slot++)
            {
                const auto& entry = list[slot];
                if (!entry.HasValue())
                {
                    slots.push_back(nullptr);
                    continue;
                }
                Require(manager.GetLoadedObject(type, slot) != nullptr, "Object slot is not loaded");
                const auto* item = context.GetObjectRepository().FindObject(entry);
                Require(item != nullptr, "Object repository entry is missing");
                slots.push_back(
                    { { "identifier", item->Identifier },
                      { "version", { std::get<0>(item->Version), std::get<1>(item->Version), std::get<2>(item->Version) } } });
                sources.push_back({ { "type", EnumValue(type) },
                                    { "slot", slot },
                                    { "identifier", item->Identifier },
                                    { "path", item->Path } });
            }
            result.push_back(
                { { "type", EnumValue(type) }, { "transient", ObjectTypeIsTransient(type) }, { "slots", std::move(slots) } });
        }
        return result;
    }

    ObjectEntryIndex TerrainSlot(IObjectManager& manager, std::string_view identifier, ObjectType type)
    {
        const auto slot = manager.GetLoadedObjectEntryIndex(identifier);
        Require(slot != kObjectEntryIndexNull, "Required terrain identifier is absent");
        Require(manager.GetLoadedObject(type, slot) != nullptr, "Required terrain slot has wrong type");
        return slot;
    }

    json_t WeatherCensus(const Weather::State& weather)
    {
        return { { "type", EnumValue(weather.weatherType) },
                 { "temperature", weather.temperature },
                 { "effect", EnumValue(weather.weatherEffect) },
                 { "gloom", weather.weatherGloom },
                 { "level", EnumValue(weather.level) } };
    }

    struct TerrainMaterials
    {
        std::array<ObjectEntryIndex, 2> surfaces;
        std::array<ObjectEntryIndex, 2> edges;
    };

    TerrainMaterials ResolveTerrainMaterials(IObjectManager& manager, bool nonuniform)
    {
        const auto grass = TerrainSlot(manager, "rct2.terrain_surface.grass", ObjectType::terrainSurface);
        const auto rock = TerrainSlot(manager, "rct2.terrain_edge.rock", ObjectType::terrainEdge);
        TerrainMaterials result{ { grass, grass }, { rock, rock } };
        if (nonuniform)
        {
            for (size_t i = 0; i < result.surfaces.size(); i++)
            {
                result.surfaces[i] = TerrainSlot(
                    manager, GiantScreenshotFixture::kSurfaceIdentifiers[i], ObjectType::terrainSurface);
                result.edges[i] = TerrainSlot(manager, GiantScreenshotFixture::kEdgeIdentifiers[i], ObjectType::terrainEdge);
            }
        }
        return result;
    }

    std::string RawElementBytes(const TileElement& element)
    {
        constexpr char digits[] = "0123456789abcdef";
        const auto* bytes = reinterpret_cast<const uint8_t*>(&element);
        std::string result;
        result.reserve(sizeof(element) * 2);
        for (size_t i = 0; i < sizeof(element); i++)
        {
            result.push_back(digits[bytes[i] >> 4]);
            result.push_back(digits[bytes[i] & 15]);
        }
        return result;
    }

    json_t Census(IContext& context, json_t& sources, bool balloons, bool legitimateBalloons, bool nonuniform)
    {
        auto& state = getGameState();
        Require(state.mapSize == TileCoordsXY{ 96, 96 }, "Declared map must be 96 by 96");
        const auto materials = ResolveTerrainMaterials(context.GetObjectManager(), nonuniform);
        const auto recipe = GiantScreenshotFixture::MakeTiles();
        json_t tiles = json_t::array();
        for (int32_t y = 0; y < 96; y++)
        {
            for (int32_t x = 0; x < 96; x++)
            {
                const auto* element = MapGetFirstElementAt(TileCoordsXY{ x, y });
                Require(element && element->isLastForTile(), "Each declared coordinate must have exactly one element");
                const auto* surface = element->asSurface();
                Require(surface && !surface->isGhost() && !surface->isInvisible(), "Surface must be visible and non-ghost");
                const auto expected = nonuniform ? recipe[y * 96 + x]
                                                 : GiantScreenshotFixture::Tile{ .grassLength = GRASS_LENGTH_CLEAR_0 };
                Require(
                    surface->getBaseZ() == expected.baseZ && surface->getClearanceZ() == expected.baseZ,
                    "Terrain elevation/clearance differs from immutable recipe");
                Require(
                    surface->getSlope() == expected.slope && surface->getWaterHeight() == expected.water,
                    "Terrain slope/water differs from immutable recipe");
                Require(
                    surface->getGrassLength() == expected.grassLength && surface->getOwnership().isEmpty()
                        && surface->getParkFences() == 0,
                    "Unexpected grass/ownership/fences");
                Require(
                    surface->getSurfaceObjectIndex() == materials.surfaces[expected.surfaceVariant]
                        && surface->getEdgeObjectIndex() == materials.edges[expected.edgeVariant],
                    "Unexpected terrain material");
                tiles.push_back({ { "x", x },
                                  { "y", y },
                                  { "baseZ", surface->getBaseZ() },
                                  { "clearanceZ", surface->getClearanceZ() },
                                  { "slope", surface->getSlope() },
                                  { "water", surface->getWaterHeight() },
                                  { "grass", surface->getGrassLength() },
                                  { "ownership", surface->getOwnership().holder },
                                  { "fences", surface->getParkFences() },
                                  { "surfaceSlot", surface->getSurfaceObjectIndex() },
                                  { "edgeSlot", surface->getEdgeObjectIndex() } });
                if (nonuniform)
                    tiles.back()["rawElementBytes"] = RawElementBytes(*element);
            }
        }
        std::array<uint32_t, EnumValue(EntityType::count)> counts{};
        for (uint32_t slot = 0; slot < kMaxEntities; slot++)
        {
            const auto* entity = state.entities.tryGetEntity(EntityId::FromUnderlying(static_cast<uint16_t>(slot)));
            if (entity)
            {
                Require(EnumValue(entity->type) < counts.size(), "Invalid entity type");
                counts[EnumValue(entity->type)]++;
                if (legitimateBalloons && entity->type == EntityType::balloon)
                {
                    const auto* balloon = entity->cast<Balloon>();
                    Require(slot < GiantScreenshotFixture::kBalloonCount, "Giant balloon ID is outside recipe");
                    const auto expected = GiantScreenshotFixture::BalloonAt(slot);
                    Require(balloon->x == expected.x && balloon->y == expected.y && balloon->z == expected.z
                        && balloon->frame == expected.frame && balloon->popped == expected.popped
                        && balloon->timeToMove == expected.timeToMove
                        && static_cast<uint8_t>(balloon->colour) == expected.colour, "Giant balloon recipe differs");
                    Require(
                        balloon->popped <= 1 && (balloon->popped == 0 || balloon->frame < 5),
                        "Legitimate balloon fixture contains an unsupported popped frame");
                }
            }
        }
        const uint32_t expectedBalloons = GiantScreenshotFixture::kBalloonCount;
        for (size_t type = 0; type < counts.size(); type++)
            Require(
                counts[type] == (type == EnumValue(EntityType::balloon) ? expectedBalloons : 0u), "Unexpected entity census");
        Require(state.entities.getNumFreeEntities() == kMaxEntities - expectedBalloons, "Entity free-slot census disagrees");
        size_t spatialEntries = 0, nonemptyBuckets = 0;
        for (int32_t y = 0; y < kMaximumMapSizeTechnical; y++)
            for (int32_t x = 0; x < kMaximumMapSizeTechnical; x++)
            {
                const auto& bucket = state.entities.getEntityTileList(CoordsXY{ x * 32, y * 32 });
                nonemptyBuckets += !bucket.empty();
                spatialEntries += bucket.size();
                for (const auto id : bucket)
                {
                    const auto* entity = state.entities.tryGetEntity(id);
                    Require(
                        entity && entity->type == EntityType::balloon && entity->x / 32 == x && entity->y / 32 == y,
                        "Spatial balloon bucket disagrees with coordinates");
                }
            }
        Require(spatialEntries == expectedBalloons, "Spatial balloon population disagrees");
        CoordsXY nullPosition{};
        nullPosition.setNull();
        Require(state.entities.getEntityTileList(nullPosition).empty(), "Nonempty null spatial bucket");
        for (const auto& ride : state.rides)
            Require(ride.id == RideId::GetNull(), "Fixture contains a ride");
        Require(state.peepSpawns.empty() && state.park.entrances.empty(), "Fixture contains spawn/entrance records");
        Require(
            state.currentTicks == 0 && state.date.monthsElapsed == 0 && state.date.monthTicks == 0,
            "Fixture advanced or date differs");
        Require(
            state.savedView
                    == (balloons         ? ScreenCoordsXY{ 0, 352 }
                            : nonuniform ? ScreenCoordsXY{ 0, 464 }
                                         : ScreenCoordsXY{ 0, 80 })
                && state.savedViewRotation == 0 && state.savedViewZoom == ZoomLevel{ 0 },
            "Saved camera differs");
        Require(
            state.park.name
                == (balloons         ? (legitimateBalloons ? "Vulkan giant seams v1" : "Vulkan static balloons v1")
                        : nonuniform ? "Vulkan nonuniform terrain v1"
                                     : "Vulkan native terrain v1"),
            "Unexpected park name");
        std::ostringstream randomState;
        randomState << state.scenarioRand;
        json_t census = { { "fixture",
                            balloons         ? (legitimateBalloons ? "giant-seams-v1" : "balloon-static-v1")
                                : nonuniform ? "nonuniform-terrain-v1"
                                             : "native-terrain-v1" },
                          { "mapSize", { 96, 96 } },
                          { "declaredElements", 9216 },
                          { "borderSurfaces", 380 },
                          { "interiorSurfaces", 8836 },
                          { "technicalStorageElements", state.tileElements.size() },
                          { "elementTypeCounts",
                            { { "surface", 9216 },
                              { "path", 0 },
                              { "track", 0 },
                              { "smallScenery", 0 },
                              { "largeScenery", 0 },
                              { "wall", 0 },
                              { "entrance", 0 },
                              { "banner", 0 } } },
                          { "surfaces", std::move(tiles) },
                          { "objects", Objects(context, sources) },
                          { "entityTypeCounts", counts },
                          { "balloons", UiParityBalloons::Census() },
                          { "freeEntitySlots", kMaxEntities - expectedBalloons },
                          { "spatialBucketsChecked", kSpatialIndexSize },
                          { "nonemptySpatialBuckets", nonemptyBuckets },
                          { "rides", 0 },
                          { "spawns", 0 },
                          { "entrances", 0 },
                          { "mapAnimationCount", "not publicly observable; surface-only map has no animated element types" },
                          { "ticks", state.currentTicks },
                          { "date", { state.date.monthsElapsed, state.date.monthTicks } },
                          { "weatherCurrent", WeatherCensus(state.weatherCurrent) },
                          { "weatherNext", WeatherCensus(state.weatherNext) },
                          { "weatherUpdateTimer", state.weatherUpdateTimer },
                          { "scenarioRandomState", randomState.str() },
                          { "camera",
                            { { "x", state.savedView.x },
                              { "y", state.savedView.y },
                              { "zoom", static_cast<int8_t>(state.savedViewZoom) },
                              { "rotation", state.savedViewRotation } } },
                          { "parkName", state.park.name } };
        if (nonuniform)
        {
            census["recipeVersion"] = GiantScreenshotFixture::kRecipeVersion;
            census["terrainMaterials"] = { { "surfaces", json_t::array() }, { "edges", json_t::array() } };
            for (size_t i = 0; i < materials.surfaces.size(); i++)
            {
                census["terrainMaterials"]["surfaces"].push_back(
                    { { "identifier", GiantScreenshotFixture::kSurfaceIdentifiers[i] }, { "slot", materials.surfaces[i] } });
                census["terrainMaterials"]["edges"].push_back(
                    { { "identifier", GiantScreenshotFixture::kEdgeIdentifiers[i] }, { "slot", materials.edges[i] } });
            }
            census["assetState"] = { { "rct1CsgLoaded", IsCsgLoaded() }, { "rct1Required", true } };
        }
        return census;
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        std::map<std::string, std::string> args;
        for (int i = 1; i < argc; i += 2)
        {
            Require(i + 1 < argc && std::string_view(argv[i]).starts_with("--"), "Expected --name value pairs");
            Require(args.emplace(argv[i] + 2, argv[i + 1]).second, "Duplicate argument");
        }
        for (const auto* key : { "mode", "park", "data", "rct2", "profile", "census" })
            Require(args.contains(key), "Missing required argument");
        const auto fixture = args.contains("fixture") ? args.at("fixture") : "giant-seams-v1";
        Require(fixture == "giant-seams-v1", "Unknown immutable giant fixture recipe");
        const bool nonuniform = true, legitimateBalloons = true, balloons = true;
        const bool prepare = args.at("mode") == "prepare";
        Require(prepare || args.at("mode") == "verify", "Unknown mode");
#ifndef NATIVE_TERRAIN_ALLOW_PREPARE
        Require(!prepare, "Current-core executable is verification-only");
#endif
        Require(!std::filesystem::exists(args.at("census")), "Refusing to overwrite census");
        if (prepare)
            Require(args.contains("output") && !std::filesystem::exists(args.at("output")), "Refusing to overwrite park");
        gCustomUserDataPath = args.at("profile");
        gCustomOpenRCT2DataPath = args.at("data");
        gCustomRCT2DataPath = args.at("rct2");
        if (args.contains("rct1"))
            gCustomRCT1DataPath = args.at("rct1");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        Require(Config::SetDefaults(), "Configuration defaults failed");
        Config::Get().general.language = LANGUAGE_ENGLISH_UK;
        auto context = CreateContext();
        if (nonuniform)
        {
            // Context construction reloads the isolated configuration. Apply the
            // pinned original-game paths afterwards, before graphics initialise.
            Require(args.contains("rct1"), "Nonuniform terrain requires pinned RCT1 assets");
            Config::Get().general.rct1Path = args.at("rct1");
            Config::Get().general.rct2Path = args.at("rct2");
        }
        Require(context->Initialise(), "Headless graphics context initialization failed");
        if (nonuniform)
            Require(IsCsgLoaded(), "Required RCT1 CSG failed to load");
        auto& manager = context->GetObjectManager();
        auto importer = prepare ? ParkImporter::CreateS6(context->GetObjectRepository())
                                : ParkImporter::CreateParkFile(context->GetObjectRepository());
        const auto loaded = importer->LoadSavedGame(args.at("park"), false);
        manager.LoadObjects(loaded.RequiredObjects);
        importer->Import(getGameState());
        auto& state = getGameState();
        if (prepare)
        {
            json_t initialSources = json_t::array();
            const auto initialObjects = Objects(*context, initialSources);
            gameStateInitAll(state, TileCoordsXY{ 96, 96 });
            json_t afterSources = json_t::array();
            Require(initialObjects == Objects(*context, afterSources), "Reset changed loaded object slots");
            const auto materials = ResolveTerrainMaterials(manager, nonuniform);
            const auto recipe = GiantScreenshotFixture::MakeTiles();
            for (int32_t y = 0; y < 96; y++)
                for (int32_t x = 0; x < 96; x++)
                {
                    auto* element = MapGetFirstElementAt(TileCoordsXY{ x, y });
                    Require(element && element->isLastForTile() && element->asSurface(), "Reset map is not surface-only");
                    auto* surface = element->asSurface();
                    const auto tile = nonuniform ? recipe[y * 96 + x]
                                                 : GiantScreenshotFixture::Tile{ .grassLength = GRASS_LENGTH_CLEAR_0 };
                    surface->setBaseZ(tile.baseZ);
                    // Match public LandSetHeightAction: clearance equals base even for sloped surfaces.
                    surface->setClearanceZ(tile.baseZ);
                    surface->setSlope(tile.slope);
                    surface->setWaterHeight(tile.water);
                    surface->setGrassLength(tile.grassLength);
                    surface->setOwnership(kUnowned);
                    surface->setParkFences(0);
                    surface->setSurfaceObjectIndex(materials.surfaces[tile.surfaceVariant]);
                    surface->setEdgeObjectIndex(materials.edges[tile.edgeVariant]);
                }
            state.currentTicks = 0;
            state.date = {};
            state.weatherCurrent = { Weather::Type::sunny, 20, Weather::EffectType::none, 0, Weather::Level::none };
            state.weatherNext = state.weatherCurrent;
            state.weatherUpdateTimer = 1920;
            state.scenarioRand.seed(1);
            state.park.name = "Vulkan native terrain v1";
            state.savedView = { 0, 80 };
            state.savedViewZoom = ZoomLevel{ 0 };
            state.savedViewRotation = 0;
            if (nonuniform)
            {
                state.park.name = "Vulkan nonuniform terrain v1";
                state.savedView = { 0, 464 }; // Projected centre of world(512,512,48), rotation0.
            }
            if (balloons)
            {
                state.park.name = legitimateBalloons ? "Vulkan giant seams v1" : "Vulkan static balloons v1";
                state.savedView = { 0, 352 };
                for (uint16_t variant = 0; variant < GiantScreenshotFixture::kBalloonCount; variant++)
                {
                    const auto expected = GiantScreenshotFixture::BalloonAt(variant);
                    Balloon::create({ expected.x, expected.y, expected.z },
                        static_cast<Drawing::Colour>(expected.colour), expected.popped != 0);
                    auto* entity = state.entities.tryGetEntity(EntityId::FromUnderlying(variant));
                    Require(entity && entity->type == EntityType::balloon, "Giant balloon creation failed or ID differs");
                    auto* balloon = entity->cast<Balloon>();
                    balloon->frame = expected.frame;
                    balloon->timeToMove = expected.timeToMove;
                }
            }
            EntityTweener::get().reset();
            MapAnimations::ClearAll();
        }
        // Direct importer use omits the normal load path's derived spatial-index rebuild.
        // Rebuild identically before both export and verification without advancing simulation.
        state.entities.resetEntitySpatialIndices();
        json_t sources = json_t::array();
        const auto census = Census(*context, sources, balloons, legitimateBalloons, nonuniform);
        json_t report = { { "schema", 1 },
                          { "mode", args.at("mode") },
                          { "census", census },
                          { "objectSources", sources },
                          { "formatVersion", kParkFileCurrentVersion },
                          { "compressionLevel", kParkFileSaveCompressionLevel },
                          { "mapAnimationsExplicitlyCleared", prepare },
                          { "noSimulationTicks", true } };
        if (prepare)
        {
            ParkFileExporter exporter;
            exporter.ExportObjectsList = manager.GetPackableObjects();
            exporter.Export(state, args.at("output"), kParkFileSaveCompressionLevel);
        }
        std::ofstream output(args.at("census"), std::ios::binary);
        output << report.dump(2) << '\n';
        Require(output.good(), "Writing census failed");
        std::cout << "Fixture " << args.at("mode") << " census complete\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
