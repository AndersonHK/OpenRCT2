// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// External original-art oracle: compile ONLY against the pristine upstream core.
// No current path rules, shaders, renderer, or precomputed paint stream are inputs.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Imaging.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/localisation/Language.h>
#include <openrct2/object/FootpathEntry.h>
#include <openrct2/object/FootpathRailingsObject.h>
#include <openrct2/object/FootpathSurfaceObject.h>
#include <openrct2/object/ObjectList.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/ObjectRepository.h>
#include <openrct2/object/PathAdditionObject.h>
#include <openrct2/park/ParkFile.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
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
    struct Materials
    {
        ObjectEntryIndex surface = kObjectEntryIndexNull;
        ObjectEntryIndex queue = kObjectEntryIndexNull;
        ObjectEntryIndex railings = kObjectEntryIndexNull;
        std::array<ObjectEntryIndex, 3> additions = { kObjectEntryIndexNull, kObjectEntryIndexNull, kObjectEntryIndexNull };
    };
    Materials SelectMaterials(OpenRCT2::IObjectManager& manager)
    {
        Materials result;
        for (ObjectEntryIndex i = 0; i < 255; i++)
        {
            if (auto* surface = manager.GetLoadedObject<FootpathSurfaceObject>(i))
            {
                if (surface->Flags & FOOTPATH_ENTRY_FLAG_IS_QUEUE)
                {
                    if (result.queue == kObjectEntryIndexNull)
                        result.queue = i;
                }
                else if (
                    !(surface->Flags & FOOTPATH_ENTRY_FLAG_SHOW_ONLY_IN_SCENARIO_EDITOR)
                    && result.surface == kObjectEntryIndexNull)
                    result.surface = i;
            }
            if (result.railings == kObjectEntryIndexNull && manager.GetLoadedObject<FootpathRailingsObject>(i))
                result.railings = i;
            if (auto* addition = manager.GetLoadedObject<PathAdditionObject>(i))
            {
                const auto* entry = static_cast<const PathAdditionEntry*>(addition->GetLegacyData());
                const auto type = static_cast<size_t>(entry->draw_type);
                if (type < result.additions.size() && result.additions[type] == kObjectEntryIndexNull)
                    result.additions[type] = i;
            }
        }
        Require(
            result.surface != kObjectEntryIndexNull && result.queue != kObjectEntryIndexNull
                && result.railings != kObjectEntryIndexNull,
            "Seed park lacks loaded normal/queue/railings materials");
        for (auto slot : result.additions)
            Require(slot != kObjectEntryIndexNull, "Seed park lacks loaded lamp/bin/bench material");
        return result;
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
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: path-fixture <new-output-directory> <seed-park>\n";
        return EXIT_FAILURE;
    }
    const fs::path output = fs::absolute(argv[1]);
    if (fs::exists(output))
    {
        std::cerr << "Output already exists\n";
        return EXIT_FAILURE;
    }
    fs::create_directories(output);
    json_t manifest = { { "schema", 1 },
                        { "fixture", "original-path-art-v1" },
                        { "status", "incomplete" },
                        { "cases", json_t::array() },
                        { "paths", json_t::array() },
                        { "noSimulationTicks", true },
                        { "renderer", "pristine-upstream-software" },
                        { "landscapeSmoothing", false },
                        { "scope",
                          "ground-level flat/junction/queue paths, terrain-matched slopes, lamps/bins/benches; no elevated "
                          "supports/banner text/tunnels" } };
    try
    {
        gCustomUserDataPath = Environment("OPENRCT2_ORACLE_USER_PATH");
        gCustomOpenRCT2DataPath = Environment("OPENRCT2_ORACLE_DATA_PATH");
        gCustomRCT1DataPath = Environment("OPENRCT2_ORACLE_RCT1_PATH");
        gCustomRCT2DataPath = Environment("OPENRCT2_ORACLE_RCT2_PATH");
        fs::create_directories(gCustomUserDataPath);
        Require(Config::SetDefaults(), "Could not set defaults");
        Config::Get().general.rct1Path = gCustomRCT1DataPath;
        Config::Get().general.rct2Path = gCustomRCT2DataPath;
        Config::Get().general.language = LANGUAGE_ENGLISH_UK;
        Config::Get().general.dayNightCycle = false;
        Config::Get().general.enableLightFx = false;
        Config::Get().general.landscapeSmoothing = false;
        auto environment = CreatePlatformEnvironment();
        Require(Config::SaveToPath(environment->GetFilePath(PathId::config)), "Could not seed isolated configuration");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        auto context = CreateContext();
        Require(context->Initialise() && context->LoadParkFromFile(argv[2]), "Could not initialise/load seed park");
        gLegacyScene = LegacyScene::playing;
        auto& manager = context->GetObjectManager();
        const auto materials = SelectMaterials(manager);
        const auto grass = manager.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto rock = manager.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        Require(grass != kObjectEntryIndexNull && rock != kObjectEntryIndexNull, "Seed park lacks grass/rock");
        manifest["objectSources"] = ObjectSources(*context);
        manifest["materials"] = {
            { "surface", materials.surface },     { "queue", materials.queue }, { "railings", materials.railings },
            { "additions", materials.additions }, { "grass", grass },           { "rock", rock }
        };
        auto& state = getGameState();
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
        int ordinal = 0;
        auto add = [&](const std::string& name, int edges, int corners, bool queue, int slope, int addition, int condition) {
            const TileCoordsXY tile{ 7 + (ordinal % 8) * 2, 9 + (ordinal / 8) * 2 };
            ordinal++;
            if (slope >= 0)
            {
                constexpr uint8_t landSlopes[4] = { 12, 9, 3, 6 };
                MapGetSurfaceElementAt(tile)->setSlope(landSlopes[slope]);
            }
            auto* path = TileElementInsert<PathElement>(CoordsXYZ{ tile.x * 32, tile.y * 32, 64 }, 15);
            Require(path != nullptr, "Could not insert path recipe");
            path->setClearanceZ(slope >= 0 ? 96 : 80);
            path->setSurfaceEntryIndex(queue ? materials.queue : materials.surface);
            path->setRailingsEntryIndex(materials.railings);
            path->setIsQueue(queue);
            path->setEdges(static_cast<uint8_t>(edges));
            path->setCorners(static_cast<uint8_t>(corners));
            path->setJunctionRailings(true);
            path->setSloped(slope >= 0);
            path->setSlopeDirection(static_cast<uint8_t>(slope >= 0 ? slope : 0));
            path->setHasQueueBanner(false);
            path->setIsBroken(condition == 2);
            path->setAddition(0);
            if (addition >= 0)
            {
                path->setAdditionEntryIndex(materials.additions[addition]);
                path->setAdditionStatus(condition == 1 ? 0 : 255);
            }
            manifest["paths"].push_back({ { "name", name },
                                          { "x", tile.x },
                                          { "y", tile.y },
                                          { "baseZ", 64 },
                                          { "clearanceZ", path->getClearanceZ() },
                                          { "edges", edges },
                                          { "corners", corners },
                                          { "queue", queue },
                                          { "slope", slope },
                                          { "additionType", addition },
                                          { "condition", condition },
                                          { "surfaceSlot", path->getSurfaceEntryIndex() },
                                          { "railingsSlot", path->getRailingsEntryIndex() },
                                          { "additionSlot", path->getAdditionEntryIndex() },
                                          { "additionStatus", path->getAdditionStatus() },
                                          { "junctionRailings", true },
                                          { "hasQueueBanner", false } });
        };
        for (int edges = 0; edges < 16; edges++)
            add("flat-" + std::to_string(edges), edges, 0, false, -1, -1, 0);
        add("flat-filled-corners", 15, 15, false, -1, -1, 0);
        for (int slope = 0; slope < 4; slope++)
            add("slope-" + std::to_string(slope), (slope & 1) != 0 ? 10 : 5, 0, false, slope, -1, 0);
        for (int edges = 0; edges < 16; edges++)
            add("queue-" + std::to_string(edges), edges, 0, true, -1, -1, 0);
        for (int type = 0; type < 3; type++)
            for (int condition = 0; condition < 3; condition++)
                add("addition-" + std::to_string(type) + "-" + std::to_string(condition), 0, 0, false, -1, type, condition);
        EntityTweener::get().reset();
        MapAnimations::ClearAll();
        state.entities.resetEntitySpatialIndices();
        state.currentTicks = 0;
        state.date = {};
        state.scenarioRand.seed(1);
        state.weatherCurrent = { Weather::Type::sunny, 20, Weather::EffectType::none, 0, Weather::Level::none };
        state.weatherNext = state.weatherCurrent;
        state.weatherUpdateTimer = 1920;
        state.park.name = "Original path art v1";
        state.savedView = { 0, 448 };
        state.savedViewZoom = ZoomLevel{ 0 };
        state.savedViewRotation = 0;
        ParkFileExporter exporter;
        exporter.ExportObjectsList = manager.GetPackableObjects();
        exporter.Export(state, (output / "paths.park").string(), kParkFileSaveCompressionLevel);
        LoadPalette();
        Require(
            std::any_of(gPalette.begin(), gPalette.end(), [](auto c) { return c.red || c.green || c.blue; }), "Empty palette");
        Bytes(output / "palette.bgra", gPalette.data(), sizeof(gPalette));
        manifest["palette"] = "palette.bgra";
        X8DrawingEngine engine(context->GetUiContext());
        for (int zoom = 0; zoom <= 1; zoom++)
            for (uint8_t rotation = 0; rotation < 4; rotation++)
            {
                const std::string name = "paths-r" + std::to_string(rotation) + "-z" + std::to_string(zoom);
                Viewport viewport{};
                viewport.width = 960;
                viewport.height = 640;
                viewport.zoom = ZoomLevel{ static_cast<int8_t>(zoom) };
                viewport.rotation = rotation;
                const auto centre = Translate3DTo2DWithZ(rotation, { 512, 512, 64 });
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
                                              { "park", "paths.park" },
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
