/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <memory>

namespace OpenRCT2
{
    class EntityPresentationSnapshot;
    class MapPresentationSnapshot;

    /**
     * One immutable world publication admitted for presentation. The generation owns every snapshot referenced by work
     * handed to a renderer; superseded generations can therefore be dropped without observing newer simulation storage.
     */
    struct PresentationGeneration
    {
        std::shared_ptr<const MapPresentationSnapshot> map;
        std::shared_ptr<const EntityPresentationSnapshot> entities;
    };

    /** Integer camera state latched with one world-scene submission. */
    struct OrthographicCamera
    {
        int32_t viewX{};
        int32_t viewY{};
        int32_t clipLeft{};
        int32_t clipTop{};
        int32_t clipRight{};
        int32_t clipBottom{};
        int8_t zoom{};
        uint8_t rotation{};
        uint8_t landscapeSmoothing{};
    };
} // namespace OpenRCT2
