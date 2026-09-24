# Simulation wait attribution: build133

The opt-in 12,000-tick trace localizes its two largest simulation stalls to **resuming the main thread after the vehicle task group is complete**. It does not reproduce or explain conclusively the 22–63 ms stalls in the earlier clean build131 runs. No scheduling or simulation change follows from this report.

## Evidence and workload

Evidence: [`performance-physical-depth133-waits-12000-01/summary.json`](../obj/vulkan-parity/performance-physical-depth133-waits-12000-01/summary.json). Summary SHA-256: `0763fd9fcb9b0cfdf9c5d02867b6c491d7755ed452e3f5a6ccb78da0e2f29957`. Build133 receipt SHA-256: `a443f333d7191fa204cf488b2ae2526741318940afa9116a4fe470f3a31c97cb`; source manifest SHA-256: `fec6c6fe4dbb72edcabf67997c72a5650e44770cd26d210665ea4aba7fdfd6f0`. These identify the built working tree, beyond its recorded base commit.

The runner completed successfully with `OPENRCT2_PROFILE_SIMULATION_WAITS=1`, ordinary Turbo's 360 TPS target, VSync, the saved EverythingPark camera and 100 warmup ticks. It measured source ticks **3133931 → 3145931**, with **13,213 → 17,042 guests**, 2,208 staff and 2,128 vehicles. Initial/final state censuses and the final entity checksum exactly match both clean build131 runs (`performance-physical-depth131-12000-01` and `-03`):

`07d58eaefde6aa6d000000000000000000000000`

The park hash is `c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`. The workload records compare equal across these runs; binaries and instrumentation differ.

| Build133 diagnostic metric | Result |
|---|---:|
| Measurement | 33.415248 s / 12,000 ticks |
| Simulation throughput | 359.117 TPS |
| Accepted presents | 4,811 / 143.976 FPS |
| Accepted-present p99 / maximum interval | 9.2 / 11.0556 ms |
| Simulation mean / maximum | 1.939536 / 7.139 ms |
| CPU draw mean / GPU frame mean | 0.182186 / 2.905817 ms |

All 4,811 submissions were accepted, with no unavailable/superseded visual packets or lost timing samples. Acceptance timestamps describe queue presentation, not physical display scanout. Rendering remains a partial native-world prototype; this is not complete-art acceptance.

## Correlated worst events

The following joins use the source tick in `result.phaseTiming` and `simulationAttribution`. Every phase/batch keeps only its own largest 16 events; absence from another phase's retained set is not evidence that the phase took zero time.

| Source tick | Whole simulation ms | Named phase / batch evidence | Interpretation |
|---|---:|---|---|
| 3136028 | 7.1387 | Vehicles 5.4301; ParallelFor 4.9975; wait 4.8921; group-ready → resumed 4.7909 | Most wait occurred after group work completed. |
| 3137581 | 7.0942 | Vehicles 5.6256; ParallelFor 5.3104; wait 5.2490; group-ready → resumed 5.0753 | Same completion-to-resumption delay. |
| 3145728 | 5.0654 | Park 2.4933 with 7,454,616 main-thread cycles | Substantial scheduled work in periodic park update, unlike the low-cycle waits. |
| 3139901 | 3.6248 | Peeps 2.7892; ParallelFor 1.4437, including submission 1.4412 and wait 0.0006 | Delay is in submitting the batch, not waiting for its running workers. |
| 3144723 | 3.5049 | Peep batch submission 0.9542; vehicle batch submission 0.7597; waits 0.0004 / 0.0005 | Two submission intervals account for 1.7139 ms; cause within enqueue/locking/scheduling remains unresolved. |

The two largest vehicle batches give stronger evidence than wall duration alone:

| Batch detail | Tick 3136028 | Tick 3137581 |
|---|---:|---:|
| Condition-wait interval | 4.8893 ms | 5.2484 ms |
| Group-ready → resumed | 4.7909 ms (98.0%) | 5.0753 ms (96.7%) |
| Main-thread cycles during wait | 36,646 | 23,458 |
| Longest worker work / its cycles | 0.1866 ms / 547,524 | 0.2184 ms / 638,982 |
| Maximum worker start delay | 0.0983 ms | 0.0374 ms |
| Executed / retired queued consumers | 31 / 0 | 19 / 12 |
| Remaining consumers at wait entry | 3 | 19 |

Both batches were vehicle ride-rating accumulation: 353 items, grain four, 31 submitted consumers. The source sets the ready timestamp after decrementing the task group's remaining count to zero and before notifying, while holding the group mutex ([JobPool.cpp](../src/openrct2/core/JobPool.cpp)). The waiting thread timestamps resumption after the predicate wait has reacquired that mutex. Thus the measured ready-to-resume interval can include notifier delay before unlocking, condition notification/wakeup, mutex reacquisition and main-thread descheduling. **The data excludes unfinished rating computation as the cause of that interval; it does not distinguish those remaining causes.** The predicate wait and ownership barrier remain required. There is no demonstrated lost-wakeup or dropped-work bug.

The staff batch at tick 3139901 had 2,208 items, grain 16 and 31 executed consumers. Its longest worker ran for only 0.0113 ms; the whole batch used 187,436 main-thread cycles despite its 1.4437 ms wall time. The measured submission interval includes group allocation, repeated group/pool locking, queue insertion and notifications. It is not a pure mutex-wait measurement. [Staff.cpp](../src/openrct2/entity/Staff.cpp) and [Vehicle.cpp](../src/openrct2/ride/Vehicle.cpp) are the two instrumented call sites; no per-entity probes were added.

Tick 3145728 is divisible by both 512 and 4096. [Park::Update](../src/openrct2/world/Park.cpp) therefore runs its periodic rating/value work and park-size scan at that tick. Another retained park event at tick 3141632 is also divisible by 4096 (1.1676 ms / 3,495,104 cycles). The coarse phase does not isolate which periodic operation dominates, but these events have considerable CPU work and are distinct from the barrier stalls. No cadence changes are justified here.

## Aggregate cost and limits

Across all 12,000 ticks, peep work totals 16.6443 s (1.387026 ms/tick, 71.5% of the enclosing measured simulation phase); vehicle work totals 5.2601 s (0.438345 ms/tick, 22.6%). Spatial-index work averages 34.79 µs/tick. The staff and vehicle ParallelFor batches average 89.424 and169.030 µs respectively, including useful caller work and batch management. These averages cannot be called pure synchronization overhead.

The diagnostic uses fixed top 16 storage and end-only JSON output. It adds phase clock/cycle probes, one diagnostic allocation per traced group, and worker timing/aggregation under existing completion locks. It changes observability and scheduling perturbation even though algorithms, grain sizes and barriers are unchanged. The 15 named phases sum 23.2384 s; the outer simulation phase totals 23.2703 s, leaving 31.8941 ms for boundary/probe gaps and other unscoped overhead. This difference is **not total diagnostic overhead**: most instrumentation cost lies inside recorded scopes.

Simulation mean increased from 1.888291 / 1.895151 ms in the two clean 131 runs to 1.939536 ms here (+51.245 / +44.385 µs, +2.71% / +2.34%). This is not an isolated overhead measurement: build133 also changes rendering and the runs have different scheduling histories. Its shorter maximum does not prove a pacing fix. The earlier clean 63.129 ms / 6.75-million-cycle event was not reproduced, and no attribution can be transferred to it merely because its symptoms look similar.

The existing broad-phase counters also show a scheduler-wait maximum 2.056 ms and draw-paint maximum 0.5384 ms in this diagnostic; neither explains the 7 ms simulation events. The accepted-present maximum 11.0556 ms is a different measurement from the maximum application draw interval 10.715 ms.

## Bounded next attribution

Keep the clean135 measurement separate from this diagnostic lane. If long stalls recur, repeat this opt-in lane without unrelated CPU workloads and correlate source ticks again. A synchronized OS thread scheduling/context-switch trace around group-ready and main-thread wakeup is the next way to distinguish descheduling from notifier/mutex delay; this report has no such trace. Alternatively, narrowly separate submission locking/notification and completion notification/unlock timestamps before considering code changes. The rating-cache mutex remains a potential worker-side wait seam in [RideRatings.cpp](../src/openrct2/ride/RideRatings.cpp), but the two largest observed stalls happened after all group work was complete, so this evidence does not implicate that mutex.

No scheduler, worker-count, spin-wait, affinity, power-policy or simulation algorithm change was made for this analysis.

## Build135 final clean run and diagnostic repeat

The clean build135 run **reproduced the unresolved long-stall problem**. Its [summary](../obj/vulkan-parity/performance-physical-depth135-12000-01/summary.json) has SHA-256 `23e920f9aee64df4ad582a6ef82b82e618ef7130e7d4ec20eb37595b1c33858d`. The subsequent opt-in [diagnostic repeat](../obj/vulkan-parity/performance-physical-depth135-waits-12000-01/summary.json) has SHA-256 `8b9ebffba2d671a4d3ddb621fd2a1de5b5efce256c683ef3cc892eb23ed796bd`. Both passed the runner and used the same build and workload; initial/final censuses and checksum again match. A runner pass does not imply good worst-case pacing.

| Metric | Clean135 | Diagnostic135 |
|---|---:|---:|
| TPS / accepted-present FPS | 359.956 / 143.7426 | 359.947 / 143.9490 |
| Accepted presents / GPU timing samples | 4,792 / 4,792 | 4,799 / 4,799 |
| CPU draw mean | 0.166315 ms | 0.166458 ms |
| GPU frame mean | 2.923830 ms | 2.919225 ms |
| Simulation mean | 1.881556 ms | 1.884240 ms |
| Largest detailed simulation event | 53.0048 ms | 14.6180 ms |
| Accepted-present p99 / maximum | 9.2 / 57.0965 ms | 9.2 / 16.5931 ms |

The clean-run stall occurs at tick 3134966 with 6,256,992 main-thread cycles. Fine attribution was disabled, so its source cannot be identified from that run. The diagnostic's two largest simulation events again expose delay after the vehicle task group completes:

| Detail | Tick 3141317 | Tick 3145003 |
|---|---:|---:|
| Whole simulation | 14.6180 ms / 6,824,254 cycles | 4.9476 ms / 6,981,244 cycles |
| Vehicle phase / ParallelFor | 12.9722 / 12.5347 ms | 3.0079 / 2.7006 ms |
| Condition wait / group-ready → resumed | 12.3404 / 12.1143 ms | 2.6072 / 2.5747 ms |
| Portion after group-ready | 98.17% | 98.75% |
| Main-thread wait cycles | 36,546 | 23,164 |
| Longest worker / corresponding cycles | 0.3967 ms / 1,183,274 | 0.1193 ms / 357,396 |
| Maximum worker start delay | 0.1590 ms | 0.0751 ms |

Both batches executed all 31 submitted consumers; three and seven respectively remained when the caller entered the wait. The evidence now reproduces the completion-to-resumption issue across two diagnostic runs, at different source ticks and with a 12.1143 ms observed delay in 135. It still cannot distinguish notifier-unlock delay, condition wakeup, mutex reacquisition and descheduling, or prove that the clean 53 ms event has the same cause. No long worker computation or missing completion is needed to explain the measured post-ready intervals. The proper ownership barrier must remain.

The diagnostic repeat's small mean difference (+2.684 µs/tick) is not an overhead calibration: it has a different distribution of stalls and different scheduling perturbation. Its shorter maximum does not establish a fix. No further runs or scheduler changes were made for this checkpoint; good average throughput and recurring poor worst-case pacing are both retained in the result.

### Final image inspection

Manually inspected the [clean135 final 3840×2160 image](../obj/vulkan-parity/performance-physical-depth135-12000-01/final-benchmark.png), the saved clean131 repeat overview, and three native-scale 135 crops covering static buildings/stations, central terrain/track crossings, and right-side water/curves. Crops are retained under `obj/vulkan-parity/physical-depth135-manual-review/`. The 135 PNG SHA-256 is `9ceb4ce07739525a9a1278944c033b71c78922438c487a13bec6ec96adb7b721`.

The image has continuous viewport coverage, the expected isometric camera and palette, visible UI corners, broad path/track networks, static buildings, water and scenery. No new gross blank region, camera displacement or widespread sprite corruption was established relative to the 131 overview. Elevated rails visibly lack physical supports; missing main-world entities and previously documented local physical-depth/geometry errors remain unqualified. This zoom 2 overview is not proof that individual track pieces, intersections, stations, slopes or transparent overlaps match original rendering.

The receipt records rotation 3, zoom 2, view position `[458,-4370]`, simulation tick 3145931 and publication source tick 3145930. Capture occurred after measurement, with authoritative state unchanged and the same final checksum. It is an explicitly partial render, not a display-scanout or upstream-pixel-parity result. See the [physical-depth checkpoint](vulkan-physical-depth-checkpoint.md) for the remaining geometry limitations.
