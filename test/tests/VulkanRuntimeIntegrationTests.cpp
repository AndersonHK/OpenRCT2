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
    #ifdef _WIN32
        #include <SDL_syswm.h>
    #endif
    #include <algorithm>
    #include <array>
    #include <chrono>
    #include <cstddef>
    #include <cstdint>
    #include <cstdlib>
    #include <cstring>
    #include <filesystem>
    #include <memory>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>
    #include <openrct2-renderer/vulkan/VulkanDevice.h>
    #include <openrct2-renderer/vulkan/VulkanPipelineCacheData.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanPlatform.h>
    #include <openrct2/drawing/LightFX.h>
    #include <optional>
    #include <span>
    #include <stdexcept>
    #include <string>
    #include <system_error>
    #include <thread>
    #include <vector>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace LightFx = OpenRCT2::Drawing::LightFx;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;

    [[nodiscard]] bool RequiresVulkanTests()
    {
        const auto* value = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        return value != nullptr && std::string(value) == "1";
    }

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
            "rgba_scale.frag.spv",
            "indexed_line.vert.spv",
            "indexed_line.frag.spv",
            "indexed_rect.vert.spv",
            "indexed_rect.frag.spv",
            "indexed_sprite.vert.spv",
            "indexed_transparent_rect.vert.spv",
            "indexed_transparent_rect.frag.spv",
            "indexed_transparency_compose.vert.spv",
            "indexed_transparency_compose.frag.spv",
            "indexed_weather.vert.spv",
            "indexed_weather.frag.spv",
            "lightfx_accumulate.comp.spv",
            "world_surface.vert.spv",
            "world_surface_compact.comp.spv",
            "world_parent_columns.comp.spv",
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
            candidates.emplace_back(executableDirectory / "OpenRCT2.app" / "Contents" / "Resources" / "shaders" / "vulkan");
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
                static_cast<int32_t>(extent.width / 2), static_cast<int32_t>(extent.height / 2), extent.width, extent.height,
                LightFx::LightType::lantern0, 255, resolved))
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
            Gpu::FrameCommandStream commands;
            commands.opaqueRects.reserve(canvas.size());
            const Gpu::Int4 clip = { 0, 0, static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height) };
            for (uint32_t y = 0; y < extent.height; ++y)
            {
                for (uint32_t x = 0; x < extent.width; ++x)
                {
                    const auto colour = std::to_integer<uint8_t>(canvas[static_cast<size_t>(y) * extent.width + x]);
                    if (colour == 0)
                        continue;
                    auto& command = commands.opaqueRects.allocate();
                    command.clip = clip;
                    command.flags = Gpu::RectCommand::FLAG_NO_TEXTURE;
                    command.colour = colour;
                    command.bounds = { static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(x + 1),
                                       static_cast<int32_t>(y + 1) };
                    command.depth = 0;
                    command.zoom = 1.0f;
                }
            }
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

    [[nodiscard]] std::optional<uint32_t> PresentCommandFrame(
        Gpu::Backend& backend, uint64_t frameNumber, const Gpu::FrameCommandStream& commands, bool capture = false)
    {
        auto frame = backend.BeginFrame(frameNumber);
        if (!frame.has_value())
        {
            backend.WaitIdle();
            frame = backend.BeginFrame(frameNumber);
            if (!frame.has_value())
                return std::nullopt;
        }

        try
        {
            if (capture && !backend.RequestFrameCapture(*frame))
                throw std::runtime_error("The Vulkan test device does not support requested SDR diagnostic capture");
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

TEST(VulkanRuntimeIntegrationTest, RejectsNullSdlPresentationWindow)
{
    EXPECT_THROW(static_cast<void>(Vulkan::Platform::CreatePresentationHost(nullptr)), std::invalid_argument);
}

TEST(VulkanRuntimeIntegrationTest, OptInUploadTelemetryAccountsAbandonPaddedAtlasAndReadback)
{
    SdlVideoScope video;
    if (!video.Initialise())
    {
        if (RequiresVulkanTests())
            FAIL() << "Required SDL video is unavailable: " << SDL_GetError();
        GTEST_SKIP() << "SDL video is unavailable: " << SDL_GetError();
    }

    using WindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
    WindowPtr window(SDL_CreateWindow(
        "OpenRCT2 Vulkan integration test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 32, 32,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI | Vulkan::Platform::GetRequiredSdlWindowFlags()));
    if (window == nullptr)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required SDL window creation is unavailable: " << SDL_GetError();
        GTEST_SKIP() << "SDL window creation is unavailable: " << SDL_GetError();
    }

    const auto drawableExtent = Vulkan::Platform::GetDrawableExtent(window.get());
    if (drawableExtent.width == 0 || drawableExtent.height == 0)
    {
        if (RequiresVulkanTests())
            FAIL() << "The required hidden Vulkan test window has no drawable extent";
        GTEST_SKIP() << "The hidden Vulkan test window has no drawable extent";
    }

    const auto shaderDirectory = FindVulkanShaderDirectory();
    if (shaderDirectory.empty())
    {
        if (RequiresVulkanTests())
            FAIL() << "The required compiled Vulkan shader set is unavailable";
        GTEST_SKIP() << "The compiled Vulkan shader set is unavailable";
    }

    constexpr Gpu::Extent initialLogicalExtent = { 32, 32 };
    const Gpu::BackendConfig config = {

        .logicalExtent = initialLogicalExtent,
        .drawableExtent = { drawableExtent.width, drawableExtent.height },
        .presentMode = Gpu::PresentMode::Immediate,
        .frameAcquireMode = Gpu::FrameAcquireMode::Wait,
        .outputColorMode = Gpu::OutputColorMode::Sdr,
        .uploadRingBytesPerFrame = 4 * 1024 * 1024,
        .shaderDirectory = shaderDirectory.string(),
        .enableUploadTelemetry = true,
    };
    auto backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(window.get()));
    try
    {
        backend->Initialise(config);
    }
    catch (const std::exception& error)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required Vulkan initialisation failed: " << error.what();
        GTEST_SKIP() << "Vulkan backend initialisation is unavailable: " << error.what();
    }
    catch (...)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required Vulkan initialisation failed";
        GTEST_SKIP() << "Vulkan backend initialisation is unavailable";
    }

    using Category = OpenRCT2::Drawing::UploadCategory;
    using Metric = OpenRCT2::Drawing::UploadMetric;
    const auto value = [](const auto& telemetry, Category c, Metric m) {
        return telemetry.bytes[static_cast<size_t>(c)][static_cast<size_t>(m)];
    };
    backend->SetPalette(MakePalette());
    Gpu::FrameCommandStream commands;
    auto& rect = commands.opaqueRects.allocate();
    rect = {};
    rect.clip = { 0, 0, 32, 32 };
    rect.bounds = { 0, 0, 32, 32 };
    rect.flags = Gpu::RectCommand::FLAG_NO_TEXTURE;
    rect.colour = 12;
    rect.zoom = 1.0f;
    commands.textureUploads.push_back({
        .atlas = 0,
        .bounds = { 0, 0, 2, 2 },
        .sourcePitch = 4,
        .descriptorIndex = 0,
        .descriptor = {},
        .pixels = std::vector<std::byte>(8, std::byte{ 7 }),
    });
    auto abandoned = backend->BeginFrame(101);
    ASSERT_TRUE(abandoned.has_value());
    backend->Submit(*abandoned, commands);
    backend->AbandonFrame(*abandoned);
    backend->WaitIdle();
    std::vector<Gpu::FrameTimings> samples;
    backend->TakeCompletedTimings(samples);
    ASSERT_EQ(samples.size(), 1u);
    ASSERT_TRUE(samples[0].uploadTelemetry.has_value());
    EXPECT_TRUE(samples[0].telemetryOnly);
    EXPECT_FALSE(samples[0].uploadTelemetry->submitted);
    const auto attempted = *samples[0].uploadTelemetry;
    EXPECT_EQ(value(attempted, Category::atlas, Metric::hostWritten), 8u + sizeof(Gpu::SpriteAssetDescriptor));
    EXPECT_EQ(value(attempted, Category::atlas, Metric::imageTransfer), 4u);
    EXPECT_EQ(value(attempted, Category::atlas, Metric::bufferTransfer), sizeof(Gpu::SpriteAssetDescriptor));
    EXPECT_EQ(value(attempted, Category::commands, Metric::hostWritten), sizeof(Gpu::RectCommand));
    EXPECT_EQ(value(attempted, Category::commands, Metric::directVertex), sizeof(Gpu::RectCommand));
    EXPECT_EQ(value(attempted, Category::commands, Metric::bufferTransfer), 0u);
    EXPECT_EQ(value(attempted, Category::palette, Metric::hostWritten), 1024u);
    EXPECT_EQ(value(attempted, Category::palette, Metric::imageTransfer), 1024u);
    uint64_t totalHostWrites = 0;
    for (const auto& category : attempted.bytes)
        totalHostWrites += category[static_cast<size_t>(Metric::hostWritten)];
    EXPECT_EQ(attempted.allocatedBytes, totalHostWrites);
    EXPECT_EQ(attempted.captureRequests, 0u);
    EXPECT_EQ(attempted.readbackRequests, 0u);

    ASSERT_TRUE(PresentCommandFrame(*backend, 102, commands).has_value());
    backend->WaitIdle();
    backend->TakeCompletedTimings(samples);
    ASSERT_EQ(samples.size(), 1u);
    ASSERT_TRUE(samples[0].uploadTelemetry.has_value());
    EXPECT_FALSE(samples[0].telemetryOnly);
    EXPECT_TRUE(samples[0].uploadTelemetry->submitted);
    EXPECT_EQ(samples[0].frameNumber, 102u);
    OpenRCT2::Drawing::RenderUploadTotals totals;
    totals.Include(attempted);
    totals.Include(*samples[0].uploadTelemetry);
    EXPECT_EQ(totals.attemptedFrames, 2u);
    EXPECT_EQ(totals.submittedFrames, 1u);
    const auto atlas = static_cast<size_t>(Category::atlas);
    const auto image = static_cast<size_t>(Metric::imageTransfer);
    EXPECT_EQ(totals.attempted.bytes[atlas][image], 8u);
    EXPECT_EQ(totals.submittedBytes[atlas][image], 4u);

    commands.textureUploads.clear();
    for (uint64_t frame = 103; frame < 103 + Vulkan::kFramesInFlight + 1; ++frame)
    {
        ASSERT_TRUE(PresentCommandFrame(*backend, frame, commands).has_value());
        backend->WaitIdle();
        backend->TakeCompletedTimings(samples);
        ASSERT_EQ(samples.size(), 1u);
        ASSERT_TRUE(samples[0].uploadTelemetry.has_value());
        EXPECT_EQ(samples[0].frameNumber, frame);
        EXPECT_EQ(value(*samples[0].uploadTelemetry, Category::atlas, Metric::hostWritten), 0u);
        EXPECT_EQ(value(*samples[0].uploadTelemetry, Category::commands, Metric::directVertex), sizeof(Gpu::RectCommand));
    }
    std::vector<std::byte> pixels(32 * 32);
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas({ 32, 32 }, pixels));
    backend->TakeCompletedTimings(samples);
    ASSERT_EQ(samples.size(), 1u);
    ASSERT_TRUE(samples[0].uploadTelemetry.has_value());
    EXPECT_TRUE(samples[0].telemetryOnly);
    EXPECT_TRUE(samples[0].uploadTelemetry->auxiliary);
    EXPECT_EQ(samples[0].uploadTelemetry->readbackRequests, 1u);
    EXPECT_EQ(samples[0].uploadTelemetry->readbackBytes, pixels.size());
    EXPECT_EQ(samples[0].uploadTelemetry->allocatedBytes, 0u);
    EXPECT_EQ(value(*samples[0].uploadTelemetry, Category::commands, Metric::hostWritten), 0u);
}

TEST(VulkanRuntimeIntegrationTest, UploadTelemetryOverflowIsExplicitAndAbandonedBytesAreNotCommitted)
{
    using namespace OpenRCT2::Drawing;
    RenderUploadTelemetry abandoned;
    abandoned.Add(UploadCategory::world, UploadMetric::bufferTransfer, 10240);
    RenderUploadTotals totals;
    totals.Include(abandoned);
    EXPECT_EQ(totals.submittedFrames, 0u);
    EXPECT_EQ(
        totals.submittedBytes[static_cast<size_t>(UploadCategory::world)][static_cast<size_t>(UploadMetric::bufferTransfer)],
        0u);
    abandoned.allocatedBytes = std::numeric_limits<uint64_t>::max();
    abandoned.Add(abandoned.allocatedBytes, 1);
    EXPECT_TRUE(abandoned.overflow);
    totals.Include(abandoned);
    EXPECT_TRUE(totals.attempted.overflow);
}

TEST(VulkanRuntimeIntegrationTest, HiddenWindowExercisesBackendLifecycleAndIndexedReadback)
{
    SdlVideoScope video;
    if (!video.Initialise())
    {
        if (RequiresVulkanTests())
            FAIL() << "Required SDL video is unavailable: " << SDL_GetError();
        GTEST_SKIP() << "SDL video is unavailable: " << SDL_GetError();
    }

    using WindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
    WindowPtr window(SDL_CreateWindow(
        "OpenRCT2 Vulkan integration test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 32, 32,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI | Vulkan::Platform::GetRequiredSdlWindowFlags()));
    if (window == nullptr)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required SDL window creation is unavailable: " << SDL_GetError();
        GTEST_SKIP() << "SDL window creation is unavailable: " << SDL_GetError();
    }

    const auto drawableExtent = Vulkan::Platform::GetDrawableExtent(window.get());
    if (drawableExtent.width == 0 || drawableExtent.height == 0)
    {
        if (RequiresVulkanTests())
            FAIL() << "The required hidden Vulkan test window has no drawable extent";
        GTEST_SKIP() << "The hidden Vulkan test window has no drawable extent";
    }

    const auto shaderDirectory = FindVulkanShaderDirectory();
    if (shaderDirectory.empty())
    {
        if (RequiresVulkanTests())
            FAIL() << "The required compiled Vulkan shader set is unavailable";
        GTEST_SKIP() << "The compiled Vulkan shader set is unavailable";
    }

    constexpr Gpu::Extent initialLogicalExtent = { 32, 32 };
    const Gpu::BackendConfig config = {

        .logicalExtent = initialLogicalExtent,
        .drawableExtent = { drawableExtent.width, drawableExtent.height },
        .presentMode = Gpu::PresentMode::Immediate,
        .frameAcquireMode = Gpu::FrameAcquireMode::SkipIfBusy,
        .outputColorMode = Gpu::OutputColorMode::Hdr10IfAvailable,
        .uploadRingBytesPerFrame = 4 * 1024 * 1024,
        .shaderDirectory = shaderDirectory.string(),
    };
    auto backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(window.get()));
    try
    {
        backend->Initialise(config);
    }
    catch (const std::exception& error)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required Vulkan initialisation failed: " << error.what();
        GTEST_SKIP() << "Vulkan backend initialisation is unavailable: " << error.what();
    }
    catch (...)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required Vulkan initialisation failed";
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
        if (RequiresVulkanTests())
            FAIL() << "The required hidden Vulkan surface could not acquire a frame";
        GTEST_SKIP() << "The hidden Vulkan surface is not available for non-blocking frame acquisition";
    }
    EXPECT_EQ(*firstFrameSlot, 0u);
    for (uint64_t frameNumber = 1; frameNumber <= Vulkan::kFramesInFlight; frameNumber++)
    {
        const auto frameSlot = PresentIndexedFrame(*backend, frameNumber, initialLogicalExtent, initialCanvas, &initialLightFx);
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
        EXPECT_FALSE(completedTimings[frameNumber].uploadTelemetry.has_value());
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

    const Gpu::Int4 fullClip = { 0, 0, static_cast<int32_t>(resizedLogicalExtent.width),
                                 static_cast<int32_t>(resizedLogicalExtent.height) };
    constexpr Gpu::Int4 spriteBounds = { 4, 5, 6, 7 };
    constexpr std::array spritePixels = {
        std::byte{ 10 },
        std::byte{ 20 },
        std::byte{ 30 },
        std::byte{ 40 },
    };

    Gpu::FrameCommandStream uploadedSprite;
    uploadedSprite.textureUploads.push_back({
        .atlas = 0,
        .bounds = { 0, 0, 2, 2 },
        .sourcePitch = 2,
        .descriptorIndex = 0,
        .descriptor = { .atlasOrigin = { 0, 0 }, .atlasLayer = 0 },
        .pixels = std::vector<std::byte>(spritePixels.begin(), spritePixels.end()),
    });
    uploadedSprite.opaqueRects.allocate() = {
        .clip = fullClip,
        .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
        .colour = 50,
        .bounds = spriteBounds,
        .depth = 0,
        .zoom = 1.0f,
    };
    uploadedSprite.opaqueSprites.allocate() = {
        .clip = fullClip,
        .bounds = spriteBounds,
        .texelOffset = { 0, 0 },
        .asset = 0,
        .palettes = Gpu::SpriteCommand::PackPalettes(0, 0, 0, 0),
        .effects = Gpu::SpriteCommand::PackEffects(0, 0),
        .depth = 1,
        .zoom = 1.0f,
    };

    const uint64_t spriteFrameNumber = resizedFrameNumber + 1;
    ASSERT_TRUE(PresentCommandFrame(*backend, spriteFrameNumber, uploadedSprite).has_value());
    readback.assign(resizedCanvas.size(), std::byte{ 0 });
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    const auto pixel = [&](uint32_t x, uint32_t y) {
        return std::to_integer<uint8_t>(readback[static_cast<size_t>(y) * resizedLogicalExtent.width + x]);
    };
    EXPECT_EQ(pixel(4, 5), 10);
    EXPECT_EQ(pixel(5, 5), 20);
    EXPECT_EQ(pixel(4, 6), 30);
    EXPECT_EQ(pixel(5, 6), 40);

    Gpu::FrameCommandStream retainedSprite;
    retainedSprite.opaqueRects.allocate() = {
        .clip = fullClip,
        .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
        .colour = 50,
        .bounds = spriteBounds,
        .depth = 1,
        .zoom = 1.0f,
    };
    retainedSprite.opaqueSprites.allocate() = {
        .clip = fullClip,
        .bounds = spriteBounds,
        .texelOffset = { 0, 0 },
        .asset = 0,
        .palettes = 0,
        .effects = 0,
        .depth = 0,
        .zoom = 1.0f,
    };
    ASSERT_TRUE(PresentCommandFrame(*backend, spriteFrameNumber + 1, retainedSprite).has_value());
    readback.assign(resizedCanvas.size(), std::byte{ 0 });
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    EXPECT_EQ(pixel(4, 5), 50);
    EXPECT_EQ(pixel(5, 5), 50);
    EXPECT_EQ(pixel(4, 6), 50);
    EXPECT_EQ(pixel(5, 6), 50);

    const auto outsidePixel = pixel(0, 0);
    Gpu::FrameCommandStream completeSpriteFrame;
    completeSpriteFrame.opaqueRects.allocate() = {
        .clip = fullClip,
        .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
        .colour = 77,
        .bounds = { 4, 5, 5, 6 },
        .depth = 0,
        .zoom = 1.0f,
    };
    ASSERT_TRUE(PresentCommandFrame(*backend, spriteFrameNumber + 2, completeSpriteFrame).has_value());
    readback.assign(resizedCanvas.size(), std::byte{ 0 });
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    EXPECT_EQ(pixel(0, 0), outsidePixel);
    EXPECT_EQ(pixel(4, 5), 77);
    EXPECT_EQ(pixel(5, 5), 0);
    EXPECT_EQ(pixel(4, 6), 0);
    EXPECT_EQ(pixel(5, 6), 0);

    auto surfaceChunk = std::make_shared<Gpu::WorldSurfaceChunk>();
    surfaceChunk->revision = 1;
    surfaceChunk->records[0].baseZ = 0;
    surfaceChunk->records[0].present = 1;
    surfaceChunk->records[0].kind = 1;

    auto surfaceSprites = std::make_shared<Gpu::WorldSurfaceSpriteTable>();
    surfaceSprites->revision = 1;
    surfaceSprites->records.resize(1);
    surfaceSprites->catalog.materials[0].surfaceCount = 1;
    surfaceSprites->catalog.spriteEnvelope[2] = { 0, 0, 2, 2 };
    surfaceSprites->records[0].variants[2] = {
        .spriteSize = { 2, 2 },
        .spriteOffset = { 0, 0 },
        .asset = 0,
        .zoom = 0,
        .coordinateShift = 0,
        .valid = 1,
    };

    Gpu::FrameCommandStream directSurface;
    directSurface.worldSurfaces.emplace(Gpu::WorldSurfaceSceneCommand{
        .worldEpoch = 1,
        .width = 1,
        .height = 1,
        .recordCount = 1,
        .clip = fullClip,
        .view = { -10, -10 },
        .zoom = 0,
        .rotation = 0,
        .depthBase = 100,
        .chunks = { surfaceChunk },
        .sprites = surfaceSprites,
    });
    // The shader must offset native order into the reserved painter interval.
    // A preceding ordinary draw cannot cover it even though that draw is issued
    // by the executor after the native pass.
    directSurface.opaqueRects.allocate() = {
        .clip = fullClip,
        .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
        .colour = 77,
        .bounds = { 10, 10, 12, 12 },
        .depth = 99,
        .zoom = 1.0f,
    };
    ASSERT_TRUE(PresentCommandFrame(*backend, spriteFrameNumber + 3, directSurface).has_value());
    readback.assign(resizedCanvas.size(), std::byte{ 0 });
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    EXPECT_EQ(pixel(10, 10), 10);
    EXPECT_EQ(pixel(11, 10), 20);
    EXPECT_EQ(pixel(10, 11), 30);
    EXPECT_EQ(pixel(11, 11), 40);

    directSurface.opaqueRects.allocate() = {
        .clip = fullClip,
        .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
        .colour = 77,
        .bounds = { 10, 10, 11, 11 },
        .depth = 101,
        .zoom = 1.0f,
    };
    ASSERT_TRUE(PresentCommandFrame(*backend, spriteFrameNumber + 4, directSurface).has_value());
    readback.assign(resizedCanvas.size(), std::byte{ 0 });
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    EXPECT_EQ(pixel(10, 10), 77);
    EXPECT_EQ(pixel(11, 10), 20);
    EXPECT_EQ(pixel(10, 11), 30);
    EXPECT_EQ(pixel(11, 11), 40);
    directSurface.opaqueRects.clear();

    // Exercise presentation semaphore reuse without a readback/device-idle
    // between frames. The WSI image order need not match the command-slot ring.
    // The required validation lane fails on semaphore-lifetime diagnostics.
    uint64_t wraparoundFrame = spriteFrameNumber + 5;
    for (const auto mode : { Gpu::PresentMode::VSync, Gpu::PresentMode::Immediate, Gpu::PresentMode::VSync })
    {
        backend->SetPresentMode(mode);
        for (uint32_t i = 0; i < Vulkan::kFramesInFlight * 16; i++)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
            auto frame = backend->BeginFrame(wraparoundFrame);
            while (!frame.has_value() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::yield();
                frame = backend->BeginFrame(wraparoundFrame);
            }
            ASSERT_TRUE(frame.has_value());
            backend->Submit(*frame, directSurface);
            backend->Present(*frame);
            wraparoundFrame++;
        }
        ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
        EXPECT_EQ(pixel(10, 10), 10);
        EXPECT_EQ(pixel(11, 11), 40);
    }

    // Descriptor entries belong to the shared atlas, not the frame-slot fence.
    // Re-upload the same entry across in-flight frames, including upload-only
    // frames that cannot establish a dependency through a later vertex read.
    for (uint32_t i = 0; i < Vulkan::kFramesInFlight * 4; i++)
    {
        Gpu::FrameCommandStream repeatedUpload;
        repeatedUpload.textureUploads = uploadedSprite.textureUploads;
        if (i % 2 != 0)
            repeatedUpload.opaqueSprites.allocate() = uploadedSprite.opaqueSprites.data()[0];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
        auto frame = backend->BeginFrame(wraparoundFrame);
        while (!frame.has_value() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
            frame = backend->BeginFrame(wraparoundFrame);
        }
        ASSERT_TRUE(frame.has_value());
        backend->Submit(*frame, repeatedUpload);
        backend->Present(*frame);
        wraparoundFrame++;
    }
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    EXPECT_EQ(pixel(4, 5), 10);
    EXPECT_EQ(pixel(5, 6), 40);

    // One resident-atlas admission exceeds this backend's 4 MiB frame ring.
    // Exercise discard, in-flight slot reuse and rendering without another upload.
    Gpu::FrameCommandStream catalogBurst;
    catalogBurst.textureUploads.push_back({
        .atlas = 0,
        .bounds = { 0, 0, Gpu::kAtlasDimension, Gpu::kAtlasDimension },
        .sourcePitch = Gpu::kAtlasDimension,
        .descriptorIndex = 0,
        .descriptor = { .atlasOrigin = { 0, 0 }, .atlasLayer = 0 },
        .pixels = std::vector<std::byte>(Gpu::kAtlasDimension * Gpu::kAtlasDimension, std::byte{ 55 }),
    });
    catalogBurst.opaqueSprites.allocate() = uploadedSprite.opaqueSprites.data()[0];
    backend->WaitIdle();
    const auto countersBeforeBurst = backend->GetFramePresentationCounters();
    EXPECT_EQ(countersBeforeBurst.visualFrameSubmissions, countersBeforeBurst.fenceCompletedFrames);
    auto abandonedBurst = backend->BeginFrame(wraparoundFrame);
    ASSERT_TRUE(abandonedBurst.has_value());
    backend->Submit(*abandonedBurst, catalogBurst);
    // Backend::Submit records commands; only Device::EndFrame submits them.
    EXPECT_EQ(backend->GetFramePresentationCounters().visualFrameSubmissions, countersBeforeBurst.visualFrameSubmissions);
    backend->AbandonFrame(*abandonedBurst);
    backend->WaitIdle();
    const auto afterAbandon = backend->GetFramePresentationCounters();
    EXPECT_EQ(afterAbandon.visualFrameSubmissions, countersBeforeBurst.visualFrameSubmissions);
    EXPECT_EQ(afterAbandon.presentRequests, countersBeforeBurst.presentRequests);
    EXPECT_EQ(afterAbandon.fenceCompletedFrames, countersBeforeBurst.fenceCompletedFrames);
    const auto firstBurstFrameNumber = wraparoundFrame;
    for (uint32_t i = 0; i < Vulkan::kFramesInFlight + 1; ++i)
    {
        ASSERT_TRUE(PresentCommandFrame(*backend, wraparoundFrame++, catalogBurst).has_value());
        catalogBurst.textureUploads.clear();
    }
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(resizedLogicalExtent, readback));
    EXPECT_EQ(pixel(4, 5), 55);
    EXPECT_EQ(pixel(5, 6), 55);
    backend->WaitIdle();
    const auto completedBurst = backend->GetFramePresentationCounters();
    constexpr uint64_t burstFrames = Vulkan::kFramesInFlight + 1;
    EXPECT_EQ(completedBurst.visualFrameSubmissions - countersBeforeBurst.visualFrameSubmissions, burstFrames);
    EXPECT_EQ(completedBurst.presentRequests - countersBeforeBurst.presentRequests, burstFrames);
    EXPECT_EQ(completedBurst.fenceCompletedFrames - countersBeforeBurst.fenceCompletedFrames, burstFrames);
    EXPECT_EQ(
        completedBurst.presentAccepted + completedBurst.presentOutOfDate - countersBeforeBurst.presentAccepted
            - countersBeforeBurst.presentOutOfDate,
        burstFrames);
    std::vector<Gpu::FrameTimings> harvested;
    backend->TakeCompletedTimings(harvested);
    uint64_t acceptedBurstSamples = 0;
    std::optional<uint64_t> lastAcceptedTimestamp;
    for (const auto& sample : harvested)
    {
        if (sample.telemetryOnly || !sample.acceptedPresentNanoseconds)
            continue;
        if (lastAcceptedTimestamp)
            EXPECT_GE(*sample.acceptedPresentNanoseconds, *lastAcceptedTimestamp);
        lastAcceptedTimestamp = sample.acceptedPresentNanoseconds;
        if (sample.frameNumber >= firstBurstFrameNumber)
            ++acceptedBurstSamples;
    }
    EXPECT_EQ(acceptedBurstSamples, completedBurst.presentAccepted - countersBeforeBurst.presentAccepted);
    // Sample consumption and repeated drains cannot reset/double-count work.
    backend->WaitIdle();
    EXPECT_EQ(backend->GetFramePresentationCounters().fenceCompletedFrames, completedBurst.fenceCompletedFrames);
    EXPECT_EQ(backend->GetFramePresentationCounters().visualFrameSubmissions, completedBurst.visualFrameSubmissions);
}

TEST(VulkanRuntimeIntegrationTest, DiagnosticCaptureTracksFinalColourAndFrameLifetime)
{
    SdlVideoScope video;
    if (!video.Initialise())
    {
        if (RequiresVulkanTests())
            FAIL() << "Required SDL video is unavailable: " << SDL_GetError();
        GTEST_SKIP() << "SDL video is unavailable: " << SDL_GetError();
    }
    using WindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
    WindowPtr window(SDL_CreateWindow(
        "OpenRCT2 Vulkan diagnostic capture test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 32, 32,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI | Vulkan::Platform::GetRequiredSdlWindowFlags()));
    const auto shaderDirectory = FindVulkanShaderDirectory();
    if (window == nullptr || shaderDirectory.empty())
    {
        if (RequiresVulkanTests())
            FAIL() << "Required Vulkan window or shader set is unavailable: " << SDL_GetError();
        GTEST_SKIP() << "Vulkan window or shader set is unavailable: " << SDL_GetError();
    }
    const auto drawable = Vulkan::Platform::GetDrawableExtent(window.get());
    if (drawable.width == 0 || drawable.height == 0)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required Vulkan window has no drawable extent";
        GTEST_SKIP() << "Vulkan window has no drawable extent";
    }
    constexpr Gpu::Extent logical = { 32, 32 };
    Gpu::BackendConfig config = {

        .logicalExtent = logical,
        .drawableExtent = { drawable.width, drawable.height },
        .presentMode = Gpu::PresentMode::Immediate,
        .frameAcquireMode = Gpu::FrameAcquireMode::Wait,
        .outputColorMode = Gpu::OutputColorMode::Sdr,
        .uploadRingBytesPerFrame = 4 * 1024 * 1024,
        .shaderDirectory = shaderDirectory.string(),
    };
    auto backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(window.get()));
    try
    {
        backend->Initialise(config);
    }
    catch (const std::exception& error)
    {
        if (RequiresVulkanTests())
            FAIL() << "Required Vulkan initialisation failed: " << error.what();
        GTEST_SKIP() << "Vulkan initialisation is unavailable: " << error.what();
    }

    auto disabledFrame = backend->BeginFrame(1);
    ASSERT_TRUE(disabledFrame.has_value());
    EXPECT_FALSE(backend->RequestFrameCapture(*disabledFrame));
    Gpu::FrameCommandStream firstPrimitives;
    firstPrimitives.opaqueRects.allocate() = {
        .clip = { 0, 0, 32, 32 },
        .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
        .colour = 5,
        .bounds = { 0, 0, 32, 32 },
        .depth = 0,
        .zoom = 1.0f,
    };
    // Abandon the very first recorded atlas/table transitions, then draw with
    // no sprite upload. The validation lane checks every bound image layout.
    backend->Submit(*disabledFrame, firstPrimitives);
    backend->AbandonFrame(*disabledFrame);
    EXPECT_FALSE(backend->ReadbackFrameRgba(1).has_value());
    ASSERT_TRUE(PresentCommandFrame(*backend, 2, firstPrimitives).has_value());
    std::vector<std::byte> recoveredIndices(logical.width * logical.height);
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(logical, recoveredIndices));
    EXPECT_EQ(recoveredIndices, std::vector<std::byte>(recoveredIndices.size(), std::byte{ 5 }));

    backend->Dispose();
    config.enableDiagnosticCapture = true;
    ASSERT_NO_THROW(backend->Initialise(config));
    std::array<std::byte, 256 * 4> palette{};
    constexpr size_t index = 5;
    palette[index * 4] = std::byte{ 10 };
    palette[index * 4 + 1] = std::byte{ 20 };
    palette[index * 4 + 2] = std::byte{ 30 };
    palette[index * 4 + 3] = std::byte{ 255 };
    backend->SetPalette(palette);
    Gpu::FrameCommandStream commands;
    commands.opaqueRects.allocate() = {
        .clip = { 0, 0, 32, 32 },
        .flags = Gpu::RectCommand::FLAG_NO_TEXTURE,
        .colour = index,
        .bounds = { 0, 0, 32, 32 },
        .depth = 0,
        .zoom = 1.0f,
    };
    ASSERT_TRUE(PresentCommandFrame(*backend, 10, commands, true).has_value());
    const auto unlit = backend->ReadbackFrameRgba(10);
    ASSERT_TRUE(unlit.has_value());
    EXPECT_EQ(unlit->frameNumber, 10u);
    EXPECT_EQ(unlit->logicalExtent, logical);
    EXPECT_EQ(unlit->drawableExtent, config.drawableExtent);
    EXPECT_FALSE(unlit->lightFxEnabled);
    EXPECT_FALSE(unlit->deviceName.empty());
    EXPECT_GT(unlit->swapchainGeneration, 0u);
    EXPECT_GT(unlit->paletteVersion, 0u);
    const size_t rgbaSize = static_cast<size_t>(drawable.width) * drawable.height * 4;
    ASSERT_EQ(unlit->rgba.size(), rgbaSize);
    const std::array unlitColour = { std::byte{ 10 }, std::byte{ 20 }, std::byte{ 30 }, std::byte{ 255 } };
    for (size_t i = 0; i < rgbaSize; i++)
        ASSERT_EQ(unlit->rgba[i], unlitColour[i % 4]) << "Unlit output byte " << i;

    std::vector<std::byte> indices(static_cast<size_t>(logical.width) * logical.height);
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(logical, indices));
    EXPECT_EQ(indices, std::vector<std::byte>(indices.size(), std::byte{ index }));

    // Deliberately supply a uniform intensity image without baked falloffs.
    // This isolates actual final-palette shader arithmetic from light rasterization.
    commands.lightFx.emplace();
    commands.lightFx->width = logical.width;
    commands.lightFx->height = logical.height;
    commands.lightFx->intensities.assign(indices.size(), std::byte{ 64 });
    commands.lightFx->lightPalette[index * 4] = std::byte{ 12 };
    commands.lightFx->lightPalette[index * 4 + 1] = std::byte{ 24 };
    commands.lightFx->lightPalette[index * 4 + 2] = std::byte{ 36 };
    ASSERT_TRUE(PresentCommandFrame(*backend, 11, commands, true).has_value());
    const auto lit = backend->ReadbackFrameRgba(11);
    ASSERT_TRUE(lit.has_value());
    ASSERT_EQ(lit->rgba.size(), rgbaSize);
    EXPECT_TRUE(lit->lightFxEnabled);
    EXPECT_EQ(lit->paletteVersion, unlit->paletteVersion);
    EXPECT_EQ(lit->swapchainGeneration, unlit->swapchainGeneration);
    EXPECT_NE(lit->rgba, unlit->rgba);
    // Per channel: dark + floor(light * (64 * 6) / 256).
    const std::array litColour = { std::byte{ 28 }, std::byte{ 56 }, std::byte{ 84 }, std::byte{ 255 } };
    for (size_t i = 0; i < rgbaSize; i++)
        ASSERT_EQ(lit->rgba[i], litColour[i % 4]) << "Lit output byte " << i;
    std::vector<std::byte> litIndices(indices.size());
    ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(logical, litIndices));
    EXPECT_EQ(litIndices, indices);
    const auto retainedUnlit = backend->ReadbackFrameRgba(10);
    ASSERT_TRUE(retainedUnlit.has_value());
    EXPECT_EQ(retainedUnlit->rgba, unlit->rgba);

    commands.lightFx.reset();
    ASSERT_TRUE(PresentCommandFrame(*backend, 12, commands).has_value());
    EXPECT_FALSE(backend->ReadbackFrameRgba(12).has_value());
    ASSERT_TRUE(PresentCommandFrame(*backend, 13, commands).has_value());
    EXPECT_FALSE(backend->ReadbackFrameRgba(10).has_value());
    EXPECT_TRUE(backend->ReadbackFrameRgba(11).has_value());
    EXPECT_FALSE(backend->ReadbackFrameRgba(999).has_value());

    const auto abandoned = backend->BeginFrame(14);
    ASSERT_TRUE(abandoned.has_value());
    ASSERT_TRUE(backend->RequestFrameCapture(*abandoned));
    EXPECT_THROW((void)backend->ReadbackFrameRgba(14), std::logic_error);
    backend->Submit(*abandoned, commands);
    EXPECT_THROW((void)backend->RequestFrameCapture(*abandoned), std::logic_error);
    backend->AbandonFrame(*abandoned);
    EXPECT_FALSE(backend->ReadbackFrameRgba(14).has_value());
    EXPECT_FALSE(backend->ReadbackFrameRgba(11).has_value());

    ASSERT_TRUE(PresentCommandFrame(*backend, 15, commands, true).has_value());
    ASSERT_TRUE(backend->ReadbackFrameRgba(15).has_value());
    constexpr Gpu::Extent resizedLogical = { 24, 24 };
    backend->Resize(resizedLogical, config.drawableExtent);
    EXPECT_FALSE(backend->ReadbackFrameRgba(15).has_value());
    ASSERT_TRUE(PresentCommandFrame(*backend, 16, commands, true).has_value());
    const auto resized = backend->ReadbackFrameRgba(16);
    ASSERT_TRUE(resized.has_value());
    EXPECT_EQ(resized->logicalExtent, resizedLogical);
    EXPECT_EQ(resized->drawableExtent, config.drawableExtent);
    EXPECT_GT(resized->swapchainGeneration, unlit->swapchainGeneration);
    ASSERT_EQ(resized->rgba.size(), rgbaSize);
    for (size_t i = 0; i < rgbaSize; i++)
        ASSERT_EQ(resized->rgba[i], unlitColour[i % 4]) << "Resized output byte " << i;
    backend->Dispose();
    EXPECT_FALSE(backend->ReadbackFrameRgba(16).has_value());
}

TEST(VulkanPipelineCacheTest, RejectsTruncatedCorruptAndForeignDriverCaches)
{
    VkPhysicalDeviceProperties device{};
    device.vendorID = 17;
    device.deviceID = 29;
    device.driverVersion = 41;
    device.pipelineCacheUUID[0] = 59;
    VkPipelineCacheHeaderVersionOne header{};
    header.headerSize = sizeof(header);
    header.headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
    header.vendorID = device.vendorID;
    header.deviceID = device.deviceID;
    std::copy(std::begin(device.pipelineCacheUUID), std::end(device.pipelineCacheUUID), header.pipelineCacheUUID);
    std::vector<std::byte> payload(sizeof(header) + 19, std::byte{ 0x5a });
    std::memcpy(payload.data(), &header, sizeof(header));
    const auto file = Vulkan::EncodePipelineCacheFile(payload, device);
    ASSERT_FALSE(file.empty());
    const auto decoded = Vulkan::DecodePipelineCacheFile(file, device);
    EXPECT_TRUE(std::ranges::equal(decoded, payload));
    for (size_t length = 0; length < file.size(); ++length)
        EXPECT_TRUE(Vulkan::DecodePipelineCacheFile(std::span(file).first(length), device).empty());
    auto changed = file;
    changed.back() ^= std::byte{ 1 };
    EXPECT_TRUE(Vulkan::DecodePipelineCacheFile(changed, device).empty());
    changed = file;
    changed[0] ^= std::byte{ 1 };
    EXPECT_TRUE(Vulkan::DecodePipelineCacheFile(changed, device).empty());
    auto foreign = device;
    ++foreign.driverVersion;
    EXPECT_TRUE(Vulkan::DecodePipelineCacheFile(file, foreign).empty());
    foreign = device;
    ++foreign.deviceID;
    EXPECT_TRUE(Vulkan::DecodePipelineCacheFile(file, foreign).empty());
    foreign = device;
    ++foreign.pipelineCacheUUID[0];
    EXPECT_TRUE(Vulkan::DecodePipelineCacheFile(file, foreign).empty());
    payload[0] ^= std::byte{ 1 };
    EXPECT_TRUE(Vulkan::EncodePipelineCacheFile(payload, device).empty());
}

TEST(VulkanStartupTest, PreparationRunsOffUiThreadAndRestoresWindowOnFailure)
{
    SdlVideoScope video;
    ASSERT_TRUE(video.Initialise());
    using WindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
    WindowPtr window(
        SDL_CreateWindow("Startup regression", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 320, SDL_WINDOW_HIDDEN));
    ASSERT_NE(window, nullptr);
    const auto uiThread = std::this_thread::get_id();
    std::thread::id workThread;
    EXPECT_THROW(
        Vulkan::Platform::PreparePipelines(
            window.get(),
            [&]() {
                workThread = std::this_thread::get_id();
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                throw std::runtime_error("expected preparation failure");
            }),
        std::runtime_error);
    EXPECT_NE(workThread, uiThread);
    EXPECT_STREQ(SDL_GetWindowTitle(window.get()), "Startup regression");
}

    #ifdef _WIN32
TEST(VulkanStartupTest, NativeWindowRemainsResponsiveDuringPreparation)
{
    SdlVideoScope video;
    ASSERT_TRUE(video.Initialise());
    using WindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
    WindowPtr window(
        SDL_CreateWindow("Responsive startup", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 320, SDL_WINDOW_HIDDEN));
    ASSERT_NE(window, nullptr);
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    ASSERT_EQ(SDL_GetWindowWMInfo(window.get(), &info), SDL_TRUE);
    bool responded = false;
    Vulkan::Platform::PreparePipelines(window.get(), [&]() {
        DWORD_PTR result{};
        // A cross-thread synchronous window message requires the UI thread to keep dispatching messages.
        responded = SendMessageTimeoutW(info.info.win.window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &result) != 0;
    });
    EXPECT_TRUE(responded);
    EXPECT_STREQ(SDL_GetWindowTitle(window.get()), "Responsive startup");
}
    #endif

#endif // ENABLE_VULKAN
