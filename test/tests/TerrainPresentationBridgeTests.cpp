// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <cstdlib>
#include <gtest/gtest.h>
#include <openrct2-renderer/gpu/GpuWorldPeepCatalog.h>
#include <openrct2-renderer/gpu/TerrainPresentationBridge.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/drawing/Image.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <stdexcept>
#include <string_view>

using namespace OpenRCT2;
namespace Terrain = OpenRCT2::Ui::Gpu::Terrain;

TEST(TerrainPresentationBridgeTest, WorldPeepCatalogAdmitsOnlyLiveObjectBanks)
{
    using namespace OpenRCT2::Ui::Gpu;
    auto catalog = std::make_shared<Drawing::RetainedPeepAnimationCatalog>();
    const auto object = [](uint32_t slot, uint32_t base, uint32_t count) {
        return Drawing::BuildRetainedPeepAnimationObject(
            { slot, 1, 1, 0, base, count, 0, 0 }, std::array{ Drawing::RetainedPeepAnimationSource{ 0, 0, base } });
    };
    catalog->slots[2] = { 1, object(2, 100, 4) };
    catalog->slots[3] = { 1, object(3, 1000000, 300000) };
    auto usage = std::make_shared<const std::vector<uint32_t>>(std::vector<uint32_t>{ 2 });
    std::vector<uint32_t> images;
    const auto append = [&](ImageId image) {
        images.push_back(image.GetIndex());
        return static_cast<uint32_t>(images.size() - 1);
    };
    const auto selected = BuildWorldPeepAssets(catalog, usage, append);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->usedObjects, usage);
    EXPECT_EQ(images.size(), 4u + 96u + 13u);
    EXPECT_EQ((std::vector<uint32_t>(images.begin(), images.begin() + 4)), (std::vector<uint32_t>{ 100, 101, 102, 103 }));
    ASSERT_EQ(selected->descriptors.size(), 3u);
    EXPECT_EQ(selected->descriptors[2].imageCount, 4u);
    EXPECT_EQ(selected->facts[0].baseImage, 0u);

    images.clear();
    const auto empty = BuildWorldPeepAssets(catalog, std::make_shared<const std::vector<uint32_t>>(), append);
    EXPECT_TRUE(empty->descriptors.empty());
    EXPECT_EQ(images.size(), 96u + 13u);
    images.clear();
    EXPECT_THROW(
        BuildWorldPeepAssets(catalog, std::make_shared<const std::vector<uint32_t>>(std::vector<uint32_t>{ 4 }), append),
        std::runtime_error);
    EXPECT_TRUE(images.empty());
}

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
        change.surface.terrain = { 16, 2, 3, 0, 0, 1 };
        change.surface.terrain.present = 1;
        change.surface.terrain.bounded = 1;
        return change;
    }
    MapPresentationChangeBatch Initial()
    {
        MapPresentationChangeBatch batch;
        batch.epoch = 314;
        batch.reset = true;
        batch.surfaceWidth = batch.surfaceHeight = 32;
        batch.terrainMaterials = Materials();
        for (uint32_t i = 0; i < 1024; i++)
            batch.changes.push_back(Tile(i));
        return batch;
    }
} // namespace

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
    batch.changes = { Tile(17) };
    batch.changes[0].surface.terrain.baseZ = 48;
    batch.changes[0].surface.terrain.slope = 9;
    source.Apply(batch);
    ASSERT_TRUE(bridge.Update(source));
    EXPECT_EQ(old.chunks[0]->records[17].baseZ, 16);
    EXPECT_EQ(bridge.GetSnapshot().chunks[0]->records[17].baseZ, 48);
    EXPECT_EQ(bridge.GetSnapshot().chunks[0]->records[17].slope, 9);
    EXPECT_NE(old.chunks[0], bridge.GetSnapshot().chunks[0]);
    for (size_t i = 1; i < 4; i++)
        EXPECT_EQ(old.chunks[i], bridge.GetSnapshot().chunks[i]);
    EXPECT_EQ(old.materials, bridge.GetSnapshot().materials);
}

TEST(TerrainPresentationBridgeTest, IdenticalObjectReloadRejectsOldPublicationAndRevisesMaterials)
{
    MapPresentationSnapshot source;
    auto batch = Initial();
    source.Apply(batch);
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    const auto old = bridge.GetSnapshot();
    AdvanceTerrainObjectRevision();
    EXPECT_FALSE(bridge.Update(source));
    batch.reset = false;
    batch.changes.clear();
    batch.terrainMaterials = Materials();
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
    auto batch = Initial();
    source.Apply(batch);
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    batch.reset = false;
    batch.changes = { Tile(513) };
    batch.changes[0].surface.terrain.kind = 0;
    source.Apply(batch);
    EXPECT_FALSE(source.HasBoundedTerrainFacts());
    EXPECT_FALSE(bridge.Update(source));
    batch.changes = { Tile(513) };
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
    auto batch = Initial();
    source.Apply(batch);
    Terrain::PresentationBridge bridge;
    ASSERT_TRUE(bridge.Update(source));
    const auto snapshot = bridge.GetSnapshot();
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 1u);
    for (size_t i = 0; i < 100; i++)
        ASSERT_TRUE(bridge.Update(source));
    EXPECT_EQ(bridge.GetMaterialMapCopies(), 1u);
    EXPECT_EQ(snapshot.materials, bridge.GetSnapshot().materials);
    EXPECT_EQ(snapshot.chunks, bridge.GetSnapshot().chunks);
    batch.reset = false;
    batch.changes = { Tile(513) };
    batch.changes[0].surface.terrain.baseZ = 48;
    source.Apply(batch);
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
        std::array<uint8_t, 4> pixels{ 1, 2, 3, 4 };
        TwoTemporarySprites()
        {
            gOpenRCT2NoGraphics = false;
            for (uint32_t i = 0; i < 2; i++)
                old[i] = *GfxGetG1Element(SPR_TEMP_BEGIN + i);
            G1Element element{};
            element.offset = pixels.data();
            element.width = element.height = 2;
            element.flags = { G1Flag::hasTransparency };
            GfxSetG1Element(SPR_TEMP_BEGIN, &element);
            element.flags = { G1Flag::hasTransparency, G1Flag::hasZoomSprite };
            element.zoomedOffset = 1;
            GfxSetG1Element(SPR_TEMP_BEGIN + 1, &element);
        }
        ~TwoTemporarySprites()
        {
            for (uint32_t i = 0; i < 2; i++)
                GfxSetG1Element(SPR_TEMP_BEGIN + i, &old[i]);
            gOpenRCT2NoGraphics = noGraphics;
        }
    };
} // namespace

namespace
{
    struct ScopedTerrainCatalog
    {
        const bool oldNoGraphics = gOpenRCT2NoGraphics;
        const bool oldHeadless = gOpenRCT2Headless;
        bool loadedG1 = false;
        uint32_t base = kImageIndexUndefined;
        std::array<uint8_t, 4> pixels{ 1, 2, 3, 4 };
        ScopedTerrainCatalog()
        {
            gOpenRCT2NoGraphics = false;
            gOpenRCT2Headless = true;
        }
        ~ScopedTerrainCatalog()
        {
            if (base != kImageIndexUndefined)
                GfxObjectFreeImages(base, 37);
            if (loadedG1)
                GfxUnloadG1();
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
                if (!GfxLoadG1(*environment))
                    return false;
                loadedG1 = true;
            }
            std::array<G1Element, 37> elements{};
            for (auto& element : elements)
            {
                element.offset = pixels.data();
                element.width = element.height = 2;
                element.flags = { G1Flag::hasTransparency };
            }
            elements[1].flags = { G1Flag::hasTransparency, G1Flag::hasZoomSprite };
            elements[1].zoomedOffset = 1;
            base = GfxObjectAllocateImages(elements.data(), static_cast<uint32_t>(elements.size()));
            return base != kImageIndexUndefined;
        }
    };
} // namespace

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
    auto cacheOwner = std::make_shared<TextureCache>(4);
    auto& cache = *cacheOwner; // Resident generations own the cache, including after the bridge is destroyed.
    cache.BeginFrame();
    ASSERT_TRUE(bridge.ResolveAssets(cache));
    const auto heldTable = bridge.GetSprites();
    ASSERT_NE(heldTable, nullptr);
    ASSERT_EQ(heldTable->records.size(), 38u);
    const auto findParent = [&](const auto& table) -> const Terrain::DrawSpriteMetadata& {
        const auto found = std::find_if(table->records.begin(), table->records.end(), [&](const auto& value) {
            return value.imageIndex == assets.base + 1;
        });
        if (found == table->records.end())
            throw std::runtime_error("Missing dynamic parent sprite");
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
    unsupported.flags = { G1Flag::isPalette }; // noZoomDraw is now an explicitly supported non-raster variant.
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

TEST(TerrainPresentationBridgeTest, AssetLeaseReplaysFirstUploadAndIgnoresUnrelatedInvalidation)
{
    using namespace OpenRCT2::Ui::Gpu;
    TwoTemporarySprites assets;
    auto cache = std::make_shared<TextureCache>(1);
    cache->BeginFrame();
    std::vector<uint32_t> dependencies;
    const auto sprite = cache->ResolveAssetSprite(ImageId(SPR_TEMP_BEGIN + 1), ZoomLevel{ 1 }, dependencies);
    ASSERT_TRUE(sprite.sprite);
    ASSERT_EQ(dependencies, (std::vector<uint32_t>{ SPR_TEMP_BEGIN + 1, SPR_TEMP_BEGIN }));
    const std::array serials{ sprite.sprite->residencyRevision };
    EXPECT_THROW(
        static_cast<void>(cache->CreateAssetLease(std::array{ serials[0], uint64_t{ 0 } }, dependencies)),
        std::invalid_argument);
    const auto held = cache->CreateAssetLease(serials, dependencies);
    auto foreignCache = std::make_shared<TextureCache>(1);
    foreignCache->BeginFrame();
    EXPECT_FALSE(foreignCache->TryBindAssetLease(held));
    foreignCache->AbortFrame();
    ASSERT_TRUE(cache->TryBindAssetLease(held));
    FrameCommandStream first;
    const auto failed = cache->SealFrame(first);
    ASSERT_EQ(first.textureUploads.size(), 1u);
    cache->RetireFrame(failed, FrameRetirement::Failed);
    cache->InvalidateImage(SPR_TEMP_BEGIN + 20); // No dependency; generation stays current.
    cache->BeginFrame();
    ASSERT_TRUE(cache->TryBindAssetLease(held)); // No individual sprite lookup/pin binding.
    FrameCommandStream retry;
    const auto accepted = cache->SealFrame(retry);
    ASSERT_EQ(retry.textureUploads.size(), 1u);
    EXPECT_EQ(retry.textureUploads[0].pixels, first.textureUploads[0].pixels);
    EXPECT_EQ(retry.textureUploads[0].descriptorIndex, first.textureUploads[0].descriptorIndex);
    cache->RetireFrame(accepted, FrameRetirement::Presented);
    cache->BeginFrame();
    ASSERT_TRUE(cache->TryBindAssetLease(held));
    FrameCommandStream settled;
    const auto complete = cache->SealFrame(settled);
    EXPECT_TRUE(settled.textureUploads.empty());
    cache->RetireFrame(complete, FrameRetirement::Presented);
    cache->InvalidateImage(SPR_TEMP_BEGIN + 1); // Parent-only metadata dependency invalidates the generation.
    cache->BeginFrame();
    EXPECT_FALSE(cache->TryBindAssetLease(held));
    const auto child = cache->GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{ 0 });
    ASSERT_TRUE(child);
    EXPECT_EQ(child->residencyRevision, sprite.sprite->residencyRevision);
    cache->AbortFrame();
    cache->DrainFrameRetirements();
}

TEST(TerrainPresentationBridgeTest, AssetLeaseKeepsRecycledImageAllocationAndCacheOwnerAlive)
{
    using namespace OpenRCT2::Ui::Gpu;
    TwoTemporarySprites assets;
    auto cache = std::make_shared<TextureCache>(1);
    const std::weak_ptr<TextureCache> observer = cache;
    cache->BeginFrame();
    std::vector<uint32_t> dependencies;
    const auto old = cache->ResolveAssetSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{ 0 }, dependencies);
    ASSERT_TRUE(old.sprite);
    auto held = cache->CreateAssetLease(std::array{ old.sprite->residencyRevision }, dependencies);
    ASSERT_TRUE(cache->TryBindAssetLease(held));
    FrameCommandStream packet;
    const auto token = cache->SealFrame(packet);
    cache->InvalidateImage(SPR_TEMP_BEGIN);
    cache->BeginFrame();
    EXPECT_FALSE(cache->TryBindAssetLease(held));
    const auto replacement = cache->GetOrLoadImageSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{ 0 });
    ASSERT_TRUE(replacement);
    EXPECT_NE(replacement->descriptorIndex, old.sprite->descriptorIndex);
    EXPECT_NE(replacement->residencyRevision, old.sprite->residencyRevision);
    EXPECT_EQ(packet.textureUploads.front().descriptorIndex, old.sprite->descriptorIndex);
    cache->AbortFrame();
    cache->RetireFrame(token, FrameRetirement::Failed);
    cache->DrainFrameRetirements();
    cache.reset();
    held.reset();
    EXPECT_FALSE(observer.expired()); // Packet survives the recording owner and holds the atlas generation.
    packet.clear();
    EXPECT_TRUE(observer.expired()); // No cache->generation->cache ownership cycle remains.
}

TEST(TerrainPresentationBridgeTest, AssetNoZoomDrawRetainsParentDependencyWithoutRasterUpload)
{
    using namespace OpenRCT2::Ui::Gpu;
    TwoTemporarySprites assets;
    auto parent = *GfxGetG1Element(SPR_TEMP_BEGIN + 1);
    parent.flags = { G1Flag::hasTransparency, G1Flag::noZoomDraw };
    GfxSetG1Element(SPR_TEMP_BEGIN + 1, &parent);
    auto cache = std::make_shared<TextureCache>(1);
    cache->BeginFrame();
    std::vector<uint32_t> dependencies;
    const auto hidden = cache->ResolveAssetSprite(ImageId(SPR_TEMP_BEGIN + 1), ZoomLevel{ 1 }, dependencies);
    EXPECT_TRUE(hidden.noZoomDraw);
    EXPECT_FALSE(hidden.sprite);
    ASSERT_EQ(dependencies, (std::vector<uint32_t>{ SPR_TEMP_BEGIN + 1 }));
    const auto lease = cache->CreateAssetLease({}, dependencies);
    ASSERT_TRUE(cache->TryBindAssetLease(lease));
    FrameCommandStream packet;
    const auto token = cache->SealFrame(packet);
    EXPECT_TRUE(packet.textureUploads.empty());
    cache->RetireFrame(token, FrameRetirement::Presented);
    cache->InvalidateImage(SPR_TEMP_BEGIN + 1);
    cache->BeginFrame();
    EXPECT_FALSE(cache->TryBindAssetLease(lease));
    cache->AbortFrame();
}

TEST(TerrainPresentationBridgeTest, StalePeepCatalogCannotReadRecycledG1)
{
    using namespace OpenRCT2::Ui::Gpu;
    auto cache = std::make_shared<TextureCache>(1);
    cache->BeginFrame();
    auto terrain = std::make_shared<Terrain::DrawSpriteTable>();
    terrain->residency = cache->CreateAssetLease({}, {});
    auto old = std::make_shared<Drawing::RetainedPeepAnimationCatalog>();
    auto current = std::make_shared<Drawing::RetainedPeepAnimationCatalog>();
    old->epoch = current->epoch = 1;
    old->sequence = 1;
    current->sequence = 2;
    PeepAssetResolver resolver;
    // No graphical source is installed here. The mismatch must return before any G1 read.
    EXPECT_EQ(resolver.Resolve(*cache, old, current, terrain), nullptr);
    EXPECT_EQ(resolver.GetCatalogBuilds(), 0u);
    cache->AbortFrame();
}

TEST(TerrainPresentationBridgeTest, PeepAssetCatalogFlattensOnceAndHoldsOriginalGenerationAcrossReplacement)
{
    using namespace OpenRCT2::Ui::Gpu;
    const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
    if (rct2 == nullptr || *rct2 == '\0')
    {
        const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        if (required != nullptr && std::string_view(required) == "1")
            FAIL() << "Peep asset lifecycle requires pinned RCT2 data";
        GTEST_SKIP() << "Peep asset lifecycle requires OPENRCT2_TEST_RCT2_PATH";
    }
    ScopedTerrainCatalog source;
    ASSERT_TRUE(source.Initialise(rct2));
    const auto makeCatalog = [&](uint32_t generation) {
        auto result = std::make_shared<Drawing::RetainedPeepAnimationCatalog>();
        result->epoch = 79;
        result->sequence = generation;
        result->slots[3] = { generation,
                             Drawing::BuildRetainedPeepAnimationObject(
                                 { 3, generation, 1, 0, source.base, 37, 0, 0 },
                                 std::array{ Drawing::RetainedPeepAnimationSource{ 0, 0, source.base } }) };
        return result;
    };
    const auto oldCatalog = makeCatalog(1);
    auto cache = std::make_shared<TextureCache>();
    cache->BeginFrame();
    auto terrain = std::make_shared<Terrain::DrawSpriteTable>();
    terrain->residency = cache->CreateAssetLease({}, {});
    PeepAssetResolver resolver;
    const auto old = resolver.Resolve(*cache, oldCatalog, oldCatalog, terrain);
    ASSERT_NE(old, nullptr);
    ASSERT_EQ(old->descriptors.size(), 4u);
    EXPECT_EQ(old->descriptors[0].groupCount, 0u);
    EXPECT_EQ(old->descriptors[3].objectGeneration, 1u);
    ASSERT_EQ(old->facts.size(), 37u);
    EXPECT_EQ(old->facts[0].baseImage, source.base);
    EXPECT_EQ(old->sprites->records.size(), 37u + 96u);
    FrameCommandStream first;
    const auto token = cache->SealFrame(first);
    cache->RetireFrame(token, FrameRetirement::Presented);
    cache->InvalidateImage(SPR_TEMP_BEGIN + 20);
    cache->BeginFrame();
    for (size_t i = 0; i < 10; ++i)
        EXPECT_EQ(resolver.Resolve(*cache, oldCatalog, oldCatalog, terrain), old);
    EXPECT_EQ(resolver.GetCatalogBuilds(), 1u);
    FrameCommandStream repeat;
    const auto repeatToken = cache->SealFrame(repeat);
    EXPECT_TRUE(repeat.textureUploads.empty());
    cache->RetireFrame(repeatToken, FrameRetirement::Presented);

    const auto oldBase = source.base;
    GfxObjectFreeImages(source.base, 37);
    source.base = kImageIndexUndefined;
    // This standalone cache is not registered with DrawingEngineInvalidateImage.
    // Retire the freed source range even when the allocator chooses a different replacement range.
    for (uint32_t i = 0; i < 37; ++i)
        cache->InvalidateImage(oldBase + i);
    ASSERT_TRUE(source.Initialise(rct2));
    const auto replacementBase = source.base;
    // G1 free-list ordering depends on preceding object loads. Generation ownership must work
    // for both numerical-ID reuse and a fresh range; exact address reuse is not the allocator contract.
    for (uint32_t i = 0; i < 37; ++i)
        cache->InvalidateImage(source.base + i);
    const auto current = makeCatalog(2);
    cache->BeginFrame();
    EXPECT_FALSE(cache->TryBindAssetLease(old->atlasLease));
    EXPECT_EQ(resolver.Resolve(*cache, oldCatalog, current, terrain), nullptr);
    const auto replacement = resolver.Resolve(*cache, current, current, terrain);
    ASSERT_NE(replacement, nullptr);
    EXPECT_NE(replacement, old);
    EXPECT_NE(replacement->revision, old->revision);
    EXPECT_EQ(old->catalog, oldCatalog);
    EXPECT_EQ(old->descriptors[3].objectGeneration, 1u);
    EXPECT_EQ(replacement->descriptors[3].objectGeneration, 2u);
    EXPECT_EQ(old->facts[0].baseImage, oldBase);
    ASSERT_FALSE(replacement->facts.empty());
    EXPECT_EQ(replacement->facts[0].baseImage, replacementBase);
    EXPECT_EQ(resolver.GetCatalogBuilds(), 2u);
    const auto find = [](const auto& generation, uint32_t image) {
        const auto found = std::find_if(
            generation->sprites->records.begin(), generation->sprites->records.end(),
            [image](const auto& metadata) { return metadata.imageIndex == image; });
        if (found == generation->sprites->records.end())
            throw std::runtime_error("Held peep asset generation lost its source image metadata");
        return found->variants[0].asset;
    };
    EXPECT_NE(find(old, oldBase), find(replacement, replacementBase));
    cache->AbortFrame();
    cache->DrainFrameRetirements();
}

TEST(TerrainPresentationBridgeTest, EmptyWorldAssetRetainsDependenciesAndDoesNotHideInvalidSprites)
{
    using namespace OpenRCT2::Ui::Gpu;
    TwoTemporarySprites assets;
    const auto visible = *GfxGetG1Element(SPR_TEMP_BEGIN);
    G1Element empty{};
    GfxSetG1Element(SPR_TEMP_BEGIN, &empty);
    auto cache = std::make_shared<TextureCache>(1);
    cache->BeginFrame();
    std::vector<uint32_t> dependencies;
    const auto blank = cache->ResolveAssetSprite(ImageId(SPR_TEMP_BEGIN + 1), ZoomLevel{ 1 }, dependencies);
    EXPECT_TRUE(blank.empty);
    EXPECT_FALSE(blank.sprite);
    EXPECT_FALSE(blank.noZoomDraw);
    ASSERT_EQ(dependencies, (std::vector<uint32_t>{ SPR_TEMP_BEGIN + 1, SPR_TEMP_BEGIN }));
    const auto lease = cache->CreateAssetLease({}, dependencies);
    ASSERT_TRUE(cache->TryBindAssetLease(lease));
    FrameCommandStream packet;
    const auto frame = cache->SealFrame(packet);
    EXPECT_TRUE(packet.textureUploads.empty());
    cache->RetireFrame(frame, FrameRetirement::Presented);

    GfxSetG1Element(SPR_TEMP_BEGIN, &visible);
    cache->InvalidateImage(SPR_TEMP_BEGIN);
    cache->BeginFrame();
    EXPECT_FALSE(cache->TryBindAssetLease(lease));
    dependencies.clear();
    const auto replacement = cache->ResolveAssetSprite(ImageId(SPR_TEMP_BEGIN + 1), ZoomLevel{ 1 }, dependencies);
    EXPECT_TRUE(replacement.sprite);
    EXPECT_FALSE(replacement.empty);
    cache->AbortFrame();

    // Only genuine zero-area art is accepted as empty, never an absent image or malformed/non-raster data.
    cache->BeginFrame();
    auto invalid = visible;
    invalid.width = -1;
    GfxSetG1Element(SPR_TEMP_BEGIN, &invalid);
    cache->InvalidateImage(SPR_TEMP_BEGIN);
    dependencies.clear();
    const auto negative = cache->ResolveAssetSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{ 0 }, dependencies);
    EXPECT_FALSE(negative.sprite);
    EXPECT_FALSE(negative.empty);
    invalid = visible;
    invalid.flags = { G1Flag::isPalette };
    GfxSetG1Element(SPR_TEMP_BEGIN, &invalid);
    cache->InvalidateImage(SPR_TEMP_BEGIN);
    dependencies.clear();
    EXPECT_THROW(
        static_cast<void>(cache->ResolveAssetSprite(ImageId(SPR_TEMP_BEGIN), ZoomLevel{ 0 }, dependencies)),
        std::invalid_argument); // SpriteAssetDecoder rejects non-indexed palette payloads explicitly.
    dependencies.clear();
    const auto missing = cache->ResolveAssetSprite(ImageId(kImageIndexUndefined), ZoomLevel{ 0 }, dependencies);
    EXPECT_FALSE(missing.sprite);
    EXPECT_FALSE(missing.empty);
    cache->AbortFrame();
}
