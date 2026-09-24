# Physical depth checkpoint

Status: implementation prototype after source checkpoint `50f16fb7e5`; no new deployment qualification.

## Contract

Graphical world state is retained in VRAM. Object movement, construction, removal and relevant graphical attributes publish snapshot deltas. Camera position/rotation/zoom are global uniforms. Rendering must not compare objects with their neighbours to rebuild painter priorities on ordinary frames.

For camera-rotated coordinates `(X,Y,Z)`, the game's orthographic projection is `u = Y-X`, `v = (X+Y)/2-Z`. Physical near-depth is proportional to `D = X+Y+Z`. The hardware z-buffer resolves opaque coverage. A component's local layer only breaks intentional coplanar ties; it is not a family-wide ranking of tracks, scenery or guests.

Sprite art remains an indexed texture with transparent holes. Its depth comes from the represented geometry: a horizontal surface at height `h` has `D = 2v+3h`; upright art at anchor `(X,Y)` has `D = 1.5(X+Y)-v`. Finite-height structures and slopes need their actual depth geometry. A flat quad texture does not require a flat, constant depth value. Filter fragments must use the identical interpolated hardware depth when applying ordered palette effects.

The first slice removes the legacy arrangement passes and approximately147.9MiB of per-pipeline arrangement buffers (64MiB duplicate instances, about55.6MiB parent/tile metadata and28.25MiB workspace), retaining existing GPU image selection and snapshot publication. It does not yet retain every generated component: materialization still visits visible world state each frame. That remaining work is measured separately and must not be described as completed persistent topology migration.

## Gates

- [x] Preserve the previous correctness source at `50f16fb7e5`.
- [x] Remove the host column arranger, duplicate ordered-instance arena and bounds profiling readback.
- [ ] Build and exercise physical-depth shader emission and composition without an arrangement fallback.
- [ ] Verify foreground UI, rotation/pan invariance, camera sampling, local layers and removal/reuse.
- [ ] Inspect underground/normal original-art samples, then track/water/ghost and animation samples. Keep exact failures.
- [ ] Measure at least3000 ticks at3840x2160; record actual accepted presents, CPU/GPU time and pacing.
- [ ] Fix finite geometry, slopes and any precision errors exposed by those images.
- [ ] Replace camera-dependent component reconstruction with retained topology and graphical delta updates where measurements justify it.
- [ ] Final12000-tick qualification at360TPS/~144FPS, including secondary previews, followed by deployment.

## Prior measured cost

Build126's3000-tick4K diagnostic sustained359.840TPS but only118.987 accepted presents/s. Its GPU frame mean was8.409ms, including5.127ms arrangement,0.756ms column counting and0.065ms column prefixing. CPU draw preparation was0.160ms/attempt. Accepted-present p99 was14.8ms (histogram upper bound), maximum16.483ms. Parent-only compiler optimization127 failed to improve throughput (118.379 accepted presents/s) and was reverted. These are instrumentation runs, not clean final qualification.

The depth replacement must be evaluated with its rendering defects visible; fast incomplete images are architectural evidence, not proof of parity.

## Measured first slice

Build129 removes the world column arranger from execution and source. Its3 focused GPU tests pass: foreground tall props over rear props across every rotation, camera-pan image invariance with zero object-buffer copies, and authored background filtering with foreground UI. No Vulkan validation diagnostics were reported.

Two serial3840x2160 runs use the same EverythingPark save,100 warmup ticks and3000 measured ticks. Both end at tick3136931 with checksum `9b7eee204b0471d7000000000000000000000000` and14477 guests. The matching128 control uses authoring16, so the recipe population is shared with129.

| Measurement |128 legacy GPU arranger |129 physical depth |
|---|---:|---:|
| TPS |359.823 |359.821 |
| Accepted presents/s, measurement interval |78.441 |143.928 |
| Mean GPU frame |12.766ms |3.169ms |
| CPU draw preparation/attempt |0.175ms |0.161ms |
| Accepted-present p99 upper bound |16.0ms |9.4ms |
| Maximum accepted-present interval |19.231ms |9.758ms |

This is about75% less mean GPU frame time in this pair. Both runs enable stage instrumentation and render an incomplete set of world categories. The129 picture also has new geometry defects, so the table validates removal of the bottleneck, not equivalent complete rendering. Accepted presents are queue acceptance, not measured display scanout.

Manual129 review exposed coplanar flat rails/decks losing to grass, inaccurate cliff planes, and partially buried buildings/slopes losing pixels in actual underground view. Build130 adds explicit surface-overlay layers, physical tile-boundary cliff faces, and direct selected-car emission with owned XYZ. Its rail-on-ground, pan, foreground UI and3 snapshot checks pass. The selected-car test's zoom1 control initially lacked a terrain sprite; build131 corrects that fixture, and the pose-depth/empty-tile/removal GPU test and packet validator pass. There are no validation diagnostics in these runs. Corrected output still requires original-art comparison; no geometry error is waived.

The first layer encoding advances representable D32 values. It remains a finite bias, not a mathematical equality-only tie: very close surfaces can move relative to each other. Distinct exactly-coplanar filters currently have a deterministic operation-based tie, which is provisional. Slopes, finite building geometry, explicit authored depth roles and persistent component topology remain open.

Evidence directories: `performance-control128-world-profile-01`, `performance-physical-depth129-world-profile-01`, `regressions129-physical-depth-gpu-01`, `regressions130-physical-depth-01`, `regressions131-selected-depth-01`, all under `obj/vulkan-parity`. Original failed images/receipts remain unchanged.
