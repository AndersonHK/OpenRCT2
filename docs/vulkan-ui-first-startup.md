# Vulkan UI-first startup

The game should use its ordinary themed progress window and Vulkan sprite/text renderer while expensive world pipelines compile. The former Win32 child window and marquee are removed; they were a second UI implementation and appeared before the game font, palette and progress sprites were ready.

## Reference inspected

The reference checkout is `C:/Users/imper/Documents/GitHub/augustus_cpp_huge`. Its `docs/loading_screens.md`, `src/platform/loading_screen.cpp`, and `src/platform/augustus.cpp` establish a useful readiness boundary: load bootstrap UI inputs first, expose loader progress through an observer, process window events, and keep incomplete world registries out of rendering. Its implementation uses synchronous progress callbacks; it does not simulate or draw a partially loaded city concurrently. Its original game font becomes usable through an explicit font-ready notification. The object-owned runtime doctrine was also read in full.

OpenRCT2 already has the appropriate themed UI (`ProgressWindow`) and preloader scene. Reuse them rather than importing a separate themed renderer or the reference game's assets.

## Ownership and stages

1. Create the window/device and minimal indexed UI/palette pipelines. Load base art, fonts and palette; initialise the preloader scene with world rendering inhibited.
2. Show the normal game progress window and submit its first frame before starting expensive native-world compilation.
3. Compile into an independently owned, unpublished world pipeline. The UI thread pumps normal window messages and redraws the progress window; the existing render worker continues submitting UI frames. No simulation tick or partially loaded registry traversal is run by this loading loop.
4. Keep indexed image handles and extents immutable during compilation. Coalesce logical resize requests until the preparation completes. OS window events are still pumped and the presentation surface can stretch the existing UI canvas.
5. Join compilation and drain UI submissions before transferring ownership of the prepared pipeline to the frame executor. Publish exactly once at that boundary. Apply the latest deferred resize and continue repository loading through the existing preloader.
6. Worker or UI callback failure joins the preparation task before any referenced resources are destroyed. No detached loader can outlive its renderer.

This addresses shader compilation startup. It does not claim that later object-file decoding, catalog construction or a large atlas admission is already asynchronous; those are separate loading stages with their own ownership requirements.

## Validation checklist

- [x] CPU startup tests: compilation runs off the UI thread; pump runs on the owner; task failure propagates; UI callback failure joins outstanding work (169 and171 focused suites).
- [x] Warm-cache startup at 3840×2160: normal themed progress UI captured and manually viewed; ordinary world frames followed in169/170c before the separately diagnosed missing vehicle-image failure.
- [ ] Cold-cache startup at 3840×2160: themed progress remains visible and responsive during native-world compilation; logs identify UI-ready and world-ready durations.
- [ ] Resize/minimise/restore during preparation: no stale descriptors, resource lifetime errors or missing first world frame.
- [ ] Close during preparation: no simulation starts, compilation joins before renderer disposal.
- [ ] Main renderer, bitmap-only auxiliary renderer, and normal eager Vulkan backend initialization all retain valid paths.

Runtime checks are intentionally pending until the parent runs the serial validation build. They must not be reported as passed from source inspection alone.

Fresh application-cache build171 produced the normal loading UI after0.295s of UI pipeline preparation, then spent29.659s compiling world pipelines. The initial themed screenshot is `obj/vulkan-parity/camera-stress171-01/profile/screenshot/Unnamed park 2026-09-24 22-16-24.png`. This is evidence of rendering before world compilation, not a cold driver-cache test, resize/close qualification, or world stability pass: that run later experienced device loss during park warmup.
