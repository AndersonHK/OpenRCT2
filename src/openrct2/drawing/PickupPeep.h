/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../interface/ScreenCoords.hpp"
#include "../interface/ZoomLevel.h"
#include "ImageId.hpp"

namespace OpenRCT2::Drawing
{
    struct RenderTarget;
}

struct PickedUpPeepState
{
    ImageId image;
    ScreenCoordsXY position;
    ZoomLevel zoom{};
};

extern PickedUpPeepState gPickupPeep;
void GfxInvalidatePickedUpPeep();
void GfxDrawPickedUpPeep(OpenRCT2::Drawing::RenderTarget& rt);
