/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include "../entity/Balloon.h"
#include "../entity/EntityRegistry.h"
#include "../entity/EntityVisualLifecycle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace OpenRCT2::Drawing
{
    // World state only: shaders will choose sprites, projection, clipping and painter order.
    struct RetainedBalloonRecord
    {
        int32_t x{}, y{}, z{};
        uint32_t id{}, generation{}, frame{}, popped{}, colour{};
        uint32_t width{}, heightMin{}, heightMax{}, present{};
        bool operator==(const RetainedBalloonRecord&) const = default;
    };
    static_assert(std::is_trivially_copyable_v<RetainedBalloonRecord>);
    static_assert(sizeof(RetainedBalloonRecord) == 48);
    static_assert(offsetof(RetainedBalloonRecord, id) == 12);
    static_assert(offsetof(RetainedBalloonRecord, present) == 44);

    constexpr size_t kRetainedBalloonChunkWidth = 64;
    constexpr size_t kRetainedBalloonChunkCount = (kMaxEntities + kRetainedBalloonChunkWidth - 1) / kRetainedBalloonChunkWidth;

    struct RetainedBalloonChunk
    {
        // Compatibility changes can advance without changing any GPU record (e.g. timeToMove).
        uint64_t revision{};
        uint64_t gpuRevision{};
        std::array<RetainedBalloonRecord, kRetainedBalloonChunkWidth> records{};
        std::array<Balloon, kRetainedBalloonChunkWidth> compatibility{};
    };

    struct RetainedBalloonSnapshot
    {
        uint64_t epoch{};
        uint64_t sequence{};
        size_t count{};
        std::array<std::shared_ptr<const RetainedBalloonChunk>, kRetainedBalloonChunkCount> chunks{};
        [[nodiscard]] const Balloon* TryGet(EntityId id) const noexcept;
    };

    struct RetainedBalloonApplyMetrics
    {
        uint64_t compatibilityCopiedBytes{};
        uint64_t recordCopiedBytes{};
    };

    struct BalloonPublicationMetrics
    {
        uint32_t sourceTick{};
        uint64_t worklistEntries{};
        uint64_t worklistVisits{}; // Two linear passes in ConsumeEntityVisualChanges; sort comparisons excluded.
        uint64_t bootstrapVisits{};
        uint64_t payloadCopiedBytes{};
        uint64_t compatibilityCopiedBytes{};
        uint64_t recordCopiedBytes{};
        uint64_t bulkCopiedBytes{};
        uint64_t fallbackSlotVisits{};
        uint64_t fallbackIndexedBalloons{};
        [[nodiscard]] uint64_t GetCopiedBalloonBytes() const noexcept
        {
            return payloadCopiedBytes + compatibilityCopiedBytes + recordCopiedBytes + bulkCopiedBytes;
        }
    };

    struct BalloonPublicationCopyTotals
    {
        uint64_t captures{};
        uint64_t worklistEntries{};
        uint64_t worklistVisits{};
        uint64_t payloadCopiedBytes{};
        uint64_t compatibilityCopiedBytes{};
        uint64_t recordCopiedBytes{};
        uint64_t bulkCopiedBytes{};
        [[nodiscard]] uint64_t GetCopiedBalloonBytes() const noexcept
        {
            return payloadCopiedBytes + compatibilityCopiedBytes + recordCopiedBytes + bulkCopiedBytes;
        }
    };

    // Single publisher; readers keep immutable complete snapshots. Not wired into software capture or frame admission.
    class RetainedBalloonScene
    {
        std::shared_ptr<const RetainedBalloonSnapshot> _snapshot;
        RetainedBalloonApplyMetrics _lastApplyMetrics;

    public:
        // Sequence belongs to the single authoritative publisher, never the dropped render packet counter.
        // Duplicate/older sequence returns false. Missing input deltas require a full reset batch.
        // Render consumers may skip any complete snapshots. Invalid batches leave the published snapshot unchanged.
        bool Apply(const EntityVisualChangeBatch& batch, uint64_t sequence);
        [[nodiscard]] RetainedBalloonApplyMetrics GetLastApplyMetrics() const noexcept
        {
            return _lastApplyMetrics;
        }
        [[nodiscard]] const std::shared_ptr<const RetainedBalloonSnapshot>& GetSnapshot() const noexcept
        {
            return _snapshot;
        }
    };
} // namespace OpenRCT2::Drawing
