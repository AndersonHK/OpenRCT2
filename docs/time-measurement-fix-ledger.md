# Time measurement fix ledger

Source: the caption transcript for Marcel Vos, "Time is a complete mess in RollerCoaster Tycoon 2", checked from https://www.youtube.com/watch?v=uQ6BD0XB6d4 on 2026-07-05. TIME-012 also uses the caption transcript and summary table from Marcel Vos, "How Long is a Tile in RCT2?", checked from https://www.youtube.com/watch?v=wMbxkzbyW4E on 2026-07-05.

This fork treats real-time simulation as canonical where the UI or gameplay text says seconds, minutes, hours, or weeks:

- 40 game ticks = 1 real second.
- 2400 game ticks = 1 real minute.
- 144000 game ticks = 1 real hour.
- 7 calendar days = 1 week.
- Calendar months remain the existing OpenRCT2/RCT operating months. Monthly systems such as staff wages and research still pay through the established quarter-month finance cadence unless they are explicitly described as weeks.

## Fixed items

| ID | Issue | Fix |
| --- | --- | --- |
| TIME-001 | Ride duration was sampled once per 32 ticks, so a displayed 20 second test run could take about 16 real seconds. | Ride test segment time now samples once per `GameTime::kTicksPerSecond` in `Vehicle.cpp`. |
| TIME-002 | Minimum and maximum station waiting times used `seconds * 32`, and station depart countdowns decremented every 32 ticks. | Waiting comparisons now use `GameTime::SecondsToTicks()`, and station countdowns decrement on whole 40-tick seconds. |
| TIME-003 | Customers per hour and income per hour used ten 960-tick buckets, making the five-minute history only 240 real seconds. | Customer history buckets now roll every 30 real seconds. `RideCustomersPerHour()` again scales a five real-minute window by 12. |
| TIME-004 | Dual-item stalls assumed a 50/50 item mix, and photo sessions mixed riders and photo purchases inconsistently. | Stall income per hour now uses rolling primary and secondary item sale counts. On-ride photo income keeps admission revenue for every rider and adds photo profit from rolling photo sales. |
| TIME-005 | Air time used `ticks * 3`, so 40 ticks displayed as 1.20 seconds. | Air time now converts ticks to centiseconds using 40 ticks per second. |
| TIME-006 | Time since last inspection and inspection intervals used 2048 ticks per displayed minute. | Ride inspection minutes now advance every 2400 ticks. |
| TIME-007 | Guest time in park used `ticks >> 11`, another 2048-tick minute. | Guest stat display now uses `GameTime::TicksToMinutes()`. |
| TIME-008 | Guests who had not ridden anything used 2048-tick minutes before deciding to find a ride or leave. | The five-minute check now uses `GameTime::MinutesToTicks(5)`. |
| TIME-009 | Queue time was derived from in-game days, added three extra days, then divided by two. | Station queue time now uses the guest's actual `timeInQueue` counter converted to real minutes. Zero queue time remains zero. |
| TIME-010 | Running costs displayed `upkeepCost * 16`, making the "per hour" cost an operating year of about 54.6 real minutes. | `RideGetUpkeepCostPerHour()` scales half-month upkeep payments to 144000 ticks, and both ride windows and ride lists use it. |
| TIME-011 | Profit per hour subtracted a 54.6-minute running cost from a 48-minute income. | Profit now subtracts the same real-hour upkeep used by the displayed running cost. |
| TIME-012 | Ride length used an inflated scale: the ride-length method gives about 4.26 m per tile, while park area and ride speed both point to about 3.16 m per tile. | Vehicle speed display was restored to the original real-world-aligned conversion. Core ride length storage is scaled by `158 / 213` when accumulated, migrated from older `.park` saves, and imported from legacy S4/S6 and T4/T6 data, so ratings, objectives, scripting, and UI now consume the corrected real length. |
| TIME-013 | Advertising campaign weeks waited for rigid quarter-month boundaries and could grant much more than the bought duration. | Campaign durations now count down daily for `weeks * 7` days. S4/S6 imports convert legacy campaign week counters to days, and the finance UI rounds remaining days back up to displayed weeks. |
| TIME-014 | The weekly history/profit graph used rigid quarter-month "weeks". | Park histories now update every seven calendar days. Quarter-month finance payments were left separate. |
| TIME-015 | New ride age advanced at the next month boundary even if the ride was built near month end. | Newly built and renewed rides now store a precise fixed-point build month. Legacy month-count build dates still decode with old semantics. |
| TIME-016 | RCT2 loan interest used `(loan * 5 * rate) >> 14`, dividing by 3276.8 instead of 3200 payments-per-percent-year. | RCT2 interest now divides by `100 * 32`, matching eight operating months times four finance periods. RCT1 legacy interest remains behind its existing compatibility flag. |
| TIME-017 | Dodgems time limits used `timeLimit * 32` against a 256-tick activity counter. | Dodgems now convert the displayed time limit with `GameTime::SecondsToTicks()`. |
| TIME-018 | Stalled-vehicle warnings used 9600 and 15360 ticks, matching five and eight minutes only at 32 TPS. | Stalled-vehicle limits now use `GameTime::MinutesToTicks(5)` and `GameTime::MinutesToTicks(8)`. |
| TIME-019 | Weather transitions used 1920/960 tick timers, matching 60/30 seconds only at 32 TPS. | Weather transition timers and the toolbar reveal threshold now use 60 and 30 real seconds. |
| TIME-020 | Boat-hire vehicles started biasing back toward the return point after 1920 ticks, matching one minute only at 32 TPS. | Boat-hire return guidance now waits `GameTime::SecondsToTicks(60)`. |

## Reviewed but not changed

| ID | Issue | Status |
| --- | --- | --- |
| TIME-R01 | Staff wages and research are described monthly, not weekly. | Left on the existing quarter-month finance cadence because the monthly total remains correct and the video called these correct. |
| TIME-R02 | `Date::IsWeekStart()` still means the legacy quarter-month boundary. | Left in place for finance-period systems. New seven-day behavior is implemented at the caller that needs real weeks instead of changing the global calendar helper. |
| TIME-R03 | Existing `.park` saves with active marketing campaigns already serialized in `weeksLeft` are ambiguous. | New campaigns and imported S4/S6 campaigns use days. Existing OpenRCT2 park saves keep their stored counter value because there is no reliable marker distinguishing old weeks from new days in that field. |
| TIME-R04 | The same tile-length video finds height-oriented methods around 3.51 m to 3.56 m per tile, which does not match the horizontal 3.16 m scale. | Left for future work. This pass corrects horizontal ride length to agree with park area and vehicle speed, and does not change height or Z-unit conversions. |
