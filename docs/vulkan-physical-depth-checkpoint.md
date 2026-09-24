# Physical depth checkpoint

Status: physical-depth architecture committed at `16435967fe`, following correctness source checkpoint `50f16fb7e5`; no new deployment qualification.

## Contract

Graphical world state is retained in VRAM. Object movement, construction, removal and relevant graphical attributes publish snapshot deltas. Camera position/rotation/zoom are global uniforms. Rendering must not compare objects with their neighbours to rebuild painter priorities on ordinary frames.

For camera-rotated coordinates `(X,Y,Z)`, the game's orthographic projection is `u = Y-X`, `v = (X+Y)/2-Z`. Physical near-depth is proportional to `D = X+Y+Z`. The hardware z-buffer resolves opaque coverage. A component's local layer only breaks intentional coplanar ties; it is not a family-wide ranking of tracks, scenery or guests.

Sprite art remains an indexed texture with transparent holes. Its depth comes from the represented geometry: a horizontal surface at height `h` has `D = 2v+3h`; upright art at anchor `(X,Y)` has `D = 1.5(X+Y)-v`. Finite-height structures and slopes need their actual depth geometry. A flat quad texture does not require a flat, constant depth value. Filter fragments must use the identical interpolated hardware depth when applying ordered palette effects.

The first slice removes the legacy arrangement passes and approximately147.9MiB of per-pipeline arrangement buffers (64MiB duplicate instances, about55.6MiB parent/tile metadata and28.25MiB workspace), retaining existing GPU image selection and snapshot publication. It does not yet retain every generated component: materialization still visits visible world state each frame. That remaining work is measured separately and must not be described as completed persistent topology migration.

## Gates

- [x] Preserve the previous correctness source at `50f16fb7e5`.
- [x] Remove the host column arranger, duplicate ordered-instance arena and bounds profiling readback.
- [x] Build and exercise physical-depth shader emission and composition without an arrangement fallback.
- [x] Verify foreground UI, rotation/pan invariance and selected-car sampling/removal; precise coplanar semantics remain open below.
- [ ] Inspect underground/normal original-art samples, then track/water/ghost and animation samples. Keep exact failures.
- [x] Measure at least3000 ticks at3840x2160; record actual accepted presents, CPU/GPU time and pacing.
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

## Longer runs and actual underground viewport

Two clean131 runs use100 warmup ticks and12000 measured ticks at3840x2160 with vsync enabled. Both finish with17042 guests at tick3145931 and checksum `07d58eaefde6aa6d000000000000000000000000`. They preserve simulation results and meet average throughput, but fail the stable-pacing objective:

| Measurement |131 run01 |131 warm-cache repeat03 |
|---|---:|---:|
| TPS |359.499 |359.044 |
| Accepted presents/s, measurement interval |143.650 |143.528 |
| Mean GPU frame |3.237ms |3.148ms |
| CPU draw preparation/attempt |0.165ms |0.170ms |
| Accepted-present p99 upper bound |9.2ms |9.2ms |
| Maximum accepted-present interval |62.747ms |65.521ms |
| Longest simulation phase |58.346ms |63.129ms |

The largest stalls occur at different simulation ticks. Main-thread cycle counts remain near ordinary ticks despite large wall-time gaps. This supports blocked/descheduled execution but does not distinguish worker waits from OS scheduling. The next diagnostic attributes named simulation phases and existing ParallelFor wait seams; it must not change scheduling or simulation behavior. Run02 failed before launch because its cache seed pointed at the parent directory; it is retained as failed evidence, not a third benchmark.

Cold world-pipeline startup in run01 took14.722s (14.859s total); the hash-pinned warm-cache repeat took0.026s (0.059s total). This is application pipeline-cache timing, not a claim that every driver/system cache was cold.

The separate `performance-physical-depth131-inside-preview-01` run exercises actual underground main-view flags plus an ordinary selected carousel preview,100+3000 ticks at4K. It reaches359.823TPS,143.689 interval accepted presents/s,3.111ms GPU and0.227ms CPU draw preparation. P99 is9.3ms and maximum23.147ms. Root inspected the full-park underground grid and populated preview. The exact user-reference scene and original-art parity remain separate gates.

This diagnostic enables upload telemetry:97,875,216 world-buffer bytes across1199 submitted frames (about81.6kB/frame) and4379 batched world copy commands (3.65/frame); no measured image readback, allocation failures or overflow. Atlas texels total7,404,097 bytes during the changing preview workload; world-state bytes are not framebuffer uploads. These are API payload counts, not physical bus bandwidth. All3000-tick runs retain the same final simulation checksum. The screenshot is taken after measurement; its receipt identifies the publication tick separately from simulation tick.

## Remaining geometry

Agent review of all288 build131 normal/underground specimen groups confirms recovery of coplanar rails and decks, but nonflat terrain, cinema/shop podiums, tower platforms and some portal edges remain incorrect. Expanded track review additionally identifies new water-splash front-rail/channel loss under water (specimens61–63). These are active defects, not intended divergences.

Build132 implements an exact planar terrain correction: raw corner heights define the slope plane; camera projection evaluates physical depth with no sorting. Nonplanar and edge-on forms stay explicitly incomplete. Arbitrary original-art building meshes cannot be inferred faithfully from legacy ordering boxes; they need authored geometry/coverage rather than resurrected ordering heuristics.

## Planar terrain, path counterpart and wait attribution

Build132 passes the coordinate tests across raw slope masks/rotations and reproduces all288 underground/control specimen groups for manual review.205 images are byte-identical131 and83 change. Planar terrain improves, but the matching path ramp reveals its own still-horizontal deck depth. Build135 gives sloped path bodies/decks the corresponding physical plane from the original path-to-land slope table; attached decks inherit it. Railings and props retain separate depth roles. Build134 failed GLSL compilation on an implicit unsigned rotation argument;135 fixes that cast and compiles successfully. No runtime evidence is claimed for134.

Build133 adds opt-in simulation phase and ParallelFor wait attribution, with fixed top16 summaries and no measured-loop logging. Sixteen JobPool/terrain tests and19 Python report tests pass. Disabled mode retains no added timing calls or diagnostic group allocations. The133 diagnostic12,000-tick run reaches359.117TPS and143.976 accepted presents/s,2.906ms GPU,0.182ms CPU draw preparation,9.2ms p99 and11.056ms maximum accepted-present interval, with the same final entity checksum as131. It is instrumented, not clean acceptance.

Its two longest ticks are7.139/7.094ms. Vehicle barriers wait4.892/5.249ms;4.791/5.075ms occurs after group work is accounted complete. That rules out late worker computation for most of those particular waits; notification/unlock, wakeup, mutex reacquisition and main-thread scheduling remain possible contributors. The earlier22–63ms stalls did not recur in this diagnostic and are not explained or waived by it. See [wait attribution](vulkan-simulation-wait-attribution.md).

The expanded131 track review covers all1536 groups:906 byte-identical128,174 duplicates of other reviewed changed views, and456 unique changed groups manually inspected. It confirms water-splash rails/end-ramp losses in all72 target views and station/entrance intersections; no additional broad disappearance of isolated ordinary track pieces was observed. See [track review](vulkan-track-regressions131-visual-review.md) and [finite building geometry design](vulkan-finite-building-depth.md).

## Final compiled source checkpoint: build135

The Release build passes22 focused CPU/GPU tests, with no Vulkan validation diagnostics: exact terrain/path plane coordinates, JobPool semantics, tall foreground scenery, camera-pan invariance without object uploads, track recipe state, owned selected-car pose/removal, and foreground UI over terrain filters. The report parser suite passes19 tests.

All288 final normal/underground groups are accounted for.272 images are byte-identical132; every one of the16 changed matching-ramp views improves after the path-plane correction. Ordinary ramp mismatch totals decrease4,714→1,960 and inside-view totals15,029→13,829. Railings, inside filters, nonplanar terrain, sloped tracks and finite building geometry still have failures. Visual review is recorded in [the physical-depth review](vulkan-physical-depth129-visual-review.md); this is not pixel parity.

The final clean `performance-physical-depth135-12000-01` run measures100 warmup+12000 ticks at3840x2160 with vsync and the360TPS cap. It records359.956TPS,143.743 accepted presents/s,2.924ms mean GPU frame and0.166ms CPU draw preparation. All4792 submitted frames are accepted with no unavailable packets or lost timing samples. P99 accepted-present interval is9.2ms, maximum57.097ms; a53.005ms simulation phase at tick3134966 has6,256,992 main-thread cycles. The recurring long-stall gate therefore remains open despite meeting average throughput. Final checksum and17042-guest census match earlier12000-tick runs.

The post-measurement final image is manually viewed separately from performance measurement. Its simulation tick is3145931 and published snapshot tick3145930, a documented one-tick asynchronous lag. No measured-loop screenshot/readback is used. A loaded pipeline cache did not contain the newly compiled shader pipeline, so startup compilation still took15.756s; this is not a same-build warm-cache result.

The source checkpoint removes sorting from the world hot path and demonstrates the expected performance benefit. It is intentionally incomplete geometry, with persistent generated components and residual pacing still open. Installed build112 remains unchanged.

The exact-build135 opt-in repeat (`performance-physical-depth135-waits-12000-01`) records359.947TPS and143.949 accepted presents/s, with16.593ms maximum accepted-present interval. At tick3141317, simulation takes14.618ms; the vehicle batch waits12.342ms, of which12.114ms occurs after group-ready. The longest worker runs0.397ms. This reproduces and enlarges the completion-to-main-resumption delay seen in133. It does not reproduce or conclusively assign the clean run's53ms stall. No scheduler policy, priority, worker count or simulation result is changed.
