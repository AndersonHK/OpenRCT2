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
#include "EntityVisualLifecycle.h"

#include <optional>
#include <vector>

namespace OpenRCT2
{
    // Owned endpoint facts only: no live entity pointer escapes to a presentation packet.
    struct EntityTweenMotion
    {
        EntityVisualHandle handle;
        CoordsXYZ previous;
        CoordsXYZ current;
        uint32_t previousTick;
        uint32_t sourceTick;
    };

    // Simulation positions remain authoritative. preTick restores any presentation-only interpolation before capturing the
    // visible set; postTick then compacts it to entities that actually moved, which tween may temporarily reposition.
    class EntityTweener
    {
    private:
        std::vector<EntityBase*> _entities;
        std::vector<CoordsXYZ> _prePositions;
        std::vector<CoordsXYZ> _postPositions;
        std::vector<EntityVisualHandle> _handles;
        // One lazy allocation, with index+1 entries. Stale entries after compaction/reset are rejected by identity checks.
        std::vector<uint32_t> _entityIndices;
        uint32_t _previousTick{};
        uint32_t _sourceTick{};
        size_t _movingCount{};
        float _renderAlpha{ 1.0f };
        bool _postReady{};

        void populateEntities();
        static CoordsXYZ interpolatePosition(const CoordsXYZ& previous, const CoordsXYZ& current, float alpha) noexcept;

    public:
        static EntityTweener& get();

        void preTick();
        void postTick();
        void removeEntity(EntityBase* entity);
        void tween(float alpha);
        void restore();
        void reset();

        // Owner-thread only. Returns authoritative endpoints even while the live coordinates contain a tweened pose.
        // Empty for nonparticipants, stale identities/ticks, or a subsequent authoritative position change.
        [[nodiscard]] std::optional<EntityTweenMotion> GetMotion(const EntityBase& entity) const noexcept;
        [[nodiscard]] float GetRenderAlpha() const noexcept
        {
            return _renderAlpha;
        }
    };

} // namespace OpenRCT2
