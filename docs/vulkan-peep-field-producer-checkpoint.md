# Peep field producer replacement

Qualified field-producer checkpoint, 2026-09-23. [Build80](../obj/vulkan-parity/build-80/receipt.json) passes; [run62](../obj/vulkan-parity/run-62/summary.json) passes 869 tests, including 66 parity cases, with unchanged pinned inputs and clean Vulkan validation. Installed checkpoint52 is unchanged. The larger native world-consumer bundle is staged separately and is not yet qualified.

## Replacement boundary

The producer no longer constructs a 96-byte peep record for each dirty identity. `RetainedPeepBatch` carries separate lifecycle, motion, appearance and animation update vectors. The registry reads only the requested groups from its existing coalesced dirty worklist. Creation, reuse, reset and catalog bootstrap capture all groups; non-peep replacements emit versioned deletion records. Capture remains non-consuming until the common publication owner successfully prepares both families and acknowledges the worklist.

The duplicate `ConsumeRetainedPeepChanges` drain is removed. The old full-record capture and validation live in a test-only fixture, outside production code. Ordinary readers and GPU selector diagnostics use the migrated field schema; there is no production full-record adapter around the new batch.

Clothing/accessory owner methods publish appearance. Animation methods publish animation/bounds; transitions whose callers also set facing retain transform notification. Broader state transitions remain full updates until their dependencies are migrated explicitly. This avoids relying on an unrelated shirt change to carry a directly written animation state.

The scene applies the groups transactionally. New/reused live identities require complete initialization; existing identities allow independent groups; deletion cannot carry live payload. Duplicate groups, mismatched generations, invalid scalars and incomplete initialization reject the batch without replacing held snapshots. Contiguous scratch storage replaces per-slot hash nodes. Unchanged groups keep their shared chunks, redundant non-interpolated motion preserves its timestamps, and empty input avoids touched-slot scratch allocation.

## Cost and limitations

For each changed peep, logical payload is 48 bytes for motion, 24 for appearance, 44 for animation, or 12 for deletion. Motion plus animation is 92 bytes; full initialization is 128 bytes. The retired combined record was 96 bytes. These figures include update identity fields; they are not VRAM upload measurements. Combined movement/animation saves much less than isolated appearance, and full initialization is larger.

Streams reserve lazily using the dirty-worklist upper bound. Mixed sparse categories may therefore reserve more capacity than their logical payload. A nonempty apply also clears a bounded slot-index array and creates contiguous touched-slot work; published directory and changed-chunk copies remain costs. The benchmark must report these limits instead of inferring throughput from payload sizes.

The animation group still contains resolved frame/bounds facts that belong in shared definitions and GPU derivation. Runtime objects still have additional public mutation paths and broader notifications. Production interpolation history and complete object-owned mutation coverage remain open. This batch does not activate the combined peep profile in the ordinary CPU-painted viewport: doing so would remove clones still required by that painter.

## Qualification checklist

- [x] Replace raw96 producer batches and remove the duplicate drain API.
- [x] Move the obsolete capture implementation physically into test-only code.
- [x] Migrate producer, snapshot apply and test callers as one replacement.
- [x] Compile and pass the full production/parity regression suite with clean Vulkan validation.
- [x] Measure the capture stage on 60,000 peeps for at least 3,000 iterations in each workload: motion, appearance, animation, simultaneous motion/animation and all groups. Compare equivalent capture work, record capacities and keep this separate from game TPS.
- [ ] Connect actual persistent GPU state and resident catalog ownership to the native world consumer; replace renderer-only animation derivation.
- [ ] Remove complete CPU world preparation for admitted scenes, then qualify dense ordinary parks at 4K over at least 3,000 measured ticks and displayed VSync pacing.

The benchmark is CPU-only capture attribution. It cannot qualify complete publication/apply cost, rendering, upload bandwidth, simulation throughput or displayed pacing. No new pixel exception is introduced by this replacement.

## Measured capture cost

[Producer run01](../obj/vulkan-parity/peep-producer-run-01/summary.json) passed byte-equivalence and held-state checks for all five workloads: 60,000 peeps, 50 warmups and 3,000 measured paired iterations each. This is a warm-cache CPU microbenchmark against a test-only reproduction of the retired raw96 capture, not a historical executable or game tick benchmark. Each workload has its own paired baseline; cross-workload timing comparisons are not meaningful.

| Workload | Raw96 capture mean | Field capture mean | Change | Peep payload/entity |
|---|---:|---:|---:|---:|
| movement-only | 2.756 ms | 1.777 ms | -35.5% | 48 B |
| appearance-only | 3.281 ms | 1.997 ms | -39.1% | 24 B |
| animation-only | 3.397 ms | 2.589 ms | -23.8% | 44 B |
| motion-and-animation | 3.015 ms | 2.809 ms | -6.8% | 92 B |
| all-groups-including-lifecycle | 2.113 ms | 2.481 ms | +17.4% | 128 B |

Movement plus animation improves capture time by only 6.8%; updating every group regresses 17.4% and grows peep payload from 96 to 128 bytes. Isolated movement, appearance and animation improve 35.5%, 39.1% and 23.8%. Retaining the field schema is justified by independent GPU updates and simpler ownership, not by a claim that this solves dense-scene performance. The all-group workload recaptures existing identities; it is not limited to creation/bootstrap. Broader full notifications remain, and their frequency in a real park has not been measured. They must be replaced with accurate category ownership before assuming the regression is rare.

The measured path still creates 3.84 MB of balloon compatibility metadata for every 60,000-peep capture, on both sides. The staged native-only publication removes this fan-out and legacy object/spatial reconstruction. Fresh allocation is included; destruction is measured separately. Setup, dirty marking, acknowledgement, snapshot apply, GPU upload, simulation and presentation are excluded. Capacity equals payload in this dense single-category setup; sparse mixed workloads remain unmeasured. No FPS, TPS or frame-pacing improvement is established here.

Run61 failed preflight because its command supplied the terrain-column receipt as the emission receipt; no GPU tests ran. Corrected run62 passed without a source correction. The failed invocation is not a rendering regression.

Independent benchmark review confirmed the arithmetic and receipts. Limits: one process, fixed workload order, inherited affinity/priority, no repeated-run confidence estimate or retained paired time series. The raw baseline includes its old scalar validation; new group validation occurs in untimed Apply, so this measures capture attribution rather than equal end-to-end validation work. Motion plus animation reduces total captured bytes by only 2.5% with compatibility metadata included. Whole-publication and real-park dirty-category measurements remain required.
