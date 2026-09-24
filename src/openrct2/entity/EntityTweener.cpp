/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "EntityTweener.h"

#include "../GameState.h"
#include "../entity/Guest.h"
#include "../entity/Staff.h"
#include "../interface/Viewport.h"
#include "../interface/WindowTypes.h"
#include "../ride/Vehicle.h"
#include "EntityList.h"

#include <algorithm>
#include <cmath>
#include <sfl/static_vector.hpp>

namespace OpenRCT2
{
    void EntityTweener::populateEntities()
    {
        sfl::static_vector<Viewport*, kWindowLimitMax> viewports;
        WindowVisitEach([&](WindowBase* window) {
            if (auto* viewport = WindowGetViewport(window);
                viewport != nullptr && viewport->isVisible && viewport->zoom <= ZoomLevel{ 0 })
            {
                viewports.push_back(viewport);
            }
        });
        if (viewports.empty())
            return;

        if (_entityIndices.empty())
            _entityIndices.resize(kMaxEntities);
        const auto& registry = getGameState().entities;

        const auto addEntity = [&](EntityBase* entity) {
            const auto location = entity->getLocation();
            const auto visible = std::ranges::any_of(viewports, [&](const auto* viewport) {
                return viewport->Contains(Translate3DTo2DWithZ(viewport->rotation, location));
            });
            if (visible)
            {
                _entities.push_back(entity);
                _prePositions.push_back(location);
                _handles.push_back(registry.GetEntityVisualHandle(entity->id));
                _entityIndices[entity->id.ToUnderlying()] = static_cast<uint32_t>(_entities.size());
            }
        };

        for (auto* ent : registry.GetEntityExecutionList(EntityType::guest))
            addEntity(ent);
        for (auto* ent : registry.GetEntityExecutionList(EntityType::staff))
            addEntity(ent);
        for (auto* ent : registry.GetEntityExecutionList(EntityType::vehicle))
            addEntity(ent);
    }

    void EntityTweener::preTick()
    {
        restore();
        reset();
        _previousTick = getGameState().currentTicks;
        populateEntities();
    }

    void EntityTweener::postTick()
    {
        _sourceTick = getGameState().currentTicks;
        auto& registry = getGameState().entities;
        _postPositions.reserve(_entities.size());
        size_t writeIndex = 0;
        for (size_t readIndex = 0; readIndex < _entities.size(); readIndex++)
        {
            auto* ent = _entities[readIndex];
            if (ent == nullptr || registry.tryGetEntity(_handles[readIndex].id) != ent
                || registry.GetEntityVisualHandle(_handles[readIndex].id) != _handles[readIndex])
                continue;

            const auto postPos = ent->getLocation();
            if (_prePositions[readIndex] == postPos)
                continue;

            // tween and transition restore only need entities which moved during this tick. Compacting the parallel arrays
            // here avoids rescanning every visible but stationary peep and vehicle for each rendered frame.
            _entities[writeIndex] = ent;
            _prePositions[writeIndex] = _prePositions[readIndex];
            _handles[writeIndex] = _handles[readIndex];
            _postPositions.push_back(postPos);
            _entityIndices[ent->id.ToUnderlying()] = static_cast<uint32_t>(writeIndex + 1);
            writeIndex++;
        }
        _entities.resize(writeIndex);
        _prePositions.resize(writeIndex);
        _handles.resize(writeIndex);
        _movingCount = writeIndex;
        _postReady = true;
        _renderAlpha = 1.0f;
    }

    void EntityTweener::removeEntity(EntityBase* entity)
    {
        if (entity->type != EntityType::guest && entity->type != EntityType::staff && entity->type != EntityType::vehicle)
            return;

        const auto id = entity->id.ToUnderlying();
        if (id >= _entityIndices.size() || _entityIndices[id] == 0)
            return;
        const auto index = _entityIndices[id] - 1;
        if (index < _entities.size() && _entities[index] == entity && _handles[index].id == entity->id)
        {
            _entities[index] = nullptr;
            _entityIndices[id] = 0;
            if (_postReady && --_movingCount == 0)
                _renderAlpha = 1.0f;
        }
    }

    CoordsXYZ EntityTweener::interpolatePosition(const CoordsXYZ& previous, const CoordsXYZ& current, float alpha) noexcept
    {
        const float inv = (1.0f - alpha);
        return { static_cast<int32_t>(std::round(current.x * alpha + previous.x * inv)),
                 static_cast<int32_t>(std::round(current.y * alpha + previous.y * inv)),
                 static_cast<int32_t>(std::round(current.z * alpha + previous.z * inv)) };
    }

    void EntityTweener::tween(float alpha)
    {
        _renderAlpha = _postReady && _movingCount != 0 ? alpha : 1.0f;
        for (size_t i = 0; i < _entities.size(); ++i)
        {
            auto* ent = _entities[i];
            if (ent == nullptr)
                continue;

            auto& posA = _prePositions[i];
            auto& posB = _postPositions[i];

            ent->moveToForTween(interpolatePosition(posA, posB, alpha));
        }
    }

    void EntityTweener::restore()
    {
        _renderAlpha = 1.0f;
        for (size_t i = 0; i < _entities.size(); ++i)
        {
            auto* ent = _entities[i];
            if (ent == nullptr)
                continue;

            ent->moveToForTween(_postPositions[i]);
        }
    }

    void EntityTweener::reset()
    {
        _entities.clear();
        _prePositions.clear();
        _postPositions.clear();
        _handles.clear();
        _movingCount = 0;
        _postReady = false;
        _renderAlpha = 1.0f;
    }

    std::optional<EntityTweenMotion> EntityTweener::GetMotion(const EntityBase& entity) const noexcept
    {
        const auto id = entity.id.ToUnderlying();
        if (!_postReady || id >= _entityIndices.size() || _entityIndices[id] == 0 || _sourceTick != getGameState().currentTicks)
            return std::nullopt;
        const auto index = _entityIndices[id] - 1;
        if (index >= _entities.size() || _entities[index] != &entity || _handles[index].id != entity.id)
            return std::nullopt;
        auto& registry = getGameState().entities;
        if (registry.tryGetEntity(entity.id) != &entity || registry.GetEntityVisualHandle(entity.id) != _handles[index])
            return std::nullopt;
        // An action can move the entity after postTick/tween. Its new live position is then authoritative instead.
        if (entity.getLocation() != interpolatePosition(_prePositions[index], _postPositions[index], _renderAlpha))
            return std::nullopt;
        return EntityTweenMotion{ _handles[index], _prePositions[index], _postPositions[index], _previousTick, _sourceTick };
    }

    static EntityTweener tweener;

    EntityTweener& EntityTweener::get()
    {
        return tweener;
    }

} // namespace OpenRCT2
