/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../Date.h"
#include "../Game.h"

#include <cstdint>

namespace OpenRCT2::GameTime
{
    constexpr uint32_t kTicksPerSecond = kGameUpdateFPS;
    constexpr uint32_t kSecondsPerMinute = 60;
    constexpr uint32_t kMinutesPerHour = 60;
    constexpr uint32_t kHoursPerDay = 24;
    constexpr uint32_t kDaysPerWeek = 7;

    constexpr uint32_t kTicksPerMinute = kTicksPerSecond * kSecondsPerMinute;
    constexpr uint32_t kTicksPerHour = kTicksPerMinute * kMinutesPerHour;

    constexpr uint32_t kCalendarMonthsPerYear = MONTH_COUNT;
    constexpr uint32_t kCalendarFinancePeriodsPerMonth = 4;
    constexpr uint32_t kCalendarFinancePeriodsPerYear = kCalendarMonthsPerYear * kCalendarFinancePeriodsPerMonth;
    constexpr uint32_t kGameTicksPerCalendarMonth = kTicksPerMonth / kMonthTicksIncrement;
    constexpr uint32_t kGameTicksPerCalendarHalfMonth = kGameTicksPerCalendarMonth / 2;

    constexpr uint32_t SecondsToTicks(uint32_t seconds)
    {
        return seconds * kTicksPerSecond;
    }

    constexpr uint32_t MinutesToTicks(uint32_t minutes)
    {
        return minutes * kTicksPerMinute;
    }

    constexpr uint32_t TicksToSeconds(uint32_t ticks)
    {
        return ticks / kTicksPerSecond;
    }

    constexpr uint32_t TicksToMinutes(uint32_t ticks)
    {
        return ticks / kTicksPerMinute;
    }

    constexpr uint32_t TicksToCentiseconds(uint32_t ticks)
    {
        return ((ticks * 100) + (kTicksPerSecond / 2)) / kTicksPerSecond;
    }

    constexpr bool IsWholeSecondTick(uint32_t currentTicks)
    {
        return (currentTicks % kTicksPerSecond) == 0;
    }

    inline uint32_t DateToMonthTicks(const Date& date)
    {
        return (date.GetMonthsElapsed() * kTicksPerMonth) + date.GetMonthTicks();
    }
} // namespace OpenRCT2::GameTime
