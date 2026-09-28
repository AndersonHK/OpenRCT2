/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "CarEntry.h"

#include "../drawing/Drawing.Sprite.h"
#include "../drawing/SpriteAssetDecoder.h"
#include "../entity/Yaw.hpp"

#include <algorithm>
#include <cstdint>

uint32_t CarEntry::numRotationSprites(SpriteGroupType spriteGroup) const
{
    return NumSpritesPrecision(spriteGroups[EnumValue(spriteGroup)].spritePrecision);
}

int32_t CarEntry::spriteByYaw(int32_t yaw, SpriteGroupType spriteGroup) const
{
    return YawToPrecision(yaw, spriteGroups[EnumValue(spriteGroup)].spritePrecision);
}

bool CarEntry::groupEnabled(SpriteGroupType spriteGroup) const
{
    return spriteGroups[EnumValue(spriteGroup)].isEnabled();
}

uint32_t CarEntry::groupImageId(SpriteGroupType spriteGroup) const
{
    return spriteGroups[EnumValue(spriteGroup)].imageId;
}

uint32_t CarEntry::getSpriteOffset(SpriteGroupType spriteGroup, int32_t imageDirection, uint8_t rankIndex) const
{
    return ((spriteByYaw(imageDirection, spriteGroup) + numRotationSprites(spriteGroup) * rankIndex) * baseNumFrames)
        + groupImageId(spriteGroup);
}

uint8_t CarEntry::getNumRiderImageBanks() const
{
    if (numSeatingRows == 0 || !flags.has(CarEntryFlag::hasRiderAnimation) || animationFrames == 0)
        return numSeatingRows;

    uint8_t animationBanks = 1;
    switch (animation)
    {
        case CarEntryAnimation::simpleVehicle:
        case CarEntryAnimation::steamLocomotive:
        case CarEntryAnimation::monorailCycle:
        case CarEntryAnimation::multiDimension:
        case CarEntryAnimation::observationTower:
            animationBanks = animationFrames;
            break;
        case CarEntryAnimation::swanBoat:
            // AnimateSwanBoat doubles GetTargetFrame's index before storing it
            // in a byte. Two authored frames therefore use rider banks 0 and 2.
            animationBanks = static_cast<uint8_t>(std::min<uint16_t>((animationFrames - 1) * 2, 254) + 1);
            break;
        case CarEntryAnimation::animalFlying:
            // UpdateAnimationAnimalFlying explicitly cycles modulo 4.
            animationBanks = 4;
            break;
        default:
            break;
    }
    return std::max(numSeatingRows, animationBanks);
}

/**
 *
 *  rct2: 0x006847BA
 */
void CarEntrySetImageMaxSizes(CarEntry& carEntry, int32_t numImages)
{
    OpenRCT2::Drawing::SpriteAssetBoundsAccumulator inference;
    for (int32_t i = 0; i < numImages; ++i)
    {
        if (const auto* element = GfxGetG1Element(carEntry.baseImageId + i); element != nullptr)
            inference.Add(*element);
    }
    const auto bounds = inference.GetBounds();
    int32_t spriteHeightNegative = bounds.heightNegative;

    // Moved from object paint

    if (carEntry.flags.has(CarEntryFlag::spriteBoundsIncludeInvertedSet))
    {
        spriteHeightNegative += 16;
    }

    carEntry.spriteWidth = bounds.width;
    carEntry.spriteHeightNegative = spriteHeightNegative;
    carEntry.spriteHeightPositive = bounds.heightPositive;
}

bool CarEntry::isVisible() const
{
    return tabRotationMask != 0;
}
