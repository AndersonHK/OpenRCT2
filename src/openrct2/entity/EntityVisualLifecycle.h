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

#include <cstdint>
#include <vector>

namespace OpenRCT2
{
    enum class EntityVisualDirty : uint8_t
    {
        none = 0,
        presence = 1 << 0,
        transform = 1 << 1,
        bounds = 1 << 2,
        appearance = 1 << 3,
        topology = 1 << 4,
        lighting = 1 << 5,
        interaction = 1 << 6,
        full = 0x7f,
    };

    constexpr EntityVisualDirty operator|(const EntityVisualDirty lhs, const EntityVisualDirty rhs) noexcept
    {
        return static_cast<EntityVisualDirty>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    struct EntityVisualHandle
    {
        uint64_t epoch{};
        EntityId id{ EntityId::GetNull() };
        uint32_t generation{};

        constexpr bool operator==(const EntityVisualHandle&) const = default;
    };

    // This is an owned presentation record: asynchronous consumers must never retain pointers into the entity union.
    struct EntityVisualChange
    {
        EntityVisualHandle handle;
        EntityType type{ EntityType::null };
        CoordsXYZ location{};
        EntitySpriteData spriteData{};
        uint8_t orientation{};
        EntityVisualDirty dirty{ EntityVisualDirty::none };
        bool present{};
    };

    struct EntityVisualChangeBatch
    {
        uint64_t epoch{};
        bool reset{};
        std::vector<EntityVisualChange> changes;
    };
} // namespace OpenRCT2
