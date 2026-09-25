// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include "VulkanParityTestSupport.h"

    #include <cstdlib>
    #include <cstring>
    #include <openrct2-renderer/gpu/GpuSelectedVehiclePaint.h>
    #include <openrct2-renderer/gpu/GpuWorldBannerText.h>
    #include <openrct2-renderer/gpu/GpuWorldFlatRideCatalog.h>
    #include <openrct2-renderer/gpu/GpuWorldPropCatalog.h>
    #include <openrct2-renderer/gpu/GpuWorldTrackCatalog.h>
    #include <openrct2-renderer/gpu/GpuWorldVehicleCatalog.h>
    #include <openrct2-renderer/vulkan/VulkanFrameExecutor.h>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
    #include <openrct2/drawing/MoneyPresentation.h>
    #include <openrct2/drawing/RetainedBalloonScene.h>
    #include <openrct2/drawing/WorldEffectSnapshot.h>
    #include <openrct2/paint/Paint.h>

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
        bool rowPattern{};
        uint32_t sample{};
        static uint8_t Ink(uint32_t image)
        {
            return static_cast<uint8_t>(image == 0 ? 20 : 100 + (image % 80));
        }
        virtual uint32_t SlotCount() const
        {
            return 1;
        }
        void SetUp() override
        {
            const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
            ASSERT_NE(shaders, nullptr);
            device = V::DeviceContext::CreateGraphicsOnly();
            slots = std::make_unique<V::SubmissionSlots>(device, 8 * 1024 * 1024, SlotCount());
            executor.Initialise(device, extent, shaders, SlotCount(), 1, true);
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
        void PrepareEntityArt()
        {
            // The prop catalog can repeat art across distinct recipe variants.
            // Entity tests use a deliberately contiguous image bank instead.
            sprites->records.clear();
            sprites->propCatalog.clear();
            for (uint32_t image = 0; image < assetCount; ++image)
            {
                G::WorldSurfaceSpriteSet sprite{};
                sprite.variants[2] = { { 8, 8 }, { 0, 0 }, image, 0, 0, 5 };
                sprite.variants[3] = { { 8, 8 }, { 0, 0 }, image, 1, 0, 5 };
                sprites->records.push_back(sprite);
            }
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
            if (rowPattern)
                for (auto& upload : commands.textureUploads)
                    for (size_t i = 0; i < upload.pixels.size(); ++i)
                        upload.pixels[i] = std::byte(std::to_integer<uint8_t>(upload.pixels[i]) + (i / 8) % 8);
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
TEST_F(VulkanWorldObjectLayerTest, HeldEffectSnapshotRebindsAfterSpriteCatalogReplacement)
{
    PrepareEntityArt();
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks[0] = empty;
    auto effects = std::make_shared<D::WorldEffectSnapshot>();
    effects->sourceTick = scene.sourceTick;
    effects->records.push_back({ .x = 8, .y = 8, .z = 8, .type = 4 });
    scene.effects = effects;
    // The production steam selector chooses G1 22637, offset 60 in the
    // resident effects bank. Distinct atlas ink exposes a stale bank header.
    sprites->effectSpriteBase = 1;
    sprites->revision++;
    Run();
    EXPECT_EQ(ColourCount(Ink(61)), 64u);
    EXPECT_EQ(ColourCount(Ink(62)), 0u);
    const auto original = pixels;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_EQ(pixels, original);

    auto sameBank = std::make_shared<G::WorldSurfaceSpriteTable>(*sprites);
    sameBank->revision++;
    scene.sprites = sameBank;
    const auto catalogOnly = Run();
    EXPECT_EQ(pixels, original);

    auto relocated = std::make_shared<G::WorldSurfaceSpriteTable>(*sameBank);
    relocated->revision++;
    relocated->effectSpriteBase = 2;
    scene.sprites = relocated;
    const auto rebound = Run();
    EXPECT_EQ(scene.effects, effects);
    EXPECT_EQ(ColourCount(Ink(61)), 0u);
    EXPECT_EQ(ColourCount(Ink(62)), 64u);
    EXPECT_EQ(rebound.worldBufferCopyCalls, catalogOnly.worldBufferCopyCalls + 1);
    constexpr auto world = static_cast<size_t>(D::UploadCategory::world);
    constexpr auto transfer = static_cast<size_t>(D::UploadMetric::bufferTransfer);
    EXPECT_EQ(rebound.bytes[world][transfer], catalogOnly.bytes[world][transfer] + 16);
    const auto relocatedPixels = pixels;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_EQ(pixels, relocatedPixels);
}

TEST_F(VulkanWorldObjectLayerTest, CommonParentOrderKeepsChildrenAtomicAcrossRotationsAndZooms)
{
    struct LegacyScope
    {
        bool previous = gPaintStableSort;
        LegacyScope()
        {
            gPaintStableSort = false;
        }
        ~LegacyScope()
        {
            gPaintStableSort = previous;
        }
    } legacy;
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    for (uint32_t rotation = 0; rotation < 4; rotation++)
        for (int zoom = -2; zoom <= 2; zoom++)
            for (bool promoteChild : { false, true })
            {
                SCOPED_TRACE(::testing::Message() << rotation << '/' << zoom << '/' << promoteChild);
                scene.rotation = rotation;
                scene.zoom = zoom;
                auto source = std::make_shared<D::SelectedVehicleSnapshot>();
                source->worldEpoch = scene.worldEpoch;
                source->entityEpoch = 1;
                source->sourceTick = scene.sourceTick;
                auto packet = std::make_shared<G::SelectedVehiclePaintPacket>();
                packet->source = source;
                packet->words.resize(16 + 12 + 4 * 12 + 4 * 16);
                auto& w = packet->words;
                w[0] = G::kSelectedVehiclePaintMagic;
                w[1] = G::kSelectedVehiclePaintVersion;
                w[2] = 1;
                w[3] = 4;
                w[4] = 16;
                w[5] = 28;
                w[6] = 76;
                w[7] = uint32_t(w.size());
                w[8] = scene.sourceTick;
                w[9] = uint32_t(scene.worldEpoch);
                w[10] = uint32_t(scene.worldEpoch >> 32);
                w[11] = 1;
                const G::SelectedVehicleCarRecord car{ 7, 1, 0, 4, 0, 0, 0, 0, { -64, -64, 64, 64 } };
                std::memcpy(w.data() + 16, &car, sizeof(car));
                std::array<G::SelectedVehicleParentRecord, 4> metadata{};
                std::array<PaintStruct, 4> paint{};
                auto session = std::make_unique<PaintSessionCore>();
                session->CurrentRotation = uint8_t(rotation);
                session->QuadrantBackIndex = UINT32_MAX;
                const std::array<int32_t, 4> heights{ 0, 3, 16, 8 };
                for (uint32_t i = 0; i < 4; i++)
                {
                    // Independent CPU painter bounds and quadrant construction. All
                    // sprites overlap exactly; their final ink exposes parent ordering.
                    const auto begin = CoordsXY{ int32_t(i * 2), int32_t(i * 2) }.rotate((rotation * 3) & 3);
                    auto size = CoordsXY{ 24, 24 };
                    if (rotation == 0 || rotation == 1)
                        --size.x;
                    if (rotation == 0 || rotation == 3)
                        --size.y;
                    size = size.rotate((rotation * 3) & 3);
                    auto& p = paint[i];
                    p.Bounds = { begin.x, begin.y, heights[i], begin.x + size.x, begin.y + size.y, heights[i] + 15 };
                    metadata[i] = { p.Bounds.x,
                                    p.Bounds.y,
                                    p.Bounds.z,
                                    p.Bounds.x_end,
                                    p.Bounds.y_end,
                                    p.Bounds.z_end,
                                    i == 1 ? 0u : i,
                                    0,
                                    i == 1 ? 257u : 256u,
                                    0,
                                    promoteChild && i == 0 ? 0u : (8u | (8u << 16)),
                                    0 };
                    G::WorldSurfaceRecord component{};
                    // Undo the sprite-facing corner; every original projection is (0,0).
                    component.world = { rotation == 1 || rotation == 2 ? -32 : 0, rotation == 2 || rotation == 3 ? -32 : 0, 0 };
                    component.valid = 17;
                    component.spriteSize = { 8, 8 };
                    component.asset = i + 1;
                    component.zoom = zoom;
                    std::memcpy(w.data() + w[6] + i * 16, &component, sizeof(component));
                    if (i == 0 && promoteChild)
                        continue;
                    if (i == 1 && !promoteChild)
                        continue;
                    int hash = begin.x + begin.y;
                    if (rotation == 1)
                        hash = begin.y - begin.x + MaxPaintQuadrants * 16;
                    else if (rotation == 2)
                        hash = -begin.x - begin.y + MaxPaintQuadrants * 32;
                    else if (rotation == 3)
                        hash = begin.x - begin.y + MaxPaintQuadrants * 16;
                    const auto q = uint32_t(std::clamp(hash / 32, 0, MaxPaintQuadrants - 1));
                    p.QuadrantIndex = uint16_t(q);
                    p.NextQuadrantEntry = session->Quadrants[q];
                    session->Quadrants[q] = &p;
                    session->QuadrantBackIndex = std::min(session->QuadrantBackIndex, q);
                    session->QuadrantFrontIndex = std::max(session->QuadrantFrontIndex, q);
                }
                std::memcpy(w.data() + w[5], metadata.data(), sizeof(metadata));
                PaintSessionArrange(*session);
                auto* last = session->PaintHead;
                ASSERT_NE(last, nullptr);
                size_t visited = 1;
                while (last->NextQuadrantEntry != nullptr && visited <= paint.size())
                {
                    last = last->NextQuadrantEntry;
                    visited++;
                }
                ASSERT_EQ(visited, 3u);
                const auto index = uint32_t(last - paint.data());
                const auto expected = Ink(index == 0 ? 2 : index + 1);
                scene.selectedVehicle = packet;
                Run();
                EXPECT_EQ(pixels[128 * extent.width + 128], std::byte(expected));
            }
    // Removing the auxiliary owner must not replay its previously uploaded words.
    scene.selectedVehicle.reset();
    Run();
    EXPECT_EQ(ColourCount(0), pixels.size());
}

TEST_F(VulkanWorldObjectLayerTest, SelectedVehicleUsesCommonTerrainOrderInNormalAndUndergroundViews)
{
    auto source = std::make_shared<D::SelectedVehicleSnapshot>();
    source->worldEpoch = scene.worldEpoch;
    source->entityEpoch = 1;
    source->sourceTick = scene.sourceTick;
    // The fixture's surface and underground grid use the same opaque 8x8 art.
    // Both must cover a car below their bounds and be covered by a car above.
    for (uint32_t flags : { 0u, 1u })
        for (int32_t height : { -16, 8 })
        {
            SCOPED_TRACE(::testing::Message() << flags << '/' << height);
            scene.viewFlags = flags;
            auto packet = std::make_shared<G::SelectedVehiclePaintPacket>();
            packet->source = source;
            auto& w = packet->words;
            w.resize(16 + 12 + 12 + 16);
            w[0] = G::kSelectedVehiclePaintMagic;
            w[1] = G::kSelectedVehiclePaintVersion;
            w[2] = 1;
            w[3] = 1;
            w[4] = 16;
            w[5] = 28;
            w[6] = 40;
            w[7] = uint32_t(w.size());
            w[8] = scene.sourceTick;
            w[9] = uint32_t(scene.worldEpoch);
            w[10] = uint32_t(scene.worldEpoch >> 32);
            w[11] = 1;
            const G::SelectedVehicleCarRecord car{ 7, 1, 0, 1, 0, 0, 0, 0, { -64, -64, 64, 64 } };
            const G::SelectedVehicleParentRecord parent{ 0, 0, height, 31, 31, height + 15, 0, 0, 256, 0, 8u | (8u << 16), 0 };
            G::WorldSurfaceRecord component{};
            component.valid = 17;
            component.spriteSize = { 8, 8 };
            component.asset = 1;
            std::memcpy(w.data() + 16, &car, sizeof(car));
            std::memcpy(w.data() + 28, &parent, sizeof(parent));
            std::memcpy(w.data() + 40, &component, sizeof(component));
            scene.selectedVehicle = packet;
            Run();
            EXPECT_EQ(pixels[128 * extent.width + 128], std::byte(height < 0 ? Ink(0) : Ink(1)));
        }
}

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
TEST_F(VulkanWorldObjectLayerTest, ScrollingBannerUsesImmutableColumnsTicksAndHeldGeneration)
{
    auto text = std::make_shared<D::ScrollingText::TextColumns>();
    text->phaseWidth = 2;
    text->repeat = false;
    text->columns.resize(128);
    for (size_t column = 0; column < text->columns.size(); column++)
        for (size_t row = 0; row < 8; row++)
            text->columns[column][row] = static_cast<uint8_t>(210 + (column & 1) + row * 2);
    auto source = std::make_shared<OpenRCT2::WorldBannerPresentation>();
    source->revision = 1;
    source->banners = { text };
    const auto held = G::BuildWorldBannerTextData(source);
    const auto heldWords = held->words;
    scene.bannerTexts = held;
    auto banner = Object(3);
    banner.direction = 3; // Ordinary banner direction3 exposes scrolling mode0.
    banner.reserved = 0;  // BannerId0 is packed in the upper sixteen bits.
    Objects({ banner });

    const auto checkText = [&](const D::ScrollingText::TextColumns& expected, uint32_t phase) {
        size_t checked = 0;
        const auto& mode = D::ScrollingText::getModeColumns()[0];
        for (size_t x = 0; x < mode.size(); x++)
        {
            const auto column = mode[x];
            if (column.sourceColumn == UINT16_MAX)
                continue;
            const size_t sourceColumn = column.sourceColumn + phase;
            ASSERT_LT(sourceColumn, expected.columns.size());
            for (size_t row = 0; row < 8 && column.y + row < 40; row++)
            {
                // Tile0, rotation0, view(-128,-128), elementZ16: original
                // text rasterZ22 and G1 offset(-32,0) give top-left(96,106).
                const size_t pixel = (106 + column.y + row) * extent.width + 96 + x;
                ASSERT_LT(pixel, pixels.size());
                EXPECT_EQ(std::to_integer<uint8_t>(pixels[pixel]), expected.columns[sourceColumn][row])
                    << "column=" << x << " row=" << row << " phase=" << phase;
                checked++;
            }
        }
        EXPECT_GT(checked, 0u);
    };
    scene.sourceTick = 0;
    Run();
    checkText(*text, 0);
    const auto first = pixels;
    scene.sourceTick = 1;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_EQ(pixels, first); // Integer tick/2, not render-frame advancement.
    scene.sourceTick = 2;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_EQ(scene.bannerTexts, held);
    checkText(*text, 1);
    EXPECT_NE(pixels, first);

    auto replacementText = std::make_shared<D::ScrollingText::TextColumns>(*text);
    for (auto& column : replacementText->columns)
        for (auto& pixel : column)
            pixel = static_cast<uint8_t>(pixel + 20);
    auto replacement = std::make_shared<OpenRCT2::WorldBannerPresentation>();
    replacement->revision = 2;
    replacement->banners = { replacementText };
    scene.bannerTexts = G::BuildWorldBannerTextData(replacement);
    scene.sourceTick = 0;
    Run();
    checkText(*replacementText, 0);
    EXPECT_NE(pixels, first);
    EXPECT_EQ(held->words, heldWords);
    EXPECT_EQ(held->source, source);
    scene.bannerTexts = held;
    Run();
    EXPECT_EQ(pixels, first);

    banner.flags = 1; // Original ghost banners keep posts but suppress text.
    Objects({ banner });
    Run();
    for (uint8_t colour = 210; colour <= 245; colour++)
        EXPECT_EQ(ColourCount(colour), 0u);
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(169) + 9)), 0u);
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
TEST_F(VulkanWorldObjectLayerTest, CameraPanPreservesPhysicalOcclusionWithoutObjectUploads)
{
    auto lower = Object(0, 2);
    auto higher = Object(1, 4);
    higher.baseZ = 24;
    Objects({ lower, higher });
    for (uint32_t rotation = 0; rotation < 4; ++rotation)
    {
        SCOPED_TRACE(rotation);
        scene.rotation = rotation;
        scene.view = { -128, -128 };
        Run();
        const auto original = pixels;
        scene.view.x += 7;
        scene.view.y += 11;
        const auto telemetry = Run();
        EXPECT_EQ(telemetry.worldBufferCopyCalls, 0u);
        size_t mismatches = 0;
        for (uint32_t y = 0; y + 11 < extent.height; ++y)
            for (uint32_t x = 0; x + 7 < extent.width; ++x)
                mismatches += pixels[y * extent.width + x] != original[(y + 11) * extent.width + x + 7];
        EXPECT_EQ(mismatches, 0u);
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
    // A rail resting on terrain is a coplanar surface overlay, regardless of
    // which primitive reaches rasterization first.
    auto coplanar = std::make_shared<G::WorldSurfaceChunk>(*scene.chunks[0]);
    coplanar->revision++;
    coplanar->records[0].baseZ = 16;
    scene.chunks[0] = coplanar;
    Run();
    EXPECT_EQ(pixels[115 * extent.width + 131], std::byte(Ink(1) + 3));
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

    // Fixed authored geometry with two resident frames: only snapshot time changes.
    // The GPU must decode the image tag before the ordinary sorted image lookup.
    Objects({ Object(4) });
    words[words[2] + recipeParts] = 0x80000000u | (1u << 19) | (1u << 22) | 1000u;
    sprites->revision++;
    scene.sourceTick = 0;
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1) + 5)), 0u);
    for (const auto tick : { 1u, 2u, 3u, 4u, 0xffffffffu, 0u })
    {
        scene.sourceTick = tick;
        EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
        const uint32_t frame = (tick / 2) % 2;
        EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1 + frame) + 5)), 0u);
        EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(2 - frame) + 5)), 0u);
    }
    const auto heldAnimated = scene.sprites;
    auto replacement = std::make_shared<G::WorldSurfaceSpriteTable>(*sprites);
    replacement->revision++;
    replacement->trackCatalog[replacement->trackCatalog[2] + recipeParts] = 1001;
    scene.sprites = replacement;
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(2) + 5)), 0u);
    scene.sprites = heldAnimated;
    scene.sourceTick = 0;
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(1) + 5)), 0u);
    auto animatedGhost = Object(4);
    animatedGhost.flags = 1;
    Objects({ animatedGhost });
    scene.sourceTick = 2;
    Run();
    EXPECT_GT(ColourCount(static_cast<uint8_t>(Ink(2) + 9)), 0u);
    EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(2) + 5)), 0u);
}
TEST_F(VulkanWorldObjectLayerTest, GroundMineTransitionStaysAboveTerrainAndBelowItsRail)
{
    PrepareEntityArt();
    // Actual Heartline source recipe and mine-support cursor, with synthetic
    // overlapping masks: half the support is uncovered and half is under rail.
    // The old scalar rail-1 hides all uncovered support pixels behind terrain.
    sprites->records[1].variants[2].spriteSize.x = 4;
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    rides.rides[0].present = true;
    rides.rides[0].rideType = OpenRCT2::RIDE_TYPE_HEARTLINE_TWISTER_COASTER;
    sprites->trackCatalog = G::BuildWorldTrackCatalog(rides, [](uint32_t image) {
                                for (int i = 0; i < G::MetalSupportRules::worldWoodenAssetCount(); ++i)
                                    if (image == static_cast<uint32_t>(G::MetalSupportRules::worldWoodenAssetImage(i)))
                                        return 2u;
                                return 1u;
                            }).words;
    sprites->revision++;
    chunk->records[0].baseZ = 336;
    // Production snapshots include the raw whole-tile height bound. Leaving
    // it at zero makes conservative tile culling reject this elevated fixture.
    chunk->records[0].maxClearanceZ = 368;
    chunk->revision++;
    scene.view.y -= 336;
    auto track = Object(4);
    track.baseZ = 336;
    track.clearanceZ = 368;
    track.direction = 3;
    track.flags = 1; // Fixed remap keeps source colour roles equally observable.
    track.trackTypeAndRideType = (uint32_t(OpenRCT2::RIDE_TYPE_HEARTLINE_TWISTER_COASTER) << 16) | 15u;
    for (uint32_t rotation = 0; rotation < 4; ++rotation)
    {
        SCOPED_TRACE(rotation);
        scene.rotation = rotation;
        Objects({ track });
        Run();
        EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(1) + 9)), 32u);
        EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(2) + 9)), 32u);
        EXPECT_EQ(ColourCount(Ink(0)), 0u);
        const auto held = pixels;
        EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
        EXPECT_EQ(pixels, held);
    }
}

TEST_F(VulkanWorldObjectLayerTest, WoodenTrackFootingWinsOwnTerrainTieWithoutMovingItsAnchor)
{
    // Use the admitted original wooden-flat recipe and support cursor. Mock
    // opaque art isolates depth: the bottom 32-high arch and terrain occupy the
    // exact same 8x8 coverage; the rail is 32 world units above it. Before the
    // placed-art layer correction every footing pixel lost to terrain under LESS.
    PrepareEntityArt();
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    rides.rides[0].present = true;
    rides.rides[0].rideType = OpenRCT2::RIDE_TYPE_WOODEN_ROLLER_COASTER;
    sprites->trackCatalog = G::BuildWorldTrackCatalog(rides, [](uint32_t image) {
                                for (int i = 0; i < G::MetalSupportRules::worldWoodenAssetCount(); ++i)
                                    if (image == static_cast<uint32_t>(G::MetalSupportRules::worldWoodenAssetImage(i)))
                                        return 2u;
                                return 1u;
                            }).words;
    sprites->revision++;
    auto track = Object(4);
    track.baseZ = 32;
    track.clearanceZ = 64;
    track.flags = 1; // Fixed ghost remap makes both authored colour roles observable.
    track.trackTypeAndRideType = uint32_t(OpenRCT2::RIDE_TYPE_WOODEN_ROLLER_COASTER) << 16;
    for (uint32_t rotation = 0; rotation < 4; ++rotation)
    {
        SCOPED_TRACE(rotation);
        scene.rotation = rotation;
        Objects({ track });
        Run();
        EXPECT_EQ(ColourCount(static_cast<uint8_t>(Ink(2) + 9)), 64u);
        EXPECT_EQ(ColourCount(Ink(0)), 0u);
        const auto held = pixels;
        EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
        EXPECT_EQ(pixels, held);
    }
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

TEST_F(VulkanWorldObjectLayerTest, MechanismPoseProgressionRepeatAndHeldGenerationKeepStaticArtResident)
{
    // One carousel body, with every frame already resident. Synthetic ink makes the
    // selected original frame identity observable independently of the shared rules.
    materials->rideObjects[0] = { 32, 32, 32, true };
    materials->stations[0].present = true;
    materials->stations[0].flags = 1u << 3; // No platforms: isolate the mechanism.
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    auto& ride = rides.rides[0];
    ride.present = true;
    ride.objectSlot = 0;
    ride.stationStyle = 0;
    ride.regularStyle = static_cast<uint16_t>(TrackStyle::merryGoRound);
    ride.vehicleColours[0].body = 2;
    sprites->flatRideCatalog = G::BuildWorldFlatRideCatalog(*materials, rides, nullptr, 17, [&](uint32_t image) {
                                   // The prop catalogue has a compact sprite directory: source image
                                   // IDs are atlas identities, not necessarily sprite-table indices.
                                   // Built-in platforms are deliberately not emitted in this fixture.
                                   if (image < 32 || image >= 64)
                                       return 0u;
                                   G::WorldSurfaceSpriteSet sprite{};
                                   sprite.variants[2] = { { 8, 8 }, { 0, 0 }, image, 0, 0, 5 };
                                   sprite.variants[3] = { { 8, 8 }, { 0, 0 }, image, 1, 0, 5 };
                                   const auto index = static_cast<uint32_t>(sprites->records.size());
                                   sprites->records.push_back(sprite);
                                   return index;
                               }).words;
    ASSERT_NO_THROW(G::ValidateWorldFlatRideCatalog(sprites->flatRideCatalog, sprites->records.size()));
    sprites->revision++;
    auto body = Object(4);
    body.sequence = 1;
    body.trackTypeAndRideType = static_cast<uint32_t>(OpenRCT2::TrackElemType::flatTrack3x3);
    Objects({ body });
    const auto heldChunk = scene.chunks[0];
    const auto heldSprites = scene.sprites;
    const auto spriteRevision = sprites->revision;
    const auto pose = [&](uint32_t tick, uint32_t frame) {
        auto records = std::make_shared<std::vector<OpenRCT2::WorldRidePoseRecord>>(1);
        auto& words = records->front().words;
        words[0] = 3; // Present, with the authoritative mechanism on track.
        for (uint32_t slot = 0; slot < 4; ++slot)
            words[4 + slot * 4] = UINT32_MAX;
        words[4] = 7;
        words[5] = 1;
        words[6] = frame;
        auto result = std::make_shared<OpenRCT2::WorldRidePoseSnapshot>();
        result->epoch = scene.worldEpoch;
        result->entityEpoch = 1;
        result->revision = uint64_t(tick) + 1;
        result->sourceTick = tick;
        result->records = std::move(records);
        return result;
    };
    const auto first = pose(10, 3);
    scene.sourceTick = first->sourceTick;
    scene.ridePoses = first;
    Run();
    ASSERT_GT(ColourCount(static_cast<uint8_t>(Ink(35) + 3)), 0u);
    const auto firstPixels = pixels;
    const auto advanced = pose(300, 13); // Skipped simulation ticks do not synthesize intervening poses.
    scene.sourceTick = advanced->sourceTick;
    scene.ridePoses = advanced;
    const auto changed = Run();
    EXPECT_EQ(changed.worldBufferCopyCalls, 1u);
    EXPECT_EQ(
        changed.bytes[static_cast<size_t>(D::UploadCategory::atlas)][static_cast<size_t>(D::UploadMetric::imageTransfer)], 0u);
    ASSERT_GT(ColourCount(static_cast<uint8_t>(Ink(45) + 3)), 0u);
    EXPECT_NE(pixels, firstPixels);
    const auto advancedPixels = pixels;
    EXPECT_EQ(Run().worldBufferCopyCalls, 0u);
    EXPECT_EQ(pixels, advancedPixels); // Repeated/paused rendering does not advance the mechanism.
    scene.sourceTick = first->sourceTick;
    scene.ridePoses = first;
    EXPECT_EQ(Run().worldBufferCopyCalls, 1u);
    EXPECT_EQ(pixels, firstPixels); // An independently held generation still owns its original pose.
    EXPECT_EQ(first->records->front().words[6], 3u);
    scene.sourceTick = advanced->sourceTick;
    scene.ridePoses = advanced;
    Run(true); // Unsubmitted pose uploads must not poison the resident upload history.
    const auto retried = Run();
    EXPECT_GT(retried.worldBufferCopyCalls, 0u);
    EXPECT_EQ(
        retried.bytes[static_cast<size_t>(D::UploadCategory::atlas)][static_cast<size_t>(D::UploadMetric::imageTransfer)], 0u);
    EXPECT_EQ(pixels, advancedPixels);
    EXPECT_EQ(scene.chunks[0], heldChunk);
    EXPECT_EQ(scene.sprites, heldSprites);
    EXPECT_EQ(sprites->revision, spriteRevision);
}

TEST_F(VulkanWorldObjectLayerTest, OperatingFlatBodyUsesOriginalEntityZoomSnap)
{
    rowPattern = true;
    materials->rideObjects[0] = { 32, 32, 32, true };
    materials->stations[0].present = true;
    materials->stations[0].flags = 1u << 3; // No platforms: isolate the mechanism.
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    auto& ride = rides.rides[0];
    ride.present = true;
    ride.objectSlot = 0;
    ride.stationStyle = 0;
    ride.regularStyle = static_cast<uint16_t>(TrackStyle::merryGoRound);
    ride.vehicleColours[0].body = 2;
    sprites->flatRideCatalog = G::BuildWorldFlatRideCatalog(*materials, rides, nullptr, 17, [&](uint32_t image) {
                                   // The prop catalogue has a compact sprite directory: source image
                                   // IDs are atlas identities, not necessarily sprite-table indices.
                                   // Built-in platforms are deliberately not emitted in this fixture.
                                   if (image < 32 || image >= 64)
                                       return 0u;
                                   G::WorldSurfaceSpriteSet sprite{};
                                   sprite.variants[2] = { { 8, 8 }, { 0, 0 }, image, 0, 0, 7 };
                                   sprite.variants[3] = { { 8, 8 }, { 0, 0 }, image, 1, 0, 7 };
                                   const auto index = static_cast<uint32_t>(sprites->records.size());
                                   sprites->records.push_back(sprite);
                                   return index;
                               }).words;
    ASSERT_NO_THROW(G::ValidateWorldFlatRideCatalog(sprites->flatRideCatalog, sprites->records.size()));
    sprites->revision++;
    auto body = Object(4);
    body.sequence = 1;
    body.trackTypeAndRideType = static_cast<uint32_t>(OpenRCT2::TrackElemType::flatTrack3x3);
    Objects({ body });

    auto records = std::make_shared<std::vector<OpenRCT2::WorldRidePoseRecord>>(1);
    records->front().words[0] = 1; // Valid ride and vehicle, but not on track.
    records->front().words[4] = 7;
    records->front().words[5] = 1;
    auto pose = std::make_shared<OpenRCT2::WorldRidePoseSnapshot>();
    pose->epoch = scene.worldEpoch;
    pose->entityEpoch = 1;
    pose->revision = 1;
    pose->sourceTick = scene.sourceTick;
    pose->records = records;
    scene.ridePoses = pose;
    scene.zoom = 1;
    Run();
    // Frozen PaintDrawStruct/GfxDrawSprite RLE arithmetic: body (32,32,23)
    // projects to (0,9). At zoom1 its first destination row is132, source row0.
    EXPECT_EQ(pixels[132 * extent.width + 128], std::byte(Ink(32) + 3));
    auto onTrackRecords = std::make_shared<std::vector<OpenRCT2::WorldRidePoseRecord>>(*records);
    onTrackRecords->front().words[0] = 3;
    auto onTrack = std::make_shared<OpenRCT2::WorldRidePoseSnapshot>(*pose);
    onTrack->revision++;
    onTrack->records = onTrackRecords;
    scene.ridePoses = onTrack;
    const auto telemetry = Run();
    // Entity interaction floors projected9 to8 BEFORE the RLE zoom phase.
    // The destination row stays132, but now samples source row1.
    EXPECT_EQ(pixels[132 * extent.width + 128], std::byte(Ink(32) + 1 + 3));
    EXPECT_EQ(
        telemetry.bytes[static_cast<size_t>(D::UploadCategory::atlas)][static_cast<size_t>(D::UploadMetric::imageTransfer)],
        0u);
    scene.ridePoses = pose;
    Run();
    EXPECT_EQ(pixels[132 * extent.width + 128], std::byte(Ink(32) + 3));
}

TEST(WorldFlatRideRulesTest, OnlyMechanismPartsInheritEntityInteraction)
{
    namespace F = G::FlatRideRules;
    auto pose = F::worldFlatEmptyPose();
    pose.present = 1;
    pose.onTrack = 1;
    F::WorldFlatPart p{};
    p.bank = 1;
    for (int family : { 4, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 })
        EXPECT_TRUE(F::worldFlatEntityPart(p, family, pose));
    for (int family : { 1, 2, 3, 5, 6, 7, 18, 19, 20, 21, 22, 23 })
        EXPECT_FALSE(F::worldFlatEntityPart(p, family, pose));
    p.bank = 0;
    for (int image : { 22150, 22151, 22152, 22153 })
    {
        p.image = image;
        EXPECT_TRUE(F::worldFlatEntityPart(p, 9, pose));
    }
    p.image = 22134;
    EXPECT_FALSE(F::worldFlatEntityPart(p, 9, pose)); // platform
    p.image = 22138;
    EXPECT_FALSE(F::worldFlatEntityPart(p, 9, pose)); // fence
    p.image = 22006;
    EXPECT_TRUE(F::worldFlatEntityPart(p, 15, pose)); // animated pendulum
    p.image = 22161;
    EXPECT_TRUE(F::worldFlatEntityPart(p, 17, pose)); // motion cabin support
    pose.onTrack = 0;
    EXPECT_FALSE(F::worldFlatEntityPart(p, 17, pose));
    pose.onTrack = 1;
    pose.present = 0;
    EXPECT_FALSE(F::worldFlatEntityPart(p, 17, pose));
}
TEST_F(VulkanWorldObjectLayerTest, CommonParentCacheBoundaryMatchesOriginalArrangement)
{
    struct LegacyScope
    {
        bool previous = gPaintStableSort;
        LegacyScope()
        {
            gPaintStableSort = false;
        }
        ~LegacyScope()
        {
            gPaintStableSort = previous;
        }
    } legacy;
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    scene.zoom = 0;
    for (uint32_t count : { 511u, 512u, 513u })
        for (uint32_t rotation = 0; rotation < 4; ++rotation)
        {
            SCOPED_TRACE(::testing::Message() << count << '/' << rotation);
            scene.rotation = rotation;
            auto source = std::make_shared<D::SelectedVehicleSnapshot>();
            source->worldEpoch = scene.worldEpoch;
            source->entityEpoch = 1;
            source->sourceTick = scene.sourceTick;
            auto packet = std::make_shared<G::SelectedVehiclePaintPacket>();
            packet->source = source;
            auto& words = packet->words;
            words.resize(16 + 12 + count * 28);
            words[0] = G::kSelectedVehiclePaintMagic;
            words[1] = G::kSelectedVehiclePaintVersion;
            words[2] = 1;
            words[3] = count;
            words[4] = 16;
            words[5] = 28;
            words[6] = 28 + count * 12;
            words[7] = static_cast<uint32_t>(words.size());
            words[8] = scene.sourceTick;
            words[9] = static_cast<uint32_t>(scene.worldEpoch);
            words[10] = static_cast<uint32_t>(scene.worldEpoch >> 32);
            words[11] = 1;
            const G::SelectedVehicleCarRecord car{ 7, 1, 0, count, 0, 0, 0, 0, { -64, -64, 128, 128 } };
            std::memcpy(words.data() + 16, &car, sizeof(car));
            std::vector<PaintStruct> paint(count);
            std::vector<G::SelectedVehicleParentRecord> metadata(count);
            auto session = std::make_unique<PaintSessionCore>();
            session->CurrentRotation = static_cast<uint8_t>(rotation);
            session->QuadrantBackIndex = UINT32_MAX;
            for (uint32_t i = 0; i < count; ++i)
            {
                // Authored test bounds and image anchors are intentionally independent.
                // Bounds occupy one parent per quadrant, with adjacent overlap, avoiding
                // a pathological all-to-all dense sort. All original8px sprites fit the
                // single paint column[0,32), so its node count is exactly count.
                const auto local = CoordsXY{ int32_t(i * 16), int32_t(i * 16) }.rotate((rotation * 3) & 3);
                const auto begin = CoordsXY{ 16000 + local.x, 16000 + local.y };
                auto size = CoordsXY{ 24, 24 };
                if (rotation == 0 || rotation == 1)
                    --size.x;
                if (rotation == 0 || rotation == 3)
                    --size.y;
                size = size.rotate((rotation * 3) & 3);
                auto& p = paint[i];
                const int32_t z = int32_t(i % 3) * 8;
                p.Bounds = { begin.x, begin.y, z, begin.x + size.x, begin.y + size.y, z + 15 };
                const uint32_t x = i % 8, y = i / 8;
                metadata[i] = { p.Bounds.x, p.Bounds.y, p.Bounds.z, p.Bounds.x_end,  p.Bounds.y_end, p.Bounds.z_end, i,
                                0,          256,        0,          8u | (8u << 16), x | (y << 16) };
                G::WorldSurfaceRecord component{};
                component.world = { rotation == 1 || rotation == 2 ? -32 : 0, rotation == 2 || rotation == 3 ? -32 : 0, 0 };
                component.valid = 17;
                component.spriteSize = { 8, 8 };
                component.spriteOffset = { int32_t(x), int32_t(y) };
                component.asset = i % (assetCount - 1) + 1;
                std::memcpy(words.data() + words[6] + i * 16, &component, sizeof(component));
                int hash = begin.x + begin.y;
                if (rotation == 1)
                    hash = begin.y - begin.x + MaxPaintQuadrants * 16;
                else if (rotation == 2)
                    hash = -begin.x - begin.y + MaxPaintQuadrants * 32;
                else if (rotation == 3)
                    hash = begin.x - begin.y + MaxPaintQuadrants * 16;
                const auto quadrant = static_cast<uint32_t>(std::clamp(hash / 32, 0, MaxPaintQuadrants - 1));
                // No clamping/collapsed quadrant can accidentally create a dense case.
                ASSERT_GT(quadrant, 0u);
                ASSERT_LT(quadrant, uint32_t(MaxPaintQuadrants - 1));
                ASSERT_EQ(session->Quadrants[quadrant], nullptr);
                p.QuadrantIndex = static_cast<uint16_t>(quadrant);
                p.NextQuadrantEntry = session->Quadrants[quadrant];
                session->Quadrants[quadrant] = &p;
                session->QuadrantBackIndex = std::min(session->QuadrantBackIndex, quadrant);
                session->QuadrantFrontIndex = std::max(session->QuadrantFrontIndex, quadrant);
            }
            std::memcpy(words.data() + words[5], metadata.data(), metadata.size() * sizeof(metadata[0]));
            G::ValidateSelectedVehiclePaintPacket(*packet);
            PaintSessionArrange(*session);
            std::vector<std::byte> expected(extent.width * extent.height);
            std::vector<bool> visited(count);
            uint32_t visits = 0;
            for (auto* parent = session->PaintHead; parent != nullptr; parent = parent->NextQuadrantEntry)
            {
                ASSERT_LT(visits++, count);
                const auto index = static_cast<uint32_t>(parent - paint.data());
                ASSERT_LT(index, count);
                ASSERT_FALSE(visited[index]);
                visited[index] = true;
                const auto ink = std::byte(Ink(index % (assetCount - 1) + 1));
                const uint32_t left = 128 + index % 8, top = 128 + index / 8;
                for (uint32_t y = top; y < top + 8; ++y)
                    for (uint32_t x = left; x < left + 8; ++x)
                        expected[y * extent.width + x] = ink;
            }
            ASSERT_EQ(visits, count);
            scene.selectedVehicle = packet;
            Run();
            EXPECT_EQ(pixels, expected); // Every pixel, not only the final overlapping ink.
        }
    scene.selectedVehicle.reset();
    Run();
    EXPECT_EQ(ColourCount(0), pixels.size());
}

namespace
{
    struct FilterTestPart
    {
        uint32_t effects{}, palette{}, asset = 1;
    };
    std::shared_ptr<G::SelectedVehiclePaintPacket> FilterPacket(
        const G::WorldSurfaceSceneCommand& scene, const std::vector<FilterTestPart>& parts)
    {
        auto source = std::make_shared<D::SelectedVehicleSnapshot>();
        source->worldEpoch = scene.worldEpoch;
        source->entityEpoch = 1;
        source->sourceTick = scene.sourceTick;
        auto packet = std::make_shared<G::SelectedVehiclePaintPacket>();
        packet->source = source;
        const auto count = static_cast<uint32_t>(parts.size());
        auto& words = packet->words;
        words.resize(28 + count * 28);
        words[0] = G::kSelectedVehiclePaintMagic;
        words[1] = G::kSelectedVehiclePaintVersion;
        words[2] = 1;
        words[3] = count;
        words[4] = 16;
        words[5] = 28;
        words[6] = 28 + count * 12;
        words[7] = static_cast<uint32_t>(words.size());
        words[8] = scene.sourceTick;
        words[9] = static_cast<uint32_t>(scene.worldEpoch);
        words[10] = static_cast<uint32_t>(scene.worldEpoch >> 32);
        words[11] = 1;
        const G::SelectedVehicleCarRecord car{ 7, 1, 0, count, 0, 0, 0, 0, { -64, -64, 64, 64 } };
        std::memcpy(words.data() + 16, &car, sizeof(car));
        for (uint32_t i = 0; i < count; i++)
        {
            // One original parent/child chain has an unambiguous, independently authored draw order.
            const G::SelectedVehicleParentRecord parent{
                0, 0, 0, 31, 31, 15, 0, 0, 256u | (i ? 1u : 0u), 0, 8u | (8u << 16), 0
            };
            G::WorldSurfaceRecord component{};
            component.world = { 0, 0, 0 };
            component.valid = 17;
            component.spriteSize = { 8, 8 };
            component.asset = parts[i].asset;
            component.effects = parts[i].effects;
            component.palettes = parts[i].palette;
            std::memcpy(words.data() + words[5] + i * 12, &parent, sizeof(parent));
            std::memcpy(words.data() + words[6] + i * 16, &component, sizeof(component));
        }
        G::ValidateSelectedVehiclePaintPacket(*packet);
        return packet;
    }
} // namespace
TEST_F(VulkanWorldObjectLayerTest, OrderedWorldFiltersComposeAroundOpaqueAndWater)
{
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    scene.depthBase = (1 << 21) + 1; // Exercise the full painter-depth interval, not a truncated20-bit key.
    std::array<std::byte, 256 * 256> palette{};
    for (uint32_t row = 0; row < 256; row++)
        for (uint32_t x = 0; x < 256; x++)
            palette[row * 256 + x] = std::byte(row == 1 ? (x == 0 ? 0 : x - 1) : row == 2 ? (x * 2) & 255 : x);
    executor.SetRemapPalette(palette);
    constexpr uint32_t glass = 1u << 10, water = 1u << 8, literalWater = 1u << 9;
    struct Case
    {
        std::vector<FilterTestPart> parts;
        uint8_t expected;
    };
    const std::array cases{ Case{ { { 0, 0, 1 }, { glass, 1 }, { glass, 1 } }, 99 }, // two distinct Darken1 applications
                            Case{ { { 0, 0, 1 }, { glass, 1 }, { 0, 0, 2 }, { glass, 1 } },
                                  101 }, // earlier filter hidden by later opaque ink102
                            Case{ { { 0, 0, 1 }, { water, 2 }, { glass, 1 } }, 201 },
                            Case{ { { 0, 0, 1 }, { glass, 1 }, { water, 2 } }, 200 },
                            Case{ { { 0, 0, 1 }, { literalWater, 0, 2 }, { glass, 1 } }, 101 },
                            Case{ { { 0, 0, 1 }, { glass, 1 }, { literalWater, 0, 2 } }, 102 } };
    // Water uses source texel101 plus palette base2 minus1, selecting row102.
    // Noncommuting row102 doubles its input, while glass row1 decrements it.
    for (uint32_t x = 0; x < 256; x++)
        palette[102 * 256 + x] = std::byte((x * 2) & 255);
    executor.SetRemapPalette(palette);
    for (size_t c = 0; c < cases.size(); c++)
    {
        SCOPED_TRACE(c);
        scene.selectedVehicle = FilterPacket(scene, cases[c].parts);
        Run();
        EXPECT_EQ(ColourCount(cases[c].expected), 64u);
        EXPECT_EQ(ColourCount(0), pixels.size() - 64);
    }
    scene.selectedVehicle.reset();
    Run();
    EXPECT_EQ(ColourCount(0), pixels.size());
}
TEST_F(VulkanWorldObjectLayerTest, WorldFilterPerPixelLimitRejectsWholeFrame)
{
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    std::vector<FilterTestPart> parts(256, { 1u << 10, 1, 1 });
    parts.insert(parts.begin(), { 0, 0, 1 });
    scene.selectedVehicle = FilterPacket(scene, parts);
    Run();
    EXPECT_EQ(ColourCount(Ink(1)), 64u); // Exactly256 additions modulo256 are accepted.
    parts.push_back({ 1u << 10, 1, 1 });
    scene.selectedVehicle = FilterPacket(scene, parts);
    Run(false, true);
    EXPECT_EQ(ColourCount(0), pixels.size()); // Global validation prevents a partly resolved image.
}
TEST_F(VulkanWorldObjectLayerTest, WorldFilterGlobalPoolLimitRejectsWholeFrame)
{
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    // 2050*64 fragments exceed the256*256*2 node arena independently of the pixel-list bound.
    std::vector<FilterTestPart> parts(2050, { 1u << 10, 1, 1 });
    parts.insert(parts.begin(), { 0, 0, 1 });
    scene.selectedVehicle = FilterPacket(scene, parts);
    Run(false, true);
    EXPECT_EQ(ColourCount(0), pixels.size());
}

namespace
{
    class VulkanWorldFilterQueuedTest : public VulkanWorldObjectLayerTest
    {
    protected:
        struct Ticket
        {
            V::SubmissionToken token;
            V::UploadAllocation readback;
            uint32_t left;
            uint8_t expected;
        };
        std::vector<V::SubmissionToken> pending;
        uint32_t SlotCount() const override
        {
            return 3;
        }
        void TearDown() override
        {
            // Failure cleanup also respects the resource owner: never destroy buffers while submitted work uses them.
            for (const auto& token : pending)
                EXPECT_TRUE(slots->Wait(token, 30'000'000'000));
            pending.clear();
            VulkanWorldObjectLayerTest::TearDown();
        }
        std::optional<Ticket> Queue(
            uint32_t slot, uint32_t left, uint8_t expected, const std::vector<FilterTestPart>& parts, bool abandon = false)
        {
            ++scene.sourceTick;
            scene.view = { -static_cast<int32_t>(left), -static_cast<int32_t>(left) };
            scene.selectedVehicle = parts.empty() ? nullptr : FilterPacket(scene, parts);
            G::FrameCommandStream commands;
            commands.worldSurfaces = scene;
            if (!uploaded)
                for (uint32_t image = 0; image < assetCount; image++)
                    commands.textureUploads.push_back(
                        { .atlas = 0,
                          .bounds = { int32_t(image * 8), 0, int32_t(image * 8 + 8), 48 },
                          .sourcePitch = 8,
                          .descriptorIndex = image,
                          .descriptor = { .atlasOrigin = { int32_t(image * 8), 0 }, .atlasLayer = 0 },
                          .pixels = std::vector<std::byte>(8 * 48, std::byte(Ink(image))) });
            // Fresh/explicitly retired slots must be available. No implicit owner wait between submissions.
            auto token = slots->Begin(slot, false);
            if (!token)
                throw std::runtime_error("Queued filter test slot unexpectedly busy");
            const auto output = executor.Record(*token, commands);
            if (abandon)
            {
                slots->Abandon(*token);
                executor.Discard(slot);
                return std::nullopt;
            }
            auto readback = token->upload->Allocate(extent.width * extent.height, 4);
            if (!readback)
                throw std::runtime_error("Queued filter readback allocation failed");
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            V::RecordImageBarrier(
                token->commandBuffer, output.canvas->GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
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
            pending.push_back(*token);
            executor.Commit();
            uploaded = true;
            return Ticket{ *token, readback, left, expected };
        }
        void RetireAndCheck(const std::array<Ticket, 3>& tickets)
        {
            // Check the last submission first: all three were queued before any wait or pixel access.
            for (uint32_t index : { 2u, 0u, 1u })
            {
                const auto& ticket = tickets[index];
                if (!slots->Wait(ticket.token, 30'000'000'000))
                    throw std::runtime_error("Queued filter completion timeout");
                executor.CompleteTerrainStatus(ticket.token.frameIndex);
                ticket.token.upload->Invalidate(ticket.readback.offset, ticket.readback.size);
                std::vector<std::byte> expectedPixels(extent.width * extent.height);
                if (ticket.expected != 0)
                    for (uint32_t y = ticket.left; y < ticket.left + 8; y++)
                        for (uint32_t x = ticket.left; x < ticket.left + 8; x++)
                            expectedPixels[y * extent.width + x] = std::byte(ticket.expected);
                const std::vector<std::byte> actual(ticket.readback.data, ticket.readback.data + extent.width * extent.height);
                EXPECT_EQ(actual, expectedPixels)
                    << "slot=" << ticket.token.frameIndex << " generation=" << ticket.token.generation;
            }
            pending.clear(); // All named tokens retired before their upload rings may be reset.
        }
    };
} // namespace
TEST_F(VulkanWorldFilterQueuedTest, ThreeQueuedCanvasesAndAbandonRetryKeepIndependentResults)
{
    static_assert(V::kFramesInFlight >= 3);
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    constexpr uint32_t glass = 1u << 10;
    const auto first = Queue(0, 128, 104, { { 0, 0, 1 }, { glass, 1 }, { glass, 2 } });
    const auto second = Queue(1, 120, 105, { { 0, 0, 2 }, { glass, 3 } });
    // Abandon a distinct recorded filter list while earlier accepted frames are still unretired.
    EXPECT_FALSE(Queue(2, 104, 0, { { 0, 0, 3 }, { glass, 70 } }, true));
    const auto third = Queue(2, 112, 109, { { 0, 0, 4 }, { glass, 5 } });
    RetireAndCheck({ first.value(), second.value(), third.value() });

    // Reuse every slot: one filtered result changes, one loses all filters, and one restores the earlier position.
    const auto reusedFirst = Queue(0, 112, 111, { { 0, 0, 2 }, { glass, 9 } });
    EXPECT_FALSE(Queue(1, 96, 0, { { 0, 0, 5 }, { glass, 80 } }, true));
    const auto reusedSecond = Queue(1, 120, 0, {});
    const auto reusedThird = Queue(2, 128, 102, { { 0, 0, 1 }, { glass, 1 } });
    RetireAndCheck({ reusedFirst.value(), reusedSecond.value(), reusedThird.value() });
}

TEST_F(VulkanWorldObjectLayerTest, CompactBoundsRangeFallbackMatchesOriginalArrangement)
{
    struct LegacyScope
    {
        bool previous = gPaintStableSort;
        LegacyScope()
        {
            gPaintStableSort = false;
        }
        ~LegacyScope()
        {
            gPaintStableSort = previous;
        }
    } legacy;
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    scene.zoom = 0;
    for (uint32_t count : { 3u })
        for (uint32_t rotation = 0; rotation < 4; ++rotation)
            for (int32_t height : { -32769, -32768, 32736, 32753 })
            {
                SCOPED_TRACE(::testing::Message() << count << '/' << rotation << '/' << height);
                scene.rotation = rotation;
                auto source = std::make_shared<D::SelectedVehicleSnapshot>();
                source->worldEpoch = scene.worldEpoch;
                source->entityEpoch = 1;
                source->sourceTick = scene.sourceTick;
                auto packet = std::make_shared<G::SelectedVehiclePaintPacket>();
                packet->source = source;
                auto& words = packet->words;
                words.resize(16 + 12 + count * 28);
                words[0] = G::kSelectedVehiclePaintMagic;
                words[1] = G::kSelectedVehiclePaintVersion;
                words[2] = 1;
                words[3] = count;
                words[4] = 16;
                words[5] = 28;
                words[6] = 28 + count * 12;
                words[7] = static_cast<uint32_t>(words.size());
                words[8] = scene.sourceTick;
                words[9] = static_cast<uint32_t>(scene.worldEpoch);
                words[10] = static_cast<uint32_t>(scene.worldEpoch >> 32);
                words[11] = 1;
                const G::SelectedVehicleCarRecord car{ 7, 1, 0, count, 0, 0, 0, 0, { -64, -64, 128, 128 } };
                std::memcpy(words.data() + 16, &car, sizeof(car));
                std::vector<PaintStruct> paint(count);
                std::vector<G::SelectedVehicleParentRecord> metadata(count);
                auto session = std::make_unique<PaintSessionCore>();
                session->CurrentRotation = static_cast<uint8_t>(rotation);
                session->QuadrantBackIndex = UINT32_MAX;
                for (uint32_t i = 0; i < count; ++i)
                {
                    // Authored test bounds and image anchors are intentionally independent.
                    // Bounds occupy one parent per quadrant, with adjacent overlap, avoiding
                    // a pathological all-to-all dense sort. All original8px sprites fit the
                    // single paint column[0,32). MetadataZ crosses signed16 limits independently of sprite anchors.
                    const auto local = CoordsXY{ int32_t(i * 16), int32_t(i * 16) }.rotate((rotation * 3) & 3);
                    const auto begin = CoordsXY{ 16000 + local.x, 16000 + local.y };
                    auto size = CoordsXY{ 24, 24 };
                    if (rotation == 0 || rotation == 1)
                        --size.x;
                    if (rotation == 0 || rotation == 3)
                        --size.y;
                    size = size.rotate((rotation * 3) & 3);
                    auto& p = paint[i];
                    const int32_t z = height + int32_t(i % 3) * 8;
                    p.Bounds = { begin.x, begin.y, z, begin.x + size.x, begin.y + size.y, z + 15 };
                    const uint32_t x = i % 8, y = i / 8;
                    metadata[i] = { p.Bounds.x, p.Bounds.y, p.Bounds.z, p.Bounds.x_end,  p.Bounds.y_end, p.Bounds.z_end, i,
                                    0,          256,        0,          8u | (8u << 16), x | (y << 16) };
                    G::WorldSurfaceRecord component{};
                    component.world = { rotation == 1 || rotation == 2 ? -32 : 0, rotation == 2 || rotation == 3 ? -32 : 0, 0 };
                    component.valid = 17;
                    component.spriteSize = { 8, 8 };
                    component.spriteOffset = { int32_t(x), int32_t(y) };
                    component.asset = i % (assetCount - 1) + 1;
                    std::memcpy(words.data() + words[6] + i * 16, &component, sizeof(component));
                    int hash = begin.x + begin.y;
                    if (rotation == 1)
                        hash = begin.y - begin.x + MaxPaintQuadrants * 16;
                    else if (rotation == 2)
                        hash = -begin.x - begin.y + MaxPaintQuadrants * 32;
                    else if (rotation == 3)
                        hash = begin.x - begin.y + MaxPaintQuadrants * 16;
                    const auto quadrant = static_cast<uint32_t>(std::clamp(hash / 32, 0, MaxPaintQuadrants - 1));
                    // No clamping/collapsed quadrant can accidentally create a dense case.
                    ASSERT_GT(quadrant, 0u);
                    ASSERT_LT(quadrant, uint32_t(MaxPaintQuadrants - 1));
                    ASSERT_EQ(session->Quadrants[quadrant], nullptr);
                    p.QuadrantIndex = static_cast<uint16_t>(quadrant);
                    p.NextQuadrantEntry = session->Quadrants[quadrant];
                    session->Quadrants[quadrant] = &p;
                    session->QuadrantBackIndex = std::min(session->QuadrantBackIndex, quadrant);
                    session->QuadrantFrontIndex = std::max(session->QuadrantFrontIndex, quadrant);
                }
                std::memcpy(words.data() + words[5], metadata.data(), metadata.size() * sizeof(metadata[0]));
                G::ValidateSelectedVehiclePaintPacket(*packet);
                PaintSessionArrange(*session);
                std::vector<std::byte> expected(extent.width * extent.height);
                std::vector<bool> visited(count);
                uint32_t visits = 0;
                for (auto* parent = session->PaintHead; parent != nullptr; parent = parent->NextQuadrantEntry)
                {
                    ASSERT_LT(visits++, count);
                    const auto index = static_cast<uint32_t>(parent - paint.data());
                    ASSERT_LT(index, count);
                    ASSERT_FALSE(visited[index]);
                    visited[index] = true;
                    const auto ink = std::byte(Ink(index % (assetCount - 1) + 1));
                    const uint32_t left = 128 + index % 8, top = 128 + index / 8;
                    for (uint32_t y = top; y < top + 8; ++y)
                        for (uint32_t x = left; x < left + 8; ++x)
                            expected[y * extent.width + x] = ink;
                }
                ASSERT_EQ(visits, count);
                scene.selectedVehicle = packet;
                Run();
                EXPECT_EQ(pixels, expected); // Every pixel, not only the final overlapping ink.
            }
    scene.selectedVehicle.reset();
    Run();
    EXPECT_EQ(ColourCount(0), pixels.size());
}

// Append inside ENABLE_VULKAN in VulkanWorldObjectLayerTests.cpp after coherent integration.
TEST_F(VulkanWorldObjectLayerTest, SelectedCarPhysicalDepthUsesOwnedPoseAndSurvivesEmptyTileAndRemoval)
{
    // This fixture normally supplies terrain only at zoom0. Both occlusion
    // controls need a resident terrain sprite at the zoom being exercised.
    sprites->records[0].variants[3] = sprites->records[0].variants[2];
    sprites->records[0].variants[3].zoom = 1;
    sprites->revision++;
    auto source = std::make_shared<D::SelectedVehicleSnapshot>();
    source->worldEpoch = scene.worldEpoch;
    source->entityEpoch = 1;
    source->sourceTick = scene.sourceTick;
    for (int zoom : { 0, 1 })
        for (int pan : { 100, 128 })
            for (int anchor : { 0, 16 })
            {
                SCOPED_TRACE(::testing::Message() << zoom << '/' << pan << '/' << anchor);
                scene.zoom = zoom;
                scene.view = { -pan, -pan };
                auto packet = std::make_shared<G::SelectedVehiclePaintPacket>();
                packet->source = source;
                auto& words = packet->words;
                words.resize(16 + 12 + 12 + 16);
                const std::array<uint32_t, 16> header{
                    G::kSelectedVehiclePaintMagic, G::kSelectedVehiclePaintVersion,  1, 1, 16, 28, 40, 56, scene.sourceTick,
                    uint32_t(scene.worldEpoch),    uint32_t(scene.worldEpoch >> 32), 1
                };
                std::copy(header.begin(), header.end(), words.begin());
                // Hold the component's raster placement fixed while changing its
                // owned pose. Avoid a coplanar tie: this checks actual XYZ depth,
                // independently of arbitrary legacy bounds and local overlay order.
                const G::SelectedVehicleCarRecord car{
                    7, 1, 0, 1, 0, anchor, anchor, anchor == 0 ? -16 : anchor, { -64, -64, 64, 64 }
                };
                const G::SelectedVehicleParentRecord parent{ 0, 0, 999, 31, 31, 999, 0, 0, 256, 0, 8u | (8u << 16), 0 };
                G::WorldSurfaceRecord component{};
                component.valid = 17;
                component.spriteSize = { 8, 8 };
                component.asset = 1;
                component.zoom = zoom;
                std::memcpy(words.data() + 16, &car, sizeof(car));
                std::memcpy(words.data() + 28, &parent, sizeof(parent));
                std::memcpy(words.data() + 40, &component, sizeof(component));
                scene.selectedVehicle = packet;
                Run();
                EXPECT_EQ(pixels[pan * extent.width + pan], std::byte(anchor == 0 ? Ink(0) : Ink(1)));
            }
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = scene.chunks[0]->revision + 1;
    scene.chunks = { empty };
    Run();
    EXPECT_GT(ColourCount(Ink(1)), 0u); // No terrain or object admission is required by the selected car.
    scene.selectedVehicle.reset();
    Run();
    EXPECT_EQ(ColourCount(0), pixels.size());
}

TEST_F(VulkanWorldObjectLayerTest, AuthoredSpriteDepthHasOneWinnerAcrossEntireTallOverlap)
{
    // These two opaque original-art stand-ins occupy the same 8x48 rectangle.
    // A ground plane and an upright plane used to cross inside that rectangle,
    // splitting the sprite. Actual anchor XYZ must instead decide every pixel.
    for (int zoom : { 0, 1 })
        sprites->records[0].variants[zoom + 2] = { { 8, 48 }, { 0, -16 }, 0, zoom, 0, 1 };
    sprites->revision++;
    auto source = std::make_shared<D::SelectedVehicleSnapshot>();
    source->worldEpoch = scene.worldEpoch;
    source->entityEpoch = 1;
    source->sourceTick = scene.sourceTick;
    for (int zoom : { 0, 1 })
        for (int pan : { 96, 128 })
            for (bool nearer : { false, true })
            {
                SCOPED_TRACE(::testing::Message() << "zoom=" << zoom << " pan=" << pan << " nearer=" << nearer);
                scene.zoom = zoom;
                scene.view = { -pan, -pan };
                auto packet = std::make_shared<G::SelectedVehiclePaintPacket>();
                packet->source = source;
                auto& words = packet->words;
                words.resize(56);
                const std::array<uint32_t, 16> header{
                    G::kSelectedVehiclePaintMagic, G::kSelectedVehiclePaintVersion,  1, 1, 16, 28, 40, 56, scene.sourceTick,
                    uint32_t(scene.worldEpoch),    uint32_t(scene.worldEpoch >> 32), 1
                };
                std::copy(header.begin(), header.end(), words.begin());
                // Terrain anchor D=0. Near pose D=24, far pose D=-8. The
                // far sprite's offset compensates its projected +8 Y; neither
                // the bitmap extent nor these unrelated bounds are a depth input.
                const int32_t xy = nearer ? 8 : 0;
                const int32_t z = nearer ? 8 : -8;
                const int32_t offsetY = nearer ? -16 : -24;
                const G::SelectedVehicleCarRecord car{ 7, 1, 0, 1, 0, xy, xy, z, { -64, -64, 64, 64 } };
                const G::SelectedVehicleParentRecord parent{
                    -128, -128, -256, 128, 128, 256, 0, 0, 256, 0, 8u | (48u << 16), uint32_t(uint16_t(offsetY)) << 16
                };
                G::WorldSurfaceRecord component{};
                component.world = { xy, xy, z };
                component.valid = 17;
                component.spriteSize = { 8, 48 };
                component.spriteOffset = { 0, offsetY };
                component.asset = 1;
                component.zoom = zoom;
                std::memcpy(words.data() + 16, &car, sizeof(car));
                std::memcpy(words.data() + 28, &parent, sizeof(parent));
                std::memcpy(words.data() + 40, &component, sizeof(component));
                G::ValidateSelectedVehiclePaintPacket(*packet);
                scene.selectedVehicle = packet;
                Run();
                const auto winner = std::byte(nearer ? Ink(1) : Ink(0));
                const int left = pan, top = pan - (16 >> zoom);
                const int width = 8 >> zoom, height = 48 >> zoom;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        ASSERT_EQ(pixels[(top + y) * extent.width + left + x], winner) << "overlap pixel " << x << ',' << y;
                EXPECT_EQ(ColourCount(nearer ? Ink(0) : Ink(1)), 0u);
                EXPECT_EQ(ColourCount(nearer ? Ink(1) : Ink(0)), size_t(width * height));
            }
}

TEST_F(VulkanWorldObjectLayerTest, ResidentCatalogBurstSurvivesDiscardAndRetiresBeforeSlotReuse)
{
    // This 20 MiB resident table exceeds the fixture's 8 MiB frame ring.
    // Padding is never visited, so the expected image is the same terrain tile.
    sprites->records.resize(100000);
    Run(true);
    Run();
    EXPECT_GT(ColourCount(Ink(0)), 0u);
    const auto held = pixels;
    for (int frame = 0; frame < 3; ++frame)
    {
        const auto telemetry = Run();
        EXPECT_EQ(pixels, held);
        EXPECT_EQ(telemetry.worldBufferCopyCalls, 0u);
    }
}

TEST_F(VulkanWorldObjectLayerTest, NativeEntitiesUseResidentFieldsAndImmutableHeldSnapshots)
{
    PrepareEntityArt();
    // Here colours identify resident artwork, independently of the fixture's
    // deliberately non-identity palette used by other remap tests.
    std::array<std::byte, 256 * 256> identity{};
    for (size_t i = 0; i < identity.size(); ++i)
        identity[i] = std::byte(i & 255);
    executor.SetRemapPalette(identity);
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    auto assets = std::make_shared<G::PeepAssetGeneration>();
    assets->revision = 1;
    assets->catalog = std::make_shared<D::RetainedPeepAnimationCatalog>();
    assets->descriptors.push_back({ 0, 1, 1, 0, 1, 32, 0, 0 });
    assets->facts.resize(37);
    assets->facts[0] = { 1, 1, 0, 0 };
    assets->balloonBase = 20;
    sprites->peepAssets = assets;
    sprites->revision++;
    D::RetainedPeepRecord raw{};
    raw.x = 8;
    raw.y = 8;
    raw.z = 8;
    raw.previousX = raw.x;
    raw.previousY = raw.y;
    raw.previousZ = raw.z;
    raw.id = 7;
    raw.generation = 1;
    raw.flags = 1;
    raw.objectGeneration = 1;
    raw.action = 254;
    raw.width = 6;
    raw.heightMin = 16;
    raw.heightMax = 8;
    auto fields = D::SplitRetainedPeepRecord(raw);
    D::RetainedPeepBatch batch{};
    batch.epoch = 177;
    batch.reset = true;
    batch.lifecycle.push_back({ 7, fields.lifecycle });
    batch.motion.push_back({ 7, 1, fields.motion });
    batch.appearance.push_back({ 7, 1, fields.appearance });
    batch.animation.push_back({ 7, 1, fields.animation });
    D::RetainedPeepScene peeps;
    ASSERT_TRUE(peeps.Apply(batch, 1));
    auto held = peeps.GetSnapshot();
    scene.peeps = held;
    auto balloons = std::make_shared<D::RetainedBalloonSnapshot>();
    balloons->epoch = 177;
    balloons->sequence = 1;
    balloons->count = 1;
    auto balloonChunk = std::make_shared<D::RetainedBalloonChunk>();
    balloonChunk->revision = balloonChunk->gpuRevision = 1;
    balloonChunk->records[9] = { 40, 8, 8, 9, 1, 0, 0, 0, 13, 22, 11, 1 };
    balloons->chunks[0] = balloonChunk;
    scene.balloons = balloons;
    Run();
    EXPECT_GT(ColourCount(Ink(1)), 0u);
    EXPECT_GT(ColourCount(Ink(20)), 0u);
    auto initial = pixels;
    Run();
    EXPECT_EQ(initial, pixels);
    // Generation deletion cannot alter a held snapshot. Resubmit it after deleting the current identity.
    D::RetainedPeepBatch deletion{};
    deletion.epoch = 177;
    deletion.lifecycle.push_back({ 7, { 1, 0 } });
    ASSERT_TRUE(peeps.Apply(deletion, 2));
    // A newer producer snapshot does not mutate the still-held renderer generation.
    Run();
    EXPECT_EQ(initial, pixels);
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        scene.rotation = rotation;
        Run();
        EXPECT_GT(ColourCount(Ink(1 + rotation)), 0u);
        EXPECT_GT(ColourCount(Ink(20)), 0u);
    }
    scene.rotation = 0;
    scene.peeps = peeps.GetSnapshot();
    Run();
    EXPECT_EQ(ColourCount(Ink(1)), 0u);
    EXPECT_TRUE(held->TryGet(EntityId::FromUnderlying(7)).has_value());
}

TEST_F(VulkanWorldObjectLayerTest, NativeVehicleStateSelectsResidentHeadingAndRejectsMixedGenerations)
{
    PrepareEntityArt();
    std::array<std::byte, 256 * 256> identity{};
    for (size_t i = 0; i < identity.size(); ++i)
        identity[i] = std::byte(i & 255);
    executor.SetRemapPalette(identity);
    auto empty = std::make_shared<G::WorldSurfaceChunk>();
    empty->revision = 2;
    scene.chunks = { empty };
    auto source = std::make_shared<D::VehiclePresentationCatalog>();
    source->cars.resize(1);
    auto& car = source->cars[0];
    car.present = true;
    car.imageBase = car.baseImage = 100000;
    car.imageCount = car.carImages = 32;
    car.baseFrames = 1;
    car.groups[0] = { car.baseImage, 6 }; // 32 authored headings.
    auto used = std::make_shared<const std::vector<uint32_t>>(std::initializer_list<uint32_t>{ 0 });
    sprites->vehicleSource = source;
    sprites->vehicleUsedCars = used;
    sprites->vehicleCatalog = G::BuildWorldVehicleCatalog(*source, *used, 1, [](uint32_t image) {
                                  return image >= 100000 ? image - 100000 + 1 : 40;
                              }).words;
    sprites->revision++;
    auto records = std::make_shared<std::vector<D::VehiclePresentationRecord>>(1);
    (*records)[0].x = (*records)[0].y = (*records)[0].z = 8;
    (*records)[0].entityId = 5;
    (*records)[0].generation = 1;
    auto vehicles = std::make_shared<D::VehiclePresentationSnapshot>();
    vehicles->worldEpoch = scene.worldEpoch;
    vehicles->sourceTick = scene.sourceTick;
    vehicles->catalog = source;
    vehicles->usedCars = used;
    vehicles->records = records;
    scene.vehicles = vehicles;
    Run();
    EXPECT_GT(ColourCount(Ink(1)), 0u);
    auto heldPixels = pixels;
    Run();
    EXPECT_EQ(pixels, heldPixels);
    scene.rotation = 1;
    Run();
    EXPECT_GT(ColourCount(Ink(9)), 0u);
    auto mismatched = std::make_shared<D::VehiclePresentationSnapshot>(*vehicles);
    mismatched->sourceTick++;
    scene.vehicles = mismatched;
    EXPECT_THROW(Run(), std::invalid_argument);
}
TEST_F(VulkanWorldObjectLayerTest, LargeObjectGlyphOverlapPreservesOriginalAttachmentTraversal)
{
    std::array<std::byte, 256 * 256> remap{};
    for (uint32_t row = 0; row < 256; ++row)
        for (uint32_t ink = 0; ink < 256; ++ink)
            remap[row * 256 + ink] = std::byte(ink);
    executor.SetRemapPalette(remap);

    auto text = std::make_shared<OpenRCT2::WorldBannerPresentation>();
    text->revision = 1;
    text->banners.resize(1);
    text->objectFontText = { std::make_shared<const std::vector<uint32_t>>(std::vector<uint32_t>{ 'A', 'B' }) };
    scene.bannerTexts = G::BuildWorldBannerTextData(text);

    struct Sample
    {
        uint32_t x, y, image;
    };
    struct Case
    {
        bool vertical;
        uint32_t direction;
        std::array<Sample, 3> samples;
    };
    // Glyph advance is one pixel but each original sprite is eight pixels wide.
    // These independent expected pixels distinguish overlap ownership from
    // accidentally reversing the phrase, its images, or its raster positions.
    // Horizontal direction 0 appends A then B: B paints last. Direction 3 and
    // vertical text prepend B before A: A paints last in the original painter.
    const std::array<Case, 4> cases = { {
        { false, 0, { Sample{ 130, 114, 104 }, Sample{ 127, 111, 102 }, Sample{ 135, 119, 104 } } },
        { false, 3, { Sample{ 130, 114, 103 }, Sample{ 127, 112, 103 }, Sample{ 135, 112, 105 } } },
        { true, 0, { Sample{ 130, 114, 100 }, Sample{ 130, 111, 100 }, Sample{ 130, 119, 102 } } },
        { true, 3, { Sample{ 130, 115, 101 }, Sample{ 130, 112, 101 }, Sample{ 130, 120, 103 } } },
    } };
    for (const auto& item : cases)
    {
        SCOPED_TRACE(testing::Message() << "vertical=" << item.vertical << " direction=" << item.direction);
        auto font = std::make_shared<OpenRCT2::LargeSceneryPresentationFont>();
        font->image = 100;
        font->numImages = 2;
        font->maxWidth = 100;
        font->flags = item.vertical ? 1 : 0;
        font->glyphs['A'] = (1u << 8) | (1u << 16);
        font->glyphs['B'] = 1u | (1u << 8) | (1u << 16);
        auto& material = materials->largeScenery[0];
        material.imageBase = 100;
        material.imageCount = 32;
        material.flags = 4; // Original object's three-dimensional font.
        material.scrollingMode = 0;
        material.font = font;
        RebuildCatalog();
        auto object = Object(1);
        object.direction = item.direction;
        Objects({ object });
        Run();
        for (const auto& point : item.samples)
            EXPECT_EQ(std::to_integer<uint8_t>(pixels[point.y * extent.width + point.x]), Ink(point.image))
                << point.x << ',' << point.y;
    }
    const auto held = pixels;
    Run();
    EXPECT_EQ(pixels, held);
}

TEST_F(VulkanWorldObjectLayerTest, MoneyCoverageComposesGlyphOrderAndZeroInkOverEarlierPixels)
{
    std::array<std::byte, 256 * 256> blend{};
    for (uint32_t ink = 0; ink < 256; ++ink)
        for (uint32_t background = 0; background < 256; ++background)
            blend[ink * 256 + background] = std::byte((background + ink * 3 + 5) & 255);
    executor.SetBlendPalette(blend);
    auto run = std::make_shared<D::TextGlyphRun>();
    D::TextGlyphPiece base;
    base.width = base.height = 8;
    base.pixels.assign(64, 40);
    run->pieces.push_back(base);
    D::TextGlyphPiece hinted = base;
    hinted.kind = 1;
    hinted.ink = 7;
    hinted.hintThreshold = 64;
    constexpr std::array<uint8_t, 8> coverage{ 0, 50, 100, 180, 181, 255, 100, 255 };
    for (size_t i = 0; i < hinted.pixels.size(); ++i)
        hinted.pixels[i] = coverage[i % 8];
    run->pieces.push_back(hinted);
    D::TextGlyphPiece zeroInk = hinted;
    zeroInk.ink = 0;
    for (size_t i = 0; i < zeroInk.pixels.size(); ++i)
        zeroInk.pixels[i] = i % 8 == 6 ? 100 : (i % 8 == 7 ? 255 : 0);
    run->pieces.push_back(zeroInk);
    D::TextGlyphPiece laterOpaque = base;
    for (size_t i = 0; i < laterOpaque.pixels.size(); ++i)
        laterOpaque.pixels[i] = i % 8 == 5 ? 99 : 0;
    run->pieces.push_back(laterOpaque);
    auto catalog = std::make_shared<D::MoneyGlyphCatalog>();
    catalog->runs.push_back(run);
    auto money = std::make_shared<D::MoneyPresentationSnapshot>();
    money->sourceTick = scene.sourceTick;
    money->catalog = catalog;
    money->records = std::make_shared<const std::vector<D::MoneyPresentationRecord>>(1);
    scene.money = money;
    Run();
    // Matches the original coverage threshold and BlendColours(ink, destination),
    // deliberately using a blend table distinct from the fixture's palette filters.
    constexpr std::array<uint8_t, 8> expected{ 40, 40, 66, 66, 7, 99, 71, 0 };
    for (uint32_t y = 128; y < 136; ++y)
        for (uint32_t x = 0; x < 8; ++x)
            EXPECT_EQ(std::to_integer<uint8_t>(pixels[y * extent.width + 128 + x]), expected[x]) << x << ',' << y;
    const auto held = pixels;
    Run();
    EXPECT_EQ(pixels, held);
}
#endif
