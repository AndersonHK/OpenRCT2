# Native object original-art review - build 103

All eight reference-left comparison sheets were manually inspected: rotations 0-3, zooms 0 and 1. Native-resolution banner crops were also inspected. The full-frame comparison remains **failed**. This is a diagnosis of individual specimens, not a parity pass or acceptance of new differences.

The reference is fresh pristine-upstream software using real RCT2 objects, not a model of their painter. The candidate uses build103's native GPU path. Both contain the saved scene plus nine identity-checked ghost flag patches, with simulation/source tick zero. The enlarged 1664x1024 framing includes the complete specimen set.

## Findings

- **Banner pole/ground occlusion differs beyond the known text omission.** Ghost banner (20,19) differs by 108 pixels at rotation 0/zoom 0 and 127 at rotation 1/zoom 0: native exposes a long lower pole that the original hides. Normal banner (8,19), rotation 1/zoom 0, has the opposite problem: native hides a pole visible upstream (46 differing pixels). Ghost banner discrepancies cannot be attributed to omitted lettering. Native-resolution crops show these differences directly. The original's default "Sign" lettering is separately absent from native banners as already declared; counts below retain both differences without masking.
- **Same-tile tree/track ordering differs.** Native tree trunks overwrite rails where the original draws rails over the trunks, for normal tree (13,24) and ghost tree (20,24). Agent three independently inspected all eight track detail crops and obtained matching counts. Chain-only regions are exact in all eight views, as is the separate tree at (16,23). Selection, projection and colours visually align for these narrow flat/chain specimens, without qualifying arbitrary track types or intersections.
- **All nine addition rectangles match exactly in all eight views: 72 zero-difference checks.** They cover lamps, bins and benches in healthy, status-zero and broken states. Status-zero represents bin fullness; lamps and benches have no separate full state. Broken lamp globes, damaged bins and broken bench slats visibly match their original art. No displaced or missing addition was observed.
- **All four walls, both two-tile large-scenery signs, and isolated normal/ghost trees match exactly in their measured rectangles.** Pale ghost appearance also matches. The large object is the sign-sized `rct2.scenery_large.ssig1`; this does not establish arbitrary buildings, animated scenery, doors, glass or scrolling signs.

The addition specimens visibly stand on grass in both outputs. The earlier upstream path corpus has the same result for isolated four-sided additions. Both fixtures create a real path at height 64 on a height-64 grass surface, zero connected edges, ash surface slot 0 and railing slot 0. Original `PaintPath` still emits the surface parent. No missing-asset or fixture field difference was found. The precise original arrangement cause requires a separate paint trace; this review does not extrapolate to ordinary connected paths.

## Local indexed measurements

Rectangles are projected from manifest tile centres using fixed unzoomed bounds: trees x +/-40, y -100..20; large scenery x +/-50, y -48..28; walls/banners x +/-40, y -48..24; additions x +/-40, y -60..24. Bounds divide by zoom scale. **Every indexed difference inside the rectangles is counted without colour masks, tolerance or excluded pixels.** Large-object rectangles overlap and are not summed. The JSON records exact crop and difference bounds. These local measurements do not replace the failed full-frame comparison.

| View | Banner pixels at x=8 / 12 / 16 / 20, y=19 | Tree/rail pixels at (13,24) / (20,24) |
|---|---:|---:|
| objects-r0-z0 | 165 / 1 / 0 / 108 | 111 / 105 |
| objects-r1-z0 | 46 / 0 / 146 / 127 | 107 / 93 |
| objects-r2-z0 | 31 / 146 / 165 / 46 | 111 / 105 |
| objects-r3-z0 | 146 / 165 / 1 / 31 | 107 / 93 |
| objects-r0-z1 | 38 / 1 / 0 / 24 | 28 / 27 |
| objects-r1-z1 | 17 / 0 / 24 / 38 | 26 / 22 |
| objects-r2-z1 | 9 / 24 / 38 / 17 | 28 / 27 |
| objects-r3-z1 | 24 / 38 / 1 / 9 | 26 / 23 |

Background colour differs outside the park. The native outer cliff skirt is the previously user-authorized difference. Both remain in global comparison counts. Banner pole discrepancies and tree/rail ordering are additional defects, with no waiver.

## Evidence and limits

Evidence is under `obj/vulkan-parity/object103-art-review`: all eight `*-reference-left.png` sheets, `banners-r0-native-crops.png`, `banners-r1-native-crops.png`, eight `*-tracks-detail.png` crops, `specimen-regions.json` and `track-regions.json`. Banner crops use nearest-neighbour enlargement only. Original indexed and image files were not modified.

- Corpus manifest SHA-256: `551018eca17e64a8cc1839b149e41e7c3aa7b519ac2e0d273bba0d7acaa1c4fe`.
- Full-frame comparison summary SHA-256: `78706af2cae6320b4e9d9594f8ad59f098b442a5a74f7d1ca9fc199770e820f4`.
- Specimen measurements SHA-256: `fdd2cfdaac2f669ebc39a0542e4f2e39c65702445c752bac2bae36303eab8874`.
- `review-input-sha256.json` SHA-256: `d2222ec56ec39a801ec5a4b4a3e9a08ccdd4f21c66617b2251dba630b8872bb5`. This pins reference/candidate indexed and PNG files, all eight comparison sheets, build receipts, region reports and banner crops.

Saved-artifact review only: no builds, application execution, GPU work or new capture. The nine CPU usage/catalog tests in object103-art passed; that does not change the failed raster result. Broader object families, animation phases, additional zooms, arbitrary intersections, interactive edits and full-park performance remain separate gates.

## Build 106 real-park checkpoint

Manually inspected the saved 3840x2160 real-park screenshot from `performance-objects106-12000-01` after the owner's successful 12,000-tick run. Also inspected two unscaled crops covering the large scenery display and the dense wooded right edge, saved as `obj/vulkan-parity/object106-park-review/scenery-native.png` and `forest-native.png`. This was saved-image review only while the owner used the deployed build.

The scene is populated: the upper display contains dense rows of walls, trees, small objects and recognizable large scenery (including castle towers and a pyramid). The wooded right boundary contains varied original tree colours and shapes. Scenery is anchored to the visible terrain, while the path network, water pools, hills and the accepted outer cliff skirt remain visible together. I saw no large empty rectangular chunk, obvious unrelated sprite substitution or atlas-corruption block in these inspected regions. The UI and whole park composition remain readable.

This is a useful composition checkpoint, **not original full-render parity**. There is no matched upstream image for this exact final camera/tick in this review. Dense overlap, animation, hidden/unsupported components and the correctness of every displayed asset cannot be established from one overview. The earlier banner pole and same-tile tree/rail ordering failures are not cleared by this image; neither are omitted entity/ride components or scrolling text. Thin track sections remain visible outside the terrain footprint, but this single image does not establish whether those are authored out-of-bounds geometry or an additional rendering error. Physical support completeness and arbitrary track geometry remain outside this scenery-focused review.

- Reviewed PNG SHA-256: `00d653a863ca3c418e6cf4711d07b2873f83e0faa73c21af4854a5b1ea4fdd4e`.
- Performance run summary SHA-256: `f33eb5f8b5c32f83a9c0b08ce74667e665d31b3e030f7f9513a929c8c9a324a7`.
- Native scenery crop SHA-256: `5c50297a6be07e14c87f4c8f7df47ad4e1c05ed486684894a6cd5a4b7f70a41a`.
- Native forest crop SHA-256: `781eb7c49b920f10a61953790bc49c7c0ee1148fb30a8fe09fff878d6d8774ec`.

## Build106 boundary-coaster diagnostic

The CPU-only pristine-upstream capture in `obj/vulkan-parity/object106-upstream-r3-01/reference.png` (SHA256 `fa08c56de93d16cf84abd8e685c6197f7f4a51a9974f90cd9855659fcdbbea62`) confirms both long rails outside the left map corner are authored tall-coaster spans. Root and the track agent inspected the capture. Missing connecting curves and supports make these spans look disconnected in build106; the lines themselves are not atlas corruption. The reference uses an approximate matching camera and the initial park state, so this diagnostic is not a pixel-parity result.

