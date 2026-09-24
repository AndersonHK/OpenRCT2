# Immutable terrain snapshots and frame pacing

Work follows checkpoint `6f7ea9c1ec` on `codex/gpu-snapshot-terrain-layers`. That commit preserves the measured GPU terrain-only baseline: 355.05–355.82 TPS, 0.363–0.366 ms CPU Draw, and 142.37–142.68 application FPS at 4K. It is intentionally incomplete and not installed over checkpoint52.

## Qualified checkpoint: build92

The final candidate passes **72 focused tests**, including real Vulkan output, overflow recovery, immutable publication, animation state and audio equivalence, with clean Vulkan/synchronization validation. Build92 has zero warnings/errors and unchanged build inputs. Two matched endurance runs and a separate uncapped headroom run pass. The installation remains checkpoint52; this is a source/binary checkpoint, not a deployment or full-render parity claim.

All rows below run the same EverythingPark input at 3840×2160, VSync enabled on the reported 144 Hz display, with 100 warmup and **12,000 measured ticks**. The final population is 17,042 guests. The first three rows preserve ordinary Turbo's 360 TPS cap.

| Run | TPS | Application FPS | CPU Draw ms | GPU ms | Frame p99 ms | Worst interval ms |
|---|---:|---:|---:|---:|---:|---:|
| Prior build85 control | 322.525 | 141.911 | 0.380390 | 0.503148 | 9.102 | 96.622 |
| Final build92, repeat 1 | 358.825 | 143.919 | 0.080657 | 1.030599 | 9.109 | 28.913 |
| Final build92, repeat 2 | 359.958 | 144.013 | 0.080832 | 1.015374 | 9.146 | 9.907 |
| Build92, uncapped diagnostic | 423.672 | 144.013 | 0.082856 | 1.032456 | 8.554 | 9.816 |

CPU Draw falls approximately **79%** while adding cliff faces and water masks/overlays. The capped endurance result improves 11.3–11.6%; this compares two incomplete renderers, not equivalent full-world output. GPU cost increases by roughly 0.52 ms for the added layers. CPU Draw excludes asynchronous submission/GPU work and is not the entire presentation budget. Candidate phase instrumentation is included in elapsed time and was absent in build85.

The final repeat's four 3000-tick windows take 8.3353, 8.3341, 8.3326 and 8.3352 seconds. The uncapped diagnostic reaches 423.672 TPS overall and approximately **388 TPS in the last window**, retaining 144.013 application FPS. This supports headroom at the fuller population; it does not change gameplay speed or imply the missing families will be free.

The scheduler now subtracts already elapsed work from its wait budget, preserves fractional waits and uses a reusable Windows high-resolution waitable timer. Expired deadlines do not yield. Fixed-frame mode has only one idle wait. Message pumping falls from 1,964,812 calls in build89 to 11,706–12,237 in final runs. The earlier 28.5 ms zero-duration yield branch is removed; final worst scheduler waits are 2.58/2.22 ms.

**Pacing remains partially qualified.** Repeat 1 retains a 26.61 ms simulation event with 7.05 million thread cycles, producing the 28.91 ms interval; repeat 2's longest simulation tick is 3.42 ms. The outlier has disproportionate wall time, consistent with off-CPU time or waiting, but this does not establish an OS/driver cause. No recurring expensive renderer branch is demonstrated. Hidden-window draw intervals do not measure physical scanout; displayed pacing is still an acceptance item.

Evidence: `obj/vulkan-parity/build-92/receipt.json`, `snapshot92-focused/summary.json`, immutable `performance-snapshot-build92/snapshot.json`, and `performance-snapshot92-12000-{01,02,headroom}/summary.json`. All endurance runs finish at tick 3,145,931 with entity checksum `07d58eaefde6aa6d000000000000000000000000`; this is not a complete map/ride/finance hash. Screenshots are captured after measurement. An agent manually inspected final samples; the repeat-2 RGBA difference is 4,148 animated water-palette pixels with **zero indexed differences**, as detailed in the [visual review](vulkan-snapshot-terrain-visual-review.md).

The final separate upload-instrumented run, `performance-snapshot92-12000-uploads`, passes with **4,787 submitted frames, 133 world buffer-copy calls and 1,097,728 world transfer bytes**. There are zero image readback/capture requests, allocation failures, lost samples or overflow. The asynchronous safety word accounts for 19,148 readback bytes (four per frame), retired through existing fences. Additional traffic is 4,901,888 palette-image bytes, 4,264 atlas-image bytes plus 832 descriptor bytes, and 62,940,504 directly written UI-command bytes. These are API payload counters, not measured physical bus bandwidth. Initial residency happens during warmup; unchanged terrain remains resident, and only subsequently changed chunks are counted here. Field-minimal updates for moving families remain future work.

## Acceptance target

The owner clarified that ordinary Turbo must remain capped at 360 TPS. The practical endurance workload is EverythingPark for 12,000 measured ticks as its guest population fills, at physical 3840×2160 with VSync and a 144 Hz display. Sustain approximately 360 TPS and 144 FPS with good pacing; higher uncapped throughput is only a secondary diagnostic. Hidden-window intervals do not establish physical scanout pacing.

## Endurance control

The pinned build85 control completed 100 warmup plus 12,000 measured ticks at 4K/VSync, with a hidden window on the 144 Hz display. It reached **322.525 TPS / 141.911 FPS**, with **0.380390 ms CPU Draw** and **0.503148 ms GPU**. Application intervals were p95 **8.568 ms**, p99 **9.102 ms**, maximum **96.622 ms**; the longest simulation tick was **94.407 ms**. The final census contained **17,042 guests**, 2,208 staff and 2,128 vehicles. This longer control supersedes the 3,000-tick number as the practical workload target.

Evidence: `obj/vulkan-parity/performance-partial85-12000-control/summary.json`; immutable binaries/shaders in `performance-partial-build85/snapshot.json`. Final simulation tick is 3,145,931 and entity checksum is `07d58eaefde6aa6d000000000000000000000000`. A candidate must preserve workload, census and checksum comparability; this checksum does not cover every map, ride or financial field.

## Snapshot and transfer contract

- One owner boundary admits a completed immutable generation and captures the next generation before window/world traversal. A worker becoming ready during drawing cannot change that frame's world state.
- Capture raw graphical terrain facts into owned storage. Workers and the Vulkan submission thread never read live tiles or simulation entities.
- A slow worker leaves the previous complete generation usable. Bootstrap and explicit lifecycle recovery are distinct from steady-state publication; no steady-state synchronization waits.
- Coalesce changed graphical state into contiguous staging buffers and batched Vulkan copies. Mutation methods mark state, never issue GPU calls. Track bytes and API call count separately; no per-tile upload calls.
- Every accepted frame redraws the visible world. No stale pixel chunks, lazy repaint or raster reuse.
- GPU output capacity errors suppress unsafe drawing and are reported through one asynchronous safety word retired by the existing submission fence. This is distinct from image readback and adds no owner-thread fence wait.

## Checklist

- [x] Create the branch and commit the existing checkpoint.
- [x] Delete obsolete viewport pixel-shift/recursive strip repaint code and duplicate publication calls from Painter/Viewport. Runtime lifecycle checks pass in build88.
- [x] Replace native tile-element copying and eight CPU image selections with raw surface facts; preserve coherent map/entity source tick and held generation lifetime.
- [x] Select surface images in shaders from generation-owned material tables.
- [x] Add GPU cliff sides and water masks/overlays without restoring the CPU painter.
- [x] Batch world updates and measure upload bytes plus buffer-copy API counts.
- [x] Correct scheduler wait accounting without changing the gameplay Turbo cap.
- [x] Record bounded worst-phase events and 3000-tick windows without logging in the timed loop; remove the observed zero-duration yield/polling branch.
- [ ] Attribute residual occasional off-CPU simulation stalls if they recur; qualify displayed pacing.
- [x] Build, run focused state/lifetime/GPU validation checks, then compare the 12,000-tick workload with the pinned build85 control.
- [x] Save final 4K images, have an agent inspect new layers and remaining defects, and record corrected expectations without claiming full parity.
- [x] Measure separate uncapped headroom without changing ordinary Turbo's 360 TPS ceiling.
- [ ] Verify displayed VSync pacing separately; preserve unsupported families and auxiliary views as explicit remaining work.

Next: complete water sides/intersections and terrain modes, then add native paths/additions and scenery with common ordering before moving entities. Each family must consume the same immutable generation, use batched state updates, remove obsolete preparation, and repeat this 12,000-tick/4K measurement plus manual visual review. Tracks/supports, vehicles, peeps, effects, auxiliary views, picking, LightFX, HDR fidelity and final CPU/X8 deletion remain open. Missing objects do not count as fixed ordering or parity exceptions.

The sections below retain intermediate measurements and failures as historical evidence.

## First measured candidate: build88

Build88 passed with zero warnings/errors and unchanged source inputs. `snapshot88-focused` passes all 39 selected tests, including actual Vulkan output/overflow, submission lifecycle, publication lifetime and scheduler checks. Validation and synchronization validation report no errors. This is a focused qualification, not restored full-world parity.

`performance-snapshot88-12000-01` passes the matched 12,000-tick control comparison: **336.701 TPS**, **143.968 FPS**, **0.086560 ms CPU Draw**, **0.942231 ms GPU**. Intervals are p95 **8.582 ms**, p99 **8.965 ms**, maximum **12.040 ms**; longest simulation tick **7.505 ms**. The four 3,000-tick windows take 8.4641, 8.7019, 9.0027 and 9.4711 seconds, so late-run throughput is still below the target. Final census/checksum match the control; screenshot publication source tick is 3,145,929, two ticks behind the authoritative 3,145,931. This is an expected immutable-generation lag, not mid-frame mutation.

New bounded main-thread phase timing is included in candidate elapsed time but absent from build85. Nested phases overlap and thread-cycle counts are not CPU milliseconds. This is a combined implementation comparison, not an isolated scheduler experiment. The ordinary wait accounting changed; the existing 100 ms catch-up limit and CPU-set policy did not.

The separate `performance-snapshot88-12000-profile` attributes approximately **1.657 ms per UI frame** to presentation audio, including **0.620 ms** to crowd audio. `MapAnimations::InvalidateAndUpdateAll` costs **0.314 ms per logical tick** despite the ordinary world painter having been removed. Follow-up removes unused crowd attenuation/filter calculations and obsolete raster animation invalidation, while retaining real simulation state transitions. Audio remains enabled in the workload; benchmark-only silencing is not a performance fix.

The separate upload-instrumented `performance-snapshot88-12000-uploads` passes: 4,965 frames, **133 world buffer-copy calls**, **1,097,728 world transfer bytes**, zero failed allocations/lost samples/overflow, and zero image capture/readback requests. Safety telemetry is separately **4,965 × 4 bytes**. UI commands, palette updates and small dynamic atlas updates are additional traffic; this is not physical bus-bandwidth measurement or final field-minimal transport for every family.

The [manual visual review](vulkan-snapshot-terrain-visual-review.md) confirms new cliff faces and water in the real park and examines all seven synthetic GPU samples. The tall corner column was subsequently confirmed as saved terrain, as described below; water sides, overlapping-water ordering and the omitted world families remain open.

## CPU cleanup: builds89–90

Build89 compiles cleanly. Its first expanded test run exposed two fixture problems: the clock fixture queued but did not commit its guest's spatial membership, and a peep catalog test assumed first-fit image allocation must return the same numerical base after an unrelated park import. Build90 fixes those setups/assertions without weakening clock behavior or stale-generation rejection. All **71 focused tests pass**, with clean validation. Build89 and build90 application/shader binaries are identical; only the tests changed.

The matched `performance-snapshot89-12000-01` run passes with **359.717 TPS / 143.557 FPS**, **0.082368 ms CPU Draw**, and **1.060796 ms GPU**. All four 3,000-tick windows remain near the cap (8.339–8.343 seconds each), including the final population. Final census and entity checksum still match build85. Simulation averages **1.843108 ms per tick**, versus build88's 2.182511 ms. This is the first checkpoint to meet the approximate endurance throughput target; it does not yet qualify pacing.

MapAnimation.cpp removes 623 lines and adds 103: raster-only tile tracking, viewport projection scans and invalidation handlers are gone, along with their public API/callers. Persistent clock and wall-door work keeps its logical cadence; temporary photo and land-door work remains. Actual map changes now use coalesced presentation notification. Legacy setters still lack owning coordinates, so these notifications currently live in the animation owner with known tile coordinates; moving that ownership into tile modules remains cleanup work. Mixed door/animated objects now advance once per scheduled logical update, instead of incidental viewport-dependent advancement; malformed clock-only flags likewise receive no accidental updates through unrelated repaint work. These are explicit behavioral corrections, not silent parity waivers.

The clock/audio/photo/door tests pass, including 972 source-position comparisons for exact crowd angles and aggregate sector weights/elevations. Audio frequency, channels, volumes and live simulation are unchanged.

Pacing remains open: the run's maximum interval is **39.079 ms**. One retained event identifies a zero-requested-duration `std::this_thread::yield()` taking **28.5107 ms** while consuming only **12,688 thread cycles**; other simulation events take 27–30 ms without proportionate thread cycles. These indicate off-CPU time or waiting, not proof of a particular OS/driver cause. The next change replaces unconditional sub-millisecond yields/repeated message polling with bounded precise waits, and will be measured again.

The corner column is confirmed saved terrain: tile `(254,1)` has baseZ 2032, slope zero; front border neighbors are at 16. Its projected position and 504-pixel cliff height match the 4K image. The imported fixture and benchmark input have identical SHA256. See the visual review for the exact projection and artifact hashes. Water intersections and omitted families remain unqualified.
