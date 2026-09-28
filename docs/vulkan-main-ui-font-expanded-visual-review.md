# Expanded actual UI font visual review

Agent `/root/decode_assets`, 2026-09-19. Four additional font profiles pass **168 captures across 24 processes**: seven states, three renderers, and two independent processes per renderer. Indexed bytes and physical RGBA match the frozen software reference exactly. All eight Vulkan processes confirm active validation without diagnostics. The [separate JSON receipt](vulkan-main-ui-font-expanded-review.json) pins all summaries, builds, raw outputs, font metadata and manually inspected images. Together with the [first two profiles](vulkan-main-ui-font-visual-review.md), the bounded font evidence totals **252 captures across 36 processes and six families**.

The new profiles use frozen UI build 16 and current/Vulkan UI build 21 with build 33 shaders. Each `ui-{frozen,current,vulkan}-{family}-01` first process passes; each `-02` repeats its own renderer with the identical build receipt. All 168 captures assert loaded RCT1 CSG. The audit reread every raw image and report hash, checked both full-frame pixel buffers directly against frozen `-01`, verified complete common font/input metadata, reran the font admission checks and checked build/reference receipts. Build 33/full run 28 separately passes 733 tests with synchronization validation; those tests do not broaden this font fixture's visual scope.

## Profiles and actual route evidence

| Family | Loaded profile | Text and dialog evidence |
| --- | --- | --- |
| `font-ttf-arial-unhinted` | Vietnamese, Arial face 0, 12 points, line height 14, offset 0/-1; actual hinting disabled, effective packet threshold 0 | 188 UTF-8 bytes, width 901, five wrapped lines; seven threshold-0 TTF commands intersect each input-dialog state. Vulkan commands are entirely opaque. |
| `font-sprite-ru` | Russian sprite mode, no selected TTF styles | Accented Latin, ligature and Cyrillic sample: 156 bytes, width 620, four lines; zero TTF commands throughout. Research and status labels are Russian. |
| `font-ttf-ja` | Japanese, MS Gothic TTC face 0, 12 points, line height 14, offset 0/-1; hinting enabled, threshold 60 | 204 bytes, width 852, four lines; eight threshold-60 TTF commands intersect each dialog state. Japanese ideographs/kana, sprite tick and TTF arrow are visible. |
| `font-family-fallback-vi` | Vietnamese; deliberately missing custom file/family and absent Arial Unicode file force the existing family fallback to Arial. Small/medium: 12 points, height 12; tiny: 10 points, height 9; threshold 40 | 188 bytes, width 901, five lines; seven threshold-40 commands intersect each dialog state. Runtime records both required absent paths and all three actually selected styles. |

Arial is pinned to SHA-256 `c9b76220a5be42ead4733611e417cd65c5fd8aeaa33eb56576ac378a37d130a1`; MS Gothic is `4bde3e6392b96910fb59094c6c1a4dbfae18fee78d0bf13dc30616837c4f95db`. Runtime cmap checks confirm every ordinary sample glyph; U+2713 deliberately follows the production sprite-symbol path. The TTC face assertion derives from the pinned bytes and production `TTF_OpenFont` face-0 behavior, rather than private font-library state. Full per-language resource hashes, requested/selected configuration and codepoint boundaries are retained in the receipt.

## Manual inspection

I viewed all 28 state sheets, containing 224 source panels: frozen software, current software, Vulkan and the Vulkan difference image, each at physical and indexed layers. The sheets under `obj/vulkan-parity/font-expanded-manual-review/` use native pixels without resampling. Empty and restored states show the complete 960×640 frame; the five active states show the UI region `[0,95,640,430]`, covering the Research window, input dialog and screen-edge clip. Full-frame byte comparisons remain separate from this explicitly bounded visual crop review.

The localized window titles, body labels, wrapping, accents, ideographs, kana, Cyrillic glyphs, tick/arrow and buttons agree in all lanes. Unhinted Arial shows the expected harder pixel edges. MS Gothic uses visibly different monospaced glyphs and wraps the Japanese sample into four lines. The fallback actually uses Arial and its built-in line-height configuration; it is not admitted merely because a dialog exists. All difference panels are black. The left-clipped dialog retains matching text and controls; closing restores the exact initial frame.

Caret start/middle/end offsets are validated UTF-8 boundaries: 0/12/188 for both Vietnamese profiles, 0/5/156 for Russian sprite text, and 0/9/204 for Japanese. Actual pixels must change on every active transition. Fixed paint ordinals 3–9 are a sequence, while repeatability is established by separate same-build processes. All 12 independent repeats match their first process and, by full-buffer comparison, the frozen first process.

No renderer change, tolerance, mask or accepted exception was needed for these profiles. The earlier first-profile setup failure remains preserved in its original review. Redundant builds UI 20/frozen 15 predate application of the expanded harness and are not evidence for these new profiles.

## Remaining scope

This qualifies only the pinned Windows font bytes and profiles at scale 1, 960×640, one device, a paused small park and full invalidation. General font/language coverage, missing-glyph behavior, font cache eviction/reload, enlarged or fractional controls, IME, RTL and complex shaping remain open. Matching frozen text does not establish generalized typography correctness or complete V06 parity.
