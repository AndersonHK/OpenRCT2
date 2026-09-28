# Independent method review: historical e874 software comparison

Reviewed 2026-09-20. Scope: small source and metadata inspection only while root owns serial benchmarks. No build, test, game, executable launch, asset inventory, or benchmark was run by this reviewer. The architecture votes and history addendum were not changed.

## Conclusion

**The method is acceptable for a historical-source software-renderer comparison, with the qualifications below.** I found no renderer, scheduler, or simulation behavior change in the measurement patch. Its only semantic additions are initial/final drawable-size and cached-refresh observations and their reporting, outside the measured interval. The inspected run metadata correctly identifies a rebuilt historical executable with a measurement patch; it does not support calling that executable an untouched historical release binary.

No blocking method defect was found in the inspected run. The important reporting qualifications are: this is a rebuilt historical source revision using the recorded contemporary build environment and historical runtime package; the dependency inputs are not fully pinned by the source-build receipt; and hidden-window output/refresh observations do not prove displayed VSync pacing or scanout.

## Patch and source provenance

- Git resolves `e874ceb7708974e00040419d30390686c52a5d31`, parent `e47e74232a4118b0977e2be97d6e3bf17da3cdc9`, dated 2026-08-01, with subject `Object Placement Fix and Merges from Root`.
- `build.py` creates its source archive directly with `git archive` for that exact revision. Its retry path checks extracted original files against archive bytes before proceeding.
- The receipt contains 11,908 original and 11,908 instrumented file hashes. Comparison of those recorded maps identifies exactly three changed paths: `src/openrct2/Context.cpp`, `src/openrct2/ui/UiContext.h`, and `src/openrct2-ui/UiContext.cpp`. The receipt reports no source changes during the build. I did not rescan the entire source tree during timing.
- Independently checked the three archived source entries against the corresponding Git blobs. Raw byte hashes differ because archive text uses CRLF and Git blobs use LF; the source text is exactly equal after normalizing CRLF. The three archive-entry hashes match `originalSourceSha256`; the three actual instrumented files match `sourceSha256`.
- Independently removed only the added lines identified by `measurement-only.patch`, in memory. Each of the three resulting files exactly matches its archived original after line-ending normalization. There are no source deletions in this patch. The instrumentation process also converts those edited files to LF; this is a formatting change, not a hidden semantic change.
- The patch's actual SHA-256 matches the receipt: `d0c06c31c01108d67cdc6232ae5eeb1c43e710f6c5afdf77b12fa2a6437347ec`.

The patch adds a default `IUiContext::GetDrawableSize()` observer and an SDL implementation that obtains `SDL_GetRendererOutputSize`. It returns an empty size if unavailable rather than inventing the requested size. It records size and `GetRefreshRate()` at measurement entry and prints both again in the finish report.

In `BeginIntegratedBenchmarkMeasurement`, these queries precede timer restart and `_benchmarkPhaseStart`. In `FinishIntegratedBenchmarkMeasurement`, elapsed time and tick totals are fixed before final queries and reporting. No hot-loop observation, renderer selection, frame deadline, tick cadence, simulation mutation, or drawing command is changed. A rebuilt C++ binary is not guaranteed to have identical code layout/performance to the old binary, even when source behavior is unchanged; that is another reason to keep the rebuilt-source label.

## Build receipt and artifact identity

`build.py` serially builds core, UI, then the Windows executable in Release x64 with MSVC toolset `14.44.35207`, Vulkan enabled and Breakpad disabled. The serial MSBuild hook disables multiprocess compilation. The receipt reports successful exits for all three stages, no missing listed artifacts, and no source mutation during the build. Its scope explicitly says that no test-suite qualification is claimed.

The snapshot receipt hash was independently checked and matches `snapshot.json`:

`91ecd3fc9037923a0c21422362f761a795eb771e239aaf697af0d355096a0394`.

The recorded rebuilt executable hash is:

`55f82bd8c910d6171a181707e44b7e89a49f1dac153fc0c74a74f5c29e0e53a1`.

The snapshot artifact set contains only that executable, with no compiled shader artifacts. This does not disqualify these software-only runs: `run-historical.py` rejects every mode except the software snapshot lane, and the shared runner explicitly launches `--benchmark-renderer software`. This receipt must not be promoted to qualification of a historical Vulkan build or complete Vulkan deployment.

**Dependency provenance limit:** the build junctions `historical-e874-source/lib/x64` to the current workspace `lib/x64` and uses the installed contemporary compiler/SDK. The receipt records commands and source/artifact hashes, but does not independently inventory those link dependencies or bind them to an original e874-era toolchain. This is adequate for the narrower rebuilt-source comparison when disclosed. It is insufficient to claim bit-for-bit historical build reproduction or to attribute every observed difference solely to source revisions. The same caveat applies to any unverified compatibility assumptions between those link inputs and the historical runtime DLL package.

## Historical package qualification and runtime selection

`run-historical.py` changes only the shared runner's input-qualification functions. Its main guard restricts it to `--mode current-software`, whose name means the snapshot-executable lane in the shared runner, not the current repository revision. It does not replace timing, rendering, or simulation functions.

The adapter compares the rollback directory's complete file inventory with `deployment-manifest.json.previousFiles` before running. The shared runner repeats qualification afterward. The deployment result separately identifies the same backup path and its creation during the 2026-09-14 deployment.

I checked metadata for `perf-precatchup-old-4k-software-vsync1-01`: its recorded package inventory has exactly 4,677 entries, with no hash disagreements against the deployment manifest's 4,677 `previousFiles` entries. I did not reread or hash those assets during the benchmark. The `historical-inputs/package` junction currently targets exactly `D:\Games\Independent\OpenRCT2Mod\backup-before-upstream-20260914-073936`.

That establishes package identity against the earlier deployment inventory. It does **not**, by itself, prove that the backup's original executable was built from e874. The actual source comparison does not require that additional claim: it runs the separately archived and rebuilt e874 source with the qualified historical runtime assets and DLLs. The report should keep those two provenance claims distinct.

The distinction is visible in the inspected run:

- Old packaged executable hash: `415cf05092e774134185c312a749896d49019cff7dca74df5cbcd56560f98bcd`.
- Executable actually selected in `runtimeBefore`: `55f82bd8c910d6171a181707e44b7e89a49f1dac153fc0c74a74f5c29e0e53a1`, matching the instrumented build.
- `currentBuild` records e874, the measurement-patch hash, three instrumented paths, and the explicit limited build scope.
- `frozenReference.deployedSource` says `historical asset package; executable rebuilt independently from e874...`.
- The summary qualification says `No Gate P acceptance claim`.

These are honest machine-readable disclosures. Human-facing tables should use a label such as **“e874 historical source, rebuilt with boundary instrumentation; historical runtime package”**, rather than “untouched old release” or relying on the generic `current-software` lane name.

## Display and comparison limits

The sampled summary records SDL boundary observations of 3840×2160 initially and finally, with cached refresh 144 Hz initially and finally. The runner requires nonzero, unchanged refresh and exact requested drawable dimensions when `--require-display-evidence` is set. `displayObservation.scope` explicitly says this is not a scanout observation. The original UI's refresh getter is a cached value, so “observed boundary drawable and cached selected refresh” is more precise than a claim to have measured physical scanout rate.

The matrix scripts omit `--visible`, and the sampled workload says `visible: false`. Thus these rows can characterize hidden-window software throughput and VSync-requested behavior. They cannot establish user-visible 4K VSync pacing. FPS counters alone should not be presented as that stronger result.

The short matrix uses 100 warm-up ticks and 3,000 measured ticks, repeats old/new and VSync combinations, and reverses run order in the second repetition. The separate long script uses 2,000 warm-up plus 12,000 measured ticks at 1.75 scale and labels that lane separately. The long lane must remain distinct from scale-one results because the logical viewport workload changes even with the same physical drawable.

Root owns metric interpretation, checksum/population comparison, and final documentation. This review did not establish source-only causality for any old/new throughput difference, equal package assets across the two lanes, or equivalent simulation trajectories. Those comparisons must be supported by the run receipts, rather than inferred from the common park filename.

## Nonblocking hardening for reuse

The current package junction is correct, but `run-historical.py` only creates it when absent and qualifies `BACKUP` rather than explicitly revalidating an existing junction target. If reused later, require that target check inside the adapter so a stale redirected junction cannot select different assets from those inventoried. No such mismatch was found here.

The hard-coded “4,677” qualification text is correct for the inspected manifest; deriving the count would make a future adaptation less error-prone. Build status should continue to be described as successful compilation of the listed ordinary software-run artifact, not a passed full test or release qualification.

## Files inspected

Requested files: complete `measurement-only.patch`, `build.py`, `run-historical.py`, and parsed `receipt.json`. Additional small evidence: `snapshot/snapshot.json`, snapshot receipt hash, the three edited source files/three corresponding Git blobs and ZIP entries, relevant shared-runner qualification/selection/display-validation code, `run-matrix.py`, `run-long-scale.py`, parsed deployment manifest/result, and selected non-performance fields of one completed old-run summary. Only junction metadata was read from the package location. No asset tree or binary was rescanned.
