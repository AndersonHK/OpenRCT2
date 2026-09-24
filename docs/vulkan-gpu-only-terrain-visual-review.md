# GPU-only terrain visual review — 2026-09-23

Independent manual inspection of the final EverythingPark image at full-frame and original 3840×2160 resolution. This is a visible partial-render checkpoint, not original-renderer parity. No build, game, test, or GPU execution was performed for this review.

## Inspected artifact and provenance

- Image: [final-benchmark.png](../obj/vulkan-parity/performance-partial85-01/final-benchmark.png).
- PNG SHA256: `97e50f08f3be0380696f126a95854d84e3795d01b76fdc31ec96f085a5c73543` (independently read from the file; agrees with receipt).
- Indexed pixels SHA256, as recorded by the runner: `6b3ec8e66be27346d2bbf98e0d17216d9e036b78160ee74b2e02b91e0183bcaa`.
- [Summary](../obj/vulkan-parity/performance-partial85-01/summary.json) SHA256: `6ef54d3566d419ab7c4028600030a16ab3a80a91a9f1a4fe70516a50294f2349`.
- Build receipt: [performance-partial-build85/receipt.json](../obj/vulkan-parity/performance-partial-build85/receipt.json), recorded SHA256 `4afa139f11ae1dca27b8609490ff7abba05a31c172427bb0670edcd6b5e1801c`.
- Camera: rotation **3**, zoom **2**, view position **(458, −4370)**, flags **536871936**. Simulation and publication source tick both **3136931**; completed final frame **1261**. Logical and physical extent both **3840×2160**.

The summary passes its qualification. The screenshot receipt reports a new completed draw after measurement stopped, with authoritative state and camera unchanged. It explicitly identifies the render scope as GPU terrain/background/UI with unsupported world categories omitted. This review read that provenance; it did not rerun its qualification checks.

## Visual observations

The terrain diamond occupies the exposed main viewport across the image. I do not see the previous first-rectangle-only failure, a large axis-aligned cleared pane, a displaced duplicate map, or whole-frame texture corruption. The flat grass areas use consistent isometric axes and repeated original-art texture. The grey and coloured surface patches remain distinguishable. The top toolbar icons and bottom status text are visible, with no evident terrain overdraw over those UI elements. These observations establish basic coverage and plausible projection/palette use at this camera; they do not establish exact placement or colour parity.

The picture is visibly incomplete even within terrain. There are dark triangular openings through the central raised formation, disconnected dark angular bands elsewhere, and a long dark gap near the lower-right map boundary. The lower-right raised formations lack a continuous closed surface. The map border is incomplete, and a small isolated green tile-shaped mark appears below its bottom point. These are recorded limitations, not accepted pixel correctness: omitted terrain sides/border and approximate ordering are plausible contributors, but this single image cannot identify every cause or distinguish absent source surfaces from rendering holes.

Guests, staff, vehicles, tracks, supports, paths, scenery, fences, and water presentation are not shown as a complete park. Their absence is intentional for this partial checkpoint. The remaining coloured/grey patches must not be interpreted as proof those families have migrated; the current pass renders surface artwork.

## Meaning of the result

The run reports **355.820 TPS**, **0.366387 ms CPU Draw**, and **0.477211 ms GPU frame**, versus the complete-world build80 control's **67.379 TPS**, **26.676563 ms**, and **1.685051 ms**. Both run 3000 measured simulation ticks, and the runner reports matching initial/final censuses and final entity checksum. The faster result includes deliberately omitted rendering work; it is not a like-for-like full-world renderer gain. Application draw rate is 142.684 FPS; the maximum recorded draw-start interval is 86.908 ms, so the averages do not establish stall-free presentation.

This is one final indexed main-canvas image at one camera, captured outside timing. It does not prove other rotations/zooms, moving-camera behaviour, secondary viewports, overlapping opaque windows, animation, complete occlusion, physical scanout cadence, or upstream pixel parity. No additional blocking whole-frame projection, palette, or clipping failure was identified for this specific incomplete-render benchmark image.
