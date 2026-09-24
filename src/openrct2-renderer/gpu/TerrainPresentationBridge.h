// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "GpuTextureCache.h"
#include "PeepAssetGeneration.h"
#include "RetainedTerrainDrawing.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <openrct2/SpriteIds.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/G1Element.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <set>

namespace OpenRCT2::Ui::Gpu::Terrain
{
    // Closed until production publication/atlas/UI composition is qualified. Device corpus proof alone is insufficient.
    inline constexpr bool kRuntimeAdmission = false;

    // Recording-owner bridge. World records are revisited only when their immutable chunk revision changes.
    class PresentationBridge final
    {
        RetainedTerrainSnapshot _snapshot;
        std::array<uint64_t, kRetainedChunkCount> _sourceRevisions{};
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> _materialIds;
        uint64_t _sourceMaterialRevision{}, _nextMaterialRevision{}, _spriteMaterialRevision{};
        std::shared_ptr<const DrawSpriteTable> _sprites;
        std::set<uint32_t> _images;
        uint64_t _materialMapCopies{}, _spriteCatalogBuilds{}, _residencyRebinds{};

    public:
        [[nodiscard]] const RetainedTerrainSnapshot& GetSnapshot() const noexcept
        {
            return _snapshot;
        }
        [[nodiscard]] const std::shared_ptr<const DrawSpriteTable>& GetSprites() const noexcept
        {
            return _sprites;
        }

        // Owner-thread counters distinguish metadata work from GPU upload bytes.
        [[nodiscard]] uint64_t GetMaterialMapCopies() const noexcept
        {
            return _materialMapCopies;
        }
        [[nodiscard]] uint64_t GetSpriteCatalogBuilds() const noexcept
        {
            return _spriteCatalogBuilds;
        }
        [[nodiscard]] uint64_t GetResidencyRebinds() const noexcept
        {
            return _residencyRebinds;
        }

        bool Update(const MapPresentationSnapshot& source)
        {
            if (!source.HasBoundedTerrainFacts() || source.GetSurfaceChunks().size() != kRetainedChunkCount)
                return false;
            const auto& materials = *source.GetTerrainMaterials();
            if (_snapshot.worldEpoch != source.GetEpoch())
            {
                *this = PresentationBridge{};
                _snapshot.worldEpoch = source.GetEpoch();
            }
            // A dropped old presentation cannot reinterpret recycled object/image indices using the live registry.
            if (materials.revision != GetTerrainObjectRevision())
                return false;
            bool changedChunks = false;
            for (size_t c = 0; c < kRetainedChunkCount; c++)
            {
                const auto& chunk = source.GetSurfaceChunks()[c];
                if (chunk == nullptr || chunk->revision == 0)
                    return false;
                changedChunks |= _sourceRevisions[c] != chunk->revision;
            }
            if (!changedChunks && _sourceMaterialRevision == materials.revision)
                return true;
            ++_materialMapCopies;
            auto ids = _materialIds;
            for (size_t c = 0; c < kRetainedChunkCount; c++)
            {
                const auto& chunk = source.GetSurfaceChunks()[c];
                if (chunk == nullptr || chunk->revision == 0)
                    return false;
                if (_sourceRevisions[c] == chunk->revision)
                    continue;
                for (const auto& record : chunk->records)
                {
                    const auto& tile = record.terrain;
                    if (tile.kind == 2)
                        continue;
                    if (tile.kind != 1 || tile.surfaceSlot >= 255 || tile.edgeSlot >= 255)
                        return false;
                    for (const auto key :
                         { std::pair{ 1u, uint32_t{ tile.surfaceSlot } }, std::pair{ 2u, uint32_t{ tile.edgeSlot } } })
                    {
                        if (!ids.contains(key))
                            ids.emplace(key, static_cast<uint32_t>(ids.size()));
                        if (ids.size() > kRetainedMaterialCapacity)
                            return false;
                    }
                }
            }
            const bool changedMaterials = _sourceMaterialRevision != materials.revision || ids != _materialIds;
            std::shared_ptr<const RetainedMaterialTable> nextMaterials = _snapshot.materials;
            if (changedMaterials)
            {
                auto next = std::make_shared<RetainedMaterialTable>();
                next->records.resize(ids.size());
                for (const auto& [key, id] : ids)
                {
                    const auto& input = key.first == 1 ? materials.surfaces[key.second] : materials.edges[key.second];
                    if (!input.supported || input.imageCount == 0 || (key.first == 2 && input.imageCount < 37)
                        || input.imageCount > kDrawSpriteCapacity
                        || static_cast<uint64_t>(input.imageBase) + input.imageCount > INT32_MAX)
                        return false;
                    auto& output = next->records[id];
                    output.imageBase = input.imageBase;
                    output.imageCount = input.imageCount;
                    output.kind = key.first;
                    output.selectors.entries = input.selectors;
                }
                next->revision = ++_nextMaterialRevision;
                nextMaterials = std::move(next);
            }
            auto chunks = _snapshot.chunks;
            for (size_t c = 0; c < kRetainedChunkCount; c++)
            {
                const auto& input = *source.GetSurfaceChunks()[c];
                if (_sourceRevisions[c] == input.revision)
                    continue;
                auto next = std::make_shared<RetainedTileChunk>();
                next->revision = input.revision;
                for (size_t i = 0; i < kRetainedChunkSize; i++)
                {
                    const auto& raw = input.records[i].terrain;
                    auto& tile = next->records[i];
                    tile.baseZ = raw.baseZ;
                    tile.slope = raw.slope;
                    tile.grass = raw.grass;
                    tile.kind = raw.kind;
                    if (raw.kind == 1)
                    {
                        tile.surfaceMaterial = ids.at({ 1, raw.surfaceSlot });
                        tile.edgeMaterial = ids.at({ 2, raw.edgeSlot });
                    }
                }
                chunks[c] = std::move(next);
            }
            _snapshot.chunks = std::move(chunks);
            _snapshot.materials = std::move(nextMaterials);
            _materialIds = std::move(ids);
            _sourceMaterialRevision = materials.revision;
            for (size_t c = 0; c < kRetainedChunkCount; c++)
                _sourceRevisions[c] = source.GetSurfaceChunks()[c]->revision;
            return true;
        }

        bool ResolveAssets(TextureCache& cache)
        {
            if (_snapshot.materials == nullptr || _sourceMaterialRevision != GetTerrainObjectRevision())
                return false;
            const bool changedMaterials = _spriteMaterialRevision != _snapshot.materials->revision;
            if (_sprites && !changedMaterials && cache.TryBindAssetLease(_sprites->residency))
            {
                ++_residencyRebinds;
                return true;
            }
            std::set<uint32_t> nextImages;
            if (changedMaterials)
            {
                nextImages.insert(SPR_BLANK_TILE);
                for (const auto& material : _snapshot.materials->records)
                {
                    if (material.kind == 1)
                    {
                        for (const auto entry : material.selectors.entries)
                            for (uint32_t offset = 0; offset < 19; offset++)
                            {
                                const uint64_t relative = uint64_t{ entry } * 19 + offset;
                                if (relative >= material.imageCount)
                                    return false;
                                nextImages.insert(material.imageBase + static_cast<uint32_t>(relative));
                            }
                    }
                    else
                        for (uint32_t offset = 0; offset < 37; offset++)
                            nextImages.insert(material.imageBase + offset);
                }
                if (nextImages.size() > kDrawSpriteCapacity)
                    return false;
            }
            const auto& images = changedMaterials ? nextImages : _images;
            ++_spriteCatalogBuilds;
            auto next = std::make_shared<DrawSpriteTable>();
            std::vector<uint64_t> residencies;
            std::vector<uint32_t> dependencies;
            next->records.reserve(images.size());
            residencies.reserve(images.size() * 2);
            for (const auto image : images)
            {
                DrawSpriteMetadata metadata{};
                if (!ResolveNativeSpriteMetadata(cache, image, metadata, residencies, dependencies, false))
                    return false;
                next->records.push_back(metadata);
            }
            // One lifetime pin set per generation; ordinary frames bind only this held lease.
            next->residency = cache.CreateAssetLease(residencies, dependencies);
            if (!cache.TryBindAssetLease(next->residency))
                return false;
            next->revision = NextNativeAssetRevision();
            _sprites = std::move(next);
            if (changedMaterials)
                _images = std::move(nextImages);
            _spriteMaterialRevision = _snapshot.materials->revision;
            return true;
        }
    };
} // namespace OpenRCT2::Ui::Gpu::Terrain
