/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "MapAnimation.h"

#include "../GameState.h"
#include "../entity/EntityList.h"
#include "../entity/Peep.h"
#include "../object/SmallSceneryEntry.h"
#include "../object/WallSceneryEntry.h"
#include "../profiling/Profiling.h"
#include "../ride/ted/TrackElemType.h"
#include "Map.h"
#include "tile_element/SmallSceneryElement.h"
#include "tile_element/TileElement.h"
#include "tile_element/TrackElement.h"
#include "tile_element/WallElement.h"

#include <set>
#include <tuple>
#include <utility>

using namespace OpenRCT2;

struct TemporaryMapAnimation
{
    CoordsXYZ location{};
    MapAnimations::TemporaryType type{};

    bool operator<(const TemporaryMapAnimation& rhs) const
    {
        return std::tie(location.y, location.x, type, location.z)
            < std::tie(rhs.location.y, rhs.location.x, rhs.type, rhs.location.z);
    }
};

struct TileCoordsXYCmp
{
    constexpr bool operator()(const TileCoordsXY& lhs, const TileCoordsXY& rhs) const noexcept
    {
        return std::tie(lhs.y, lhs.x) < std::tie(rhs.y, rhs.x);
    }
};

// These sets contain stateful simulation work only. Frame-driven sprite animation
// is selected by the GPU during a full redraw and requires no viewport invalidation.
static std::set<TileCoordsXY, TileCoordsXYCmp> _mapAnimationsUpdate;
static std::set<TemporaryMapAnimation> _temporaryMapAnimations;

static bool UpdateClock(const SmallSceneryElement& scenery, const CoordsXY& loc)
{
    const auto* entry = scenery.getEntry();
    if (entry == nullptr || !entry->flags.has(SmallSceneryFlag::isClock))
        return false;

    // Ghost clocks remain registered but must never affect peeps.
    if (!scenery.isGhost() && !(getGameState().currentTicks & 0x3FF))
    {
        auto quad = EntityTileList<Peep>(loc - CoordsDirectionDelta[scenery.getDirection()]);
        for (auto peep : quad)
        {
            if (peep->state != PeepState::walking || peep->z != scenery.getBaseZ() || !peep->isActionInterruptableSafely())
                continue;
            peep->action = PeepActionType::checkTime;
            peep->animationFrameNum = 0;
            peep->animationImageIdOffset = 0;
            peep->updateCurrentAnimationType();
            break;
        }
    }
    return true;
}

static bool UpdateWallDoor(WallElement& wall, const CoordsXY& loc)
{
    const auto* entry = wall.getEntry();
    if (entry == nullptr || !entry->flags.has(WallSceneryFlag::isDoor) || !wall.isAnimating())
        return false;

    bool removeAnim = true;
    const auto currentFrame = wall.getAnimationFrame();
    if (currentFrame != 0)
    {
        auto newFrame = currentFrame;
        if (currentFrame == 15)
        {
            newFrame = 0;
            wall.setIsAnimating(false);
        }
        else
        {
            removeAnim = false;
            if (currentFrame != 5)
            {
                newFrame++;
                if (newFrame == 13 && !entry->flags.has(WallSceneryFlag::hasLongDoorAnimation))
                    newFrame = 15;
            }
        }
        if (currentFrame != newFrame)
        {
            wall.setAnimationFrame(newFrame);
            MarkMapTilePresentationDirty(loc);
        }
    }
    return !removeAnim;
}

static bool UpdateTile(const TileCoordsXY& coords)
{
    auto* element = MapGetFirstElementAt(coords);
    if (element == nullptr)
        return false;
    bool active = false;
    do
    {
        if (element->getType() == TileElementType::smallScenery)
            active |= UpdateClock(*element->asSmallScenery(), coords.toCoordsXY());
        else if (element->getType() == TileElementType::wall)
            active |= UpdateWallDoor(*element->asWall(), coords.toCoordsXY());
    } while (!(element++)->isLastForTile());
    return active;
}

static bool UpdateOnRidePhotoAnimation(TrackElement& track, const CoordsXYZ& coords)
{
    if (!track.isTakingPhoto())
        return false;
    track.decrementPhotoTimeout();
    MarkMapTilePresentationDirty(coords);
    return true;
}

static bool UpdateLandEdgeDoorsAnimation(TrackElement& track, const CoordsXYZ& coords)
{
    if (getGameState().currentTicks & 3)
        return true;

    bool isAnimating = false;
    bool changed = false;
    const auto doorAState = track.getDoorAState();
    if (doorAState >= kLandEdgeDoorFrameEnd)
    {
        track.setDoorAState(kLandEdgeDoorFrameClosed);
        changed = true;
    }
    else if (doorAState != kLandEdgeDoorFrameClosed && doorAState != kLandEdgeDoorFrameOpen)
    {
        track.setDoorAState(doorAState + 1);
        changed = true;
        isAnimating = true;
    }
    const auto doorBState = track.getDoorBState();
    if (doorBState >= kLandEdgeDoorFrameEnd)
    {
        track.setDoorBState(kLandEdgeDoorFrameClosed);
        changed = true;
    }
    else if (doorBState != kLandEdgeDoorFrameClosed && doorBState != kLandEdgeDoorFrameOpen)
    {
        track.setDoorBState(doorBState + 1);
        changed = true;
        isAnimating = true;
    }
    if (changed)
        MarkMapTilePresentationDirty(coords);
    return isAnimating;
}

static bool UpdateTemporaryAnimation(const TemporaryMapAnimation& animation)
{
    const TileCoordsXYZ tileCoords{ animation.location };
    TileElement* element = MapGetFirstElementAt(tileCoords);
    if (element == nullptr)
        return true;
    bool isAnimating = false;
    do
    {
        if (element->getType() != TileElementType::track || element->baseHeight != tileCoords.z)
            continue;
        auto& track = *element->asTrack();
        switch (animation.type)
        {
            case MapAnimations::TemporaryType::onRidePhoto:
                if (track.getTrackType() == TrackElemType::onRidePhoto)
                    isAnimating |= UpdateOnRidePhotoAnimation(track, animation.location);
                break;
            case MapAnimations::TemporaryType::landEdgeDoor:
                isAnimating |= UpdateLandEdgeDoorsAnimation(track, animation.location);
                break;
        }
    } while (!(element++)->isLastForTile());
    return isAnimating;
}

static bool NeedsStateUpdate(const TileElementBase& element)
{
    if (element.getType() == TileElementType::wall)
    {
        const auto* wall = element.asWall();
        const auto* entry = wall->getEntry();
        return entry != nullptr && entry->flags.has(WallSceneryFlag::isDoor) && wall->isAnimating();
    }
    if (element.getType() == TileElementType::smallScenery)
    {
        const auto* entry = element.asSmallScenery()->getEntry();
        return entry != nullptr && entry->flags.hasAll(SmallSceneryFlag::isAnimated, SmallSceneryFlag::isClock);
    }
    return false;
}

void MapAnimations::MarkTileForUpdate(const TileCoordsXY coords)
{
    if (!MapIsEdge(coords.toCoordsXY()))
        _mapAnimationsUpdate.insert(coords);
}

void MapAnimations::CreateTemporary(const CoordsXYZ& coords, const TemporaryType type)
{
    _temporaryMapAnimations.insert(TemporaryMapAnimation{ coords, type });
}

void MapAnimations::MarkAllTiles()
{
    TileElementIterator it;
    TileElementIteratorBegin(&it);
    while (TileElementIteratorNext(&it))
        if (NeedsStateUpdate(*it.element))
            MarkTileForUpdate(TileCoordsXY(it.x, it.y));
}

void MapAnimations::UpdateAll()
{
    PROFILED_FUNCTION();
    // Persistent clocks/doors update on even logical ticks, independent of camera visibility.
    if (!(getGameState().currentTicks & 1))
    {
        auto it = _mapAnimationsUpdate.begin();
        while (it != _mapAnimationsUpdate.end())
        {
            if (UpdateTile(*it))
                ++it;
            else
                it = _mapAnimationsUpdate.erase(it);
        }
    }
    // Photos update every logical tick; land doors retain their internal four-tick cadence.
    auto it = _temporaryMapAnimations.begin();
    while (it != _temporaryMapAnimations.end())
    {
        if (UpdateTemporaryAnimation(*it))
            ++it;
        else
            it = _temporaryMapAnimations.erase(it);
    }
}

void MapAnimations::ClearAll()
{
    _mapAnimationsUpdate.clear();
    _temporaryMapAnimations.clear();
}

void MapAnimations::ShiftAll(const TileCoordsXY amount)
{
    if (amount.x == 0 && amount.y == 0)
        return;
    std::set<TileCoordsXY, TileCoordsXYCmp> shifted;
    for (const auto coords : _mapAnimationsUpdate)
        shifted.insert(coords + amount);
    _mapAnimationsUpdate = std::move(shifted);
    std::set<TemporaryMapAnimation> temporary;
    for (const auto& animation : _temporaryMapAnimations)
        temporary.insert(TemporaryMapAnimation{ animation.location + CoordsXYZ(amount.toCoordsXY(), 0), animation.type });
    _temporaryMapAnimations = std::move(temporary);
}
