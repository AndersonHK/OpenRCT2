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

## Resident ownership during scene reloads

The loading batch following checkpoint176 separates map lifetime from unchanged asset lifetime. `ObjectManager::LoadObjects` first compares required object slots with the resident bindings. An identical list does not reopen material/peep mutation generations or reload objects. Changed bindings still use the existing guarded load/unload boundary, including exceptional exits and alias rebinds.

Terrain metadata now follows terrain-object revision rather than map epoch. Ride metadata is fully recaptured on an epoch switch and retains the previous immutable allocation only when all facts match. The world recorder resets map chunks on an epoch change but can keep its sprite table when the object catalogs, ride facts and usage membership still match and its dependency-qualified atlas lease remains current. Equal vehicle-art membership may similarly survive a new map/entity epoch; the hot entity records retain their own epoch qualification. No retired G1 allocation is read to rebuild an old catalog.

Authored regressions cover identical object reload, alias rebind/reset, held old material facts, high station changes across a map reset, and cold vehicle membership sharing without sharing replacement hot records. Compilation and runtime results belong to the parent's serial qualification; these changes alone do **not** establish a few-frame different-park transition.

The remaining large boundary is the combined sprite table: it owns both hundreds of thousands of six-zoom image records and small scene-specific ride descriptors. A different title park can change the latter and trigger complete image enumeration/resolution even when much of the art is still resident. Measure object preflight/parse/install, world/peep/vehicle catalog construction, and atlas staging separately before the next split. The intended ownership split is an immutable resident image-bank generation plus scene descriptor tables referencing its indices, with the existing image dependency invalidation and submission leases. Actual object unload/replacement must still retire the affected bank; retaining stale scene descriptors is not a valid loading optimization.

The build181 candidate replaces the vehicle catalog's per-image ordered set with a sorted union of original contiguous image-bank intervals. This removes a tree allocation for each admitted image and repeated insertion of overlapping banks while preserving the exact ascending unique image IDs, effect images, lookup indices and owning-allocation checks. The regression compares overlapping, adjacent, duplicate, empty and effect-overlapping banks against an independent ordered-set oracle, and rejects an overflowing image range. This remains catalog construction work: every unique image still resolves its resident zoom variants. The build184 runtime evidence below includes this change, but does not isolate its performance contribution or establish a few-frame transition.

`OPENRCT2_LOADING_REPORT=1` now reports those application wall-time stages without per-image clocks, GPU waits or readbacks. Catalog messages carry map epoch and catalog revision; atlas messages include queued image count, payload bytes and CPU recording wall time. These are separate from actual GPU transfer duration. The normal path does not collect clocks.

`scripts/rendering/run-title-loading.py` prepares a receipt-qualified executable/shader copy and a fresh profile under its new output directory. It copies the accepted bundled title archive's actual parks and original first two camera commands per park, shortens camera holds to 1000 ms, and runs two loops through the selected parks. `--parks 2` is the bounded first two parks; `--parks 0` selects all 17 bundled parks. The summary pins the original and modified archive, individual park bytes, configuration, binary/shaders, licensed assets and command markers. Scene/camera intervals include the explicit holds and simulation; they are not displayed-frame latency measurements. The runner sets the separate `OPENRCT2_TITLE_LOADING_EXIT_AT_END=1` opt-in so END requests normal context teardown in title mode. No user profile, input automation or save is involved; timeout/forced termination is recorded as failure.

Loading diagnostics must also be silent and hidden from window creation: use the dummy audio context and an SDL hidden window only for the explicitly enabled loading-report plus exit-at-END diagnostic mode. The build180 attempt did not meet that requirement and must not be repeated with that binary. It exited normally but its receipt remains failed because Console ANSI colour-reset bytes prevented marker parsing. The corrected parser removes only SGR formatting and checks every authored command kind, index and monotonic timestamp. Separate `title-loading180-two-parks-01/timing-reanalysis.json` preserves the original failed receipt and log; it does not qualify the final asset checks skipped after the parser failure or establish a silent/hidden runtime pass.

### Silent, hidden build184 title qualification

`obj/vulkan-parity/title-loading184-two-parks-01/summary.json` passes with normal exit 0 and all 31 command markers. The log explicitly confirms hidden-window creation and dummy audio. The same source title archive, park bytes and authored command sequence were used as in180: Kahuna Point (A), Bear Hollow Park (B), then A and B again, with two 1000 ms camera holds per scene. The generated ZIP hashes differ, so matching archive bytes are not claimed. Build180 remains an unacceptable audible/windowed attempt; its repaired timings are historical diagnostic evidence only.

All values below are CPU-side application wall milliseconds, shown as **180 → 184**. Catalog construction happens after the LOAD command returns, so the load-command column is not time to a completed first frame.

| Scene | LOAD command | Object loading total | Catalog total | Vehicle catalog stage |
| --- | ---: | ---: | ---: | ---: |
| A, first visit | 364.2 → 358.5 | 265.8 → 259.0 | 199.6 → 194.5 | 77.0 → 71.6 |
| B, first visit | 193.8 → 191.6 | 101.9 → 95.3 | 161.5 → 150.0 | 85.9 → 76.5 |
| A, return visit | 132.5 → 129.4 | 44.0 → 45.6 | 71.2 → 64.5 | 33.3 → 27.9 |
| B, return visit | 192.6 → 187.6 | 99.7 → 97.3 | 112.4 → 104.3 | 57.5 → 50.4 |

Both runs retain the same scene catalog counts: 73,356 sprite sets for A and 102,836 for B. No object/catalog/atlas stages appear on camera-only rotations and moves. In184 the first-camera-to-second-camera intervals were 1105.9, 1611.0, 991.9 and 1132.4 ms; these include simulation, asynchronous rendering and the requested hold, whose scheduling is not exact wall time. Total playback was 9707.3 ms versus 9839.6 ms in180, including eight requested one-second holds. The process also spent 30.921 s preparing world pipelines before playback. None of these measurements identifies the first completed/presented frame, qualifies displayed pacing, or demonstrates a few-frame park transition. Changed audio/window behavior, intervening code changes and single samples prevent attributing the differences solely to the interval union.

The ownership work remains concrete: B's return visit still parses 162 objects (54.6 ms) and installs their images (37.0 ms); A reparses 74. The active object registry unloads scene-exclusive objects, so a warm loop does not retain their decoded ownership. A deliberate bounded inactive-object owner, separate from active registry bindings, is needed to eliminate that work safely. Independently, each scene still rebuilds six-zoom image records and their combined lease: even on return visits, vehicle catalog construction costs 27.9/50.4 ms and effects-plus-lease costs 16.2/24.8 ms. Separating immutable resident image banks from scene descriptors can remove that repeated resolution/pinning without reusing retired G1 allocations. Atlas recording is smaller (A return 2.851 ms; B return 5.195 and 4.921 ms for two logged submissions), and these figures measure command recording rather than transfer completion. Keep the silent/hidden paired loop and add an explicit completed-first-frame marker when qualifying either ownership change.

Evidence SHA-256: build184 summary `b8f2ff2dd9e5c177c93c43ca8228bfd5ff761c2adce97c47276b39d46a669988`; build184 log `cbe81e5d481cc1f560b168de2ac44a4960676dc3226b98f93233acf59f3108d2`; separate build180 timing reanalysis `a10fee1606e1d80a856383e6ebb6edd91ece6f5ed1a2696473e9e7ceb05749ab`.
