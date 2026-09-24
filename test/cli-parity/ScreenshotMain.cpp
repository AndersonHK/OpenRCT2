/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
// Diagnostic driver; configured mode observes the production factory without changing its policy.
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <openrct2-renderer/RenderServiceFactory.h>
#include <openrct2-renderer/gpu/GpuGraphicsLookupTables.h>
#include <openrct2-renderer/vulkan/VulkanRenderService.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/command_line/CommandLine.hpp>
#include <openrct2/config/Config.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <span>
#include "SoftwareTileDiagnostic.h"
#include "CaptureImageDiagnostic.h"

namespace
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    namespace Gpu = OpenRCT2::Ui::Gpu;

    struct Diagnostics
    {
        std::filesystem::path directory;
        std::atomic_uint serviceCreations{};
        std::atomic_uint deviceCreations{};
        json_t capture;
        bool giant{};
        uint32_t liveSessions{}, peakSessions{}, begunSessions{}, retiredSessions{};
        json_t tiles = json_t::array();
        json_t tileBegins = json_t::array();
        json_t assetState;
        std::shared_ptr<Vulkan::DeviceContext> device;
        std::mutex deviceMutex;
        json_t enabledChecks = json_t::array();
    };
    void WriteBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        if (std::filesystem::exists(path))
            throw std::runtime_error("Capture output already exists: " + path.string());
        std::ofstream file(path, std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    json_t Palette(const RenderPalette& palette)
    {
        json_t result = json_t::array();
        for (const auto& colour : palette)
            result.push_back({ colour.red, colour.green, colour.blue, colour.alpha });
        return result;
    }
    class Completion final : public IRenderCompletion
    {
        std::shared_ptr<IRenderCompletion> _inner;
        std::shared_ptr<Diagnostics> _diagnostics;
        OffscreenRenderRequest _request;
        bool _saved{};

    public:
        Completion(
            std::shared_ptr<IRenderCompletion> inner, std::shared_ptr<Diagnostics> diagnostics, OffscreenRenderRequest request)
            : _inner(std::move(inner))
            , _diagnostics(std::move(diagnostics))
            , _request(std::move(request))
        {
        }
        const RenderSubmissionIdentity& GetIdentity() const noexcept override
        {
            return _inner->GetIdentity();
        }
        void Cancel() override
        {
            _inner->Cancel();
        }
        RenderOutcome Wait(std::chrono::milliseconds timeout) override
        {
            auto outcome = _inner->Wait(timeout);
            if (outcome.result && !_saved)
            {
                const auto& result = *outcome.result;
                if (!_diagnostics->directory.empty())
                {
                    std::filesystem::create_directories(_diagnostics->directory);
                    if (_diagnostics->giant)
                        WriteBytes(_diagnostics->directory / (result.identity.name + ".indexed"), result.indexed);
                    else
                    {
                        WriteBytes(_diagnostics->directory / "screen.indexed", result.indexed);
                        WriteBytes(_diagnostics->directory / "screen.rgba", result.rgba);
                    }
                }
                _diagnostics->capture = { { "name", result.identity.name },
                                          { "submissionId", result.identity.submissionId },
                                          { "targetId", result.identity.targetId },
                                          { "targetGeneration", result.identity.targetGeneration },
                                          { "logicalExtent", { result.logicalExtent.width, result.logicalExtent.height } },
                                          { "outputExtent", { result.outputExtent.width, result.outputExtent.height } },
                                          { "requestPalette", Palette(_request.palette) },
                                          { "resultPalette", Palette(result.palette) },
                                          { "alphaPolicy", "transparentIndexZero" },
                                          { "alphaPolicyValue", static_cast<uint8_t>(_request.alphaPolicy) },
                                          { "scaleQuality", static_cast<uint8_t>(_request.scaleQuality) },
                                          { "indexedOutput", _request.indexedOutput },
                                          { "rgbaOutput", _request.rgbaOutput },
                                          { "lightingEnabled", _request.lightingEnabled },
                                          { "clearIndex", _request.clearIndex },
                                          { "indexedBytes", result.indexed.size() },
                                          { "rgbaBytes", result.rgba.size() } };
                if (_diagnostics->giant)
                {
                    auto tile = _diagnostics->capture;
                    tile["tickAtReadback"] = getGameState().currentTicks;
                    tile["completionIdentityMatches"] = outcome.identity == GetIdentity()
                        && result.identity == outcome.identity;
                    tile["ordinal"] = _diagnostics->tiles.size();
                    _diagnostics->tiles.push_back(std::move(tile));
                    _diagnostics->capture = nullptr;
                }
                _saved = true;
            }
            return outcome;
        }
    };
    class Session final : public IRenderSession
    {
        std::unique_ptr<IRenderSession> _inner;
        std::shared_ptr<Diagnostics> _diagnostics;
        OffscreenRenderRequest _request;

    public:
        Session(std::unique_ptr<IRenderSession> inner, std::shared_ptr<Diagnostics> diagnostics, OffscreenRenderRequest request)
            : _inner(std::move(inner))
            , _diagnostics(std::move(diagnostics))
            , _request(std::move(request))
        {
            if (_diagnostics->giant)
            {
                ++_diagnostics->begunSessions;
                ++_diagnostics->liveSessions;
                _diagnostics->peakSessions = std::max(_diagnostics->peakSessions, _diagnostics->liveSessions);
                _diagnostics->tileBegins.push_back({ { "name", _request.name },
                    { "extent", { _request.logicalExtent.width, _request.logicalExtent.height } },
                    { "tick", getGameState().currentTicks }, { "liveSessions", _diagnostics->liveSessions },
                    { "completedBeforeBegin", _diagnostics->tiles.size() },
                    { "retiredBeforeBegin", _diagnostics->retiredSessions } });
            }
        }
        ~Session() override
        {
            _inner.reset();
            if (_diagnostics->giant)
            {
                --_diagnostics->liveSessions;
                ++_diagnostics->retiredSessions;
            }
        }
        IDrawingContext& GetDrawingContext() override
        {
            return _inner->GetDrawingContext();
        }
        RenderTarget& GetRenderTarget() override
        {
            return _inner->GetRenderTarget();
        }
        void Cancel() noexcept override
        {
            _inner->Cancel();
        }
        std::shared_ptr<IRenderCompletion> Submit() override
        {
            return std::make_shared<Completion>(_inner->Submit(), _diagnostics, std::move(_request));
        }
    };
    class Service final : public IRenderService
    {
        std::unique_ptr<IRenderService> _inner;
        std::shared_ptr<Diagnostics> _diagnostics;

    public:
        Service(std::unique_ptr<IRenderService> inner, std::shared_ptr<Diagnostics> diagnostics)
            : _inner(std::move(inner))
            , _diagnostics(std::move(diagnostics))
        {
        }
        std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest request) override
        {
            return std::make_unique<Session>(_inner->BeginOffscreen(request), _diagnostics, request);
        }
        void Shutdown() noexcept override
        {
            _inner->Shutdown();
        }
        void InvalidateImage(uint32_t image) override { _inner->InvalidateImage(image); }
    };
    class Factory final : public IRenderServiceFactory
    {
        std::shared_ptr<Diagnostics> _diagnostics;
        std::shared_ptr<IRenderServiceFactory> _configured;

    public:
        explicit Factory(
            std::shared_ptr<Diagnostics> diagnostics, std::shared_ptr<IRenderServiceFactory> configured = {})
            : _diagnostics(std::move(diagnostics))
            , _configured(std::move(configured))
        {
        }
        bool IsEnabled() const override
        {
            if (!_configured)
                return true;
            const bool enabled = _configured->IsEnabled();
#ifdef OPENRCT2_VULKAN_ONLY
            _diagnostics->enabledChecks.push_back({ { "renderer", "vulkan" }, { "enabled", enabled } });
#else
            _diagnostics->enabledChecks.push_back(
                { { "configuredEngine", static_cast<int32_t>(Config::Get().general.drawingEngine) },
                  { "benchmarkOverride", gIntegratedBenchmark.drawingEngine.has_value() }, { "enabled", enabled } });
#endif
            return enabled;
        }
        std::unique_ptr<IRenderService> Create() override
        {
            ++_diagnostics->serviceCreations;
            // Called by the lazy Context on the owner thread, after park and graphics resources are ready.
            const bool csgLoaded = IsCsgLoaded();
            if (!gCustomRCT1DataPath.empty() && !csgLoaded)
                throw std::runtime_error("Requested RCT1 CSG assets were not loaded");
            uint32_t g1Records = 0;
            uint32_t g1Payloads = 0;
            for (uint32_t index = 0; index < SPR_G1_END; ++index)
            {
                const auto* asset = GfxGetG1Element(index);
                if (asset)
                {
                    ++g1Records;
                    if (asset->offset)
                        ++g1Payloads;
                }
            }
            if (g1Records != SPR_G1_END || g1Payloads == 0)
                throw std::runtime_error("Required original G1 records were not loaded");
            _diagnostics->assetState = { { "rct1Required", !gCustomRCT1DataPath.empty() },
                                         { "rct1CsgLoaded", csgLoaded },
                                         { "g1RecordCount", g1Records },
                                         { "g1PayloadCount", g1Payloads },
                                         { "configuredRct1Path", Config::Get().general.rct1Path },
                                         { "configuredRct2Path", Config::Get().general.rct2Path } };
            // Observe owned output while delegating all lookup, shader-path and device policy to production.
            if (_configured)
                return std::make_unique<Service>(_configured->Create(), _diagnostics);
            const auto tables = Gpu::CaptureGraphicsLookupTables();
            Vulkan::RenderServiceOptions options;
            if (const auto* path = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                options.shaderDirectory = path;
            else
                options.shaderDirectory = std::filesystem::u8path(GetContext()->GetPlatformEnvironment().GetDirectoryPath(
                                              DirBase::openrct2, DirId::shaders))
                    / "vulkan";
            options.remapPalette.assign(tables.remap.begin(), tables.remap.end());
            if (tables.hasBlend)
                options.blendPalette.assign(tables.blend.begin(), tables.blend.end());
            auto factory = Vulkan::CreateRenderServiceFactory(std::move(options), [state = _diagnostics] {
                const std::lock_guard lock(state->deviceMutex);
                if (!state->device)
                {
                    ++state->deviceCreations;
                    state->device = Vulkan::DeviceContext::CreateGraphicsOnly();
                }
                return state->device;
            });
            return std::make_unique<Service>(factory->Create(), _diagnostics);
        }
    };
} // namespace

int main(int argc, const char** argv)
{
    const auto diagnostics = std::make_shared<Diagnostics>();
    const auto* captureImageMode = std::getenv("OPENRCT2_CLI_CAPTURE_IMAGE");
    const bool captureImage = captureImageMode && std::string(captureImageMode) == "1";
    const auto* parkPreviewMode = std::getenv("OPENRCT2_CLI_PARK_PREVIEW");
    const bool parkPreview = parkPreviewMode && std::string(parkPreviewMode) == "1";
    const auto* giantMode = std::getenv("OPENRCT2_CLI_GIANT_PARITY");
    diagnostics->giant = captureImage || (giantMode && std::string(giantMode) == "1");
    if (const auto* path = std::getenv("OPENRCT2_CLI_PARITY_ARTIFACTS"))
        diagnostics->directory = path;
    const auto* mode = std::getenv("OPENRCT2_DIAGNOSTIC_OFFSCREEN_SCREENSHOT");
    const bool vulkan = mode && std::string(mode) == "1";
    const auto* configuredMode = std::getenv("OPENRCT2_CLI_CONFIGURED_FACTORY");
    const bool configured = configuredMode && std::string(configuredMode) == "1";
    std::shared_ptr<Vulkan::DeviceContextOwner> owner;
    std::shared_ptr<IRenderServiceFactory> factory;
    if (configured)
    {
        owner = std::make_shared<Vulkan::DeviceContextOwner>(false);
        factory = std::make_shared<Factory>(diagnostics, Renderer::CreateConfiguredRenderServiceFactory(owner));
    }
    else if (vulkan)
        factory = std::make_shared<Factory>(diagnostics);
    json_t softwareTiling;
    json_t imageOperationMetadata;
    int result = EXIT_FAILURE;
    std::string error;
    try
    {
#ifdef OPENRCT2_VULKAN_ONLY
        if (!configured && !vulkan)
            throw std::runtime_error("Software screenshot diagnostics require the external frozen software reference.");
#endif
        const auto requirePath = [](const char* variable) {
            const auto* path = std::getenv(variable);
            if (!path || !*path)
                throw std::runtime_error(std::string("Diagnostic isolation requires ") + variable);
            return std::filesystem::absolute(path).string();
        };
        gCustomUserDataPath = requirePath("OPENRCT2_ORACLE_USER_PATH");
        gCustomOpenRCT2DataPath = requirePath("OPENRCT2_ORACLE_DATA_PATH");
        gCustomRCT2DataPath = requirePath("OPENRCT2_ORACLE_RCT2_PATH");
        if (const auto* path = std::getenv("OPENRCT2_ORACLE_RCT1_PATH"); path && *path)
            gCustomRCT1DataPath = std::filesystem::absolute(path).string();
        std::filesystem::create_directories(gCustomUserDataPath);
        // CreatePlatformEnvironment reloads config when the actual handler constructs its Context.
        // Persist only these isolated profile fields so CSG's config-based lookup agrees with environment roots.
        auto environment = CreatePlatformEnvironment();
        Config::Get().general.rct1Path = gCustomRCT1DataPath;
        Config::Get().general.rct2Path = gCustomRCT2DataPath;
        if (!Config::SaveToPath(environment->GetFilePath(PathId::config)))
            throw std::runtime_error("Could not seed isolated screenshot profile");
        const auto* softwareTileMode = std::getenv("OPENRCT2_SOFTWARE_TILE_DIAGNOSTIC");
        if (captureImage || parkPreview)
        {
#ifdef OPENRCT2_VULKAN_ONLY
            if (!configured || diagnostics->directory.empty() || (captureImage && parkPreview)
                || (parkPreview && diagnostics->giant)
                || (softwareTileMode && std::string(softwareTileMode) == "1"))
                throw std::runtime_error("CaptureImage diagnostic requires configured mode, artifacts and no software override");
            imageOperationMetadata = CaptureImageDiagnostic::Run(argc, argv, factory, parkPreview);
            result = EXIT_SUCCESS;
#else
            throw std::runtime_error("CaptureImage diagnostic is available only in the current Vulkan-only build");
#endif
        }
        else if (softwareTileMode && std::string(softwareTileMode) == "1")
        {
            if (configured || vulkan || diagnostics->giant)
                throw std::runtime_error("Software tile diagnostic cannot enable a service or giant observer");
            softwareTiling = SoftwareTileDiagnostic::Run(argc, argv, diagnostics->directory);
            result = EXIT_SUCCESS;
        }
        else
        {
            const auto code = CommandLineRun(argv, argc, factory);
            if (code == CommandLine::ExitCode::launch)
                throw std::runtime_error("Diagnostic screenshot driver supports command handlers only, not the game loop");
            result = code == CommandLine::ExitCode::fail ? EXIT_FAILURE : EXIT_SUCCESS;
        }
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        std::cerr << error << '\n';
    }
    if (!diagnostics->directory.empty())
    {
        std::filesystem::create_directories(diagnostics->directory);
        json_t report = { { "schema", 1 },
                                { "fixture", "screenshot-cli" },
                                { "fixtureVersion", 1 },
#ifdef OPENRCT2_VULKAN_ONLY
                                { "mode", (configured || vulkan) ? "vulkan" : "unsupported" },
#else
                                { "mode", vulkan ? "vulkan" : "software" },
#endif
                                { "exitCode", result },
                                { "error", error },
                                { "serviceCreations", diagnostics->serviceCreations.load() },
                                { "deviceCreations", diagnostics->deviceCreations.load() },
                                { "capture", diagnostics->capture },
                                { "assetState", diagnostics->assetState } };
        if (!softwareTiling.is_null())
        {
            report["fixture"] = "software-giant-tiling-r2z0";
            report["softwareTiling"] = softwareTiling;
        }
        if (diagnostics->giant)
        {
            report["fixture"] = "screenshot-cli-giant";
            report["fixtureVersion"] = 1;
            report["tileBegins"] = diagnostics->tileBegins;
            report["tiles"] = diagnostics->tiles;
            report["sessions"] = { { "begun", diagnostics->begunSessions },
                { "retired", diagnostics->retiredSessions }, { "live", diagnostics->liveSessions },
                { "peak", diagnostics->peakSessions } };
        }
        if (configured)
        {
            // The production owner cannot replace its device. IsCreated observes zero/one without bootstrapping it.
            const bool created = owner->IsCreated();
#ifdef OPENRCT2_VULKAN_ONLY
            report["mode"] = "vulkan";
#else
            report["mode"] = Config::Get().general.drawingEngine == DrawingEngine::vulkan ? "vulkan" : "software";
#endif
            report["deviceCreations"] = created ? 1 : 0;
            report["productionFactory"] = {
#ifdef OPENRCT2_VULKAN_ONLY
                { "kind", "configured" }, { "renderer", "vulkan" },
#else
                { "kind", "configured" }, { "configuredEngine", static_cast<int32_t>(Config::Get().general.drawingEngine) },
                { "benchmarkOverride", gIntegratedBenchmark.drawingEngine.has_value() },
#endif
                { "enabledChecks", diagnostics->enabledChecks }, { "ownerCreated", created },
                { "deviceObservation", "persistent-owner-created-state" } };
        }
        if (captureImage)
        {
            report["fixture"] = "screenshot-capture-image";
            report["captureImage"] = imageOperationMetadata;
        }
        if (parkPreview)
        {
            report["fixture"] = "screenshot-park-preview";
            report["parkPreview"] = imageOperationMetadata;
        }
        const auto text = report.dump(2) + "\n";
        WriteBytes(diagnostics->directory / "report.json", std::as_bytes(std::span(text)));
    }
    std::cout << "Offscreen service creations: " << diagnostics->serviceCreations.load()
              << ", devices: " << (configured ? static_cast<unsigned>(owner->IsCreated()) : diagnostics->deviceCreations.load())
              << '\n';
    return result;
}
