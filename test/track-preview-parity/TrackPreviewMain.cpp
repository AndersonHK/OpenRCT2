/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
// The same driver is compiled separately against current and pristine upstream cores.
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/audio/AudioContext.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Imaging.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/PaletteIndex.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideConstruction.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/TrackDesign.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/world/Map.h>
#ifdef OPENRCT2_VULKAN_ONLY
    #include <openrct2-renderer/RenderServiceFactory.h>
    #include <openrct2-renderer/vulkan/VulkanDeviceContext.h>
    #include <openrct2/drawing/RenderService.h>
    #include <openrct2/world/MapPresentationSnapshot.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
namespace fs = std::filesystem;

namespace
{
    void WriteBytes(const fs::path& path, const void* data, size_t size)
    {
        if (fs::exists(path))
            throw std::runtime_error("Artifact already exists: " + path.string());
        std::ofstream stream(path, std::ios::binary);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    std::string RequirePath(const char* name)
    {
        const auto* value = std::getenv(name);
        if (!value || !*value)
            throw std::runtime_error(std::string("Required isolation path: ") + name);
        return fs::absolute(value).string();
    }

    // A diagnostic before/after fingerprint, not a cryptographic provenance claim.
    std::string MapFingerprint()
    {
        const auto& tiles = getGameState().tileElements;
        const auto* bytes = reinterpret_cast<const uint8_t*>(tiles.data());
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < tiles.size() * sizeof(TileElement); ++i)
            hash = (hash ^ bytes[i]) * 1099511628211ULL;
        return std::to_string(hash);
    }

    json_t WorldState()
    {
        auto& state = getGameState();
        json_t rides = json_t::array();
        for (const auto& ride : RideManager(state))
            rides.push_back({ ride.id.ToUnderlying(), ride.type });
        json_t result{
            { "tick", state.currentTicks }, { "mapSize", { state.mapSize.x, state.mapSize.y } },
            { "tileCount", state.tileElements.size() }, { "tileBytesFnv1a64", MapFingerprint() },
            { "rides", std::move(rides) }, { "guests", state.entities.getEntityListCount(EntityType::guest) },
            { "staff", state.entities.getEntityListCount(EntityType::staff) },
            { "vehicles", state.entities.getEntityListCount(EntityType::vehicle) },
            { "savedView", { state.savedView.x, state.savedView.y } },
            { "savedViewZoom", static_cast<int8_t>(state.savedViewZoom) }, { "savedViewRotation", state.savedViewRotation },
            { "constructionDirection", _currentTrackPieceDirection }, { "constructionRide", _currentRideIndex.ToUnderlying() },
            { "drawingPreview", _trackDesignDrawingPreview },
            { "forbidHighConstruction", state.park.flags.has(ParkFlag::forbidHighConstruction) }
        };
#ifdef OPENRCT2_VULKAN_ONLY
        result["mapEpoch"] = GetMapPresentationEpoch();
        result["tileRevision"] = GetTileElementRevision({ 2, 2 });
#endif
        return result;
    }

    uint8_t PreviewFlags(const TrackDesign& design)
    {
#ifdef OPENRCT2_VULKAN_ONLY
        return design.gameStateData.flags;
#else
        return design.gameStateData.flags.holder;
#endif
    }

#ifdef OPENRCT2_VULKAN_ONLY
    struct Observation
    {
        fs::path directory;
        size_t serviceCreations{};
        json_t sessions = json_t::array();
        json_t completions = json_t::array();
    };

    class Completion final : public IRenderCompletion
    {
        std::shared_ptr<IRenderCompletion> _inner;
        Observation& _observation;
        bool _saved{};

    public:
        Completion(std::shared_ptr<IRenderCompletion> inner, Observation& observation)
            : _inner(std::move(inner)), _observation(observation) {}
        const RenderSubmissionIdentity& GetIdentity() const noexcept override { return _inner->GetIdentity(); }
        void Cancel() override { _inner->Cancel(); }
        RenderOutcome Wait(std::chrono::milliseconds timeout) override
        {
            auto outcome = _inner->Wait(timeout);
            if (!_saved && outcome.result)
            {
                const auto& result = *outcome.result;
                const auto file = "owned-" + std::to_string(result.identity.submissionId) + ".indexed";
                WriteBytes(_observation.directory / file, result.indexed.data(), result.indexed.size());
                _observation.completions.push_back({
                    { "name", result.identity.name }, { "submissionId", result.identity.submissionId },
                    { "targetId", result.identity.targetId }, { "targetGeneration", result.identity.targetGeneration },
                    { "identityMatches", outcome.identity == GetIdentity() && result.identity == outcome.identity },
                    { "logicalExtent", { result.logicalExtent.width, result.logicalExtent.height } },
                    { "outputExtent", { result.outputExtent.width, result.outputExtent.height } },
                    { "indexedBytes", result.indexed.size() }, { "rgbaBytes", result.rgba.size() }, { "file", file }
                });
                _saved = true;
            }
            return outcome;
        }
    };

    class Session final : public IRenderSession
    {
        std::unique_ptr<IRenderSession> _inner;
        Observation& _observation;

    public:
        Session(std::unique_ptr<IRenderSession> inner, Observation& observation)
            : _inner(std::move(inner)), _observation(observation) {}
        IDrawingContext& GetDrawingContext() override { return _inner->GetDrawingContext(); }
        RenderTarget& GetRenderTarget() override { return _inner->GetRenderTarget(); }
        void Cancel() noexcept override { _inner->Cancel(); }
        std::shared_ptr<IRenderCompletion> Submit() override
        {
            auto completion = _inner->Submit();
            if (!completion)
                throw std::runtime_error("Production track preview returned no completion");
            return std::make_shared<Completion>(std::move(completion), _observation);
        }
    };

    class Service final : public IRenderService
    {
        std::unique_ptr<IRenderService> _inner;
        Observation& _observation;

    public:
        Service(std::unique_ptr<IRenderService> inner, Observation& observation)
            : _inner(std::move(inner)), _observation(observation) {}
        std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest request) override
        {
            _observation.sessions.push_back({
                { "name", request.name }, { "extent", { request.logicalExtent.width, request.logicalExtent.height } },
                { "indexedOutput", request.indexedOutput }, { "rgbaOutput", request.rgbaOutput },
                { "lightingEnabled", request.lightingEnabled }, { "sourceTick", getGameState().currentTicks }
            });
            auto session = _inner->BeginOffscreen(std::move(request));
            if (!session)
                throw std::runtime_error("Production track preview returned no session");
            return std::make_unique<Session>(std::move(session), _observation);
        }
        void Shutdown() noexcept override { _inner->Shutdown(); }
        void InvalidateImage(uint32_t image) override { _inner->InvalidateImage(image); }
    };

    class Factory final : public IRenderServiceFactory
    {
        std::shared_ptr<IRenderServiceFactory> _inner;
        Observation& _observation;

    public:
        Factory(std::shared_ptr<IRenderServiceFactory> inner, Observation& observation)
            : _inner(std::move(inner)), _observation(observation) {}
        bool IsEnabled() const override { return _inner->IsEnabled(); }
        std::unique_ptr<IRenderService> Create() override
        {
            ++_observation.serviceCreations;
            return std::make_unique<Service>(_inner->Create(), _observation);
        }
    };
#endif
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: track-preview-parity <new-output-directory> <park-path>\n";
        return EXIT_FAILURE;
    }
    const auto output = fs::absolute(argv[1]);
    if (fs::exists(output))
    {
        std::cerr << "Output directory already exists\n";
        return EXIT_FAILURE;
    }
    fs::create_directories(output);
    json_t report{ { "schema", 1 }, { "fixture", "track-design-preview" }, { "fixtureVersion", 1 },
                   { "cases", json_t::array() }, { "status", "incomplete" }, { "error", "" } };
#ifdef OPENRCT2_VULKAN_ONLY
    report["renderer"] = "vulkan";
    Observation observation{ output };
    auto owner = std::make_shared<Ui::Vulkan::DeviceContextOwner>(false);
#else
    report["renderer"] = "upstream-software";
#endif
    try
    {
        gCustomUserDataPath = RequirePath("OPENRCT2_ORACLE_USER_PATH");
        gCustomOpenRCT2DataPath = RequirePath("OPENRCT2_ORACLE_DATA_PATH");
        gCustomRCT1DataPath = RequirePath("OPENRCT2_ORACLE_RCT1_PATH");
        gCustomRCT2DataPath = RequirePath("OPENRCT2_ORACLE_RCT2_PATH");
        fs::create_directories(gCustomUserDataPath);
        auto environment = CreatePlatformEnvironment();
        Config::Get().general.rct1Path = gCustomRCT1DataPath;
        Config::Get().general.rct2Path = gCustomRCT2DataPath;
        Config::Get().general.dayNightCycle = false;
        Config::Get().general.enableLightFx = false;
        if (!Config::SaveToPath(environment->GetFilePath(PathId::config)))
            throw std::runtime_error("Could not seed isolated track preview profile");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
#ifdef OPENRCT2_VULKAN_ONLY
        auto factory = std::make_shared<Factory>(Renderer::CreateConfiguredRenderServiceFactory(owner), observation);
        auto context = CreateContext(
            CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(), Ui::CreateDummyUiContext(), factory);
#else
        auto context = CreateContext(
            CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(), Ui::CreateDummyUiContext());
#endif
        if (!context->Initialise() || !context->LoadParkFromFile(argv[2]))
            throw std::runtime_error("Could not initialise or load track preview reference park");
        gLegacyScene = LegacyScene::playing;
        auto& manager = context->GetObjectManager();
        if (!manager.LoadObject("rct2.ride.spboat"))
            throw std::runtime_error("Original splash boat object could not be loaded");
        LoadPalette();
        uint32_t g1Records{}, g1Payloads{};
        for (uint32_t index = 0; index < SPR_G1_END; ++index)
        {
            if (const auto* sprite = GfxGetG1Element(index))
            {
                ++g1Records;
                if (sprite->offset)
                    ++g1Payloads;
            }
        }
        report["assetState"] = { { "g1RecordCount", g1Records }, { "g1PayloadCount", g1Payloads },
                                  { "rct1CsgLoaded", IsCsgLoaded() }, { "rct1Required", true },
                                  { "configuredRct1Path", Config::Get().general.rct1Path },
                                  { "configuredRct2Path", Config::Get().general.rct2Path } };
        if (g1Records != SPR_G1_END || g1Payloads == 0 || !IsCsgLoaded())
            throw std::runtime_error("Required original graphics are not loaded");
        if (std::none_of(gPalette.begin(), gPalette.end(), [](const auto& c) { return c.red || c.green || c.blue; }))
            throw std::runtime_error("Original game palette is empty");
        for (const size_t length : { 1u, 35u, 55u })
        {
            const auto name = "flat" + std::to_string(length);
            const auto folder = output / name;
            fs::create_directory(folder);
            TrackDesign design;
            design.trackAndVehicle.rtdIndex = RIDE_TYPE_SPLASH_BOATS;
            design.trackAndVehicle.vehicleObject = ObjectEntryDescriptor("rct2.ride.spboat");
            design.trackElements.resize(length);
            for (auto& element : design.trackElements)
                element.type = TrackElemType::flat;
            for (auto& colours : design.appearance.trackColours)
            {
                colours.main = Colour::brightRed;
                colours.additional = Colour::yellow;
                colours.supports = Colour::lightBlue;
            }
            auto pixels = std::make_unique<TrackDesignPreviewBuffer>();
            pixels->fill(PaletteIndex::transparent);
            const auto before = WorldState();
            TrackDesignDrawPreview(design, *pixels, false);
            const auto after = WorldState();
            json_t differences = json_t::array();
            for (const auto& [key, value] : before.items())
                if (value != after.at(key))
                    differences.push_back(key);
            auto requiredBefore = before;
            auto requiredAfter = after;
            json_t restorationExclusions = json_t::array();
#ifndef OPENRCT2_VULKAN_ONLY
            // The pristine implementation does not preserve these construction globals.
            // Keep their actual values/differences visible; pixel equality remains strict.
            for (const char* key : { "constructionRide", "drawingPreview" })
            {
                requiredBefore.erase(key);
                requiredAfter.erase(key);
                restorationExclusions.push_back(key);
            }
#endif
            json_t item{ { "name", name }, { "trackType", "splash-boats" }, { "flatElementCount", length },
                         { "expectedZoom", length == 1 ? 1 : length == 35 ? 2 : 3 }, { "extent", { 370, 217 } },
                         { "placeScenery", false }, { "before", before }, { "after", after },
                         { "restored", before == after }, { "rotations", json_t::array() },
                         { "restorationDifferences", std::move(differences) },
                         { "restorationExclusions", std::move(restorationExclusions) },
                         { "restorationContractPassed", requiredBefore == requiredAfter },
                         { "gameStateData", { { "flags", PreviewFlags(design) }, { "cost", design.gameStateData.cost } } } };
            WriteBytes(folder / "preview.indexed", pixels->data(), pixels->size());
            std::vector<uint8_t> palette;
            for (const auto& colour : gPalette)
            {
                palette.push_back(colour.red);
                palette.push_back(colour.green);
                palette.push_back(colour.blue);
            }
            WriteBytes(folder / "palette.bin", palette.data(), palette.size());
            for (uint32_t rotation = 0; rotation < 4; ++rotation)
            {
                Image image;
                image.Width = 370;
                image.Height = 217;
                image.Depth = 8;
                image.Stride = 370;
                image.Palette = gPalette;
                const auto* begin = reinterpret_cast<const uint8_t*>(pixels->data()) + rotation * kTrackPreviewImageSize;
                image.Pixels.assign(begin, begin + kTrackPreviewImageSize);
                const auto file = "r" + std::to_string(rotation) + ".png";
                Imaging::WriteToFile((folder / file).string(), image, ImageFormat::png);
                const auto nonzero = std::count_if(image.Pixels.begin(), image.Pixels.end(), [](auto pixel) { return pixel != 0; });
                item["rotations"].push_back({ { "rotation", rotation }, { "file", name + "/" + file },
                                              { "nonzeroPixels", nonzero } });
            }
            report["cases"].push_back(std::move(item));
            if (requiredBefore != requiredAfter)
                throw std::runtime_error("Track preview did not restore its live world state");
            for (const auto& rotation : report["cases"].back()["rotations"])
                if (rotation["nonzeroPixels"] == 0)
                    throw std::runtime_error("Track preview returned an empty rotation");
        }
#ifdef OPENRCT2_VULKAN_ONLY
        report["serviceCreations"] = observation.serviceCreations;
        report["deviceCreated"] = owner->IsCreated();
        report["sessions"] = observation.sessions;
        report["completions"] = observation.completions;
        if (observation.serviceCreations != 1 || !owner->IsCreated()
            || observation.sessions.size() != 12 || observation.completions.size() != 12)
            throw std::runtime_error("Track preview shared service lifecycle is incomplete");
#endif
        report["status"] = "pass";
    }
    catch (const std::exception& error)
    {
        report["status"] = "fail";
        report["error"] = error.what();
        std::cerr << error.what() << '\n';
    }
#ifdef OPENRCT2_VULKAN_ONLY
    report["serviceCreations"] = observation.serviceCreations;
    report["deviceCreated"] = owner->IsCreated();
    report["sessions"] = observation.sessions;
    report["completions"] = observation.completions;
#endif
    std::ofstream file(output / "report.json");
    file << report.dump(2) << '\n';
    return report["status"] == "pass" ? EXIT_SUCCESS : EXIT_FAILURE;
}
