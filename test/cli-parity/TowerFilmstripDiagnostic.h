// Diagnostic only. GPL-3.0-or-later.
#pragma once
#ifdef OPENRCT2_VULKAN_ONLY
    #include "CaptureImageDiagnostic.h"

    #include <array>
    #include <fstream>
    #include <functional>
    #include <openrct2/drawing/PresentationGeneration.h>
    #include <openrct2/drawing/VehiclePresentation.h>
    #include <openrct2/entity/EntityList.h>
    #include <openrct2/interface/ScreenshotTiling.h>
    #include <openrct2/ride/Vehicle.h>

namespace TowerFilmstripDiagnostic
{
    using namespace OpenRCT2;

    inline json_t Pose(uint16_t ride)
    {
        json_t result = json_t::array();
        for (const auto* car : EntityList<Vehicle>())
        {
            if (car->ride.ToUnderlying() != ride)
                continue;
            result.push_back({ { "entity", car->id.ToUnderlying() },
                               { "worldXYZ", { car->x, car->y, car->z } },
                               { "orientation", car->orientation },
                               { "animation", car->animation_frame },
                               { "restraints", car->restraints_position },
                               { "riders", car->num_peeps },
                               { "status", static_cast<uint8_t>(car->status) } });
        }
        if (result.size() != 1)
            throw std::runtime_error("Tower filmstrip requires exactly one actual vehicle on the selected ride");
        return result;
    }

    inline json_t Run(
        int argc, const char** argv, const std::shared_ptr<Drawing::IRenderServiceFactory>& factory,
        const std::filesystem::path& output, const std::function<void(const std::filesystem::path&)>& observeFrame)
    {
        // Use the ordinary explicit screenshot camera contract, but write a filmstrip
        // directory instead of one PNG. No vehicle pose, ride state or RNG is patched.
        if (argc != 11 || std::string_view(argv[1]) != "screenshot" || !factory || !factory->IsEnabled())
            throw std::invalid_argument("Tower filmstrip requires screenshot park output width height x y z zoom rotation");
        const auto integer = CaptureImageDiagnostic::Integer;
        const auto width = integer(argv[4]), height = integer(argv[5]);
        const auto zoom = integer(argv[9]), rotation = integer(argv[10]);
        if (width < 1 || height < 1 || width > 2048 || height > 2048 || zoom < -1 || zoom > 3 || rotation < 0 || rotation > 3)
            throw std::invalid_argument("Tower filmstrip camera exceeds its bounded diagnostic domain");
        const char* tickText = std::getenv("OPENRCT2_TOWER_MAX_TICKS");
        const int maxTicks = tickText ? integer(tickText) : 6000;
        if (maxTicks < 1 || maxTicks > 12000)
            throw std::invalid_argument("Tower filmstrip tick budget must be 1..12000");
        const auto destination = std::filesystem::absolute(argv[3]);
        if (destination != std::filesystem::absolute(output) || std::filesystem::exists(destination / "trajectory.jsonl"))
            throw std::invalid_argument("Tower filmstrip needs a fresh output matching OPENRCT2_CLI_PARITY_ARTIFACTS");
        std::filesystem::create_directories(destination);
        std::ofstream trajectory(destination / "trajectory.jsonl");
        trajectory.exceptions(std::ios::badbit | std::ios::failbit);
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        auto context = CreateContext(
            CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(), Ui::CreateDummyUiContext(), factory);
        if (!context->Initialise() || !context->LoadParkFromFile(argv[2]))
            throw std::runtime_error("Tower filmstrip could not initialise and load its park");
        gLegacyScene = LegacyScene::playing;
        Viewport camera{};
        camera.width = width;
        camera.height = height;
        camera.zoom = ZoomLevel{ static_cast<int8_t>(zoom) };
        camera.rotation = static_cast<uint8_t>(rotation);
        if (Config::Get().general.transparentScreenshot)
            camera.flags |= VIEWPORT_FLAG_TRANSPARENT_BACKGROUND;
        const CoordsXYZ centre{ integer(argv[6]), integer(argv[7]), integer(argv[8]) };
        const auto projected = Translate3DTo2DWithZ(camera.rotation, centre);
        camera.viewPos = { projected.x - camera.ViewWidth() / 2, projected.y - camera.ViewHeight() / 2 };
        constexpr uint16_t ride = 181;
        constexpr std::array<int, 3> boundaries{ 432, 464, 496 };
        std::array<std::array<bool, 3>, 2> crossed{}, approaching{};
        json_t frames = json_t::array(), crossings = json_t::array();
        auto pose = Pose(ride);
        const auto identity = pose[0]["entity"];
        const auto initialTick = getGameState().currentTicks;
        int previousZ = pose[0]["worldXYZ"][2].get<int>();
        int previousDirection = 0, tail = 0;
        bool firstMotion = true;
        const auto capture = [&](const json_t& framePose, const json_t& reason) {
            if (frames.size() >= 64)
                throw std::runtime_error("Tower filmstrip exceeded 64 bounded captures");
            const auto tick = getGameState().currentTicks;
            const auto frameName = "tick-" + std::to_string(tick);
            const auto frameDirectory = destination / frameName;
            std::filesystem::create_directories(frameDirectory);
            observeFrame(frameDirectory);
            ResetAllSpriteQuadrantPlacements();
            const auto publication = ViewportCaptureAuxiliaryGeneration();
            if (!publication || publication->sourceTick != tick || !publication->vehicles
                || publication->vehicles->sourceTick != tick || !publication->vehicles->records)
                throw std::runtime_error("Tower filmstrip captured a stale or absent vehicle publication");
            const auto& records = *publication->vehicles->records;
            const auto found = std::ranges::find_if(
                records, [&](const auto& record) { return record.entityId == framePose[0]["entity"].get<uint32_t>(); });
            if (found == records.end() || found->GetRideId() != ride
                || json_t::array({ found->x, found->y, found->z }) != framePose[0]["worldXYZ"])
                throw std::runtime_error("Tower publication does not contain the observed authoritative vehicle pose");
            const auto image = ScreenshotTiling::Render(
                context->GetRenderService(), camera, Drawing::gPalette,
                [publication](Drawing::RenderTarget& target, const Viewport& tileCamera) {
                    ViewportRender(target, &tileCamera, ViewportGenerationDomain::fullViewportHeight, publication);
                });
            if (getGameState().currentTicks != tick || Pose(ride) != framePose)
                throw std::runtime_error("Tower screenshot mutated the simulation tick or selected vehicle pose");
            Imaging::WriteToFile((frameDirectory / "screen.png").string(), image, ImageFormat::png);
            frames.push_back({ { "tick", tick },
                               { "publicationTick", publication->sourceTick },
                               { "pose", framePose },
                               { "reason", reason },
                               { "image", (frameDirectory / "screen.png").string() } });
            observeFrame(destination);
        };
        trajectory << json_t{ { "tick", initialTick }, { "pose", pose } }.dump() << '\n';
        capture(pose, { "initial" });
        int steps = 0;
        for (; steps < maxTicks; ++steps)
        {
            const auto tick = getGameState().currentTicks;
            const auto pause = gGamePaused;
            gGamePaused = 0;
            try
            {
                gameStateUpdateLogic(false);
            }
            catch (...)
            {
                gGamePaused = pause;
                throw;
            }
            gGamePaused = pause;
            if (getGameState().currentTicks != tick + 1)
                throw std::runtime_error("Tower diagnostic did not advance exactly one actual simulation tick");
            pose = Pose(ride);
            if (pose[0]["entity"] != identity)
                throw std::runtime_error("Tower diagnostic vehicle identity changed during its filmstrip");
            trajectory << json_t{ { "tick", getGameState().currentTicks }, { "pose", pose } }.dump() << '\n';
            const int z = pose[0]["worldXYZ"][2].get<int>();
            const int direction = (z > previousZ) - (z < previousZ);
            json_t reasons = json_t::array();
            if (direction != 0)
            {
                if (firstMotion || (previousDirection != 0 && direction != previousDirection))
                    reasons.push_back(firstMotion ? "first-motion" : "direction-reversal");
                firstMotion = false;
                const auto row = direction > 0 ? 0 : 1;
                for (size_t i = 0; i < boundaries.size(); ++i)
                {
                    const int boundary = boundaries[i];
                    const bool passed = direction > 0 ? previousZ < boundary && z >= boundary
                                                      : previousZ > boundary && z <= boundary;
                    const int remaining = (boundary - z) * direction;
                    if (!approaching[row][i] && !crossed[row][i] && remaining >= 0 && remaining <= 4)
                    {
                        approaching[row][i] = true;
                        reasons.push_back("approach-" + std::to_string(boundary));
                    }
                    if (passed && !crossed[row][i])
                    {
                        crossed[row][i] = true;
                        crossings.push_back({ { "tick", getGameState().currentTicks },
                                              { "boundary", boundary },
                                              { "direction", direction },
                                              { "previousZ", previousZ },
                                              { "worldZ", z } });
                        reasons.push_back("cross-" + std::to_string(boundary));
                        tail = 3;
                    }
                }
                previousDirection = direction;
            }
            if (!reasons.empty() || tail > 0)
            {
                if (tail > 0)
                {
                    reasons.push_back("consecutive-crossing-window");
                    --tail;
                }
                capture(pose, reasons);
            }
            previousZ = z;
            if (tail == 0 && std::ranges::all_of(crossed[0], [](bool x) { return x; })
                && std::ranges::all_of(crossed[1], [](bool x) { return x; }))
            {
                ++steps;
                break;
            }
        }
        trajectory.flush();
        observeFrame(destination);
        return { { "ride", ride },
                 { "initialTick", initialTick },
                 { "finalTick", getGameState().currentTicks },
                 { "simulationSteps", steps },
                 { "maxTicks", maxTicks },
                 { "crossed", crossed },
                 { "coverageComplete", crossings.size() == 6 },
                 { "crossings", crossings },
                 { "frames", frames },
                 { "cameraWorldXYZ", { centre.x, centre.y, centre.z } },
                 { "rotation", rotation },
                 { "zoom", zoom },
                 { "simulationPolicy", "real gameStateUpdateLogic(false), temporary unpause only; no pose edits" } };
    }
} // namespace TowerFilmstripDiagnostic
#endif
