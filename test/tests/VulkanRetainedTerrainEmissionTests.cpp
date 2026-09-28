/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
#include "../terrain-parity/NonuniformTerrainRecipe.h"
#include <openrct2-renderer/gpu/RetainedTerrain.h>
#include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
#include <openrct2-renderer/vulkan/VulkanTerrainEmissionPipeline.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/TerrainEdgeObject.h>
#include <openrct2/object/TerrainSurfaceObject.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/paint/tile_element/Paint.TileElement.h>
#include <openrct2/profiling/Profiling.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapSelection.h>
#include <openrct2/world/tile_element/Slope.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>

namespace Terrain = OpenRCT2::Ui::Gpu::Terrain;
namespace Vulkan = OpenRCT2::Ui::Vulkan;

namespace RetainedFrozenOracle
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    struct CapturedParent { uint32_t image; CoordsXYZ offset; CoordsXYZ bounds; };
    static thread_local std::vector<CapturedParent> parents;
    static PaintStruct* CaptureParent(PaintSession& session, ImageId image, const CoordsXYZ& offset, const BoundBoxXYZ& box)
    {
        EXPECT_EQ(box.offset, offset);
        parents.push_back({ image.GetIndex(), offset, box.length });
        session.LastAttachedPS = nullptr;
        return session.AllocateNormalPaintEntry();
    }
    static PaintStruct* CaptureParent(PaintSession& session, ImageId image, const CoordsXYZ& offset, const CoordsXYZ& bounds)
    { return CaptureParent(session, image, offset, BoundBoxXYZ{ offset, bounds }); }
    static bool CaptureAttached(PaintSession& session, ImageId image, int32_t x, int32_t y);

    // Same byte-pinned frozen source, compiled in a separate namespace. No new
    // terrain shader/CPU rule helper contributes expected edge or shape values.
#include "../terrain-parity/FrozenTerrainEdgeOracle.inc"
    static bool CaptureAttached(PaintSession& session, ImageId image, int32_t x, int32_t y)
    { return RetainedFrozenOracle::PaintAttachToPreviousPS(session, image, x, y); }

    static TileDescriptor Describe(const Terrain::RetainedTile& tile, int rotation, const TileElement* element)
    {
        SurfaceElement surface{};
        surface.setSlope(static_cast<uint8_t>(tile.slope));
        const auto relative = ViewportSurfacePaintSetupGetRelativeSlope(surface, rotation);
        auto corners = GetSlopeRelativeCornerHeights(relative);
        corners.top += tile.baseZ / 16; corners.right += tile.baseZ / 16;
        corners.bottom += tile.baseZ / 16; corners.left += tile.baseZ / 16;
        return { {}, element, nullptr, relative, corners };
    }
}

namespace
{
    using Snapshot = Terrain::RetainedTerrainSnapshot;
    using Primitive = Terrain::RetainedTerrainPrimitive;
    using Status = Terrain::RetainedTerrainStatus;
    constexpr uint64_t kInitialUpload = Terrain::kRetainedTileCount * sizeof(Terrain::RetainedTile)
        + 4 * sizeof(Terrain::RetainedMaterial);
    constexpr uint64_t kChunkUpload = Terrain::kRetainedChunkSize * sizeof(Terrain::RetainedTile);
    constexpr uint32_t kTestCapacity = 8192;

    const Terrain::RetainedTile& TileAt(const Snapshot& snapshot, uint32_t index)
    { return snapshot.chunks[index / Terrain::kRetainedChunkSize]->records[index % Terrain::kRetainedChunkSize]; }

    Snapshot Recipe()
    {
        Snapshot snapshot;
        snapshot.worldEpoch = 1;
        const auto recipe = NonuniformTerrainFixture::MakeTiles();
        for (uint32_t chunkIndex = 0; chunkIndex < snapshot.chunks.size(); chunkIndex++)
        {
            auto chunk = std::make_shared<Terrain::RetainedTileChunk>();
            chunk->revision = chunkIndex + 1;
            for (uint32_t local = 0; local < Terrain::kRetainedChunkSize; local++)
            {
                const uint32_t index = chunkIndex * Terrain::kRetainedChunkSize + local;
                const auto& tile = recipe[index];
                const bool border = index % 32 == 0 || index % 32 == 31 || index / 32 == 0 || index / 32 == 31;
                chunk->records[local] = { tile.baseZ, tile.slope, tile.grassLength, tile.surfaceVariant,
                    2u + tile.edgeVariant, border ? 2u : 1u, 0, 0 };
            }
            snapshot.chunks[chunkIndex] = std::move(chunk);
        }
        auto materials = std::make_shared<Terrain::RetainedMaterialTable>();
        materials->revision = 1;
        const std::array<Terrain::MaterialSpecial, 2> specials = { {
            { 4, 3, 2, 1 }, { 5, 6, Terrain::kAny, Terrain::kAny }
        } };
        for (uint32_t index = 0; index < 4; index++)
            materials->records.push_back({ 100000u * (index + 1), index < 2 ? 19u * 8 : 36u,
                index < 2 ? 1u : 2u, 0, Terrain::CompileMaterialLookup(index, specials) });
        snapshot.materials = std::move(materials);
        return snapshot;
    }

    // CPU work below belongs exclusively to the independent diagnostic oracle.
    std::vector<Primitive> Expected(const Snapshot& snapshot, uint32_t rotation, int32_t zoom, bool transparent,
        Status& status, uint32_t defaultEntryOffset = 0)
    {
        std::vector<Primitive> result;
        status = {}; status.firstInvalidTile = UINT32_MAX;
        auto session = std::make_unique<PaintSession>();
        OpenRCT2::TileElement element{};
        OpenRCT2::TerrainEdgeObject edgeObject;
        std::array<OpenRCT2::TerrainSurfaceObject, 2> surfaces;
        for (uint32_t index = 0; index < surfaces.size(); index++)
        {
            surfaces[index].EntryBaseImageId = snapshot.materials->records[index].imageBase;
            surfaces[index].DefaultEntry = index + defaultEntryOffset;
            surfaces[index].Colour = OpenRCT2::Drawing::kColourNull;
            surfaces[index].SpecialEntries.push_back({ 4, 3, 2, 1 });
            surfaces[index].SpecialEntries.push_back({ 5, 6, 255, 255 });
        }
        for (uint32_t index = 0; index < Terrain::kRetainedTileCount; index++)
        {
            const auto& tile = TileAt(snapshot, index);
            const bool border = index % 32 == 0 || index % 32 == 31 || index / 32 == 0 || index / 32 == 31;
            if (border && transparent) continue;
            const auto baseIndex = static_cast<uint32_t>(result.size());
            Primitive base{ index, 3123u, border ? 3u : 0u, baseIndex,
                0, 0, border ? 16 : tile.baseZ, 0, 32, 32, -1, rotation, 0, -1, 0, 0 };
            if (border) { result.push_back(base); status.borderCount++; continue; }
            const auto own = RetainedFrozenOracle::Describe(tile, rotation, &element);
            base.imageIndex = surfaces[tile.surfaceMaterial].GetImageId(
                { static_cast<int32_t>(index % 32) * 32, static_cast<int32_t>(index / 32) * 32 },
                static_cast<uint8_t>(zoom > 0 ? 255 : tile.grass), static_cast<uint8_t>(rotation),
                RetainedFrozenOracle::Byte97B444[own.slope], false, false).GetIndex();
            result.push_back(base); status.baseCount++;
            edgeObject.BaseImageId = snapshot.materials->records[tile.edgeMaterial].imageBase;
            session->paintEntries.clear();
            auto* frozenBase = session->AllocateNormalPaintEntry();
            RetainedFrozenOracle::parents.clear();
            std::array<RetainedFrozenOracle::TileDescriptor, 4> neighbours{};
            for (size_t edge = 0; edge < 4; edge++)
            {
                const auto offset = RetainedFrozenOracle::kNeighbouringTileCoordOffsets[edge][rotation];
                const auto x = static_cast<int32_t>(index % 32) + offset.x / 32;
                const auto y = static_cast<int32_t>(index / 32) + offset.y / 32;
                if (x >= 0 && y >= 0 && x < 32 && y < 32)
                    neighbours[edge] = RetainedFrozenOracle::Describe(TileAt(snapshot, y * 32 + x), rotation, &element);
            }
            RetainedFrozenOracle::InvokeFrozenSideSequence(*session, static_cast<uint16_t>(tile.baseZ),
                &edgeObject, own, neighbours.data());
            uint32_t ordinal = 1;
            for (auto* attached = frozenBase->Attached; attached != nullptr; attached = attached->NextEntry)
            {
                auto output = base;
                output.imageIndex = attached->image_id.GetIndex(); output.kind = 1;
                output.attachedY = attached->RelativePos.y;
                EXPECT_EQ(attached->RelativePos.x, 0);
                output.boundsX = 0; output.boundsY = 0; output.boundsZ = 0;
                output.localOrdinal = ordinal++;
                output.edge = output.imageIndex - edgeObject.BaseImageId >= 33u ? 2 : 3;
                result.push_back(output); status.rearCount++;
            }
            for (const auto& parent : RetainedFrozenOracle::parents)
            {
                auto output = base;
                output.imageIndex = parent.image; output.kind = 2;
                output.parentIndex = static_cast<uint32_t>(result.size());
                output.offsetX = parent.offset.x; output.offsetY = parent.offset.y; output.offsetZ = parent.offset.z;
                output.boundsX = parent.bounds.x; output.boundsY = parent.bounds.y; output.boundsZ = parent.bounds.z;
                output.localOrdinal = ordinal++;
                output.edge = parent.image - edgeObject.BaseImageId >= 5u ? 1 : 0;
                result.push_back(output); status.frontCount++;
            }
        }
        status.primitiveCount = status.requiredCapacity = static_cast<uint32_t>(result.size());
        return result;
    }

    class VulkanRetainedTerrainEmissionTest : public testing::Test
    {
    protected:
        std::shared_ptr<Vulkan::DeviceContext> context;
        std::unique_ptr<Vulkan::SubmissionSlots> slots;
        std::unique_ptr<Vulkan::TerrainEmissionPipeline> pipeline;
        std::filesystem::path artifacts;
        uint32_t sample{};
        void SetUp() override
        {
            const auto* shader = std::getenv("OPENRCT2_TERRAIN_EMISSION_SPV");
            if (shader == nullptr || *shader == 0)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required != nullptr && std::string_view(required) == "1")
                    FAIL() << "Required retained terrain test needs receipt-pinned OPENRCT2_TERRAIN_EMISSION_SPV";
                GTEST_SKIP() << "No retained terrain emission shader supplied";
            }
            context = Vulkan::DeviceContext::CreateGraphicsOnly();
            slots = std::make_unique<Vulkan::SubmissionSlots>(context, 8 * 1024 * 1024, 1);
            pipeline = std::make_unique<Vulkan::TerrainEmissionPipeline>();
            pipeline->Initialise(*context, shader, kTestCapacity);
            if (const auto* path = std::getenv("OPENRCT2_TERRAIN_EMISSION_ARTIFACTS"))
            {
                artifacts = path;
                artifacts /= testing::UnitTest::GetInstance()->current_test_info()->name();
                std::filesystem::create_directories(artifacts);
            }
        }
        void TearDown() override
        {
            RecordProperty("retainedTerrainSamples", static_cast<int>(sample));
            // Wait helpers below finish every submission; slot owner is retired before
            // pipeline resources if a fatal assertion interrupted a diagnostic step.
            slots.reset(); pipeline.reset(); context.reset();
        }
        struct Capture { Status status; std::vector<std::byte> before; std::vector<std::byte> after; uint64_t uploaded; };
        void Write(const std::filesystem::path& folder, const char* name, const void* bytes, size_t size)
        {
            std::ofstream file(folder / name, std::ios::binary);
            file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
            if (!file) throw std::runtime_error("Retained terrain evidence write failed");
        }
        Capture Run(const Snapshot& snapshot, uint32_t rotation, int32_t zoom, bool transparent,
            uint32_t capacity, std::span<const Primitive> expected, uint32_t error = 0)
        {
            OpenRCT2::Drawing::RenderUploadTelemetry telemetry;
            auto token = slots->Begin(0, true, &telemetry);
            if (!token) throw std::runtime_error("No retained terrain submission slot");
            const size_t bytes = static_cast<size_t>(pipeline->GetPrimitiveBuffer().GetSize());
            // Every diagnostic clears the entire output to poison. Keep complete
            // before/after bytes, including every unused capacity record.
            Capture capture{ {}, {}, {}, 0 };
            const size_t beforeOffset = sizeof(Status) + bytes;
            const size_t tilesOffset = beforeOffset + bytes;
            std::array<Terrain::RetainedTile, Terrain::kRetainedTileCount> tiles{};
            for (uint32_t index = 0; index < tiles.size(); index++) tiles[index] = TileAt(snapshot, index);
            const size_t materialsOffset = tilesOffset + sizeof(tiles);
            const size_t materialBytes = snapshot.materials->records.size() * sizeof(Terrain::RetainedMaterial);
            auto readback = token->upload->Allocate(materialsOffset + materialBytes, 16);
            if (!readback) throw std::runtime_error("No retained terrain readback allocation");
            const VkMemoryBarrier previous{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT };
            vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 1, &previous, 0, nullptr, 0, nullptr);
            vkCmdFillBuffer(token->commandBuffer, pipeline->GetPrimitiveBuffer().GetBuffer(), 0, bytes, 0xa5a5a5a5u);
            const VkMemoryBarrier filled{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
            vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 1, &filled, 0, nullptr, 0, nullptr);
            const VkBufferCopy copyBefore{ 0, readback.offset + beforeOffset, bytes };
            vkCmdCopyBuffer(token->commandBuffer, pipeline->GetPrimitiveBuffer().GetBuffer(), readback.buffer, 1, &copyBefore);
            pipeline->Record(*token, snapshot, rotation, zoom, transparent, capacity);
            const VkMemoryBarrier diagnosticRead{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
            vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 1, &diagnosticRead, 0, nullptr, 0, nullptr);
            const VkBufferCopy copyStatus{ 0, readback.offset, sizeof(Status) };
            const VkBufferCopy copyOutput{ 0, readback.offset + sizeof(Status), bytes };
            vkCmdCopyBuffer(token->commandBuffer, pipeline->GetStatusBuffer().GetBuffer(), readback.buffer, 1, &copyStatus);
            vkCmdCopyBuffer(token->commandBuffer, pipeline->GetPrimitiveBuffer().GetBuffer(), readback.buffer, 1, &copyOutput);
            const VkBufferCopy copyTiles{ 0, readback.offset + tilesOffset, sizeof(tiles) };
            const VkBufferCopy copyMaterials{ 0, readback.offset + materialsOffset, materialBytes };
            vkCmdCopyBuffer(token->commandBuffer, pipeline->GetTileBuffer().GetBuffer(), readback.buffer, 1, &copyTiles);
            vkCmdCopyBuffer(token->commandBuffer, pipeline->GetMaterialBuffer().GetBuffer(), readback.buffer, 1, &copyMaterials);
            const VkMemoryBarrier host{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 1, &host, 0, nullptr, 0, nullptr);
            slots->Submit(*token);
            if (!slots->Wait(*token, UINT64_MAX)) throw std::runtime_error("Terrain submission did not complete");
            token->upload->Invalidate(readback.offset, readback.size);
            std::memcpy(&capture.status, readback.data, sizeof(Status));
            capture.after.resize(bytes);
            std::memcpy(capture.after.data(), readback.data + sizeof(Status), bytes);
            capture.before.resize(bytes);
            std::memcpy(capture.before.data(), readback.data + beforeOffset, bytes);
            EXPECT_TRUE(std::all_of(capture.before.begin(), capture.before.end(), [](auto b) { return b == std::byte{ 0xa5 }; }));
            EXPECT_EQ(std::memcmp(readback.data + tilesOffset, tiles.data(), sizeof(tiles)), 0);
            EXPECT_EQ(std::memcmp(readback.data + materialsOffset, snapshot.materials->records.data(), materialBytes), 0);
            capture.uploaded = telemetry.bytes[static_cast<size_t>(OpenRCT2::Drawing::UploadCategory::world)]
                [static_cast<size_t>(OpenRCT2::Drawing::UploadMetric::bufferTransfer)];
            EXPECT_EQ(capture.uploaded, telemetry.bytes[static_cast<size_t>(OpenRCT2::Drawing::UploadCategory::world)]
                [static_cast<size_t>(OpenRCT2::Drawing::UploadMetric::hostWritten)]);
            EXPECT_EQ(capture.status.error, error);
            EXPECT_EQ(capture.status.drawingQualified, 0u);
            EXPECT_EQ(capture.status.primitiveCount, error == 0 ? static_cast<uint32_t>(expected.size()) : 0u);
            if (error == 0)
            {
                // Decode into an aligned typed value, rather than aliasing arbitrary mapped bytes.
                for (size_t index = 0; index < expected.size(); index++)
                {
                    Primitive actual{};
                    std::memcpy(&actual, capture.after.data() + index * sizeof(Primitive), sizeof(actual));
                    EXPECT_EQ(std::memcmp(&actual, &expected[index], sizeof(actual)), 0) << "primitive " << index;
                }
            }
            const size_t guardStart = error == 0 ? expected.size_bytes() : 0;
            EXPECT_TRUE(std::equal(capture.after.begin() + guardStart, capture.after.end(), capture.before.begin() + guardStart));
            if (!artifacts.empty())
            {
                const auto folder = artifacts / std::to_string(sample);
                if (!std::filesystem::create_directory(folder)) throw std::runtime_error("Terrain evidence directory exists");
                Write(folder, "before.bin", capture.before.data(), capture.before.size());
                Write(folder, "after.bin", capture.after.data(), capture.after.size());
                Write(folder, "expected.bin", expected.data(), expected.size_bytes());
                Write(folder, "status.bin", &capture.status, sizeof(capture.status));
                Write(folder, "tiles.bin", tiles.data(), sizeof(tiles));
                Write(folder, "materials.bin", snapshot.materials->records.data(), snapshot.materials->records.size() * sizeof(Terrain::RetainedMaterial));
                Write(folder, "gpu-tiles.bin", readback.data + tilesOffset, sizeof(tiles));
                Write(folder, "gpu-materials.bin", readback.data + materialsOffset, materialBytes);
                std::ofstream metadata(folder / "sample.json");
                metadata << "{\"rotation\":" << rotation << ",\"zoom\":" << zoom << ",\"transparent\":" << transparent
                    << ",\"capacity\":" << capacity << ",\"worldEpoch\":" << snapshot.worldEpoch
                    << ",\"materialRevision\":" << snapshot.materials->revision << ",\"uploadedWorldBytes\":" << capture.uploaded
                    << ",\"expectedError\":" << error << ",\"expectedPrimitiveCount\":" << expected.size() << ",\"chunkRevisions\":[";
                for (size_t index = 0; index < snapshot.chunks.size(); index++)
                    metadata << (index == 0 ? "" : ",") << snapshot.chunks[index]->revision;
                metadata << "]}\n";
                if (!metadata) throw std::runtime_error("Terrain metadata write failed");
            }
            sample++;
            return capture;
        }
    };
}

TEST_F(VulkanRetainedTerrainEmissionTest, RecipeDescriptorsAndRetentionMatchFrozenTrace)
{
    const auto original = Recipe();
    for (bool transparent : { false, true })
    for (int32_t zoom : { 0, 1 })
    for (uint32_t rotation = 0; rotation < 4; rotation++)
    {
        Status expectedStatus{};
        const auto expected = Expected(original, rotation, zoom, transparent, expectedStatus);
        ASSERT_GT(expectedStatus.frontCount, 0u); ASSERT_GT(expectedStatus.rearCount, 0u);
        const auto capture = Run(original, rotation, zoom, transparent, kTestCapacity, expected);
        EXPECT_EQ(std::memcmp(&capture.status, &expectedStatus, sizeof(Status)), 0);
        EXPECT_EQ(capture.uploaded, sample == 1 ? kInitialUpload : uint64_t{ 0 });
        ASSERT_FALSE(HasFatalFailure());
    }
    Status expectedStatus{};
    const auto expected = Expected(original, 0, 0, false, expectedStatus);
    const auto heldCapture = Run(original, 0, 0, false, expectedStatus.primitiveCount, expected);
    EXPECT_EQ(heldCapture.uploaded, uint64_t{ 0 });
    auto edited = original;
    auto chunk = std::make_shared<Terrain::RetainedTileChunk>(*edited.chunks[1]);
    chunk->revision = 101;
    chunk->records[80].baseZ = 64; chunk->records[80].slope = 7;
    edited.chunks[1] = std::move(chunk);
    const auto editExpected = Expected(edited, 0, 0, false, expectedStatus);
    const auto editCapture = Run(edited, 0, 0, false, kTestCapacity, editExpected);
    EXPECT_EQ(editCapture.uploaded, kChunkUpload);
    EXPECT_NE(heldCapture.after, editCapture.after);
    EXPECT_NE(TileAt(original, 336).baseZ, TileAt(edited, 336).baseZ);
    const auto restored = Run(original, 0, 0, false, kTestCapacity, expected);
    EXPECT_EQ(restored.uploaded, kChunkUpload);
    EXPECT_EQ(restored.after, heldCapture.after); // Old snapshot/output remained immutable.
    auto materialEdit = original;
    auto materials = std::make_shared<Terrain::RetainedMaterialTable>(*original.materials);
    materials->revision = 2;
    const std::array<Terrain::MaterialSpecial, 2> specials = { { {4,3,2,1}, {5,6,255,255} } };
    for (uint32_t index = 0; index < 2; index++) materials->records[index].selectors = Terrain::CompileMaterialLookup(index + 1, specials);
    materialEdit.materials = std::move(materials);
    const auto materialExpected = Expected(materialEdit, 0, 0, false, expectedStatus, 1);
    EXPECT_EQ(Run(materialEdit, 0, 0, false, kTestCapacity, materialExpected).uploaded,
        uint64_t{ 4 * sizeof(Terrain::RetainedMaterial) });
    auto reset = original; reset.worldEpoch = 2;
    EXPECT_EQ(Run(reset, 0, 0, false, kTestCapacity, expected).uploaded, kInitialUpload);
    // Record a newer generation, abandon it, then re-request that same revision.
    auto token = slots->Begin(0, true);
    ASSERT_TRUE(token);
    pipeline->Record(*token, edited, 0, 0, false, kTestCapacity);
    slots->Abandon(*token); pipeline->DiscardPendingUploads();
    EXPECT_EQ(Run(edited, 0, 0, false, kTestCapacity, editExpected).uploaded, kInitialUpload);
}

TEST_F(VulkanRetainedTerrainEmissionTest, InvalidStateAndCapacityEmitNothing)
{
    auto snapshot = Recipe();
    Status status{};
    const auto expected = Expected(snapshot, 0, 0, false, status);
    for (uint32_t capacity : { 0u, status.primitiveCount - 1 })
    {
        const auto capture = Run(snapshot, 0, 0, false, capacity, expected, 2);
        EXPECT_EQ(capture.status.requiredCapacity, status.primitiveCount);
    }
    auto invalid = snapshot;
    auto chunk = std::make_shared<Terrain::RetainedTileChunk>(*snapshot.chunks[1]);
    chunk->revision = 100; chunk->records[80].slope = 16;
    invalid.chunks[1] = std::move(chunk);
    EXPECT_EQ(Run(invalid, 0, 0, false, kTestCapacity, expected, 1).status.firstInvalidTile, 336u);
    auto badMaterials = std::make_shared<Terrain::RetainedMaterialTable>(*snapshot.materials);
    badMaterials->revision = 100; badMaterials->records[0].selectors.entries.fill(UINT32_MAX);
    invalid = snapshot; invalid.materials = std::move(badMaterials);
    EXPECT_EQ(Run(invalid, 0, 0, false, kTestCapacity, expected, 1).status.firstInvalidTile, 33u);
    EXPECT_EQ(Run(snapshot, 0, 0, false, status.primitiveCount, expected).status.error, 0u);
}
#endif
