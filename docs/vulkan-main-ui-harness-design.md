# Main-window and UI parity harness

Status: implementation proposal, inspected against the current source on 2026-09-19. No renderer changes are made by this document. The existing primitive, auxiliary viewport and SDL scaling fixtures do not establish complete main-window parity.

## Proposed shape

Build a small, process-isolated UI capture executable from the actual UI library and a shared test driver. Run that driver against (1) the separately rebuilt frozen source using software-with-hardware-display, (2) the current source using software-with-hardware-display, and (3) the current source using Vulkan. Give every process the same immutable park, assets, explicit profile and fixture description. Capture the actual final SDL or Vulkan display image, with no PNG palette expansion standing in for presentation.

Keep all three outputs. Frozen software versus current software detects changes in shared scene publication, UI painting, asset metadata and palette code that a same-build software/Vulkan comparison could conceal. Frozen software versus current Vulkan is the acceptance comparison. Current software versus current Vulkan is a useful diagnostic, never a replacement oracle. A frozen/current software difference requires investigation even when the current pair matches.

Use the existing source extraction and integrity machinery in `scripts/rendering/prepare-frozen-oracle.py` as the model for a separate UI-oracle preparation script. Do not change the accepted archive or reuse the instrumented CLI directory in place. Record every added harness/build input, source hashes, compiler options, dependency hashes, executable hashes, fixture JSON hash and output hashes in its receipt. Verify that the frozen renderer, UI, viewport, painter and asset sources remain byte-identical to the archive. Both binaries compile the same driver revision; rendering algorithms come exclusively from their respective source trees.

## Actual software display capture without source edits

`src/openrct2-ui/drawing/engines/HardwareDisplayDrawingEngine.cpp`, `Display()` (present call currently line 327), is the precise seam. At that point the implementation has uploaded dirty/full pixels or LightFX, completed both smooth-scaling copies where applicable, restored the default SDL render target, and drawn optional dirty-region visualisation. Capture immediately before its real `SDL_RenderPresent` call.

Compile only this translation unit with a test-only present substitution. A robust forced-include header can first include `SDL_render.h`, declare `extern "C" void SDLCALL OraclePresent(SDL_Renderer*)`, then define `SDL_RenderPresent` as `OraclePresent`. This preserves the original SDL declaration before macro substitution and avoids relying on platform-specific import attributes. Disable precompiled-header use for this instrumented translation unit if necessary. A separate wrapper translation unit, compiled without that macro, calls the real SDL function. A plain source-scoped define also works with this checkout's empty Windows SDL `DECLSPEC`, but the forced-include arrangement makes the boundary explicit.

The wrapper should:

1. If no named capture is armed, immediately forward to real `SDL_RenderPresent`.
2. Require `SDL_GetRenderTarget(renderer) == nullptr`, query `SDL_GetRendererOutputSize`, allocate an owned RGBA byte buffer and call `SDL_RenderReadPixels` with `SDL_PIXELFORMAT_RGBA32` and pitch `width * 4`.
3. Record renderer name/flags, physical dimensions, logical dimensions and scale configuration. Propagate failures into the harness result; never turn them into a passing skip.
4. Forward to real `SDL_RenderPresent` exactly once, then complete the named request. The hook must not alter blend state, render targets, renderer scale, texture contents or palette conversion.

The harness arms capture only after park loading, window setup and any fixed warm-up draws. Startup progress-window presents therefore cannot satisfy a fixture request. Capture one named frame at a time. The current software screenshot API exports an indexed canvas and cannot replace this hook for the final-screen contract. Reading SDL's backbuffer only after `EndDraw()` returns is also the wrong seam because `Display()` has already presented it.

## Vulkan frontend bridge

The backend already has exact-frame `RequestFrameCapture` and `ReadbackFrameRgba` APIs. The missing link is `src/openrct2-ui/drawing/engines/vulkan/VulkanDrawingEngine.cpp`, whose backend and frame mailbox are private. Its public `Screenshot()` intentionally produces indexed PNG output and does not provide final RGBA evidence.

Add a narrow named diagnostic request API implemented inside that translation unit and declared in a diagnostic header. A request is armed on the UI thread before the next `BeginDraw`; `EndDrawQueued` attaches it to that exact immutable packet and records its frame number. The worker enables diagnostic capture at backend creation, calls `RequestFrameCapture` after acquiring this packet's frame and before `Submit`, then copies `ReadbackFrameRgba(packet.frameNumber)` into owned request storage after `Present`. Include the corresponding indexed canvas and packet metadata in the response for diagnosis. The caller waits on request completion with a bounded failure timeout.

The diagnostic build can enable capture through an explicit harness-only initialization hook, registered before constructing the context and snapshotted by the Vulkan engine constructor. Compile that plumbing only in the capture executable. This avoids a permanent global user setting or changing the software renderer. A production diagnostic factory option is a possible later replacement; it is not needed to prove parity.

Attach capture identity to the packet, not to whichever frame happens to be newest when a worker callback runs. The renderer's normal mailbox remains in use. For initial fixtures submit one frame and wait before submitting another; no frame coalescing is necessary. A skipped acquisition, supersession, failed submission, resize cancellation or shutdown must fail that named request through the same retirement paths used for screenshot requests. Never return a previous successful image. Later queue-stress fixtures can deliberately exercise those failure paths. Diagnostic requests may wait for GPU completion without changing ordinary frame behavior.

At the same attachment boundary record command counts, whether `commands.worldSurfaces` is present, world/map/entity revisions if available, palette/resize versions and selected output mode. These are coverage assertions, not a substitute for image comparison. There is no need to change generic `IDrawingEngine` drawing methods or copy any renderer into the harness.

## Deterministic actual UI setup

The necessary production entry points are already public:

- `CreatePlatformEnvironment`, `CreateDummyAudioContext`, `OpenRCT2::Ui::CreateUiContext` and the three-argument `CreateContext` overload reproduce `src/openrct2-ui/Ui.cpp` startup.
- `IContext::Initialise()` creates the SDL window and actual configured drawing engine. `gIntegratedBenchmark.enabled = true`, `visible = false` uses the existing hidden-window branch in `UiContext::CreateWindow` and suppresses several user-config writes/DPI inference. Use this flag solely for initialization; do not run the time-based benchmark loop.
- `IContext::LoadParkFromFile` imports assets/state and calls `GameLoadInit`, which establishes the playing scene and UI. Assert that `WindowGetMain()` and its viewport exist rather than silently creating an unrelated auxiliary viewport.
- `IContext::GetPainter()->Paint(*engine)`, between `engine->BeginDraw()` and `engine->EndDraw()`, is the same paint sequence used by private `Context::Draw` at lines 1995–1997. Calling those public interfaces avoids a new frozen-context hook or wall-clock-driven simulation loop. `Painter::Paint` traverses actual windows, UI overlay/chat/console, pickup peeps, weather, palette effects and optional notices.

Use a new workspace-local profile for every process, pin RCT1/RCT2/bundled assets and theme/language/font selection before initialization, disable audio/network/version checks/intro/FPS/dirty visualization, pin logical and physical sizes, use SDR and explicit VSync/scale policy. Set `gOpenRCT2Headless = false` and `gOpenRCT2NoGraphics = false`; a dummy UI is insufficient. The window minimum is 720×480, so start at 960×640 or another explicit legal size.

After load, explicitly set main viewport rotation, zoom, projected position, flags, selection state, weather/time-of-day, simulation tick, palette-effect frame, pause state and entity interpolation state. Fix all chosen window positions/sizes/tab selections and any text/scroll values through existing UI/window APIs. Do not pump arbitrary wall-clock updates between captures. Run a fixed number of paint-only warm-up frames for initialization, then arm the named frame. Record `gCurrentDrawCount` and input state before/after each draw because `Painter::Paint` increments that count. Fail if the same fixture run twice yields different output.

Initially force full invalidation to establish the full-screen baseline. Subsequently keep the software damage system active and drive named state transitions: open/close/move an overlapping window, scroll a list, change a text field, resize, and return to the baseline. Those transitions must repaint correctly without imposing a full repaint that conceals damage bugs. Use separate processes per fixture family to avoid static scripting/UI/presentation lifetime contamination seen in earlier tests.

## Main publication and direct terrain must be demonstrated

`ViewportPaint` considers a viewport part of main presentation only when `rt.DrawingEngine == GetContext()->GetDrawingEngine()`. Direct terrain additionally requires the actual `ViewportGetMain()` viewport, a published generation, no surface-overlay flags (grid/underground/hide-base/clip/heights/ownership), empty map selection, no tile-inspector selection and no track-designer/manager mode (`Viewport.cpp`, around lines 1030–1088).

`CommandDrawingContext::DrawWorldSurfaceScene` also requires `CanDrawSurfaceBaseIndependently()`, no captured entities, landscape smoothing disabled and representable surface sprites. It now rejects covered-zero assets back to ordinary Vulkan painting. A populated park can therefore pass every UI fixture without ever drawing a native surface command.

Provide two initial park families: an unchanged populated saved park to cover ordinary main publication and overlapping UI, plus a deterministic entity-free terrain-only park that actually satisfies native-surface eligibility. Save/hash the latter once and load exactly that file in both oracle and candidate. Do not remove entities only on the candidate side. Assert `worldSurfaces` present for native terrain fixtures and absent for deliberately ineligible variants. Test four rotations, zoom −1/0/1/2, nonaligned camera positions and cropped viewport edges. Add slope/water/ownership/gridline/smoothing variants incrementally with explicit admission expectations, rather than treating fallback as native-path coverage.

Capture at least one real main-viewport publication transition after a synchronous entity/map edit. A same-build pair can share the stale-generation bug; the independent frozen UI oracle and logged input state are required to distinguish that from a Vulkan rasterization defect.

## Minimal build surface and implementation checklist

Create a standalone `test/ui-parity` driver and capture support, rather than linking UI's `main` into the current gtest target. CMake can reuse the UI source list with `Ui.cpp` excluded; MSBuild can provide a dedicated executable project referencing the existing core/UI libraries built into distinct diagnostic intermediate/output directories. Instrument the hardware-display source only in the diagnostic UI library variant. Add the forwarding wrapper object to both current and frozen capture targets. Never depend on static-library duplicate-symbol selection or overwrite the ordinary UI library with an instrumented object.

- [x] Add driver/fixture schema and named SDL capture state. Current software baseline repeats exactly in ui-run-01; both layers repeat exactly in ui-current-run-02.
- [x] Build the separate frozen-source UI oracle with the identical driver/hook revision and verify its source receipt. ui-frozen-run-01 and ui-current-run-02 agree exactly with all 12,008 original frozen source files verified unchanged.
- [x] Add coordinated Vulkan packet capture bridge and frame/retirement tests; keep normal factory behavior unchanged. ui-build-05 compiles the diagnostic frontend; ui-vulkan-run-01 proves named final-output capture and exact baseline pixels, but remains failed on a synchronization hazard awaiting its fix/build/rerun.
- [ ] Run baseline main window/toolbars, overlapping ordinary window, clipped scroll list and text fixtures. Require actual capture dimensions and UI-specific landmark assertions.
- [ ] Add direct-terrain admission fixtures and logged command coverage; preserve ordinary Vulkan fallback cases.
- [ ] Add dirty transitions, tooltip/dropdown/cursor-related drawing, sprite and pinned TTF text, palette/weather/LightFX, scale/resize and queue lifecycle families.
- [x] Store raw buffers, reference/candidate/difference PNGs, exact counts/bounds/channel errors and complete provenance. Manual review remains mandatory for every new divergent sample group before any fix/exception decision.
- [ ] Integrate required fixture/capture counts into the runner so missing output, hidden-window acquisition failure, stale IDs or skips fail acceptance.

The first implementation can be small: one software hook, one standalone driver, one frozen build recipe and one Vulkan frontend diagnostic bridge. Broad UI fixture coverage follows only after that independent final-display baseline is repeatable.
