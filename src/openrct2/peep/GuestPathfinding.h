/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../ride/RideTypes.h"
#include "../world/Location.hpp"

#include <memory>

namespace OpenRCT2
{
    struct Guest;
    struct Peep;
    struct TileElement;
} // namespace OpenRCT2

namespace OpenRCT2::PathFinding
{
    Direction ChooseDirection(
        const TileCoordsXYZ& loc, const TileCoordsXYZ& goal, Peep& peep, bool ignoreForeignQueues, RideId queueRideIndex);

    int32_t CalculateNextDestination(Guest& peep);

    // Evaluates complete station-to-station transport journeys in milliseconds
    // for a guest's resolved destination. Called only when the cached goal changes.
    bool PlanTransportRoute(Guest& peep, const TileCoordsXYZ& finalGoal, bool hasWalkingAlternative = true);
    int32_t CalculateTransportCandidateRadiusTiles(
        int64_t walkingSpeedMillimetresPerSecond, int64_t maximumWalkingTimeMs);

    int32_t GuestPathFindParkEntranceEntering(Peep& peep, uint8_t edges);

    int32_t GuestPathFindPeepSpawn(Peep& peep, uint8_t edges);

    int32_t GuestPathFindParkEntranceLeaving(Peep& peep, uint8_t edges);

} // namespace OpenRCT2::PathFinding
