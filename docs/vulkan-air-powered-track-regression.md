# Air Powered Vertical Coaster missing pieces

Reported against build 199 in Everything Park, alongside large track-design ghost movement stalls. These are separate defects.

## Cause and change

The immutable track authoring tool rejected the complete seven-sequence vertical transition because sequence 5 tests the return value of a wooden-support call. Both the ascending transition (124) and descending transition (215) consequently had no rail recipe. This is a missing recipe, not missing installed art or a depth failure. The same source pattern also rejected Reverse Freefall's ascending transition.

The new immutable condition record contains the original support operation and two bounded art spans. The GPU evaluates the existing wooden-support return predicate against the tile's graphical support state and selects the appropriate span. Supported terrain gets a floor parent and column child; the other outcome gets the independent column parent. Both image banks remain resident. The shared predicate is also used by Miniature Railway, without changing its behavior. No CPU per-frame artwork decision, painting, sorting, or image upload is introduced.

Air Powered's booster (100) was separately rejected because it copies the primary track colour into the secondary remap channel while preserving the ghost/highlight override. An explicit palette metadata flag now represents that operation. Arbitrary ghost-dependent graphical branches remain rejected.

## Qualification checklist

- [x] Enumerate every original getter case: Air Powered now has recipes for all 22 defined track types.
- [x] All 82 authoring tests pass, including all seven transition sequences in both directions, four rotations, both support-owned art branches, original image IDs, and booster remapping.
- [x] Preserve all 239,852 previously admitted recipe rows byte-for-byte; add only Air Powered 100/124/215 and Reverse Freefall 124.
- [x] Preserve all 200,088 prior support sidecar rows byte-for-byte; add exactly the same four programs.
- [x] Build 203: all 68 focused native tests pass, including the new Air test and catalog-wide bounds/residency validation.
- [x] Manually inspect matched upstream/native Everything Park closeups in all four rotations, including transition joins and tower bases.
- [ ] Add elevated-terrain raster coverage that visibly distinguishes both conditional floor outcomes; current source tests exercise both branches, while the park captures do not establish both terrain outcomes.
- [ ] Confirm hidden, silent 4K benchmark performance with no concurrent GPU workload.

Evidence is under `obj/vulkan-parity/native-track-recipe-authoring-air201`; four-rotation camera requests are `obj/vulkan-parity/air201-cameras.json`, centered on Everything Park rides 405–407. Source admission and matching old rows do not establish raster parity.

## Build 203 manual visual review

Reviewed the four complete upstream/native pairs in `obj/vulkan-parity/air203-paired-01`, then the four original-resolution transition crops and four magnified tower details in its `manual-review` directory. The formerly missing silver ascending/descending transitions are continuous, join the flat track and tower coherently, and retain the original supports in every rotation. No whole track segment remains absent in these views.

This is restoration of missing artwork, not a pixel-parity claim. The narrow tower samples have 4, 0, 46 and 23 differing pixels in rotations 0–3 respectively; their exact rectangles are retained in `manual-review/tower-samples.json`. Rotation 1's sampled tower rectangle is identical. Rotation 2 contains a small gray triangular rail/support contact patch where upstream leaves grass, shown at nearest-neighbor magnification in `manual-review/r2-contact-detail.png`. Other tiny differences lie around background art seen between braces. Keep that contact-order discrepancy in the backlog; the captures do not justify a blanket offset change.

The wider crops also retain vehicle/guest and sign-lettering differences elsewhere in the scene. They are not waived by this track-coverage fix. Both conditional support art spans are covered by source/catalog tests, but a deliberately elevated/unsupported pair is still needed to establish raster equivalence of both floor outcomes.

Build204 retains these shader/recipe changes and passes the72 selected native checks across the documented resumed run. The final moving-ghost4K/3000-tick capture also retains the restored Air transitions in the overview. Clean performance qualification remains pending; see `vulkan-track-ghost-performance.md`.
