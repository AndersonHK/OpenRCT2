// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "GpuTextureCache.h"
#include "RetainedTerrainDrawing.h"

#include <algorithm>
#include <atomic>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/G1Element.h>
#include <openrct2/drawing/RetainedPeepState.h>
#include <openrct2/peep/PeepSpriteIds.h>
#include <set>
#include <stdexcept>

namespace OpenRCT2::Ui::Gpu
{
    struct PeepAssetGeneration
    {
        uint64_t revision{};
        uint32_t accessoryBase{}, balloonBase{};
        std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> catalog;
        std::shared_ptr<const std::vector<uint32_t>> usedObjects;
        std::shared_ptr<const Terrain::DrawSpriteTable> terrainSprites;
        std::shared_ptr<const Terrain::DrawSpriteTable> sprites;
        // Slot-indexed descriptors; holes have generation/groupCount zero and cannot be selected.
        std::vector<Drawing::RetainedPeepAnimationDescriptor> descriptors;
        std::vector<Drawing::RetainedPeepAnimationFact> facts;
        std::shared_ptr<const AtlasAssetLease> atlasLease;
    };

    // Shared generation identities cannot alias after a viewport bridge is recreated.
    inline uint64_t NextNativeAssetRevision()
    {
        static std::atomic<uint64_t> next{ 1 };
        auto value = next.load(std::memory_order_relaxed);
        for (;;)
        {
            if (value == UINT64_MAX)
                throw std::overflow_error("Native asset generation exhausted");
            if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
                return value;
        }
    }

    // Original culling dimensions remain independent of the resolved zoom image. A noZoomDraw body
    // still allocates its parent and can carry an accessory; the GPU suppresses only its raster.
    inline bool ResolveNativeSpriteMetadata(
        TextureCache& cache, uint32_t image, Terrain::DrawSpriteMetadata& metadata, std::vector<uint64_t>& residencies,
        std::vector<uint32_t>& dependencies, bool peepRemapped)
    {
        const auto* original = GfxGetG1Element(image);
        if (original == nullptr || original->offset == nullptr || original->width <= 0 || original->height <= 0
            || (original->flags.holder & ~55u) != 0)
            return false;
        metadata.imageIndex = image;
        metadata.width = original->width;
        metadata.height = original->height;
        metadata.xOffset = original->xOffset;
        metadata.yOffset = original->yOffset;
        for (int8_t zoom = 0; zoom < 2; ++zoom)
        {
            // Peep body/accessories always use a remap; zero is transparent in that operation.
            const auto id = peepRemapped ? ImageId(image).WithPrimary(static_cast<Drawing::Colour>(0)) : ImageId(image);
            const auto resolved = cache.ResolveAssetSprite(id, ZoomLevel{ zoom }, dependencies);
            if (resolved.noZoomDraw)
            {
                metadata.variants[zoom].flags = 2;
                continue;
            }
            if (!resolved.sprite || resolved.sprite->zeroCoverage || resolved.sprite->width <= 0
                || resolved.sprite->height <= 0)
                return false;
            const auto& sprite = *resolved.sprite;
            metadata.variants[zoom] = { sprite.width,
                                        sprite.height,
                                        sprite.xOffset,
                                        sprite.yOffset,
                                        sprite.descriptorIndex,
                                        sprite.hasRleCompression ? 1u : 0u,
                                        static_cast<int8_t>(sprite.zoom),
                                        sprite.coordinateShift };
            residencies.push_back(sprite.residencyRevision);
        }
        return true;
    }

    class PeepAssetResolver final
    {
        std::shared_ptr<const PeepAssetGeneration> _generation;
        uint64_t _builds{}, _bindings{};

    public:
        uint64_t GetCatalogBuilds() const noexcept
        {
            return _builds;
        }
        uint64_t GetGenerationBindings() const noexcept
        {
            return _bindings;
        }

        std::shared_ptr<const PeepAssetGeneration> Resolve(
            TextureCache& cache, const std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog>& held,
            const std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog>& current,
            const std::shared_ptr<const Terrain::DrawSpriteTable>& terrain)
        {
            if (!held || !terrain || !terrain->residency)
                return {};
            if (_generation && _generation->catalog == held && _generation->terrainSprites == terrain
                && cache.TryBindAssetLease(_generation->atlasLease) && cache.TryBindAssetLease(terrain->residency))
            {
                ++_bindings;
                return _generation;
            }
            // A held fact catalog alone does not own live G1. Never decode recycled image IDs from it.
            if (held != current || !cache.TryBindAssetLease(terrain->residency))
                return {};
            auto next = std::make_shared<PeepAssetGeneration>();
            next->catalog = held;
            next->terrainSprites = terrain;
            std::set<uint32_t> images;
            constexpr size_t kMaximumFacts = 64 * 256 * Drawing::kRetainedPeepAnimationTypes;
            for (const auto& [slot, entry] : held->slots)
            {
                if (slot >= UINT16_MAX)
                    return {};
                if (!entry.object)
                    continue;
                auto descriptor = entry.object->descriptor;
                if (entry.generation == 0 || descriptor.objectGeneration != entry.generation || descriptor.objectIndex != slot
                    || descriptor.factOffset != 0 || descriptor.groupCount == 0 || descriptor.groupCount > 256
                    || descriptor.reserved0 != 0 || descriptor.reserved1 != 0
                    || entry.object->facts.size() != size_t{ descriptor.groupCount } * Drawing::kRetainedPeepAnimationTypes
                    || next->facts.size() + entry.object->facts.size() > kMaximumFacts || descriptor.imageCount == 0
                    || descriptor.imageCount > kSpriteAssetDescriptorCount
                    || uint64_t{ descriptor.imageBase } + descriptor.imageCount > kImageIndexUndefined)
                    return {};
                if (next->descriptors.size() <= slot)
                    next->descriptors.resize(size_t{ slot } + 1);
                descriptor.factOffset = static_cast<uint32_t>(next->facts.size());
                for (const auto& fact : entry.object->facts)
                    if (fact.reserved0 != 0 || fact.reserved1 != 0 || fact.valid > 1
                        || (fact.valid
                            && (fact.baseImage < descriptor.imageBase
                                || uint64_t{ fact.baseImage } >= uint64_t{ descriptor.imageBase } + descriptor.imageCount)))
                        return {};
                next->descriptors[slot] = descriptor;
                next->facts.insert(next->facts.end(), entry.object->facts.begin(), entry.object->facts.end());
                for (uint32_t offset = 0; offset < descriptor.imageCount; ++offset)
                    images.insert(descriptor.imageBase + offset);
                if (images.size() > kSpriteAssetDescriptorCount)
                    return {};
            }
            // Eight frames x four directions. Free balloons are a separate family.
            for (const uint32_t base : { uint32_t{ kPeepSpriteHatItemStart }, uint32_t{ kPeepSpriteBalloonItemStart },
                                         uint32_t{ kPeepSpriteUmbrellaItemStart } })
                for (uint32_t offset = 0; offset < 32; ++offset)
                    images.insert(base + offset);
            std::vector<uint64_t> residencies;
            std::vector<uint32_t> dependencies;
            auto merged = std::make_shared<Terrain::DrawSpriteTable>();
            merged->records = terrain->records;
            merged->records.reserve(terrain->records.size() + images.size());
            for (const auto image : images)
            {
                // A terrain and peep ID overlap is not a safe metadata/remap alias.
                if (std::binary_search(
                        terrain->records.begin(), terrain->records.end(), Terrain::DrawSpriteMetadata{ .imageIndex = image },
                        [](const auto& a, const auto& b) { return a.imageIndex < b.imageIndex; }))
                    return {};
                Terrain::DrawSpriteMetadata metadata{};
                if (!ResolveNativeSpriteMetadata(cache, image, metadata, residencies, dependencies, true))
                    return {};
                merged->records.push_back(metadata);
            }
            if (merged->records.size() > kSpriteAssetDescriptorCount)
                return {};
            std::sort(merged->records.begin(), merged->records.end(), [](const auto& a, const auto& b) {
                return a.imageIndex < b.imageIndex;
            });
            next->atlasLease = cache.CreateAssetLease(residencies, dependencies);
            if (!cache.TryBindAssetLease(next->atlasLease))
                return {};
            next->revision = merged->revision = NextNativeAssetRevision();
            merged->residency = next->atlasLease;
            next->sprites = std::move(merged);
            _generation = std::move(next);
            ++_builds;
            ++_bindings;
            return _generation;
        }
    };
} // namespace OpenRCT2::Ui::Gpu
