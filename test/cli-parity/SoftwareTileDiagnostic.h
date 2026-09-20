// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
// Diagnostic only: unchanged linked X8 painter, fixed accepted giant-input-01 r2z0 camera.
#include <openrct2/Game.h>
#include <openrct2/core/Imaging.h>
#include <openrct2/drawing/NewDrawing.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/interface/ScreenshotTiling.h>
#include <cstring>

namespace SoftwareTileDiagnostic
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;

    inline void Require(bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    }
    inline void Bytes(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        Require(!std::filesystem::exists(path), "Diagnostic output already exists");
        std::ofstream output(path, std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    inline json_t Camera(const Viewport& view)
    {
        return { { "viewPosition", { view.viewPos.x, view.viewPos.y } },
            { "extent", { view.width, view.height } }, { "position", { view.pos.x, view.pos.y } },
            { "rotation", view.rotation }, { "zoom", static_cast<int8_t>(view.zoom) }, { "flags", view.flags } };
    }
    inline std::vector<uint8_t> Paint(IContext& context, const Viewport& view, int32_t width, int32_t height,
        ViewportGenerationDomain domain = ViewportGenerationDomain::targetClip)
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height, 0);
        X8DrawingEngine engine(context.GetUiContext());
        RenderTarget target{};
        target.bits = reinterpret_cast<PaletteIndex*>(pixels.data());
        target.width = width; target.height = height; target.DrawingEngine = &engine;
        engine.BeginDraw();
        ViewportRender(target, &view, domain);
        engine.EndDraw();
        return pixels;
    }
    inline void Save(const std::filesystem::path& folder, const std::vector<uint8_t>& pixels,
        const Viewport& view, const GamePalette& palette)
    {
        Require(!std::filesystem::exists(folder), "Diagnostic image folder already exists");
        std::filesystem::create_directories(folder);
        std::vector<uint8_t> rgb, alpha(256, 255), rgba;
        alpha[0] = 0;
        for (const auto& colour : palette)
            rgb.insert(rgb.end(), { colour.red, colour.green, colour.blue });
        rgba.reserve(pixels.size() * 4);
        for (const auto index : pixels)
            rgba.insert(rgba.end(), { palette[index].red, palette[index].green, palette[index].blue, alpha[index] });
        Bytes(folder / "indexed.bin", pixels); Bytes(folder / "palette.bin", rgb);
        Bytes(folder / "alpha.bin", alpha); Bytes(folder / "rgba.bin", rgba);
        Image image;
        image.Width = view.width; image.Height = view.height; image.Depth = 8;
        image.Stride = view.width; image.Palette = palette; image.Pixels = pixels;
        Imaging::WriteToFile((folder / "screen.png").string(), image, ImageFormat::png);
    }
    inline json_t Run(int argc, const char** argv, const std::filesystem::path& output)
    {
        Require(argc == 7 || argc == 8, "Software tile diagnostic requires screenshot park output giant 0 2 [--transparent]");
        Require(std::string(argv[1]) == "screenshot" && std::string(argv[4]) == "giant"
            && std::string(argv[5]) == "0" && std::string(argv[6]) == "2", "Diagnostic accepts only r2z0 giant arguments");
        const bool transparent = argc == 8;
        const auto* generationPolicy = std::getenv("OPENRCT2_SOFTWARE_TILE_FULL_GENERATION");
        const bool fullGeneration = generationPolicy && std::string(generationPolicy) == "1";
        Require(!transparent || std::string(argv[7]) == "--transparent", "Unsupported diagnostic option");
        Require(!output.empty() && !std::filesystem::exists(output), "Diagnostic needs a new artifact directory");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        auto context = CreateContext();
        Require(Config::Get().general.drawingEngine == DrawingEngine::softwareWithHardwareDisplay,
            "Diagnostic must use the isolated software configuration");
        Require(!Config::Get().general.transparentScreenshot, "Diagnostic requires explicit background policy");
        Require(context->Initialise(), "Headless screenshot context initialization failed");
        DrawingEngineInit();
        Require(context->LoadParkFromFile(argv[2]), "Diagnostic park load failed");
        gLegacyScene = LegacyScene::playing;
        Require(getGameState().mapSize.x == 96 && getGameState().mapSize.y == 96, "Diagnostic requires accepted 96-square giant fixture");
        Require(IsCsgLoaded(), "Required CSG assets not loaded");
        uint32_t g1Records = 0, g1Payloads = 0;
        for (uint32_t index = 0; index < SPR_G1_END; index++)
            if (const auto* asset = GfxGetG1Element(index))
            {
                ++g1Records;
                if (asset->offset) ++g1Payloads;
            }
        Require(g1Records == SPR_G1_END && g1Payloads > 0, "Original G1 assets incomplete");
        Viewport whole{};
        // Deliberately explicit, checked against the accepted fixture receipt by the runner.
        // This varies only painter subdivision, not the bounding/camera algorithm.
        whole.width = 6016; whole.height = 3488; whole.viewPos = { -3008, -3536 };
        whole.zoom = ZoomLevel{ 0 }; whole.rotation = 2;
        if (transparent) whole.flags |= VIEWPORT_FLAG_TRANSPARENT_BACKGROUND;
        const auto tick = getGameState().currentTicks;
        const auto palette = gPalette;
        ResetAllSpriteQuadrantPlacements();
        const auto full = Paint(*context, whole, whole.width, whole.height);
        Save(output / "full", full, whole, palette);
        std::vector<uint8_t> tiled(full.size(), 0);
        json_t tiles = json_t::array();
        ResetAllSpriteQuadrantPlacements(); // Same once-per-whole reset as the actual tiled CLI path.
        for (int32_t y = 0; y < whole.height;)
        {
            const auto height = std::min(ScreenshotTiling::kTileSide, whole.height - y);
            for (int32_t x = 0; x < whole.width;)
            {
                const auto width = std::min(ScreenshotTiling::kTileSide, whole.width - x);
                const ScreenshotTiling::Tile tile{ x, y, width, height };
                const auto view = ScreenshotTiling::TileViewport(whole, tile);
                const auto pixels = Paint(*context, view, width, height,
                    fullGeneration ? ViewportGenerationDomain::fullViewportHeight : ViewportGenerationDomain::targetClip);
                for (int32_t row = 0; row < height; row++)
                    std::memcpy(tiled.data() + static_cast<size_t>(y + row) * whole.width + x,
                        pixels.data() + static_cast<size_t>(row) * width, width);
                const auto filename = "tile-" + std::to_string(tiles.size()) + ".indexed";
                Bytes(output / filename, pixels);
                tiles.push_back({ { "ordinal", tiles.size() }, { "bounds", { x, y, width, height } },
                    { "camera", Camera(view) }, { "indexed", filename }, { "tick", getGameState().currentTicks } });
                x += width;
            }
            y += height;
        }
        Save(output / "tiled", tiled, whole, palette);
        ResetAllSpriteQuadrantPlacements();
        const auto fullAfter = Paint(*context, whole, whole.width, whole.height);
        Require(fullAfter == full, "Full X8 rendering changed after tile sequence");
        Require(tick == getGameState().currentTicks, "Diagnostic advanced simulation");
        Require(std::memcmp(palette.data(), gPalette.data(), sizeof(palette)) == 0, "Diagnostic changed palette");
        size_t differences = 0;
        for (size_t i = 0; i < full.size(); i++) differences += full[i] != tiled[i];
        return { { "schema", 1 }, { "fixture", "software-giant-tiling-r2z0" },
            { "camera", Camera(whole) }, { "tileSide", ScreenshotTiling::kTileSide }, { "tiles", tiles },
            { "tickBefore", tick }, { "tickAfter", getGameState().currentTicks }, { "fullAfterTiledExact", true },
            { "fullTiledDifferentPixels", differences }, { "transparent", transparent },
            { "generationDomain", fullGeneration ? "fullViewportHeight" : "targetClip" },
            { "configuredEngine", static_cast<int32_t>(Config::Get().general.drawingEngine) },
            { "landscapeSmoothing", Config::Get().general.landscapeSmoothing },
            { "assetState", { { "csgLoaded", IsCsgLoaded() }, { "g1Records", g1Records }, { "g1Payloads", g1Payloads } } },
            { "renderer", "unchanged-linked-X8DrawingEngine" }, { "serviceCreations", 0 }, { "deviceCreations", 0 } };
    }
}
