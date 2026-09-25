/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include "VulkanParityTestSupport.h"

    #include <atomic>
    #include <cmath>
    #include <cstring>
    #include <cstdlib>
    #include <fstream>
    #include <future>
    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2-renderer/vulkan/VulkanRenderService.h>
    #include <openrct2-renderer/vulkan/VulkanFrameExecutor.h>
    #include <openrct2-renderer/vulkan/VulkanPalettePipeline.h>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/SpriteIds.h>
    #include <openrct2/command_line/CommandLine.hpp>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/Drawing.h>
    #include <openrct2/drawing/RenderTarget.h>
    #include <openrct2/drawing/X8DrawingEngine.h>

namespace
{
    using namespace OpenRCT2::Drawing;
    using namespace std::chrono_literals;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    namespace Gpu = OpenRCT2::Ui::Gpu;

    OffscreenRenderRequest Request()
    {
        OffscreenRenderRequest request{
            .name = "offscreen-primitives", .logicalExtent = { 64, 48 }, .outputExtent = { 64, 48 }, .clearIndex = 7
        };
        request.rgbaOutput = true;
        for (size_t i = 0; i < 256; ++i)
            request.palette[i] = { static_cast<uint8_t>(i), static_cast<uint8_t>((i * 37) % 256),
                                   static_cast<uint8_t>((i * 83) % 256), static_cast<uint8_t>((i * 13) % 256) };
        return request;
    }
    void Paint(IDrawingContext& context, RenderTarget& target)
    {
        context.FillRect(target, static_cast<PaletteIndex>(61), -7, 3, 28, 25);
        context.FillRect(target, static_cast<PaletteIndex>(132), 17, 11, 62, 43, true);
        context.FillRect(target, static_cast<PaletteIndex>(0), 3, 7, 13, 18);
        context.DrawLine(target, static_cast<PaletteIndex>(203), { { 2, 45 }, { 61, 1 } });
        auto clip = target.Crop({ 25, 20 }, { 23, 18 });
        context.FillRect(clip, static_cast<PaletteIndex>(77), 25, 20, 47, 37);
        context.DrawLine(clip, static_cast<PaletteIndex>(12), { { 20, 17 }, { 49, 39 } });
    }
    std::vector<std::byte> Reference(const OffscreenRenderRequest& request)
    {
        auto bytes = request.initialIndices;
        if (bytes.empty())
            bytes.assign(
                static_cast<size_t>(request.logicalExtent.width) * request.logicalExtent.height,
                static_cast<std::byte>(request.clearIndex));
        RenderTarget target{ .bits = reinterpret_cast<PaletteIndex*>(bytes.data()),
                             .width = static_cast<int32_t>(request.logicalExtent.width),
                             .height = static_cast<int32_t>(request.logicalExtent.height) };
        X8DrawingContext context(nullptr);
        context.BeginDraw();
        Paint(context, target);
        context.EndDraw();
        return bytes;
    }
    std::array<std::byte, 1024> EffectivePalette(const OffscreenRenderRequest& request)
    {
        std::array<std::byte, 1024> palette{};
        for (size_t i = 0; i < 256; ++i)
        {
            const auto colour = request.palette[i];
            palette[i * 4] = static_cast<std::byte>(colour.red);
            palette[i * 4 + 1] = static_cast<std::byte>(colour.green);
            palette[i * 4 + 2] = static_cast<std::byte>(colour.blue);
            palette[i * 4 + 3] = static_cast<std::byte>(
                request.alphaPolicy == RenderAlphaPolicy::opaque                     ? 255
                    : request.alphaPolicy == RenderAlphaPolicy::transparentIndexZero ? (i == 0 ? 0 : 255)
                                                                                     : colour.alpha);
        }
        return palette;
    }
    class VulkanOffscreenRenderTest : public testing::Test
    {
    protected:
        std::shared_ptr<Vulkan::DeviceContext> context;
        Vulkan::RenderServiceOptions options;
        void SetUp() override
        {
            if (const auto* path = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                options.shaderDirectory = path;
            else
                options.shaderDirectory = std::filesystem::current_path() / "data/shaders/vulkan";
            ASSERT_TRUE(std::filesystem::exists(options.shaderDirectory / "indexed_palette.frag.spv"))
                << "Set OPENRCT2_VULKAN_SHADER_DIRECTORY to receipt-qualified installed shaders";
            try
            {
                context = Vulkan::DeviceContext::CreateGraphicsOnly();
            }
            catch (const std::exception& error)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required && std::string(required) == "1")
                    FAIL() << error.what();
                GTEST_SKIP() << error.what();
            }
        }
        std::unique_ptr<IRenderService> Service()
        {
            return Vulkan::CreateRenderServiceFactory(options, [shared = context] { return shared; })->Create();
        }
        void Compare(const OffscreenRenderRequest& request, const RenderResult& actual)
        {
            const auto expected = Reference(request);
            const auto palette = EffectivePalette(request);
            const auto rgba = VulkanParitySupport::Expand(expected, palette);
            std::filesystem::path artifacts;
            if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
                artifacts = std::filesystem::path(path) / "offscreen";
            const json_t metadata = { { "fixture", request.name },
                                      { "fixtureVersion", 1 },
                                      { "offscreen", true },
                                      { "submissionId", actual.identity.submissionId },
                                      { "targetGeneration", actual.identity.targetGeneration },
                                      { "alphaPolicy", static_cast<uint8_t>(request.alphaPolicy) } };
            EXPECT_EQ(
                VulkanParitySupport::CompareAndReport(
                    artifacts, request.name, "indexed", expected, actual.indexed, 1, palette, metadata,
                    { request.logicalExtent.width, request.logicalExtent.height }),
                0u);
            EXPECT_EQ(
                VulkanParitySupport::CompareAndReport(
                    artifacts, request.name, "rgba", rgba, actual.rgba, 4, palette, metadata,
                    { request.outputExtent.width, request.outputExtent.height }),
                0u);
        }
    };
} // namespace

// Separate suite keeps the existing offscreen image-report contract unchanged.
class VulkanHdrOutputTest : public VulkanOffscreenRenderTest
{
};

TEST_F(VulkanHdrOutputTest, AbsolutePqWhiteAndSdrPaletteSurviveDynamicWhiteChanges)
{
    constexpr Gpu::Extent extent{ 16, 16 };
    std::array<std::byte, 1024> palette{};
    std::array<std::byte, 256> indices{};
    for (size_t i = 0; i < 256; ++i)
    {
        indices[i] = static_cast<std::byte>(i);
        for (size_t channel = 0; channel < 3; ++channel)
            palette[i * 4 + channel] = static_cast<std::byte>(i);
        palette[i * 4 + 3] = std::byte{ 255 };
    }
    for (size_t primary = 0; primary < 3; ++primary)
        for (size_t channel = 0; channel < 3; ++channel)
            palette[(252 + primary) * 4 + channel] = static_cast<std::byte>(primary == channel ? 255 : 0);

    // Independent double-precision reference: IEC sRGB EOTF, linear D65
    // sRGB-to-BT.2020 primaries, then the absolute ST 2084 OETF (10,000 nits).
    const auto reference = [&](size_t index, float white) {
        std::array<double, 3> linear{};
        for (size_t channel = 0; channel < 3; ++channel)
        {
            const auto value = std::to_integer<uint8_t>(palette[index * 4 + channel]) / 255.0;
            linear[channel] = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        }
        constexpr double matrix[3][3] = {
            { 0.6274038959, 0.3292830384, 0.0433130657 },
            { 0.0690972894, 0.9195403951, 0.0113623156 },
            { 0.0163914389, 0.0880133079, 0.8955952532 },
        };
        std::array<uint32_t, 3> codes{};
        for (size_t channel = 0; channel < 3; ++channel)
        {
            double luminance = 0;
            for (size_t component = 0; component < 3; ++component)
                luminance += matrix[channel][component] * linear[component];
            const auto p = std::pow(luminance * white / 10000.0, 2610.0 / 16384.0);
            const auto pq = std::pow((3424.0 / 4096.0 + 2413.0 / 128.0 * p) / (1 + 2392.0 / 128.0 * p), 2523.0 / 32.0);
            codes[channel] = static_cast<uint32_t>(std::lround(pq * 1023));
        }
        return codes;
    };
    json_t reports = json_t::array();
    for (const auto format : { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                               VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB })
    {
        SCOPED_TRACE(format);
        const bool hdr = format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        Vulkan::FrameExecutor executor;
        executor.Initialise(context, extent, options.shaderDirectory, 1, 1, false);
        executor.SetPalette(palette);
        Vulkan::SubmissionSlots slots(context, 1024 * 1024, 1);
        Vulkan::Image image;
        image.Initialise(context->GetPhysicalDevice(), context->GetDevice(), { 16, 16, 1 }, 1, format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        Vulkan::PalettePipeline pipeline;
        pipeline.Initialise(*context, executor.GetResources(), options.shaderDirectory, 203.0f);
        const std::array views{ image.GetView() };
        pipeline.RefreshOutput(format, { 16, 16 }, views, 1, hdr ? 2 : (format == VK_FORMAT_R8G8B8A8_SRGB ? 1 : 0),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        for (const auto mode : { Gpu::ScaleMode::Nearest, Gpu::ScaleMode::Linear, Gpu::ScaleMode::SmoothNearest })
        {
            for (const float white : { 80.0f, 203.0f, 280.0f, 400.0f, 1000.0f, 203.0f })
            {
                SCOPED_TRACE(white);
                pipeline.SetHdrPaperWhiteNits(white);
                auto token = slots.Begin(0, true);
                ASSERT_TRUE(token.has_value());
                const auto output = executor.Record(*token, {}, 0, indices);
                ASSERT_NE(output.canvas, nullptr);
                pipeline.SetCanvasSource(0, *output.canvas);
                pipeline.Record(*token, 0, { 16, 16 }, false, extent, { mode, 2 });
                auto readback = token->upload->Allocate(1024, 16);
                ASSERT_TRUE(static_cast<bool>(readback));
                const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                Vulkan::RecordImageBarrier(token->commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
                const VkBufferImageCopy copy{ .bufferOffset = readback.offset,
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { 16, 16, 1 } };
                vkCmdCopyImageToBuffer(token->commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    readback.buffer, 1, &copy);
                const VkMemoryBarrier host{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
                vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    0, 1, &host, 0, nullptr, 0, nullptr);
                slots.Submit(*token);
                ASSERT_TRUE(slots.Wait(*token, 30'000'000'000ULL));
                executor.Commit();
                token->upload->Invalidate(readback.offset, readback.size);
                std::array<uint32_t, 256> packed{};
                std::memcpy(packed.data(), readback.data, sizeof(packed));
                uint32_t maximumError = 0;
                json_t expectedCodes = json_t::array();
                for (size_t index = 0; index < 256; ++index)
                {
                    if (hdr)
                    {
                        const auto expected = reference(index, white);
                        expectedCodes.push_back(expected);
                        for (size_t channel = 0; channel < 3; ++channel)
                        {
                            const auto shift = static_cast<uint32_t>((format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ? channel : 2 - channel) * 10);
                            const auto actual = (packed[index] >> shift) & 1023;
                            const auto error = actual > expected[channel] ? actual - expected[channel] : expected[channel] - actual;
                            maximumError = std::max(maximumError, error);
                        }
                        EXPECT_EQ(packed[index] >> 30, 3u);
                    }
                    else
                    {
                        uint32_t expected;
                        std::memcpy(&expected, palette.data() + index * 4, 4);
                        EXPECT_EQ(packed[index], expected) << "SDR palette index " << index;
                        maximumError += packed[index] != expected ? 1 : 0;
                    }
                }
                EXPECT_LE(maximumError, hdr ? 1u : 0u) << "format=" << format << " mode=" << static_cast<int>(mode);
                reports.push_back({ { "format", format }, { "mode", static_cast<int>(mode) }, { "whiteNits", white },
                    { "maximumError", maximumError }, { "packedActual", packed }, { "referenceRgb10", expectedCodes } });
            }
        }
    }
    ASSERT_EQ(reports.size(), 72u);
    if (const auto* directory = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
    {
        const auto path = std::filesystem::path(directory) / "hdr-output";
        std::filesystem::create_directories(path);
        std::ofstream report(path / "palette-reference.json");
        ASSERT_TRUE(report.good());
        report << json_t{ { "schemaVersion", 1 }, { "passed", !HasFailure() }, { "samples", reports }, { "hdrCodeTolerance", 1 },
            { "sdrByteTolerance", 0 }, { "hardwareReadback", true }, { "requiresHdrMonitor", false } }.dump(2);
        report.close();
        ASSERT_FALSE(report.fail());
    }
    RecordProperty("hdrOutputSamples", 72);
}

TEST_F(VulkanOffscreenRenderTest, TransparentPeelsPreserveExactCommandDepthAcrossQuadDiagonals)
{
    constexpr std::array<int32_t, 24> depthBases{
        0, 1, 7, 31, 127, 511, 1023, 2047, 4095, 8191, 16383, 32767,
        65535, 131071, 262143, 524287, 1048575, 2097151, 4194280, 7919, 15838, 23757, 31676, 39595 };
    std::array<std::byte, 256 * 256> remap{};
    for (uint32_t row = 0; row < 256; row++)
        for (uint32_t index = 0; index < 256; index++)
            remap[row * 256 + index] = static_cast<std::byte>(index);
    for (uint32_t row = 1; row <= 3; row++)
        for (uint32_t index = 1; index < 256; index++)
            remap[row * 256 + index] = static_cast<std::byte>(1 + ((index - 1) * (row == 2 ? 7 : 2) + row * 17) % 255);
    std::filesystem::path artifacts;
    if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS")) artifacts = std::filesystem::path(path) / "offscreen";
    for (const auto extent : { Gpu::Extent{2048,1440}, Gpu::Extent{2047,1439} })
    {
        SCOPED_TRACE(extent.height);
        Vulkan::FrameExecutor executor;
        executor.Initialise(context, extent, options.shaderDirectory, 1, 1, false);
        Vulkan::SubmissionSlots slots(context, 32 * 1024 * 1024, 1);
        executor.SetRemapPalette(remap);
        const size_t pixels = static_cast<size_t>(extent.width) * extent.height;
        std::vector<std::byte> initial(pixels), expected;
        for (size_t index = 0; index < pixels; index++) initial[index] = static_cast<std::byte>(1 + index % 255);
        expected = initial;

        Gpu::FrameCommandStream commands;
        // The first clipped quad has the observed giant tile's local diagonal:
        // (192,32) to (160,64). Other groups vary clipping, shape and draw order.
        for (int32_t group = 0; group < 96; group++)
        {
            const int32_t left = 160 + (group % 12) * 64;
            const int32_t top = 32 + (group / 12) * 80;
            const int32_t width = group % 3 == 0 ? 32 : 31 + group % 7;
            const int32_t height = group % 3 == 0 ? 32 : 29 + group % 11;
            const int32_t base = depthBases[static_cast<size_t>(group) % depthBases.size()];
            const auto opaqueColour = static_cast<uint32_t>(1 + (group * 11) % 255);
            auto& opaque = commands.opaqueRects.allocate();
            opaque = { .clip = { left, top, left + width, top + height },
                .flags = Gpu::RectCommand::FLAG_NO_TEXTURE, .colour = opaqueColour,
                .bounds = { left - 32, top, left + width, top + height }, .depth = base, .zoom = 1.0f };
            for (int32_t y = top; y < top + height; y++)
                for (int32_t x = left; x < left + width; x++)
                    expected[static_cast<size_t>(y) * extent.width + static_cast<size_t>(x)] = static_cast<std::byte>(opaqueColour);
            for (int32_t layer = 0; layer < 3; layer++)
            {
                auto& command = commands.transparentRects.allocate();
                command = { .clip = { left, top, left + width, top + height },
                    .flags = Gpu::RectCommand::FLAG_NO_TEXTURE, .colour = static_cast<uint32_t>(layer + 1),
                    .bounds = { left - 32, top, left + width, top + height }, .depth = base + layer + 1, .zoom = 1.0f };
                for (int32_t y = top; y < top + height; y++)
                    for (int32_t x = left; x < left + width; x++)
                    {
                        const size_t index = static_cast<size_t>(y) * extent.width + static_cast<size_t>(x);
                        expected[index] = remap[static_cast<size_t>(layer + 1) * 256 + std::to_integer<uint8_t>(expected[index])];
                    }
            }
        }
        auto token = slots.Begin(0, true); ASSERT_TRUE(token.has_value());
        const auto output = executor.Record(*token, commands, 0, initial);
        ASSERT_NE(output.canvas, nullptr);
        auto readback = token->upload->Allocate(pixels, 16);
        ASSERT_TRUE(static_cast<bool>(readback));
        const VkImageSubresourceRange colourRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Vulkan::RecordImageBarrier(token->commandBuffer, output.canvas->GetImage(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colourRange,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        const VkBufferImageCopy colourCopy{ .bufferOffset = readback.offset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { extent.width, extent.height, 1 } };
        vkCmdCopyImageToBuffer(token->commandBuffer, output.canvas->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readback.buffer, 1, &colourCopy);
        const VkMemoryBarrier host{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host, 0, nullptr, 0, nullptr);
        slots.Submit(*token); ASSERT_TRUE(slots.Wait(*token, UINT64_MAX)); executor.Commit();
        token->upload->Invalidate(readback.offset, readback.size);
        const auto name = "exact-peel-depth-" + std::to_string(extent.width) + "x" + std::to_string(extent.height);
        json_t metadata = { { "fixtureVersion", 1 }, { "referenceImageLabel", "ordered-remap-oracle" },
            { "resultImageLabel", "vulkan" }, { "quadGroups", 96 }, { "peels", 3 },
            { "scope", "Independent ordered indexed remap composition across exact command depths; no scene exception" } };
        EXPECT_EQ(VulkanParitySupport::CompareAndReport(artifacts, name, "indexed", expected,
            std::span<const std::byte>(readback.data, pixels), 1, EffectivePalette(Request()), metadata, extent), 0u);
    }
    RecordProperty("exactDepthSamples", 2);
}

TEST_F(VulkanOffscreenRenderTest, LaterOpaqueWaterOverlaySuppressesEarlierMaskAtAdjacentDepth)
{
    constexpr Gpu::Extent extent{ 2048, 1440 };
    constexpr int32_t left = 161, top = 31, side = 32;
    constexpr std::array<int32_t, 8> depths{ 0, 127, 1023, 8191, 16383, 32767, 65535, 131071 };
    constexpr uint8_t background = 55;
    std::array<std::byte, 256 * 256> remap{};
    for (uint32_t row = 0; row < 256; row++)
        for (uint32_t index = 0; index < 256; index++)
            remap[row * 256 + index] = static_cast<std::byte>(index);
    for (uint32_t row = 1; row <= 5; row++)
        for (uint32_t index = 1; index < 256; index++)
            remap[row * 256 + index] = static_cast<std::byte>(193 + (index * (row == 2 ? 5 : 1) + row) % 6);
    Gpu::FrameCommandStream commands;
    for (uint32_t asset = 0; asset < 2; asset++)
    {
        Gpu::TextureUpload upload;
        upload.bounds = { static_cast<int32_t>(asset * 64), 0, static_cast<int32_t>((asset + 1) * 64), 32 };
        upload.sourcePitch = 64; upload.descriptorIndex = asset;
        upload.descriptor = { { static_cast<int32_t>(asset * 64), 0 }, 0, 0 };
        upload.pixels.resize(64 * 32);
        for (uint32_t y = 0; y < 32; y++)
            for (uint32_t x = 0; x < 64; x++)
            {
                const uint32_t value = asset == 0 ? 1 + (x * 3 + y) % 5
                    : (x + y) % 5 == 0 ? 0 : 193 + (x + y * 3) % 6;
                upload.pixels[y * 64 + x] = static_cast<std::byte>(value);
            }
        commands.textureUploads.push_back(std::move(upload));
    }
    // These are already clipped as CalculateSpriteGeometry produces them;
    // atlas offset32 selects the right half of the original64x32 sprite.
    auto& mask = commands.transparentRects.allocate();
    mask = { .clip = { left, top, left + side, top + side }, .texColourAtlas = 0,
        .texColourBounds = { 32.0f, 0.0f, 2048.0f, 2048.0f }, .flags = 0, .colour = 0,
        .bounds = { left, top, left + side, top + side }, .zoom = 1.0f };
    auto& overlay = commands.opaqueSprites.allocate();
    overlay = { .clip = { left, top, left + side, top + side },
        .bounds = { left, top, left + side, top + side }, .texelOffset = { 32, 0 }, .asset = 1, .zoom = 1.0f };
    const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
    std::vector<std::byte> expected(pixelCount, static_cast<std::byte>(background));
    for (int32_t y = 0; y < side; y++)
        for (int32_t x = 0; x < side; x++)
        {
            const size_t source = static_cast<size_t>(y) * 64 + static_cast<size_t>(x + 32);
            const auto waterRow = std::to_integer<uint8_t>(commands.textureUploads[0].pixels[source]);
            const auto opaque = commands.textureUploads[1].pixels[source];
            expected[static_cast<size_t>(top + y) * extent.width + static_cast<size_t>(left + x)]
                = opaque != std::byte{0} ? opaque : remap[waterRow * 256 + background];
        }
    std::filesystem::path artifacts;
    if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS")) artifacts = std::filesystem::path(path) / "offscreen";
    Vulkan::FrameExecutor executor;
    executor.Initialise(context, extent, options.shaderDirectory, 1, 1, false);
    Vulkan::SubmissionSlots slots(context, 16 * 1024 * 1024, 1);
    executor.SetRemapPalette(remap);
    for (const auto depth : depths)
    {
        SCOPED_TRACE(depth);
        mask.depth = depth; overlay.depth = depth + 1;
        auto token = slots.Begin(0, true); ASSERT_TRUE(token.has_value());
        const auto output = executor.Record(*token, commands, background);
        ASSERT_NE(output.canvas, nullptr);
        auto readback = token->upload->Allocate(pixelCount, 16); ASSERT_TRUE(static_cast<bool>(readback));
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Vulkan::RecordImageBarrier(token->commandBuffer, output.canvas->GetImage(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        const VkBufferImageCopy copy{ .bufferOffset = readback.offset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { extent.width, extent.height, 1 } };
        vkCmdCopyImageToBuffer(token->commandBuffer, output.canvas->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readback.buffer, 1, &copy);
        Vulkan::RecordImageBarrier(token->commandBuffer, output.canvas->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
        const VkMemoryBarrier host{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host, 0, nullptr, 0, nullptr);
        slots.Submit(*token); ASSERT_TRUE(slots.Wait(*token, UINT64_MAX)); executor.Commit();
        token->upload->Invalidate(readback.offset, readback.size);
        const auto name = "water-overlay-adjacent-depth-" + std::to_string(depth);
        const json_t metadata = { { "fixtureVersion", 1 }, { "referenceImageLabel", "water-order-oracle" },
            { "resultImageLabel", "vulkan" }, { "waterMaskDepth", depth }, { "opaqueOverlayDepth", depth + 1 },
            { "transparentOverlaps", 1 }, { "bounds", { left, top, left + side, top + side } },
            { "scope", "Synthetic water parent then opaque overlay; compose comparison only, no second peel" } };
        EXPECT_EQ(VulkanParitySupport::CompareAndReport(artifacts, name, "indexed", expected,
            std::span<const std::byte>(readback.data, pixelCount), 1, EffectivePalette(Request()), metadata, extent), 0u);
        commands.textureUploads.clear(); // Same retained immutable atlas for the next depth value.
    }
    RecordProperty("waterOrderSamples", static_cast<int>(depths.size()));
}

TEST_F(VulkanOffscreenRenderTest, FrozenPrimitivesOwnedInitialContentsAndAllAlphaPoliciesAreExact)
{
    auto service = Service();
    for (const bool initial : { false, true })
    {
        for (const auto alpha :
             { RenderAlphaPolicy::opaque, RenderAlphaPolicy::paletteAlpha, RenderAlphaPolicy::transparentIndexZero })
        {
            auto request = Request();
            request.alphaPolicy = alpha;
            request.name = std::string(initial ? "owned-" : "clear-") + std::to_string(static_cast<uint8_t>(alpha));
            if (initial)
            {
                request.initialContents = RenderInitialContents::ownedIndices;
                request.initialIndices.resize(64 * 48);
                for (size_t i = 0; i < request.initialIndices.size(); ++i)
                    request.initialIndices[i] = static_cast<std::byte>(i % 256);
            }
            auto session = service->BeginOffscreen(request);
            ASSERT_NE(session->GetRenderTarget().DrawingEngine, nullptr);
            EXPECT_EQ(session->GetRenderTarget().DrawingEngine->getRT(), &session->GetRenderTarget());
            Paint(*session->GetRenderTarget().DrawingEngine->GetDrawingContext(), session->GetRenderTarget());
            const auto completion = session->Submit();
            session.reset(); // Job must own all commands, payloads and leases.
            const auto outcome = completion->Wait(30s);
            ASSERT_FALSE(outcome.error) << outcome.error->message;
            ASSERT_NE(outcome.result, nullptr);
            Compare(request, *outcome.result);
        }
    }
}

TEST_F(VulkanOffscreenRenderTest, ReadbackCapacityIsIndependentOfTheUploadBudget)
{
    options.uploadBytes = 1024 * 1024;
    auto service = Service();
    auto request = Request();
    request.logicalExtent = { 513, 512 };
    request.outputExtent = request.logicalExtent;
    for (const uint8_t colour : { 7, 93 })
    {
        request.clearIndex = colour;
        auto session = service->BeginOffscreen(request);
        const auto outcome = session->Submit()->Wait(30s);
        ASSERT_FALSE(outcome.error) << outcome.error->message;
        ASSERT_NE(outcome.result, nullptr);
        const std::vector<std::byte> expected(513 * 512, static_cast<std::byte>(colour));
        EXPECT_EQ(outcome.result->indexed, expected);
        EXPECT_EQ(outcome.result->rgba, VulkanParitySupport::Expand(expected, EffectivePalette(request)));
    }
}

TEST_F(VulkanOffscreenRenderTest, SharedDeviceAuxiliaryCompletionLeavesMainRecordingAndSlotUntouched)
{
    Vulkan::SubmissionSlots mainSlots(context, 1024 * 1024, 1);
    const auto mainToken = *mainSlots.Begin(0, true);
    std::vector<PaletteIndex> bits(64 * 48);
    RenderTarget target{ .bits = bits.data(), .width = 64, .height = 48 };
    Gpu::TextureCache mainCache(1);
    Gpu::FrameCommandStream mainCommands;
    Gpu::CommandDrawingContext mainRecorder(target, mainCache);
    mainCache.BeginFrame();
    mainRecorder.Begin(mainCommands);
    mainRecorder.FillRect(target, static_cast<PaletteIndex>(19), 1, 1, 8, 8);
    const auto before = mainCommands.opaqueRects.size();
    auto service = Service();
    auto request = Request();
    auto session = service->BeginOffscreen(request);
    Paint(session->GetDrawingContext(), session->GetRenderTarget());
    const auto outcome = session->Submit()->Wait(30s);
    ASSERT_FALSE(outcome.error) << outcome.error->message;
    EXPECT_TRUE(mainRecorder.IsActive());
    EXPECT_EQ(mainCommands.opaqueRects.size(), before);
    EXPECT_THROW(mainSlots.Begin(0, false), std::logic_error); // Still unsubmitted, same generation.
    mainRecorder.FillRect(target, static_cast<PaletteIndex>(21), 10, 10, 17, 17);
    EXPECT_EQ(mainCommands.opaqueRects.size(), before + 1);
    mainRecorder.End();
    mainCache.AbortFrame();
    mainSlots.Abandon(mainToken);
    Compare(request, *outcome.result);
}

TEST_F(VulkanOffscreenRenderTest, TargetReuseResizeAndServiceShutdownPreserveOwnedResults)
{
    auto service = Service();
    auto request = Request();
    auto first = service->BeginOffscreen(request);
    Paint(first->GetDrawingContext(), first->GetRenderTarget());
    const auto one = first->Submit()->Wait(30s);
    ASSERT_FALSE(one.error) << one.error->message;
    const auto retained = one.result->rgba;
    request.name = "resized-clear";
    request.logicalExtent = { 19, 11 };
    request.outputExtent = { 38, 22 };
    request.clearIndex = 93;
    request.alphaPolicy = RenderAlphaPolicy::opaque;
    auto second = service->BeginOffscreen(request);
    const auto two = second->Submit()->Wait(30s);
    ASSERT_FALSE(two.error) << two.error->message;
    EXPECT_EQ(one.identity.targetId, two.identity.targetId);
    EXPECT_LT(one.identity.targetGeneration, two.identity.targetGeneration);
    EXPECT_NE(one.identity.submissionId, two.identity.submissionId);
    EXPECT_EQ(two.result->indexed, std::vector<std::byte>(19 * 11, std::byte{ 93 }));
    const auto palette = EffectivePalette(request);
    const auto expected = VulkanParitySupport::Expand(std::vector<std::byte>(38 * 22, std::byte{ 93 }), palette);
    EXPECT_EQ(two.result->rgba, expected);
    service->Shutdown();
    service.reset();
    EXPECT_EQ(one.result->rgba, retained);
}

TEST_F(VulkanOffscreenRenderTest, SubmittedSpritePayloadAndCoveredZeroAreOwnedBeforeWorkerExecution)
{
    struct Restore
    {
        bool noGraphics = gOpenRCT2NoGraphics;
        OpenRCT2::G1Element element{};
        Restore()
        {
            gOpenRCT2NoGraphics = false;
            element = *GfxGetG1Element(SPR_TEMP_BEGIN);
        }
        ~Restore()
        {
            GfxSetG1Element(SPR_TEMP_BEGIN, &element);
            gOpenRCT2NoGraphics = noGraphics;
        }
    } restore;
    gOpenRCT2NoGraphics = false;
    std::vector<uint8_t> source(17 * 13);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = i % 5 == 0 ? 0 : static_cast<uint8_t>(1 + (i * 17) % 254);
    OpenRCT2::G1Element sprite{ .offset = source.data(), .width = 17, .height = 13 }; // Covered zero is opaque.
    GfxSetG1Element(SPR_TEMP_BEGIN, &sprite);
    auto request = Request();
    request.name = "owned-sprite-covered-zero";
    std::vector<std::byte> expected(64 * 48, std::byte{ 7 });
    RenderTarget target{ .bits = reinterpret_cast<PaletteIndex*>(expected.data()), .width = 64, .height = 48 };
    X8DrawingContext oracle(nullptr);
    oracle.BeginDraw();
    oracle.DrawSprite(target, ImageId(SPR_TEMP_BEGIN), 11, 9);
    oracle.EndDraw();
    std::promise<void> release;
    auto gate = release.get_future().share();
    auto service = Vulkan::CreateRenderServiceFactory(options, [&] {
                       gate.wait();
                       return context;
                   })->Create();
    auto session = service->BeginOffscreen(request);
    session->GetDrawingContext().DrawSprite(session->GetRenderTarget(), ImageId(SPR_TEMP_BEGIN), 11, 9);
    const auto completion = session->Submit();
    std::fill(source.begin(), source.end(), 199);
    sprite.width = 1;
    sprite.height = 1;
    GfxSetG1Element(SPR_TEMP_BEGIN, &sprite);
    session.reset();
    release.set_value();
    const auto outcome = completion->Wait(30s);
    ASSERT_FALSE(outcome.error) << outcome.error->message;
    const auto palette = EffectivePalette(request);
    const auto expectedRgba = VulkanParitySupport::Expand(expected, palette);
    std::filesystem::path artifacts;
    if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
        artifacts = std::filesystem::path(path) / "offscreen";
    EXPECT_EQ(
        VulkanParitySupport::CompareAndReport(
            artifacts, request.name, "indexed", expected, outcome.result->indexed, 1, palette,
            { { "fixture", request.name }, { "fixtureVersion", 1 }, { "mutatedSourceBeforeExecution", true } }, { 64, 48 }),
        0u);
    EXPECT_EQ(
        VulkanParitySupport::CompareAndReport(
            artifacts, request.name, "rgba", expectedRgba, outcome.result->rgba, 4, palette,
            { { "fixture", request.name }, { "fixtureVersion", 1 }, { "mutatedSourceBeforeExecution", true } }, { 64, 48 }),
        0u);
}

TEST_F(VulkanOffscreenRenderTest, TimeoutAndCancellationDoNotReleaseTheBusyDomainBeforeItsJobRetires)
{
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto service = Vulkan::CreateRenderServiceFactory(options, [&] {
                       entered.set_value();
                       gate.wait();
                       return context;
                   })->Create();
    auto session = service->BeginOffscreen(Request());
    const auto completion = session->Submit();
    entered.get_future().wait();
    const auto timeout = completion->Wait(0ms);
    EXPECT_TRUE(timeout.error);
    if (timeout.error)
        EXPECT_EQ(timeout.error->code, RenderErrorCode::timeout);
    completion->Cancel();
    // BeginOffscreen waits for actual retirement. While this test deliberately
    // holds the worker gate, the nonblocking API must still report the slot busy.
    EXPECT_EQ(service->TryBeginOffscreen(Request()), nullptr);
    const auto cancelled = completion->Wait(0ms);
    EXPECT_TRUE(cancelled.error);
    if (cancelled.error)
        EXPECT_EQ(cancelled.error->code, RenderErrorCode::cancelled);
    release.set_value();
    service->Shutdown(); // Drains the actual submitted job despite terminal delivery.
    EXPECT_FALSE(completion->Wait(0ms).result);
}

TEST(VulkanOffscreenServiceContract, FactoryLazyOwnerDoesNotCreateGpuForUnusedOrCancelledRecording)
{
    std::atomic_uint calls = 0;
    auto factory = Vulkan::CreateRenderServiceFactory({ .shaderDirectory = "not-used" }, [&] {
        ++calls;
        return std::shared_ptr<Vulkan::DeviceContext>{};
    });
    {
        LazyRenderService unused(factory);
    }
    EXPECT_EQ(calls, 0u);
    {
        LazyRenderService lazy(factory);
        auto session = lazy.Get().BeginOffscreen(Request());
        session->Cancel();
        session.reset();
        lazy.Shutdown();
    }
    EXPECT_EQ(calls, 0u);
}

TEST(VulkanOffscreenServiceContract, MissingInjectedDeviceFailsNamedImageAndNeverCreatesFallback)
{
    std::atomic_uint calls = 0;
    auto service = Vulkan::CreateRenderServiceFactory({ .shaderDirectory = "not-used" }, [&] {
                       ++calls;
                       return std::shared_ptr<Vulkan::DeviceContext>{};
                   })->Create();
    auto session = service->BeginOffscreen(Request());
    const auto outcome = session->Submit()->Wait(5s);
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(outcome.error->code, RenderErrorCode::unavailable);
    EXPECT_EQ(outcome.identity.name, "offscreen-primitives");
    EXPECT_EQ(calls, 1u);
    EXPECT_THROW(service->BeginOffscreen(Request()), RenderServiceException);
}

TEST(VulkanOffscreenServiceContract, ShutdownReleasesDeviceProviderEvenWhenAnUnsubmittedRecorderSurvives)
{
    auto marker = std::make_shared<int>(1);
    std::weak_ptr<int> lifetime = marker;
    auto service = Vulkan::CreateRenderServiceFactory({ .shaderDirectory = "not-used" }, [marker] {
                       ADD_FAILURE() << "Unsubmitted recording must not acquire a device";
                       return std::shared_ptr<Vulkan::DeviceContext>{};
                   })->Create();
    marker.reset();
    auto session = service->BeginOffscreen(Request());
    EXPECT_FALSE(lifetime.expired());
    service->Shutdown();
    EXPECT_TRUE(lifetime.expired());
    EXPECT_THROW(session->Submit(), RenderServiceException);
    session->Cancel();
}

TEST(VulkanOffscreenServiceContract, InvalidUnsupportedAndWrongThreadRequestsDoNotStartDevice)
{
    std::atomic_uint calls = 0;
    auto service = Vulkan::CreateRenderServiceFactory({ .shaderDirectory = "not-used", .maxTargetPixels = 4096 }, [&] {
                       ++calls;
                       return std::shared_ptr<Vulkan::DeviceContext>{};
                   })->Create();
    auto request = Request();
    request.lightingEnabled = true;
    EXPECT_THROW(service->BeginOffscreen(request), RenderServiceException);
    request.lightingEnabled = false;
    request.logicalExtent = { 100, 100 };
    EXPECT_THROW(service->BeginOffscreen(request), RenderServiceException);
    auto other = std::async(
        std::launch::async, [&] { EXPECT_THROW(service->BeginOffscreen(Request()), RenderServiceException); });
    other.get();
    auto active = service->BeginOffscreen(Request());
    EXPECT_THROW(service->BeginOffscreen(Request()), RenderServiceException);
    active.reset();
    auto next = service->BeginOffscreen(Request());
    next.reset();
    service->Shutdown();
    EXPECT_EQ(calls, 0u);
}
TEST(VulkanOffscreenServiceContract, SessionTargetRoutesLegacyDrawingAndRejectsLifetimeChangesWithoutGpu)
{
    std::atomic_uint calls = 0;
    auto service = Vulkan::CreateRenderServiceFactory({ .shaderDirectory = "not-used" }, [&] {
                       ++calls;
                       return std::shared_ptr<Vulkan::DeviceContext>{};
                   })->Create();
    auto session = service->BeginOffscreen(Request());
    auto& target = session->GetRenderTarget();
    auto* engine = target.DrawingEngine;
    ASSERT_NE(engine, nullptr);
    EXPECT_FALSE(engine->GetFlags().has(DrawingEngineFlag::parallelDrawing));
    EXPECT_EQ(engine->GetDrawingContext(), &session->GetDrawingContext());
    auto cropped = target.Crop({ 3, 4 }, { 20, 10 });
    EXPECT_EQ(cropped.DrawingEngine, engine);
    GfxClear(cropped, static_cast<PaletteIndex>(42));
    EXPECT_THROW(engine->Resize(99, 33), RenderServiceException);
    EXPECT_THROW(engine->BeginDraw(), RenderServiceException);
    EXPECT_THROW(engine->EndDraw(), RenderServiceException);
    session->Cancel();
    EXPECT_THROW(engine->GetDrawingContext(), RenderServiceException);
    EXPECT_THROW(engine->getRT(), RenderServiceException);
    EXPECT_EQ(calls, 0u);
}

TEST(VulkanOffscreenServiceContract, RejectedCommandDoesNotInvokeInjectedFactory)
{
    struct Factory final : IRenderServiceFactory
    {
        uint32_t calls{};
        std::unique_ptr<IRenderService> Create() override
        {
            ++calls;
            throw std::runtime_error("Non-rendering command requested a renderer");
        }
    };
    auto factory = std::make_shared<Factory>();
    const char* arguments[] = { "offscreen-diagnostic", "--invalid-offscreen-contract-option" };
    EXPECT_EQ(OpenRCT2::CommandLineRun(arguments, 2, factory), OpenRCT2::CommandLine::ExitCode::fail);
    EXPECT_EQ(factory->calls, 0u);
}
#endif
