# Flat mechanism animation visual review

Build114's three separately authored pose corpora remain **not qualified for pixel parity**. All 288 animated mechanism comparisons were manually inspected: 12 families, three poses, four camera rotations and zooms 0/1. The 24 body-focused sheets in `obj/vulkan-parity/animated114-review` preserve original upstream/native/diff pixels. Their input rectangles are recorded; full unmasked comparison counts were not changed. This review does not claim inspection of every nonanimated neighbour or a running simulation sequence.

At zoom0, the three captured poses select visually matching original mechanism art and displacement: Haunted House overlay, Spiral Slide progress, carousel, Ferris wheel, four independent Space Rings, Twist, raised Enterprise wheel, both swinging ships, Magic Carpet, Top Spin seat/arms/restraints and Motion Simulator cabin/restraints. Phase1 and phase2 show the expected large changes in Enterprise tilt, ship angle, Carpet height and Top Spin pose on both sides. No distinct wrong-frame/body-colour discrepancy was found in these selected poses. That is narrower than proving every possible animation frame or rider state.

Unresolved differences are concrete:

- Zoom1 has body-wide source sampling differences in Haunted House, carousel, Ferris wheel, Space Rings, Twist, Enterprise, both swinging ships, Magic Carpet and Top Spin, across all three poses and rotations. Spiral Slide is much closer; Motion Simulator also has several matching body views.
- Enterprise pose0, rotation3, zoom0 draws the adjacent entrance over its wheel where upstream puts the wheel in front. Ship/Inverter lower hull/platform overlap pixels differ in several views. These remain ordering failures; they are not excused by correct frame selection.
- Entrance lettering remains missing. Neighbouring Dodgems portal/roof ordering and other known static differences contaminate some wide specimen counts. No whole rectangle was excluded or masked.

## Identified zoom cause and correction awaiting qualification

The upstream mechanism helpers set `InteractionType::entity` while adding their body and associated support children. `PaintDrawStruct` floors those projected coordinates to multiples of two at zoom1 (four at higher zoom) before sprite resolution. The native mechanism emitter omitted that existing entity snap. Odd body heights such as 3 and 7 therefore selected the wrong source row phase; Spiral Slide does not use this entity interaction, and Motion Simulator's commonly even height explains the differing symptom.

The scoped correction marks only operating mechanism parts with the existing entity-snap validity bit. Floors/fences and Spiral Slide retain their original unsnapped behavior. It includes built-in Ferris/Ship/Inverter/Carpet/Motion support parts that are inside the original helper's entity-interaction scope. A row-pattern device regression distinguishes source row0 from row1 at the same zoom1 destination, restores the held inactive pose, and checks no new atlas upload. CPU cases distinguish mechanism parts from floor/fence parts and inactive/missing poses.

Build116 caught an invalid read-modify-write of a write-only output SSBO in the first correction. That failed receipt remains evidence. The revised helper passes the extra validity flag into the local output record before its single write, preserving write-only storage. CPU-only `glslc -O --target-env=vulkan1.1` passed after correction. Build117, actual device regression and fresh original-art comparisons are pending; no corrected-image claim is made here.

## Evidence

The review inventory `obj/vulkan-parity/animated114-review/reviewed-artifacts.json` pins all288 input strips, 24 inspected body sheets and three original specimen summaries. SHA256: `743b49f5c28b8ecb1deee7adc9ef7d76f34790777a07c047b1741771b2bfb0f5`.

The inputs are `animated-buildings-pose{0,1,2}-upstream-corpus-01`, `animated114-pose{0,1,2}-art-01`, and their `compare-01`/`specimens-01` directories. The oracle renders pristine upstream painters with deterministic saved raw poses. Build114 images precede the later native import-fact preflight and must be superseded by corrected captures for qualification. These static pose samples do not qualify pause/resume, real-park animation performance, general vehicle/rider coverage, or startup atlas burst handling.


## Corrected117 original-art review

Manually inspected all288 corrected animated-family comparisons in `animated117-pose{0,1,2}-specimens-01`, using24 sheets in `obj/vulkan-parity/animated117-review`: the same12 families, three authored poses, four rotations and zooms0/1 as114. The saved full upstream/native comparisons and their failing results are unchanged. Review used no game, GPU or build execution.

The mechanism sampling correction is visibly effective. Every one of the144 zoom1 body crops has exactly matching RGB pixels, without a tolerance or mask. The original larger indexed rectangles are exact in120 of144 cases. The other24 have40 differing indexed pixels each at the far map boundary above the mechanism: in each pose, items3/4/10/15 at rotation0, items3/4 at rotation1 and items10/15 at rotation3. These counts retain the original full rectangle; the body crop is explanatory evidence, not a replacement parity gate.

At zoom0, all three selected mechanism poses, colours and displacements also align visually. Enterprise's former entrance-over-wheel overlap and the ship/inverter lower-part ordering now align with the reference. The remaining differences visible in these body-focused sheets are missing entrance lettering, including partially occluded lettering behind mechanisms. No residual wrong-frame, wrong-colour, wrong-pose or body-wide sampling defect was found in the reviewed12 families. This supersedes the114 body/overlap findings for these samples; it does not accept absent text, qualify every frame or rider state, or qualify all other families that happen to appear at a crop edge.

Concrete examples are `animated117-pose0-specimens-01/objects-r3-z0-item11.png` (the formerly wrong Enterprise/entrance overlap) and all `objects-r*-z1-item{03,04,07,08,09,10,11,12,13,14,15,16}.png` in the three pose directories (corrected zoom1 bodies). Full-frame and whole-corpus exact comparison remains **FAIL**, because missing glyphs, exterior differences and other nonmechanism scene content remain included. These deterministic snapshots do not themselves prove a live simulation sequence, pause/resume behavior, general vehicle/rider coverage or real-park performance.

The inventory `obj/vulkan-parity/animated117-review/reviewed-artifacts.json` pins all288 source strips, their unchanged review rectangles,24 inspected sheets and the three original summaries. SHA256: `32411f4013661386b6d96cdfb97885d2d3257286e69cdfc215abcde771865545`. Summary hashes in pose order0/1/2: `87f8c829287f6b7390ae85c1069d1ac04ced29f77eba1e14165b81e93d09a3c8`, `bf704104ce20157e8b86da9960c7fbf69abb92b93387f64152d6f0843eff491e`, `d9f1b06419c5541fcf69e2e7b5efe6eea632bb47ee7145351fa891565dad83b7`.


## Native119 comparison to reviewed117

The119 capture preserves every117 saved pixel: all648 complete specimen strip PNGs (27 specimens, eight cameras, three poses) are byte-identical, including all288 animated mechanism comparisons manually reviewed above. The24 full native PNGs and24 full indexed outputs also match117 byte for byte. There are therefore no changed mechanism groups to re-inspect; this section reuses the explicitly scoped117 manual review rather than claiming a second independent review of identical images.

The mechanism geometry, entity-snap sampling and overlap observations from117 remain valid for these deterministic119 poses. Their lettering/exterior and other whole-scene failures remain failures. No live animation, arbitrary rider state, UI window, performance or successful-display-rate claim follows from this byte equivalence.

`obj/vulkan-parity/animated119-review/reviewed-artifacts.json` pins all648 strip hashes,48 full-output hashes and the prior117 manual-review inventory; SHA256 `0eb989186c6e7f9d15f08b0b00da15db8461118d6993275567a25ada43794e69`.
