// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
    #include "VulkanParityTestSupport.h"

    #include <cstdlib>
    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2-renderer/gpu/GpuGraphicsLookupTables.h>
    #include <openrct2-renderer/vulkan/VulkanFrameExecutor.h>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
    #include <openrct2/Context.h>
    #include <openrct2/Date.h>
    #include <openrct2/Game.h>
    #include <openrct2/GameState.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/ParkImporter.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/config/Config.h>
    #include <openrct2/drawing/Palette.h>
    #include <openrct2/drawing/PaletteIndex.h>
    #include <openrct2/drawing/PresentationScene.h>
    #include <openrct2/drawing/RenderTarget.h>
    #include <openrct2/drawing/WorldSelection.h>
    #include <openrct2/entity/EntityPresentationSnapshot.h>
    #include <openrct2/object/ObjectManager.h>
    #include <openrct2/ride/Ride.h>
    #include <openrct2/ride/Vehicle.h>
    #include <openrct2/world/Map.h>
    #include <openrct2/world/MapAnimation.h>
    #include <openrct2/world/MapPresentationSnapshot.h>
    #include <openrct2/world/tile_element/TrackElement.h>

namespace
{
    using namespace OpenRCT2;
    namespace G = OpenRCT2::Ui::Gpu;
    namespace V = OpenRCT2::Ui::Vulkan;
    namespace D = OpenRCT2::Drawing;
    namespace fs = std::filesystem;

    fs::path ContainedFile(const fs::path& root, const std::string& name)
    {
        const fs::path relative(name);
        if (relative.is_absolute() || relative.has_parent_path())
            throw std::runtime_error("Corpus files must be direct children of the corpus directory");
        return root / relative;
    }

    std::vector<std::byte> ReadBytes(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            throw std::runtime_error("Missing corpus file: " + path.string());
        const auto size = stream.tellg();
        if (size < 0 || size > 32 * 1024 * 1024)
            throw std::runtime_error("Invalid corpus file size");
        std::vector<std::byte> result(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
        if (!stream)
            throw std::runtime_error("Incomplete corpus file");
        return result;
    }

    class VulkanWorldPathArtTest : public testing::Test
    {
    protected:
        bool oldHeadless = gOpenRCT2Headless;
        bool oldNoGraphics = gOpenRCT2NoGraphics;
        bool oldSmoothing{};
        bool oldTransparentWater{};
        bool capturedConfig{};
        std::unique_ptr<IContext> context;
        void SetUp() override
        {
            if (std::getenv("OPENRCT2_PATH_ART_CORPUS") == nullptr)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required != nullptr && std::string_view(required) == "1")
                    FAIL() << "Path art qualification requires an external upstream corpus";
                GTEST_SKIP() << "No external path art corpus supplied";
            }
            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = false;
            context = CreateContext();
            auto& environment = context->GetPlatformEnvironment();
            const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
            const auto* rct1 = std::getenv("OPENRCT2_TEST_RCT1_PATH");
            const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
            ASSERT_NE(rct2, nullptr);
            ASSERT_NE(rct1, nullptr);
            ASSERT_NE(shaders, nullptr);
            environment.SetBasePath(DirBase::rct2, rct2);
            environment.SetBasePath(DirBase::rct1, rct1);
            Config::Get().general.rct1Path = rct1;
            environment.SetBasePath(DirBase::openrct2, fs::path(shaders).parent_path().parent_path().string());
            ASSERT_TRUE(context->Initialise());
            oldSmoothing = Config::Get().general.landscapeSmoothing;
            oldTransparentWater = Config::Get().general.transparentWater;
            capturedConfig = true;
            Config::Get().general.landscapeSmoothing = false;
        }
        void TearDown() override
        {
            if (capturedConfig)
            {
                Config::Get().general.landscapeSmoothing = oldSmoothing;
                Config::Get().general.transparentWater = oldTransparentWater;
            }
            context.reset();
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
        }
    };
} // namespace

// Explicit corpus lane only. Missing external evidence is a failure, never a silently skipped parity case.
TEST_F(VulkanWorldPathArtTest, ImportedOriginalArtMatchesExternalUpstreamCorpus)
{
    const auto* corpusEnv = std::getenv("OPENRCT2_PATH_ART_CORPUS");
    const auto* outputEnv = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS");
    const auto* shaderEnv = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
    ASSERT_NE(corpusEnv, nullptr);
    ASSERT_NE(outputEnv, nullptr);
    ASSERT_NE(shaderEnv, nullptr);
    const fs::path corpus = fs::absolute(corpusEnv);
    std::ifstream manifestFile(corpus / "manifest.json");
    ASSERT_TRUE(manifestFile.good());
    json_t manifest;
    manifestFile >> manifest;
    ASSERT_EQ(manifest.at("schema"), 1);
    ASSERT_EQ(manifest.at("status"), "pass");
    ASSERT_EQ(manifest.at("cases").size(), 8u);
    std::array<bool, 8> seen{};
    for (const auto& entry : manifest.at("cases"))
    {
        const auto rotation = entry.at("rotation").get<uint32_t>();
        const auto zoom = entry.at("zoom").get<uint32_t>();
        ASSERT_LT(rotation, 4u);
        ASSERT_LT(zoom, 2u);
        ASSERT_FALSE(seen[zoom * 4 + rotation]);
        seen[zoom * 4 + rotation] = true;
    }
    const auto output = fs::path(outputEnv) / "world-path-art";
    fs::create_directories(output);
    json_t reports = json_t::array();
    auto device = V::DeviceContext::CreateGraphicsOnly();
    for (const auto& entry : manifest.at("cases"))
    {
        const auto name = entry.at("name").get<std::string>();
        SCOPED_TRACE(name);
        const auto prefix = ContainedFile(output, name);
        auto importer = ParkImporter::CreateParkFile(context->GetObjectRepository());
        const auto load = importer->LoadSavedGame(ContainedFile(corpus, entry.at("park").get<std::string>()).string(), false);
        context->GetObjectManager().LoadObjects(load.RequiredObjects);
        MapAnimations::ClearAll();
        importer->Import(getGameState());
        Config::Get().general.transparentWater = entry.value("transparentWater", manifest.value("transparentWater", true));
        getGameState().entities.resetEntitySpatialIndices();
        ResetAllSpriteQuadrantPlacements();
        if (manifest.contains("mechanismPoses"))
            for (const auto& expected : manifest.at("mechanismPoses"))
            {
                SCOPED_TRACE(expected.dump());
                const auto* ride = GetRide(RideId::FromUnderlying(expected.at("ride").get<uint16_t>()));
                ASSERT_NE(ride, nullptr);
                ASSERT_EQ(uint32_t(ride->type), expected.at("rideType").get<uint32_t>());
                ASSERT_EQ(uint32_t(ride->subtype), expected.at("objectSlot").get<uint32_t>());
                ASSERT_EQ(ride->flags.has(RideFlag::onTrack), expected.at("onTrack").get<bool>());
                ASSERT_EQ(ride->flags.has(RideFlag::breakdownPending), expected.at("breakdownPending").get<bool>());
                ASSERT_EQ(uint32_t(ride->breakdownReasonPending), expected.at("breakdownReason").get<uint32_t>());
                ASSERT_EQ(uint32_t(ride->breakdownSoundModifier), expected.at("breakdownModifier").get<uint32_t>());
                ASSERT_EQ(uint32_t(ride->slideInUse), expected.at("slideInUse").get<uint32_t>());
                ASSERT_EQ(uint32_t(ride->spiralSlideProgress), expected.at("slideProgress").get<uint32_t>());
                ASSERT_EQ(uint32_t(ride->slidePeepTShirtColour), expected.at("slideColour").get<uint32_t>());
                for (const auto& pose : expected.at("vehicles"))
                {
                    const auto slot = pose.at("slot").get<size_t>();
                    ASSERT_LT(slot, std::size(ride->vehicles));
                    const auto id = EntityId::FromUnderlying(pose.at("entityId").get<uint16_t>());
                    ASSERT_EQ(ride->vehicles[slot], id);
                    const auto* vehicle = getGameState().entities.tryGetEntity<Vehicle>(id);
                    ASSERT_NE(vehicle, nullptr);
                    ASSERT_EQ(uint32_t(vehicle->flatRideAnimationFrame), pose.at("frame").get<uint32_t>());
                    ASSERT_EQ(uint32_t(vehicle->flatRideSecondaryAnimationFrame), pose.at("secondary").get<uint32_t>());
                    ASSERT_EQ(uint32_t(vehicle->orientation), pose.at("orientation").get<uint32_t>());
                    ASSERT_EQ(uint32_t(vehicle->restraints_position), pose.at("restraints").get<uint32_t>());
                    ASSERT_EQ(int32_t(vehicle->current_time), pose.at("currentTime").get<int32_t>());
                }
            }
        if (manifest.contains("ghostPatches"))
            for (const auto& patch : manifest.at("ghostPatches"))
            {
                auto* element = MapGetFirstElementAt(
                    TileCoordsXY{ patch.at("x").get<int32_t>(), patch.at("y").get<int32_t>() });
                ASSERT_NE(element, nullptr);
                const auto ordinal = patch.at("elementOrdinal").get<uint32_t>();
                for (uint32_t i = 0; i < ordinal; i++)
                {
                    ASSERT_FALSE(element->isLastForTile());
                    ++element;
                }
                ASSERT_EQ(static_cast<uint32_t>(element->getType()), patch.at("type").get<uint32_t>());
                ASSERT_EQ(element->getBaseZ(), patch.at("baseZ").get<int32_t>());
                ASSERT_EQ(element->getClearanceZ(), patch.at("clearanceZ").get<int32_t>());
                element->setGhost(true);
            }
        if (manifest.contains("photoStates"))
            for (const auto& expected : manifest.at("photoStates"))
            {
                SCOPED_TRACE(expected.dump());
                auto* element = MapGetFirstElementAt(
                    TileCoordsXY{ expected.at("x").get<int32_t>(), expected.at("y").get<int32_t>() });
                ASSERT_NE(element, nullptr);
                const TrackElement* photo = nullptr;
                do
                {
                    const auto* track = element->asTrack();
                    if (track != nullptr && track->getRideIndex().ToUnderlying() == expected.at("ride").get<uint16_t>())
                    {
                        ASSERT_EQ(photo, nullptr);
                        photo = track;
                    }
                } while (!(element++)->isLastForTile());
                ASSERT_NE(photo, nullptr);
                ASSERT_EQ(photo->getBaseZ(), expected.at("baseZ").get<int32_t>());
                ASSERT_EQ(photo->getClearanceZ(), expected.at("clearanceZ").get<int32_t>());
                ASSERT_EQ(static_cast<uint16_t>(photo->getTrackType()), expected.at("trackType").get<uint16_t>());
                ASSERT_EQ(photo->getSequenceIndex(), expected.at("sequence").get<uint8_t>());
                ASSERT_EQ(photo->getDirection(), expected.at("direction").get<uint8_t>());
                ASSERT_EQ(photo->getPhotoTimeout(), expected.at("photoTimeout").get<uint8_t>());
                ASSERT_EQ(photo->isGhost(), expected.at("ghost").get<bool>());
            }
        if (manifest.contains("objects"))
            for (const auto& expected : manifest.at("objects"))
            {
                if (!expected.contains("brakeClosed"))
                    continue;
                SCOPED_TRACE(expected.dump());
                for (const auto& placement : expected.at("placements"))
                {
                    auto* element = MapGetFirstElementAt(
                        TileCoordsXY{ placement.at("x").get<int32_t>(), placement.at("y").get<int32_t>() });
                    ASSERT_NE(element, nullptr);
                    const TrackElement* found = nullptr;
                    do
                    {
                        const auto* track = element->asTrack();
                        if (track != nullptr && track->getRideIndex().ToUnderlying() == expected.at("ride").get<uint16_t>()
                            && track->getSequenceIndex() == placement.at("sequence").get<uint8_t>())
                        {
                            ASSERT_EQ(found, nullptr);
                            found = track;
                        }
                    } while (!(element++)->isLastForTile());
                    ASSERT_NE(found, nullptr);
                    ASSERT_EQ(found->getBaseZ(), placement.at("baseZ").get<int32_t>());
                    ASSERT_EQ(static_cast<uint16_t>(found->getTrackType()), placement.at("trackType").get<uint16_t>());
                    ASSERT_EQ(found->isBrakeClosed(), expected.at("brakeClosed").get<bool>());
                    ASSERT_EQ(found->hasChain(), expected.at("chain").get<bool>());
                    ASSERT_EQ(found->isInverted(), expected.at("inverted").get<bool>());
                }
            }
        if (manifest.contains("clockHour") && manifest.contains("clockMinute"))
        {
            gRealTimeOfDay.hour = manifest.at("clockHour").get<uint8_t>();
            gRealTimeOfDay.minute = manifest.at("clockMinute").get<uint8_t>();
        }
        const auto sourceTick = getGameState().currentTicks;
        PresentationScene publication;
        ASSERT_TRUE(publication.BeginFrame(
            context->GetJobPool(), getGameState().entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
        const G::Extent extent{ entry.at("width"), entry.at("height") };
        ASSERT_GT(extent.width, 0u);
        ASSERT_GT(extent.height, 0u);
        const bool expandedTrackCorpus = manifest.value("fixture", std::string{}) == "original-track-regressions-v2";
        ASSERT_LE(extent.width, expandedTrackCorpus ? 4096u : 3840u);
        ASSERT_LE(extent.height, expandedTrackCorpus ? 2304u : 2160u);
        std::vector<D::PaletteIndex> addressSpace(static_cast<size_t>(extent.width) * extent.height);
        D::RenderTarget target{ .bits = addressSpace.data(),
                                .width = static_cast<int32_t>(extent.width),
                                .height = static_cast<int32_t>(extent.height) };
        auto cache = std::make_shared<G::TextureCache>();
        G::CommandDrawingContext drawing(target, *cache);
        G::FrameCommandStream commands;
        cache->BeginFrame();
        drawing.Begin(commands);
        // Match ViewportRender's world background, including off-map pixels.
        drawing.Clear(target, D::PaletteIndex::pi10);
        std::shared_ptr<const std::vector<uint32_t>> selection;
        const auto* selectionJson = entry.contains("selection") ? &entry.at("selection")
            : manifest.contains("selection")                    ? &manifest.at("selection")
                                                                : nullptr;
        if (selectionJson != nullptr)
        {
            const auto& value = *selectionJson;
            const auto first = value.at("first").get<std::array<int32_t, 2>>();
            const auto last = value.at("last").get<std::array<int32_t, 2>>();
            const auto arrow = value.at("arrow").get<std::array<int32_t, 3>>();
            std::vector<CoordsXY> tiles;
            for (const auto& tile : value.at("tiles"))
            {
                const auto position = tile.get<std::array<int32_t, 2>>();
                tiles.emplace_back(position[0], position[1]);
            }
            selection = std::make_shared<const std::vector<uint32_t>>(D::MakeWorldSelectionWords(
                value.at("flags").get<uint32_t>(), value.at("type").get<uint32_t>(), { first[0], first[1] },
                { last[0], last[1] }, { arrow[0], arrow[1], arrow[2] }, value.at("direction").get<uint32_t>(), tiles));
        }
        const OrthographicCamera camera{ .viewX = entry.at("viewX"),
                                         .viewY = entry.at("viewY"),
                                         .clipRight = static_cast<int32_t>(extent.width),
                                         .clipBottom = static_cast<int32_t>(extent.height),
                                         .zoom = entry.at("zoom"),
                                         .rotation = entry.at("rotation"),
                                         .nativeEntitiesAllowed = true,
                                         .viewFlags = entry.value("viewFlags", manifest.value("viewFlags", uint32_t{})),
                                         .selection = std::move(selection) };
        ASSERT_TRUE(drawing.DrawWorldSurfaceScene(target, publication.GetGeneration(), camera));
        drawing.End();
        const auto residency = cache->SealFrame(commands);
        V::FrameExecutor executor;
        executor.Initialise(device, extent, shaderEnv, 1);
        executor.SetRemapPalette(G::BuildRemapPalette());
        V::SubmissionSlots slots(device, G::kDefaultUploadRingBytes, 1);
        const auto token = slots.Begin(0, true);
        ASSERT_TRUE(token.has_value());
        const auto rendered = executor.Record(*token, commands);
        auto readback = token->upload->Allocate(addressSpace.size(), 4);
        ASSERT_TRUE(readback);
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        V::RecordImageBarrier(
            token->commandBuffer, rendered.canvas->GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        const VkBufferImageCopy copy{ .bufferOffset = readback.offset,
                                      .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                      .imageExtent = { extent.width, extent.height, 1 } };
        vkCmdCopyImageToBuffer(
            token->commandBuffer, rendered.canvas->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
        const VkMemoryBarrier host{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                                    VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(
            token->commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
            nullptr);
        slots.Submit(*token);
        executor.Commit();
        ASSERT_TRUE(slots.Wait(*token, 30'000'000'000));
        executor.CompleteTerrainStatus(0);
        token->upload->Invalidate(readback.offset, readback.size);
        const std::vector<std::byte> actual(readback.data, readback.data + addressSpace.size());
        cache->RetireFrame(residency, G::FrameRetirement::Presented);
        cache->DrainFrameRetirements();
        const auto expected = ReadBytes(ContainedFile(corpus, entry.at("referenceIndexed").get<std::string>()));
        ASSERT_EQ(expected.size(), actual.size());
        size_t differences = 0;
        for (size_t i = 0; i < actual.size(); ++i)
            differences += actual[i] != expected[i];
        std::array<std::byte, 1024> palette{};
        D::LoadPalette();
        for (size_t i = 0; i < 256; ++i)
        {
            palette[i * 4] = std::byte(D::gPalette[i].red);
            palette[i * 4 + 1] = std::byte(D::gPalette[i].green);
            palette[i * 4 + 2] = std::byte(D::gPalette[i].blue);
            palette[i * 4 + 3] = std::byte{ 255 };
        }
        VulkanParitySupport::SaveRgba(prefix.string() + ".png", VulkanParitySupport::Expand(actual, palette), extent);
        std::ofstream raw(prefix.string() + ".indexed", std::ios::binary);
        raw.write(reinterpret_cast<const char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
        reports.push_back({ { "name", name },
                            { "differentIndexedPixels", differences },
                            { "transparentWater", Config::Get().general.transparentWater },
                            { "viewFlags", entry.value("viewFlags", manifest.value("viewFlags", 0u)) },
                            { "sourceTick", sourceTick },
                            { "finalTick", getGameState().currentTicks },
                            { "width", extent.width },
                            { "height", extent.height } });
        EXPECT_EQ(differences, 0u);
        EXPECT_EQ(getGameState().currentTicks, sourceTick);
        publication.Reset(context->GetJobPool());
    }
    std::ofstream report(output / "report.json");
    report << json_t{ { "schema", 1 },
                      { "cases", reports },
                      { "scope", "Imported native GPU output versus external upstream indexed images" } }
                  .dump(2);
}
#endif
