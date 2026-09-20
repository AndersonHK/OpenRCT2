/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "RetainedBalloonScene.h"

#include <bitset>
#include <cstring>
#include <stdexcept>

namespace OpenRCT2::Drawing
{
    const Balloon* RetainedBalloonSnapshot::TryGet(EntityId id) const noexcept
    {
        const size_t index = id.ToUnderlying();
        if (index >= kMaxEntities)
            return nullptr;
        const auto& chunk = chunks[index / kRetainedBalloonChunkWidth];
        const auto offset = index % kRetainedBalloonChunkWidth;
        return chunk != nullptr && chunk->records[offset].present != 0 ? &chunk->compatibility[offset] : nullptr;
    }

    bool RetainedBalloonScene::Apply(const EntityVisualChangeBatch& batch, uint64_t sequence)
    {
        if (_snapshot != nullptr && sequence <= _snapshot->sequence)
            return false;
        if (sequence == 0 || batch.epoch == 0 || (_snapshot == nullptr && !batch.reset)
            || (_snapshot != nullptr && batch.epoch != _snapshot->epoch && !batch.reset)
            || (_snapshot != nullptr && batch.epoch < _snapshot->epoch)
            || (_snapshot != nullptr && !batch.reset && sequence - _snapshot->sequence != 1))
            throw std::invalid_argument("Invalid retained balloon publication epoch or sequence");

        RetainedBalloonApplyMetrics metrics{};
        // Validate before mutation: a bad tail cannot publish a partially applied batch.
        std::bitset<kMaxEntities> seen;
        for (const auto& change : batch.changes)
        {
            const size_t index = change.handle.id.ToUnderlying();
            if (index >= kMaxEntities || change.handle.epoch != batch.epoch || change.handle.generation == 0
                || seen.test(index))
                throw std::invalid_argument("Invalid or duplicate retained balloon handle");
            seen.set(index);
            if (!batch.reset && _snapshot != nullptr)
            {
                const auto& chunk = _snapshot->chunks[index / kRetainedBalloonChunkWidth];
                if (chunk != nullptr)
                {
                    const auto& old = chunk->records[index % kRetainedBalloonChunkWidth];
                    // Registry slot generations skip zero when uint32_t wraps. Half-range ordering
                    // accepts that valid reuse while rejecting delayed handles.
                    const uint32_t generationDelta = change.handle.generation - old.generation;
                    if ((old.generation != 0 && generationDelta >= (uint32_t{ 1 } << 31))
                        || (change.present && change.type == EntityType::balloon && old.generation != 0
                            && change.handle.generation == old.generation && old.present == 0))
                        throw std::invalid_argument("Stale retained balloon generation");
                }
            }
            if (change.present && change.type == EntityType::balloon)
            {
                if (change.payloadSize != sizeof(Balloon) || change.payloadOffset > batch.payload.size()
                    || change.payloadSize > batch.payload.size() - change.payloadOffset)
                    throw std::invalid_argument("Invalid retained balloon payload extent");
                Balloon owned{};
                std::memcpy(&owned, batch.payload.data() + change.payloadOffset, sizeof(owned));
                metrics.compatibilityCopiedBytes += sizeof(owned);
                if (owned.type != EntityType::balloon || owned.id != change.handle.id)
                    throw std::invalid_argument("Retained balloon payload identity mismatch");
            }
        }
        auto next = !batch.reset && _snapshot != nullptr ? std::make_shared<RetainedBalloonSnapshot>(*_snapshot)
                                                         : std::make_shared<RetainedBalloonSnapshot>();
        next->epoch = batch.epoch;
        next->sequence = sequence;
        std::array<std::shared_ptr<RetainedBalloonChunk>, kRetainedBalloonChunkCount> writable{};
        for (const auto& change : batch.changes)
        {
            const size_t index = change.handle.id.ToUnderlying();
            const size_t ci = index / kRetainedBalloonChunkWidth;
            const size_t offset = index % kRetainedBalloonChunkWidth;
            const bool present = change.present && change.type == EntityType::balloon;
            // Ignore other families unless this slot previously held a balloon (including its tombstone).
            if (!present && (next->chunks[ci] == nullptr || next->chunks[ci]->records[offset].generation == 0))
                continue;
            auto& chunk = writable[ci];
            if (chunk == nullptr)
            {
                chunk = next->chunks[ci] != nullptr ? std::make_shared<RetainedBalloonChunk>(*next->chunks[ci])
                                                    : std::make_shared<RetainedBalloonChunk>();
                if (next->chunks[ci] != nullptr)
                {
                    metrics.compatibilityCopiedBytes += sizeof(chunk->compatibility);
                    metrics.recordCopiedBytes += sizeof(chunk->records);
                }
                next->chunks[ci] = chunk;
            }
            auto& old = chunk->records[offset];
            if (old.present != 0 && !present)
                --next->count;
            else if (old.present == 0 && present)
                ++next->count;
            RetainedBalloonRecord record{};
            record.id = static_cast<uint32_t>(index);
            record.generation = change.handle.generation;
            if (present)
            {
                auto& owned = chunk->compatibility[offset];
                std::memcpy(&owned, batch.payload.data() + change.payloadOffset, sizeof(owned));
                metrics.compatibilityCopiedBytes += sizeof(owned);
                record.x = owned.x;
                record.y = owned.y;
                record.z = owned.z;
                record.frame = owned.frame;
                record.popped = owned.popped;
                record.colour = static_cast<uint32_t>(owned.colour);
                record.width = owned.spriteData.width;
                record.heightMin = owned.spriteData.heightMin;
                record.heightMax = owned.spriteData.heightMax;
                record.present = 1;
            }
            else
                chunk->compatibility[offset] = {};
            chunk->revision = sequence;
            if (old != record)
                chunk->gpuRevision = sequence;
            old = record;
            metrics.recordCopiedBytes += sizeof(record);
        }
        _snapshot = std::move(next);
        _lastApplyMetrics = metrics;
        return true;
    }
} // namespace OpenRCT2::Drawing
