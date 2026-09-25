# Native world text follow-up

The saved matched-asset Everything Park path-gallery crops from `everything143-manual-three/paths-r{0,1,2,3}-detail.png` were manually inspected. The ordinary banner row is readable in upstream r1/r2 and blank in native. Offline park facts identify banner elements at tile x72/y218–242, including banner 9 at tile 72,233 (baseZ 352, object slot 0, flags 0). An empty custom string means the localized default sign, not empty text. The other two camera faces do not establish a missing-text exception.

Reviewed crop SHA-256:

| Rotation | SHA-256 |
| --- | --- |
| 0 | `49c6c0d9d3116fb55e252b31232dcba78c0d0be1a97bf95b01790df8bee66cf9` |
| 1 | `82817c4edc0c856b921b286c004785ab72fb87146a49340f62dfb3ef2b62a264` |
| 2 | `8f93236fc68ad63f5809f013cd134a9b87791ce1e3da1093a545b6816823972c` |
| 3 | `189a1337ac696deeb49e53d3bd46bd1458652e16e4ab6b0f2fd4b3368ff39aac` |

The cause was a shared metadata packing error: absent `pathSurfaceSlot=0xffff` was shifted by 8 without masking, overwriting the low byte of `bannerId`. Banner 9 became 255, so no text descriptor was found. `PackWorldObjectMetadata` now enforces the 8/8/16-bit ownership boundary; its regression covers real low IDs, high IDs, and both null sentinels.

The current source also implements wall and non-3D large scrolling signs through the existing immutable glyph-column stream. Optional initial-ink masks retain element-secondary directional shades and ghost grey while preserving explicit formatting colours and TTF hint samples. The GPU selects face, sequence, zoom, mode, ink and scroll phase. Park entrances use the same stream, with an owned park-name/closed-text descriptor that changes only on name/open/font changes. No per-frame bitmap raster or per-tile text scan is introduced. Source-derived rule tests and comparisons with the original scrolling bitmap are authored but have not been executed by this reviewer.

Floating money has a separate medium-font glyph-run producer. It captures the existing formatter/parser output and owns original glyph pixels or cached TTF coverage only when amount/font/currency changes. Motion/wiggle uses a separate raw snapshot; the GPU moves sprite characters using the original 22-step wave. Original TTF literal runs do not consume that wave cursor. Glyphs use the explicitly reserved after-world/before-UI annotation depth slot. This source has not yet been built or visually qualified here.

Large scenery flagged `is3DText` now has an implemented, unqualified source path. The immutable object catalog owns its original 256 glyph facts, exact glyph allocation, directional baselines, maximum width and vertical/two-line flags. The banner generation owns the original formatter's bounded 256-byte string decoded as codepoints. GPU layout reproduces the original overshooting display-prefix rule, whitespace line split, signed floor division and half-pixel glyph variants, with original attachment draw order and one sibling depth layer. The object's original art remains resident; there is no per-frame text raster or CPU glyph selection. Focused tests cover layout, ownership, allocation rejection and held text generations. Build/device tests and fresh original-art closeups remain required; implementation is not pixel qualification.

## Build160 Everything Park path view

Manually inspected `everything160-matched-art-01/paths-r3/comparison.png` (upstream left/native right), then three paired nearest-neighbour crops covering the entrance row, banner gallery and lower overlapping structures. Visible yellow queue-banner and ride-entrance roof lettering is present and visually aligned; this view does not show the earlier blanket missing-text defect. The ordinary gallery signs expose their backs at rotation3: rotation1/2 remains required to qualify their formerly blank `Sign` faces. There is no identified active construction selection or ghost specimen here, so this image does not qualify those states or animation progression.

The complete image still differs at 12,783 pixels; this is not an exact-parity pass. The three overlapping review rectangles contain 930, 3,290 and 2,751 differing RGBA pixels respectively, including peeps/balloons and track/support intersections. These counts are not text-only classifications or exclusions. Crops, coordinates and immutable source-image hashes are recorded in `obj/vulkan-parity/everything160-text-review/paths-r3-regions.json`.

Build160 does **not** contain the subsequent large-3D glyph sibling-order correction. Source review found that equal-depth opaque glyphs used original last-painted traversal under GPU `LESS`, which instead preserves the first submitted glyph. The subsequent fix reverses submission only, retaining original phrase positions, images, palette and shared child depth. Its new GPU overlap regression covers horizontal/vertical fonts in both visible directions; execution and real object-font visual evidence remain pending.

| Build160 paths-r3 artifact | SHA-256 |
| --- | --- |
| upstream.png | `6375cb5c3ebb7ba7f6011009d5c0171104ac252bd7ffee059b5424942e0e14a7` |
| native.png | `b00dee07718f0f41e9de3dd47b709e88fd6f84f816573fca61e283df0385f5e6` |
| comparison.png | `181d012bb1416d5c488cea54e346917ad0974a912e4bc729a616c5cee17d2c29` |
| difference.png | `6db32823fa588a00335cefb91e7cc627ebca3af4548cd6d08adaad97c8afa67e` |

The saved build160 `glass-r3` full comparison and a 4× nearest-neighbour entrance crop were also inspected. The roof-lettering rectangle `(510,318)-(548,336)` is exactly RGBA-identical. The wider `(440,275)-(605,425)` crop has 433 differing pixels, including peep/vehicle/station overlaps; a native peep is visible through a rear/side station region where upstream hides it. Thus the intact lettering and recognizable pane/frame structure do not establish complete glass-scene parity. Review crop: `obj/vulkan-parity/everything160-text-review/glass-portals.png`. Source native PNG SHA-256 `f69c33b0bde61e4d10933a83f7c3a8fca0222ec29b6034ece68544f6dd8a0fde`; upstream `2d28a0d325b920728d70684f3f5a2ee39700306dae18efbdac445287be1de209`.
