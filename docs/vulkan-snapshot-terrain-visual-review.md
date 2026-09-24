# GPU snapshot terrain visual review — builds88–92

Reviewed 2026-09-23 from saved artifacts only. No build, test, game, or GPU process was launched during this review. All seven synthetic PNGs and both full real-park PNGs were opened and visually inspected. Pixel histograms and hashes were read independently from those files.

## Synthetic layer samples

The [focused-run receipt](../obj/vulkan-parity/snapshot88-focused/summary.json) records 39 tests, zero failures/errors, exit0, unchanged pinned artifacts, and an empty validation-diagnostic list. This review concerns the seven saved images under [world-surface-layers](../obj/vulkan-parity/snapshot88-focused/samples/world-surface-layers); the receipt covers additional tests as listed in its command.

These are deliberately sparse 96×96 grayscale test images, not miniature park scenes. The source test supplies rectangular8×40 terrain/water sprites,8×8 cliff sprites, and a one-pixel-wide opaque overlay stripe. It does not load original game art. The tall narrow rectangle at screen x16..23 and detached right-hand cliff squares around x46..53 are therefore expected fixture geometry; they do not themselves indicate missing terrain or a projection defect. The left/front strip can lie outside this very small viewport and be clipped.

Visual observations:

- Samples0 and1 contain the same vertically layered bar, a brighter overlay stripe, and a detached cliff square. They are byte-identical PNGs and decoded images. This is an unchanged repeat after the first accepted upload, not evidence about paused real-world animation.
- Samples2..5 keep the terrain/water anchor fixed by changing camera view together with rotation. Their shades and cliff markers change with slope orientation. Extra separated cliff markers in2 and5, and the upper patch in4, are visible; the fixture emits individual small rectangular edge sprites, so these are not evidence of cracks in original terrain art. There are no unexpected full-screen writes or clipped-frame strips in these samples.
- The overflow sample is uniformly black: all9,216 grayscale values are0. The test separately requires the completed-frame status to report failure, so the black image is an intentional rejection rather than successful empty-scene rendering.

The assertions give the exact layer interpretation. In the flat sample, pixel(18,40)=27 is terrain20 filtered through synthetic water row7; (16,40)=99 is the opaque overlay stripe; (18,64)=20 is exposed terrain; (48,65)=42 is the front cliff. The histogram also contains49 where water filters a cliff42 and7 where it filters the zero background. The rotated slope samples check terrain values22,21,28,24 and filtered values29,28,35,31 respectively. These checks demonstrate concrete layer output; they are not a complete per-pixel external-reference comparison.

All six populated samples have nonzero bounds[16,16,54,72), while the overflow sample has no nonzero bounds.

| Sample | Intended case | PNG SHA256 |
|---|---|---|
| `0.png` | Flat first accepted frame after abandoned upload | `dd5bba4ee108f453d7fba1900e1c9d9d7906b02e3c34c6ae27ad8955093e2b58` |
| `1.png` | Identical flat repeat | `dd5bba4ee108f453d7fba1900e1c9d9d7906b02e3c34c6ae27ad8955093e2b58` |
| `2.png` | Slope1, rotation0 | `b62c16acd0a5ee9ca84f4ca748f982bb66bcccd4353238fc657cebf92ccfde77` |
| `3.png` | Slope1, rotation1 | `2225d099d8588f436f8b91539439744bf6a955a3fc0822245149b45085e734fb` |
| `4.png` | Slope1, rotation2 | `ec5e9a4bd4fb82a2d22b4742d10f99728933a35ab24650068feae3d1c87ca228` |
| `5.png` | Slope1, rotation3 | `62b8396e19f0f5b1ba1d4df3289a758fabe63768223e40e334697a082f7a9910` |
| `overflow/0.png` | Explicit output capacity1; all world drawing rejected | `fa4499b3003a7e7c5a7a9eb757d06e363d8ddda4b551998207e770f9a708521f` |

Focused summary SHA256: `220715e246d7e0dc265f1d16a029cefe5f2b33540c0e39bfda6a2da27a024795`. Build receipt SHA256 recorded by that summary: `fd7b42ee23423077e7e7fd8988ea4d8cdc58bcf3197d68da161f80be5e113062`. Reviewed [test source](../test/tests/VulkanWorldSurfaceLayerTests.cpp) SHA256: `ef1edb68ea752d3e0f3bc83787a9c159297c41cd7e092eb1ebd739aefcd69b2a`.

## Real park: partial85 control versus snapshot88

Opened both complete3840×2160 post-measurement frames:

- [Partial85 control](../obj/vulkan-parity/performance-partial85-12000-control/final-benchmark.png), PNG SHA256 `1aafd922c48c1af5eca0880d23f64ad3004648074d1bf6ab5d5e2c3bba7426fe`.
- [Snapshot88 candidate](../obj/vulkan-parity/performance-snapshot88-12000-01/final-benchmark.png), PNG SHA256 `fda1d13ec6b68f5c1cdd671ea894b5ba10b2ab5ad55d1a928cc6cfee7fda29c9`.

The candidate visibly adds cyan water surfaces to the previously empty or land-only pools, including the upper-right round pool, middle/right rectangular pools, smaller left pool, and lower-right pools. Cliff faces now fill the left face of the central raised formation, the material demonstration walls, and the long exposed foreground boundary. The terrain map remains populated across the full main viewport, with UI at its edges. Most park objects remain intentionally absent in this partial renderer; empty grass areas must not be interpreted as a complete rendering of the simulated park.

**Corner column resolved as legitimate source terrain:** the initial visual review flagged the thin tall column at the nearest map corner for investigation. The subsequent [raw imported-terrain report](../obj/vulkan-parity/snapshot89-focused/samples/terrain-publication/EverythingPark/report.json) confirms that interior tile(254,1) is a present flat surface at baseZ2032, surfaceSlot0, edgeSlot0, with kind1 and no water. It is not a missing tile or a border record. The imported map is256×256. Its neighbor(253,1) is at336, neighbor(254,2) is at16, and front-facing border neighbors(254,0) and(255,1) are at16 with kind2. The column is therefore expected geometry from a genuinely high isolated terrain tile; this evidence does not support hiding it or adding a border exception.

The [candidate capture receipt](../obj/vulkan-parity/performance-snapshot88-12000-01/summary.json) records rotation3, zoom2 and saved view position(458,-4370), final simulation tick3145931 and held publication tick3145929. Brown column pixels occupy x1926..1941 and approximately y1593..2099 in the3840×2160 PNG. At this camera, tile(254,1) begins at world(8128,32). The renderer's rotation3 facing-corner adjustment gives(8128,64); its projection at source height2032 is(8192,2000). Applying zoom2 and the camera origin approximately(114,-1093) places the top anchor at(1934,1593), matching the observed column. Both front edges face border neighbors: the shader plans from minimum z16 to baseZ2032, yielding126 full16-unit strips per face, or504 vertical screen pixels before sprite offsets and clipping. Their camera-relative(30,0)/(0,30) offsets account for the two adjacent narrow faces. The raw height and projection jointly explain the previously suspected needle.

The [source shader](../data/shaders/vulkan/world_surface_compact.comp) and [edge rules](../data/shaders/vulkan/world_surface_rules.glsl) have no special corner-column emission. They skip drawing the border's own base while treating it as a minimum-height neighbor for exposed interior faces. Here that rule operates on an actual high interior surface. Source89's GPU pipeline is unchanged from the reviewed build88; the new evidence is a CPU-only imported-state diagnostic, not a replacement GPU screenshot.

The raw dump was captured at imported sourceTick3133831, with no simulation advances or GPU readback. Its fixture and the benchmark reference were independently rehashed during this review and both equal `c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`. The report does not claim a tick-for-tick dump of the final rendered packet, but it establishes the high tile in the exact input park and its geometry matches the rendered column. Material slot0 has an84-image supported edge range in this imported generation; its numeric G1 base is generation-local and must not be reused as a cross-run identity.

Raw evidence SHA256:

| Artifact | SHA256 |
|---|---|
| `report.json` | `8a4cb06336ad66b1391a3f5f596254b804857bbcdcba37603e86ab846951f834` |
| `surfaces.csv` | `fd02bd9e6d753751f1bbebd2d150fef6394e82aa4f5a3148c851e778cdcd9250` |
| `materials.csv` | `9e10a7e7848fa05788b613e5b13b1b5ae67d51518f19d65b5a01c06444a4db74` |

The two run receipts report matching workload/state for comparison. They are performance-lane final captures, not synchronized original-renderer pixel oracles. Candidate screenshot capture is explicitly outside measurement and records authoritative state unchanged by capture. Summary hashes: control `db33e1b400646671d5e3601b9622b5646a038dcb68e3921387c31fde260b9836`, candidate `a0ece2b59af19e12be98811f10bce22bcbe30ef9a3370b683a406d994b92dd3f`. Candidate decoded RGBA SHA256 independently recomputed as `38433d2048bdb29f4bab70dc3201c1b4da6a98be2fd257fb5b42186bd56d8426`, matching the receipt.

## Build89 real-park repeat after CPU-work removal

Manually opened the complete [build89 final capture](../obj/vulkan-parity/performance-snapshot89-12000-01/final-benchmark.png) and compared it with the already reviewed build88 capture. The water pools, cliff faces, central terrain formations, legitimate corner column, background and UI all remain visually unchanged. Independent full-image comparison finds **zero differing RGBA pixels across3840×2160**. The PNG files themselves are byte-identical, so no difference crop is necessary.

Both files have PNG SHA256 `fda1d13ec6b68f5c1cdd671ea894b5ba10b2ab5ad55d1a928cc6cfee7fda29c9` and decoded RGBA SHA256 `38433d2048bdb29f4bab70dc3201c1b4da6a98be2fd257fb5b42186bd56d8426`. The [build89 run summary](../obj/vulkan-parity/performance-snapshot89-12000-01/summary.json) hashes to `17a679d507694ef594014d5ebdf67107b0e4cfa6e482bff128e0b8a18ed17011` and records status pass. Both captures use rotation3, zoom2, view(458,-4370), final simulation tick3145931 and the same final entity checksum. The held publication ticks differ:3145929 for build88 and3145930 for build89. Both receipts state the final capture was outside measurement and left authoritative state unchanged.

This establishes no visual regression in this saved final view after the removal of unused crowd calculations and CPU animation repaint work. It does not qualify animation continuity, additional cameras, all simulation states, or the intentionally absent world families. Source90's reported test-only changes and identical executable/shader hashes do not constitute an additional visual run; this section identifies the actual build89 capture reviewed.

## Final build92 visual checkpoint

Manually opened the full 3840×2160 final images from [ordinary run01](../obj/vulkan-parity/performance-snapshot92-12000-01/final-benchmark.png), [ordinary run02](../obj/vulkan-parity/performance-snapshot92-12000-02/final-benchmark.png), and the [headroom run](../obj/vulkan-parity/performance-snapshot92-12000-headroom/final-benchmark.png). The terrain formations, water coverage, exposed cliff faces, legitimate tall corner tile, background, and UI retain the reviewed build88 arrangement. All three have the same decoded indexed SHA256, `60976319f143ae88d87fe706f3e9ecf342ef9699e3f519f5e544d066420595b9`, with zero differing indexed pixels against build88. Ordinary run01 and headroom are also byte-identical PNGs to build88.

Ordinary run02 is **not RGBA-identical**: exactly 4,148 pixels differ, within half-open bounds `[1507,436,3505,1439)`. Independent comparison attributes every changed pixel to three changed palette entries, with no index changes or transparency-index changes:

| Palette index | Run01/build88 RGB | Run02 RGB | Affected pixels |
|---|---|---|---:|
| 232 | (0,87,79) | (0,95,87) | 846 |
| 233 | (15,115,107) | (23,127,119) | 837 |
| 234 | (7,107,99) | (0,95,87) | 2,465 |

Palette entries237,238,239,243,245 also differ but are unused in these images. All other palette entries match. The affected visible regions are water, rather than UI or changed terrain geometry. [PaletteIndex.h](../src/openrct2/drawing/PaletteIndex.h) identifies230..234 as water-wave entries, and [Palette.cpp](../src/openrct2/drawing/Palette.cpp) updates them from `gPaletteEffectFrame`. Thus the observed difference is consistent with water palette animation at a different final frame phase; the receipt does not record that phase directly. This classification preserves the actual RGBA mismatch and does not turn it into an exact-match claim or a parity exception.

| Saved capture | PNG SHA256 | Decoded RGBA SHA256 |
|---|---|---|
| Ordinary92 run01 | `fda1d13ec6b68f5c1cdd671ea894b5ba10b2ab5ad55d1a928cc6cfee7fda29c9` | `38433d2048bdb29f4bab70dc3201c1b4da6a98be2fd257fb5b42186bd56d8426` |
| Ordinary92 run02 | `18d6b6b5217e6ffa067b08f6502f2fecb24f7384ad41a0a0c0f9d2d671935f6b` | `d5972ddd1dec423983d08a354df47863b4cb4f98a0063da2bf432e3ff49c1658` |
| Headroom92 | `fda1d13ec6b68f5c1cdd671ea894b5ba10b2ab5ad55d1a928cc6cfee7fda29c9` | `38433d2048bdb29f4bab70dc3201c1b4da6a98be2fd257fb5b42186bd56d8426` |

All three receipts record rotation3, zoom2, view(458,-4370), final simulation tick3145931, publication source tick3145930, and entity checksum `07d58eaefde6aa6d000000000000000000000000`. Their completed frame numbers differ:4863,4846,4129 respectively. Each records capture outside measurement, unchanged authoritative state, and partial terrain/background/UI rendering. These are three final captures, not an animation sequence or original-renderer references.

The seven [build92 synthetic layer PNGs](../obj/vulkan-parity/snapshot92-focused/samples/world-surface-layers) were independently compared by file hash with their matching build88 samples: all seven are byte-identical. Their existing manual observations and per-image hashes above therefore apply unchanged; no new fixture visual output is inferred from test counts alone. The [build92 focused receipt](../obj/vulkan-parity/snapshot92-focused/summary.json) records72 tests, zero failures/errors, exit0, unchanged pinned artifacts, and no validation diagnostics.

Final receipt SHA256 pins:

| Receipt | SHA256 |
|---|---|
| Ordinary92 run01 summary | `96505181f58261aa5d0887c5eaa5fcfdc83f0175fe9af4f541fbf4e1e5766e45` |
| Ordinary92 run02 summary | `e8c81034f37e6b821ba6cac035347597fbe446e160754aedfc3b6e4db68e5c16` |
| Headroom92 summary | `734545b97270b4823ec841b9a62c64713bffd7804b8e6633f288ef7e6b100c4b` |
| Focused92 summary | `85f5a4f629acd0277f6c6cd0d21a16f36e781767dbbb131344a5fa37cfc687a4` |
| Build receipt pinned by focused92 | `cac6b255c6044195ab6687333ee1dabbbd179627d35d7dbbe202dc6d1cf7dc8a` |

This final checkpoint shows no new indexed-output regression in the captured view. It does not establish correct frame pacing, animation continuity, complete-world rendering, or performance acceptance; those require their respective measured evidence and broader rendering work.

## Scope

This review supports visible output from the new terrain, cliff, and water layers and bounded overflow handling in the synthetic fixture. It does not establish full-world parity, correct general cross-tile ordering, water-side/tunnel rendering, overlapping-water composition, moving entities, arbitrary object reloads, HDR, scanout cadence, or a performance conclusion from images. The real corner column is explained by legitimate imported terrain, rather than retained as an unexplained defect. Existing partial ordering and water-intersection limitations remain open; resolving this column does not establish general pixel parity.
