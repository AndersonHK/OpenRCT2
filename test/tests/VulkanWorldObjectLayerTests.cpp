// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include "VulkanParityTestSupport.h"

    #include <cstdlib>
    #include <openrct2-renderer/gpu/GpuWorldFlatRideCatalog.h>
    #include <openrct2-renderer/gpu/GpuWorldPropCatalog.h>
    #include <openrct2-renderer/vulkan/VulkanFrameExecutor.h>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>

namespace
{
    namespace G = OpenRCT2::Ui::Gpu;
    namespace V = OpenRCT2::Ui::Vulkan;
    namespace D = OpenRCT2::Drawing;
    class VulkanWorldObjectLayerTest : public testing::Test
    {
    protected:
        static constexpr G::Extent extent{ 256, 256 };
        static constexpr uint32_t assetCount = 170;
        std::shared_ptr<V::DeviceContext> device;
        V::FrameExecutor executor;
        std::unique_ptr<V::SubmissionSlots> slots;
        std::shared_ptr<G::WorldSurfaceChunk> chunk;
        std::shared_ptr<G::WorldSurfaceSpriteTable> sprites;
        std::unique_ptr<OpenRCT2::WorldObjectPresentationMaterials> materials;
        G::WorldSurfaceSceneCommand scene;
        std::vector<std::byte> pixels;
        bool uploaded{};
        uint32_t sample{};
        static uint8_t Ink(uint32_t image)
        {
            return static_cast<uint8_t>(image == 0 ? 20 : 100 + (image % 80));
        }
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
            chunk->records[0] = { 0, 0, 0, 0, 0, 0, 1, 1 };
            sprites = std::make_shared<G::WorldSurfaceSpriteTable>();
            materials = std::make_unique<OpenRCT2::WorldObjectPresentationMaterials>();
            auto& small = materials->smallScenery[0];
            small.imageBase = small.image = 1;
            small.imageCount = 116;
            small.flags = 1u << 10;
            small.height = 16;
            small.present = true;
            auto& large = materials->largeScenery[0];
            large.imageBase = large.image = 117;
            large.imageCount = 8;
            large.flags = 1;
            large.present = true;
            large.tiles.push_back({ .zClearance = 32, .corners = 15 });
            auto& wall = materials->walls[0];
            wall.imageBase = wall.image = 125;
            wall.imageCount = 37;
            wall.flags = 1;
            wall.height = 4;
            wall.present = true;
            auto& banner = materials->banners[0];
            banner.imageBase = banner.image = 162;
            banner.imageCount = 8;
            banner.flags = 1;
            banner.present = true;
            RebuildCatalog();
            scene = { .worldEpoch = 1,
                      .width = 1,
                      .height = 1,
                      .recordCount = 1,
                      .clip = { 0, 0, 256, 256 },
                      .view = { -128, -128 },
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
        void RebuildCatalog()
        {
            sprites->records.clear();
            G::WorldSurfaceSpriteSet terrain{};
            terrain.variants[2] = { { 8, 8 }, { 0, 0 }, 0, 0, 0, 1 };
            sprites->records.push_back(terrain);
            sprites->catalog.materials[0].surfaceCount = 1;
            sprites->catalog.spriteEnvelope[2] = { -40, -40, 48, 48 };
            sprites->catalog.reserved = 9;
            sprites->propCatalog = G::BuildWorldPropCatalog(*materials, 17, [&](uint32_t image) {
                                       G::WorldSurfaceSpriteSet sprite{};
                                       sprite.variants[2] = { { 8, 8 }, { 0, 0 }, image, 0, 0, 5 };
                                       sprite.variants[3] = { { 8, 8 }, { 0, 0 }, image, 1, 0, 5 };
                                       const auto index = static_cast<uint32_t>(sprites->records.size());
                                       sprites->records.push_back(sprite);
                                       return index;
                                   }).words;
            sprites->revision++;
        }
        static G::WorldObjectSourceRecord Object(uint32_t kind, uint32_t colour = 2)
        {
            return { .baseZ = 16, .clearanceZ = 48, .objectSlot = 0, .kind = kind, .colours = colour };
        }
        void Objects(std::vector<G::WorldObjectSourceRecord> objects)
        {
            auto next = std::make_shared<G::WorldSurfaceChunk>(*scene.chunks[0]);
            next->revision++;
            next->objects = std::move(objects);
            next->records[0].objectFirst = 0;
            next->records[0].objectCount = static_cast<uint32_t>(next->objects.size());
            next->records[0].objectMaxZ = 0;
            for (const auto& object : next->objects)
                next->records[0].objectMaxZ = std::max(next->records[0].objectMaxZ, object.clearanceZ);
            scene.chunks[0] = next;
        }
        size_t ColourCount(uint8_t colour) const
        {
            return static_cast<size_t>(std::count(pixels.begin(), pixels.end(), std::byte(colour)));
        }
        D::RenderUploadTelemetry Run(bool abandon = false, bool overflow = false)
        {
            G::FrameCommandStream commands;
            commands.worldSurfaces = scene;
            if (!uploaded)
                for (uint32_t image = 0; image < assetCount; image++)
                {
                    commands.textureUploads.push_back(
                        { .atlas = 0,
                          .bounds = { int32_t(image * 8), 0, int32_t(image * 8 + 8), 48 },
                          .sourcePitch = 8,
                          .descriptorIndex = image,
                          .descriptor = { .atlasOrigin = { int32_t(image * 8), 0 }, .atlasLayer = 0 },
                          .pixels = std::vector<std::byte>(8 * 48, std::byte(Ink(image))) });
                }
            D::RenderUploadTelemetry telemetry;
            auto token = slots->Begin(0, true, &telemetry);
            if (!token)
                throw std::runtime_error("World object test slot unavailable");
            const auto output = executor.Record(*token, commands);
            if (abandon)
            {
                slots->Abandon(*token);
                executor.Discard(0);
                return telemetry;
            }
            auto readback = token->upload->Allocate(extent.width * extent.height, 4);
            if (!readback)
                throw std::runtime_error("World object readback allocation failed");
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
                throw std::runtime_error("World object test timeout");
            if (overflow)
                EXPECT_THROW(executor.CompleteTerrainStatus(0), std::runtime_error);
            else
                executor.CompleteTerrainStatus(0);
            token->upload->Invalidate(readback.offset, readback.size);
            pixels.assign(readback.data, readback.data + extent.width * extent.height);
            uploaded = true;
            if (const auto* directory = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
            {
                const auto path = std::filesystem::path(directory) / "world-object-layers"
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
TEST_F(VulkanWorldObjectLayerTest, EveryPropFamilySelectsDirectionalArtAndInstanceColours)
{
    for (uint32_t family = 0; family < 4; family++)
        for (uint32_t rotation = 0; rotation < 4; rotation++)
        {
            SCOPED_TRACE(family);
            SCOPED_TRACE(rotation);
            scene.rotation = rotation;
            Objects({ Object(family) });
            Run();
            const uint32_t wallOffsets[4] = { 1, 0, 1, 0 };
            const uint32_t image = family == 0
                ? 1 + rotation
                : (family == 1 ? 121 + rotation : (family == 2 ? 125 + wallOffsets[rotation] : 162 + rotation * 2));
            EXPECT_GT(
                ColourCount(static_cast<uint8_t>(Ink(image) + 3))
                    + (family == 3 ? ColourCount(static_cast<uint8_t>(Ink(image + 1) + 3)) : 0),
                0u);
            if (family != 3)
                EXPECT_GT(ColourCount(20), 0u);
            auto ghost = Object(family);
            ghost.flags = 1;
            Objects({ ghost });
            Run();
            EXPECT_GT(
                ColourCount(static_cast<uint8_t>(Ink(image) + 9))
                    + (family == 3 ? ColourCount(static_cast<uint8_t>(Ink(image + 1) + 9)) : 0),
                0u);
            EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(image) + 3)), 0u);
        }
}
TEST_F(VulkanWorldObjectLayerTest, StationaryAnimationUsesSceneTickWithoutSourceUploads)
{
    auto& small = materials->smallScenery[0];
    small.flags = (1u << 4) | (1u << 15) | (1u << 21) | (1u << 22) | (1u << 10);
    small.frameOffsets = { 1, 3 };
    small.animationMask = 1;
    small.animationDelay = 0;
    RebuildCatalog();
    Objects({ Object(0) });
    scene.sourceTick = 0;
    Run(true);
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1 + 4 + 4) + 3)), 0u);
    const auto first = pixels;
    scene.sourceTick = 1;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1 + 12 + 4) + 3)), 0u);
    EXPECT_NE(pixels, first);
    scene.sourceTick = 0;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_EQ(pixels, first);
}
TEST_F(VulkanWorldObjectLayerTest, GlassUsesFixedBackgroundFilterAndGhostSuppressesGlass)
{
    materials->walls[0].flags = 3;
    RebuildCatalog();
    auto wall = Object(2);
    Objects({ wall });
    Run();
    const auto filtered = static_cast<uint8_t>(Ink(126) + 3 + 17 + 2);
    EXPECT_GT(ColourCount(filtered), 0u);
    wall.flags = 1;
    Objects({ wall });
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(126) + 9)), 0u);
    EXPECT_EQ(ColourCount(filtered), 0u);
}
TEST_F(VulkanWorldObjectLayerTest, RawDoorFramesAndWitheringReplaceArtWithoutCatalogRebuild)
{
    materials->walls[0].flags = 1 | (1u << 4);
    materials->smallScenery[0].flags = (1u << 10) | (1u << 5);
    RebuildCatalog();
    auto wall = Object(2);
    Objects({ wall });
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(128) + 3)), 0u);
    wall.data1 = 2;
    Objects({ wall });
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(148) + 3)), 0u);
    auto small = Object(0);
    small.data0 = 55;
    Objects({ small });
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(9) + 3)), 0u);
    small.data0 = 0;
    Objects({ small });
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1) + 3)), 0u);
    EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(9) + 3)), 0u);
}
TEST_F(VulkanWorldObjectLayerTest, RemovalAndReusedArenaCannotRevealOldPropsAfterAbandon)
{
    Objects({ Object(0) });
    Run();
    const auto before = pixels;
    Objects({ Object(1) });
    Run(true);
    Objects({ Object(2) });
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(126) + 3)), 0u);
    EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(1) + 3)), 0u);
    Objects({});
    Run();
    EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(126) + 3)), 0u);
    Objects({ Object(0) });
    Run();
    EXPECT_EQ(pixels, before);
}
TEST_F(VulkanWorldObjectLayerTest, InvalidObjectRangeRejectsBeforeCopiesAndRetryRecovers)
{
    Objects({ Object(0) });
    auto bad = std::make_shared<G::WorldSurfaceChunk>(*scene.chunks[0]);
    bad->revision++;
    bad->records[0].objectCount = 2;
    scene.chunks[0] = bad;
    G::FrameCommandStream commands;
    commands.worldSurfaces = scene;
    D::RenderUploadTelemetry telemetry;
    auto token = slots->Begin(0, true, &telemetry);
    ASSERT_TRUE(token);
    EXPECT_THROW(static_cast<void>(executor.Record(*token, commands)), std::invalid_argument);
    EXPECT_EQ(telemetry.worldBufferCopyCalls, 0u);
    slots->Abandon(*token);
    executor.Discard(0);
    Objects({ Object(0) });
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1) + 3)), 0u);
}
TEST_F(VulkanWorldObjectLayerTest, EmptyCatalogCannotReuseResidentMetadataAndRetryRecovers)
{
    Objects({ Object(0) });
    Run();
    const auto accepted = pixels;
    auto missing = std::make_shared<G::WorldSurfaceSpriteTable>(*sprites);
    missing->revision++;
    missing->propCatalog.clear();
    scene.sprites = missing;
    G::FrameCommandStream commands;
    commands.worldSurfaces = scene;
    D::RenderUploadTelemetry telemetry;
    auto token = slots->Begin(0, true, &telemetry);
    ASSERT_TRUE(token);
    EXPECT_THROW(static_cast<void>(executor.Record(*token, commands)), std::invalid_argument);
    EXPECT_EQ(telemetry.worldBufferCopyCalls, 0u);
    slots->Abandon(*token);
    executor.Discard(0);
    scene.sprites = sprites;
    Run();
    EXPECT_EQ(pixels, accepted);
}
TEST_F(VulkanWorldObjectLayerTest, ForegroundTallPropWinsOverRearPropForEveryRotation)
{
    scene.width = scene.height = 2;
    scene.recordCount = 4;
    for (auto& record : sprites->records)
        if (record.variants[2].asset != 0)
        {
            record.variants[2].spriteSize = { 8, 48 };
            record.variants[2].spriteOffset = { 0, -40 };
        }
    sprites->revision++;
    const uint32_t rearTiles[4] = { 0, 1, 3, 2 };
    const uint32_t frontTiles[4] = { 3, 2, 0, 1 };
    for (uint32_t rotation = 0; rotation < 4; rotation++)
    {
        SCOPED_TRACE(rotation);
        scene.rotation = rotation;
        auto next = std::make_shared<G::WorldSurfaceChunk>();
        next->revision = 10 + rotation;
        auto rear = Object(0, 2);
        auto front = Object(0, 4);
        next->objects = { rear, front };
        for (uint32_t i = 0; i < 2; i++)
        {
            const auto tile = i == 0 ? rearTiles[rotation] : frontTiles[rotation];
            auto& record = next->records[tile];
            record.objectFirst = i;
            record.objectCount = 1;
            record.objectMaxZ = 48;
        }
        scene.chunks[0] = next;
        Run();
        const auto rectangle = [&](uint32_t tile) {
            int x = int(tile % 2) * 32, y = int(tile / 2) * 32;
            if (rotation == 1)
                x += 32;
            else if (rotation == 2)
            {
                x += 32;
                y += 32;
            }
            else if (rotation == 3)
                y += 32;
            int rx = x, ry = y;
            if (rotation == 1)
            {
                rx = y;
                ry = -x;
            }
            else if (rotation == 2)
            {
                rx = -x;
                ry = -y;
            }
            else if (rotation == 3)
            {
                rx = -y;
                ry = x;
            }
            const int qx[4] = { 7, 7, 23, 23 }, qy[4] = { 7, 23, 23, 7 };
            const int left = ry - rx + qy[rotation] - qx[rotation] + 128;
            const int top = (rx + ry) / 2 + (qx[rotation] + qy[rotation]) / 2 - 16 - 40 + 128;
            return G::Int4{ left, top, left + 8, top + 48 };
        };
        const auto a = rectangle(rearTiles[rotation]), b = rectangle(frontTiles[rotation]);
        const auto left = std::max(a.x, b.x), top = std::max(a.y, b.y), right = std::min(a.z, b.z), bottom = std::min(a.w, b.w);
        ASSERT_GT(right, left);
        ASSERT_GT(bottom, top);
        for (int y = top; y < bottom; y++)
            for (int x = left; x < right; x++)
                EXPECT_EQ(
                    std::to_integer<uint8_t>(pixels[size_t(y) * extent.width + x]),
                    static_cast<uint8_t>(Ink(1 + rotation) + 5));
    }
}
TEST_F(VulkanWorldObjectLayerTest, TrackLookupUsesRawDirectionChainBrakeGhostAndRideColours)
{
    // One style/type, one sequence, chain+brake mask: four variants times four directions.
    // Two source image identities map to the fixture's already resident ordinary sprites.
    constexpr uint32_t recipeRows = 11, recipeParts = 43, recipeWords = 235;
    std::vector<uint32_t> recipes(recipeWords);
    recipes[0] = 0x5452434b;
    recipes[1] = 1;
    recipes[2] = 1;
    recipes[3] = 1;
    recipes[4] = 8;
    recipes[5] = recipeRows;
    recipes[6] = recipeParts;
    recipes[7] = recipeWords;
    recipes[8] = 0;
    recipes[9] = 1;
    recipes[10] = 5;
    for (uint32_t variant = 0; variant < 4; variant++)
        for (uint32_t direction = 0; direction < 4; direction++)
        {
            const uint32_t part = variant * 4 + direction;
            recipes[recipeRows + part * 2] = part;
            recipes[recipeRows + part * 2 + 1] = 1;
            const auto offset = recipeParts + part * 12;
            recipes[offset] = 1000 + ((variant ^ (variant >> 1) ^ (direction & 1)) & 1);
            recipes[offset + 1] = direction * 2;
            recipes[offset + 7] = 8;
            recipes[offset + 8] = 8;
            recipes[offset + 9] = 1;
            recipes[offset + 11] = UINT32_MAX;
        }
    auto& words = sprites->trackCatalog;
    words.assign(12, 0);
    words[0] = 0x5754524b;
    words[1] = 1;
    words[2] = 12;
    words.insert(words.end(), recipes.begin(), recipes.end());
    words[3] = static_cast<uint32_t>(words.size());
    words[4] = 1;
    words.insert(words.end(), { 1, 0, 0, 0, 2, 2, 2, 2 });
    words[5] = static_cast<uint32_t>(words.size());
    words[6] = 1;
    words.insert(words.end(), { 0, 0, 0, 0 });
    words[7] = static_cast<uint32_t>(words.size());
    words[8] = 2;
    words.insert(words.end(), { 0, 1 }); // Type one has no authored recipe.
    words[9] = static_cast<uint32_t>(words.size());
    words[10] = 2;
    words.insert(words.end(), { 1000, 1, 1001, 2 });
    words[11] = static_cast<uint32_t>(words.size());
    sprites->revision++;
    for (uint32_t variant = 0; variant < 4; variant++)
        for (uint32_t direction = 0; direction < 4; direction++)
        {
            SCOPED_TRACE(variant);
            SCOPED_TRACE(direction);
            auto track = Object(4, 99);
            track.direction = direction;
            track.flags = ((variant & 1) << 6) | (((variant >> 1) & 1) << 9);
            Objects({ track });
            Run();
            const auto image = 1 + ((variant ^ (variant >> 1) ^ (direction & 1)) & 1);
            const auto expected = static_cast<uint8_t>(Ink(image) + 3);
            EXPECT_GT(ColourCount(expected), 0u);
            EXPECT_EQ(
                std::to_integer<uint8_t>(pixels[(112 + direction + 3) * extent.width + 128 - direction * 2 + 3]), expected);
            track.flags |= 1;
            Objects({ track });
            Run();
            EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(image) + 9)), 0u);
            EXPECT_EQ(ColourCount(expected), 0u);
        }
    Objects({ Object(4) });
    Run();
    words[words[3] + 4] = 4;
    sprites->revision++;
    // Only sprite/catalog buffers change; source+object arenas would add two copy calls.
    EXPECT_EQ(Run().worldBufferCopyCalls, 6u); // Includes two cleared absent building-catalog headers.
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1) + 5)), 0u);
    auto unsupported = Object(4);
    unsupported.trackTypeAndRideType = 1;
    Objects({ unsupported });
    Run();
    EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(1) + 5)), 0u);
    EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(2) + 5)), 0u);
    EXPECT_GT(ColourCount(20), 0u);
}
TEST(WorldPropCatalogTest, OwnedVariableMetadataAndMalformedImageRange)
{
    auto materials = std::make_unique<OpenRCT2::WorldObjectPresentationMaterials>();
    auto& small = materials->smallScenery[0];
    small.present = true;
    small.imageBase = small.image = 100;
    small.imageCount = 1028;
    small.flags = (1u << 15) | (1u << 21);
    small.frameOffsets.resize(300, 255);
    uint32_t next = 0;
    const auto catalog = G::BuildWorldPropCatalog(*materials, 17, [&](uint32_t) { return next++; });
    const auto entry = catalog.words[0];
    EXPECT_EQ(catalog.words[entry + 8], 300u);
    EXPECT_EQ(catalog.words[entry + 1], 1028u);
    small.frameOffsets[0] = 0;
    EXPECT_EQ(catalog.words[catalog.words[entry + 7]], 255u);
    small.image = 99;
    EXPECT_THROW(
        static_cast<void>(G::BuildWorldPropCatalog(*materials, 17, [&](uint32_t) { return next++; })), std::runtime_error);
}
TEST(WorldPropCatalogTest, UsageResolvesOnlyReferencedMaterialSlots)
{
    auto materials = std::make_unique<OpenRCT2::WorldObjectPresentationMaterials>();
    const auto prepare = [](auto& entries, uint32_t family, uint32_t count) {
        for (uint32_t slot = 0; slot < 2; slot++)
        {
            auto& material = entries[slot];
            material.present = true;
            material.imageBase = material.image = 1000 + family * 1000 + slot * 100;
            material.imageCount = count;
        }
    };
    prepare(materials->smallScenery, 0, 4);
    prepare(materials->largeScenery, 1, 4);
    prepare(materials->walls, 2, 37);
    prepare(materials->banners, 3, 8);
    OpenRCT2::WorldObjectPresentationUsage usage;
    std::vector<uint32_t> appended;
    const auto append = [&](uint32_t image) {
        appended.push_back(image);
        return static_cast<uint32_t>(appended.size() - 1);
    };
    const auto empty = G::BuildWorldPropCatalog(*materials, 17, append, &usage);
    EXPECT_TRUE(appended.empty());
    for (uint32_t family = 0; family < 4; family++)
    {
        EXPECT_EQ(empty.words[empty.words[family] + 1], 0u);
        EXPECT_EQ(empty.words[empty.words[family] + G::kWorldPropMaterialWords + 1], 0u);
        usage.slots[family].set(1);
    }
    const auto selected = G::BuildWorldPropCatalog(*materials, 17, append, &usage);
    std::vector<uint32_t> expected;
    constexpr uint32_t counts[] = { 4, 4, 37, 8 };
    uint32_t first = 0;
    for (uint32_t family = 0; family < 4; family++)
    {
        const auto unused = selected.words[family];
        const auto used = unused + G::kWorldPropMaterialWords;
        EXPECT_EQ(selected.words[unused + 1], 0u);
        EXPECT_EQ(selected.words[used], first);
        EXPECT_EQ(selected.words[used + 1], counts[family]);
        for (uint32_t offset = 0; offset < counts[family]; offset++)
            expected.push_back(1100 + family * 1000 + offset);
        first += counts[family];
    }
    EXPECT_EQ(appended, expected);
}
TEST_F(VulkanWorldObjectLayerTest, StaticRideBodyUsesRawDirectionGhostAndResidentCatalog)
{
    materials->rideObjects[0] = { 1, 4, 1, true };
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    auto& ride = rides.rides[0];
    ride.present = true;
    ride.objectSlot = 0;
    ride.regularStyle = static_cast<uint16_t>(TrackStyle::shop);
    ride.trackColours[0].main = 2;
    auto rebuild = [&]() {
        sprites->flatRideCatalog = G::BuildWorldFlatRideCatalog(*materials, rides, nullptr, 17, [&](uint32_t image) {
                                       EXPECT_GE(image, 1u);
                                       EXPECT_LE(image, 4u);
                                       return image; // Existing fixture sprites 1..4 own these exact source image identities.
                                   }).words;
        sprites->revision++;
    };
    rebuild();
    auto body = Object(4);
    body.trackTypeAndRideType = static_cast<uint32_t>(OpenRCT2::TrackElemType::flatTrack1x1A);
    for (uint32_t rotation = 0; rotation < 4; rotation++)
    {
        SCOPED_TRACE(rotation);
        scene.rotation = rotation;
        body.flags = 0;
        Objects({ body });
        Run();
        EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1 + rotation) + 3)), 0u);
        const auto held = pixels;
        scene.sourceTick++;
        EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
        EXPECT_EQ(pixels, held); // A new shared clock tick does not rebuild static ride state/art.
        body.flags = 1;
        Objects({ body });
        Run();
        EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1 + rotation) + 9)), 0u);
        EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(1 + rotation) + 3)), 0u);
    }
    scene.rotation = 0;
    body.flags = 0;
    Objects({ body });
    Run();
    const auto heldChunk = scene.chunks[0];
    ride.trackColours[0].main = 4;
    rebuild();
    Run();
    EXPECT_EQ(scene.chunks[0], heldChunk);
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1) + 5)), 0u);
    const auto recoloured = pixels;
    Objects({});
    Run();
    EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(1) + 5)), 0u);
    Objects({ body });
    Run(true);
    Run();
    EXPECT_EQ(pixels, recoloured);
}
#endif
