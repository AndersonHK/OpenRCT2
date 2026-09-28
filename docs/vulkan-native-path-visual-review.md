# Native path visual review

## Reference inspection — 2026-09-23

The first section reviews the upstream references. The build96 and build97 comparisons remain exact whole-image failures; no native GPU world parity pass is claimed.

I opened all eight 960×640 PNGs in [path-upstream-corpus-01](../obj/vulkan-parity/path-upstream-corpus-01): rotations 0–3 at zoom 0 and 1. The [manifest](../obj/vulkan-parity/path-upstream-corpus-01/manifest.json) records 46 paths on a static 32×32 map: 16 flat edge masks, one filled-corner surface, four terrain-matched slopes, 16 queue masks, and three states each for lamps, bins and benches. The fixture advances no simulation ticks. Smoothing and light effects are disabled.

The independent [upstream builder receipt](../obj/vulkan-parity/upstream-path-fixture-build-02/receipt.json) identifies pristine upstream commit `b80a4a84e92be8e07904b38d1032d0bb88280bb4`. Its standalone driver invokes that core's software `ViewportRender`; the reference does not use the new GPU path rules.

| Selected role | Object identifier | Slot |
| --- | --- | ---: |
| Normal surface | `rct2.footpath_surface.ash` | 0 |
| Queue surface | `rct2.footpath_surface.queue_yellow` | 1 |
| Railings | `rct2.footpath_railings.bamboo_black` | 0 |
| Lamp | `rct2.footpath_item.lamp2` | 2 |
| Bin | `rct2.footpath_item.litter1` | 4 |
| Bench | `rct2.footpath_item.bench1` | 0 |

The dark ash surface shapes are distinguishable from the grass. Their endpoint, corner and full-tile silhouettes rotate consistently. The four slopes visibly change elevation with exposed soil sides and direction-dependent railings. Yellow queue flooring and white queue railings remain identifiable at both zooms. Some deliberately supplied queue junction masks show railings with no queue floor in the upstream image; these are raw representation cases, not proof that every mask is an ordinarily constructible queue.

The additions are visibly original sprites: intact multi-globe lamps versus broken posts, bins in three supplied states, and intact benches versus broken fragments. The lamp and bench “full” cases intentionally repeat their normal appearance because bin fullness is not a visual state of those types. At zoom 1 the additions remain present at reduced scale. These observations establish useful reference content, not correctness of any candidate image.

**Coverage limitation:** `flat-0` at tile `(7,9)` is clipped at the right edge of `paths-r1-z0.png` and left edge of `paths-r3-z0.png`. It is visible in the other views, and all 46 placements are covered by the camera set. Preserve this corpus; do not claim every placement is fully visible in every individual view. The zoom-1 images also expose the map boundary, black skirt and outer background, so whole-image differences may include terrain/border behavior unrelated to path image selection.

This first corpus does not qualify elevated physical supports, queue banner text, tunnels, legacy path objects, fountain animation entities, remaps/ghosts, dense overlaps or arbitrary reordered tile stacks. Four separated slopes and isolated additions cannot establish general cross-family occlusion. Candidate comparison should preserve strict indexed differences and use the captured upstream palette for both sides; a visual match alone is insufficient.

## Build96 candidate inspection

I manually opened all eight shared-palette reference/candidate sheets and all eight binary difference images in [path96-art-review](../obj/vulkan-parity/path96-art-review). Its [summary](../obj/vulkan-parity/path96-art-review/summary.json) pins both indexed inputs for each view. The same upstream BGRA palette expands both images, so these differences cannot be dismissed as different PNG palettes.

Whole-image mismatches remain **26,287 / 26,291 / 26,287 / 26,291 pixels** for zoom-0 rotations 0–3, and **387,617 pixels per rotation** at zoom 1. Large visible differences are the candidate's black outside background and exposed textured terrain skirt versus the upstream blank-tile background/skirt. There are also real interior differences: narrow vertical lamp segments, small bin/bench overlaps, sloped path edges and some queue corners. At zoom 1 the differences extend to several flat path silhouettes and many queue/addition pixels. These are not solely border failures.

An artifact-only byte analysis, without rerendering or changing the comparator, counted differences in diagnostic rectangles derived from the manifest's world positions. Project each tile centre `(32*x+16,32*y+16,64)` through its camera. At zoom 0 use `[centreX-34, centreX+34) × [centreY-72, centreY+32)`; divide offsets by two at zoom 1 and clip to the image. Deduplicate overlapping rectangles. Their union contains **367 / 371 / 367 / 371** mismatches at zoom 0 and **3,137** at zoom 1 in every rotation. These deliberately generous rectangles include neighboring terrain/components; they locate the problem and are not a per-family attribution or acceptance mask.

The tighter ground rectangles `[centreX-32,centreX+32) × [centreY-16,centreY+16)`, similarly zoom-scaled, have **zero mismatches across the 17 flat samples in each zoom-0 view**. Their zoom-1 counterparts have **455 mismatches per view**. Queue ground rectangles have **13 mismatches per view at zoom 0** and **1,440 per view at zoom 1**. This supports only the stated bounded observations, not full path-component or world parity.

Concrete zoom-0 examples in `paths-r0-z0`: queue-6 has four differing pixels in `[512,256,576,288)`; queue-9 has nine in `[320,352,384,384)`. Lamp differences occur near `(466,346–362)`, `(402,378–394)` and `(338,412–426)`. At zoom 1, flat-3 alone has 72 differing pixels in `[400,240,432,256)`. The repeated rotation-dependent shape selection suggests a separate zoom sampling issue; intra-path parent ordering is another candidate explanation for the small zoom-0 overlaps. Both explanations require implementation evidence and another strict capture before confirmation. No tolerance or broad exception was introduced.

## Build97 candidate inspection

I manually inspected all eight shared-palette sheets and all eight binary difference images in [path97-art-review](../obj/vulkan-parity/path97-art-review), then inspected enlarged nearest-neighbor slope details. The [summary](../obj/vulkan-parity/path97-art-review/summary.json) pins the indexed inputs. Whole-image differences are **26,062 / 26,066 / 26,062 / 26,066 pixels** for zoom-0 rotations 0–3 and **384,537 pixels per rotation** at zoom 1. This removes 225 differences per zoom-0 view and 3,080 per zoom-1 view relative to build96. The whole-image result remains **fail**: the background and outer skirt still differ, and there are smaller interior slope differences. No comparison tolerance or acceptance mask was added.

Repeating the same diagnostic rectangles described above gives **142 / 146 / 142 / 146** differences in their deduplicated broad union at zoom 0, and **57 per rotation** at zoom 1. All 17 flat and all 16 queue samples have **zero differences in their tight ground rectangles at both zoom levels and every rotation**. The former lamp, bin and bench overlap differences are no longer present in the inspected difference groups. Broad rectangles can overlap neighboring slopes; their individual counts are not independent per-object failures.

The remaining zoom-0 interior differences lie around the four sloped terrain/path boundaries. Every differing indexed pair there uses indices **70–76**, and the enlarged images show grass texture differences beside the path rather than changed ash flooring or white rails. This is evidence for a terrain seam issue in these samples, not a general exemption for terrain differences.

The zoom-1 residual is confined to one camera-facing slope: `slope-0`, `slope-3`, `slope-2`, and `slope-1` respectively for rotations 0–3. In `paths-r0-z1`, the full residual lies in `[529,233,560,254)`: **11 pixels form a diagonal left-railing edge**, and **46 pixels lie around the right exposed soil edge**. The other rotations repeat the same translated pattern. This includes a path railing difference, so it cannot honestly be labeled solely a terrain/background failure. The remaining slope sampling/terrain seam requires further qualification; a general cross-tile sorting rewrite was not attempted for this checkpoint.

The [interior diagnostic artifact](../obj/vulkan-parity/path97-art-review/interior-diagnostics.json) records exact pixel coordinates, indexed pairs, rectangles and zoom-1 connected groups. It is analysis of existing captures, not a modified comparator. The bounded evidence supports the corrected isolated flat, queue and addition samples at these cameras; it does **not** qualify every path pixel, general cross-family occlusion, other zooms, or the omitted families listed above. Original-art raster comparison remains the authority despite successful scalar-rule and local-parent-order tests.

## Reference artifact identities

SHA-256:

- `manifest.json`: `a713169d2ca905db1cfc873d8298cf5c69665288ac90545c726dd8a553a9d198`
- `paths.park`: `1294c56b15f18210ea008b76e23daba2a7181f1aec6ba7410a730aba74497158`
- `palette.bgra`: `f1dbd3f4c62318deaf4e4b34d48db75f54025c0e6b57c3546990e1da386d9e73`
- Driver used by upstream build02: `84962012e5b64658c5d93b7d764ecfeff3d620a1eb84eee19bd1f2960bb7d566`

| Inspected PNG | SHA-256 |
| --- | --- |
| `paths-r0-z0.png` | `24dab8157d365a58516b4190ea30ae362790657e1be0511ff3131e814818429c` |
| `paths-r0-z1.png` | `b2b5a79c703f80a2d2ead2345c4a072f93d3b5f3517ea73300c93761a11c5df1` |
| `paths-r1-z0.png` | `88020ce36cdd94608e1d583ff5e4cfeaeebf91f43b488a6f0aabd09a2b6fda2c` |
| `paths-r1-z1.png` | `1c57b50061349cf26beeacceb5a09725956488b4139eef880c5a447d9fccdd06` |
| `paths-r2-z0.png` | `cd18b9a9e26635e0f35c079f684f6c48fc5b6ec760f25908daa6d75c35a61c94` |
| `paths-r2-z1.png` | `a674c067383be26acbbf6cb32fc273435d2259cc89b5305892f64d171a57591e` |
| `paths-r3-z0.png` | `e6ca7639912fc94b237714332239971b2f02daba569a6dc3eff779aaa80bd97b` |
| `paths-r3-z1.png` | `a8587acc945858abea5bc28ec62126803fbd24b15969e7e7ff69f1d854654974` |

## EverythingPark build97 manual review

Inspected the full saved 3840×2160 final images from `performance-path97-12000-01` and the matched build92 control `performance-path-baseline92-12000`, including a native-resolution view of build97. Both show rotation 3, zoom 2 and the same park framing. This is a comparison between two incomplete native renderers, not an upstream pixel-parity test.

Build97 restores a substantial, spatially coherent path network over the previously sparse terrain: connected light and dark walkway routes, coloured queue strips, raised routes and the varied path samples near the upper park edge. The network follows the map's tile directions without an obvious global shift, scale error or corrupted scatter. The terrain plateaus, slopes and water basins remain recognizable in the same locations; UI and the park-wide image are populated. I found no additional gross image failure in this view. Fine addition/fence details are intentionally not drawn at zoom 2, so this overview does not qualify those components; their closer reference comparisons remain separate above.

The image still omits guests, staff, vehicles, rides/tracks and scenery. Missing physical path supports and mixed-category ordering remain open; apparently unsupported raised paths are not evidence of complete bridge rendering. The outer cliff skirt is the owner's explicitly accepted aesthetic difference. The thin tall foreground column is also present in build92 and was previously traced to the actual high surface at tile (254,1), rather than introduced by this path layer. This review does not extend acceptance to other cliff or path differences.

The build97 screenshot receipt records a completed frame after measurement, unchanged authoritative state, simulation tick 3,145,931 and publication source tick 3,145,928. The three-tick difference is recorded asynchronous snapshot age, not a claim that the final image depicts the newest simulation state. Its `renderScope` text still says “terrain/background/UI only”; that historical literal understates the now-visible paths. No artifact was rewritten. Manual placement/terrain editing, displayed frame pacing and other cameras remain separate runtime checks.

| Evidence | SHA-256 |
| --- | --- |
| Build97 final PNG | `90bce206cbb6adb0b76686223e925f8a5259ba0c6fcd229af72ab62d7ec38750` |
| Build92 control final PNG | `18d6b6b5217e6ffa067b08f6502f2fceb24f7384ad41a0a0c0f9d2d671935f6b` |
| Build97 performance summary | `4dfaa04d0dcd236034bf54e720620e8a5363a2ac5189d16b46c8f81c192b7e39` |

### Final build98 confirmation

Manually viewed `performance-path98-12000-01/final-benchmark.png` after the final build. It is byte-identical to the reviewed build97 PNG: SHA-256 `90bce206cbb6adb0b76686223e925f8a5259ba0c6fcd229af72ab62d7ec38750`. The receipts also match both decoded indexed pixels (`f70da944b9a728b569ac2856a5e6dbf2af0a2ebe19f3729a3e87c16f752cab05`) and RGBA pixels (`409a04adb34fb5b5cafaba3dd2343b2b5a0721831b3d226d1163a8f50a546b10`), so there is no new visual divergence. The existing scoped observations and limitations above carry forward.

Build98 correctly labels the image “GPU terrain, paths/additions, background and UI; other world categories omitted”. Its completed frame has publication tick 3,145,930 and simulation tick 3,145,931, with unchanged authoritative state and capture outside measurement. Final summary SHA-256: `8b13b35ab371f975f14467cf56f6c1206dc7a033a9147d41e1c9b4f415f710ce`. No new runtime or GPU work was performed for this review.
