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

#include <cstdint>

struct Ride;
struct TileCoordsXYZD;

void RideUpdateStation(Ride& ride, StationIndex stationIndex, uint32_t currentTicks, bool wholeSecondTick);
StationIndex RideGetFirstValidStationExit(const Ride& ride);
StationIndex RideGetFirstValidStationStart(const Ride& ride);
StationIndex RideGetFirstEmptyStationStart(const Ride& ride);
