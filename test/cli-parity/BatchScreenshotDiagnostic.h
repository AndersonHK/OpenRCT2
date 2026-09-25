// Diagnostic only. GPL-3.0-or-later.
#pragma once
#ifdef OPENRCT2_VULKAN_ONLY
    #include "CaptureImageDiagnostic.h"

    #include <cstdlib>
    #include <fstream>
    #include <functional>
    #include <openrct2/drawing/PresentationGeneration.h>
    #include <openrct2/interface/ScreenshotTiling.h>
    #include <openrct2/object/ObjectManager.h>
    #include <openrct2/park/ParkFile.h>
    #include <set>

namespace BatchScreenshotDiagnostic
{
    using namespace OpenRCT2;

    struct Request
    {
        std::string name;
        CoordsXYZ world;
        Viewport camera;
    };

    inline bool WantsUpstreamExport()
    {
        const auto* value = std::getenv("OPENRCT2_CLI_EXPORT_UPSTREAM_PARK");
        if (value == nullptr)
            return false;
        // Frozen oracle b80a4a84: ParkFile.h current61, minimum57. This is a
        // deliberately bounded diagnostic conversion, not a general save UI.
        if (std::string_view(value) != "61")
            throw std::invalid_argument("Upstream diagnostic export supports only explicit target61");
        return true;
    }

    inline json_t ExportUpstreamCopy(
        IContext& context, const std::filesystem::path& source, const std::filesystem::path& output)
    {
        const auto directory = output / "upstream-export";
        // A new, fixed artifact directory prevents replacing either a previous
        // conversion or the source save. User save paths are only ever read.
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Diagnostic export refuses an existing upstream-export directory");
        const auto path = directory / "park-v61.park";
        ParkFileExporter exporter;
        exporter.TargetVersion = 61;
        exporter.ExportObjectsList = context.GetObjectManager().GetPackableObjects();
        const auto tick = getGameState().currentTicks;
        // Only the optional save preview is omitted. Conversion must not add an
        // unobserved GPU capture after the explicitly requested camera frames.
        const bool previousNoGraphics = gOpenRCT2NoGraphics;
        gOpenRCT2NoGraphics = true;
        try
        {
            exporter.Export(getGameState(), path.string(), kParkFileSaveCompressionLevel);
        }
        catch (...)
        {
            gOpenRCT2NoGraphics = previousNoGraphics;
            throw;
        }
        gOpenRCT2NoGraphics = previousNoGraphics;
        if (getGameState().currentTicks != tick || !std::filesystem::is_regular_file(path)
            || std::filesystem::file_size(path) == 0)
            throw std::runtime_error("Diagnostic export did not produce a static nonempty park copy");
        json_t receipt{
            { "source", std::filesystem::absolute(source).string() },
            { "output", path.string() },
            { "targetVersion", 61 },
            { "oracleRevision", "b80a4a84e92be8e07904b38d1032d0bb88280bb4" },
            { "sourceTick", tick },
            { "packedObjectCount", exporter.ExportObjectsList.size() },
            { "previewPolicy", "minimap only; optional GPU screenshot omitted" },
            { "comparisonPolicy", "Reload this exact converted copy in both renderers; original-save parity is not implied" },
            { "conversionScope", "Fork-only simulation fields are omitted; packable custom objects are embedded" }
        };
        std::ofstream metadata(directory / "conversion.json");
        metadata.exceptions(std::ios::badbit | std::ios::failbit);
        metadata << receipt.dump(2) << '\n';
        return receipt;
    }

    inline int32_t Integer(const json_t& value)
    {
        if (!value.is_number_integer())
            throw std::invalid_argument("Batch screenshot camera fields must be exact integers");
        if (value.is_number_unsigned() && value.get<uint64_t>() > INT32_MAX)
            throw std::invalid_argument("Batch screenshot camera field exceeds int32");
        const auto integer = value.get<int64_t>();
        if (integer < INT32_MIN || integer > INT32_MAX)
            throw std::invalid_argument("Batch screenshot camera field exceeds int32");
        return static_cast<int32_t>(integer);
    }

    inline std::vector<Request> Read(const std::filesystem::path& path, const std::filesystem::path& output)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("Could not open batch screenshot request JSON");
        const auto document = json_t::parse(input);
        if (!document.is_array() || document.empty() || document.size() > 64)
            throw std::invalid_argument("Batch screenshot JSON must contain1..64 camera entries");
        std::vector<Request> requests;
        std::set<std::string> names;
        for (const auto& entry : document)
        {
            Request request;
            request.name = entry.at("name").get<std::string>();
            auto nameKey = request.name;
            for (auto& letter : nameKey)
                if (letter >= 'A' && letter <= 'Z')
                    letter = static_cast<char>(letter - 'A' + 'a');
            if (request.name.empty() || request.name.size() > 80
                || !std::ranges::all_of(
                    request.name,
                    [](char c) {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                            || c == '_';
                    })
                || !names.insert(nameKey).second)
                throw std::invalid_argument("Batch screenshot names must be unique alphanumeric/dash/underscore labels");
            if (std::filesystem::exists(output / request.name))
                throw std::invalid_argument("Batch screenshot refuses an existing frame directory: " + request.name);
            request.camera.width = Integer(entry.at("width"));
            request.camera.height = Integer(entry.at("height"));
            const int zoom = Integer(entry.at("zoom")), rotation = Integer(entry.at("rotation"));
            if (request.camera.width < 1 || request.camera.height < 1 || request.camera.width > 4096
                || request.camera.height > 4096 || zoom < static_cast<int8_t>(ZoomLevel::min())
                || zoom > static_cast<int8_t>(ZoomLevel::max()) || rotation < 0 || rotation > 3)
                throw std::invalid_argument("Batch screenshot camera exceeds its bounded extent/zoom/rotation domain");
            const auto& world = entry.at("worldXYZ");
            if (!world.is_array() || world.size() != 3)
                throw std::invalid_argument("Batch screenshot worldXYZ must contain exactly three integers");
            request.world = { Integer(world[0]), Integer(world[1]), Integer(world[2]) };
            // Bound projection arithmetic while permitting views just outside the map.
            if (request.world.x < -65536 || request.world.x > 65536 || request.world.y < -65536 || request.world.y > 65536
                || request.world.z < -65536 || request.world.z > 65536)
                throw std::invalid_argument("Batch screenshot worldXYZ exceeds the diagnostic camera domain");
            request.camera.zoom = ZoomLevel{ static_cast<int8_t>(zoom) };
            request.camera.rotation = static_cast<uint8_t>(rotation);
            const auto projected = Translate3DTo2DWithZ(request.camera.rotation, request.world);
            request.camera.viewPos = { projected.x - request.camera.ViewWidth() / 2,
                                       projected.y - request.camera.ViewHeight() / 2 };
            requests.push_back(std::move(request));
        }
        return requests;
    }

    inline json_t Run(
        int argc, const char** argv, const std::shared_ptr<Drawing::IRenderServiceFactory>& factory,
        const std::filesystem::path& output, const std::function<void(const std::filesystem::path&)>& observeFrame)
    {
        if (argc != 4 || std::string_view(argv[1]) != "screenshot" || !factory || !factory->IsEnabled())
            throw std::invalid_argument("Batch screenshot requires: screenshot park batch.json");
        const auto destination = std::filesystem::absolute(output);
        const bool exportUpstream = WantsUpstreamExport();
        auto requests = Read(std::filesystem::absolute(argv[3]), destination);
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        auto context = CreateContext(
            CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(), Ui::CreateDummyUiContext(), factory);
        if (!context->Initialise() || !context->LoadParkFromFile(argv[2]))
            throw std::runtime_error("Batch screenshot could not initialise and load its park");
        gLegacyScene = LegacyScene::playing;
        // Match the ordinary screenshot command's background policy for every
        // camera, including later views of the same retained publication.
        if (Config::Get().general.transparentScreenshot)
            for (auto& request : requests)
                request.camera.flags |= VIEWPORT_FLAG_TRANSPARENT_BACKGROUND;
        const auto tick = getGameState().currentTicks;
        ResetAllSpriteQuadrantPlacements();
        const auto publication = ViewportCaptureAuxiliaryGeneration();
        if (!publication || publication->sourceTick != tick)
            throw std::runtime_error("Batch screenshot captured a stale or absent publication");
        auto& service = context->GetRenderService();
        json_t frames = json_t::array();
        for (const auto& request : requests)
        {
            const auto frameDirectory = destination / request.name;
            std::filesystem::create_directories(frameDirectory);
            observeFrame(frameDirectory);
            const auto image = ScreenshotTiling::Render(
                service, request.camera, Drawing::gPalette,
                [publication](Drawing::RenderTarget& target, const Viewport& camera) {
                    ViewportRender(target, &camera, ViewportGenerationDomain::fullViewportHeight, publication);
                });
            if (getGameState().currentTicks != tick)
                throw std::runtime_error("Batch screenshot advanced simulation during its static publication");
            const auto imagePath = frameDirectory / "screen.png";
            Imaging::WriteToFile(imagePath.string(), image, ImageFormat::png);
            json_t frame{ { "name", request.name },
                          { "tick", tick },
                          { "publicationTick", publication->sourceTick },
                          { "extent", { request.camera.width, request.camera.height } },
                          { "worldXYZ", { request.world.x, request.world.y, request.world.z } },
                          { "viewXY", { request.camera.viewPos.x, request.camera.viewPos.y } },
                          { "zoom", static_cast<int8_t>(request.camera.zoom) },
                          { "rotation", request.camera.rotation },
                          { "image", imagePath.string() } };
            // Preserve completed-frame attribution even if a later request fails.
            std::ofstream metadata(frameDirectory / "frame.json");
            metadata.exceptions(std::ios::badbit | std::ios::failbit);
            metadata << frame.dump(2) << '\n';
            frames.push_back(std::move(frame));
            observeFrame(destination);
        }
        // Downgrade writing may normalise unsupported simulation fields. Keep
        // the original-save captures above independent of that conversion.
        const json_t exported = exportUpstream ? ExportUpstreamCopy(*context, argv[2], destination) : json_t(nullptr);
        return { { "sourceTick", tick },
                 { "finalTick", getGameState().currentTicks },
                 { "publicationCount", 1 },
                 { "frames", frames },
                 { "requestFile", std::filesystem::absolute(argv[3]).string() },
                 { "upstreamExport", exported },
                 { "simulationPolicy", "static; one immutable publication shared by all cameras; no update calls" } };
    }
} // namespace BatchScreenshotDiagnostic
#endif
