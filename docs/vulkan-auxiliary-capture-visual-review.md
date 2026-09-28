# Production CaptureImage visual review

Reviewed 2026-09-23 by reviewer one. The migrated production `CaptureImage` output matches the separately built upstream software CLI exactly in the four reviewed static views. The scene is populated and contains meaningful tree, track and support overlaps. No differing region or acceptance exception was found.

## Ordinary zoom 0 capture

The immutable receipt is [`auxiliary-capture-upstream-01/summary.json`](../obj/vulkan-parity/auxiliary-capture-upstream-01/summary.json), SHA-256 `5c809c201eb30d2196cf4d5a8f44f688e16efacb55c4f4aa912b4ec30011e592`. Its `exact-match-pending-review` status is preserved; this document supplies the separate manual review.

The unmodified EverythingPark camera is `(4944,6736,336)`, rotations 0–3, zoom 0, output 640×480, transparency disabled. Current diagnostics identify the actual `CaptureImage` API, a production file under each fresh profile's `screenshot/capture-image.png`, and an identical copied comparison PNG. The world camera Z equals the production API's terrain-derived Z. Source tick, tick before and tick after are all `3133831` in every current case.

The receipt pins these inputs:

| Input | SHA-256 |
| --- | --- |
| Upstream build `upstream-screenshot-build-01/receipt.json` | `965b40dff3363291dc99c4034b412d8d5148c4f4c6afdbda224988de3f32b025` |
| Current wrapper build `auxiliary-cli-build-01/receipt.json` | `62eaa7cc9a3c9e5782c2ced76d03876fc0677019fa88c95137670eeff9318e8f` |
| Shader build `build-68/receipt.json` | `9ed95526d7246e2660528dcd0212c777ff60d0af05d1159ea2e4e6d07f0ff609` |
| Comparison runner | `d5ba3de55197edca8c3d7fc44d870e00f3f94bd3132fd71e8eb4b69f0da51856` |

Upstream revision is `b80a4a84e92be8e07904b38d1032d0bb88280bb4`. The frozen package supplies immutable art and runtime dependencies only; its renderer is not the reference. Complete build, park, art and output identities remain in the receipt.

I separately opened all eight full reference/candidate images and all four amplified differences in [`comparisons`](../obj/vulkan-parity/auxiliary-capture-upstream-01/comparisons). Each full pair looks identical, and every amplified difference is completely black. Decoded indexed pixels, palette entries and RGBA pixels each have zero differences at every rotation.

| Rotation | Observations |
| --- | --- |
| 0 | Large diagonal wooden lift, dense lattice supports, orange track, birches, paths, guests and balloons. The foreground tree silhouettes and support intersections look plausible. |
| 1 | Lift and curved track cross the scene above red-roofed buildings, queues and several trees. The canopies and trunks have the same visible occlusion in both images. |
| 2 | Central dark tower, broad supported wooden curve, birches and red-roofed buildings. This is a complete rendered world view, not an empty or loading frame. |
| 3 | Broad banked wooden curve, dense support field, trees at the right and bottom, populated path and balloons. No isolated cutout or ordering difference is visible. |

These reviewed files were independently hashed after capture completion. Paths are relative to `auxiliary-capture-upstream-01/comparisons/`.

| Reviewed files | SHA-256 of each file |
| --- | --- |
| `r0/reference.png`, `r0/candidate.png` | `617fe21e72a53515584b4ecb3dd18a2390ac4cb5fb8d47c046a1fa1ca0bbdfe7` |
| `r1/reference.png`, `r1/candidate.png` | `6041eda513abfcd4731dbacc1136ccd4e5fc4cb7deabf832ca84c09778bcf708` |
| `r2/reference.png`, `r2/candidate.png` | `8f6a4af6c0a1d9c2d59cd4182f74c166b916ce6fc466183912104baa7d23afd8` |
| `r3/reference.png`, `r3/candidate.png` | `764abe5e25800d8055baf20faa238963742ee9b5122fd400d3caa3f97f4d3a21` |
| `r0/diff-amplified.png`, `r1/diff-amplified.png`, `r2/diff-amplified.png`, `r3/diff-amplified.png` | `9347fe86f6b448518e30d671057b4ece3e7f2cf3906488168f63736a4c990bc0` |

Every current case reports one shared Vulkan service and device, one completed and retired tile session, zero live sessions, and peak live count one. The runner checked that the owned tile's indices equal the production PNG, and that its palette, output ownership, camera and source tick match. Khronos synchronization validation activated in all four current processes and emitted no recorded diagnostics. All eight processes exited successfully and the receipt has no failures.

## Transparent zoom 1 attempt: partial review, run failed

The separate immutable receipt [`auxiliary-capture-transparent-upstream-01/summary.json`](../obj/vulkan-parity/auxiliary-capture-transparent-upstream-01/summary.json) has SHA-256 `78eaa7995c6ccfe34f57cbb8a33466090434c8056ff5c4ca1c774d831d56ed3c` and status `fail`. This result is not accepted as a complete four-rotation run. It used the same centre and 640×480 extent at zoom 1 with transparency requested.

Current rotation 1 exited with `GPU sprite atlas layer limit reached`. Its report has `deviceCreations: 0`, `ownerCreated: false`, no completed tile, and no `captureImage` metadata. Its one begun session retired with zero live sessions. Validation activation was absent; this follows the pre-device failure rather than constituting clean Vulkan execution. There is no current rotation 1 PNG to review. The runner produced comparisons for rotation 0, then stopped comparison assembly at missing rotation 1; the successful rotation 2 and 3 lane artifacts remain available.

I explicitly opened the six actual `upstream-software/r{0,2,3}/screen.png` and `current-vulkan/r{0,2,3}/screen.png` files, plus the existing rotation 0 amplified diff. I did not substitute images from the ordinary run or claim nonexistent rotation 2/3 diff images. Each reviewed pair appears identical, and rotation 0's amplified difference is black. Independent SHA-256 comparison of the saved indexed, palette and RGBA buffers confirms equality for all three available pairs, including rotations 2 and 3 that the runner did not reach in its comparison table.

These wider views contain the wooden coaster, dense supports, trees, guests and balloons, surrounding flat rides and water. Rotation 0 includes the tall purple track across the foreground; rotation 2 shows the wooden banked turn and tower; rotation 3 shows the outer wooden curve and adjacent white coaster. Visible tree/support overlaps look plausible and match their reference images. No differing region was found.

| Reviewed file pair within `auxiliary-capture-transparent-upstream-01/` | SHA-256 of each PNG |
| --- | --- |
| `upstream-software/r0/screen.png`, `current-vulkan/r0/screen.png` | `c4038f169625348afc5c6402675395518c387daaaddcc3a70751b7ad72ce1ed5` |
| `upstream-software/r2/screen.png`, `current-vulkan/r2/screen.png` | `ce5e44daf836829f7c6091d9df8a7c7273f7594e287dcd011eca81b940162bdb` |
| `upstream-software/r3/screen.png`, `current-vulkan/r3/screen.png` | `6bfb55413a2b2dce78d49e9388dc1d15651e2cbf9985e1f7748bc6b4229c9e2b` |
| `comparisons/r0/diff-amplified.png` | `9347fe86f6b448518e30d671057b4ece3e7f2cf3906488168f63736a4c990bc0` |

Successful current rotations 0, 2 and 3 each report transparency true, viewport flags `524288`, unchanged tick `3133831`, successful owned tile validation, Khronos validation activation and zero recorded validation messages. All 307,200 decoded pixels in each successful RGBA artifact nevertheless have alpha 255: the visible world covers the frame. Thus these cases exercise the requested transparency mode and zoom-1 rendering, but do not demonstrate transparent background pixels or alpha edges. Full transparent-mode qualification remains incomplete because rotation 1 failed; a corrected fresh run must be reviewed separately without rewriting this evidence.

## Transparent zoom 1 corrected attempt: complete review

The new immutable receipt [`auxiliary-capture-transparent-upstream-02/summary.json`](../obj/vulkan-parity/auxiliary-capture-transparent-upstream-02/summary.json), SHA-256 `176a95820f75e4605e44afd94ff7883b34b4f9280e95e665185653a923873a1f`, reports `exact-match-pending-review` with no failures. This is a fresh capture after the configured service's atlas capacity was aligned with the display renderer's existing 64-layer limit. Attempt 01 remains a failed historical result above.

The camera, park, 640×480 extent, zoom 1 and transparency request are unchanged. The new current build receipt `auxiliary-cli-build-02/receipt.json` is pinned at `46d4bfa23420436fb9ed0cd291cda6aaab89bd531e06d5803406d96b7096562e`; the shader build receipt `build-69/receipt.json` is `16a94ceedc68ad1e64ec83e2ef0b08077f41479146b4b90a3cc1b608138da4f2`. The runner and upstream build hashes remain those reported above.

I separately opened all eight full images and all four amplified differences from this new run's [`comparisons`](../obj/vulkan-parity/auxiliary-capture-transparent-upstream-02/comparisons). All four pairs look identical; all four differences are entirely black. The receipt reports zero indexed, palette and RGBA differences at every rotation, with qualified production PNG/tile diagnostics, successful processes, validation activation and no recorded validation messages in all four current cases. Rotation 1 now contains a complete populated view of the wooden lift and lattice supports, central tower, trees, paths, flat rides and lake. Its visible tree/support overlaps look plausible and agree with upstream; no exceptional pixel treatment was needed. The other three views have the same appearances described for attempt 01.

Independent hashes of every reviewed image follow. Paths are relative to `auxiliary-capture-transparent-upstream-02/comparisons/`.

| Reviewed files | SHA-256 of each file |
| --- | --- |
| `r0/reference.png`, `r0/candidate.png` | `c4038f169625348afc5c6402675395518c387daaaddcc3a70751b7ad72ce1ed5` |
| `r1/reference.png`, `r1/candidate.png` | `42a0758802824d8d15a8eb217999bc5bc453e0801cc304de189b65e8d24afc3c` |
| `r2/reference.png`, `r2/candidate.png` | `ce5e44daf836829f7c6091d9df8a7c7273f7594e287dcd011eca81b940162bdb` |
| `r3/reference.png`, `r3/candidate.png` | `6bfb55413a2b2dce78d49e9388dc1d15651e2cbf9985e1f7748bc6b4229c9e2b` |
| `r0/diff-amplified.png`, `r1/diff-amplified.png`, `r2/diff-amplified.png`, `r3/diff-amplified.png` | `9347fe86f6b448518e30d671057b4ece3e7f2cf3906488168f63736a4c990bc0` |

Independent inspection of decoded alpha bytes again finds exactly 307,200 pixels with alpha 255 in every current image, including rotation 1. The successful run therefore qualifies the transparency request path and these four zoom-1 views, but not visible transparent background pixels or alpha edges.

Read-only source review of the capacity fix found no issue: the factory assigns `Gpu::kAtlasLayers` to `RenderServiceOptions::atlasLayers`, which feeds both the recording cache and executor allocation. The new `RenderServiceProductionRecordingTest.RecordsFiveSpriteSizeClassesWithoutAcquiringDevice` uses widths 32, 64, 128, 256 and 512 to require five distinct allocation classes, meaning the former four-layer setting cannot satisfy it. It also checks cancellation/retry and absence of device creation while recording. That focused test covers recording capacity and laziness; actual output and GPU submission are covered by the external comparison, not by the recording test. The fixed capacity retains the documented 256 MiB R8 atlas allocation tradeoff when submitted; this review does not claim a reduced memory footprint.

## 4K tiled CaptureImage: complete review with visible transparency

The immutable receipt [`auxiliary-capture-4k-upstream-01/summary.json`](../obj/vulkan-parity/auxiliary-capture-4k-upstream-01/summary.json), SHA-256 `f3c7594582dd4b9376a36090ebe3cbac19b0026ad341a591efb8392a5a13deb9`, reports `exact-match-pending-review` and no failures. This actual bounded `CaptureImage` call uses 3840×2160, the same centre `(4944,6736,336)`, rotation 1, zoom 1, and transparency true. It has `giant: false`: this is a large explicit view, not the whole-map camera selection or interactive `ScreenshotGiant` entrypoint.

I opened the full reference/candidate pair and amplified difference under [`comparisons/r1`](../obj/vulkan-parity/auxiliary-capture-4k-upstream-01/comparisons/r1). The full pair contains a broad populated area of EverythingPark, with many coasters, dense supports, tree rows, paths, water and rides. The amplified difference is black. The receipt reports zero indexed, palette and RGBA differences, and the full PNG hashes agree:

| Reviewed artifact | SHA-256 |
| --- | --- |
| `comparisons/r1/reference.png` and `comparisons/r1/candidate.png` (each) | `55f8c4de11a6f4d1efeaf718faf92dd533ae7f45314cf92ad11b349dae3621e9` |
| `comparisons/r1/diff-amplified.png` | `f7386401a241a025d5598360dbe3165348cde2ed9db6b9baf4f4ae0d8e5a937c` |

The full-image viewer downsampled to 2048×1152, so I also created and separately viewed full-resolution crops of both source images around vertical seam x=2048, horizontal seam y=2048, their intersection, and the visible park/transparent-background boundary. The crops composite alpha over a checkerboard for inspection; they do not alter the comparison inputs. Pixel rectangles and hashes are preserved in [`auxiliary-capture-4k-visual-audit/receipt.json`](../obj/vulkan-parity/auxiliary-capture-4k-visual-audit/receipt.json). No discontinuity, missing strip, shifted track, support cut or alpha mismatch is visible across the reviewed seams. Each reference/candidate crop has identical bytes.

| Full-resolution crop rectangle `(left,top,right,bottom)` | SHA-256 of each reference/candidate crop |
| --- | --- |
| Vertical seam `(1792,704,2304,1216)` | `f66e3ba8036de108a28120b6a95b51a0888d220449d5e80742d5665bf4923877` |
| Horizontal seam `(2720,1936,3232,2160)` | `9c8da2a6b87a128df89bb0cb5f5dd41339791654af87c78de55f8687d408ef1c` |
| Seam intersection `(1792,1936,2304,2160)` | `9fdc12c8cc9208901fc60e5c496998410313c842f6b6b67a95556778d990fe7b` |
| Alpha boundary `(0,784,512,1296)` | `f5e868e0dab33d539a6942505fd439d564018db311eb4d94bd82eb2a36dfdafe` |

Independent counting of the current decoded RGBA buffer finds **1,539,332 transparent pixels (alpha 0)** and **6,755,068 opaque pixels (alpha 255)**, totaling 8,294,400 pixels. No intermediate alpha values occur. The transparent background is visible outside the park in the checkerboard crops, including the seam-intersection crop. Unlike the smaller captures, this comparison exercises visible transparent output and its boundary, with exact upstream agreement.

Four sequential tile sessions use extents 2048×2048, 1792×2048, 2048×112 and 1792×112. The receipt records four begun and retired sessions, zero live sessions and peak live count one. Each tile begins after the preceding tile completed and retired, all at unchanged source tick `3133831`; owned tile pixels were checked against the production PNG. Khronos synchronization validation activated and recorded no diagnostics. This qualifies this 4K tiled offscreen output and its visible transparency, without implying 4K main-window performance or VSync qualification.

## Giant CLI corpus against frozen historical software

The immutable [`vulkan-only-giant-02/summary.json`](../obj/vulkan-parity/vulkan-only-giant-02/summary.json), SHA-256 `3519a97902b18bddf34c4fc1dbad9a54e98701accb01f637c7f1020648cc7ff8`, reports all 20 cases passing without failures. **This corpus uses the frozen historical software renderer, not the separately built upstream oracle used above.** Its reference receipt is [`giant-cli-frozen-02/summary.json`](../obj/vulkan-parity/giant-cli-frozen-02/summary.json), SHA-256 `498410ffb474c45525053323a2b9fe89eee427a437ce60fd46f91b08a11d1ce6`. The deterministic `giant-seams-v1.park` SHA-256 is `e2cc424634e592b9ba0783dfe042ec7d0f9e2338132f2caa1558c07171d48d0f`.

Coverage is four rotations at zoom 0 and zoom 1, plus rotation 0 at zoom 2 and zoom 3, each with ordinary and transparent backgrounds. Extents and submitted tile counts are 6016×3488 / six tiles at zoom 0; 3008×1744 / two tiles at zoom 1; 1504×872 / one tile at zoom 2; and 752×436 / one tile at zoom 3. All 20 cases declare zero indexed, palette, alpha and RGBA differences. I independently hashed all four decoded buffers for all 20 reference/candidate pairs and confirmed equality. Every current case records validation activation and zero validation messages.

I reviewed paired full-world contact sheets covering all 20 cases and native-resolution paired seam crops for `ordinary-r0z0`, `ordinary-r1z1`, `ordinary-r2z0`, `ordinary-r3z1`, `transparent-r0z1`, `transparent-r1z0`, `transparent-r2z1`, and `transparent-r3z0`. This deliberately covers every rotation and both tiled zoom levels across both background policies. Full-world sheets include the zoom-2 and zoom-3 cases as well. The fixture contains repeated sloped terrain, steep cliff faces, water patches, tall landmarks and balloons; it is a seam stress fixture rather than an ordinary ride-filled park scene. Full views retain the complete map silhouette and landmarks. Sampled seam regions show continuous slope patterns, water surfaces, cliff textures and sprite pixels with no new horizontal or vertical cut, missing strip or reference/candidate discrepancy. Transparent full-world sheets show the background checkerboard up to the map edge; ordinary backgrounds remain opaque and visibly distinct.

All original PNG hashes, independent decoded-buffer equality results, crop rectangles and generated review-sheet hashes are recorded in [`vulkan-only-giant-visual-audit/receipt.json`](../obj/vulkan-parity/vulkan-only-giant-visual-audit/receipt.json), SHA-256 `93ad9506191b9c6f90d0147ca23ce441a383b241f65d0eb95bc92fec807011dc`. The four viewed full-world sheets are `fullworld-0.png` through `fullworld-3.png` in that directory. Zoom-0 seam crops span `(1792,1792,2304,2304)`, crossing x=2048 and y=2048; zoom-1 crops span `(1792,600,2304,1112)`, crossing x=2048. These artifact-only crops composite transparency over a checkerboard and leave all immutable inputs unchanged. No divergent pixels or visual exceptions were declared.

The current `Screenshot.cpp` giant CLI branch uses the shared private `RenderViewport` helper, which calls `ScreenshotTiling::Render` on the context-owned offscreen service. `CaptureImage` and interactive `ScreenshotGiant` also call this helper. The corpus therefore adds meaningful exact output coverage of that shared giant render/tiling implementation and CLI whole-map camera selection. It does not itself invoke interactive `ScreenshotGiant` or test its UI notification/file-error flow, and frozen historical agreement is not independent upstream agreement.

Fresh repeat [`vulkan-only-giant-03/summary.json`](../obj/vulkan-parity/vulkan-only-giant-03/summary.json), SHA-256 `64b491fe841e2bab5a53a9ea60f27fa7bea6f5bd1dcd7b6469d01302168fe896`, also reports all 20 cases passing against both the frozen reference and run 02, with no failures and activated clean validation in every case. Each run submitted 68 tiles in total, with maximum assembled output 6016×3488. I independently compared all 100 PNG/indexed/palette/alpha/RGBA files across the two runs: every SHA-256 matched. This permits reuse of the completed visual review without claiming a second image-by-image inspection. The independent result is [`vulkan-only-giant-visual-audit/repeat-equivalence.json`](../obj/vulkan-parity/vulkan-only-giant-visual-audit/repeat-equivalence.json), SHA-256 `070398039cb5d4d757316e44b28adaceebaa6bab71d9253ea863a3280e6056ba`.

## Scope

This qualifies the actual bounded `CaptureImage` entrypoint for this park area and four rotations in ordinary zoom 0 and transparency-requested zoom 1 captures, plus one 4K zoom-1 rotation with four tiles and visible transparent background. The separately identified giant CLI corpus qualifies its shared tiling implementation against frozen historical software for the listed 20 fixture cases. It does not qualify interactive `ScreenshotGiant`, ParkPreview, main-window presentation, moving vehicle order, HDR, 4K VSync or TPS. Nor does exact upstream agreement establish that the originally reported pink-canopy ordering defect was reproduced or fixed.

No build, test, game or GPU process was run for this review. No immutable capture artifact or production source was edited.
