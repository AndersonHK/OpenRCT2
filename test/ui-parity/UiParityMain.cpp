/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "BalloonFixtureState.h"
#include "SdlCapture.h"
#include "UiFixtures.h"
#include "UiTreeTrackSceneLocator.h"
#include "UiCherryTrackFixture.h"
#include <openrct2/drawing/IDrawingContext.h>
#include "UiFontFixture.h"
#include "UiLightFixture.h"
#include "UiWeatherFixture.h"
#ifdef OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS
    #include <openrct2/interface/ViewportPaintDiagnostics.h>
#endif

#include <SDL.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <openrct2-ui/UiContext.h>
#include <openrct2-ui/drawing/BitmapReader.h>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/audio/AudioContext.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Imaging.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Drawing.Screen.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/drawing/PresentationGeneration.h>
#include <openrct2/world/Map.h>
#include <openrct2/network/Network.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/interface/Window.h>
#include <openrct2/interface/WindowBase.h>
#include <openrct2/paint/Painter.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/scenes/SceneManager.h>
#include <openrct2/scenes/preloader/PreloaderScene.h>
#include <openrct2/ui/UiContext.h>
#include <span>
#include <stdexcept>

#if defined(ENABLE_VULKAN) && defined(OPENRCT2_VULKAN_DIAGNOSTICS)
    #include <openrct2-ui/drawing/engines/vulkan/VulkanDiagnosticCapture.h>
    #define UI_PARITY_HAS_VULKAN_CAPTURE
    #include "UiSharedServiceLifecycle.h"
    #include "UiTerrainFixture.h"
#endif

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

namespace
{
    json_t MotionVehicleCensus(const EntityPresentationSnapshot* snapshot = nullptr)
    {
        auto records = json_t::array();
        auto& state = getGameState();
        for (uint32_t id = 0; id < kMaxEntities; ++id)
        {
            const auto entityId = EntityId::FromUnderlying(static_cast<uint16_t>(id));
            const auto* entity = snapshot == nullptr ? state.entities.tryGetEntity(entityId) : snapshot->TryGetEntity(entityId);
            if (entity == nullptr || entity->type != EntityType::vehicle)
                continue;
            const auto& v = *entity->cast<Vehicle>();
            records.push_back(std::array<int64_t, 25>{ v.id.ToUnderlying(), v.ride.ToUnderlying(), v.x, v.y, v.z,
                v.orientation, static_cast<uint8_t>(v.pitch), static_cast<uint8_t>(v.roll), v.TrackTypeAndDirection,
                v.TrackLocation.x, v.TrackLocation.y, v.TrackLocation.z, v.track_progress, v.velocity, v.acceleration,
                static_cast<uint8_t>(v.status), v.animation_frame, v.animationState, v.next_vehicle_on_train.ToUnderlying(),
                v.prev_vehicle_on_ride.ToUnderlying(), v.next_vehicle_on_ride.ToUnderlying(), v.vehicle_type,
                v.spriteData.width, v.spriteData.heightMin, v.spriteData.heightMax });
        }
        const auto random = state.scenarioRand.state();
        return { {"schema",1}, {"columns",{"id","ride","x","y","z","orientation","pitch","roll","trackTypeDirection",
            "trackX","trackY","trackZ","trackProgress","velocity","acceleration","status","animationFrame","animationState",
            "nextTrain","previousRide","nextRide","vehicleType","width","heightMin","heightMax"}},
            {"count",records.size()}, {"records",std::move(records)}, {"scenarioRng",{random.s0,random.s1}} };
    }

    std::vector<std::string> MotionSteps(uint32_t ticks)
    {
        std::vector<std::string> result;
        for (uint32_t tick = 0; tick <= ticks; ++tick)
            for (const auto* pass : { "damage", "full" })
                result.push_back("motion-" + std::to_string(tick) + "-" + pass);
        return result;
    }

    std::map<std::string, std::string> ParseArguments(int argc, char** argv)
    {
        std::map<std::string, std::string> arguments;
        for (int i = 1; i < argc; i += 2)
        {
            if (i + 1 >= argc || !std::string_view(argv[i]).starts_with("--"))
                throw std::invalid_argument("Arguments must be --name value pairs");
            if (!arguments.emplace(argv[i] + 2, argv[i + 1]).second)
                throw std::invalid_argument("Duplicate argument: " + std::string(argv[i]));
        }
        for (const auto* required : { "park", "data", "rct2", "profile", "output" })
            if (!arguments.contains(required))
                throw std::invalid_argument("Missing --" + std::string(required));
        return arguments;
    }

    uint64_t HashFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Cannot read input: " + path.string());
        uint64_t hash = 14695981039346656037ULL;
        char value;
        while (stream.get(value))
            hash = (hash ^ static_cast<uint8_t>(value)) * 1099511628211ULL;
        return hash;
    }

    void WriteBytes(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void SaveCapture(const std::filesystem::path& output, const UiParity::SdlCapture& capture, json_t metadata)
    {
        const auto directory = output / capture.name;
        std::filesystem::create_directories(directory);
        Image image{ .Width = capture.width, .Height = capture.height, .Depth = 32 };
        image.Stride = capture.width * 4;
        image.Pixels = capture.rgba;
        Imaging::WriteToFile((directory / "screen.png").string(), image, ImageFormat::png);
        WriteBytes(directory / "screen.rgba", capture.rgba);
        metadata["name"] = capture.name;
        metadata["physicalExtent"] = { capture.width, capture.height };
        if (!capture.rendererName.empty())
        {
            metadata["sdlRenderer"] = capture.rendererName;
            metadata["sdlRendererFlags"] = capture.rendererFlags;
            metadata["presentOrdinal"] = capture.presentOrdinal;
            metadata["captureSource"] = "actual SDL default backbuffer immediately before SDL_RenderPresent";
        }
        Json::WriteToFile((directory / "report.json").string(), metadata);
    }

#ifndef OPENRCT2_VULKAN_ONLY
    std::vector<uint8_t> ReadSoftwareCanvas(const RenderTarget& target)
    {
        if (target.bits == nullptr || target.width <= 0 || target.height <= 0 || target.zoom_level != ZoomLevel{})
            throw std::runtime_error("Software main canvas is unavailable");
        std::vector<uint8_t> result(static_cast<size_t>(target.width) * target.height);
        for (int32_t y = 0; y < target.height; y++)
            std::memcpy(
                result.data() + static_cast<size_t>(y) * target.width, target.bits + y * target.LineStride(), target.width);
        return result;
    }
#endif

#ifdef UI_PARITY_HAS_VULKAN_CAPTURE
    void AddVulkanMetadata(json_t& metadata, const Ui::Vulkan::Diagnostic::CaptureResult& capture)
    {
        const auto& frame = capture.output;
        const auto& coverage = capture.coverage;
        if (capture.balloonPublication.has_value())
        {
            const auto& publication = *capture.balloonPublication;
            const auto& m = publication.metrics;
            const auto& t = publication.producerTotals;
            metadata["balloonPublication"] = { { "profile", publication.retained ? "retainedBalloons" : "legacyBulk" },
                                               { "epoch", publication.epoch },
                                               { "sequence", publication.sequence },
                                               { "count", publication.count },
                                               { "entityCount", publication.entityCount },
                                               { "records", publication.records },
                                               { "sourceTick", m.sourceTick },
                                               { "captureMetrics",
                                                 { { "worklistEntries", m.worklistEntries },
                                                   { "worklistVisits", m.worklistVisits },
                                                   { "bootstrapVisits", m.bootstrapVisits },
                                                   { "payloadCopiedBytes", m.payloadCopiedBytes },
                                                   { "compatibilityCopiedBytes", m.compatibilityCopiedBytes },
                                                   { "recordCopiedBytes", m.recordCopiedBytes },
                                                   { "bulkCopiedBytes", m.bulkCopiedBytes },
                                                   { "copiedBalloonBytes", m.GetCopiedBalloonBytes() },
                                                   { "fallbackSlotVisits", m.fallbackSlotVisits },
                                                   { "fallbackIndexedBalloons", m.fallbackIndexedBalloons } } },
                                               { "producerTotals",
                                                 { { "captures", t.captures },
                                                   { "worklistEntries", t.worklistEntries },
                                                   { "worklistVisits", t.worklistVisits },
                                                   { "payloadCopiedBytes", t.payloadCopiedBytes },
                                                   { "compatibilityCopiedBytes", t.compatibilityCopiedBytes },
                                                   { "recordCopiedBytes", t.recordCopiedBytes },
                                                   { "bulkCopiedBytes", t.bulkCopiedBytes },
                                                   { "copiedBalloonBytes", t.GetCopiedBalloonBytes() } } },
                                               { "gpuAdmission", coverage.nativeBalloonViewports != 0 },
                                               { "measurementScope",
                                                 "publication copies; excludes diagnostic census and JSON; capture metrics "
                                                 "must not be summed over reused generations" } };
        }
        metadata["captureSource"] = "actual Vulkan swapchain copy for the named frontend packet before presentation";
        metadata["frameNumber"] = frame.frameNumber;
        metadata["paletteVersion"] = frame.paletteVersion;
        metadata["swapchainGeneration"] = frame.swapchainGeneration;
        metadata["sourceFormat"] = frame.sourceFormat;
        metadata["deviceName"] = frame.deviceName;
        metadata["vendorId"] = frame.vendorId;
        metadata["deviceId"] = frame.deviceId;
        metadata["driverVersion"] = frame.driverVersion;
        metadata["lightFxEnabled"] = frame.lightFxEnabled;
        metadata["nativeBalloonFixture"] = { { "viewports", coverage.nativeBalloonViewports },
                                             { "cpuBalloonSpriteCalls", coverage.cpuBalloonSpriteCalls },
                                             { "sourceUploadBytes", frame.balloonUploads.sourceBytes },
                                             { "spriteUploadBytes", frame.balloonUploads.spriteBytes },
                                             { "uploadEpoch", frame.balloonUploads.epoch },
                                             { "uploadSequence", frame.balloonUploads.sequence },
                                             { "uploadGpuRevision", frame.balloonUploads.gpuRevision },
                                             { "submittedViewports", frame.balloonUploads.viewportSubmissions } };
        metadata["lightFxCommands"] = { { "count", coverage.lightFxCommandCount },
                                        { "cpuIntensityAttached", coverage.lightFxCpuIntensityAttached },
                                        { "commandFnv1a64", coverage.lightFxCommandHash },
                                        { "lightPaletteFnv1a64", coverage.lightFxPaletteHash },
                                        { "typeHistogram", coverage.lightFxTypeHistogram } };
        metadata["ttfCommands"] = { { "opaque", coverage.ttfOpaqueCount },
                                    { "transparent", coverage.ttfTransparentCount },
                                    { "clippedBoundsAndThreshold", coverage.ttfClippedBoundsAndThreshold } };
        metadata["commandCoverage"] = { { "frameNumber", coverage.frameNumber },
                                        { "resizeVersion", coverage.resizeVersion },
                                        { "surfaceFormatVersion", coverage.surfaceFormatVersion },
                                        { "paletteVersion", coverage.paletteVersion },
                                        { "graphicsLookupTablesVersion", coverage.graphicsLookupTablesVersion },
                                        { "lines", coverage.lineCount },
                                        { "opaqueRectangles", coverage.opaqueRectCount },
                                        { "opaqueSprites", coverage.opaqueSpriteCount },
                                        { "transparentRectangles", coverage.transparentRectCount },
                                        { "weather", coverage.weatherCount },
                                        { "worldSurfaces", coverage.worldSurfaces },
                                        { "worldEpoch", coverage.worldEpoch },
                                        { "worldSurfaceRecordCount", coverage.worldSurfaceRecordCount } };
    }
#endif
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto arguments = ParseArguments(argc, argv);
        const auto value = [&](const char* name, std::string fallback) {
            const auto it = arguments.find(name);
            return it == arguments.end() ? fallback : it->second;
        };
#ifdef OPENRCT2_VULKAN_ONLY
        const auto renderer = value("renderer", "vulkan");
#else
        const auto renderer = value("renderer", "software");
#endif
        const auto lifecycleArgument = value("shared-service-lifecycle", "false");
        if (lifecycleArgument != "true" && lifecycleArgument != "false")
            throw std::invalid_argument("--shared-service-lifecycle must be true or false");
        const bool sharedServiceLifecycle = lifecycleArgument == "true";
        const auto retainedArgument = value("retained-balloons", "false");
        if (retainedArgument != "true" && retainedArgument != "false")
            throw std::invalid_argument("--retained-balloons must be true or false");
        const bool retainedBalloons = retainedArgument == "true";
        const auto nativeBalloonArgument = value("gpu-balloons", "false");
        if (nativeBalloonArgument != "true" && nativeBalloonArgument != "false")
            throw std::invalid_argument("--gpu-balloons must be true or false");
        const bool nativeBalloons = nativeBalloonArgument == "true";
        const auto nativeTerrainArgument = value("gpu-terrain", "false");
        if (nativeTerrainArgument != "true" && nativeTerrainArgument != "false")
            throw std::invalid_argument("--gpu-terrain must be true or false");
        const bool nativeTerrain = nativeTerrainArgument == "true";
        if (nativeTerrain && (renderer != "vulkan" || nativeBalloons || retainedBalloons || sharedServiceLifecycle))
            throw std::invalid_argument("Native terrain requires an isolated Vulkan fixture");
#ifndef OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS
        if (nativeTerrain)
            throw std::invalid_argument("Native terrain requires diagnostic core paint instrumentation");
#endif
        const auto fallbackArgument = value("expect-gpu-balloon-fallback", "false");
        if (fallbackArgument != "true" && fallbackArgument != "false")
            throw std::invalid_argument("--expect-gpu-balloon-fallback must be true or false");
        const bool expectBalloonFallback = fallbackArgument == "true";
        if (expectBalloonFallback && !nativeBalloons)
            throw std::invalid_argument("--expect-gpu-balloon-fallback requires --gpu-balloons");
        if (nativeBalloons && !retainedBalloons)
            throw std::invalid_argument("--gpu-balloons requires retained publication");
        const auto countArgument = value("require-balloon-count", "0");
        uint32_t requiredBalloonCount{};
        const auto countParse = std::from_chars(
            countArgument.data(), countArgument.data() + countArgument.size(), requiredBalloonCount);
        if (countParse.ec != std::errc{} || countParse.ptr != countArgument.data() + countArgument.size()
            || (arguments.contains("require-balloon-count") && requiredBalloonCount == 0))
            throw std::invalid_argument("--require-balloon-count must be a positive unsigned integer");
        if (retainedBalloons && (renderer != "vulkan" || requiredBalloonCount == 0))
            throw std::invalid_argument("Retained balloon diagnostics require Vulkan and a positive --require-balloon-count");
        const auto stableSort = value("paint-stable-sort", "false");
        if (stableSort != "true" && stableSort != "false")
            throw std::invalid_argument("--paint-stable-sort must be true or false");
        const auto smoothing = value("landscape-smoothing", "true");
        if (smoothing != "true" && smoothing != "false")
            throw std::invalid_argument("--landscape-smoothing must be true or false");
        const auto requireWorldSurfaces = value("require-world-surfaces", "any");
        if (requireWorldSurfaces != "true" && requireWorldSurfaces != "false" && requireWorldSurfaces != "any")
            throw std::invalid_argument("--require-world-surfaces must be true, false or any");
        const auto viewportFlagsArgument = value("viewport-flags", "0");
        uint32_t viewportFlags{};
        const auto flagsParse = std::from_chars(
            viewportFlagsArgument.data(), viewportFlagsArgument.data() + viewportFlagsArgument.size(), viewportFlags);
        if (flagsParse.ec != std::errc{} || flagsParse.ptr != viewportFlagsArgument.data() + viewportFlagsArgument.size())
            throw std::invalid_argument("--viewport-flags must be an unsigned 32-bit decimal integer");
        const auto fixture = value("fixture", "baseline");
        constexpr std::string_view incrementalSuffix = "-incremental";
        const bool incremental = fixture.ends_with(incrementalSuffix);
        const auto compositionFamily = incremental ? fixture.substr(0, fixture.size() - incrementalSuffix.size()) : fixture;
        const bool worldMotion = compositionFamily == "world-motion16" || compositionFamily == "world-motion32";
        const uint32_t motionTicks = compositionFamily == "world-motion32" ? 32u : 16u;
        if (worldMotion && (incremental || nativeTerrain || nativeBalloons || retainedBalloons || sharedServiceLifecycle
                || viewportFlags != 0 || requiredBalloonCount != 0 || gPaintForceRedraw))
            throw std::invalid_argument("World motion requires isolated ordinary world painting and the normal invalidation grid");
        if (compositionFamily == "world-dirty" && (nativeTerrain || nativeBalloons || retainedBalloons || sharedServiceLifecycle
                || viewportFlags != 0 || requiredBalloonCount != 0 || gPaintForceRedraw))
            throw std::invalid_argument("Dirty-world requires ordinary world painting, viewport flags0 and the normal invalidation grid");
        const bool lightNight = compositionFamily == "light-night";
        const bool weatherFixture = UiParityWeather::IsFamily(compositionFamily);
        const bool fontFixture = UiParityFonts::IsFamily(compositionFamily);
        const bool transparentHistory = compositionFamily == "transparent-history";
        const auto historyClearArgument = value("history-clear-zero", "false");
        if (historyClearArgument != "true" && historyClearArgument != "false")
            throw std::invalid_argument("--history-clear-zero must be true or false");
        const bool historyClearZero = historyClearArgument == "true";
        if (historyClearZero && !transparentHistory)
            throw std::invalid_argument("Explicit zero clear is restricted to the transparent history probe");
        if (transparentHistory && (incremental || nativeTerrain || nativeBalloons || retainedBalloons
                || sharedServiceLifecycle || viewportFlags != 0 || smoothing != "false"
                || requiredBalloonCount != 0))
            throw std::invalid_argument("Transparent history requires isolated ordinary painting and initial opaque flags");
        const bool ordinalSequence = lightNight || weatherFixture || fontFixture || transparentHistory || worldMotion;
        if (ordinalSequence && incremental)
            throw std::invalid_argument("Lighting/weather/font sequences use full invalidation at every named paint ordinal");
        const auto steps = lightNight ? UiParityLight::Steps()
            : worldMotion             ? MotionSteps(motionTicks)
            : weatherFixture          ? UiParityWeather::Steps(compositionFamily)
            : transparentHistory      ? std::vector<std::string>{ "history-post-load", "history-opaque-first", "history-opaque-settled",
                                          "history-transparent-first", "history-transparent-second",
                                          "history-transparent-settled", "history-opaque-restored" }
            : fontFixture             ? UiParityFonts::Steps()
                                      : UiParityFixtures::FixtureSteps(compositionFamily);
        if (incremental && compositionFamily == "baseline")
            throw std::invalid_argument("Incremental fixtures require overlap, scroll or text window operations");
#ifdef OPENRCT2_VULKAN_ONLY
        if (renderer != "vulkan")
            throw std::invalid_argument("This build requires --renderer vulkan");
#else
        if (renderer != "software" && renderer != "vulkan")
            throw std::invalid_argument("--renderer must be software or vulkan");
#endif
        const bool vulkan = renderer == "vulkan";
#ifndef UI_PARITY_HAS_VULKAN_CAPTURE
        if (vulkan)
            throw std::invalid_argument("This diagnostic UI build does not include Vulkan capture");
#else
        if (vulkan)
        {
            Ui::Vulkan::Diagnostic::EnableCaptureForTesting();
            Ui::Vulkan::Diagnostic::SetRetainedBalloonPublicationForTesting(retainedBalloons);
            Ui::Vulkan::Diagnostic::SetNativeBalloonFixtureForTesting(nativeBalloons);
            Ui::Vulkan::Diagnostic::SetNativeTerrainFixtureForTesting(nativeTerrain);
        }
#endif
        if (sharedServiceLifecycle && (!vulkan || fixture != "baseline" || retainedBalloons || nativeBalloons
                || requiredBalloonCount != 0 || requireWorldSurfaces != "any"))
            throw std::invalid_argument("Shared lifecycle requires Vulkan baseline without native admission options");
#ifndef OPENRCT2_VULKAN_ONLY
        const auto drawingEngine = vulkan && !sharedServiceLifecycle ? DrawingEngine::vulkan
                                                                    : DrawingEngine::softwareWithHardwareDisplay;
#endif
        const auto output = std::filesystem::absolute(arguments.at("output"));
        const auto profile = std::filesystem::absolute(arguments.at("profile"));
        if (std::filesystem::exists(output) || std::filesystem::exists(profile))
            throw std::runtime_error("Output and profile must both be new directories");
        std::filesystem::create_directories(output);
        std::filesystem::create_directories(profile);
        gCustomUserDataPath = profile.string();
        gCustomOpenRCT2DataPath = std::filesystem::absolute(arguments.at("data")).string();
        gCustomRCT2DataPath = std::filesystem::absolute(arguments.at("rct2")).string();
        if (arguments.contains("rct1"))
            gCustomRCT1DataPath = std::filesystem::absolute(arguments.at("rct1")).string();
        gOpenRCT2Headless = false;
        gOpenRCT2NoGraphics = false;
        gIntegratedBenchmark.enabled = true;
        gIntegratedBenchmark.visible = false;
#ifndef OPENRCT2_VULKAN_ONLY
        gIntegratedBenchmark.drawingEngine = drawingEngine;
#endif
        gIntegratedBenchmark.useVSync = false;
        auto environment = CreatePlatformEnvironment();
        auto& config = Config::Get();
#ifndef OPENRCT2_VULKAN_ONLY
        config.general.drawingEngine = drawingEngine;
#endif
        config.general.windowWidth = std::stoi(value("width", "960"));
        config.general.windowHeight = std::stoi(value("height", "640"));
        if (config.general.windowWidth < 720 || config.general.windowHeight < 480)
            throw std::invalid_argument("The UI fixture must respect the SDL minimum window size 720x480");
        const auto requestedPhysicalWidth = config.general.windowWidth;
        const auto requestedPhysicalHeight = config.general.windowHeight;
        const auto scaleArgument = value("window-scale", "1");
        if (scaleArgument != "1" && scaleArgument != "1.25" && scaleArgument != "1.5" && scaleArgument != "2")
            throw std::invalid_argument("--window-scale must be 1, 1.25, 1.5 or 2");
        const bool scaledFontFixture = scaleArgument != "1";
        config.general.windowScale = std::stof(scaleArgument);
        const auto expectedScaleQuality = scaleArgument == "1" || scaleArgument == "2" ? ScaleQuality::nearestNeighbour
                                                                                       : ScaleQuality::smoothNearestNeighbour;
        if (scaledFontFixture
            && (!fontFixture || config.general.windowWidth != static_cast<int32_t>(960 * config.general.windowScale)
                || config.general.windowHeight != static_cast<int32_t>(640 * config.general.windowScale)))
            throw std::invalid_argument("Scaled font fixtures require physical dimensions equal to 960x640 times scale");
        if (fontFixture)
        {
            config.general.language = LANGUAGE_ENGLISH_UK;
            config.fonts.fileName.clear();
            config.interface.enlargedUi = false;
            config.interface.touchEnhancements = false;
        }
        config.general.inferDisplayDPI = false;
        config.general.useVSync = false;
        config.general.enableHdr10Output = false;
        config.general.showFPS = false;
        config.general.multiThreading = false;
        config.general.playIntro = false;
        config.general.dayNightCycle = false;
        // Loading progress can repaint on a wall-clock throttle. Keep lighting
        // inactive until loading completes so those paints cannot advance its
        // persistent palette adaptation before the numbered fixture sequence.
        config.general.enableLightFx = false;
        config.general.enableLightFxForVehicles = false;
        config.general.renderWeatherEffects = false;
        config.general.renderWeatherGloom = false;
        config.general.landscapeSmoothing = smoothing == "true";
        gPaintStableSort = stableSort == "true";
        config.general.trapCursor = false;
        config.sound.masterSoundEnabled = false;
        config.sound.soundEnabled = false;
        config.sound.rideMusicEnabled = false;
        if (sharedServiceLifecycle && (requestedPhysicalWidth != 960 || requestedPhysicalHeight != 640 || scaledFontFixture))
            throw std::invalid_argument("Shared lifecycle requires a 960x640 scale-1 baseline");
        Ui::RegisterBitmapReader();
#ifdef UI_PARITY_HAS_VULKAN_CAPTURE
        std::shared_ptr<IRenderServiceFactory> serviceFactory;
        std::unique_ptr<Ui::IUiContext> ui;
        std::unique_ptr<UiParitySharedService::Lifecycle> lifecycle;
        if (sharedServiceLifecycle)
        {
            lifecycle = std::make_unique<UiParitySharedService::Lifecycle>();
            ui = Ui::CreateUiContext(*environment, std::make_shared<Ui::DrawingEngineFactory>(lifecycle->Owner()));
            serviceFactory = Renderer::CreateConfiguredRenderServiceFactory(lifecycle->Owner());
        }
        else
            ui = Ui::CreateUiContext(*environment);
        auto context = CreateContext(
            std::move(environment), Audio::CreateDummyAudioContext(), std::move(ui), std::move(serviceFactory));
#else
        auto ui = Ui::CreateUiContext(*environment);
        auto context = CreateContext(std::move(environment), Audio::CreateDummyAudioContext(), std::move(ui));
#endif
        // The environment override selects data roots, while the legacy CSG
        // loader consults these configuration fields directly.
        config.general.rct1Path = gCustomRCT1DataPath;
        config.general.rct2Path = gCustomRCT2DataPath;
        if (!context->Initialise())
            throw std::runtime_error("Actual UI context failed to initialise");
        // Initialise schedules repository loading in the real UI preloader. Join it before loading the fixture.
        static_cast<PreloaderScene*>(context->GetSceneManager()->getPreloaderScene())->WaitForJobs();
        if (arguments.contains("rct1") && !IsCsgLoaded())
            throw std::runtime_error("Requested RCT1 CSG assets were not loaded");
        const auto park = std::filesystem::absolute(arguments.at("park"));
        if (!context->LoadParkFromFile(park.string()))
            throw std::runtime_error("Failed to load UI fixture park");
        auto* mainWindow = WindowGetMain();
        if (mainWindow == nullptr || mainWindow->viewport == nullptr)
            throw std::runtime_error("Fixture did not establish the real main window and viewport");
        auto& viewport = *mainWindow->viewport;
        json_t cherryFixture;
        if (arguments.contains("cherry-fixture"))
        {
            if (compositionFamily != "world-dirty" && !worldMotion)
                throw std::invalid_argument("Cherry substitution requires an explicit world ordering fixture");
            gGamePaused = GAME_PAUSED_NORMAL;
            cherryFixture = UiParity::ApplyCherryTrackFixture(
                context->GetObjectManager(), arguments.at("cherry-fixture"), viewport.width, viewport.height);
            MapInvalidateTileFull({ 154 * 32, 210 * 32 });
            // Preserve the complete map publication: this mutation contributes a
            // tile delta, not a fresh complete-world bootstrap.
            GfxInvalidateScreen();
        }
        json_t sceneLocatorBinding;
        if (compositionFamily == "world-dirty" || worldMotion)
        {
            // The ordinary harness also pauses below; establish that same state
            // before the read-only census, before camera edits or warmup paints.
            gGamePaused = GAME_PAUSED_NORMAL;
            const auto censusTick = getGameState().currentTicks;
            auto census = UiParity::LocateTreeTrackScenes(context->GetObjectManager(), viewport.width, viewport.height);
            if (getGameState().currentTicks != censusTick || gGamePaused != GAME_PAUSED_NORMAL)
                throw std::runtime_error("Tree/track scene census changed the paused simulation state");
            census["simulationTicks"] = censusTick;
            census["paused"] = true;
            census["simulationTicksAfter"] = getGameState().currentTicks;
            Json::WriteToFile((output / "tree-track-scenes.json").string(), census);
            sceneLocatorBinding = { { "simulationTicks", censusTick }, { "viewportExtent", { viewport.width, viewport.height } } };
        }
        const auto cameraPose = [&]() {
            return json_t{ { "viewPosition", { viewport.viewPos.x, viewport.viewPos.y } },
                           { "rotation", viewport.rotation }, { "zoom", static_cast<int8_t>(viewport.zoom) } };
        };
        const auto loadedCameraPose = cameraPose();
        const auto loadedViewportFlags = viewport.flags;
        const json_t loadedTargetPose{ { "viewPosition", { mainWindow->savedViewPos.x, mainWindow->savedViewPos.y } },
                                       { "rotation", viewport.rotation }, { "zoom", static_cast<int8_t>(viewport.zoom) } };
        const auto cameraMode = value("camera-mode", "explicit");
        if (cameraMode != "explicit" && cameraMode != "saved")
            throw std::invalid_argument("--camera-mode must be explicit or saved");
        if (cameraMode == "saved" && (arguments.contains("view-x") || arguments.contains("view-y")))
            throw std::invalid_argument("Saved camera cannot have explicit view coordinates");
        if (cameraMode == "explicit")
        {
            // Omitted coordinates inherit the authoritative loaded window target,
            // not the transient viewport position awaiting its first paint.
            viewport.viewPos = mainWindow->savedViewPos;
            const auto requestedRotation = std::stoi(value("rotation", "0"));
            if (requestedRotation < 0 || requestedRotation > 3)
                throw std::invalid_argument("--rotation must be in 0..3");
            viewport.rotation = static_cast<uint8_t>(requestedRotation);
            const auto requestedZoom = std::stoi(value("zoom", "0"));
            if (requestedZoom < static_cast<int8_t>(ZoomLevel::min()) || requestedZoom > static_cast<int8_t>(ZoomLevel::max()))
                throw std::invalid_argument("--zoom is outside the supported viewport range");
            viewport.zoom = ZoomLevel{ static_cast<int8_t>(requestedZoom) };
        }
        if (arguments.contains("view-x"))
            viewport.viewPos.x = std::stoi(arguments.at("view-x"));
        if (arguments.contains("view-y"))
            viewport.viewPos.y = std::stoi(arguments.at("view-y"));
        if (cameraMode == "explicit")
        {
            // The production painter updates viewport.viewPos from this window target
            // before drawing. Keep both representations consistent; do not bypass its
            // map-boundary clamp. Out-of-range requests must fail the pose checks.
            mainWindow->savedViewPos = viewport.viewPos;
            mainWindow->viewportTargetSprite = EntityId::GetNull();
            mainWindow->viewportSmartFollowSprite = EntityId::GetNull();
            mainWindow->flags.unset(WindowFlag::scrollingToLocation);
        }
        // Loading can leave the viewport at an intermediate position until the
        // first production paint applies savedViewPos. Preserve that loaded target.
        // This harness requires a stationary camera; do not silently clear saved follow state.
        if (cameraMode == "saved"
            && (!mainWindow->viewportTargetSprite.IsNull() || !mainWindow->viewportSmartFollowSprite.IsNull()
                || mainWindow->flags.has(WindowFlag::scrollingToLocation)))
            throw std::runtime_error("Saved camera has active follow/scroll state; stationary capture is not admissible");
        const auto expectedCameraPose = cameraMode == "saved" ? loadedTargetPose : cameraPose();
        const auto requireCameraPose = [&](const char* phase) {
            const auto actual = cameraPose();
            if (cameraMode == "saved"
                && (mainWindow->savedViewPos.x != loadedTargetPose.at("viewPosition").at(0).get<int32_t>()
                    || mainWindow->savedViewPos.y != loadedTargetPose.at("viewPosition").at(1).get<int32_t>()))
                throw std::runtime_error(std::string("Loaded saved camera target changed ") + phase);
            if (actual != expectedCameraPose)
                throw std::runtime_error(std::string("Main viewport camera changed ") + phase + ": expected "
                                         + expectedCameraPose.dump() + ", actual " + actual.dump());
        };
        viewport.flags = viewportFlags;
        const auto initialBalloons = requiredBalloonCount != 0 ? UiParityBalloons::Census() : json_t{};
        if (requiredBalloonCount != 0 && initialBalloons.at("count").get<uint32_t>() != requiredBalloonCount)
            throw std::runtime_error("Immutable fixture does not contain the required positive balloon count");
        gGamePaused = GAME_PAUSED_NORMAL;
        gPaletteEffectFrame = 0;
        gDayNightCycle = 0;
        EntityTweener::get().reset();
        auto* engine = context->GetDrawingEngine();
        if (engine == nullptr
#ifndef OPENRCT2_VULKAN_ONLY
            || context->GetDrawingEngineType() != drawingEngine
#endif
        )
            throw std::runtime_error("Fixture did not select the requested actual display engine");
        if (lightNight)
        {
            config.general.enableLightFx = true;
            engine->Resize(context->GetUiContext().GetWidth(), context->GetUiContext().GetHeight());
            UiParityLight::PinInputs();
        }
        if (weatherFixture)
            UiParityWeather::Initialise(compositionFamily);
        if (fontFixture)
            UiParityFonts::Configure(compositionFamily);
        if (scaledFontFixture
            && (context->GetUiContext().GetWidth() != 960 || context->GetUiContext().GetHeight() != 640
                || context->GetUiContext().GetScaleQuality() != expectedScaleQuality))
            throw std::runtime_error(
                "Actual UI scale did not produce the required logical extent and production filter policy");
        const auto lifecycleTick = getGameState().currentTicks;
        if (worldMotion && (Network::GetMode() != Network::Mode::none || MotionVehicleCensus().at("count").get<size_t>() == 0))
            throw std::runtime_error("World motion requires an offline park containing vehicles");
        uint32_t paintOrdinal = 0;
        const auto draw = [&](bool forceFullInvalidation = true) {
            if (paintOrdinal == 0 && cameraMode == "saved")
            {
                if (cameraPose() != loadedCameraPose)
                    throw std::runtime_error("Saved viewport changed before its initial production paint");
            }
            else
                requireCameraPose("before paint");
            if (sharedServiceLifecycle && getGameState().currentTicks != lifecycleTick)
                throw std::runtime_error("Shared lifecycle advanced the paused simulation tick");
            if (forceFullInvalidation)
                GfxInvalidateScreen();
            engine->BeginDraw();
            if (transparentHistory && historyClearZero)
                engine->GetDrawingContext()->Clear(*engine->getRT(), PaletteIndex::transparent);
            if (!transparentHistory || paintOrdinal != 0)
                context->GetPainter()->Paint(*engine);
            engine->EndDraw();
            paintOrdinal++;
            requireCameraPose("after paint");
        };
        if (!transparentHistory)
        {
            draw();
            draw();
        }
        if (vulkan)
        {
            std::vector<FrameTimings> timings;
            engine->DrainFrameTimings(timings);
        }
        json_t metadata{ { "fixture", "main-ui-" + fixture },
                         { "fixtureVersion", lightNight ? 2 : 1 },
                         { "compositionFamily", compositionFamily },
                         { "renderer", vulkan ? "vulkan" : "softwareWithHardwareDisplay" },
                         { "park", park.string() },
                         { "parkFnv1a64", HashFile(park) },
                         { "inputHashAlgorithm", "FNV-1a-64; build/runner receipts additionally use SHA-256" },
                         { "logicalExtent", { context->GetUiContext().GetWidth(), context->GetUiContext().GetHeight() } },
                         { "viewPosition", { viewport.viewPos.x, viewport.viewPos.y } },
                         { "cameraMode", cameraMode },
                         { "cameraContract",
                           { { "version", 2 }, { "loadedPose", loadedCameraPose }, { "loadedTargetPose", loadedTargetPose },
                             { "expectedPose", expectedCameraPose },
                             { "afterWarmupPose", cameraPose() }, { "warmupPaints", paintOrdinal } } },
                         { "assetState",
                           { { "rct1CsgLoaded", IsCsgLoaded() }, { "rct1Required", arguments.contains("rct1") } } },
                         { "rotation", viewport.rotation },
                         { "zoom", static_cast<int8_t>(viewport.zoom) },
                         { "simulationTicks", getGameState().currentTicks },
                         { "paletteEffectFrame", gPaletteEffectFrame },
                         { "language", config.general.language },
                         { "themePreset", config.interface.currentThemePreset },
                         { "windowScale", config.general.windowScale },
                         { "scaleQuality", static_cast<int32_t>(context->GetUiContext().GetScaleQuality()) },
                         { "sdlVideoDriver", SDL_GetCurrentVideoDriver() },
                         { "landscapeSmoothing", config.general.landscapeSmoothing },
                         { "viewportFlags", viewport.flags },
                         { "requireWorldSurfaces", requireWorldSurfaces },
                         { "state", "paused; paint-only warmup=2; weather/lighting/FPS disabled; no event/tick loop" } };
#ifdef OPENRCT2_VULKAN_ONLY
        metadata["vulkanOnly"] = true;
#endif
        if (!sceneLocatorBinding.is_null())
            metadata["sceneLocator"] = sceneLocatorBinding;
        if (!cherryFixture.is_null())
            metadata["cherryFixture"] = cherryFixture;
        if (transparentHistory)
            metadata["state"] = "paused; no warmup; post-load no-painter frame then opaque/transparent history; no event/tick loop";
        if (worldMotion)
            metadata["state"] = "two paint-only warmups; explicit single gameStateUpdateLogic(false) ticks; authoritative positions; damage/full pairs";
        if (lightNight)
            metadata["state"] = "paused; night=1; sunny20C; vehicles/FPS/precipitation disabled; one palette refresh; two "
                                "warmup paints; no event/tick loop";
        if (weatherFixture)
            metadata["state"] = "paused fixed tick; public weather transitions; LightFX disabled; two clear warmup paints; no "
                                "weather/simulation update";
        if (fontFixture)
        {
            metadata["fonts"] = UiParityFonts::Metadata(compositionFamily);
            metadata["state"] = "paused; real localized UI and font loading; fixed UTF-8 caret updates; no event/tick loop";
        }
        if (scaledFontFixture)
            metadata["scaling"] = {
                { "fixtureVersion", 1 },
                { "requestedWindowScale", config.general.windowScale },
                { "requestedWindowExtent", { requestedPhysicalWidth, requestedPhysicalHeight } },
                { "requiredLogicalExtent", { 960, 640 } },
                { "expectedQuality", static_cast<int32_t>(expectedScaleQuality) },
                { "qualityPolicy", "production UiContext: integer nearest; fractional smooth nearest with linear final stage" }
            };
        struct SampleBytes
        {
            std::vector<uint8_t> rgba;
            std::vector<uint8_t> indexed;
            json_t inputState;
        };
        std::map<std::string, SampleBytes> firstSamples;
        std::map<std::string, size_t> weatherCommandCounts;
        size_t captureCount = 0;
        for (int sample = 0; sample < (sharedServiceLifecycle ? 6 : ordinalSequence ? 1 : 2); sample++)
        {
#ifdef UI_PARITY_HAS_VULKAN_CAPTURE
            if (lifecycle)
            {
                lifecycle->BeforePhase(static_cast<size_t>(sample), *context, output);
                engine = context->GetDrawingEngine();
                if (engine == nullptr)
                    throw std::runtime_error("Lifecycle transition lost the drawing engine");
                if (sample != 0)
                {
                    draw();
                    draw();
                }
            }
#endif
            size_t stepIndex = 0;
            const auto captureStep = [&](const std::string& step, json_t inputState) {
                if (stepIndex >= steps.size() || steps[stepIndex++] != step)
                    throw std::runtime_error("UI fixture capture sequence differs from its declared contract");
                auto name = ordinalSequence ? step : step + "-" + std::to_string(sample);
#ifdef UI_PARITY_HAS_VULKAN_CAPTURE
                if (lifecycle)
                    name = UiParitySharedService::kPhases.at(static_cast<size_t>(sample));
#endif
                UiParity::SdlCapture capture;
                std::vector<uint8_t> indexed;
                std::string admissionError;
#ifdef OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS
                Drawing::Diagnostic::ResetViewportPaintCounts();
#endif
                if (gPaintStableSort != (stableSort == "true"))
                    throw std::runtime_error("Paint stable-sort policy changed before named capture");
                auto sampleMetadata = metadata;
                const auto paintDrawCountBefore = gCurrentDrawCount;
                if (compositionFamily == "world-dirty" && getGameState().currentTicks != lifecycleTick)
                    throw std::runtime_error("Dirty-world static fixture advanced the paused simulation tick");
                sampleMetadata["paintStableSort"] = gPaintStableSort;
                sampleMetadata["worldSurfaceAdmission"] = "unavailable-software";
                const bool forceFullInvalidation = worldMotion ? step.ends_with("-full") || captureCount == 0
                    : !incremental || captureCount == 0;
                if (worldMotion)
                {
                    inputState["invalidation"] = "ordinary-motion-damage-and-explicit-full-control";
                    sampleMetadata["simulationTicks"] = getGameState().currentTicks;
                    sampleMetadata["paletteEffectFrame"] = gPaletteEffectFrame;
                    sampleMetadata["worldMotion"] = { {"schema",1}, {"initialTick",lifecycleTick},
                        {"tickOffset",captureCount / 2}, {"totalTicks",motionTicks}, {"pass",step.ends_with("-full") ? "full" : "damage"},
                        {"positions","authoritative; no tween"}, {"vehicles",MotionVehicleCensus()} };
                }
                if (incremental)
                    inputState["invalidation"] = compositionFamily == "world-dirty"
                        ? "public-render-target-rectangles" : "public-window-operations";
                if (requiredBalloonCount != 0)
                {
                    const auto census = UiParityBalloons::Census();
                    if (census != initialBalloons)
                        throw std::runtime_error("Paused balloon state changed before named paint");
                    sampleMetadata["balloonState"] = census;
                }
                sampleMetadata["step"] = step;
                if (transparentHistory)
                {
                    if (getGameState().currentTicks != lifecycleTick || paintOrdinal + 1 != stepIndex)
                        throw std::runtime_error("Transparent history tick or preceding paint ordinal changed");
                    sampleMetadata["viewportFlags"] = viewport.flags;
                    sampleMetadata["transparentHistory"] = {
                        {"schema",1}, {"phase",step}, {"loadedViewportFlags",loadedViewportFlags},
                        {"warmupViewportFlags",0}, {"warmupPaints",0}, {"paintOrdinal",paintOrdinal + 1},
                        {"viewportFlags",viewport.flags}, {"priorOpaqueCaptures",stepIndex > 3 ? 2 : stepIndex > 1 ? stepIndex - 2 : 0},
                        {"explicitClearZero",historyClearZero}, {"painterCalled",stepIndex != 1},
                        {"canvasSeed","post-load existing canvas; no assumption that park loading performed no paints"}};
                }
                sampleMetadata["repetition"] = sharedServiceLifecycle ? 0 : sample;
#ifdef OPENRCT2_VULKAN_ONLY
                constexpr bool captureVulkan = true;
#else
                const bool captureVulkan = context->GetDrawingEngineType() == DrawingEngine::vulkan;
#endif
                sampleMetadata["renderer"] = captureVulkan
                    ? "vulkan" : "softwareWithHardwareDisplay";
                sampleMetadata["inputState"] = inputState;
                sampleMetadata["forcedFullInvalidation"] = forceFullInvalidation;
                if (captureVulkan)
                {
#ifdef UI_PARITY_HAS_VULKAN_CAPTURE
                    const auto request = Ui::Vulkan::Diagnostic::ArmNextCapture(*engine, name);
                    draw(forceFullInvalidation);
                    auto result = request->Wait(std::chrono::seconds(30));
                    if (result.name != name
                        || result.output.logicalExtent.width != static_cast<uint32_t>(context->GetUiContext().GetWidth())
                        || result.output.logicalExtent.height != static_cast<uint32_t>(context->GetUiContext().GetHeight()))
                        throw std::runtime_error("Vulkan diagnostic frame does not match the named logical target");
                    if (!(transparentHistory && step == "history-post-load")
                        && (result.coverage.opaqueSpriteCount == 0 || result.coverage.opaqueRectCount == 0))
                        throw std::runtime_error("Main UI fixture did not record observable sprite and rectangle commands");
                    capture.name = name;
                    capture.width = result.output.drawableExtent.width;
                    capture.height = result.output.drawableExtent.height;
                    capture.rgba.resize(result.output.rgba.size());
                    std::transform(
                        result.output.rgba.begin(), result.output.rgba.end(), capture.rgba.begin(),
                        [](std::byte pixel) { return std::to_integer<uint8_t>(pixel); });
                    indexed.resize(result.indexed.size());
                    std::transform(result.indexed.begin(), result.indexed.end(), indexed.begin(), [](std::byte pixel) {
                        return std::to_integer<uint8_t>(pixel);
                    });
                    AddVulkanMetadata(sampleMetadata, result);
                    if (nativeTerrain)
                        sampleMetadata["nativeTerrainFixture"] = UiParityTerrain::Describe(result);
                    if (nativeBalloons && !expectBalloonFallback
                        && (result.coverage.nativeBalloonViewports == 0 || result.coverage.cpuBalloonSpriteCalls != 0
                            || result.output.balloonUploads.viewportSubmissions != result.coverage.nativeBalloonViewports
                            || !result.balloonPublication.has_value()
                            || result.output.balloonUploads.epoch != result.balloonPublication->epoch
                            || result.output.balloonUploads.sequence != result.balloonPublication->sequence))
                        admissionError = "Native balloon fixture did not completely replace this frame's CPU balloon paint";
                    if (expectBalloonFallback
                        && (result.coverage.nativeBalloonViewports != 0
                            || result.output.balloonUploads.viewportSubmissions != 0
                            || result.output.balloonUploads.sourceBytes != 0 || result.output.balloonUploads.spriteBytes != 0))
                        admissionError = "Native balloon fixture did not completely decline the expected fallback control";
                    if (retainedBalloons)
                    {
                        const auto& p = result.balloonPublication;
                        if (!p.has_value() || !p->retained || p->count != requiredBalloonCount || p->epoch == 0
                            || p->sequence == 0 || json_t(p->records) != initialBalloons.at("records")
                            || p->metrics.sourceTick != getGameState().currentTicks || p->metrics.bulkCopiedBytes != 0
                            || p->metrics.fallbackIndexedBalloons != requiredBalloonCount)
                            admissionError = "Named retained balloon publication differs from immutable positive fixture "
                                             "census";
                    }
                    sampleMetadata["worldSurfaceAdmission"] = result.coverage.worldSurfaces ? "native" : "ordinary-paint";
                    if (requireWorldSurfaces != "any" && result.coverage.worldSurfaces != (requireWorldSurfaces == "true"))
                        admissionError = "Vulkan world-surface admission differs from --require-world-surfaces at " + name;
                    if (requireWorldSurfaces == "true" && result.coverage.worldSurfaceRecordCount == 0)
                        admissionError = "Vulkan native world-surface capture contains no surface records at " + name;
                    if (lightNight)
                    {
                        const bool commandOnly = !result.coverage.lightFxCpuIntensityAttached;
                        sampleMetadata["gpuLightRouteProof"] = "Enabled successfully presented command-only packet; backend "
                                                               "throws if compute cannot consume it";
                        if (!result.output.lightFxEnabled || result.coverage.lightFxCommandCount == 0 || !commandOnly)
                            admissionError = "Natural main UI LightFX requires nonempty GPU-only accumulation at " + name;
                    }
                    if (fontFixture)
                    {
                        const bool hasTtf = result.coverage.ttfOpaqueCount + result.coverage.ttfTransparentCount != 0;
                        if (hasTtf != UiParityFonts::IsTtf(compositionFamily))
                            admissionError = "Actual TTF command coverage differs from selected font mode at " + name;
                        if (UiParityFonts::IsTtf(compositionFamily) && inputState.at("fontTextInput").contains("textBounds"))
                        {
                            const auto bounds = inputState.at("fontTextInput").at("textBounds").get<std::array<int32_t, 4>>();
                            size_t textCommands = 0;
                            for (const auto& command : result.coverage.ttfClippedBoundsAndThreshold)
                                if (std::max(bounds[0], command[0]) < std::min(bounds[2], command[2])
                                    && std::max(bounds[1], command[1]) < std::min(bounds[3], command[3])
                                    && command[4] == UiParityFonts::ExpectedHintingThreshold(compositionFamily))
                                    textCommands++;
                            sampleMetadata["ttfTextRegionCommandCount"] = textCommands;
                            if (textCommands == 0)
                                admissionError = "No TrueType commands at the prescribed threshold intersect the text-input "
                                                 "content at "
                                    + name;
                        }
                    }
                    if (weatherFixture)
                    {
                        weatherCommandCounts[step] = result.coverage.weatherCount;
                        const bool expected = Config::Get().general.renderWeatherEffects && Weather::hasWeatherEffect();
                        if ((result.coverage.weatherCount != 0) != expected)
                            admissionError = "Weather GPU command coverage differs from prescribed precipitation state at "
                                + name;
                        if (result.output.lightFxEnabled)
                            admissionError = "Isolated weather fixture unexpectedly enabled LightFX";
                    }
#endif
                }
#ifndef OPENRCT2_VULKAN_ONLY
                else
                {
                    UiParity::ArmSdlCapture(name);
                    draw(forceFullInvalidation);
                    capture = UiParity::TakeSdlCapture();
                    indexed = ReadSoftwareCanvas(*engine->getRT());
                }
#endif
#ifdef OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS
                const auto cpuPaint = Drawing::Diagnostic::ReadViewportPaintCounts();
                sampleMetadata["cpuViewportPaint"] = {{"generate",cpuPaint.generate},{"arrange",cpuPaint.arrange},
                    {"draw",cpuPaint.draw},{"scope","all actual viewport column calls in this named synchronous draw, including provisional attempts"}};
#endif
                if (compositionFamily == "world-dirty")
                {
                    sampleMetadata["worldDirtyDrawCount"] = {{"before",paintDrawCountBefore},{"after",gCurrentDrawCount},
                        {"scope","ordinary Painter::Paint counter; never reset by this fixture"}};
                    if (getGameState().currentTicks != lifecycleTick || gCurrentDrawCount != paintDrawCountBefore + 1)
                        throw std::runtime_error("Dirty-world phase did not perform exactly one paused paint");
                }
                if (scaledFontFixture
                    && (capture.width != static_cast<uint32_t>(requestedPhysicalWidth)
                        || capture.height != static_cast<uint32_t>(requestedPhysicalHeight)))
                    throw std::runtime_error("Realized physical capture extent differs from requested scale fixture");
                if (capture.width == 0 || capture.height == 0
                    || capture.rgba.size() != static_cast<size_t>(capture.width) * capture.height * 4)
                    throw std::runtime_error("Empty UI display capture");
                if (indexed.size()
                    != static_cast<size_t>(context->GetUiContext().GetWidth()) * context->GetUiContext().GetHeight())
                    throw std::runtime_error("Indexed capture dimensions do not match the main UI canvas");
                if (lightNight)
                {
                    if (paintOrdinal != UiParityLight::kWarmupPaints + stepIndex)
                        throw std::runtime_error("Lighting paint ordinal differs from fixed fixture sequence");
                    sampleMetadata["lighting"] = UiParityLight::Metadata(paintOrdinal);
                    sampleMetadata["repeatScope"] = "sequence-only; fresh-process repeat comparison belongs to runner";
                }
                if (weatherFixture)
                {
                    if (paintOrdinal != 2 + stepIndex)
                        throw std::runtime_error("Weather paint ordinal differs from prescribed sequence");
                    sampleMetadata["weather"] = { { "beforePaint", inputState.at("weatherBeforePaint") },
                                                  { "afterPaint", UiParityWeather::Metadata() },
                                                  { "paintOrdinal", paintOrdinal } };
                    sampleMetadata["repeatScope"] = "sequence-only; fresh-process repeat comparison belongs to runner";
                }
                if (fontFixture)
                {
                    if (paintOrdinal != 2 + stepIndex)
                        throw std::runtime_error("Font paint ordinal differs from prescribed sequence");
                    sampleMetadata["fonts"]["paintOrdinal"] = paintOrdinal;
                    sampleMetadata["repeatScope"] = "sequence-only; fresh-process repeat comparison belongs to runner";
                }
                if (requiredBalloonCount != 0 && UiParityBalloons::Census() != initialBalloons)
                    admissionError = "Paint mutated the immutable balloon fixture";
                requireCameraPose("before saving capture");
                sampleMetadata["viewPosition"] = { viewport.viewPos.x, viewport.viewPos.y };
                sampleMetadata["mainViewportBounds"] = {viewport.pos.x,viewport.pos.y,viewport.width,viewport.height};
                sampleMetadata["rotation"] = viewport.rotation;
                sampleMetadata["zoom"] = static_cast<int8_t>(viewport.zoom);
                sampleMetadata["cameraContract"]["afterPaintPose"] = cameraPose();
                sampleMetadata["cameraContract"]["paintOrdinal"] = paintOrdinal;
                if (worldMotion)
                {
                    sampleMetadata["worldMotion"]["drawCount"] = {{"before",paintDrawCountBefore},{"after",gCurrentDrawCount}};
#ifdef OPENRCT2_PRESENTATION_SOURCE_TICK_VERSION
                    const auto consumed = ViewportGetPresentationGeneration();
                    if (consumed == nullptr || consumed->entities == nullptr)
                        throw std::runtime_error("Motion capture did not consume an immutable entity publication");
                    auto consumedVehicles = MotionVehicleCensus(consumed->entities.get());
                    consumedVehicles.erase("scenarioRng"); // The published payload does not own the live RNG.
                    sampleMetadata["worldMotion"]["consumedVehicles"] = std::move(consumedVehicles);
                    sampleMetadata["worldMotion"]["consumedSourceTick"] = consumed->sourceTick;
#endif
                    if (sampleMetadata.at("simulationTicks") != getGameState().currentTicks
                        || sampleMetadata.at("paletteEffectFrame") != gPaletteEffectFrame
                        || sampleMetadata.at("worldMotion").at("vehicles") != MotionVehicleCensus()
                        || gCurrentDrawCount != paintDrawCountBefore + 1)
                        admissionError = "Paint changed authoritative motion input or skipped its ordinary paint counter";
                }
                SaveCapture(output, capture, sampleMetadata);
                WriteBytes(output / name / "screen.indexed", indexed);
                // Preserve the actual rendered frame even when the intended native/fallback coverage was not reached.
                if (!admissionError.empty())
                    throw std::runtime_error(admissionError);
                if (sample != 0)
                {
                    const auto& first = firstSamples.at(step);
                    // Retain every diagnostic dirty-world phase even if history changes its pixels.
                    // The runner still requires exact repeats and full-state equality; this is not an exception.
                    if (compositionFamily != "world-dirty"
                        && (capture.rgba != first.rgba || indexed != first.indexed || inputState != first.inputState))
                        throw std::runtime_error("Repeated UI fixture pixels or input state differ at " + step);
                }
                else
                {
                    firstSamples.emplace(step, SampleBytes{ capture.rgba, indexed, inputState });
                }
                if (step == "restored")
                {
                    const auto& empty = firstSamples.at("empty-ui");
                    if (capture.rgba != empty.rgba || indexed != empty.indexed || inputState != empty.inputState)
                        throw std::runtime_error("Closing fixture windows did not restore the initial UI baseline");
                }
                const auto requireChanged = [&](const char* prior) {
                    const auto& previous = firstSamples.at(prior);
                    if (capture.rgba == previous.rgba || indexed == previous.indexed)
                        throw std::runtime_error("UI transition did not visibly change both output layers at " + step);
                };
                if (step == "finances-front")
                    requireChanged("research-front");
                if (step == "scroll-partial-row")
                    requireChanged("scroll-top");
                if (step == "wrapped-caret-end")
                    requireChanged("wrapped-caret-start");
                const auto requireRestoredPixels = [&](const char* prior) {
                    const auto& previous = firstSamples.at(prior);
                    if (capture.rgba != previous.rgba || indexed != previous.indexed)
                        throw std::runtime_error("Weather/palette restoration differs at " + step);
                };
                if (step == "font-localized-window")
                    requireChanged("font-empty-ui");
                if (step == "font-caret-start")
                    requireChanged("font-localized-window");
                if (step == "font-caret-mid")
                    requireChanged("font-caret-start");
                if (step == "font-caret-end")
                    requireChanged("font-caret-mid");
                if (step == "font-screen-clip")
                    requireChanged("font-caret-end");
                if (step == "font-restored")
                    requireRestoredPixels("font-empty-ui");
                if (step == "weather-rain-light")
                    requireChanged("weather-clear");
                if (step == "weather-rain-heavy")
                    requireChanged("weather-rain-light");
                if (step == "weather-rain-occluded")
                    requireChanged("weather-rain-heavy");
                if (step == "weather-snow-light")
                    requireChanged("weather-rain-light");
                if (step == "weather-snow-heavy")
                    requireChanged("weather-snow-light");
                if (step == "weather-rain-restored")
                    requireRestoredPixels("weather-rain-heavy");
                if (step == "weather-cleared")
                    requireRestoredPixels("weather-clear");
                if (step == "palette-gloom-1")
                    requireChanged("palette-clear");
                if (step == "palette-gloom-2")
                    requireChanged("palette-gloom-1");
                if (step == "palette-lightning")
                {
                    const auto& previous = firstSamples.at("palette-gloom-2");
                    if (capture.rgba == previous.rgba || indexed != previous.indexed)
                        throw std::runtime_error("Lightning must change physical colours while retaining gloom-2 indices");
                }
                if (step == "palette-lightning-recovered")
                    requireRestoredPixels("palette-gloom-2");
                if (step == "palette-restored")
                    requireRestoredPixels("palette-clear");
                if (vulkan && step == "weather-rain-heavy"
                    && weatherCommandCounts.at(step) != 2 * weatherCommandCounts.at("weather-rain-light"))
                    throw std::runtime_error("Heavy rain did not double actual GPU weather passes over the unchanged viewport");
                if (vulkan && step == "weather-snow-light"
                    && weatherCommandCounts.at(step) != weatherCommandCounts.at("weather-rain-light"))
                    throw std::runtime_error("Light snow command coverage differs from light rain visible rectangles");
                if (vulkan && step == "weather-snow-heavy"
                    && weatherCommandCounts.at(step) != weatherCommandCounts.at("weather-rain-heavy"))
                    throw std::runtime_error("Heavy snow command coverage differs from heavy rain visible rectangles");
                captureCount++;
            };
            if (worldMotion)
            {
                for (uint32_t tick = 0; tick <= motionTicks; ++tick)
                {
                    if (tick != 0)
                    {
                        if (Network::GetMode() != Network::Mode::none)
                            throw std::runtime_error("Motion fixture became networked");
                        const auto before = getGameState().currentTicks;
                        const auto paused = gGamePaused;
                        gGamePaused = 0;
                        try { gameStateUpdateLogic(false); }
                        catch (...) { gGamePaused = paused; throw; }
                        gGamePaused = paused;
                        if (getGameState().currentTicks != before + 1)
                            throw std::runtime_error("Motion fixture did not advance exactly one logical tick");
                    }
                    for (size_t pass = 0; pass < 2; ++pass)
                        captureStep(steps[static_cast<size_t>(tick) * 2 + pass], UiParityFixtures::WindowInputState());
                }
            }
            else if (lightNight)
            {
                for (const auto& step : steps)
                    captureStep(step, UiParityFixtures::WindowInputState());
            }
            else if (transparentHistory)
            {
                for (size_t phase = 0; phase < steps.size(); phase++)
                {
                    viewport.flags = phase >= 3 && phase <= 5 ? VIEWPORT_FLAG_TRANSPARENT_BACKGROUND : 0;
                    captureStep(steps[phase], UiParityFixtures::WindowInputState());
                }
            }
            else if (fontFixture)
                UiParityFonts::Run(compositionFamily, captureStep);
            else if (weatherFixture)
                UiParityWeather::Run(compositionFamily, captureStep);
            else
                UiParityFixtures::RunFixture(compositionFamily, captureStep);
            if (stepIndex != steps.size())
                throw std::runtime_error("UI fixture omitted an expected capture step");
#ifdef UI_PARITY_HAS_VULKAN_CAPTURE
            if (lifecycle)
                lifecycle->AfterPhase(static_cast<size_t>(sample), *context, getGameState().currentTicks, output);
#endif
        }
        Json::WriteToFile(
            (output / "summary.json").string(),
            json_t{ { "status", "pass" },
                    { "fixture", "main-ui-" + fixture },
                    { "fixtureVersion", lightNight ? 2 : 1 },
                    { "compositionFamily", compositionFamily },
                    { "invalidation", incremental ? "public-window-operations" : "full-screen" },
                    { "steps", steps },
                    { "captures", captureCount },
                    { "repetitions", sharedServiceLifecycle ? 6 : ordinalSequence ? 1 : 2 },
                    { "repeatDifferences", 0 },
                    { "repeatScope",
                      ordinalSequence
                          ? "sequence-only; adjacent frames deliberately not compared; fresh-process repetition is runner-owned"
                          : "complete sequence repeated in one process; fresh-process repetition is runner-owned" } });
#ifdef UI_PARITY_HAS_VULKAN_CAPTURE
        if (lifecycle)
            lifecycle->Shutdown(context, output);
#endif
        std::cout << "Captured " << captureCount << " actual " << renderer << " UI frames for " << fixture << " in " << output
                  << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "UI parity capture failed: " << error.what() << '\n';
        return 1;
    }
}
