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
    // Simulation positions remain authoritative. preTick restores any presentation-only interpolation before capturing the
    // visible set; postTick then compacts it to entities that actually moved, which tween may temporarily reposition.
    class EntityTweener
    {
    private:
        std::vector<EntityBase*> _entities;
        std::vector<CoordsXYZ> _prePositions;
        std::vector<CoordsXYZ> _postPositions;

        void populateEntities();

    public:
        static EntityTweener& get();

        void preTick();
        void postTick();
        void removeEntity(EntityBase* entity);
        void tween(float alpha);
        void restore();
        void reset();
    };

} // namespace OpenRCT2
