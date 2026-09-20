// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#include <stdexcept>
#include <cstdlib>
#include <string_view>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/drawing/Image.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2-renderer/gpu/TerrainPresentationBridge.h>
#include <openrct2/world/MapPresentationSnapshot.h>

using namespace OpenRCT2;
namespace Terrain = OpenRCT2::Ui::Gpu::Terrain;

namespace
{
    std::shared_ptr<const TerrainPresentationMaterials> Materials()
    {
        auto result = std::make_shared<TerrainPresentationMaterials>();
        result->revision = GetTerrainObjectRevision();
        result->surfaces[2].supported = true;
        result->surfaces[2].imageBase = 1000;
        result->surfaces[2].imageCount = 19;
        result->edges[3].supported = true;
        result->edges[3].imageBase = 2000;
        result->edges[3].imageCount = 37;
        return result;
    }
    MapPresentationTileChange Tile(uint32_t i)
    {
        MapPresentationTileChange change;
        change.index = (i / 32) * kMaximumMapSizeTechnical + i % 32;
        change.surfaceIndex = i;
        change.surface.valid = 1;
        change.surface.baseZ = 16;
        change.surface.terrain = {16, 2, 3, 0, 0, 1};
        return change;
    }
    MapPresentationChangeBatch Initial()
    {
        MapPresentationChangeBatch batch;
        batch.epoch = 314;
        batch.reset = true;
        batch.surfaceWidth = batch.surfaceHeight = 32;
        batch.terrainMaterials = Materials();
        for (uint32_t i = 0; i < 1024; i++) batch.changes.push_back(Tile(i));
        return batch;
    }
}

TEST(TerrainPresentationBridgeTest, DirtyChunkPublishesNewFactsAndRetainsOldGeneration)
{
    MapPresentationSnapshot source;
    auto batch = Initial();
    source.Apply(batch);
    ASSERT_TRUE(source.HasBoundedTerrainFacts());
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    const auto old = bridge.GetSnapshot();
    ASSERT_TRUE(bridge.Update(source));
    EXPECT_EQ(old.chunks, bridge.GetSnapshot().chunks);
    EXPECT_EQ(old.materials, bridge.GetSnapshot().materials);
    batch.reset = false;
    batch.changes = {Tile(17)};
    batch.changes[0].surface.terrain.baseZ = 48;
    batch.changes[0].surface.terrain.slope = 9;
    source.Apply(batch);
    ASSERT_TRUE(bridge.Update(source));
    EXPECT_EQ(old.chunks[0]->records[17].baseZ, 16);
    EXPECT_EQ(bridge.GetSnapshot().chunks[0]->records[17].baseZ, 48);
    EXPECT_EQ(bridge.GetSnapshot().chunks[0]->records[17].slope, 9);
    EXPECT_NE(old.chunks[0], bridge.GetSnapshot().chunks[0]);
    for (size_t i = 1; i < 4; i++) EXPECT_EQ(old.chunks[i], bridge.GetSnapshot().chunks[i]);
    EXPECT_EQ(old.materials, bridge.GetSnapshot().materials);
}

TEST(TerrainPresentationBridgeTest, IdenticalObjectReloadRejectsOldPublicationAndRevisesMaterials)
{
    MapPresentationSnapshot source;
    auto batch = Initial(); source.Apply(batch);
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    const auto old = bridge.GetSnapshot();
    AdvanceTerrainObjectRevision();
    EXPECT_FALSE(bridge.Update(source));
    batch.reset = false; batch.changes.clear(); batch.terrainMaterials = Materials();
    source.Apply(batch);
    ASSERT_TRUE(bridge.Update(source));
    EXPECT_NE(old.materials->revision, bridge.GetSnapshot().materials->revision);
    EXPECT_EQ(old.chunks, bridge.GetSnapshot().chunks);
    EXPECT_EQ(old.materials->records[0].imageBase, bridge.GetSnapshot().materials->records[0].imageBase);
}

TEST(TerrainPresentationBridgeTest, UnsupportedTileDeclinesAndRestoredTileReopensFactsOnly)
{
    EXPECT_FALSE(Terrain::kRuntimeAdmission);
    MapPresentationSnapshot source;
    auto batch = Initial(); source.Apply(batch);
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    batch.reset = false; batch.changes = {Tile(513)};
    batch.changes[0].surface.terrain.kind = 0;
    source.Apply(batch);
    EXPECT_FALSE(source.HasBoundedTerrainFacts());
    EXPECT_FALSE(bridge.Update(source));
    batch.changes = {Tile(513)};
    source.Apply(batch);
    EXPECT_TRUE(source.HasBoundedTerrainFacts());
    EXPECT_TRUE(bridge.Update(source));
    EXPECT_FALSE(Terrain::kRuntimeAdmission);
}

TEST(TerrainPresentationBridgeTest, EdgeRangeCannotResolveImagesFromAdjacentObjects)
{
    MapPresentationSnapshot source;
    auto batch = Initial();
    auto malformed = std::make_shared<TerrainPresentationMaterials>(*batch.terrainMaterials);
    malformed->edges[3].imageCount = 36;
    // Even a claimed supported catalog cannot authorize reading imageBase+36 beyond this object.
    batch.terrainMaterials = std::move(malformed);
    source.Apply(batch);
    ASSERT_TRUE(source.HasBoundedTerrainFacts());
    Terrain::PresentationBridge bridge;
    EXPECT_FALSE(bridge.Update(source));
    EXPECT_EQ(bridge.GetSnapshot().materials, nullptr);
}

TEST(TerrainPresentationBridgeTest, UnchangedPublicationAvoidsMaterialMapCopies)
{
    MapPresentationSnapshot source;
    auto batch = Initial(); source.Apply(batch);
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    const auto snapshot = bridge.GetSnapshot();
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 1u);
    for (size_t i = 0; i < 100; i++) ASSERT_TRUE(bridge.Update(source));
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 1u);
    EXPECT_EQ(snapshot.materials, bridge.GetSnapshot().materials);
    EXPECT_EQ(snapshot.chunks, bridge.GetSnapshot().chunks);
    batch.reset = false; batch.changes = { Tile(513) };
    batch.changes[0].surface.terrain.baseZ = 48; source.Apply(batch);
    ASSERT_TRUE(bridge.Update(source));
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 2u);
    EXPECT_EQ(snapshot.materials, bridge.GetSnapshot().materials);
    EXPECT_NE(snapshot.chunks[2], bridge.GetSnapshot().chunks[2]);
    ASSERT_TRUE(bridge.Update(source));
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 2u);
    AdvanceTerrainObjectRevision();
    EXPECT_FALSE(bridge.Update(source));
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 2u);
}

namespace
{
    struct TwoTemporarySprites
    {
        const bool noGraphics = gOpenRCT2NoGraphics;
        std::array<G1Element, 2> old{};
        std::array<uint8_t, 4> pixels{1, 2, 3, 4};
        TwoTemporarySprites()
        {
            gOpenRCT2NoGraphics = false;
            for (uint32_t i = 0; i < 2; i++) old[i] = *GfxGetG1Element(SPR_TEMP_BEGIN + i);
            G1Element element{};
            element.offset = pixels.data(); element.width = element.height = 2;
            element.flags = { G1Flag::hasTransparency };
            GfxSetG1Element(SPR_TEMP_BEGIN, &element);
            element.flags = { G1Flag::hasTransparency, G1Flag::hasZoomSprite };
            element.zoomedOffset = 1;
            GfxSetG1Element(SPR_TEMP_BEGIN + 1, &element);
        }
        ~TwoTemporarySprites()
        {
            for (uint32_t i = 0; i < 2; i++) GfxSetG1Element(SPR_TEMP_BEGIN + i, &old[i]);
            gOpenRCT2NoGraphics = noGraphics;
        }
    };
}

TEST(TerrainPresentationBridgeTest, ReboundResidenciesUseIndependentFramePinsAndOwnedUploads)
{
    using namespace OpenRCT2::Ui::Gpu;
    TwoTemporarySprites assets;
    TextureCache cache(1);
    cache.BeginFrame();
    const auto original = cache.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{0});
    ASSERT_TRUE(original.has_value());
    const auto generation = cache.GetImageResidencyGeneration();
    const std::array serials{original->residencyRevision};
    FrameCommandStream heldCommands;
    const auto held = cache.SealFrame(heldCommands);
    ASSERT_EQ(heldCommands.textureUploads.size(), 1u);
    cache.BeginFrame();
    ASSERT_TRUE(cache.TryBindImageResidencies(generation, serials));
    FrameCommandStream secondCommands;
    const auto second = cache.SealFrame(secondCommands);
    EXPECT_NE(held, second);
    // The first packet is still uncommitted; rebinding keeps its upload available.
    ASSERT_EQ(secondCommands.textureUploads.size(), 1u);
    EXPECT_EQ(secondCommands.textureUploads[0].pixels, heldCommands.textureUploads[0].pixels);
    cache.RetireFrame(second, FrameRetirement::Presented);
    cache.BeginFrame();
    ASSERT_TRUE(cache.TryBindImageResidencies(generation, serials));
    FrameCommandStream thirdCommands;
    const auto third = cache.SealFrame(thirdCommands);
    EXPECT_TRUE(thirdCommands.textureUploads.empty());
    assets.pixels[0] = 9;
    cache.InvalidateImage(SPR_TEMP_BEGIN);
    cache.BeginFrame();
    EXPECT_FALSE(cache.TryBindImageResidencies(generation, serials));
    const auto replacement = cache.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{0});
    ASSERT_TRUE(replacement.has_value());
    EXPECT_NE(replacement->residencyRevision, original->residencyRevision);
    EXPECT_NE(replacement->descriptorIndex, original->descriptorIndex); // Held lease prevents slot reuse.
    FrameCommandStream replacementCommands;
    const auto replacementLease = cache.SealFrame(replacementCommands);
    EXPECT_EQ(heldCommands.textureUploads[0].pixels[0], std::byte{1});
    ASSERT_EQ(replacementCommands.textureUploads.size(), 1u);
    EXPECT_EQ(replacementCommands.textureUploads[0].pixels[0], std::byte{9});
    cache.RetireFrame(held, FrameRetirement::Failed);
    cache.RetireFrame(third, FrameRetirement::Presented);
    cache.RetireFrame(replacementLease, FrameRetirement::Presented);
    cache.DrainFrameRetirements();
}

TEST(TerrainPresentationBridgeTest, CatalogGenerationRejectsParentInvalidationForeignCacheAndPartialSets)
{
    using namespace OpenRCT2::Ui::Gpu;
    TwoTemporarySprites assets;
    TextureCache cache(1), other(1);
    cache.BeginFrame();
    const auto linked = cache.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN + 1), ZoomLevel{1});
    ASSERT_TRUE(linked.has_value());
    const auto generation = cache.GetImageResidencyGeneration();
    const std::array serials{linked->residencyRevision};
    FrameCommandStream initialCommands;
    const auto initial = cache.SealFrame(initialCommands);
    cache.BeginFrame();
    const std::array invalidSet{linked->residencyRevision, uint64_t{0}};
    EXPECT_FALSE(cache.TryBindImageResidencies(generation, invalidSet));
    FrameCommandStream emptyCommands;
    const auto empty = cache.SealFrame(emptyCommands);
    EXPECT_TRUE(emptyCommands.textureUploads.empty()); // No prefix of a rejected set was bound.
    other.BeginFrame();
    ASSERT_TRUE(other.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{0}).has_value());
    EXPECT_FALSE(other.TryBindImageResidencies(generation, serials));
    other.AbortFrame();
    cache.InvalidateImage(SPR_TEMP_BEGIN + 1); // Child allocation remains resident, parent metadata does not.
    cache.BeginFrame();
    EXPECT_FALSE(cache.TryBindImageResidencies(generation, serials));
    const auto child = cache.GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{0});
    ASSERT_TRUE(child.has_value());
    EXPECT_EQ(child->residencyRevision, linked->residencyRevision);
    cache.AbortFrame();
    cache.RetireFrame(initial, FrameRetirement::Failed);
    cache.RetireFrame(empty, FrameRetirement::Presented);
    cache.DrainFrameRetirements();
    EXPECT_THROW(static_cast<void>(cache.TryBindImageResidencies(generation, serials)), std::logic_error);
}

namespace
{
    struct ScopedTerrainCatalog
    {
        const bool oldNoGraphics = gOpenRCT2NoGraphics;
        const bool oldHeadless = gOpenRCT2Headless;
        bool loadedG1 = false;
        uint32_t base = kImageIndexUndefined;
        std::array<uint8_t, 4> pixels{1, 2, 3, 4};
        ScopedTerrainCatalog() { gOpenRCT2NoGraphics = false; gOpenRCT2Headless = true; }
        ~ScopedTerrainCatalog()
        {
            if (base != kImageIndexUndefined) GfxObjectFreeImages(base, 37);
            if (loadedG1) GfxUnloadG1();
            gOpenRCT2NoGraphics = oldNoGraphics;
            gOpenRCT2Headless = oldHeadless;
        }
        bool Initialise(const char* rct2)
        {
            // Reuse a previously loaded registry without unloading or replacing it.
            if (GfxGetG1Element(SPR_BLANK_TILE) == nullptr)
            {
                auto environment = CreatePlatformEnvironment();
                environment->SetBasePath(DirBase::rct2, rct2);
                if (!GfxLoadG1(*environment)) return false;
                loadedG1 = true;
            }
            std::array<G1Element, 37> elements{};
            for (auto& element : elements)
            {
                element.offset = pixels.data(); element.width = element.height = 2;
                element.flags = { G1Flag::hasTransparency };
            }
            elements[1].flags = { G1Flag::hasTransparency, G1Flag::hasZoomSprite };
            elements[1].zoomedOffset = 1;
            base = GfxObjectAllocateImages(elements.data(), static_cast<uint32_t>(elements.size()));
            return base != kImageIndexUndefined;
        }
    };
}

TEST(TerrainPresentationBridgeTest, ResolveAssetsRetainsRebuildsAndRecoversWithoutCommittingFailure)
{
    using namespace OpenRCT2::Ui::Gpu;
    const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
    if (rct2 == nullptr || *rct2 == '\0')
    {
        const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        if (required != nullptr && std::string_view(required) == "1")
            FAIL() << "Bridge asset lifecycle requires the pinned RCT2 path";
        GTEST_SKIP() << "Bridge asset lifecycle requires OPENRCT2_TEST_RCT2_PATH";
    }
    ScopedTerrainCatalog assets;
    ASSERT_TRUE(assets.Initialise(rct2));
    MapPresentationSnapshot source;
    auto batch = Initial();
    auto materials = std::make_shared<TerrainPresentationMaterials>(*batch.terrainMaterials);
    materials->surfaces[2].imageBase = assets.base;
    materials->edges[3].imageBase = assets.base;
    batch.terrainMaterials = materials;
    source.Apply(batch);
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    TextureCache cache(4); // Real blank tile and synthetic sprites use different atlas size classes.
    cache.BeginFrame();
    ASSERT_TRUE(bridge.ResolveAssets(cache));
    const auto heldTable = bridge.GetSprites();
    ASSERT_NE(heldTable, nullptr);
    ASSERT_EQ(heldTable->records.size(), 38u);
    const auto findParent = [&](const auto& table) -> const Terrain::DrawSpriteMetadata& {
        const auto found = std::find_if(table->records.begin(), table->records.end(), [&](const auto& value) {
            return value.imageIndex == assets.base + 1;
        });
        if (found == table->records.end()) throw std::runtime_error("Missing dynamic parent sprite");
        return *found;
    };
    const auto oldParent = findParent(heldTable);
    FrameCommandStream heldCommands;
    const auto heldLease = cache.SealFrame(heldCommands);
    cache.BeginFrame();
    ASSERT_TRUE(bridge.Update(source));
    ASSERT_TRUE(bridge.ResolveAssets(cache));
    EXPECT_EQ(bridge.GetSprites(), heldTable);
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 1u);
    EXPECT_EQ(bridge.GetSpriteCatalogBuilds(), 1u);
    EXPECT_EQ(bridge.GetResidencyRebinds(), 1u);
    FrameCommandStream reboundCommands;
    const auto reboundLease = cache.SealFrame(reboundCommands);
    EXPECT_NE(heldLease, reboundLease);
    EXPECT_FALSE(reboundCommands.textureUploads.empty()); // Held unsubmitted packet owns an independent upload lease.
    cache.RetireFrame(reboundLease, FrameRetirement::Presented);

    auto parent = *GfxGetG1Element(assets.base + 1);
    parent.xOffset = 7;
    GfxSetG1Element(assets.base + 1, &parent);
    cache.InvalidateImage(assets.base + 1);
    cache.BeginFrame();
    ASSERT_TRUE(bridge.ResolveAssets(cache));
    const auto rebuiltTable = bridge.GetSprites();
    EXPECT_NE(rebuiltTable, heldTable);
    EXPECT_GT(rebuiltTable->revision, heldTable->revision);
    EXPECT_EQ(findParent(rebuiltTable).xOffset, 7);
    EXPECT_EQ(findParent(heldTable).xOffset, oldParent.xOffset);
    EXPECT_EQ(findParent(rebuiltTable).variants[1].asset, oldParent.variants[1].asset); // Unchanged child stays resident.
    EXPECT_EQ(bridge.GetSpriteCatalogBuilds(), 2u);
    FrameCommandStream rebuiltCommands;
    const auto rebuiltLease = cache.SealFrame(rebuiltCommands);
    cache.RetireFrame(rebuiltLease, FrameRetirement::Presented);

    auto unsupported = parent;
    unsupported.flags = { G1Flag::noZoomDraw };
    GfxSetG1Element(assets.base + 1, &unsupported);
    cache.InvalidateImage(assets.base + 1);
    cache.BeginFrame();
    EXPECT_FALSE(bridge.ResolveAssets(cache));
    EXPECT_EQ(bridge.GetSprites(), rebuiltTable);
    EXPECT_EQ(bridge.GetSpriteCatalogBuilds(), 3u);
    EXPECT_EQ(bridge.GetResidencyRebinds(), 1u);
    cache.AbortFrame();
    parent.xOffset = 9;
    GfxSetG1Element(assets.base + 1, &parent);
    cache.InvalidateImage(assets.base + 1);
    cache.BeginFrame();
    ASSERT_TRUE(bridge.ResolveAssets(cache));
    const auto recoveredTable = bridge.GetSprites();
    EXPECT_NE(recoveredTable, rebuiltTable);
    EXPECT_EQ(recoveredTable->revision, rebuiltTable->revision + 1); // Failed resolution published no revision.
    EXPECT_EQ(findParent(recoveredTable).xOffset, 9);
    EXPECT_EQ(findParent(rebuiltTable).xOffset, 7);
    EXPECT_EQ(bridge.GetSpriteCatalogBuilds(), 4u);
    FrameCommandStream recoveredCommands;
    const auto recoveredLease = cache.SealFrame(recoveredCommands);
    cache.RetireFrame(recoveredLease, FrameRetirement::Presented);
    cache.BeginFrame();
    ASSERT_TRUE(bridge.ResolveAssets(cache));
    EXPECT_EQ(bridge.GetSprites(), recoveredTable);
    EXPECT_EQ(bridge.GetSpriteCatalogBuilds(), 4u);
    EXPECT_EQ(bridge.GetResidencyRebinds(), 2u);
    FrameCommandStream settledCommands;
    const auto settledLease = cache.SealFrame(settledCommands);
    EXPECT_TRUE(settledCommands.textureUploads.empty());
    cache.RetireFrame(settledLease, FrameRetirement::Presented);
    cache.RetireFrame(heldLease, FrameRetirement::Failed);
    cache.DrainFrameRetirements();
}