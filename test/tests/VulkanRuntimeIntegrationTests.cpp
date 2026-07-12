/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#ifdef ENABLE_VULKAN

    #include <SDL.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanBackend.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanDevice.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanPlatform.h>
    #include <openrct2/drawing/LightFX.h>

    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <filesystem>
    #include <memory>
    #include <optional>
    #include <span>
    #include <stdexcept>
    #include <string>
    #include <system_error>
    #include <vector>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace LightFx = OpenRCT2::Drawing::LightFx;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;

    class SdlVideoScope final
    {
    private:
        bool _ownsVideo = false;

    public:
        [[nodiscard]] bool Initialise()
        {
            if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0)
            {
                return true;
            }
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
            {
                return false;
            }
            _ownsVideo = true;
            return true;
        }

        ~SdlVideoScope()
        {
            if (_ownsVideo)
            {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
            }
        }
    };

    struct SdlWindowDeleter
    {
        void operator()(SDL_Window* window) const noexcept
        {
            if (window != nullptr)
            {
                SDL_DestroyWindow(window);
            }
        }
    };

    [[nodiscard]] bool HasVulkanShaderSet(const std::filesystem::path& directory)
    {
        constexpr std::array requiredShaders = {
            "indexed_palette.vert.spv",
            "indexed_palette.frag.spv",
            "indexed_line.vert.spv",
            "indexed_line.frag.spv",
            "indexed_rect.vert.spv",
            "indexed_rect.frag.spv",
            "indexed_transparent_rect.vert.spv",
            "indexed_transparent_rect.frag.spv",
            "indexed_transparency_compose.vert.spv",
            "indexed_transparency_compose.frag.spv",
            "indexed_weather.vert.spv",
            "indexed_weather.frag.spv",
            "lightfx_accumulate.comp.spv",
        };
        return std::all_of(requiredShaders.begin(), requiredShaders.end(), [&directory](const char* name) {
            std::error_code error;
            return std::filesystem::is_regular_file(directory / name, error);
        });
    }

    [[nodiscard]] std::filesystem::path FindVulkanShaderDirectory()
    {
        std::vector<std::filesystem::path> candidates;
        std::error_code error;
        const auto currentPath = std::filesystem::current_path(error);
        candidates.push_back(currentPath / "data" / "shaders" / "vulkan");
        candidates.push_back(currentPath / "OpenRCT2.app" / "Contents" / "Resources" / "shaders" / "vulkan");
        if (char* basePath = SDL_GetBasePath(); basePath != nullptr)
        {
            const std::filesystem::path executableDirectory(basePath);
            candidates.emplace_back(executableDirectory / "data" / "shaders" / "vulkan");
            candidates.emplace_back(
                executableDirectory / "OpenRCT2.app" / "Contents" / "Resources" / "shaders" / "vulkan");
            SDL_free(basePath);
        }
        const auto found = std::find_if(candidates.begin(), candidates.end(), HasVulkanShaderSet);
        return found == candidates.end() ? std::filesystem::path{} : *found;
    }

    [[nodiscard]] std::array<std::byte, 256 * 4> MakePalette()
    {
        std::array<std::byte, 256 * 4> palette{};
        for (size_t i = 0; i < 256; i++)
        {
            palette[i * 4] = static_cast<std::byte>(i);
            palette[i * 4 + 1] = static_cast<std::byte>(255 - i);
            palette[i * 4 + 2] = static_cast<std::byte>((i * 37) & 0xff);
            palette[i * 4 + 3] = i == 0 ? std::byte{ 0 } : std::byte{ 0xff };
        }
        return palette;
    }

    [[nodiscard]] std::vector<std::byte> MakeCanvas(Gpu::Extent extent)
    {
        std::vector<std::byte> canvas(static_cast<size_t>(extent.width) * extent.height);
        for (uint32_t y = 0; y < extent.height; y++)
        {
            for (uint32_t x = 0; x < extent.width; x++)
            {
                canvas[static_cast<size_t>(y) * extent.width + x] = static_cast<std::byte>((x * 7 + y * 13) & 0xff);
            }
        }
        return canvas;
    }

    [[nodiscard]] Gpu::LightFxFrameSnapshot MakeLightFxSnapshot(
        Gpu::Extent extent, const std::array<std::byte, 256 * 4>& palette)
    {
        LightFx::FrameSnapshot::ResolvedLight resolved;
        if (!LightFx::ResolveLightCommandForCanvas(
                static_cast<int32_t>(extent.width / 2), static_cast<int32_t>(extent.height / 2), extent.width,
                extent.height, LightFx::LightType::lantern0, 255, resolved))
        {
            throw std::runtime_error("Could not resolve the Vulkan integration-test LightFX command");
        }

        std::vector<uint8_t> intensities(static_cast<size_t>(extent.width) * extent.height);
        if (!LightFx::RasterizeResolvedLightCommands(extent.width, extent.height, { &resolved, 1 }, intensities))
        {
            throw std::runtime_error("Could not rasterize the Vulkan integration-test LightFX fallback");
        }

        Gpu::LightFxFrameSnapshot snapshot;
        snapshot.width = extent.width;
        snapshot.height = extent.height;
        snapshot.lightPalette = palette;
        // Preserve both representations: capable devices consume the resolved
        // command in compute, while other devices upload these CPU intensities.
        snapshot.intensities.resize(intensities.size());
        std::transform(intensities.begin(), intensities.end(), snapshot.intensities.begin(), [](uint8_t intensity) {
            return static_cast<std::byte>(intensity);
        });
        snapshot.lights.push_back(Gpu::MakeLightFxCommand(resolved));
        return snapshot;
    }

    [[nodiscard]] std::optional<uint32_t> PresentIndexedFrame(
        Gpu::Backend& backend, uint64_t frameNumber, Gpu::Extent extent, std::span<const std::byte> canvas,
        const Gpu::LightFxFrameSnapshot* lightFx)
    {
        auto frame = backend.BeginFrame(frameNumber);
        if (!frame.has_value())
        {
            backend.WaitIdle();
            frame = backend.BeginFrame(frameNumber);
            if (!frame.has_value())
            {
                return std::nullopt;
            }
        }

        try
        {
            auto upload = backend.AllocateUpload(canvas.size(), alignof(uint32_t));
            if (!upload)
            {
                throw std::runtime_error("Vulkan integration test could not allocate its indexed canvas upload");
            }
            std::copy(canvas.begin(), canvas.end(), upload.bytes.begin());

            Gpu::FrameCommandStream commands;
            commands.canvasUpload = Gpu::CanvasUpload{
                .sourceOffset = static_cast<uint32_t>(upload.offset),
                .sourcePitch = extent.width,
                .width = extent.width,
                .height = extent.height,
            };
            if (lightFx != nullptr)
            {
                commands.lightFx = *lightFx;
            }
            backend.Submit(*frame, commands);
            backend.Present(*frame);
        }
        catch (...)
        {
            try
            {
                backend.AbandonFrame(*frame);
            }
            catch (...)
            {
                // Preserve the integration failure that abandoned the frame.
            }
            throw;
        }
        return frame->frameSlot;
    }

} // namespace

TEST(VulkanRuntimeIntegrationTest, HiddenWindowExercisesBackendLifecycleAndIndexedReadback)
{
    SdlVideoScope video;
    if (!video.Initialise())
    {
        GTEST_SKIP() << "SDL video is unavailable: " << SDL_GetError();
    }

    using WindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
    WindowPtr window(SDL_CreateWindow(
        "OpenRCT2 Vulkan integration test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 32, 32,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI | Vulkan::Platform::GetRequiredSdlWindowFlags()));
    if (window == nullptr)
    {
        GTEST_SKIP() << "SDL window creation is unavailable: " << SDL_GetError();
    }

    const auto drawableExtent = Vulkan::Platform::GetDrawableExtent(window.get());
    if (drawableExtent.width == 0 || drawableExtent.height == 0)
    {
        GTEST_SKIP() << "The hidden Vulkan test window has no drawable extent";
    }

    const auto shaderDirectory = FindVulkanShaderDirectory();
    if (shaderDirectory.empty())
    {
        GTEST_SKIP() << "The compiled Vulkan shader set is unavailable";
    }

    constexpr Gpu::Extent initialLogicalExtent = { 32, 32 };
    const Gpu::BackendConfig config = {
        .nativeWindow = window.get(),
        .logicalExtent = initialLogicalExtent,
        .drawableExtent = { drawableExtent.width, drawableExtent.height },
        .presentMode = Gpu::PresentMode::Immediate,
        .frameAcquireMode = Gpu::FrameAcquireMode::SkipIfBusy,
        .outputColorMode = Gpu::OutputColorMode::Hdr10IfAvailable,
        .uploadRingBytesPerFrame = 4 * 1024 * 1024,
        .shaderDirectory = shaderDirectory.string(),
    };
    auto backend = Vulkan::CreateBackend();
    try
    {
        backend->Initialise(config);
    }
    catch (const std::exception& error)
    {
        GTEST_SKIP() << "Vulkan backend initialisation is unavailable: " << error.what();
    }
    catch (...)
    {
        GTEST_SKIP() << "Vulkan backend initialisation is unavailable";
    }

    LightFx::Init();
    const auto palette = MakePalette();
    std::vector<std::byte> remap(256 * 256);
    for (size_t i = 0; i < remap.size(); i++)
        remap[i] = static_cast<std::byte>(i & 0xff);
    const auto falloffs = LightFx::CaptureBakedFalloffs();
    ASSERT_EQ(falloffs.size(), 8u * 256 * 256);
    backend->SetPalette(palette);
    backend->SetRemapPalette(remap);
    backend->SetBlendPalette(remap);
    backend->SetLightFxFalloffs(falloffs);

    const auto initialCanvas = MakeCanvas(initialLogicalExtent);
    const auto initialLightFx = MakeLightFxSnapshot(initialLogicalExtent, palette);
    ASSERT_TRUE(initialLightFx.IsValid());
    ASSERT_TRUE(initialLightFx.HasCpuIntensity());
    ASSERT_FALSE(initialLightFx.lights.empty());
    auto firstFrameSlot = PresentIndexedFrame(*backend, 0, initialLogicalExtent, initialCanvas, &initialLightFx);
    if (!firstFrameSlot.has_value())
    {
        GTEST_SKIP() << "The hidden Vulkan surface is not available for non-blocking frame acquisition";
    }
    EXPECT_EQ(*firstFrameSlot, 0u);
    for (uint64_t frameNumber = 1; frameNumber <= Vulkan::kFramesInFlight; frameNumber++)
    {
        const auto frameSlot = PresentIndexedFrame(
            *backend, frameNumber, initialLogicalExtent, initialCanvas, &initialLightFx);
        ASSERT_TRUE(frameSlot.has_value());
        EXPECT_EQ(*frameSlot, frameNumber % Vulkan::kFramesInFlight);
    }

    backend->WaitIdle();
    std::vector<Gpu::FrameTimings> completedTimings;
    backend->TakeCompletedTimings(completedTimings);
    ASSERT_EQ(completedTimings.size(), Vulkan::kFramesInFlight + 1u);
    for (uint64_t frameNumber = 0; frameNumber < completedTimings.size(); frameNumber++)
    {
        EXPECT_EQ(completedTimings[frameNumber].frameNumber, frameNumber);
    }

    const auto timings = backend->GetLatestTimings();
    ASSERT_TRUE(timings.has_value());
    EXPECT_EQ(timings->frameNumber, Vulkan::kFramesInFlight);
    EXPECT_GE(timings->cpuSubmitMicroseconds, 0.0);
    EXPECT_GE(timings->cpuPresentMicroseconds, 0.0);
    EXPECT_TRUE(timings->hasPresentCallMeasurement);
    EXPECT_GE(timings->presentCallMicroseconds, 0.0);
    if (timings->hasGpuTimestamp)
    {
        EXPECT_TRUE(timings->hasGpuPassTimestamps);
        EXPECT_GE(timings->gpuMicroseconds, 0.0);
        EXPECT_NEAR(
            timings->gpuUploadMicroseconds + timings->gpuDrawMicroseconds + timings->gpuLightFxMicroseconds
                + timings->gpuCompositeMicroseconds,
            timings->gpuMicroseconds, std::max(0.001, timings->gpuMicroseconds * 1e-9));
    }
    else
    {
        EXPECT_FALSE(timings->hasGpuTimestamp);
        EXPECT_FALSE(timings->hasGpuPassTimestamps);
    }

    std::vector<std::byte> readback(initialCanvas.size());
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(initialLogicalExtent, readback));
    EXPECT_EQ(readback, initialCanvas);

    backend->RequestSurfaceFormatRefresh();
    const uint64_t refreshedFrameNumber = Vulkan::kFramesInFlight + 1;
    const auto refreshedFrameSlot = PresentIndexedFrame(
        *backend, refreshedFrameNumber, initialLogicalExtent, initialCanvas, &initialLightFx);
    ASSERT_TRUE(refreshedFrameSlot.has_value());
    EXPECT_EQ(*refreshedFrameSlot, refreshedFrameNumber % Vulkan::kFramesInFlight);
    readback.assign(initialCanvas.size(), std::byte{ 0 });
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(initialLogicalExtent, readback));
    EXPECT_EQ(readback, initialCanvas);

    constexpr Gpu::Extent resizedLogicalExtent = { 48, 40 };
    SDL_SetWindowSize(window.get(), 48, 40);
    SDL_PumpEvents();
    const auto resizedExtent = Vulkan::Platform::GetDrawableExtent(window.get());
    ASSERT_GT(resizedExtent.width, 0U);
    ASSERT_GT(resizedExtent.height, 0U);
    const Gpu::Extent resizedDrawableExtent = {
        resizedExtent.width,
        resizedExtent.height,
    };
    backend->Resize(resizedLogicalExtent, resizedDrawableExtent);

    const auto resizedCanvas = MakeCanvas(resizedLogicalExtent);
    const auto resizedLightFx = MakeLightFxSnapshot(resizedLogicalExtent, palette);
    const uint64_t resizedFrameNumber = refreshedFrameNumber + 1;
    const auto resizedFrameSlot = PresentIndexedFrame(
        *backend, resizedFrameNumber, resizedLogicalExtent, resizedCanvas, &resizedLightFx);
    ASSERT_TRUE(resizedFrameSlot.has_value());
    EXPECT_EQ(*resizedFrameSlot, resizedFrameNumber % Vulkan::kFramesInFlight);

    readback.assign(resizedCanvas.size(), std::byte{ 0 });
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    EXPECT_EQ(readback, resizedCanvas);
}

#endif // ENABLE_VULKAN
