# Vulkan-only selection checkpoint — 2026-09-23

The owner moved backend removal to the start of the migration. This checkpoint removes the selectable Software renderer; it does not claim the native GPU world rewrite is complete.

## Implementation

- The graphical factory creates Vulkan directly. The renderer enum, selection getter, config field/table/read/write, benchmark override and SDL software-window branches are gone.
- `HardwareDisplayDrawingEngine.cpp` and its project entry are deleted. Options has no renderer dropdown; unused translated labels are removed and their numeric string slots remain reserved.
- An old `drawing_engine` config key is ignored, then omitted on save. LightFX, vehicle lighting and HDR preferences retain their independent values. A round-trip regression covers missing, Software, OpenGL, Vulkan and unknown values.
- Graphical CMake/MSBuild targets require Vulkan. The main runtime cannot fall back to Software. The benchmark compatibility argument accepts only `vulkan`; current-software performance runs are removed.
- The shared offscreen factory is always selected and remains lazy. CLI screenshots require it; their unused X8 initialization and software fallback are deleted. Pure simulation does not eagerly create a Vulkan device.
- Current UI/CLI diagnostic harnesses use the one-backend contract. Historical source builds retain guarded reference support. Shared-device lifecycle checks now recreate Vulkan windows instead of switching to Software.
- The frozen historical archive/package is still verified. Its verification no longer requires retaining its ten old source files in the active production tree.

## Remaining CPU work

All CPU world painting and graphics preparation is deprecated. The [retirement inventory](vulkan-cpu-rendering-retirement-inventory.md) names concrete callers and deletion boundaries, including world columns/sorting, family-specific paint functions, legacy presentation clones, picking, LightFX occlusion and scrolling text. X8 still supports auxiliary images and in-process reference tests; those dependencies must be severed before its files can be deleted.

The [revised plan](vulkan-plan-b-retained-sprites.md) publishes coherent raw state, separates shared catalogs from individual instances and slow-changing from fast-changing fields, and excludes nongraphical simulation data. The GPU selects assets/components, projects, culls and orders a complete visible world every rendered frame. There is no lazy repainting or retained world framebuffer strategy.

## Independent reference

The historical fork archive is not upstream. The new external upstream reference is pinned to `b80a4a84e92be8e07904b38d1032d0bb88280bb4`; [its dedicated builder and scope](vulkan-upstream-reference.md) preserve that distinction. Static CLI captures do not qualify moving-frame parity, UI parity or performance. Those require their own matched workloads and receipts.

The independent upstream build passes. `upstream-vulkan-static-02` compares the unchanged EverythingPark tree/wooden-track scene at 640×480, zoom zero, all four rotations. All eight isolated processes succeed; indexed pixels, palettes and RGBA match exactly in every pair, current Vulkan synchronization validation is clean, and pinned inputs are unchanged. The raw result deliberately remains `exact-match-pending-review`; manual inspection is a separate agent review record, not a modification of captured evidence. Attempt01 is preserved as a prelaunch harness failure: the statically linked upstream has no runtime DLLs, and the wrapper incorrectly required a nonempty DLL directory. Attempt02 permits the receipt's empty set while still checking exact dependency equality.

The [independent visual review](vulkan-upstream-static-visual-review.md) is complete: an agent viewed every reference/candidate pair and amplified difference image, confirmed populated relevant views and plausible static ordering, and recorded the receipt/image hashes. There are no divergences or accepted exceptions in this sample. The originally reported canopy/vehicle defect remains unproven as fixed.

## Qualification

The [checkpoint qualification receipt](../obj/vulkan-parity/vulkan-only-checkpoint-01/qualification.json) pins the build, test, UI, startup, performance, upstream and manual-review evidence. Final verification confirms all 1,986 build66 inputs and all 1,861 UI-build inputs still match their receipts. It also rechecks every upstream comparison output hash. Its passing scope is graphical backend exclusivity and the stated bounded checks, not completion of the native GPU world migration.

Windows Release x64 build65 and final build66 both pass with zero warnings/errors and unchanged source inputs. Full validation run52 passes 837/837 production tests, including 66 image-parity tests, with no failed/skipped/disabled tests in that gate and no input mutation. The previously rejected test-only mixed-depth diagnostic remains explicitly excluded and separately failing; this checkpoint does not close that issue. Eleven Python diagnostic-runner regressions also pass. The isolated UI build08 passes. `vulkan-only-options-01` passes six captures, exact repeated/restored output and clean validation; an agent [manually inspected the screen](vulkan-only-selection-visual-review.md). `vulkan-only-lifecycle-02/03` pass six captures each against a fresh external historical baseline and a fresh-process repeat, with unchanged shared-device identity, four window recreations, auxiliary output and shutdown checks. Lifecycle01 is preserved as a metadata-only failure against an older baseline lacking the current camera/asset/flag stamps; it had no pixel divergences. The fresh external baseline fixes that reference-input mismatch without relaxing the comparison.

`vulkan-only-startup-01` confirms graphical MSBuild rejects disabled Vulkan and a missing SDK; the shipping CLI rejects `--benchmark-renderer software` before creating any profile files. The ordinary Vulkan startup and benchmark pass in `obj/vulkan-parity/vulkan-only-performance-01/summary.json`, using the pinned build66 snapshot. The clean ordinary-main-loop run measures 100 warmup plus 3,000 ticks at observed 3840×2160, VSync enabled and a reported 144 Hz display:

| Metric | Build66 |
|---|---:|
| Simulation | 73.542 TPS |
| Draw rate | 29.343 FPS |
| CPU draw, including presentation | 26.218 ms |
| GPU frame | 1.661 ms |
| Frame interval p95 / p99 | 35.984 / 36.518 ms |
| Final entity checksum | `9b7eee204b0471d7000000000000000000000000` |

This is one hidden-window regression observation, not a displayed-pacing qualification or evidence of a substantial improvement. The census/checksum matches the prior 3,000-tick workload, but this run is not a newly counterbalanced comparison. No speedup follows merely from removing the selector. CPU world preparation remains the dominant cost; replacing it is still required.

The installed checkpoint52 has not been replaced. The long-lived goal remains paused as requested; this work is one explicit turn.
