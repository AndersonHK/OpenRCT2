# GPU-resident state rendering and performance evaluation

Status: source-backed design/audit, 2026-09-19, `screen_capture`, with root-owned
smoke measurements and upload accounting recorded below. GPU-resident world
state, incremental updates, shader-side graphical decisions/transforms/offsets,
low CPU and memory-bandwidth cost, VSync pacing and substantial large-park TPS
improvement are explicit user requirements for the final architecture. Completing
the current screen-command renderer alone does not satisfy them.

The simulation remains authoritative on the CPU. The GPU owns a retained
presentation representation and derives visible graphics from it. The final
steady-state contract is changed world/entity/object state plus small camera,
time, viewport and UI changes; it is not a full image or a regenerated CPU world
draw stream for every frame. Camera-only movement must not republish unchanged
world records or sprite pixels. Exact frozen-software output, the documented
exception policy and manual sample review remain acceptance gates throughout.

## Current implementation: useful foundations and remaining CPU work

These are source observations, not measured cost rankings. Relative links name
the files and symbols inspected so later changes can be reviewed against them.

| Area | Source-backed behavior | Implication and measurement needed |
| --- | --- | --- |
| Frame admission | [VulkanDrawingEngine.cpp](../src/openrct2-ui/drawing/engines/vulkan/VulkanDrawingEngine.cpp), `CanBeginFrame`, `BeginDrawQueued`, `EndDrawQueued`; [GpuFrameMailbox.h](../src/openrct2-renderer/gpu/GpuFrameMailbox.h), `LatestFrameMailbox`: one pending newest packet; vectors recycle; stale visual work can be superseded. | Already bounds queue growth and avoids some unnecessary recording. Count admission denials, superseded packets, CPU work spent on superseded frames and generation age. A dropped frame must never drop a required state delta in the retained design. |
| Main window traversal | `VulkanDrawingEngine::PaintWindows` calls `WindowUpdateAllViewports`, clears provisional strip batches if necessary, then `ViewportBeginPresentationFrame` and full-screen `WindowDrawAll`. Dirty rectangle invalidation and `CopyRect` do not retain the canvas. | Every admitted frame still traverses the UI/world. Measure traversal, provisional work discarded, per-viewport recording time and emitted world versus UI commands separately. The legacy target allocation carries pointer offsets; it is not uploaded as a framebuffer. |
| World paint preparation | [Viewport.cpp](../src/openrct2/interface/Viewport.cpp), `ViewportFillColumn`, `CreatePreparedViewportFrame`, `ViewportPaintColumn`: column sessions call `PaintSessionGenerate` and `PaintSessionArrange`, then emit graphics commands. | Parallel CPU preparation still performs tile/entity traversal, graphical rule selection and painter arrangement. Native base terrain does not remove the remaining column traversal or other categories. Profile generation, arrangement, traversal and recording separately. |
| Map publication | [Map.cpp](../src/openrct2/world/Map.cpp), `ConsumeMapPresentationChanges`, `MapPresentationSnapshot::Apply`; [PresentationScene.cpp](../src/openrct2/drawing/PresentationScene.cpp), `MapPresentationPublisher`: revisioned changes, immutable copy-on-write chunks, background apply. | Reuse these authoritative mutation boundaries. Count changed tiles/chunks, captured bytes, chunk copies, reset publications and jobs' critical-path waits. Snapshot pointer-array copying is not the same as copying all tile payloads. |
| Entity publication | [EntityRegistry.cpp](../src/openrct2/entity/EntityRegistry.cpp), nested `EntityStorage::Capture`: copies every allocated concrete-type page with `memcpy`. `EntityPresentationPublisher` currently calls `CaptureStorage`, not the incremental `Apply` path. [EntityPresentationSnapshot.cpp](../src/openrct2/entity/EntityPresentationSnapshot.cpp), `BuildCapturedStorage`: clears full lookup arrays, walks occupied slots and rebuilds spatial buckets. | This normal path is bulk capture despite the incremental API/documentation also present. Count allocated-page bytes versus live/changed visual bytes, lookup clears, rebuilt bucket entries and publication CPU time. Do not relabel it incremental or change shared software publication to improve an apparent renderer benchmark. |
| Entity dirty-state foundation | [EntityVisualLifecycle.h](../src/openrct2/entity/EntityVisualLifecycle.h), handles `(epoch,id,generation)`, presence/transform/bounds/appearance/topology/lighting/interaction flags; `EntityRegistry::ConsumeEntityVisualChanges`; `EntityPresentationSnapshot::Apply`. | Suitable identity/lifetime foundation, but payload currently carries concrete entity bytes. Audit complete dirty marking for every visual mutation before routing a compact GPU-specific state stream through it. GPU records must not contain host pointers or whole concrete simulation objects. |
| Asset residency | [GpuTextureCache.cpp](../src/openrct2-renderer/gpu/GpuTextureCache.cpp), `GetOrLoadImageSprite`, `SealFrame`, `RetireFrame`, invalidation; [GpuAtlas.h](../src/openrct2-renderer/gpu/GpuAtlas.h): resident indexed assets, descriptor indices, generations, pin/retirement leases, explicit zero coverage. | Images are already cached rather than reuploaded unconditionally. CPU still resolves/binds assets per recorded draw and seals leases per frame. Measure hits, misses, decode time, atlas pixel/coverage/descriptor bytes, pinning work, eviction churn and dynamic glyph/preview traffic. Retained records need durable asset-generation references independent of a screen-command packet. |
| Native surface state | [GpuCommandDrawingContext.cpp](../src/openrct2-renderer/gpu/GpuCommandDrawingContext.cpp), `DrawWorldSurfaceScene`: caches converted chunks and sprite sets; scans chunk references, active sprite sets and resolves variants. Admission rejects entities, smoothing and maps whose base surfaces cannot be independent. | A retained foundation, but narrow: the flat entity-free fixture is not representative of a large operating park. Measure rejection reasons, admitted world categories, unchanged chunk scans, variant resolution and actual uploaded deltas. Dense record count includes reserved boundary records; those must remain inactive after the map-border fix. |
| Native GPU implementation | [VulkanWorldSurfacePipeline.cpp](../src/openrct2-renderer/vulkan/VulkanWorldSurfacePipeline.cpp), `Initialise`, `Record`, `DiscardPendingUploads`; [world_surface_compact.comp](../data/shaders/vulkan/world_surface_compact.comp), [world_surface.vert](../data/shaders/vulkan/world_surface.vert). Revision-different chunks copy into device-local source buffers. Compute chooses variants, projects/culls, performs stable block compaction and emits indirect draw commands; vertex shader applies sprite offsets and projection. | Reuse this approach, including abandonment recovery. Compute still scans the entire declared surface domain each render, calls `buildRecord` twice and reconstructs projection again in the vertex stage. Measure traversal/visible ratio before choosing spatial hierarchy, cached projected records or smaller dispatch regions. No result has established this as the bottleneck. |
| Frame command upload | [VulkanRectPipeline.cpp](../src/openrct2-renderer/vulkan/VulkanRectPipeline.cpp), `Record`: copies full rectangle/sprite batches into the upload ring; line/weather/transparency pipelines use corresponding per-frame inputs. [VulkanDevice.cpp](../src/openrct2-renderer/vulkan/VulkanDevice.cpp), `UploadRing`: host-visible mapped memory, flush when needed. | Count bytes written, flushed and explicitly copied by category, separately from device-local memory traffic. A GPU vertex fetch directly from host-visible memory is not automatically represented by transfer-command byte counters, and logical byte counts are not measured PCIe bandwidth. |
| Transparency | [GpuTransparencyDepth.cpp](../src/openrct2-renderer/gpu/GpuTransparencyDepth.cpp), `MaxTransparencyDepth`: CPU event sorting and range maximum; [VulkanBackend.cpp](../src/openrct2-renderer/vulkan/VulkanBackend.cpp), `Submit`: depth peeling/composition. | Measure CPU overlap analysis, actual overlap depth, pixels shaded and GPU peel/composition time. Ordering is observable; replacing it with approximate order-independent blending is not permitted. |
| LightFX | `VulkanDrawingEngine::CaptureLightFx` resolves light commands/palette; `Backend::RecordLightFx` prefers GPU light rasterization but can upload CPU intensity images. | Measure and expose actual route, state/light bytes and fallback intensity bytes. Final retained design must use light state/falloff assets on GPU for supported operation. Existing uniform-light parity alone is insufficient; spatial/temporal LightFX qualification is separate work. |

The native surface ABI currently uses 40-byte source records, 64-byte generated
visible records and 200-byte sprite sets, with 256-record upload chunks and
1,024-record compute blocks ([GpuCommandStream.h](../src/openrct2-renderer/gpu/GpuCommandStream.h)).
One dirty chunk therefore transfers 10,240 source bytes even for a single changed
tile. `Initialise` allocates for the maximum 1001×1001 map: approximately 40 MB
source storage, 64 MB output storage and 0.82 MB sprite-table capacity, before
atlas, canvases, scratch and allocation overhead. These are arithmetic capacity
estimates, not observed allocations/resident VRAM. Reusing one shared service for
main and auxiliary targets must avoid duplicating a maximum-sized world per
thumbnail; see [offscreen service design](vulkan-offscreen-service-design.md).

## Required retained-scene migration sequence

Each stage is independently reviewable. Temporary CPU-command coexistence is a
migration aid; it cannot remain the final world rendering architecture.

1. **Instrument and pin the baselines.** Add category-tagged CPU timers, bytes,
   record counts, publication age and native/fallback coverage before replacing
   code. Pin large-park inputs, cameras, profiles, hardware/build receipts and
   sample tick checkpoints. Keep the frozen oracle and current Vulkan command
   path for comparisons. Establish present cost and the attainable CPU budget.
2. **Define retained identity and update ownership.** Introduce renderer records
   with stable scene epoch, category, slot and generation; static asset/object
   IDs refer to immutable versioned tables. CPU publishes create/update/delete
   state transactions from completed authoritative ticks. Record base sequence
   and target sequence; acknowledge GPU-applied revisions after successful queue
   submission/completion as appropriate to lifetime. Camera packets may replace
   each other, but state updates must be coalesced against the last applied
   generation or reconstructed from the latest owned state. Retain tombstones
   and old resources until referencing work completes. Test drop, cancellation,
   resize, park reload, rapid slot reuse and device recovery explicitly.
3. **Separate static visual rules from changing state.** Upload indexed sprite
   assets, sizes, offsets, zoom variants, remap/blend/coverage tables and object
   visual-rule data once per asset generation. Retained instances contain world
   coordinates, material/object IDs, orientation, visual state, animation clock
   inputs and dependency references. Conversion on object load/edit is allowed;
   per-camera/per-frame CPU sprite selection is not the target. Keep glyph
   rasterization as an asset-generation event and UI text/layout changes as
   dirty retained UI state, not as reason to redraw/reupload the whole park.
4. **Generalize terrain within measured parity.** Keep the successful flat-state
   delta path, fix border semantics, then add explicit terrain height/slope,
   material, edges, smoothing neighbours, water and fences. Dirty dependencies
   include neighbouring tiles whose appearance changes. Move selection and
   offsets into shader/table rules matching integer rounding, clipping, zoom
   and palette semantics. Use spatial chunks to dispatch visible candidates;
   do not require CPU screen projection to discover them. Do not simply remove
   admission guards for unsupported interactions.
5. **Add static object families incrementally.** Start with a bounded scenery
   family, then paths/additions, walls, large scenery, track/support/tunnel
   components, entrances and overlays. Create/update instance state on actual
   world/object changes. Encode parent/child/attached-sprite rules and bounding
   relationships explicitly. Derive image variant, offsets and draw records on
   GPU. Each family requires all rotations/zooms, clipping, overlap, remap,
   covered-zero, construction preview and visibility-flag cases before CPU
   generation is disabled for that family.
   A bounded intermediate representation is a per-object world-space paint
   template generated on object load or structural mutation, stored on GPU and
   instantiated by stable world records. Enumerated direction/zoom/animation
   alternatives and graphical dependency data can bridge existing paint rules
   without translating every rule into shader source immediately. Templates
   must remain camera-independent, with shader-side variant/offset selection;
   refreshing templates or sorting the whole world on the CPU every frame would
   fail the requirement. Measure invalidation fan-out and template memory before
   adopting this representation for a family.
6. **Move entity presentation to compact dirty records.** Use existing identity
   generations, but audit guests, staff, vehicles and every effect type for
   transform/appearance/spawn/despawn mutations. Upload previous/current tick
   position and visual state plus interpolation fraction; compute projection,
   sprite animation selection, clothing/remap, passenger/attachment offsets and
   graphical effects on GPU. CPU continues simulation decisions. Retained
   dependency updates must cover ride/object changes affecting many entities.
   Prove replay/checksum invariance and visual parity at frozen ticks and tween
   fractions, including reversed orientation and slot reuse.
7. **Unify exact ordering and compositing.** Port the actual paint precedence
   relations and stable tie-breaking into retained sort keys/dependency data,
   then GPU ordering/compaction and indirect draws. A single depth sort of
   `(x+y,z)` is not an adequate substitute for `PaintSessionArrange`, crossing
   bounds and parent/child rules. Retain exact palette filter/blend/coverage and
   overlapping text/weather behavior. Validate adversarial interleaving and deep
   transparency, including the existing 32/33-layer cases. Plan overflow
   handling that preserves every visible primitive; no silent capacity drops.
8. **Finish retained UI, overlays, lights and auxiliary views.** Retain widget/
   text command ranges until properties, clipping, z-order or assets change;
   CPU text shaping/localization may run on text changes. Parameterize cameras
   and targets so main UI, screenshots, minimaps/previews and export use one
   resource owner and the same graphical rules. Replace remaining full intensity
   uploads and full-world CPU draw recording. Keep readback only for explicit
   consumers/diagnostics. Hit testing must preserve IDs/visibility semantics;
   decide CPU spatial queries versus targeted asynchronous GPU ID readback with
   latency tests rather than forcing a synchronous frame readback.
9. **Remove transitional paths after coverage and performance pass.** Trace
   ordinary large-park frames to show zero CPU world draw generation, zero
   unchanged-world payload upload and zero routine screen-image upload/readback.
   Delete software renderer and temporary CPU world-command routing only after
   all behavioral consumers and parity/exception reviews pass. Shared simulation
   data APIs and CPU asset decoding are not software rasterizer dependencies.

Dependency order is identity/assets/publication before retained categories;
general scene ordering must be available before independently ordered categories
can be combined. Do not claim full-world native coverage by counting a terrain
pass while the expensive rides/entities still use CPU-generated commands.

## Counters and benchmarks that determine acceptance

Existing instrumentation is useful but incomplete.
[IDrawingEngine.h](../src/openrct2/drawing/IDrawingEngine.h) `FrameTimings` reports
CPU submit/present, GPU total/upload/draw/LightFX/composite and present-call time.
Backend timings are collected from completed frame slots, not an extra per-frame
readback. Native state copies/compute currently occur after the
`uploadsComplete` timestamp and therefore contribute to the broad draw interval,
not the advertised upload interval. Add dedicated state-upload/cull/order/draw
boundaries before attributing a performance gain to those stages.

[Context.cpp](../src/openrct2/Context.cpp) integrated benchmark reports actual
logical TPS, draws/FPS, frame-interval percentiles, simulation/draw CPU time,
message/window update rates, renderer averages, state snapshots and entity
checksum. `IsPresentationDue`, `AdvancePresentationDeadline` and `CanBeginFrame`
already gate CPU frame construction at the refresh deadline and queue capacity.
`presentCallMicroseconds` measures an API call, not displayed-frame cadence or
input-to-photon latency. App draw intervals similarly do not prove compositor
presentation cadence. Hidden-window tests qualify rendering, not visible VSync.

Add bounded, aggregated counters (per frame and per completed tick, with frame/
scene IDs) for the following; no per-primitive logging in acceptance runs:

- CPU simulation, mutation capture, snapshot copy/apply, world paint generation,
  arrange/order, UI recording, asset resolution/decoding, packet sealing and
  worker submission; include allocations and bytes copied/cleared where known.
- State records created/updated/deleted, dirty chunks, entity pages copied,
  active/visible instances, emitted CPU world commands by category and explicit
  fallback reasons. Track upload payload, aligned-ring consumption and flushed
  bytes separately for state, sprite pixels, descriptors, commands, palettes,
  lighting and UI. Report initial population separately from steady state.
- GPU transfer, coarse cull, variant selection, ordering/compaction, indexed
  draw, transparency, lighting and final scaling/composite time; buffer/atlas
  occupancy, committed/resident budget when available, high-water marks and
  eviction/reset events. Logical byte counters cannot alone claim lower DRAM or
  PCIe bandwidth; use platform/GPU profiler observations for those claims.
- Admission denials, packet drops, source-tick-to-present age, queue depth,
  acquire/fence waits, actual selected present mode and displayed intervals where
  observable. Keep CPU submit latency separate from GPU work and VSync blocking.

Run three distinct lanes on the same pinned machine and monitor configuration:

1. **Correctness lane:** frozen software, current software where it still exists,
   and candidate Vulkan at identical scene/tick/camera/input states. Required
   indexed and actual final RGBA captures, exact repeatability, validation and
   every divergent/corrected triplet manually reviewed. Include large-park
   checkpoints as well as primitive/terrain/UI fixtures. Captures, validation
   layers and forced synchronous publication are excluded from performance runs.
2. **Attribution lane:** profiler/counter-enabled repeats, warm and cold assets
   separately. Stationary paused scene, camera pan/rotation/zoom with no simulation,
   normal running park, uncapped fast simulation, bursty construction/deletion,
   full asset reload and window/LightFX activity. Profile CPU+GPU critical paths
   and publication contention. Compare renderer-disabled/headless simulation as
   an upper-bound diagnostic with its different workload explicitly stated.
3. **Acceptance lane:** clean release builds, profiling/validation/captures off,
   visible foreground window, fixed drawable size and refresh rate, same game
   speed and deterministic tick interval. Compare frozen software, pre-retained
   Vulkan and retained candidate using fresh identical profiles. Use at least
   five paired trials in alternating order; warm up with fixed ticks and measure
   the same subsequent tick count, preferably long enough for 60 seconds or more
   of reference execution. Report every trial, median, spread/confidence interval,
   frame p50/p95/p99/max and actual TPS; do not hide a regression with FPS alone.

The existing CLI options in [RootCommands.cpp](../src/openrct2/command_line/RootCommands.cpp)
include `--benchmark-ui`, `--benchmark-visible`, `--benchmark-renderer`,
`--benchmark-vsync`, `--benchmark-warmup-ticks`, `--benchmark-ticks` and the
separate `--benchmark-profile` attribution lane. Preserve commands, input hashes,
asset/config receipts, compiler/shader/device/driver details, monitor refresh,
power/thermal conditions and checksums in a machine-readable run receipt.

The large-park set must include at least a dense guest/vehicle operating park,
a scenery/ride-heavy park and a broad map whose total world greatly exceeds the
viewport. Pin actual park hashes and entity/tile/ride counts before calling this
a representative result. Run VSync-on at the user's actual refresh rate and an
uncapped/VSync-off diagnostic. At VSync-on, require continued responsive UI and
presentation near the available refresh budget while simulation throughput
improves; dropping nearly all visible frames is not a TPS success. At ordinary
game speed the simulator may already meet its target TPS, so judge headroom and
frame cost there; use a pinned fast-simulation workload to demonstrate extra TPS.

Use the canonical [migration plan](vulkan-exclusive-migration-plan.md) Gate P
P01–P10 as the acceptance ledger. Its provisional pacing targets are at least
99% of steady-state frame deadlines met, p99 CPU frame preparation below 10% of
the display interval and GPU work below 80% of that interval. Report measured
display intervals, missed deadlines and p99 source-state age rather than only
application draw timing. These are provisional engineering targets, not measured
achievements or numbers supplied by the user. Lock a numeric **substantial TPS
gain** target after the pinned baseline and before claiming optimization success,
with a confidence interval excluding no gain and no material regressions in the
other representative parks. A mere no-regression or 5% regression allowance is
not the requested large-park improvement. Publish preparation-time and upload-
byte reduction targets at that same baseline checkpoint. Exact-pixel parity,
no stale/missing state and the architectural zero-unchanged-world-upload contract
remain mandatory. Memory budgets must be set per supported hardware tier after
measuring committed and resident usage rather than assuming more persistent
VRAM is always faster.

## Explicit missing evidence and next checklist

- [ ] Pin representative large parks and baseline source/build/asset/hardware receipts.
- [ ] Measure current CPU stage times, copied bytes, upload categories and actual native/fallback coverage.
- [ ] Split native upload/compute timing from the current broad GPU draw timestamp interval.
- [ ] Measure displayed VSync cadence and source-tick age; current API/draw timing is insufficient.
- [ ] Audit dirty marking completeness and compact retained schemas for each entity/world category.
- [ ] Specify ordering/dependency and overflow behavior before general native category interleaving.
- [ ] Prove no lost updates under skipped/superseded frames and no stale references across reload/slot reuse.
- [ ] Implement/qualify stages 2–8 with exact screenshots and manual divergence review.
- [ ] Record clean paired performance trials and large-park TPS gains before claiming the requirement complete.
- [ ] Remove transitional world-command and software rendering paths only after the full migration gates pass.

No large-park speedup, bytes-per-frame reduction, bandwidth reduction, displayed
VSync cadence, cross-device VRAM budget or full retained-world coverage has been
measured by this audit. Existing native-terrain pixel results establish only
their specified fixture/route, not those performance claims.

## Actionable first baseline, using existing controls

Read-only follow-up, 2026-09-19. The archived
`oracle-ui-source-02/src/openrct2/command_line/RootCommands.cpp` has the same
integrated benchmark/path controls listed below. Prefer the ordinary full UI
entry point and existing `Context::RunOpenRCT2` benchmark loop for the first
throughput comparison. A loop which manually calls public tick/paint methods
would need to reproduce scheduling, admission, tweening, message pumping and
VSync deadlines, changing the workload being measured.

The pinned first large input already exists at
`obj/vulkan-parity/reference-9a092745f3/testdata/parks/EverythingPark.park`, SHA-256
`c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`.
It is a first baseline, not a complete representative park set. Use the saved
camera initially, identical across processes, and retain the startup/end state
snapshots and entity checksum. A normal-speed or scripted-camera benchmark
requires an additional driver/hook: the existing integrated benchmark explicitly
forces **ordinary Turbo** after park load and fails if the game leaves that
speed, becomes paused/networked, or leaves the game scene.

| Existing control | Exact role/limit |
| --- | --- |
| `--benchmark-ui` | Runs the integrated benchmark through the full UI executable; rejects `--headless`. Window is hidden unless explicitly made visible. |
| `--benchmark-visible` | Requests a visible window; required for display-pacing acceptance. The benchmark suppresses audio via the normal dummy audio context and does not trap the cursor. |
| `--benchmark-renderer software` / `vulkan` | Software means the software rasterizer with hardware display. Vulkan is accepted only by a Vulkan-enabled build. Confirm the logged actual renderer. |
| `--benchmark-vsync 1` / `0` | Overrides configuration. Record both requested mode and actual Vulkan present mode when that diagnostic becomes available. |
| `--benchmark-warmup-ticks N`, `--benchmark-ticks N` | Pin identical completed logical-tick ranges. Warm-up 0 is legal; measured ticks must be positive. These take precedence over second-based settings. |
| `--benchmark-warmup N`, `--benchmark-duration N` | Second-based diagnostic alternative. Its changing end state makes it unsuitable for exact end-state comparisons without additional checkpoints. |
| `--benchmark-profile path.csv` or `.json` | Enables existing `PROFILED_FUNCTION` instrumentation only during measurement, exports on completion. This is a separate attribution run, never acceptance TPS. |
| `--user-data-path`, `--openrct2-data-path`, `--rct1-data-path`, `--rct2-data-path` | Explicit workspace profile and immutable bundled/licensed assets. Do not inherit the user's normal profile or let one renderer's run rewrite the next renderer's starting config. |

Before executing, qualify the executable/build receipt and resolve its exact
path. The accepted binary is `reference-9a092745f3/package/openrct2.exe`; inspect
its `--help` against the pinned source because its deployment revision is
separately recorded. Root subsequently qualified the ordinary build 26
`openrct2.exe` and preserved it, its shaders and its build receipt under
`obj/vulkan-parity/performance-baseline-build26`. Runs must use that immutable
snapshot rather than the mutable `bin/openrct2.exe`. Missing controls fail
qualification rather than silently changing the workload.

Prepare a new output root and two new profile directories. Copy the same seed
`config.ini` into each, pinning at least `[general]` `window_width=960`,
`window_height=640`, `window_scale=1`, `uncap_fps=true`, `multithreading=true`,
language, smoothing, weather/LightFX and scale-quality settings; record the seed
hash. Use the same settings as the representative gameplay workload rather than
disabling expensive features silently. The initial clean lane may deliberately
have LightFX off while its new spatial fixtures are qualified, with that limit
reported. Verify actual drawable extent/display refresh, not only the config.
For current Vulkan, create an isolated data overlay containing accepted bundled
assets plus receipt-matched current SPIR-V, following the checks in
`scripts/rendering/run-ui-parity.py`; do not run that pixel-capture runner for the
benchmark. Frozen software uses the unchanged accepted bundled data directory.

The first paired smoke commands are the following argument arrays, for a runner
to pass directly to `subprocess.run` with stdout/stderr to a new log. Values in
angle brackets are resolved paths, not literal inputs:

```text
<qualified-frozen-full-ui-exe> <pinned-EverythingPark.park>
  --benchmark-ui --benchmark-visible --benchmark-renderer software --benchmark-vsync 1
  --benchmark-warmup-ticks 2000 --benchmark-ticks 10000
  --user-data-path <new-workspace-output/frozen-profile>
  --openrct2-data-path <accepted-package/data>
  --rct1-data-path "D:/Games/GOG Games/RollerCoaster Tycoon Deluxe"
  --rct2-data-path "D:/Games/GOG Games/RollerCoaster Tycoon 2 Triple Thrill Pack"

<qualified-current-full-ui-exe> <same-pinned-EverythingPark.park>
  --benchmark-ui --benchmark-visible --benchmark-renderer vulkan --benchmark-vsync 1
  --benchmark-warmup-ticks 2000 --benchmark-ticks 10000
  --user-data-path <new-workspace-output/vulkan-profile>
  --openrct2-data-path <verified-current-shader-data-overlay>
  --rct1-data-path "D:/Games/GOG Games/RollerCoaster Tycoon Deluxe"
  --rct2-data-path "D:/Games/GOG Games/RollerCoaster Tycoon 2 Triple Thrill Pack"
```

These tick counts are a smoke protocol, not a claimed 60-second sample. Measure
its duration, then choose and lock a sufficiently long tick count before the
five paired acceptance trials. Include current-software as a third control if
shared simulation changes can otherwise be mistaken for a rendering benefit.
Repeat a new pair with VSync 0 for attribution; it is not a replacement for the
VSync-on claim. Add `--benchmark-profile <new-output/profile.csv>` only to new
profiling runs. Hash input/profile/executable/shader receipts before and after,
reject startup failure/timeouts and inconsistent state checksums, and retain all
trial results. Launching these explicitly visible benchmarks is intentional;
background build/helpers should remain hidden.

No capture is requested by this mode: no `SDL_RenderReadPixels`, screenshot,
diagnostic arming, final RGBA/indexed readback or pixel comparison belongs inside
the measured interval. `BeginIntegratedBenchmarkMeasurement` and completion call
`DrainFrameTimings` at the interval boundaries; Vulkan drains the worker/fences
and collects timestamp queries without copying image pixels. The boundary wait
is outside elapsed/draw measurements. Record that cost separately if studying
startup/shutdown responsiveness.

### Qualified ordinary executable and independent runner

Inspection of `src/openrct2-win/openrct2-win.cpp` and its project confirms the
ordinary Windows console `wmain` forwards directly to `NormalisedMain`, with
core/UI linked statically. No new driver or capture-instrumented UI library is
needed. The earlier proposed driver/pass-through hook is superseded. The
ordinary frozen package executable and receipt-pinned ordinary current
executable provide normal startup, dummy benchmark audio, scene scheduling and
render admission. `scripts/rendering/run-render-performance.py` stages these
into independent output directories and invokes the existing integrated flags.
No parity driver, `OraclePresent`, capture request or image readback is involved.

Only if a later phase needs controlled camera motion/normal-speed scenarios
should a separate workload control be added. A custom public tick/paint loop
would have to reproduce message pumping, scene transitions, admission, tween
phase and presentation deadlines and prove equivalence first. The normal-loop
integrated run remains the primary TPS baseline lane.

The implemented runner accepts `--mode frozen-software`, `current-software` or
`current-vulkan`; current modes require the preserved `--current-snapshot`
(default: `performance-baseline-build26/snapshot.json`). It verifies that every
snapshot artifact agrees with the successful source-stable build receipt and
hashes the complete accepted package before and after execution. Runtime and
shader copies are isolated; immutable accepted asset directories are linked.
Both original-game installations are hashed, including their file sets, before
and after. A new profile is created for each process. Default configuration is
960×640, scale 1, uncapped frame scheduling, multithreading, smoothing and
weather enabled, LightFX/day-night/HDR disabled, fixed en-US language and RCT2
theme, no autosave/edge scrolling/version-check activity. `--config-seed` pins
additional workload options identically; the final input config is retained.
The renderer and VSync overrides are explicit command arguments. The ordinary
benchmark controls Turbo speed and loads the park's saved camera.

First root-owned smoke command (execute each run serially without build/test
load; substitute only a fresh output and mode for the two candidates):

```text
python scripts/rendering/run-render-performance.py
  --mode frozen-software --output obj/vulkan-parity/perf-frozen-smoke-01
  --rct1-path "D:/Games/GOG Games/RollerCoaster Tycoon Deluxe"
  --rct2-path "D:/Games/GOG Games/RollerCoaster Tycoon 2 Triple Thrill Pack"
  --warmup-ticks 100 --ticks 400 --vsync 1
```

For the current-software and current-Vulkan commands add
`--compare-run obj/vulkan-parity/perf-frozen-smoke-01`. The default park is the
accepted EverythingPark. Default hidden mode is deliberately only a smoke
measurement. Use `--visible` for later compositor trials and compare against
equally visible references. Add `--attribution-profile csv` or `json` only for
a separate attribution lane; clean/attribution or hidden/visible comparisons
are rejected. The runner cannot approve Gate P from a single run.

The parser requires one complete terminal benchmark report, exact logical tick
count, initial/final state census and final entity checksum; it validates the
checkpoint tick delta and rounded rate arithmetic. Explicitly unavailable GPU
timers remain null with diagnostics. There is **no emitted initial entity
checksum**: initial equivalence is the recorded census plus pinned input and
warmup, not a claim of a measured full initial-state hash. `--compare-run`
reparses the retained log and rejects changed provenance, configuration,
workload, host/driver/power observation, either state census or final checksum.
It reports ratios and all frame metrics without an acceptance claim.

Runtime failures and timeouts retain `summary.json`, launch records and logs.
Host CIM queries can be denied on this machine; stderr and missing CPU/GPU
observations are recorded even if PowerShell exits zero. Configured dimensions
are distinct from actual drawable extent and selected-monitor refresh, which
remain explicitly unknown without application/OS evidence. System desktop
refresh observations, when available, do not resolve that distinction.

The first root-owned `perf-frozen-smoke-01` run was correctly rejected despite
producing benchmark metrics: its log warned that RCT1 was not linked and fallback
images would be used. The retained final profile had empty `rct1_path` and
`game_path`; Context's configuration reload replaced the paths supplied only
on the command line. This is an invalid workload, not a renderer exception or
performance baseline. Its evidence is preserved unchanged:

| Original failed artifact | SHA-256 |
| --- | --- |
| `obj/vulkan-parity/perf-frozen-smoke-01/summary.json` | `b11f582def08dccb6a05e1e86b0cab93264aa4ed5b4c48cf567b146d39e97335` |
| `obj/vulkan-parity/perf-frozen-smoke-01/benchmark.log` | `28f6113dbe1112a7e2a24339b1b6bb228829ccecb712a4ca4305f421117850d8` |
| `obj/vulkan-parity/perf-frozen-smoke-01/config-input.ini` | `13d252d64ad138c6f8389182e82f8470c19a84f4544e7d4742b54c4ce7572d06` |

The runner now writes both resolved installation paths into the fresh profile's
`[general]` section as well as passing CLI arguments. Quoting escapes backslashes
and double quotes exactly like `config/IniWriter.cpp::WriteString`. The warning
remains a hard failure; neither frozen nor current production code was changed.
Corrected qualification requires new run directories and matching asset inputs.

### What the first run can and cannot conclude

It immediately supplies clean elapsed logical TPS, CPU simulation/draw totals,
draw/FPS counts, application frame-interval p50/p95/p99/max, UI/message rates,
renderer CPU/GPU timing means and matching state checkpoints/checksums. Existing
profiler output can attribute marked CPU functions without adding source hooks.
It does **not** supply per-category upload/copied bytes, CPU world-command versus
UI counts, native/fallback proportions, publication age or GPU cull/order
subtimings; those need the proposed aggregated instrumentation. A GPU timing
sample is not image readback, but timer availability and missing samples must be
reported rather than filled with zero.

For displayed cadence, collect a separate OS presentation trace keyed to the
benchmark process and actual monitor while the window is visible/unoccluded.
No presentation-tracing utility was checked or installed by this audit; its
availability, trace overhead and interpretation remain open work. API-call
durations and app frame intervals alone cannot certify the 99% display-deadline
gate. Do not silently substitute a hidden-window run if foreground display
measurement is unavailable.

The existing headless `simulate <park> <ticks> --benchmark --warmup N` and
`--profile <path>` controls in `SimulateCommands.cpp` provide a useful separate
simulation ceiling/profile. They call `gameStateUpdateLogic` directly without
UI/presentation scheduling. Inspect that subcommand's isolated-path startup
handling before running; the previously built frozen CLI oracle uses documented
startup-path plumbing for this reason. Headless TPS is not a Vulkan speedup or
a VSync result and must not be mixed with the integrated lane.

## First qualified measurement smoke, not performance acceptance

The runner now also reads the Windows hardware registry for CPU identity when
CIM inventory is unavailable. Two matching read-only observations report an
Intel Core i9-13900K and 32 logical CPUs on this host; the registry source and
remaining CIM errors are retained in
`obj/vulkan-parity/performance-host-registry-observation.json`. This does not
recover missing selected-display/refresh observations or retrofit older receipts.
New paired runs must share this expanded host record; the comparator continues
to reject differing host evidence instead of ignoring newly collected fields.

The isolated linked-asset config correction passes all three `perf-*-smoke-02` runs. Their initial/final simulation census and final entity checksum match. [Exact receipts and reported metrics](vulkan-performance-smoke-baseline.json) pin the frozen software and ordinary pre-extraction build26 executables. Each measured only400ticks after100warmup ticks with a hidden960x640window, VSync requested, no capture/validation/profiler.

| Renderer | Reported logical TPS | Mean CPU draw including presentation | Mean GPU frame |
| --- | ---: | ---: | ---: |
| Frozen software | 152.589 | 6.954ms | unavailable |
| Current software control | 156.714 | 6.349ms | unavailable |
| Current Vulkan command path | 256.398 | 4.936ms | 0.304ms |

These1.56–2.62second single samples validate the runner and are insufficient to accept a speedup, set final budgets or certify displayed VSync cadence. They motivate measuring CPU preparation: GPU execution is small relative to the reported CPU draw interval, but that interval includes presentation and cannot yet attribute the difference to paint/packing/copies. Actual selected display extent/refresh, physical presentation trace, full initial entity checksum and byte counters remain unavailable. CIM CPU/GPU queries were denied; the receipt records this instead of substituting guessed hardware. GateP remains open.

### Uncapped smoke exposes an unresolved CPU cost

The VSync0 lane also passes workload/state checks: [three pinned receipts](vulkan-performance-vsync0-smoke.json), frozen software versus ordinary E2a build29, 100 warmup and 400 measured ticks, hidden window, no capture/validation/profile. Frozen software reports 162.081 TPS and 2.261 ms mean CPU draw; current software 164.482 TPS and 2.289 ms; Vulkan 116.554 TPS and 4.955 ms. Vulkan's mean GPU frame is 0.279 ms and its present API mean is 0.123 ms. This is a short single-run observation of worse uncapped throughput, not a statistically qualified regression measurement or a final budget.

VSync1 and VSync0 admit different draw counts (approximately 200 versus 400 here), so cross-mode TPS ratios do not isolate renderer speed. The uncapped result rules out treating the earlier VSync1 result as proof of the requested throughput improvement. Attribute CPU traversal, preparation, publication and upload work, then compare sustained matched lanes. Retained GPU state and shader graphical rules remain required; backend extraction and pixel parity alone cannot close Gate P.

### Instrumented CPU work census, build35

A separate ordinary UI profiler run advances 2,000 measured ticks after 100 warmup ticks, with VSync off and no framebuffer readback or validation. Its 2,000 draws invoke 242,000 viewport column fills, 70,664,000 tile setups, 141,328,000 entity setups and 45,160,000 surface painters: respectively 121, 35,332, 70,664 and 22,580 calls per draw. [Pinned profile and counts](vulkan-cpu-attribution-build35.json). These counts expose repeated CPU world traversal; a GPU rasterizer alone does not remove that work.

This is attribution evidence, not a clean TPS comparison. Nested, parallel profiler scopes overlap and their durations must not be summed as wall time. Instrumentation and concurrent agent staging/visual review were not calibrated. The first GPU balloon slice must remove balloon graphical preparation, while tile traversal and other world families remain explicit work for Gate P.
