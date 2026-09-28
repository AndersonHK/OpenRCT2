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


## Native114 tunnel comparison

Manually inspected all48 upstream/native/exact-difference pages in `obj/vulkan-parity/underground114-specimens-01`: all128 specimen views, four rotations and zoom0/1, against unchanged `underground-upstream-corpus-01`. This is saved-artifact review only, with no game/build/GPU execution by the reviewer. Both full-frame and specimen comparisons remain **FAIL**; no masks or tolerances were introduced.

Original tunnel frames now appear for flat paths0/1, flat coaster4 and the lower level of stacked paths15. Their differences decrease in every camera. However, path mouths remain visibly closed by grey tunnel backing instead of showing the dark path continuing inside. Coaster approaches also intersect the backing incorrectly. Case15's upper deck remains intact. This improves on112's solid cliffs but does not qualify the openings as correct.

Case15's fixed rectangles isolate the mouth without a boundary seam. Differing indexed pixel counts, in rotation order0,1,2,3:

| Zoom | Native112 | Native114 |
| --- | --- | --- |
| 0 | 617,632,617,632 | 287,298,287,298 |
| 1 | 156,190,156,190 | 75,149,75,149 |

Source inspection supports a parent-order diagnosis, not a portal-type substitution. `worldTunnelImageOffset` uses the upstream path type10 offsets76..79 and standard-flat offsets36..39. `worldEmitTunnelRequest` emits both portal parents consecutively after the below-land path/track region; upstream `Paint.Surface.cpp` supplies distinct lower/top parent bounds for global arrangement with the path/track. Correct art can therefore still cover the approach with its backing. This source-supported diagnosis has not been experimentally corrected during review.

Prior independent failures persist: stations6/7 lose their exposed end section; shallow Cinema8 shows too much body; deeply buried Cinema9 leaks the same71 pixels at rotation1/zoom0 and16 at zoom1; shallow shop10 hides valid body; equal-base path/track12/13 cross raised corners at rotations1/2; elevated path14 lacks supports. Fully buried shop11 stays hidden. Case5 remains exactly equal in all eight fixed rectangles. Slope controls2/3 retain their previous counts, including known boundary and zoom1 edge differences. No new nonportal body/slope regression was found. A small reduction in the wide case6 rotation0 crop comes from a neighboring path mouth, not a repaired station.

Every reviewed page/strip hash, input indexed hash, rectangle and112-to114 count is pinned in `obj/vulkan-parity/underground114-review/reviewed-files.json` (SHA256 `3811f79cd9e7aa8ec41f11abacb0e6199a1af82477ef03c27b7892a344f16169`). Specimen summary SHA256: `ad415f4365765812dba61ee0439547c564b5424b09c1e69d9d971627e4f5209e`. Full-frame differing indexed totals are195159,197607,198536,195319 at zoom0 and51427,51578,51693,51008 at zoom1. These include the separately accepted outer skirt and recorded unrelated differences; they are not isolated tunnel counts. Normal-view evidence does not qualify underground/inside controls.


## Actual inside-view reference corpus

External fixture build11 captured `underground-view-upstream-corpus-01` and the identical-scene `underground-view-control-upstream-corpus-01`. The reference uses upstream `ViewportFlag::undergroundInside`, serialized as raw viewport flags in every case, rather than drawing ordinary tunnel mouths under normal view. Both corpora retain all four rotations and zooms0/1. The scene includes the earlier16 path/track/station/building cases plus shallow and deeply buried Observation Tower station bases and stacked tower sections.

Manual inspection of both full r0z0 reference PNGs confirms meaningful coverage: inside view has dark ground with green tile grid and transparent vertical terrain framework, exposing buried paths, track/station pieces, full Cinema/shop bodies and both tower bases. The matching normal control hides those structures under opaque grass and cliffs while retaining exposed tower/roof portions. This establishes the requested reference behavior, not native correctness.

The native comparison must cover every rotation/zoom in both modes, including grid placement, transparent cliff layering, visibility of buried station/flat-building bodies, tower base/section ordering and normal-view occlusion. The prior normal-view tunnel-mouth corpus cannot substitute for this gate. Support omissions, lettering, aperture/order failures and the separately accepted outside skirt retain their existing scope; no general underground waiver applies.

Reference manifest SHA256: inside `374d2bbb8c25485a1b1362b409d7338b5c5d20c5406f38766b43e98310a61cc3`; normal control `29b86a9225513cc8230f194cebe22a056490a17f775a2be19078d243cbd77dfb`.


## Native117 actual inside view and normal control

Manually reviewed all288 upstream/native/exact-difference specimen strips:18 specimens, four rotations, zooms0/1, in both `underground-view117-specimens-01` and `underground-control117-specimens-01`. The48 contact sheets in `obj/vulkan-parity/underground117-review` preserve every complete strip without masks, recolouring or tolerances. This review used saved artifacts only. Both exact comparisons remain **FAIL**; the capture owner reports clean Vulkan validation.

Inside view now exposes the buried paths, rails/stations, Cinema/shop bodies and tower station bases, with transparent vertical terrain framework. However, the ground between the green grid lines is black instead of the reference dark green in every camera. Independent source diagnosis traced this to the world pass sampling the initial zero background before the recorded opaque viewport clear executes. That correction is pending new evidence; it is not a palette-art exception. White station fence details and shop structure differences remain visible independently of the widespread backing error. Physical path supports are still absent. The accepted exterior cliff skirt does not waive any interior grid, aperture or ordering difference.

The matching normal controls show a substantial correction to114's parent ordering. Ordinary flat path and coaster mouths now have the correct visible approaches at zoom0, including the lower mouth under the intact stacked path. Shallow Cinema8 has the reference terrain intersection in every camera; deeply buried Cinema9 no longer leaks through the cliff. Equal-base path/track12/13 now break at the raised terrain correctly. The completely buried shop11 remains hidden. These are bounded sample observations, not general underground qualification.

Remaining actionable differences:

| Sample | Camera scope | Observation |
| --- | --- | --- |
| Stations6/7 |6: all eight views;7: rotations1/2/3 at both zooms | The exposed station opening is covered by cliff while disconnected white fence fragments remain visible. Rotation0's deep station is hidden by the reference too; its wide zoom1 rectangle contains17 pixels from a neighboring feature. |
| Shallow shop10 | Rotations1/2, both zooms | Grey frame/structural pieces visible in the reference are missing. Fixed rectangle differences are830/816 pixels at zoom0 and210/205 at zoom1; rotations0/3 are exactly equal. This needs a source trace before classifying it as a support or ordering omission. |
| Deep tower17 | All eight views | The square ground aperture around the emerging tower is absent. At rotations1/2 its isolated rectangle differs by757 pixels at zoom0 and187 at zoom1; other cameras also include the exterior skirt. |
| Portal sampling | Zoom1: path0 rotations1/3; path1 rotations0/2; coaster4 all rotations; stacked path15 rotations1/3 | Narrow differences follow the portal art while zoom0 mouths align. Stacked path15 provides a clean isolated control:0 pixels in every zoom0 view and0,78,0,78 pixels at zoom1. Do not treat this as a remaining closed-mouth ordering failure or accept a sampling tolerance. |
| Sloped terrain edge sampling | Zoom1 rotation0, cases12/13 and nearby slope controls | Small edge differences remain independently of corrected object overlap. Case13 is exactly equal in all zoom0 views and zoom1 rotations1/2/3, but differs by173 pixels at zoom1 rotation0. |
| Elevated path14 | All eight views | The approach decks align; their physical supports remain missing. |

Useful exact controls from the original fixed rectangles: deeply buried Cinema9 is0 in all eight views; equal-base track13 is0 in seven; stacked paths15 is0 in six. Cinema8 and several other wide rectangles include a one-pixel exterior boundary seam, so their nonzero totals must not be called building-body failures. No region was excluded from the actual comparisons.

`obj/vulkan-parity/underground117-review/reviewed-files.json` pins all48 reviewed sheets, the inventory of288 original strips, and both full specimen summaries; SHA256 `fe12fde065f5d77845468a82c19253cce6f9673c36a39a8c1146ba62c4572d72`. Inside summary SHA256: `4a38955ebff40076cbd9194f579371d7966952dd4e4732ef01f630b007211175`. Normal summary SHA256: `35b67b8a18d4ae11fbfc6ef9a864869ee6b3f0cf43e02d521a57356e40933e8e`. These results use the fresh build11 upstream scene described above; historical112/114 fixtures and failures remain separate evidence.


## Native118 backing correction and unchanged normal control

Reviewed all288 saved case/view groups again:18 specimens, four rotations and zooms0/1 in both inside and normal modes. The inside review used24 six-specimen reference/native sheets; the normal review used eight18-specimen reference/native overviews. The complete original strips retain the exact-difference panels and their hashes. This is artifact inspection, not a new rendering run. Both exact comparison summaries still report **FAIL**.

The118 inside images restore the reference dark green ground between the green grid lines. The widespread black backing seen in117 is gone in every camera. Transparent cliff framework and buried path, rail/station, Cinema/shop and tower bodies remain visible. No newly missing broad body or shifted grid was found. All144 inside specimen rectangles improve numerically: the sum of their differing indexed pixels falls from5,466,000 to329,317. These fixed rectangles overlap, so those sums are review diagnostics, not unique full-frame pixel counts or a passing tolerance.

All144 normal-control strip PNGs are byte-identical to117, and their exact indexed counts are unchanged. Manual inspection of every reference/native pair found the same corrected Cinema/terrain and raised-corner path/track intersections described above. The118 background change therefore introduces no pixel change in these normal controls. This does not establish correctness of every normal scene.

The remaining117 findings stay open: exposed station ends/fences, shallow-shop structural pieces, the deep tower ground aperture, fine zoom1 portal/terrain sampling and missing elevated-path supports. Inside view also retains fine fence/structural differences after its backing is corrected. No exception, mask or tolerance was added. These corpus images contain world rendering without application chrome, so they do not qualify toolbar/window composition, an actual selected-vehicle window or UI interaction. The forthcoming contextual preview capture remains a separate gate.

Hash inventory: `obj/vulkan-parity/underground118-review/reviewed-files.json` SHA256 `c1a36eefe57154df65738f037c72e1ae0d7a4d571e85b0c47d2480704f0e57c1` pins32 reviewed sheets, all288 original strip hashes through `image-hashes.json`, and both source specimen summaries. Inside summary SHA256 is `4a2352f96759d3d9899a68f78d0e012dbc31d87e42bda5eda5460b198c891ef6`; normal-control summary SHA256 is `d193f1f41c103c65f857e30f2609a6e3bcbb4bbbc7a8cf689c8b07ba1afacdd6`. The upstream references remain the fresh build11 corpora specified above.

### Read-only119 column-dispatch review

Inspected the proposed32-lane `world_parent_columns.comp` dispatch and matching `VulkanWorldSurfacePipeline.cpp` allocation/dispatch barriers before qualification. Per-invocation private state remains independent; each active lane owns one column. Transposed quadrant heads use `HEAD_WORDS + quadrant * columnCount + column`, within the allocated2,002-heads-per-column region. Column metadata and node-prefix ranges remain disjoint. Phases5/7 dispatch `ceil(columnCount/32)` groups; excess lanes return, while single-invocation phases6/8 explicitly admit only global invocation zero. Existing compute write-to-read/write barriers separate the phases, and no workgroup barrier makes these early returns unsafe. No concrete new bounds or inter-lane race defect was found in this source review. It is not evidence of119 runtime correctness or performance.


## Native119 station, shop, aperture and sampling corrections

Manually inspected all288 inside/normal specimen groups using16 complete reference/native overviews, with16 additional enlarged reference/native/exact-difference sheets for stations6/7, shop10 and tower17. All source rectangles and comparison failures remain unchanged. The capture summaries contain no validation diagnostics; this review performed no GPU execution. Visual correctness here is independent of119's unsuccessful performance experiment and does not endorse its32-lane dispatch for deployment.

The previously missing exposed station ends and shallow-shop structural pieces are restored. Stations6/7 and shop10 now have **zero differing indexed pixels in all eight cameras in both modes**. The deeply buried Cinema9 remains exact in normal view and now also matches in every inside-view camera after the zoom1 sampling change. No newly missing major body or reversed terrain intersection was found.

Normal-view zoom1 portal and slope sampling also improve: equal-base track13 and stacked path15 now match their entire fixed rectangles in all eight views. Path/coaster portal cases0/1/4 retain only their wide rectangles' exterior-edge differences; the former isolated zoom1 portal discrepancy disappears. The deep tower17 now emerges through the correct square aperture: normal rotations1/2 match exactly at both zooms, while rotations0/3 retain an exterior triangle below the specimen. The shallow tower16's exposed geometry remains intact.

Inside-mode green grid and cliff framework retain the118 dark backing. The tower's remaining inside differences were localized rather than treated as a new aperture failure. At rotation1/zoom0, they occupy full-frame bounds `[1090,412,1406,614)`; rotation2/zoom0 occupies `[2787,588,3102,790)`. Every differing reference pixel is background palette index10, while candidate pixels are cliff colours. Projecting these pixels onto the outer east edge `x = 63 * 32 = 2016` gives heights17.5..61.5 and17..61.5 respectively. The authored64x64 fixture explicitly sets all perimeter terrain to height64. The other six tower views' residuals project onto the same exterior plane at heights17..65, with raster-edge rounding. This identifies the peripheral strips as the native outer cliff skirt visible through inside mode, not a missing internal tower body or aperture. The already recorded exterior-skirt preference retains its narrow scope; it is not a general underground exception. Exact counts and bounds for every tower view are in the pinned localization artifact.

Remaining interior differences include the deliberately unresolved physical supports of elevated path14 and a few fine green terrain/slope pixels in inside cases3/12/13. The fully buried shop11's wide rotation3 rectangle includes a neighboring feature; its small residual must not be described as the buried shop becoming visible. No rendering mask, threshold or tolerance was introduced, and full-frame/whole-corpus comparisons remain **FAIL**. These world-only captures do not qualify contextual selected-vehicle windows, UI composition, real-time motion or pacing.

Compared with118,87 inside strips improve and57 are byte-identical;43 normal strips improve and101 are byte-identical. No specimen's differing indexed count increases. Sums over the overlapping fixed rectangles fall from329,317 to96,020 inside and32,342 to16,932 normal. These are not unique full-frame counts.

The119 inventory `obj/vulkan-parity/underground119-review/reviewed-files.json` has SHA256 `42fe4fe5902cc0a5d280a855f26913e8ac08eddad113c21083ec209bcff85ad3`. It pins32 reviewed sheets,288 original strip hashes, both source/capture summaries and `tower-localization.json`. Specimen summary SHA256s are `87923c71454048b4466f208f0bb8db56cdd922af0a86783476de36834e9f562c` (inside) and `3858c961e06103f1bc07613d13e77f1db6195c26219e7d05f85379574a89b7b5` (normal).


###119 internal residual diagnosis: repeated palette filters

The remaining internal slope pixels are actionable, distinct from the exterior skirt. Clean equal-base track case13 at rotation0/zoom0 has12 differences in `[2630,1184,2669,1204)`: reference/native palette pairs217/218 (eight pixels),218/219 (two) and109/110 (two). At rotation3/zoom0 its26-pixel residual follows the corresponding slope edge. Case3's buried path also has fine interior differences along the deck where terrain filters overlap; this is not missing path geometry.

For all recorded non-background-reference differences in cases3/12/13 at rotations0/3 and zooms0/1, applying the actual `palette_darken_1.png` mapping once to the native index produces the reference index. The saved evidence includes exact coordinates, pairs, unmodified reference/native/difference crops and the palette hash: `obj/vulkan-parity/underground119-review/internal-slopes/filter-composition-evidence.json`, SHA256 `5e7d5795fef8f8ece3ed3a247277b09f91c093fa07ed79a7f3a2969c24d2457b`. The background-reference exclusion is only a diagnostic localization aid; full exact comparisons and all failures remain intact.

Source explains the lost composition: upstream emits a transparent Darken1 parent for each terrain surface and applies it to the current destination in arranged order. The native world pipeline draws opaque records, copies one background, then every filter samples that same immutable copy. Two overlapping Darken1 surfaces therefore apply one effective darkening instead of two. Shifting terrain geometry or accepting a sampling tolerance would conceal the actual compositor defect.

A Darken1 coverage count can reproduce repeated identical filters above the final opaque depth; this palette reaches a fixed point after at most11 applications. However, the deferred native phase also contains water-row filters, literal water overlays, Dodgems Darken3, coloured glass and blended selected-vehicle parts. Those operations do not generally commute. A count-only resolve placed indiscriminately before or after water/glass would leave new ordering errors. The existing Vulkan transparency peeling/composition implementation offers reusable ordered resolve logic, but needs a GPU-world-indirect input and an explicit bounded-layer overflow guard instead of its CPU rectangle input and host layer count. The next correction must test mixed filter order, intervening opaque/grid pixels, water overlays, viewport clipping and later UI composition. No source correction or new runtime qualification is claimed by this diagnosis.

## Native125 repeated-filter correction

Manually reviewed all288 inside/normal groups again:18 specimens, four rotations and zooms0/1, using16 complete reference/native overview sheets and eight full reference/native/difference detail sheets for cases3/12/13. Both capture summaries completed with no validation diagnostics. This review used saved artifacts only. Whole-frame and whole-corpus exact comparisons remain **FAIL**; no tolerance or acceptance mask was introduced.

The repeated-filter interior defect diagnosed in119 is corrected. Across all24 case3/12/13 rectangles, all349 former differing pixels whose reference index was not background10 now match exactly. Case13's entire fixed rectangle is exact in every camera. Case3's rotation3 rectangles also become exact at both zooms. The remaining wide-rectangle differences in cases3/12 are exterior skirt pixels, visible in the reviewed difference panels. The non-background subset is solely a diagnostic localization of the prior defect, not a replacement comparison or acceptance rule. Independent full-frame indexed comparison finds no pixel that matched the reference in119 and becomes incorrect in125, in any of the eight inside views.

Dark green backing, green terrain grid, buried paths and rails, corrected station ends/shop structures, and tower apertures remain intact. No new broad geometry, palette or sampling defect was found in these specimens. Elevated-path14 still lacks physical supports. Tower16/17 skirt colours change where repeated filters overlap: those changed residuals have reference background index10, and the previously localized exterior skirt remains present. Four tower strips therefore change bytes without changing their mismatch counts. This is not a new tower-body or aperture failure, and the user's exterior-skirt preference remains narrowly scoped.

Inside overlapping-rectangle differences decrease from96,020 to95,243:16 rectangles improve, none worsen,124 strip PNGs remain byte-identical, and four others change residual skirt colours with unchanged counts. Every one of the144 normal-control strip PNGs is byte-identical to119; their sum remains16,932. These sums count overlapping rectangles and are not unique full-frame counts. The corpus does not specifically exercise mixed water/glass filter ordering, selected-vehicle transparency, application chrome or later UI composition; it cannot close those separate gates.

Evidence is under `obj/vulkan-parity/underground125-review`: `internal-filter125-evidence.json` pins all24 focused raw-index comparisons (SHA256 `db1dc081b466033c92ba67c5d9e19596542286d2b7c2d25032c3b82e01d8c31e`), while `changed-residuals.json` records the tower residual classification. The inventory `reviewed-files.json` (SHA256 `879591978ef6f9943a16538a504b49941c0dcfe33a40636014114bcc5d3fa342`) pins the24 reviewed sheets, original specimen strips, source summaries and numerical diagnostics. Inside specimen-summary SHA256 is `66aa7dab6c7a707436e2b323a474a5bf7b0d4bf15cca376f1dd479e96903487b`; normal-control summary SHA256 is `ae220a1e7ac4f235103e9d60da4d70133c46eb68628596efdcf32ec7c1568e09`. References remain the original build11 inside/control corpora.
