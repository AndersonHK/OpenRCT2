# Vulkan auxiliary migration and peep publication checkpoint

This continues the active migration goal after Vulkan-only selection. It advances CPU-path deletion and the raw-state publisher; it does not claim the native GPU world pass or substantial performance gain is complete. The installed checkpoint52 is unchanged.

## Latest source checkpoint: build79/run60

[Build79](../obj/vulkan-parity/build-79/receipt.json) passes with zero warnings/errors; [run60](../obj/vulkan-parity/run-60/summary.json) passes all 867 tests, including 66 parity cases, with clean validation and unchanged pinned inputs. This adds object-owned appearance mutation, independently shared peep field groups, newer-epoch resets, ordered GPU self-copy and late track-preview Submit/Wait restoration. All fifteen alias samples match exactly and pass [manual review](vulkan-custom-image-alias-visual-review.md); run58/59/60 reports are byte-identical. [Source details, preserved failures and open gates](vulkan-state-ownership-checkpoint.md).

Run58 aborted in a late-preview test configured without graphics assets; run59 completed with one stale test expectation after a notifying owner setter. Those failed receipts are preserved, with corrections qualified in run60. Large scripted images, native GPU world rendering and substantial performance remain open. Historical checkpoints below retain their original evidence scope.

## Source changes

- `Screenshot.cpp` no longer includes or constructs X8. `CaptureImage`, interactive giant screenshots and the giant CLI route share the existing bounded Vulkan offscreen tiler. Camera calculations and viewport flags are preserved. The owned final image is written only after all tiles complete; writing failures propagate to the scripted caller or the interactive error report.
- `ParkPreview.cpp` obtains its 250×200 indexed screenshot through the Context's shared render service. It validates target, completion identity, palette, dimensions and payload before copying into the serialized preview. A failed optional screenshot is logged and omitted, preserving park metadata/minimap and the ability to save. There is no Software fallback.
- Authoritative peep mutation sites now enqueue appearance/state changes without relying on screen invalidation. Coverage includes uniforms, guest accessories/clothes, costumes, scripting direction/type/colour, animation/frame/bounds changes, queue poses, direct state transitions and cross-peep effects. Entries coalesce in the existing identity queue. No population scan or lazy repaint subsystem is added.
- Four new peep regressions exercise actual production uniform actions, stationary state changes, loaded animation frames and queue arrival/waiting. Four preview regressions cover no-device omission, unavailable service, malformed sessions and actual park export/readback after simulated device loss.

The explicit remaining publication caveat is unnecessary notifications from unchanged crossing-wait animation helpers. The queue deduplicates within a publication, but repeated later publications can still copy raw records because their source tick changes. Measure and remove that redundant state work before claiming minimal bandwidth. These notifications do not enable native peep rendering: catalog/atlas lifetime, interpolation history and mixed-world GPU visibility/order remain open.

## Qualification status

### Remaining callers: work in progress after build69

**Track-preview image gate, captured before the next rewrite:** `ui-cache-build-11`, `track-preview-current-build-01` and `upstream-track-preview-build-02` pass. The two drivers compile identical diagnostic source against the current shared Vulkan implementation and pristine upstream core respectively. `track-preview-upstream-01` compares all twelve 370×217 previews from flat splash-boat designs of lengths 1, 35 and 55, at all four rotations: zero indexed, palette or RGBA differences, one shared Vulkan service/device, twelve owned completions and clean validation. [Independent visual review](vulkan-track-preview-visual-review.md) passes every full pair and normal/amplified diff; original receipts are unchanged. The zoom numbers are recipe expectations, not separately observed viewport values. This qualifies that synthetic track corpus only, without scenery, other track families or an interactive install/list session. It applies to the build75-era captured code, not the subsequent unqualified state/self-copy rewrite.

**Historical build75 regression checkpoint:** [build75](../obj/vulkan-parity/build-75/receipt.json) passes with zero warnings/errors. [Run57](../obj/vulkan-parity/run-57/summary.json) passes all 855 production tests, including 66 parity cases, with clean validation and unchanged pinned inputs. The experimental mixed-depth diagnostic remains an explicit separately recorded failure outside this production gate; no exception has been accepted. No deployment or performance claim accompanies this regression checkpoint.

Build74 corrected auxiliary invalidation so worker threads enqueue changes and the recording owner drains them before source lookup, without creating an unused service/device. Run56 passed 854/855 tests: the remaining nested-script assertion incorrectly expected reserved glyph indices to survive the existing default three-colour remap. Build75 changes those expectations, explicitly checks the legacy remap and loaded palette entry, and changes no production remapping. The corrected focused test and full run57 pass, including all subsequent nested/RLE, native-failure cancellation/retry and empty-clip checks. All three temporary track-world failure/restoration tests and the lazy/worker invalidation tests pass. Earlier failed receipts are preserved.

The paired external track-preview capture and manual review subsequently passed as recorded above; upstream build02 replaces build01 for that comparison. The later bounded GPU overlap batch is qualified in run60 as recorded above; large-target compatibility remains open. These open gates prevent treating this as a fully qualified deployment.

Track previews, scripted images and the default nongraphical UI no longer construct X8. Track previews stage four rotations, restore the temporary world on exceptions, and expose a logged optional-preview failure to track-list/install UI. The dummy UI rejects implicit display creation; the legacy publication test supplies its own explicit test adapter. Scripted images use owned indexed seeds and synchronous shared-service segments so nested callbacks and immediate reads do not rerun user code.

**Historical attempts before run57:** Build70 failed on a test's discarded `nodiscard` result. Build71 compiled successfully but its provenance check correctly rejected a diagnostic source change during the build. Corrected build72 passes with zero warnings/errors. Run55 preserves **848 passing tests and four failures out of 852**, with all 66 parity cases passing: three new track failure tests assumed an empty ride registry without initializing it, and the nested custom-image test exposed a real stale auxiliary atlas entry. The track fixture now initializes the full game state. The stale image requires shared-service asset invalidation, including notifications from scrolling-text paint workers and mutations after an offscreen session begins. The subsequent corrections are qualified in run57 above; run55 is not relabelled as passing.

At that earlier checkpoint the independent upstream track-preview driver build01 passed; the later build02 paired capture and review above supersede its pending status. No deployment or new throughput result is claimed. [Custom-image review](vulkan-custom-image-migration-review.md) records translated self-copy and large-target compatibility gaps. Neither is an accepted parity exception.

Build67 and corrected [build68](../obj/vulkan-parity/build-68/receipt.json) pass Windows Release x64 with zero warnings/errors and unchanged source inputs. [Run53](../obj/vulkan-parity/run-53/summary.json) passes all 845 production regressions and 66 parity cases with clean validation. This includes the four park-preview and four authoritative peep-mutation regressions. Builds and GPU work are serial.

The actual production `CaptureImage` API matches external upstream at all four ordinary rotations, 640×480 and zoom 0: indexed pixels, palette entries and RGBA pixels all match exactly in `auxiliary-capture-upstream-01`. [Manual review](vulkan-auxiliary-capture-visual-review.md) of all full pairs and amplified differences passes. Actual park-preview output also matches exactly in `auxiliary-preview-upstream-01`; [manual review](vulkan-park-preview-visual-review.md) confirms a populated 250×200 entrance scene, actual rotation 1 and zoom 1. Both reviews preserve their static scope and source-tick/camera evidence; neither establishes broad auxiliary or world-renderer completion.

**Historical failure, corrected and requalified below:** `auxiliary-capture-transparent-upstream-01` fails the transparent zoom-1 rotation-1 current capture with `GPU sprite atlas layer limit reached`. The report records one service creation and zero device creations: the failure occurs during CPU atlas preparation before device creation, so absent validation activation is a consequence, not a clean GPU-validation result. Source diagnosis identified the auxiliary service's four-layer atlas default versus the main renderer's 64-layer capacity. The correction has passed build69/run54 below; the original receipt remains failed. Corrected upstream captures, including four tiles at 4K, and the separate synthetic giant CLI corpus are qualified below; interactive-main-window qualification remains open.

The external comparator now invokes the real `CaptureImage` API and the real park-preview producer through current-only diagnostic controls. It checks configured service/device ownership, loaded original art, raw indexed output, palette, PNG RGBA, camera, source ticks, tile lifetime and validation logs. For park preview, the producer first records its unchanged entrance-derived camera; upstream then receives the equivalent explicit camera. It does not align pixels after rendering or alter the park. Frozen reference source and old artifacts remain unchanged.

### Capacity correction qualification

[Build69](../obj/vulkan-parity/build-69/receipt.json) passes with zero warnings/errors. [Run54](../obj/vulkan-parity/run-54/summary.json) passes all 846 production tests and 66 parity cases with clean validation. The new `RenderServiceProductionRecordingTest` records, cancels and retries five sprite-size classes through the actual configured factory without creating a device. This exercises the production atlas-recording path that failed in the earlier transparent capture, rather than only a separately configured test cache.

The auxiliary atlas now has the same bounded 64-layer capacity as the main renderer. At the first submitted job, the R8 atlas allocation increases from 16 MiB to 256 MiB and descriptor storage from 0.25 MiB to 4 MiB. Creation remains lazy: factory acquisition and recording/cancellation do not allocate a Vulkan device, and unused services incur no device allocation. This is an explicit capacity/resource tradeoff, not a performance improvement claim.

`ui-cache-build-10` and `auxiliary-cli-build-02` pass and provide pinned current diagnostic binaries. Historical `auxiliary-capture-transparent-upstream-01` remains failed. Corrected `auxiliary-capture-transparent-upstream-02` matches external upstream exactly at all four rotations and zoom 1, with clean validation and completed independent manual review. Those small views contain only opaque pixels despite the transparent request; they do not prove visible background transparency.

`auxiliary-capture-4k-upstream-01` also matches external upstream exactly at 3840×2160, rotation 1, zoom 1 and a transparent request. The actual `CaptureImage` API used four bounded tiles, one shared service/device, one live session at a time and four retired sessions. Indexed pixels, palette and RGBA all agree; validation is clean. Independent manual review covers the full pair, amplified diff, native-resolution horizontal/vertical seams, their intersection and the alpha boundary. The image genuinely exercises transparency: 1,539,332 alpha-zero and 6,755,068 opaque pixels, with no intermediate alpha. Crops and hashes are preserved separately in `auxiliary-capture-4k-visual-audit`; the original receipt is unchanged. This proves tiled output for that camera, not every interactive giant-capture mode.

### Giant fixture provenance preflight

`vulkan-only-giant-01` failed before any GPU case: its historical fixture receipt still pinned live language-source bytes changed by the authorized renderer-menu removal. This is a provenance preflight failure, not a giant image result. The runner now verifies 27 identical historical producer language files from the existing independent frozen extraction, requiring agreement among current/frozen producer receipts, the extraction's original-source inventory, both fixture path pins, and the accepted archive/revision hash chain. Nonmatching source versions remain live-pinned. Runtime asset roots/inventories and exact indexed/palette/alpha/RGBA comparisons are unchanged; no hash is waived and no old fixture or receipt is rewritten.

Corrected provenance attempt `vulkan-only-giant-02` and fresh-process repeat `vulkan-only-giant-03` each pass all 20 cases with exact indexed, palette, alpha and RGBA buffers against the qualified external historical frozen reference and clean validation. Repeat03 also matches giant02, with all 100 artifact hashes identical. Each run uses 68 bounded tiles, with a largest output of 6016×3488. Independent manual review passes all 20 full reference/candidate pairs and eight native-resolution seam samples, covering every rotation at zooms 0–1 and both background policies; zooms 2–3 receive full-image review. This qualifies the synthetic giant CLI corpus only. Its historical fork reference is distinct from the upstream build used for `CaptureImage`; interactive `ScreenshotGiant` with actual main-window flags/zoom remains open. The separately reviewed actual 4K `CaptureImage` result, including 1,539,332 transparent pixels, remains separate evidence.

## Paired 4K performance evidence

Fresh serial runs compare the build66 control with the build68 auxiliary/publication candidate at actual 3840×2160, VSync enabled, a hidden window, 100 warm-up ticks and 3,000 measured ticks each. Both receipts pass with matched simulation state, workload and final checksum. This pair measures ordinary rendering, not screenshot generation throughput.

| Measurement | Build66 control | Build68 candidate |
| --- | ---: | ---: |
| TPS | 68.148 | 68.073 |
| FPS | 28.895 | 28.931 |
| CPU draw (ms) | 26.990 | 26.944 |
| GPU (ms) | 1.690 | 1.694 |
| Frame interval p95 (ms) | 36.077 | 36.100 |
| Frame interval p99 (ms) | 36.407 | 36.614 |

Candidate TPS is **0.110% lower** in this pair. There is no demonstrated performance gain, and one pair does not establish statistical equivalence. Hidden-window frame intervals do not qualify displayed VSync pacing.

- [Control receipt](../obj/vulkan-parity/performance-aux-control66-01/summary.json), SHA-256 `46126bf892fe087ff4d5f19a1049eefa0ba52ccfb46f19228e5d288f822083e6`.
- [Candidate receipt](../obj/vulkan-parity/performance-aux-candidate68-01/summary.json), SHA-256 `3df15b7e9deafaa151b9207b0e411a257862bd8d59dcdad98d22a84aa9dc9434`.

The candidate is the build68 snapshot and **excludes the subsequent auxiliary atlas-capacity correction**. Build69 and run54 now pass as recorded above. These performance figures cannot be relabelled as build69 results or used to close the failed transparent-capture gate. Corrected actual-output qualification must be recorded separately.

## Remaining retirement work

- Track-design previews, scripted custom images and the dummy-context X8 construction policy are replaced in the working tree; build75/run57 pass the regression scope, while track-preview external image comparison and the script compatibility gaps remain open. Track previews stage four rotations atomically and restore temporary map/construction state. Nongraphical contexts reject implicit display creation; tests explicitly inject a legacy adapter when required. Scripted images flush/resume synchronous nested callbacks through owned Vulkan results. Overlapping translated self-blits and large targets remain known compatibility gaps, not accepted exceptions.
- In-process software oracles and residual software blitters still need externalization or removal.
- All screenshot/preview callbacks still enter CPU `ViewportRender`; native GPU world preparation must replace that shared behavior too.
- Interactive giant capture with actual main-window zoom/flags needs its own UI qualification. CLI/tiled output alone does not prove that path.
- Complete GPU world selection/expansion/projection/ordering, all families/effects/picking, actual 4K performance over at least 3,000 measured ticks and displayed frame pacing remain required.

See the [active checklist](vulkan-exclusive-migration-plan.md) and [deletion inventory](vulkan-cpu-rendering-retirement-inventory.md). None of these open requirements is waived by the narrower checks above.
