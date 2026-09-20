# Actual main UI weather and palette visual qualification

Reviewed by `/root/decode_assets` on 2026-09-19. Fixture version 1 passes exact indexed and physical RGBA comparison for the bounded sequences below. No renderer correction, pixel tolerance or exception was needed.

The independent reference is the frozen software UI executable from frozen UI build 11. Current software and Vulkan use UI build 13; the Vulkan shader receipt is build 28. Runs are `obj/vulkan-parity/ui-{frozen,current,vulkan}-weather-{precipitation,palette}-{01,02}`. Every lane has a separate fresh-process repeat: run 02 compares matching named ordinals against that lane's run 01 with the same executable/build receipt. Current and Vulkan run 01 compare to frozen run 01. All 12 run summaries pass. The root runner reports Vulkan validation clean.

I independently reread all 84 captured frames and verified every indexed byte and physical RGBA byte against the corresponding frozen run 01 frame. All common weather and window input metadata also match. These are not adjacent-frame repeat comparisons: each process paints a prescribed sequence once, after two clear warmup paints.

## Manual inspection

I viewed all 14 named states as lossless contact sheets with native 960×640 panels: physical reference, candidate and diff above indexed reference, candidate and diff. This covers 84 original PNGs. All reference/candidate pairs are visually identical; every diff is uniformly black. The [JSON review receipt](vulkan-main-ui-weather-review.json) records the original PNGs and sheet SHA-256 hashes, all 12 summary receipts, every raw capture hash, metadata hashes and per-frame weather state. Sheets are in `obj/vulkan-parity/weather-manual-review/`.

| Named state | Observed result | Vulkan weather commands |
| --- | --- | ---: |
| `weather-clear` | Clear viewport, intact UI and park | 0 |
| `weather-rain-light` | Sparse rain streaks across the viewport; toolbar areas remain excluded | 6 |
| `weather-rain-heavy` | Denser rain with the same scene and clipping | 12 |
| `weather-rain-occluded` | Research window covers part of the scene; rain stops at its edges and does not paint over its contents | 24 |
| `weather-rain-restored` | Closing Research restores the preceding heavy-rain image exactly | 12 |
| `weather-snow-light` | Bright snow pattern differs from rain; UI and scene edges remain aligned | 6 |
| `weather-snow-heavy` | Denser bright snow pattern across the viewport | 12 |
| `weather-cleared` | Precipitation disappears and both output layers restore the clear baseline exactly | 0 |
| `palette-clear` | Clear baseline | 0 |
| `palette-gloom-1` | First gloom shade on park terrain and sprites, no precipitation | 0 |
| `palette-gloom-2` | Stronger darkening of terrain and sprites | 0 |
| `palette-lightning` | Scene, background and UI colours visibly brighten; indexed image remains identical to gloom 2 | 0 |
| `palette-lightning-recovered` | Both output layers restore gloom 2 exactly | 0 |
| `palette-restored` | Both layers restore the clear baseline exactly | 0 |

The increased occluded command count is expected: production UI weather clipping splits visible rectangles around the new window. Heavy rain issues twice the light-rain passes on the unchanged viewport. Light/heavy snow issue the corresponding rain counts. The Vulkan harness fails if precipitation emits no commands, if clear/palette-only frames emit weather commands, or if LightFX is enabled.

Numeric checks confirm these effects are present in the scene, beyond changes to the toolbar weather icon. Excluding the top 28 and bottom 32 rows, clear-to-light-rain changes 3,233 indexed pixels; light-to-heavy rain changes 3,280; the rain-to-light-snow transition changes 9,744. Clear-to-gloom-1 changes 249,246 indexed pixels in that interior, and gloom-1-to-gloom-2 changes 239,965. Lightning changes physical colours while preserving every index.

## Exact inputs and limits

The park is `small_park_with_ferris_wheel.sv6`, camera rotation 0, zoom 0, logical and physical extent 960×640, loaded tick 1215, paused. Each capture fully invalidates the screen. The public weather setter selects traits and the next state is made equal to the current state; the seeded scenario RNG and full traits are logged. No simulation or weather update runs between captures. LightFX stays disabled throughout startup and capture. The public lightning flag advances 1→2 on the flash paint, then 2→0 on recovery through ordinary palette update.

Loaded objects are `rct2.climate.cool_and_wet` and `rct2.water.wtrcyan`, with water palette images 128194, 128195 and 128198. The display palette FNV-1a-64 hashes are 3621528282574585282 for clear/precipitation, 1866208864008416821 for gloom 1, 6215128908824923765 for gloom 2/recovery and 7051952898255628084 during lightning. Full before/after palette and input hashes are in the JSON receipt.

This qualifies the named paused main UI sequences on the recorded system. It does not close natural unpaused weather progression, stochastic lightning scheduling, other animation ticks, weather with LightFX, startup-enabled lighting adaptation, weather with native terrain admission, other cameras/zooms/scales/windows/GPUs, or visible water-wave and sparkle animation. This small park has no visible water; matching water palette metadata does not establish visible water coverage. Those remain separate checklist work.
