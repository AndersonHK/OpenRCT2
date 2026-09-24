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
    class VulkanWorldPathLayerTest : public testing::Test
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
            const auto addRange = [&](uint32_t asset, uint32_t count, int32_t height) {
                const auto base = static_cast<uint32_t>(sprites->records.size());
                for (uint32_t i = 0; i < count; i++)
                {
                    G::WorldSurfaceSpriteSet sprite{};
                    sprite.variants[2] = { { 8, height }, { 0, 0 }, asset, 0, 0, 5 };
                    sprite.variants[3] = { { 8, height }, { 0, 0 }, asset, 1, 0, 5 };
                    sprite.effects = i << 16;
                    sprites->records.push_back(sprite);
                }
                return base;
            };
            auto& material = sprites->catalog.paths[0];
            material.surfaceBase = addRange(4, 51, 40);
            material.surfaceCount = 51;
            material.queueBase = addRange(5, 20, 40);
            material.queueCount = 20;
            material.railingsBase = addRange(6, 36, 8);
            material.railingsCount = 36;
            material.bridgeBase = addRange(4, 55, 40);
            material.bridgeCount = 55;
            material.flags = 2; // Original material draws the surface over bridge decks.
            sprites->catalog.paths[255] = material;
            sprites->catalog.additions[0] = { addRange(7, 13, 8), 13, 0, 2 };
            sprites->catalog.spriteEnvelope[3] = { -2, -2, 7, 23 };
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
        void Paths(std::vector<G::WorldPathSourceRecord> paths)
        {
            auto next = std::make_shared<G::WorldSurfaceChunk>(*scene.chunks[0]);
            next->revision++;
            next->paths = std::move(paths);
            next->records[0].pathFirst = 0;
            next->records[0].pathCount = static_cast<uint32_t>(next->paths.size());
            next->records[0].pathMaxZ = 0;
            for (const auto& path : next->paths)
                next->records[0].pathMaxZ = std::max(next->records[0].pathMaxZ, path.clearanceZ);
            scene.chunks[0] = next;
        }
        static G::WorldPathSourceRecord Path(int32_t z, uint32_t flags = 0, uint32_t edges = 0)
        {
            return { .baseZ = z,
                     .clearanceZ = z + 32,
                     .flags = flags,
                     .additionSlot = 65535,
                     .rideId = 65535,
                     .edgesAndCorners = edges };
        }
        size_t ColourCount(uint8_t first, uint8_t last) const
        {
            return std::count_if(pixels.begin(), pixels.end(), [&](auto p) {
                const auto value = std::to_integer<uint8_t>(p);
                return value >= first && value <= last;
            });
        }
        D::RenderUploadTelemetry Run(bool abandon = false, bool overflow = false)
        {
            G::FrameCommandStream commands;
            commands.worldSurfaces = scene;
            if (!uploaded)
                for (uint32_t asset = 0; asset < 8; asset++)
                {
                    std::vector<std::byte> texels(8 * 40);
                    for (uint32_t y = 0; y < 40; y++)
                        for (uint32_t x = 0; x < 8; x++)
                            texels[y * 8 + x] = std::byte(
                                asset == 0       ? 20
                                    : asset == 1 ? 1
                                    : asset == 2 ? (x == 0 ? 99 : 0)
                                    : asset == 3 ? 42
                                    : asset == 4 ? 80
                                    : asset == 5 ? 140
                                    : asset == 6 ? 160
                                                 : 200);
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
                const auto path = std::filesystem::path(directory) / "world-path-layers"
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
TEST_F(VulkanWorldPathLayerTest, PathsMergeWithTerrainAndWaterByHeightAndRetryAbandonedUploads)
{
    // Three ascending layers on one tile: underground, submerged, and above water.
    auto under = Path(16);
    auto submerged = Path(40);
    auto above = Path(56, 2); // Queue uses a distinct raw material range.
    Paths({ under, submerged, above });
    Run(true);
    const auto first = Run();
    EXPECT_EQ(first.worldBufferCopyCalls, 4u);
    EXPECT_EQ(Pixel(18, 40), 140); // Water must not tint the higher queue.
    EXPECT_EQ(Pixel(18, 52), 87);  // Submerged path80 filtered by water row7.
    EXPECT_EQ(Pixel(18, 68), 20);  // Surface hides the lower path.
    ASSERT_GT(ColourCount(140, 140), 0u);
    ASSERT_GT(ColourCount(20, 20), 0u);
    const auto baseline = pixels;
    const auto repeat = Run();
    EXPECT_EQ(repeat.worldBufferCopyCalls, 0u);
    EXPECT_EQ(repeat.bytes[size_t(D::UploadCategory::world)][size_t(D::UploadMetric::bufferTransfer)], 0u);
    EXPECT_EQ(pixels, baseline);
}

TEST_F(VulkanWorldPathLayerTest, RawSlopeQueueAndLegacySelectionWorkForEveryRotation)
{
    chunk->records[0].waterHeight = 0;
    constexpr G::Int2 views[4]{ { -16, -64 }, { -48, -80 }, { -16, -96 }, { 16, -80 } };
    for (uint32_t rotation = 0; rotation < 4; rotation++)
    {
        scene.rotation = rotation;
        scene.view = views[rotation];
        Paths({ Path(32, 1) });
        Run();
        EXPECT_EQ(Pixel(18, 64), 96 + rotation);
        Paths({ Path(32, 1 | 2 | 128) });
        Run();
        EXPECT_EQ(Pixel(18, 64), 156 + rotation);
    }
}

TEST_F(VulkanWorldPathLayerTest, StackGrowthShrinkRemovalAndReuseCannotRevealStalePaths)
{
    chunk->records[0].waterHeight = 0;
    std::vector<G::WorldPathSourceRecord> stack(97, Path(32));
    stack.back() = Path(32, 2);
    Paths(stack); // Deliberately exceeds common small per-tile array limits.
    Run();
    EXPECT_EQ(Pixel(18, 64), 140);
    Paths({ Path(32, 0, 1) });
    auto telemetry = Run();
    EXPECT_EQ(telemetry.worldBufferCopyCalls, 2u);
    EXPECT_EQ(Pixel(18, 64), 81);
    Paths({});
    telemetry = Run();
    EXPECT_EQ(telemetry.worldBufferCopyCalls, 1u);
    EXPECT_EQ(Pixel(18, 64), 20);
    EXPECT_EQ(ColourCount(80, 159), 0u);
    Paths({ Path(32, 0, 2) });
    Run(true); // Retry a changed existing arena range, not only a first bootstrap.
    Run();
    EXPECT_EQ(Pixel(18, 64), 82);
    scene.worldEpoch++;
    Paths({});
    Run();
    EXPECT_EQ(ColourCount(80, 159), 0u);
}

TEST_F(VulkanWorldPathLayerTest, RailingsAndAdditionsAreEmittedAndTallPathsSurviveTerrainCulling)
{
    chunk->records[0].waterHeight = 0;
    auto path = Path(32, 2, 5);
    path.additionSlot = 0;
    Paths({ path });
    Run();
    EXPECT_GT(ColourCount(140, 159), 0u);
    EXPECT_GT(ColourCount(174, 195), 0u);
    EXPECT_GT(ColourCount(201, 212), 0u);
    // The terrain anchor is below the target; only the elevated raw path intersects it.
    scene.view = { -16, -240 };
    Paths({ Path(208) });
    Run();
    EXPECT_EQ(Pixel(18, 64), 80);
    EXPECT_EQ(ColourCount(20, 20), 0u);
}

TEST_F(VulkanWorldPathLayerTest, OutputOverflowRejectsWholeWorldAndLatchesFailure)
{
    Paths({ Path(32), Path(40), Path(48) });
    scene.outputCapacity = 1;
    Run(false, true);
    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](auto p) { return p == std::byte{}; }));
    // Completed GPU failures deliberately poison this executor until it is recreated.
    EXPECT_THROW(executor.CompleteTerrainStatus(0), std::runtime_error);
}

TEST_F(VulkanWorldPathLayerTest, InvalidRangesFailBeforeSubmission)
{
    // A separate fixture preserves the production failure latch while independently testing input validation.
    Paths({ Path(32), Path(40), Path(48) });
    auto invalid = std::make_shared<G::WorldSurfaceChunk>(*scene.chunks[0]);
    invalid->revision++;
    invalid->records[0].pathCount = 4;
    scene.chunks[0] = invalid;
    G::FrameCommandStream commands;
    commands.worldSurfaces = scene;
    auto token = slots->Begin(0, true);
    ASSERT_TRUE(token);
    EXPECT_THROW(static_cast<void>(executor.Record(*token, commands)), std::invalid_argument);
    slots->Abandon(*token);
    executor.Discard(0);
}

TEST_F(VulkanWorldPathLayerTest, GlobalSourceCapacityIsExplicitAndRetryDoesNotKeepRejectedState)
{
    auto over = std::make_shared<G::WorldSurfaceChunk>(*chunk);
    over->revision = 2;
    over->paths.resize(size_t(G::kWorldPathSourceCapacity) + 1, Path(32));
    over->records[0].pathCount = 1; // Isolate arena-capacity rejection from the per-tile dispatch budget.
    scene.chunks[0] = over;
    G::FrameCommandStream commands;
    commands.worldSurfaces = scene;
    auto token = slots->Begin(0, true);
    ASSERT_TRUE(token);
    EXPECT_THROW(static_cast<void>(executor.Record(*token, commands)), std::overflow_error);
    slots->Abandon(*token);
    executor.Discard(0);
    scene.chunks[0] = chunk;
    Paths({ Path(32) });
    Run();
    EXPECT_EQ(Pixel(18, 64), 80);
}
TEST_F(VulkanWorldPathLayerTest, ExcessiveSingleTileWorkRejectsWithoutTruncationAndRetryRecovers)
{
    Paths(std::vector<G::WorldPathSourceRecord>(G::kWorldPathMaximumTileWork + 1, Path(32)));
    G::FrameCommandStream commands;
    commands.worldSurfaces = scene;
    D::RenderUploadTelemetry telemetry;
    auto token = slots->Begin(0, true, &telemetry);
    ASSERT_TRUE(token);
    EXPECT_THROW(static_cast<void>(executor.Record(*token, commands)), std::overflow_error);
    EXPECT_EQ(telemetry.worldBufferCopyCalls, 0u);
    slots->Abandon(*token);
    executor.Discard(0);
    Paths({ Path(32) });
    Run();
    EXPECT_EQ(Pixel(18, 64), 80);
}

TEST_F(VulkanWorldPathLayerTest, GhostPathsAndGhostAdditionsUseIndependentInstanceRemaps)
{
    chunk->records[0].waterHeight = 0;
    sprites->catalog.reserved = 9; // Fixture palette shifts every covered index by nine.
    auto path = Path(32, 1u << 4, 15);
    Paths({ path });
    Run();
    EXPECT_EQ(Pixel(18, 64), 80 + 15 + 9);
    path.flags = 0;
    Paths({ path });
    Run();
    EXPECT_EQ(Pixel(18, 64), 80 + 15);
    path.edgesAndCorners = 0;
    path.additionSlot = 0;
    path.flags = 1u << 5;
    Paths({ path });
    Run();
    EXPECT_GT(ColourCount(210, 213), 0u);
    EXPECT_EQ(ColourCount(201, 204), 0u);
    const auto held = pixels;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_EQ(pixels, held);
}

TEST_F(VulkanWorldPathLayerTest, DamagedLampsBinsAndBenchesChangeArtAndRepairWithoutStaleState)
{
    chunk->records[0].waterHeight = 0;
    for (uint32_t type = 0; type < 3; type++)
    {
        sprites->catalog.additions[0].drawType = type;
        sprites->revision++;
        auto path = Path(32);
        path.additionSlot = 0;
        path.additionStatus = 255;
        Paths({ path });
        Run();
        EXPECT_GT(ColourCount(201, 204), 0u);
        EXPECT_EQ(ColourCount(205, 212), 0u);
        const auto intact = pixels;
        path.flags = 1u << 6;
        path.additionStatus = 0; // Broken bins must override the full-bin art.
        Paths({ path });
        Run();
        EXPECT_GT(ColourCount(205, 208), 0u);
        EXPECT_EQ(ColourCount(201, 204), 0u);
        EXPECT_EQ(ColourCount(209, 212), 0u);
        path.flags = 0;
        path.additionStatus = 255;
        Paths({ path });
        Run();
        EXPECT_EQ(pixels, intact);
        if (type == 1)
        {
            path.additionStatus = 0;
            Paths({ path });
            Run();
            EXPECT_GT(ColourCount(209, 212), 0u);
        }
    }
}
#endif
