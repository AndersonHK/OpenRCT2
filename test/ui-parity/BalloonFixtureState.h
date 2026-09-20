/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <openrct2/GameState.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/entity/Balloon.h>
#include <stdexcept>

namespace UiParityBalloons
{
    // Explicit scalar fields; excludes padding, viewport-derived spriteRect and private lifecycle generations.
    inline auto Record(const OpenRCT2::Balloon& balloon)
    {
        return std::array<int64_t, 12>{ balloon.id.ToUnderlying(),
                                        balloon.x,
                                        balloon.y,
                                        balloon.z,
                                        balloon.frame,
                                        balloon.popped,
                                        balloon.timeToMove,
                                        static_cast<uint8_t>(balloon.colour),
                                        balloon.spriteData.width,
                                        balloon.spriteData.heightMin,
                                        balloon.spriteData.heightMax,
                                        balloon.orientation };
    }

    inline json_t Census()
    {
        auto& registry = OpenRCT2::getGameState().entities;
        auto records = json_t::array();
        for (uint32_t id = 0; id < OpenRCT2::kMaxEntities; id++)
        {
            const auto* entity = registry.tryGetEntity(EntityId::FromUnderlying(static_cast<uint16_t>(id)));
            if (entity != nullptr && entity->type == OpenRCT2::EntityType::balloon)
                records.push_back(Record(*entity->cast<OpenRCT2::Balloon>()));
        }
        return { { "schema", 1 },
                 { "columns",
                   { "id", "x", "y", "z", "frame", "popped", "timeToMove", "colour", "width", "heightMin", "heightMax",
                     "orientation" } },
                 { "count", records.size() },
                 { "records", std::move(records) } };
    }
} // namespace UiParityBalloons
