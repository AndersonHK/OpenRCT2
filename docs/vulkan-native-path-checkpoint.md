# Native path layers checkpoint

Work starts from accepted implementation `775c86edf0` and checklist `d90d4243d9` on `codex/gpu-snapshot-terrain-layers`. The [terrain checkpoint](vulkan-snapshot-terrain-checkpoint.md) is the pinned performance and snapshot baseline. Ordinary Turbo remains capped at 360 TPS; this turn does not resume automatic goal cycles.

## Scope and implementation checklist

- [x] Publish variable-length, immutable raw path records, including stacked paths, through the same world generation as terrain. Preserve removed/changed paths and object catalog lifetimes without copying legacy tile structures.
- [x] Keep path surface, queue, slope and railing image selection in shaders. Integrate components with tile/height ordering; do not append the entire path family over the world.
- [x] Add path additions supported by the native rule/catalog contract; explicitly inventory remaining supports, banners, text and effects.
- [x] Coalesce dirty state into batched uploads; unchanged/camera-only frames perform no path instance upload. Qualify removal, source-arena reuse, abandoned uploads and explicit capacity rejection.
- [x] Preserve existing terrain/water tests and identify prerequisite ordering/composition gaps exposed by paths.
- [x] Add focused publication, rule and actual Vulkan tests. Run serially with validation and synchronization validation; save visual samples.
- [x] Inspect original-art path output and compare supported cases to pinned external reference behavior. Have an agent manually inspect all divergent sample groups.
- [ ] Close residual slope-edge/railing pixels, cross-path/cross-tile ordering and all zooms; whole-image path corpus is still not exact.
- [x] Repeat matched EverythingPark runs at 4K/VSync, 100 warmup plus 12,000 measured ticks. Compare build92 and the new candidate's four 3000-tick windows, state census/checksum, CPU/GPU costs and frame intervals.
- [x] Measure uncapped headroom and upload traffic separately; investigate reproducible regressions before promoting the candidate.
- [x] Record limitations, update the migration checklist and commit the incremental checkpoint.
- [x] Deploy the tested binaries/shaders with verified preimage backups for owner placement, terrain and camera testing.
- [x] Record the owner's interactive test: no dirty frames, correct perspective for present objects and no construction problems.

## Candidate results

Build97 passes **97 focused checks** with zero skips, failures or Vulkan/synchronization diagnostics. It fixes progress-draw reentrancy during object loading and distinguishes deliberately empty sprite slots from corrupt/missing images. A bounded GPU arranger orders each path's component parents, retaining bridge deck children; it bypasses sorting for counting and single-parent paths. Path zoom 0/1 sampling now preserves bitmap/RLE behavior. This does not implement general cross-family scene ordering.

Build98 has zero compiler warnings/errors and changes only the screenshot scope description in `Context.cpp` relative to build97; all shader hashes are identical. The description now includes paths/additions. The existing 97-check qualification applies to the rendering implementation; final build98 is separately exercised by endurance/headroom/upload runs.

Both capped rows below are 3840×2160, VSync on the reported 144 Hz display, 100 warmup plus 12,000 measured ticks. Final state matches the pinned control: tick 3,145,931, 17,042 guests, 2,208 staff, 2,128 vehicles and entity checksum `07d58eaefde6aa6d000000000000000000000000`.

| Candidate | TPS | Application FPS | CPU Draw ms | GPU ms | Frame p99 ms | Worst interval ms |
|---|---:|---:|---:|---:|---:|---:|
| Build97 | 359.829 | 143.992 | 0.105283 | 1.372230 | 9.021 | 9.616 |
| Build98 | 359.953 | 144.011 | 0.106977 | 1.427410 | 9.010 | 9.919 |

Build98's four 3000-tick windows take 8.335862, 8.334373, 8.331864 and 8.335604 seconds. The new layer adds approximately 0.026 ms CPU Draw and 0.35–0.41 ms GPU relative to the accepted build92 runs, while retaining the throughput target. CPU Draw excludes other publication/UI/submission work. Application cadence is not independently measured physical scanout, and absence of a rare stall in these two runs does not resolve its earlier cause.

The two final images have identical indexed SHA-256 `f70da944b9a728b569ac2856a5e6dbf2af0a2ebe19f3729a3e87c16f752cab05`. Agents reviewed the real-park images and all eight original-art reference/candidate groups; see [manual review](vulkan-native-path-visual-review.md). Tight ground regions for all 17 flat and 16 queue samples match at both tested zooms in all four rotations. Remaining interior diagnostic differences are 142–146 grass-edge pixels at zoom 0 and 57 slope pixels at zoom 1 (including 11 railing pixels). These remain open defects, not accepted tolerances. Whole-image comparisons remain failures because they also include the accepted outer skirt and unqualified background differences.

The separate build98 uncapped run reaches **420.575 TPS / 144.012 FPS**, with 451.925, 432.362, 413.618 and **389.489 TPS** in its four windows. This is within 0.7% of the accepted build92 aggregate and retains slightly higher final-window headroom; the fresh build92 control measured 426.442 overall and 391.480 in its final window. Ordinary gameplay remains capped at 360 TPS.

Upload telemetry completes 12,000 ticks with **4,801 submitted frames, 1,108 world buffer-copy calls and 7,751,744 world transfer bytes** (roughly 0.23 MB/s). Allocation failures, lost samples, overflow and image capture/readback requests are zero. Safety status is 19,204 bytes, four per frame, retired through existing fences. Palette image updates and UI/atlas traffic are separate. World traffic rises from the terrain-only baseline as path/addition state changes; transport is batched at chunk granularity, not yet minimal individual-field deltas. Terrain changes can resend unchanged paths in the same chunk. Camera-only/unchanged frames retain their instances.

The first upload invocation selected the uninstrumented control by mistake; the game itself completed successfully, but its comparison gate correctly rejected differing telemetry settings. `path98-upload-comparison/comparison.json` independently verifies the saved log/provenance and compares the unchanged result against the correctly instrumented build92 control, passing workload/state/environment matching. The original failed invocation is preserved. A second upload run was interrupted at the owner's request for immediate deployment and is not evidence. No further GPU work runs alongside the owner's manual test.

## Deployment and next checkpoint

The [machine-readable evidence index](vulkan-native-path-checkpoint.json) pins the build, tests, performance, visual comparison, upload and deployment receipts.

**Accepted implementation checkpoint: `9b02bf4661`.** After testing the deployed build, the owner reported: "It worked. No dirty frames, perspective was correct for all objects present, no problems with construction." The owner accepted the benchmark results and authorized the checkpoint. This closes the basic interactive deployment check; the detailed parity and missing-layer work above remains open.

Build98 is installed at **`D:\Games\Independent\OpenRCT2Mod`**. `deployment-paths-build98-01/receipt.json` reports `deployed-verified`: 52 qualified files verified, 34 replaced, with no deletions. Previous bytes are backed up in `obj/vulkan-parity/deployment-paths-build98-01/backup`; the adjacent manifest records every preimage and installed hash. Saves, configuration, objects and destination-only files were not modified. Build receipt SHA-256: `15d4ca6bb2cdefe27fc2335ccb5de26682eca597679f97aa3de1d8fe3b2471fb`.

The next checkpoint should first incorporate owner feedback and close path slope seams/railing sampling, construction overlays and structural supports. Continue terrain modes/water intersections and common ordering before adding scenery or moving entities. Final software/X8/painter source retirement remains open: path painting still has residual picking/auxiliary consumers, even though ordinary native rendering bypasses it. This increment adds layers without claiming a dead-code-deletion speedup or full migration completion.

## Baseline and evidence

Build92 established 358.825–359.958 TPS, 143.919–144.013 application FPS, approximately 0.081 ms CPU Draw and 1.02 ms GPU over 12,000 ticks. Its fullest uncapped window is approximately 388 TPS. Hidden-window intervals are not physical scanout evidence. Rare residual simulation stalls remain separately tracked.

A fresh pinned build92 control in `obj/vulkan-parity/performance-path-baseline92-12000` passes workload/state comparison: **358.747 TPS / 141.227 application FPS**, 0.079064 ms CPU Draw, 0.969098 ms GPU, p99 9.248 ms and maximum interval 99.196 ms. The maximum aligns with a 95.0784 ms simulation event consuming 5,873,590 thread cycles, reproducing a residual stall before path changes. This evidence prevents attributing all later outliers to the new layer; its cause is still unresolved.

The separate fresh uncapped control, `performance-path-baseline92-12000-headroom`, records **426.442 TPS**, approximately **391.48 TPS in the final window**, and 141.508 application FPS. Its maximum interval is 90.637 ms, again in unchanged code. Both controls use the saved immutable build92 package and preserve the final entity checksum/census.

No candidate performance or parity acceptance is implied by these controls. Terrain modes/water-side intersections, general mixed-scene ordering, scenery/tracks/entities/effects, auxiliary views and final CPU/X8 source deletion remain outside this first path increment unless explicitly completed below.

## Implemented state and GPU contract

Each immutable 256-tile chunk holds variable-length path lists in original element order. The native producer captures raw height, clearance, material slots, edges/corners, slope, queue/banner, addition state and flags. It does not clone legacy tile elements or select path sprites. Held generations retain their material facts and atlas dependencies across object replacement. Object catalog changes and ordinary edits publish through the existing world-generation boundary; fixes add missing dirty notifications for queue banners, wide flags, repair-vandalism and litter-bin cheats.

The GPU keeps a persistent arena of 48-byte path records and 44-byte surface records. Changed ranges share staging storage, using at most one source-buffer copy and one path-buffer copy; unchanged and camera-only frames do not upload path instances. Catalogs and sprites upload only when their generations change. The shader selects surfaces, queues, slopes, bridge decks, railings, static queue signs, lamps, bins, benches and fountain bases. It merges paths below terrain, between terrain and water, or above water; exact general bounding-box ordering remains open.

The arena holds 1,048,576 raw paths. A temporary admission guard rejects more than 4,096 paths on one tile before dispatch, avoiding an unbounded single shader invocation. Nothing is silently truncated. Output overflow suppresses unsafe drawing and is latched through asynchronous status retirement. Arbitrarily reordered Tile Inspector stacks, physical support columns, tunnels/cuts, scrolling sign text, fountain effects and ghost/selection overlays are not qualified.

## Qualification history and manual testing

Build93 failed two test-only compile errors (a missing declaration include and a signed comparison); build94 compiles. Its first 91-test run passes 90 and exposes a test lifecycle error: completed GPU overflow intentionally latches failure, so an unrelated invalid-input assertion cannot reuse that executor. Build95 separates those fixtures and explicitly tests the latch. Original-art loading also exposes empty image slots in eagerly loaded path material ranges; this must be resolved before deployment. Failed artifacts are retained.

An agent inspected all 20 synthetic images from `path94-focused`, covering 17 unique images. Terrain/water merge, rotated surface/fence markers, stacked path shrink/removal/reuse, elevated-path culling, additions and whole-world overflow rejection match fixture expectations. These are synthetic rectangles, not original-art parity evidence.

The owner requests deployment before closing this checkpoint. Manual testing should cover committed path placement/removal, slopes and queues, terrain height changes, camera pan/zoom/rotation, and park reload. Edits appear in the next completed immutable generation. Construction ghosts/selection overlays and secondary world viewports are still incomplete; their absence is tracked migration work. User saves/configuration must remain untouched by deployment.

## Owner-accepted outer cliff skirt

On 2026-09-23, the owner explicitly accepted the native outer cliff skirt as an intentional visual difference: "I actually like the map having a skirt, it is nicer." The owner compared it to isometric SimCity and requested that it remain. This is an aesthetic exception, not original-renderer parity.

The original painter reads the technical-border surface's actual height when deciding whether adjacent interior cliff faces exist. The native edge rule treats non-drawn technical-border records (`kind == 2`) as absent neighbors and extends those interior faces down to the minimum land level. In the path corpus, border and interior land are both at world Z 64: the original therefore has no outer cliff face, while the native result has a skirt down to Z 16. Publication already retains the true border heights; no map data or simulation height change is involved. The proposed neighbor-rule correction was not applied.

This acceptance covers the outer cliff skirt only. It does not waive interior cliff errors, path component differences, or the separate original blank-tile/background composition difference.
