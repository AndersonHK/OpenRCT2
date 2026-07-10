# EverythingPark 320 TPS refactor plan

## Goal and acceptance criteria

The performance target is **320 completed logical simulation ticks per wall-clock second** on
`test/tests/testdata/parks/EverythingPark.park`, corresponding to the normal Turbo speed setting. A requested game-speed
multiplier is not evidence of success: the measured `GameState::currentTicks` delta must approach 320 TPS while the game
remains responsive.

The work follows these priorities:

1. A Vulkan-first renderer, GPU rasterisation and GPU-resident assets are preferred over moving complete frames across
   the CPU/GPU bus. OpenGL is a migration reference, not the target architecture.
2. RAM-heavy caches and precomputed indexes are preferred over repeating expensive deterministic calculations.
3. Coarse, deterministic multithreading is useful where ownership is clear. One process-lifetime worker pool is created at
   startup and reused; no hot path creates ad-hoc threads.
4. Single-threaded per-guest, per-vehicle, and per-pixel work is the most expensive resource and must be reduced first.

The target is measured in a Release x64 build with the same park checksum and deterministic simulation result. Visual
correctness, replay/network determinism, save compatibility, and ordinary 40 TPS timing remain hard constraints.

## Benchmark contract

### Pure simulation ceiling

The command-line simulation benchmark must separate these phases:

- context and object initialisation;
- park loading;
- a configurable warm-up that populates ride-rating and path caches;
- a configurable measured interval;
- checksum generation after the measured interval.

It reports total ticks, elapsed time, TPS, mean tick duration, and percentile/max tick latency. Cold-cache and warm-cache
results are recorded separately. Profiler export must cover the measured interval only.

### Integrated Turbo benchmark

The interactive benchmark loads the same park and records actual logical TPS, render FPS, simulation time, render time,
and time spent waiting for VSync separately. The controlled matrix is:

| Variable | Values |
| --- | --- |
| Renderer | Vulkan; OpenGL and software-with-hardware-display only as migration baselines |
| Viewport workers | disabled, enabled |
| VSync | disabled for throughput; enabled for the playability check |
| Camera | fixed position, rotation, zoom and viewport size |
| Speed | Turbo; debug Hyper only as a saturation diagnostic |

Turbo may render less often than it simulates, but input/event pumping must remain responsive. A 15-30 FPS presentation
cadence is acceptable during Turbo if it materially raises TPS. Normal speed keeps smooth variable-frame interpolation.

Each result is a warmed median of repeated runs, accompanied by the simulation checksum. Process-startup time is never
divided by the measured tick count.

## Prong A: Vulkan renderer, GPU-owned composition and bus traffic

Owner: durable `gpu_renderer` specialist.

1. Add an explicit renderer interface whose resource, command, synchronisation and presentation contracts do not expose
   OpenGL state. Implement Vulkan as the primary backend on Windows and Linux and use Vulkan portability through MoltenVK
   on macOS. Keep platform-window creation and input independent of the graphics API.
2. Require a modern Vulkan-capable device. Select queue families and memory types once, create a reusable pipeline cache,
   and retain swapchain-independent resources across swapchain rebuilds. Supporting obsolete GPU feature levels is not a
   constraint.
3. Keep indexed sprite atlases, palettes, remap tables, glyph atlases, weather state and immutable geometry GPU-resident.
   Upload changed assets and dirty dynamic ranges, not complete frames or complete atlases.
4. Replace CPU pixel rasterisation with instanced sprite, line, terrain, track and particle command streams. Preserve the
   painter's deterministic ordering initially, then move command culling, grouping and indirect draw generation into
   compute where ordering permits it.
5. Use persistently reusable staging and device buffers, timeline synchronisation and multiple frames in flight.
   Suballocate from bounded ring buffers instead of creating buffers or waiting for device idle in frame hot paths.
6. Batch opaque, masked, palette-remapped, transparent, text and weather work into stable Vulkan pipelines. Pipeline and
   descriptor selection must be command-stream data, not an excuse for a CPU flush per sprite.
7. Move precipitation generation and composition, palette lookup, remapping, clipping, scaling and final composition to
   shaders. Weather must not force a CPU scene rebuild or a full-frame CPU upload.
8. Preserve dirty-region knowledge as GPU scissor or damage metadata where it saves work, but prefer cheap complete GPU
   composition over complicated CPU dirty-pixel maintenance when measurements support it.
9. Remove framebuffer readbacks from ordinary presentation. Screenshots and diagnostics use explicit asynchronous
   transfer jobs and are the only routine readback path.
10. Treat the existing OpenGL and software renderers as visual-parity and bring-up references. Once Vulkan covers gameplay,
    screenshots and headless or test needs on all three desktop platforms, delete duplicate legacy rendering paths rather
    than carrying permanent abstraction and maintenance cost.
11. Validate rotations, zooms, transparency, palette animation, remaps, TTF text, weather, screenshots, resize and
    fullscreen, device loss and swapchain recreation on Windows, Linux and macOS.

Expected result: the CPU emits compact scene commands instead of rasterising pixels; pixels, palette work, effects and
most composition remain on the GPU. CPU-GPU traffic scales with changed commands and assets rather than display resolution.
OpenGL cleanup completed during bring-up is retained only where it reduces migration risk and is removed with that backend
after Vulkan parity.

### Vulkan integration checkpoint

The gated backend now owns indexed line, opaque, transparency/blend, weather, SDR/HDR10 presentation and asynchronous indexed
readback passes. Readback reuses the persistently mapped per-frame ring, is keyed by request id, and is harvested only after the
owning frame fence signals; ordinary presentation performs no CPU readback or device-idle wait.

An `ENABLE_VULKAN_DRAWING_ENGINE` validation path wires SDL Vulkan window creation, resize, palette updates, presentation and the
existing synchronous screenshot surface through the engine factory. For bring-up it uploads the authoritative X8 indexed canvas
as one transfer. This remains an explicitly excluded fallback: no result from it belongs in the Vulkan performance row.

The separate, also-off-by-default `ENABLE_VULKAN_DIRECT_DRAWING_CONTEXT` gate selects direct `IDrawingContext` command recording
and generation-aware persistent atlas residency. It preserves clipping, sprite remaps, masks, transparency depth order,
glyph/TTF layers and weather commands while performing no routine CPU framebuffer upload or readback. It currently redraws the
complete command list, leaves dirty-region `CopyRect` disabled, and returns no screenshot until asynchronous readback is adapted
to the synchronous screenshot consumer. Only this direct gate may enter the EverythingPark renderer matrix, after visual parity
and screenshot handling pass.

## Prong B: ride-rating and local-context caches

Owner: durable `rating_cache` specialist.

1. Replace the linear active-rating-sample search with direct entity-to-sample lookup while preserving stable sample
   publication order.
2. Retain each sampled vehicle's last local-context key and score. Reuse it until tile, height, ride, track type or
   direction changes instead of hashing the global cache on every physics tick.
3. Replace whole-cache invalidation scans with spatial buckets, chunk generations, or a similarly deterministic spatial
   invalidation scheme.
4. Reserve cache and sample storage based on ride vehicle counts to avoid growth churn.
5. Preserve the exact existing excitement, intensity, nausea, comfort and decoration totals; this is a calculation-cache
   refactor, not a rating rebalance.
6. Add profiler scopes around cold local-context construction, cache lookup, sample lookup and sample publication.

Expected result: steady-state vehicle updates become O(number of sampled cars), not O(cars squared), and most sub-tile
vehicle ticks avoid both hash lookup and the 15 by 15 local-context scan.

### Measured vehicle-update follow-up

A warmed 500-tick EverythingPark profile after the first cache slices attributed 1,381,668 microseconds to
`VehicleUpdateAll`, or 2.763 milliseconds per simulation tick. For comparison, `PeepUpdateAll` accounted for 1,920,543
microseconds and `Ride::updateAll` for only 15,720 microseconds. Vehicle local-context scoring accounted for 149,841
microseconds across 18,569 calls. Local context is therefore worth retaining and refining, but it cannot explain most of
the remaining vehicle cost; state and track-motion work must be measured separately.

The next vehicle slice resolves the owning `Ride` and `RideObjectEntry` once when a train head begins its update and keeps
those pointers in a scoped, transient lookup context. Existing helpers still call `Vehicle::GetRide()` and
`Vehicle::GetRideEntry()`: they receive the cached pointer only when the queried ride id or loaded-object subtype matches,
and otherwise execute the original lookup. The scope is restored on every return, including cable-lift updates. It is not
saved, does not outlive one head update, does not change train order, and assumes the existing invariant that ride deletion
and object unloading do not occur recursively inside a vehicle update.

Profiler scopes divide `Vehicle::Update()` into live rating sampling, test measurements, common station states,
travelling, track motion, and sound. The acceptance check is another identical warmed 500-tick run with the same final
checksum. It must report both the `VehicleUpdateAll` delta and the child-scope distribution; until that rerun is recorded,
the lookup cache is an implementation-backed optimization candidate rather than a claimed TPS improvement.

The station-platform work also replaces byte-sized, repeated train capacity totals with one deterministic
`TrainSeatSummary`. It traverses the linked cars once and reports a wide capacity, current passengers, reserved seats,
rider presence, and stable car pointer order. Live rating sampling and station dispatch consume the same summary shape;
the transient platform template consumes its exact car/seat mapping at departure transitions. It is synchronous runtime
state rather than saved redundancy, so later profiling can safely add a one-head-update lifetime cache without creating a
second persistent source of truth.

Before the eligibility refactor, `VehicleUpdateAll` used 1,273,315 microseconds in the warmed 500-tick profile and
`RideRatingUpdateLiveTrainSample` reported 1,081,990 microseconds across all 485,000 vehicle updates. Moving the unchanged
eligibility gate to the caller reduced sampler entries to 280,975. The first checksum-matched rerun measured
`VehicleUpdateAll` at 1,410,234 microseconds, `Vehicle::Update` at 1,391,864 microseconds, and eligible live sampling at
1,189,440 microseconds. `RideRatingBuildLocalContextScore` accounted for 154,018 microseconds across 18,736 cache misses and
`UpdateTrackMotion` for 92,766 microseconds, leaving the sampler's ordinary self/per-car work as the next measured target.
Two clean 2,000-tick runs reached 254.297 and 256.592 TPS with matching `182e7448...` checksums.

The next attribution layer profiles consist construction and rider qualification once per eligible head, synchronization
once per sampled train, active-accumulator resolution and vehicle accumulation once per sampled car, and force,
environment, and score application below that. It also removes immutable duplicate derivation without changing cadence:
each sampled car now resolves its track descriptor once for both G-force and feature scoring, computes normalized speed once,
and receives boat-hire/transport flags derived once per train. This is runtime stack state only; car order, longitudinal-G
history, context-cache ownership, accumulator lookup keys, and publication remain unchanged. The extra child scopes are
diagnostic, so no TPS improvement is claimed until a profiler-disabled checksum-matched rerun separates code savings from
profiling overhead.

No additional persistent train cache is introduced. Rating-active statuses and the station's `waitingForPassengers` status
are mutually exclusive within one vehicle update, so the current summary already has a single consumer in the measured path.
Active accumulators likewise retain the existing validated entity-id index; caching vector pointers across creation would be
less robust because vector growth can invalidate them.

The directed-leg/save/cache checkpoint repeats the clean 2,000-tick run at 251.729 and 251.123 TPS with matching
`89b1134c...` checksums and median ticks of 3.719 and 3.720 milliseconds. The faster result reaches 78.7% of Turbo 320 and
is 51.7% above the original baseline. The corresponding instrumented 500-tick capture reports 1,358,188 microseconds across
279,531 eligible sampler calls. Of the 1,238,562 microseconds inside per-car accumulation, environment resolution accounts
for 1,082,148 microseconds across 582,586 cars while actual cold local-context construction accounts for only 159,625
microseconds across 19,627 calls. Enabled profiler scopes include clock reads, atomic sample updates, and thread-local stack
bookkeeping, so these totals identify relative ownership rather than forecasting unprofiled TPS. They still separate the
remaining bottleneck from the 15-by-15 context scan: the next safe slice must reduce cache/key/generation overhead on warm
environment access without changing spatial invalidation or sample cadence.

The reviewed upstream-integration checkpoint, including removal of always-on spatial counters/sorting and conservative
single-generation rating-cache validation, reaches 261.961 and 263.398 TPS in two clean 2,000-tick runs. Both finish at
`72638ee2...`; median ticks are 3.699 and 3.692 milliseconds. The faster run reaches 82.3% of Turbo 320 and is 58.8% above
the original 165.895-TPS baseline. This checksum differs from the pre-merge checkpoint because upstream changes advance the
deterministic simulation/network version; equality between the two post-merge repetitions is the acceptance signal.

## Prong C: scheduler, instrumentation and fast-forward cadence

Owner: durable `benchmark_scheduler` specialist.

1. Implement the benchmark contract above in the CLI without using shell timing as the primary measurement.
2. Expose actual TPS independently from FPS and requested game speed.
3. Keep normal 40 TPS scheduling unchanged.
4. During Turbo, limit presentation work to a configurable responsiveness cadence while continuing to pump input and
   network events.
5. Avoid giant uninterruptible batches: cap the amount of simulation work performed before event pumping or presentation
   reconsideration.
6. Keep multiplayer clients server-tick-driven and exclude local fast-forward policy from deterministic game state.
7. Export profiler data from a warmed, precisely bounded tick range.

Expected result: the benchmark identifies real simulation ceilings, and Turbo does not waste the main thread rendering
frames more quickly than a user can perceive.

### Hot typed-entity iteration

`PeepUpdateAll` walks the guest and staff type lists every tick, and many spatial checks use the mixed per-tile entity lists.
The iterators now retain the owning `EntityRegistry` reference instead of reacquiring global game state for every element.
For concrete typed lists they compare the stored entity tag directly and cast only after it matches, avoiding the out-of-line
`EntityBase::is<T>()` specialization call on every guest, staff, vehicle, litter, or effect. Polymorphic `Peep` tile queries
retain the original `as<Peep>()` check because they intentionally accept both guests and staff. This deliberately preserves
the existing `std::list` traversal and its mutation semantics; converting live entity lists to vectors or tick snapshots is
not yet justified. The previously latent postfix-iterator implementation also now returns the pre-increment iterator as the
standard contract requires.

Acceptance requires the full entity/pathfinding suite and an unchanged EverythingPark checksum. The warmed benchmark must
show the combined result; no standalone TPS gain is claimed for this small iterator reduction.

`UpdateEntitiesSpatialIndex` no longer walks every typed entity list merely to find the few entities whose location crossed a
spatial bucket. `EntityBase::setLocation()` now adds registered entities to a deduplicated worklist at the same point that it
sets the existing dirty bit. End-of-tick commits process those deduplicated ids directly; the established sorted bucket insertion
preserves exact per-bucket order independently of worklist order. An immediate spatial update leaves its id represented until the
end-of-tick pass, so a second move in the same tick still commits; removal and same-tick id reuse resolve against the live registry
slot. Full spatial rebuilds clear the worklist and remain authoritative for resets and importers that assign coordinates directly.

Empty worklists return before entering the profiler scope. Temporary always-on diagnostic counters have been removed rather than
taxing every enqueue and commit or exposing a configuration-specific public API. The existing runtime-enabled profiler scope
remains available for nonempty passes, but no TPS gain is claimed until the checksum-matched maximum-speed benchmark is repeated.

## Prong D: transport graph, fare buckets and route intelligence

Owner: root integration engineer.

1. Build a cache of open transport services. Each directed station segment stores boarding and destination station,
   measured time, distance, quality-adjusted value, fare, shelter and current crowding state.
2. Invalidate only the affected service when ride status, price target, station layout, measurements, breakdown state or
   queue/platform crowding changes.
3. Support journeys remaining on the same vehicle through any number of intermediate stations, including the explicit
   two-segment case.
4. Compare total door-to-door time: walk to station, queue/platform wait, all onboard segments, and walk from the selected
   exit to the final target.
5. Use the four dynamic fare buckets:
   - free: zero fare;
   - discount: half of the selected journey's quality-adjusted value;
   - fair: the journey's full value;
   - extortive: twice the journey value.
6. Free journeys may be selected when approximately tied with walking. Discount requires a modest saving; fair requires
   a stronger saving. Precipitation relaxes those thresholds in proportion to shelter and saved exposure.
7. Extortive journeys are a last-resort connectivity edge only: no walking route and no usable non-extortive transport
   alternative may exist. The guest must be able to afford it. Paying the fare lowers happiness and creates a specific
   thought.
8. A free voucher and no-money park make the guest's effective bucket free without changing the ride's configured bucket.
9. Route selection and boarding admission use the same journey/fare calculator so a route accepted by planning is not
   rejected by a different single-segment calculation at the entrance.

Expected result: transport behaves as directed path edges rather than attraction visits, including multi-station trips,
without rescanning every ride and its recent samples for every destination decision.

## Prong E: transport station capacity and crowding

Owner: root integration engineer.

1. Capacity applies only to rides with `RtdFlag::isTransportRide`.
2. Platform capacity is captured from one actual stopped consist: exact linked cars, masked seats, loading positions, and
   station-relative wait coordinates. Before capture, supported rails use the first valid linked consist for nominal capacity;
   invalid or absent train entities report zero. Only transport types or station styles without a platform adapter retain the
   conservative station-tile estimate; a supported rail using JIT loading still reports its real nominal consist capacity.
   Ordinary roller-coaster stations are not changed in this phase.
3. Queue-full state is station-specific rather than relying solely on the ride-wide historical `queueFull` hint.
4. Platform occupancy covers the visible abstract train-load waiting for future service. Through-riders unload and compact
   before that cohort binds FIFO into actual empty seats. Registry transitions avoid a park-wide route-planning scan.
5. A station is overcrowded only when both its queue line and its transport platform are full. New route plans avoid it;
   an already committed guest may replan if the station remains unavailable.
6. Save loading reconstructs transient occupancy from authoritative guest state under private version `60014`; older-target
   exports convert platform guests to a coherent station-exit approach.

Chairlift now uses the same stopped-pose template with its established 32-coordinate entrance geometry. Lift remains JIT:
its vertical shuttle does not expose a trustworthy generic physical-clear transition because `FinishDeparting()` occurs at
the tower top. A Lift-specific clearance event is required before enabling its platform cohort. Compatible coaster stations
remain a later opt-in through the same template rather than duplicated capacity or boarding logic.

## Prong F: shared pathfinding performance

Owner: root initially; durable specialists are re-tasked here after their first prongs stabilise.

1. Add profiler coverage to destination planning, `ChooseDirection`, transport candidate evaluation, and failed searches.
2. Replace repeated park-wide destination searches with shared reverse route fields for stable high-demand targets such as
   exits, first aid, ride entrances and transport stations.
3. Cache path topology separately from dynamic congestion/cost. Invalidate topology by edited chunks rather than clearing
   all route knowledge.
4. Use spatial station/ride candidate buckets before invoking detailed route evaluation.
5. Avoid duplicate searches: the direct walking reachability result used for extortive-fare policy is also the first step
   used when walking wins.
6. Bound per-tick route work and carry resumable searches forward where a full answer is not required immediately.
7. Preserve deterministic tie-breaking and existing pathfinding fixtures.

Measured first slice: a warmed 500-tick EverythingPark profile attributed 1,125,511 microseconds to 12,093
`ChooseDirection` calls, including 416,955 microseconds and 3,008,010 calls in thin-junction classification. Exact
`MapPathTopology` nodes now precompute that classification and retain one chunk view during each synchronous search. Inexact
chunks, unmatched live nodes, and unsupported ghost layouts retain the original tile-element fallback. The acceptance check
is a same-configuration warmed rerun with the same checksum; `PathIsThinJunctionLive` should be rare, and reduced
`ChooseDirection` time rather than a synthetic microbenchmark is the success measure.

Recorded outcome: the repeated clean 2,000-tick run improved from 165.895 to 184.352 TPS (`+11.1%`) and reduced median tick
time from 5.919 to 5.054 milliseconds (`-14.6%`) with the same `efedf32f...` checksum. In the repeated 500-tick profile,
`ChooseDirection` fell to 540,621 microseconds (`-52.0%`), `PeepUpdateAll` fell to 1,271,760 microseconds (`-33.8%`), and
`PathIsThinJunctionLive` recorded zero calls. This validates the exact-cache path and leaves the live fallback available for
parks that contain unsupported layouts.

The next slice shares reverse path-tile distance fields for concrete park and resolved ride/facility entrances. Exact
topology is frozen on the main thread, independent target fields are built through the synchronous process-lifetime worker
pool, and epoch validation plus stable target-order publication remain serial. Guest queue/banner/history decisions still
validate the proposed edge in `ChooseDirection`; unsupported targets retain the bounded heuristic. See
[Shared destination route fields](shared-route-fields.md).

Recorded shared-field outcome: two clean 2,000-tick runs measured `241.767` and `241.871` TPS with matching
`2fc90d5f...` checksums and median tick times of `3.868` and `3.851` milliseconds. That is `+31.2%` over the preceding
`184.352`-TPS checkpoint and `+45.8%` over the original `165.895`-TPS baseline. The result reaches `75.6%` of the 320-TPS
target; the remaining median budget is approximately `0.726 ms` per tick. In the 500-tick profiled run, `ChooseDirection`
fell to `201,038` microseconds and `PeepUpdateAll` to `880,203` microseconds. `VehicleUpdateAll` is now the dominant measured
subsystem at `1,273,315` microseconds, including `1,081,990` microseconds across 485,000 live-rating sample calls, so the
next optimization pass follows that measured child scope rather than broadening the routing cache speculatively.

Source-only routing follow-up after that checkpoint: the common station-less shop/facility class, including first aid, now
contributes an explicitly typed target only after the main thread validates its descriptor, station-zero coordinate, and
owning track element. Frozen path slope data preserves the exact entry height. A sorted ride-to-target index is published only
for rides with one distinct, successfully seeded target, so those junction decisions avoid repeated station scans and queue-end
walks; rides with multiple destinations keep the existing closest-station policy. Lookups allocate nothing per tick, workers
still read only frozen data, and topology-epoch validation still precedes serial publication. No TPS improvement is claimed
until the same warmed EverythingPark run repeats with a matching new checksum; the expected effect is a smaller residual
destination-resolution/`ChooseDirection` cost, not another field-build-scale jump.

Recorded combined follow-up outcome: two clean 2,000-tick runs measured `254.297` and `256.592` TPS with matching
`182e7448...` checksums and median tick times of `3.834` and `3.794` milliseconds. The faster run reaches `80.2%` of Turbo
320 and is `54.7%` above the original `165.895`-TPS baseline. In the new 500-tick profile, `ChooseDirection` used `81,798`
microseconds, while `RideRatingUpdateLiveTrainSample` still dominated at `1,189,440` microseconds across `280,975` eligible
head calls; local-context construction accounted for only `154,018` microseconds. The eligibility gate removed 204,025
sampler entries but did not remove the per-eligible-head cost, so the next vehicle slice must optimize that work without
reducing sample cadence or changing rating semantics.

Source-only route-field ownership follow-up: published fields now retain only one next-direction byte per path-node/target
pair plus the shared sorted path-location index. Per-worker 32-bit BFS distances and queues, and the frozen entrances,
connections, reverse edges, and seed indexes are preparation-only and are released after epoch validation and serial
publication. Entrance seeds use a location index and station-less facilities probe only adjacent candidate paths. If any
required chunk is inexact, the epoch is published as fallback-only rather than constructing a graph with holes. This is a
memory/cold-build correction with no new TPS claim until the benchmark is repeated.

Expected result: destination-aware guests share expensive topology work, while dynamic transport and crowding costs remain
cheap overlays.

## Integration sequence and gates

Agents edit without compiling. Root reviews and integrates each prong, then exclusively runs builds and tests in this
order:

1. static diff and call-graph review;
2. formatting only for changed hunks;
3. compile the changed native projects;
4. focused renderer/rating/pathfinding/park-file tests;
5. full test suite;
6. cold and warm headless EverythingPark benchmarks;
7. interactive Turbo renderer matrix;
8. Release deployment and binary hash verification.

Any compile or test failure is returned to the specialist that owns the affected prong unless it is an integration fault.
Agents are re-used rather than replaced so they retain subsystem knowledge. After correctness is green, profiler evidence
determines the next re-task; speculative micro-optimisations do not outrank measured hot paths.

## Completion audit

Completion requires evidence for all of the following:

- actual warmed Turbo TPS is reported against the 320 target;
- normal-speed, multiplayer/replay determinism and simulation checksum remain valid;
- Vulkan performs GPU sprite and effect composition without weather-triggered CPU redraws or routine framebuffer uploads;
- Windows, Linux and macOS Vulkan-portability builds pass renderer parity checks before legacy backends are removed;
- no per-car quadratic rating-sample lookup remains;
- repeated sub-tile vehicle ticks reuse local-context results;
- transport fares are route-specific and exactly follow the four buckets;
- free, discount, fair and extortive route-choice tests pass;
- extortive transport is impossible when walking or a non-extortive service is usable;
- extortive travel causes the documented happiness/thought effect;
- two-segment and longer same-ride journeys alight at the selected station;
- transport-only platform capacity and station-specific overcrowding tests pass;
- old parks load with safe defaults and new parks round-trip the new persistent state;
- focused and full tests pass, followed by a successful Release deployment.
