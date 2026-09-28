# Retained balloon publication visual review

## Baseline input02: exact compatibility, fixture correction required

Manually inspected ten images from `obj/vulkan-parity/ui-{frozen,current,vulkan}-balloon-baseline-01`: all three physical baseline-0 screens, both indexed comparison images and mask, physical difference, and baseline-1 physical reference/candidate/difference. Exact paths and SHA-256 receipts are in `vulkan-balloon-visual-review.json`.

All three runs pass. Frozen/current software and retained-publication Vulkan have identical indexed and physical RGBA bytes in both repetitions. The Vulkan generation reports 24 retained balloons, zero balloon bulk-copy bytes, and canonical raw state equal to the frozen reload. This qualifies publication compatibility only; GPU-native balloon drawing is not enabled.

The real park is clearly visible with top toolbars, pause controls, bottom guest/status panels, grass and map boundaries. The separated grid shows eight intact balloon frame positions, five valid popped states, and a central tightly overlapping cluster with partly occluded heads and strings. Inspected difference images are black.

Manual review also caught a fixture error: the three lower-grid records with popped frames 5–7 draw unrelated structure sprites, identically in all three renderer lanes. `Balloon::update` removes popped balloons at frame 5; `PaintBalloon` blindly adds the raw frame offset. These states therefore sample beyond the legitimate balloon animation family. The raw fixture had documented them as unreachable states, but describing them as 16 balloon variants was incorrect.

Preserve input02 and its captures as invalid-state compatibility evidence. Create a new immutable recipe replacing those three records with additional valid intact frames/colours, then recheck the census and pixels in all lanes. Native balloon admission must reject unsupported popped frames instead of using this passing invalid-state sample to claim complete balloon animation support. The legitimate family has eight intact and five popped sprite frames. This is a test-fixture correction, not a renderer exception or approved pixel divergence.

The guarded correction is staged at `obj/vulkan-parity/balloon-recipe-v2-staged`: explicit recipe `balloon-static-v2` changes the three invalid popped records to intact frames5–7, retaining24 total balloons. The v1 recipe remains explicit compatibility evidence. V2 export, independent reloads, builds and corrected visual inspection are pending; no corrected-parity result is claimed yet.


## Corrected v2 legitimate animation and window ordering review

The corrected `balloon-static-input-03` recipe contains 24 records: eight intact frames, five valid popped frames, three extra intact frames and eight overlapping intact balloons. Both fresh processes passed all three lanes (frozen software, current software and retained-publication Vulkan), across baseline, full redraw overlap and incremental overlap. These 18 runs contain 156 captures. The earlier v1 invalid-state compatibility evidence remains above; no renderer exception was granted.

Manual inspection covered all decoded image content from the captures and both indexed/physical comparison triplets, including full-state comparisons: 1,484 PNGs reduce by exact decoded RGBA SHA-256 to 11 distinct images. Each of those 11 was opened and inspected at full sample size; the machine-readable receipt pins every source image, each group and the run summaries. This includes both repetitions and both fresh processes, rather than treating an uninspected repeated image as a new visual judgment.

The baseline shows a nonblank flat park with real toolbars, status, map edges, eight intact animation states and five progressively fragmented popped states. The three unrelated building sprites are gone; their positions show replacement intact balloons. The central overlap cluster has orange/purple heads and partly hidden stems. Finances correctly occludes the upper/central grid, Research moves in front and behind Finances as specified, the negative-x window clips at the left screen boundary, and closing windows restores the baseline positions. Indexed silhouettes and physical palette colours agree; every difference/mask image is black. Empty/restored states are decoded-identical to baseline.

The `*-overlap-incremental-02` summaries explicitly pin comparison against frozen full-redraw overlap process 01 in addition to same-profile process 01. Each captured Vulkan generation reports 24 retained balloons and zero steady capture `bulkCopiedBytes`/`copiedBalloonBytes`; bootstrap totals are nonzero and remain separately reported. `gpuAdmission` is false: these results qualify immutable retained publication with the existing CPU paint path, not the staged GPU balloon family. Ordering, uploads and skipped CPU graphical work require a subsequent native-enabled qualification.
