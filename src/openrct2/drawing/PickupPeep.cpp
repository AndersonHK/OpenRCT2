/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "PickupPeep.h"

#include "../interface/ScreenCoords.hpp"
#include "../interface/Viewport.h"
#include "../interface/Window.h"
#include "../interface/WindowBase.h"
#include "../interface/ZoomLevel.h"
#include "Drawing.Sprite.h"
#include "Drawing.h"
#include "ImageId.hpp"
#include "RenderTarget.h"

#include <algorithm>
#include <array>
#include <cassert>

namespace OpenRCT2::Drawing
{
    namespace
    {
        struct PickedUpPeepState
        {
            ImageId image;
            ScreenCoordsXY position;
            ZoomLevel zoom{};
        };

        PickedUpPeepState _pickupPeep;
        constexpr std::array<int8_t, 3> kPickedUpPeepYOffsets = { 0, 16, 48 };
    }

    void pickupPeepSetImage(ImageIndex baseImageId, Colour primaryColour, Colour secondaryColour)
    {
        _pickupPeep.image = ImageId(baseImageId, primaryColour, secondaryColour);
    }

    void pickupPeepSetPosition(ScreenCoordsXY position)
    {
        _pickupPeep.position = position;
        _pickupPeep.zoom = ZoomLevel{ 0 };
        const auto* mainWindow = WindowGetMain();
        if (mainWindow != nullptr && mainWindow->viewport != nullptr)
            _pickupPeep.zoom = std::min(mainWindow->viewport->zoom, ZoomLevel{ 0 });
    }

    void pickupPeepClear()
    {
        _pickupPeep.image = ImageId();
    }

    void pickupPeepInvalidate()
    {
        if (!_pickupPeep.image.HasValue())
            return;

        const auto* g1 = GfxGetG1Element(_pickupPeep.image);
        if (g1 == nullptr)
            return;

        const auto zoomIndex = static_cast<size_t>(-static_cast<int8_t>(_pickupPeep.zoom));
        assert(zoomIndex < kPickedUpPeepYOffsets.size());
        const auto xOffset = static_cast<int32_t>(zoomIndex);
        const auto yOffset = kPickedUpPeepYOffsets[zoomIndex];
        const auto left = _pickupPeep.position.x + _pickupPeep.zoom.ApplyInversedTo(g1->xOffset) + xOffset;
        const auto top = _pickupPeep.position.y + _pickupPeep.zoom.ApplyInversedTo(g1->yOffset) + yOffset;
        const auto right = left + _pickupPeep.zoom.ApplyInversedTo(g1->width);
        const auto bottom = top + _pickupPeep.zoom.ApplyInversedTo(g1->height);
        GfxSetDirtyBlocks({ { left, top }, { right, bottom } });
    }

    void pickupPeepDraw(RenderTarget& rt)
    {
        if (!_pickupPeep.image.HasValue())
            return;

        assert(rt.zoom_level == ZoomLevel{ 0 });
        const auto zoomIndex = static_cast<size_t>(-static_cast<int8_t>(_pickupPeep.zoom));
        assert(zoomIndex < kPickedUpPeepYOffsets.size());
        const auto xOffset = static_cast<int32_t>(zoomIndex);
        const auto yOffset = kPickedUpPeepYOffsets[zoomIndex];
        const auto position = ScreenCoordsXY{
            _pickupPeep.zoom.ApplyTo(_pickupPeep.position.x + xOffset),
            _pickupPeep.zoom.ApplyTo(_pickupPeep.position.y + yOffset),
        };

        auto peepTarget = rt;
        peepTarget.zoom_level = _pickupPeep.zoom;
        peepTarget.pitch = _pickupPeep.zoom.ApplyTo(rt.pitch);
        GfxDrawSprite(peepTarget, _pickupPeep.image, position);
    }
} // namespace OpenRCT2::Drawing
