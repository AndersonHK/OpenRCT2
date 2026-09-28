# Native terrain visual review

Reviewer: `screen_capture`, 2026-09-19. Input: the immutable `native-terrain-v1`
park pinned in [the input receipt](vulkan-native-terrain-input.json). This is an
actual main-UI SDL frozen-software display versus an actual Vulkan final-output
capture, with a named diagnostic frame and native world-surface coverage.

## First admitted view: rotation 0, zoom 0

I opened all four `captures/baseline-{0,1}/screen.png` images under:

- `obj/vulkan-parity/ui-frozen-terrain-r0z0-01/`
- `obj/vulkan-parity/ui-vulkan-terrain-r0z0-01/`

I also opened both Vulkan run `comparisons/baseline-{0,1}/diff-amplified.png`
images. Each is uniformly black, consistent with the measured zero differences.

The 960×640 screen contains a visible textured green terrain corner rather than
a blank viewport. Its point sits near (480,256), with two straight diagonal map
boundaries reaching approximately (0,496) and (959,496). Fine grass texture fills
the lower portion without visible holes, vertical strips, doubled edges or gaps.
The region outside the map is consistently dark. Top toolbar icons are readable
on both sides. Bottom panels show `0 guests`, `March 1st, Year 1`, and sunny
`68°F`; the empty park has no stray guests, rides, scenery or construction grid.
The Vulkan sample and its repetition preserve all of these landmarks exactly.
This is the intended near-corner view, with meaningful terrain, map-edge and UI
coverage. There is no research window in this baseline; overlap is a separate
pending view.

Both repetitions have **zero differing pixels in indexed and physical RGBA
layers**, with identical output hashes. No exception or tolerance is accepted.
The run status is `pass` with no validation diagnostics. The first captured
packet is frame 4, `worldSurfaces=true`, `worldSurfaceRecordCount=1024`,
`worldEpoch=4`; ordinary UI commands remain present (32 opaque rectangles,
1,316 opaque sprites and 20 transparent rectangles). Admission is `native`,
smoothing is disabled, viewport flags are zero, and reported viewport position
is `(-480,-240)`, rotation 0, zoom 0. GPU: NVIDIA GeForce RTX 5070 Ti,
vendor/device `4318/11269`, reported driver version `2585198592`.

| Artifact | SHA-256 |
| --- | --- |
| Each raw RGBA sample | `7e5f218a1b022658bbaff6c6c4e277042e66a80ce2cb525cb862c9cbb00fcde6` |
| Each raw indexed sample | `89d22c8c6254ef5f4aa2d636e9174a65498d6015f6e0e9539da6a8cbfbba057f` |
| Frozen `baseline-0/screen.png` | `74c81b869799ef80cb52b243bdb47f1374eae2858767a4c018713e23e63cf95a` |
| Frozen run `summary.json` | `ff2122a51003925767eab603558ca10dbd8bd4b9e635032397ec372a4380d6d2` |
| Vulkan run `summary.json` | `76b07adab7c348195fe0b2a6e6cb6117b8b5cb4e9690bc4a3fbf7f5841690087` |

This first view qualifies the shown flat, uniform-material terrain corner and
its native route. Rotations, zoom/phase, native UI overlap, smoothing/grid
fallback controls, and an independent current-software comparison remain
separate coverage requirements. Slopes, water, fences, differing elevations,
other devices and production-wide native admission are not inferred from it.

## Reserved map-border defect found in additional views

Before implementing the fix, I opened all 36 comparison images from the two
repetitions of `ui-vulkan-terrain-r1z0-01`, `ui-vulkan-terrain-r2z0-01` and
`ui-vulkan-terrain-r0z1phase-01`: physical reference/candidate/amplified difference,
and indexed reference/candidate/difference mask. The [baseline review receipt](vulkan-native-terrain-border-baseline-review.json)
pins every image and run summary SHA-256, both layer counts, and output hashes.
Original failed runs remain unchanged.

| View | Different pixels per layer and repetition | Exclusive mismatch bounds | Manual observation |
| --- | ---: | --- | --- |
| Rotation 1, zoom 0 | 26,918 | `(0,25)-(480,463)` | Vulkan extends the right-pointing corner about 64 pixels to the right, producing a grass strip along both diagonals outside the frozen map. |
| Rotation 2, zoom 0 | 20,024 | `(88,0)-(736,223)` | Vulkan extends the downward-pointing corner about 32 pixels downward, producing two extra diagonal grass strips. |
| Rotation 0, zoom 1 phase view | 2,560 | `(0,488)-(160,583)` | A narrow extra grass strip follows the lower-left diagonal, clipped at the left display edge; the rest of the terrain diamond matches. |

Indexed masks have the same shapes as physical differences; the issue exists
before palette conversion. The interior texture and toolbar/status panels are
otherwise stable. Frozen and current software agree for these inputs. These are
Vulkan defects, with no accepted exception or tolerance.

Source evidence: `world/Map.cpp::MapIsEdge` treats world coordinates below 32 or
at/above `(mapSize-1)*32` as reserved map border.
`paint/tile_element/Paint.TileElement.cpp::TileElementPaintSetup` uses
`BlankTilesPaint` there, rather than painting the stored surface. The native
recorder previously copied the snapshot's 1,024 surface records as drawable,
including all 124 border records. Existing ordinary blank-tile commands obscured
some of the invalid native coverage, which explains why rotation 0/3 baselines
could pass while other edges revealed it.

The bounded fix in `GpuCommandDrawingContext.cpp::DrawWorldSurfaceScene` leaves
the dense snapshot indices and record count intact, but marks records drawable
only for `0 < tileX < width-1` and `0 < tileY < height-1`. It uses the immutable
declared scene dimensions, not technical storage dimensions or live global map
state. Ordinary blank-tile commands still supply the border. Shader projection,
software rendering, saved input and expected images are unchanged. Corrected
captures and their manual review were pending at this point; the following
review closes these entries.

## Corrected border and control review: run 02

On 2026-09-19 I opened 63 corrected images, pinned individually in the
[corrected review receipt](vulkan-native-terrain-corrected-review.json): all 36
physical/indexed reference, Vulkan and difference images for both repetitions
of `r1z0`, `r2z0` and `r0z1phase`; the three current-software first-repetition
screens for those views; 12 physical triplet images for `r0z0`, `r3z0`, `smooth`
and `grid`; and 12 physical triplet images for the overlap steps listed below.
These are the `ui-{frozen,current,vulkan}-terrain-<view>-02` runs, using frozen
UI build 08, current UI build 10 and build 26 shaders. The receipt pins all 24
run summaries, build references, sample hashes, numerical comparisons and
Vulkan validation logs. The original run 01 failure artifacts remain intact.

| View | Manual corrected observation |
| --- | --- |
| `r1z0`, both repetitions/layers | Right-pointing grass corner ends near `(416,224)`; both diagonal edges match frozen/current software. The extra border strip and former extension toward x=480 are gone. |
| `r2z0`, both repetitions/layers | Down-pointing corner ends near `(480,192)` with clean, matched diagonals; the extra lower grass strip is gone. |
| `r0z1phase`, both repetitions/layers | Left-clipped diamond has corners near `(240,128)`, `(720,368)` and `(240,608)`. The lower-left edge follows the frozen boundary without the former extra strip. |
| `r0z0`, `r3z0` | Upper-pointing and left-pointing corners retain continuous grass texture and clean map/background boundaries. No missing native terrain, doubled edges or stray strips. |
| `smooth` | The ordinary-paint control retains the expected textured upper-pointing corner and intact UI. |
| `grid` | Visible diagonal tile grid overlays the grass consistently, reaching the correct map edges. |
| Overlap `research-front-0` | Research correctly covers the financial window; both windows occlude the terrain and retain readable labels. |
| Overlap `finances-front-0` | Financial Summary completely covers the research window; no terrain or hidden-window pixels leak through. |
| Overlap `partially-clipped-0` | The financial window clips correctly at negative left origin; the right strip of the research window remains visible behind it. |
| Overlap `restored-1` | Closing the windows restores the complete original grass corner without stale window pixels or gaps. |

The physical difference images and indexed difference masks are black. All
eight cases, including all overlap sequence captures and repetitions, have
**zero differing pixels in both layers across frozen software, current software
and Vulkan**. The receipt checks both layer hashes for every named sample
across all three renderers, not just the representative images opened above.
All eight Vulkan logs contain no validation diagnostics. Native admission is
present for the five terrain views and overlap, with 1,024 dense records; the
smoothing and grid controls explicitly report ordinary-paint admission with
zero native records. Toolbar and status panels remain readable and consistent.

This closes `NATIVE-TERRAIN-001-map-border-{r1z0,r2z0,r0z1phase}` as corrected
Vulkan defects, with no software change, accepted exception or pixel tolerance.
It qualifies this pinned flat terrain and these controls on the measured
device. Slopes, water, heterogeneous materials, entities, other devices and
general native admission remain separate gates. Root-owned fresh-process run
03 subsequently passed all 24 processes. Its summaries, logs and hashes are
included under `freshProcessVerification` in the receipt: every build/shader
receipt and named output hash matches run 02, with zero differences and no
Vulkan validation diagnostics. Those images were not reopened; their equality
to the manually inspected run 02 samples is verified by both layer hashes.
