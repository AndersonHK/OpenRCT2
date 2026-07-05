# Real park seasonality research

Research date: 2026-07-04.

This note checks whether OpenRCT2's inherited March-through-October operating year is a reasonable model for real amusement parks. The short answer is: it is reasonable for a temperate seasonal-park abstraction, but it is not universal. Real parks fall into several patterns: full winter closure, shoulder-season weekend or event operations, year-round or near-year-round theme-park operation, and separately seasonal water-park operation.

Exact operating calendars are live park data and can change after this research date.

## Current in-game calendar

OpenRCT2 inherits the RCT2 eight-month operating year. `src/openrct2/Date.h` defines only March, April, May, June, July, August, September, and October, with `MONTH_COUNT` equal to those eight months. `src/openrct2/Date.cpp` also treats a game year as `monthsElapsed / MONTH_COUNT`.

That means a scenario "year" is not a literal twelve-month civil year. It is already an operating-season abstraction.

## Findings

| Park | Evidence checked | Seasonality read |
| --- | --- | --- |
| Alton Towers, included through RCT1 scenario support | The official opening-times page says the theme park is open March until November, then closes for winter around the start of November. It also lists selected Christmas/Festive Short Breaks and says the waterpark operates almost all year. Source: https://www.altontowers.com/plan-your-visit/before-you-visit/opening-times/ | Mostly seasonal theme-park operation, with selected off-season resort/waterpark products rather than a full winter theme-park month. |
| Blackpool Pleasure Beach, included through RCT1 scenario support | The official opening-times page says gates are open from March through the end of November, with opening and closing times varying by time of year. Source: https://www.blackpoolpleasurebeach.com/opening-times-prices/ | Seasonal operation with variable service levels. March-through-October misses some November operation but broadly matches the main operating season. |
| Six Flags Great Adventure, included through RCT2 scenario support | Official Opening Day is March 28, 2026. Official Holiday in the Park returns in Winter 2026 with entertainment, lights, and thrill rides. Sources: https://www.sixflags.com/greatadventure/events/opening-day and https://www.sixflags.com/greatadventure/events/holiday-in-the-park | Main season begins in March, but winter is not simply closed; selected holiday-event operations matter. |
| Six Flags over Texas, included through RCT2 scenario support | Official Opening Weekend begins February 28, 2026. Source: https://www.sixflags.com/overtexas/events/opening-weekend | Warmer-climate seasonal calendar starts before March; a strict March start is too late for this park. |
| Six Flags Magic Mountain, included through RCT2 scenario support | Official pages advertise Holiday in the Park as winter fun with select rides, and memberships/passes are marketed around all-season or year-round thrills. Sources: https://www.sixflags.com/magicmountain/events/holiday-in-the-park and https://www.sixflags.com/magicmountain/memberships | Near-year-round or year-round operation. A no-winter simulation is not realistic for this park type. |
| Walibi Holland, formerly Six Flags Holland in RCT2 | The official opening-hours page labels Season 2026 and says hours vary by period, with longer summer hours than fall. Source: https://www.walibi.nl/en/plan-your-visit/opening-hours | Seasonal/variable operation. This supports reduced shoulder-season modeling more than a flat full-service calendar. |
| Six Flags Great America, common comparison park | Official event pages show 2026 summer anniversary events from June 20 through August 9, and Halloween/Oktoberfest/Fright Fest events on select dates September 19 through November 1. Local reporting based on the park announcement gives April 25, 2026 as opening day. Sources: https://www.sixflags.com/greatamerica/events and https://www.lakemchenryscanner.com/2026/04/09/six-flags-great-america-in-gurnee-announces-park-will-officially-open-for-2026-season-later-this-month/ | Strongly seasonal in the Midwest: closed winter, opens later than March, extends through Halloween on selected dates. |
| California's Great America, common comparison park | A Six Flags-linked press release repost says the park opens March 28, 2026 and South Bay Shores opens May 23. The official event page currently shows July events. Sources: https://www.parkjourney.com/industry-news/californias-great-america-kicks-off-milestone-50th-season-on-march-28 and https://www.sixflags.com/cagreatamerica/events | Theme park opens in spring, while the water park has a narrower late-spring/summer season. |
| Cedar Point, common seasonal benchmark | Official Opening Day is May 9, 2026 for the 157th summer season. Source: https://www.sixflags.com/cedarpoint/events/opening-day | Classic seasonal park. March and April are not normal operating months; May-through-fall is the better abstraction. |
| Knott's Berry Farm, common warm-climate benchmark | Official events page says its "Seasons of Fun" occur throughout the year and includes Knott's Merry Farm from November 20, 2026 through January 3, 2027. Knott's Soak City is separately listed for select dates May 16 through September 7, 2026. Sources: https://www.sixflags.com/knotts/events and https://www.sixflags.com/knotts/soak-city | Theme-park operation is year-round or near-year-round, while the adjacent water park is summer-seasonal. |

## Design implications

The current eight-month calendar is defensible if the game is modeling a generic temperate seasonal amusement park, especially one like Alton Towers, Blackpool Pleasure Beach, Six Flags Great America, or Cedar Point. It is not a good universal model for warm-climate parks, destination parks, or parks with winter holiday events.

If this fork later expands beyond March-through-October, the extra months should not behave like normal summer months by default. A stronger model would make seasonality a scenario or park profile:

- `Closed`: no ordinary guest generation, no normal ride operation, but finance, maintenance, construction, research, loans, and scenario timers may still advance depending on design goals.
- `Reduced`: lower guest generation, shorter implied hours, fewer water rides, lower food/drink demand, and higher weather-driven happiness/comfort sensitivity.
- `Event`: winter or Halloween operations with special demand, selected attractions, higher entertainment/decoration weight, and possible cold-weather ride limits.
- `Full`: normal operating months, roughly matching the current assumptions.

This would let northern seasonal parks close or thin out, while warm-climate parks and holiday-event parks continue operating without pretending January is another July.

## Current implementation boundary

No gameplay seasonality profile is implemented in this documentation pass. Guest generation and park rating still assume every in-game month is an ordinary operating month within the inherited eight-month RCT calendar.
