// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once

#include <openrct2/drawing/RetainedPeepState.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Staff.h>
#include <stdexcept>

// Diagnostic baseline only. Production publication captures notified field groups directly.
namespace OpenRCT2::Drawing
{
    struct RetainedPeepPreviousPosition
    {
        EntityVisualHandle handle;
        uint32_t tick{};
        CoordsXYZ position{};
    };


    inline void ValidateCapturedPeepRecord(const RetainedPeepRecord& record)
    {
        if (record.id >= kMaxEntities || record.generation == 0)
            throw std::invalid_argument("Invalid retained peep identity");
        if (!(record.flags & kRetainedPeepPresent))
        {
            RetainedPeepRecord tombstone{};
            tombstone.id = record.id;
            tombstone.generation = record.generation;
            if (record != tombstone)
                throw std::invalid_argument("Retained peep tombstone carries live state");
            return;
        }
        if ((record.flags & ~(kRetainedPeepPresent | kRetainedPeepStaff | kRetainedPeepInterpolate | kRetainedPeepStateMask)) != 0
            || record.objectIndex >= UINT16_MAX || record.objectGeneration == 0 || record.orientation > 31
            || record.action > UINT8_MAX || record.animationGroup > UINT8_MAX || record.animationType > UINT8_MAX
            || record.nextAnimationType > UINT8_MAX || record.frameOffset > UINT8_MAX || record.colours > UINT16_MAX
            || record.width > UINT8_MAX || record.heightMin > UINT8_MAX || record.heightMax > UINT8_MAX)
            throw std::invalid_argument("Invalid retained peep raw scalar range");
        if (record.flags & kRetainedPeepInterpolate)
        {
            if (record.sourceTick - record.previousTick != 1)
                throw std::invalid_argument("Retained peep interpolation requires adjacent authoritative ticks");
        }
        else if (record.previousTick != record.sourceTick || record.previousX != record.x
            || record.previousY != record.y || record.previousZ != record.z)
            throw std::invalid_argument("Retained peep without history has different previous state");
    }


    inline RetainedPeepRecord CaptureRetainedPeepRecord(
        const Peep& peep, EntityVisualHandle handle, uint32_t objectGeneration, uint32_t tick,
        const std::optional<RetainedPeepPreviousPosition>& previous = std::nullopt)
    {
        if (handle.epoch == 0 || handle.generation == 0 || handle.id != peep.id
            || (peep.type != EntityType::guest && peep.type != EntityType::staff))
            throw std::invalid_argument("Retained peep capture identity mismatch");
        RetainedPeepRecord result{};
        result.id = peep.id.ToUnderlying();
        result.generation = handle.generation;
        result.x = result.previousX = peep.x;
        result.y = result.previousY = peep.y;
        result.z = result.previousZ = peep.z;
        result.sourceTick = result.previousTick = tick;
        result.objectIndex = peep.animationObjectIndex;
        result.objectGeneration = objectGeneration;
        result.orientation = peep.orientation;
        result.action = static_cast<uint32_t>(peep.action);
        result.animationGroup = static_cast<uint32_t>(peep.animationGroup);
        result.animationType = static_cast<uint32_t>(peep.animationType);
        result.nextAnimationType = static_cast<uint32_t>(peep.nextAnimationType);
        result.frameOffset = peep.animationImageIdOffset;
        result.colours = static_cast<uint32_t>(peep.getTShirtColour()) | (static_cast<uint32_t>(peep.getTrousersColour()) << 8);
        result.width = peep.spriteData.width;
        result.heightMin = peep.spriteData.heightMin;
        result.heightMax = peep.spriteData.heightMax;
        result.flags = kRetainedPeepPresent | (static_cast<uint32_t>(peep.state) << kRetainedPeepStateShift);
        if (peep.type == EntityType::guest)
        {
            const auto& guest = static_cast<const Guest&>(peep);
            result.accessoryColours = static_cast<uint32_t>(guest.getHatColour())
                | (static_cast<uint32_t>(guest.getBalloonColour()) << 8) | (static_cast<uint32_t>(guest.getUmbrellaColour()) << 16);
        }
        else
        {
            const auto& staff = static_cast<const Staff&>(peep);
            result.flags |= kRetainedPeepStaff;
            result.accessoryColours = static_cast<uint32_t>(staff.assignedStaffType) << 24;
        }
        if (previous)
        {
            if (previous->handle != handle || tick - previous->tick != 1)
                throw std::invalid_argument("Retained peep history belongs to another identity or tick");
            result.previousX = previous->position.x;
            result.previousY = previous->position.y;
            result.previousZ = previous->position.z;
            result.previousTick = previous->tick;
            result.flags |= kRetainedPeepInterpolate;
        }
        ValidateCapturedPeepRecord(result);
        return result;
    }

}
