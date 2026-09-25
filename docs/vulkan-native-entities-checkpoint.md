# Native entities checkpoint

Current deployed checkpoint: build167, following build164 / commit `e132aaa338`. Chairlift station and foreground-track-anchor candidates remain staged outside production and are not included. Current remaining visual failures are listed in [the close-up review](vulkan-native-entities-visual-review.md).

This follows `2337f3b8da` (supports, signs and component anchors). Everything Park remains the common original-art visual and performance sample. The production world path now consumes owned graphical state for vehicles, peeps, balloons, litter, fountains, ducks, particles and money annotations. Runtime and visual qualification are in progress; implementation alone is not pixel parity.

## Architecture

The simulation boundary captures one immutable generation. Peeps retain their existing lifecycle, motion, appearance and animation field streams; changed fields are packed into a single upload and applied to resident GPU arrays. Balloons upload changed resident chunks in one copy command. Vehicles publish compact graphical records and an immutable catalog of the car types actually used. The shader selects headings, slopes, roll, animation and rider images from original image-group metadata. Effect records contain raw positions, types and frame/state counters. Text glyph data changes when text or its font changes, while motion and scrolling remain separate hot state.

World rendering does not build a CPU entity paint list. The previous selected-vehicle CPU paint capture is bypassed in the production profile; auxiliary previews consume the same native vehicle generation. Resident asset leases keep art alive for held snapshots. Source tick and catalog ownership checks reject mixed generations.

Each authored sprite receives one constant depth. Integer anchor priority dominates a bounded local layer. The corrected D32 encoding prevents child layers from jumping ahead of neighboring anchors. Monetary annotations have a separate bounded interval above the world and below UI; constant per-glyph priorities preserve overlapping outlines and hint blending. New RotoDrop seat anchors are explicit geometry requiring visual review, not claimed original source coordinates.

## Checklist

- [x] Preserve the preceding reviewed checkpoint in commit `2337f3b8da`.
- [x] Connect raw entity snapshots to the production Vulkan world visitor.
- [x] Select peep body/accessory images on GPU from resident animation metadata.
- [x] Add free balloons, litter, ducks, fountains and particle effects.
- [x] Add vehicle image selection and specialized styles without CPU paint replay.
- [x] Add immutable money glyph runs and shader wave motion.
- [x] Fix banner identity corruption at the object packing boundary.
- [x] Correct zero-filled array extraction and restore support operations in the admitted track set.
- [x] Integrate large scenery custom lettering and pass its CPU layout/ownership tests.
- [x] Qualify standard vehicle selectors against the original source oracle, including fallback predicates and reversed/inverted cars.
- [x] Run held-snapshot, lifecycle, depth and synchronization tests (build153:50 passed; build157:catalog burst lifetime passed).
- [ ] Inspect matched-art Everything Park closeups, including all four rotations and underground/cutaway views.
- [ ] Restore remaining whole-track recipe omissions from shared source adapters.
- [x] Measure 4K, 12,000 ticks, 360 TPS cap and vsync; compare CPU draw, GPU time and long-frame distribution against build144.
- [ ] Restore the 360 TPS target: build167 improves to 330.267 TPS / 143.804 accepted presents per second with 0.727 ms CPU draw; the target remains open.
- [ ] Use stable resident catalog handles and append-only admissions; ordinary object/frame changes must not rebuild or re-upload the full catalog.
- [x] Commit the qualified checkpoint with explicit remaining regressions (`e132aaa338`) and deploy for manual testing (28 files verified, 7 changed).

## Integration findings

Build 149 passed all 58 selected CPU/source-oracle tests. Its two GPU tests stopped at pipeline construction: NVIDIA reported `NVVM compilation failed: 3` for the money fragment decoder despite valid SPIR-V. An isolated minimal-fragment run proved the vertex/compute paths built; replacing the bool/out decoder and dynamic division with a scalar sentinel and checked multiplication then built the actual fragment on the device. These artifact-local runs are diagnostics, not parity qualification. The complete next build also tests correct blend-palette handling for hinted money text and zero ink.

The first Everything Park admission reached 62,978 static sprite sets before entities and exposed the old 65,536-entry limit. The checked table now permits 262,144 entries (50 MiB), within the existing device storage-range preflight. No sprites are silently truncated. The final loaded count and performance remain to be measured.

Track authoring 18/support 06 restores 72 more style/type programs. River Rapids waterfall, rapids and whirlpool art use resident image ranges and the existing immutable snapshot clock; they do not upload newly selected images every frame. All earlier rows are preserved except 60 Go-Karts bounds rows corrected against their literal source aggregates. See the track coverage document for remaining helper/ride-state omissions.

## Performance baseline and remaining limits

Build 144 rendered a partial world: 359.960 TPS, 143.774 accepted presents/s, 0.151 ms CPU draw, 3.271 ms GPU frame, 9.1 ms P99 and 24.101 ms maximum accepted-present interval. These are hidden-window queue metrics, not measured display scanout. The new entity load must be measured rather than assuming those numbers persist.

Vehicle records currently form one contiguous hot batch, rather than separate motion and appearance streams; this is a bounded first implementation to measure. Static sprite materialization still occurs on the GPU each frame. Remaining accepted divergences are the outer map and underground skirts. Pathological buried Cinema geometry is low priority, not accepted parity. Whole-track omissions, underground/filter behavior, rare vehicle styles and independent equal-anchor components remain explicit review areas until fresh captures qualify them.

## Build 151 integration gate

All 51 selected assertions passed, including native peep/balloon held generations, deletion and reuse, native vehicle heading selection and mixed-generation rejection. The runner still failed qualification because synchronization validation detected one balloon reset clear-to-copy write hazard. An explicit transfer-write dependency now separates the full reset clear from the live chunk batch; the next build must pass the same validation test.

The full park still stopped before device creation during asset admission. Increasing the old limits exposed the actual issue: the peep catalog admitted all loaded animation objects, including unused costumes. The correction maintains used-object membership at lifecycle/appearance mutation boundaries and admits only complete referenced banks. Membership identity is stable across motion and animation updates. This is an ownership fix rather than image selection on the CPU. Atlas capacity is currently 256 indexed 2048-square pages (1 GiB); final occupancy and any further reduction remain to be measured.

Effect snapshots now use completed simulation positions, matching vehicles and money, rather than any temporarily interpolated live entity positions.

## Builds 153–154 ownership and capacity evidence

Build153 passes50 selected tests with zero skips/failures and no synchronization diagnostics. This includes the balloon reset dependency, held native entities, vehicle generation rejection, used peep bank membership, paused caption/status publication, and new Monorail/spinning-tunnel source rules. Build152 was a missing direct Colour header include in the new standalone catalog test;153 corrects it.

Everything Park now admits only7,788 peep/accessory/balloon sprite sets, for71,460 sets including the world. The remaining admission failure is measured capacity, not duplicate peep banks:264 admitted vehicle banks across338 referenced car slots need329,224 unique images including splash art. Their contiguous ranges match RideObject::Load and deduplicate globally. With612 effects, total resident sets are401,296. The table is therefore524,288 entries/100MiB, below Vulkan's minimum128MiB storage-buffer range and still subject to the actual device check. Complete car animation banks remain resident; no heading/frame rows are dropped to meet the old limit. Detailed inventory evidence is `obj/vulkan-parity/everything154-admission-inventory/paths-r3/native.log`.

## Full-world admission and auxiliary ownership

Build155 admits all401,296 resident sprite sets and316,136,472 bytes of decoded art. Its first offscreen render correctly rejected a mismatched vehicle/map identity: independently captured auxiliary maps use a separate epoch namespace. The auxiliary boundary now copies only the vehicle snapshot wrapper into that namespace, preserving shared owned records/catalogs and the source tick.

Build156 then exposed the80,259,200-byte sprite table exceeding the ordinary frame upload ring alongside map state. Large world catalog admissions now use one bounded temporary staging allocation owned by the submitting slot's existing fence. It is retired on completion/discard, and ordinary frames retain the original upload-ring size. A GPU regression covers a20MiB table against an8MiB ring, discard/retry and repeated slot reuse without another world copy. These corrections do not add per-object upload calls or CPU image selection.

## Complete catalog integration (builds157–159)

Build157 passes the large-catalog GPU regression with synchronization validation: a20MiB table on an8MiB ordinary ring survives discard/retry and three held frames without repeated world uploads. The complete park subsequently exposed missing specialized car images. Original RotoDrop, River Rapids, Virginia Reel, observation tower, freefall and classic spinner painters address specialized ranges beyond generic car image counts. Catalog admission now includes those complete source-defined ranges, the shared first-car Mini Golf bank and the184 cable-lift images. The CPU admission regression passes in build159. No missing-image fallback is permitted.

These extra716 vehicle images expose inefficient square atlas slots at the existing256-page limit. Rectangular power-of-two slot classes are being qualified to recover space without increasing the allocation ceiling. Actual new full-park visual and performance results remain pending; earlier unit passes do not qualify this larger admission.

## Rectangular resident atlas (build160)

The complete402,012-set catalog now admits316,281,540 decoded art bytes in189 atlas pages. Width and height independently select power-of-two slot classes (minimum32 on each axis), preserving descriptor identity, allocation serials and fence retirement. A234x28 image uses a256x32 slot:512 per page rather than64. The fixed256-page Vulkan allocation ceiling is unchanged; this reduces packing waste and recovers admission headroom, not the allocated texture array size. Tests cover all49 size classes, non-overlap, exact bounds and free/reuse identity.

## Full-park visual evidence and startup root cause

Build160 produces matched-original-art Everything Park closeups with vehicles, peeps, balloons and effects. All four captures complete without Vulkan validation diagnostics. Changed pixels against upstream (1024x768 each):paths-r3 12,783; flats-r3 17,648; glass-r3 13,470; wooden-tree-r3 19,590. These are unmasked differences, not parity passes. Agent close inspection finds sampled held balloons/owners exactly matching, while some rail/vehicle/peep overlaps remain wrong. The missing gray flats strip is precisely Chairlift ride513's station at tiles141,75..80, not a missing vehicle or depth-hidden rail. See `vulkan-native-entities-visual-review.md` and `vulkan-world-text-review.md`.

The first build160 4K/12,000-tick benchmark was deliberately stopped during warmup; there are **no performance results** from it. CPU usage continued while the GPU was idle. Source audit found `ApplyFrameRetirement` removing each presented atlas upload by separately scanning/compacting the entire pending vector. The400,000-upload catalog makes that quadratic. The correction removes the completed batch in one pass while preserving allocation identity and retry/discard semantics. That correction must be qualified before another performance run. Evidence of the aborted run is retained in `obj/vulkan-parity/performance-entities160-12000-01`.

## Checkpoint boundary after the reported restart

The user reports an automatic restart with0x119 VIDEO_SCHEDULER_INTERNAL_ERROR while build161's benchmark was starting. Its log ends after pipeline preparation and object/scenario index loading, without a terminal benchmark report. Treat that run as interrupted, not measured performance or a successful stability test. Build161 logged24.706s graphics preparation, excluded from any steady-state metric. No causal attribution to the renderer or hardware is established.

A fresh read-only review found no concrete staging/fence/balloon synchronization fault explaining the restart. It found a portability preflight omission: the compute layout combines25 world storage buffers and5 filter buffers, so build163 checks30 descriptors. The actual RTX5070Ti previously passed layout creation; this correction is not presented as a crash fix.

The glyph overlap regression in161 passed overlap ownership but exposed four negative-coordinate fringe errors. Build163 explicitly rounds negative odd coordinates downward instead of relying on different C++/GLSL signed remainder behavior. Pixel assertions are unchanged. Batch-retirement lifecycle and Go-Karts station CPU tests passed in161. Final163 tests, screenshots and timing are recorded below when complete.

This checkpoint includes5470 track style/type pairs (authoring20/support08), with Go-Karts station types1/2/3 added. All239404 previous rail rows and5467 previous support programs are unchanged in that addition. Chairlift station restoration and named foreground-track anchor metadata are unqualified follow-up candidates, excluded from this deployment.

## Build163 qualification

The serial build passes with zero warnings/errors and unchanged source inputs. All36 selected tests pass, zero skips/failures and no Vulkan validation diagnostics (`obj/vulkan-parity/native-entities163-01`). This includes actual-device held peep/balloon generations, lifecycle reuse, native vehicle selection/mixed-generation rejection, large-catalog discard/retry/slot reuse, overlapping large-font glyphs in both visible directions and both text layouts, rectangular atlas allocation, partial batch upload retirement, source vehicle selectors, used peep bank membership, paused captions/status and Go-Karts station rules.

## Stable residency and required incremental admission

Build163's performance attempt exposed hot vehicle pose bits changing the set of admitted car variants. Each inversion/roll transition could therefore rebuild the entire402k-entry sprite table. The run was stopped after diagnosing repeated catalog-admission logs; it is not a completed performance measurement. Build164 derives resident variants from the complete present-car family of each instantiated ride object. Changes to position, orientation, pitch, roll, inversion and animation preserve shared residency identity. A regression covers lifecycle, cable lifts, unrelated loaded families and catalog/epoch replacement.

This fixes pose churn, not the entire catalog architecture. The user's explicit next requirement is one initial catalog with stable handles and append-only incremental additions during its world lifetime. New art uploads only its texels/descriptors; animation references already-resident ranges. CPU admission may use a hash map for deduplication; shaders should prefer dense direct-indexed records using stable integer handles. Genuine new-family/object changes can still rebuild the combined table in this checkpoint and remain an open performance requirement. Avoid describing that path as fully incremental.

Build164 also gives ordinary wooden and metal support parents local placed-art layer1. Their authored scalar depth and cap below their own rail are unchanged; explicitly attached rail children remain unchanged. This resolves their tie with their own terrain. The user's missing dark 'shadow' sample is primarily opaque wooden support artwork hidden by grass, so the same correction targets it; no fabricated lighting/shadow pass was added.

## Completed build164 performance run

The4K, vsync,100-warmup +12,000-measured-tick Everything Park run completed successfully. The harness pass covers completion/integrity and at least143 accepted presents/sec; it does **not** mean the360 TPS target passed. This is a hidden-window queue measurement, not displayed scanout. No device failure occurred in this run.

| Metric | Partial-world build144 | Entity-world build164 |
| --- | ---: | ---: |
| Logical TPS |359.960|282.536|
| Accepted presents/sec |143.774|143.693|
| CPU draw/frame |0.151ms|1.613ms|
| GPU frame |3.271ms|4.181ms|
| Accepted-present P99 interval |9.1ms|9.5ms|
| Maximum accepted-present interval |24.101ms|17.035ms|
| Mean simulation tick |1.769ms|1.921ms|

All6,103 submitted measured frames were accepted; none were superseded/discarded. The final checksum is unchanged: `07d58eaefde6aa6d000000000000000000000000`, at tick3,145,931 with17,042 guests,2,208 staff and2,128 vehicles. One initial resident catalog admission appears in the log; ordinary inversion/animation no longer rebuilds it. The expanded family catalog has416,312 sprite sets in193pages. Graphics pipeline preparation took34.396s outside measurement with an older cache seed; this is not a same-build warm-restart timing.

This leaves a concrete performance regression: CPU drawing and UI work now consume headroom needed for360 TPS. Next checkpoint must attribute that cost and reduce it without dropping entities or reducing the144Hz target. The maximum application draw interval was41.071ms, distinct from the17.035ms maximum accepted-present interval; the former includes a29.154ms first measured draw-begin retirement. Both remain recorded, not filtered away.

Evidence: `obj/vulkan-parity/performance-entities164-12000-01/summary.json`, complete log and `final-benchmark.png`. All38 focused164 tests passed, including the four-rotation ground-contact support GPU regression and stable vehicle-family residency, with no validation diagnostics. Build163's36-test GPU/lifecycle/text qualification is retained separately. The final full-park image was manually inspected; pixel parity remains open.

## Fresh support visual qualification (build164)

Matched-art Everything Park captures correct 852 pixels in paths-r3 and 3,083 in glass-r3 relative to build160; every changed pixel now matches upstream and no new mismatches were introduced. Remaining differences are 11,931 and 10,387 respectively. An agent inspected enlarged regions manually. The exact earlier shadow crop retains 398 differences around vehicle/rail contact; do not treat all shadow/occlusion reports as resolved. Evidence: `obj/vulkan-parity/everything164-matched-art-01` and `obj/vulkan-parity/checkpoint164-support-review`.

## CPU regression source audit

The main-thread drawPaint phase grew from 0.142 to 1.595 ms/frame. Native snapshot capture and retained-family application currently happen synchronously before scheduling a worker whose native BuildCapturedStorage path returns immediately. Added work includes dirty-registry capture, peep validation and copy-on-write, balloon application, full vehicle records, effects and money capture. Avoidable work identified for the next checkpoint includes notifying unrelated families on ordinary motion, sorting already entity-ID-ordered vehicles, and packing unused passenger colour slots. Their individual cost is not yet measured. Preserve immutable publication and lifecycle/reuse semantics while eliminating this work; move owned batch processing off the owner thread only with explicit publication and failure handling.

Deployment receipt: `obj/vulkan-parity/deploy-entities164-01/receipt.json`. The existing installation at `D:\Games\Independent\OpenRCT2Mod` was updated without launching the game or changing settings/saves.

## CPU attribution and first reduction (build165)

A separate 4K/12,000-tick instrumented run attributes average native publication work as follows (nested scopes overlap): retained-family preparation 1.186 ms, dirty registry capture 0.254 ms, peep application 0.720 ms, balloon application 0.039 ms, vehicle capture 0.092 ms, effects 0.006 ms, money 0.003 ms and acknowledgement 0.007 ms. World draw-command preparation is 0.065 ms. Evidence: `obj/vulkan-parity/performance-entities165-12000-profile/profile.json`. Profiler throughput is not clean performance acceptance.

The first reduction removes cross-family motion notifications, redundant sorting of already ordered vehicles, unused rider-colour packing, and empty native worker jobs. It preserves presence tombstones across removal/reuse and original odd-passenger paired colour lookup. All 49 selected publication/lifecycle/vehicle tests pass (`native-cpu165-01`). A separate clean 4K/12,000-tick run improves from 282.536 to 299.008 TPS, with 143.798 accepted presents/s and 1.398 ms CPU draw (previously 1.613 ms). Mean GPU time is 5.338 ms in this run; it is not a GPU improvement. P99 accepted-present interval is 9.2 ms, maximum 19.611 ms. The authoritative final checksum still matches. The 360 TPS gate remains open.

- [x] Attribute the CPU regression to measured native publication stages.
- [x] Remove redundant cross-family work and vehicle preparation; qualify lifecycle/reuse semantics.
- [x] Move owned retained-family application off the simulation/UI thread, preserving coordinated generations and failure recovery (build167).
- [x] Re-measure clean 4K/12,000 ticks after async publication; 360 TPS remains unfulfilled, with the presentation target sustained.
- [ ] Restore original peep Z+5 depth anchor without moving raster position; qualify surface contact in four rotations and closeups.

The reported pink-support circuit is not missing native geometry: Everything Park's Mini Coaster 1 (ride239) assigns invisible/invisible/bright-pink colours to all 193 track elements. Fresh upstream and Vulkan images agree. Save-byte and original G1-art evidence is in `obj/vulkan-parity/missing-pink-track239-verdict.json`; see the closeup review for genuine remaining contact/overlap differences in that same scene.

## Async preparation and withdrawn shader candidate (builds166–167)

The owner now captures one owned retained-family batch plus vehicle/effect/money records at the publication boundary. Peep/balloon validation and copy-on-write application run on the existing background worker pool. Both sources publish only after coordinated completion. A failed worker preserves the displayed generation and triggers a full bootstrap; acknowledgement occurs only after successful enqueue. Tests cover live mutation behind a blocked worker, retained generations, acknowledged-input recovery, and entity/catalog resets. Independent source review found no ownership defect.

Build166 passed 54 focused tests including two actual-GPU tests with synchronization validation. However, its full Everything Park benchmark failed during warmup with VK_ERROR_DEVICE_LOST, and a subsequent single offscreen capture with validation also failed. Windows recorded Display4101/NVIDIA driver recoveries at 20:35:35 and 20:40:03 on 2026-09-24. Neither run provides performance/parity results. This candidate was never deployed.

The peep Z+5 shader change and its test were withdrawn from production and archived at `obj/vulkan-parity/peep-contact166-withdrawn`. Build167 retains the async CPU change and restores all 26 shader binaries byte-for-byte to successful build165. Its 52 selected CPU/publication tests pass. Full-scene qualification remains necessary before deployment. The small GPU test passing does not qualify the withdrawn shader on the full park; the relationship to the two driver failures is being isolated, not declared proven. The two large atlas-upload log entries also exist in successful165, so they are not unique evidence of the166 failure.

## Qualified asynchronous checkpoint (build167)

Build167 has zero warnings/errors and passes all 52 selected CPU/publication tests. Its full-art offscreen Mini Coaster capture passes synchronization validation and is pixel-identical to build164 (zero changed pixels; 5,830 existing upstream differences). An agent manually inspected the complete native image and enlarged crowd contact region. The root inspected the final 4K benchmark screenshot. Peep Z+5 remains withdrawn, so this checkpoint preserves the previous visual state rather than claiming the contact defect fixed.

| Metric | Entity-world build164 | Async build167 |
| --- | ---: | ---: |
| Logical TPS, 12,000 ticks | 282.536 | 330.267 |
| Accepted presents/sec | 143.693 | 143.804 |
| CPU draw/frame | 1.613 ms | 0.727 ms |
| GPU frame | 4.181 ms | 4.425 ms |
| Accepted-present P99 | 9.5 ms | 9.0 ms |
| Maximum accepted-present interval | 17.035 ms | 12.622 ms |
| Mean simulation tick | 1.921 ms | 1.984 ms |

The completed run preserves the authoritative checksum `07d58eaefde6aa6d000000000000000000000000`, with 17,042 guests, 2,208 staff and 2,128 vehicles. All 5,225 measured submissions were accepted. It used the prior successful shader cache (0.094 s graphics preparation), not a cold-start measurement. No driver recovery was observed during this run. This success does not explain the two withdrawn166 failures or certify general driver stability. Evidence: `obj/vulkan-parity/performance-entities167-12000-01/summary.json` and `everything167-mini-01/summary.json`.

The remaining main-thread budget is now explicit: roughly 0.727 ms drawing plus 1.647 ms UI/presentation work per frame, and 1.984 ms per simulation tick. At 144 frames/s and 360 ticks/s those averages require about 1.056 seconds of main-thread work per second, before other overhead. Recovering the remaining target therefore needs approximately 0.4 ms/frame or 0.16 ms/tick (or a combination), not a higher speed cap. The earlier separate profile attributes almost all UI time to presentation audio, including crowd/vehicle spatial processing; ride music also repeats listener preparation across emitters. Next work should measure and eliminate redundant presentation preparation, share listener state across one audio update, and examine retained batch allocation/validation and simulation local-context queries. Keep audio behavior, simulation checksum and full entity rendering intact. These are follow-up candidates, not implemented gains.

Open rendering work remains: peep contact depth needs a full-park-safe implementation; foreground vehicle/rail and station overlaps, Chairlift stations/remaining recipe omissions, underground/construction/preview and rare-family qualification, and append-only catalog admissions. Do not retry the withdrawn166 shader as an ordinary benchmark. Any later shader investigation must isolate it from the qualified CPU path and preserve these failure receipts.
Deployment167: `obj/vulkan-parity/deploy-async167-01/receipt.json` verifies all 28 files (2 changed) in `D:\Games\Independent\OpenRCT2Mod`. No automatic launch or settings/save changes.
