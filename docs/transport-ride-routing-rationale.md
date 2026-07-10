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

The scan is performed only when the guest's concrete destination, precipitation state, or map-topology epoch changes, or when its cached path goal is invalid. An active route stores the ride, boarding station, selected destination station, and topology epoch that justified it. Path or entrance edits therefore invalidate a commitment before boarding; an Extortive fare cannot rely on stale “no walking route” evidence. The ride subsystem validates its transient service cache at most once per simulation tick, computes quality once per changed ride, and prebuilds the bounded all-pairs journey table over at most the ride station limit. Boarding/destination lookup is a binary search over that tiny ordered table, allowing unreachable pairs to be omitted without corrupting later indices. Guest-specific walking, queue delay, crowding, vouchers, cash, and fare policy remain live overlays. This keeps normal per-step pathfinding unchanged and makes the extra work proportional to the small cached directed station graph, not repeated segment aggregation or park path tiles. The mature bounded footpath search remains the single walking-direction implementation on either side of the transport journey.

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
| Selected-leg quality | That edge's measured comfort and decoration percentages |
| Selected segment | `Segment A to B: time in seconds, distance in metres, fare value` |

The station-leg selector exposes every measured directed edge in stable endpoint order. The panel and route planner share `RideGetTransportLegQuality()` and the exact-endpoint `RideGetTransportSegment()` overload, while the heading retains `RideGetTransportQuality()` as the ride-wide overview. This keeps the selected edge's displayed comfort, decoration, and value identical to the factors guests use.

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

### Real station platform pre-queue

Miniature Railway, Monorail, Suspended Monorail, and Chairlift stations use a real second waiting stage. Immediately before a stopped train or chair departs, the station captures its actual linked cars, masked seat counts, orientation, reversed-car flags, and object-defined loading positions. When that vehicle physically clears the station, the captured template becomes available. The front guests then leave the external queue, walk through the entrance, and wait visibly beside their assigned future car/seat position. Removing them from the linked path queue immediately frees outside queue space. Both shipped Chairlift objects use two scalar loading positions (`4, 0`), no loading waypoints, and the established cardinal boarding path with a 32-coordinate entrance offset. The adapter reuses those semantics, so staged positions match existing boarding geometry rather than applying the rail offset.

The platform capacity is exactly one actual consist, `sum(car.num_seats & 0x7f)`, rather than a station-tile approximation. Before a stopped pose has been captured, supported services derive nominal capacity from their first valid linked train or chair; once captured, the stopped template's slot count is authoritative. Invalid or absent vehicle entities safely report zero instead of consulting a guessed station size. A transient station registry maps those slots to guest entity ids in queue order. The guest's existing ride, station, car, seat, destination, and new platform substate fields remain authoritative; the registry is only a deterministic index and is never saved. Unsupported station styles without platforms and Lift retain the established just-in-time boarding path and its conservative legacy tile estimate. The shipped 16-seat Lift cabin uses per-seat three-point loading waypoints, while shuttle mode treats the complete tower ascent as departing and reaches `FinishDeparting()` only at the tower top. It therefore lacks both the scalar slot geometry and generic physical-clear signal required by this adapter. Other loading-waypoint vehicle objects also retain just-in-time boarding, but their valid consist remains the nominal routing capacity. A supported service with incomplete entrance/exit or stopped-pose geometry does not activate staging, although its real consist remains the nominal capacity.

Platform admission itself is free. The guest repeats current affordability and price eligibility only when it is first in platform order and a real train has finished unloading. Only then does the guest reserve an actual free vehicle seat, pay the current journey fare, publish its complete queue time, and approach the arrived vehicle. If the fare changed beyond its means, it releases the abstract slot and follows the station exit. This avoids refunds and paid-but-never-ridden passengers after closure or consist changes.

Transport unloading still compacts through-riders to the front of each vehicle first. Waiting guests then bind FIFO into the remaining seats; overflow remains on the platform for the next train. Once an actual seat is reserved, the existing `num_peeps == next_free_seat` dispatch invariant keeps the train in the station until the assigned walker boards. Maximum-wait, block-section, leave-when-another-arrives, and synchronized-departure rules therefore retain their established ownership.

The route-planning overcrowding rule remains transport-only: a new route avoids the station only when both the external queue reports full and the real train-load platform is full. Coasters are not opted into transport routing or fare semantics. Station status displays external queue length separately from `platform occupancy / consist capacity`, and guest status distinguishes walking to and waiting on the platform.

Transport measurements use the same authoritative physical-leg boundary as multi-station ride ratings. Once a measured
origin-to-destination leg exists, its destination replaces the numeric station-order fallback and its own measured distance,
duration, speed, and distance-weighted comfort/decoration sample price that segment. Directed edges are keyed by both endpoints,
so a middle shuttle station may retain independently measured departures in both directions. Journey construction runs a small,
deterministic shortest-time search over those edges and adds each selected leg's time, distance, and fare exactly. Fare breaks an
equal-time tie. Cycles and unreachable destinations therefore cannot manufacture a partial journey.
Before a physical leg has been observed, the established numeric successor and ride-wide quality estimate remain the safe
fallback only for a station with no measured outbound edge. The service cache builds the same directed journeys, including routes
containing two or more segments and stations with more than one physical departure, rather than assuming that station array order
is track order. A destination-free request at an ambiguous shuttle departure returns no single segment instead of guessing.

This does not opt coaster stations into the transport platform pre-queue. Multi-station coasters share only the leg-rating
ownership and Measurements presentation; transport admission, routing, pricing, and platform staging remain gated by the
transport ride descriptor and the explicit platform adapter allowlist.

## Save compatibility and tests

Private park version `60012` stores the distance-weighted transport quality accumulators. Version `60013` adds the selected transport destination station and permits the appended Free price-policy value. Version `60014` recognises the platform approach/wait substates. Version `60015` stores rolling samples and final ratings keyed by the directed station pair. Pre-`60015` loads discard in-progress rating samples because those records have no authoritative departure station; completed legacy ride-wide ratings remain available. Older-target exports write coherent empty sample buffers while preserving the live current-version state. Current saves rebuild the transient platform registry once from guest state after entity import. Occupied slots recover their saved visible destinations; unknown empty slot coordinates remain closed to admission until the next stopped template is captured, so new guests cannot bypass the recovered FIFO cohort. Old saves naturally contain no platform cohort. Exports targeting an older version serialize a platform guest as a normal station-exit approach with an exit-side destination, without mutating the live guest or exposing an unknown substate.

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
- the real rail/Chairlift platform cohort has exact stopped-consist capacity, frees external queue space, waits visibly, and binds FIFO after through-rider compaction, while Lift retains safe JIT boarding;
- current-version platform state round-trips, while an older target receives a coherent station-exit recovery state;
- platform capacity and route overcrowding remain transport-only; and
- segment distance remains the leading fare input.
