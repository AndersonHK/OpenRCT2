# Transport ride routing rationale

## Goal

Transport rides are mobility services. Guests should board them because a station-to-station leg helps them reach a concrete destination, not because the normal attraction chooser happened to select a railway, monorail, chairlift, or lift.

This follows the central limitation described in Marcel Vos's *RCT2 - Ride overview - Transport rides*: the original guest logic largely treats transports like ordinary rides and does not deliberately use them to reach another part of the park. The implementation retains the existing ride and station machinery, but gives transport rides separate guest admission, route value, and trip-result semantics.

## Route choice

Every concrete guest destination is resolved before travel-mode choice. Park exits and the entrances for attractions, shops, first aid, toilets, cash machines, and other facilities all pass through `GuestPathFindToDestination()`. Ride-specific advertising already assigns the same `guestHeadingToRideId` used by ordinary target selection, so advertised destinations enter this bottleneck as soon as the guest is inside the park. Entry gates and off-map spawn points also use the helper, but transport is deliberately unavailable while the guest remains outside the park.

When a ride or the park has multiple concrete entrances, their exact shared-field distances are compared in one ordered batch.
The source path node is resolved once, equal distances retain station/park order, and the connectivity epoch invalidates the
whole lookup. Missing or inexact fields retain geometric selection. Exact unreachable fields reject that destination rather
than silently reintroducing it through Manhattan distance. Ordinary attraction/facility and first-aid choice batch their
candidate ride ids through the same exact fields; advertising continues to specify one ride id and changes only which target
reaches the pathfinder.

All of those resolved walking targets, including a committed transport boarding station and the final walk after a multi-leg
journey, share one topology-epoch-owned flat source-node index. Exact packed XYZ equality resolves hash collisions, while candidate
and service ordering remain outside the index. Rebuilding or resetting the route fields replaces or releases the matching index, so
no route can reuse a source-node mapping from another topology.

When that resolved destination changes, the existing bounded walking search is run once. Its first direction is retained if walking wins, while its success or failure is also the connectivity test for extortive fares. `PathFinding::PlanTransportRoute()` then compares direct walking with every usable forward station-to-station journey on each open transport ride. A journey may remain aboard through any number of intermediate stations. Its estimated time is:

`walk to boarding entrance + queue/boarding delay + all onboard segment times + walk from destination exit`

The retained direction is selected with an explicit engaged/empty branch. `std::optional::value_or()` is not suitable here:
its fallback expression is evaluated eagerly, so passing a second `ChooseDirection()` call to `value_or()` performed and then
discarded a duplicate shared-field lookup or bounded heuristic search whenever the first direction already existed. The
explicit branch preserves the first search's direction and path-history mutation exactly once.

The existing bounded footpath search still chooses each walking direction. The transport scan only supplies an intermediate station goal; it does not replace the mature path walker or run a park-wide A* search.

Once a transport leg is committed, `setTransportRoute()` clears the former final-destination path goal. The first walk toward
the boarding station resolves its entrance to the outer queue end and stores that concrete tile as the new path goal.
Subsequent junctions reuse the stored boarding goal while the committed route remains active and its path-connectivity epoch
still matches. A path, queue, banner, or entrance edit advances that epoch and takes the existing invalidation/replanning path;
the optimization therefore removes repeated queue-chain walks without retaining a second unsynchronised station target.

All terms are converted to milliseconds before comparison. A current shared field supplies exact path-tile distance for the
direct final walk, the walk to the resolved boarding queue end, and the walk from a station exit to the final target. The
conversion uses four metres per path tile and a three-mph baseline adjusted by the guest's energy and slow-walk state. If the
field is inexact or unavailable, the established geometric path heuristic remains the fallback; an exact unreachable access
or egress leg rejects that transport candidate. Segment time uses the measured station-to-station duration when available,
falling back to displayed distance and speed. Queue time is converted from minutes and boarding adds eight seconds.

Walking speed is derived once per synchronous transport-planning pass from the guest's energy and slow-walk flag, then reused
for the direct walk, candidate radius, boarding walk, and exit walk. Exact and fallback distances use the same integer
conversion and division by that speed, so per-leg rounding remains deterministic even where the more accurate path length
changes route ordering.

The configured fare bucket controls how strong the time advantage must be:

- Free service can win a reasonable tie: up to 15 seconds or 10% slower than walking in dry weather.
- Discount service must save at least five seconds and 10% of walking time.
- Fair service must save at least ten seconds and 20% of walking time.
- Extortive service is never considered while the walking search succeeds.

During precipitation, any positive saving is enough for discount and fair service, while free service receives a wider tie tolerance. Rain discounts only the onboard part of candidate ranking: 15% for all vehicle time plus 20% of the measured sheltered time on the selected directed journey. A fully sheltered journey therefore receives the established 35% maximum preference, while an exposed leg of an otherwise covered ride receives no shelter credit. Unmeasured fallback legs conservatively receive no shelter credit. Each guest remembers whether the last mode comparison occurred during precipitation, so a weather transition causes one immediate re-evaluation even when the concrete destination did not change.

The scan is performed only when the guest's concrete destination, precipitation state, path-connectivity epoch, or material transport-crowding generation changes, or when its cached path goal is invalid. The service cache derives that generation once per simulation tick from the actual full-queue-and-full-platform predicate, so ordinary occupancy churn does not start a park-wide guest scan. An uncommitted walker reconsiders when a service becomes available. A committed route checks its own selected boarding station directly and is discarded only if that station is now overcrowded; unrelated station churn cannot make every active transport user change plans. An active route also stores the ride, boarding station, selected destination station, and connectivity epoch that justified it. Path, banner, queue, or entrance edits therefore invalidate a commitment before boarding; derived wide-path maintenance does not, because it cannot create or remove a walking route. An Extortive fare cannot rely on stale “no walking route” evidence. The ride subsystem validates its transient service cache at most once per simulation tick, computes quality once per changed ride, and prebuilds the bounded all-pairs journey table over at most the ride station limit. Boarding/destination lookup is a binary search over that tiny ordered table, allowing unreachable pairs to be omitted without corrupting later indices. Guest-specific walking, queue delay, crowding, vouchers, cash, and fare policy remain live overlays. This keeps normal per-step pathfinding unchanged and makes the extra work proportional to the small cached directed station graph, not repeated segment aggregation or park path tiles. The mature bounded footpath search remains the single walking-direction implementation on either side of the transport journey.

The quality pass tightens those same ownership boundaries. A multi-target lookup reads its topology epoch once, a rebuilt path
reads slope metadata once, and shop/facility fields avoid an unnecessary node-index search. Station-exit distance is resolved once
per ride/destination instead of once per boarding/destination pair, and multi-entrance ride selection uses one station traversal.
Transport journey composition uses fixed scratch arrays sized to the engine station limit, so no heap allocation is needed while
solving the bounded directed graph. These are cache and traversal changes only; route ordering and deterministic tie behavior are
unchanged.

Completed physical-leg samples change time, sheltered exposure, quality, and fare inputs, so they dirty the owning service and force validation
before the next cache view. A dirty measured service remains indexed while its bounded journey table is refreshed. Station
endpoint references are removed and reinserted only when station count, entrance, exit, direction, ride identity, or
availability actually changes; ordinary rolling-sample publication does not churn the spatial buckets or sorted available
ride list. Multiple publications before the next query coalesce into the same forced validation. This cache state is
transient and deterministic. The service and crowding caches introduce no park-format fields.

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

Ride-specific advertising and free-ride voucher campaigns treat transports as route services rather than attractions. Transport rides are omitted from the campaign picker and rejected by the campaign action; a legacy or imported campaign that still names one gives a generated guest neither an attraction target nor a free-ride voucher. Ordinary advertised attractions retain their specified destination and can still use a different transport service to reach it.

A completed transport journey does not increment ordinary ride count or ride history and does not alter favourite-ride selection, ride satisfaction, or nausea. Happiness changes only for the explicit extortive-fare penalty. The existing station code still owns queues, ticket payment, revenue, and customer throughput.

### Real station platform pre-queue

Miniature Railway, Monorail, Suspended Monorail, and Chairlift stations use a real second waiting stage. Immediately before a stopped train or chair departs, the station captures its actual linked cars, masked seat counts, orientation, reversed-car flags, and object-defined loading positions. When that vehicle physically clears the station, the captured template becomes available. The front guests then leave the external queue, walk through the entrance, and wait visibly beside their assigned future car/seat position. Removing them from the linked path queue immediately frees outside queue space. Both shipped Chairlift objects use two scalar loading positions (`4, 0`), no loading waypoints, and the established cardinal boarding path with a 32-coordinate entrance offset. The adapter reuses those semantics, so staged positions match existing boarding geometry rather than applying the rail offset.

The platform capacity is exactly one actual consist, `sum(car.num_seats & 0x7f)`, rather than a station-tile approximation. Before a stopped pose has been captured, supported services derive nominal capacity from their first valid linked train or chair; once captured, the stopped template's slot count is authoritative. Invalid or absent vehicle entities safely report zero instead of consulting a guessed station size. A transient station registry maps those slots to guest entity ids in queue order. The guest's existing ride, station, car, seat, destination, and new platform substate fields remain authoritative; the registry is only a deterministic index and is never saved. Unsupported station styles without platforms and Lift retain the established just-in-time boarding path and its conservative legacy tile estimate. The shipped 16-seat Lift cabin uses per-seat three-point loading waypoints, while shuttle mode treats the complete tower ascent as departing and reaches `FinishDeparting()` only at the tower top. It therefore lacks both the scalar slot geometry and generic physical-clear signal required by this adapter. Other loading-waypoint vehicle objects also retain just-in-time boarding, but their valid consist remains the nominal routing capacity. A supported service with incomplete entrance/exit or stopped-pose geometry does not activate staging, although its real consist remains the nominal capacity.

For roller coasters, a station captures that platform template only when its entrance and exit open onto the two opposite lateral
platform edges. Endpoint directions are normalized against that station's origin track direction; same-side openings and hacked
longitudinal-end pairs retain ordinary just-in-time boarding even when their compass directions are opposites. The decision is per
station, so one station on a multi-station coaster may stage while another does not. Save-load reconstruction applies the same
gate and recovers a formerly staged guest through the station exit when the saved coaster layout is ineligible. Supported
transport stations do not use this gate: their established same-side and opposite-side layouts continue to stage identically.

The entrance transition follows the guest's actual platform assignment, not the ride category alone. A same-side coaster guest
therefore remains on the ordinary train/car/seat reservation path; it cannot enter platform recovery and abandon a live vehicle
reservation merely because another station or the ride type supports staging.

Affordability and current price eligibility are checked before a guest leaves the external queue. The admission or distance fare
is then paid when the guest crosses the entrance into the station, before walking to a platform assignment; boarding only consumes
the already-paid admission. A paired-seat or configuration rollback may return that guest to the front of the same queue, so the
paid entrance substate follows that attempt and prevents a second charge when the guest crosses again. Closing the ride after a
guest has entered the paid station area does not retroactively refund the ticket.

The station assignment is immutable during normal operation. Departure capture creates an ordered set of exact
`(carIndex, seatIndex, waitPosition)` slots from the stopped consist. Pair-loaded cars contribute one complete pair per pass
across all cars before any car contributes its next pair; scalar cars contribute one seat per pass. Guests claim those slots in
that stored order, which spreads a partial cohort along the full train while preserving visible pairs.

`RideStation::TrainAtStation` identifies the only train that may accept those assignments, but publication never rewrites them.
Finishing unload, arriving behind another train, and changing from platform approach to platform wait cannot alter a staged
guest's car, seat, or destination. Boarding transfers the exact captured seat to the vehicle and erases that one assignment only
after binding succeeds.

Transport unloading still compacts through-riders into each car's active prefix. If a continuing rider occupies a staged
guest's exact seat, that guest remains at the same platform marker for the next train; later guests whose own exact seats are
available may board. There is deliberately no arrival-time compaction, FIFO remap, second available-seat plan, or parallel
consumption ledger.

An arriving follower may be physically stationary behind the loading train while still reporting `movingToEndOfStation`.
Vehicle spacing has stopped it before the station-end transition. The front train treats this as an arrival before applying
empty, minimum-time, or minimum-load waits. Once ready, it rejects new platform bindings while already bound walkers retain their
vehicle reservations until seated. For ordinary circuits the arrival completes the dwell signal; block-section clearance,
adjacent-station synchronization, and restraint safety remain authoritative.

Pair-loaded cars use only the active `next_free_seat` prefix when resolving the adjacent passenger. Inactive array entries may
retain ids from previous riders and are not current partners. A lone bound guest can therefore enter without consulting a stale
tail, and the matching guest can enter normally when the first partner is already seated. A lone tail reservation still unwinds
when departure closes boarding, allowing `num_peeps` and `next_free_seat` to converge.

The visible walk uses the established two-part station geometry. A staged guest first completes the inward entrance target at
the station-facing edge of the entrance tile, then turns along the car-aligned loading-position line. This keeps the route inside
the entrance opening and platform fence. Both approach and waiting substates attempt the same exact-seat binding without moving
the guest back to a different marker.

Ride vehicle configuration remains the sole owner of consist shape. Construction and scripted station or vehicle changes clear
transient platform state through the existing invalidation lifecycle. Save loading rebuilds each assignment by matching its saved
car and seat identity against freshly captured geometry, rather than assuming a car-major slot number. Invalid configuration
recovers through the exit-first path; normal arrival is not an invalidation boundary.

Successful exact-seat binding increments the vehicle reservation prefix before the guest walks into the car, so the established
`num_peeps == next_free_seat` dispatch invariant holds the train until every bound walker is seated. All departure settings feed
one priority decision: incoming train, initial dwell, empty/minimum waits, maximum wait, target load, then ordinary departure.
Closing boarding prevents new bindings without abandoning existing ones.

The platform queue is the FIFO collection of assignment objects itself; a guest id and slot index cannot exist in a separate
occupancy map. Cancelling removes that assignment, while consuming it atomically transfers ownership to the vehicle passenger
array. Guest pickup and deletion use the same idempotent release method, and scripted station edits clear the transient platform
geometry before invalidating the ride's transport service.

The fixed vehicle passenger array is not itself an occupancy bitmap. Ordinary alighting decrements `num_peeps` from the end
without clearing the vacated entry, and transport alighting leaves the compacted through-rider prefix ahead of an inactive tail.
At platform preparation and boarding, the clamped maximum of `num_peeps` and `next_free_seat` is therefore the authoritative
occupied-or-reserved prefix; every later physical seat is reusable even when it contains a persisted old guest id. Availability
and duplicate checks share that boundary. This preserves the legacy unload representation while preventing an empty post-unload
train from appearing full or rejecting a returning guest as a duplicate of its inactive prior-trip entry.

Unload completion applies the same invariant per car. Setting `next_free_seat` to zero starts ordinary alighting; it does not mean
that the car is empty. A train remains in `unloadingPassengers` until each car's `num_peeps` has fallen to its continuing or
reserved prefix. Only then may it move to the station end and publish itself for boarding. This prevents platform assignments from
being calculated against seats whose previous occupants are still walking out.

The route-planning overcrowding rule remains transport-only: a new route avoids the station only when both the external queue reports full and the real train-load platform is full. Coasters are not opted into transport routing or fare semantics. Eligible opposite-side roller-coaster stations do use the same physical pre-queue when their station object has a visible platform and their stopped cars expose scalar loading positions. Capacity is therefore exactly one captured train, and same-side stations or unsupported waypoint-loading cars retain just-in-time boarding. Station status displays external queue length separately from `platform occupancy / consist capacity`, and guest status distinguishes walking to and waiting on the platform.

Transport measurements use the same authoritative physical-leg boundary as multi-station ride ratings. Once a measured
origin-to-destination leg exists, its destination replaces the numeric station-order fallback and its own measured distance,
duration, speed, distance-weighted sheltered exposure, and distance-weighted comfort/decoration sample price that segment. Directed edges are keyed by both endpoints,
so a middle shuttle station may retain independently measured departures in both directions. Journey construction runs a small,
deterministic shortest-time search over those edges and adds each selected leg's time, sheltered time, distance, and fare exactly. Fare breaks an
equal-time tie, then greater sheltered time breaks an equal-time/equal-fare tie; stable endpoint order resolves a complete tie. Cycles and unreachable destinations therefore cannot manufacture a partial journey.
Before a physical leg has been observed, the established numeric successor and ride-wide quality estimate remain the safe
fallback only for a station with no measured outbound edge. The service cache builds the same directed journeys, including routes
containing two or more segments and stations with more than one physical departure, rather than assuming that station array order
is track order. A destination-free request at an ambiguous shuttle departure returns no single segment instead of guessing.

This also does not opt coaster stations into transport admission, routing, pricing, or overcrowding decisions. Multi-station
coasters share directed leg-rating ownership and the physical one-train platform pre-queue only. The adapter uses the actual
stopped cars, station entrance side, descriptor platform height, and station object's `noPlatforms` flag, so it does not infer
capacity or standing geometry from station-tile count. Cars with loading waypoints remain outside the adapter.

## Save compatibility and tests

Private park version `60012` stores the distance-weighted transport quality accumulators. Version `60013` adds the selected transport destination station and permits the appended Free price-policy value. Version `60014` recognises the platform approach/wait substates. Version `60015` stores rolling samples and final ratings keyed by the directed station pair. Version `60016` stores distance-weighted sheltered exposure on those samples; `60015` loads begin conservatively at zero exposure until new ticks are sampled. Pre-`60015` loads discard in-progress rating samples because those records have no authoritative departure station; completed legacy ride-wide ratings remain available. Older-target exports write coherent empty sample buffers while preserving the live current-version state. Current saves rebuild the transient platform registry once from guest state after entity import. Occupied slots recover their saved visible destinations; unknown empty slot coordinates remain closed to admission until the next stopped template is captured, so new guests cannot bypass the recovered FIFO cohort. Old saves naturally contain no platform cohort. Exports targeting an older version serialize a platform guest as a normal station-exit approach with an exit-side destination, without mutating the live guest or exposing an unknown substate.

Focused coverage verifies that:

- a reasonably priced monorail is selected over a long walk and unaffordable journey fares are rejected;
- route comparisons use measured/fallback segment seconds and guest-adjusted walking seconds rather than distance-like score units;
- free transports are not boarded without a planned leg, including by a guest leaving the park;
- transport rides cannot be selected as ordinary attraction targets by ride advertising or free-ride voucher campaigns;
- rain admits a useful marginal route that dry-weather thresholds reject and prefers the sheltered selected leg between otherwise equivalent services;
- Free tie tolerance and dry middle-tier ordering where Discount accepts an identical marginal service that Fair rejects;
- Extortive routing is impossible with either a walking route or a usable non-extortive service;
- named exact-field cases cover a park-exit walk from transport, an ordinary ride target, first aid, and preservation of an advertised target;
- transport completion does not count as an attraction visit;
- G-forces reduce distance-weighted comfort and scenery raises decoration value;
- the transport measurements panel exposes comfort, decoration, average speed, and per-segment time/distance/value; and
- two-segment and longer journeys accumulate time, sheltered time, distance, and fare, prefer more shelter only after equal time/fare, and alight at the stored station;
- station overcrowding requires both a full external queue and full actual platform/consist capacity, while zero capacity is
  never overcrowded and unsupported platform adapters retain their conservative capacity fallback; route-level coverage also
  excludes an overcrowded service, retains single-component-full services, refreshes a stale selected route, and ignores unrelated churn;
- the real rail/Chairlift platform cohort has exact stopped-consist capacity, frees external queue space, waits visibly, and binds
  each fixed reservation to its exact car/seat after through-rider compaction, while an occupied exact seat waits for the next
  train and Lift retains safe JIT boarding; a whole-simulation arrival test proves that staged assignments remain unchanged through
  unload, publication, exact-seat binding, and physical boarding;
- current-version platform state round-trips, while an older target receives a coherent station-exit recovery state;
- route-planning overcrowding remains transport-only while supported coaster staging is capped to one exact train; and
- segment distance remains the leading fare input.
