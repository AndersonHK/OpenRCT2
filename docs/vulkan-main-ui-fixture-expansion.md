# Main UI parity fixture expansion

Status: proposed next bounded implementation after the initial frozen-software/current-software/Vulkan main-window comparison. This document is a source review, not evidence that these fixtures have passed. No renderer or harness changes accompany it.

The first four families below exercise real production windows through public APIs. Each family runs in a new process/profile against the same pinned park, assets, language, theme, dimensions and harness source. Keep the existing actual SDL pre-present and named Vulkan swapchain capture mechanisms. Compare the entire physical RGBA image and the logical indexed canvas; neither a crop nor a command count can replace that comparison.

## Shared fixture contract

- Extend the driver with a named fixture selector and a versioned list of capture steps. Root's runner must require every expected step in both repetitions and all three executables. Keep the existing baseline fixture unchanged.
- Initialize and load through the existing standalone UI driver. Pin simulation ticks, pause, weather, palette frame, interpolation, camera, language and theme. No wall-clock update loop, SDL event pumping or simulation ticks between fixture steps. Record window class/number/rectangle, selected tab where public, scroll state, text/caret inputs, viewport and palette state in each report.
- After setup, use the existing fixed paint-only warmups. Drain Vulkan warmups before arming the first named packet. Require named capture success, matching dimensions, exact byte lengths and nonzero UI sprite/rectangle coverage. Record packet frame/version/device metadata.
- Initial implementations use full invalidation for stable composition snapshots. A separately named incremental sequence omits `GfxInvalidateScreen()` after the initial frame and relies on the actual public window operations' invalidations. This distinction prevents a full repaint from concealing damage bugs. Compare each incremental result with the frozen incremental reference and its matching full-invalidation state.
- Repeat the whole operation sequence in a fresh process rather than repainting just its final state. Compare both repetitions byte for byte. Advance only explicitly prescribed per-window update counts (the text-caret fixture below); do not update every window.
- Close only windows opened by the fixture using `IWindowManager::Close`/`CloseByClass`, then public `WindowCullDead()`. `IWindowManager::Cleanup()` closes **all** windows before culling; it is application teardown, not closed-window garbage collection. The first frozen family runs demonstrated that misuse by losing the main viewport at the first fixture close. Reacquire pointers after operations which may close windows. Verify main viewport and toolbars remain. Capture a final baseline restoration and require it equal the family's initial baseline when no intentional state change remains.
- Every divergence retains reference/result/diff PNGs, raw indexed/RGBA bytes, first differing pixel, counts, hashes and input metadata. An agent visually reviews all divergent groups before any implementation correction or exception decision; review corrected samples before closing the gap. Missing coverage is a failure, not a passing skip.

## 1. Overlapping, reordered and moved windows

Use the populated Ferris-wheel park already accepted by the baseline. Call `Ui::Windows::FinancesOpen()` and `ResearchOpen()` from `src/openrct2-ui/windows/Windows.h`. These are ordinary production windows with deterministic park-dependent text and sprite tabs. Avoid About, changelog or network/version windows, whose text can legitimately differ between source versions.

1. Capture `empty-ui` before opening either window.
2. Open finances, move it to `(64, 72)` with `WindowSetPosition`; open research and position it at `(260, 132)`. Capture `research-front`.
3. Use `GetWindowManager()->BringToFront(*finances)` and capture `finances-front`.
4. Move finances to `(-24, 96)` through `WindowSetPosition` and capture `partially-clipped`. The negative origin deliberately exercises outer-window and screen clipping, without changing any private widget geometry.
5. Close finances and call `WindowCullDead()`, capture `research-only`; close research, call `WindowCullDead()` and capture `restored`.

Assertions: both windows exist with the requested public rectangles; their intersection has positive area; the offscreen case intersects the screen but extends past its left edge. Choose an interior non-caption point in the intersection and assert `IWindowManager::FindFromPoint` identifies the intended front window after each reorder. Compare successive images and require the changed regions to be nonempty, then require restored output to equal `empty-ui`. Include the window rectangles and expected front class in reports. Do not claim transparent-window coverage from this family; their normal backgrounds are opaque.

Relevant APIs: `src/openrct2/ui/WindowManager.h` (`BringToFront`, `FindFromPoint`, `Close`); `src/openrct2/interface/Window.h` (`WindowCullDead`); `src/openrct2-ui/interface/Window.h` (`WindowSetPosition`); implementations in `windows/Finances.cpp` and `windows/Research.cpp`.

## 2. Scrolled, clipped shortcut list

Call `ShortcutKeysOpen()`, which creates a 420-by-280 window with a real overflowing list, group separators, shortcut text and scrollbar. A fresh profile supplies default shortcut bindings. Its list is sorted by registered `orderIndex` in `ShortcutKeys.cpp::initialiseList`; no park population is needed. Do not use the one-ride park's ride list as proof of scrolling.

1. Place the shortcut window at `(90, 80)`. Find its unique public `WidgetType::scroll` widget by traversing `WindowBase::widgets`, deriving its scroll-array index from preceding scroll widgets. Assert exactly one rather than copying the file-private `WIDX_SCROLL` constant.
2. Call `onPrepareDraw()` and `WindowUpdateScrollWidgets()`. Capture `scroll-top` with offset zero; record widget bounds, content size and visible viewport size. Require vertical overflow and `ScrollFlag::vScrollbarVisible`.
3. Set the public `ScrollArea::contentOffsetY` to 37, call `WindowUpdateScrollWidgets`, `widgetScrollUpdateThumbs(window, discoveredWidgetIndex)` and `window.invalidate()`. Require the resulting offset remains 37 and capture `scroll-partial-row`. This uses the generic public scroll model; it does not access ShortcutKeysWindow's private list or highlight fields.
4. Move the window to `(-28, 110)` and capture `scroll-screen-clip`; resize by a fixed positive delta through `WindowResizeByDelta` and capture `scroll-resized`. Record actual resulting size and post-clamp scroll offset.
5. Close and capture `restored`.

Assertions: offset and thumb change from the top capture, scrolled content changes, no step silently clamps the requested offset to zero, resized dimensions change within actual window limits, and clipping remains visible. Preserve the same default tab and do not invoke shortcut reassignment/reset (which would alter profile bindings). First registration can calibrate the exact content height once and pin it in the fixture version; if a later source version changes the registered list, report input divergence instead of treating it as a renderer exception.

Relevant APIs: `src/openrct2/interface/ScrollArea.h`; `WindowUpdateScrollWidgets` in `src/openrct2-ui/interface/Window.cpp`; `widgetScrollUpdateThumbs` in `src/openrct2-ui/interface/Widget.h`; production list behavior in `src/openrct2-ui/windows/ShortcutKeys.cpp`.

## 3. Wrapped text input and visible caret

Use the string-based `WindowTextInputOpen(title, description, initialValue, maxLength, callback, cancelCallback)` overload. It creates the real modal text input, starts its normal text session and computes height from wrapping. Supply fixed English ASCII strings first, a long initial value (for example the sentence `Ferris wheel entrance beside the garden path. ` repeated four times), and maximum length 256. Supply callbacks that only record their invocation and received text in the harness.

1. Open a research window behind the dialog so its clipping/occlusion is covered; open the text input with fixed title and description. Find `WindowClass::textinput` and assert the public text session buffer contains the exact input.
2. Call `SetTextboxCaret` with a specified UTF-8 byte offset, initially zero. Advance only this window's public `onUpdate()` exactly 16 times. Its `_cursorBlink` starts at zero, and `TextInput.cpp::onDraw` draws the caret only when that value exceeds 15. Paint-only warmups do not advance it. Capture `wrapped-caret-start`.
3. Call `SetTextboxCaret(initialValue.size())`, invalidate the dialog and capture `wrapped-caret-end`. Assert the session's public selection offset matches and the two images differ. Record dialog height and require it exceed the empty-string dialog height measured by a separately named control step; this proves wrapping occurred.
4. Call `WindowTextInputKey(dialog, SDLK_RETURN)` to submit through the production public entry point. Require one successful callback containing the exact original text and no remaining textinput window, then capture `submitted`. Close research and capture `restored`.

Do not cast to the private TextInputWindow type, modify its private buffer/blink counter, or hardcode private button indices. Selection highlighting and actual text insertion/IME are later fixtures; the initial family proves real wrapping, caret placement, inset rectangle, glyph drawing and modal composition. Add non-ASCII/TTF variants only with pinned language/font files and explicit font metadata, after the basic dialog repeats exactly.

Relevant APIs: `src/openrct2-ui/windows/Windows.h`; `src/openrct2-ui/interface/Window.h` (`GetTextboxSession`, `SetTextboxCaret`); real behavior in `src/openrct2-ui/windows/TextInput.cpp` (`setText`, `onUpdate`, `onPrepareDraw`, `onDraw`, `WindowTextInputKey`).

## 4. Entity-free main viewport and direct terrain

The existing populated park cannot prove direct terrain: `GpuCommandDrawingContext.cpp::DrawWorldSurfaceScene` rejects any captured entities, landscape smoothing, non-independent map surfaces or unsupported surface assets. The existing default UI driver enables smoothing. A passing image with `worldSurfaces == false` is therefore insufficient.

Prepare a separate pinned input park once, then load identical bytes in every renderer process. Use an isolated preparation context with the existing park's loaded object set and public `gameStateInitAll(getGameState(), TileCoordsXY{32, 32})`. This calls `MapInit`, resets rides/entities/date/weather and establishes default surface elements; do not perform this broad reset on the live capture context. Export with public `ParkFileExporter::Export` from `src/openrct2/park/ParkFile.h`. Prefer the frozen preparer's exporter/format, and prove both executables can load the result before accepting its SHA-256. Record object identifiers/hashes, map dimensions, tile-element-type census and every entity-type count; require surface-only tiles and zero entities. An export made once is an input artifact, not a second renderer reference.

Begin with ordinary flat default terrain. Verify each surface/edge object resolves from the retained object set; do not assume object index zero exists. Save the chosen camera in the artifact manifest. Use `IWindowManager::SetMainView` with that projected position, zoom and rotation; require the returned actual main viewport matches. Disable landscape smoothing on both renderers; clear viewport overlay flags, tool/selection state and inspector selections through normal setup, retain the playing scene, and keep the terrain unobscured for the initial case.

Initial capture matrix: four rotations at zoom 0, plus rotation 0 at zoom 1 with a camera offset of `(1, 3)` from the pinned view. Require `worldSurfaces == true`, nonzero `worldSurfaceRecordCount`, valid world epoch and named packet identity on every Vulkan capture. Require a meaningful nonuniform terrain image and visible map edges so a flat or off-map frame cannot pass as coverage. If the frozen preparation/load path cannot retain valid terrain objects, stop this fixture with a precise setup failure; do not force native eligibility or construct a substitute offscreen target.

Then add two explicit controls from the same input: smoothing enabled and gridlines enabled. Both should have `worldSurfaces == false` while still matching their own software reference. Finally open an overlapping research window over the eligible terrain and require both native records and UI commands; this exercises main-window clipping around native output. Later terrain versions can introduce slopes, material stripes and water through public map/surface APIs in the one-time preparer, followed by reload/census/hash validation, rather than editing source-side state independently in each renderer run.

Relevant source: `src/openrct2/GameState.cpp::gameStateInitAll`, `world/Map.cpp::MapInit`, `world/tile_element/SurfaceElement.h`, `park/ParkFile.h`; native admission in `src/openrct2/interface/Viewport.cpp::ViewportPaint` and `src/openrct2-renderer/gpu/GpuCommandDrawingContext.cpp::DrawWorldSurfaceScene`. The diagnostic bridge already exposes `worldSurfaces`, `worldEpoch` and `worldSurfaceRecordCount`; no additional renderer API is needed for this first matrix.

## Implementation order and acceptance checklist

- [x] Complete and manually inspect the existing three-way baseline before changing its driver snapshot.
- [x] Add versioned fixture dispatch, expected step manifests and window/input metadata; build the exact same harness snapshot into frozen software, current software and Vulkan executables.
- [x] Implement overlapping windows first, then shortcut scrolling, then wrapped text; run each full composition sequence twice and inspect differences before fixes. All three `-02` families match at both layers; canonical review records corrected caret/close/restoration samples.
- [x] Add separately labelled incremental-damage variants and baseline-restoration assertions. All nine `-incremental-01` runs match both frozen incremental and full-invalidation states at both layers, including the second complete sequence without forced full invalidation; representative corrected outputs/diffs are reviewed.
- [ ] Produce and validate the entity-free park once; freeze its bytes and input census before rendering comparisons.
- [ ] Require native terrain coverage for eligible cases and explicit fallback coverage for controls; run all selected camera variants twice.
- [ ] Record source/dependency/toolchain/build receipts and all step hashes with each run. Do not reuse an older UI library if hook or fixture-relevant production source changed.
- [ ] Update the migration checklist with measured coverage and remaining UI families (dropdowns, tooltips, disabled widgets, object previews, TTF/locales, selections/IME, resize/DPI and palette/weather transitions). These four families do not establish complete UI parity by themselves.

## 5. Actual weather, gloom and lightning palette sequences

Read-only design inspected 2026-09-19. This section is an implementation proposal, not passing coverage. It extends the actual main-UI driver with two separately named fixture families after the next coordinated source checkpoint. Keep software rasterizer and palette code frozen, and retain GPU weather rasterization/composition. Do not add a CPU-generated precipitation image upload to Vulkan.

### Public inputs and deterministic initialization

Use the same pinned Ferris-wheel park, camera, 960×640 canvas, assets, theme and paused tick 1215. Keep `enableLightFx=false` throughout these first weather/palette sequences, including loading; enable no vehicle lights. The accepted light-night off-to-on sequence already demonstrates why uncounted loading-progress paints must not advance stateful LightFX adaptation. Cross-product lighting/weather and startup-enabled behavior remain later fixtures.

After loading, assert `GetObjectManager().GetLoadedObject<ClimateObject>(0)` and the active water palette object exist. `Weather::forceWeather(Type)` from `world/Weather.h` is the normal public state setter: it selects the loaded climate's temperature, precipitation intensity/effect and gloom traits, schedules the next weather and invalidates the screen. It consumes `ScenarioRand()` through `determineFutureWeather`, so call public `ScenarioRandSeed(0x12345678, 0x9abcdef0)` once at the beginning of the controlled sequence and record the before/after public `ScenarioRandState()` at each prescribed transition. Do not call weather/simulation updates, utility RNG, or an event-loop timer.

Critically, `DrawWeather` selects snow if **either** `weatherCurrent.weatherType` or `weatherNext.weatherType` is snow, heavy snow or blizzard. After each normal `forceWeather` step, set the public `gameState.weatherNext = gameState.weatherCurrent` to define a steady-state fixture and record that explicit input normalization. A later separately named transition case should deliberately set current rain/next snow rather than inheriting an accidental random next state. Record complete current and next states (type, temperature, effect, gloom, level) and the weather update timer; fail if the requested type/level did not take effect. No private weather state needs alteration.

Pin `gDayNightCycle=0`, `gPaletteEffectFrame=0`, `Weather::gLightningFlash=0`, clear main viewport flags, and retain normal full invalidation. Perform one documented full `LoadPalette()` refresh, then exactly two paint-only warmups with clear weather before the named sequence. Each capture records both pre-paint inputs and post-paint outputs; never call `UpdatePaletteEffects()` separately for inspection. It already runs once in `Painter::Paint`.

Rain/snow animation offsets are determined by `gameState.currentTicks`, not wall time. Light snow uses `cos(currentTicks*0.05)` as well; use the same pinned platform/toolchain/dependencies for the initial matrix. Holding tick 1215 fixed yields a deterministic stationary pattern while still exercising real weather traversal, clipping and shader drawing. A follow-up phase test can set ticks to an explicit second value without running simulation, but must label that as a controlled render-state input and record it. Do not claim elapsed gameplay/weather evolution from paint-only captures.

### First family: `weather-precipitation`

Use `renderWeatherEffects=true`, `renderWeatherGloom=false`, no lightning and no LightFX. Run this fixed sequence once per process, capturing each exact step name:

| Step | Controlled public input and operation | Required evidence |
| --- | --- | --- |
| `weather-clear` | `forceWeather(sunny)`, next=current | No GPU weather commands; clear reference |
| `weather-rain-light` | `forceWeather(rain)`, next=current | Light rain, nonzero weather commands; actual indexed and RGBA changes |
| `weather-rain-heavy` | `forceWeather(heavyRain)`, next=current | Heavy rain, increased passes through real weather drawer |
| `weather-rain-occluded` | Keep heavy rain, open Research through public UI API at (260,132) | Nonempty overlapping window; rain clipped around its full rectangle by production `DrawWeatherWindow` |
| `weather-rain-restored` | Close only Research, `WindowCullDead()` | Exact restoration to `weather-rain-heavy` at both layers |
| `weather-snow-light` | `forceWeather(snow)`, next=current | Light snow selected from production state |
| `weather-snow-heavy` | `forceWeather(heavySnow)`, next=current | Heavy snow selected; nonempty precipitation differences |
| `weather-cleared` | `forceWeather(sunny)`, next=current | No weather commands; exact restoration to `weather-clear` |

`WeatherDrawer.cpp` issues two pattern passes for light rain/snow and four for heavy rain/snow **per visible viewport rectangle**. Main toolbars and the added window split those rectangles, so do not hardcode the total weather-command count as merely 2 or 4. Existing diagnostic `commandCoverage.weather` proves nonzero coverage; a later diagnostic-only histogram/hash can qualify rain versus snow command patterns and clipped bounds directly from the already-owned packet. GPU `WeatherDrawer` records bounds, offsets and pattern ID; `VulkanWeatherPipeline` executes the pattern shader. No software pixel fallback should satisfy this gate.

The shader-generated indexed precipitation must match frozen X8 weather drawing as well as the actual SDL/Vulkan physical output. Require changes inside the unobscured main viewport, no precipitation leaking into the added window, and the two restoration equalities. Compare the complete screen for acceptance; rectangles are only coverage diagnostics. Preserve every differing triplet for manual review before corrections.

### Second family: `weather-palette`

Use `renderWeatherEffects=false`, `renderWeatherGloom=true`, no LightFX and fixed palette frame 0. This isolates viewport gloom remapping and actual palette publication from precipitation. Begin with the same explicit clear-weather initialization and two warmups.

| Step | Controlled public input and operation | Required evidence |
| --- | --- | --- |
| `palette-clear` | Sunny, next=current, lightning 0 | Clear indexed/RGBA baseline |
| `palette-gloom-1` | Rain traits, next=current | Gloom level 1; weather-command count 0; visible viewport index remap |
| `palette-gloom-2` | Heavy rain traits, next=current | Gloom level 2; distinct remapped indexed/RGBA image |
| `palette-lightning` | Keep heavy-rain traits; set public `Weather::gLightningFlash=1` immediately before this paint | Actual `UpdatePaletteEffects` flash branch runs; post-paint flash state exactly 2; indexed canvas remains equal to gloom-2 while physical RGB changes |
| `palette-lightning-recovered` | No reset or extra palette call; next ordinary paint | Actual restoration branch consumes state 2 and resets it to 0; full indexed/RGBA match to gloom-2 |
| `palette-restored` | Sunny, next=current, lightning already 0 | Full restoration to palette-clear |

Gloom has two production mechanisms: `ViewportPaintWeatherGloom` remaps viewport indices (unless excluded by hide-entities/path-issues/track-design flags), and `UpdatePaletteEffects` selects weather-shaded animated water/sparkle/chain palettes. The tiny park may not visibly use every animated palette entry. This family must claim only observed viewport gloom and flash behavior. Visible water-wave/sparkle palette animation needs a separately pinned park containing water, or a frozen prepared park with a documented water census; changing palette hashes alone does not prove those colours reached the screen. Future explicit palette phases should record used-index census and require relevant pixels to change.

Setting the exported lightning flag is controlled input to the real production flash renderer, not proof of stochastic thunder-event scheduling. `Weather::updateLightning` uses `UtilRand`, which this fixture deliberately never calls. No timer or RNG tolerance is justified. The flash's first paint does not execute the usual animated-palette branch, while the recovery paint does; hold `gPaletteEffectFrame` fixed and compare prescribed ordinals so that ordering is part of the test.

### Evidence and acceptance contract

Each family runs once per fresh process using one identical harness revision linked with frozen software, current software and Vulkan. Run every executable twice, compare each matching named step against the corresponding first process, and compare current/Vulkan against frozen. Do not rely on adjacent-frame equality for the flash sequence; the explicit restoration assertions above are the only within-sequence equalities. Record exact step list, warmup count and paint ordinal. Missing or skipped named captures fail the gate.

Common metadata: complete pre/post current and next weather states; before/after lightning flag; current ticks; palette effect frame; day/night value; effects/gloom/LightFX switches; scenario RNG state and weather timer; native `gGamePalette` and display `gPalette` hashes; active water/climate identifiers and palette image IDs; window rectangles/ordering; viewport flags/camera; full-invalidation mode. Frozen/current/Vulkan must agree on these common inputs and postconditions. Backend frame IDs/palette versions remain renderer-local diagnostics. Save raw indices, actual physical RGBA, PNG triplets, hashes and build/asset receipts through existing capture seams. No GPU readback is introduced into ordinary gameplay; opt-in named capture remains the test oracle only.

Required independent reference remains the unchanged frozen software executable using its actual SDL pre-present backbuffer. Do not substitute a CPU recreation of the GPU pattern shader or a test-only weather compositor as the reference. Initial implementation may add only a fixture helper/driver dispatch and diagnostic metadata from the already-owned frame packet; production fixes require measured divergence and manual visual review first.

- [ ] Implement steady precipitation family and verify frozen fresh-process repetition before Vulkan comparison.
- [ ] Verify rain/snow type, nonzero GPU command coverage, clipped-window behavior and restoration at both layers.
- [ ] Implement gloom/flash family with exact pre/post lightning states and physical-only flash expectation.
- [ ] Verify frozen/current/Vulkan fresh-process repeats, complete common metadata, and all required triplets.
- [ ] Review every divergence manually; retain zero-tolerance acceptance or document a separately justified software defect.
- [ ] Follow with incremental weather restoration, current-rain/next-snow transition, explicit animation phases, visible water palette animation, LightFX cross-products and broader camera/resize cases. None is closed by this design.

## 6. Actual UI fonts, localisation and UTF-8 input (read-only design, 2026-09-19)

This is a proposed extension, not passing evidence. The audit read source and installed font files only; no game execution, capture, font installation or download was performed. Existing `text` fixtures use ASCII, default language and scale 1. Synthetic TTF bitmap parity proves GPU compositing but does not prove the real language/font selection, FreeType raster cache, wrapping or input path.

### Source contracts that fixtures must respect

- `LanguageOpen(id)` invokes the localisation service, reloads localised object strings, invalidates scrolling text and notifies windows. Use this public operation, check success and actual current language, and finish it before warmup. Set `Config::Get().general.language` consistently; metadata must not merely echo a requested locale.
- `TryLoadFonts` in `interface/Fonts.cpp` considers a custom font only when the language descriptor selects a TTF family. English, French and Russian select sprite fonts. Setting `fonts.fileName=arial.ttf` while retaining English does **not** exercise TTF. Vietnamese selects sans-serif; Japanese, Korean and Chinese select their respective TTF families.
- Windows `Platform::GetFontPath` combines the system Fonts directory with the descriptor filename. Pin the resolved file and its SHA-256; the descriptor family name alone does not select a face. `TTF_OpenFont` opens collection face index 0. Installed `msgothic.ttc` face 0 is **MS Gothic**, although the built-in descriptor says MS PGothic. Record both facts without changing production selection.
- The custom descriptor is a function-local static initialised from configuration once, retaining string pointers. Configure its strings and all numeric fields before the first custom-font load and keep them unchanged for the process. Run each custom font/size profile in a fresh process; do not claim that editing config and reopening language necessarily rebuilds that descriptor.
- `Drawing.String.cpp` splits designated symbols into sprite runs even in TTF mode (`✓`, `▶`, quotation marks and other enumerated UI symbols). Other characters become TTF runs; `TTFSurfaceCacheGetOrAdd` obtains the real FreeType raster and the normal drawing context consumes it. An unsupported arbitrary TTF codepoint is not a general automatic sprite-font fallback. `FontSupportsStringTTF` can diagnose support before drawing.
- `TTFSDLPort.cpp` uses FreeType character lookup, glyph loading and pair kerning. It is not a general shaping engine. RTL language files separately pass through `FixRTL` at load time; on Windows that depends on `USE_FRIBIDI`, otherwise text is returned unchanged with a warning. Arbitrary user text does not pass through `LanguagePack` RTL conversion. Arabic correctness, combining-mark positioning and grapheme-aware editing cannot be inferred from pixel parity with the existing implementation.
- Text-input caret offsets are UTF-8 **byte** offsets. `WindowTextInputOpen`, `GetTextboxSession`, `SetTextboxCaret`, window `onUpdate` and `WindowTextInputKey` already provide the deterministic public path used by the ASCII fixture. The dialog measures prefixes and individual codepoints and draws a caret after 16 updates. Use only valid codepoint boundaries and record exact bytes; no timed event loop or OS keyboard/IME injection is needed.

### Locally available pinned inputs

These files were read and hashed on this host. They remain local licensed OS resources, not repository additions. Missing or changed required files must fail the relevant fixture explicitly. Do not silently select another font, install optional language packs, download fonts or weaken comparison.

| Resolved file | Bytes | SHA-256 |
| --- | ---: | --- |
| `C:/Windows/Fonts/arial.ttf` | 1,036,584 | `c9b76220a5be42ead4733611e417cd65c5fd8aeaa33eb56576ac378a37d130a1` |
| `C:/Windows/Fonts/msgothic.ttc` | 8,990,160 | `4bde3e6392b96910fb59094c6c1a4dbfae18fee78d0bf13dc30616837c4f95db` |
| `C:/Windows/Fonts/malgun.ttf` | 13,457,164 | `0086c19e81d293a542e7d75564c645fb58070cc850aefebf8fa1c397858e510c` |
| `C:/Windows/Fonts/simsun.ttc` | 18,316,748 | `1526ac24375f51f6eb73bc2d3f8072dbe4a80a3a65217677c9d9a84f67dab2ab` |
| `C:/Windows/Fonts/msyh.ttc` | 19,704,352 | `d79c55e68b1131eea0cc1c47be4f572d964f28c682e143db2ad09c1e4cb07a3f` |

Read-only fontTools cmap inspection confirms Arial covers the Latin/Vietnamese/Cyrillic sample below except `✓`; MS Gothic face 0 covers the Japanese sample including `✓`; Malgun covers the Korean sample except `✓`. The explicit sprite-symbol branch covers the tick during mixed TTF rendering, so require TTF glyph support for ordinary runs, not for characters intentionally routed to sprites. Runtime `TTFProvidesGlyph` checks remain required; cmap inspection is a design aid, not a raster result.

`arialuni.ttf`, `gulim.ttc`, `NanumGothic.ttf`, `NotoSansCJK-Regular.ttc` and `NotoNaskhArabic-Regular.ttf` were absent from the system Fonts directory. This permits a bounded language-family fallback test with installed Arial or Malgun; record the required absences and selected final font instead of assuming fallback order will remain unchanged on another machine.

Pin the **same** language asset bytes in every lane, alongside existing G1/G2/object receipts. The following current `data/language` files are candidate inputs; these are not a claim that an arbitrary frozen package already contains identical files. The runner must resolve/hash its actual packaged paths and reject differences before rendering:

| Locale | SHA-256 |
| --- | --- |
| en-GB | `5c1db96c56e43ecd037ae7f1dd6107da6b1f6b039d6243d2bc87a00f3da41bf0` |
| fr-FR | `a749f0517fe8659718d2495d1fb6a9c58497f8b0ea9e3faac948b98cdddd68b2` |
| ru-RU | `2d71aa4f71b025c65e95c5080d4d0524647bbaf0c6ebedad2852c20d0a1abdf6` |
| vi-VN | `48e17ce043703c295bd2a0b0e5ae8eb882fae834bf2d2eb765b6379f07bf061d` |
| ja-JP | `1c05edc51e94540aaf7c0b37711797b5b9734cd164675a6fa8a51f13787997ec` |
| ko-KR | `4923b18389e6305afbb164dd5f70acebfd1353bc78a45abd902f417e70204e71` |
| ar-EG (later RTL scope) | `20b0f93d7597b436f7bf80e622520e3eb9dbb7b4e1e8458e3d2337d58f9771ed` |

### Concrete initial families and sequence

Implement each family as a separate fresh process, using the pinned small park, paused tick, fixed camera, weather/LightFX disabled, full invalidation and two warmups. Explicitly start with a known sprite language, then apply the selected font configuration and public `LanguageOpen` after park loading. Check the active language/font afterwards so startup configuration reloads cannot silently defeat the fixture.

| Family | Exact intended configuration and text |
| --- | --- |
| `font-sprite-fr` | `fr-FR`, empty custom filename, TTF false; `Café été Straße Œuf — Жук Я` repeated four times with a trailing space |
| `font-sprite-ru` | `ru-RU`, empty custom filename, TTF false; same mixed accented/Cyrillic text to distinguish language selection from glyph input |
| `font-ttf-vi-hinted` | `vi-VN`, custom `arial.ttf`/`Arial`; all three custom size fields 12, all three line-height fields 14, x offset 0, y offset -1, hinting enabled, threshold 40; `AVATAR Café Việt Nam Đường vào ✓ →` repeated four times |
| `font-ttf-vi-unhinted` | Identical profile, hinting disabled from first font load; separate process |
| `font-ttf-ja` | `ja-JP`, custom `msgothic.ttc`/`MS Gothic`; same explicit 12/14 size/height and offsets, hinting enabled, threshold 60; `観覧車 入口 日本語 あいうえお ✓ →` repeated four times |
| `font-family-fallback-vi` | `vi-VN`, custom filename `OpenRCT2-parity-intentionally-missing.ttf` with explicit absence check; public loading must fall back through the language family to installed built-in Arial descriptors, not remain on custom or sprite mode |

Initial implementation can stage French sprite and hinted Vietnamese first, then the remaining rows after their fixture inputs and reference repetition are proven. Declared but unimplemented rows remain checklist gaps.

Every family uses seven literal steps: `font-empty-ui`, `font-localized-window`, `font-caret-start`, `font-caret-mid`, `font-caret-end`, `font-screen-clip`, `font-restored`. Open Research at `(260,132)` for the localized window. Open the real text-input dialog with fixed literal title/description and the family text. Run exactly 16 dialog updates to expose the caret, then capture start byte 0, a precomputed valid boundary immediately after the first non-ASCII word, and the complete buffer length without further updates. Move the same dialog to x=-24 through `Ui::Windows::WindowSetPosition` for screen clipping. Submit with the public Return-key callback, assert the exact UTF-8 buffer is returned once, close Research, and require exact restoration to `font-empty-ui`.

The loaded locale must visibly change real translated controls such as the Research title and text-dialog OK/Cancel labels. Record their resolved StringIds/UTF-8 values and actual widget rectangles. Require repeated text to wrap, the dialog to be taller than an empty-text probe, distinct caret screenshots, nonempty text in the clipped case and exact final restoration. A whole-frame difference caused only by a translated title is insufficient TTF coverage: require nonzero actual TTF bitmap draw commands in the selected text region and nonzero actual glyph coverage. A minimal opt-in diagnostic counter on the already-recorded packet can provide this later; the present capture's generic rectangle/sprite counts do not identify TTF usage.

### Scaling, enlarged controls, fallback and cache follow-ups

Add `font-ttf-vi-large125` and `font-sprite-fr-large125` using the same seven-step contract, `interface.enlargedUi=true`, `touchEnhancements=false`, scale 1.25, native window 1200×800 for a 960×640 logical canvas, and an explicitly pinned scaling quality. Set initial scale before display initialisation; for a later in-process scale transition use the same public resize path as Options (`ContextTriggerResize` and cursor scale update). Apply enlarged-frame changes through `WindowVisitEach(...resizeFrame())` and manager invalidation as production Options does. Check widget/title/close-box geometry actually changes; scale metadata alone is not evidence. These are separate acceptance groups from scale-1 font rasterization.

Hinting-cache invalidation deserves a later named sequence using `TTFToggleHinting` after changing the public hinting config: hinted → unhinted → hinted-restored, preserving other inputs and exact restored pixels. Do not modify the static custom descriptor in place. Do not require numeric cache IDs or pointer addresses to match across processes; require raster dimensions/content and final output instead. The new current TTF cache identity implementation must not be read through a frozen-only-incompatible structure layout in a shared harness.

Keep three meanings of fallback distinct: failed font-file/family load (the Vietnamese row), intentional sprite symbols within a TTF string (`✓`; U+2192 `→` remains a TTF glyph), and missing codepoint rendering. For a missing-glyph follow-up use valid Unicode U+10FFFF, assert lack of font support, capture the existing frozen result and compare exactly. Sprite mode currently maps unsupported codepoints to `?`; TTF uses its FreeType missing-glyph behavior. Neither outcome automatically justifies a correctness exception. Arabic later requires a pinned language pack, font, `USE_FRIBIDI`/dependency receipt and separate review of localized labels versus raw user-entered text. Do not label a simple TTF match as complete shaping/RTL support.

### Evidence and acceptance

For every enabled family run frozen software, current software and Vulkan twice in separate fresh processes, using matching named ordinals. Require exact raw indexed bytes, actual SDL/Vulkan physical RGBA and common input metadata; retain reference/candidate/diff PNGs for every state. Inspect every divergence manually before changing either implementation or fixture. No renderer changes or exception are authorised by this design.

Record actual locale and language fallback order; language-file and font-file SHA-256s; compiled TTF/RTL availability and linked FreeType version/build receipt; active per-style filename, face index, point size, offsets, line height, hinting threshold and `TTF_GetFontHinting`; support census for each ordinary codepoint and intentional sprite symbol; exact UTF-8 bytes/codepoints/byte offsets; text widths and wrapped line count; window/widget rectangles, caret update count, scale/enlarged flags and physical/logical extents. CPU FreeType glyph **asset** rasterization remains the existing input-generation path; Vulkan composes the resulting masks on the GPU. These fixtures must not introduce a CPU final-screen renderer or routine frame readback.

- [ ] Implement first sprite/TTF families with explicit active-language/font assertions and font/language receipts.
- [ ] Add non-vacuous actual TTF command coverage and inspect frozen fresh-process repetitions before candidate qualification.
- [ ] Qualify accented/Cyrillic sprites, Japanese TTF, hinting modes and deliberate file/family fallback.
- [ ] Qualify UTF-8 caret boundaries, wrapping, clipping, submission and exact restoration in all enabled families.
- [ ] Qualify enlarged controls/fractional scale and explicit hinting cache invalidation as separate groups.
- [ ] Retain complex shaping/RTL, combining sequences, grapheme navigation, IME composition, other scripts/fonts/styles/sizes and platform font discovery as separate gaps until exercised.

Font fixture setup correction: frozen font run01 stopped before capture because the initial probe incorrectly classified U+2192 RIGHTWARDS ARROW as a forced sprite. Production `UnicodeChar::right` is U+25B6 BLACK RIGHT-POINTING TRIANGLE. The staged correction keeps the exact multilingual text, requires pinned Arial support for U+2192 (`arrowright` in its cmap), and retains the sprite check for U+2713. This is a failed fixture precondition, not a renderer divergence or accepted exception.
