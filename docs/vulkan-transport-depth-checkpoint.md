# Transport, component contacts and loading checkpoint

## Follow-up checkpoint193

This checkpoint restores live selected-train viewports for complete native scenes and gives source-owned photo platforms a floor layer beneath their own rails. It does not change world anchors. The attempted slope and blanket track-contact changes were withdrawn after visual regressions. See the [current regression checklist](vulkan-depth-contact-regressions.md) and [ordering-constraint proposal](vulkan-depth-constraint-fitting.md).

- [x] Build193 succeeds with zero warnings/errors;88 focused CPU tests,79 extractor tests and32 benchmark-report tests pass.
- [x] Inspect exact Corkscrew Park photo contacts in four rotations. Every changed pixel in the combined candidate/path-rollback comparison now matches upstream; remaining half-loop/fence overlaps remain open.
- [x] Inspect18 Everything Park paired views. Six path and four small Corkscrew controls match deployed186 exactly;656 changed glass-station pixels all improve against upstream.
- [x] Validate the actual ride16 train window in Corkscrew Park over3,000 ticks at4K and inspect its nonblank follow-view crop. Deploy193:29 verified files,3 changed, backed up; shared test save preserved. Receipt: `obj/vulkan-parity/deploy-checkpoint193-01/receipt.json`.
- [x] Run a hidden, silent4K12,000-tick benchmark and inspect its final image.193 produces351.056TPS,143.875 accepted presents/sec,0.643122ms CPU drawing and4.67468ms GPU, versus186's349.326TPS/144.009/0.637148ms/4.717201ms. All4,918 submissions complete and receive accepted presents, zero discarded packets, unchanged final entity checksum.
- [ ] Sustain360TPS through the final heavy interval. The four3,000-tick windows are359.89/359.92/357.54/328.91TPS.
- [ ] Resolve all pacing outliers. Accepted-present p99 is9.0ms and maximum9.5706ms, but the first application interval is34.4603ms with25.607ms in drawBegin. That measurement-boundary event is retained, not silently excluded; accepted presents are not display scanout.
- [ ] Finish native component identities and bounded offline fitting before reintroducing slope/helix offsets. The initial extraction tool has768 repeatable mask-order observations across16 historical fixture cases; no fitted current GPU parameters are claimed.

Performance receipt: `obj/vulkan-parity/performance-entities193-12000-01/summary.json`. Actual selected-ride-window and deployment qualification are recorded in the current regression checklist.

## Prior checkpoint186

Deployed build186 followed commit `68b3880700` / build176. Its qualification is retained below; implementation alone is not full parity.

## Changes

- Miniature railway: a fresh GPU visitor consumes an immutable, reproducibly authored grammar for all27 track types. It selects support/floor outcomes and path crossing artwork from graphical state; no CPU paint replay or per-frame catalog construction was added.
- Pirate Ship: preserve the five authored component groups and their column coverage instead of drawing five unrestricted copies at one whole-ride depth. Give foreground fences their own authored edge contact.
- Tower rides: use one cold ride-owned worldXY/base contact for rear vehicle, shaft and front vehicle. Actual moving height remains a raster input. Shaft self-overlap and external elevated contacts must also pass inspection before acceptance.
- Parking scenery: use authored wall-edge contacts instead of bitmap placement offsets. The supplied Kahuna Point example uses thin wall scenery partially buried below tarmac paths; retain that burial.
- Separate tile and entity compute kernels, while keeping one immutable snapshot, batched uploads and the existing output/prefix ownership. This reduces the amount of code carried by each kernel; a performance improvement is not yet established.
- Reuse unchanged object/material facts across reloads; enumerate vehicle image banks through a union of intervals instead of allocating a tree node per image. Different-park transitions still need a separate resident-image-bank/scene-descriptor ownership split.
- Sample the spatial audio listener once per presentation batch, rather than repeatedly per ride speaker.
- Give auxiliary readbacks their own reusable allocation sized from the validated target extents. Sprite uploads must not consume screenshot readback capacity.

## Evidence and checklist

- [x] Build180: zero warnings/errors;146 focused CPU tests pass.
- [x] Build182:206 focused CPU tests pass. Build186 compiles with zero warnings/errors and unchanged inputs.
- [x] Actual GPU depth, filter composition, readback and cancellation checks pass after replacing stale fixtures. The stronger selected-car test found a real split-kernel distribution bug; its exact pixel checks pass in185 after the fix.
- [x] Build186: rounded prefix capacity boundary test and20MiB catalog discard/reuse/epoch-transition GPU regression pass with synchronization validation.
- [x] Actual GPU readback regression: dual output larger than a1MiB upload budget, repeated reuse, odd-size resize and retained output pass.
- [x] Everything Park railway: all four original/native closeups rendered successfully; an agent inspected exposed rails, curves and station floor strips. No obvious whole-segment omission in those samples.
- [x] Kahuna Point: reproduce the parking bay truncation in the user's rotated title scene. The original and native use the same park and licensed art.
- [x] Ship: inspect all four build180 views, including previously rejected entrance/exit contacts. Main contacts improve; one new foreground fence overlap requires the follow-up correction.
- [x] Visually inspect parking and Ship fence follow-ups against the same original samples. Ship fence improves without new Ship overlap errors in four rotations and zoom1. Parking bays/lanes reappear in both rotations, but some buried wall borders remain one or two pixels too thick; this is an open parity gap.
- [x] Tower: inspect all four static rotations, nearby elevated geometry, and a real-tick ascent/descent filmstrip. Build186 captures24 frames across1280 actual ticks, including six crossings of worldZ432/464/496. All were manually inspected; no ring/shaft flip or repeating ledge appears. The interior static shaft matches4294 upstream pixels in each rotation. Filmstrip scope is ride181, rotation0, zoom0, empty seats; occupied riders, other families and the off-camera top stop remain unqualified.
- [ ] Qualify railway elevated floors/supports, crossings, diagonals, tunnels and construction ghosts beyond the exposed-track samples.
- [x] Measure title transitions with the isolated two-park/two-loop runner. Build184 confirms a hidden window, dummy audio, normal exit and all31 command markers. Cold world compilation is30.921 seconds; return-visit catalog construction is64.5/104.3ms. Few-frame park transitions remain unfinished; see the loading document.
- [x] Run final4K moving-camera/zoom stress:186 passes3000 ticks,189 camera steps, five zoom levels and37 captures. An agent visually inspected all overview samples and four detailed scene crops; no persistent black world, monochrome UI or whole-layer disappearance was seen.
- [x] Run final4K12000-tick performance check with ordinary360TPS target and VSync. Build186:349.326TPS,144.009 accepted presents/sec,0.637148ms CPU drawing,4.717201ms GPU. Accepted-present p99 is9.0ms, maximum10.6409ms,4947 submissions/presents/completions, zero discarded packets. Checksum matches176/185. Receipts: `obj/vulkan-parity/performance-entities186-12000-01/summary.json` and `camera-entities186-3000-01/summary.json`.
- [ ] Sustain360TPS in the late populated-park windows; attribute and eliminate remaining pacing outliers. The quieter final sample does not prove rare outliers resolved.
- [x] Commit the reviewed source with explicit remaining gaps and deploy the qualified artifacts for manual testing. `D:\Games\Independent\OpenRCT2Mod` contains all29 verified qualified files (10 changed), with replaced files backed up. Deployment receipt: `obj/vulkan-parity/deploy-checkpoint186-01/receipt.json`. The unchanged shared `Everything Park - Renderer Test.park` save remains available to both installations. No game is automatically launched.

## Performance evidence before final allocation fixes

The hidden, silent build185 run uses the same Everything Park, configuration,3840x2160 drawable extent,144Hz monitor and12,000 measured logical ticks after100 warmup ticks as build176. Both finish with checksum `07d58eaefde6aa6d000000000000000000000000`. All4949 submitted frames receive accepted presents and fence completions; there are no discarded visual packets. Receipts: `obj/vulkan-parity/performance-entities185-12000-01/summary.json` and the corresponding176 directory.

| Metric | Deployed176 | Candidate185 |
| --- | ---: | ---: |
| Logical TPS |346.942|348.938|
| Accepted presents/sec |143.779|143.908|
| CPU drawing ms/frame |0.680|0.654|
| GPU ms/frame |3.757|4.740|
| Accepted-present interval p99 ms |9.0|9.1|
| Accepted-present maximum ms |22.780|22.457|

This is near-baseline throughput with improved visual coverage, not a substantial performance win. The GPU cost increased; no isolated A/B establishes how much belongs to extra railway art versus the kernel split or other changes. Accepted presents measure queue acceptance, not scanout. The360TPS goal remains open: successive3000-tick windows deliver359.2,358.7,352.1 and327.7TPS as guests grow from13,213 to17,042. Simulation time rises from1.776 to2.034ms/tick; CPU drawing stays around0.614–0.677ms. One18.067ms draw-paint wall outlier has only2.11million thread cycles, which does not establish18ms of CPU computation; scheduling/wait attribution remains needed. Do not infer a resolved pacing tail or a named simulation culprit from this sample.

## Failures retained as evidence

Build177's earlier railway candidate failed a full-scene GPU submission with device loss. It was withdrawn; no causal attribution to the hardware, driver or a particular shader instruction has been established. Its source and logs are archived under `obj/vulkan-parity/railway177-withdrawn`. The fresh implementation above is independent. Do not treat later successful images as proof of the earlier failure's cause.

The first build178 title screenshot exhausted a shared upload/readback ring. This was a named, recoverable allocation failure, not device loss. The dedicated readback fix passed its GPU regression and allowed build180 to capture that scene.

Build179 stopped at compilation because the generated railway table spelled sentinel words as negative unsigned literals. The author now explicitly serializes32-bit words; build180 compiled successfully. No179 runtime was qualified.

Existing accepted outer-map/underground skirts remain intentional differences. Small Ship foot/water-edge differences, missing Ship passenger overlays and other documented ordering/coverage gaps remain open unless individually closed by subsequent evidence.

## Final bounded source review and build185 follow-up

The independent review traced both compute kernels through count, global prefix and materialization. Tile/entity work owns separate complete1024-entry blocks; barriers order both count domains before the global prefix and both write domains before vertex/indirect reads. Host validation requires `width*height == recordCount`; a zero-record scene returns before any dispatch or selected-car division. The new three-car GPU test passes in185 after fixing auxiliary distribution to stride over actual tiles, not padded entity work.

The review found a concrete allocation boundary defect, fixed and tested in186. Prefix storage previously had1264145 words, while the1235-block admission guard could accept1264640. For a1001x1001 map, padded tile work plus four65535-entry entity domains uses1264636 words and could overrun by491 despite passing that guard. Prefix allocation now uses the same complete-block capacity as dispatch admission. `MaterializationPrefixAllocationIncludesTheAcceptedPartialBlock` passes for this exact counterexample, every count in the last admitted block, and rejection beyond it.

The shared integer-to-D32 map keeps all priorities in the established22-bit domain, emits positive normal depths, and preserves the filter compositor's30-bit order field (maximum0x200000ff). Each world contact has512 representable depth slots; fine layers0..255 cannot cross the next contact. Parent/preview child budgets remain separately bounded15, while tower rear/shaft/front/passenger roles occupy0..157. No layer spill was found in the reviewed paths. This arithmetic and source review does not substitute for the tower ascent/descent filmstrip.

Build185's first temporal tower image succeeded, but its second capture failed with `Vulkan upload ring has no room for world-surface sprite sets` (`obj/vulkan-parity/tower185-filmstrip-01/native.log`). Each auxiliary publication intentionally receives a new high-bit map epoch; immutable416468-entry art remains shared. The cold-staging preflight checked only sprite revision, while `Record` subsequently reset that revision when the map epoch changed. Thus the second frame attempted the large table transfer through the ordinary ring. `NeedsSceneReset` now owns the shared epoch/dimension/chunk-count predicate for both preflight and recording. The existing20MiB resident-table GPU test now holds art constant across an epoch change, checks the rendered result and verifies zero repeated copies afterward. Neither the normal frame ring nor per-frame residency policy was broadened. The build186 GPU regression and the24-frame temporal capture both pass.

Dedicated auxiliary readback lifetime was also reviewed: one service worker owns one active submission; transfer-to-host barriers and the submission fence precede invalidation/copy into owned result vectors. Resize/reset follows the previous completed job, and post-submit exceptions retire work before destruction/reuse. The mapped allocation includes alignment space for dual indexed/RGBA outputs and remains separate from catalog/atlas uploads. No new lifetime defect was found in this bounded review; existing successful odd-size/reuse/dual-output GPU evidence remains applicable.
