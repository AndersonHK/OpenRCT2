// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include "RetainedPeepTestHelpers.h"
    #include "VulkanParityTestSupport.h"

    #include <cmath>
    #include <cstdlib>
    #include <cstring>
    #include <map>
    #include <openrct2-renderer/gpu/PeepAssetGeneration.h>
    #include <openrct2-renderer/vulkan/VulkanFrameExecutor.h>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/drawing/Colour.h>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/Image.h>
    #include <openrct2/drawing/PaletteMap.h>
    #include <openrct2/drawing/RenderTarget.h>
    #include <openrct2/interface/Viewport.h>
    #include <openrct2/paint/Paint.h>
    #include <openrct2/world/Location.hpp>

namespace
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    namespace V = OpenRCT2::Ui::Vulkan;
    namespace G = OpenRCT2::Ui::Gpu;
    namespace T = G::Terrain;
    constexpr G::Extent kExtent{ 321, 241 }; // Deliberately clips both column edges.
    constexpr uint32_t kImages = 64, kBodyOffset = 56;
    void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    int Floor(int value, int step)
    {
        return value - (value % step) - (value % step < 0 ? step : 0);
    }

    // Synthetic indexed art only. The oracle calls the original CPU parent allocation,
    // linked arrangement and bitmap raster; it never uses GPU parent/command output.
    // These fixtures do not replace the external original-art/upstream comparison.
    struct Art
    {
        bool oldNoGraphics = gOpenRCT2NoGraphics;
        uint32_t base = kImageIndexUndefined;
        std::array<std::vector<uint8_t>, kImages> pixels;
        Art()
        {
            gOpenRCT2NoGraphics = false;
            std::array<G1Element, kImages> images{};
            for (uint32_t i = 0; i < kImages; i++)
            {
                const bool body = i >= kBodyOffset;
                const int w = body ? 15 : 64, h = body ? 27 : 32;
                pixels[i].resize(w * h);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                    {
                        const bool covered = body ? (x >= 2 && x < 13 && y >= 1 && (x + y + int(i)) % 7 != 0)
                                                  : (std::abs(x - 31) * 16 + std::abs(y - 15) * 32 <= 512);
                        pixels[i][y * w + x] = !covered ? 0 : body ? (y < 14 ? 243 : 202) : uint8_t(72 + (x / 8 + y / 4) % 8);
                    }
                images[i].offset = pixels[i].data();
                images[i].width = w;
                images[i].height = h;
                images[i].xOffset = body ? -7 : -32;
                images[i].yOffset = body ? -24 : 0;
                images[i].flags = { G1Flag::hasTransparency };
            }
            base = GfxObjectAllocateImages(images.data(), kImages);
            Require(base != kImageIndexUndefined, "Synthetic native-scene image allocation failed");
        }
        ~Art()
        {
            GfxObjectFreeImages(base, kImages);
            gOpenRCT2NoGraphics = oldNoGraphics;
        }
    };

    class VulkanNativePeepSceneTest : public testing::Test
    {
    protected:
        std::unique_ptr<Art> art;
        std::shared_ptr<G::TextureCache> cache;
        std::shared_ptr<V::DeviceContext> device;
        V::FrameExecutor executor;
        std::unique_ptr<V::SubmissionSlots> slots;
        std::shared_ptr<G::PeepAssetGeneration> assets;
        T::RetainedTerrainSnapshot terrain;
        RetainedPeepScene scene;
        std::vector<RetainedPeepRecord> raw;
        uint64_t sequence{}, epoch = 1;
        uint32_t sample{};
        std::filesystem::path artifacts;
        bool oldSort = gPaintStableSort;
        void SetUp() override
        {
            const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
            ASSERT_NE(shaders, nullptr);
            ASSERT_TRUE(std::filesystem::is_regular_file(std::filesystem::path(shaders) / "peep_fields.comp.spv"));
            const auto* output = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS");
            ASSERT_NE(output, nullptr);
            artifacts = std::filesystem::path(output) / "native-peep-scene"
                / testing::UnitTest::GetInstance()->current_test_info()->name();
            std::filesystem::create_directories(artifacts);
            gPaintStableSort = false;
            art = std::make_unique<Art>();
            cache = std::make_shared<G::TextureCache>(1);
            device = V::DeviceContext::CreateGraphicsOnly();
            slots = std::make_unique<V::SubmissionSlots>(device, 32 * 1024 * 1024, 2);
            executor.Initialise(device, kExtent, shaders, 2, 1, true);
            std::array<std::byte, 256 * 256> remap{};
            for (uint32_t row = 0; row < 256; row++)
                for (uint32_t x = 0; x < 256; x++)
                    remap[row * 256 + x] = std::byte(x >= 243 && x < 255 ? 100 + (row % 56) * 2 + (x - 243) % 2 : x);
            executor.SetRemapPalette(remap);
            terrain.worldEpoch = 1;
            for (uint32_t c = 0; c < T::kRetainedChunkCount; c++)
            {
                auto chunk = std::make_shared<T::RetainedTileChunk>();
                chunk->revision = 1;
                for (uint32_t local = 0; local < T::kRetainedChunkSize; local++)
                {
                    const auto i = c * T::kRetainedChunkSize + local;
                    const bool border = i % 32 == 0 || i % 32 == 31 || i / 32 == 0 || i / 32 == 31;
                    chunk->records[local] = { 16, 0, 0, 0, 1, border ? 2u : 1u, 0, 0 };
                }
                terrain.chunks[c] = std::move(chunk);
            }
            auto materials = std::make_shared<T::RetainedMaterialTable>();
            materials->revision = 1;
            T::RetainedMaterial surface{}, edge{};
            surface.imageBase = art->base;
            surface.imageCount = 19;
            surface.kind = 1;
            edge.imageBase = art->base + 19;
            edge.imageCount = 37;
            edge.kind = 2;
            materials->records = { surface, edge };
            terrain.materials = std::move(materials);
            cache->BeginFrame();
            auto sprites = std::make_shared<T::DrawSpriteTable>();
            sprites->revision = 1;
            std::vector<uint64_t> residencies;
            std::vector<uint32_t> dependencies;
            for (uint32_t i = 0; i < kImages; i++)
            {
                T::DrawSpriteMetadata metadata;
                ASSERT_TRUE(G::ResolveNativeSpriteMetadata(
                    *cache, art->base + i, metadata, residencies, dependencies, i >= kBodyOffset));
                sprites->records.push_back(metadata);
            }
            sprites->residency = cache->CreateAssetLease(residencies, dependencies);
            auto catalog = std::make_shared<RetainedPeepAnimationCatalog>();
            catalog->epoch = 1;
            catalog->sequence = 1;
            const auto object = BuildRetainedPeepAnimationObject(
                { 0, 1, 1, 0, art->base + kBodyOffset, 8, 0, 0 },
                std::array{ RetainedPeepAnimationSource{ 0, 0, art->base + kBodyOffset } });
            catalog->slots.emplace(0, RetainedPeepAnimationSlot{ 1, object });
            assets = std::make_shared<G::PeepAssetGeneration>();
            assets->revision = 1;
            assets->catalog = catalog;
            assets->sprites = sprites;
            assets->terrainSprites = sprites;
            assets->atlasLease = sprites->residency;
            assets->descriptors = { object->descriptor };
            assets->facts = object->facts;
            cache->AbortFrame(); // First real frame must replay the atlas upload.
            for (uint32_t i = 0; i < 6; i++)
            {
                RetainedPeepRecord p{};
                p.id = 13 + i * 61;
                p.generation = 1;
                p.objectGeneration = 1;
                p.x = p.previousX = 480 + int(i % 3) * 5;
                p.y = p.previousY = 480 + int(i / 3) * 7;
                p.z = p.previousZ = 16;
                p.orientation = i * 4;
                p.action = 0;
                p.width = 12;
                p.heightMin = 28;
                p.heightMax = 5;
                p.colours = (i + 2) | ((i + 9) << 8);
                p.sourceTick = p.previousTick = 10;
                p.flags = kRetainedPeepPresent | (i % 2 ? kRetainedPeepStaff : 0);
                raw.push_back(p);
            }
            Publish(true);
        }
        void TearDown() override
        {
            if (device)
                (void)device->WaitIdle();
            executor.Dispose();
            slots.reset();
            assets.reset();
            cache.reset();
            art.reset();
            device.reset();
            gPaintStableSort = oldSort;
        }
        void Publish(bool reset = false)
        {
            RetainedPeepBatch batch{ .epoch = epoch, .reset = reset };
            for (const auto& record : raw)
                Drawing::Test::AppendFullPeep(batch, record);
            Require(scene.Apply(batch, ++sequence), "Native fixture publication rejected");
        }
        T::DrawCamera Camera(uint32_t rotation, int zoom, float alpha = 1, uint32_t tick = 10)
        {
            const auto center = Translate3DTo2DWithZ(rotation, { 488, 488, 16 });
            T::DrawCamera c{ (center.x >> zoom) - int(kExtent.width / 2) + 3,
                             (center.y >> zoom) - int(kExtent.height / 2) + 1,
                             int(kExtent.width),
                             int(kExtent.height),
                             0,
                             0,
                             rotation,
                             zoom,
                             1,
                             0,
                             1 };
            c.entityInterpolation = alpha;
            c.sourceTick = tick;
            return c;
        }
        std::vector<std::byte> Reference(const T::DrawCamera& c)
        {
            std::vector<std::byte> output(size_t(kExtent.width) * kExtent.height);
            const int interval = 32 >> c.zoom;
            const auto interpolate = [&](int now, int previous) {
                // Volatile forces the same two rounded products as EntityTweener, without FMA.
                volatile float inverse = 1.0f - c.entityInterpolation;
                volatile float post = float(now) * c.entityInterpolation, pre = float(previous) * inverse;
                return int(std::round(post + pre));
            };
            for (int aligned = Floor(c.x, interval); aligned < c.x + c.width; aligned += interval)
            {
                RenderTarget rt{};
                rt.x = std::max(c.x, aligned);
                rt.y = c.y;
                rt.width = std::min(c.x + c.width, aligned + interval) - rt.x;
                rt.height = c.height;
                rt.pitch = c.width - rt.width;
                rt.bits = reinterpret_cast<PaletteIndex*>(output.data()) + rt.x - c.x;
                rt.zoom_level = ZoomLevel{ int8_t(c.zoom) };
                rt.cullingX = aligned;
                rt.cullingWidth = interval;
                rt.cullingY = -1048576;
                rt.cullingHeight = 2097152;
                auto session = std::make_unique<PaintSession>();
                session->rt = rt;
                session->CurrentRotation = c.rotation;
                session->QuadrantBackIndex = UINT32_MAX;
                std::map<const PaintStruct*, uint32_t> peepNodes;
                const auto addTerrain = [&](CoordsXY tile) {
                    if (tile.x < 32 || tile.y < 32 || tile.x >= 31 * 32 || tile.y >= 31 * 32)
                        return;
                    CoordsXY origin = tile;
                    if (c.rotation == 1 || c.rotation == 2)
                        origin.x += 32;
                    if (c.rotation == 2 || c.rotation == 3)
                        origin.y += 32;
                    const auto screen = Translate3DTo2DWithZ(c.rotation, { origin, 0 });
                    if (screen.y + 52 <= rt.WorldY() || screen.y - 48 >= rt.WorldY() + rt.WorldHeight())
                        return;
                    session->SpritePosition = origin;
                    session->MapPosition = tile;
                    PaintAddImageAsParent(*session, ImageId(art->base), { 0, 0, 16 }, { 32, 32, -1 });
                };
                const auto addEntities = [&](CoordsXY tile) {
                    for (const auto& source : raw)
                    {
                        if (!(source.flags & kRetainedPeepPresent) || Floor(source.x, 32) != tile.x
                            || Floor(source.y, 32) != tile.y)
                            continue;
                        auto p = source;
                        if ((p.flags & kRetainedPeepInterpolate) && p.sourceTick == c.sourceTick)
                        {
                            p.x = interpolate(p.x, p.previousX);
                            p.y = interpolate(p.y, p.previousY);
                            p.z = interpolate(p.z, p.previousZ);
                        }
                        const auto screen = Translate3DTo2DWithZ(c.rotation, { p.x, p.y, p.z });
                        if (rt.y + rt.height <= ((screen.y - int(p.heightMin)) >> c.zoom)
                            || ((screen.y + int(p.heightMax)) >> c.zoom) <= rt.y
                            || rt.x + rt.width <= ((screen.x - int(p.width)) >> c.zoom)
                            || ((screen.x + int(p.width)) >> c.zoom) <= rt.x)
                            continue;
                        session->SpritePosition = { p.x, p.y };
                        session->MapPosition = tile;
                        const uint32_t image = art->base + kBodyOffset + (((c.rotation * 8 + p.orientation) & 31) >> 3)
                            + p.frameOffset * 4;
                        const auto node = PaintAddImageAsParent(
                            *session, ImageId(image), { 0, 0, p.z }, { { 0, 0, p.z + 5 }, { 1, 1, 11 } });
                        if (node)
                            peepNodes.emplace(node, p.id);
                    }
                };
                // Original PaintSessionGenerate traversal (four merged entity visits per two terrain tiles).
                const auto direction = uint8_t((3 * c.rotation) & 3);
                const int sx = Floor(rt.WorldX(), 32), sy = Floor(rt.WorldY() - 16, 32);
                CoordsXY tile = CoordsXY{ sy - sx / 2, sy + sx / 2 }.rotate(direction);
                if (direction & 1)
                    tile.y -= 16;
                tile = tile.toTileStart();
                for (int remaining = (rt.WorldHeight() + 2128) >> 5; remaining > 0; remaining--)
                {
                    addTerrain(tile);
                    addEntities(tile);
                    addEntities(tile + CoordsXY{ -32, 32 }.rotate(direction));
                    const auto second = tile + CoordsXY{ 0, 32 }.rotate(direction);
                    addTerrain(second);
                    addEntities(second);
                    addEntities(tile + CoordsXY{ 32, 0 }.rotate(direction));
                    tile += CoordsXY{ 32, 32 }.rotate(direction);
                }
                PaintSessionArrange(*session);
                for (auto* node = session->PaintHead; node; node = node->NextQuadrantEntry)
                {
                    auto screen = node->ScreenPos;
                    std::array<PaletteIndex, 256> palette;
                    for (uint32_t i = 0; i < 256; i++)
                        palette[i] = PaletteIndex(i);
                    if (const auto it = peepNodes.find(node); it != peepNodes.end())
                    {
                        const auto& p = *std::find_if(
                            raw.begin(), raw.end(), [&](const auto& v) { return v.id == it->second; });
                        if (c.zoom > 0)
                        {
                            screen.x = Floor(screen.x, 2);
                            screen.y = Floor(screen.y, 2);
                        }
                        for (uint32_t i = 0; i < 12; i++)
                        {
                            palette[243 + i] = PaletteIndex(100 + ((p.colours & 255) + 1) * 2 + i % 2);
                            if (!(p.flags & kRetainedPeepStaff))
                                palette[202 + i] = PaletteIndex(100 + (((p.colours >> 8) & 255) + 1) * 2 + i % 2);
                        }
                    }
                    GfxDrawSpritePaletteSetSoftware(rt, node->image_id.WithPrimary(Colour::black), screen, PaletteMap(palette));
                }
            }
            return output;
        }
        uint64_t Run(const T::DrawCamera& camera, bool abandon = false, bool expectedFailure = false)
        {
            cache->BeginFrame();
            Require(cache->TryBindAssetLease(assets->atlasLease), "Native test atlas lease invalid");
            G::FrameCommandStream commands;
            commands.terrainScenes.push_back({ terrain, assets->sprites, camera, scene.GetSnapshot(), assets });
            const auto lease = cache->SealFrame(commands);
            Require(!commands.atlasAssetLeases.empty(), "Native packet lost its atlas owner");
            const uint32_t slot = sample % 2;
            RenderUploadTelemetry telemetry;
            auto token = slots->Begin(slot, true, &telemetry);
            Require(token.has_value(), "Native scene slot unavailable");
            const auto result = executor.Record(*token, commands);
            Require(result.canvas != nullptr, "Native complete scene returned no canvas");
            if (abandon)
            {
                slots->Abandon(*token);
                executor.Discard(slot);
                cache->RetireFrame(lease, G::FrameRetirement::Failed);
                cache->DrainFrameRetirements();
                return 0;
            }
            auto readback = token->upload->Allocate(size_t(kExtent.width) * kExtent.height, 4);
            Require(bool(readback), "Native scene readback allocation failed");
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            V::RecordImageBarrier(
                token->commandBuffer, result.canvas->GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            const VkBufferImageCopy copy{ .bufferOffset = readback.offset,
                                          .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                          .imageExtent = { kExtent.width, kExtent.height, 1 } };
            vkCmdCopyImageToBuffer(
                token->commandBuffer, result.canvas->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                &copy);
            V::RecordImageBarrier(
                token->commandBuffer, result.canvas->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
            const VkMemoryBarrier host{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(
                token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
                nullptr);
            slots->Submit(*token);
            executor.Commit();
            Require(slots->Wait(*token, 30'000'000'000), "Native scene GPU timeout");
            if (expectedFailure)
            {
                EXPECT_THROW(executor.CompleteTerrainStatus(slot), std::runtime_error);
            }
            else
                executor.CompleteTerrainStatus(slot);
            token->upload->Invalidate(readback.offset, readback.size);
            const auto expected = expectedFailure ? std::vector<std::byte>(size_t(kExtent.width) * kExtent.height)
                                                  : Reference(camera);
            const auto terrainPixels = std::count_if(expected.begin(), expected.end(), [](std::byte p) {
                const auto v = std::to_integer<uint8_t>(p);
                return v >= 72 && v <= 79;
            });
            const auto peepPixels = std::count_if(
                expected.begin(), expected.end(), [](std::byte p) { return std::to_integer<uint8_t>(p) >= 100; });
            if (!expectedFailure)
            {
                Require(terrainPixels > 100, "CPU oracle must contain populated terrain");
                if (std::any_of(raw.begin(), raw.end(), [](const auto& p) { return (p.flags & kRetainedPeepPresent) != 0; }))
                    Require(peepPixels > 0, "CPU oracle must contain visible peep art");
                else
                    Require(peepPixels == 0, "Empty native epoch must not retain old peep pixels");
            }
            std::array<std::byte, 1024> palette{};
            for (uint32_t i = 0; i < 256; i++)
            {
                palette[i * 4] = palette[i * 4 + 1] = palette[i * 4 + 2] = std::byte(i);
                palette[i * 4 + 3] = std::byte{ 255 };
            }
            const auto uploaded = telemetry.bytes[size_t(UploadCategory::world)][size_t(UploadMetric::bufferTransfer)];
            EXPECT_EQ(
                VulkanParitySupport::CompareAndReport(
                    artifacts, std::to_string(sample++), "indexed", expected, { readback.data, expected.size() }, 1, palette,
                    { { "rotation", camera.rotation },
                      { "zoom", camera.zoom },
                      { "epoch", scene.GetSnapshot()->epoch },
                      { "sequence", scene.GetSnapshot()->sequence },
                      { "alpha", camera.entityInterpolation },
                      { "renderTick", camera.sourceTick },
                      { "referenceTerrainPixels", terrainPixels },
                      { "referencePeepPixels", peepPixels },
                      { "expectedWholeSceneFailure", expectedFailure },
                      { "uploadedWorldBytes", uploaded },
                      { "syntheticArt", true },
                      { "referenceImageLabel", "cpu-original-parent-sort-raster" },
                      { "resultImageLabel", "native-terrain-peeps" } },
                    kExtent),
                0u);
            cache->RetireFrame(lease, G::FrameRetirement::Presented);
            cache->DrainFrameRetirements();
            return uploaded;
        }
    };
} // namespace

TEST_F(VulkanNativePeepSceneTest, PersistentFieldsCommonOrderingAndAbandonedBootstrapMatchCpuRaster)
{
    Run(Camera(0, 0), true); // Abandon FIRST field/catalog/atlas upload before submission.
    EXPECT_GT(Run(Camera(0, 0)), 0u);
    for (int zoom = 0; zoom < 2; zoom++)
        for (uint32_t rotation = 0; rotation < 4; rotation++)
            EXPECT_EQ(Run(Camera(rotation, zoom)), 0u); // Independent cameras reuse persistent fields/atlas.
    raw[0].colours = 17 | (23 << 8);
    Publish();
    EXPECT_EQ(Run(Camera(0, 0)), 24u);
    raw[1].frameOffset = 1;
    Publish();
    EXPECT_EQ(Run(Camera(1, 0)), 44u);
    raw[2].previousX = raw[2].x;
    raw[2].previousY = raw[2].y;
    raw[2].previousZ = raw[2].z;
    raw[2].x += 21;
    raw[2].previousTick = 10;
    raw[2].sourceTick = 14;
    raw[2].flags |= kRetainedPeepInterpolate;
    Publish();
    EXPECT_EQ(Run(Camera(2, 0, 0.5f, 14)), 48u);
    EXPECT_EQ(Run(Camera(2, 1, 0.5f, 14)), 0u);  // A participating peep's tween pose also reaches zoom1.
    EXPECT_EQ(Run(Camera(2, 0, 0.25f, 15)), 0u); // Stale published endpoints must use current pose.
    const auto removed = raw[3];
    raw[3] = {};
    raw[3].id = removed.id;
    raw[3].generation = removed.generation;
    Publish();
    EXPECT_EQ(Run(Camera(3, 0)), 12u);
    raw[3] = removed;
    raw[3].generation++;
    raw[3].colours = 27;
    Publish();
    EXPECT_EQ(Run(Camera(3, 1)), 128u);
    auto replacement = std::make_shared<G::PeepAssetGeneration>(*assets);
    replacement->revision++;
    assets = std::move(replacement);
    EXPECT_GT(Run(Camera(0, 0)), 0u); // Catalog-only update, no motion/bin rebuild.
    raw.clear();
    epoch++;
    Publish(true);
    EXPECT_EQ(Run(Camera(1, 0)), 0u); // Empty new epoch clears old GPU slots/bins.
    EXPECT_EQ(Run(Camera(1, 1)), 0u);
    RecordProperty("nativeSyntheticSceneSamples", sample);
    RecordProperty("externalOriginalArtQualification", false);
}
TEST_F(VulkanNativePeepSceneTest, CapacityOverflowPublishesNoPartialTerrainOrPeeps)
{
    auto camera = Camera(0, 0);
    camera.commandCapacity = 1;
    Run(camera, false, true); // All column indirect counts must be zero, completion must latch the error.
}
#endif
