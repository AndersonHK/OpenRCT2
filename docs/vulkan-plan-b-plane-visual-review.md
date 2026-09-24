# Plan B plane diagnostic: visual review, 2026-09-23

The plane experiment removes the obvious support-through-rail and balloon-cutting defects of the first scalar experiment. The reviewed scenes look coherently ordered at normal scale. Exact legacy parity remains unresolved at a few handyman/path contact pixels; manual inspection classifies those differences as **ambiguous**, not a demonstrated correction of an original bug and not a clear perceptual regression. No pixel exceptions or runtime admission are approved.

## Evidence and coverage

The root executed `obj/vulkan-parity/plane-run-01`; this review only read artifacts and generated CPU comparison/crop files. No game, build, test executable or GPU work was run by the reviewer. All 32 samples were inspected through exact indexed/RGBA byte comparisons and grouped visual review. They cover two poses, four rotations, legacy/stable references and two repeated draws.

All eight canonical legacy scene pairs were reviewed in full, plus all six connected differing-region crops across the four distinct nonempty mismatch signatures. Exact-repeat and alternate-reference membership is preserved in `obj/vulkan-parity/plane-run-01/visual-audit/summary.json`. That file also verifies indexed and RGBA differences occur at the same locations in every sample. `coverage-and-prediction.json` records all 32 actual GPU/reference hashes and verifies every GPU indexed buffer equals the corresponding independently generated `.physical-centre-finite-support.indexed.bin` prediction byte for byte.

| Reference | Phase 0, rotations 0–3 | Phase 1, rotations 0–3 |
| --- | --- | --- |
| Legacy | 0, 0, 6, 5 pixels | 0, 0, 2, 5 pixels |
| Stable | 0, 0, 6, 48 pixels | 0, 0, 2, 48 pixels |

Both repeated draws give the same results. Sixteen of the 32 samples are exact; sixteen diverge. The extra 43 pixels against stable rotation 3 are the previously reviewed stable-versus-legacy balloon/support difference, not an additional defect introduced by the plane experiment. The experiment now matches legacy at that complete balloon and guest/support-base overlap.

## Remaining contact regions

Coordinates below are exclusive local raster bounds in the 192 × 128 fixture.

| Region | Pixels | Visual classification |
| --- | ---: | --- |
| Phase 0 r2, [106,93,109,96] | 6 | Ambiguous handyman shoe/contact edge. Original path covers these pixels; GPU retains them. |
| Phase 1 r2, [106,94,107,96] | 2 | Same ambiguous contact, partly occluded by the changed umbrella guest. |
| Both phases r3, [97,100,100,103] | 5 | Ambiguous handyman shoe/contact edge from the next view. Original path covers pixels retained by GPU. |

The crops do not provide enough evidence to declare that these pixels represent the physical top of a shoe, a contact/shadow convention, or intentionally hidden sprite overhang. Both versions look plausible at normal scale. Thus a claim that the GPU fixes a software bug would exceed the evidence; calling the tiny visible contact change an obvious visual regression would also overstate it. It is an exact compatibility mismatch under the current oracle contract.

The independent geometry derivation attributes all canonical residuals to equal-depth contact ties at authored Z 32 between the handyman and path. The current owner ordinal prefers the handyman; original painter ordering prefers the path. That derivation explains the mismatch but does not itself decide the correct artistic tie rule. Previously tried footprint-based tie variants create additional differences elsewhere. A new rule needs independent justification and more contact fixtures, not a family-specific draw-last priority or a raised path depth chosen to fit these pixels.

Stable r3 controls add two already understood regions: 23 balloon-edge pixels at [125,75,128,85] and 20 guest/support-base pixels at [118,91,125,96]. GPU agrees with the reviewed legacy foreground relation there. The separate legacy/stable reference policy remains unchanged by this review; no expected image, test threshold or exception list was edited.

## Run qualifications and limits

The actual run reports validation activated with no validation messages, unchanged pinned inputs, zero GPU errors, 46 emitted components, and uploads matching the expected 54,144-byte initialization, 288-byte changed pose, and zero bytes for the other thirty samples. These are qualifications of a bounded diagnostic run. They are not live-renderer admission, large-park performance evidence, or proof of coverage beyond the finite original-art fixture. The strict mixed runner correctly remains failed under its current exact comparison contract.

Artifacts are retained under `obj/vulkan-parity/plane-run-01/visual-audit/`: `all-eight-canonical-pairs.png`, `all-distinct-regions.png`, individual crops, `summary.json`, and `coverage-and-prediction.json`. Prior scalar failures remain separately recorded under `mixed-run-01`; they must not be overwritten with plane results.

## Paused-scene clarification

The earlier natural-tree and substituted-cherry comparisons did not reproduce the user's canopy defect. Rechecking all eight valid natural/cherry runs found twelve identical PNGs and indexed buffers per run, all at reported tick 3133831. Their small deltas were between separate legacy/stable runs, in world guests and held-item/occluder edges, not changing frames within a paused run. Those captures therefore do not demonstrate bespoke UI or animation updates advancing while paused. The target canopy/support relation stayed unchanged, consistent with the user's observation that those displayed views looked correctly ordered. Detailed phase hashes are in `obj/vulkan-parity/plan-b-cherry-visual-audit/paused-repeat-audit.json`.

## Fresh archived-diagnostic run qualification

After diagnostic isolation and the archived plane rebuild, root executed `plane-run-02`. An independent artifact-only comparison confirmed that its complete set of **192 binary artifacts** is SHA-256 identical to `plane-run-01`: all 32 samples' software/GPU indexed buffers, software/GPU RGBA buffers, status buffers and command buffers. Per-file hashes are recorded in `obj/vulkan-parity/plane-run-02/reviewer-one-repeat-confirmation.json`, independently corroborating root's `repeat-equivalence.json`. The prior full-scene and region visual review therefore applies unchanged to the fresh run; this is reuse of verified identical raster evidence, not a new claim of manually observing a device run.

The fresh summary reports validation activated, no validation messages and unchanged inputs. Its strict mixed parity status remains **fail**, with the same contact ambiguities and stable-reference controls documented above. No threshold, expected raster, accepted exception or runtime admission changed. Root separately reports ordinary full run 51 passing 836/836 tests with only the explicitly identified experimental diagnostic excluded; that production checkpoint does not imply mixed-plane parity passed. This reviewer performed no build, test executable or GPU execution for the confirmation.
