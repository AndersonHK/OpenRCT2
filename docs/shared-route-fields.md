# Shared destination route fields

## Scope

`MapPathRouteCache` shares the stable part of repeated walking searches to concrete park entrances, resolved ride entrances,
and validated station-less shop or facility track terminals. It does not replace guest routing policy. A field answers one
narrow question: from an exact cached path node, which permitted edge reduces the path-tile distance to this concrete target?

The preparation pass runs after provisional elements are removed and path-wide flags are updated, immediately before guest
updates. It collects every park entrance and every ride station with an entrance. Queue entrances are resolved to the same
outer queue tile used by `GuestPathfinding`. A ride with no entrance contributes its station-zero track coordinate only when
its descriptor identifies a shop or facility and a live track element at that coordinate belongs to the same ride. That
main-thread validation covers destinations such as first aid, toilets, ATMs, and shops without allowing arbitrary non-path
coordinates to seed fields.

## Frozen graph and worker boundary

The main thread freezes the current exact `MapPathTopology` views into compact path and entrance records. If any required map
chunk is inexact, preparation publishes an empty fallback-only state for that topology epoch. It never builds fields around a
hole, because an omitted chunk can change the shortest route from otherwise exact source and target chunks. The temporary
frozen graph contains:

- path location, directed banner-permitted edges and resolved path-to-path connections;
- path slope state and direction, used to match the exact walking height at a shop/facility terminal;
- queue ownership, so an owned queue is traversable only by the field for that ride;
- ride and park entrance connections used as terminal seeds;
- a deterministic reverse-adjacency index.

Targets are sorted by X, Y, Z and ride id and duplicates are removed. Entrance terminals use a frozen location index, while a
station-less shop/facility probes only the adjacent flat or rising path heights rather than scanning every path node. The
process-lifetime `JobPool` then builds one private reverse breadth-first field per target. Workers read only the frozen graph
and write only their assigned distance/direction buffers. They never read mutable tile elements, publish cache state, mutate
guests, or consume scenario randomness.

After the synchronous worker barrier, the main thread compares the current topology epoch with the frozen epoch. A mismatch
discards the pending result. Otherwise only the next-direction arrays and the shared path-location-to-node index are published
in sorted target order. BFS distances, entrances, reverse edges, node connections, and seed indexes remain private to
preparation and are released after the serial publication barrier. Equal-distance choices use ascending direction order,
making results independent of worker scheduling and caller target order.

Publication also builds a sorted, allocation-free lookup from ride id to concrete target only when that ride has exactly one
distinct prepared destination and its field seeded successfully. This avoids rescanning stations and rewalking the same queue
at every junction for the common one-target case. Rides with multiple distinct targets retain the existing live closest-station
selection and queue resolution.

## Live decision boundary

`ChooseDirection` remains the only place that commits a guest direction. Before consulting a field it still:

- matches the guest to live path elements;
- applies current no-entry banners and the staff/guest banner rule;
- applies the guest's remembered thin-junction history and resets history for a changed goal;
- identifies the target ride whose queue may be entered.

A field edge is accepted only if it remains in that live guest-specific edge mask. The normal `ChooseDirection` history
update then records the selected edge. Missing targets, unreachable nodes, any inexact required chunk, stale epochs, foreign
queues, unvalidated or missing shop/facility track terminals, multi-target ride selection, and rejected live edges all fall
back to the existing bounded heuristic search.

Transport price, service availability, station crowding, precipitation preference and pre-queue admission are intentionally
absent. Transport planning still decides whether to walk or board; shared fields only accelerate the walking leg to the
already selected concrete entrance.

## Invalidation and cost

Any `MapTopology` epoch change invalidates all published fields. This is conservative but makes lookup an epoch comparison
instead of checking every chunk touched by a route. The next gameplay preparation freezes the new exact graph and rebuilds
all stable targets. `MapPathTopology::Reset` also releases route-field storage.

Published storage is proportional to exact path nodes times concrete targets at one byte of next-direction data per pair, plus
one shared sorted location index. Each worker temporarily owns one 32-bit distance array and one node queue while building its
assigned field; the frozen graph and all reverse-edge and terminal-seed data are released after publication. This deliberately
exchanges bounded parallel cold-build work and compact RAM for fewer recursive searches during guest updates. If target count
or edit-time rebuild latency becomes excessive, the next refinement should retain only active/high-demand targets or
generation-partition fields; it must not weaken the all-exact and serial-publication boundaries.

## Verification contract

Focused tests cover entrance seeding, directed no-entry edges, epoch invalidation, target-order determinism, global fallback
when an intermediate chunk is inexact, foreign-queue exclusion, target-queue admission, rejection of a shared proposal masked
by guest junction history, explicit shop/facility terminal typing, exact sloped-terminal height, and the single-target index's
deduplication and multi-target rejection.
Performance validation remains the warmed EverythingPark benchmark with a checksum that repeats across identical new runs.
The checksum may differ from the pre-field build because shortest-field routing intentionally changes a guest's choice where
the legacy bounded heuristic preferred another path. Expected evidence is fewer `ChooseDirection` recursive searches and
lower total peep time; build time, field memory and fallback counts must be reported separately before claiming a net gain.

## Recorded EverythingPark result

After the shared fields and transport-platform integration, two clean 2,000-tick runs measured `241.767` and `241.871` TPS
with matching `2fc90d5f...` checksums and median tick times of `3.868` and `3.851` milliseconds. Against the preceding
`184.352`-TPS checkpoint, this is a repeatable `31.2%` throughput gain. A separate 500-tick profiled run reduced
`ChooseDirection` from `540,621` to `201,038` microseconds (`-62.8%`) and `PeepUpdateAll` from `1,271,760` to `880,203`
microseconds (`-30.8%`). The changed checksum relative to the pre-field build is expected from the more exact route choice;
the two identical new runs demonstrate determinism for this checkpoint.

The follow-up that added validated station-less shop/facility targets and the single-seeded-target index was measured together
with the live-rating eligibility gate. Two clean 2,000-tick runs reached `254.297` and `256.592` TPS with matching
`182e7448...` checksums and median tick times of `3.834` and `3.794` milliseconds. Because that checkpoint contains both
routing and vehicle changes, this document does not assign the entire gain to route lookup; the residual profiled
`ChooseDirection` cost fell from `201,038` to `81,798` microseconds over 500 ticks.

The station-less facility and single-target lookup follow-up has not been benchmarked independently of the live-rating
eligibility change in the same checkpoint. No isolated throughput gain is assigned to it.
