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
2. Platform capacity is derived from that station's special track-piece length; ordinary roller-coaster stations are not
   changed in this phase.
3. Queue-full state is station-specific rather than relying solely on the ride-wide historical `queueFull` hint.
4. Platform occupancy covers guests reserved/approaching a vehicle and passengers actively alighting. It is maintained or
   derived without a park-wide guest scan during route planning.
5. A station is overcrowded only when both its queue line and its transport platform are full. New route plans avoid it;
   an already committed guest may replan if the station remains unavailable.
6. Save loading either restores the new state under a fork park version or reconstructs transient occupancy from
   authoritative guest/vehicle state. Old parks default to fair transport pricing and empty transient crowding state.

Stretch goal: coaster platform waiting requires matching platform reservations to exact train/car capacity and reviewing
station graphics/loading geometry. It is intentionally excluded until transport-only behavior is validated.

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
