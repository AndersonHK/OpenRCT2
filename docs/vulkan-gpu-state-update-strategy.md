# GPU graphical-state update strategy

Planning study, 2026-09-23. Requested independently during the ghosts/props/tracks checkpoint. **This document proposes future work; it changes no runtime behavior and reports no new benchmark.** Code observations include the checkpoint's in-progress working tree, based on `2af12bbc3c`. The accepted measured baseline remains [native paths](vulkan-native-path-checkpoint.md).

## Recommendation

Keep complete, compact graphical records as the default update unit for stationary track elements and props. Batch their changed records into a small number of uploads. Separate the update streams for terrain, paths, objects, shared ride appearance and dynamic poses. For moving entities, prefer a dense array of small pose records, independent of cold appearance, when most poses change. Preserve a sparse mode for genuinely sparse changes. Choose between these using measured packing and submission cost, not byte count alone.

Do not resend an entire ride's track when one brake opens. Do not send separately selected rail, support and decoration sprites: those remain GPU products of one element's state and shared catalog. The user is right that a whole compact object can be cheaper than several tiny field patches; that does not imply a whole ride, legacy simulation object or map chunk is the right upload unit.

The first optimization should separate graphical notifications from broad map invalidation, then separate family revisions and GPU residency. Bit-level transport compression comes later if measurements justify it. There is no proposal to restore screen damage, cached painted chunks or lazy repaint. Every admitted frame still renders the entire visible scene from one immutable generation.

## What the code currently does

| Boundary | Observed behavior | Implication |
|---|---|---|
| `MapInvalidateTileFull` in `world/Map.cpp` | Calls `MapInvalidateTile` with Z range 0–2080. | This means one map tile, not every element of an entire ride. |
| `MapInvalidateTile` | Invalidates nearby ride-rating context, then invokes graphical invalidation. | A purely graphical flag change can create unrelated simulation-cache work. Check semantics before removing this side effect. |
| `MapInvalidateElement` | Supplies narrower Z bounds to the same map invalidation path. | Changing only this function name does **not** narrow native publication granularity or avoid the rating invalidation. |
| `MarkMapTilePresentationDirty` | Sets a tile bit and adds it to a worklist only once until publication. | Repeated notifications do not cause one snapshot/upload per call. Rating-cache invalidation occurs before this deduplication. |
| `ViewportsInvalidate` | Returns early when the drawing engine can skip viewport invalidation. | Full-height projection is already avoided in the native full-redraw path; optimizing rectangle height misses the principal remaining cost. |
| `ConsumeMapPresentationChanges` | Sorts dirty tile IDs, walks each tile's elements, captures surface, paths and objects. | A changed brake may recapture unchanged terrain, paths and scenery on that tile. No whole-map capture in the steady state. |
| `MapPresentationSnapshot::Apply` | Surface chunks are cloned/revised for dirty tiles. Path/object lists are compared first; actual changes rebuild a 256-tile family chunk. | Equality avoids some allocations, but variable-length list reconstruction can copy unrelated elements within a changed chunk. |
| `CommandDrawingContext::DrawWorldSurfaceScene` | Repacks a combined GPU chunk when any family revision changes; byte comparison preserves the previous GPU chunk if all resulting graphical bytes match. | A no-op invalidation can still cost CPU capture/repacking without causing a GPU transfer. |
| `WorldSurfacePipeline` | One upload-ring allocation for changed world chunks; at most one buffer-copy command for each of source records, paths and objects, each with multiple ranges. Any changed combined chunk resends all three resident families in it. | Many dirty tiles already do **not** imply thousands of GPU API calls. The opportunity is less CPU preparation and less unrelated data, not replacing an existing per-object submit loop. |

At review time GPU source records are 56 bytes per surface/tile directory entry, 48 bytes per path and 64 bytes per prop/track element. A source chunk alone is `256 × 56 = 14,336` bytes, before path/object payload. These are the in-progress object checkpoint sizes; accepted build98 used 44-byte surface records.

Concrete track notifications in `ride/Ride.cpp` include `setBrakeClosed` followed by `MapInvalidateTileFull`, cable-lift block changes, and chairlift/spiral-slide animation invalidations. `ride/Track.cpp` uses `MapInvalidateElement` when station track type changes. Construction/removal may legitimately affect terrain cuts, adjacency, supports, topology and ratings; brake art changes need a more specific audit. `RideRating::InvalidateLocalContextCacheAround` advances nearby cache generations rather than immediately recomputing ratings, so both notification cost and resulting later cache misses must be measured.

Integration note: the checkpoint owner is correcting newly added graphical-only track hooks to use the existing `MarkMapTilePresentationDirty` directly. That avoids newly introduced ride-rating side effects; it does not implement this document's future granularity/transport plan and still dirties the same tile/chunk. Existing construction/topology invalidations retain their semantic responsibilities.

Ride colours/styles are already captured into a shared `WorldRidePresentationMaterials` table. The current producer builds and compares bounded ride facts at publication; later object-owned ride revisions can remove that polling. Recolouring a ride should update its shared style row, rather than dirty every track tile. A style change which genuinely alters geometry may require dependent GPU metadata/catalog refresh, but still need not copy every track's unchanged coordinates.

Relevant source: [map publication](../src/openrct2/world/Map.cpp), [snapshot types](../src/openrct2/world/MapPresentationSnapshot.h), [graphical object facts](../src/openrct2/world/WorldObjectPresentation.h), [GPU conversion](../src/openrct2-renderer/gpu/GpuCommandDrawingContext.cpp), [GPU transfer/arena](../src/openrct2-renderer/vulkan/VulkanWorldSurfacePipeline.cpp), [viewport bypass](../src/openrct2/interface/Viewport.cpp), [rating generations](../src/openrct2/ride/RideRatings.cpp).

## Define the object before choosing the transfer size

1. **An entire ride's track:** many elements spread across many tiles. Suitable for bootstrap, ride deletion or an explicitly global topology replacement, not a local brake/door/photo state change. Global colours belong to one ride appearance record.
2. **A logical construction piece:** sometimes several tile elements/sequences, such as a multi-tile turn. A construction transaction publishes all affected sequences atomically. It need not retransmit unaffected pieces of the ride.
3. **One `TrackElement`:** a natural stationary instance update unit. Its complete compact graphical record is currently 64 bytes. Frequently changing brakes/doors/lights can later have a separate packed hot-state row if they dominate changes.
4. **One expanded GPU component:** rail image, support, foreground trim, etc. These are derived output. Sending them individually reinstates CPU painting and duplicates GPU rules; reject that architecture.

The same distinction applies to multi-tile scenery: atomic construction/removal is a publication property, not a demand to turn every tile into a separate GPU call. Ghosts are a small transient state stream and should eventually avoid churn in the permanent map arena when the cursor moves.

## Cost model and alternatives

Compare the complete path, not PCIe bytes in isolation:

`cost ≈ notification + capture + COW + packing + allocation + Ccall × calls + Crange × ranges + bytes / effectiveBandwidth + synchronization + GPU update work`

The terms overlap in a pipelined renderer; do not simply sum them to predict a frame time. Measure owner-thread time, worker time and GPU critical-path time separately. Effective bandwidth here includes host writes and device transfers on the target machine. More copy ranges can incur work even inside one API call. A scalar patch also needs a destination address/ID, generation or lifecycle protection and alignment; “one bool changed” does not imply one transmitted byte.

| Transport choice | Benefits | Costs / recommended use |
|---|---|---|
| Current combined 256-tile chunks | Simple immutable ownership, few calls, contiguous copies | Recapture/repack/resend amplification; keep as correctness/control baseline. |
| Independent family chunks | Removes cross-family amplification with little new identity machinery | Still copies unchanged members of the changed family; recommended first structural step. |
| Whole compact changed elements, merged ranges | Simple schema; cheap packing; bounded calls | Needs stable slots or tile-range replacement on insertion/removal; recommended stationary-object default. |
| Field patches into resident records | Lowest payload for very sparse changes | Metadata, gather, merging, variable schema and stale-handle complexity; use only for a measured hot field group. |
| Packed indexed updates plus GPU scatter | One contiguous host packet and one batched dispatch even for scattered slots | Extra GPU read/write/barrier and dispatch; compare against multi-range copies, not thousands of API calls. |
| Dense hot-state slab | Predictable serial copy, no change search or per-record patch headers | Sends unchanged hot slots; recommended candidate when most moving entities change. |
| Whole-ride or whole-legacy-object copy | Superficially simple | Copies irrelevant simulation and unchanged topology; reject as steady-state policy. |

For `K` changed compact records of size `S`, sparse indexed transport is roughly `K × (S + H)` bytes with per-update header `H`; dense transport is `N × S`. Ignoring CPU/GPU overhead, sparse wins only below `K/N < S/(S+H)`. This is a starting calculation, not a dispatch policy. Contiguous destination ranges can avoid per-record wire headers; highly scattered ranges can instead make GPU scatter worthwhile.

Example using **existing peep types**, not invented measured performance: a motion row is 40 bytes and its sparse update is 48 bytes including ID/generation, so the payload-only crossover is 83.3% changed. Actual crossover can be substantially lower due to gathering and range processing. CPU dirty masks remain useful for deciding what to publish even when a dense GPU packet is selected.

A future coalescer can merge adjacent destination ranges across small unchanged gaps when `extraBytes/effectiveBandwidth` is cheaper than the saved range-preparation/processing cost. Keep this deterministic and bounded, using calibrated constants and counters; do not add per-frame timing-driven policy changes before simple fixed choices are measured. Initial arena growth and rare repacks need separate p99/max accounting because a good average can hide a construction stall.

## Hot poses and cold appearance

The user's suggested array of structs is a sound default **within the pose group**. Keep positions/orientation and other jointly consumed motion fields contiguous per entity; keep appearance/catalog identities and lifecycle separate. This is a hybrid: separate arrays by update frequency, small structs within each array.

There is already machinery worth reusing: [RetainedPeepState.h](../src/openrct2/drawing/RetainedPeepState.h) separates lifecycle (8 bytes), motion (40), appearance (16) and animation (36), with 64-slot immutable field chunks and per-slot revisions. It has prepare/commit deltas, tombstones and generation checks. [EntityRegistry](../src/openrct2/entity/EntityRegistry.cpp) deduplicates visual change IDs and classifies `EntityVisualDirty` groups. The ordinary native terrain/object profile does not yet render these peeps. Reuse or simplify this ownership/lifecycle contract when bringing them back; do not create a competing journal.

The current motion type retains previous XYZ/tick for interpolation. Whether those fields are needed in the final native mode is a behavioral decision: do not remove them purely to advertise a smaller record. For an authoritative-pose-only mode, a 16–32-byte hot row may suffice, but that is a design estimate, not an implemented ABI. Animation state can have its own array if its update cadence differs. Purely visual regular cycles can derive phase from a pinned tick plus instance phase/speed; simulation-dependent animation transitions must remain authoritative. Never use wall-clock progression to guess simulation animation through pause, speed changes or skipped snapshots.

For scale only: 20,000 rows × 32 bytes × 144 publications/s is 92.16 MB/s; 60,000 is 276.48 MB/s. A 40-byte motion row at 60,000/144 is 345.6 MB/s. These are arithmetic payload bounds, **not measured bandwidth or a claim of free uploads**. CPU reads/copies, old-generation ownership, lifecycle arrays and animation add cost. Publishing every 360 simulation ticks instead of admitted ~144 frames would multiply this transport needlessly. Simulation methods update their compact owned graphical state as they run; frame publication transfers the latest completed values once, coalescing intermediate ticks.

AoS helps when the CPU producer and shader consume XYZ/orientation together. Pure SoA can help a compute visibility pass that reads only position, but adds streams and may hurt whole-pose writes. An AoSoA page can improve grouping/locality while keeping fixed-size snapshots, at the cost of indexing complexity. Benchmark these only after the hot/cold boundary: separating 32–40 hot bytes from a large simulation record is likely more valuable than choosing between X/Y/Z arrays. No fresh whole-population gather over legacy entity memory should be introduced just to create a “dense” upload; maintain the compact pose array from owner updates.

## Object-owned mutation and immutable publication

Proposed API shape, not implemented code:

```cpp
track.SetBrakeClosed(value);       // no-op if unchanged; updates its graphical module
track.SetTrackType(type);          // topology/appearance effects owned here
ride.Appearance().SetColours(c);  // shared ride row revision, not a map walk
peep.Pose().SetPosition(position); // hot graphical row; simulation index policy stays owned
```

Objects compose a reference to the world/ride/entity presentation module which owns stable graphical slots, dirty masks and publication. Shared object materials remain immutable catalog modules referenced by stable identity. Setters classify their own effects, rather than relying on every external caller to remember render and simulation side effects. Transaction methods for construction/import can defer publication until the complete mutation is ready; no observer sees half a logical piece.

Legacy packed `TileElement` storage and save/network formats must not gain raw runtime pointers or references. Use an object-owned runtime facade/module beside serialized storage, with checked handles. Tile ordinal is not a stable long-lived identity after insertion, removal or Tile Inspector reorder. Until stable per-element handles exist, use whole tile-family replacement for topology changes and only use record-slot updates when that identity is proven unchanged. Include world epoch and allocation generation to prevent stale updates after park load or slot reuse.

Separate semantic effects explicitly: graphical state, physical topology/clearance, ride-rating context, routing and selection/picking. A generic `MapInvalidateTileFull` bridge can remain for unmigrated callers temporarily, but a graphical-only mutation must not silently dirty simulation caches. Conversely, replacing all invalidations with a graphics-only call would break correctness. Audit each owner method once, migrate its callers with compiler assistance, then delete the redundant notification code.

One immutable generation owns lifecycle, poses, appearance, topology and catalog revisions. CPU producers never write its pages after publication. GPU updates are ordered before the consuming draw and after earlier users of the destination storage; in-flight storage is not overwritten unsafely. Reuse existing frame-slot/fence and upload-ring lifetime rules, with no per-object fences, waits or submissions. A dropped mailbox packet does not advance the consumer's residency revision. Keep absolute current values and deletion tombstones so skipping generations cannot lose a deletion or require replaying every intermediate mutation.

## Evidence and experiments

The accepted path checkpoint measured 359.953 TPS / 144.011 application FPS, about 0.107 ms CPU Draw and 1.427 ms GPU at 4K over 12,000 measured ticks. Instrumented world uploads were 7,751,744 bytes and 1,108 copy calls over 4,801 frames, roughly 0.23 MB/s. **That is not evidence of a bus bottleneck.** CPU Draw excludes some publication/submission work, and the added object checkpoint may change these numbers. Establish its own baseline before attributing a regression to invalidation.

The [archived performance history](archive/performance-320-tps-refactor-history.md) records failed whole-map/entity-copy approaches: a full-map redraw snapshot fell to 139.278 TPS/36.406 FPS; a compact population capture alone cost about 270 microseconds, while rebuilding spatial ownership competed with simulation. Later legacy-compatible frame capture cost about 3 ms on the main thread. These are historical workloads, not apples-to-apples comparisons with this checkpoint. Their relevant lesson is to emit compact graphical state at the mutation owner, avoid full-world gathering and unnecessary indices, and keep one complete frame generation. The archive's old software fallback and lower acceptance floors are superseded by the current Vulkan-only contract.

Future experiments, in this order:

1. **Attribute current work.** Count invalidation calls by cause, unique dirty tiles, actually changed records per family, captured bytes, COW bytes, repacked bytes, GPU bytes, copy calls/ranges, arena growth/repack and snapshot age. Time notification, owner capture, worker apply, conversion/packing and submission separately. Include later ride-rating cache miss/rebuild counts. Instrumented comparison lanes must match; keep headline timing uninstrumented.
2. **Graphical-only owner notifications.** Compare audited brake/door/light changes with broad invalidation while preserving construction semantics. Qualification requires identical simulation checksum/census and exact visual output. Do not infer a gain solely from fewer calls.
3. **Family separation.** Independent CPU capture masks and GPU revisions for terrain/path/object state. Compare unchanged camera motion and single-brake workloads, then the populated park. Target no terrain/path payload for a brake-only edit whose tile directory/bounds stay unchanged.
4. **Stationary upload granularity.** Compare family chunks, complete compact element updates and one indexed GPU scatter batch using recorded real mutation distributions, plus a bounded construction burst. Sweep 1%, 10%, 25%, 50%, 75% and 100% changed records for crossover characterization; these synthetic sweeps are diagnostics, not the product acceptance workload.
5. **Moving families.** Reuse existing peep lifecycle/field contracts. Compare sparse 48-byte motion updates, contiguous whole motion slabs and changed-page copies. Add vehicle hot pose/orientation plus separate appearance and animation state. Test dense pose AoS first; try SoA/AoSoA only if profiling identifies useful bandwidth/cache savings.

Run gameplay acceptance at 3840×2160, VSync on the same 144 Hz display, EverythingPark, 100 warmup plus **12,000 measured ticks** with four 3,000-tick windows. Keep normal Turbo capped at 360; measure headroom separately. Pin park/assets/camera/build and verify final state. Use serial control/candidate/control runs, repeated when a difference is near run-to-run noise. No simultaneous builds or GPU workloads. Inspect final images and construction/animation samples manually; pixel-identical supported output is required except separately documented accepted divergences.

Proposed decision gates, to calibrate rather than hardcode now:

- Preserve approximately 360 TPS and 144 FPS in the full growing-park run, including the final dense window. Diagnose every new long-frame cluster; p99/max and counts above one/two display intervals matter more than average FPS alone.
- A repeated increase of 0.05 ms in publication/packing owner time or a 5% decrease in uncapped final-window TPS is an investigation trigger, not automatic proof of cause. Retain a more complex transport only if its CPU or frame-tail benefit exceeds measured control variation, or it removes substantial code with no measurable regression.
- Keep per-family copy calls bounded independently of object count. Track range count separately; one call containing thousands of ranges is not equivalent to one contiguous range.
- Use measured sparse/dense crossover with hysteresis (for example, switch dense above the measured crossover and back below it minus 10 percentage points) only if adaptive selection outperforms the simplest fixed mode. Never choose from a supposed universal PCIe threshold.
- Zero stale removals, ghost leaks, partial generations, allocation failures, unhandled overflow, validation errors or CPU waits added per object. Dense updates must not send unrelated cold appearance each frame.

## Future checklist

- [ ] Pin the completed ghosts/props/tracks checkpoint as the control; this study does not change that implementation.
- [ ] Add attribution for mutation causes, publication/packing CPU time, family amplification and transfer ranges.
- [ ] Audit track brake/light/door owners and split graphical changes from rating/topology effects.
- [ ] Separate family publication/residency revisions, retaining one coherent world generation.
- [ ] Introduce stable runtime graphical handles or constrain topology changes to whole tile-family replacement.
- [ ] Batch complete compact stationary element updates; qualify removal, reorder, arena reuse and dropped frames.
- [ ] Reuse/simplify peep field lifecycle publication and prototype dense hot pose AoS against sparse updates.
- [ ] Extend the same owner/module contract to vehicle pose, appearance and animation.
- [ ] Measure calibrated sparse/dense/range-merging choices only if simpler modes miss the target.
- [ ] Repeat exact visual and 4K/12,000-tick acceptance, report uncapped headroom and frame tails separately.
- [ ] Delete superseded generic notifications, polling and transport branches after ownership migration is complete.

The independent recommendation is therefore **whole compact element updates for mostly static world objects, shared ride rows for shared changes, and dense hot-pose arrays for busy moving populations**. Batch size and state granularity are separate choices: narrow semantic updates can still travel in a few large buffers.
