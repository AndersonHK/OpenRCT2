// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once

#include <openrct2/drawing/IDrawingEngine.h>

#ifdef OPENRCT2_VULKAN_ONLY
    #include <algorithm>
    #include <charconv>
    #include <filesystem>
    #include <openrct2/Context.h>
    #include <openrct2/Game.h>
    #include <openrct2/GameState.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/audio/AudioContext.h>
    #include <openrct2/config/Config.h>
    #include <openrct2/core/Imaging.h>
    #include <openrct2/core/Json.hpp>
    #include <openrct2/drawing/Palette.h>
    #include <openrct2/drawing/RenderService.h>
    #include <openrct2/interface/Screenshot.h>
    #include <openrct2/interface/Viewport.h>
    #include <openrct2/park/ParkPreview.h>
    #include <openrct2/ui/UiContext.h>
    #include <openrct2/world/Map.h>
    #include <stdexcept>
    #include <string_view>

namespace CaptureImageDiagnostic
{
    using namespace OpenRCT2;

    inline int32_t Integer(const char* text)
    {
        const std::string_view input(text);
        int32_t value{};
        const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
        if (result.ec != std::errc{} || result.ptr != input.data() + input.size())
            throw std::invalid_argument("CaptureImage diagnostic needs exact integer camera arguments");
        return value;
    }

    inline json_t Run(
        int argc, const char** argv, const std::shared_ptr<Drawing::IRenderServiceFactory>& factory, bool parkPreview = false)
    {
        // Deliberately reuse the screenshot CLI's explicit positional contract. The
        // ordinary dispatcher and all frozen reference behaviour remain unchanged.
        if (argc < 7 || std::string_view(argv[1]) != "screenshot")
            throw std::invalid_argument("CaptureImage diagnostic needs explicit screenshot arguments");
        const bool giant = std::string_view(argv[4]) == "giant";
        const int expected = giant ? 7 : 11;
        const bool transparent = argc == expected + 1;
        if ((argc != expected && !transparent)
            || (transparent && std::string_view(argv[expected]) != "--transparent"))
            throw std::invalid_argument("CaptureImage diagnostic accepts only an optional --transparent switch");
        if (parkPreview && (giant || transparent))
            throw std::invalid_argument("Park preview diagnostic requires bounded ordinary screenshot arguments");
        if (!factory || !factory->IsEnabled())
            throw std::runtime_error("CaptureImage diagnostic requires the configured Vulkan service");
        if (Config::Get().general.transparentScreenshot)
            throw std::runtime_error("CaptureImage diagnostic requires explicit screenshot transparency policy");
        const auto destination = std::filesystem::absolute(argv[3]);
        if (std::filesystem::exists(destination))
            throw std::runtime_error("CaptureImage diagnostic output already exists");
        CaptureOptions options;
        options.Filename = "capture-image.png";
        options.Transparent = transparent;
        const auto zoom = Integer(argv[giant ? 5 : 9]);
        const auto rotation = Integer(argv[giant ? 6 : 10]);
        if (zoom < static_cast<int8_t>(ZoomLevel::min()) || zoom > static_cast<int8_t>(ZoomLevel::max())
            || rotation < 0 || rotation > 3)
            throw std::invalid_argument("CaptureImage diagnostic camera is outside the supported range");
        options.Zoom = ZoomLevel{ static_cast<int8_t>(zoom) };
        options.Rotation = static_cast<uint8_t>(rotation);
        if (!giant)
        {
            const auto width = Integer(argv[4]);
            const auto height = Integer(argv[5]);
            if (width <= 0 || height <= 0)
                throw std::invalid_argument("CaptureImage diagnostic extent must be positive");
            options.View = CaptureView{ width, height, { Integer(argv[6]), Integer(argv[7]) } };
        }
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        auto context = CreateContext(
            CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(), Ui::CreateDummyUiContext(), factory);
        if (!context->Initialise() || !context->LoadParkFromFile(argv[2]))
            throw std::runtime_error("CaptureImage diagnostic could not initialise and load its park");
        gLegacyScene = LegacyScene::playing;
        if (parkPreview)
        {
            const auto& state = getGameState();
            if (WindowGetMain() != nullptr || state.park.entrances.empty()
                || options.View->Width != 250 || options.View->Height != 200 || zoom != 1)
                throw std::runtime_error("Park preview diagnostic needs the entrance camera, 250x200 extent and zoom1");
            const auto& entrance = state.park.entrances.front();
            const CoordsXYZ position{ entrance.x + 16, entrance.y + 16, entrance.z + 32 };
            Viewport camera{};
            camera.width = 250;
            camera.height = 200;
            camera.zoom = ZoomLevel{ 1 };
            camera.rotation = DirectionReverse(entrance.direction);
            const auto origin = centre2dCoordinates(position, &camera);
            if (!origin)
                throw std::runtime_error("Park preview entrance camera is invalid");
            const auto tick = state.currentTicks;
            const auto preview = generatePreviewFromGameState(state);
            const PreviewImage* screenshot = nullptr;
            for (const auto& image : preview.images)
            {
                if (image.type != PreviewImageType::screenshot)
                    continue;
                if (screenshot != nullptr)
                    throw std::runtime_error("Park preview returned duplicate screenshot images");
                screenshot = &image;
            }
            if (!screenshot || screenshot->width != 250 || screenshot->height != 200 || state.currentTicks != tick)
                throw std::runtime_error("Park preview omitted its screenshot or changed extent/simulation tick");
            Image image;
            image.Width = screenshot->width;
            image.Height = screenshot->height;
            image.Depth = 8;
            image.Stride = image.Width;
            image.Palette = Drawing::gPalette;
            image.Pixels.resize(static_cast<size_t>(image.Width) * image.Height);
            std::transform(screenshot->pixels, screenshot->pixels + image.Pixels.size(), image.Pixels.begin(),
                           [](Drawing::PaletteIndex index) { return static_cast<uint8_t>(index); });
            Imaging::WriteToFile(destination.string(), image, ImageFormat::png);
            // Screenshot CLI applies zoom to initially unzoomed dimensions before
            // assigning viewport.zoom. Its explicit centre therefore matches this
            // entrance centre, with no inferred pixel alignment or camera mutation.
            const auto projected = Translate3DTo2DWithZ(camera.rotation, position);
            const ScreenCoordsXY cliOrigin{ projected.x - ((250 << zoom) / 2), projected.y - ((200 << zoom) / 2) };
            if (cliOrigin != *origin)
                throw std::runtime_error("Park preview and explicit CLI camera derivations disagree");
            return { { "api", "generatePreviewFromGameState" }, { "cameraSource", "first-park-entrance" },
                     { "extent", { 250, 200 } }, { "zoom", 1 }, { "rotation", camera.rotation }, { "flags", 0 },
                     { "transparent", false }, { "worldPosition", { position.x, position.y, position.z } },
                     { "cliEquivalentWorldPosition", { position.x, position.y, position.z } },
                     { "viewPosition", { origin->x, origin->y } }, { "cliEquivalentViewPosition", { cliOrigin.x, cliOrigin.y } },
                     { "cameraDerivation", "entrance+(16,16,32), reversed entrance direction; projected centre minus zoomed half extent" },
                     { "tickBefore", tick }, { "tickAfter", state.currentTicks }, { "sourceTick", tick },
                     { "copiedOutput", destination.string() }, { "serializer", "diagnostic PNG of production PreviewImage indices" } };
        }
        auto& environment = context->GetPlatformEnvironment();
        const auto screenshotDirectory = std::filesystem::u8path(environment.GetDirectoryPath(DirBase::user, DirId::screenshots));
        std::filesystem::create_directories(screenshotDirectory);
        const auto produced = screenshotDirectory / options.Filename;
        if (std::filesystem::exists(produced))
            throw std::runtime_error("CaptureImage diagnostic needs a fresh screenshot profile");
        json_t metadata{ { "api", "CaptureImage" }, { "giant", giant }, { "zoom", zoom }, { "rotation", rotation },
                         { "transparent", transparent }, { "productionOutput", produced.string() },
                         { "flags", transparent ? VIEWPORT_FLAG_TRANSPARENT_BACKGROUND : 0 },
                         { "copiedOutput", destination.string() } };
        if (options.View)
        {
            const auto position = options.View->Position;
            const auto z = TileElementHeight(position);
            if (z != Integer(argv[8]))
                throw std::invalid_argument("CaptureImage terrain-derived camera Z differs from the explicit CLI reference Z: "
                                            + std::to_string(z));
            metadata["extent"] = { options.View->Width, options.View->Height };
            metadata["worldPosition"] = { position.x, position.y, z };
            const auto projected = Translate3DTo2DWithZ(options.Rotation, CoordsXYZ{ position, z });
            metadata["viewPosition"] = { projected.x - options.Zoom.ApplyTo(options.View->Width) / 2,
                                          projected.y - options.Zoom.ApplyTo(options.View->Height) / 2 };
        }
        const auto tick = getGameState().currentTicks;
        metadata["sourceTick"] = tick;
        metadata["tickBefore"] = tick;
        CaptureImage(options);
        metadata["tickAfter"] = getGameState().currentTicks;
        if (getGameState().currentTicks != tick)
            throw std::runtime_error("CaptureImage advanced simulation during the static capture");
        // Production owns filename validation and PNG writing. Copy only its
        // completed result to the caller's artifact path, preserving the original.
        std::filesystem::copy_file(produced, destination);
        return metadata;
    }
}
#endif
