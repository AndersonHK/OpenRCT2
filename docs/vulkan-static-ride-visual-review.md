# Static ride visual review

Build108 is visually reviewed with defects and a fixture qualification caveat. This is not a pixel-parity pass, a full-world approval, or a performance result.

The review covered all64 comparison pages `objects-r{0..3}-z{0..1}-page{00..07}.png` in `obj/vulkan-parity/building108-specimens-01`:23 static ride, shop, facility, tower and maze families, each in four rotations and two zooms (184 specimens). Each page places pristine upstream, native Vulkan and amplified difference side by side. Station item23 appears incidentally on page7 and has a separate reviewer. Entrances and ordinary coaster stations are separately reviewed by their owners.

The exact reviewed page and specimen PNG SHA256 values are preserved in `obj/vulkan-parity/building108-flat-review/reviewed-files.json` (SHA256 `5fc31f60ff8a58d950037d868d2956104c046d0e05181baa2883be51c31bbf25`). The comparison summary SHA256 is `957217ae1e66186bce983aca767a023d7e40ef6e94ba1082303e36d8b05e766a`. The summary records every input indexed-buffer hash and fixed-rectangle mismatch count. Rectangles overlap and their counts must not be summed into a whole-frame result. No masks, pixel tolerances or new acceptance exceptions were used.

## Observations

The principal parked body shapes, placement and colours align at zoom0 across the reviewed rotations. Tower bodies, stacked sections and caps align visually. This does not establish exact parity for the surrounding platforms, physical supports or cross-object occlusion.

- Maze item22 shows incorrect internal wall visibility in every view. Its authored wall image/offset rules agree with the original painter; emitting those parents in recipe order misses the original local bounds arrangement. A bounded local maze arranger and an exhaustive CPU comparison against the production legacy arranger are prepared for the next build. Build108 images do not qualify that correction.
- Small fence/platform ground-contact differences recur. Some can involve omitted physical supports or interaction with another tile's parents. Their exact causes remain open pending the corrected fixture; they are not approved differences.
- Dodgems item5 has roof/portal overlap differences, and Enterprise item11 rotation3 has a ride/entrance overlap difference. These require cross-object ordering or filter-destination investigation with the fresh fixture. Isolated entrance alignment does not resolve those interactions.
- Cinema item0, Twist item10 and Swinging Ship item12 show broad zoom1 body differences. A confirmed fixture defect prevents attributing those differences to production zoom rules, as described below.

## Fixture correction required before qualification

The old static-building diagnostic reused ride slots after `gameStateInitAll` and assigned only selected fields. `RideInitAll` calls `RideReset`, which resets identity/type/name/measurement but does not value-initialize all flags, vehicle IDs or station state. The original Cinema, Twist and Swinging Ship painters can consequently mark their bodies as entity paint from stale `onTrack`/vehicle state. Entity paint snaps projected anchors at zoom1. This contradicts the diagnostic's intended clean, closed, no-vehicle setup and explains a concrete source of the apparent zoom-phase mismatch. Other stale station/train fields can also influence the old fixture.

Root corrected both static-building and special-track fixtures to allocate rides through `RideAllocateAtIndex`, which creates the normal initialized ride state. Fresh upstream corpus03 must be compared against the next native candidate. No production zoom adjustment was made to match the malformed fixture. All old images and exact failures remain preserved.

## Scope and remaining work

These captures exercise parked static recipes, not operating ride motion, riders, vehicles, arbitrary station configurations or all construction states. General inter-family bounds ordering and physical supports remain incomplete. The already owner-approved outer-map cliff skirt remains a separate intentional divergence; it does not waive interior ride differences. The direct diagnostic background clear was also corrected independently to match the real native viewport/upstream clear index.

Build108 completed the eight original-art views with clean Vulkan validation, but that does not qualify the earlier build107 EverythingPark device loss or establish acceptable pipeline creation cost. The next checkpoint still requires CPU rule checks, fresh original-art images, manual review and the serial real-park performance/safety run.

## Build110 against the corrected fixture

All64 build110 pages0..7 were manually viewed again, covering all184 specimens for items0..22 at four rotations and zooms0/1. These compare `building110-art-01` with the corrected `building-upstream-corpus-03`. The new review does not reuse the earlier images as evidence for the fix.

Exact SHA256 coverage is in `obj/vulkan-parity/building110-flat-review/reviewed-files.json` (SHA256 `cb304c04e2f458a855179dd1f4cff98e69eb89323178ee7ded8fd329c0ba50b3`). It pins all64 reviewed page PNGs and184 specimen PNGs, includes the unchanged fixed-rectangle results, and records the eight additional maze rectangles. The source comparison summary SHA256 is `3d5d66ce347c920579a4d8f138c677f9bfa50e6b589cfd9a0985da702218511d`. The artifact-only script `obj/vulkan-parity/building110-flat-review.py` records the calculation; it reads saved buffers and does not execute a renderer.

The bounded local maze correction is now qualified for the captured maze geometry: its wall visibility and internal gaps match visually in all eight views. A fixed96×88 rectangle around its authored anchor at zoom0, scaled to48×44 at zoom1, contains the full maze and has **zero indexed-pixel differences in every rotation and zoom**. These rectangles are chosen from the authored anchor, not from a difference mask. They do not replace or relax the whole-image comparison. The larger original specimen rectangles include neighboring portals and consequently retain some zoom0 differences unrelated to the maze body. Root also reports all46 CPU preflight checks passed in2.879 seconds, including the exhaustive65,536 maze masks ×4 rotations against actual legacy `PaintSessionArrange`.

The broad zoom1 body differences previously visible on Cinema, Twist and Swinging Ship disappear in the fresh, properly initialized fixture. Their principal parked body placement and sampling now visually align across the eight views. This confirms that changing production zoom snapping to imitate the old fixture would have been unjustified. Narrow floor/base differences can still occur, so this observation is not an exact whole-family approval.

The three tower families' static bodies, sections and caps align visually in all eight views. Other isolated shop/facility and parked ride bodies also align in shape and colour; surrounding objects can contribute differences inside their deliberately broad specimen rectangles. Space Rings correctly has an empty parked floor when the clean fixture has zero trains, so these images do not qualify its nonempty seat/body configurations.

Remaining visible defects are preserved:

- Dodgems roof/filter and ride-portal overlaps differ in all four rotations at both zooms. The native portals appear above roof regions that cover or darken them in upstream. The fresh fixture confirms this is not solely stale ride state.
- Enterprise's entrance roof visibly covers part of its parked ride in native rotation3 where upstream draws the ride in front; smaller front platform/fence contacts also differ in other rotations.
- Small floor/fence/ground-contact strips remain on several families, including the ship bases, Flying Saucers boundary, Ferris Wheel contacts and Space Rings floor. Some platform/entrance contours recur in neighboring specimens. This review does not claim a source cause for every pixel or waive them as physical supports.
- The background boundary and other previously recorded general scene limitations remain separate from the newly qualified maze result. Whole-image and all-specimen exact comparison still has status `fail`.

The small build110 GPU lifecycle test passed with clean validation, but its70.729-second duration still exposes unresolved cold pipeline creation cost. Visual agreement on these static captures does not establish large-park throughput, operating animation correctness or resolution of the build107 device loss. No source, shader, build, test or GPU execution was performed during this visual review.

## Build110 real EverythingPark view

Manually inspected the full3840×2160 final image from `performance-buildings110-12000-01` against `performance-objects106-12000-01`, then four unscaled side-by-side crop pairs covering the flat-ride row, central stations/mazes, roof/ride area and boundary tracks. The original PNG SHA256 values are:

| Run | Final PNG SHA256 |
| --- | --- |
| Build106 | `00d653a863ca3c418e6cf4711d07b2873f83e0faa73c21af4854a5b1ea4fdd4e` |
| Build110 | `6f718a1b489176d131c017ce6c95ba052edd49b4d76ccc6af3ff7f108eb9534f` |

`obj/vulkan-parity/building110-park-review/manifest.json` records both input paths/hashes and exact rectangles/hashes for the four reviewed crop PNGs. Their top half is106 and bottom half110. These are unchanged-pixel crops from saved output, not a new renderer run.

The added geometry is clearly visible: Cinema domes and other flat bodies fill the previously vacant foreground ride row; towers, maze walls, portals and station roofs populate the central and upper ride grids. Numerous formerly disconnected track sections now connect, including the large purple spans along the forest boundary and further blue/orange coaster curves. I did not see a newly blank broad region, a whole building block translated away from its footprint, or general frame corruption. Terrain, the existing scenery rows and the intentional outer cliff skirt remain visually consistent with106.

This is an improvement comparison against a partial native renderer, not an upstream correctness oracle. Physical supports remain missing; pink water-slide-like sections and some lower-right track runs still look fragmented. Parked ride bodies do not establish operating animation, vehicle or rider rendering. The static corpus's known portal/roof and ground-contact ordering defects remain open even when too small to diagnose in this zoomed-out real-park view.

The completed110 run reports357.408 TPS,144.006 application FPS,2.413ms GPU time and0.599ms CPU draw, with no device reset during this attempt. The TPS result is below the360 target. Its successful completion is evidence for this one run, not proof the earlier107 reset can never recur. CPU draw growth and cold pipeline compilation remain separate performance investigations; this visual review neither ran the benchmark nor changes its receipt or acceptance policy.
