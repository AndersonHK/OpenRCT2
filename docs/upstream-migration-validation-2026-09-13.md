# Migration validation

## Batch 05 — after U036

Checkpoint: `8a80e52197a864e6ee65ed6498035ac9fb1bc3e6` (36 receipts; 325 remaining).

- Complete Release x64 MSVC/Vulkan build passed: 0 warnings/errors, 85.41 seconds. Log: `obj/upstream-audit/batch-05-names-build.log`.
- Source verification checked each enum rename against complete upstream parent/child files, allowing only the reviewed identifiers and whitespace. Names were applied to fork implementations while retaining numeric values and serialized indices.

## Batch 04 — UI checkpoint after U021

Code checkpoint: `f1c21f7e6598552653a24cd6d99328804682c5ee` (21 receipts; 340 remaining). U022 only added Dutch/Russian language entries during this build; C++ source remained fixed.

- Complete Release x64 MSVC/Vulkan build passed: 0 warnings/errors, 42.28 seconds. Log: `obj/upstream-audit/batch-04-ui-build.log`.
- All 16 selected tests passed: 11 widget-state tests (including hidden-control representation/state preservation), four language parser tests, and English-US ride-window fallback. Log/XML: `obj/upstream-audit/batch-04-ui-tests.*`.
- Dropdown call-site audit found no obsolete numeric flags. The inverse stay-open/autoClose predicate preserves all four combinations with touch enhancements. This source audit does not establish interactive click/release behavior.
- Still pending: interactive pricing, graph/leg, construction, scenario/guest-list and toolbar visibility transitions; earlier conversion, award and terrain visual cases. Tests do not substitute for those checks.

## Batch 03 — after U018

Source checkpoint: `2886403bc27edc170a91dc16422a5b5e65d2b6d9` (18 receipts; 343 remaining).

- Complete Release x64 MSVC/Vulkan build, including tests: passed, 0 warnings/errors, 79.19 seconds. Log: `obj/upstream-audit/batch-03-includes-build.log`.
- This validates compilation of the custom-object converter port, include cleanup with fork-only dependencies retained, and stream revision correction. Custom-object conversion round trips remain pending; no runtime conversion claim is implied by this build.

## Batch 01 — after U010

Source checkpoint: `c9446e6d6f178155a8680fcdc6dda5fb34b70a59` (10 receipts; 351 remaining).

- Release x64 MSVC 14.44.35207, Vulkan enabled: `openrct2.proj` build passed, exit 0. Fresh `test/tests/tests.vcxproj` build passed with 0 warnings/errors. Logs: `obj/upstream-audit/batch-01-build-fixed.log` and `batch-01-tests-build.log`.
- `PlayTests.ArbitraryRideTypeCheatRejectsOutOfRangeTypes`: passed against the freshly rebuilt executable. Rejects both `RIDE_TYPE_COUNT` and 255, preserves type, allows valid type with the cheat, and rejects it without the cheat. Expected rejection diagnostics are present.
- Objects v1.7.11 downloaded and independently verified: 11,071,539 bytes, SHA-256 `74e73bbd012339511bb359dd0fbb148fda899790ae37d7d69861d88183b19452`.
- Initial build attempts exposed host `PATH`/`Path` duplication and restricted dependency network access. A scratch build launcher normalizes environment key casing; the authorized dependency download and subsequent complete build succeeded. No product workaround was added.
- `LanguagePackTest.*` and `Localisation.*`: all 9 tests passed (parser and legacy encoding coverage). Log: `obj/upstream-audit/batch-01-language-tests.log`.
- Test startup reports 419 loose-object/parkobj conflicts in local `bin/data/object`; no pre-update runtime comparison was captured to date their appearance. The existing object precedence is preserved; no local object directories were deleted. The action test still passed, but these conflicts limit claims about the exact local object presentation.

Still pending: Best Staff runtime eligibility boundary coverage and map-edge visual checks in four rotations. Generic language parser tests do not replace a translated UI inspection. Crash-report upload and non-Windows packaging were not exercised.

## Batch 02 — after U016

Source checkpoint: `543cbdfa279ef9fb12f6762c05bb8e715d21f622` (16 receipts; 345 remaining).

- Complete Release x64 MSVC/Vulkan build, including tests and G2 resource generation: passed, 0 warnings/errors, 89.60 seconds. Log: `obj/upstream-audit/batch-02-build.log`.
- All 17 selected tests passed: ten widget-state tests, four language parser tests, the English-US ride-window fallback regression, and two maze-capacity policy/legacy mapping regressions. Log/XML: `obj/upstream-audit/batch-02-ui-tests.*`.
- Source checks found unique English IDs, complete 42-string relocation below the object allocator, unchanged fork text/placeholders, and aligned visibility icon manifest/symbol. The existing English fallback test now runs against the relocated symbolic IDs.
- Remaining visual cases: Operations/Appearance layouts across coaster, maze, shop and facility; toggling cheats while those tabs are open; facility sprite composition; four terrain-edge rotations. No claim of interactive visual verification is made by the widget-state suite.

## Policy correction — recorded with U018

U008 reset the stream revision from 4 to 0 with upstream release metadata. Although the full version/flavor string avoided an immediate old-release collision, this contradicted the approved plan's independent fork revision policy. U018 restores revision 4, keeps version 0.5.4 and flavor `andersonhk`, and adds an explanatory source comment. Expected identity is `0.5.4-andersonhk-4`. Subsequent upstream release resets must not reset this counter; accepted protocol changes may advance it independently. Existing receipt history and source counts are unchanged. This corrects the implementation, not the owner's approved policy.

## Batch 06 — companion objects correction after U037

The complete Windows Release x64 MSVC/Vulkan build passed with 0 warnings/errors (8.75 seconds). All 45 selected tests across AudioChannel, SpatialAudio, ScriptingTests, WidgetStateTest, RideVehicleStation and TrainStationAssignment passed. The shipped kart data test now reads the companion fork calibration, and startup no longer reports the 419 packed/loose conflicts. This supersedes the initial Batch 05 test failure and the removed temporary workaround; see [the full correction and objects ledger](upstream-objects-migration-2026-09-13.md). U037 Appearance code is included in this successful build.

The objects exporter build passed with five pre-existing target/framework compatibility warnings, documented in the correction log. CMake installed all 4,454 tracked object files into clean scratch. Windows development install verified the linked source without writes. Installer source-byte equivalence, four kart gains, mismatch rejection and repeat-install checks are recorded in local scratch. Full non-Windows application packaging and exporter pixel-output comparisons remain unverified.

## Batch 07 — UI header checkpoint U047

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 46.93 seconds. This compiles U039's bank/speed widget-type repair, U040-U045 header ownership changes (including fork-only consumers), and U046's missing-station caption guard. All 27 selected widget, language/legacy-localisation and station assignment tests passed. Logs: `obj/upstream-audit/batch-07-ui-headers-build.log` and `batch-07-ui-tests.*`. U048's text-only Esperanto addition was recorded while this build was running; no C++ edits overlapped compilation. Missing-object interactive caption drawing and construction transitions remain pending visual checks.

## Batch 08 — U049 construction control separation

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 30.83 seconds. All 18 selected WidgetStateTest, RideVehicleStation and TrainStationAssignment regressions passed. New independent speed-widget indices fit the existing 64-bit state masks, enforced by a compile-time assertion. Source review preserved speed increments/minimum/maximum, cheat minimum zero, selected-piece action dispatch and unbuilt-piece defaults. Four installer preflight fixtures also passed, and the linked companion install validated without copies. Logs: `obj/upstream-audit/batch-08-construction-build.log` and `batch-08-construction-tests.*`. Interactive geometry and global 14-pixel spinner/dropdown appearance remain visual-validation debt.

## Batch 09 — naming checkpoint U060

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 80.48 seconds. All 56 selected tests passed across PathfindingTestBase (including rain/transport pricing and sheltered-leg choice), ScriptingTests, AudioChannel, SpatialAudio, WidgetStateTest and LanguagePackTest. Logs: `obj/upstream-audit/batch-09-enum-build.log` and `batch-09-enum-tests-correct-cwd.*`.

The first test invocation ran from the repository root, so PathfindingTestBase could not find relative `testdata/parks/pathfinding-tests.sv6`; nine tests failed or raised access violations after that missing fixture. Rerunning the identical binary/filter from `bin` found the fixture and passed all 56. This was an invocation correction, with no product-code or test-expectation change. Future park-fixture suites must run from `bin`. Restricted profile/index writes still emit diagnostics; the fixture loads and assertions pass. U061 source edits began only after the U060 build finished and are not covered by this binary.

## Batch 10 — naming checkpoint U070

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 16.47 seconds. All 51 selected tests passed from the required `bin` working directory: maze capacity/import mapping and maze rating/local-context cases, scripting, audio channel/spatial audio and widget state. Logs: `obj/upstream-audit/batch-10-names-build.log` and `batch-10-names-tests.*`. This validates U061-U070 compilation and preserves the selected fork maze/audio behaviors. Replay end-to-end determinism and interactive custom-image/guest-list UI remain outside this selected suite.
