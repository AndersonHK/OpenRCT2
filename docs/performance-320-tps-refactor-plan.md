# EverythingPark 360 TPS refactor plan

## Goal and acceptance criteria

The performance target is **360 completed logical simulation ticks per wall-clock second while sustaining 144 presented frames
per second** on `test/tests/testdata/parks/EverythingPark.park`. A requested game-speed multiplier is not evidence of success:
the measured `GameState::currentTicks` and completed-frame deltas must reach both targets in the same interval while the game
remains responsive. Ordinary offline Turbo requests nine logical updates per 40 Hz scene batch, or 360 TPS. Network sessions
retain the established eight-update cadence so the local performance target does not alter protocol pacing.

The work follows these priorities:

1. Vulkan owns accelerated rendering, GPU rasterisation, and GPU-resident assets instead of moving complete frames across
   the CPU/GPU bus. The software renderer remains a diagnostic baseline; the duplicate OpenGL backend has been retired.
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

It reports total ticks, elapsed time, TPS, mean tick duration, percentile/max tick latency, and simulation-state snapshots at
the measured interval boundaries. Cold-cache and warm-cache results are recorded separately. Profiler export must cover the
measured interval only.

### Integrated Turbo benchmark

The interactive benchmark loads the same park and records actual logical TPS, render FPS, simulation time, render time,
and renderer-exposed presentation/GPU timings separately. Complete draw-call time remains the portable measure when a backend
cannot isolate a VSync wait. The controlled matrix is:

| Variable | Values |
| --- | --- |
| Renderer | Vulkan; software-with-hardware-display only as a diagnostic baseline |
| Viewport workers | disabled, enabled |
| VSync | disabled for throughput; enabled for the playability check |
| Camera | fixed position, rotation, zoom and viewport size |
| Speed | Turbo; debug Hyper only as a saturation diagnostic |

Turbo presentation follows the current SDL display refresh rate while VSync is enabled. Simulation throughput is no longer
allowed to buy TPS by deliberately reducing Turbo to 15-30 FPS: message pumping, window input, command generation and
presentation must continue at the display cadence whenever complete logical-tick slices fit within that budget. Normal speed
keeps variable-frame entity interpolation. A single logical update remains the deterministic, non-preemptible unit, so a tick
whose own wall time exceeds one refresh interval is reported as a latency miss rather than hidden by an average FPS value.

Each result is a warmed median of repeated runs, accompanied by the simulation checksum. Process-startup time is never
divided by the measured tick count.

The full executable now provides an opt-in, non-interactive form of this benchmark:

```text
openrct2 EverythingPark.park --benchmark-ui --benchmark-warmup=5 --benchmark-duration=30 \
    --benchmark-renderer=vulkan --benchmark-vsync=0
```

For deterministic comparisons, replace the time-based phase limits with fixed counts:

```text
openrct2 EverythingPark.park --benchmark-ui --benchmark-warmup-ticks=1998 --benchmark-ticks=3600 \
    --benchmark-renderer=vulkan --benchmark-vsync=1
```

Fixed counts must be multiples of the current nine-tick offline Turbo batch. This keeps the initial/final population, route-cache
state, and checksum identical across builds instead of allowing a faster build to simulate farther during a time-based warm-up
and benchmark a different park population.

Add `--benchmark-profile=integrated.csv` (or `.json`) to profile only the fixed measurement phase. Profiling adds timing and
atomic bookkeeping, so its output locates large subsystems while a separate profiler-disabled run remains the acceptance result.

The first fixed-tick profile established the size of the rendering problem. Across 512 measured logical ticks,
`ViewportFillColumn` accumulated 18.076 seconds of worker CPU time over 27,300 calls, `TileElementPaintSetup` accumulated
6.223 seconds over 5,460,000 calls, and `EntityPaintSetup` accumulated 4.456 seconds over 10,920,000 calls. The corresponding
GPU frame averaged about 0.124 ms. A headless fixed-state run reached 551.972 TPS while the integrated Vulkan/VSync run was in
the low 200s. Exact small run-to-run differences are ambient noise; the actionable result is that the CPU repeatedly rebuilds
semantic world graphics that the GPU could retain, cull, sort, and instance.

Add `--benchmark-visible` to retain the benchmark's ordinary window for compositor and hands-on playability checks. It remains
non-interactive in duration and exits automatically; fullscreen, cursor trapping, configuration persistence, and park audio stay
disabled so the visible and hidden rows differ only at the window/compositor boundary.

It creates the renderer's normal SDL window and surface with `SDL_WINDOW_HIDDEN`, waits for the requested park to become the
active game scene, selects ordinary Turbo, warms by steady-clock time, measures, prints the result, and exits. A failed park
load also exits instead of leaving a hidden title screen running. The hidden-window exception applies only to benchmark draw
scheduling: ordinary hidden or minimised windows still suppress drawing. Audio uses the dummy context, and fullscreen is not
entered, so the run does not take focus or produce park sound.

A 60-second startup watchdog covers a stuck preloader, missing-object prompt, or other hidden modal failure. Once warm-up begins,
the run also aborts if it leaves the game scene, becomes paused or networked, or changes away from ordinary Turbo; it never prints
such an invalid interval as a successful matrix result.

Renderer and VSync overrides are applied directly while constructing the window and drawing engine. They never replace or save
the corresponding user configuration values. The output always identifies the actual renderer, and an explicitly requested
renderer falling back is a benchmark failure so an automated matrix cannot silently mislabel its row. Omitting
`--benchmark-renderer` or using `--benchmark-vsync=-1` retains the configured choice. The UI benchmark cannot be combined with
`--headless` or run from the CLI-only executable, because its purpose is to retain real paint and presentation work.

The measured report separates the monotonic logical-tick delta/TPS, draw count/FPS, main-thread game-scene time, and complete
draw-call time. The latter includes presentation so it remains meaningful on every backend. When the renderer exposes
fence-complete timing samples, the report additionally gives CPU submit, CPU present, the measured presentation API call,
whole-frame GPU, and GPU-pass means; unsupported values are printed as unavailable rather than inferred from wall time. A
non-timed boundary drains and discards warm-up frames before measurement, while the final non-timed drain includes every submitted
measurement frame without adding GPU wait time to the reported wall interval. Initial/final population and route snapshots plus
the entity checksum are collected outside the measured interval. The rate/mean calculation is a pure helper with focused
zero-input and representative-counter coverage.

The first clean integrated matrix used three independent two-second-warm-up/five-second-measurement runs per renderer before
OpenGL retirement and the refresh-paced scheduler. These figures are historical throughput baselines, not the current
playability result. The runs used ordinary Turbo, VSync disabled, the same hidden window, and the same EverythingPark input.
Medians are:

| Renderer | Logical TPS | Draw FPS | Complete draw | Renderer detail |
| --- | ---: | ---: | ---: | --- |
| Software with hardware display | 318.324 | 13.397 | 970.191 us/draw | Backend timing unavailable |
| OpenGL | 318.323 | 13.397 | 2,252.785 us/draw | Backend timing unavailable |
| Vulkan direct/render worker | 318.400 | 13.400 | 1,797.404 us/draw | 165.573 us submit; 70.403 us present; 29.061 us `vkQueuePresentKHR` |

Vulkan reports 139.091 us total GPU time per presented frame: 17.154 us upload, 116.354 us indexed drawing,
0.596 us LightFX, and 5.142 us final composition. A separate Vulkan/VSync playability run reaches 316.830 logical TPS
and 13.334 draw FPS, with a 28.016 us present call and 134.014 us GPU frame. The result reaches 99.5% of the exact 320
wall-clock target while retaining the normal scheduler rather than running a synthetic tight loop. Rendering is not the
limiter at this cadence: even the complete Vulkan draw occupies about 2.4% of the measured wall interval and the GPU itself
about 0.14 ms per presented frame. The configuration file hash and modification time are unchanged across the benchmark.

The run also exposed and closed an initialization-order defect before acceptance. Vulkan originally captured remap, blend,
and baked-light tables while the drawing engine was created, before base graphics and LightFX were loaded, producing exactly
192 invalid `palettes.dat` requests. Capture now occurs once at the first real draw and is versioned through the render-worker
packet. Repeated Vulkan runs report zero palette warnings.

### Refresh-paced Turbo checkpoint

The former Turbo scheduler explicitly limited presentation to one frame every `1 / 15` seconds and then ran eight logical
updates in a 40 Hz scene batch before returning to SDL. That policy explains the measured 13.334 FPS: it was intentional
throttling, not a Vulkan or GPU ceiling. Turbo now yields only between completed logical updates. At that safe boundary it can
pump SDL, dispatch completed background work, process window input, update the UI, and paint when the monitor-derived refresh
deadline is due. Forty-Hz scene housekeeping remains unchanged; offline Turbo now performs nine logical updates per batch.

Presentation deadlines remain anchored to the current refresh rate. Turbo simulation now owns a separate deadline instead of
borrowing the ordinary four-frame accumulator. Any lateness observed before a batch is discarded, so rendering, OS scheduling,
or a long tick cannot manufacture catch-up work and produce a repeating fast/slow oscillation. A batch that itself exceeds its
25 ms budget resumes immediately at sustainable throughput. Changing speed, time scale, benchmark phase, or leaving offline
play resets the simulation deadline; network catch-up retains the established bounded accumulator.

SDL display changes refresh the cached presentation rate; invalid or unavailable rates use a 60 Hz fallback. Vulkan VSync
prefers `VK_PRESENT_MODE_MAILBOX_KHR`, when available, so both the CPU frame mailbox and the swapchain keep the newest completed
image instead of rebuilding a stale FIFO queue. FIFO remains the mandatory tear-free fallback, and Immediate remains the
non-VSync choice.

Two independent two-second-warm-up/five-second EverythingPark runs on the 144 Hz display produced:

| Run | Logical TPS | Draw FPS | Message pumps | Window updates | p50 / p95 / max interval | GPU frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Refresh paced 1 | 264.897 | 144.018 | 156.784 Hz | 156.186 Hz | 7.100 / 8.341 / 9.553 ms | 702.754 us |
| Refresh paced 2 | 265.265 | 144.018 | 158.600 Hz | 158.000 Hz | 7.111 / 8.184 / 11.363 ms | 704.099 us |

This deliberately spends about 25% of the wall interval on 722/721 complete paints instead of 67 paints, so integrated TPS
falls from the old 316.830 throughput-oriented VSync row to about 265. The headless simulation ceiling remains above 600 TPS;
the new result expresses the chosen playability priority rather than a simulation regression. The benchmark now also reports
message-pump and window-update rates, frame-interval p50/p95/p99/max, mean and longest 40 Hz scene batches, and the longest
UI-bounded logical slice. Renderer timing samples equal the produced-frame counts in both accepted runs.

Those two rows predate the present transport-routing workload. The current three-second-warm-up/ten-second Vulkan/VSync result
is 250.173 logical TPS and 144.069 FPS. Frame intervals are 7.270/8.456/9.310 ms at p50/p95/p99 with a 10.463 ms maximum. Mean
GPU work is 124.006 microseconds per frame; simulation averages 3.123 ms per logical tick and remains the limiting workload.
The 144 Hz presentation target is met without hiding current routing cost behind skipped frames.

The default benchmark still creates a hidden SDL window for repeatability; `--benchmark-visible` exposes the same timed run for
the compositor/playability row because a compositor may treat a hidden surface differently. Hitting 144 produced frames also
does not yet prove a hard guarantee when one future logical tick itself takes
longer than 6.94 ms. That stronger guarantee requires moving simulation behind an immutable/double-buffered visual snapshot so
the UI thread can present and accept input while the next state is being computed. The next renderer slice should likewise
move paint traversal and command preparation behind that boundary; Vulkan backend submission already consumes immutable newest
frame packets without a full-canvas upload.

### Nine-tick and retained-canvas checkpoint

The Turbo scheduler now treats simulation and presentation as separate deadlines and sleeps only until the earlier one. The
previous simulation-only sleep could wake after a 144 Hz presentation deadline even when a frame took less than one millisecond,
which produced the misleading 132 FPS plateau on Diamond Heights. A fixed 3,600-tick run now sustains 144.021 FPS with
6.946/7.277/7.657 ms p50/p95/p99 frame intervals and 354.877 TPS. CPU frame construction averages 0.170 ms and GPU execution
0.047 ms, leaving substantial presentation headroom; the small TPS deficit is scheduler and simulation time, not rendering.

Vulkan now retains the pre-weather indexed canvas in device-local memory. Generation-tagged dirty cells remain pending until a
frame is actually presented, so replacing a stale mailbox packet cannot lose damage. Sparse changes restore the retained canvas,
clear and redraw only dirty regions, then snapshot the new base before weather. When more than half the damage grid is dirty, the
renderer deliberately crosses over to one full scene traversal: repeated clipped viewport entry is measurably slower on dense,
highly animated parks.

On EverythingPark the adaptive crossover sustains 144.035 FPS and 248.336 TPS over the same fixed 3,600 logical ticks. Complete
CPU-side draw construction averages 1.422 ms while the GPU averages 0.101 ms. Simulation averages 3.193 ms per logical tick, so
the combined 360 TPS / 144 FPS target is not yet reached. The next large gain requires persistent world records and GPU culling
or indirect draw generation, plus simulation hot-path work; further present-call tuning cannot recover this gap.

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
10. Keep software rendering as an explicit diagnostic and headless/test reference. OpenGL and the full-canvas Vulkan bridge are
    already deleted; continue removing duplicate CPU presentation paths as Vulkan coverage expands across desktop platforms.
11. Validate rotations, zooms, transparency, palette animation, remaps, TTF text, weather, screenshots, resize and
    fullscreen, device loss and swapchain recreation on Windows, Linux and macOS.

The retained-data implementation order is:

1. retain immutable sprite asset descriptors beside atlas allocations and send compact per-instance commands;
2. publish generation-safe, pointer-free entity visual changes from the owning registry;
3. freeze pointer-free main-viewport world records after visibility resolution and process them on a latest-only worker;
4. replace per-frame tile/entity pull traversal with dirty paged scene buffers;
5. move shared world culling, ordering, and indirect draw generation to compute shaders;
6. remove obsolete CPU knowledge only after the Vulkan path owns the same correctness boundaries.

Entities cannot be migrated as a separate unordered overlay: vehicles, guests, terrain, track, and scenery currently share the
same painter sort. The retained buffers must therefore converge on one world-space ordering model. Park reset, entity ID reuse,
object unload, renderer changes, screenshots, and shutdown are generation or drain boundaries, not hot-path reasons to block.

The first retained-descriptor checkpoint uses 60-byte ordinary sprite instances instead of the general 100-byte rectangle
record. A device-local 4 MiB table owns atlas origins and layers, and the existing render worker transfers descriptor and sprite
pixels transactionally on first residency. A fixed 2,000-tick validation row produced 248.800 TPS and 144.055 FPS with the same
`c241cc46...` checksum; mean GPU time was 0.096 ms. This single row is a correctness/stall check, not a claimed TPS improvement:
it lies inside the established run variance and does not yet remove CPU paint traversal.

Expected result: the CPU emits compact scene commands instead of rasterising pixels; pixels, palette work, effects and
most composition remain on the GPU. CPU-GPU traffic scales with changed commands and assets rather than display resolution.
The duplicate OpenGL backend and its GL state, upload, shader, and transparency-depth implementations have been removed. Old
configuration values migrate to the hardware-presented software renderer.

### Vulkan integration checkpoint

The backend owns indexed line, opaque, transparency/blend, weather, SDR/HDR10 presentation and asynchronous indexed
readback passes. Readback reuses the persistently mapped per-frame ring, is keyed by request id, and is harvested only after the
owning frame fence signals; ordinary presentation performs no CPU readback or device-idle wait.

The Vulkan drawing engine uses direct `IDrawingContext` command recording and generation-aware persistent atlas residency. It
preserves clipping, sprite remaps, masks, transparency depth order,
glyph/TTF layers and weather commands while performing no routine CPU framebuffer upload or readback. It currently redraws the
complete command list and leaves dirty-region `CopyRect` disabled. Screenshot requests are explicit synchronization boundaries:
the worker presents the attached visual packet, reads the indexed canvas, and returns through the established PNG writer.

### Vulkan presentation-boundary checkpoint

The backend now exposes an explicit frame-acquisition policy. The Vulkan drawing engine uses `SkipIfBusy`: a busy
frame-slot fence or unavailable swapchain image drops that presentation attempt instead of waiting on the simulation/UI thread.
The default API policy remains blocking so validation tools and any later screenshot adapter must opt into disposal deliberately.
Resize, shutdown, device recovery, and explicit readback remain allowed synchronization points.

An acquired frame also has an explicit abandonment path. Recording or upload failures reset the unfinished command buffer,
consume the already-signalled image-available semaphore with an empty queue submission, and invalidate the swapchain so the
acquired image is released without presenting incomplete contents. Small palette tables are marked dirty again. Persistent atlas
uploads remain pending until presentation succeeds; transient glyph allocations are still retired with their recording frame.
This closes the previous failure mode where one upload-ring overflow could leave an active frame and make every later
`BeginFrame` fail.

The same ownership pass corrects swapchain teardown order: queued work completes, palette framebuffers/render-pass resources are
released, and only then are their old image views destroyed. Recreation remains an exceptional blocking boundary, never a normal
frame-hot-path wait.

The X8 validation bridge has been deleted. The direct path avoids that transfer, but paint traversal, clipping, command generation,
first-use sprite/glyph rasterisation, and transparency-depth estimation remain CPU work on the caller. Immutable visual packets
and newest-frame replacement isolate backend submission on the render worker; worker-built viewport ranges and GPU culling remain
later steps. Performance claims require the integrated Turbo matrix rather than architecture alone.

The next bounded handoff slice removes backend staging addresses from direct first-use texture uploads. Each direct frame now owns
the indexed upload bytes and atlas metadata it recorded; Vulkan allocates the per-frame staging slice and copies those bytes only
inside submission. Cache entries remain transactional until successful presentation, so cancellation can retry the payload.
This makes the direct command stream movable across a thread boundary without retaining caller or mapped-Vulkan pointers. It does
use per-snapshot atlas residency leases plus a serialized mailbox for palette updates, resize/present-mode changes and readback.

The direct path now sends compact resolved LightFX commands to an exact integer Vulkan compute accumulator rather than
rasterising a logical-screen intensity map on the CPU. Its fixed dispatch shape, `R32_UINT` atomic/image usage, and requested extent
are capability-gated; unsupported devices retain the CPU-intensity fallback without failing renderer initialisation. Compute-only
capture updates clipping dimensions without allocating the legacy full-screen CPU light buffers. These are ownership and traffic
improvements, not a measured TPS claim: GPU/CPU pixel parity, cancellation, resize, MoltenVK fallback, SDR screenshots and HDR10
output still require the interactive renderer validation matrix.

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

The next source-only vehicle slice hoists nonlinear speed scoring to the sampled-train boundary. Every car in one train has
the same signed train velocity and ride rating profile, but the previous per-car path independently evaluated the normalized
`speed^1.5` excitement curve. The recorded profile has 582,586 per-car accumulation calls and no more than 279,531 sampled
train-head calls, so this removes at least 303,055 duplicate square-root evaluations per 500 ticks. Absolute fixed-point
velocity, display-scale speed, and the resulting speed tuple are now derived once and passed through the stable car-order
loop. Per-car G forces, track features, shelter, local context, transport comfort, accumulator order, and integer rounding
remain unchanged. This is synchronous stack state with no persistent cache, invalidation rule, or park-format field. A TPS
gain is not claimed until the clean checksum-matched benchmark is repeated by the integration owner.

The clean `239a80ec4f` baseline also exposes a time-window stability problem: two 2,000-tick warmups reached `271.755` and
`266.432` TPS, but the immediately following 2,000 measured ticks fell to `188.809` and `185.541` TPS with matching
`7cd4d475...` checksums and `5.167`/`5.208` millisecond median ticks. Rating-owned collections are bounded: recent samples
hold twenty entries, directed legs cannot exceed the station-pair limit, active samples reuse cleared vehicle slots, and
station-platform state is bounded by ride/station capacity. One unnecessary transition still appeared as slow transport
rides began publishing physical legs: every completed sample deactivated its service, removed all endpoint references and
available-service membership, and forced the next guest query to reconstruct them.

Transport service invalidation now marks an existing transport service dirty in place. Validation still occurs before every
public cache view and still rebuilds directed journey costs from the latest rolling measurements, but unchanged station
endpoints keep their spatial-index references and an available ride keeps its sorted available-service membership. Status,
type, deletion, station-count, entrance, exit, and direction changes are detected deterministically; unavailable or reused
ride slots still deactivate fully. Repeated sample invalidations within a tick coalesce behind the existing forced-validation
flag. This adds one transient dirty bit, no saved state, and no relaxation of fare or route freshness. The follow-up benchmark
must compare both the warmup and measured windows; improving only the first window does not resolve the reported degradation.
The early/late 500-tick profile attributes only about `5.5` milliseconds to transport planning, so this cleanup is not claimed
as the explanation for the full slowdown. Over the same windows, environment calls fell from `557,989` to `547,403` while
their instrumented time rose from `1.078` to `1.169` seconds and cold context builds changed only from `17,038` to `18,197`.
That points to worsening per-call locality or wider system pressure, not growth in rating sample counts. The leaf diagnostic
scopes used to establish that ownership are now retired from the production car loop; coarse sampler/train scopes remain.

The locality audit found that the process-wide context cache's hash did not actually include map position in its final
64-bit value. It appended fields with left shifts totalling more than the word width after X and Y were introduced; by the
final sample-kind shift, both coordinates had been discarded. Contexts on different tiles but with the same Z, ride, track
type, direction, and sample kind therefore accumulated in one `unordered_map` bucket, lengthening collision walks as more
of the park was sampled. The replacement mixes every fixed-width field before the next field is added, so X and Y affect
bucket selection throughout the run. Exact key equality, cached environment values, origin-chunk generations, runtime
accumulator caches, and all invalidation rules are unchanged. This is a locality-only change with no saved state or ordering
effect; the integration benchmark must confirm the late-window per-call reduction.

The validated hash change lifts the 2,000-warmup checkpoint to `408.992` and `410.762` TPS with matching `7feaeed9...`
checksums, but a 4,000-warmup/2,000-measured run still falls to `302.507` TPS. Correct bucket distribution therefore exposed
the cache's second age-dependent property: it was an unbounded node map cleared only on whole-cache reset. Long rides can
continue discovering exact tile/Z/track/direction keys for several thousand ticks, retaining every node and periodically
rehashing even though the immediate accumulator cache needs only its current key.

The first fixed-table trial used 16,384 sets and four ways, capping the shared cache at 65,536 exact environments. It was
rejected: combined with the scheduler slice it measured `363.284` TPS after a 2,000-tick warmup and `300.332` TPS after a
4,000-tick warmup, versus the hash-only cache's `408.992`/`410.762` and `302.507` TPS. The small table sacrificed the hot
window to replacement churn and produced no meaningful late-window gain.

The 65,536-set revision, holding at most 262,144 environments, recovered the hot window at `412.822` TPS after a 2,000-tick
warmup and improved the late window to `314.538` TPS after a 4,000-tick warmup. The checksums were `0ed4276c...` and
`8f43babc...`; their difference from earlier candidates is expected because the concurrently validated path-reuse change
legitimately changes guest trajectories. This is the accepted bounded-cache fallback: it removes node allocation, rehash pauses,
and unbounded retained memory without the smaller table's hot-window churn, although its late result remains `5.462` TPS below
the 320-TPS target.

A final 131,072-set, 524,288-entry trial reached `415.031` TPS after a 2,000-tick warmup but fell to `289.782` and `290.332` TPS after a
4,000-tick warmup, with the same `0ed4276c...` and `8f43babc...` checksums as the 262,144-entry candidate. The slight hot-window
gain therefore came with a severe late-window regression: the larger fixed footprint hurt working-set locality more than its
additional retention helped. It was rejected, and the validated 65,536-set, 262,144-entry table remains the accepted design.
Each set fills empty ways and then replaces them in deterministic FIFO order without mutating replacement state on a hit. A slot
is accepted only when the complete key and current origin-chunk generation match, so eviction can only cause a cold recalculation
of the same environment; it cannot substitute another key or reuse stale map context.

The fully integrated checkpoint also reuses committed boarding queue-end goals, computes walking speed once per transport
planning pass, and resets the network delta clock when a client/server session starts after offline ticks. Two clean standard
windows reach `418.561` and `412.871` TPS with matching `0ed4276c...` checksums and `2.197`/`2.212` millisecond medians. Two
deeper 4,000-warmup windows reach `301.747` and `308.582` TPS with matching `8f43babc...` checksums and `3.247`/`3.185`
millisecond medians. The standard Turbo 320 acceptance target is therefore exceeded repeatably; the deeper window is recorded
separately and remains below target rather than being conflated with the prescribed benchmark.

The deep-window ownership audit found no unbounded scheduler or pathfinding work queue. Shared-route worker jobs finish behind
a synchronous barrier; publication replaces the previous node index and fields; the persistent footprint is bounded by the
current path nodes and concrete targets. Transport station query buffers clear or assign on every use, and the entity spatial
worklist is deduplicated, drained, and recycled each tick. In contrast, the measured park remains live: ordinary guest
generation runs every logical tick, every active marketing campaign adds another generation chance, and `PeepUpdateAll` walks
the complete guest and staff lists in stable id order. A later warm-up therefore measures a later and potentially larger park,
not merely an older process.

The command-line benchmark now captures state immediately before and after its timed interval. It reports inside/outside guest
counts, major guest-state counts, active transport commitments, staff and vehicle counts, plus shared-route node, target,
direction-entry, and single-ride-target totals. Both traversals are outside the measured interval and read transient state only;
they do not add a branch or counter to a simulation tick and cannot affect the checksum. The next paired standard/deep run can
therefore distinguish population growth from route-cache growth before another hot-path change is justified. A stable route
footprint alongside a rising guest count would make per-guest profiles the relevant comparison; changing route totals or a stale
cache state would instead direct the audit back to topology invalidation and preparation cadence.

The resulting snapshots rule out both explanations as sufficient for the remaining deep-window gap. The standard window began
with 14,084 guests and reached 486-493 TPS; the 4,000-warmup window began with 14,845 guests and reached 312-322 TPS. Staff,
vehicles, and the shared-route footprint stayed fixed at 6,663 nodes, 543 targets, and 3,611,346 direction entries. Active
transport commitments rose from 139 to 343, but matched profiles show they are not multiplying route work: `ChooseDirection`
calls changed only from 12,914 to 12,981, while `PlanTransportRoute` calls fell from 901 to 796 and used less than five
milliseconds in the deep 500-tick capture.

The slowdown instead affects unrelated work at nearly unchanged call counts. `PeepUpdateAll` rose from 779,709 to 1,271,305
microseconds; its `UpdateAction` calls rose only 1.4% and `CheckForPath` calls 2.6%, while their time rose 44.7% and 52.8%.
Vehicle update time similarly rose from 401,291 to 613,342 microseconds even though eligible rating-sample calls fell from
257,214 to 248,335. This is evidence of worsening time per operation across the process, not a superlinear committed-route
algorithm. Plausible causes include a wider live cache-line working set, cache pressure from progressively occupied fixed
tables, sustained CPU frequency, or external contention; the profiles do not distinguish them.

The benchmark therefore also reports the mean tick time for the first and last quarter of its already-recorded chronological
samples and their percentage change. This adds no clock read, counter, or branch to a simulation tick. A deep run that is slow
but flat from its first quarter points toward state/working-set pressure already established during warm-up. Continued decay
inside the measured interval instead points toward process-duration effects such as ongoing cache occupation, frequency, or
contention. No guest, vehicle, pathfinding, RNG, or update-order semantics are changed on the strength of a broad per-call
slowdown with flat algorithm counts.

The 8,000-warmup run starts with 16,104 guests, retains the same shared-route footprint, and reaches 301.822 TPS. Its mean tick
time rises only 2.8% from the first measured quarter to the last, confirming that most of the slowdown is already established
during warm-up rather than caused by an unbounded collection growing through the measured interval. The additional 1,259 guests
since the 4,000-warmup snapshot accompany only a small further TPS decline; they do not explain the earlier standard-to-deep
cliff.

One fixed peep-loop overhead remains removable without changing that state. In the matched 500-tick captures,
`Peep::UpdateAction` and `Peep::CheckForPath` together enter more than 5.1 million times. Their temporary leaf profiler scopes
have now established that call counts rise only 1-3% between windows. A disabled scope still constructs its guard and performs
an acquire load of the global profiler-enabled flag on every entry; an enabled scope adds timing and atomic bookkeeping that
also distorts these extremely small leaves. Those two leaf scopes are retired, while the enclosing `PeepUpdateAll` scope keeps
the authoritative subsystem total and the materially expensive `ChooseDirection` and transport-planning scopes remain. This
removes only diagnostic machinery: path-check cadence, action animation, entity order, RNG, positions, and network state are
unchanged. A clean benchmark is required before assigning a TPS gain.

The next deep-window candidate keeps the accepted 65,536-set capacity but compacts each four-way set. Validity and the FIFO
cursor are set-level bytes; keys and spatial generations are contiguous metadata arrays, followed by the environment payloads.
On the 64-bit layout this reduces an expected 360-byte set to 312 bytes, taking the fixed table from about 22.5 MiB to 19.5 MiB;
an upper-bound `static_assert` prevents later fields from silently losing that footprint. A shared lookup now reads validity and
keys together and touches a generation/payload only for the matching way. The probe records both the matching way and first
invalid way in one pass, so a cold full-set miss no longer performs a second key scan and a third invalid-way scan after building
the context. The recorded 19,627 cold builds per 500 ticks imply up to 157,016 avoided slot visits when those sets are full.
Complete-key equality, exact origin generation, first-invalid insertion, hit-stable replacement state, and deterministic FIFO
on a full miss are unchanged. Whole-cache clear now resets only each set's mask and cursor; ignored payload bytes cannot be read.

The same slice memoizes longitudinal G within one sampled-train call, keyed by the exact previous velocity stored by each car's
independent accumulator. Current train velocity is immutable for that call. Equal previous velocities reuse the pure integer
calculation; a new accumulator still skips it, and a divergent previous velocity recomputes it before that accumulator is updated
in the original car order. The attribution capture contained 582,586 car accumulations and no more than 279,531 train-head calls,
so a fully warm capture with shared histories contains roughly 303,055 or more duplicate absolute-value/division calculations;
first-history or divergent samples reduce that estimate. No G value, accumulator field, tick cadence, or publication order changes.

A fresh 500-tick profile after the 4,000-tick warmup reports `613,342` microseconds in `VehicleUpdateAll`, `592,749` across
485,000 `Vehicle::Update` calls, `359,231` across 248,335 live-rating sample calls, and `321,462` across 191,241 sampled-train
accumulations. Cold local-context construction is `158,414` microseconds across 16,242 calls, leaving rating accumulation as the
measured vehicle bottleneck. Per-car leaf scopes had already been removed, so this capture deliberately provides no current
per-car count; the earlier 582,586 figure belongs to an older attribution run.

The next exact train-boundary hoist prepares the G-force speed-coupling context once in each of those 191,241 sampled-train
accumulations. Normalized speed and the coupling numerator depend only on the train's shared speed and the ride profile; the raw
vertical, lateral, airtime, and longitudinal force curves remain car-specific. The prepared overload applies the identical
two-stage integer divisions to each component, preserving their rounding boundaries. The existing direct scoring entry point now
delegates through the same pure preparation helper, and focused tests cover half, baseline, double, stopped, and clamped inputs.
Sample order, accumulator values, local-context caching, RNG, replacement state, saved state, and publication order are unchanged.

The next exact rating slice adds a one-entry G-force score memo at that same train lifetime. Its hot key is only the finalized
three-integer `(vertical, lateral, longitudinal)` tuple; the sampled profile and prepared speed-coupling context are immutable for
the memo's enclosing train call. An exact hit avoids the force curves, four three-component profile scales, and final speed-
coupling arithmetic. A miss invokes the existing pure scorer at the same point in car processing and replaces the one entry.
The fresh capture supplies 191,241 train accumulations but deliberately has no per-car leaf count; the older 582,586-car capture
shows the multi-car opportunity but is not subtracted from the fresh number. Focused coverage compares memo hits, misses, signed
longitudinal keys, severe forces, and replacement back to the direct prepared scorer. Car iteration, accumulator-history updates,
integer rounding, rating totals, RNG, publication, and save state are unchanged.

The validated post-hoist checkpoint reaches `493.690` and `494.169` TPS in two 500-tick measurements after the standard
2,000-tick warm-up, with matching `c9af7010...` checksums and `1.986`/`2.004` millisecond medians. Two measurements after a
4,000-tick warm-up reach `319.695` and `328.639` TPS with matching `02e17cf2...` checksums and `3.087`/`2.992` millisecond
medians. Their first-to-last-quarter trends are only `+2.5%` and `+2.8%`, confirming that the later park state starts slower
rather than accumulating a runaway cache during the 500 measured ticks. An additional 8,000-warm-up population-pressure
window begins with 16,104 guests and reaches `301.822` TPS (`3.272` millisecond median, `+2.8%` trend) with checksum
`d7909f27...`. The shared route footprint remains exactly 6,663 nodes, 543 targets, and 3,611,346 direction entries in all
three windows. The prescribed standard target is cleared by more than 50%; the deliberately later live-population window is
reported separately and honestly remains 18.178 TPS short of 320.

The next population-pressure locality candidate keeps the accepted 65,536-set, four-way cache capacity unchanged and compacts
only its shared payload. `LocalContextScore` exposes thirteen 32-bit values, but excitement, intensity, nausea, and aggregate
vertical interaction are exact deterministic formulas over the other nine component values. The fixed table therefore stores
those nine components plus shelter and reconstructs the four aggregates through the same finalizer used by cold construction.
Each active accumulator still owns the full public environment in its one-entry runtime cache, so repeated same-key vehicle ticks
perform no reconstruction. The expected shared payload falls from 56 to 40 bytes and each set from 312 to 248 bytes, reducing the
fixed table from 19.5 MiB to 15.5 MiB without changing capacity, hashing, generation checks, first-invalid insertion, or FIFO.
Focused coverage forces a shared-table hit through a second runtime cache and compares every reconstructed score field and shelter.
This is a candidate locality gain; no TPS claim is made until the deep population-pressure window is repeated.

The accumulator-owned environment cache now also retains the local-context score after profile scaling and before speed scaling.
On an unchanged environment, scoring mode, and profile coefficient, the hot path performs a validity check and two scalar key
comparisons instead of repeating the tracked/boat context decomposition and three coefficient divisions. Per-tick speed scaling
still executes with the original component-by-component multiply/divide order, so speed changes retain identical rounding. Installing
a rebuilt or shared-hit environment invalidates the prepared score; an unchanged origin generation retains it. Boat-hire mode and
the original, unclamped profile coefficient are explicit keys, while `clear()` invalidates both environment and prepared-score state.
The state remains accumulator-local and runtime-only, so park serialization, sample cadence, totals, and publication are unchanged.
The older capture's 582,586 environment calls and 19,627 cold builds establish repetition but do not distinguish runtime hits from
shared lookups, so no hit rate or TPS improvement is inferred until the integration benchmark is repeated.

### Game-speed transition audit

Changing speed does not invalidate simulation caches, rebuild render resources, restart audio, or alter the outer real-time tick
accumulator. The action changes `gGameSpeed` and invalidates only the toolbar. Entering fixed-frame Turbo restores a preceding
frame's interpolated positions when necessary; a change discovered at the end of a simulation tick is already at authoritative
positions and only discards its unused snapshot. Leaving Turbo begins with the empty tweener. Presentation audio is sampled on the
last completed logical update of a batch, network clients remain server-tick-driven, and the renderer's Turbo draw accumulator
controls only presentation cadence.

One transition latency was real: UI actions are queued outside update code and execute from `GameActions::ProcessQueue()` near the
end of the first logical update, after `gameStateTick()` has already sized its batch from the old speed. Reducing ordinary Turbo
could therefore run seven more updates, and reducing debug speed 8 could run 127 more, before returning to messages and input.
The offline batch now snapshots its starting speed and stops immediately after an update changes it. The existing early-batch
audio fallback samples that final completed state. Network server and client batching is deliberately untouched. No update is
interrupted, and action order, tick order, RNG, cache state, saved state, and per-tick accumulator values are unchanged; the next
outer iteration resumes at the new cadence. The frame layer now also stops consuming accumulated scheduler ticks as soon as that
action changes the desired fixed/variable mode. An in-tick transition discards its unconsumed snapshot at authoritative positions
and marks the frame fixed immediately; the post-input mode check still restores any state interpolated by an earlier frame when a
mode change originates elsewhere. Entering offline Turbo still consumes the current presentation credit when that old variable
frame was already scheduled to draw. Leaving Turbo no longer treats the mode change itself as presentation credit: if the 15 FPS
Turbo cadence did not schedule that fixed frame, the authoritative state waits for the immediately following unthrottled variable
frame instead of forcing one full paint and then painting again on the next outer iteration. Input, background completion, window
updates, action processing, audio, and the completed tick remain on the original frame. This changes no logical update or action
boundary; it only prevents catch-up and presentation work from continuing through a stale frame mode.

Variable-frame catch-up now also coalesces interpolation snapshots. When a delayed frame contains multiple 25-millisecond
scheduler ticks, only the final tick can supply endpoints to the one draw at the end of that frame. The first intermediate tick
restores any positions tweened by the preceding frame and clears those old endpoints; intermediate ticks then run at authoritative
positions without repeatedly scanning every visible guest, staff member, and vehicle. `PreTick`/`PostTick` populate and compact
the tweener only around the final drawable tick. A speed change to fixed-frame Turbo before that point exits with an empty tweener;
a change on the final tick discards the captured pre-tick positions without constructing endpoints that fixed mode cannot consume.
The number and order of scheduler ticks and logical updates, final entity positions, interpolation endpoints, RNG, audio sampling,
action timing, and checksum state are unchanged. Ordinary variable frames which remain variable retain the existing path exactly.

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
Concrete typed lists now use their registry-owned membership table as the type invariant and dereference the corresponding
entity slot directly. This removes both the out-of-line `TryGetEntity()` call and the repeated runtime entity-tag comparison
from every guest, staff, vehicle, litter, or effect step. At the validated 16,104-guest population, the primary guest loop alone
corresponds to roughly 8,052,000 dereferences in 500 ticks, before staff, vehicles, effects, and secondary list traversals. Mixed
per-tile queries retain their checked `as<T>()` path because one spatial bucket intentionally contains different entity types.
The previously latent postfix-iterator implementation also returns the pre-increment iterator as the standard contract requires.

The same concrete-type resolution now applies to direct registry lookups, not only typed-list iteration. `EntityId` has exactly
65,535 addressable values plus its null sentinel; a compile-time assertion binds that identifier contract to the registry size.
`GetEntity<T>()` therefore handles null inline, indexes the owning slot directly, and compares `T::cEntityType` before casting.
`TryGetEntity()` likewise performs its one bounds check inline. Polymorphic `GetEntity<Peep>()` and `TryGetEntity<Peep>()`
retain `as<Peep>()` because both guest and staff tags are valid. This removes the out-of-line base-registry lookup and concrete
`EntityBase::is<T>()` specialization from linked train/car traversal, cached train-head iteration, queue links, and other repeated
ID resolutions. The available 500-tick profile contains 485,000 `Vehicle::Update` calls, while the earlier per-car attribution
contains 582,586 rating accumulations before other station and track-chain walks; those figures establish a high call surface but
are not treated as an exact lookup count or a TPS claim. Null results, wrong-type rejection, entity order, mutation visibility,
RNG, saved state, and checksums are unchanged.

Typed membership no longer allocates one `std::list` node per live entity or follows allocator-scattered next pointers.
Each of the thirteen entity types owns a fixed 65,535-bit membership table (8 KiB per type) and a count. Add and remove are
constant-time bit operations; iteration scans contiguous 64-bit words and still visits live entities in ascending id order.
The iterator retains numeric current and preselected-next ids rather than a container node: removing the current entity is
safe, an entity removed before its turn is skipped, and an insertion before the preselected next id is not newly visited. This
matches the former list cursor's pre-increment behavior while making removal-ahead handling defined. Insertions beyond that
cursor can still become visible when a later step selects them. That is the mutation contract for synchronous guest, staff,
vehicle, and miscellaneous updates; a flat vector or tick snapshot would not preserve it. The registry's read-only range API is unchanged in role, so
map cleanup and vehicle-head collection consume the same ordered view. No persistent entity data, save format, network field,
RNG call, or update order changes. Focused coverage exercises self-removal, removal ahead, and additions on both sides of the
preselected cursor.

Increment now consumes that preselected id directly when its membership bit is still live, instead of asking the bitset to find
the same id a second time. Only removal-ahead falls back to the next-word search. An attempted adjacent-id fast path added another
branch to every increment and regressed the measured guest loop, so it was rejected; the accepted form keeps the common path to
one membership test and one registry-slot dereference.

The same ordered bitmap now owns free entity ids instead of mirroring availability in a reverse-sorted vector. A 16-word summary
identifies non-empty 64-id blocks, so lowest-id allocation and sparse typed iteration skip empty ranges without vector search,
insertion, or erasure. Exact-id imports and mutation-visible ascending iteration preserve their established deterministic order.
A representative 20,000-cycle, 256-removal churn benchmark retained the same allocation checksum across five runs, ran
2.02-2.06 times faster, and reduced allocator storage from 131,070 to 8,328 bytes. This is a focused allocator result, not a TPS
claim; EverythingPark remains the integrated acceptance benchmark.

The integrated checkpoint after entity allocation, persistent Vulkan TTF residency, and audio lifecycle cleanup reached
631.114 TPS over 2,000 measured EverythingPark ticks after a 2,000-tick warm-up, with checksum
`93d0bf66ac3305c3000000000000000000000000`. A separate hidden five-second Vulkan/VSync run produced 142.727 FPS, a 7.279 ms
median frame interval, and a 10.808 ms maximum while the later live-park population lowered logical throughput to 224.228 TPS.
The two measurements preserve the distinction between the simulation ceiling and refresh-paced presentation cost.

The 1-in-128 guest/staff maintenance schedule now tracks the next matching ordinal while traversing those same stable lists.
This replaces a mask-and-compare on every peep with an equality check and one addition only when a scheduled ordinal is
reached. The selected ordinal sequence is still `currentTicks & 127`, then every 128th combined guest/staff index; periodic
maintenance still runs immediately before that peep's ordinary update, so RNG and mutation order are unchanged.

Two exact empty-work guards remove further ordinary-guest call overhead. `GuestUpdatethoughts` exits immediately without a
write when the first thought type is `none`, so the caller now detects that empty list before entering the scan. The
non-movement Easter-egg hook is called only when at least one of its five controlling flags is present: purple, pizza,
contagious, ice cream, or joy. The joy flag is essential because that branch consumes `ScenarioRand()` and can start the joy
animation; an earlier four-flag experiment omitted it and was correctly rejected after a checksum divergence. With the complete
mask, every guest that can enter any internal branch still calls the unchanged function at the same point, while ordinary guests
skip five flag tests and a function call. The 16,104-guest snapshot corresponds to roughly 8.05 million guest updates per 500
ticks, but the empty-thought and non-movement rates were not separately measured, so no avoided-call or TPS total is invented.

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

### Speed-transition and long-run cache-churn follow-up

The speed action itself only assigns `gGameSpeed` and invalidates the toolbar. The expensive transition is the next scheduler
batch: Turbo expands one 40 Hz scheduler tick into eight ordered logical updates. Presentation audio previously ran inside
each logical update, repeating the full vehicle candidate scan/sort/channel reconciliation, visible-guest crowd
spatialisation, and weather-channel update eight times before the renderer or audio device could observe an intermediate
state. Those wall-clock presentation functions now sample the final state once after each scheduler batch. Normal speed still
runs one audio pass per logical update; simulation-side vehicle sound state and scenario-random consumption remain at their
original per-logical-tick cadence.

Offline logical updates also stop at the network boundary before calling `Network::Tick`, `PostTick`, or `Flush`. Those
functions cannot produce offline state, but `Tick` still sampled the platform clock and all three crossed the context/network
facade and rechecked mode on every logical update. Turbo multiplied that empty work by eight. Network clients and servers
retain the original per-logical-tick calls and ordering; mode is sampled around the network tick before the existing
server/client simulation branches. Successful client and server startup now reset the network tick clock immediately before
returning, including reconnect through `BeginClient`. The first connected tick therefore measures only time since startup
rather than the whole offline interval; cooldown decay retains the existing minimum-one-millisecond delta.

The 3-to-4 and 4-to-3 boundary audit found no duplicated accumulator or audio work. The scheduler accumulator remains in
25-millisecond units because speed changes the number of logical updates inside a scheduler tick, not its wall-clock deadline.
Presentation audio still runs once at the final state of the selected batch. One presentation edge was avoidable: a speed action
could change the desired frame mode inside `Tick()`, after `RunFrame()` had selected its old path. A delayed frame could then
consume more accumulated ticks with variable-frame snapshots after entering Turbo, or with the fixed-frame loop after leaving it;
tweener cleanup and the first uncapped draw also waited for the next outer iteration. Both loops stop at the mode boundary; an
in-tick variable-to-fixed transition synchronises immediately, and the post-input check covers changes from other frame paths.
Entering Turbo presents the authoritative transition state once and starts a fresh 15 FPS interval; leaving Turbo draws
on the current fixed frame only when its existing Turbo presentation cadence was due. Otherwise the next outer iteration is
already variable and unthrottled, so it supplies the first draw without an empty restore/reset or a back-to-back full paint. The
toolbar action still invalidates only the toolbar class.

The remaining transition hitch was spatial-index work accidentally caused by presentation interpolation. `Tween()` and
`Restore()` moved temporary display coordinates through the authoritative `setLocation()` path, so a cross-tile interpolation
queued a remove/reinsert even though the simulation had already committed the entity's real bucket. Presentation movement now
shares the established map validation, sprite-rectangle update, and old/new screen invalidation but preserves spatial-index
membership. The subsequent logical tick therefore starts from the same authoritative bucket without a redundant dirty pass.
Focused coverage moves a guest across tiles for interpolation, verifies that neither the dirty bit nor worklist changes, and
restores the authoritative coordinates.

One bounded entity cost remains necessary before a variable-frame simulation tick: positions interpolated by the preceding draw
must be restored before simulation, and visible entities are captured in case the frame remains variable. The tweener previously
retained every visible guest, staff member, and vehicle after `PostTick`, even when its position was unchanged, then rediscovered
that fact in both `Tween` and a later transition `Restore`. `PostTick` compacts the parallel entity and position arrays to moved
entities only during ordinary interpolation. If the tick instead enters Turbo, fixed mode cannot consume interpolation endpoints,
so the pre-tick arrays are cleared without the `PostTick` scan or a second position restore. Fixed frames never populate the
tweener, and the reverse 4-to-3 transition does not call empty restore/reset passes.

The speed action itself is still processed through the deterministic game-action queue. `gameStateTick` selects its batch size
before the queued action executes in the first logical update, then the existing batch-speed guard returns without running the
remaining updates selected at the old speed. The frame-mode guard retains any additional wall-clock accumulator for the next
outer iteration rather than consuming it through the stale path. Moving the action ahead of that first logical update would alter
action-to-tick timing and remains deliberately excluded. When the mode changes on the final drawable variable tick, the tick has
already left every entity at its authoritative post-tick position. The frame therefore discards the unused pre-tick snapshot
directly instead of building `PostTick` endpoints only to restore and clear them before the fixed-frame draw. This removes one
visible-entity scan and the moved-entity restore from the 3-to-4 transition without changing any completed logical update, entity
position, interpolation formula, input order, audio cadence, RNG, checksum state, or multiplayer behavior.

A separate clean-binary observation exposed long-run drift rather than only transition latency. After a 2,000-tick warm-up,
two runs reached `271.755` and `266.432` TPS; immediately following 2,000-tick intervals fell to `188.809` and `185.541` TPS
with matching `7cd4d475...` checksums and `5.167`/`5.208` millisecond medians. Static tracing found that the rolling 128-tile
wide-path maintenance shared the global invalidation epoch with reverse destination fields. A changed derived wide flag could
therefore trigger a full park graph freeze and rebuild of every target even though reverse fields do not consume wide or
thin-junction flags. `MapTopology` now keeps a path-connectivity epoch separate from its general/chunk generations. Wide-flag
maintenance still invalidates local topology chunks for live thin-junction classification, while only structural path,
banner, queue, and entrance changes invalidate reverse fields or guests' committed transport routes. Worker ownership, the
synchronous barrier, epoch validation, stable target order, and serial publication are unchanged.

These changes are source-only until the integration owner repeats the checksum-matched long-interval benchmark. Acceptance
requires both restored sustained TPS across consecutive intervals and no regression in speed-change input/audio
responsiveness; the baseline figures above are diagnostic evidence, not a claimed gain.

The process-lifetime worker pool is deliberately not applied to `PeepUpdateAll` or `VehicleUpdateAll`. Guest updates consume
the shared scenario RNG and synchronously mutate queues, rides, path history, thoughts, and entity lists in entity-id order.
Train-head updates mutate shared station, block, collision, and ride state. A safe parallel version needs immutable input
snapshots plus deterministic per-entity command buffers and serial conflict resolution; dispatching the current mutating
methods as independent jobs would introduce races and checksum changes. Until profiles justify that larger staged-update
architecture, orchestration work remains serial and optimization stays at exact lookup or batch-presentation boundaries.

A fresh process-lifetime pool audit finds no unused steady-state simulation batch to move safely. File-index construction,
object parsing, viewport columns, and profiler capture already submit independent indexed work outside the logical update
contract. The one simulation consumer is shared-route preparation: each target builds into its own pre-sized field slot from
one immutable frozen graph, the caller waits at the barrier, rechecks the topology epoch, and publishes all fields serially.
It runs only when topology or the target set changes, not once per guest or once per tick in the stable EverythingPark window.
Entity visibility tests are read-only in isolation, but parallel tween capture would first construct and publish a stable pointer
snapshot every drawable tick; its allocation and barrier cost are not an unambiguous win over the current contiguous scan.
The latest available 500-tick profile attributes 1,246,935 microseconds to `PeepUpdateAll`; its measured `UpdateAction`,
`CheckForPath`, and `ChooseDirection` children total 276,957 microseconds, leaving most guest-state work distributed across
serial state handlers. `VehicleUpdateAll` uses 1,508,328 microseconds, of which the excluded live-rating child accounts for
1,235,234 microseconds. The remaining vehicle motion changes shared track, station, collision, and ride state in train order.
Parallelising either residual therefore requires the immutable input snapshots, per-entity command buffers, and deterministic
serial conflict resolution described above; the pool itself cannot make those mutable loops safe.

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

The final leg-materialization audit removes one remaining local rescan without adding another cache. A measured edge's
twenty-sample history is averaged once, then the same temporary accumulator supplies both distance/time/speed measurements and
distance-weighted comfort/decoration. Freshness hashing and dirty-service graph construction previously invoked those consumers
independently and traversed the ring twice per edge. Their integer divisions, clamps, fallbacks, directed ordering, fare formulas,
and journey composition are unchanged; through-rider publication and park serialization are outside this temporary lifetime.

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

The duplicate-search audit found that item 5 was not actually satisfied by the implementation. The first direct walking
`ChooseDirection` result was stored in an `optional`, but `optional::value_or(ChooseDirection(...))` eagerly evaluated the
fallback before selecting the stored value. Every transport-planning refresh where walking remained selected therefore ran
the same shared-field lookup or bounded heuristic search twice and discarded the second result; on thin junctions it could
also mutate path history twice. An explicit `has_value()` branch now reuses the first direction without invoking the fallback.
Concrete park exits, ordinary or advertised ride targets, and station-less facilities such as first aid all reach this same
`GuestPathFindToDestination` bottleneck. Outside entry gates and off-map spawns use it for walking, while transport planning
remains disabled until the guest is inside the park.

The same audit removed two smaller repeated calculations within transport planning. Guest walking speed is now derived once
per planning pass and passed to every direct, boarding, exit, and spatial-radius estimate; every distance retains its original
integer division and rounding. After a route is committed, the first station-bound search stores the resolved outer queue end
in `PathfindGoal`. Later junctions reuse that goal only while the route remains active and its path-connectivity epoch matches,
rather than rescanning the entrance and walking the queue chain each time. Connectivity edits retain the existing clear and
replan path, and `setTransportRoute()` still clears the prior final-destination goal before the first boarding search.

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

The multi-entrance follow-up retains those already-computed 32-bit distances only for owners with more than one distinct
target. Ordinary rides and facilities now choose the first shortest reachable entrance from the guest's exact path node rather
than the geometrically closest station; unavailable fields retain the old heuristic fallback. Multiple park entrances use the
same ranking, while a leaving guest keeps its selected entrance. Synchronized adjacent stations deliberately retain their
guest-ride-count rotation. The selected concrete target still enters `GuestPathFindToDestination`, so live banner masks,
foreign-queue rules, junction history, transport planning, and deterministic direction commitment remain centralized. This is
a route-smartness correction; focused validation covers disconnected targets, unequal path lengths, multi-target publication,
distance release when an owner returns to one target, and the null-ride park-entrance group. Synchronized-station rotation and
leaving-park commitment remain on their established branches, and repeated simulation checksums remain unchanged.

The final shared-bottleneck audit batches multi-target distance comparison. Ride and park-entrance selection now resolves the
current exact path node once, then probes every ordered candidate field at that node; the previous loop repeated the same binary
node-index lookup per entrance. The returned candidate index preserves station/park order for equal distances. Connectivity-epoch
validation, synchronized-station rotation, geometric fallback, live `ChooseDirection` checks, advertised and facility targets,
and multi-leg transport-service lookup remain unchanged. The candidate buffers are thread-local scratch, not guest or saved state.
No TPS gain is claimed until the checksum-matched benchmark is repeated.

The substantial shared-lookup follow-up replaces the published sorted source-node vector with a deterministic flat open-address
index. The frozen graph keeps its sorted vector while workers build edges and fields; after the path-connectivity epoch passes the
publication check, the main thread inserts complete packed XYZ keys into a power-of-two table capped at 50% load. Park exits,
ordinary and advertised rides, first aid/facilities, transport station walking legs, and multi-target comparison all use this same
lookup. Collisions use exact-key linear probing and cannot affect direction or tie order. Stale epochs are rejected before lookup;
publication replaces the index with its matching fields and reset releases it. This trades roughly one additional compact entry per
path node for removing a binary search from every shared-field direction query. No TPS gain is claimed before the required benchmark.

Root validation of the combined cache, exact-distance routing, guest hot path, typed membership, rating memo, scheduler, and
renderer slices is deterministic. After the platform-seat, visual-invalidation, and speed-transition follow-up, two independent
2,000-warm-up/500-measurement windows reach `590.596` and `588.405` TPS with `1.670`/`1.684` millisecond medians and the same
`6088da79...` checksum. Two independent 8,000-warm-up windows reach `382.847` and `403.916` TPS, with `2.468`/`2.406`
millisecond medians and the same `405ee291...` checksum. Their initial/final populations, guest-state counts, transport-route
counts, staff, and vehicles also match, and both deep runs exceed Turbo 320 by more than 19%. All warm-up depths
retain the same 6,663-node, 543-target, 3,611,346-direction-entry, 3,611,346-distance-entry, 517-single-target route footprint.
The retained 32-bit distances add about 13.8 MiB before vector/index overhead. Focused coverage passes 204/204 and the full
suite passes 509/509. Release core, data/shaders, CLI, game, and the Vulkan UI build with
warnings treated as errors. The hidden Windows Vulkan lifecycle test also passes on the real device with the Khronos validation
layer enabled, covering swapchain creation, uploads, compute/CPU LightFX input, presentation, resize, frame rotation, and exact
indexed readback.

The final profiled deep window keeps checksum `ceeaa856...` and reaches `363.146` TPS with instrumentation enabled. Across 500
ticks, `PeepUpdateAll` accounts for 912.247 ms, `VehicleUpdateAll` for 406.739 ms, live train rating samples for 188.307 ms,
local-context scoring for 82.226 ms, and 10,882 `ChooseDirection` calls for 35.733 ms. Transport planning itself is 4.761 ms
across 486 calls. Compared with the preceding deep profile, exact shared distances and the surrounding locality work reduce
direction-selection cost without moving dynamic fare, crowding, or weather policy into the topology cache. The remaining broad
peep/vehicle loops still require deterministic staged-mutation architecture before they can safely use the worker pool.

The July 13 integrated follow-up removes presentation work from intermediate logical ticks without changing authoritative tick
order. Ride-music listener projection and channel selection now run only for the final tick in an offline fast-forward batch;
intermediate ticks advance the deterministic saved cursor. Vulkan can acknowledge dense viewport damage before the caller pays
for repeated world-to-screen projection, while serial tracking preserves sparse invalidations submitted after an older frame.
Paint sessions clear only quadrants that actually received entries. The Turbo pacer retains phase across bounded wake and batch
overrun jitter, drops debt after a complete missed interval, and waits on one monotonic simulation/presentation deadline rather
than re-entering the SDL loop at kilohertz rates.

On the fixed 1,998-warm-up/3,600-measurement EverythingPark Vulkan/VSync fixture, the exact source baseline was `262.825` TPS
at `144.043` FPS. The combined result is `360.032` TPS at `144.013` FPS, a `37.0%` throughput increase, with the identical
`1322b2e30a3c8e84000000000000000000000000` checksum and exact initial/final guest, state, transport, staff, vehicle, and route
cache counts. Headless simulation remains above target at `558.302` TPS with checksum
`0d01d80aa473aa1a000000000000000000000000`. A longer 10,800-tick integrated run is deliberately retained as the next gate:
it held `144.016` FPS but averaged `347.182` TPS as the park grew from 14,084 to 17,231 guests. The fixed fixture now reaches
360, but the sustained growing-park requirement is not complete. The next architectural slice remains deterministic staged guest
and vehicle mutation or an immutable retained visual snapshot; scheduler tuning alone cannot create that remaining headroom.

Expected result: destination-aware guests share expensive topology work, while dynamic transport and crowding costs remain
cheap overlays.

## Fork-wide consolidation checkpoint

The first post-feature quality pass removes 2,877 net lines of C++ and replaces defensive polling with mutation-owned
state. Transport services no longer recompute a freshness hash over every ride, station, and directed rating leg once per tick.
Ride construction, entrance/exit placement, rating publication, status changes, and breakdown transitions mark the indexed
service dirty; the next reader rebuilds it once. Dynamic queue time, pricing, weather, and crowding remain live inputs and are not
folded into the service graph.

The platform registry is now a fixed ride/station index with one explicit FIFO, route targets use one sorted index, and topology
publication uses one connection resolver. EntityRegistry uses one typed membership path. Vulkan pipelines share result handling, shader lifetime,
fixed-state construction, and upload staging. The real-time audio callback no longer gathers unused five-second telemetry or
selects AVX2 versus scalar code per speaker. Its channel-completion path now also removes the unused secondary ownership flag and
combines mixing with completed-channel removal in one traversal. These changes target both instruction count and maintainability.

The completed follow-up function audit reduces the live fork delta from 31,911 to 30,000 net C++ lines. It removes duplicate save-repair
and unloading traversals, redundant topology and transport-exit queries, one-use state wrappers, per-frame Vulkan readback
bookkeeping, repeated audio configuration reads, and duplicated test setup while preserving distinct regression cases. Transport
journey searches use fixed station-limit scratch arrays, platform reservations use one FIFO guest-to-slot relationship,
and routing fixtures preserve their 49 cases through shared lifecycle-owned setup. The 30,000-line target is met.

The combined Release build is warning-clean and all 520 tests pass. Two independent headless runs, each with 2,000 warm-up ticks
and 2,000 measured ticks, completed at 633.359 and 630.453 TPS with the identical final checksum
`93d0bf66ac3305c3000000000000000000000000`. The hidden five-second Vulkan/VSync run reaches 143.746 FPS and 249.159 logical
TPS with 7.296 ms median and 11.995 ms maximum frame intervals. The pure-simulation and integrated presentation measurements
therefore both retain their separate gates.

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

- actual warmed Turbo TPS and completed presentation FPS are reported together against the 360/144 target;
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
