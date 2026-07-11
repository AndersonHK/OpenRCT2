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

After the synchronous worker barrier, the main thread compares the current path-connectivity epoch with the frozen epoch. A
mismatch discards the pending result. Next-direction arrays, exact BFS distances, and a shared flat
path-location-to-node index are published in sorted target order. Distances are retained for every target, including the
common single-target ride/facility case, because transport mode choice and ordinary target choice now consume the same exact
walking cost. The compact frozen entrance list and its sorted location index are also published so a transport station exit
can resolve its exact adjacent path node without a live map search. Reverse edges and terminal-seed working data remain
private to preparation and are released after the serial publication barrier.
Equal-distance choices use ascending direction order, making results independent of worker scheduling and caller target order.

Publication also builds a sorted, allocation-free lookup from ride id to concrete target only when that ride has exactly one
distinct prepared destination and its field seeded successfully. This avoids rescanning stations and rewalking the same queue
at every junction for the common one-target case. For multiple ordinary entrances, the destination selector compares the
retained exact distance from the guest's current path node and chooses the first station with the shortest reachable field.
Station order therefore remains the deterministic tie-break. If no exact source/target distance is available, geometric
selection remains the fallback. Rides synchronised with adjacent stations retain their established guest-ride-count rotation
and do not use distance ranking. Queue ends remain the concrete targets in both cases.

Multi-target selection resolves the guest's exact source node once for the whole ordered candidate span, then probes each
candidate field at that node. The previous caller loop repeated the same binary source-node lookup for every entrance. The batch
result is an index into caller order, so station order and park-entrance order remain the tie-break without adding target-owner
state to the cache. The lookup first validates the published path-connectivity epoch; stale topology, missing fields, absent
retained distances, and unreachable candidates return no selection and preserve the geometric fallback.

Ordinary ride/facility and first-aid selection uses a second allocation-free batch over ride ids. A flat ride-to-field index
finds each candidate's concrete prepared targets without rescanning all fields; the source node is still resolved once.
Candidate order, followed by sorted concrete-target order, is the deterministic tie-break. An exact empty result means the
prepared topology proves none of those destinations reachable and is not replaced with Manhattan selection. Manhattan
selection remains only when the epoch is stale, the source is not an exact path node, or the shared publication is unavailable.
Ride-specific advertising continues to assign its specified ride id directly, and synchronized multi-station rides retain
their established station-choice rule.

Multiple park entrances use the same reachable-distance comparison before the geometric fallback. A leaving guest still
commits to the chosen entrance through its existing pathfinding goal and chosen-entrance flag; it is not retargeted every tick.
Outside guests may reevaluate while approaching the park, matching the previous nearest-entrance behavior.

## Live decision boundary

`ChooseDirection` remains the only place that commits a guest direction. Before consulting a field it still:

- matches the guest to live path elements;
- applies current no-entry banners and the staff/guest banner rule;
- applies the guest's remembered thin-junction history and resets history for a changed goal;
- identifies the target ride whose queue may be entered.

A field edge is accepted only if it remains in that live guest-specific edge mask. The normal `ChooseDirection` history
update then records the selected edge. Missing targets, unreachable nodes, any inexact required chunk, stale epochs, foreign
queues, unvalidated or missing shop/facility track terminals, unavailable multi-target distances, and rejected live edges all
fall back to the existing bounded heuristic search.

Transport price, service availability, station crowding, precipitation preference and pre-queue admission remain outside
the cache. Transport planning still decides whether to walk or board, but its direct walk, walk to the resolved boarding
queue end, and station-exit-to-final-target walk use exact field distance when available. An exact unreachable access or
egress leg rejects that candidate. Inexact or missing publications retain the previous geometric time estimate.

## Invalidation and cost

Any path-connectivity epoch change invalidates all published fields. Structural path, banner, queue, and entrance edits use
that conservative global signal, which keeps lookup to one epoch comparison instead of checking every chunk touched by a
route. The rolling wide-path updater advances affected topology chunks, because live thin-junction classification consumes
wide flags, but does not advance the connectivity epoch. Reverse fields do not store or consult wide/thin flags, so rebuilding
every park destination for that derived maintenance change was both unnecessary and a source of repeated full-park cache
churn. The next gameplay preparation rebuilds all stable targets only after connectivity changes. `MapPathTopology::Reset`
also releases route-field storage.

Published storage is proportional to exact path nodes times concrete targets at five bytes per pair: one byte of
next-direction data and four bytes of path-tile distance, plus
one shared location index. Frozen graph construction retains its sorted index for deterministic edge building. Publication converts
that index once into a power-of-two open-address table at no more than 50% load, so every guest destination lookup avoids a binary
search over all path locations. Hash collisions probe linearly and require the complete packed XYZ key; lookup never iterates or
changes simulation order. The table is built only after epoch validation, replaced with the fields, rejected with a stale build,
and released by `Reset()`. Published entrance-source records add one compact record per ride/park entrance plus a sorted
location pair; they do not scale with guests. Each worker already owns one distance array and one node queue while building
its field, so retaining all distance arrays increases steady-state RAM but does not add a second build-time traversal. This
deliberately exchanges bounded RAM for exact destination and transport walking costs without adding per-guest searches. If
target count, memory, or edit-time rebuild latency becomes excessive,
the next refinement should retain only active/high-demand targets or generation-partition fields; it must not weaken the
all-exact and serial-publication boundaries.

At the recorded EverythingPark footprint of 3,611,346 direction entries, the matching 32-bit distance arrays add 14,445,384
bytes (about 13.8 MiB), plus vector allocation overhead and the much smaller entrance-source index. Benchmark reporting now
prints direction and distance entry counts separately so future target-count changes cannot hide this RAM tradeoff.

## Verification contract

Focused tests cover entrance seeding, directed no-entry edges, connectivity-epoch invalidation, wide-flag epoch separation,
target-order determinism, global fallback
when an intermediate chunk is inexact, foreign-queue exclusion, target-queue admission, rejection of a shared proposal masked
by guest junction history, explicit shop/facility terminal typing, exact sloped-terminal height, and the single-target index's
deduplication and multi-target rejection. Multi-target coverage verifies exact distance, unreachable-source rejection, ordered
batch selection, duplicate-distance tie stability, and single-target distance retention. Named cases cover a park-exit walk
from a transport exit, ordinary ride selection by shortest reachable field, first-aid rejection of a disconnected nearer
facility, and preservation of an advertised ride's specified target. Selection review also preserves synchronized-station
rotation and leaving-guest entrance commitment. A dense-line lookup test queries every exact
source through the published open-address index, then verifies that reset removes all entries and prevents stale reuse.
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

The all-target distance-retention and exact transport-walking slice has not been benchmarked. Its expected hot-path effect is
fewer geometric mis-rankings with constant-time field probes and no new path search; its explicit cost is four additional
bytes per exact path-node/target pair plus the compact published entrance-source index. No TPS claim is assigned until the
same warmed EverythingPark protocol reports throughput, cache entry counts, and process memory.
