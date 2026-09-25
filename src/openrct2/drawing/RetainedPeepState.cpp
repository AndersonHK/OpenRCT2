/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "RetainedPeepState.h"

#include "../object/PeepAnimationsObject.h"
#include "../peep/PeepAnimations.h"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace OpenRCT2::Drawing
{
    namespace
    {
        bool IsStaleGeneration(uint32_t old, uint32_t next)
        {
            return old != 0 && next - old >= (uint32_t{ 1 } << 31);
        }

        void ValidateSequence(uint64_t oldEpoch, uint64_t oldSequence, uint64_t epoch, uint64_t sequence, bool reset)
        {
            if (epoch == 0 || sequence == 0 || (oldSequence == 0 && !reset) || epoch < oldEpoch || (epoch != oldEpoch && !reset)
                || (oldEpoch != 0 && epoch == oldEpoch && reset) || sequence <= oldSequence
                || (!reset && sequence - oldSequence != 1))
                throw std::invalid_argument("Invalid retained peep epoch or publication sequence");
        }

        void ValidateDescriptor(const RetainedPeepAnimationDescriptor& d)
        {
            if (d.objectIndex >= UINT16_MAX || d.objectGeneration == 0 || d.groupCount == 0 || d.groupCount > 256
                || d.factOffset != 0 || d.imageCount == 0 || d.imageBase == UINT32_MAX
                || d.imageCount > UINT32_MAX - d.imageBase || d.reserved0 || d.reserved1)
                throw std::invalid_argument("Invalid retained peep animation object extent");
        }

        void ValidateObject(const RetainedPeepAnimationObject& object)
        {
            const auto& d = object.descriptor;
            ValidateDescriptor(d);
            if (object.facts.size() != static_cast<size_t>(d.groupCount) * kRetainedPeepAnimationTypes)
                throw std::invalid_argument("Invalid retained peep animation fact extent");
            for (const auto& f : object.facts)
            {
                if (f.valid > 1 || f.reserved0 || f.reserved1 || (!f.valid && f.baseImage != 0)
                    || (f.valid && (f.baseImage < d.imageBase || f.baseImage - d.imageBase >= d.imageCount)))
                    throw std::invalid_argument("Invalid retained peep animation fact");
            }
        }
    } // namespace

    RetainedPeepFields SplitRetainedPeepRecord(const RetainedPeepRecord& r) noexcept
    {
        return { r.id,
                 { r.generation, r.flags & ~(kRetainedPeepInterpolate | kRetainedPeepStateMask) },
                 { r.x, r.y, r.z, r.orientation, r.previousX, r.previousY, r.previousZ, r.sourceTick, r.previousTick,
                   r.flags & kRetainedPeepInterpolate },
                 { r.objectIndex, r.objectGeneration, r.colours, r.accessoryColours },
                 { r.action, r.animationGroup, r.animationType, r.nextAnimationType, r.frameOffset, r.width, r.heightMin,
                   r.heightMax, r.flags & kRetainedPeepStateMask } };
    }

    RetainedPeepRecord JoinRetainedPeepFields(const RetainedPeepFields& f) noexcept
    {
        RetainedPeepRecord r{};
        r.id = f.id;
        r.generation = f.lifecycle.generation;
        r.flags = f.lifecycle.flags | f.motion.flags | f.animation.flags;
        r.x = f.motion.x;
        r.y = f.motion.y;
        r.z = f.motion.z;
        r.orientation = f.motion.orientation;
        r.previousX = f.motion.previousX;
        r.previousY = f.motion.previousY;
        r.previousZ = f.motion.previousZ;
        r.sourceTick = f.motion.sourceTick;
        r.previousTick = f.motion.previousTick;
        r.objectIndex = f.appearance.objectIndex;
        r.objectGeneration = f.appearance.objectGeneration;
        r.colours = f.appearance.colours;
        r.accessoryColours = f.appearance.accessoryColours;
        r.action = f.animation.action;
        r.animationGroup = f.animation.animationGroup;
        r.animationType = f.animation.animationType;
        r.nextAnimationType = f.animation.nextAnimationType;
        r.frameOffset = f.animation.frameOffset;
        r.width = f.animation.width;
        r.heightMin = f.animation.heightMin;
        r.heightMax = f.animation.heightMax;
        return r;
    }

    std::optional<RetainedPeepRecord> RetainedPeepSnapshot::TryGet(EntityId id) const noexcept
    {
        const size_t index = id.ToUnderlying();
        if (index >= kMaxEntities)
            return std::nullopt;
        const auto& c = chunks[index / kRetainedPeepChunkWidth];
        const auto offset = index % kRetainedPeepChunkWidth;
        if (!c || !c->lifecycle || !(c->lifecycle->values[offset].flags & kRetainedPeepPresent))
            return std::nullopt;
        return JoinRetainedPeepFields({ static_cast<uint32_t>(index), c->lifecycle->values[offset], c->motion->values[offset],
                                        c->appearance->values[offset], c->animation->values[offset] });
    }

    namespace
    {
        template<typename T>
        bool SetField(
            std::shared_ptr<const RetainedPeepFieldChunk<T>>& published, std::shared_ptr<RetainedPeepFieldChunk<T>>& writable,
            size_t offset, const T& value, uint64_t sequence, bool force, RetainedPeepApplyMetrics& metrics)
        {
            if (!force && published && published->values[offset] == value)
                return false;
            if (!writable)
            {
                writable = published ? std::make_shared<RetainedPeepFieldChunk<T>>(*published)
                                     : std::make_shared<RetainedPeepFieldChunk<T>>();
                if (published)
                {
                    ++metrics.clonedChunks;
                    metrics.copiedRecordBytes += sizeof(writable->values);
                    metrics.copiedRevisionBytes += sizeof(writable->revisions);
                }
                published = writable;
            }
            writable->values[offset] = value;
            writable->revisions[offset] = sequence;
            metrics.copiedRecordBytes += sizeof(T);
            metrics.copiedRevisionBytes += sizeof(uint64_t);
            return true;
        }
        struct WritablePeepChunk
        {
            std::shared_ptr<RetainedPeepChunk> owner;
            std::shared_ptr<RetainedPeepFieldChunk<RetainedPeepLifecycle>> lifecycle;
            std::shared_ptr<RetainedPeepFieldChunk<RetainedPeepMotion>> motion;
            std::shared_ptr<RetainedPeepFieldChunk<RetainedPeepAppearance>> appearance;
            std::shared_ptr<RetainedPeepFieldChunk<RetainedPeepAnimation>> animation;
        };
    } // namespace

    bool RetainedPeepScene::Apply(const RetainedPeepBatch& batch, uint64_t sequence)
    {
        if (_snapshot && batch.epoch == _snapshot->epoch && sequence != 0 && sequence <= _snapshot->sequence)
            return false;
        ValidateSequence(
            _snapshot ? _snapshot->epoch : 0, _snapshot ? _snapshot->sequence : 0, batch.epoch, sequence, batch.reset);
        struct Pending
        {
            uint32_t id{};
            const RetainedPeepLifecycleUpdate* lifecycle{};
            const RetainedPeepFieldUpdate<RetainedPeepMotion>* motion{};
            const RetainedPeepFieldUpdate<RetainedPeepAppearance>* appearance{};
            const RetainedPeepFieldUpdate<RetainedPeepAnimation>* animation{};
            RetainedPeepLifecycle oldLife{}, life{};
            bool newIdentity{};
        };
        // Contiguous touched entries avoid a heap node per moving peep. Nonempty batches pay one bounded
        // slot-index clear; empty batches allocate neither work/index arrays nor writable field chunks.
        constexpr uint32_t missing = UINT32_MAX;
        std::vector<Pending> pending;
        std::vector<uint32_t> slotToPending;
        const auto updateCount = batch.lifecycle.size() + batch.motion.size() + batch.appearance.size()
            + batch.animation.size();
        if (updateCount != 0)
        {
            pending.reserve(std::min(updateCount, static_cast<size_t>(kMaxEntities)));
            slotToPending.resize(kMaxEntities, missing);
        }
        auto collect = [&](const auto& updates, auto member) {
            for (const auto& update : updates)
            {
                if (update.id >= kMaxEntities)
                    throw std::invalid_argument("Invalid retained peep slot");
                auto& index = slotToPending[update.id];
                if (index == missing)
                {
                    index = static_cast<uint32_t>(pending.size());
                    pending.push_back({ .id = update.id });
                }
                auto& entry = pending[index];
                if (entry.*member)
                    throw std::invalid_argument("Duplicate retained peep field group");
                entry.*member = &update;
            }
        };
        collect(batch.lifecycle, &Pending::lifecycle);
        collect(batch.motion, &Pending::motion);
        collect(batch.appearance, &Pending::appearance);
        collect(batch.animation, &Pending::animation);
        for (auto& entry : pending)
        {
            const auto id = entry.id;
            if (_snapshot && !batch.reset)
            {
                const auto& chunk = _snapshot->chunks[id / kRetainedPeepChunkWidth];
                if (chunk && chunk->lifecycle)
                    entry.oldLife = chunk->lifecycle->values[id % kRetainedPeepChunkWidth];
            }
            entry.life = entry.lifecycle ? entry.lifecycle->value : entry.oldLife;
            const auto& life = entry.life;
            const auto& old = entry.oldLife;
            const bool present = (life.flags & kRetainedPeepPresent) != 0;
            if (life.generation == 0 || (life.flags & ~(kRetainedPeepPresent | kRetainedPeepStaff))
                || (!present && life.flags != 0))
                throw std::invalid_argument("Invalid retained peep lifecycle");
            if (IsStaleGeneration(old.generation, life.generation)
                || (old.generation == life.generation && present
                    && (!(old.flags & kRetainedPeepPresent) || ((old.flags ^ life.flags) & kRetainedPeepStaff))))
                throw std::invalid_argument("Stale or reused retained peep generation");
            entry.newIdentity = present && (old.generation != life.generation || !(old.flags & kRetainedPeepPresent));
            if (entry.newIdentity && (!entry.lifecycle || !entry.motion || !entry.appearance || !entry.animation))
                throw std::invalid_argument("New retained peep identity requires every field group");
            auto validateIdentity = [&](const auto* update) {
                if (update && (!present || update->generation != life.generation))
                    throw std::invalid_argument("Retained peep field belongs to a different or deleted identity");
            };
            validateIdentity(entry.motion);
            validateIdentity(entry.appearance);
            validateIdentity(entry.animation);
            if (entry.motion)
            {
                const auto& m = entry.motion->value;
                if (m.orientation > 31 || (m.flags & ~kRetainedPeepInterpolate))
                    throw std::invalid_argument("Invalid retained peep motion range");
                if (m.flags & kRetainedPeepInterpolate)
                {
                    const auto span = m.sourceTick - m.previousTick;
                    if (span == 0 || span >= (uint32_t{ 1 } << 31))
                        throw std::invalid_argument("Retained peep interpolation requires a positive unambiguous tick span");
                }
                else if (m.previousTick != m.sourceTick || m.previousX != m.x || m.previousY != m.y || m.previousZ != m.z)
                    throw std::invalid_argument("Retained peep without history has different previous state");
                if (!entry.newIdentity && m.flags == 0)
                {
                    const auto& oldMotion = _snapshot->chunks[id / kRetainedPeepChunkWidth]
                                                ->motion->values[id % kRetainedPeepChunkWidth];
                    auto comparison = m;
                    comparison.sourceTick = oldMotion.sourceTick;
                    comparison.previousTick = oldMotion.previousTick;
                    // Redundant full/transform notifications must not retime a stationary sample.
                    // Explicit authoritative interpolation history, including tick wrap, remains authoritative.
                    if (comparison == oldMotion)
                        entry.motion = nullptr;
                }
            }
            if (entry.appearance)
            {
                const auto& a = entry.appearance->value;
                if (a.objectIndex >= UINT16_MAX || a.objectGeneration == 0 || a.colours > UINT16_MAX)
                    throw std::invalid_argument("Invalid retained peep appearance range");
            }
            if (entry.animation)
            {
                const auto& a = entry.animation->value;
                if (a.action > UINT8_MAX || a.animationGroup > UINT8_MAX || a.animationType > UINT8_MAX
                    || a.nextAnimationType > UINT8_MAX || a.frameOffset > UINT8_MAX || a.width > UINT8_MAX
                    || a.heightMin > UINT8_MAX || a.heightMax > UINT8_MAX || (a.flags & ~kRetainedPeepStateMask))
                    throw std::invalid_argument("Invalid retained peep animation range");
            }
        }
        // All validation and allocations precede publication; held snapshots and metrics survive any failure.
        auto next = _snapshot && !batch.reset ? std::make_shared<RetainedPeepSnapshot>(*_snapshot)
                                              : std::make_shared<RetainedPeepSnapshot>();
        next->epoch = batch.epoch;
        next->sequence = sequence;
        std::shared_ptr<std::map<uint32_t, uint32_t>> objectUseCounts;
        for (const auto& entry : pending)
        {
            const bool wasPresent = (entry.oldLife.flags & kRetainedPeepPresent) != 0;
            const bool present = (entry.life.flags & kRetainedPeepPresent) != 0;
            if (!entry.lifecycle && !entry.appearance)
                continue;
            uint32_t oldObject = 0;
            if (wasPresent)
                oldObject = _snapshot->chunks[entry.id / kRetainedPeepChunkWidth]
                                ->appearance->values[entry.id % kRetainedPeepChunkWidth]
                                .objectIndex;
            const auto newObject = entry.appearance ? entry.appearance->value.objectIndex : oldObject;
            if (wasPresent == present && (!present || oldObject == newObject))
                continue;
            if (!objectUseCounts)
                objectUseCounts = next->objectUseCounts ? std::make_shared<std::map<uint32_t, uint32_t>>(*next->objectUseCounts)
                                                        : std::make_shared<std::map<uint32_t, uint32_t>>();
            if (wasPresent)
            {
                auto old = objectUseCounts->find(oldObject);
                if (old == objectUseCounts->end() || old->second == 0)
                    throw std::logic_error("Retained peep object usage is inconsistent");
                if (--old->second == 0)
                    objectUseCounts->erase(old);
            }
            if (present)
                ++(*objectUseCounts)[newObject];
        }
        if (objectUseCounts || !next->usedObjects)
        {
            if (!objectUseCounts)
                objectUseCounts = std::make_shared<std::map<uint32_t, uint32_t>>();
            auto usedObjects = std::make_shared<std::vector<uint32_t>>();
            usedObjects->reserve(objectUseCounts->size());
            for (const auto& [object, owners] : *objectUseCounts)
                usedObjects->push_back(object);
            if (!next->usedObjects || *next->usedObjects != *usedObjects)
                next->usedObjects = std::move(usedObjects);
            next->objectUseCounts = std::move(objectUseCounts);
        }
        RetainedPeepApplyMetrics metrics{};
        std::vector<WritablePeepChunk> writable;
        std::vector<uint32_t> chunkToWritable;
        if (!pending.empty())
        {
            writable.reserve(std::min(pending.size(), kRetainedPeepChunkCount));
            chunkToWritable.resize(kRetainedPeepChunkCount, missing);
        }
        for (const auto& entry : pending)
        {
            const auto id = entry.id;
            const size_t ci = id / kRetainedPeepChunkWidth, offset = id % kRetainedPeepChunkWidth;
            const auto& old = next->chunks[ci];
            const bool lifeChanged = entry.lifecycle && entry.oldLife != entry.life;
            const bool motionChanged = entry.motion
                && (entry.newIdentity || !old->motion || old->motion->values[offset] != entry.motion->value);
            const bool appearanceChanged = entry.appearance
                && (entry.newIdentity || !old->appearance || old->appearance->values[offset] != entry.appearance->value);
            const bool animationChanged = entry.animation
                && (entry.newIdentity || !old->animation || old->animation->values[offset] != entry.animation->value);
            if (!(lifeChanged || motionChanged || appearanceChanged || animationChanged))
                continue;
            auto& wi = chunkToWritable[ci];
            if (wi == missing)
            {
                wi = static_cast<uint32_t>(writable.size());
                writable.emplace_back();
            }
            auto& w = writable[wi];
            if (!w.owner)
            {
                w.owner = old ? std::make_shared<RetainedPeepChunk>(*old) : std::make_shared<RetainedPeepChunk>();
                if (old)
                    metrics.copiedChunkMetadataBytes += sizeof(RetainedPeepChunk);
                next->chunks[ci] = w.owner;
            }
            auto& c = *w.owner;
            if (lifeChanged)
                SetField(c.lifecycle, w.lifecycle, offset, entry.life, sequence, true, metrics);
            if (motionChanged)
                SetField(c.motion, w.motion, offset, entry.motion->value, sequence, true, metrics);
            if (appearanceChanged)
                SetField(c.appearance, w.appearance, offset, entry.appearance->value, sequence, true, metrics);
            if (animationChanged)
                SetField(c.animation, w.animation, offset, entry.animation->value, sequence, true, metrics);
            const bool wasPresent = (entry.oldLife.flags & kRetainedPeepPresent) != 0;
            const bool present = (entry.life.flags & kRetainedPeepPresent) != 0;
            if (wasPresent && !present)
                --next->count;
            if (!wasPresent && present)
                ++next->count;
            c.revision = sequence;
            ++metrics.changedRecords;
        }
        _snapshot = std::move(next);
        _metrics = metrics;
        return true;
    }

    size_t RetainedPeepBatch::PayloadBytes() const noexcept
    {
        return lifecycle.size() * sizeof(RetainedPeepLifecycleUpdate)
            + motion.size() * sizeof(RetainedPeepFieldUpdate<RetainedPeepMotion>)
            + appearance.size() * sizeof(RetainedPeepFieldUpdate<RetainedPeepAppearance>)
            + animation.size() * sizeof(RetainedPeepFieldUpdate<RetainedPeepAnimation>);
    }

    size_t RetainedPeepFieldDelta::PayloadBytes() const noexcept
    {
        return lifecycle.size() * sizeof(RetainedPeepLifecycleUpdate)
            + motion.size() * sizeof(RetainedPeepFieldUpdate<RetainedPeepMotion>)
            + appearance.size() * sizeof(RetainedPeepFieldUpdate<RetainedPeepAppearance>)
            + animation.size() * sizeof(RetainedPeepFieldUpdate<RetainedPeepAnimation>);
    }

    RetainedPeepFieldDelta RetainedPeepFieldConsumer::Prepare(std::shared_ptr<const RetainedPeepSnapshot> snapshot) const
    {
        if (!snapshot || snapshot->epoch == 0 || snapshot->sequence == 0 || snapshot->epoch < _epoch
            || (snapshot->epoch == _epoch && snapshot->sequence < _sequence))
            throw std::invalid_argument("Stale retained peep consumer revision");
        RetainedPeepFieldDelta delta{ .baseEpoch = _epoch, .baseSequence = _sequence, .snapshot = std::move(snapshot) };
        delta.reset = delta.snapshot->epoch != _epoch;
        const auto since = delta.reset ? 0 : _sequence;
        for (size_t ci = 0; ci < kRetainedPeepChunkCount; ++ci)
        {
            const auto& c = delta.snapshot->chunks[ci];
            if (!c || c->revision <= since || !c->lifecycle)
                continue;
            for (size_t offset = 0; offset < kRetainedPeepChunkWidth; ++offset)
            {
                const auto id = static_cast<uint32_t>(ci * kRetainedPeepChunkWidth + offset);
                const auto& life = c->lifecycle->values[offset];
                if (id >= kMaxEntities || life.generation == 0)
                    continue;
                if (c->lifecycle->revisions[offset] > since)
                    delta.lifecycle.push_back({ id, life });
                if (!(life.flags & kRetainedPeepPresent))
                    continue;
                if (c->motion->revisions[offset] > since)
                    delta.motion.push_back({ id, life.generation, c->motion->values[offset] });
                if (c->appearance->revisions[offset] > since)
                    delta.appearance.push_back({ id, life.generation, c->appearance->values[offset] });
                if (c->animation->revisions[offset] > since)
                    delta.animation.push_back({ id, life.generation, c->animation->values[offset] });
            }
        }
        return delta;
    }

    void RetainedPeepFieldConsumer::Commit(const RetainedPeepFieldDelta& delta)
    {
        if (delta.baseEpoch != _epoch || delta.baseSequence != _sequence || !delta.snapshot || delta.snapshot->epoch == 0
            || delta.snapshot->epoch < _epoch || (delta.snapshot->epoch == _epoch && delta.snapshot->sequence < _sequence)
            || delta.snapshot->sequence == 0 || delta.reset != (delta.snapshot->epoch != _epoch))
            throw std::invalid_argument("Retained peep field commit has a stale base");
        _epoch = delta.snapshot->epoch;
        _sequence = delta.snapshot->sequence;
    }

    std::shared_ptr<const RetainedPeepAnimationObject> BuildRetainedPeepAnimationObject(
        RetainedPeepAnimationDescriptor descriptor, std::span<const RetainedPeepAnimationSource> sources)
    {
        ValidateDescriptor(descriptor);
        auto result = std::make_shared<RetainedPeepAnimationObject>();
        result->descriptor = descriptor;
        result->facts.resize(static_cast<size_t>(descriptor.groupCount) * kRetainedPeepAnimationTypes);
        for (const auto& source : sources)
        {
            if (source.group >= descriptor.groupCount || source.type >= kRetainedPeepAnimationTypes)
                throw std::invalid_argument("Retained peep animation source outside table");
            auto& fact = result->facts[source.group * kRetainedPeepAnimationTypes + source.type];
            if (fact.valid)
                throw std::invalid_argument("Duplicate retained peep animation source");
            fact = { source.baseImage, 1, 0, 0 };
        }
        ValidateObject(*result);
        return result;
    }

    std::shared_ptr<const RetainedPeepAnimationObject> CaptureRetainedPeepAnimationObject(
        const PeepAnimationsObject& object, uint32_t objectIndex, uint32_t generation)
    {
        const auto groups = object.GetNumAnimationGroups();
        if (groups == 0 || groups > 256 || object.GetNumImages() == 0 || object.GetBaseImageId() == kImageIndexUndefined)
            throw std::invalid_argument("Peep animation object is empty or unloaded");
        std::optional<uint32_t> imageBase;
        std::vector<RetainedPeepAnimationSource> sources;
        for (uint32_t group = 0; group < groups; ++group)
        {
            for (const auto& entry : getAnimationsByPeepType(object.GetPeepType()))
            {
                const auto type = entry.second;
                const auto& animation = object.GetPeepAnimation(static_cast<PeepAnimationGroup>(group), type);
                if (animation.imageTableOffset >= object.GetNumImages() || animation.baseImage < animation.imageTableOffset)
                    throw std::invalid_argument("Peep animation object has invalid loaded image offset");
                const uint32_t base = animation.baseImage - animation.imageTableOffset;
                if (base != object.GetBaseImageId() || (imageBase && *imageBase != base))
                    throw std::invalid_argument("Peep animation object has inconsistent loaded image bases");
                imageBase = base;
                sources.push_back({ group, static_cast<uint32_t>(type), animation.baseImage });
            }
        }
        if (!imageBase)
            throw std::invalid_argument("Peep animation object has no animation rules");
        return BuildRetainedPeepAnimationObject(
            { objectIndex, generation, static_cast<uint32_t>(groups), 0, *imageBase, object.GetNumImages(), 0, 0 }, sources);
    }

    std::shared_ptr<const RetainedPeepAnimationCatalog> PublishRetainedPeepAnimationCatalog(
        const std::shared_ptr<const RetainedPeepAnimationCatalog>& previous, uint64_t epoch, uint64_t sequence, bool reset,
        std::span<const RetainedPeepAnimationChange> changes)
    {
        if (previous && epoch == previous->epoch && sequence != 0 && sequence <= previous->sequence)
            return previous;
        ValidateSequence(previous ? previous->epoch : 0, previous ? previous->sequence : 0, epoch, sequence, reset);
        std::set<uint32_t> seen;
        auto next = previous && !reset ? std::make_shared<RetainedPeepAnimationCatalog>(*previous)
                                       : std::make_shared<RetainedPeepAnimationCatalog>();
        next->epoch = epoch;
        next->sequence = sequence;
        for (const auto& change : changes)
        {
            if (change.objectIndex >= UINT16_MAX || change.generation == 0 || !seen.insert(change.objectIndex).second)
                throw std::invalid_argument("Invalid retained peep animation slot identity");
            const auto prior = next->slots.find(change.objectIndex);
            if (prior != next->slots.end()
                && (IsStaleGeneration(prior->second.generation, change.generation)
                    || (change.object && prior->second.generation == change.generation)))
                throw std::invalid_argument("Retained peep object replacement must advance generation");
            std::shared_ptr<const RetainedPeepAnimationObject> owned;
            if (change.object)
            {
                ValidateObject(*change.object);
                if (change.object->descriptor.objectIndex != change.objectIndex
                    || change.object->descriptor.objectGeneration != change.generation)
                    throw std::invalid_argument("Retained peep animation object identity mismatch");
                // Copies only on object changes, so a caller retaining a mutable alias cannot change published facts.
                owned = std::make_shared<const RetainedPeepAnimationObject>(*change.object);
            }
            next->slots[change.objectIndex] = { change.generation, std::move(owned) };
        }
        return next;
    }
} // namespace OpenRCT2::Drawing
