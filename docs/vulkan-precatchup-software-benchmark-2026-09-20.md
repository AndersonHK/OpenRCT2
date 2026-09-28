# Pre-catchup software benchmark: 2026-09-20

**Historical software throughput above 300 TPS is supported by surviving logs. The fresh pre-catchup source benchmark does not reproduce that throughput at the tested 4K workloads, and its performance is close to the current software renderer.**

## Source and method

The requested pre-catchup develop revision is `e874ceb7708974e00040419d30390686c52a5d31`, dated August 1, 2026, tagged `stable-before-upstream-2026-09-13`. The September catchup advanced develop by fast-forward to `f69067effa`; there is no merge commit whose first parent must be inferred. See the [deployment record](upstream-deployment-2026-09-14.md).

The old source was extracted into an isolated directory and rebuilt in Release x64 with MSVC 14.44.35207, Vulkan enabled and Breakpad disabled. Core, UI and Windows executable builds all pass with zero warnings/errors. Only three source files receive measurement additions: initial/final SDL drawable size and cached monitor refresh, queried outside the timed interval. Rendering, simulation and scheduling are unchanged. No historical full-test-suite or Vulkan qualification is claimed. The rebuilt executable SHA-256 is `55f82bd8c910d6171a181707e44b7e89a49f1dac153fc0c74a74f5c29e0e53a1`.

The current lane uses the immutable build 56 snapshot; its source inventory matches current committed production source `d16251af26`. The historical runtime assets come from the pre-catchup backup, with all 4,677 files matching the saved deployment inventory. Current assets use the frozen accepted post-catchup package. Both use the same original-game installations and unchanged EverythingPark fixture. Asset versions are intentionally recorded rather than claimed identical.

All runs use the ordinary software-with-hardware-display renderer, ordinary Turbo, isolated profiles, fixed saved camera, enabled viewport multithreading, disabled HDR/LightFX, no profiler/capture/validation, and a hidden window. Actual SDL output is **3840×2160**, with **144 Hz** reported at both boundaries. Draws/sec and frame intervals describe application drawing, not physical displayed pacing. The runner confirms the actual renderer and tick count.

This is a historical-source rebuild using the present compiler and shared dependency cache, not a bit-for-bit historical release reproduction. The dependency cache is not fully pinned by the build receipt. An [independent method review](vulkan-architecture-reviews/historical-benchmark-method.md) found no blocking method defect and records the qualifications.

## Repeated scale-one comparison

Each cell has two independent processes, **100 warmup + 3,000 measured ticks**, with old/current order reversed in the second repetition. Values below are per-column medians of two observations.

| Software source | VSync | TPS | Draws/sec | CPU draw ms, including presentation |
|---|---|---:|---:|---:|
| Pre-catchup e874 | Off | 65.831 | 65.852 | 10.476 |
| Current build 56 | Off | 64.874 | 64.897 | 10.728 |
| Pre-catchup e874 | On | 115.295 | 57.124 | 10.478 |
| Current build 56 | On | 115.017 | 56.488 | 10.661 |

Current versus pre-catchup median TPS differs by about **−0.24% with VSync** and **−1.45% uncapped**. These small descriptive samples do not establish statistical equivalence, but provide no evidence of a large software-throughput regression on this workload. The VSync TPS increase occurs alongside fewer draws; it is not equivalent visual work or proof of improved renderer efficiency.

All eight initial/final simulation censuses match, as does final checksum `9b7eee204b0471d7000000000000000000000000`. Config bytes match within each VSync policy; the park hash and recorded host properties match. The park grows from 13213 to 14477 guests.

Individual results:

| Source | VSync | Repeat | TPS | Draws/sec | CPU draw ms |
|---|---|---:|---:|---:|---:|
| old | 1 | 1 | 117.069 | 56.466 | 10.556 |
| new | 1 | 1 | 115.670 | 56.061 | 10.740 |
| old | 0 | 1 | 65.551 | 65.573 | 10.540 |
| new | 0 | 1 | 65.054 | 65.076 | 10.703 |
| new | 0 | 2 | 64.695 | 64.717 | 10.754 |
| old | 0 | 2 | 66.110 | 66.132 | 10.412 |
| new | 1 | 2 | 114.363 | 56.915 | 10.582 |
| old | 1 | 2 | 113.521 | 57.782 | 10.399 |

## Separate sustained 1.75× scale comparison

These two runs retain native **4K output** but use 1.75× UI scale, **2,000 warmup + 12,000 measured ticks**, and VSync enabled. Each source has one run. Scaling reduces logical canvas/world coverage; the different warmup and length also change population and cache state. This is a separate workload, not a controlled scale-only delta from the preceding table.

| Software source | TPS | Draws/sec | CPU draw ms |
|---|---:|---:|---:|
| Pre-catchup e874 | 183.165 | 71.999 | 5.675 |
| Current build 56 | 180.044 | 72.018 | 5.787 |

Both initial/final censuses match, and both finish with checksum `65ae3b2fa03fa4cb000000000000000000000000`. The park grows from 14046 to 17352 guests. Current TPS is about 1.70% below the old run; a single pair does not establish a causal regression. Neither run reaches 300 TPS or 144 draws/sec.

## Relationship to the historical journal

The [historical evidence audit](vulkan-historical-software-evidence.md) locates three original software logs at **318.027–318.400 TPS and 13.185–13.400 FPS**. Those used time-based two-second warmup/five-second measurement, only 1,592 measured ticks, a deliberately low presentation cadence, and unrecorded resolution/config/binary identity. They support the owner’s recollection of software throughput above 300 TPS, but do not establish 300 TPS at 4K or high-refresh presentation.

The full [1,265-line performance history](archive/performance-320-tps-refactor-history.md) was read by the coordinator and each of three independent reviewers. Later results above 300 TPS at approximately 144 FPS are Vulkan results at different checkpoints and insufficiently specified display workloads. They are useful evidence that the architecture has previously approached the target, not directly comparable rows for this test.

## Consequences and retained evidence

The catchup boundary is not showing a large software regression under these controlled tests. Continue from the historical ownership/residency work and measure CPU preparation, publication and upload costs before attributing gains to a new representation. Preserve coherent complete-frame drawing; do not buy TPS through reduced frame work or reintroduce rejected bulk-copy and retained-pixel designs. The [two plans and independent votes](vulkan-architecture-comparison-2026-09-20.md) remain proposals pending the next implementation decision.

All **10 runs / 48,000 measured ticks** passed. The installed game, real profile and production source were not changed by this task. The broad migration goal remains paused; no alternative architecture was implemented.

[Machine-readable result and evidence pins](vulkan-precatchup-software-benchmark-2026-09-20.json) reference logs, launch/config records, input inventories, source archive/patch/build receipt and executable snapshot under `obj/vulkan-parity`. Reproduction drivers are `historical-e874-build-01/run-matrix.py` and `run-long-scale.py`; choose fresh output names before rerunning. The historical adapter’s `current-software` mode name selects the snapshot-executable lane; its recorded source is explicitly e874, not current HEAD.
