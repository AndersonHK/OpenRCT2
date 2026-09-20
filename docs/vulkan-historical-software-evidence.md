# Historical software performance evidence

Audit date: 2026-09-20. This is a read-only provenance audit, not a new benchmark. No build, test, or game process was launched. The independently sealed architecture votes were not revised.

**The historical software result above 300 TPS is supported by surviving raw logs, but it ran at about 13.4 draw FPS, in a hidden window with VSync disabled. Its actual rendering resolution is not recorded in the located evidence. It is not evidence of 300 TPS at 4K, visible VSync cadence, or the later rendering architecture.**

## Original software measurements

The [archived performance journal, Integrated Turbo benchmark](archive/performance-320-tps-refactor-history.md#integrated-turbo-benchmark) records three independent runs per renderer, each with a two-second warm-up and five-second measurement on EverythingPark. It labels software as “Software with hardware display.” The [July 10 overhaul entry](archive/openrct2-overhaul-changelog.md#2026-07-10) repeats the median 318.324 TPS software result and approximately 13.4 FPS. Raw files are present in the local `bin` directory:

| Raw evidence | Measured seconds | Logical ticks | TPS | Draws / FPS | Complete draw, us/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| [software run 1](../bin/benchmark-ui-software.log) | 5.005858 | 1,592 | 318.027 | 66 / 13.185 | 1,002.991 |
| [software run 2](../bin/benchmark-ui-software-2.log) | 5.001188 | 1,592 | 318.324 | 67 / 13.397 | 939.724 |
| [software run 3](../bin/benchmark-ui-software-3.log) | 5.000000 | 1,592 | 318.400 | 67 / 13.400 | 970.191 |

All three explicitly report `renderer=software`, `VSync=disabled`, `hidden window`, `ordinary Turbo`, two seconds of warm-up, and five seconds of measurement. Backend CPU, presentation API, GPU frame and GPU pass timings are unavailable. The published 318.324 TPS / 13.397 FPS / 970.191 us draw row contains independent column medians; all three median values do not come from one run.

Each measured interval has only 1,592 ticks. Three repeated short intervals are not one sustained 3,000-tick interval. The initial simulation tick is 3,134,495 in runs 1 and 2, and 3,134,487 in run 3. Final ticks are respectively 3,136,087 and 3,136,079. Time-based warm-up therefore did not produce identical starting states in every repeat. Runs 1 and 2 grow from 13,516 to 14,125 guests; run 3 grows from 13,513 to 14,123. All report 2,208 staff, 2,128 vehicles, and the same shared-route-cache footprint of 6,663 nodes, 543 targets, 3,611,346 direction entries, 3,611,346 distance entries and 517 single-ride targets. [Raw runs 1–3](../bin/benchmark-ui-software.log)

Final checksums are `1a6fde66772f7990000000000000000000000000` for runs 1 and 2, and `3fc5f1bbea6164fb000000000000000000000000` for run 3. The differing third checksum is associated with its different measured state boundaries; these logs alone do not establish a determinism defect. They also do not supply the exact warm-up tick count.

The logs contain startup messages `Error creating key`, failed object-index persistence, and an index rebuild of 2,534 items before reaching benchmark readiness and successful completion. These should remain visible in provenance. They are not evidence that the measured interval failed, but these were not warning-free launches. The command fragments identify `openrct2.exe` and `testdata/parks/EverythingPark.park`; they do not preserve the complete shell command.

## Resolution, assets and binary provenance

The located software logs contain no window dimensions, drawable dimensions, internal render extent, scale, camera position, rotation, zoom, viewport-worker setting, CPU model, executable hash, commit SHA, or configuration snapshot. The journal calls for a fixed camera/viewport and says the configuration hash and modification time were unchanged, but does not publish that hash or actual dimensions alongside this matrix. No contemporaneous config file was located for these three runs. The existing user config has a September 20, 2026 modification time; it cannot establish the July benchmark settings. The later isolated profile configs under `obj/vulkan-parity` cannot establish them either.

The logs were last written at 2026-07-11 05:48:51, 05:57:43 and 05:57:51 UTC according to current filesystem metadata. This is consistent with the journal's July 10 local-date entry. Filesystem times are supporting metadata, not immutable build provenance.

Git history first introduces the 318.324 documentation in commit `19b44306c69e3ac96b252200e195aeeb0d749445` (2026-07-11 02:30:35 -07:00, “Vulkan, Performance Refactor, and Station Waiting”), in `docs/performance-320-tps-refactor-plan.md` and `docs/openrct2-overhaul-changelog.md`. Commit `4f235b5cb193edb06e4194b8cd0e6df49082aa71` subsequently archives those documents. This identifies when the report entered Git; it does **not** prove that the benchmark executable exactly matches the introducing commit. The raw logs do not close that source-to-binary gap.

The current `bin/testdata/parks/EverythingPark.park` and repository fixture have identical SHA-256 `c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`. The repository fixture's Git blob is `bcecee11c3fe4aa46f2ee20f3eb9bd77237ea42e` at the July reporting commit, the pre-catch-up checkpoint, and the current file. This supports continuity of the park fixture, but the July logs contain no contemporaneous park-file hash or full object inventory; the 2,534-item index is not an asset manifest.

For identification of the surviving raw files, SHA-256 values observed during this audit are:

| File | SHA-256 |
| --- | --- |
| `bin/benchmark-ui-software.log` | `77c940ba266b4e2ca81d16756addaa6bc127f94be3e51136b696ee689da6074f` |
| `bin/benchmark-ui-software-2.log` | `eba904202cb39b8055a9ab750524daafe36a98aab5bfe23387f558e2d02dabc2` |
| `bin/benchmark-ui-software-3.log` | `00d7d6bfbcc6000604f189c389b5a19c420dd5d9dd4eb72dc0af9d9bad7d4b4f` |

These are local scratch artifacts, not tracked release receipts. Their current hashes identify what was inspected, without retroactively authenticating how they were generated.

## Why later 144 FPS numbers are separate evidence

The [refresh-paced checkpoint](archive/performance-320-tps-refactor-history.md#refresh-paced-turbo-checkpoint) states that the old scheduler limited presentation to one frame per 1/15 second and performed eight logical updates per 40 Hz scene batch. The approximately 13.4 FPS was a deliberate cadence limit. The software draw occupied only about 1.3% of the measured interval in the raw logs; that does not predict cost when rendering much more often, at another resolution, or with another redraw policy.

The surviving [old Vulkan VSync log](../bin/benchmark-ui-vulkan-vsync.log) is 316.830 TPS / 13.334 FPS, hidden, with two seconds warm-up and five seconds measurement. It is Vulkan, not a software VSync result.

After cadence changed, the [first anchored Vulkan log](../bin/benchmark-ui-vulkan-refresh-anchored.log) records 264.897 TPS / 144.018 FPS, 722 draws over 5.013267 seconds, VSync enabled, hidden. Complete draw consumes 25.0% of the interval, compared with 2.4% in the older Vulkan VSync log. The journal reports a second 265.265 TPS / 144.018 FPS row and warns that these precede a changed transport-routing workload. Neither row is a software result or a 4K measurement. Hidden submitted/produced FPS also does not certify visible display pacing.

Later Vulkan results include 360.032 TPS / 144.013 FPS for 1,998 warm-up plus 3,600 measured ticks, and 347.182 TPS / 144.016 FPS over 10,800 measured ticks as the population grows. The [July 13 changelog](archive/openrct2-overhaul-changelog.md#2026-07-13) additionally reports 354.475 TPS / 144.006 FPS for 2,000 warm-up plus 12,000 measured ticks, with checksum `ca1cebcdee9abff4000000000000000000000000`. These are later Vulkan scheduler/renderer checkpoints with different state boundaries and implementations. They must not be attributed to the historical software renderer. Raw receipts for those particular later figures were not traced in this bounded audit.

## Pre-catch-up checkpoint and new comparison

The annotated tag `stable-before-upstream-2026-09-13`, dereferenced to its commit, resolves to `e874ceb7708974e00040419d30390686c52a5d31`. That commit is dated 2026-08-01 and is not the July reporting commit. The [September 14 deployment record](upstream-deployment-2026-09-14.md#manual-acceptance-and-next-work) states that accepted upstream catch-up advanced `develop` by **fast-forward** from `e874ceb770` to `f69067effaa97669b7b8d5ba5512dfa1ab346ea1`; there is no catch-up merge commit to select as a parent-based baseline. It also identifies deployed production source `c6560d2679cd7871ba583236fd9515bb81955511` and separately records executable hashes.

A new build of exact source `e874ceb770...` can establish pre-catch-up behavior under controlled present-day conditions. It is not automatically a reproduction of the older 318 TPS row. Record executable/source identity, actual 3840×2160 drawable and internal dimensions, scale/camera, park and assets, fixed warm-up and measured tick boundaries, actual renderer, VSync mode, visible cadence, logical ticks, initial/final state and checksum. Compare like-for-like before attributing any difference to upstream catch-up, GPU representation, or simulation regression.
