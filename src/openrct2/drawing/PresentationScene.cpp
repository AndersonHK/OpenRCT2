/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "PresentationScene.h"

#include "../GameState.h"
#include "../core/JobPool.h"
#include "../entity/EntityPresentationSnapshot.h"
#include "../world/MapPresentationSnapshot.h"
#include "PresentationTask.h"
#include "RetainedBalloonScene.h"
#include "RetainedPeepState.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace OpenRCT2
{
    namespace
    {
        // A render consumer can outlive a scene owner. Allocate only at a complete combined-family
        // bootstrap, so recreated owners cannot alias held snapshots or resident chunk revisions.
        std::atomic<uint64_t> _nextRetainedPublicationEpoch{ 1 };
        uint64_t NextRetainedPublicationEpoch()
        {
            auto epoch = _nextRetainedPublicationEpoch.load(std::memory_order_relaxed);
            do
            {
                if (epoch == UINT64_MAX)
                    throw std::overflow_error("Retained publication epoch exhausted");
            } while (!_nextRetainedPublicationEpoch.compare_exchange_weak(epoch, epoch + 1, std::memory_order_relaxed));
            return epoch;
        }

        class MapPresentationPublisher final
        {
        private:
            std::shared_ptr<const MapPresentationSnapshot> _front;
            std::shared_ptr<MapPresentationSnapshot> _pending;
            std::optional<JobPool::TaskGroup> _pendingGroup;
            bool _pendingReset{};
            MapPublicationProfile _profile{ MapPublicationProfile::legacyTiles };

        public:
            struct AcquireResult
            {
                std::shared_ptr<const MapPresentationSnapshot> snapshot;
                bool sceneReset{};
            };

            bool HasPending() const
            {
                return _pendingGroup.has_value();
            }
            bool IsReady() const
            {
                return !HasPending() || _pendingGroup->IsComplete();
            }

            void Reset(JobPool& jobs)
            {
                if (_pendingGroup.has_value())
                    Detail::WaitAndReleasePresentationTask(jobs, _pendingGroup);
                _front.reset();
                _pending.reset();
                _pendingGroup.reset();
                _pendingReset = false;
            }

            void SetProfile(JobPool& jobs, MapPublicationProfile profile)
            {
                if (_profile == profile)
                    return;
                Reset(jobs);
                _profile = profile;
            }

            AcquireResult Acquire(JobPool& jobs)
            {
                bool sceneReset = false;
                if (_pendingGroup.has_value() && _pendingGroup->IsComplete())
                {
                    Detail::WaitAndReleasePresentationTask(jobs, _pendingGroup);
                    _front = std::move(_pending);
                    _pendingGroup.reset();
                    sceneReset = std::exchange(_pendingReset, false);
                }

                if (_front == nullptr)
                {
                    auto initial = std::make_shared<MapPresentationSnapshot>();
                    auto changes = ConsumeMapPresentationChanges(true, _profile);
                    sceneReset |= changes.reset;
                    initial->Apply(changes);
                    _front = std::move(initial);
                }
                return { _front, sceneReset };
            }

            AcquireResult AcquireSynchronously(JobPool& jobs)
            {
                bool sceneReset = false;
                if (_pendingGroup.has_value())
                {
                    Detail::WaitAndReleasePresentationTask(jobs, _pendingGroup);
                    _front = std::move(_pending);
                    _pendingGroup.reset();
                    sceneReset = std::exchange(_pendingReset, false);
                }

                auto changes = ConsumeMapPresentationChanges(_front == nullptr, _profile);
                sceneReset |= changes.reset;
                if (_front == nullptr)
                {
                    auto current = std::make_shared<MapPresentationSnapshot>();
                    current->Apply(changes);
                    _front = std::move(current);
                }
                else if (
                    changes.reset || !changes.changes.empty() || changes.sourceTick != _front->GetSourceTick()
                    || changes.terrainMaterials != _front->GetTerrainMaterials())
                {
                    auto current = std::make_shared<MapPresentationSnapshot>(*_front);
                    current->Apply(changes);
                    _front = std::move(current);
                }
                return { _front, sceneReset };
            }

            void Schedule(JobPool& jobs)
            {
                if (_front == nullptr || _pendingGroup.has_value())
                    return;

                auto changes = ConsumeMapPresentationChanges(false, _profile);
                if (!changes.reset && changes.changes.empty() && changes.sourceTick == _front->GetSourceTick()
                    && changes.terrainMaterials == _front->GetTerrainMaterials())
                    return;

                _pendingReset = changes.reset;
                _pending = std::make_shared<MapPresentationSnapshot>(*_front);
                _pendingGroup.emplace(jobs.CreateTaskGroup());
                const auto target = _pending;
                jobs.AddTask(
                    *_pendingGroup, [target, changes = std::move(changes)]() { target->Apply(changes); },
                    JobPool::TaskPriority::background);
            }
        };

        class EntityPresentationPublisher final
        {
        private:
            std::shared_ptr<const EntityPresentationSnapshot> _front;
            std::shared_ptr<EntityPresentationSnapshot> _pending;
            std::shared_ptr<EntityPresentationSnapshot> _recycle;
            std::optional<JobPool::TaskGroup> _pendingGroup;

            EntityPublicationProfile _profile = EntityPublicationProfile::legacyBulk;
            Drawing::RetainedBalloonScene _balloons;
            Drawing::RetainedPeepScene _peeps;
            std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> _peepAnimations;
            std::vector<uint32_t> _peepObjectGenerations;
            uint64_t _peepSourceEpoch{};
            uint64_t _retainedPublicationEpoch{};
            // Lifetime-monotonic even across profile/reset bootstrap: the same epoch must never reuse GPU revisions.
            uint64_t _balloonSequence{};
            Drawing::BalloonPublicationCopyTotals _balloonCopyTotals{};

            void CaptureRetainedFamilies(EntityPresentationSnapshot& target, EntityRegistry& registry)
            {
                if (_peepAnimations == nullptr)
                    throw std::invalid_argument("Combined peep publication requires an owned animation catalog");
                const bool bootstrap = _peeps.GetSnapshot() == nullptr || _peepSourceEpoch != registry.GetEntityVisualEpoch();
                if (_balloonSequence == UINT64_MAX)
                    throw std::overflow_error("Retained publication identity exhausted");
                const auto epoch = bootstrap ? NextRetainedPublicationEpoch() : _retainedPublicationEpoch;
                const auto sequence = _balloonSequence + 1;
                auto input = registry.CaptureRetainedEntityPublication(
                    getGameState().currentTicks, _peepObjectGenerations, bootstrap,
                    _profile != EntityPublicationProfile::nativePeeps);
                input.peeps.epoch = input.balloons.epoch = epoch;
                // Existing balloon adapter validates handle epochs as well as batch identity.
                for (auto& change : input.balloons.changes)
                    change.handle.epoch = epoch;
                auto nextPeeps = _peeps;
                auto nextBalloons = _balloons;
                nextPeeps.Apply(input.peeps, sequence);
                if (_profile != EntityPublicationProfile::nativePeeps)
                    nextBalloons.Apply(input.balloons, sequence);
                Drawing::BalloonPublicationMetrics metrics{};
                metrics.sourceTick = input.sourceTick;
                metrics.worklistEntries = input.dirtyVisits;
                metrics.worklistVisits = 2 * input.dirtyVisits; // Capture plus acknowledgement; no sorting.
                metrics.bootstrapVisits = input.bootstrapVisits;
                metrics.payloadCopiedBytes = input.balloons.payload.size();
                const auto copied = nextBalloons.GetLastApplyMetrics();
                metrics.compatibilityCopiedBytes = copied.compatibilityCopiedBytes;
                metrics.recordCopiedBytes = copied.recordCopiedBytes;
                if (_profile == EntityPublicationProfile::nativePeeps)
                    target.CaptureNativeStorage(registry, nextPeeps.GetSnapshot(), _peepAnimations);
                else
                    target.CaptureStorage(
                        registry, nextBalloons.GetSnapshot(), metrics, nextPeeps.GetSnapshot(), _peepAnimations);
                // No mutation may intervene between prepare and this acknowledgement on the authoritative owner.
                registry.AcknowledgeRetainedEntityPublication();
                _peeps = std::move(nextPeeps);
                _balloons = std::move(nextBalloons);
                _peepSourceEpoch = input.sourceEpoch;
                _retainedPublicationEpoch = epoch;
                _balloonSequence = sequence;
                ++_balloonCopyTotals.captures;
                _balloonCopyTotals.worklistEntries += metrics.worklistEntries;
                _balloonCopyTotals.worklistVisits += metrics.worklistVisits;
                _balloonCopyTotals.payloadCopiedBytes += metrics.payloadCopiedBytes;
                _balloonCopyTotals.compatibilityCopiedBytes += metrics.compatibilityCopiedBytes;
                _balloonCopyTotals.recordCopiedBytes += metrics.recordCopiedBytes;
                _balloonCopyTotals.bulkCopiedBytes += target.GetBalloonMetrics().bulkCopiedBytes;
            }

            void Capture(EntityPresentationSnapshot& target, EntityRegistry& registry)
            {
                if (_profile == EntityPublicationProfile::gpuTerrainOnly)
                {
                    // The first GPU-only layer owns no entity graphics yet. Leave simulation and its
                    // coalesced mutation worklist intact; a later family activation bootstraps its state.
                    target.CaptureNativeStorage(registry, {}, {});
                    return;
                }
                if (_profile == EntityPublicationProfile::legacyBulk)
                {
                    target.CaptureStorage(registry);
                    return;
                }
                if ((_profile == EntityPublicationProfile::retainedPeepsAndBalloons
                     || _profile == EntityPublicationProfile::nativePeeps))
                {
                    CaptureRetainedFamilies(target, registry);
                    return;
                }
                Drawing::BalloonPublicationMetrics metrics{};
                metrics.sourceTick = getGameState().currentTicks;
                // This is the only production owner of the dirty worklist. Other families still use bulk capture.
                auto changes = registry.ConsumeEntityVisualChanges(EntityType::balloon);
                metrics.worklistEntries = changes.changes.size();
                metrics.worklistVisits = 2 * metrics.worklistEntries;
                metrics.payloadCopiedBytes = changes.payload.size();
                if (_balloons.GetSnapshot() == nullptr || changes.reset || _balloons.GetSnapshot()->epoch != changes.epoch)
                {
                    // A profile switch may occur after an earlier consumer cleared the worklist. Bootstrap finalized
                    // live family state, never assume the outstanding deltas contain every existing balloon.
                    EntityVisualChangeBatch full{ .epoch = changes.epoch, .reset = true };
                    const auto& entries = registry.GetEntityExecutionList(EntityType::balloon);
                    metrics.bootstrapVisits = entries.size();
                    full.changes.reserve(entries.size());
                    full.payload.reserve(entries.size() * sizeof(Balloon));
                    for (const auto* entity : entries)
                    {
                        EntityVisualChange change{};
                        change.handle = registry.GetEntityVisualHandle(entity->id);
                        change.type = EntityType::balloon;
                        change.present = true;
                        change.dirty = EntityVisualDirty::full;
                        change.location = entity->getLocation();
                        change.spriteData = entity->spriteData;
                        change.orientation = entity->orientation;
                        change.payloadOffset = static_cast<uint32_t>(full.payload.size());
                        change.payloadSize = static_cast<uint16_t>(sizeof(Balloon));
                        const auto* bytes = reinterpret_cast<const std::byte*>(entity);
                        full.payload.insert(full.payload.end(), bytes, bytes + sizeof(Balloon));
                        full.changes.push_back(change);
                    }
                    metrics.payloadCopiedBytes += full.payload.size();
                    changes = std::move(full);
                    _balloons = {};
                }
                if (_balloonSequence == std::numeric_limits<uint64_t>::max())
                    throw std::overflow_error("Retained balloon publication sequence exhausted");
                _balloons.Apply(changes, ++_balloonSequence);
                const auto copied = _balloons.GetLastApplyMetrics();
                metrics.compatibilityCopiedBytes = copied.compatibilityCopiedBytes;
                metrics.recordCopiedBytes = copied.recordCopiedBytes;
                // Both captures occur on the presentation owner thread at this same completed-state boundary.
                target.CaptureStorage(registry, _balloons.GetSnapshot(), metrics);
                ++_balloonCopyTotals.captures;
                _balloonCopyTotals.worklistEntries += metrics.worklistEntries;
                _balloonCopyTotals.worklistVisits += metrics.worklistVisits;
                _balloonCopyTotals.payloadCopiedBytes += metrics.payloadCopiedBytes;
                _balloonCopyTotals.compatibilityCopiedBytes += metrics.compatibilityCopiedBytes;
                _balloonCopyTotals.recordCopiedBytes += metrics.recordCopiedBytes;
                _balloonCopyTotals.bulkCopiedBytes += target.GetBalloonMetrics().bulkCopiedBytes;
            }

        public:
            bool HasPending() const
            {
                return _pending != nullptr;
            }
            bool IsReady() const
            {
                return !_pendingGroup.has_value() || _pendingGroup->IsComplete();
            }

            void Reset(JobPool& jobs)
            {
                if (_pendingGroup.has_value())
                    Detail::WaitAndReleasePresentationTask(jobs, _pendingGroup);
                _front.reset();
                _pending.reset();
                _recycle.reset();
                _pendingGroup.reset();
                _balloons = {};
                _peeps = {};
                _peepSourceEpoch = 0;
            }

            void SetPeepCatalog(JobPool& jobs, std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> catalog)
            {
                if (_profile != EntityPublicationProfile::retainedPeepsAndBalloons
                    && _profile != EntityPublicationProfile::nativePeeps)
                {
                    _peepAnimations.reset();
                    _peepObjectGenerations.clear();
                    return;
                }
                if (catalog == nullptr || catalog->epoch == 0 || catalog->sequence == 0)
                    throw std::invalid_argument("Combined publication requires an identified catalog");
                if (catalog == _peepAnimations)
                    return;
                std::vector<uint32_t> generations;
                for (const auto& [slot, entry] : catalog->slots)
                {
                    if (slot >= UINT16_MAX || entry.generation == 0)
                        throw std::invalid_argument("Invalid peep catalog identity");
                    if (entry.object == nullptr)
                        continue;
                    if (entry.object->descriptor.objectIndex != slot
                        || entry.object->descriptor.objectGeneration != entry.generation)
                        throw std::invalid_argument("Peep catalog descriptor differs from slot identity");
                    if (generations.size() <= slot)
                        generations.resize(slot + 1);
                    generations[slot] = entry.generation;
                }
                // Object mutation is an exceptional dependency transition: discard prepared work and bootstrap once.
                Reset(jobs);
                _peepAnimations = std::move(catalog);
                _peepObjectGenerations = std::move(generations);
            }

            Drawing::BalloonPublicationCopyTotals GetCopyTotals() const noexcept
            {
                return _balloonCopyTotals;
            }

            void SetProfile(JobPool& jobs, EntityPublicationProfile profile)
            {
                if (_profile == profile)
                    return;
                Reset(jobs);
                _profile = profile;
            }

            std::shared_ptr<const EntityPresentationSnapshot> Acquire(JobPool& jobs, EntityRegistry& registry)
            {
                if (_pending != nullptr && (!_pendingGroup.has_value() || _pendingGroup->IsComplete()))
                {
                    if (_pendingGroup.has_value())
                        Detail::WaitAndReleasePresentationTask(jobs, _pendingGroup);
                    _recycle = std::const_pointer_cast<EntityPresentationSnapshot>(std::move(_front));
                    _front = std::move(_pending);
                    _pendingGroup.reset();
                }

                if (_front == nullptr)
                {
                    auto initial = std::make_shared<EntityPresentationSnapshot>();
                    Capture(*initial, registry);
                    initial->BuildCapturedStorage();
                    _front = std::move(initial);
                }
                return _front;
            }

            std::shared_ptr<const EntityPresentationSnapshot> AcquireSynchronously(JobPool& jobs, EntityRegistry& registry)
            {
                if (_pendingGroup.has_value())
                {
                    Detail::WaitAndReleasePresentationTask(jobs, _pendingGroup);
                    _pendingGroup.reset();
                }
                _pending.reset();
                // A synchronous map capture describes current live state, even if the prepared entity
                // snapshot came from an earlier tick. Capture both sources at this same boundary.
                auto current = _recycle != nullptr && _recycle.use_count() == 1
                    ? std::move(_recycle)
                    : std::make_shared<EntityPresentationSnapshot>();
                Capture(*current, registry);
                current->BuildCapturedStorage();
                _recycle = std::const_pointer_cast<EntityPresentationSnapshot>(std::move(_front));
                _front = std::move(current);
                return _front;
            }

            void Schedule(JobPool& jobs, EntityRegistry& registry)
            {
                if (_front == nullptr || HasPending())
                    return;

                // Earlier frame packets may still retain this generation. Never overwrite their storage.
                _pending = _recycle != nullptr && _recycle.use_count() == 1 ? std::move(_recycle)
                                                                            : std::make_shared<EntityPresentationSnapshot>();
                Capture(*_pending, registry);
                // The terrain-only identity has no spatial/index work. Hold it alongside
                // the map job captured at this exact owner boundary, without another worker.
                if (_profile == EntityPublicationProfile::gpuTerrainOnly)
                    return;
                _pendingGroup.emplace(jobs.CreateTaskGroup());
                const auto target = _pending;
                jobs.AddTask(*_pendingGroup, [target]() { target->BuildCapturedStorage(); }, JobPool::TaskPriority::background);
            }
        };
    } // namespace

    struct PresentationScene::Impl
    {
        MapPresentationPublisher map;
        EntityPresentationPublisher entities;
        std::shared_ptr<const PresentationGeneration> generation;
        uint32_t drawCount = std::numeric_limits<uint32_t>::max();
        EntityPublicationProfile profile = EntityPublicationProfile::legacyBulk;
        bool retrySynchronously{};
    };

    PresentationScene::PresentationScene()
        : _impl(std::make_unique<Impl>())
    {
    }

    PresentationScene::~PresentationScene() = default;

    bool PresentationScene::BeginFrame(
        JobPool& jobs, EntityRegistry& entities, const uint32_t drawCount, const bool synchronousMapPublication,
        const EntityPublicationProfile profile, std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> peepAnimations)
    {
        try
        {
            if ((profile == EntityPublicationProfile::retainedPeepsAndBalloons
                 || profile == EntityPublicationProfile::nativePeeps)
                && peepAnimations == nullptr)
                throw std::invalid_argument("Combined publication requires an owned animation catalog");
            const bool catalogChanged = (profile == EntityPublicationProfile::retainedPeepsAndBalloons
                                         || profile == EntityPublicationProfile::nativePeeps)
                && (_impl->generation == nullptr || _impl->generation->peepAnimations != peepAnimations);
            const bool profileChanged = _impl->profile != profile;
            // Terrain object replacement can retire atlas dependencies before an older queued map is admitted.
            // Refresh both sources at this exceptional lifecycle boundary; ordinary tile edits remain asynchronous.
            const bool terrainCatalogChanged = profile == EntityPublicationProfile::gpuTerrainOnly
                && _impl->generation != nullptr && _impl->generation->map != nullptr
                && (_impl->generation->map->GetTerrainMaterials() == nullptr
                    || _impl->generation->map->GetTerrainMaterials()->revision != GetTerrainObjectRevision());
            if (!_impl->retrySynchronously && !profileChanged && !catalogChanged && !terrainCatalogChanged
                && _impl->drawCount == drawCount)
                return false;
            _impl->map.SetProfile(
                jobs,
                profile == EntityPublicationProfile::gpuTerrainOnly ? MapPublicationProfile::rawTerrain
                                                                    : MapPublicationProfile::legacyTiles);
            _impl->entities.SetProfile(jobs, profile);
            _impl->entities.SetPeepCatalog(jobs, std::move(peepAnimations));
            _impl->profile = profile;
            const bool entityEpochChanged = _impl->generation != nullptr
                && _impl->generation->sourceEntityEpoch != entities.GetEntityVisualEpoch();
            const bool synchronous = _impl->retrySynchronously
                || (synchronousMapPublication && profile != EntityPublicationProfile::gpuTerrainOnly) || profileChanged
                || entityEpochChanged || catalogChanged || terrainCatalogChanged;

            const bool worldEpochChanged = _impl->generation != nullptr && _impl->generation->map != nullptr
                && _impl->generation->map->GetEpoch() != GetMapPresentationEpoch();
            if (worldEpochChanged || _impl->retrySynchronously)
            {
                // A failed capture may already have consumed one source's dirty input. Wait/discard both
                // preparations and request a complete cold bootstrap, including unchanged tiles/entities.
                // Keep the previously exposed generation until the complete replacement succeeds.
                _impl->map.Reset(jobs);
                _impl->entities.Reset(jobs);
            }

            // Publication is all-or-nothing, including when one source had no changes to prepare.
            if (!synchronous && (!_impl->map.IsReady() || !_impl->entities.IsReady()))
                return false;

            const auto map = synchronous ? _impl->map.AcquireSynchronously(jobs) : _impl->map.Acquire(jobs);
            if (map.sceneReset)
                _impl->entities.Reset(jobs);
            const auto entitySnapshot = synchronous ? _impl->entities.AcquireSynchronously(jobs, entities)
                                                    : _impl->entities.Acquire(jobs, entities);
            if (map.snapshot->GetSourceTick() != entitySnapshot->GetSourceTick())
                throw std::logic_error("Map and entity publication source ticks differ");
            _impl->generation = std::make_shared<PresentationGeneration>(PresentationGeneration{
                .map = map.snapshot,
                .entities = entitySnapshot,
                .balloons = entitySnapshot->GetRetainedBalloons(),
                .sourceTick = entitySnapshot->GetSourceTick(),
                .sourceEntityEpoch = entitySnapshot->GetSourceEpoch(),
                .peeps = entitySnapshot->GetRetainedPeeps(),
                .peepAnimations = entitySnapshot->GetPeepAnimations(),
            });
            _impl->drawCount = drawCount; // Failed preparation may retry this same draw boundary.
            _impl->retrySynchronously = false;
            return true;
        }
        catch (...)
        {
            _impl->retrySynchronously = true;
            throw;
        }
    }

    void PresentationScene::ScheduleNext(
        JobPool& jobs, EntityRegistry& entities, std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> peepAnimations)
    {
        // Only BeginFrame may recover a failed coordinated publication. Do not drain more input meanwhile.
        if (_impl->retrySynchronously)
            return;
        try
        {
            _impl->entities.SetPeepCatalog(jobs, std::move(peepAnimations));
            // Both captures must describe one source state. Do not queue a newer map while entities from
            // an earlier tick are still pending (or vice versa).
            if (_impl->map.HasPending() || _impl->entities.HasPending())
                return;
            _impl->map.Schedule(jobs);
            _impl->entities.Schedule(jobs, entities);
        }
        catch (...)
        {
            _impl->retrySynchronously = true;
            throw;
        }
    }

    void PresentationScene::Reset(JobPool& jobs)
    {
        _impl->map.Reset(jobs);
        _impl->entities.Reset(jobs);
        _impl->generation.reset();
        _impl->drawCount = std::numeric_limits<uint32_t>::max();
        _impl->retrySynchronously = false;
    }

    Drawing::BalloonPublicationCopyTotals PresentationScene::GetBalloonPublicationCopyTotals() const noexcept
    {
        return _impl->entities.GetCopyTotals();
    }

    const std::shared_ptr<const PresentationGeneration>& PresentationScene::GetGeneration() const noexcept
    {
        return _impl->generation;
    }
} // namespace OpenRCT2
