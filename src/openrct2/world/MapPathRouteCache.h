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

#include <cstddef>
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

        [[nodiscard]] bool operator==(const RouteTarget& other) const noexcept = default;
    };

    struct RouteStep
    {
        Direction direction{ kInvalidDirection };
    };

    struct RouteDistance
    {
        // A current exact field can still report no distance when the source cannot reach the target. Callers should
        // retain their legacy estimate only when isExact is false, not when an exact field proves the route unreachable.
        std::optional<uint32_t> pathTiles;
        bool isExact{};
    };

    struct ReachableRideTarget
    {
        size_t candidateIndex{};
        RouteTarget target{};
        uint32_t pathTiles{};
    };

    struct ReachableRideTargetResult
    {
        std::optional<ReachableRideTarget> selection;
        bool isExact{};
    };

    struct Statistics
    {
        size_t nodeCount{};
        size_t targetCount{};
        size_t directionEntryCount{};
        size_t distanceEntryCount{};
        size_t singleRideTargetCount{};
        bool preparedForCurrentTopology{};
    };

    [[nodiscard]] bool IsPreparedForCurrentTopology() noexcept;

    // Builds reverse fields from an exact topology snapshot and publishes them if connectivity stays unchanged.
    void Prepare(std::span<const RouteTarget> targets);

    [[nodiscard]] std::optional<RouteStep> GetNextStep(
        const RouteTarget& target, const TileCoordsXYZ& source) noexcept;

    // Distinguishes an unavailable field from an exact but unreachable route.
    [[nodiscard]] RouteDistance QueryDistanceToTarget(
        const RouteTarget& target, const TileCoordsXYZ& source) noexcept;

    // Measures from the path adjacent to a ride exit in the same topology snapshot.
    [[nodiscard]] RouteDistance QueryDistanceFromRideExitToTarget(
        const RouteTarget& target, const TileCoordsXYZ& sourceExit, RideId sourceRide) noexcept;

    // Candidate order breaks equal-distance ties.
    [[nodiscard]] std::optional<size_t> GetClosestReachableTargetIndex(
        std::span<const RouteTarget> candidates, const TileCoordsXYZ& source) noexcept;

    // Compares every prepared target owned by each ride; candidate and target order break ties.
    [[nodiscard]] ReachableRideTargetResult GetClosestReachableRideTarget(
        std::span<const RideId> candidates, const TileCoordsXYZ& source) noexcept;

    // Returns a target only when the ride has one distinct prepared destination.
    [[nodiscard]] std::optional<RouteTarget> GetSingleTargetForRide(RideId ride) noexcept;

    // Traverses target fields; keep this out of the simulation hot path.
    [[nodiscard]] Statistics GetStatistics() noexcept;

    void Reset() noexcept;
} // namespace OpenRCT2::MapPathRouteCache
