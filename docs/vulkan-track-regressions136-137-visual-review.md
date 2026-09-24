# Build136/137 curated track visual review

Reviewed saved original-art comparison images only. No game, build, test binary, or GPU work was executed for this review. The reviewed candidate is the constant component-depth implementation; these findings do not qualify all track styles or complete park rendering.

## Coverage

The three completed lanes contain 64 specimens × four rotations × zooms 0/1: 1,536 groups. All 399 unique changed reference/131/136 triples were manually inspected in 62 sheets split into 124 native-resolution half-sheets. Zoom-zero pixels were displayed 1:1; zoom-one crops were enlarged 2× with nearest-neighbor sampling. Full specimen rectangles were retained, including neighboring specimens. No difference mask or tolerance hid failures.

| Lane | Groups | Identical to reviewed 131 | Changed | Changed triples reused from another lane | Unique changed triples viewed |
|---|---:|---:|---:|---:|---:|
| transparent | 512 | 330 | 182 | 0 | 182 |
| opaque | 512 | 330 | 182 | 147 | 35 |
| inside | 512 | 330 | 182 | 0 | 182 |

The 990 unchanged groups reuse the completed build131 review at `obj/vulkan-parity/track-regressions131-review/vulkan-track-regressions131-visual-review.md`. Cross-mode reuse requires the complete old/new PNG pair to match, including its reference panel. `comparison.json` lists every group and reuse decision; `pages.json` lists every newly inspected sheet. All three build137 lanes were subsequently compared by SHA-256 against build136, with matching case/specimen/rectangle/object metadata: **1,536/1,536 PNGs identical; zero changed groups**. `build137-identity.json` records both hashes for each group.

## Findings

1. **Water-splash rails remain incorrectly occluded.** Specimens 61–63 (styles 9/10/79, type117) lose substantial near rails, fences and the entry/exit ramp artwork beneath the water rectangle in all 72 combinations of mode, rotation and zoom. Build136 reveals a few isolated rail fragments compared with131, but still resembles an open rectangular trough where the upstream reference has continuous track and end ramps. This is an existing physical-depth regression, not accepted skirt variation. Examples: `native/transparent-objects-r0-z0-04-right.png`, `native/opaque-objects-r3-z0-00-left.png`, and `native/inside-objects-r3-z0-03-right.png`. The same splash may appear in adjacent specimen rectangles; those repetitions are not additional failed families.

2. **Station/platform versus entrance-building order remains wrong.** Specimens33/34 (styles53/67, type1) regain visible near white fences compared with131. However, the rail/platform deck still draws across building walls where the original building hides it; this is particularly clear in rotation1 and3. Missing support legs and scrolling text are separate existing omissions and do not explain the deck overlap. Examples: `native/inside-objects-r1-z0-02-right.png`, `native/inside-objects-r3-z0-01-right.png`, with normal-mode equivalents. This review does not waive the remaining overlap because another fence improved.

3. **No additional broad isolated rail-body omission was identified in the changed groups.** Ordinary curves, banks, helixes, diagonals and chained sections retain their visible body shapes, orientation and palette relative to131. This is a bounded visual observation, not pixel parity: thin edges and contact pixels still differ, support structures are extensively absent, and the unchanged groups inherit the prior review limitations. The grid remains visible in inside mode; no new wholesale black interior or erased-track failure was observed.

4. **Background/skirt differences remain visible and counted.** Native boundary skirts and their underground appearance are covered by the owner’s explicit acceptance. This exception does not cover splash masks, station overlap, missing supports, missing text or other ordering defects. These curated scenes do not exercise the pathological buried Cinema case.

## Exact comparison status

All lanes remain exact failures against upstream. The following totals sum `differentIndexedPixels` over specimen rectangles (rectangles can overlap and therefore these are not unique full-frame pixel counts). No exclusion or tolerance was applied.

| Lane | Build136 | Build137 |
|---|---:|---:|
| transparent | 1,511,628 | 1,511,628 |
| opaque | 1,508,052 | 1,508,052 |
| inside | 1,848,757 | 1,848,757 |

## Immutable evidence

Paths below are relative to the repository. Source captures and original comparison receipts were not modified. Analytical sheets are under `obj/vulkan-parity/track-regressions136-review/native/`.

| Artifact | SHA-256 |
|---|---|
| `obj/vulkan-parity/track-regressions136-specimens-01/summary.json` | `f88c4de4c2da8b6b363a7df4ed1c1e3d80c12b21195d74f55d687b83f6a99677` |
| `obj/vulkan-parity/track-regressions137-specimens-01/summary.json` | `6a303ba9c0dc68fe08ee7d2e385c965c2e01ccfab5797a7c4aec302cb0e2f3b1` |
| `obj/vulkan-parity/track-regressions-opaque136-specimens-01/summary.json` | `cd7579e55b2b1f49d1a0a7603b648b2bdd2b8cf2f47dce40d8c1a3efc7c3ac16` |
| `obj/vulkan-parity/track-regressions-opaque137-specimens-01/summary.json` | `65130547e6a63a35ad9a7990ba93205409b1123a6bed25e950e8f6d1620f13fe` |
| `obj/vulkan-parity/track-regressions-inside136-specimens-01/summary.json` | `1dca267ffa9f95cc9486ea257cccbe8996b4d94f3ab4791f105a5a43d3a56146` |
| `obj/vulkan-parity/track-regressions-inside137-specimens-01/summary.json` | `2c9d6c23ad46090f5d198c941fb251d710fe36169e84575b84f692ff635f19b4` |
| `obj/vulkan-parity/track-regressions136-review/comparison.json` | `f8052c94a57923a44d6c1e86877a5a4cc9f8c490fec55e435010e61852a449ce` |
| `obj/vulkan-parity/track-regressions136-review/pages.json` | `334dbe765070236218191365841c15abbdfd3f07f17161ffaac2da43d459ad84` |
| `obj/vulkan-parity/track-regressions136-review/build137-identity.json` | `d03a1c6c2d4bd41c4ee747e324309e310728e1ece7b3e53b0a9b01bf646f0b23` |

The build137 identity result transfers only this track-corpus review. It does not verify the separately changed tunnel-portal anchors or named flat-ride offsets, and it does not turn the deployment into full original-renderer parity.
