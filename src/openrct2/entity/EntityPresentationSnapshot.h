/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "EntityRegistry.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace OpenRCT2
{
    /**
     * Immutable, incrementally published entity storage for presentation work.
     *
     * Entity and spatial chunks are copy-on-write. A background worker applies owned records captured at a completed logical
     * tick, while paint workers retain the last complete scene. Legacy paint functions therefore keep exact concrete layouts
     * without reading mutable simulation storage or rebuilding the full entity world for every viewport.
     */
    class EntityPresentationSnapshot
    {
    private:
        friend class EntityStorage;

        static constexpr size_t kEntityChunkWidth = 64;
        static constexpr size_t kEntityChunkCount = (kMaxEntities + kEntityChunkWidth - 1) / kEntityChunkWidth;
        static constexpr size_t kSpatialChunkWidth = 32;
        static constexpr size_t kSpatialChunkCount = (kSpatialIndexSize + kSpatialChunkWidth - 1) / kSpatialChunkWidth;

        struct EntityChunk
        {
            std::array<Entity_t, kEntityChunkWidth> entities;
            std::bitset<kEntityChunkWidth> present;
        };
        using SpatialChunk = std::array<std::vector<EntityId>, kSpatialChunkWidth>;

        struct BulkPage
        {
            EntityType type{ EntityType::null };
            size_t stride{};
            uint32_t firstSlot{};
            std::array<uint64_t, 4> occupied{};
            std::vector<std::byte> storage;
        };

        struct BulkSpatialBucket
        {
            uint32_t index{};
            std::vector<EntityId> entities;
        };

        std::array<std::shared_ptr<const EntityChunk>, kEntityChunkCount> _entityChunks;
        std::array<std::shared_ptr<const SpatialChunk>, kSpatialChunkCount> _spatialChunks;
        std::vector<BulkPage> _bulkPages;
        size_t _bulkPageCount{};
        std::array<const EntityBase*, kMaxEntities> _bulkEntityIndex{};
        std::vector<uint16_t> _bulkSpatialBucketIndex;
        std::vector<BulkSpatialBucket> _bulkSpatialBuckets;
        size_t _bulkSpatialBucketCount{};
        bool _bulkMode{};
        size_t _entityCount{};
        uint64_t _epoch{};

    public:
        EntityPresentationSnapshot();

        [[nodiscard]] static std::shared_ptr<const EntityPresentationSnapshot> Capture(
            EntityRegistry& registry, std::span<const CoordsXY> tileLocations, std::span<const EntityId> lookupRoots = {});

        void Apply(const EntityVisualChangeBatch& batch);
        void CaptureStorage(EntityRegistry& registry);
        void BuildCapturedStorage();

        [[nodiscard]] const EntityBase* TryGetEntity(EntityId id) const noexcept;
        [[nodiscard]] const std::vector<EntityId>& GetEntityTileList(const CoordsXY& location) const noexcept;
        [[nodiscard]] size_t GetCapturedEntityCount() const noexcept { return _entityCount; }
    };

    class ScopedEntityPresentationSnapshot
    {
    private:
        const EntityPresentationSnapshot* _previous{};

    public:
        explicit ScopedEntityPresentationSnapshot(const EntityPresentationSnapshot* snapshot) noexcept;
        ~ScopedEntityPresentationSnapshot();

        ScopedEntityPresentationSnapshot(const ScopedEntityPresentationSnapshot&) = delete;
        ScopedEntityPresentationSnapshot& operator=(const ScopedEntityPresentationSnapshot&) = delete;
    };

    [[nodiscard]] const EntityPresentationSnapshot* GetCurrentEntityPresentationSnapshot() noexcept;
} // namespace OpenRCT2
