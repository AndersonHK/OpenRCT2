# Map topology generations

`MapTopology` is the invalidation foundation for caches that derive stable routing data from the map. It deliberately does
not build a path field or change guest pathfinding.

## Reader contract

- `GetEpoch()` returns the global topology epoch.
- `GetChunkGeneration(tile)` returns the generation for the tile's 16-by-16 routing chunk without allocating or locking.
- A derived cache records the generations of every chunk it reads. It remains valid only while those generations match.
- Derived caches must store values or stable identifiers, not tile-element pointers; storage reorganisation intentionally does
  not change topology generations.
- Invalid coordinates return the global epoch, giving callers a deterministic sentinel without allocating a special entry.
- Atomic generations make validation lock-free; they do not by themselves make concurrent access to mutable tile elements
  safe. Background builders still need an immutable snapshot or the engine's map-access synchronisation.

The fixed technical-map table contains 63 by 63 atomic generations, approximately 32 KiB. Normal edits advance the global
epoch once and store that generation in the edited chunk. If an edited tile is on a chunk boundary, the cardinally adjacent
chunk is advanced too because a path edge can cross that boundary. Diagonal chunks are not advanced because routing edges are
cardinal.

`Reset()` advances all chunks together. Map replacement, load/import through `SetTileElements`, resize, shift, clear, and
stash swaps use this broad reset.

## Writer coverage

The following mutation bottlenecks invalidate routing topology:

- ordinary footpath and queue placement, replacement, edge connection, edge removal, and queue-chain relinking;
- changes to the derived wide-path flag, including its incremental map update and scripting edits;
- track-design path layout placement, whose supplied edges bypass ordinary edge connection;
- ride and park entrance placement/removal, including scenario entrance/exit repair;
- blocking-banner placement, removal, no-entry changes, and rotation/edge edits;
- tile-inspector removal, paste, rotation, height, path-edge/slope, entrance, and banner operations;
- scripting raw-tile replacement/removal and routing-relevant element properties;
- bulk map replacement, clear, resize, shift, and stash swaps, plus chunk-local queue/entrance changes during ride removal.

Ghost elements are excluded. They are construction previews and are not part of the guest routing graph.

## Deliberate separation from congestion

These generations represent stable topology only. Peep density, vehicle-blocked paths, weather, shelter preference, live
queue lengths and waits, transport timing, prices, and other time-varying traversal costs must use a separate congestion/cost
signal. Mixing those values into topology generations would invalidate shared fields every tick and defeat the cache.

## Low-level mutation rule

`TileElementRemove` and most individual tile-element setters do not know their map coordinate, so they cannot safely
invalidate a chunk themselves. New callers that bypass the covered actions, footpath routines, tile inspector, scripting
bindings, or bulk-map setters must explicitly call `MapTopology::InvalidateTileAndNeighbours`. Importers are covered when
their finished element array is installed by `SetTileElements`; mutating an already-installed imported map requires a local
invalidation.
