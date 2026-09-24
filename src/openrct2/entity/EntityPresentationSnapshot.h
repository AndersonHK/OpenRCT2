/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../drawing/RetainedBalloonScene.h"
#include "../drawing/RetainedPeepState.h"
#include "EntityRegistry.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <memory>
#include <optional>
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

        std::vector<std::shared_ptr<const EntityChunk>> _entityChunks;
        std::vector<std::shared_ptr<const SpatialChunk>> _spatialChunks;
        std::vector<BulkPage> _bulkPages;
        size_t _bulkPageCount{};
        std::vector<const EntityBase*> _bulkEntityIndex;
        std::vector<uint16_t> _bulkSpatialBucketIndex;
        std::vector<BulkSpatialBucket> _bulkSpatialBuckets;
        size_t _bulkSpatialBucketCount{};
        bool _bulkMode{};
        bool _nativeOnly{};
        size_t _unsupportedEntityCount{};
        size_t _entityCount{};
        uint64_t _epoch{};
        uint32_t _sourceTick{};
        std::shared_ptr<const Drawing::RetainedBalloonSnapshot> _retainedBalloons;
        Drawing::BalloonPublicationMetrics _balloonMetrics{};
        std::shared_ptr<const Drawing::RetainedPeepSnapshot> _retainedPeeps;
        std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> _peepAnimations;

    public:
        EntityPresentationSnapshot();

        [[nodiscard]] static std::shared_ptr<const EntityPresentationSnapshot> Capture(
            EntityRegistry& registry, std::span<const CoordsXY> tileLocations, std::span<const EntityId> lookupRoots = {});

        void Apply(const EntityVisualChangeBatch& batch);
        void CaptureStorage(
            EntityRegistry& registry, std::shared_ptr<const Drawing::RetainedBalloonSnapshot> balloons = {},
            Drawing::BalloonPublicationMetrics metrics = {}, std::shared_ptr<const Drawing::RetainedPeepSnapshot> peeps = {},
            std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> peepAnimations = {});
        [[nodiscard]] const auto& GetRetainedPeeps() const noexcept
        {
            return _retainedPeeps;
        }
        [[nodiscard]] const auto& GetPeepAnimations() const noexcept
        {
            return _peepAnimations;
        }
        [[nodiscard]] uint64_t GetSourceEpoch() const noexcept
        {
            return _epoch;
        }
        [[nodiscard]] const auto& GetRetainedBalloons() const noexcept
        {
            return _retainedBalloons;
        }
        [[nodiscard]] const auto& GetBalloonMetrics() const noexcept
        {
            return _balloonMetrics;
        }
        // Native snapshots own semantic state only, with no legacy entity copies or CPU spatial tables.
        void CaptureNativeStorage(
            EntityRegistry& registry, std::shared_ptr<const Drawing::RetainedPeepSnapshot> peeps,
            std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> catalog);
        [[nodiscard]] bool IsNativeOnly() const noexcept
        {
            return _nativeOnly;
        }
        [[nodiscard]] bool IsTerrainOnly() const noexcept
        {
            return _nativeOnly && _retainedPeeps == nullptr;
        }
        [[nodiscard]] bool HasLegacyStorage() const noexcept
        {
            return _entityChunks.capacity() != 0 || _spatialChunks.capacity() != 0 || _bulkPages.capacity() != 0
                || _bulkEntityIndex.capacity() != 0 || _bulkSpatialBucketIndex.capacity() != 0
                || _bulkSpatialBuckets.capacity() != 0;
        }
        [[nodiscard]] size_t GetUnsupportedEntityCount() const noexcept
        {
            return _unsupportedEntityCount;
        }
        void BuildCapturedStorage();
        [[nodiscard]] uint32_t GetSourceTick() const noexcept
        {
            return _sourceTick;
        }

        [[nodiscard]] const EntityBase* TryGetEntity(EntityId id) const noexcept;
        [[nodiscard]] const std::vector<EntityId>& GetEntityTileList(const CoordsXY& location) const noexcept;
        [[nodiscard]] size_t GetCapturedEntityCount() const noexcept
        {
            return _entityCount + (_retainedPeeps ? _retainedPeeps->count : 0);
        }
    };

    // Narrow owned facts for cross-family riders. No fabricated Guest layout or live-registry fallback in raw mode.
    struct GuestPresentationFacts
    {
        uint32_t colours{};
        uint8_t state{};
    };
    [[nodiscard]] std::optional<GuestPresentationFacts> GetGuestPresentationFacts(EntityId id) noexcept;

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
