# GPU-only terrain experiment

The owner has authorized an intentionally incomplete renderer to establish the architecture's performance on a real operating park. [Build85](../obj/vulkan-parity/build-85/receipt.json) passes with zero warnings/errors and unchanged inputs. Two real-park runs pass; both complete 3000 ticks and produce identical indexed/RGBA final pixels. Installed checkpoint52 is unchanged; this experimental source has not been deployed.

## What is removed

The ordinary viewport no longer creates paint columns, calls CPU world generation/arrangement, or records per-object sprite draws. Those helpers and their fallback are deleted from Viewport.cpp (278 removed / 76 added, net 202 fewer lines versus HEAD after clipping corrections). Context no longer scans visible entities or temporarily rewrites their positions for CPU interpolation. The terrain-only publication profile copies no Guest/Staff/Vehicle objects, creates no entity spatial buckets, and schedules no entity preparation worker. Legacy painters still exist for explicitly remaining consumers and diagnostics; their complete deletion is unfinished.

The full simulation still runs. All guests, staff, vehicles and rides continue to update even though their graphics are omitted. The GPU projects/culls/draws base terrain from persistent surface records, then draws UI. Unchanged artwork binds one resident asset lease. Full visible output is redrawn; this is not lazy repainting.

Remaining CPU work includes UI recording, submission, per-chunk revision/directory handling, and actual dirty-tile capture. The latter still copies tile-element vectors and chooses four detailed plus four distant surface image IDs. Moving those last selection rules into shaders remains part of the terrain migration. This first experiment is not the final minimal-state producer.

## Comparable workload

The control is the archived build80 complete renderer, separately qualified by run62. Its package is `obj/vulkan-parity/performance-complete-build80/snapshot.json`.

- Park: frozen `EverythingPark.park`, SHA256 `c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`.
- Saved camera; ordinary Turbo scheduler; 3840×2160 physical output; scale 1; VSync enabled; 144 Hz reported display.
- 100 warmup ticks and 3000 measured logical ticks, no profiler or upload instrumentation.
- Hidden run. Application intervals and present timing do not establish actual display cadence.
- Candidate must match park/config/camera, initial/final population and tick summaries, and final entity checksum. The only intended difference is rendering work.
- One final candidate draw/readback occurs after all measured timing/count samples are frozen. The receipt verifies unchanged authoritative state and camera and records the publication source tick. The screenshot is part of the evidence, not measured workload.

| Metric | Complete build80 control | GPU-only build85 run 1 | GPU-only build85 run 2 |
|---|---:|---:|---:|
| Logical TPS | 67.379 | 355.820 | 355.048 |
| FPS | 29.287 | 142.684 | 142.374 |
| CPU Draw | 26.677 ms | 0.366 ms | 0.363 ms |
| Active simulation tick | 2.490 ms | 2.022 ms | 2.030 ms |
| GPU frame | 1.685 ms | 0.477 ms | 0.480 ms |
| Application interval p95 / p99 | 36.022 / 36.629 ms | 8.201 / 8.541 ms | 8.153 / 8.684 ms |
| Maximum application interval | 42.694 ms | 86.908 ms | 52.497 ms |

Control receipt: [performance-prepartial80-control-01](../obj/vulkan-parity/performance-prepartial80-control-01/summary.json). CPU Draw covers BeginDraw/Paint/EndDraw and deadline bookkeeping; UI update and the retired tween preparation lie outside that timer. TPS measures total elapsed time. Worker/GPU timings overlap and must not be added as serial costs.

A [second control](../obj/vulkan-parity/performance-prepartial80-control-02/summary.json), run after both candidates, also passes matched simulation/configuration checks: 67.048 TPS, 28.987 FPS, 26.907 ms CPU Draw, 1.644 ms GPU, 36.031/36.589 ms p95/p99 intervals, 42.254 ms maximum. The throughput difference persists across both control and candidate repeats; these few sequential runs do not quantify thermal/background-load effects or establish displayed-pacing acceptance.

Candidate receipts: [run 1](../obj/vulkan-parity/performance-partial85-01/summary.json), [run 2](../obj/vulkan-parity/performance-partial85-02/summary.json), [pinned executable and shaders](../obj/vulkan-parity/performance-partial-build85/snapshot.json). Both match the control's configuration, initial/final tick and population census, and final entity checksum `9b7eee204b0471d7000000000000000000000000`. That checksum covers guests, staff, vehicles and litter, not every park-state field. Each reports actual 3840×2160 output and 144 Hz at both boundaries. Draw CPU is about 98.6% lower and TPS is 5.27–5.28 times the control with intentionally unequal rendered content. This validates the cost of bypassing CPU world painting, not the performance of a finished renderer.

Both final captures use rotation 3, zoom 2, view position (458, −4370), source/simulation tick 3136931. Indexed hash: `6b3ec8e66be27346d2bbf98e0d17216d9e036b78160ee74b2e02b91e0183bcaa`; RGBA hash: `02bcd17faedd903277e925dba11b7eac1fdcc924998a647930b5b5ec17e9a1e9`. PNG containers differ; decoded pixels match exactly. [Image](../obj/vulkan-parity/performance-partial85-01/final-benchmark.png), [independent manual review](vulkan-gpu-only-terrain-visual-review.md). Capture is the indexed main canvas with its palette, not HDR/LightFX post-composition or physical scanout.

The candidate's longest measured simulation batches are 80.605 and 47.525 ms. Long application intervals remain unresolved; aggregate timers cannot distinguish expensive tick work from scheduling/preemption or establish exact causal alignment. Do not declare stall-free VSync from mean FPS. Next profiling must retain worst-frame/tick attribution while avoiding measured-loop image readback.

A separate [3000-tick attribution run](../obj/vulkan-parity/performance-partial85-profile-01/summary.json) with the existing CSV profiler passes, but does not reproduce the outlier: longest simulation batch 3.756 ms and maximum application interval 11.499 ms. Its instrumented throughput is 331.839 TPS and CPU Draw 0.374 ms; it is excluded from clean speedup comparisons. The profiler records aggregate function durations, so no cause or fix for the clean-run spikes is established. A narrow dirty-chunk/asset-membership suppression patch remains staged and unapplied; the next preferred change is deleting the legacy tile-copy/image-selection producer rather than adding more intermediate caching.

[Focused checks](../obj/vulkan-parity/partial85-focused/summary.json): 61/61 CPU publication, asset-lifetime and GPU-contract foundation tests pass with unchanged test/shader artifacts. This is not a full regression or Vulkan-validation lane. The newly integrated bounded native-peep draw path remains off in ordinary rendering and is not runtime-qualified by these results. Builds81–84 are retained failed compiler receipts; build85 is the passing correction.

## Missing layers and iteration order

- [x] GPU base terrain/background and existing GPU UI path wired as the sole ordinary viewport renderer; build85 and both real-park runs pass.
- [x] Inspect the real-park benchmark image independently; repeat yields identical pixels. Main world coverage and UI are coherent; visible terrain holes, edge gaps and a detached tile remain explicit incompleteness.
- [ ] Verify the full-main-viewport correction: opaque-window splits record one complete terrain background behind UI, without subsequent clears. Independent secondary viewports remain intentionally empty in this checkpoint.
- [ ] Complete terrain: edges/cliffs, water, smoothing, overlays, view flags and raw graphical-state shader selection.
- [ ] Attribute and eliminate long frames; verify actual displayed pacing separately from hidden benchmark cadence.
- [ ] Port paths and ground-attached details, then scenery and their depth/occlusion relationships.
- [ ] Integrate guest/staff state and resident animation assets into ordinary larger maps; the bounded common-pass implementation must be measured and corrected before this activation.
- [ ] Port tracks, supports and vehicles together with the remaining moving entities and effects.
- [ ] Restore independent auxiliary world captures/previews. They currently remain empty rather than invoke CPU world paint or borrow a stale main snapshot.
- [ ] Re-establish complete upstream pixel comparisons, manually review divergences, remove obsolete CPU renderer code and qualify displayed VSync pacing.

Each layer follows the same loop: render the real park for at least 3000 measured ticks, save the final image, inspect visible errors, correct them, and investigate regressions before adding more layers. An omitted-layer speedup validates only part of the architecture; it is not equal-output performance acceptance.

## Next terrain replacement

- [ ] Make the authoritative tile mutation owner publish compact raw graphical fields through the existing dirty-tile stream: geometry (base height, complete slope bits, presence/visibility), appearance (surface/edge material slots and grass), and water height. Publish only changed fields/chunks; bootstrap once per world epoch. Keep neighboring tiles resident so edge evaluation needs no extra CPU neighborhood scan.
- [ ] Hold generation-qualified object catalogs containing surface selectors, edge variants, water masks/overlays and palette-filter metadata. Reuse the existing atlas leases, including zoom dependencies; rebuild catalogs only on relevant object mutations. Shaders choose images from these static rules and current raw fields.
- [ ] Delete native-path tile-element vector copies and the four detailed plus four distant image selections in `Map.cpp`. Replace `SurfacePresentationRecord` image arrays, `GetOrCreateSurfaceSpriteSet` conversion and the selected-image input of `world_surface_compact.comp` with that raw contract. Keep any remaining legacy snapshot requirements explicit rather than duplicating them into the native producer.
- [ ] Reuse `terrainPlanEdge` / `terrainEdgeAt` for cliffs, extending steep-slope and map-boundary rules. Replace the existing 32×32/height restrictions with full-map dimensions and bounded visible-column dispatches; do not simply enlarge fixed arrays or serial shader loops. Preserve rear edges as surface attachments and front strips as independently ordered parents.
- [ ] Add water using the original shoreline masks, blended water palette, attached overlay and neighbor-water rules from `Paint.Surface.cpp`. Water requires ordered destination-dependent indexed blending and must interleave with cliffs; drawing all water last or treating it as opaque is incorrect. Tunnel mouths remain explicitly incomplete until track/path owners publish the required facts, without restoring CPU paint.
- [ ] After each coherent replacement, repeat the same real operating `EverythingPark` run at physical 3840×2160, VSync enabled and at least 3000 measured ticks. Preserve simulation/config/camera/checksum checks, capture only after measurement, inspect the final image and investigate CPU/GPU/TPS regressions before adding the next layer. Continue labeling omitted categories and unequal-output performance scope.
