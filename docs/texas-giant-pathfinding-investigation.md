# Texas Giant three-tile guest loop

Investigated 2026-09-25 against workspace revision `95230ad7c7`.

The reported loop is reproducible. The exact route solver finds a valid route, but guest movement sometimes returns before consulting it. The old preference against entering a wide path conflicts with the shared exact route field.

The initial investigation below is retained as before-fix evidence. The subsequent implementation and validation are recorded next.

## Implemented fix and measured validation

`CalculateNextDestination()` now resolves a reachable exact destination before applying wide-neighbour avoidance, no-backtracking preferences, and the single-edge early return. Ride destination selection is shared with the fallback, including closest reachable and synchronized multi-station behavior. The same movement path still performs transport planning and service/weather revalidation. For leaving guests, a transport boarding goal is not mistaken for the final park entrance.

`ChooseDirection()` now accepts a permitted exact step before consulting exploratory junction history. Stale, absent, inexact, and unreachable fields retain the old bounded fallback. Banner permissions remain a hard constraint. No route-field rebuild, persistent per-guest state, or new graph search is added to the common exact step.

The native regression fixture models the two-wide avenue and cross path without shipping proprietary park data. It exercises both loop arrivals, actual guest stepping with decreasing route distance, reversal, banners, foreign queues, stale fields, unavailable targets, changed destinations, closed/aimless behavior, active transport and service closure, multi-station selection, and park exits. The existing history test now checks both exact-route precedence and legacy fallback history.

Replaying the same autosave for 4,096 ticks with the fix:

- All **20** guests initially in the trap escaped while still targeting Texas Giant, first observed outside it at ticks 32–128 (at most 3.2 nominal simulation seconds).
- Cecil N. escaped at tick **112**, still targeting Texas Giant. He subsequently changed destination; this report does not claim he reached the ride.
- Four of those 20 guests were observed reaching the Texas Giant target/queue during the run; the others changed destination before a target arrival was observed.
- Sustained trap occupancy fell from **31 to 0** guests in the autosave and **8 to 0** in the 16,384-tick original-scenario run, using the same 100-consecutive-sample criterion as the original investigation.

The fixed replay asserts that every initially trapped guest escapes. Detailed samples, including guests after their target changes, are in `artifacts/texas-fixed-autosave-tracked.csv`; the native and independent-graph reports use the `texas-fixed-autosave` and `texas-fixed-scenario` prefixes.

### Automated checks

- **64/64 focused native tests passed**, including all 12 new movement regressions, topology/route-field coverage, existing scenario pathfinding cases, and the revised history contract. Log: `artifacts/texas-final-focused.log`.
- **5/5 Python analog tests passed**. The independent graph verifier also matched all 11,578 exported nodes in each Texas park replay, with no exact-distance mismatches.
- The completed broad run excluding Vulkan-named tests (`--gtest_filter=-*Vulkan*`) ran **947 tests: 940 passed, four skipped, three failed**. The same three fail on the unchanged baseline: track-preview failure-injection parameters 1 and 2 throw access-violation exceptions, and the replay parameterized suite has no instantiated data. Logs: `artifacts/texas-nonvulkan-tests.log` and `artifacts/texas-baseline-nonvulkan-failures.log`. No new failures were observed, but the broad suite is not fully green.
- The optional full-suite attempt was stopped during its slow parameterized Vulkan sweep after **965 passes, 53 failures, and 16 skips** (`artifacts/texas-full-tests.log`). All 53 failures also reproduce on the unchanged baseline (`artifacts/texas-baseline-failures.log`). Fifty fail setup because `OPENRCT2_VULKAN_SHADER_DIRECTORY` is unset; the remaining three are the indexed-readback assertion and two scene-publication failures. The full suite did not complete, and this does not constitute a green renderer qualification.
- A hidden-window game smoke test loaded the report-time autosave, ran 100 warmup plus 1,000 measured ticks, and exited successfully after saving a final screenshot. The screenshot shows the formerly congested path clear of the trapped crowd. This confirms startup, simulation, and rendering together; guest escape is established by the native traces above. The existing renderer reports `partialRender: true`, so this is not a claim of complete visual parity.

### Performance

Five alternating baseline/candidate runs per park used the same seed, 2,000 unmeasured warmup ticks, isolated user data, and the same Release x64 compiler settings. Only measured simulation time is compared; loading, graph export, assertions, and checksum calculation are outside that interval. The baseline is revision `95230ad7c7`; the candidate adds only this production routing change. No builds or other test jobs ran alongside the measured series.

| Park | Measured ticks/run | Baseline median µs/tick | Candidate median µs/tick | Change |
| --- | ---: | ---: | ---: | ---: |
| Texas autosave | 20,000 | 257.874 | 253.946 | **−1.52%** |
| EverythingPark | 8,000 | 1,780.160 | 1,810.290 | **+1.69%** |
| Blackpool Pleasure Beach | 16,000 | 446.599 | 450.265 | **+0.82%** |

The maximum measured median increase is **1.7%**, not a substantial simulation regression in these workloads. This is a simulation CPU comparison, not a claim that unrelated renderer frame-rate targets are met. Guest trajectories and ride utilization naturally differ after the fix.

Each build reproduced its final entity checksum across all five runs for each park. Checksums intentionally differ between baseline and candidate because routing decisions change; mixed-version replay/network compatibility is not claimed. Shared direction/distance table entry counts were identical between builds (486,276; 3,611,346; 469,200 respectively), and the change adds no per-guest storage. Logs and summaries are in `artifacts/texas-performance/`. The reusable harness is `test/pathfinding-investigation/benchmark-routing.py`.

### Deployment scope

The deployment target is **`D:\Games\Independent\OpenRCT2Mod` only**. The separate `OpenRCT2` installation is unmodified upstream and must never receive this mod build.

The routing build is isolated under `artifacts/texas-routing-build`, based on `95230ad7c7`. Graphics checkpoint `d7e9d45b8e` was committed separately during this work and explicitly remains unqualified for deployment. Its code is excluded from this routing-only binary; the routing source commit does not modify or revert that separate checkpoint.

Deployed on **2026-09-25 at 08:07 PDT**. Both installed executables were backed up to `artifacts/texas-deployment-backup-20260925-080729`, then replaced and SHA-256 verified against the tested build. Only `GuestPathfinding.cpp` differs in the isolated production source tree from the archived base. The mod's existing assets were retained. Receipt: `artifacts/texas-deployment-receipt.json`.

| Deployed file | SHA-256 |
| --- | --- |
| `OpenRCT2Mod/openrct2.exe` | `C727D081CFE29A84CE611A3238B05A90AD478101F3B5D25D380C39E08DC3BD96` |
| `OpenRCT2Mod/openrct2-cli.exe` | `51CF27FBDBF89D1578BDC4AC8FE213894975EE9F444E32DF65CCB6335EB78272` |

The separate upstream executable was read only for verification and retains SHA-256 `F1513E7BFD36D243D72AB490DA50C8551FC79A724B33DC3BFA61BBB6D6B52E69`.

A second smoke test launched **the deployed mod executable with its installed assets**, using separate user data. It also completed 100 warmup plus 1,000 measured ticks and a screenshot, exiting with code 0. Both smoke runs ended at simulation tick 63,655 with entity checksum `bf47f31db36f6159000000000000000000000000`. Logs and the final image are in `artifacts/texas-deployed-smoke/`. The normal user configuration and input saves were not changed.

## Park evidence

The local autosave `autosave_2026-09-25_07-08-06.park` contains **Cecil N., entity 2996**, the guest named in the screenshot. At the first sample he is heading to Texas Giant, on tile `(55,141,14)`, with no planned transport leg. The nearby water ride is **Splash Water Falls**, ride 16. Texas Giant is ride 2; its entrance is `(53,109,22)` and resolved queue-end target is `(45,114,14)`.

Coordinates below are tile coordinates; Z is in the engine's eight-unit height steps. North means decreasing map Y, not screen-up in the isometric view.

| Tile | Wide flag | Exact distance to queue end | Exact next step |
| --- | --- | ---: | --- |
| `(55,140,14)` | Yes | 36 | North |
| `(55,141,14)` A | Yes | 37 | North |
| `(55,142,14)` B | No | 38 | North, to A |
| `(56,141,14)` C | No | 38 | West, to A |

The observed loop is **A → B → A → C → A**. The valid escape is A → `(55,140,14)` → north along the avenue.

The south branch beyond B includes Splash Water Falls' sloped queue. That queue belongs to another ride and is correctly excluded from Texas Giant's exact route field.

## Runs and results

Both inputs were loaded into the native engine with `GameLoadInit()`, seed `0x12345678 / 0x87654321`, and advanced with `gameStateUpdateLogic(false)`. The test uses a separate user-data directory and does not write either input park.

| Input | Starting tick | Ticks advanced | Guests sampled continuously inside A/B/C for at least 100 samples |
| --- | ---: | ---: | ---: |
| Report-time autosave | 62,555 | 4,096 | 31 |
| Original `Six Flags over Texas.SC6` | 843 | 16,384 | 8 |

Samples are 16 ticks apart and include only guests currently heading to Texas Giant. One hundred consecutive samples span at least 1,584 ticks (39.6 nominal simulation seconds). These counts measure observed confinement, not every affected guest in the park. Seven autosave guests remained in the three tiles at all 257 samples across the entire run. Cecil was recorded there for 117 consecutive samples, through relative tick 1,856; after that he no longer appeared in the Texas-Giant-target subset. This does not establish that he reached the ride.

For **each input**, the export contains 11,578 path nodes. An independent reverse breadth-first search found 868 nodes able to reach the Texas Giant target, with **zero distance mismatches** against the native route cache and **zero native next-step distance violations**. Connectivity is exported by the engine, so this independently checks route computation, not the correctness of every map-to-graph conversion rule.

The movement-policy analog reproduced the same three-tile loop for 1,000 decisions. Giving a valid exact step precedence over the wide-path/no-backtracking shortcuts reached the queue end in **37 steps**, with strictly decreasing distance. This was the initial validation of the proposed ordering; the native implementation and replay results are recorded above.

Native probes also called the real `ChooseDirection()` and `CalculateNextDestination()` from identical fresh guest states:

| Position / arrival | `ChooseDirection()` | Actual movement decision |
| --- | --- | --- |
| A, arrived moving west from C | North | South to B |
| A, arrived moving north from B | North | East to C |
| B, arrived moving south from A | North to A | North to A |
| C, arrived moving east from A | West to A | West to A |

Validation: **5/5 synthetic analog tests passed**, both native park diagnostics passed, and **52/52 existing topology/pathfinding tests passed** when run from `bin`, where their `testdata` directory is located. A first focused run from the repository root failed to locate that fixture; rerunning from the correct directory resolved those failures. The initial build also required explicitly selecting MSVC `14.44.35207` to match the existing libraries.

## Cause in the code

In the **before-fix revision `95230ad7c7`** of `src/openrct2/peep/GuestPathfinding.cpp`:

1. `CalculateNextDestination()` starts at line 2552. For a guest with a destination, lines 2575–2599 remove edges whose neighbour has the wide flag when any non-wide edge remains.
2. At A, north leads to another wide tile, while south and east lead to non-wide tiles. North is removed.
3. Lines 2601–2613 remove the arrival edge when another edge remains. On the two loop arrivals this leaves exactly one edge.
4. Lines 2616–2622 immediately move on that edge. Destination resolution and exact routing later in the function are never reached.
5. At B and C in the loop, the early single-edge return does not fire. `ChooseDirection()` re-reads the permitted edges, consults `MapPathRouteCache::GetNextStep()` around line 1918, and returns the shortest route back to A.

Thus two policies alternate: an older wide-path avoidance shortcut sends the guest away, and exact routing brings the guest back. Increasing heuristic search depth or rebuilding the same cache will not resolve this case. The valid next step is already available.

Thin-junction history did not rescue this loop: the relevant wide and foreign-queue neighbours prevent these positions from behaving as ordinary remembered thin junctions. Separately, the old exact-step proposal was subordinate to junction history in `ChooseDirection()`. The fix also corrects that ordering, although it was not a second independently proven cause of this particular loop.

## Original implementation plan (now implemented)

1. **Add a small native movement regression fixture before changing behavior.** Recreate the avenue/cross-path geometry with synthetic path elements, including the wide flags, and run `CalculateNextDestination()` plus actual guest stepping. Assert arrival at the destination and no repeated directed state while following a valid exact field. Test both arrivals at A. Keep the proprietary scenario out of the test repository.

2. **Resolve the active destination before applying optional movement preferences.** Introduce a shared resolution path for the current ride/entrance/queue target and active transport boarding target. Preserve ride status checks, multi-station selection, leaving/entering-park rules, weather/service revalidation, and transport planning. Do not insert an unconditional walk-to-final-ride shortcut that bypasses planned transport.

3. **Give a validated exact step priority over soft preferences.** Once the active target is resolved and a current, reachable exact field is available, apply its permitted next step before wide-neighbour filtering, no-backtracking preference, and the single-edge early return. Continue enforcing live banner permissions, height/slope connectivity, target-queue ownership, and target validity. A goal tile with no next step must retain normal arrival/queue handling. Stale, unsupported, or absent fields retain the legacy fallback; keep exact-unreachable distinct from missing data.

4. **Separate exact-route progress from heuristic exploration history.** Exact routing on an unchanged graph has a decreasing distance measure, so its valid step should not be vetoed merely because the heuristic previously tried it. Retain junction-history behavior for legacy fallback. Review and deliberately revise `SharedRouteProposalCannotBypassGuestJunctionHistory` rather than accidentally invalidating its contract. Clear/reset relevant state on target or topology changes without adding per-tick allocations.

5. **Cover integration boundaries.** Test wide-to-wide and wide-to-narrow junctions, both arrival directions, legal immediate reversals, no-entry banners, slopes/bridges, foreign queues, queue endpoints, stale/inexact topology, disconnected targets, changed targets/history, multi-station rides, transport legs, park exits, and aimless guests. The analog's banner and foreign-queue controls pass, but equivalent native movement checks are required for the fix.

6. **Replay and measure before deployment.** Repeat the autosave and original scenario runs with the production fix. Trace affected guests until they leave the loop and reach the queue or explicitly change destination. Compare the same deterministic seeds and all previously passing tests; benchmark populated parks because consulting routes before the current shortcut increases lookup frequency. Preserve existing shared fields and topology epochs instead of running a new global search per guest. Check multiplayer/replay determinism because guest choices and RNG consumption may change.

Acceptance: stable exact-route steps decrease remaining distance, the synthetic and real loops disappear, hard path restrictions and transport behavior remain correct, and CPU/memory costs remain within an agreed measured budget.

## Reproduction

Build `test/tests/tests.vcxproj` in Release/x64 with the workspace's matching MSVC and Vulkan setup. For this installation the command was:

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  test\tests\tests.vcxproj /p:Configuration=Release /p:Platform=x64 `
  /p:BuildProjectReferences=false /p:VCToolsVersion=14.44.35207 /m
```

`BuildProjectReferences=false` assumes the existing engine libraries already match the source. Omit it when they need rebuilding.

Run synthetic tests:

```powershell
python test/pathfinding-investigation/texas_pathfinding_analog.py
```

Run the native diagnostic and independent analysis against a locally owned park:

```powershell
./test/pathfinding-investigation/run-texas-investigation.ps1 `
  -Park 'C:\Users\imper\Documents\OpenRCT2\save\autosave\autosave_2026-09-25_07-08-06.park' `
  -RCT2Data 'D:\Games\GOG Games\RollerCoaster Tycoon 2 Triple Thrill Pack' `
  -Ticks 4096 -Label texas-autosave
```

For the original scenario, use its `Scenarios\Six Flags over Texas.SC6` file and `-Ticks 16384 -Label texas-scenario`. The native diagnostic is skipped in normal test runs unless its park/output environment variables are explicitly set.

Local evidence files are `artifacts/texas-{autosave,scenario}.log`, `-paths.csv`, `-guests.csv`, and `-analysis.json`; focused-suite results are in `artifacts/texas-focused-tests.log`. These generated exports remain local artifacts.

Input SHA-256 values:

- Autosave: `CF03640CD2CC3E7167890F9D8CB3E03D331CEEECC1BDE09CD167BBEB29CF99EC`
- Scenario: `229CF5777D1B90034F3B711EED45C4D6AB9A368A466FD4DB758A1EAC21632A35`

## Limits

Desktop inspection during the original investigation was denied with “Computer Use was not approved to use OpenRCT2,” so the guest-trace evidence comes from native simulation of the saved park, not instrumentation of the user's original live process. The autosave's matching guest, location, destination, and movement pattern provide a direct connection to the report. Deployment and renderer smoke-test results are recorded separately above. The original park and autosave are preserved.
