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
#include "RetainedBalloonScene.h"

#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace OpenRCT2
{
    namespace
    {
        class MapPresentationPublisher final
        {
        private:
            std::shared_ptr<const MapPresentationSnapshot> _front;
            std::shared_ptr<MapPresentationSnapshot> _pending;
            std::optional<JobPool::TaskGroup> _pendingGroup;
            bool _pendingReset{};

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
                    jobs.Wait(*_pendingGroup);
                _front.reset();
                _pending.reset();
                _pendingGroup.reset();
                _pendingReset = false;
            }

            AcquireResult Acquire(JobPool& jobs)
            {
                bool sceneReset = false;
                if (_pendingGroup.has_value() && _pendingGroup->IsComplete())
                {
                    jobs.Wait(*_pendingGroup);
                    _front = std::move(_pending);
                    _pendingGroup.reset();
                    sceneReset = std::exchange(_pendingReset, false);
                }

                if (_front == nullptr)
                {
                    auto initial = std::make_shared<MapPresentationSnapshot>();
                    initial->Apply(ConsumeMapPresentationChanges());
                    _front = std::move(initial);
                }
                return { _front, sceneReset };
            }

            AcquireResult AcquireSynchronously(JobPool& jobs)
            {
                bool sceneReset = false;
                if (_pendingGroup.has_value())
                {
                    jobs.Wait(*_pendingGroup);
                    _front = std::move(_pending);
                    _pendingGroup.reset();
                    sceneReset = std::exchange(_pendingReset, false);
                }

                auto changes = ConsumeMapPresentationChanges();
                sceneReset |= changes.reset;
                if (_front == nullptr)
                {
                    auto current = std::make_shared<MapPresentationSnapshot>();
                    current->Apply(changes);
                    _front = std::move(current);
                }
                else if (changes.reset || !changes.changes.empty())
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

                auto changes = ConsumeMapPresentationChanges();
                if (!changes.reset && changes.changes.empty())
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
            // Lifetime-monotonic even across profile/reset bootstrap: the same epoch must never reuse GPU revisions.
            uint64_t _balloonSequence{};
            Drawing::BalloonPublicationCopyTotals _balloonCopyTotals{};

            void Capture(EntityPresentationSnapshot& target, EntityRegistry& registry)
            {
                if (_profile == EntityPublicationProfile::legacyBulk)
                {
                    target.CaptureStorage(registry);
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
                return _pendingGroup.has_value();
            }
            bool IsReady() const
            {
                return !HasPending() || _pendingGroup->IsComplete();
            }

            void Reset(JobPool& jobs)
            {
                if (_pendingGroup.has_value())
                    jobs.Wait(*_pendingGroup);
                _front.reset();
                _pending.reset();
                _recycle.reset();
                _pendingGroup.reset();
                _balloons = {};
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
                if (_pendingGroup.has_value() && _pendingGroup->IsComplete())
                {
                    jobs.Wait(*_pendingGroup);
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
                    jobs.Wait(*_pendingGroup);
                    _pending.reset();
                    _pendingGroup.reset();
                }
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
                if (_front == nullptr || _pendingGroup.has_value())
                    return;

                // Earlier frame packets may still retain this generation. Never overwrite their storage.
                _pending = _recycle != nullptr && _recycle.use_count() == 1 ? std::move(_recycle)
                                                                            : std::make_shared<EntityPresentationSnapshot>();
                Capture(*_pending, registry);
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
    };

    PresentationScene::PresentationScene()
        : _impl(std::make_unique<Impl>())
    {
    }

    PresentationScene::~PresentationScene() = default;

    bool PresentationScene::BeginFrame(
        JobPool& jobs, EntityRegistry& entities, const uint32_t drawCount, const bool synchronousMapPublication,
        const EntityPublicationProfile profile)
    {
        const bool profileChanged = _impl->profile != profile;
        if (!profileChanged && _impl->drawCount == drawCount)
            return false;
        _impl->drawCount = drawCount;
        _impl->entities.SetProfile(jobs, profile);
        _impl->profile = profile;
        const bool entityEpochChanged = _impl->generation != nullptr && _impl->generation->balloons != nullptr
            && _impl->generation->balloons->epoch != entities.GetEntityVisualEpoch();
        const bool synchronous = synchronousMapPublication || profileChanged || entityEpochChanged;

        const bool worldEpochChanged = _impl->generation != nullptr && _impl->generation->map != nullptr
            && _impl->generation->map->GetEpoch() != GetMapPresentationEpoch();
        if (worldEpochChanged)
        {
            // Park loading replaces map, object, ride, and entity registries together. No prepared work may cross that epoch.
            _impl->map.Reset(jobs);
            _impl->entities.Reset(jobs);
            _impl->generation.reset();
        }

        // Publication is all-or-nothing, including when one source had no changes to prepare.
        if (!synchronous && (!_impl->map.IsReady() || !_impl->entities.IsReady()))
            return false;

        const auto map = synchronous ? _impl->map.AcquireSynchronously(jobs) : _impl->map.Acquire(jobs);
        if (map.sceneReset)
            _impl->entities.Reset(jobs);
        const auto entitySnapshot = synchronous ? _impl->entities.AcquireSynchronously(jobs, entities)
                                                : _impl->entities.Acquire(jobs, entities);
        _impl->generation = std::make_shared<PresentationGeneration>(PresentationGeneration{
            .map = map.snapshot,
            .entities = entitySnapshot,
            .balloons = entitySnapshot->GetRetainedBalloons(),
        });
        return true;
    }

    void PresentationScene::ScheduleNext(JobPool& jobs, EntityRegistry& entities)
    {
        // Both captures must describe one source state. Do not queue a newer map while entities from
        // an earlier tick are still pending (or vice versa).
        if (_impl->map.HasPending() || _impl->entities.HasPending())
            return;
        _impl->map.Schedule(jobs);
        _impl->entities.Schedule(jobs, entities);
    }

    void PresentationScene::Reset(JobPool& jobs)
    {
        _impl->map.Reset(jobs);
        _impl->entities.Reset(jobs);
        _impl->generation.reset();
        _impl->drawCount = std::numeric_limits<uint32_t>::max();
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
