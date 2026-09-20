# Primitive differential visual review

Reviewer: implementation agent `/root/primitive_parity`, 2026-09-19.
Evidence: `obj/vulkan-parity/run-01`, produced by the required runner against the frozen software source.
No software-defect exception is accepted by this review.

The agent opened every divergent sample with `view_image`: software, Vulkan and amplified difference images for both indexed and actual captured RGBA layers, for both fixtures. The two layers show the same spatial defects. PNGs, raw pixel buffers, shader hashes, device metadata, reports and the runner's SHA-256 receipt remain in the original run directory.

| Fixture | Indexed / RGBA different pixels | Inclusive bounds | Visual observation and disposition |
| --- | --- | --- | --- |
| Rectangles | 543 / 543 | (65,12)–(87,46) | Both index-zero rectangles are absent in Vulkan. The top 23×14 fill and lower 17×13 cropped clear retain the green background. The magenta diff consists of exactly these two solid blocks. Fix the Vulkan rectangle shader's unconditional rejection of index zero for untextured commands; index zero is a writable canvas value, although transparent sprite texels must still be discarded. |
| Lines | 391 / 391 | (1,0)–(95,79) | Vulkan lines occupy neighboring rows/columns and differ at clipped diagonals and endpoints. The one-pixel horizontal line at the top appears on row zero in Vulkan; the software reference places it on row one. The difference image traces numerous line edges, rather than a palette/lighting problem. Vulkan's native line rasterization with integer vertices and pre-raster endpoint clipping does not match the frozen software's segmented Bresenham coverage. Generate identical integer horizontal spans in the recorder and submit rectangles; do not change the frozen reference or approve an endpoint exception. |

Selected source-image SHA-256 values:

| Sample | SHA-256 |
| --- | --- |
| Rectangles indexed software | `e663d567590c550b82f4b16282eaccf30903b2258f2df1d00feb455714fb0d6a` |
| Rectangles indexed Vulkan | `5310884eb41b6bf2f617c881268e4ae7b97d28b02d1929869e1908e4a83e49ee` |
| Lines indexed software | `f22479ee519fe1d0e859a2a05e10322bb769d737491a8bb3088b85e6ff17819b` |
| Lines indexed Vulkan | `d134ea840f026f0e38f917b62325c969d9e812c193d3881100ee9be90b48c1de` |

Hatches, bitmap sprites, RLE sprites and filter fixtures have zero indexed and RGBA differences in this run. This is narrow fixture evidence, not complete feature or game-screen parity. The next run also loads bundled palette assets for all primitive fixtures to remove incidental resource-lookup fallback from sprite tests. Final RGBA reference here is deterministic palette expansion of the software canvas; actual SDL software presentation is a separate scaling test.

## Fix verification

In `obj/vulkan-parity/run-02`, all six primitive fixtures now have **zero indexed and zero final RGBA differing pixels**. All bundled palette lookups succeed. The agent opened the corrected rectangle and line reference, Vulkan and difference RGBA images: both missing rectangle regions are restored, line positions and endpoints match, and both difference images are black. The corrected Vulkan PNG hashes exactly match the original software samples above: rectangles `e663d567590c550b82f4b16282eaccf30903b2258f2df1d00feb455714fb0d6a`, lines `f22479ee519fe1d0e859a2a05e10322bb769d737491a8bb3088b85e6ff17819b`. No exception or tolerance was needed.

## Expanded fixtures: raw masks

The agent opened all twelve divergent PNGs in `obj/vulkan-parity/run-04`: indexed and RGBA software/Vulkan/difference triplets for `Masks` and `MasksZoomIn`. Both show matching shape placement and clipping, but many colours differ inside the masks. The software image has fewer red/magenta source colours; Vulkan preserves those colours under partially set mask bytes. Differences align with the periodic partial-bit mask rows and double in physical size in the magnified fixture. This is not an accepted correction of software behavior: the documented raw-mask contract applies bitwise AND, whereas the Vulkan shader only tests mask bytes for nonzero.

| Fixture | Indexed / RGBA differences | Inclusive bounds | Software PNG SHA-256 | Vulkan PNG SHA-256 |
| --- | --- | --- | --- | --- |
| Masks | 1,389 / 1,389 | (0,0)–(70,44) | `c27acf1ab8e10ceaa15eda6ff6462d8a095c4bad3ab43dc4065313d406338896` | `3a01fbdb6e490ff83bf7b3d7198ba7a33237069c17012740714a12565dacb939` |
| MasksZoomIn | 3,242 / 3,242 | (0,0)–(95,79) | `d296c5836bc29cdbb60ccfc9f16127de354bee7734a6392d650b0d9b8e9685a5` | `44ce97a188ed9496b67aa8432f284bee01d6100e58a02b2343a02f3d8be52fff` |

The other twelve primitive fixtures, including bitmap/RLE zoom in/out, negative target origins, three-colour remaps and overlapping glass/water/filter/opaque commands, are pixel exact in both layers. The scene test also reports all eight rotation/zoom views exact after format-aware sprite geometry; its dedicated agent records the corresponding scene visual review. No exception or global tolerance is introduced.

## Expanded fixtures: covered zero indices

The agent inspected every indexed/RGBA software/Vulkan/difference triplet for `OpaqueZeroBitmapRaw` and `CoveredZeroRleRaw` in `obj/vulkan-parity/run-05`. Sprite bounds and nonzero pixels match. The difference image shows regularly spaced points inside each sprite where the software writes palette index zero and Vulkan leaves the green background. Those points are source coverage, not transparent gaps between RLE runs. The raw-mask fixes are now numerically exact in both layers; the zero-source probes with primary remapping and solid silhouettes also pass, demonstrating that zero treatment depends on the drawing operation.

| Fixture | Indexed / RGBA differences | Inclusive bounds | Software PNG SHA-256 | Vulkan PNG SHA-256 |
| --- | --- | --- | --- | --- |
| OpaqueZeroBitmapRaw | 489 / 489 | (0,0)–(94,68) | `ac3b4b0b3db3ee9c73b1f18abe80e60071290608e4fd7a10c6da9a251ec738df` | `8daf68ed2aae8c4c0264d13de68f8edc221e127b8f9dba58ddd62d9986935c2c` |
| CoveredZeroRleRaw | 634 / 634 | (0,0)–(92,68) | `8a5d2d9d0fce978de5fffba9ce556b3cc9cccff3354f3d6ac904f78359c98619` | `2ba223261866283597288f57b2d47490da39b44fb79e99f618ecaec7d290580b` |

The decoder already preserves an independent coverage vector; the current atlas upload keeps only its index vector, losing the distinction between a covered zero and an uncovered texel. A universal RG8 atlas would increase the reserved 2048×2048×64 sprite allocation from 256 MiB to 512 MiB and require changes to every upload. A narrower option is an additional R8 coverage allocation only for assets containing covered zeros, using existing masked rectangle commands on that uncommon path; this retains the 256 MiB atlas allocation and consumes extra slots/bytes only for affected sprites. A raw opaque bitmap can alternatively use a command flag with no additional texture. No new exception is approved.

## Raw-mask fix visual verification

The agent opened all twelve corrected PNGs from `run-05` for `Masks` and `MasksZoomIn`, including both indexed and captured RGBA triplets. The partial-bit rows now have the same colours as software, clipped edges and magnification agree, and all four difference images are black. Both layers report zero differences. Corrected Vulkan indexed PNG hashes match the original software hashes: `c27acf1ab8e10ceaa15eda6ff6462d8a095c4bad3ab43dc4065313d406338896` and `d296c5836bc29cdbb60ccfc9f16127de354bee7734a6392d650b0d9b8e9685a5`. This closes these measured divergences without exceptions.

## Covered-zero fix and ordering verification

In `targeted-07`, all 27 primitive fixtures have zero differing pixels in indexed and captured RGBA output. The agent opened all twelve corrected raw-zero PNGs and both new ordering fixture RGBA triplets. Missing zero pixels are restored; neighbouring colours and clipped shapes agree with software. The corrected raw bitmap/RLE PNG SHA-256 values match their original software references above (`ac3b4b…738df`, `8a5d2d…98619`). The ordering samples include preceding and following filters, later opaque fills, nested clipping, and sprite zoom in/out. No exception was needed.

The fix retains the existing R8 atlas and adds a zero-only coverage allocation only when decoding finds covered-zero texels. A distinct command flag writes those pixels, with the same geometry and immediately following depth as the source sprite; raw masks and solid/remapped sprites keep their original semantics. Three passing cache tests verify rollback after a second-allocation failure, both allocations remaining pinned across invalidation, failed-frame upload retries, and suppression of the companion for remapped/minified RLE commands. The loaded static census (G1, bundled G2 and fonts) contains 30,475 bitmap/RLE assets and no covered-zero assets, so this corpus uses no additional coverage slots. Dynamic/object/custom assets remain outside that census.

The agent also opened rain/snow RGBA triplets. Their deterministic negative phase, partial region and bottom-right one-pixel draw match exactly. Rain paints 14 index-12, 13 index-14 and 13 index-16 pixels; snow paints 13 index-16 pixels, plus its index-32 rows. These colours differ from the fixture's 30–125 striped background. The previously suspected pattern row-31 defect does not exist: enumerating the 32 actual rows shows only rows 0, 1 and 2 draw; the final zero pair lies beyond those rows. No weather production change is necessary beyond extracting the existing recorder for direct tests.
