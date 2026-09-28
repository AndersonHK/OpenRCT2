/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#ifdef ENABLE_VULKAN

    #include "VulkanParityTestSupport.h"

    #include <SDL.h>
    #include <algorithm>
    #include <array>
    #include <cmath>
    #include <cstdlib>
    #include <cstring>
    #include <filesystem>
    #include <fstream>
    #include <memory>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanPlatform.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanScreenPalette.h>
    #include <openrct2/drawing/X8DrawingEngine.h>
    #include <stdexcept>
    #include <string>
    #include <string_view>
    #include <vector>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    using namespace OpenRCT2::Drawing;

    struct ScalingCase
    {
        const char* name;
        int quality; // HardwareDisplay: 0 nearest, 1 linear, 2 smooth nearest.
        Gpu::Extent output;
        float windowScale;
        Gpu::Extent logical{ 32, 24 };
        bool screenPalette = false;
    };

    void CheckSdl(int result, const char* operation)
    {
        if (result < 0)
            throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }

    void SetScaleHint(const char* value)
    {
        if (SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, value) != SDL_TRUE)
            throw std::runtime_error("SDL scale-quality hint is overridden; cannot certify scaling reference");
    }

    struct ScaleHintScope
    {
        std::string value;
        bool hadValue = false;
        ScaleHintScope()
        {
            if (const char* current = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY))
            {
                value = current;
                hadValue = true;
            }
        }
        ~ScaleHintScope()
        {
            if (hadValue)
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, value.c_str());
            else
                SDL_ResetHint(SDL_HINT_RENDER_SCALE_QUALITY);
        }
    };

    uint32_t SelectTextureFormat(const SDL_RendererInfo& info)
    {
        uint32_t selected = SDL_PIXELFORMAT_UNKNOWN;
        uint32_t fallback = SDL_PIXELFORMAT_UNKNOWN;
        for (uint32_t i = 0; i < info.num_texture_formats; i++)
        {
            const auto format = info.texture_formats[i];
            if (SDL_ISPIXELFORMAT_FOURCC(format) || SDL_ISPIXELFORMAT_INDEXED(format))
                continue;
            if (fallback == SDL_PIXELFORMAT_UNKNOWN)
                fallback = format;
            if (format == SDL_PIXELFORMAT_ARGB8888)
                return format;
            if (selected == SDL_PIXELFORMAT_UNKNOWN && SDL_BYTESPERPIXEL(format) == 4)
                selected = format;
        }
        return selected == SDL_PIXELFORMAT_UNKNOWN ? fallback : selected;
    }

    std::array<std::byte, 1024> MakeScalingPalette()
    {
        std::array<std::byte, 1024> palette{};
        for (size_t i = 0; i < 256; i++)
        {
            palette[i * 4] = static_cast<std::byte>(i);
            palette[i * 4 + 1] = static_cast<std::byte>((i * 37) & 255);
            palette[i * 4 + 2] = static_cast<std::byte>(255 - i);
            palette[i * 4 + 3] = std::byte{ 255 };
        }
        return palette;
    }

    std::vector<PaletteIndex> MakeFrozenIndices(Gpu::Extent logical)
    {
        std::vector<PaletteIndex> indices(logical.width * logical.height);
        RenderTarget target{ .bits = indices.data(),
                             .width = static_cast<int32_t>(logical.width),
                             .height = static_cast<int32_t>(logical.height) };
        X8DrawingContext software(nullptr);
        software.BeginDraw();
        software.Clear(target, static_cast<PaletteIndex>(1));
        for (int32_t y = 0; y < static_cast<int32_t>(logical.height); y++)
        {
            for (int32_t x = 0; x < static_cast<int32_t>(logical.width); x++)
            {
                const int colour = y < 8 ? 1 + (x * 8) % 254
                    : y < 16             ? ((x + y) % 2 == 0 ? 17 : 237)
                                         : 1 + ((x / 4 + y / 2) * 47) % 254;
                software.FillRect(target, static_cast<PaletteIndex>(colour), x, y, x, y, false);
            }
        }
        software.EndDraw();
        return indices;
    }

    class VulkanScalingParityTest : public testing::TestWithParam<ScalingCase>
    {
    protected:
        bool ownsVideo = false;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> softwareWindow{ nullptr, SDL_DestroyWindow };
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{ nullptr, SDL_DestroyRenderer };
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> vulkanWindow{ nullptr, SDL_DestroyWindow };
        std::unique_ptr<Gpu::Backend> backend;
        SDL_RendererInfo rendererInfo{};
        uint32_t textureFormat = SDL_PIXELFORMAT_UNKNOWN;
        std::filesystem::path shaders;

        std::string Initialise()
        {
            ownsVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
            if (ownsVideo && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                return SDL_GetError();
            const auto& fixture = GetParam();
            softwareWindow.reset(SDL_CreateWindow(
                "Frozen SDL scaling reference", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, fixture.output.width,
                fixture.output.height, SDL_WINDOW_HIDDEN));
            if (!softwareWindow)
                return SDL_GetError();
            // Explicitly select an accelerated renderer. SDL's software driver
            // has different filtering arithmetic and is not the HWDisplay oracle.
            for (int i = 0; i < SDL_GetNumRenderDrivers(); i++)
            {
                SDL_RendererInfo candidate{};
                if (SDL_GetRenderDriverInfo(i, &candidate) != 0 || (candidate.flags & SDL_RENDERER_ACCELERATED) == 0
                    || (candidate.flags & SDL_RENDERER_TARGETTEXTURE) == 0)
                    continue;
                renderer.reset(SDL_CreateRenderer(softwareWindow.get(), i, SDL_RENDERER_ACCELERATED));
                if (renderer)
                    break;
            }
            if (!renderer)
                return "No accelerated SDL renderer with target-texture support is available";
            CheckSdl(SDL_GetRendererInfo(renderer.get(), &rendererInfo), "Get SDL renderer info");
            textureFormat = SelectTextureFormat(rendererInfo);
            if (textureFormat == SDL_PIXELFORMAT_UNKNOWN)
                return "SDL renderer exposes no usable RGB texture format";
            int width = 0, height = 0;
            CheckSdl(SDL_GetRendererOutputSize(renderer.get(), &width, &height), "Get SDL output extent");
            if (width != static_cast<int>(fixture.output.width) || height != static_cast<int>(fixture.output.height))
                return "Scaling fixture requires its exact declared drawable extent";
            vulkanWindow.reset(SDL_CreateWindow(
                "Vulkan scaling result", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, fixture.output.width,
                fixture.output.height, SDL_WINDOW_HIDDEN | Vulkan::Platform::GetRequiredSdlWindowFlags()));
            if (!vulkanWindow)
                return SDL_GetError();
            const auto drawable = Vulkan::Platform::GetDrawableExtent(vulkanWindow.get());
            if (drawable.width != fixture.output.width || drawable.height != fixture.output.height)
                return "SDL and Vulkan output extents differ";
            std::vector<std::filesystem::path> candidates{ std::filesystem::current_path() / "data/shaders/vulkan" };
            if (char* base = SDL_GetBasePath())
            {
                candidates.emplace_back(std::filesystem::path(base) / "data/shaders/vulkan");
                SDL_free(base);
            }
            if (const auto* path = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                candidates.insert(candidates.begin(), path);
            for (const auto& path : candidates)
            {
                if (std::filesystem::is_regular_file(path / "indexed_palette.frag.spv"))
                {
                    shaders = path;
                    break;
                }
            }
            if (shaders.empty())
                return "Compiled Vulkan shaders unavailable";
            const Gpu::BackendConfig config{

                .logicalExtent = fixture.logical,
                .drawableExtent = fixture.output,
                .presentMode = Gpu::PresentMode::Immediate,
                .frameAcquireMode = Gpu::FrameAcquireMode::Wait,
                .shaderDirectory = shaders.string(),
                .enableDiagnosticCapture = true,
                .scaleSettings = { static_cast<Gpu::ScaleMode>(fixture.quality),
                                   static_cast<uint32_t>(std::ceil(fixture.windowScale)) },
            };
            backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(vulkanWindow.get()));
            backend->Initialise(config);
            return {};
        }

        std::vector<std::byte> RenderSdl(
            const ScalingCase& fixture, std::span<const PaletteIndex> indices, const std::array<std::byte, 1024>& palette)
        {
            ScaleHintScope hint;
            SetScaleHint(fixture.quality == 1 ? "1" : "0");
            using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
            Texture input(
                SDL_CreateTexture(
                    renderer.get(), textureFormat, SDL_TEXTUREACCESS_STREAMING, fixture.logical.width, fixture.logical.height),
                SDL_DestroyTexture);
            if (!input)
                throw std::runtime_error(SDL_GetError());
            std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> format(SDL_AllocFormat(textureFormat), SDL_FreeFormat);
            if (!format)
                throw std::runtime_error(SDL_GetError());
            const auto bytesPerPixel = format->BytesPerPixel;
            std::vector<uint8_t> upload(indices.size() * bytesPerPixel);
            for (size_t i = 0; i < indices.size(); i++)
            {
                const auto entry = static_cast<uint8_t>(indices[i]);
                const uint32_t mapped = SDL_MapRGB(
                    format.get(), std::to_integer<uint8_t>(palette[entry * 4]),
                    std::to_integer<uint8_t>(palette[entry * 4 + 1]), std::to_integer<uint8_t>(palette[entry * 4 + 2]));
                auto* destination = upload.data() + i * bytesPerPixel;
                if (bytesPerPixel == 4)
                    std::memcpy(destination, &mapped, 4);
                else if (bytesPerPixel == 2)
                {
                    const auto word = static_cast<uint16_t>(mapped);
                    std::memcpy(destination, &word, 2);
                }
                else if (bytesPerPixel == 3)
                {
    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                    destination[0] = static_cast<uint8_t>(mapped >> 16);
                    destination[1] = static_cast<uint8_t>(mapped >> 8);
                    destination[2] = static_cast<uint8_t>(mapped);
    #else
                    destination[0] = static_cast<uint8_t>(mapped);
                    destination[1] = static_cast<uint8_t>(mapped >> 8);
                    destination[2] = static_cast<uint8_t>(mapped >> 16);
    #endif
                }
                else
                    destination[0] = static_cast<uint8_t>(mapped);
            }
            CheckSdl(
                SDL_UpdateTexture(input.get(), nullptr, upload.data(), fixture.logical.width * bytesPerPixel),
                "Upload SDL palette pixels");
            Texture intermediate(nullptr, SDL_DestroyTexture);
            if (fixture.quality == 2)
            {
                SetScaleHint("1");
                const auto factor = static_cast<uint32_t>(std::ceil(fixture.windowScale));
                intermediate.reset(SDL_CreateTexture(
                    renderer.get(), textureFormat, SDL_TEXTUREACCESS_TARGET, fixture.logical.width * factor,
                    fixture.logical.height * factor));
                if (!intermediate)
                    throw std::runtime_error(SDL_GetError());
                CheckSdl(SDL_SetRenderTarget(renderer.get(), intermediate.get()), "Select smooth-nearest intermediate");
                CheckSdl(SDL_RenderCopy(renderer.get(), input.get(), nullptr, nullptr), "Nearest integer upscale");
                CheckSdl(SDL_SetRenderTarget(renderer.get(), nullptr), "Select SDL window output");
            }
            CheckSdl(
                SDL_RenderCopy(renderer.get(), intermediate ? intermediate.get() : input.get(), nullptr, nullptr),
                "Scale SDL output");
            std::vector<std::byte> rgba(static_cast<size_t>(fixture.output.width) * fixture.output.height * 4);
            CheckSdl(
                SDL_RenderReadPixels(renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA32, rgba.data(), fixture.output.width * 4),
                "Read actual SDL output");
            SDL_RenderPresent(renderer.get());
            return rgba;
        }

        void TearDown() override
        {
            backend.reset();
            vulkanWindow.reset();
            renderer.reset();
            softwareWindow.reset();
            if (ownsVideo)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    };
} // namespace

TEST_P(VulkanScalingParityTest, MatchesFrozenSdlDisplayPixels)
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
            FAIL() << "Required SDL/Vulkan scaling parity unavailable: " << unavailable;
        GTEST_SKIP() << unavailable;
    }
    const auto& fixture = GetParam();
    auto indices = MakeFrozenIndices(fixture.logical);
    auto palette = MakeScalingPalette();
    if (fixture.screenPalette)
    {
        GamePalette gamePalette{};
        for (size_t i = 0; i < gamePalette.size(); i++)
        {
            gamePalette[i].red = std::to_integer<uint8_t>(palette[i * 4]);
            gamePalette[i].green = std::to_integer<uint8_t>(palette[i * 4 + 1]);
            gamePalette[i].blue = std::to_integer<uint8_t>(palette[i * 4 + 2]);
            gamePalette[i].alpha = static_cast<uint8_t>((i * 11) & 255);
        }
        palette = Vulkan::ConvertScreenPalette(gamePalette);
        RenderTarget target{ .bits = indices.data(),
                             .width = static_cast<int32_t>(fixture.logical.width),
                             .height = static_cast<int32_t>(fixture.logical.height) };
        X8DrawingContext softwareContext(nullptr);
        softwareContext.BeginDraw();
        for (size_t i = 0; i < indices.size(); i++)
        {
            const auto x = static_cast<int32_t>(i % fixture.logical.width);
            const auto y = static_cast<int32_t>(i / fixture.logical.width);
            softwareContext.FillRect(target, static_cast<PaletteIndex>(i & 255), x, y, x, y, false);
        }
        softwareContext.EndDraw();
    }
    const auto software = RenderSdl(fixture, indices, palette);
    backend->SetPalette(palette);
    Gpu::FrameCommandStream commands;
    for (uint32_t y = 0; y < fixture.logical.height; y++)
    {
        for (uint32_t x = 0; x < fixture.logical.width; x++)
        {
            commands.opaqueRects.allocate() = {
                .clip = { 0, 0, static_cast<int32_t>(fixture.logical.width), static_cast<int32_t>(fixture.logical.height) },
                .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
                .colour = static_cast<uint8_t>(indices[y * fixture.logical.width + x]),
                .bounds = { static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(x + 1),
                            static_cast<int32_t>(y + 1) },
                .depth = 0,
                .zoom = 1.0f,
            };
        }
    }
    constexpr uint64_t frameNumber = 517;
    const auto frame = backend->BeginFrame(frameNumber);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(backend->RequestFrameCapture(*frame));
    backend->Submit(*frame, commands);
    backend->Present(*frame);
    const auto output = backend->ReadbackFrameRgba(frameNumber);
    ASSERT_TRUE(output.has_value());
    ASSERT_EQ(output->drawableExtent, fixture.output);
    ASSERT_EQ(output->rgba.size(), software.size());
    std::vector<std::byte> gpuIndices(indices.size());
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(fixture.logical, gpuIndices));
    EXPECT_TRUE(std::equal(gpuIndices.begin(), gpuIndices.end(), std::as_bytes(std::span(indices)).begin()));

    json_t metadata{
        { "reference", "Frozen X8 indices through actual SDL accelerated HWDisplay-equivalent presentation" },
        { "referenceLimit",
          "Isolated full texture upload; excludes main UI, dirty regions, LightFX, OS compositor and cursor" },
        { "logicalExtent", { fixture.logical.width, fixture.logical.height } },
        { "scaleQuality", fixture.quality },
        { "screenPaletteConversion", fixture.screenPalette },
        { "windowScale", fixture.windowScale },
        { "capturedScaleMode", static_cast<int>(output->scaleSettings.mode) },
        { "capturedIntegerScale", output->scaleSettings.integerScale },
        { "sdlRenderer", rendererInfo.name },
        { "sdlRendererFlags", rendererInfo.flags },
        { "sdlTextureFormat", SDL_GetPixelFormatName(textureFormat) },
        { "deviceName", output->deviceName },
        { "vendorId", output->vendorId },
        { "deviceId", output->deviceId },
        { "driverVersion", output->driverVersion },
        { "sourceFormat", output->sourceFormat },
        { "frameNumber", output->frameNumber },
        { "paletteVersion", output->paletteVersion },
        { "swapchainGeneration", output->swapchainGeneration },
    };
    for (const auto& shader : std::filesystem::directory_iterator(shaders))
        if (shader.path().extension() == ".spv")
            metadata["shaderFnv1a64"][shader.path().filename().string()] = VulkanParitySupport::HashFile(shader.path());
    std::filesystem::path artifacts;
    if (const auto* directory = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
        artifacts = std::filesystem::path(directory) / "Scaling";
    const auto different = VulkanParitySupport::CompareAndReport(
        artifacts, fixture.name, "physical", software, output->rgba, 4, palette, metadata, fixture.output);
    if (!artifacts.empty())
    {
        const auto path = artifacts / fixture.name / "physical";
        std::ofstream input(path / "frozen-indices.bin", std::ios::binary);
        input.exceptions(std::ios::badbit | std::ios::failbit);
        input.write(reinterpret_cast<const char*>(indices.data()), indices.size());
        std::ofstream paletteFile(path / "palette-rgba.bin", std::ios::binary);
        paletteFile.exceptions(std::ios::badbit | std::ios::failbit);
        paletteFile.write(reinterpret_cast<const char*>(palette.data()), palette.size());
    }
    EXPECT_EQ(different, 0u) << "Physical scaling divergence requires an agent's visual review; see " << artifacts;

    if (!fixture.screenPalette)
    {
        // A uniform LightFX input has an independent exact byte oracle: intensity
        // 64 and light RGB (12,24,36) add (18,36,54), clamped, before SDL filtering.
        // This isolates ordering of lighting and interpolation; it does not claim
        // coverage of the software LightFX world/rasterization path.
        auto litPalette = palette;
        constexpr std::array<int, 3> additions{ 18, 36, 54 };
        commands.lightFx.emplace();
        commands.lightFx->width = fixture.logical.width;
        commands.lightFx->height = fixture.logical.height;
        commands.lightFx->intensities.assign(indices.size(), std::byte{ 64 });
        for (size_t i = 0; i < 256; i++)
        {
            for (size_t channel = 0; channel < 3; channel++)
            {
                litPalette[i * 4 + channel] = static_cast<std::byte>(
                    std::min(255, std::to_integer<int>(palette[i * 4 + channel]) + additions[channel]));
                commands.lightFx->lightPalette[i * 4 + channel] = static_cast<std::byte>(12 * (channel + 1));
            }
        }
        const auto litSoftware = RenderSdl(fixture, indices, litPalette);
        const auto litFrame = backend->BeginFrame(frameNumber + 1);
        ASSERT_TRUE(litFrame.has_value());
        ASSERT_TRUE(backend->RequestFrameCapture(*litFrame));
        backend->Submit(*litFrame, commands);
        backend->Present(*litFrame);
        const auto litOutput = backend->ReadbackFrameRgba(litFrame->frameNumber);
        ASSERT_TRUE(litOutput.has_value());
        EXPECT_TRUE(litOutput->lightFxEnabled);
        auto litMetadata = metadata;
        litMetadata["frameNumber"] = litOutput->frameNumber;
        litMetadata["lightFxIntensity"] = 64;
        litMetadata["lightFxRgb"] = { 12, 24, 36 };
        litMetadata["referenceLimit"] = "Exact uniform LightFX arithmetic followed by actual SDL scaling; excludes world light "
                                        "rasterization";
        EXPECT_EQ(
            VulkanParitySupport::CompareAndReport(
                artifacts, fixture.name, "uniform-lightfx", litSoftware, litOutput->rgba, 4, litPalette, litMetadata,
                fixture.output),
            0u);
        commands.lightFx.reset();
    }

    if (std::string_view(fixture.name) == "SmoothNearestFractional")
    {
        uint64_t nextFrame = frameNumber + 2;
        const auto checkLiveFrame = [&](const ScalingCase& state, std::string_view stage,
                                        const std::vector<std::byte>& expected) {
            const auto handle = backend->BeginFrame(nextFrame++);
            ASSERT_TRUE(handle.has_value());
            ASSERT_TRUE(backend->RequestFrameCapture(*handle));
            backend->Submit(*handle, commands);
            backend->Present(*handle);
            const auto captured = backend->ReadbackFrameRgba(handle->frameNumber);
            ASSERT_TRUE(captured.has_value());
            EXPECT_EQ(captured->logicalExtent, state.logical);
            EXPECT_EQ(captured->drawableExtent, state.output);
            EXPECT_EQ(captured->scaleSettings.mode, static_cast<Gpu::ScaleMode>(state.quality));
            EXPECT_EQ(captured->scaleSettings.integerScale, static_cast<uint32_t>(std::ceil(state.windowScale)));
            auto frameMetadata = metadata;
            frameMetadata["logicalExtent"] = { state.logical.width, state.logical.height };
            frameMetadata["scaleQuality"] = state.quality;
            frameMetadata["windowScale"] = state.windowScale;
            frameMetadata["frameNumber"] = captured->frameNumber;
            frameMetadata["swapchainGeneration"] = captured->swapchainGeneration;
            frameMetadata["capturedScaleMode"] = static_cast<int>(captured->scaleSettings.mode);
            frameMetadata["capturedIntegerScale"] = captured->scaleSettings.integerScale;
            EXPECT_EQ(
                VulkanParitySupport::CompareAndReport(
                    artifacts, fixture.name, stage, expected, captured->rgba, 4, palette, frameMetadata, state.output),
                0u);
        };
        // Reuse all three frame slots while changing mode and then changing the
        // intermediate extent, with no global idle or backend reconstruction.
        auto alternate = fixture;
        alternate.quality = 1;
        alternate.windowScale = 1.0f;
        backend->SetScaleSettings({ Gpu::ScaleMode::Linear, 1 });
        auto expected = RenderSdl(alternate, indices, palette);
        for (uint32_t i = 0; i < Vulkan::kFramesInFlight; i++)
            checkLiveFrame(alternate, "live-linear-slot-" + std::to_string(i), expected);
        alternate.quality = 2;
        alternate.windowScale = 1.5f;
        backend->SetScaleSettings({ Gpu::ScaleMode::SmoothNearest, 2 });
        expected = RenderSdl(alternate, indices, palette);
        for (uint32_t i = 0; i < Vulkan::kFramesInFlight; i++)
            checkLiveFrame(alternate, "live-smooth-slot-" + std::to_string(i), expected);

        const ScalingCase resized{ "LiveResize", 2, { 101, 77 }, 2.7f, { 37, 28 } };
        SDL_SetWindowSize(softwareWindow.get(), resized.output.width, resized.output.height);
        SDL_SetWindowSize(vulkanWindow.get(), resized.output.width, resized.output.height);
        SDL_PumpEvents();
        const auto newDrawable = Vulkan::Platform::GetDrawableExtent(vulkanWindow.get());
        ASSERT_EQ(newDrawable.width, resized.output.width);
        ASSERT_EQ(newDrawable.height, resized.output.height);
        int sdlWidth = 0, sdlHeight = 0;
        ASSERT_EQ(SDL_GetRendererOutputSize(renderer.get(), &sdlWidth, &sdlHeight), 0);
        ASSERT_EQ(sdlWidth, static_cast<int>(resized.output.width));
        ASSERT_EQ(sdlHeight, static_cast<int>(resized.output.height));
        backend->Resize(resized.logical, resized.output);
        backend->SetScaleSettings({ Gpu::ScaleMode::SmoothNearest, 3 });
        EXPECT_FALSE(backend->ReadbackFrameRgba(nextFrame - 1).has_value());
        const auto resizedIndices = MakeFrozenIndices(resized.logical);
        commands.clear();
        for (uint32_t y = 0; y < resized.logical.height; y++)
        {
            for (uint32_t x = 0; x < resized.logical.width; x++)
            {
                commands.opaqueRects.allocate() = {
                    .clip = { 0, 0, static_cast<int32_t>(resized.logical.width), static_cast<int32_t>(resized.logical.height) },
                    .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
                    .colour = static_cast<uint8_t>(resizedIndices[y * resized.logical.width + x]),
                    .bounds = { static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(x + 1),
                                static_cast<int32_t>(y + 1) },
                    .depth = 0,
                    .zoom = 1.0f,
                };
            }
        }
        expected = RenderSdl(resized, resizedIndices, palette);
        checkLiveFrame(resized, "live-resize", expected);
        backend->RequestSurfaceFormatRefresh();
        checkLiveFrame(resized, "surface-refresh", expected);
    }
}

INSTANTIATE_TEST_SUITE_P(
    DisplayModes, VulkanScalingParityTest,
    testing::Values(
        ScalingCase{ "NearestInteger", 0, { 96, 72 }, 3.0f }, ScalingCase{ "NearestFractional", 0, { 93, 71 }, 2.9f },
        ScalingCase{ "LinearInteger", 1, { 96, 72 }, 3.0f }, ScalingCase{ "LinearFractional", 1, { 93, 71 }, 2.9f },
        ScalingCase{ "SmoothNearestInteger", 2, { 96, 72 }, 3.0f },
        ScalingCase{ "SmoothNearestFractional", 2, { 93, 71 }, 2.9f },
        ScalingCase{ "NearestHiDpiRatio", 0, { 186, 142 }, 2.9f }, ScalingCase{ "LinearHiDpiRatio", 1, { 186, 142 }, 2.9f },
        ScalingCase{ "SmoothNearestHiDpiRatio", 2, { 186, 142 }, 2.9f },
        ScalingCase{ "SmoothNearestSmallScale", 2, { 40, 30 }, 1.25f },
        ScalingCase{ "SmoothNearestDownscale", 2, { 24, 18 }, 0.75f },
        ScalingCase{ "SmoothNearestOddLogical", 2, { 99, 74 }, 2.5f, { 39, 29 } },
        ScalingCase{ "ScreenPaletteAllIndices", 0, { 96, 72 }, 3.0f, { 32, 24 }, true }),
    [](const testing::TestParamInfo<ScalingCase>& info) { return info.param.name; });

#endif // ENABLE_VULKAN
