# Path topology cache

`MapPathTopology` is a compact, lazy adjacency snapshot used by the stable-topology hot reads in `GuestPathfinding` and
intended as input for future shared path fields.

## Stored topology

Each warmed 16-by-16 chunk stores path nodes in deterministic tile and tile-element order. A path node contains:

- local tile X/Y and base Z;
- raw edges and non-ghost-banner-permitted edges, preserving directed no-entry restrictions;
- slope state and direction;
- the derived wide flag;
- queue, queue-banner direction, ride ownership, and station ownership;
- one compact path-to-path target per raw edge, including the target base Z and whether that target is wide or an owned
  queue;
- the derived thin-junction classification used by the legacy heuristic search.

Entrance elements are stored separately with type, sequence, direction, ride/station identity, their rotated absolute
connection mask, and adjacent path targets. This preserves ride, exit, and park-entrance connectivity without treating every
entrance as an ordinary walkable path.

`PathNode` and `EntranceNode` are each statically limited to 24 bytes. Chunk vectors allocate only while a stale chunk is
rebuilt. A warm `GetChunk` checks five atomic generations and returns spans without allocating.

## Invalidation and lifetime

A cache entry records its own topology generation plus the west, north, east, and south chunk generations. The neighbour
dependencies are intentional: an edge node on a chunk boundary resolves against the adjacent chunk. A change anywhere in a
dependency chunk conservatively rebuilds the cached chunk on its next read.

`MapTopology::Reset` releases all warmed path-cache vectors. Local edits retain allocations and rebuild only stale dependent
chunks. Returned spans are main-thread, short-lived views; they become invalid when their chunk rebuilds or the map resets.
They do not make concurrent reads of mutable tile elements safe.

## Exactness boundary

`ChunkView::isExact` describes the represented stable-topology scope. It becomes false for duplicate non-ghost paths at the
same tile/Z or invalid entrance type/sequence data. Those cases require a live fallback.

The cache intentionally does not represent:

- guest density or other congestion;
- vehicle-blocked path state;
- weather, shelter preference, transport time, price, or availability;
- shop/facility track terminals, whose classification depends on live ride descriptors;
- surface wandering, walls, or off-path terrain traversal;
- construction-preview elements, including the existing pathfinder quirk where a ghost no-entry banner can temporarily
  affect live permitted edges.

These exclusions avoid either rebuilding stable topology every tick or silently approximating existing behavior. Local
callers must retain the live path for unsupported terminal classification and any chunk where `isExact` is false. A global
route field has a stronger boundary: one inexact required chunk makes the entire publication fallback-only, because omitting
that chunk could distort routes chosen from otherwise exact chunks.

## GuestPathfinding ownership

The cache owns three stable facts used by the hot path: permitted path edges, adjacent path classification as ordinary, wide,
or owned queue, and whether more than two raw source edges lead to ordinary thin paths. The last fact exactly matches the
legacy thin-junction rule: missing paths, wide paths, and queues with a non-null ride owner do not count; an unowned queue
still counts as an ordinary path.

`ChooseDirection` retains the last exact chunk view for the duration of one synchronous search. Recursive steps within that
16-by-16 chunk therefore avoid repeating the five generation reads performed by `GetChunk`. The view never escapes the
search and pathfinding does not mutate topology, so its lifetime remains inside the existing main-thread guarantee.

`GuestPathfinding` still owns all decisions and uses its original live-map routines whenever a chunk is inexact, a cached node
cannot be matched to the live path's height, edges, and slope, a requested direction is not a source edge, or a ghost
path/banner affects the live banner chain. Thin-junction fallback has its own profiler scope so an unexpected rise in
unsupported layouts remains visible. Staff banner bypass remains a direct raw-edge read.

Ride and park policy, recursive fallback search, random choices, congestion, and transport pricing remain in
`GuestPathfinding` and related ride logic. `MapPathRouteCache` may accept a concrete station-less shop/facility target only
after `GuestPathfinding` validates the live ride descriptor and owning track element on the main thread. It derives that
terminal's exact entry edge and height from frozen adjacent path slope facts; `MapPathTopology` itself still does not classify
track elements as guest destinations.

## Benchmark expectation

The first warmed EverythingPark profile before cached thin-junction classification measured 500 ticks with
`ChooseDirection` at 1,125,511 microseconds across 12,093 calls. `PathIsThinJunction` alone accounted for 416,955
microseconds across 3,008,010 calls. The cache change is expected to remove nearly all of those repeated neighbour scans in
ordinary layouts and reduce same-chunk generation checks. It must be judged by a repeated warmed 500-tick comparison using
the same park, build configuration, warm-up, checksum, and profiler setting; the expected signature is a near-zero
`PathIsThinJunctionLive` call count, lower `ChooseDirection` time, and an unchanged simulation checksum. No gain is claimed
until that comparison is recorded.

The recorded repeated run met those gates. Clean throughput rose from 165.895 to 184.352 TPS and median tick latency fell
from 5.919 to 5.054 milliseconds with the same final checksum. In the profiled 500-tick interval, `ChooseDirection` fell
from 1,125,511 to 540,621 microseconds, `PeepUpdateAll` fell from 1,920,543 to 1,271,760 microseconds, and
`PathIsThinJunctionLive` recorded zero calls. These figures describe this EverythingPark/build/machine comparison, not a
universal hardware guarantee.

Shared reverse fields built from these exact nodes are documented separately in
[Shared destination route fields](shared-route-fields.md).

The source follow-up extends those fields to validated station-less shop/facility terminals and publishes an allocation-free
ride-to-target index only for rides with one distinct, successfully seeded destination. This removes repeated station scans
and queue-end walks in the common one-target route without changing multi-station selection. It was measured only in a later
combined routing/vehicle checkpoint, so no isolated TPS gain is assigned to this slice.
