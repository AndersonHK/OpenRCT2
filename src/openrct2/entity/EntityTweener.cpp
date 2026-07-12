/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "EntityTweener.h"

#include "../entity/Guest.h"
#include "../entity/Staff.h"
#include "../interface/Viewport.h"
#include "../interface/Window.h"
#include "../ride/Vehicle.h"
#include "EntityList.h"
#include "EntityRegistry.h"

#include <algorithm>
#include <cmath>
#include <sfl/static_vector.hpp>

namespace OpenRCT2
{
    void EntityTweener::PopulateEntities()
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

        const auto addEntity = [&](EntityBase* entity) {
            const auto location = entity->getLocation();
            const auto visible = std::ranges::any_of(viewports, [&](const auto* viewport) {
                return viewport->Contains(Translate3DTo2DWithZ(viewport->rotation, location));
            });
            if (visible)
            {
                _entities.push_back(entity);
                _prePositions.push_back(location);
            }
        };

        for (auto ent : EntityList<Guest>())
            addEntity(ent);
        for (auto ent : EntityList<Staff>())
            addEntity(ent);
        for (auto ent : EntityList<Vehicle>())
            addEntity(ent);
    }

    void EntityTweener::PreTick()
    {
        Restore();
        Reset();
        PopulateEntities();
    }

    void EntityTweener::PostTick()
    {
        _postPositions.reserve(_entities.size());
        size_t writeIndex = 0;
        for (size_t readIndex = 0; readIndex < _entities.size(); readIndex++)
        {
            auto* ent = _entities[readIndex];
            if (ent == nullptr)
                continue;

            const auto postPos = ent->getLocation();
            if (_prePositions[readIndex] == postPos)
                continue;

            // Tween and transition restore only need entities which moved during this tick. Compacting the parallel arrays
            // here avoids rescanning every visible but stationary peep and vehicle for each rendered frame.
            _entities[writeIndex] = ent;
            _prePositions[writeIndex] = _prePositions[readIndex];
            _postPositions.push_back(postPos);
            writeIndex++;
        }
        _entities.resize(writeIndex);
        _prePositions.resize(writeIndex);
    }

    void EntityTweener::RemoveEntity(EntityBase* entity)
    {
        if (entity->type != EntityType::guest && entity->type != EntityType::staff && entity->type != EntityType::vehicle)
            return;

        auto it = std::find(_entities.begin(), _entities.end(), entity);
        if (it != _entities.end())
            *it = nullptr;
    }

    void EntityTweener::Tween(float alpha)
    {
        const float inv = (1.0f - alpha);
        for (size_t i = 0; i < _entities.size(); ++i)
        {
            auto* ent = _entities[i];
            if (ent == nullptr)
                continue;

            auto& posA = _prePositions[i];
            auto& posB = _postPositions[i];

            ent->moveToForTween(
                { static_cast<int32_t>(std::round(posB.x * alpha + posA.x * inv)),
                  static_cast<int32_t>(std::round(posB.y * alpha + posA.y * inv)),
                  static_cast<int32_t>(std::round(posB.z * alpha + posA.z * inv)) });
        }
    }

    void EntityTweener::Restore()
    {
        for (size_t i = 0; i < _entities.size(); ++i)
        {
            auto* ent = _entities[i];
            if (ent == nullptr)
                continue;

            ent->moveToForTween(_postPositions[i]);
        }
    }

    void EntityTweener::Reset()
    {
        _entities.clear();
        _prePositions.clear();
        _postPositions.clear();
    }

    static EntityTweener tweener;

    EntityTweener& EntityTweener::Get()
    {
        return tweener;
    }

} // namespace OpenRCT2
