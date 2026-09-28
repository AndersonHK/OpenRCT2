# Separate upstream screenshot reference

The external upstream reference is pinned to **b80a4a84e92be8e07904b38d1032d0bb88280bb4** (2026-09-23 04:05:43 UTC), available in the local Git object database. It is independent of the accepted frozen `9a092745f3` oracle. Neither reference is replaced or re-certified by the other. Upstream is an ordering reference with possible bugs, not blanket authority for unexplained pixel differences.

## Build

From the repository root, with the pinned local dependencies and MSVC 14.44.35207 installed:

```powershell
python scripts/rendering/build-upstream-screenshot-oracle.py --output obj/vulkan-parity/upstream-screenshot-build-01
```

Use a fresh output directory. `--msbuild ABSOLUTE_PATH` is optional. The builder archives the exact local commit, extracts pristine sources under that output and exposes the existing `lib/x64` through a dependency junction. It never fetches, restores, builds assets, runs the game, changes the active checkout or reuses a different core binary. Core and CLI compile sequentially with `/m:1`, per-file multiprocessing disabled, x64 host/target, Release and fixed toolset 14.44.35207. Dependency project references and restore imports are removed from generated build metadata; original files remain unchanged.

The sole code instrumentation is an external copy of upstream `Cli.cpp`: add `<cstdlib>` and the four already-reviewed `OPENRCT2_ORACLE_*` environment-path assignments before `CommandLineRun`. Rendering code is unchanged. The existing UI project builder is reused only for core-project relocation, hashing and compiler discovery; the frozen-reference verification scripts are untouched.

`receipt.json` records the commit, pristine archive/source hashes, original CLI and wrapper hashes, exact commands, toolchain, generated projects, local dependencies, DLLs, output artifacts and build log. Source/dependency/compiler/generated-input mutations, absent inputs, missing toolset, failed compilation or incomplete artifacts fail the receipt. The build establishes no pixel result. The local dependency package is recorded, not claimed to be an officially qualified upstream package.

## First static comparison

After a successful build, create a fresh profile and output directory and set absolute paths to the agreed immutable assets:

```powershell
$env:OPENRCT2_ORACLE_USER_PATH = 'ABSOLUTE_FRESH_PROFILE'
$env:OPENRCT2_ORACLE_DATA_PATH = 'ABSOLUTE_PINNED_OPENRCT2_DATA'
$env:OPENRCT2_ORACLE_RCT2_PATH = 'ABSOLUTE_PINNED_RCT2'
# Set OPENRCT2_ORACLE_RCT1_PATH too if the fixture needs RCT1 assets.
& 'obj/vulkan-parity/upstream-screenshot-build-01/bin/upstream-screenshot-cli.exe' screenshot 'PARK.park' 'OUTPUT.png' WIDTH HEIGHT X Y Z ZOOM ROTATION
```

Replace the capitalized argument placeholders with the agreed numeric camera and paths. Use explicit world-centre `X Y Z`, zoom and rotation; do not assume these are the UI driver's screen-space `view-x/view-y`. Match both programs' computed viewport position and extent. At zoom zero, subtract half the output width/height from the projected centre to obtain viewport position. Prefer zoom zero for the first ordering proof, then validate each zoom explicitly.

The upstream command loads the park and renders without entering a simulation loop. It provides a fixed post-load world capture, not the paused UI driver's two warmup draws, UI composition, interpolation phases or moving-park performance. Pin park/art/object files, profile/configuration, executable/DLLs and camera before and after each run. Align weather, palette state, visibility flags and sorting policy; the ordinary upstream CLI does not expose arbitrary view flags or a sorter selector. Do not add tidy/grass/guest-hiding options unless the candidate uses exactly the same scene mutation. Compare decoded indexed pixels and palette, not only PNG file bytes. Check PNG presence, dimensions and successful decoding as well as exit status because upstream does not propagate every image-write failure.

The screenshot subcommand does not accept the global profile/data-path options and bypasses the handler that applies them. The wrapper environment variables are therefore necessary for isolated captures. A capture runner must provide its own timeout/log and input/output receipts; this builder intentionally does not execute captures.

## Why not reuse the UI driver unchanged?

The current driver requires local retained/presentation APIs absent upstream. The older frozen UI driver also references absent `gIntegratedBenchmark`, `enableHdr10Output` and `PreloaderScene::WaitForJobs`, and its integer viewport flags need conversion to upstream's typed flags. Upstream retains the SDL software display engine, so a separately adapted UI driver/present hook remains possible later. The existing frozen extraction and CLI builders deliberately verify the original `9a092745f3` package and must not be repointed or weakened to certify upstream.

## Paired static capture runner

`run-upstream-screenshot-comparison.py` accepts the upstream receipt, current screenshot-driver receipt, root shader receipt, original-art paths and explicit world `--x/--y/--z`. It creates eight serial fresh-profile processes (four rotations per renderer), compares decoded indexed/palette/RGBA buffers, hashes inputs and outputs, and preserves differences for manual review. It never treats the historical fork executable as upstream. The initial planned focus is the unchanged EverythingPark tree at tile154,210 beside elevated wooden track at155,210: camera centre `(4944,6736,336)`, zoom0, 640×480. A successful capture at this camera is bounded static evidence, not proof that the earlier reported cherry/vehicle issue was reproduced or fixed.

Executed result: `obj/vulkan-parity/upstream-vulkan-static-02/summary.json` reports zero differences in indexed pixels, palette entries and RGBA for all four rotations. Every current process activates synchronization validation and emits no validation diagnostics; all pinned inputs remain unchanged. This uses `upstream-screenshot-build-01`, current `vulkan-only-cli-build-01` and shader `build-66` receipts. No exception is needed for this sample. The first attempt failed before capture because the wrapper required a nonempty DLL set for a statically linked binary; its failure is retained, and the correction preserves exact equality against the build receipt's dependency set. Agent image review is recorded separately from the immutable capture receipt.

[Manual review of all four pairs and amplified diffs](vulkan-upstream-static-visual-review.md) passes for this bounded static sample. It confirms populated relevant views, not the originally reported moving defect or complete world parity.
