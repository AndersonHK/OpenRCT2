// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include "../terrain-parity/NonuniformTerrainRecipe.h"
    #include "VulkanParityTestSupport.h"

    #include <cstdlib>
    #include <cstring>
    #include <map>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
    #include <openrct2-renderer/vulkan/VulkanTerrainDrawPipeline.h>
    #include <set>

namespace
{
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Terrain = Gpu::Terrain;
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    json_t Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        Require(file.good(), "Missing terrain draw corpus input");
        json_t value;
        file >> value;
        return value;
    }
    std::vector<std::byte> Hex(const std::string& hex)
    {
        Require(hex.size() % 2 == 0, "Odd corpus hex length");
        const auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9')
                return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<uint8_t>(c - 'a' + 10);
            throw std::runtime_error("Invalid corpus hex");
        };
        std::vector<std::byte> bytes(hex.size() / 2);
        for (size_t i = 0; i < bytes.size(); i++)
            bytes[i] = static_cast<std::byte>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
        return bytes;
    }
    Terrain::RetainedTerrainSnapshot Snapshot(const json_t& materials)
    {
        Terrain::RetainedTerrainSnapshot snapshot;
        snapshot.worldEpoch = 1;
        const auto recipe = NonuniformTerrainFixture::MakeTiles();
        for (uint32_t index = 0; index < Terrain::kRetainedChunkCount; index++)
        {
            auto chunk = std::make_shared<Terrain::RetainedTileChunk>();
            chunk->revision = index + 1;
            for (uint32_t local = 0; local < Terrain::kRetainedChunkSize; local++)
            {
                const auto tileIndex = index * Terrain::kRetainedChunkSize + local;
                const auto& tile = recipe[tileIndex];
                const bool border = tileIndex % 32 == 0 || tileIndex % 32 == 31 || tileIndex / 32 == 0 || tileIndex / 32 == 31;
                chunk->records[local] = {
                    tile.baseZ, tile.slope, tile.grassLength, tile.surfaceVariant, 2u + tile.edgeVariant, border ? 2u : 1u, 0, 0
                };
            }
            snapshot.chunks[index] = std::move(chunk);
        }
        Require(materials.is_array() && materials.size() == 4, "Unexpected corpus material count");
        auto table = std::make_shared<Terrain::RetainedMaterialTable>();
        table->revision = 1;
        for (const auto& value : materials)
        {
            Terrain::RetainedMaterial material{};
            material.imageBase = value.at("imageBase");
            material.imageCount = value.at("imageCount");
            material.kind = value.at("kind");
            material.selectors.entries = value.at("selectors").get<decltype(material.selectors.entries)>();
            table->records.push_back(material);
        }
        snapshot.materials = std::move(table);
        return snapshot;
    }
    struct Asset
    {
        json_t metadata;
        uint32_t index;
        int32_t x, y;
        std::vector<std::byte> pixels;
    };
    struct Corpus
    {
        std::vector<json_t> cases;
        std::map<uint32_t, Asset> assets;
        Terrain::DrawSpriteTable sprites;
        Terrain::RetainedTerrainSnapshot snapshot;
        explicit Corpus(const std::filesystem::path& folder)
        {
            const auto manifest = Read(folder / "corpus.json");
            Require(
                manifest.at("schema") == 1 && manifest.at("cases").size() == 32,
                "Expected 32 frozen terrain camera/policy cases");
            std::set<uint32_t> combinations, originals;
            for (const auto& name : manifest.at("cases"))
            {
                const auto relative = std::filesystem::path(name.get<std::string>());
                Require(
                    relative == relative.filename() && relative.extension() == ".json",
                    "Corpus case must be a contained filename");
                auto value = Read(folder / relative);
                Require(
                    value.at("schema") == 2 && value.at("landscapeSmoothing") == false && value.at("clearIndex") == 0,
                    "Unsupported corpus trace schema or policy");
                const auto rotation = value.at("rotation").get<uint32_t>();
                const auto zoom = value.at("zoom").get<uint32_t>();
                const auto transparent = value.at("transparent").get<bool>();
                const auto stable = value.at("stableSort").get<bool>();
                Require(rotation < 4 && zoom < 2, "Invalid corpus camera");
                Require(
                    combinations.insert(rotation + 4 * zoom + 8 * transparent + 16 * stable).second,
                    "Duplicate corpus camera policy");
                Require(!value.at("columns").empty(), "Empty terrain trace");
                if (cases.empty())
                    snapshot = Snapshot(value.at("materials"));
                else
                    Require(value.at("materials") == cases.front().at("materials"), "Corpus material generation changed");
                for (const auto& column : value.at("columns"))
                    for (const auto& parent : column.at("parentsBeforeArrange"))
                    {
                        originals.insert(parent.at("image").at("index").get<uint32_t>());
                        for (const auto& attached : parent.at("attached"))
                            originals.insert(attached.at("image").at("index").get<uint32_t>());
                    }
                for (const auto& metadata : value.at("observedSpriteMetadata"))
                {
                    const auto image = metadata.at("image").get<uint32_t>();
                    if (assets.contains(image))
                        Require(assets.at(image).metadata == metadata, "Corpus sprite generation changed");
                    else
                        assets.emplace(image, Asset{ metadata, 0, 0, 0, {} });
                }
                cases.push_back(std::move(value));
            }
            Require(combinations.size() == 32 && !originals.empty(), "Incomplete terrain corpus coverage");
            int32_t x = 0, y = 0, rowHeight = 0;
            uint32_t index = 0;
            for (auto& [image, asset] : assets)
            {
                static_cast<void>(image);
                const auto width = asset.metadata.at("width").get<int32_t>();
                const auto height = asset.metadata.at("height").get<int32_t>();
                Require(width > 0 && width <= 2048 && height > 0 && height <= 2048, "Unsupported corpus image dimensions");
                Require(
                    asset.metadata.at("coveredZeroPixels") == 0,
                    "Covered-zero asset needs an explicit rendering path before admission");
                if (x + width > 2048)
                {
                    x = 0;
                    y += rowHeight;
                    rowHeight = 0;
                }
                Require(y + height <= 2048, "Terrain diagnostic atlas capacity exceeded");
                asset.index = index++;
                asset.x = x;
                asset.y = y;
                x += width;
                rowHeight = std::max(rowHeight, height);
                asset.pixels = Hex(asset.metadata.at("decodedIndexedHex"));
                Require(asset.pixels.size() == static_cast<size_t>(width) * height, "Corpus asset length mismatch");
            }
            sprites.revision = 1;
            for (const auto image : originals)
            {
                const auto& original = assets.at(image).metadata;
                Terrain::DrawSpriteMetadata metadata{};
                metadata.imageIndex = image;
                metadata.width = original.at("width");
                metadata.height = original.at("height");
                metadata.xOffset = original.at("xOffset");
                metadata.yOffset = original.at("yOffset");
                const auto originalFlags = original.at("flags").get<uint32_t>();
                Require((originalFlags & ~23u) == 0, "Unsupported corpus G1 flags");
                for (int32_t zoom = 0; zoom < 2; zoom++)
                {
                    const bool linked = zoom == 1 && (originalFlags & 16u) != 0;
                    const auto linkedIndex = int64_t{ image } - original.at("zoomedOffset").get<int32_t>();
                    Require(!linked || (linkedIndex >= 0 && linkedIndex <= UINT32_MAX), "Invalid corpus linked image");
                    const auto& asset = assets.at(linked ? static_cast<uint32_t>(linkedIndex) : image);
                    const auto& variant = asset.metadata;
                    metadata.variants[zoom] = {
                        variant.at("width"),   variant.at("height"), variant.at("xOffset"),
                        variant.at("yOffset"), asset.index,          (variant.at("flags").get<uint32_t>() & 4u) != 0 ? 1u : 0u,
                        linked ? 0 : zoom,     linked ? 1 : 0
                    };
                }
                sprites.records.push_back(metadata);
            }
        }
    };
    class VulkanRetainedTerrainDrawTest : public testing::Test
    {
    protected:
        std::shared_ptr<Vulkan::DeviceContext> device;
        std::unique_ptr<Vulkan::SubmissionSlots> slots;
        Vulkan::IndexedResources resources;
        Vulkan::TerrainDrawPipeline pipeline;
        std::unique_ptr<Corpus> corpus;
        std::filesystem::path artifacts;
        void SetUp() override
        {
            const auto* corpusPath = std::getenv("OPENRCT2_TERRAIN_DRAW_CORPUS");
            const auto* emission = std::getenv("OPENRCT2_TERRAIN_EMISSION_SPV");
            const auto* columns = std::getenv("OPENRCT2_TERRAIN_COLUMNS_SPV");
            const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
            if (!corpusPath || !emission || !columns || !shaders)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required && std::string_view(required) == "1")
                    FAIL() << "Terrain drawing requires pinned corpus, emission and column shaders";
                GTEST_SKIP() << "No qualified terrain drawing corpus/shaders supplied";
            }
            corpus = std::make_unique<Corpus>(corpusPath);
            device = Vulkan::DeviceContext::CreateGraphicsOnly();
            slots = std::make_unique<Vulkan::SubmissionSlots>(device, 192 * 1024 * 1024, 2);
            resources.Initialise(*device, { 960, 640 }, false, 2, 1);
            pipeline.Initialise(*device, resources, shaders, emission, columns);
            const auto* output = std::getenv("OPENRCT2_TERRAIN_DRAW_ARTIFACTS");
            Require(output && *output, "Terrain drawing requires durable readback artifacts");
            artifacts = output;
            const std::string_view testName = testing::UnitTest::GetInstance()->current_test_info()->name();
            if (testName == "ZeroPublicationOnInvalidInputsAndCapacityOverflow")
                artifacts /= "guards";
            if (testName == "ImmutableGenerationsAbandonAndSlotReusePreserveFrozenRaster")
                artifacts /= "generations";
            std::filesystem::create_directories(artifacts);
        }
        void TearDown() override
        {
            slots.reset();
            pipeline.Dispose();
            resources.Dispose();
            device.reset();
        }
        void UploadAssets(const Vulkan::SubmissionToken& token)
        {
            // The shared fragment shader statically declares the remap sampler,
            // even though this bounded corpus uses no recolouring. Initialise its
            // contents/layout before binding the existing pipeline descriptors.
            auto remap = token.upload->Allocate(256 * 256, 4);
            Require(static_cast<bool>(remap), "Missing remap palette allocation");
            for (size_t i = 0; i < 256 * 256; i++)
                remap.data[i] = static_cast<std::byte>(i % 256);
            remap.RecordHostWrite();
            resources.RecordIndexTableUpload(token.commandBuffer, remap, false);
            resources.BeginAtlasUploads(token.commandBuffer);
            for (const auto& [image, asset] : corpus->assets)
            {
                static_cast<void>(image);
                auto upload = token.upload->Allocate(asset.pixels.size(), 4);
                Require(static_cast<bool>(upload), "Missing atlas upload allocation");
                std::memcpy(upload.data, asset.pixels.data(), asset.pixels.size());
                upload.RecordHostWrite();
                const int32_t width = asset.metadata.at("width"), height = asset.metadata.at("height");
                resources.RecordAtlasUpload(
                    token.commandBuffer, upload, 0, { asset.x, asset.y, asset.x + width, asset.y + height }, width);
                const Gpu::SpriteAssetDescriptor descriptor{ { asset.x, asset.y }, 0, 0 };
                auto descriptorUpload = token.upload->Allocate(sizeof(descriptor), 4);
                Require(static_cast<bool>(descriptorUpload), "Missing sprite descriptor allocation");
                std::memcpy(descriptorUpload.data, &descriptor, sizeof(descriptor));
                descriptorUpload.RecordHostWrite();
                resources.RecordSpriteDescriptorUpload(token.commandBuffer, descriptorUpload, asset.index);
            }
            resources.EndAtlasUploads(token.commandBuffer);
        }
        template<typename T>
        static T Load(const std::byte* bytes, size_t index)
        {
            T value{};
            std::memcpy(&value, bytes + index * sizeof(T), sizeof(T));
            return value;
        }
        void Write(const std::filesystem::path& path, const void* data, size_t size)
        {
            std::ofstream file(path, std::ios::binary);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        struct RunOptions
        {
            size_t referenceCase = SIZE_MAX;
            const Terrain::RetainedTerrainSnapshot* snapshot{};
            const Terrain::DrawSpriteTable* sprites{};
            uint32_t parentCapacity = Terrain::kDrawColumnCapacity;
            uint32_t commandCapacity = Terrain::kDrawColumnCapacity;
            uint32_t expectedError{};
            uint64_t expectedUpload = UINT64_MAX;
            uint32_t frameIndex{};
        };
        void Run(size_t sample)
        {
            Run(sample, RunOptions{});
        }
        void Run(size_t sample, const RunOptions& options)
        {
            const auto& reference = corpus->cases.at(options.referenceCase == SIZE_MAX ? sample : options.referenceCase);
            const auto& snapshot = options.snapshot ? *options.snapshot : corpus->snapshot;
            const auto& sprites = options.sprites ? *options.sprites : corpus->sprites;
            const auto target = reference.at("worldTarget").get<std::array<int32_t, 4>>();
            Require(target[2] == 960 && target[3] == 640, "Unexpected terrain corpus raster dimensions");
            Terrain::DrawCamera camera{ target[0],
                                        target[1],
                                        target[2],
                                        target[3],
                                        0,
                                        0,
                                        reference.at("rotation"),
                                        reference.at("zoom"),
                                        reference.at("transparent").get<bool>() ? 1u : 0u,
                                        reference.at("stableSort").get<bool>() ? 1u : 0u,
                                        1 };
            camera.parentCapacity = options.parentCapacity;
            camera.commandCapacity = options.commandCapacity;
            OpenRCT2::Drawing::RenderUploadTelemetry telemetry;
            auto token = slots->Begin(options.frameIndex, true, &telemetry);
            Require(token.has_value(), "No terrain drawing submission slot");
            if (sample == 0)
                UploadAssets(*token);
            resources.RecordCanvasAndDepthClear(token->commandBuffer, options.frameIndex, 0);
            const size_t pixelBytes = 960 * 640;
            const size_t parentOffset = pixelBytes;
            const size_t columnOffset = parentOffset + static_cast<size_t>(pipeline.GetParentBuffer().GetSize());
            const size_t commandOffset = columnOffset + static_cast<size_t>(pipeline.GetColumnBuffer().GetSize());
            const size_t beforeOffset = commandOffset + static_cast<size_t>(pipeline.GetCommandBuffer().GetSize());
            const size_t bytes = beforeOffset + beforeOffset - pixelBytes;
            auto readback = token->upload->Allocate(bytes, 16);
            Require(static_cast<bool>(readback), "No terrain drawing readback allocation");
            const std::array<std::pair<const Vulkan::Buffer*, size_t>, 3> buffers = {
                { { &pipeline.GetParentBuffer(), parentOffset },
                  { &pipeline.GetColumnBuffer(), columnOffset },
                  { &pipeline.GetCommandBuffer(), commandOffset } }
            };
            const VkMemoryBarrier reuse{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                         .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT };
            vkCmdPipelineBarrier(
                token->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &reuse, 0,
                nullptr, 0, nullptr);
            for (const auto& [buffer, offset] : buffers)
            {
                static_cast<void>(offset);
                vkCmdFillBuffer(token->commandBuffer, buffer->GetBuffer(), 0, buffer->GetSize(), 0xa5a5a5a5u);
            }
            const VkMemoryBarrier poison{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
            vkCmdPipelineBarrier(
                token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &poison, 0, nullptr,
                0, nullptr);
            for (const auto& [buffer, offset] : buffers)
            {
                const VkBufferCopy copy{ 0, readback.offset + beforeOffset + offset - pixelBytes, buffer->GetSize() };
                vkCmdCopyBuffer(token->commandBuffer, buffer->GetBuffer(), readback.buffer, 1, &copy);
            }
            pipeline.Record(*token, snapshot, sprites, camera, static_cast<uint32_t>(corpus->assets.size()));
            const VkMemoryBarrier transfer{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
            vkCmdPipelineBarrier(
                token->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &transfer, 0,
                nullptr, 0, nullptr);
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            Vulkan::RecordImageBarrier(
                token->commandBuffer, resources.GetIndexedCanvas(options.frameIndex).GetImage(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            const VkBufferImageCopy imageCopy{ .bufferOffset = readback.offset,
                                               .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                               .imageExtent = { 960, 640, 1 } };
            vkCmdCopyImageToBuffer(
                token->commandBuffer, resources.GetIndexedCanvas(options.frameIndex).GetImage(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &imageCopy);
            Vulkan::RecordImageBarrier(
                token->commandBuffer, resources.GetIndexedCanvas(options.frameIndex).GetImage(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_SHADER_READ_BIT);
            for (const auto& [buffer, offset] : buffers)
            {
                const VkBufferCopy copy{ 0, readback.offset + offset, buffer->GetSize() };
                vkCmdCopyBuffer(token->commandBuffer, buffer->GetBuffer(), readback.buffer, 1, &copy);
            }
            const VkMemoryBarrier host{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(
                token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
                nullptr);
            slots->Submit(*token);
            Require(slots->Wait(*token, UINT64_MAX), "Terrain drawing submission did not complete");
            resources.CommitFrameLayouts();
            token->upload->Invalidate(readback.offset, readback.size);
            // UploadRing memory prefers HOST_COHERENT, not HOST_CACHED. Read it
            // once after completion/invalidation; all guard scans and durable
            // artifacts then use the same complete owned CPU snapshot.
            const auto completedReadback = [&] {
                std::vector<std::byte> snapshotBytes(static_cast<size_t>(readback.size));
                std::memcpy(snapshotBytes.data(), readback.data, snapshotBytes.size());
                return snapshotBytes;
            }();
            const auto folder = artifacts / std::to_string(sample);
            Require(std::filesystem::create_directory(folder), "Refusing to overwrite terrain draw evidence");
            Write(folder / "parents.bin", completedReadback.data() + parentOffset, columnOffset - parentOffset);
            Write(folder / "columns.bin", completedReadback.data() + columnOffset, commandOffset - columnOffset);
            Write(folder / "commands.bin", completedReadback.data() + commandOffset, beforeOffset - commandOffset);
            Write(folder / "before.bin", completedReadback.data() + beforeOffset, bytes - beforeOffset);
            EXPECT_TRUE(std::all_of(completedReadback.data() + beforeOffset, completedReadback.data() + bytes, [](auto byte) {
                return byte == std::byte{ 0xa5 };
            }));
            const auto expected = options.expectedError == 0 ? Hex(reference.at("indexedHex"))
                                                             : std::vector<std::byte>(pixelBytes, std::byte{ 0 });
            const std::span<const std::byte> actual(completedReadback.data(), pixelBytes);
            std::array<std::byte, 1024> palette{};
            for (size_t i = 0; i < 256; i++)
            {
                palette[i * 4] = palette[i * 4 + 1] = palette[i * 4 + 2] = static_cast<std::byte>(i);
                palette[i * 4 + 3] = std::byte{ 255 };
            }
            const auto uploaded = telemetry.bytes[static_cast<size_t>(OpenRCT2::Drawing::UploadCategory::world)]
                                                 [static_cast<size_t>(OpenRCT2::Drawing::UploadMetric::bufferTransfer)];
            const uint64_t initialBytes = Terrain::kRetainedTileCount * sizeof(Terrain::RetainedTile)
                + 4 * sizeof(Terrain::RetainedMaterial) + sprites.records.size() * sizeof(Terrain::DrawSpriteMetadata);
            const auto wantedUpload = options.expectedUpload != UINT64_MAX ? options.expectedUpload
                : sample == 0                                              ? initialBytes
                                                                           : uint64_t{ 0 };
            EXPECT_EQ(uploaded, wantedUpload);
            json_t metadata = { { "rotation", camera.rotation },
                                { "zoom", camera.zoom },
                                { "transparent", camera.transparent },
                                { "stableSort", camera.stableSort },
                                { "uploadedWorldBytes", uploaded },
                                { "runtimeAdmission", false },
                                { "expectedUploadBytes", wantedUpload },
                                { "expectedGpuError", options.expectedError },
                                { "parentCapacity", camera.parentCapacity },
                                { "commandCapacity", camera.commandCapacity },
                                { "worldEpoch", snapshot.worldEpoch },
                                { "materialRevision", snapshot.materials->revision },
                                { "spriteRevision", sprites.revision },
                                { "frameIndex", options.frameIndex },
                                { "submissionGeneration", token->generation },
                                { "referenceCase", options.referenceCase == SIZE_MAX ? sample : options.referenceCase },
                                { "referenceImageLabel", options.expectedError == 0 ? "frozen-software" : "required-empty" },
                                { "resultImageLabel", "vulkan" } };
            EXPECT_EQ(
                VulkanParitySupport::CompareAndReport(
                    artifacts, std::to_string(sample), "indexed", expected, actual, 1, palette, metadata, { 960, 640 }),
                0u);
            uint32_t maximumError = 0;
            for (size_t columnIndex = 0; columnIndex < pipeline.GetColumnBuffer().GetSize() / sizeof(Terrain::DrawColumnStatus);
                 columnIndex++)
            {
                const auto column = Load<Terrain::DrawColumnStatus>(completedReadback.data() + columnOffset, columnIndex);
                if (columnIndex >= reference.at("columns").size())
                {
                    EXPECT_TRUE(std::all_of(
                        completedReadback.data() + columnOffset + columnIndex * sizeof(column),
                        completedReadback.data() + columnOffset + (columnIndex + 1) * sizeof(column),
                        [](auto byte) { return byte == std::byte{ 0xa5 }; }));
                }
                const uint32_t parentCount = columnIndex < reference.at("columns").size() ? column.parentCount : 0;
                const uint32_t commandCount = columnIndex < reference.at("columns").size() ? column.commandCount : 0;
                ASSERT_LE(parentCount, camera.parentCapacity);
                ASSERT_LE(commandCount, camera.commandCapacity);
                const auto parentStart = parentOffset
                    + (columnIndex * Terrain::kDrawColumnCapacity + parentCount) * sizeof(Terrain::DrawParent);
                const auto parentEnd = parentOffset
                    + (columnIndex + 1) * Terrain::kDrawColumnCapacity * sizeof(Terrain::DrawParent);
                const auto commandStart = commandOffset
                    + (columnIndex * Terrain::kDrawColumnCapacity + commandCount) * sizeof(Gpu::SpriteCommand);
                const auto commandEnd = commandOffset
                    + (columnIndex + 1) * Terrain::kDrawColumnCapacity * sizeof(Gpu::SpriteCommand);
                EXPECT_TRUE(
                    std::all_of(completedReadback.data() + parentStart, completedReadback.data() + parentEnd, [](auto byte) {
                        return byte == std::byte{ 0xa5 };
                    }));
                EXPECT_TRUE(
                    std::all_of(completedReadback.data() + commandStart, completedReadback.data() + commandEnd, [](auto byte) {
                        return byte == std::byte{ 0xa5 };
                    }));
                if (columnIndex >= reference.at("columns").size())
                    continue;
                maximumError = std::max(maximumError, column.error);
                EXPECT_EQ(column.instanceCount, options.expectedError == 0 ? column.commandCount : 0u);
                if (options.expectedError != 0)
                    continue;
                const auto& frozen = reference.at("columns").at(columnIndex);
                EXPECT_EQ(column.error, 0u);
                EXPECT_EQ(column.firstInstance, 0u);
                EXPECT_EQ(column.vertexCount, 4u);
                ASSERT_EQ(column.parentCount, frozen.at("parentsBeforeArrange").size());
                ASSERT_LE(column.parentCount, Terrain::kDrawColumnCapacity);
                int32_t node = column.head;
                std::set<int32_t> visited;
                for (const auto& identity : frozen.at("arrangedParentIds"))
                {
                    ASSERT_GE(node, 0);
                    ASSERT_LT(static_cast<uint32_t>(node), column.parentCount);
                    ASSERT_TRUE(visited.insert(node).second);
                    const auto parent = Load<Terrain::DrawParent>(
                        completedReadback.data() + parentOffset,
                        columnIndex * Terrain::kDrawColumnCapacity + static_cast<uint32_t>(node));
                    const auto& original = frozen.at("parentsBeforeArrange").at(identity.get<size_t>());
                    ASSERT_LT(parent.sprite, sprites.records.size());
                    EXPECT_EQ(sprites.records[parent.sprite].imageIndex, original.at("image").at("index").get<uint32_t>());
                    EXPECT_EQ(
                        (std::array{ parent.x, parent.y, parent.z, parent.xEnd, parent.yEnd, parent.zEnd }),
                        (original.at("bounds").get<std::array<int32_t, 6>>()));
                    EXPECT_EQ(
                        (std::array{ parent.screenX, parent.screenY }), (original.at("screen").get<std::array<int32_t, 2>>()));
                    EXPECT_EQ(parent.attachedCount, original.at("attached").size());
                    node = parent.next;
                }
                EXPECT_EQ(node, -1);
                EXPECT_EQ(visited.size(), column.parentCount);
            }
            EXPECT_EQ(maximumError, options.expectedError);
        }
    };
} // namespace
TEST_F(VulkanRetainedTerrainDrawTest, FrozenColumnsAndActualIndexedPixelsAcrossAllCameraPolicies)
{
    for (size_t sample = 0; sample < corpus->cases.size(); sample++)
    {
        SCOPED_TRACE(sample);
        Run(sample);
        ASSERT_FALSE(HasFatalFailure());
    }
    RecordProperty("terrainDrawSamples", static_cast<int>(corpus->cases.size()));
}
TEST_F(VulkanRetainedTerrainDrawTest, ZeroPublicationOnInvalidInputsAndCapacityOverflow)
{
    Run(0);
    ASSERT_FALSE(HasFatalFailure());
    RunOptions options;
    options.referenceCase = 0;
    options.parentCapacity = 0;
    options.expectedError = 2;
    Run(1, options);
    ASSERT_FALSE(HasFatalFailure());
    options.parentCapacity = Terrain::kDrawColumnCapacity;
    options.commandCapacity = 0;
    Run(2, options);
    ASSERT_FALSE(HasFatalFailure());
    options.parentCapacity = 1;
    options.commandCapacity = Terrain::kDrawColumnCapacity;
    Run(3, options);
    ASSERT_FALSE(HasFatalFailure());
    options.parentCapacity = Terrain::kDrawColumnCapacity;
    options.commandCapacity = 1;
    Run(4, options);
    ASSERT_FALSE(HasFatalFailure());
    auto invalid = corpus->snapshot;
    auto chunk = std::make_shared<Terrain::RetainedTileChunk>(*invalid.chunks[0]);
    chunk->revision = 100;
    chunk->records[33].baseZ = 15;
    invalid.chunks[0] = std::move(chunk);
    options.commandCapacity = Terrain::kDrawColumnCapacity;
    options.expectedError = 1;
    options.snapshot = &invalid;
    options.expectedUpload = sizeof(Terrain::RetainedTileChunk::records);
    Run(5, options);
    ASSERT_FALSE(HasFatalFailure());
    options.snapshot = nullptr;
    options.expectedError = 0;
    Run(6, options);
    ASSERT_FALSE(HasFatalFailure());
    auto missing = corpus->sprites;
    missing.revision = 2;
    missing.records.resize(1);
    options.sprites = &missing;
    options.expectedError = 3;
    options.expectedUpload = sizeof(Terrain::DrawSpriteMetadata);
    Run(7, options);
    ASSERT_FALSE(HasFatalFailure());
    options.sprites = nullptr;
    options.expectedError = 0;
    options.expectedUpload = corpus->sprites.records.size() * sizeof(Terrain::DrawSpriteMetadata);
    Run(8, options);
    ASSERT_FALSE(HasFatalFailure());
    // Invalid CPU contracts must reject before publication; abandoning their
    // command buffer must not poison the next accepted generation.
    auto token = slots->Begin(0, true);
    ASSERT_TRUE(token.has_value());
    Terrain::DrawCamera camera{ 0, 0, 960, 640, 0, 0, 0, 0, 0, 0, 1 };
    camera.parentCapacity = Terrain::kDrawColumnCapacity + 1;
    EXPECT_THROW(
        pipeline.Record(*token, corpus->snapshot, corpus->sprites, camera, static_cast<uint32_t>(corpus->assets.size())),
        std::invalid_argument);
    camera.parentCapacity = Terrain::kDrawColumnCapacity;
    camera.commandCapacity = Terrain::kDrawColumnCapacity + 1;
    EXPECT_THROW(
        pipeline.Record(*token, corpus->snapshot, corpus->sprites, camera, static_cast<uint32_t>(corpus->assets.size())),
        std::invalid_argument);
    slots->Abandon(*token);
    pipeline.DiscardPendingUploads();
    options.expectedUpload = Terrain::kRetainedTileCount * sizeof(Terrain::RetainedTile) + 4 * sizeof(Terrain::RetainedMaterial)
        + corpus->sprites.records.size() * sizeof(Terrain::DrawSpriteMetadata);
    Run(9, options);
    ASSERT_FALSE(HasFatalFailure());
    RecordProperty("terrainDrawSamples", 10);
    RecordProperty("cpuRejections", 2);
}
TEST_F(VulkanRetainedTerrainDrawTest, ImmutableGenerationsAbandonAndSlotReusePreserveFrozenRaster)
{
    Run(0);
    ASSERT_FALSE(HasFatalFailure());
    auto snapshot = corpus->snapshot;
    auto sprites = corpus->sprites;
    RunOptions options;
    options.referenceCase = 0;
    options.snapshot = &snapshot;
    options.sprites = &sprites;
    auto chunk = std::make_shared<Terrain::RetainedTileChunk>(*snapshot.chunks[0]);
    chunk->revision = 100;
    snapshot.chunks[0] = std::move(chunk);
    options.expectedUpload = sizeof(Terrain::RetainedTileChunk::records);
    options.frameIndex = 1;
    Run(1, options);
    ASSERT_FALSE(HasFatalFailure());
    auto materials = std::make_shared<Terrain::RetainedMaterialTable>(*snapshot.materials);
    materials->revision = 2;
    snapshot.materials = std::move(materials);
    options.expectedUpload = 4 * sizeof(Terrain::RetainedMaterial);
    options.frameIndex = 0;
    Run(2, options);
    ASSERT_FALSE(HasFatalFailure());
    sprites.revision = 2;
    options.expectedUpload = sprites.records.size() * sizeof(Terrain::DrawSpriteMetadata);
    options.frameIndex = 1;
    Run(3, options);
    ASSERT_FALSE(HasFatalFailure());
    snapshot.worldEpoch = 2;
    const uint64_t allBytes = Terrain::kRetainedTileCount * sizeof(Terrain::RetainedTile)
        + 4 * sizeof(Terrain::RetainedMaterial) + sprites.records.size() * sizeof(Terrain::DrawSpriteMetadata);
    options.expectedUpload = allBytes;
    options.frameIndex = 0;
    Run(4, options);
    ASSERT_FALSE(HasFatalFailure());
    auto token = slots->Begin(1, true);
    ASSERT_TRUE(token.has_value());
    const auto target = corpus->cases.front().at("worldTarget").get<std::array<int32_t, 4>>();
    Terrain::DrawCamera camera{ target[0], target[1], target[2], target[3], 0, 0, 0, 0, 0, 0, 1 };
    resources.RecordCanvasAndDepthClear(token->commandBuffer, 1, 0);
    snapshot.worldEpoch = 3;
    pipeline.Record(*token, snapshot, sprites, camera, static_cast<uint32_t>(corpus->assets.size()));
    slots->Abandon(*token);
    resources.DiscardFrameLayouts(1);
    pipeline.DiscardPendingUploads();
    options.frameIndex = 1;
    Run(5, options);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_THROW(slots->Submit(*token), std::logic_error);
    options.expectedUpload = 0;
    options.frameIndex = 0;
    options.referenceCase = 31;
    Run(6, options);
    ASSERT_FALSE(HasFatalFailure());
    options.frameIndex = 1;
    options.referenceCase = 0;
    Run(7, options);
    ASSERT_FALSE(HasFatalFailure());
    RecordProperty("terrainDrawSamples", 8);
    RecordProperty("abandonedRecordings", 1);
}
#endif
