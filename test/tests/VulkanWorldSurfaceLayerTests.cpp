// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include "VulkanParityTestSupport.h"

    #include <cstdlib>
    #include <cstring>
    #include <openrct2-renderer/vulkan/VulkanFrameExecutor.h>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>

namespace
{
    namespace G = OpenRCT2::Ui::Gpu;
    namespace V = OpenRCT2::Ui::Vulkan;
    namespace D = OpenRCT2::Drawing;
    class VulkanWorldSurfaceLayerTest : public testing::Test
    {
    protected:
        static constexpr G::Extent extent{ 96, 96 };
        std::shared_ptr<V::DeviceContext> device;
        V::FrameExecutor executor;
        std::unique_ptr<V::SubmissionSlots> slots;
        std::shared_ptr<G::WorldSurfaceChunk> chunk;
        std::shared_ptr<G::WorldSurfaceSpriteTable> sprites;
        G::WorldSurfaceSceneCommand scene;
        bool uploaded{};
        bool authoredBackgroundAndForeground{};
        bool edgeColumnPattern{};
        uint32_t sample{};
        std::vector<std::byte> pixels;
        void SetUp() override
        {
            const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
            ASSERT_NE(shaders, nullptr);
            device = V::DeviceContext::CreateGraphicsOnly();
            slots = std::make_unique<V::SubmissionSlots>(device, 8 * 1024 * 1024, 1);
            executor.Initialise(device, extent, shaders, 1, 1, true);
            std::array<std::byte, 256 * 256> remap{};
            for (uint32_t row = 0; row < 256; row++)
                for (uint32_t x = 0; x < 256; x++)
                    remap[row * 256 + x] = std::byte((x + row) & 255);
            executor.SetRemapPalette(remap);
            chunk = std::make_shared<G::WorldSurfaceChunk>();
            chunk->revision = 1;
            chunk->records[0] = { 32, 48, 0, 0, 0, 0, 1, 1 };
            sprites = std::make_shared<G::WorldSurfaceSpriteTable>();
            sprites->revision = 1;
            sprites->records.resize(59);
            for (uint32_t i = 0; i < 59; i++)
            {
                auto& sprite = sprites->records[i];
                const bool edge = i >= 19 && i < 56;
                sprite.variants[2] = { { 8, edge ? 8 : 40 }, { 0, 0 }, edge ? 3u : 0u, 0, 0, 1 };
                if (i < 19)
                    sprite.effects = i << 16;
            }
            sprites->catalog.materials[0].surfaceCount = 19;
            sprites->catalog.materials[0].edgeBase = 19;
            sprites->catalog.materials[0].edgeCount = 37;
            sprites->catalog.spriteEnvelope[2] = { -2, -2, 11, 43 };
            for (uint32_t shape = 0; shape < 5; shape++)
            {
                sprites->catalog.waterMask[shape] = 56;
                sprites->catalog.waterOverlay[shape] = 57;
                sprites->catalog.waterOpaque[shape] = 58;
            }
            sprites->records[56].variants[2].asset = 1;
            sprites->records[56].effects = 1u << 8;
            sprites->records[56].palettes = 7;
            sprites->records[57].variants[2].asset = 2;
            sprites->records[57].effects = 1u << 9;
            sprites->records[58] = sprites->records[57];
            scene = { .worldEpoch = 1,
                      .width = 1,
                      .height = 1,
                      .recordCount = 1,
                      .clip = { 0, 0, 96, 96 },
                      .view = { -16, -64 },
                      .zoom = 0,
                      .rotation = 0,
                      .depthBase = 1,
                      .chunks = { chunk },
                      .sprites = sprites };
        }
        void TearDown() override
        {
            executor.Dispose();
            slots.reset();
            device.reset();
        }
        uint8_t Pixel(uint32_t x, uint32_t y) const
        {
            return std::to_integer<uint8_t>(pixels[y * extent.width + x]);
        }
        D::RenderUploadTelemetry Run(bool abandon = false, bool overflow = false)
        {
            G::FrameCommandStream commands;
            commands.worldSurfaces = scene;
            if (authoredBackgroundAndForeground)
            {
                commands.opaqueRects.allocate() = { .clip = { 0, 0, 96, 96 },
                                                    .flags = G::RectCommand::FLAG_NO_TEXTURE,
                                                    .colour = 10,
                                                    .bounds = { 0, 0, 96, 96 },
                                                    .depth = 0,
                                                    .zoom = 1.0f };
                commands.opaqueRects.allocate() = { .clip = { 0, 0, 96, 96 },
                                                    .flags = G::RectCommand::FLAG_NO_TEXTURE,
                                                    .colour = 77,
                                                    .bounds = { 16, 38, 18, 42 },
                                                    .depth = scene.depthBase + G::kWorldSurfaceDepthCapacity,
                                                    .zoom = 1.0f };
            }
            if (!uploaded)
                for (uint32_t asset = 0; asset < 4; asset++)
                {
                    std::vector<std::byte> texels(8 * 40);
                    for (uint32_t y = 0; y < 40; y++)
                        for (uint32_t x = 0; x < 8; x++)
                            texels[y * 8 + x] = std::byte(
                                asset == 0       ? 20
                                    : asset == 1 ? 1
                                    : asset == 2 ? (x == 0 ? 99 : 0)
                                                 : (edgeColumnPattern ? 42 + x : 42));
                    commands.textureUploads.push_back(
                        { .atlas = 0,
                          .bounds = { int32_t(asset * 8), 0, int32_t(asset * 8 + 8), 40 },
                          .sourcePitch = 8,
                          .descriptorIndex = asset,
                          .descriptor = { .atlasOrigin = { int32_t(asset * 8), 0 }, .atlasLayer = 0 },
                          .pixels = std::move(texels) });
                }
            D::RenderUploadTelemetry telemetry;
            auto token = slots->Begin(0, true, &telemetry);
            if (!token)
                throw std::runtime_error("Terrain layer test slot unavailable");
            const auto output = executor.Record(*token, commands);
            if (abandon)
            {
                slots->Abandon(*token);
                executor.Discard(0);
                return telemetry;
            }
            auto readback = token->upload->Allocate(extent.width * extent.height, 4);
            if (!readback)
                throw std::runtime_error("Terrain layer readback allocation failed");
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            V::RecordImageBarrier(
                token->commandBuffer, output.canvas->GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            const VkBufferImageCopy copy{ .bufferOffset = readback.offset,
                                          .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                          .imageExtent = { extent.width, extent.height, 1 } };
            vkCmdCopyImageToBuffer(
                token->commandBuffer, output.canvas->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                &copy);
            V::RecordImageBarrier(
                token->commandBuffer, output.canvas->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
            const VkMemoryBarrier host{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                                        VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(
                token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
                nullptr);
            slots->Submit(*token);
            executor.Commit();
            if (!slots->Wait(*token, 30'000'000'000))
                throw std::runtime_error("Terrain layer test timeout");
            if (overflow)
                EXPECT_THROW(executor.CompleteTerrainStatus(0), std::runtime_error);
            else
                executor.CompleteTerrainStatus(0);
            token->upload->Invalidate(readback.offset, readback.size);
            pixels.assign(readback.data, readback.data + extent.width * extent.height);
            uploaded = true;
            if (const auto* directory = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
            {
                const auto path = std::filesystem::path(directory) / "world-surface-layers"
                    / testing::UnitTest::GetInstance()->current_test_info()->name();
                std::filesystem::create_directories(path);
                std::array<std::byte, 1024> palette{};
                for (uint32_t i = 0; i < 256; i++)
                {
                    palette[4 * i] = palette[4 * i + 1] = palette[4 * i + 2] = std::byte(i);
                    palette[4 * i + 3] = std::byte{ 255 };
                }
                VulkanParitySupport::SaveRgba(
                    path / (std::to_string(sample++) + ".png"), VulkanParitySupport::Expand(pixels, palette), extent);
            }
            return telemetry;
        }
    };
} // namespace
TEST_F(VulkanWorldSurfaceLayerTest, RawFactsSelectImagesAndRenderCliffWaterLayersWithoutRepeatUploads)
{
    Run(true); // Abandon the first catalog/chunk upload: the next accepted frame must resend it.
    const auto first = Run();
    EXPECT_EQ(first.worldBufferCopyCalls, 8u); // Includes absent catalogs, pose and both selection headers.
    EXPECT_EQ(Pixel(18, 40), 27);              // Original20 filtered by mask row7.
    EXPECT_EQ(Pixel(16, 40), 99);              // Opaque ripple overlay, after the filter.
    EXPECT_EQ(Pixel(18, 64), 20);              // Terrain outside the water sprite.
    // The synthetic front-right strip projects to world X30..38. Original tile
    // visitation owns columns[-32,0) and[0,32), so the art must stop at world X32
    // (framebuffer X48). Real edge art fits that footprint; our fake8px art crosses it.
    EXPECT_EQ(Pixel(47, 65), 42); // Cliff remains visible inside its original column.
    EXPECT_EQ(Pixel(48, 65), 0);  // It cannot bleed into a column that never visits this tile.
    const auto second = Run();
    EXPECT_EQ(second.worldBufferCopyCalls, 0u);
    EXPECT_EQ(second.bytes[size_t(D::UploadCategory::world)][size_t(D::UploadMetric::bufferTransfer)], 0u);
    constexpr int offsets[4]{ 2, 1, 8, 4 };
    constexpr G::Int2 views[4]{ { -16, -64 }, { -48, -80 }, { -16, -96 }, { 16, -80 } };
    for (uint32_t rotation = 0; rotation < 4; rotation++)
    {
        auto changed = std::make_shared<G::WorldSurfaceChunk>(*chunk);
        changed->revision = 2;
        changed->records[0].slope = 1;
        scene.chunks[0] = changed;
        scene.rotation = rotation;
        scene.view = views[rotation];
        const auto telemetry = Run();
        EXPECT_EQ(telemetry.worldBufferCopyCalls, rotation == 0 ? 1u : 0u);
        EXPECT_EQ(Pixel(18, 64), 20 + offsets[rotation]);
        EXPECT_EQ(Pixel(18, 40), 27 + offsets[rotation]);
    }
}
TEST_F(VulkanWorldSurfaceLayerTest, OverflowReportsFailureAndDrawsNoPartialWorld)
{
    scene.outputCapacity = 1;
    Run(false, true);
    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](std::byte pixel) { return pixel == std::byte{}; }));
}
TEST_F(VulkanWorldSurfaceLayerTest, TerrainFilterReadsAuthoredBackgroundAndPreservesLaterOpaqueForeground)
{
    authoredBackgroundAndForeground = true;
    chunk->records[0].waterHeight = 0;
    sprites->catalog.materials[0].edgeCount = 0;
    sprites->catalog.viewPalettes[0] = 7;
    scene.viewFlags = 1u << 12; // HIDE_BASE: surface coverage filters prior canvas, without an underground grid overlay.
    Run();
    EXPECT_EQ(Pixel(18, 40), 17); // Authored pi10 background through synthetic row7; never initial canvas0.
    EXPECT_EQ(Pixel(17, 40), 77); // Higher-depth opaque UI is neither darkened nor covered by terrain.
    EXPECT_EQ(Pixel(50, 20), 10); // Outside surface coverage, the authored clear remains unchanged.
}
TEST_F(VulkanWorldSurfaceLayerTest, ZoomedRleCliffRetainsOriginalOddOffsetSampling)
{
    // Original RLE drawing first subtracts the zoom mask from the projected
    // origin, then rounds X after adding the G1 offset. At right-edge X30,
    // offset1, zoom1, source column0 lands at framebuffer X23, not X24.
    edgeColumnPattern = true;
    chunk->records[0].waterHeight = 0;
    scene.zoom = 1;
    scene.view = { -8, -32 };
    for (uint32_t i = 0; i < sprites->records.size(); ++i)
    {
        auto variant = sprites->records[i].variants[2];
        variant.zoom = 1;
        variant.valid |= 2;
        if (i >= 19 && i < 56)
            variant.spriteOffset.x = 1;
        sprites->records[i].variants[3] = variant;
    }
    Run();
    EXPECT_EQ(Pixel(23, 31), 42u);
    EXPECT_EQ(Pixel(24, 31), 0u); // Original 32-world-unit column ends here.
}

#endif
