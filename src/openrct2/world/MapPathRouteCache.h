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

        [[nodiscard]] bool operator==(const RouteTarget& other) const noexcept;
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

    // Freezes exact MapPathTopology data on the calling thread, builds one private reverse field per sorted target, and
    // publishes only if the path-connectivity epoch is unchanged after the worker barrier. Dynamic wide-path flags do not
    // affect these fields and therefore do not invalidate them.
    void Prepare(std::span<const RouteTarget> targets);

    [[nodiscard]] std::optional<RouteStep> GetNextStep(
        const RouteTarget& target, const TileCoordsXYZ& source) noexcept;

    // Every current field retains exact path-tile distance. The richer query distinguishes an unavailable field from an
    // exact-but-unreachable route so callers only use geometric fallback when exact data is unavailable.
    [[nodiscard]] RouteDistance QueryDistanceToTarget(
        const RouteTarget& target, const TileCoordsXYZ& source) noexcept;

    // Ride exits are entrance elements rather than path nodes. This resolves the exact adjacent path connection captured
    // in the same topology snapshot and measures the remaining walk without a live map search.
    [[nodiscard]] RouteDistance QueryDistanceFromRideExitToTarget(
        const RouteTarget& target, const TileCoordsXYZ& sourceExit, RideId sourceRide) noexcept;

    // Resolves the source node once, then returns the first candidate with the shortest retained exact distance. Candidate
    // order is the deterministic tie-break. The result is unavailable after topology invalidation or when none is reachable.
    [[nodiscard]] std::optional<size_t> GetClosestReachableTargetIndex(
        std::span<const RouteTarget> candidates, const TileCoordsXYZ& source) noexcept;

    // Resolves the source node once and compares every prepared concrete target owned by each candidate ride. Candidate
    // order and then prepared target order are stable tie-breaks. An exact empty result means none is reachable.
    [[nodiscard]] ReachableRideTargetResult GetClosestReachableRideTarget(
        std::span<const RideId> candidates, const TileCoordsXYZ& source) noexcept;

    // Returns a topology-validated concrete target only when this ride has exactly one distinct prepared destination.
    // Multi-target rides use GetClosestReachableTargetIndex for reachable entrance selection.
    [[nodiscard]] std::optional<RouteTarget> GetSingleTargetForRide(RideId ride) noexcept;

    // Intended for out-of-band benchmark reporting. Computing this traverses the target fields, so it must not be sampled
    // from the simulation hot path.
    [[nodiscard]] Statistics GetStatistics() noexcept;

    void Reset() noexcept;
} // namespace OpenRCT2::MapPathRouteCache
