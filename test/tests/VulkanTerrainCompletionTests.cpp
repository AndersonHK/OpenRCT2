// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
#include "VulkanParityTestSupport.h"
#include <openrct2-renderer/vulkan/VulkanFrameExecutor.h>
#include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
#include <cstdlib>

namespace
{
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Terrain = Gpu::Terrain;

    Gpu::TerrainSceneCommand Scene(int32_t height, uint64_t revision)
    {
        Gpu::TerrainSceneCommand scene;
        scene.snapshot.worldEpoch = 1;
        for (uint32_t index = 0; index < Terrain::kRetainedChunkCount; index++)
        {
            auto chunk = std::make_shared<Terrain::RetainedTileChunk>();
            chunk->revision = revision;
            for (auto& tile : chunk->records)
                tile = { height, 0, 0, 0, 1, 1, 0, 0 };
            scene.snapshot.chunks[index] = std::move(chunk);
        }
        auto materials = std::make_shared<Terrain::RetainedMaterialTable>();
        materials->revision = 1;
        Terrain::RetainedMaterial surface{};
        surface.imageBase = 100; surface.imageCount = 19; surface.kind = 1;
        Terrain::RetainedMaterial edge{};
        edge.imageBase = 200; edge.imageCount = 37; edge.kind = 2;
        materials->records = { surface, edge };
        scene.snapshot.materials = std::move(materials);
        auto sprites = std::make_shared<Terrain::DrawSpriteTable>();
        sprites->revision = 1;
        Terrain::DrawSpriteMetadata sprite{};
        // Deliberately lacks every emitted image. This is an error-contract test,
        // never a valid image/atlas admission or pixel-parity sample.
        sprite.imageIndex = 999999; sprite.width = sprite.height = 1;
        sprite.variants[0] = { 1, 1, 0, 0, 0, 0, 0, 0 };
        sprite.variants[1] = { 1, 1, 0, 0, 0, 0, 1, 0 };
        sprites->records.push_back(sprite);
        scene.sprites = std::move(sprites);
        scene.camera = { -480, -240, 960, 640, 0, 0, 0, 0, 0, 0, 1 };
        return scene;
    }

    class VulkanTerrainCompletionTest : public testing::Test
    {
    protected:
        std::shared_ptr<Vulkan::DeviceContext> device;
        Vulkan::FrameExecutor executor;
        std::unique_ptr<Vulkan::SubmissionSlots> slots;
        void SetUp() override
        {
            const char* directory = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
            ASSERT_NE(directory, nullptr);
            const std::filesystem::path shaders(directory);
            ASSERT_TRUE(std::filesystem::exists(shaders / "terrain_retained_emit.comp.spv"));
            ASSERT_TRUE(std::filesystem::exists(shaders / "terrain_columns.comp.spv"));
            try { device = Vulkan::DeviceContext::CreateGraphicsOnly(); }
            catch (const std::exception& error)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required && std::string(required) == "1") FAIL() << error.what();
                GTEST_SKIP() << error.what();
            }
            executor.Initialise(device, { 960, 640 }, shaders, 1, 1, true);
            slots = std::make_unique<Vulkan::SubmissionSlots>(device, 16 * 1024 * 1024, 1);
        }
        void TearDown() override
        {
            if (device) (void)device->WaitIdle();
            executor.Dispose();
            slots.reset();
        }
        void Save(const std::string& name, const json_t& report)
        {
            if (const char* directory = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
            {
                const auto folder = std::filesystem::path(directory) / "terrain-completion";
                std::filesystem::create_directories(folder);
                std::ofstream file(folder / (name + ".json"));
                file << report.dump(2) << '\n';
            }
        }
    };
}

TEST_F(VulkanTerrainCompletionTest, GpuFailureSurvivesLaterViewportAndLatchesAtRetirement)
{
    OpenRCT2::Drawing::RenderUploadTelemetry telemetry;
    auto token = slots->Begin(0, true, &telemetry);
    ASSERT_TRUE(token.has_value());
    Gpu::FrameCommandStream commands;
    commands.terrainScenes.push_back(Scene(0, 1)); // GPU raw-height rejection: error 1.
    commands.terrainScenes.push_back(Scene(16, 2)); // Later column-buffer generation, missing sprite: error 3.
    ASSERT_NO_THROW(executor.Record(*token, commands, 7));
    slots->Submit(*token);
    executor.Commit();
    ASSERT_TRUE(slots->Wait(*token, 30'000'000'000));
    std::string message;
    try { executor.CompleteTerrainStatus(0); }
    catch (const std::runtime_error& error) { message = error.what(); }
    EXPECT_NE(message.find("viewport=0"), std::string::npos);
    EXPECT_NE(message.find("error=1"), std::string::npos);
    EXPECT_EQ(telemetry.readbackRequests, 2u);
    EXPECT_EQ(telemetry.readbackBytes, 240u); // Two 30-column, 4-byte status captures.
    EXPECT_THROW(executor.CompleteTerrainStatus(0), std::runtime_error);
    auto next = slots->Begin(0, true);
    ASSERT_TRUE(next.has_value());
    Gpu::FrameCommandStream empty;
    EXPECT_THROW(executor.Record(*next, empty), std::runtime_error);
    slots->Abandon(*next);
    Save("retirement-error", { { "errorContract", true }, { "message", message },
        { "statusReadbackBytes", telemetry.readbackBytes }, { "latched", !message.empty() },
        { "pixelParityClaim", false }, { "windowPresentationTested", false } });
}

TEST_F(VulkanTerrainCompletionTest, AbandonedRecordingDiscardsUnsubmittedStatus)
{
    auto token = slots->Begin(0, true);
    ASSERT_TRUE(token.has_value());
    Gpu::FrameCommandStream commands;
    commands.terrainScenes.push_back(Scene(0, 1));
    executor.Record(*token, commands, 7);
    slots->Abandon(*token);
    executor.Discard(0);
    EXPECT_NO_THROW(executor.CompleteTerrainStatus(0));
    auto next = slots->Begin(0, true);
    ASSERT_TRUE(next.has_value());
    Gpu::FrameCommandStream empty;
    EXPECT_NO_THROW(executor.Record(*next, empty));
    slots->Submit(*next);
    executor.Commit();
    ASSERT_TRUE(slots->Wait(*next, 30'000'000'000));
    EXPECT_NO_THROW(executor.CompleteTerrainStatus(0));
    Save("abandoned-status", { { "errorContract", true }, { "unsubmittedStatusDiscarded", true },
        { "reusedSlotHealthy", true }, { "pixelParityClaim", false } });
}
#endif
