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
#include <vector>

#define OPENRCT2_PRESENTATION_SOURCE_TICK_VERSION 1

namespace OpenRCT2
{
    namespace Drawing
    {
        struct RetainedBalloonSnapshot;
        struct RetainedPeepSnapshot;
        struct RetainedPeepAnimationCatalog;
        struct SelectedVehicleSnapshot;
        struct VehiclePresentationSnapshot;
        struct WorldEffectSnapshot;
        struct MoneyPresentationSnapshot;
    } // namespace Drawing
    enum class EntityPublicationProfile : uint8_t
    {
        legacyBulk,
        retainedBalloons,
        retainedPeepsAndBalloons,
        nativePeeps,
        gpuTerrainOnly,
        gpuWorld
    };
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
        std::shared_ptr<const Drawing::RetainedBalloonSnapshot> balloons;
        // Tick of the immutable entity payload, not the live tick when it is displayed.
        uint32_t sourceTick{};
        uint64_t sourceEntityEpoch{};
        std::shared_ptr<const Drawing::RetainedPeepSnapshot> peeps;
        std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> peepAnimations;
        std::shared_ptr<const Drawing::SelectedVehicleSnapshot> selectedVehicles;
        std::shared_ptr<const Drawing::VehiclePresentationSnapshot> vehicles;
        std::shared_ptr<const Drawing::WorldEffectSnapshot> effects;
        std::shared_ptr<const Drawing::MoneyPresentationSnapshot> money;
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
        bool nativeEntitiesAllowed{};
        float entityInterpolation{ 1.0f };
        // Current render endpoint tick; a held older generation must not replay history with a newer alpha.
        uint32_t entityInterpolationSourceTick{};
        uint32_t viewFlags{};
        std::shared_ptr<const std::vector<uint32_t>> selection;
        uint64_t selectedVehicleViewport{};
    };
} // namespace OpenRCT2
