// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "../entity/EntityVisualLifecycle.h"
#include "../interface/ScreenCoords.hpp"
#include "ImageId.hpp"

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace OpenRCT2::Drawing
{
    struct SelectedVehicleRequest
    {
        uint64_t viewport{}; // Opaque owner identity; never dereferenced by a renderer or worker.
        EntityVisualHandle entity;
        uint32_t viewFlags{};
        int8_t zoom{};
        uint8_t rotation{}, clipHeight{};
        CoordsXY clipFirst{}, clipLast{};
        bool operator==(const SelectedVehicleRequest&) const = default;
    };

    enum class SelectedVehicleRelation : uint32_t
    {
        parent,
        child,
        attached
    };
    struct SelectedVehicleComponent
    {
        std::array<int32_t, 6> bounds{}; // Exact compatibility begin/end, including inverted endpoints.
        ScreenCoordsXY screen;           // Unsnapped world projection; independent of the current camera translation.
        ImageId originalImage, image, maskImage;
        uint32_t parent{}, car{};
        SelectedVehicleRelation relation{};
        bool masked{};
    };
    struct SelectedVehicleCar
    {
        EntityVisualHandle entity;
        CoordsXYZ position; // Owned authoritative car anchor for geometric depth.
        CoordsXY tile;      // Authoritative spatial bucket, not image anchor or bounding-box origin.
        ScreenRect coarseCull;
        uint32_t first{}, count{};
    };
    struct SelectedVehicleView
    {
        SelectedVehicleRequest request;
        std::vector<SelectedVehicleCar> cars;
        std::vector<SelectedVehicleComponent> components;
    };
    struct SelectedVehicleSnapshot
    {
        uint64_t worldEpoch{}, entityEpoch{}, objectRevision{};
        uint32_t sourceTick{};
        std::vector<SelectedVehicleView> views;
    };
    // Explicit bounded auxiliary compatibility authoring. No world traversal, sorting, rasterization or worker reads.
    std::shared_ptr<const SelectedVehicleSnapshot> CaptureSelectedVehicleSnapshot(std::span<const SelectedVehicleRequest>);
} // namespace OpenRCT2::Drawing
