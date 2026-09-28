# Actual main UI physical-scale visual review

Agent `/root/decode_assets`, 2026-09-19. The two font profiles at three physical scales pass **252 captures across 36 processes**: seven states, three rendering lanes and two separate processes per lane. Every logical indexed buffer and final physical RGBA buffer matches the frozen software reference exactly. All twelve Vulkan processes confirm active validation with no VUID or synchronization diagnostics. The [JSON receipt](vulkan-main-ui-scale-review.json) pins all summaries, builds, raw buffers, common inputs, font resources and manually inspected images.

The reference uses frozen UI build 17; current software and Vulkan use UI build 23, with build 35 shaders for Vulkan. The run names are `ui-{frozen,current,vulkan}-{font-ttf-arial-hinted,font-sprite-fr}-scale{125,150,200}-{01,02}`. All captures assert that RCT1 CSG was loaded. The executed runner is preserved in `obj/vulkan-parity/ui-scale-balloon-combined-staged/candidate/scripts/rendering/run-ui-parity.py`, SHA-256 `7678cdbbe63aa34942544fa76c994e7bd4e26a0cc8815cae34c0cac4e84c4656`. Subsequent live runner changes to balloon receipt admission are not attributed to these executions.

## Realized scaling and non-vacuity

| Requested scale | Required and realized physical extent | Logical extent | Production quality |
| --- | --- | --- | --- |
| 1.25 | 1200×800 | 960×640 | Smooth nearest, including the linear final stage |
| 1.5 | 1440×960 | 960×640 | Smooth nearest, including the linear final stage |
| 2 | 1920×1280 | 960×640 | Nearest neighbour |

These exercise the actual `UiContext` configuration policy and physical presentation surfaces. The harness rejects clamped or unexpected dimensions. The logical images match the corresponding unscaled references exactly. At 2×, every physical image also matches the exact nearest-neighbour expansion of its actual unscaled output. At fractional scales, every full physical image differs from nearest expansion; the frozen software output itself remains the pixel oracle. Across the seven states, the fractional comparisons differ from nearest expansion by 249,104–287,507 pixels for Arial at 1.25, 225,435–261,565 at 1.5, 247,781–286,952 for French at 1.25, and 223,813–261,059 at 1.5. The supplemental new-colour counts are recorded in the receipt.

The font bytes, language resources, codepoint coverage and configuration remain identical to the [first font profiles](vulkan-main-ui-font-visual-review.md). Vietnamese Arial is genuinely loaded, with hinting enabled and threshold 40; seven TTF commands intersect each input-dialog state. The French sprite profile emits no TTF commands. Runtime font admission checks were rerun for every process. The same 188-byte Vietnamese and 156-byte sprite samples retain their five-line and four-line wrapping and their validated caret offsets.

## Manual and independent raw review

I viewed all **42 state sheets** in `obj/vulkan-parity/scale-threeway-manual-review/`: frozen software, current software, Vulkan and Vulkan difference panels at physical and indexed layers. The seven states are empty UI, localized Research window, caret start, caret middle, caret end, screen-edge clipping and restored UI. Empty/restored sheets show full frames; active states show logical crop `[0,95,640,430]` and its physical-scale equivalent. Sheet generation performs no resampling. Because the viewer resized the very wide 2× full-frame sheets, I additionally inspected sixteen individual native-sized lane panels for both profiles' empty and restored states.

Fractional edges are visibly softened in the same way in all three lanes; 2× edges retain crisp pixel blocks. Localized labels, accented glyphs, tick/arrow symbols, text wrapping, caret positions, dialog borders, buttons and left-screen clipping agree. Difference panels are black at both layers. The restored state has no leftover dialog pixels and exactly matches its initial frame.

An independent audit reread every raw buffer and report, verified their recorded SHA-256 values, directly compared both complete buffers from all 252 captures to the matching frozen first process, checked common font/input/scale metadata, verified every build and comparison-reference receipt, reran font admission and recomputed the 42 frozen scale-versus-unscaled checks. The full-frame comparisons cover pixels outside the manual active-state crop. Fresh-process outputs are byte-identical to the manually viewed first outputs.

The second-process launch commands omitted the runner's `--fresh-repeat` flag, so their original summaries still say that a fresh-process repeat is required. Those summaries are preserved unchanged. The root agent confirmed eighteen separate runner invocations and fresh executable launches. This review independently qualifies those repeats by identical renderer, build and shader receipts; distinct profile/output directories in the recorded commands; pinned references to each lane's own first run; and exact complete-buffer equality. It does not claim that the runner's explicit fresh-repeat admission path was used.

The isolated scale admission probes also pass 63 positive/negative checks, pinned in the receipt. These probes use synthetic data to check rejection behavior; they are not game screenshots or an alternative fractional-rendering oracle.

## Remaining scope

No renderer correction, tolerance, mask or accepted exception was needed. This is bounded evidence for two profiles, three exact scale factors, one paused park, full invalidation, one device and the pinned Windows font bytes. It does not qualify enlarged-control layouts, dynamic resize or DPI transitions, odd physical rounding phases, other font profiles at scale, a forced linear-only main UI policy, or combined scaling with weather/LightFX. Broader V06, presentation and performance gates remain open. Diagnostic readback captures are not a performance result.
