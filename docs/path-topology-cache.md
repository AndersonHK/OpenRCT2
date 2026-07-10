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
- one compact path-to-path target per raw edge, including the target base Z.

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

These exclusions avoid either rebuilding stable topology every tick or silently approximating existing behavior. Future
callers must retain the live path for unsupported terminal classification and any chunk where `isExact` is false.

## GuestPathfinding ownership

The cache now owns two stable facts: permitted path edges and adjacent path classification as ordinary, wide, or owned queue.
`GuestPathfinding` still owns all decisions and uses its original live-map routines whenever a chunk is inexact, a cached node
cannot be matched, a requested direction is not a source edge, or a ghost path/banner affects the live banner chain. Staff
banner bypass remains a direct raw-edge read.

Ride/park/shop terminal classification, recursive search policy, random choices, congestion, and transport pricing remain
entirely in `GuestPathfinding` and related ride logic.
