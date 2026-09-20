# Post-checkpoint corrections and 4K measurements

The owner requested a stable stopping point for another day. Build 56 is the validated source checkpoint; `3b28313a1b` / checkpoint52 remains installed. The work is not Vulkan-exclusive or fully visually qualified. See the separate [new orthographic renderer proposal](vulkan-orthographic-renderer-proposal.md) before choosing the next implementation direction.

## Owner feedback

The owner reports darker HDR output, foreground cherry canopies occluded by supports, and vehicles intermittently changing the order on affected tiles. The owner subsequently confirmed the ordering defect in **both deployed renderers**. Deployed software is therefore a candidate, not the canonical reference. Diagnosis uses the immutable pre-migration reference `9a092745f3` and may require a narrowly reviewed correction if that reference also contains the defect.

| Work | Evidence / remaining acceptance |
|---|---|
| HDR balance | Windows DISPLAY1 reports 280-nit SDR white; current Vulkan used 203 nits. Display-aware immutable-frame white and matching metadata are implemented. Final build56/run48 passes800/800 tests, including72 GPU reference samples and exact SDR controls, with clean synchronization validation. Actual HDR display appearance remains a manual check. |
| Static ordering | Frozen Trinity full/dirty controls: 24 exact captures; manually inspected conifer/track overlap. This saved camera lacks the supplied cherry/lattice scene and is not a confirmed reproduction. Current renderer comparisons remain pending. |
| Vehicle flicker | Common frozen/current consecutive-tick diagnostic implemented and frozen22 compiled. Its first frozen Trinity run captured34frames: each of16 advancing ticks differs between damage/full paints. Published-state lag is a confounder; this is not a passing golden or a confirmed ordering reproduction. No ordering exception or production sort change accepted. |
| Deadline scheduling | Experiment withdrawn from production after measured TPS regression. Original scheduling is restored; prototype, tests and failed performance decision remain in obj/vulkan-parity. |
| World preparation | Still CPU command generation for ordinary parks. Retained native terrain admission remains closed. Guest/staff raw-state and GPU-selector drafts are preserved but incomplete/unapplied. Review the orthographic replacement proposal before expanding the old painter integration. |

## New required workload

Every performance lane below uses EverythingPark's saved camera, ordinary Turbo, **observed 3840×2160 physical output**, scale 1, 100 warmup ticks and **3,000 measured logical ticks**. SDL reports 144 Hz at both measurement boundaries. Windows are hidden; these numbers describe application drawing, **not displayed scanout pacing**. Scale 1 exposes more world area than the owner's 1.75 UI scale, so these measurements are not interchangeable with their interactive view. Each run uses an isolated profile, no capture, no validation and no profiling. Software/Vulkan initial/final state censuses and final checksum `9b7eee204b0471d7000000000000000000000000` agree.

Build 53 adds physical-size reporting to checkpoint 52 behavior; the scheduling/HDR corrections are not included in this baseline.

| Build 53 baseline | TPS | Draws/sec | CPU draw ms | GPU ms | Frame p50 / p95 / p99 ms |
|---|---:|---:|---:|---:|---:|
| Software, uncapped |66.220|66.242|10.427|unavailable|15.038 /15.658 /16.495|
| Vulkan, uncapped |31.541|31.551|27.003|1.671|31.300 /33.541 /39.874|
| Software, VSync |86.916|49.861|13.650|unavailable|21.144 /21.784 /22.183|
| Vulkan, VSync |71.737|29.675|26.080|1.641|34.255 /35.894 /36.484|

These are four initial descriptive runs, not a counterbalanced acceptance series. Vulkan's CPU frame preparation is the dominant measured cost. Its VSync TPS increase comes with fewer rendered frames; it does not demonstrate a throughput gain at equivalent visual work. A scheduling correction cannot eliminate 27 ms of CPU preparation. The required architecture is simulation that publishes simulation state only, resident VRAM assets and world state, and GPU decisions for sprite/texture selection, animation, visibility, quad offsets/transforms and ordering. CPU-generated per-object draw streams are transitional and do not satisfy the goal. The owner used >1,000 FPS to communicate this architecture and the light GPU workload, not as a standalone benchmark promise. Acceptance measures sustained VSync pacing and simulation throughput after removing visual preparation from simulation.

Evidence lives under `obj/vulkan-parity`: `build-53/receipt.json`, `performance-build53/snapshot.json`, and the four `perf-build53-4k-{software,vulkan}-vsync{0,1}-01/summary.json` directories. Each summary pins executable/shaders, asset manifests, config, park, launch, log, actual extent and state checkpoints. The helper now defaults to 4K/3,000 ticks; `--require-display-evidence` rejects missing/wrong drawable size or changed refresh. Four parser/acceptance guards pass.

## Withdrawn scheduling experiment

Build55 used the frame-start deadline and passed804 tests, but its actual4K/3,000-tick VSync run measured **33.105TPS /33.116draws per second**, versus baseline **71.737TPS /29.675draws per second**. Frame p50/p95/p99 improved from34.255/35.894/36.484ms to30.003/31.666/33.034ms, at a53.85% TPS loss. The producer spent more time making frames and less time simulating. This is not an acceptable improvement for this checkpoint. Build56 restores the original scheduler and removes the unused prototype helper/tests from live source.

Preserved evidence: `presentation-start-deadline-staged`, `presentation-start-deadline-withdrawal.json`, `build-55`, `run-47`, and `perf-build55-4k-vulkan-vsync1-01`. The earlier `run-46` remains failed despite804 passing assertions because its new HDR test readback barrier produced synchronization errors. A test-only barrier correction matching the production readback contract produced clean run47; no validation message was suppressed.

## Final source checkpoint

The final build 56 Vulkan VSync smoke restores the earlier throughput regime: **70.461 TPS, 30.157 draws/sec, 25.722 ms CPU draw and 1.817 ms GPU time**. Frame intervals are 33.926 / 35.349 / 35.999 ms at p50 / p95 / p99. This uses the same observed 4K, 144 Hz, 3,000-tick workload above. TPS is 1.78% below the initial baseline in this single descriptive run; no performance improvement or displayed-pacing qualification is claimed. Evidence: `perf-build56-4k-vulkan-vsync1-01/summary.json`.

An agent manually inspected all 16 divergent frozen motion pairs, their difference masks and changed-region crops. Changes follow vehicles and walking guests; this does not establish a static tree/support defect. Differing damage pixels match the prior tick's full image in 15 of 16 pairs, reinforcing the need to identify the consumed publication revision. The agent also independently checked all 72 HDR samples: at most one ten-bit code of error, with exact SDR bytes. The review is pinned in `final-motion-hdr-review/manual-review.json` (SHA-256 `4d50cec60a281a7216f08df93e8ecaca018f1cccf5da0aab8fc125b5c4f3b2df`). No new pixel exception is accepted.

Build56 passes with zero warnings/errors. Run48 passes **800 tests in86 suites**, including66 parity tests and the72-sample HDR/SDR corpus, with no failed/skipped/disabled tests and clean synchronization validation. The common frozen diagnostic driver22 also compiles. Pure evidence guards pass: four physical-display checks, four tree/track census checks, four static dirty-world checks and five temporal checks.

Retained changes are display-aware HDR white, physical extent/refresh benchmark reporting,4K/3,000-tick benchmark defaults, coarse optional profiling scopes, and diagnostic ordering/scene-location tooling. The proposed scheduling behavior is absent from the final code. Protected software rasterizer sources verify unchanged. Current/Vulkan builds of the expanded diagnostic driver and full-scene HDR appearance are future work; passing ordinary unit suites does not substitute for them.

The installed game remains checkpoint52. No partial peep or replacement-renderer implementation is enabled. The source working tree is left uncommitted for the owner's next checkpoint commit; build/run receipts and immutable binary snapshots are preserved.

## Resume map

1. Read the orthographic replacement proposal with the owner. The first meaningful architecture proof is a mixed terrain/tree/support/moving-vehicle scene with GPU visual selection and geometric depth/visibility, independent of legacy CPU paint calls.
2. For the existing ordering investigation, use `ordering-frozen-trinity-motion-01` only as failed diagnostic evidence. Establish which publication revision each capture consumed before treating same-live-tick frames as equivalent. Trinity's census found no wooden-track/tree candidate, so it is not the supplied cherry/lattice scene.
3. The common driver now writes a tree/wooden-track locator census. Next candidate is repository EverythingPark; user save candidates and hashes are preserved in `checkpoint3b28313a-ordering-feedback-review/candidate-parks.json`. Current/Vulkan temporal comparisons, interpolation and actual cherry scenes are not yet qualified. Older static receipts lack the newly required census and cannot serve as new-family references without renewal.
4. Preserve drafts `retained-peep-foundation-staged` and `peep-gpu-rules-staged`. They are unbuilt, unreviewed and unapproved for application. The raw-state draft has a known same-epoch reset/tombstone lifetime issue; tests and load-boundary proof are absent. GPU rules lack culling/linked-zoom/interpolation/LightFX/common geometric ordering and device proof. Reuse appropriate state/catalog ideas only after selecting the renderer direction.
5. The pure `invalidation-callback-probe-staged` identifies a possible reentrant helper contract issue, but ordinary-frame reachability was not established. Both frozen/current painters acquire presentation before damage traversal. Do not resume that investigation as though it were the reported ordering cause.
6. Final HDR display review, clean repeated4K performance, displayed pacing evidence, arbitrary-world GPU state/geometry, auxiliary callers, platform qualification and software removal remain open.

## Next acceptance checks

- [x] Final build56/run48:800/800 tests, original scheduler, exact SDR controls and72owned-buffer HDR reference samples; clean synchronization validation.
- [x] Recheck the restored scheduler at observed 4K for 3,000 ticks and preserve agent review of all 16 motion divergences.
- [ ] Confirm HDR appearance on the actual display.
- [ ] Reproduce ordering against the true frozen reference; manually inspect every divergent sample and the corrected result.
- [ ] Repeat observed 4K/3,000-tick clean performance lanes after changes, with a separate attribution/upload run.
- [ ] Establish displayed VSync pacing using actual presentation evidence; application percentiles alone do not close this gate.
- [ ] Reduce ordinary-park CPU command preparation/copies through retained state and immutable publication, then measure progress toward 360 TPS.
- [ ] Deploy the next qualified checkpoint with rollback and updated receipts.

The [migration checklist](vulkan-exclusive-migration-plan.md) retains all remaining parity, auxiliary, platform and software-removal gates.
