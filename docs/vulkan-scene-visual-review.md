# Vulkan auxiliary scene visual review

Reviewer: `/root/decode_assets` implementation/review agent, 2026-09-19. Evidence: `obj/vulkan-parity/run-03/samples/SmallPark_R*_Z1`. This is an agent's manual inspection of the software reference, Vulkan result and amplified difference PNGs for every divergent scene group. It is not approval to waive pixel parity.

## Frozen scene and comparison

The fixture imports `test/tests/testdata/parks/small_park_with_ferris_wheel.sv6` once and paints independent auxiliary viewports with X8 and the Vulkan command recorder. No simulation updates occur between the two paints. After GPU recording, the software viewport is painted again and must match its earlier bytes exactly. The test also checks unchanged simulation tick, map element revisions and palette-effect frame.

The recorded focus is **(336, 112, 144)**; the canvas is **640 × 480**; the tick is **1215**. Both the pinned RCT2 G1 and RCT1 CSG assets were available. Individual report JSON files contain the input and shader hashes, camera positions, device/driver information, frame identity and exact mismatch statistics. The device was an NVIDIA GeForce RTX 5070 Ti. The RGBA reference is an opaque SDR expansion of the software indexed canvas using the frozen game palette; it is not a capture of the software SDL presentation target.

All four rotation samples at zoom 0 matched exactly. All four samples at zoom 1 differed. Indexed and RGBA differing-pixel counts agree. No exceptions were accepted.

## Manual inspection of every divergent group

| Fixture | Differing pixels | Inclusive bounds | Observations from software, Vulkan and difference images |
| --- | ---: | --- | --- |
| [R0 Z1](../obj/vulkan-parity/run-03/samples/SmallPark_R0_Z1/rgba/report.json) | 3,725 | (272,169)–(608,351) | Differences trace the rear and foreground white park fences and the small red-roofed ride building. Fine fence-post and building-edge patterns shift/change. The grass field and exposed map cliff texture match; there is no broad colour shift. |
| [R1 Z1](../obj/vulkan-parity/run-03/samples/SmallPark_R1_Z1/rgba/report.json) | 3,597 | (128,179)–(480,399) | Differences follow the left and front fences, with smaller clusters in and behind the wheel/entrance area. The apparent placement of the park and terrain is stable, while thin sprite details differ. |
| [R2 Z1](../obj/vulkan-parity/run-03/samples/SmallPark_R2_Z1/rgba/report.json) | 4,507 | (32,153)–(399,335) | Fence differences recur around three visible sides. A substantial additional cluster lies on the right-hand entrance tower/roof; small scattered details near the wheel platform also differ. Terrain and grass remain visually identical. |
| [R3 Z1](../obj/vulkan-parity/run-03/samples/SmallPark_R3_Z1/rgba/report.json) | 5,568 | (160,105)–(512,271) | The amplified difference exposes changes over much of the wheel's thin framework/spokes, as well as fences and the ride/entrance boundary. The normal images look close at this scale, but the exact differences are structural sprite details and cannot be excused as imperceptible noise. |

Inspected image triplets: each linked fixture directory's `software.png`, `vulkan.png` and `diff.png`. Raw indexed/RGBA bytes and indexed-layer triplets are retained beside them. Maximum RGBA channel errors range up to 236, with zero alpha error, so a broad rounding tolerance would be inappropriate.

## Diagnosis and required follow-up

The concentration on sprite details, agreement at zoom 0, and unchanged terrain at zoom 1 suggest a minified-sprite sampling/alignment issue. This is a **hypothesis**, not a demonstrated root cause. The primitive-parity agent is extending independent zoom/offset/clip cases before changing the GPU recorder. The frozen software source must remain unchanged.

These run-03 samples are retained as failing regression evidence. There is no evidence here that the software result is glitched, and no software-defect exception is proposed.

This fixture covers live auxiliary scene traversal and paint ordering. It deliberately does not establish parity of the main presentation snapshots, direct-terrain path, UI, weather, LightFX, physical display scaling or temporal sequences. A separate test-lifetime issue also appeared after this scene test: following primitive tests reported access violations during cleanup despite producing their image reports. That lifecycle failure must be fixed independently of the pixel discrepancies.

## Corrected samples: run-04

The recorder now retains the sprite's bitmap/RLE format and applies format-specific minification alignment and clipping to compact and palette sprite geometry. The frozen software renderer was unchanged. Build 08 / run-04 reports **zero differing pixels for both indexed and RGBA layers in all eight cameras**. The correction confirms the sampling/alignment diagnosis above for this fixture.

The same agent manually inspected all four corrected zoom-1 software/Vulkan/difference triplets. Fence posts, entrance edges and wheel spokes now match in each rotation; all four difference images are black. The following SHA-256 values identify the corrected raw RGBA Vulkan buffers (equal to their software buffers):

| Fixture | SHA-256 (`rgba/vulkan.bin`) |
| --- | --- |
| R0 Z1 | `989261AADE4C902EF1531A3313103FEA69C4D1AFF9B3AE526FBB6F7885EF197C` |
| R1 Z1 | `58194E2C961276495774AA7E67DEFE88D5E435143D3E4375E73EE1ACC75DD0D7` |
| R2 Z1 | `330A39B6B8DF2E4AA7AA04E8D9E80A6E0156A6CE035C0FF11DA1CBA046A34DA9` |
| R3 Z1 | `381C1B7B1D9F630FEA3191FF4C514657A60C6309C1678EE3D485165007444C75` |

Evidence lives in `obj/vulkan-parity/run-04/samples/SmallPark_R*_Z1/rgba`. No exception is needed for these four divergences. The subsequent primitive cleanup crash was separately resolved by giving its test context a complete scripting-engine lifecycle; run-04 no longer crashes there.

These captures still use the linked, frozen software source in the test executable. An independent frozen-source sidecar harness remains required for a separately built oracle: the existing frozen screenshot CLI cannot select an isolated user-data directory on Windows. Neither these exact auxiliary captures nor the separate publication-data tests qualify the main UI or direct-world-surface path.

## Fixture version 2: external-oracle input normalization

The separately built frozen-source CLI now supports isolated asset/config paths through startup plumbing, with its renderer unchanged. Version 2 changes all paired/repeated initial clears and the nonblank check to index 0, and records `clearIndex: 0`. **An initial clear does not determine the final background:** ordinary viewport painting subsequently paints blank map tiles with index 10. Screenshot options or `transparentScreenshot` configuration can set `VIEWPORT_FLAG_TRANSPARENT_BACKGROUND`, which suppresses those tiles and retains index 0. Run-06's ordinary scenes therefore still have final background index 10 despite their initial clear 0. The agent inspected all four zoom-1 software/Vulkan/difference triplets and found identical visible results and black diffs; this observation does not establish a final background index of 0.

The fixture retains the eight ordinary views and adds eight `SmallParkTransparent_R*_Z*` views with that viewport flag explicitly enabled, for an equivalent-input comparison with transparent external screenshots. Each report records the actual flags and `transparentBackground` choice. This is input normalization, not a renderer exception or a changed pixel tolerance. Version-1 hashes above remain evidence for their original inputs and must not be treated as version-2 goldens. External screenshot PNG transparency must be expanded through its palette with opaque alpha when comparing against the opaque screen RGBA contract.

Run-06 also passed asset-metadata comparisons against frozen software for all 341 bundled tiny glyphs, one loaded Ferris wheel car and 33 loaded guest walking groups. Its asset census covered 4,588 bitmap and 25,887 RLE assets in pinned G1, bundled G2 and fonts, and found no encoded covered-zero samples. This census does not include dynamic or custom object images; synthetic zero-clearing tests remain necessary.

## Independent frozen-source oracle closure

`obj/vulkan-parity/frozen-scenes-02/summary.json` records the external frozen-source executable, source receipt, explicit `--transparent` commands and input/output hashes. All eight rotation/zoom views have zero indexed and opaque-RGBA differences against **both** run-08 software and Vulkan transparent variants (32 exact comparisons). Screenshot export alpha is expanded to the declared opaque-screen contract; indexed bytes are compared unchanged.

The reviewer manually opened the representative `SmallParkTransparent_R0_Z0` frozen PNG, run-08 software PNG, Vulkan PNG and difference PNG. Wheel spokes, entrances, fences, grass, cliff texture and background agree; the difference image is black. The matching raw indexed SHA-256 is `8e8d0ffb170de0ebaf9f5ac586bfc66ae2e28e69d84be5090a0db7fd7678c6e5`; matching opaque RGBA SHA-256 is `b032f4c1833faa73b7e6dff49e4b1cf18c2c0ce141c4e634ed007010bdaa0a59`. This closes `ORACLE-001-background-input` by aligning viewport inputs. No render exception, pixel mask or tolerance was accepted. The ordinary opaque-background fixtures remain separate and continue to require exact software/Vulkan equality.
