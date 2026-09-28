# Track-design preview visual review

Reviewed 2026-09-23: **all 12 saved reference/candidate pairs match visually**, with no visible defect or mismatch requiring an exception. This review qualifies the saved `track-preview-upstream-01` capture from the frozen build75-era source. It does not qualify subsequent source changes.

The immutable [run summary](../obj/vulkan-parity/track-preview-upstream-01/summary.json) remains `exact-match-pending-review`; it was not edited. It reports zero indexed, palette and RGBA differences in every case, two successful processes, and current Vulkan synchronization validation activated with no messages. This separate document supplies the manual review.

## Images inspected

The actual production `TrackDesignDrawPreview` entry point rendered three synthetic splash-boat designs containing 1, 35 and 55 flat track elements. Each was captured at all four rotations at 370×217, without scenery. The driver labels their recipe `expectedZoom` as 1, 2 and 3 respectively; it does **not** independently observe or report the actual internal viewport zoom. These labels must not be promoted to measured camera evidence.

I viewed every complete reference image, candidate image, normal diff and amplified diff in three contact sheets. Each source image remains at native 370×217 resolution, without cropping or resampling; transparent pixels are composited over a neutral grey background solely for viewing. All 24 difference images are black. The [review artifact receipt](../obj/vulkan-parity/track-preview-visual-audit/receipt.json) pins all 48 inspected source files and the three sheets.

| Design | Visual observation | Nonzero indexed pixels by rotation 0/1/2/3 |
|---|---|---|
| flat1 | A small centred flat section is visibly present in each view, with cyan interior and coloured edges. Rotation-dependent edge detail matches in both lanes. | 311 / 310 / 311 / 310 |
| flat35 | A long continuous diagonal crosses most of the preview, with visible cyan interior and red/yellow edge detail. Both endpoints remain inside the image. The diagonal reverses with rotation; no candidate-only gaps, clipping or shifts are visible. | 2527 / 2387 / 2527 / 2387 |
| flat55 | A thinner diagonal remains centred and complete, with matching endpoints and alternating edge detail. Its smaller displayed width is consistent with the recipe's intended more distant view, without independently proving the internal zoom. | 881 / 881 / 881 / 881 |

The images are nonempty actual track previews, not loading screens or blank placeholders. They do not contain the populated park itself: EverythingPark is the loaded live world used to exercise temporary-map restoration, while the rendered scene is the synthetic design.

Independent artifact-only checks verified that each comparison PNG matches its hash in the run summary, that all 12 decoded RGBA pairs are byte-identical, and that each case's complete four-rotation `preview.indexed` and `palette.bin` match between lanes. The raw palette SHA256 is `aa57418ece0263440a7b8d0ba8864021d98a07449a23017473f091bb5ee62db9` in all cases. Both reports state 29,294 loaded G1 records and payloads, loaded RCT1 CSG, and no before/after restoration differences. The current report records one shared service creation, a created device, and 12 sessions/completions. These are capture-report observations, not newly executed tests.

## Provenance pins

| Artifact | SHA256 |
|---|---|
| Immutable run summary | `91a4b5a142965d9c2bf008c2de45fde232b02e6dcdd00c36ff094ff695914ef4` |
| Review artifact receipt | `e88d356135a3f200bc6a60e06bc2fd0f12709f2c4a67a6dd8d6ecc905eeb12e8` |
| Shared diagnostic driver captured by the run | `511be6bdb4d7253819ed5ea97c2c5896d2c6eff7dd98fd37bcf6eae048f5969b` |
| Current track-preview build receipt | `33e2b0e3e8a8cdede0b5fe53797fa4b645b0883fe9bdac67e0ce05d0e0da30d6` |
| Current UI-cache build11 receipt | `d5dc23bcaa5deee1c1bf386fd745a86198e4b3126da4573ebc108e3bc4e66041` |
| Build75 shader receipt | `6d5ac6c44d3fca9687117243b62800344522594be58df2acba4b66756bfd0e6d` |
| Upstream track-preview build02 receipt | `3872617e92a4a3b77ba49c0760b6402e1572cf2de2f716b474ad3ce5725f4ccc` |
| Current executed binary | `bbece1e445900ac4fc7845f4868d10e9d0f8a418c6e7459f2aa0bb2122962a5d` |
| Upstream executed binary | `a7074859a5c3e6d02edff6486a83f9b125539433903bc96466e58c11cf135adf` |

The separately built pristine upstream reference revision is `b80a4a84e92be8e07904b38d1032d0bb88280bb4`. The existing frozen package supplied immutable park/data dependencies; it was not the source reference for this comparison.

## Reviewed image hashes

Each row's SHA256 applies to both `comparisons/<case>/r<rotation>/reference.png` and `candidate.png`. Every normal/amplified diff has SHA256 `e154d2c067fca8ac840374cbb2321832a0ee9afd65441c47f3e1ea4eb15868a6`.

| Case | Rotation | Reference and candidate PNG SHA256 |
|---|---|---|
| flat1 | 0 | `90e80d954f04bb55e14633f6da55dcc48d6ef31bea808ce11cf1f35fd1611543` |
| flat1 | 1 | `59da283c51974d3af0e2380157bafad408931ee2b5095f27a71bd2f17b66e4bf` |
| flat1 | 2 | `693410ba5ab302e0ffb73a4432546b094193c8c0827825409ced302599a87030` |
| flat1 | 3 | `64e2a1a4386b033166fc5a949b55faba654506c399dd18182d72140d746322ec` |
| flat35 | 0 | `1c1be25438e95beab3fd939e2e5aac48f63bc70d1bb87c1388ac431941420284` |
| flat35 | 1 | `9bf4cfb6fb9df5d0e8e521ac2b8cdc3bf5c9dc954e2a47054a527e9711aebfe8` |
| flat35 | 2 | `6999d0ef844b0e94065638d74d0cac9425d5fc15ab0336ec67c877501126e77a` |
| flat35 | 3 | `c8328cb6b515f709d9fbc5135c257a1e46f2f3f46b5d5653d89acc34e963c280` |
| flat55 | 0 | `0705f4dc45110a6e8580058578f2ef1ec8df39674266978ff0725d2da67399bf` |
| flat55 | 1 | `c2de5cd385dd759c69280eb018c2c2f3c0627ad2a602a522678e3be7bc2ff8b7` |
| flat55 | 2 | `710287c45bd8c31806c550423e681b281f6a601e343b90870224575c0b3a5587` |
| flat55 | 3 | `65d374da05e41bad0c0b5c4c00577640bceff682d1a2323dbd0202b40c2f30e0` |

This is narrow static preview evidence. It does not cover curved, elevated, twisted or complete ride designs; scenery placement; other ride families; interactive track-list/install windows; moving vehicles; disputed tree/support ordering; native retained world admission; HDR; 4K pacing; TPS; scripted custom images; or later source changes. Failure/restore tests remain separate evidence. No build, test, game or GPU process was launched during this review; only saved artifacts were read, hashed and arranged for inspection.
