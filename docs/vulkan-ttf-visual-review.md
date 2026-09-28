# TTF bitmap parity review

Reviewer: `decode_assets` agent, 2026-09-19. These fixtures compare the frozen X8 `DrawTTFBitmap` implementation against Vulkan using identical synthetic, immutable 16×7 coverage surfaces. They exercise coverage values around the hinting threshold and the hard-coded solid threshold 180; outlines, inset, clipping, overlapping draw order, and cache identity replacement. Indexed and final opaque RGBA comparisons use zero tolerance. This is a post-font-rasterization test: actual font selection, shaping, library rasterization and higher-level zoomed text layout remain unqualified. The direct frozen bitmap contract requires zoom zero.

## Run 11: invalid remap input identified

All **54 PNGs** were opened individually: software, Vulkan and diff for both indexed and RGBA layers of all nine fixtures. Both layers show the same pixel locations. The fixture accidentally supplied an all-zero remap table. Vulkan's transparent composition requires row zero to be the identity mapping, including when a pixel has no transparent command. The invalid table erased the background stripes, opaque threshold-zero text and outline/inset pixels; repeated compositions in the ordering case erased the entire image. This is a fixture input defect, not a permitted rendering exception.

| Fixture | Different pixels, each layer | Visual observation |
| --- | ---: | --- |
| TtfThresholds | 7,232 | Reference vertical stripes and top text disappear; surviving hinted text lies over uniform index-zero green. |
| TtfOutline | 7,232 | Reference cyan outlines disappear with the stripes; remaining text resembles the unoutlined Vulkan case. |
| TtfInset | 7,232 | Reference offset cyan shadow disappears with the background. |
| TtfOutlineInset | 7,232 | Both reference outline and offset shadow disappear; broad diff surrounds retained glyph fragments. |
| TtfClipping | 7,156 | Cropped fragments remain at the expected right-hand positions, but stripes, outline and opaque fragments disappear. |
| TtfOrdering | 7,680 | Vulkan is entirely index-zero green, while reference retains all stripes, text and overlap detail. |
| TtfCacheReplacement_F0 | 7,232 | Same loss as Thresholds; initial bitmap pattern is recognizable in retained hinted text. |
| TtfCacheReplacement_F1 | 7,232 | Replacement bitmap pattern changes as expected, with the same background/opaque loss. |
| TtfCacheReplacement_F2 | 7,232 | Cache-hit result matches F1 visually, including the same defect. |

Every report has full-image inclusive bounds `(0,0)-(95,79)`. [The review receipt](vulkan-ttf-run11-review.json) records SHA-256 for every inspected image. Artifacts remain under `obj/vulkan-parity/run-11/samples/Ttf*/{indexed,rgba}`.

Fixture version 2 initializes remap row zero to identity and records that input explicitly. No production code changed for this correction. A fresh run is required before attributing remaining differences to the renderer. Source inspection separately identifies a possible mismatch for thresholds above 180: software treats coverage above 180 as solid before testing the threshold, whereas Vulkan currently tests the threshold first. This is a hypothesis awaiting the corrected-input screenshots, not a closed defect or exception.

## Run 12: solid coverage threshold defect reproduced

All 54 corrected-input PNGs were opened individually again. All nine fixtures have exactly **70 differing pixels in each layer**, inclusive bounds `(4,51)-(43,69)`. The background stripes now match. Outline, inset, clipped right-hand fragments and overlapping draw-order samples also match outside those bounds. The diff consists solely of missing solid glyph fragments in the threshold-181 and threshold-255 rows. Cache replacement moves these fragments with the new coverage pattern; F1 and F2 remain identical. [The run 12 review receipt](vulkan-ttf-run12-review.json) records every inspected PNG hash.

The production cause is confirmed: `indexed_transparent_rect.frag` and the duplicated TTF branch in `indexed_rect.frag` first suppress colour if coverage does not exceed the hinting threshold, then retain the solid marker. Frozen X8 writes full foreground colour whenever coverage exceeds 180, independently of the hinting threshold. The Vulkan branch must first retain solid pixels, discarding only non-solid coverage that does not exceed the hinting threshold. No software change or exception is appropriate. Production edits await the coordinated source-build freeze release.

The next test-only case `TtfZeroInk` exercises the same direct draw contract with palette index zero for fill and outline/inset. Zero is a valid frozen X8 output index; current Vulkan shader discard/transparent encoding may not represent it correctly. This case remains an unmeasured extension until its first build/run and visual inspection. It does not establish that normal font-colour selection currently emits index zero.

## Run 13: covered index-zero text reproduced

All six `TtfZeroInk` reference/Vulkan/diff PNGs were opened individually. Both layers differ in **1,604 pixels**, inclusive bounds `(3,2)-(44,70)`. Frozen X8 produces filled index-zero glyph blocks with outlines/inset; Vulkan drops their opaque zero pixels and leaves only sparse incorrectly encoded hinted fragments. Background outside glyph coverage remains correct. Maximum RGBA error is `(57,57,237,0)`. [The zero-ink review receipt](vulkan-ttf-run13-zero-review.json) preserves all six image hashes. This is a valid direct bitmap API contract even though ordinary UI colour selection emitting index zero has not been established.

After both defects were measured and manually inspected, a narrow shader correction was implemented: retain solid coverage before threshold testing, allow covered opaque TTF index-zero output after source coverage checking, and encode hinted zero ink as `0x0102` (blend) or `0x0103` (solid). These two values cannot collide with filter rows `0x0000..0x00ff` or ordinary hinted text, whose low byte is zero or one. The composition shader decodes them explicitly; attachment formats and memory use remain unchanged. Frozen software remains untouched. A compiled rerun and corrected-sample manual review are still required; no exception is accepted for either defect.

## Run 14: exact correction verified

Build 18/run 14 passed all 116 required tests with zero synchronization validation errors. All **20 TTF reports** (ten sample frames, indexed and RGBA) have zero differing pixels. The reviewer opened all **60 corrected PNGs** individually: each candidate matches the software reference; every difference image is black. Threshold-181/255 solid fragments are restored; outlines, inset, nested clips and overlapping text retain their exact reference ordering. The zero-ink glyph blocks, outlines and inset now match. Cache F1 and F2 show identical replacement coverage, distinct from F0, with expected upload counts 1/1/0.

[The corrected review receipt](vulkan-ttf-run14-review.json) preserves SHA-256 for every reviewed PNG. TTF-002 and TTF-003 are closed as fixed Vulkan defects, with no accepted software exception. This closes the measured direct-bitmap cases only; the font-loading/shaping/rasterization, wider zoomed layout, cache eviction pressure and main UI limits above remain open.
