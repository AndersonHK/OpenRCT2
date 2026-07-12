/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "EntityBase.h"

#include <vector>

namespace OpenRCT2
{
    // Simulation positions remain authoritative. PreTick restores any presentation-only interpolation before capturing the
    // visible set; PostTick then compacts it to entities that actually moved, which Tween may temporarily reposition.
    class EntityTweener
    {
    private:
        std::vector<EntityBase*> _entities;
        std::vector<CoordsXYZ> _prePositions;
        std::vector<CoordsXYZ> _postPositions;

        void PopulateEntities();

    public:
        static EntityTweener& Get();

        void PreTick();
        void PostTick();
        void RemoveEntity(EntityBase* entity);
        void Tween(float alpha);
        void Restore();
        void Reset();
    };

} // namespace OpenRCT2
