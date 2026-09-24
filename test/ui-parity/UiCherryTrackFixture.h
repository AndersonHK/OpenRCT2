/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

// Explicit test-only substitution; never selected by an ordinary game viewport.
#include <openrct2/GameState.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/SmallSceneryObject.h>
#include <openrct2/world/TileElementsView.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <openrct2/world/tile_element/TrackElement.h>
#include <stdexcept>
#include <string>

namespace UiParity
{
    // Call exactly once on the game owner thread after loading EverythingPark,
    // before capture warmup, publication or simulation advancement. Reload the
    // original park for every independent run. This never saves the mutation.
    // The caller must bind the original park SHA-256 and this mutation receipt
    // into every sample, invalidate the screen, and refresh any render snapshot.
    inline json_t ApplyCherryTrackFixture(
        OpenRCT2::IObjectManager& objects, const std::string& cherryId, int32_t width, int32_t height)
    {
        using namespace OpenRCT2;
        if (cherryId != "rct2ww.scenery_small.japchblo" && cherryId != "rct2ww.scenery_small.jachtree")
            throw std::invalid_argument("Cherry fixture requires an explicit supported loaded cherry identifier");
        const auto mapSize = getGameState().mapSize;
        if (mapSize.x <= 155 || mapSize.y <= 210 || mapSize.x > 1024 || mapSize.y > 1024
            || width <= 0 || height <= 0 || width > 16384 || height > 16384)
            throw std::invalid_argument("Cherry fixture map or viewport extent is invalid");

        SmallSceneryElement* target = nullptr;
        for (auto* element : TileElementsView<SmallSceneryElement>(TileCoordsXY{ 154, 210 }))
        {
            const auto* object = objects.GetLoadedObject<SmallSceneryObject>(element->getEntryIndex());
            if (!element->isGhost() && element->getBaseZ() == 336 && object != nullptr
                && object->GetIdentifier() == "rct2.scenery_small.tsb")
            {
                if (target != nullptr)
                    throw std::runtime_error("Cherry fixture target is not unique");
                target = element;
            }
        }
        if (target == nullptr || target->getEntry() == nullptr)
            throw std::runtime_error("Expected EverythingPark birch at tile (154,210), base Z 336 is missing");

        const TrackElement* track = nullptr;
        for (const auto* element : TileElementsView<TrackElement>(TileCoordsXY{ 155, 210 }))
        {
            if (!element->isGhost() && element->getBaseZ() == 456 && element->getRideIndex().ToUnderlying() == 157
                && element->getRideType() == 99 && static_cast<uint32_t>(element->getTrackType()) == 4)
            {
                if (track != nullptr)
                    throw std::runtime_error("Cherry fixture adjacent wooden track is not unique");
                track = element;
            }
        }
        if (track == nullptr)
            throw std::runtime_error("Expected adjacent EverythingPark wooden track is missing");

        const SmallSceneryElement* donor = nullptr;
        TileCoordsXY donorTile{};
        for (int32_t y = 0; y < mapSize.y; ++y)
        {
            for (int32_t x = 0; x < mapSize.x; ++x)
            {
                for (const auto* element : TileElementsView<SmallSceneryElement>(TileCoordsXY{ x, y }))
                {
                    const auto* object = objects.GetLoadedObject<SmallSceneryObject>(element->getEntryIndex());
                    if (!element->isGhost() && object != nullptr && object->GetIdentifier() == cherryId)
                    {
                        if (donor != nullptr)
                            throw std::runtime_error("Cherry fixture donor is not unique");
                        donor = element;
                        donorTile = { x, y };
                    }
                }
            }
        }
        if (donor == nullptr || donor->getEntry() == nullptr || !donor->getEntry()->flags.has(SmallSceneryFlag::isTree))
            throw std::runtime_error("Expected loaded cherry-tree donor is missing");
        const auto* cherry = donor->getEntry();
        const auto clearance = (target->getBaseZ() + cherry->height + 7) & ~7;
        if (clearance > kMaxTileElementHeight * kCoordsZStep)
            throw std::runtime_error("Cherry fixture exceeds tile-element clearance storage");

        // Validate everything before the two writes. Preserve target placement,
        // direction, colours, age, tile-list flags and all adjacent track data.
        const auto oldSlot = target->getEntryIndex();
        const auto oldClearance = target->getClearanceZ();
        const auto oldHeight = target->getEntry()->height;
        target->setEntryIndex(donor->getEntryIndex());
        target->setClearanceZ(clearance);

        json_t cameras = json_t::array();
        // Keep the already-reviewed birch camera centre constant between assets.
        const CoordsXYZ centre{ 4960, 6736, 431 };
        for (int32_t rotation = 0; rotation < 4; ++rotation)
        {
            const auto projected = Translate3DTo2DWithZ(rotation, centre);
            cameras.push_back({ { "rotation", rotation }, { "zoom", 0 },
                { "viewPosition", { projected.x - width / 2, projected.y - height / 2 } },
                { "treeGroundAnchorInFrontOfTrack", rotation == 1 || rotation == 2 } });
        }
        return { { "schemaVersion", 1 }, { "kind", "everythingpark-adjacent-cherry-substitution" },
            { "mutatedWorld", true }, { "simulationTicks", getGameState().currentTicks },
            { "treeTile", { 154, 210 } }, { "treeBaseZ", 336 },
            { "before", { { "id", "rct2.scenery_small.tsb" }, { "slot", oldSlot },
                            { "height", oldHeight }, { "clearanceZ", oldClearance } } },
            { "after", { { "id", cherryId }, { "slot", donor->getEntryIndex() },
                           { "height", cherry->height }, { "clearanceZ", clearance } } },
            { "donorTile", { donorTile.x, donorTile.y } },
            { "track", { { "tile", { 155, 210 } }, { "baseZ", 456 }, { "rideId", 157 },
                           { "rideType", 99 }, { "trackType", 4 } } },
            { "writes", { "target.entryIndex", "target.clearanceHeight" } }, { "cameras", cameras },
            { "interpretation", "Adjacent tiles; ground-anchor foreground is a scene locator, not a per-pixel depth oracle. Other track columns remain present." } };
    }
} // namespace UiParity
