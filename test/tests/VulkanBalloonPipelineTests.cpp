/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include <algorithm>
    #include <array>
    #include <cstdlib>
    #include <cstring>
    #include <filesystem>
    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2-renderer/vulkan/VulkanBalloonPipeline.h>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/SpriteIds.h>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/RenderTarget.h>
    #include <openrct2/drawing/RetainedBalloonScene.h>
    #include <openrct2/interface/Viewport.h>
    #include <openrct2/paint/Paint.h>
    #include <openrct2/world/Location.hpp>

namespace
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;

    struct SyntheticBounds
    {
        std::array<G1Element, 2> saved;
        std::array<uint8_t, 64 * 32> pixels{};
        bool noGraphics = gOpenRCT2NoGraphics;
        bool stableSort = gPaintStableSort;
        SyntheticBounds()
        {
            gOpenRCT2NoGraphics = false;
            gPaintStableSort = false;
            for (uint32_t i = 0; i < 2; ++i)
                saved[i] = *GfxGetG1Element(SPR_TEMP_BEGIN + i);
            G1Element ground{ .offset = pixels.data(), .width = 64, .height = 32, .xOffset = -32, .yOffset = 0 };
            G1Element balloon{ .offset = pixels.data(), .width = 17, .height = 23, .xOffset = -8, .yOffset = -22 };
            GfxSetG1Element(SPR_TEMP_BEGIN, &ground);
            GfxSetG1Element(SPR_TEMP_BEGIN + 1, &balloon);
        }
        ~SyntheticBounds()
        {
            for (uint32_t i = 0; i < 2; ++i)
                GfxSetG1Element(SPR_TEMP_BEGIN + i, &saved[i]);
            gOpenRCT2NoGraphics = noGraphics;
            gPaintStableSort = stableSort;
        }
    };

    Gpu::BalloonSceneCommand Scene()
    {
        auto chunk = std::make_shared<RetainedBalloonChunk>();
        chunk->revision = chunk->gpuRevision = 1;
        for (uint32_t i = 0; i < 24; ++i)
        {
            auto& r = chunk->records[i];
            // Eight partly overlapping records additionally test same-quadrant reverse insertion and equal endpoints.
            r = { 416 + static_cast<int32_t>(i % 4) * 64,
                  416 + static_cast<int32_t>(i / 4) * 64,
                  96,
                  i,
                  1,
                  i < 8 ? i : (i < 13 ? i - 8 : i & 7),
                  i >= 8 && i < 13 ? 1U : 0U,
                  i,
                  13,
                  22,
                  11,
                  1 };
            if (i >= 16)
            {
                r.x = 508 + static_cast<int32_t>((i - 16) % 3) * 3;
                r.y = 508 + static_cast<int32_t>((i - 16) / 3) * 3;
                r.z = 116 - static_cast<int32_t>((i - 16) % 4);
            }
        }
        auto snapshot = std::make_shared<RetainedBalloonSnapshot>();
        snapshot->epoch = snapshot->sequence = 1;
        snapshot->count = 24;
        snapshot->chunks[0] = chunk;
        auto sprites = std::make_shared<Gpu::BalloonSpriteTable>();
        sprites->revision = 1;
        for (uint32_t i = 0; i < Gpu::kBalloonSpriteCount; ++i)
            sprites->records[i] = { { 17, 23 }, { -8, -22 }, i, 0, 0, 1 };
        return { snapshot, sprites, { 0, 0, 640, 480 }, { -320, 160 }, 1024, true };
    }

    // The reference uses the frozen parent creation and actual legacy linked-list arranger, including ground nodes.
    // Only this test's small traversal setup is duplicated; actual-main-UI tests remain the independent scene oracle.
    std::vector<uint32_t> ReferenceColumn(const Gpu::BalloonSceneCommand& scene, uint32_t column)
    {
        const int32_t left = (scene.view.x & ~31) + static_cast<int32_t>(column) * 32;
        const int32_t visibleLeft = std::max(left, scene.view.x);
        const int32_t visibleRight = std::min(left + 32, scene.view.x + scene.clip.z - scene.clip.x);
        RenderTarget rt{ .x = visibleLeft,
                         .y = scene.view.y,
                         .width = visibleRight - visibleLeft,
                         .height = scene.clip.w - scene.clip.y,
                         .cullingX = left,
                         .cullingY = -100000,
                         .cullingWidth = 32,
                         .cullingHeight = 200000 };
        auto session = std::make_unique<PaintSession>();
        PaintSessionInitialise(*session, rt, 0, 0);
        const auto paintGround = [&](const CoordsXY& tile) {
            if (tile.x < 0 || tile.y < 0 || tile.x >= 1024 || tile.y >= 1024)
                return;
            session->SpritePosition = tile;
            PaintAddImageAsParent(*session, ImageId(SPR_TEMP_BEGIN), { 0, 0, 16 }, CoordsXYZ{ 32, 32, -1 });
        };
        const auto paintEntities = [&](const CoordsXY& tile) {
            for (const auto& r : scene.snapshot->chunks[0]->records)
            {
                if (r.present == 0 || (r.x & ~31) != tile.x || (r.y & ~31) != tile.y)
                    continue;
                const auto projected = Translate3DTo2DWithZ(0, { r.x, r.y, r.z });
                if (rt.y + rt.height <= projected.y - static_cast<int32_t>(r.heightMin)
                    || projected.y + static_cast<int32_t>(r.heightMax) <= rt.y
                    || rt.x + rt.width <= projected.x - static_cast<int32_t>(r.width)
                    || projected.x + static_cast<int32_t>(r.width) <= rt.x)
                    continue;
                session->SpritePosition = { r.x, r.y };
                PaintAddImageAsParent(
                    *session, ImageId(SPR_TEMP_BEGIN + 1, static_cast<Colour>(r.id)), { 0, 0, r.z }, CoordsXYZ{ 1, 1, 0 });
            }
        };
        const int32_t startY = (rt.y - 16) & ~31;
        CoordsXY tile{ (startY - left / 2) & ~31, (startY + left / 2) & ~31 };
        for (int32_t row = 0; row < (rt.height + 2128) / 32; ++row)
        {
            paintGround(tile);
            paintEntities(tile);
            paintEntities(tile + CoordsXY{ -32, 32 });
            paintGround(tile + CoordsXY{ 0, 32 });
            paintEntities(tile + CoordsXY{ 0, 32 });
            paintEntities(tile + CoordsXY{ 32, 0 });
            tile += CoordsXY{ 32, 32 };
        }
        PaintSessionArrange(*session);
        std::vector<uint32_t> result;
        for (auto* entry = session->PaintHead; entry != nullptr; entry = entry->NextQuadrantEntry)
            if (entry->image_id.GetIndex() == SPR_TEMP_BEGIN + 1)
                result.push_back(static_cast<uint32_t>(entry->image_id.GetPrimary()));
        return result;
    }

    class VulkanBalloonPipelineTest : public testing::Test
    {
    protected:
        std::shared_ptr<Vulkan::DeviceContext> context;
        std::unique_ptr<Vulkan::SubmissionSlots> slots;
        Vulkan::IndexedResources resources;
        Vulkan::BalloonPipeline pipeline;
        std::filesystem::path shaders;
        void SetUp() override
        {
            const auto* path = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
            shaders = path != nullptr ? std::filesystem::path(path) : std::filesystem::current_path() / "data/shaders/vulkan";
            ASSERT_TRUE(std::filesystem::exists(shaders / "balloon_order.comp.spv"));
            try
            {
                context = Vulkan::DeviceContext::CreateGraphicsOnly();
            }
            catch (const std::exception& e)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required && std::string(required) == "1")
                    FAIL() << e.what();
                GTEST_SKIP() << e.what();
            }
            slots = std::make_unique<Vulkan::SubmissionSlots>(context, 4 * 1024 * 1024, 1);
            resources.Initialise(*context, { 640, 480 }, false, 1, 1);
            pipeline.Initialise(*context, resources, shaders);
            const auto token = *slots->Begin(0, true);
            auto pixels = token.upload->Allocate(64 * 32, 4);
            std::memset(pixels.data, 201, static_cast<size_t>(pixels.size));
            auto descriptors = token.upload->Allocate(sizeof(Gpu::SpriteAssetDescriptor) * 13, 4);
            std::memset(descriptors.data, 0, static_cast<size_t>(descriptors.size));
            auto remap = token.upload->Allocate(256 * 256, 4);
            for (size_t i = 0; i < 256 * 256; ++i)
                remap.data[i] = static_cast<std::byte>(i & 255);
            resources.BeginAtlasUploads(token.commandBuffer);
            resources.RecordAtlasUpload(token.commandBuffer, pixels, 0, { 0, 0, 64, 32 }, 64);
            for (uint32_t i = 0; i < 13; ++i)
            {
                auto descriptor = descriptors;
                descriptor.offset += i * sizeof(Gpu::SpriteAssetDescriptor);
                descriptor.size = sizeof(Gpu::SpriteAssetDescriptor);
                resources.RecordSpriteDescriptorUpload(token.commandBuffer, descriptor, i);
            }
            resources.EndAtlasUploads(token.commandBuffer);
            resources.RecordIndexTableUpload(token.commandBuffer, remap, false);
            slots->Submit(token);
            ASSERT_TRUE(slots->Wait(token, 5000000000ULL));
            resources.CommitFrameLayouts();
        }
        void TearDown() override
        {
            // Tests wait each submitted fence; slot destruction also retires failed assertion paths before resource disposal.
            slots.reset();
            pipeline.Dispose();
            resources.Dispose();
            context.reset();
        }
        Vulkan::SubmissionToken Begin()
        {
            auto token = *slots->Begin(0, true);
            resources.RecordCanvasAndDepthClear(token.commandBuffer, 0, 0);
            pipeline.BeginFrame();
            return token;
        }
        bool Submit(const Vulkan::SubmissionToken& token)
        {
            slots->Submit(token);
            if (!slots->Wait(token, 5000000000ULL))
                return false;
            resources.CommitFrameLayouts();
            return true;
        }
        Vulkan::UploadAllocation Readback(const Vulkan::SubmissionToken& token, VkBuffer buffer, VkDeviceSize bytes)
        {
            auto destination = token.upload->Allocate(bytes, 4);
            if (!destination)
                throw std::runtime_error("Diagnostic balloon readback allocation failed");
            VkBufferMemoryBarrier barrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                           .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                           .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                           .buffer = buffer,
                                           .offset = 0,
                                           .size = bytes };
            vkCmdPipelineBarrier(
                token.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            VkBufferCopy copy{ 0, destination.offset, bytes };
            vkCmdCopyBuffer(token.commandBuffer, buffer, destination.buffer, 1, &copy);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            barrier.buffer = destination.buffer;
            barrier.offset = destination.offset;
            vkCmdPipelineBarrier(
                token.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0,
                nullptr);
            // This diagnostic read is ordered before the next frame's compute overwrite without affecting normal frames.
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.buffer = buffer;
            barrier.offset = 0;
            vkCmdPipelineBarrier(
                token.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                &barrier, 0, nullptr);
            return destination;
        }
    };
} // namespace

TEST(VulkanBalloonAdmissionTest, InvalidAnimationAndUnsupportedRecordsFailClosed)
{
    const auto scene = Scene();
    const auto supported = Gpu::CommandDrawingContext::AreBalloonRecordsSupported;
    EXPECT_TRUE(supported(*scene.snapshot));
    const auto rejected = [&](const auto& mutate) {
        auto snapshot = *scene.snapshot;
        auto chunk = std::make_shared<RetainedBalloonChunk>(*snapshot.chunks[0]);
        snapshot.chunks[0] = chunk;
        mutate(snapshot, *chunk);
        EXPECT_FALSE(supported(snapshot));
    };
    for (uint32_t invalidFrame = 5; invalidFrame <= 7; ++invalidFrame)
        rejected([&](auto&, auto& c) { c.records[8].frame = invalidFrame; });
    rejected([](auto&, auto& c) { c.records[0].present = 0; });
    rejected([](auto&, auto& c) { c.records[24] = c.records[0]; });
    rejected([](auto&, auto& c) { c.records[0].generation = 0; });
    rejected([](auto&, auto& c) { c.records[0].width = 14; });
    rejected([](auto&, auto& c) { c.records[0].colour = 54; });
    rejected([](auto&, auto& c) { c.records[0].z = 79; });
    rejected([](auto&, auto& c) { c.records[0].x = 625; });
    rejected([](auto& s, auto&) { s.count = 25; });
    rejected([](auto& s, auto&) { s.epoch = 0; });
    rejected([](auto& s, auto&) { s.sequence = 0; });
    rejected([](auto&, auto& c) { c.gpuRevision = 0; });
    EXPECT_TRUE(supported(*scene.snapshot)) << "Validation must leave the owned frozen input untouched";
}

TEST_F(VulkanBalloonPipelineTest, UnchangedAndCompatibilityOnlyGenerationsDoNotUploadState)
{
    auto scene = Scene();
    for (uint64_t sequence = 1; sequence <= 3; ++sequence)
    {
        auto snapshot = std::make_shared<RetainedBalloonSnapshot>(*scene.snapshot);
        snapshot->sequence = sequence;
        if (sequence == 3)
        {
            auto chunk = std::make_shared<RetainedBalloonChunk>(*snapshot->chunks[0]);
            ++chunk->revision; // Compatibility state changes, GPU data does not.
            snapshot->chunks[0] = chunk;
        }
        scene.snapshot = snapshot;
        auto token = Begin();
        pipeline.Record(token, scene);
        auto stats = pipeline.GetUploadStats();
        EXPECT_EQ(stats.sourceBytes, sequence == 1 ? 3072u : 0u);
        EXPECT_EQ(stats.spriteBytes, sequence == 1 ? 416u : 0u);
        EXPECT_EQ(stats.sequence, sequence);
        EXPECT_EQ(stats.viewportSubmissions, 1u);
        ASSERT_TRUE(Submit(token));
    }
}

TEST_F(VulkanBalloonPipelineTest, DroppedRevisionAbandonAndEpochResetCannotLoseRequiredUploads)
{
    auto scene = Scene();
    auto run = [&](uint64_t expectedSource, uint64_t expectedSprite, bool abandon) {
        auto token = Begin();
        pipeline.Record(token, scene);
        EXPECT_EQ(pipeline.GetUploadStats().sourceBytes, expectedSource);
        EXPECT_EQ(pipeline.GetUploadStats().spriteBytes, expectedSprite);
        if (abandon)
        {
            slots->Abandon(token);
            resources.DiscardFrameLayouts(0);
            pipeline.DiscardPendingUploads();
        }
        else
            ASSERT_TRUE(Submit(token));
    };
    run(3072, 416, false);
    auto snapshot = std::make_shared<RetainedBalloonSnapshot>(*scene.snapshot);
    auto chunk = std::make_shared<RetainedBalloonChunk>(*snapshot->chunks[0]);
    snapshot->sequence = 7; // Intermediate complete generations were dropped.
    chunk->gpuRevision = chunk->revision = 9;
    chunk->records[0].x += 1;
    snapshot->chunks[0] = chunk;
    scene.snapshot = snapshot;
    run(3072, 0, true);
    run(3072, 416, false);
    snapshot = std::make_shared<RetainedBalloonSnapshot>(*snapshot);
    ++snapshot->epoch;
    snapshot->sequence = 1;
    scene.snapshot = snapshot;
    run(3072, 416, false);
    pipeline.Dispose();
    pipeline.Initialise(*context, resources, shaders);
    run(3072, 416, false);
}

TEST_F(VulkanBalloonPipelineTest, ShaderOrderAndAnimationMatchFrozenParentCreationAndLegacyArrange)
{
    SyntheticBounds bounds;
    auto scene = Scene();
    size_t totalCompared = 0;
    for (const auto view : { Gpu::Int2{ -320, 160 }, Gpu::Int2{ -301, 177 }, Gpu::Int2{ -3, 385 } })
    {
        scene.view = view;
        auto token = Begin();
        pipeline.Record(token, scene);
        const auto columns = Gpu::GetBalloonColumnCount(scene);
        auto indirect = Readback(token, pipeline.GetDiagnosticIndirectBuffer(), columns * sizeof(VkDrawIndirectCommand));
        auto output = Readback(token, pipeline.GetDiagnosticOutputBuffer(), columns * 64 * sizeof(Gpu::WorldSurfaceRecord));
        ASSERT_TRUE(Submit(token));
        token.upload->Invalidate(indirect.offset, indirect.size);
        token.upload->Invalidate(output.offset, output.size);
        std::vector<VkDrawIndirectCommand> draws(columns);
        std::vector<Gpu::WorldSurfaceRecord> records(columns * 64);
        std::memcpy(draws.data(), indirect.data, static_cast<size_t>(indirect.size));
        std::memcpy(records.data(), output.data, static_cast<size_t>(output.size));
        for (uint32_t column = 0; column < columns; ++column)
        {
            SCOPED_TRACE(testing::Message() << "view=" << view.x << ',' << view.y << " column=" << column);
            const auto reference = ReferenceColumn(scene, column);
            ASSERT_EQ(draws[column].instanceCount, reference.size());
            ASSERT_EQ(draws[column].firstInstance, 0u);
            ASSERT_EQ(draws[column].vertexCount, 4u);
            for (size_t i = 0; i < reference.size(); ++i)
            {
                const auto& actual = records[column * 64 + i];
                const auto& expected = scene.snapshot->chunks[0]->records[reference[i]];
                EXPECT_EQ(actual.palettes, expected.colour + 1);
                EXPECT_EQ(actual.asset, (expected.frame & 7) + (expected.popped ? 8 : 0));
                EXPECT_EQ(actual.world.x, expected.x);
                EXPECT_EQ(actual.world.y, expected.y);
                EXPECT_EQ(actual.world.z, expected.z);
                EXPECT_EQ(actual.depth, static_cast<int32_t>(scene.depthBase + i));
                ++totalCompared;
            }
        }
    }
    EXPECT_GT(totalCompared, 24u);
}
#endif
