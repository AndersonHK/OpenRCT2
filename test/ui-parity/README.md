# Actual UI parity fixtures

This standalone harness drives the real UI painter and display engine. The rendering scripts build it against the frozen software source and the current source in separate directories. The root runner records source, assets, shaders, executable and screenshot receipts. Do not substitute an auxiliary drawing context for the main UI capture.

## Weather fixtures

`--fixture weather-precipitation` captures eight literal names in order:

1. `weather-clear`
2. `weather-rain-light`
3. `weather-rain-heavy`
4. `weather-rain-occluded`
5. `weather-rain-restored`
6. `weather-snow-light`
7. `weather-snow-heavy`
8. `weather-cleared`

`--fixture weather-palette` captures six literal names:

1. `palette-clear`
2. `palette-gloom-1`
3. `palette-gloom-2`
4. `palette-lightning`
5. `palette-lightning-recovered`
6. `palette-restored`

Both begin with two clear warmup paints. Their first capture is paint ordinal 3. They use a paused park at its loaded tick, full invalidation on every capture, no event loop or simulation update, and LightFX disabled throughout startup. Public `Weather::forceWeather` selects each state; the next state is explicitly made equal to the current state so an unrelated scheduled snow transition cannot select the snow drawer. The scenario RNG receives a fixed seed before the first weather change. Weather updates and stochastic lightning scheduling are deliberately not called.

The precipitation family enables weather effects and disables gloom. Opening and closing a Research window exercises the production weather clipping path; closing it must restore the preceding heavy-rain output exactly. The palette family disables precipitation and enables gloom. Setting the public lightning flag to 1 before its named paint exercises normal palette update: that paint advances it to 2, and the following paint restores it to 0. No extra palette update is inserted between paints.

Each report has a renderer-independent `weather` object containing `beforePaint`, `afterPaint` and `paintOrdinal`. Each state records current/next weather traits, timer, scenario RNG, tick, night factor, palette animation frame, lightning flag, weather/gloom/LightFX configuration, snow selection, native palette hashes, climate/water object identifiers and water palette sprite IDs. `inputState.weatherBeforePaint` also preserves the pre-paint state alongside the real window census.

Vulkan reports retain the existing `commandCoverage.weather` count. The harness rejects missing precipitation commands, unexpected commands on clear or palette-only frames, and enabled LightFX. On the unchanged viewport, heavy rain must issue twice the light-rain passes, and snow must have the corresponding rain pass counts. Visible changes and clear/restored equality are checked against the actual indexed canvas and physical RGBA output. Lightning must preserve the gloom-2 indices while changing physical colours; the following paint must restore both layers exactly.

Each process captures its sequence once. Determinism requires a separate fresh process with the same executable/build receipt and comparison of matching named ordinals. Adjacent frames are different prescribed inputs and are not repeat samples. Frozen/current software and Vulkan comparisons require exact metadata, indices and physical RGBA; there is no tolerance or accepted weather exception.

This first scope does not qualify natural stochastic weather progression, startup-enabled LightFX/weather adaptation, weather plus native terrain, all camera/zoom combinations, or visible water animation. The small park has no visible water. A successful palette hash comparison does not establish water-wave or sparkle coverage.


## Physical scaling of actual font UI

The first scaling slice reuses the exact seven-state `font-ttf-arial-hinted` and `font-sprite-fr` families and their unchanged text/font inputs. Other qualified font families also support the option, but do not gain evidence until actually captured and reviewed. `--window-scale` defaults to 1 and leaves existing metadata unchanged. Non-unit scaling adds an explicit common `scaling` object.

| Window scale | Required physical window/capture | Required logical canvas | Production filter |
| --- | --- | --- | --- |
| 1.25 | 1200x800 | 960x640 | Smooth nearest, linear final stage |
| 1.5 | 1440x960 | 960x640 | Smooth nearest, linear final stage |
| 2 | 1920x1280 | 960x640 | Nearest neighbour |

Pass `--window-scale`, `--width` and `--height` to the runner together with `--compare-unscaled-run` pointing to a passing scale-1 capture of the identical family. The driver sets actual `Config.general.windowScale`; it never rescales a captured image to impersonate display output. Actual UiContext selects the filter from this scale. The driver requires the realized logical canvas and production quality, then checks captured physical dimensions against the original request. Window-manager clamping or unexpected DPI therefore fails admission.

The runner requires identical logical index bytes and common scene/font/input metadata against the unscaled reference. Integer 2x must equal exact nearest pixel expansion of that reference's physical output. Fractional output must differ from nearest expansion; new physical-colour counts are recorded as supplemental evidence, not a required assumption about font antialiasing. This expansion is a non-vacuity probe, never a replacement for the real frozen software display oracle. Candidate/current/frozen pixel comparison uses actual physical output and exact matching scaling metadata. Every sequence also needs a separate same-build fresh-process repeat at the same scale, with matching capture ordinals.

No arbitrary main-UI linear-only mode is added: production offers automatic nearest or smooth-nearest policy here. The existing auxiliary scaling suite retains separate linear-only coverage. This slice does not qualify enlarged-controls configuration, dynamic resize/DPI changes, fractional logical truncation phases, all fonts, or mixed scaling with lighting/weather.
