# Underground rendering audit

This audit separates three mechanisms: terrain occlusion in normal view, actual tunnel mouths cut into terrain edges, and the explicit underground/inside viewport mode. Builds110/111 do not implement all three. No missing-tunnel explanation should be used to waive unrelated building/terrain overlap.

## Source evidence

`data/shaders/vulkan/world_surface_compact.comp` assigns each raw path/object to one of three regions using only `baseZ < surface.baseZ` and water height. It emits the below-base region, then the entire terrain top and cliff strips, then remaining objects/water. The shader's global output order follows camera-relative tile diagonals. This hides many ordinary buried sprites, but is not the original global parent bounding-box arrangement.

The surface's element ordinal is absent from `SourceRecord`. Equal-base objects therefore always enter the after-land region; the original path painter uses whether traversal has actually passed the surface, which also changes path bounds/deck behavior. Sloped corner heights and a tall sprite crossing an adjacent tile are not represented by that single height comparison. These are concrete differences in implementation; dedicated images must establish which cases produce incorrect pixels before changing the predicate. Replacing the threshold with the highest corner or drawing all objects last would be unjustified.

Native cliff strips are uninterrupted. In contrast, `Paint.Surface.cpp` around561–621 consumes left/right tunnel requests, emits land only up to each opening, selects a tunnel type and low-clearance alternative, emits the lower/upper portal parents, skips the opening height, then resumes cliff strips. `Paint.Path.cpp::PathPaintFencesAdditionsTunnels` derives path requests from rotated edges and slope, including a16-unit height adjustment on descending entrances. Native path/track recipes currently do not deliver corresponding requests to the terrain edge emitter. Adding only portal art above a still-solid cliff would not reproduce an opening.

The native world push constants carry camera, tick and water settings but no underground/inside, hide-base or hide-vertical flags. Original `Paint.Surface.cpp` uses those flags for different edge art, darkened/translucent land and water treatment. Consequently normal-view occlusion and the user's explicit underground-view controls require separate tests; the new fixture below initially covers normal view only.

## Saved real-park images

Manually reviewed the full110/111 final benchmark images, the saved pristine-upstream static EverythingPark reference `object106-upstream-r3-01/reference.png`, and four side-by-side unscaled crops of the central hill, front raised terrace, lower hill and upper slope row. Inputs and crop rectangles/SHA256s are pinned in `obj/vulkan-parity/underground-source-review/manifest.json`. The upstream receipt explicitly says its camera is approximate and it has no final benchmark simulation advancement; this is a visual diagnostic, not an exact comparison.

The central and lower hills hide substantial below-ground content in all three images; there is no evidence here that every underground object is drawn above ground. The front raised terrace has approaches meeting solid native cliff faces, while the upstream has additional entry/track detail. Normal supports are also absent from native, so these wide crops alone cannot attribute each differing pixel to a tunnel. The newly visible bodies remain on their expected footprint rows. The prominent difference in the upper slope row is omitted support structures, not evidence for an underground clipping fix. The dedicated fixture is needed to isolate these cases without supports/vehicles dominating the images.

## Dedicated external fixture

`test/object-parity/ObjectFixtureMain.cpp --underground` adds16 separated scenes on a64×64 park, using only pristine-upstream engine APIs and loaded original-art objects. It renders3840×2160, all four rotations at zoom0/1. It uses normal initialized rides via `RideAllocateAtIndex`, no vehicles, fixed weather/clock, no simulation advancement and normal viewport flags.

Cases cover flat paths crossing a terrace in both axes; a matching path/terrain slope and path below sloped land; flat/up25 coaster approaches; partly and deeply buried stations; partly and fully buried Cinema/shop; equal-base flat paths/tracks on raised corners; a path at terrace-top height; and stacked buried/exposed paths. The manifest records authored placements and the ordered type/baseZ/clearanceZ/slope facts of every surrounding tile. That makes an observed overlap attributable to actual source geometry and order. This authoring intentionally permits buried geometry, as required to exercise rendering; it is not a construction-validity or simulation test. Existing fixture modes and immutable old receipts remain unchanged.

Root built the external driver and captured `obj/vulkan-parity/underground-upstream-corpus-01`. No build, game or GPU was run by this reviewer. No production rendering behavior changed during this audit.

## Upstream fixture qualification

Manually inspected all eight full upstream PNGs (four rotations, zoom0/1), covering all16 authored cases. All scenes are populated and separated; there is no loading image, empty capture or viewport clipping of a specimen. Additional close inspection of rotation0/zoom0 cases7,9,12 and15 confirmed the deeply buried station, fully buried Cinema, equal-base path and stacked paths. The remaining prepared crops are review aids, not a claim of128 additional individual inspections.

The references establish useful, distinct expectations:

- Flat path/coaster approaches have actual tunnel mouths interrupting the terrace face. A portal painted over an intact cliff would differ from this reference.
- The shallow Cinema/shop cases legitimately show protruding roof/body art. Their deeply buried counterparts disappear beneath the larger plateau.
- Case7, named `station-fully-buried`, still exposes a station-end/tunnel-like fragment at the terrace edge. Its label describes the base height, not an assertion that every station pixel vanishes. Cases6/7 deliberately put a station end at that edge.
- Equal-base flat paths and tracks over raised slope corners are intermittently hidden by the land. These scenes distinguish the current native after-land classification from correct sloped occlusion without needing a moving vehicle.
- The stacked path case visibly contains the exposed upper deck and the lower tunnel approach. The isolated up25 case has an entry notch; it is not a complete operational ride.

Prepared128 upstream crops using projected bounds of the authored surrounding tile facts, including the large fully buried plateaus. The crop rectangles, individual PNG SHA256s and all eight reviewed full-image hashes are recorded in `obj/vulkan-parity/underground-review/upstream-review-manifest.json` (SHA256 `3aa47106318b67419c6f33f4098a549606ed432d30fc22e71eba35167b840825`). The immutable input manifest SHA256 is `b99400869ad5427569ef3471705cfeb7dbd4f82f026691b66f83a98f1d66749f`; capture receipt SHA256 is `c96e2209b5d074e4f64d98192f3155588385d1d6d62da67cc5e9178c784c4f4c`. Some wide crop margins include part of an adjacent specimen; the raw case identity and rectangle remain explicit.

This qualifies the upstream normal-view fixture for the upcoming native112 comparison. Native pixels have not yet been reviewed here, and no rendering gap is accepted or declared fixed by this upstream-only inspection.

## Native112 comparison

Subsequently reviewed every one of the48 upstream/native/exact-diff pages in `obj/vulkan-parity/underground112-specimens-01`: all16 cases at all four rotations and both zooms,128 comparisons. Root captured the native images in `underground112-art-01`; the native shader generation is unchanged from110. The exact comparison remains **FAIL**, with no tolerance or exception introduced by this review.

All reviewed page hashes, individual strip hashes, input image/indexed hashes and original rectangle counts are pinned in `obj/vulkan-parity/underground112-review/reviewed-files.json` (SHA256 `4f4c0c21ed9ef2a8237082ebd332dafbb35994c17b2abc50215c4502fb16ec50`). Its source specimen summary SHA256 is `1ccd13b0b875f82fa7308d92bc381160fec65e1cfbf93215605402b656a0acc8`. These rectangles sometimes include map-boundary seams or neighboring specimens. Counts therefore must not be presented as isolated counts for the named building: for example, case11 rotation3 includes missing supports belonging to another scene at the crop's top edge.

| Cases | Observed native result | Narrow interpretation |
| --- | --- | --- |
| 0/1 flat paths,4 flat coaster,15 stacked paths | Cliff faces remain solid where upstream has a tunnel mouth, in every view. Case15's upper path is present. | Missing physical aperture/portal work; preserve upper-path rendering. |
| 6/7 shallow/deep stations | Native terrain hides the exposed station-end section that upstream retains at the cliff face, at both terrain heights and every view. | Include station tunnel operations and actual cliff cuts in the tunnel work. Making the entire station visible would be incorrect. |
| 2 matching path slope,3 path beneath sloped ground,5 isolated up25 | No visible native-only exposed path/track or missing broad body. Case5's rectangles are exactly equal in all eight views. Some other rectangles contain boundary seams and existing zoom1 terrain-edge differences. | Do not change all sloped/buried classification because of the failing cases below. These are useful controls. |
| 8 shallow Cinema | Native draws too much dome/body over terrain in all views. The upstream terrain intersects the dome and, depending on rotation, a lower building section. | Cross-tile body/terrain ordering, independent of tunnel mouths. |
| 9 deeply buried Cinema | A native-only narrow yellow/blue vertical fragment leaks through the cliff at rotation1, both zooms. Other views hide the body. At zoom0 the isolated difference is71 pixels within `[2240,1254,2242,1294)`. Zoom1 has16 differing pixels. | A genuine buried-body ordering leak, not a permissible protrusion or missing portal. Trace the responsible body parent and foreground terrain tile. |
| 10 shallow shop | Native hides more of the valid roof/body than upstream in every view, leaving a smaller cap. | Opposite failure to case8; a blanket hide/draw-last rule cannot fix both. |
| 11 deeply buried shop | Its body remains hidden in all views. | Preserve this behavior; neighboring support differences in wide crops are separate. |
| 12 equal-base path,13 equal-base track on raised corners | At rotations1/2, native draws a continuous deck/rail through corners where upstream hides segments. Rotations0/3 agree in the main object overlap; zoom1 rotation0 also contains existing terrain-edge sampling differences. | Parent/terrain ordering sensitive to camera and slope, not missing tunnels. |
| 14 terrace-top path | Decks align; native lacks the tall supports of exposed approaches. | Existing physical support gap, separate from underground clipping. |

The manifest narrows the ordinal hypothesis: cases12/13 have the slope12 surface at ordinal0 and the equal-base path/track at ordinal1, both baseZ64. The native after-surface decision already agrees with that source traversal. Adding the surface ordinal alone cannot correct their observed rotation1/2 differences. Likewise shallow Cinema/shop have baseZ64 objects before their higher surface in the raw list, matching the current height classification. Their opposite overlap failures require the actual parent geometry and cross-tile relationship, not a different scalar height cutoff.

The smallest justified implementation split is therefore: (1) retain/derive path and track tunnel operations, then split cliff emission around correctly typed openings, using cases0/1/4/6/7/15 as strict targets; (2) separately trace parent bounds and terrain ownership for Cinema8/9, shop10 and slope cases12/13 before changing GPU order. A bounded diagnostic that identifies the exact emitted parent and foreground terrain parent at the leaking pixel is useful; a CPU per-instance graphical sorting fallback is not needed or proposed. Cases2/3/5/11 and the unaffected views remain regression controls. No source-ordinal-only fix, highest-corner threshold, whole-building hide rule or family draw-last exception is supported by these images.

## Concrete next steps

1. Compare the16 normal-view cases exactly in all eight views, preserving all failures. Separate a fully buried body's visibility from a portal/cutout error and from a valid roof protruding through shallow terrain. Check equal-base raw order and matching slope cases independently.
2. For proven equal-base traversal defects, publish the raw surface ordinal and use it for the passed-surface decision. This is a narrow raw-state correction, not CPU instance image selection; it does not by itself fix general cross-tile sorting.
3. Implement path tunnel request rules on GPU from edges/slope. Extend the immutable offline track definitions with authored tunnel edge/height/type operations where needed. Consume those requests during cliff emission so land strips are split around openings and correct upper/lower art is emitted. Respect low-clearance alternatives and multiple vertically stacked requests; do not silently truncate a fixed small list. Reuse batched resident art and existing count/prefix/output bounds.
4. If the isolated buried-building case still fails after these narrow changes, use actual parent bounds in the affected terrain/object ordering path. Do not hide whole buildings by baseZ or add a blanket draw-last family override.
5. Add a separate upstream/native viewport-flag lane for underground/inside, hide-base and hide-vertical. Normal-view parity is not evidence those controls work.

This is a correctness audit and test plan. It does not claim the native renderer already supports tunnels, approve an exception, or change the unrelated simulation-spike investigation.
