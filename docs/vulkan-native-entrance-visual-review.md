# Native entrance visual review

## Supersession status

The build-108 images below are historical diagnostic evidence, not the current acceptance fixture. The replacement upstream corpus `building-upstream-corpus-03` uses normal `RideAllocateAtIndex` allocation instead of the earlier fixture's malformed ride setup. The direct GPU art-test background clear was also corrected from index 0 to upstream's index 10; the production world renderer already used index 10. Fresh build-110 review is complete below; its findings were checked against the new images rather than carried forward automatically.

## Build 110 — corrected fixture

Manually inspected all eight `objects-r*-z*-portals.png` sheets in `obj/vulkan-parity/building110-entrance-review` and all eight `objects-r*-z*-item26.png` park-entrance comparisons there. These are 416 fixed, unmasked entry/exit rectangles and eight full park-entrance specimen crops, covering rotations 0–3 and zooms 0–1. They use the same analytical projection/crop procedure as the historical review, with fresh corpus-03 and build-110 indexed inputs. No runtime, GPU, build, or production-source work was performed for this review.

Isolated red-roof portal bodies still match in placement, orientation, colours, and internal front/back ordering. Park-entrance towers, statues, arch and banner geometry also match. No isolated entrance shader correction was identified. In total 201 of the 416 unmasked portal rectangles are exactly equal; the others include missing text and neighbouring ride differences, so this is not a whole-entrance parity pass.

Current unwaived findings:

- Scrolling glyphs remain absent at zoom 0. Specimen 01 still has 63 entry differences per rotation, with 6/9/9/6 neighbouring glyph pixels in exit rectangles; both rectangles are exact at zoom 1 in all rotations.
- Dodgems (05) still composites its roof and portal differently in every rotation at both zooms. Enterprise (11) still overlaps the portal differently in rotation 3. These shared scene-order/compositing defects survive the corrected fixture; isolated portal recipe changes would not repair them.
- The former neighbouring zoom-1 body-sampling differences for Cinema (00), Twist (10), and Swinging Ship (12) are no longer visible in these portal crops. Small support/geometry differences elsewhere remain, including the ship's six-pixel neighbouring line at r0/z1.
- Park-entrance specimen rectangles have exact difference counts r0/r1/r2/r3 of **76/0/320/370 at zoom 0** and **0/0/80/80 at zoom 1**. Missing banner glyphs and neighbouring entrance glyphs explain the forward-facing text differences. Rotations 2 and 3 retain a thin map-boundary line; the previous solid background-wedge mismatch is gone. All counts remain unmasked.
- Transparent/ghost station glass, varied station styles, elevated entrance supports, and lights remain unqualified. The fixture still uses station slot 0 for present ride portals and absent slot 65535 for specimens 17 and 18. No new glass coverage is inferred from the successful opaque bodies.

Fresh artifact identities (paths relative to `obj/vulkan-parity`):

| Artifact | SHA-256 |
| --- | --- |
| `building-upstream-corpus-03/objects.park` | `ae15a452ddde9898def6ec5903c987b3edabd1a92ee9be07592c3c9cd2015da5` |
| `building-upstream-corpus-03/manifest.json` | `11f4773776b4a5153b0692032301b4aaf7e4f5d84e756b6a29ac98fab89ac607` |
| `building110-specimens-01/summary.json` | `3d5d66ce347c920579a4d8f138c677f9bfa50e6b589cfd9a0985da702218511d` |
| `building110-entrance-review/portal-regions.json` | `4a2bea1852be61bad63d5631ca1fe3293920447bf8993a083b7cdf4ec818de1d` |
| `building110-entrance-review/reviewed-artifacts.json` | `7e5fa8ccf7a363dc51da56fb23311e096cf5af9771fdc06f3d0d03636b7cbfff` |

The fresh specimen summary pins all reference/native indexed input pairs, and the fresh reviewed-artifact receipt pins every inspected sheet/crop. The manifest metadata hash happens to match corpus-02; the independently pinned park and indexed buffers identify the corrected fixture.

## Historical build 108

Manual review used saved original upstream/native/indexed-difference artifacts only. No game, GPU capture, build, or test was run for this review. The full comparison remains a failure; this is not entrance or whole-scene parity acceptance.

Reviewed all four rotations at zooms 0 and 1: eight `objects-r*-z*-portals.png` sheets in `obj/vulkan-parity/building108-entrance-review`, containing 416 fixed entrance/exit rectangles across specimens 00–25, plus all eight original `objects-r*-z*-item26.png` park-entrance comparisons. The sheets derive directly from the original indexed buffers and real palette. Zoom-1 rectangles are enlarged by nearest-neighbour sampling for inspection. Counts use the original indexed pixels without tolerance, masking, or replacement; rectangles can contain neighbouring buildings or portals. The supplied specimen sheet `objects-r0-z0-page00.png` was also inspected for context.

The ordinary red-roof entrance and exit bodies align in position, orientation, colours, and their internal front/back composition across these views. Park-entrance towers, statues, arch, and hanging banner geometry also align. No isolated entrance recipe offset, colour, or parent-order defect was identified that warrants changing the entrance shader from these images.

Remaining differences are unwaived:

- Ride entrance scrolling glyphs are absent at zoom 0. For example, specimen 01 has 63 differing pixels in each entry rectangle; its exit rectangles have 6/9/9/6 pixels from the neighbouring entrance text. Both fixed rectangles are exactly equal at zoom 1 in all four rotations. This does not waive text at zoom 0.
- Park-entrance text is absent on the forward-facing banner in rotations 0 and 3 at zoom 0. Its rear-facing banner and body geometry align; neighbouring maze/background differences remain in the larger specimen rectangles.
- Dodgems (specimen 05) overlaps the portal roof/body differently in all rotations. Enterprise (specimen 11) has a substantial ride/portal overlap error in rotation 3. These are shared scene ordering/compositing defects, reported to the flat-ride owner; changing isolated entrance art offsets would not correct them. They persist at both zooms. Other neighbouring ride, maze, and zoom-sampling differences are retained in the exact rectangle counts rather than excluded.
- Entrance supports and lights are not implemented. This flat-ground daytime corpus cannot qualify elevated supports or lighting.
- All present ride portals use station slot 0; specimens 17 and 18 use absent station slot 65535 and omit their portals in both renderers. There is no separately identified transparent station style, varied glass colour, or ghost portal specimen here. Transparent station glass and ghost/glass combinations remain unqualified, even though glass recipe ownership/order has CPU coverage. The park-entrance sample likewise does not exercise coloured station glass.

Artifact identities:

| Artifact | SHA-256 |
| --- | --- |
| `building-upstream-corpus-02/manifest.json` | `11f4773776b4a5153b0692032301b4aaf7e4f5d84e756b6a29ac98fab89ac607` |
| `building108-specimens-01/summary.json` | `957217ae1e66186bce983aca767a023d7e40ef6e94ba1082303e36d8b05e766a` |
| `building108-entrance-review/portal-regions.json` | `d265235e4ac0a2adecf77dd54245117ae7485aa0f88cb1913adf30bccf832bdd` |
| `building108-entrance-review/reviewed-artifacts.json` | `9393946b10138a0852865cc2de07cfeaf106068ca845702ac2aecf026121b00a` |

Paths in the table are relative to `obj/vulkan-parity`. The reviewed-artifact receipt pins every inspected derived PNG; the original specimen summary pins all eight reference/candidate indexed buffer pairs. The analytical cropping script is saved beside the sheets. No production source changed during this review.
