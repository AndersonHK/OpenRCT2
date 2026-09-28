/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#ifdef ENABLE_VULKAN
    #include "VulkanParityTestSupport.h"

    #include <SDL.h>
    #include <cstdlib>
    #include <memory>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanPlatform.h>
    #include <openrct2/drawing/LightFX.h>
    #include <openrct2/drawing/X8DrawingEngine.h>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    using namespace OpenRCT2::Drawing;
    using namespace VulkanParitySupport;

    struct SpatialCase
    {
        const char* name;
        uint32_t kind;
        Gpu::Extent extent{ 321, 279 };
    };

    void CheckSdl(int result, const char* operation)
    {
        if (result < 0)
            throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }

    uint64_t HashBytes(std::span<const std::byte> bytes)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (const auto byte : bytes)
            hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1099511628211ULL;
        return hash;
    }

    class VulkanSpatialLightParityTest : public testing::TestWithParam<SpatialCase>
    {
    protected:
        bool ownsVideo = false;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> softwareWindow{ nullptr, SDL_DestroyWindow };
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{ nullptr, SDL_DestroyRenderer };
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> vulkanWindow{ nullptr, SDL_DestroyWindow };
        std::unique_ptr<Gpu::Backend> backend;
        SDL_RendererInfo rendererInfo{};
        std::filesystem::path shaders;
        std::vector<std::byte> falloffs;

        std::string Initialise()
        {
            const auto extent = GetParam().extent;
            ownsVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
            if (ownsVideo && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                return SDL_GetError();
            softwareWindow.reset(SDL_CreateWindow(
                "Frozen CPU spatial LightFX", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, extent.width, extent.height,
                SDL_WINDOW_HIDDEN));
            if (!softwareWindow)
                return SDL_GetError();
            for (int i = 0; i < SDL_GetNumRenderDrivers(); i++)
            {
                SDL_RendererInfo candidate{};
                if (SDL_GetRenderDriverInfo(i, &candidate) != 0 || (candidate.flags & SDL_RENDERER_ACCELERATED) == 0)
                    continue;
                renderer.reset(SDL_CreateRenderer(softwareWindow.get(), i, SDL_RENDERER_ACCELERATED));
                if (renderer)
                    break;
            }
            if (!renderer)
                return "Accelerated SDL reference renderer unavailable";
            CheckSdl(SDL_GetRendererInfo(renderer.get(), &rendererInfo), "SDL renderer metadata");
            // HardwareDisplay selects ARGB8888 first. This fixture intentionally
            // requires that exact format rather than inventing a fallback oracle.
            if (std::find(
                    rendererInfo.texture_formats, rendererInfo.texture_formats + rendererInfo.num_texture_formats,
                    static_cast<Uint32>(SDL_PIXELFORMAT_ARGB8888))
                == rendererInfo.texture_formats + rendererInfo.num_texture_formats)
                return "SDL reference does not expose HardwareDisplay ARGB8888 texture format";
            int width{}, height{};
            CheckSdl(SDL_GetRendererOutputSize(renderer.get(), &width, &height), "SDL drawable extent");
            if (width != static_cast<int>(extent.width) || height != static_cast<int>(extent.height))
                return "Spatial fixture requires unscaled SDL output";
            vulkanWindow.reset(SDL_CreateWindow(
                "GPU spatial LightFX", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, extent.width, extent.height,
                SDL_WINDOW_HIDDEN | Vulkan::Platform::GetRequiredSdlWindowFlags()));
            if (!vulkanWindow)
                return SDL_GetError();
            const auto drawable = Vulkan::Platform::GetDrawableExtent(vulkanWindow.get());
            if (drawable.width != extent.width || drawable.height != extent.height)
                return "Spatial fixture requires equal Vulkan drawable extent";
            if (const auto* path = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                shaders = path;
            if (shaders.empty() || !std::filesystem::is_regular_file(shaders / "lightfx_accumulate.comp.spv"))
                return "Pinned OPENRCT2_VULKAN_SHADER_DIRECTORY required";
            Gpu::BackendConfig config{ .logicalExtent = extent,
                                       .drawableExtent = extent,
                                       .presentMode = Gpu::PresentMode::Immediate,
                                       .frameAcquireMode = Gpu::FrameAcquireMode::Wait,
                                       .shaderDirectory = shaders.string(),
                                       .enableDiagnosticCapture = true };
            backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(vulkanWindow.get()));
            backend->Initialise(config);
            if (!backend->SupportsGpuLightFxRasterization())
                return "Required GPU spatial LightFX accumulation unsupported";
            // This procedural bake does not read game assets or advance a world frame.
            // The implementation belongs to the protected frozen LightFX source.
            LightFx::Init();
            falloffs = LightFx::CaptureBakedFalloffs();
            backend->SetLightFxFalloffs(falloffs);
            return {};
        }

        std::vector<std::byte> RenderSdl(std::span<const std::byte> rgba)
        {
            const auto extent = GetParam().extent;
            std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
                SDL_CreateTexture(
                    renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, extent.width, extent.height),
                SDL_DestroyTexture);
            if (!texture)
                throw std::runtime_error(SDL_GetError());
            CheckSdl(SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE), "Opaque SDL screen texture");
            std::vector<uint32_t> packed(rgba.size() / 4);
            std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> format(
                SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888), SDL_FreeFormat);
            if (!format)
                throw std::runtime_error(SDL_GetError());
            for (size_t i = 0; i < packed.size(); i++)
                packed[i] = SDL_MapRGB(
                    format.get(), std::to_integer<uint8_t>(rgba[i * 4]), std::to_integer<uint8_t>(rgba[i * 4 + 1]),
                    std::to_integer<uint8_t>(rgba[i * 4 + 2]));
            CheckSdl(SDL_UpdateTexture(texture.get(), nullptr, packed.data(), extent.width * 4), "Upload CPU lit pixels");
            CheckSdl(SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr), "Present CPU lit pixels");
            std::vector<std::byte> result(rgba.size());
            CheckSdl(
                SDL_RenderReadPixels(renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA32, result.data(), extent.width * 4),
                "Capture actual SDL output");
            SDL_RenderPresent(renderer.get());
            return result;
        }

        void TearDown() override
        {
            if (backend)
                backend->Dispose();
            backend.reset();
            vulkanWindow.reset();
            renderer.reset();
            softwareWindow.reset();
            if (ownsVideo)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    };
} // namespace

TEST_P(VulkanSpatialLightParityTest, CommandOnlyGpuMatchesFrozenCpuAndSdl)
{
    std::string unavailable;
    try
    {
        unavailable = Initialise();
    }
    catch (const std::exception& error)
    {
        unavailable = error.what();
    }
    if (!unavailable.empty())
    {
        const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        if (required != nullptr && std::string_view(required) == "1")
            FAIL() << unavailable;
        GTEST_SKIP() << unavailable;
    }
    const auto& fixture = GetParam();
    const auto extent = fixture.extent;
    const int32_t width = static_cast<int32_t>(extent.width), height = static_cast<int32_t>(extent.height);
    std::vector<LightFx::FrameSnapshot::ResolvedLight> lights;
    auto add = [&](int32_t x, int32_t y, uint32_t type, uint8_t intensity) {
        LightFx::FrameSnapshot::ResolvedLight command;
        if (LightFx::ResolveLightCommandForCanvas(
                x, y, extent.width, extent.height, static_cast<LightFx::LightType>(type), intensity, command))
            lights.push_back(command);
    };
    if (fixture.kind < 8)
        add(width / 2, height / 2, fixture.kind + 4, 255);
    else if (fixture.kind == 8)
    {
        constexpr uint8_t intensities[]{ 0, 1, 63, 127, 128, 254, 255, 255 };
        for (uint32_t i = 0; i < 8; i++)
            add(width / 2 + static_cast<int32_t>(i % 3) * 5, height / 2 - static_cast<int32_t>(i % 2) * 7, i + 4,
                intensities[i]);
        for (int i = 0; i < 4; i++)
            add(width / 2, height / 2, 7, 255);
    }
    else if (fixture.kind == 9)
    {
        const std::array<std::pair<int32_t, int32_t>, 8> centres{ { { -7, -9 },
                                                                    { width / 2, -3 },
                                                                    { width + 5, -7 },
                                                                    { -1, height / 2 },
                                                                    { width + 4, height / 2 },
                                                                    { -3, height + 5 },
                                                                    { width / 2, height + 1 },
                                                                    { width + 9, height + 2 } } };
        for (uint32_t i = 0; i < 8; i++)
            add(centres[i].first, centres[i].second, i + 4, static_cast<uint8_t>(255 - i * 17));
    }
    else if (fixture.kind == 10)
    {
        for (uint32_t i = 0; i < 8; i++)
            add(width / 2 + static_cast<int32_t>(i) - 4, height / 2, i + 4, static_cast<uint8_t>(31 + i * 32));
    }

    std::vector<PaletteIndex> indices(static_cast<size_t>(width) * height);
    RenderTarget target{ .bits = indices.data(), .width = width, .height = height };
    X8DrawingContext software(nullptr);
    software.BeginDraw();
    for (int32_t y = 0; y < height; y++)
        for (int32_t x = 0; x < width; x++)
            software.FillRect(target, static_cast<PaletteIndex>((x / 13 + y / 11 * 17) & 255), x, y, x, y, false);
    software.EndDraw();
    std::array<std::byte, 1024> palette{}, lightPalette{};
    for (size_t i = 0; i < 256; i++)
    {
        palette[i * 4] = static_cast<std::byte>(i % 53);
        palette[i * 4 + 1] = static_cast<std::byte>((i * 7) % 61);
        palette[i * 4 + 2] = static_cast<std::byte>((i * 11) % 73);
        palette[i * 4 + 3] = lightPalette[i * 4 + 3] = std::byte{ 255 };
        lightPalette[i * 4] = static_cast<std::byte>(17 + i % 67);
        lightPalette[i * 4 + 1] = static_cast<std::byte>(11 + (i * 3) % 79);
        lightPalette[i * 4 + 2] = static_cast<std::byte>(7 + (i * 5) % 97);
    }
    backend->SetPalette(palette);
    Gpu::FrameCommandStream commands;
    for (int32_t y = 0; y < height; y++)
        for (int32_t x = 0; x < width; x++)
            commands.opaqueRects.allocate() = { .clip = { 0, 0, width, height },
                                                .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
                                                .colour = static_cast<uint8_t>(indices[y * width + x]),
                                                .bounds = { x, y, x + 1, y + 1 },
                                                .depth = 0,
                                                .zoom = 1.0f };

    std::filesystem::path artifacts;
    if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
        artifacts = std::filesystem::path(path) / "SpatialLight";
    // The overlap case additionally removes all lights on the same backend. This
    // measures actual accumulator clearing across frames rather than a new device.
    const int frameCount = fixture.kind == 8 ? 2 : 1;
    for (int ordinal = 0; ordinal < frameCount; ordinal++)
    {
        if (ordinal != 0)
            lights.clear();
        std::vector<uint8_t> intensity(indices.size());
        ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(extent.width, extent.height, lights, intensity));
        const auto nonzero = std::count_if(intensity.begin(), intensity.end(), [](auto value) { return value != 0; });
        const auto saturated = std::count(intensity.begin(), intensity.end(), 255);
        if (!lights.empty())
        {
            ASSERT_GT(nonzero, 0);
            const auto [minimum, maximum] = std::minmax_element(intensity.begin(), intensity.end());
            ASSERT_LT(*minimum, *maximum);
            if (fixture.kind < 8)
                ASSERT_LT(nonzero, static_cast<ptrdiff_t>(intensity.size()));
        }
        else
            ASSERT_EQ(nonzero, 0);
        if (fixture.kind == 8 && ordinal == 0)
            ASSERT_GT(saturated, 0);
        auto cpuRgba = Expand(std::as_bytes(std::span(indices)), palette);
        for (size_t i = 0; i < intensity.size(); i++)
            for (size_t channel = 0; channel < 3; channel++)
            {
                const uint32_t dark = std::to_integer<uint8_t>(cpuRgba[i * 4 + channel]);
                const uint32_t light = std::to_integer<uint8_t>(lightPalette[static_cast<uint8_t>(indices[i]) * 4 + channel]);
                // Literal frozen LightFX.cpp MixLight contract, separate from GPU helpers.
                cpuRgba[i * 4 + channel] = static_cast<std::byte>(std::min(255u, dark + ((light * intensity[i] * 6) >> 8)));
            }
        const auto reference = RenderSdl(cpuRgba);
        commands.lightFx.emplace();
        commands.lightFx->width = extent.width;
        commands.lightFx->height = extent.height;
        commands.lightFx->lightPalette = lightPalette;
        for (const auto& light : lights)
            commands.lightFx->lights.push_back(Gpu::MakeLightFxCommand(light));
        ASSERT_FALSE(commands.lightFx->HasCpuIntensity());
        const uint64_t frameNumber = 8000 + fixture.kind * 10 + ordinal;
        const auto frame = backend->BeginFrame(frameNumber);
        ASSERT_TRUE(frame.has_value());
        ASSERT_TRUE(backend->SupportsGpuLightFxRasterization());
        ASSERT_TRUE(backend->RequestFrameCapture(*frame));
        // Backend throws if GPU accumulation fails: there is deliberately no CPU map.
        backend->Submit(*frame, commands);
        backend->Present(*frame);
        const auto output = backend->ReadbackFrameRgba(frameNumber);
        ASSERT_TRUE(output.has_value());
        ASSERT_TRUE(output->lightFxEnabled);
        ASSERT_EQ(output->drawableExtent, extent);
        std::vector<std::byte> gpuIndices(indices.size());
        ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(extent, gpuIndices));
        json_t metadata{
            { "fixtureVersion", 1 },
            { "reference",
              "Frozen CPU resolved light rasterizer and MixLight arithmetic through actual accelerated SDL output" },
            { "referenceLimit",
              "Synthetic resolved commands; excludes world projection, occlusion, light discovery, temporal resolver, main UI "
              "and OS compositor" },
            { "routeEvidence",
              "Successful command-only Submit: backend throws if GPU accumulation cannot consume snapshot; no CPU intensity "
              "attached" },
            { "cpuIntensityAttached", false },
            { "lightFxEnabled", output->lightFxEnabled },
            { "frameNumber", output->frameNumber },
            { "nonzeroIntensityPixels", nonzero },
            { "saturatedIntensityPixels", saturated },
            { "falloffsFnv1a64", HashBytes(falloffs) },
            { "cpuIntensityFnv1a64", HashBytes(std::as_bytes(std::span(intensity))) },
            { "paletteFnv1a64", HashBytes(palette) },
            { "lightPaletteFnv1a64", HashBytes(lightPalette) },
            { "deviceName", output->deviceName },
            { "driverVersion", output->driverVersion },
            { "sourceFormat", output->sourceFormat },
            { "sdlRenderer", rendererInfo.name },
            { "sdlTextureFormat", "ARGB8888" },
            { "commands", json_t::array() }
        };
        for (const auto& light : lights)
            metadata["commands"].push_back({ light.destinationX, light.destinationY, light.width, light.height,
                                             light.sourceOffset, light.sourceStride, light.type, light.intensity });
        for (const auto& shader : std::filesystem::directory_iterator(shaders))
            if (shader.path().extension() == ".spv")
                metadata["shaderFnv1a64"][shader.path().filename().string()] = HashFile(shader.path());
        const std::string name = ordinal == 0 ? fixture.name : std::string(fixture.name) + "_Cleared";
        EXPECT_EQ(
            CompareAndReport(
                artifacts, name, "indexed", std::as_bytes(std::span(indices)), gpuIndices, 1, palette, metadata, extent),
            0u);
        EXPECT_EQ(CompareAndReport(artifacts, name, "physical", reference, output->rgba, 4, palette, metadata, extent), 0u)
            << "Every divergence requires manual triplet review: " << artifacts;
        if (!artifacts.empty())
        {
            auto diagnosticPalette = palette;
            for (size_t i = 0; i < 256; i++)
                for (size_t c = 0; c < 3; c++)
                    diagnosticPalette[i * 4 + c] = static_cast<std::byte>(i);
            SaveRgba(
                artifacts / name / "cpu-intensity-diagnostic.png",
                Expand(std::as_bytes(std::span(intensity)), diagnosticPalette), extent);
            std::ofstream raw(artifacts / name / "cpu-intensity.bin", std::ios::binary);
            raw.exceptions(std::ios::badbit | std::ios::failbit);
            raw.write(reinterpret_cast<const char*>(intensity.data()), intensity.size());
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    Spatial, VulkanSpatialLightParityTest,
    testing::Values(
        SpatialCase{ "Lantern0", 0 }, SpatialCase{ "Lantern1", 1 }, SpatialCase{ "Lantern2", 2 }, SpatialCase{ "Lantern3", 3 },
        SpatialCase{ "Spot0", 4 }, SpatialCase{ "Spot1", 5 }, SpatialCase{ "Spot2", 6 }, SpatialCase{ "Spot3", 7 },
        SpatialCase{ "OverlapSaturation", 8 }, SpatialCase{ "EdgesCorners", 9 },
        SpatialCase{ "SmallCanvasStride", 10, { 79, 61 } }, SpatialCase{ "Empty", 11 }),
    [](const testing::TestParamInfo<SpatialCase>& info) { return info.param.name; });
#endif
