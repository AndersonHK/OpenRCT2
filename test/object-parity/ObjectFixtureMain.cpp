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
#include <openrct2/ride/ted/TrackElemType.h>
#include <openrct2/world/Banner.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/tile_element/BannerElement.h>
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
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: object-fixture <new-output-directory> <seed-park>\n";
        return EXIT_FAILURE;
    }
    const fs::path output = fs::absolute(argv[1]);
    if (fs::exists(output))
    {
        std::cerr << "Output already exists\n";
        return EXIT_FAILURE;
    }
    fs::create_directories(output);
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
        ObjectEntryIndex treeSlot = kObjectEntryIndexNull, largeSlot = kObjectEntryIndexNull, wallSlot = kObjectEntryIndexNull,
                         bannerSlot = kObjectEntryIndexNull;
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
                && !(surface->Flags & FOOTPATH_ENTRY_FLAG_SHOW_ONLY_IN_SCENARIO_EDITOR) && pathSurface == kObjectEntryIndexNull)
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
                const CoordsXYZ position{ (18 + ghost * 6) * 32 + tile.offset.x, 9 * 32 + tile.offset.y, 64 + tile.offset.z };
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
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
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
                viewport.width = 1664;
                viewport.height = 1024;
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
