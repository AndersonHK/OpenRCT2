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

namespace OpenRCT2
{
    static inline ViewportList GetUnzoomedViewports() noexcept
    {
        ViewportList viewports;
        WindowVisitEach([&](WindowBase* w) {
            if (auto* vp = WindowGetViewport(w); vp != nullptr)
            {
                if (!vp->isVisible)
                {
                    // Ignore viewports that are not visible.
                    return;
                }
                if (vp->zoom > ZoomLevel{ 0 })
                {
                    // Ignore viewports that are zoomed out, interpolation wouldn't have much of an effect
                    // due to the loss of detail.
                    return;
                }
                viewports.push_back(vp);
            }
        });
        return viewports;
    }

    static inline bool IsEntityVisible(const ViewportList& vpList, const EntityBase* entity) noexcept
    {
        const auto worldLoc = entity->getLocation();

        for (const auto* vp : vpList)
        {
            const auto screenPos = Translate3DTo2DWithZ(vp->rotation, worldLoc);
            if (vp->Contains(screenPos))
            {
                // Entity is visible in at least one viewport, tween.
                return true;
            }
        }

        return false;
    }

    void EntityTweener::AddEntity(const ViewportList& vpList, EntityBase* entity)
    {
        if (!IsEntityVisible(vpList, entity))
        {
            return;
        }

        Entities.push_back(entity);
        PrePos.emplace_back(entity->getLocation());
    }

    void EntityTweener::PopulateEntities()
    {
        const auto vpList = GetUnzoomedViewports();
        if (vpList.empty())
        {
            // No viewports that fit the criteria, bail.
            return;
        }

        for (auto ent : EntityList<Guest>())
        {
            AddEntity(vpList, ent);
        }
        for (auto ent : EntityList<Staff>())
        {
            AddEntity(vpList, ent);
        }
        for (auto ent : EntityList<Vehicle>())
        {
            AddEntity(vpList, ent);
        }
    }

    void EntityTweener::PreTick()
    {
        Restore();
        Reset();
        PopulateEntities();
    }

    void EntityTweener::PostTick()
    {
        PostPos.reserve(Entities.size());
        size_t writeIndex = 0;
        for (size_t readIndex = 0; readIndex < Entities.size(); readIndex++)
        {
            auto* ent = Entities[readIndex];
            if (ent == nullptr)
                continue;

            const auto postPos = ent->getLocation();
            if (PrePos[readIndex] == postPos)
                continue;

            // Tween and transition restore only need entities which moved during this tick. Compacting the parallel arrays
            // here avoids rescanning every visible but stationary peep and vehicle for each rendered frame.
            Entities[writeIndex] = ent;
            PrePos[writeIndex] = PrePos[readIndex];
            PostPos.push_back(postPos);
            writeIndex++;
        }
        Entities.resize(writeIndex);
        PrePos.resize(writeIndex);
    }

    static bool CanTweenEntity(EntityBase* ent)
    {
        if (ent->is<Guest>() || ent->is<Staff>() || ent->is<Vehicle>())
            return true;
        return false;
    }

    void EntityTweener::RemoveEntity(EntityBase* entity)
    {
        if (!CanTweenEntity(entity))
        {
            // Only peeps and vehicles are tweened, bail if type is incorrect.
            return;
        }

        auto it = std::find(Entities.begin(), Entities.end(), entity);
        if (it != Entities.end())
            *it = nullptr;
    }

    void EntityTweener::Tween(float alpha)
    {
        const float inv = (1.0f - alpha);
        for (size_t i = 0; i < Entities.size(); ++i)
        {
            auto* ent = Entities[i];
            if (ent == nullptr)
                continue;

            auto& posA = PrePos[i];
            auto& posB = PostPos[i];

            ent->moveToForTween(
                { static_cast<int32_t>(std::round(posB.x * alpha + posA.x * inv)),
                  static_cast<int32_t>(std::round(posB.y * alpha + posA.y * inv)),
                  static_cast<int32_t>(std::round(posB.z * alpha + posA.z * inv)) });
        }
    }

    void EntityTweener::Restore()
    {
        for (size_t i = 0; i < Entities.size(); ++i)
        {
            auto* ent = Entities[i];
            if (ent == nullptr)
                continue;

            ent->moveToForTween(PostPos[i]);
        }
    }

    void EntityTweener::Reset()
    {
        Entities.clear();
        PrePos.clear();
        PostPos.clear();
    }

    static EntityTweener tweener;

    EntityTweener& EntityTweener::Get()
    {
        return tweener;
    }

} // namespace OpenRCT2
