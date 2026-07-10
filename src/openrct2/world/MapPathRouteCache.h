/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../Identifiers.h"
#include "Location.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace OpenRCT2::MapPathRouteCache
{
    enum class RouteTargetKind : uint8_t
    {
        pathOrEntrance,
        shopOrFacilityTrack,
    };

    struct RouteTarget
    {
        TileCoordsXYZ location{};
        RideId queueRide{ RideId::GetNull() };
        RouteTargetKind kind{ RouteTargetKind::pathOrEntrance };

        [[nodiscard]] bool operator==(const RouteTarget& other) const noexcept;
    };

    struct RouteStep
    {
        Direction direction{ kInvalidDirection };
    };

    [[nodiscard]] bool IsPreparedForCurrentTopology() noexcept;

    // Freezes exact MapPathTopology data on the calling thread, builds one private reverse field per sorted target, and
    // publishes only if the topology epoch is unchanged after the worker barrier.
    void Prepare(std::span<const RouteTarget> targets);

    [[nodiscard]] std::optional<RouteStep> GetNextStep(
        const RouteTarget& target, const TileCoordsXYZ& source) noexcept;

    // Returns a topology-validated concrete target only when this ride has exactly one distinct prepared destination.
    // Multi-target rides retain GuestPathfinding's existing station-selection policy.
    [[nodiscard]] std::optional<RouteTarget> GetSingleTargetForRide(RideId ride) noexcept;

    void Reset() noexcept;
} // namespace OpenRCT2::MapPathRouteCache
