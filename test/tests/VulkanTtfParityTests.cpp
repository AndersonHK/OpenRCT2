/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <cstdlib>
#include <gtest/gtest.h>
#include <string_view>

namespace
{
    bool RequiredTtfParity()
    {
        const auto* value = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        return value != nullptr && std::string_view(value) == "1";
    }
} // namespace

#if defined(ENABLE_VULKAN) && !defined(DISABLE_TTF)
    #include "VulkanParityTestSupport.h"

    #include <SDL.h>
    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanPlatform.h>
    #include <openrct2/drawing/BlendColourMap.h>
    #include <openrct2/drawing/Drawing.String.h>
    #include <openrct2/drawing/TTF.h>
    #include <openrct2/drawing/X8DrawingEngine.h>
    #include <openrct2/interface/ColourWithFlags.h>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    using namespace VulkanParitySupport;
    constexpr Gpu::Extent kExtent{ 96, 80 };
    constexpr size_t kPixelCount = kExtent.width * kExtent.height;

    struct FixtureBlendMap
    {
        BlendColourMapType* destination = GetBlendColourMap();
        BlendColourMapType original = *destination;
        FixtureBlendMap()
        {
            // The blend lookup is an explicit input, independent of host fonts or mutable game palette.
            // Both renderers consume these same fixed bytes, restoring the previous table after the test.
            for (size_t a = 0; a < 256; a++)
                for (size_t b = 0; b < 256; b++)
                    (*destination)[a][b] = static_cast<PaletteIndex>((a + b) / 2);
        }
        ~FixtureBlendMap()
        {
            *destination = original;
        }
    };

    std::array<std::byte, 1024> TtfPalette()
    {
        std::array<std::byte, 1024> palette{};
        for (size_t i = 0; i < 256; i++)
        {
            palette[i * 4] = static_cast<std::byte>(i);
            palette[i * 4 + 1] = static_cast<std::byte>(255 - i);
            palette[i * 4 + 2] = static_cast<std::byte>((i * 37) & 255);
            palette[i * 4 + 3] = std::byte{ 255 };
        }
        return palette;
    }

    std::vector<uint8_t> CoverageBytes(uint32_t variant = 0)
    {
        constexpr std::array<uint8_t, 16> values{ 0, 1, 63, 64, 65, 127, 128, 179, 180, 181, 200, 254, 255, 42, 90, 160 };
        std::vector<uint8_t> pixels(16 * 7);
        for (size_t y = 0; y < 7; y++)
            for (size_t x = 0; x < 16; x++)
                pixels[y * 16 + x] = values[(x + y * 3 + variant * 5) % values.size()];
        return pixels;
    }

    void DrawTtfFixture(std::string_view name, IDrawingContext& drawing, RenderTarget& target, TTFSurface& surface)
    {
        drawing.Clear(target, static_cast<PaletteIndex>(7));
        for (int32_t x = 0; x < 96; x += 8)
            drawing.FillRect(target, static_cast<PaletteIndex>(17 + x), x, 0, x + 7, 79);
        TextDrawInfo info{};
        info.palette.fill = static_cast<PaletteIndex>(211);
        info.palette.shadowOutline = static_cast<PaletteIndex>(13);
        if (name == "TtfOutline" || name == "TtfOutlineInset")
            info.colourFlags.set(ColourFlag::withOutline);
        if (name == "TtfInset" || name == "TtfOutlineInset")
            info.colourFlags.set(ColourFlag::inset);
        if (name == "TtfZeroInk")
        {
            info.palette.fill = PaletteIndex::transparent;
            info.palette.shadowOutline = PaletteIndex::transparent;
            info.colourFlags.set(ColourFlag::withOutline);
            info.colourFlags.set(ColourFlag::inset);
        }
        const std::array<uint8_t, 6> thresholds{ 0, 1, 64, 180, 181, 255 };
        for (size_t row = 0; row < thresholds.size(); row++)
        {
            drawing.DrawTTFBitmap(target, info, &surface, 4, 3 + static_cast<int32_t>(row) * 12, thresholds[row]);
            drawing.DrawTTFBitmap(target, info, &surface, 28, 3 + static_cast<int32_t>(row) * 12, thresholds[row]);
        }
        if (name == "TtfClipping")
        {
            auto child = target.Crop({ 51, 9 }, { 31, 51 });
            child.x = -11;
            child.y = 13;
            info.colourFlags.set(ColourFlag::withOutline);
            drawing.DrawTTFBitmap(child, info, &surface, -16, 10, 64);
            drawing.DrawTTFBitmap(child, info, &surface, 12, 60, 1);
            auto nested = child.Crop({ 4, 9 }, { 19, 25 });
            nested.x = 27;
            nested.y = -3;
            drawing.DrawTTFBitmap(nested, info, &surface, 23, -5, 180);
            drawing.DrawTTFBitmap(nested, info, &surface, 42, 18, 0);
        }
        if (name == "TtfOrdering")
        {
            drawing.DrawTTFBitmap(target, info, &surface, 57, 17, 64);
            drawing.FillRect(target, static_cast<PaletteIndex>(67), 60, 19, 67, 30);
            info.palette.fill = static_cast<PaletteIndex>(151);
            info.colourFlags.set(ColourFlag::withOutline);
            drawing.DrawTTFBitmap(target, info, &surface, 59, 20, 1);
            drawing.DrawTTFBitmap(target, info, &surface, 63, 23, 180);
        }
    }

    class VulkanTtfParityTest : public testing::TestWithParam<const char*>
    {
    protected:
        bool ownsVideo = false;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{ nullptr, SDL_DestroyWindow };
        std::unique_ptr<Gpu::Backend> backend;
        std::filesystem::path shaders;
        std::string Initialise()
        {
            ownsVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
            if (ownsVideo && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                return SDL_GetError();
            window.reset(SDL_CreateWindow(
                "OpenRCT2 TTF bitmap parity", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kExtent.width, kExtent.height,
                SDL_WINDOW_HIDDEN | Vulkan::Platform::GetRequiredSdlWindowFlags()));
            if (!window)
                return SDL_GetError();
            std::vector<std::filesystem::path> candidates{ std::filesystem::current_path() / "data/shaders/vulkan" };
            if (char* base = SDL_GetBasePath())
            {
                candidates.emplace_back(std::filesystem::path(base) / "data/shaders/vulkan");
                SDL_free(base);
            }
            if (const auto* overridePath = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                candidates.insert(candidates.begin(), overridePath);
            for (const auto& path : candidates)
                if (std::filesystem::is_regular_file(path / "indexed_palette.frag.spv"))
                {
                    shaders = path;
                    break;
                }
            if (shaders.empty())
                return "Compiled Vulkan shaders unavailable";
            const auto drawable = Vulkan::Platform::GetDrawableExtent(window.get());
            if (drawable.width != kExtent.width || drawable.height != kExtent.height)
                return "The primitive fixture requires an unscaled 96x80 drawable";
            Gpu::BackendConfig config{ .logicalExtent = kExtent,
                                       .drawableExtent = kExtent,
                                       .presentMode = Gpu::PresentMode::Immediate,
                                       .frameAcquireMode = Gpu::FrameAcquireMode::Wait,
                                       .shaderDirectory = shaders.string() };
            config.enableDiagnosticCapture = true;
            backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(window.get()));
            try
            {
                backend->Initialise(config);
            }
            catch (const std::exception& error)
            {
                return error.what();
            }
            return {};
        }

        void TearDown() override
        {
            backend.reset();
            window.reset();
            if (ownsVideo)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    };
} // namespace

TEST_P(VulkanTtfParityTest, FrozenSoftwareBitmapPixels)
{
    const auto unavailable = Initialise();
    if (!unavailable.empty())
    {
        if (RequiredTtfParity())
            FAIL() << unavailable;
        GTEST_SKIP() << unavailable;
    }
    FixtureBlendMap blend;
    const auto palette = TtfPalette();
    backend->SetPalette(palette);
    std::vector<std::byte> remap(256 * 256);
    // Transparent-pass composition uses remap row zero for an unchanged destination.
    // Keep that required identity row even though this fixture draws no remapped sprites.
    for (size_t i = 0; i < 256; i++)
        remap[i] = static_cast<std::byte>(i);
    backend->SetRemapPalette(remap);
    backend->SetBlendPalette(std::as_bytes(std::span<const BlendColourMapType>{ blend.destination, 1 }));
    Gpu::TextureCache textures;
    auto pixels = CoverageBytes();
    TTFSurface surface{ pixels.data(), 16, 7, 7001 };
    std::vector<PaletteIndex> reference(kPixelCount);
    std::vector<PaletteIndex> addressSpace(kPixelCount);
    RenderTarget referenceTarget{ .bits = reference.data(), .width = kExtent.width, .height = kExtent.height };
    RenderTarget gpuTarget{ .bits = addressSpace.data(), .width = kExtent.width, .height = kExtent.height };
    const bool cache = std::string_view(GetParam()) == "TtfCacheReplacement";
    for (uint32_t frameIndex = 0; frameIndex < (cache ? 3U : 1U); frameIndex++)
    {
        // A newly rasterized bitmap gets a new identity, even when its allocation address is reused.
        // A copied immutable bitmap keeps the identity on frame2 and must remain a cache hit.
        if (frameIndex == 1)
        {
            const auto replacement = CoverageBytes(1);
            std::copy(replacement.begin(), replacement.end(), pixels.begin());
            surface.cacheId = 7002;
        }
        std::vector<uint8_t> relocated;
        if (frameIndex == 2)
        {
            relocated = pixels;
            surface.pixels = relocated.data();
        }
        X8DrawingContext software(nullptr);
        software.BeginDraw();
        DrawTtfFixture(GetParam(), software, referenceTarget, surface);
        software.EndDraw();
        const auto ink = std::string_view(GetParam()) == "TtfZeroInk" ? PaletteIndex::transparent
                                                                      : static_cast<PaletteIndex>(211);
        ASSERT_GT(std::count(reference.begin(), reference.end(), ink), 0);
        Gpu::FrameCommandStream commands;
        Gpu::CommandDrawingContext recorder(gpuTarget, textures);
        textures.BeginFrame();
        recorder.Begin(commands);
        DrawTtfFixture(GetParam(), recorder, gpuTarget, surface);
        recorder.End();
        const auto lease = textures.SealFrame(commands);
        EXPECT_EQ(commands.textureUploads.size(), frameIndex == 2 ? 0u : 1u);
        const uint64_t frameNumber = 7000 + frameIndex;
        const auto frame = backend->BeginFrame(frameNumber);
        ASSERT_TRUE(frame.has_value());
        ASSERT_TRUE(backend->RequestFrameCapture(*frame));
        backend->Submit(*frame, commands);
        backend->Present(*frame);
        backend->WaitIdle();
        textures.RetireFrame(lease, Gpu::FrameRetirement::Presented);
        textures.DrainFrameRetirements();
        std::filesystem::path artifacts;
        if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
            artifacts = path;
        const auto fixture = std::string(GetParam()) + (cache ? "_F" + std::to_string(frameIndex) : "");
        json_t metadata{
            { "fixtureVersion", 2 },
            { "remapRowZero", "identity" },
            { "inkIndex", static_cast<uint8_t>(ink) },
            { "frameNumber", frameNumber },
            { "softwareReference", "frozen X8 DrawTTFBitmap; synthetic immutable coverage bytes" },
            { "coverageBytes", pixels },
            { "coverageWidth", 16 },
            { "coverageHeight", 7 },
            { "hintingThresholds", { 0, 1, 64, 180, 181, 255 } },
            { "blendLookup", "floor((sourceIndex+destinationIndex)/2), fixed symmetric fixture table" },
            { "zoom", 0 },
            { "zoomLimit", "direct frozen TTF bitmap contract asserts zoom0; font shaping/layout at other zooms unqualified" },
            { "cacheId", surface.cacheId },
            { "uploadCount", commands.textureUploads.size() },
            { "fontLimit", "Synthetic post-raster TTFSurface; excludes font selection/shaping and TTF library rasterization" },
        };
        for (const auto& entry : std::filesystem::directory_iterator(shaders))
            if (entry.path().extension() == ".spv")
                metadata["shaderHashes"][entry.path().filename().string()] = HashFile(entry.path());
        std::vector<std::byte> indexed(kPixelCount);
        ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(kExtent, indexed));
        const auto expected = std::as_bytes(std::span(reference));
        const auto captured = backend->ReadbackFrameRgba(frameNumber);
        ASSERT_TRUE(captured.has_value());
        metadata["deviceName"] = captured->deviceName;
        metadata["driverVersion"] = captured->driverVersion;
        EXPECT_EQ(CompareAndReport(artifacts, fixture, "indexed", expected, indexed, 1, palette, metadata, kExtent), 0u);
        EXPECT_EQ(
            CompareAndReport(
                artifacts, fixture, "rgba", Expand(expected, palette), captured->rgba, 4, palette, metadata, kExtent),
            0u);
    }
}

INSTANTIATE_TEST_SUITE_P(
    Ttf, VulkanTtfParityTest,
    testing::Values(
        "TtfThresholds", "TtfOutline", "TtfInset", "TtfOutlineInset", "TtfClipping", "TtfOrdering", "TtfCacheReplacement",
        "TtfZeroInk"),
    [](const testing::TestParamInfo<const char*>& info) { return info.param; });

#else
TEST(VulkanTtfParityTest, RequiredBackendAvailability)
{
    if (RequiredTtfParity())
        FAIL() << "Required TTF parity needs Vulkan and TTF enabled";
    GTEST_SKIP() << "Vulkan or TTF unavailable";
}
#endif
