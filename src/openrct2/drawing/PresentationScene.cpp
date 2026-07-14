/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "PresentationScene.h"

#include "../core/JobPool.h"
#include "../entity/EntityPresentationSnapshot.h"
#include "../world/MapPresentationSnapshot.h"

#include <limits>
#include <optional>
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

        public:
            void Reset(JobPool& jobs)
            {
                if (_pendingGroup.has_value())
                    jobs.Wait(*_pendingGroup);
                _front.reset();
                _pending.reset();
                _recycle.reset();
                _pendingGroup.reset();
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
                    initial->CaptureStorage(registry);
                    initial->BuildCapturedStorage();
                    _front = std::move(initial);
                }
                return _front;
            }

            void Schedule(JobPool& jobs, EntityRegistry& registry)
            {
                if (_front == nullptr || _pendingGroup.has_value())
                    return;

                _pending = _recycle == nullptr ? std::make_shared<EntityPresentationSnapshot>() : std::move(_recycle);
                _pending->CaptureStorage(registry);
                _pendingGroup.emplace(jobs.CreateTaskGroup());
                const auto target = _pending;
                jobs.AddTask(
                    *_pendingGroup, [target]() { target->BuildCapturedStorage(); }, JobPool::TaskPriority::background);
            }
        };
    } // namespace

    struct PresentationScene::Impl
    {
        MapPresentationPublisher map;
        EntityPresentationPublisher entities;
        std::shared_ptr<const PresentationGeneration> generation;
        uint32_t drawCount = std::numeric_limits<uint32_t>::max();
    };

    PresentationScene::PresentationScene()
        : _impl(std::make_unique<Impl>())
    {
    }

    PresentationScene::~PresentationScene() = default;

    bool PresentationScene::BeginFrame(
        JobPool& jobs, EntityRegistry& entities, const uint32_t drawCount, const bool synchronousMapPublication)
    {
        if (_impl->drawCount == drawCount)
            return false;
        _impl->drawCount = drawCount;

        const bool worldEpochChanged = _impl->generation != nullptr && _impl->generation->map != nullptr
            && _impl->generation->map->GetEpoch() != GetMapPresentationEpoch();
        if (worldEpochChanged)
        {
            // Park loading replaces map, object, ride, and entity registries together. No prepared work may cross that epoch.
            _impl->map.Reset(jobs);
            _impl->entities.Reset(jobs);
            _impl->generation.reset();
        }

        const auto map = synchronousMapPublication ? _impl->map.AcquireSynchronously(jobs) : _impl->map.Acquire(jobs);
        if (map.sceneReset)
            _impl->entities.Reset(jobs);
        const auto entitySnapshot = _impl->entities.Acquire(jobs, entities);
        _impl->generation = std::make_shared<PresentationGeneration>(PresentationGeneration{
            .map = map.snapshot,
            .entities = entitySnapshot,
        });
        return true;
    }

    void PresentationScene::ScheduleNext(JobPool& jobs, EntityRegistry& entities)
    {
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

    const std::shared_ptr<const PresentationGeneration>& PresentationScene::GetGeneration() const noexcept
    {
        return _impl->generation;
    }
} // namespace OpenRCT2
