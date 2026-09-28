# Independent history addendum: reviewer three

Date: 2026-09-20. I read all 1,265 lines of `docs/archive/performance-320-tps-refactor-history.md`, in four contiguous ranges covering lines 1-330, 331-660, 661-990 and 991-1265. No other reviewer vote, author preference, linked document or project history was consulted. My initial vote remains unchanged and sealed. No build, test, game execution or production edit was performed.

## Revised assessment

| Question | Initial decision | After reading history |
| --- | --- | --- |
| Plan A | approve prototype | unchanged |
| Plan B | approve prototype | unchanged |
| Preferred first implementation | Plan B, bounded mixed-scene proof | unchanged |

Confidence remains moderate for the representation choice. Confidence is higher that the shared publication and CPU-work constraints are necessary. The history makes B a more economical first experiment because significant resident-sprite infrastructure and terrain compute work already exist. It does not demonstrate that B's proposed scalar depth is correct for mixed world content. The historical terrain path explicitly excludes mixed scenes until common ordering is proven, and EverythingPark remained on the adapter at that checkpoint. Nor does the history show a successful original-art geometric-world proof that would invalidate A's central uncertainty.

The history identifies the problem more precisely than either short plan: this is a replacement of CPU semantic paint preparation and costly state capture on top of an existing Vulkan renderer. It is not a first migration of rasterization, atlas residency or backend submission to Vulkan. Rebuilding those foundations would spend effort without attacking the measured bottleneck.

Historical rows must remain historical evidence. The document records multiple scheduler generations, park states, warmup counts, monitor rates and superseded implementations. Its opening explicitly calls it a journal rather than an architecture contract. Reported low GPU times and large simulation/integrated gaps motivate the work but establish neither today's baseline nor a 4K guarantee.

## Failed experiments to avoid repeating

1. **Full or repeated population/map capture disguised as asynchronous rendering.** Full-map-per-redraw reached 139.278 TPS / 36.406 FPS. Compact peep capture with dense spatial indexing reached 295.455 / 110.713; sparse publication still reached only 297.101 / 75.101. Full entity clones, compact variants and typed-page bulk capture also lost against live-state baselines. A frame-ahead clone of entity/ride storage consumed roughly 3 ms on the main thread, and viewport restriction did not rescue it. Publish small records at owning mutation/update boundaries; do not scan/copy the whole population or reconstruct a spatial index to feed a worker.
2. **Retained pixels and stale prepared paint lists.** Damage acknowledgments can refer to a different generation from the displayed world, leaving clipped vehicle trails. Retained prepared lists subsequently failed construction ghosts even after other visual gates passed. Retain world data, not old screen pixels or mutable paint lists. Fully redraw the visible scene from one pinned generation.
3. **Overlays and fabricated depth bands.** The terrain compute checkpoint specifically refuses a terrain-only depth interval in mixed scenes. Entities cannot be an unordered overlay. Both candidates must demonstrate shared visibility among terrain, vehicles, peeps, track and scenery before category migration is called successful.
4. **Scheduler changes presented as rendering speedups.** Old 15 FPS Turbo rows reached attractive TPS by drawing less; uncapped single-tick scheduling later repeated enough scene work to lose gains. Preserve honest completed logical ticks, display cadence and input responsiveness together. More admitted work or a faster present call is not the objective.
5. **Extra scheduling/classification passes without data ownership.** A peep queue split increased update time and changed deterministic state; an on-ride deferred task queue also lost throughput. The parallel rating phase regressed when it bypassed the shared locality cache. Independent simulation needs explicit ownership and deterministic commit order, not dispatching current mutating loops to workers.
6. **Assuming more retained memory always helps.** A larger fixed rating cache improved the early window but worsened the late one. Shared CPU caches, GPU publication staging and revision queues must have measured working-set and memory budgets. GPU-resident data does not make CPU-side duplication free.

## Existing foundations to reuse

The journal reports a latest-frame mailbox and dedicated render worker; three fence-protected Vulkan frame slots; a reusable persistently mapped upload ring; transactional, generation-aware indexed sprite atlas residency and descriptors; compact instanced sprite passes; palette/remap/mask/blend/weather and SDR/HDR composition; explicit asynchronous indexed readback; and a compute LightFX path. Reuse their ownership and failure-handling contracts, verifying current implementation before relying on historical status.

The nonblocking `SkipIfBusy` admission policy, explicit frame abandonment, transactional upload retry, packet leases and fence-based retirement are particularly relevant. A superseded frame must not lose persistent scene updates or free in-flight assets. Resize, park load, readback and shutdown already have distinct lifecycle boundaries to preserve.

Also reuse applicable chunked map publication, dense active-map row-major terrain records, deduplicated detailed/distant sprite sets, GPU camera-dependent culling/selection/compaction and indirect drawing. The 1,024-record stable block approach is an existing concrete technique, not evidence that all world categories share its ordering. Typed entity pages, stable IDs, mutation-owned invalidations, independently waitable pool groups and priority queues are useful foundations. Dormant entity snapshots are an adapter/reference resource; the history explicitly warns against enabling their broad capture as the new hot path.

Reuse exact fixed-tick benchmark infrastructure, separate headless diagnostics, checksums and boundary population/route snapshots, renderer timing, visible-window checks, and frame-interval reporting. The history already contains fast-coaster trails, title-demo transitions and construction ghosts as discriminating correctness regressions.

## Required revisions to both plans

- Begin with a concrete inventory of current retained resources and missing ownership boundaries. State that implementation extends the existing Vulkan service instead of duplicating it. Historical proposed structures are not automatically present or accepted.
- Define a complete revision contract covering map, entities, rides, object-derived catalogs, weather/global flags, construction ghosts and camera capture. No GPU expansion or background stage may depend on live mutable registries. Specify epoch reset, stable-ID reuse, unload, cancellation, overflow, screenshot and fence retirement behavior.
- Require mutation/update-owned compact publication, changed static ranges and bounded latest-complete consumption. Avoid new whole-population capture passes, full per-frame dynamic payload copies, and camera-dependent CPU expansion. Measure CPU emission, worker time and transferred bytes independently. The historical 64-byte pre-expanded `WorldSpriteRecord` proposal must not silently replace the current requirement that shaders select frames and graphical parts from compact authoritative state.
- Make complete visible-scene redraw explicit, with generation pinning and snapshot age telemetry. Add fast vehicles across several ticks, edit/ghost movement, park resets and title transitions to the first correctness sequence. Slow-renderer stress must verify simulation progress and bounded queues without presenting partial revisions.
- Name original palette/remap/mask/zoom-art and destination-dependent effects as existing behavior to preserve through shared composition. Neither changing world representation nor a successful opaque depth test closes those requirements.
- Retain the 3,000-tick minimum but add the existing 2,000-warmup/12,000-measured EverythingPark growing-population gate at actual 3840x2160. Fix camera, display refresh, build, park state and benchmark configuration; report visible presentation pacing, snapshots, population endpoints and checksums. Keep throughput, profiled attribution and simulation-only ceiling rows distinct. Do not promote an old short-run figure to current acceptance evidence.

For **A**, add a specific depth-asset derivation/authoring contract and measured per-family cost before scaling. Reuse existing projected sprite sampling and composition wherever possible. The dense terrain compute path can supply resident inputs and visibility groundwork, but does not prove per-pixel geometric depth. Explicitly test whether geometric visibility fixes B failures without changing intended pixels or adding another CPU conversion stage.

For **B**, explicitly distinguish the proposed constant-depth model from the history's deterministic painter-key ordering pipeline. Bounds/grouping order is not established as a world-coordinate scalar merely because compute compaction preserves an existing order. Define an early counterexample gate and a bounded choice among additional splitting, GPU ordering and selected depth-bearing surfaces. Do not reopen the legacy painter as a concealed per-frame dependency or claim mixed-scene support from uniform terrain success.

The smallest experiment remains the shared mixed-scene visibility proof in my sealed vote, now implemented through this existing service and extended with a moving construction ghost and park-generation replacement. It should answer the representation question before either plan incurs a broad entity-publication or asset-conversion migration.
