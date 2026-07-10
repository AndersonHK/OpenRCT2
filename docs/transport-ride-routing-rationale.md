# Transport ride routing rationale

## Goal

Transport rides are mobility services. Guests should board them because a station-to-station leg helps them reach a concrete destination, not because the normal attraction chooser happened to select a railway, monorail, chairlift, or lift.

This follows the central limitation described in Marcel Vos's *RCT2 - Ride overview - Transport rides*: the original guest logic largely treats transports like ordinary rides and does not deliberately use them to reach another part of the park. The implementation retains the existing ride and station machinery, but gives transport rides separate guest admission, route value, and trip-result semantics.

## Route choice

Every concrete guest destination is resolved before travel-mode choice. Park exits and the entrances for attractions, shops, first aid, toilets, cash machines, and other facilities all pass through `GuestPathFindToDestination()`. Ride-specific advertising already assigns the same `guestHeadingToRideId` used by ordinary target selection, so advertised destinations enter this bottleneck as soon as the guest is inside the park. Entry gates and off-map spawn points also use the helper, but transport is deliberately unavailable while the guest remains outside the park.

When that resolved destination changes, the existing bounded walking search is run once. Its first direction is retained if walking wins, while its success or failure is also the connectivity test for extortive fares. `PathFinding::PlanTransportRoute()` then compares direct walking with every usable forward station-to-station journey on each open transport ride. A journey may remain aboard through any number of intermediate stations. Its estimated time is:

`walk to boarding entrance + queue/boarding delay + all onboard segment times + walk from destination exit`

The existing bounded footpath search still chooses each walking direction. The transport scan only supplies an intermediate station goal; it does not replace the mature path walker or run a park-wide A* search.

All terms are converted to milliseconds before comparison. The walking estimate converts the existing geometric path heuristic to metres at four metres per path tile and applies a three-mph baseline adjusted by the guest's energy and slow-walk state. Segment time uses the measured station-to-station duration when available, falling back to displayed distance and speed. Queue time is converted from minutes and boarding adds eight seconds.

The configured fare bucket controls how strong the time advantage must be:

- Free service can win a reasonable tie: up to 15 seconds or 10% slower than walking in dry weather.
- Discount service must save at least five seconds and 10% of walking time.
- Fair service must save at least ten seconds and 20% of walking time.
- Extortive service is never considered while the walking search succeeds.

During precipitation, any positive saving is enough for discount and fair service, while free service receives a wider tie tolerance. Rain discounts only the onboard part of candidate ranking by 15%, plus another 2.5% for every sheltered eighth of the ride, so sheltered service wins close comparisons without pretending that the walks to and from the station are covered. Each guest remembers whether the last mode comparison occurred during precipitation, so a weather transition causes one immediate re-evaluation even when the concrete destination did not change.

The scan is performed only when the guest's concrete destination, precipitation state, or map-topology epoch changes, or when its cached path goal is invalid. An active route stores the ride, boarding station, selected destination station, and topology epoch that justified it. Path or entrance edits therefore invalidate a commitment before boarding; an Extortive fare cannot rely on stale “no walking route” evidence. The ride subsystem validates its transient service cache at most once per simulation tick, computes quality once per changed ride, builds every forward journey in quadratic station count, and provides O(1) boarding/destination lookup. Guest-specific walking, queue delay, crowding, vouchers, cash, and fare policy remain live overlays. This keeps normal per-step pathfinding unchanged and makes the extra work proportional to the number of cached directed station pairs, not repeated segment aggregation or park path tiles. The mature bounded footpath search remains the single walking-direction implementation on either side of the transport journey.

Transport trains selectively unload only guests whose stored destination is the current station. Continuing passengers are kept at the front of the vehicle's compact seat array; alighting passengers remain at its tail so the established last-seat-first exit animation and mass bookkeeping stay valid. Non-transport rides retain the original unload-everyone behavior.

## Fare value

Transport fares are calculated for the selected journey rather than stored as one ride-wide ticket price. Each segment contributes this quality-adjusted value:

`distance value * speed factor * comfort factor * decoration factor`

- Distance leads the calculation at one half-cent per displayed metre, with a 20-cent minimum base.
- Speed uses displayed average speed, falling back to maximum speed. Twelve mph is `1.0`; the factor is clamped to `0.6..1.8`.
- Comfort starts at `1.0` for each moving vehicle tick. Deviation from neutral vertical G subtracts two points per hundredth of a G; lateral and realised longitudinal G subtract three. The result is clamped to `0.1..1.0`.
- Decoration starts at `1.0` and adds the existing local scenery score, capped at `1.5`.

Comfort and decoration are both multiplied by distance travelled on every sampled tick before aggregation. A short rough section therefore matters in proportion to its length rather than receiving the same weight as a long smooth segment. Rides without a completed sample use conservative neutral defaults: `0.9` comfort and `1.0` decoration.

Segment values are added across the selected journey, then the configured fare bucket is applied:

| Bucket | Fare |
| --- | --- |
| Free | zero |
| Discount | 50% of journey value |
| Fair price | 100% of journey value |
| Extortive | 200% of journey value |

The resulting fare remains within the existing `$0.20..$20.00` bounds except that Free is exactly zero. A voucher, a no-money park, or locked ride pricing makes the guest's effective route-choice bucket Free without changing the operator's setting. Guests must be able to afford paid fares. The same journey function is used during planning and boarding so a route cannot be admitted using a next-segment estimate and charged using a different distance.

An extortive journey is a connectivity fallback, not merely an expensive alternative. It may be selected only if the direct walking search failed and no usable non-extortive transport journey exists. Paying it reduces happiness and produces the specific thought, “The fare for X was extortionate!”

## Measurements screen

The ride measurements tab replaces attraction ratings with a transport-specific service panel for transport rides. Its template is:

| Row | Display |
| --- | --- |
| Heading | Transport service quality |
| Comfort | Measured percentage, or `90% (estimated)` before a completed sample |
| Decoration bonus | Measured bonus above neutral, or `+0% (estimated)` before a completed sample |
| Average speed | Existing measured average-speed display |
| Segment rows | `Segment A to B: time in seconds, distance in metres, fare value` |

The panel and route planner both consume `RideGetTransportQuality()` and `RideGetTransportSegment()`. This avoids separately rescanning the rolling rating samples for comfort and decoration and keeps displayed segment values identical to the values guests use.

The income tab presents transport policy instead of a misleading representative ticket price. Its dropdown template is:

| Selection | Caption |
| --- | --- |
| Free | `Free (no fare)` |
| Discount | `Discount (50% of journey value)` |
| Fair price | `Fair price (100% of journey value)` |
| Extortive | `Extortive (200% of journey value)` |

## Guest semantics

A guest boards a transport ride only when its ride and entrance match the stored route. The former special case that made free transports automatically acceptable is removed. Planned transport remains available to unhappy guests and guests leaving the park because reaching their target is the purpose of the service.

A completed transport journey does not increment ordinary ride count or ride history and does not alter favourite-ride selection, ride satisfaction, or nausea. Happiness changes only for the explicit extortive-fare penalty. The existing station code still owns queues, ticket payment, revenue, and customer throughput.

Transport platforms have a finite two-guests-per-station-tile capacity. This rule is gated by `RtdFlag::isTransportRide`; coaster and ordinary ride stations are unchanged. A new route avoids a station only when both its queue admission has reported full and the vehicle-derived platform occupancy has reached capacity. Platform occupancy uses reserved/loading seats and actively alighting passengers, avoiding a park-wide guest scan. The station-full signal is transient and rebuilt by live admission, so it is not saved.

## Save compatibility and tests

Private park version `60012` stores the distance-weighted transport quality accumulators. Version `60013` adds the selected transport destination station and permits the appended Free price-policy value. The previous-ride id, otherwise-unused high timeout bit, and current station retain ride and boarding state without repurposing a peep flag. Older parks load a null transport destination, Fair pricing, empty quality totals, and empty transient crowding state.

Focused coverage verifies that:

- a reasonably priced monorail is selected over a long walk and unaffordable journey fares are rejected;
- route comparisons use measured/fallback segment seconds and guest-adjusted walking seconds rather than distance-like score units;
- free transports are not boarded without a planned leg, including by a guest leaving the park;
- rain admits a useful marginal route that dry-weather thresholds reject;
- Free tie tolerance and the stricter Discount and Fair thresholds;
- Extortive routing is impossible with either a walking route or a usable non-extortive service;
- park exits, advertised attractions, first aid, and every other ride or facility target converge on the same resolved-destination helper;
- transport completion does not count as an attraction visit;
- G-forces reduce distance-weighted comfort and scenery raises decoration value;
- the transport measurements panel exposes comfort, decoration, average speed, and per-segment time/distance/value; and
- two-segment and longer journeys accumulate time, distance, and fare and alight at the stored station;
- platform capacity is transport-only; and
- segment distance remains the leading fare input.
