# Native ghosts, props and tracks checkpoint

Baseline: accepted paths implementation `9b02bf4661`, acceptance `2af12bbc3c`, immutable build98 performance package. This checkpoint adds construction ghosts, scenery props and track art to the native GPU world path. Ordinary Turbo remains capped at 360 TPS. No automatic goal cycles are resumed.

## Acceptance checklist

- [x] Publish pointer-free prop and track state in the same immutable generation as terrain/paths; preserve held generations, removals, reloads and object catalog lifetimes.
- [x] Batch changed raw records into resident GPU buffers; camera-only frames and unchanged instances must not re-upload world state.
- [x] Render path and addition ghosts independently using the original construction palette.
- [x] Render small/large scenery, walls and static banners from shared original-art catalogs; GPU selects directions, colours and animation frames.
- [x] Render supported track styles/pieces from shared native rule catalogs; record exact coverage and reject unsupported extraction rather than guessing image indices.
- [x] Render scenery and track ghosts through the same native rules as committed elements.
- [ ] Check interleaving with terrain, water and paths, especially tracks behind foreground trees, all camera rotations and camera motion.
- [x] Verify damaged lamps, bins and benches select broken art, broken bins override full-bin art, and repairs publish without stale pixels. Check rendered samples as well as state/rule tests.
- [x] Run focused actual Vulkan tests serially with validation and synchronization validation; preserve failure artifacts.
- [x] Compare original-art samples to the pinned upstream reference and have an agent inspect every divergent group. Only the already accepted outer cliff skirt is waived.
- [x] Run matched 3840×2160 VSync EverythingPark endurance: 100 warmup + 12,000 measured ticks, four 3000-tick windows, same final state/checksum as build98.
- [ ] Record CPU/GPU time, FPS/TPS, tail frame intervals and separate uncapped/upload evidence; preserve approximately 360 TPS / 144 FPS.
- [ ] Deploy receipt-qualified artifacts with verified preimage backups for interactive construction testing, then record checkpoint and remaining work.
- [x] Deploy build106 at the owner's request while reviews continue; 26 qualified files verified, 5 replaced, preimages backed up.
- [x] Record owner's interactive feedback: station pieces, special pieces and curves are visibly missing. These are required follow-up work, not accepted parity exceptions.
- [ ] Close same-tile tree/rail and banner-pole occlusion differences found in the original-art corpus; dynamic lettering remains missing.
- [ ] Owner approved extending the next implementation to flat rides, entrances, exits and static buildings using [the flat-ride plan](vulkan-native-flat-rides-plan.md); finish stations and missing track geometry alongside them. Animation remains the following larger checkpoint.

## Implementation contract

The map snapshot owns variable-length object lists in immutable 256-tile chunks. Per-instance records contain only graphical facts: object family and slot, tile-relative state, heights, orientation, colours, animation state and track/ride identifiers. Shared object rules and art are resolved once per catalog generation. Camera and source tick are frame-global. The renderer does not call the CPU world painters or build per-instance sprite commands on the CPU.

One persistent object arena serves props and tracks. Dirty chunk ranges use batched transfers; capacity and per-tile dispatch bounds are explicit rejection conditions, never silent truncation. The GPU redraws the visible world each frame. This is not cached screen repainting.

Prop residency follows an immutable used-material inventory. The map snapshot updates occurrence counts only when changed tile lists change object references; ordinary colour/ghost/animation changes bypass that accounting. Membership changes rebuild the shared catalog, while held generations retain their own counts, inventory and asset leases. Track residency follows present rides' shared styles. These inventories select shared art to retain, never sprites to draw for individual instances.

Track rules vary by style, sequence, direction and operating state. Any source translation used to author shared rule tables runs offline and reports unsupported constructs. Runtime shader selection consumes raw instance state and those immutable tables. Full track/support/station parity must not be inferred from partial rule coverage.

Ride graphical metadata is initially captured by a bounded comparison of ride-owned facts at the publication boundary. A new immutable catalog is retained only when graphical values differ. This avoids missing direct legacy mutations while keeping graphics decisions off the CPU; a future object-owned mutation API can remove the bounded scan.

The owner requested a separate planning study of update granularity and hot pose arrays. A fresh agent produced [the GPU state-update strategy](vulkan-gpu-state-update-strategy.md); its proposed packing/family changes are not implemented here. Newly introduced brake-only notifications use the existing graphical dirty marker to avoid adding ride-rating invalidations. They still use the current tile/chunk publication granularity.

## Evidence

### Deployed candidate

Build106 has zero compiler warnings/errors and unchanged source inputs during its build. Its shader hashes match the inspected build103 corpus. The 118 actual Vulkan/publication/scheduler checks passed in build101; later residency changes passed nine focused CPU checks in build103 and all 13 selected catalog/usage/atlas CPU tests in build106. The complete enlarged GPU suite has not been rerun against build106. GPU workloads are paused while the owner tests the installed game.

| Matched 4K/VSync, 12,000 ticks | TPS | Application FPS | CPU Draw ms | GPU ms | Frame p99 ms | Worst interval ms |
|---|---:|---:|---:|---:|---:|---:|
| Fresh accepted build98 control | 359.956 | 143.352 | 0.102572 | 1.450594 | 9.114 | 36.116 |
| Deployed build106 | 359.542 | 140.162 | 0.133015 | 1.590457 | 9.323 | 97.000 |

CPU Draw increases by about 0.030 ms and GPU work by 0.140 ms while adding the new families. Draw cost is not the entire publication/submission cost. The four 3000-tick windows take 8.3369964, 8.3330707, 8.4398104 and 8.2658982 seconds. Final tick 3145931, guest/staff/vehicle census and entity checksum `07d58eaefde6aa6d000000000000000000000000` match the control.

**Frame pacing is not accepted as solved.** Candidate simulation samples include 94.504, 91.031 and 90.871 ms wall intervals with only about 5.7–6.0 million thread cycles, plus a 31.4095 ms scheduler wait requested for 0.381 ms. The 97 ms frame interval overlaps the later simulation stall, not the largest 94.504 ms sample. These are consistent with blocked/descheduled time but do not identify its cause. The control also stalls, but the candidate's worse tails cannot be dismissed as equivalent noise without paired repeats/attribution. Separate uncapped headroom and upload-telemetry runs remain pending.

Deployment `deployment-objects-build106-01/receipt.json` reports `deployed-verified` at `D:\Games\Independent\OpenRCT2Mod`: 26 qualified files verified and 5 replaced. Preimages are in its `backup` directory; the manifest pins prior/current hashes. No saves, objects or configuration were changed. Build receipt SHA-256: `90f3e27b92bf6a4c7aa07ae0c5638ad1efae617e31ab1fc39101493f58b3a5b4`.

### Qualification history

Build106 is deployed for owner testing; this remains an incremental checkpoint with unwaived ordering and missing-feature gaps. Final acceptance and additional performance lanes remain open. Build98 measured 359.953 TPS, 144.011 application FPS, 0.106977 ms CPU Draw, 1.427410 ms GPU and 9.919 ms maximum interval over 12,000 ticks. Application timestamps are not independent physical scanout measurements.

The fresh build98 control (`performance-object-baseline98-12000`) completes the same 4K/12,000-tick workload at 359.956 TPS / 143.352 FPS, 0.102572 ms CPU Draw and 1.450594 ms GPU. Its p99 interval is 9.114 ms and maximum is 36.116 ms, alongside a 33.982 ms simulation batch. Final census/checksum match. Occasional long intervals therefore remain reproducible before the object-layer change.

Build101 passes all 118 focused tests with zero skips or Vulkan/synchronization diagnostics. An agent manually inspected all 121 synthetic object/path PNGs; the test art validates state changes and composition, not original-art parity. Damaged lamps/bins/benches, repaired state, independent ghosts, animation without instance uploads, glass filtering, arena recovery, catalog preflight and shader track selection all pass.

Build100's interrupted test run is preserved: it contained a stale 44-byte ABI expectation (the directory is now 56 bytes), and fresh pipeline creation took about 23 seconds. Sharing count/write and height-region visitor call sites reduces creation to about 6 seconds in build101, matching the previous path baseline; it preserves the painter sequence and all focused pixel assertions. Build102's six track-catalog/rule checks also pass.

The original-art attempts in build101/102 exposed atlas exhaustion before rasterization. Resolving all loaded object art is not an acceptable residency policy. Track art is now selected by present ride styles at catalog generation; build103 adds the corresponding incremental used-prop inventory. These failed attempts are preserved and do not establish image parity.

Build103 completes all eight original-art captures and nine usage/catalog checks. [Agent inspection and local indexed measurements](vulkan-native-object-visual-review.md) find all 72 addition specimens exact, as well as the measured walls, large signs and isolated trees. Banner-pole occlusion and same-tile tree/rail overlap remain unwaived defects. The full-frame test remains failed; its background difference and intended outer skirt are not removed from its counts.

EverythingPark then exposed two bootstrap limits: its used art exceeds the former 64-layer/256 MiB atlas, and the combined initial texture/state upload exceeds the 96 MiB upload ring. Build104 raises the bounded atlas to 128 layers/512 MiB; build105 changed only the device fallback and exposed a duplicate 96 MiB backend default; build106 replaces both with one shared 128 MiB per-frame-slot setting. Device limits remain checked, no required images are dropped, and the failed103/104/105 runs are preserved. Capacity changes do not add per-frame image uploads; steady-state performance still needs qualification.
