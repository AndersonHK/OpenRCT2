# Independent history-informed addendum — reviewer two

Date: 2026-09-20. The initial `reviewer-two.md` remains unchanged and sealed. This addendum follows an independent, complete reading of `docs/archive/performance-320-tps-refactor-history.md` (1,265 lines, read sequentially). No other review, author preference, linked design document, or repository history was consulted. No builds, tests, game execution, or production edits were performed.

## Updated votes

| Candidate | Initial vote | History-informed vote |
| --- | --- | --- |
| Plan B: retained world sprite components | Approve prototype | **Approve prototype, unchanged** |
| Plan A: orthographic world surfaces | Reserve | **Reserve, unchanged** |

**Preferred first implementation remains Plan B's bounded mixed-scene prototype**, conditional on the revisions below. Confidence in that priority remains approximately 70%. The history increases confidence in the shared retained-publication direction and in the need to eliminate CPU paint preparation. It does not materially increase confidence that B's initial scalar ordering will work, or supply the missing depth information needed by A.

The most important change to my assessment is that the publication problem is no longer merely a generic risk: several closely related approaches were tried, measured, and rejected. Both plans should make those constraints explicit before implementation. Starting either representation with an extra full-world snapshot pass would repeat known failures.

## What the history establishes, and what it does not

The journal identifies repeated CPU semantic paint preparation as substantial work beside a small measured GPU duration. It also records real improvements from retained map chunks, owned publication, and worker scheduling. This supports both candidates' decision to keep world state resident and perform visual evaluation on the GPU.

It does not establish a completed universal GPU visibility model. In particular, the dense-terrain compute checkpoint explicitly admits uniform terrain-only generations and leaves EverythingPark on the legacy adapter until mixed categories share correct ordering (around lines 333–367). That is a useful implementation foundation and an honest coverage limit. It is not evidence that an anchor-based scalar solves mixed sprites.

The journal's term “orthographic renderer” also does not prove Plan A. Its described production end state includes instanced orthographic quads and painter keys; the document does not demonstrate broad reconstruction of depth-bearing artwork or exact surface visibility. Conversely, the earlier proposal for CPU-stable painter keys is not proof of Plan B's new scalar hypothesis. These representations must still be discriminated experimentally.

The archive describes multiple epochs, changed schedulers, removed prototypes, proposed stages, and different benchmark intervals. Its figures are evidence about the stated experiments, not a current checkout performance claim or an apples-to-apples ranking of the two new plans. The opening compute checkpoint reports 335.156 TPS and 144.005 FPS over 12,000 measured ticks, but the history does not establish that this qualified actual 3840×2160 output. None of these rows closes the new 4K requirement.

## Prior failures to avoid

1. **Full-map cloning and frame-ahead legacy-state copying.** A full-map-per-redraw prototype fell to 139.278 TPS / 36.406 FPS; the retained map update was reported around 6 microseconds before offloading. Later, copying legacy entity/ride state cost roughly 3 ms before command recording; viewport restriction still lost to synchronous paint (lines 104–113 and 1128–1133). Neither plan should rebuild a legacy-compatible world to make background painting possible.

2. **Extra population scans and rebuilt spatial ownership.** Compact peep copies plus dense indexing reached 295.455 TPS / 110.713 FPS, and an update-loop capture with sparse publication reached 297.101 TPS / 75.101 FPS. The journal attributes about 270 microseconds to compact population capture alone and identifies memory-bandwidth competition. Full, compact, incremental, and typed-page entity captures also failed their contemporary baselines; a deferred on-ride queue did not recover the loss (lines 115–135). Mutation-owned compact state is the starting point. “Incremental” or “on a worker” is insufficient unless the whole producer and consumer cost is reduced.

3. **Damage repair across mismatched generations.** Fast vehicles could leave square-clipped fragments when damage acknowledgement and displayed snapshots described different generations. Retained-canvas restore/store was deleted. The later correctness baseline redraws the full visible frame from one pinned generation (lines 1145–1158). Retained records are appropriate; retained screen pixels are not a shortcut to revive.

4. **Cached prepared paint lists that omit transient state.** A generation-change redraw passed coaster/title checks at 343.822 TPS / 143.975 FPS but later failed construction ghosts. Removing the lazy scene restored correctness while dropping to 306.934 TPS with substantial CPU preparation (lines 1197–1203). Construction, ghosts, selections, auxiliary views, and lifecycle changes must be covered by ownership and invalidation contracts, not postponed as incidental polish.

5. **Unordered category overlays.** Terrain cannot reserve its own depth interval while guests, vehicles, and track are composed elsewhere. The terrain checkpoint explicitly refuses this shortcut. Both candidates need one valid cross-category visibility model in the first overlapping fixture.

6. **Worker dispatch layered on unsafe or expensive classification.** The peep queue split increased measured loop time and changed deterministic state. Other passages explain shared RNG and mutable guest/train dependencies. Render-worker isolation does not authorize parallelizing those simulation methods. Reuse the pool only behind ownership boundaries; do not add another population classification pass to feed it.

7. **Locality losses hidden by apparently beneficial caching.** Smaller rating tables churned; the larger 524,288-entry trial hurt the late window despite a small hot-window gain. Bypassing a shared rating memo also amplified worker work. These are not renderer comparisons, but they directly warn against assuming more retained data or more jobs must help. Account for working-set size, producer copies, shared CPU bandwidth, and growing-park behavior.

8. **Misleading acceptance through cadence or benchmark selection.** Earlier high TPS coexisted with about 13 FPS because rendering was deliberately throttled. Short or earlier-state results differed from growing-population windows. Hidden-surface production counts are not proof of visible display cadence. Do not exchange frames for TPS or use a headless result as the integrated outcome.

## Infrastructure to reuse

The history reports the following foundations. Their current exact implementation and eligibility must be checked before coding; this addendum does not claim to have independently revalidated all of them in production source.

- The shared Vulkan service, dedicated render worker, latest-only mailbox and admission gate, three fence-protected frame slots, mapped upload ring, and resident indexed atlases/descriptors. Reuse transactional asset residency and leases rather than introducing a second backend or upload manager.
- `SkipIfBusy`, explicit abandonment after recording/upload failure, retryable palette/atlas updates, swapchain teardown ordering, and explicit screenshot/readback paths. These are part of correctness under cancellation, not optional housekeeping.
- Indexed sprite/remap/mask composition, transparency and weather passes, SDR/HDR presentation, persistent glyph residency, and compute LightFX. Neither candidate needs to restart palette/effect implementation from nothing; changes to world visibility still require parity checks against those passes.
- Immutable generation/frame pinning, map-load epochs, copy-on-write map chunks, entity generations, typed entity storage, ordered active views, and owner-maintained mutation worklists. My initial scoped source inspection also found generation and retained-balloon contracts. Reuse the ownership mechanisms; do not enable every historically dormant clone path merely because it exists.
- Dense active-map terrain records, revisioned changed chunks, deduplicated sprite-set catalogs, GPU camera-dependent selection/culling, stable compaction, and indirect draws, as reported by the terrain checkpoint. Preserve their explicit mixed-scene eligibility limit until the prototype proves broader visibility.
- The process-lifetime pool with independently waitable groups and foreground/normal/background priorities. Publication must not take workers from latency-sensitive current-frame work without measurement.
- Fixed-tick benchmark phase boundaries, population/cache snapshots, checksums, tail timings, renderer/GPU timings, visible/hidden modes, and the historical visual regression cases. Reuse the measurement framework while specifying the new resolution and candidate scope explicitly.

## Revisions required in both plans

1. **Add an existing-foundation inventory before the first implementation step.** State what is reused, what remains a migration adapter, and what each prototype actually replaces. “Define publication and asset ownership” currently risks restarting work the history already did.

2. **Require owner-emitted changes without graphics preparation.** Simulation owners publish compact authoritative state or mark compact changed records at their existing mutation/update boundaries. GPU stages derive frames, components, geometry and visibility. Do not copy the archive's historical CPU `WorldSpriteRecord`/painter-key preparation literally if it reintroduces per-frame semantic graphics work. Initial reset scans and asset/catalog compilation need explicit amortized costs; normal frames and camera motion must not scan the population or map on the CPU to rebuild graphics.

3. **Name the complete coherence domain.** One revision must cover map, entities, rides, loaded-object-derived catalogs, weather/global state, and transient construction/ghost state. It needs epoch/tick identity, stable IDs/generations, immutable lifetime through GPU use, and coherent handling of skipped or superseded publications. Data freshness and screen damage are separate concepts.

4. **Make complete visible-frame redraw the baseline.** Retain state and assets, clear/recompose visible targets, and avoid regional damage repair. Camera capture, interpolation and picking must refer to the same admitted generation. An old frame may remain displayable while simulation advances, but active frame data must never mutate underneath it.

5. **Specify the independence and overload contract.** State which thread owns each mutable registry, where publication commits, whether simulation ever waits for render capacity, and how consumers skip to the newest complete revision. A background Vulkan submitter alone does not prove independently advancing simulation or responsive UI during a long tick. Exercise renderer stalls and publication backlog, and measure snapshot age and memory bounds.

6. **Preserve resource and effect correctness from the existing backend.** Include transfer/compute/indirect/vertex/image barriers, frame-fence lifetimes, overflow behavior, failed-frame recovery, object unload and reset, and explicit destination-dependent palette/transparency composition. Stable tie handling must not depend on raw atomic append order. Prototype fallback must be visible in telemetry; a result rendered through legacy painting does not count as evidence for the candidate. Vulkan-exclusive release must handle capacity limits without silently dropping records or relying on the removed software path.

7. **Strengthen qualification to retain the historical hard cases.** Keep the requested 4K and minimum 3,000 measured ticks, and add the existing 2,000-warm-up/12,000-measured growing EverythingPark interval as a stronger comparison where applicable. State actual refresh rate, visible display pacing, identical park/camera/settings, initial/final populations, checksums, early/late time trends, total capture/copy/publication CPU, discarded work, uploads, GPU stages, and snapshot age. Profile attribution separately from clean acceptance runs. Historical 310/130 floors or 360/144 targets should be labeled historical, not silently substituted for the user's current acceptance contract.

8. **Avoid promising simulation parallelism as a renderer side effect.** Lower rendering contention can improve integrated TPS. Further gains beyond the simulation ceiling require their own deterministic staged-mutation work. Do not alter simulation order, batching, or RNG to make a renderer benchmark look better.

## Candidate-specific revisions and next experiment

For **B**, explicitly distinguish its scalar-depth proposal from the history's bounding-box painter, CPU-stable keys, and terrain-only key order. The first experiment must use real cross-category overlap and contain a failure rule for exploding decomposition or GPU ordering work. It must not graduate on balloons over a separately painted world or terrain-only compaction. Reuse resident art and compute plumbing, but retain the adversarial fixture and per-pixel explanation requirement from my initial vote.

For **A**, add a depth-data acquisition and maintenance contract: where each family's depth comes from, how rotation/animation/zoom variants remain aligned to indexed coverage, how unusual/custom objects are covered, and how authoring effort is counted. Reuse the same publication, assets, effects, and frame infrastructure as B. “Orthographic” in the history is not a successful depth-reconstruction experiment. Restrict its initial competing representation to the same hardest overlaps so the cost of obtaining correct depth can actually be compared.

The first discriminating experiment remains the shared small overlapping scene from the sealed review, with **two added early gates**: prove mutation-owned publication has no recurring full-world scan/copy, and include a transient construction ghost plus a park-epoch transition before extending family coverage. Reproduce the historical fast-coaster trail case using complete redraw. Then compare B's general depth rule against A's least costly depth-bearing representation on identical color coverage and movement phases. Only correct, candidate-rendered frames enter the subsequent 4K and large-park performance comparison.

## Reading and evidence boundary

Additional source read for this addendum: the complete `docs/archive/performance-320-tps-refactor-history.md`, lines 1–1265, including renderer, simulation, routing, consolidation, and completion sections. Targeted text searches were used afterward only to locate line references in that same document. The source list in the sealed initial review remains the entire production-source reading performed for this assessment. Historical findings are attributed to the journal; no unrun experiment or current performance result is asserted.
