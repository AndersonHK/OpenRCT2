# Vulkan migration: manual-test checkpoint 52

This is an intermediate testing checkpoint, not the completed Vulkan-only migration. The ordinary Windows build passes with zero warnings/errors; all **798 tests**, including 66 parity tests, pass with active synchronization validation. Six terrain diagnostic gates also pass. The software renderer remains available and its frozen reference is unchanged.

**Deployed for manual testing:** `D:\Games\Independent\OpenRCT2Mod\openrct2.exe`. All 4,715 installed package files were hash-verified. Both installed launchers pass version smoke; the installed Vulkan main loop passes a 100-tick smoke using installed data/shaders and an isolated profile. Your personal configuration hash is unchanged. Select **Options → Display → Drawing engine → Vulkan** to test this renderer.

**Rollback:** `D:\Games\Independent\OpenRCT2Mod\backup-before-vulkan-20260920-000626-696` contains the verified previous managed files. Profiles/saves and older backups are preserved. [Pinned checkpoint receipt](vulkan-checkpoint-52.json) records the package, exact archived source inputs, validation, visual reviews, metrics and deployment evidence.

## Progress and remaining work

| Workstream | Status | Completed evidence | Still required |
|---|---|---|---|
| Frozen reference and exact comparisons | Qualified infrastructure | Immutable software reference, exact indexed and displayed RGBA comparisons, fresh-process repeats and agent visual reviews | Expand the scene/feature matrix as new world categories migrate |
| Ordinary Vulkan rendering | Partial parity | Sprites, UI/fonts/scaling, lighting/weather, scene depth and large-park samples | Close the public transparent-viewport history gap and remaining feature combinations |
| World rendering from VRAM | Bounded implementation | Shader terrain projection/selection/order plus retained terrain/balloon state in diagnostic fixtures | Real terrain edits, arbitrary maps, water, scenery, paths, rides, vehicles and peeps; broader runtime admission |
| CPU and upload reduction | Bounded implementation | Settled diagnostic terrain avoids CPU world painting and state uploads; catalog now reuses metadata | Eliminate general per-frame CPU world command generation; measure total traffic and CPU cost |
| Shared Vulkan service and image callers | Partial migration | Shared device/executor, ordinary/giant CLI image parity and lifecycle tests | Interactive giant screenshots, park previews, minimap and scripting/image callers |
| Performance target | Open | Six fresh ordinary-build measurements below | Sustained large-park TPS gain, independent displayed-VSync pacing and headroom evidence |
| Packaging/platforms | Windows checkpoint deployed | Verified local package/rollback and installed Vulkan smoke; SDL-free renderer target with 23 translation units and 21 shaders | Clean-machine/runtime loader checks; declared non-Windows targets; explicit web-platform decision |
| Exclusive Vulkan and cleanup | Gated | Migration boundaries and software freeze are established | Make Vulkan default, remove all software dependencies and dead paths only after parity/performance/platform gates pass |

No honest overall completion percentage is available: most remaining work lies in general world categories and performance, rather than in the number of checked test fixtures.

## What changed in this checkpoint

The retained terrain bridge now skips material-map copies when the world publication is unchanged. It keeps its sprite catalog and rebinds existing atlas allocations through the normal frame leases. Invalidation, retirement, cache identity and catalog rebuilds have regression coverage. This removes repeated metadata preparation in the bounded diagnostic terrain path; it does not enable that path for arbitrary player parks.

Production native-terrain admission remains closed. Ordinary parks still use CPU-generated world drawing commands with GPU rasterization, so the final state-update-only rendering objective is unfinished.

## Known rendering limitation

A script can enable transparent backgrounds on the main viewport. Software deliberately retains pixels outside the map, while Vulkan currently clears each replacement frame. The sampled scene differs by 15,134 background pixels and 3,856 pixels in the bottom status/date panels, where software reapplies palette filters to old UI. An agent inspected all 84 diagnostic captures. Explicit clear0 controls match in all 42 samples, but this control does not replace the actual retained-background contract. No exception has been accepted for this gap. A GPU canvas-history design is staged for later review; it is not part of this deployment.

The post-load diagnostic also exposes randomized loading artwork; explicitly painted park frames remain independently comparable. Failed observations are preserved.

## Manual testing

- Select **Vulkan** in the game's drawing-engine setting. Deployment preserves your existing preference, which currently selects software with hardware display.
- Load a representative large park; rotate, zoom and scroll, then open/move/close overlapping windows.
- Check your normal display scale, fullscreen transitions, text, lighting, weather, shadows and translucent UI.
- Exercise ordinary screenshots, park loading/saving and exit/relaunch. Auxiliary caller migration is incomplete; this is a checkpoint test, not a claim that every image caller is already Vulkan-only.
- Compare responsiveness and FPS/TPS on the same park, camera, speed and VSync setting. Report the park/view and setting with any discrepancy.

## Verification, performance and deployment

Checkpoint52 build, run45 and UI07 receipts are under `obj/vulkan-parity`. The screen renewal passes 22 processes / 84 captures, including all four rotations, two zooms, overlapping windows, incremental redraw and a stable-sort fallback control. An agent inspected 21 distinct comparison sheets and verified all raw pixels. In 80 native captures, CPU world generate/arrange/draw calls and settled tile/material/sprite upload bytes are zero; map copies and catalog builds remain at one while resource rebinds advance. The other four captures correctly use the CPU fallback. These diagnostic figures do not describe arbitrary parks in the deployed game.

Fresh ordinary-build measurements use EverythingPark at 960×640, saved camera, ordinary Turbo, 100 warmup ticks and 2,000 measured ticks per process on the i9-13900K/RTX5070Ti. Uncapped runs are repeated in software/Vulkan/Vulkan/software order; each VSync-enabled row has one process. Asset/config identities, initial/final state census and final entity checksum agree between compared lanes.

| Setting | Software TPS | Vulkan TPS | Software draw FPS | Vulkan draw FPS |
|---|---:|---:|---:|---:|
| VSync off, two runs | 160.8–164.6 | 118.8–120.0 | 161.0–164.7 | 118.8–120.0 |
| VSync on, one run | 147.8 | 251.7 | 144.0 | 78.4 |

Vulkan's mean GPU frame time is approximately **0.27ms**, but uncapped main-loop draw CPU time is **4.94–4.99ms**, versus software's **2.53–2.62ms**. The uncapped Vulkan TPS is about 27% lower. With VSync enabled, Vulkan advances more simulation ticks while issuing substantially fewer draws; that is not proof of the required VSync-locked frame rate. These are short hidden-window measurements (roughly 8–17 seconds each); application draw FPS is not independently measured displayed FPS. Sustained performance and displayed pacing remain open gates. Current measurement receipts: `obj/vulkan-parity/performance-checkpoint52/measurement-summary.json` and the six `perf-checkpoint52-*-vsync*` runs.

A separate 400-tick instrumented run records **3.82MB of CPU-written command payload per submitted frame** (1,531,135,300 bytes across 401 submissions), plus palette/atlas updates. It reports zero capture/readback requests or bytes and no lost samples or allocation failures. These counters measure API payload, not physical memory bandwidth; producer-side copies are not included. This confirms why zero terrain uploads in the diagnostic fixture cannot be generalized to ordinary large parks. Evidence: `perf-checkpoint52-vulkan-upload-01/summary.json`.

Deployment is complete; automated game processes have exited for manual testing. The detailed implementation checklist remains [the migration plan](vulkan-exclusive-migration-plan.md).
