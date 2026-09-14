/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "PickupPeep.h"

#include "Drawing.Sprite.h"
#include "Drawing.h"
#include "RenderTarget.h"

#include <array>
#include <cassert>

using OpenRCT2::Drawing::RenderTarget;

PickedUpPeepState gPickupPeep;

namespace
{
    constexpr std::array<int8_t, 3> kPickedUpPeepYOffsets = { 0, 16, 48 };
}

void GfxInvalidatePickedUpPeep()
{
    if (!gPickupPeep.image.HasValue())
        return;

    const auto* g1 = GfxGetG1Element(gPickupPeep.image);
    if (g1 == nullptr)
        return;

    const auto zoomIndex = static_cast<size_t>(-static_cast<int8_t>(gPickupPeep.zoom));
    assert(zoomIndex < kPickedUpPeepYOffsets.size());
    const auto xOffset = static_cast<int32_t>(zoomIndex);
    const auto yOffset = kPickedUpPeepYOffsets[zoomIndex];
    const auto left = gPickupPeep.position.x + gPickupPeep.zoom.ApplyInversedTo(g1->xOffset) + xOffset;
    const auto top = gPickupPeep.position.y + gPickupPeep.zoom.ApplyInversedTo(g1->yOffset) + yOffset;
    const auto right = left + gPickupPeep.zoom.ApplyInversedTo(g1->width);
    const auto bottom = top + gPickupPeep.zoom.ApplyInversedTo(g1->height);
    GfxSetDirtyBlocks({ { left, top }, { right, bottom } });
}

void GfxDrawPickedUpPeep(RenderTarget& rt)
{
    if (!gPickupPeep.image.HasValue())
        return;

    assert(rt.zoom_level == ZoomLevel{ 0 });
    const auto zoomIndex = static_cast<size_t>(-static_cast<int8_t>(gPickupPeep.zoom));
    assert(zoomIndex < kPickedUpPeepYOffsets.size());
    const auto xOffset = static_cast<int32_t>(zoomIndex);
    const auto yOffset = kPickedUpPeepYOffsets[zoomIndex];
    const auto position = ScreenCoordsXY{
        gPickupPeep.zoom.ApplyTo(gPickupPeep.position.x + xOffset),
        gPickupPeep.zoom.ApplyTo(gPickupPeep.position.y + yOffset),
    };

    auto peepTarget = rt;
    peepTarget.zoom_level = gPickupPeep.zoom;
    peepTarget.pitch = gPickupPeep.zoom.ApplyTo(rt.pitch);
    GfxDrawSprite(peepTarget, gPickupPeep.image, position);
}
