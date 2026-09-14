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

## Batch 11 — naming checkpoint U083

Full Release x64 MSVC/Vulkan build passed. All 16 selected tests passed from `bin`: sprite details/build, failed sprite build preserving prior output, image import, scripting and widget state. Build log: `obj/upstream-audit/batch-11-import-names-build.log`; test XML: `obj/upstream-audit/batch-11-tests.xml`. This clears compilation debt through U083, including HTTP, palette and lighting identifier changes. The failed-build fixture deliberately reports an invalid PNG; profile/index writes remain restricted and emit diagnostics. Native non-Windows HTTP backends and interactive lighting remain unverified.

## Batch 12 — ratings and pathfinding checkpoint U097

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 79.27 seconds. All 149 selected tests passed from `bin` in 19.49 seconds across RideRatings, PathfindingTestBase, ParkFileMigration, EntityImportTests, TrainStationAssignment and RideVehicleStation. Logs: `obj/upstream-audit/batch-12-ratings-names-build.log`, `batch-12-ratings-names-tests.log` and matching test XML. This clears compilation debt through U097 and exercises fork ratings, transport routing, platform-guest round trips and older-save recovery. Source transforms also verified that no upstream rating coefficients, deleted maze-size bonus, or changed state ordinals were introduced. U096 removed pre-existing trailing spaces on six renamed modifier lines to pass the whitespace gate.

## Batch 13 — completed enum group U108

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 77.43 seconds. All 37 selected NetworkTests, ScriptingTests, EntityImportTests and WidgetStateTest tests passed from `bin` in 2.71 seconds. Logs: `obj/upstream-audit/batch-13-enum-completion-build.log`, `batch-13-enum-completion-tests.log` and matching XML. Compilation debt through U108 is clear. This includes the GameCommand ID rename and mini-golf state/animation table substitutions. Clang-tidy was not available on PATH, so the new naming rules have not had a dedicated lint run; the compiler/test pass does not claim that coverage.

## Batch 14 — header cleanup through U113

Initial full build failed with four compiler diagnostics rooted in two fork calls to `GetTrackElementDescriptor` after the proposed removal of `TrackData.h` from RideRatings.cpp. That function is declared in TrackData.h, so the direct include was restored without changing ratings code. The complete Release x64 MSVC/Vulkan rebuild then passed, 0 warnings/errors, 18.59 seconds. Logs: `obj/upstream-audit/batch-14-headers-build.log` and `batch-14-headers-rebuild.log`.

All 154 selected tests passed from `bin` in 19.58 seconds: ratings, pathfinding, park migration, entity import, station/vehicle assignment and legacy localisation. Log/XML: `obj/upstream-audit/batch-14-headers-tests.*`. A token comparison of all 160 changed U113 source paths confirmed only include directives, forward declarations and whitespace changed. No runtime expression or rating coefficient changed. Non-Windows platform compilation and interactive checks remain outside this checkpoint.

## Batch 15 — Ride/Vehicle namespace port U115

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 5.80 seconds for the final incremental pass. The initial build exposed a lost `PatrolArea` forward declaration and a fork-only scripting `::GetRide` call; the second reached the test project and exposed the unqualified station-assignment helper. Restoring/moving declarations and qualifying callers resolved those failures without changing implementations. Legacy importers now explicitly name `OpenRCT2::Ride` and `OpenRCT2::Vehicle` for destination types, leaving RCT1/RCT2 source types intact. Logs: `obj/upstream-audit/batch-15-namespace-build.log`, `batch-15-namespace-rebuild.log`, `batch-15-namespace-final-build.log`.

All 177 selected tests passed from `bin` in 20.08 seconds across ratings, pathfinding, park migration, entity import, train/station assignment, network, scripting and audio. Log/XML: `obj/upstream-audit/batch-15-namespace-tests.*`. An initial invocation used the wrong executable name and did not run tests; the successful run used `tests.exe`. Token comparison of the nine large implementation files found only namespace/qualification changes, the approved spelling correction and two include reorderings. Header review preserved every fork field and default, including longitudinal G and sampled ratings; the extracted vehicle flag list and uint32 storage are identical. A pre-existing tab in the RTD initializer was replaced with spaces to satisfy the whitespace gate. No age-value bonus, rating coefficient, timing, fare or motion expression changed. Native non-Windows and interactive validation remain outstanding.

## Batch 16 — script/window headers U116–U117

Full Release x64 MSVC/Vulkan rebuild passed, 0 warnings/errors, 36.00 seconds. Initial compilation identified the obsolete global PromptMode declaration beside fork-specific declarations and the fork replay camera dependency on Window.h. Correcting declaration ownership and retaining that direct include resolved the failures. The Game.cpp namespace directive was moved before Emscripten declarations, and the new header was registered in MSBuild. No runtime expressions changed. Logs: `obj/upstream-audit/batch-16-window-headers-build.log` and `batch-16-window-headers-rebuild.log`.

All 13 selected scripting, widget-state and network tests passed from bin (0.214 seconds); log/XML: `obj/upstream-audit/batch-16-window-headers-tests.*`. All 14 extracted type definitions were compared against the fork before this change: field order, enum values and declarations are preserved. U116 retains cstring/utility for fork tile-replacement operations. Emscripten/native non-Windows builds and interactive window/replay behavior remain unverified.

## Batch 17 — typed wall/scroll/toolbar flags through U123

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 45.28 seconds. All 50 selected widget, scripting, entity-import and pathfinding tests passed from bin in 2.81 seconds. Logs: `obj/upstream-audit/batch-17-flag-types-build.log` and `batch-17-flag-types-tests.*`. An exhaustive comparison of all 65,536 uint16 values confirms U121 retains the old 0xFF11 reset-mask effect. Source review preserves wall bit positions, DAT byte storage, JSON aliases and inverted slope default; toolbar refresh targets and order are unchanged. Interactive scrollbar/door rendering and native non-Windows builds remain unverified.

## Batch 18 — D10 clear-scenery modes U124

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 41.45 seconds, including the two source-exact new G2 icons. All 18 selected tests passed from bin in 0.374 seconds. Logs: `obj/upstream-audit/batch-18-clear-modes-build.log` and `batch-18-clear-modes-tests.*`. The new ClearScenery test serializes and reloads each of the 32 masks, checks query non-mutation and query/execute cost equality, independent walls/path/additions behavior, two adjacent wall erasures and preservation of ghost walls/paths. The fixture covers the new modes and unchanged exclusions of unrelated masks; it does not claim large/small-object removal-price coverage.

No clear-action callers were found in tracked JS/TS/JSON scripts; the local Documents/OpenRCT2/plugin tree contained no JS/TS files. Embedded park scripts were not inventoried. Approved API-117 semantics apply without a compatibility shim: mask 1 is small scenery only; callers wanting the old combined behavior use 9. UI defaults select small scenery and walls. Fork stream revision advances 4 to 5, yielding 0.5.4-andersonhk-5, because command meaning changed. Existing staff service claims are coordinate-based; bin-emptying rechecks HasAddition before using the object and resets when it is absent. No pointer/claim workaround was introduced. A directed live staff-claim test, positive removal-cost insufficient-funds combinations, embedded park-script checks and interactive new-button appearance remain validation debt.

## Batch 19 — remaining header cleanup U127–U132

Initial full build reached two diagnostics for the fork PathElement::IsBin helper after removing PathAdditionEntry.h. Restoring that direct include fixed both. The complete Release x64 MSVC/Vulkan rebuild passed, 0 warnings/errors, 18.42 seconds. Logs: `obj/upstream-audit/batch-19-world-headers-build.log` and `batch-19-world-headers-rebuild.log`. Map.cpp also retains Guard.hpp and TerrainSurfaceObject.h for fork mutation assertions and snapshot rendering, and U130 retains script profiling/assertion headers.

All 61 selected Sawyer coding, map/path topology, pathfinding, clear-scenery and scripting tests passed from bin in 1.661 seconds; log/XML: `obj/upstream-audit/batch-19-world-headers-tests.*`. The 46 U132 fork file deltas were verified to contain only includes and forward declarations. No clearance, economics, bin, routing or presentation expressions changed. Native non-Windows builds remain unverified.

## Batch 20 — typed clear flags U134

Full Release x64 MSVC/Vulkan build passed, 0 warnings/errors, 13.66 seconds. All three selected clear-scenery, scripting and network tests passed from bin in 0.377 seconds, including the all-32-mask serialization/query/execute matrix introduced with U124. Logs: `obj/upstream-audit/batch-20-clear-flags-build.log` and `batch-20-clear-flags-tests.*`. Five bit positions and the serialized/visitor uint8 payload remain unchanged; the visitor still exposes itemsToClear. The test now constructs the holder explicitly from its raw byte. Fork protocol revision remains 5. U124 behavioral validation debt remains as recorded above.


## Batch 21 — typed peep flags U135

Full Release x64 MSVC/Vulkan build passed with 0 warnings/errors in 77.30 seconds. All 151 selected tests across eight suites passed from bin in 19.859 seconds: pathfinding, ride ratings, train/station assignment, entity import, park migration, scripting and network. Logs: `obj/upstream-audit/batch-21-build.log` and `batch-21-tests.*`. Nine large fork implementation/import/test files passed a body-token comparison after normalizing the reviewed flag operations and Easter-egg setter simplification. The initial post-format comparison caught an include reordering; checking the include multiset separately restored the proof. All 32 peep flag positions retain their previous mask, including bit 31. Modern save/action/visitor/network serialization uses the same uint32 holder; the packed RCT2 source field remains raw uint32. No protocol or park version bump.

Fork fare checks, transport-route speed, optimized Easter-egg interaction mask, guest happiness/motive logic and random-call ordering are preserved. Interactive tracking/Easter-egg presentation and native non-Windows builds remain unverified.


## Batch 22 — typed park flags U136

Full Release x64 MSVC/Vulkan rebuild passed with 0 warnings/errors in 28.15 seconds. All 176 selected tests in nine suites passed from bin in 27.616 seconds, including all PlayTests payment, entrance-fee target, price, guest-generation and boarding checks, plus ratings, pathfinding, imports, park migration, scripting and network. Logs: `obj/upstream-audit/batch-22-build.log`, `batch-22-rebuild.log`, and `batch-22-tests.*`. The first build reported 72 diagnostics because the manual flag conversion removed required if-condition parentheses. Reconstructing those uncommitted conversion edits from their original fork files and retaining the parentheses fixed the syntax; no test expectations were weakened. The 53 ordinary caller-file deltas then passed body-token comparison after flag-expression normalization.

All 21 modern flag positions remain unchanged in uint64 storage; the 15 RCT1 flag positions remain in packed uint32 storage and RCT2 source flags stay raw uint32. Legacy no-money scenario translation and RCT1 anti-cheat clearing/free-entry/interest translation are retained. Temporary backups now preserve all 64 bits; unset no longer clears unused upper 32 bits as the old uint32 complement did. No defined fork flag uses those upper bits. This is an intentional preservation correction, not a newly active gameplay option. No protocol/save-version bump. Interactive scenario-editor/track-design backup restoration and native non-Windows builds remain unverified.


## Batch 23 — approved D03 water-clearance cheat U138

Full Release x64 MSVC/Vulkan rebuild passed, 0 warnings/errors, 6.97 seconds for the final incremental pass. The first build reached ten diagnostics from a test-only assumption that RideManager iterators support operator->; explicitly dereferencing the iterator fixed them. Logs: `obj/upstream-audit/batch-23-build.log` and `batch-23-rebuild.log`. Three clearance/network tests passed in 1.031 seconds, followed by all 29 map/path-topology tests in 1.218 seconds. The initial filter did not match the topology suite names; the follow-up used their actual names. Logs/XML: `batch-23-tests.*` and `batch-23-topology-tests.*`.

The new PlayTests regression uses a loaded boat ride and actual placed track. It checks ordinary and cheat-mode query results for correct water, dry land, a mismatched water level and raised terrain; every permitted placement executes and matches query cost. It then checks water removal and land raising beneath the real water-dependent track: normal mode rejects, queries do not mutate, cheat mode executes at the queried cost and leaves the expected surface height/water level. Existing map topology and all-32 clear-tool mask checks also pass.

D03 adoption only moves the three water-specific restrictions behind disableClearanceChecks. Ownership, missing-surface checks, other parameter validation, support limits and fork clearance/erasure traversal are retained. Protocol changes from 0.5.4-andersonhk-5 to 0.5.4-andersonhk-6 because action validity changes. Ordinary gameplay is unchanged. Interactive placement previews, running water rides off water and native non-Windows builds remain unverified.


## Batch 24 — wall secondary flags and separate door sound U139

Full Release x64 MSVC/Vulkan build passed (20.55 seconds initially; final incremental rebuild 7.63 seconds), 0 warnings/errors. All 51 wall import, entity import, audio-channel and spatial-audio tests passed in 2.726 seconds. Logs: `obj/upstream-audit/batch-24-build.log`, `batch-24-rebuild.log`, `batch-24-final-build.log`, `batch-24-tests.*`, `batch-24-final-tests.*`. The initial test run had 50 passes and one access exception in the new standalone wall test because it omitted the game context required by object reading. Adding the normal initialized headless context fixed the fixture. The synthetic legacy price was corrected to ten old tenths-of-a-pound units before executing tests; production cent-money conversion was unchanged.

The new test imports all 256 legacy flags2 bytes, verifies the separated sound and retained flag bits and price, then imports JSON sound values 0, 1, 2, 3, 4, 5, 255, 256 and 257 alongside transparency, animation and secondary-colour flags. The seven wall definitions with doorSound in the authoritative objects fork all use valid 1/2 values; nine terrain-edge definitions also use 1/2. No objects change or repin is needed.

Additional decision: validate wall sound indices like the existing terrain-edge reader. Valid 0/1/2 behavior remains identical. Invalid legacy mask value 3 and unsupported JSON values become no sound, avoiding an unchecked array access; JSON values no longer wrap/truncate into a valid old two-bit sound. Explicit default/reset prevents stale sound if an object is read again. Missing/non-numeric JSON sound remains none. This is a malformed-object behavior correction; door animation timing and the XXWLBR03 compatibility correction are retained. Interactive door sound/rendering and native non-Windows builds remain unverified.
