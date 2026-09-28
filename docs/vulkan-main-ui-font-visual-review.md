# Actual UI font visual review

Agent `/root/decode_assets`, 2026-09-19. Both bounded font families pass all seven states across frozen software, current software and Vulkan. Independent same-build fresh processes also pass: **84 captures across 12 processes**, exact in indexed bytes and physical RGBA. All four Vulkan runs confirm active validation with no diagnostics. [The JSON receipt](vulkan-main-ui-font-review.json) pins summaries, builds, outputs, metadata and all manually viewed images.

## Inputs and failed first attempt

`font-ttf-arial-hinted` uses Vietnamese (`vi-VN`) with the installed Arial file SHA-256 `c9b76220a5be42ead4733611e417cd65c5fd8aeaa33eb56576ac378a37d130a1`. All three font styles load face 0 at 12 points, line height 14, offset 0/-1, hinting enabled and threshold 40. Actual selected fonts and cmap support are checked. The sample is `AVATAR Café Việt Nam Đường vào ✓ → ` repeated four times: 188 UTF-8 bytes. The tick uses the explicit sprite-symbol branch; the arrow is an actual Arial TTF glyph.

First frozen run `ui-frozen-font-ttf-arial-hinted-01` failed before any capture because the fixture misclassified U+2192 as a sprite-forced symbol. Production `UnicodeChar::right` denotes U+25B6. The harness-only predicate correction retained the exact text, Arial requirements and sprite tick check. The failed run remains preserved; there was no renderer change, visual mismatch or exception.

`font-sprite-fr` uses French (`fr-FR`) and actual sprite mode, with no selected TTF styles or Vulkan TTF commands. Its 156-byte sample `Café été Straße Œuf — Жук Я ` repeats four times. Runtime support and sprite offsets are recorded, including accented Latin, ligature and Cyrillic glyphs.

The first passing TTF group uses frozen UI 14/run 02 and current/Vulkan UI 18/run 02 with build 32 shaders. French uses frozen UI 13/run 01 and current/Vulkan UI 18/run 02. Run 03 repeats each renderer with its identical first passing binary: French frozen 03 specifically retains UI 13. All 84 captures explicitly assert loaded RCT1 CSG. Complete common font/input metadata and both raw pixel layers were independently reread and checked against the first frozen reference; fresh captures additionally match their own renderer's first process.

## Manual inspection and non-vacuity

I viewed 14 lossless sheets containing 112 source images: each state's frozen, current software, Vulkan and difference images at physical and indexed layers. Sheets preserve each 960×640 panel without resampling under `obj/vulkan-parity/font-manual-review/`.

- Empty/restored states show matching localized status panels and the same small park; restoration is byte-exact in both layers.
- The Research window shows Vietnamese TTF labels or French sprite labels, with matching title, colour, placement and window overlap.
- The real input dialog wraps Vietnamese text into five lines and sprite text into four. Accents, the sprite tick/TTF arrow, French ligature and Cyrillic samples agree in all lanes. The fixed English description clips equally at the dialog edge; this is preserved reference behavior, not an exception.
- The visible caret moves from byte 0 to 12 to 188 for the TTF sample, and 0 to 5 to 156 for the sprite sample. Every position is a validated UTF-8 codepoint boundary. Pixel changes are required at each transition.
- Moving the dialog to x=-24 clips its left edge consistently; text, buttons and remaining window contents match, and all difference panels are uniformly black.

Actual Vulkan TTF opaque/transparent command counts by state are `0/4, 4/13, 12/30, 12/30, 12/30, 8/24, 0/4`. The four dialog states each include **seven threshold 40 TTF commands intersecting the actual text-input bounds**, so title-only text cannot satisfy admission. French has 0/0 throughout. Metadata records four versus three wrapping breaks, exact language-file hashes, glyph support, text width and wrapped bytes. The runner requires metadata equality, changed pixels for each active transition and exact restored pixels. No adjacent paint is called a repeat: the seven steps are fixed ordinals 3 through 9, and repeat qualification comes from separate matching processes.

## Limits

This closes only these two profiles on the recorded Windows device at scale 1 and 960×640, with full invalidation and a fixed park state. It does not close all V06 text/UI coverage. Other fonts/languages, font-family fallback, missing-glyph behavior, unhinted output, enlarged/fractional controls, cache eviction/reload, IME, complex shaping and RTL remain open. No generalized typography-correctness claim follows from matching the frozen implementation.
