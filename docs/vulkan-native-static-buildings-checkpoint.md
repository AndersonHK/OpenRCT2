# Static buildings and expanded tracks checkpoint

Build112 is deployed to `D:\Games\Independent\OpenRCT2Mod` for owner testing. It adds parked flat rides/buildings, portals, station pieces and expanded curves to baseline build106 (`fbbd7c977e`), fixes station-publication CPU overhead and removes an avoidable parallel-job wait. Two clean 4K/12,000-tick runs reach 358.9–359.8 TPS / 143.9 application FPS with 0.156–0.159 ms CPU Draw. Full visual parity remains incomplete, including confirmed underground defects below. This checkpoint does not resume the paused long-lived goal.

## Checklist

- [x] Preserve the deployed ghosts/props/partial-track implementation in a commit and retain its receipts and known defects.
- [x] Capture a pristine-upstream diagnostic confirming the tall boundary-coaster spans are authored geometry; their missing connections/supports remain defects.
- [x] Build an external upstream comparison park containing 23 closed flat-ride/shop/facility/tower/maze families and three coaster station styles, ride portals and a park entrance, captured at 3840×2160 in all four rotations and zooms 0/1.
- [x] Implement the first track expansion: authoring09 admits 4,916 style/type pairs across 56 styles (previously 3,227 across 47), including quarter helices, narrow/pier stations and corrected Junior/Water S-bends/transitions. This is definition coverage, not a full raster-parity claim.
- [x] Implement standard, narrow and pier station platforms/fences/shelters from raw station state and resident rules; image qualification remains below.
- [x] Implement 19 classic flat-ride/shop/facility families, three tower families and maze walls in native GPU rules. Parked compositions omit operating animations; physical supports and full cross-object ordering remain incomplete.
- [x] Implement raw publication and native rules for ride entrances/exits and park entrance pieces, including ghost/remap/glass state. Construction and raster qualification remain below.
- [x] Preserve shared object residency and coherent immutable snapshots; discover art only for used families. No CPU instance sprite selection was added.
- [x] Test held generations, removal/reuse, construction state and catalogue preflight; keep transfers batched and bounded. Final GPU rerun and owner interaction testing remain separate gates.
- [x] Capture both original-art corpora and have agents inspect every sampled group: 216 building and 336 special-track rectangles, plus detailed portal crops.
- [ ] Close remaining supports, glyphs, contact edges and cross-owner ordering before declaring full family parity; transparent/ghost portals and all styles still need coverage.
- [x] Complete a serial 4K, 100-warmup + 12,000-tick EverythingPark run with four3000-tick windows and matching final simulation checksum (build110).
- [x] Remove the full-station polling regression and repeat clean timing: build112 CPU Draw is 0.156–0.159 ms, down from build111's 0.457 ms.
- [x] Remove the proven queued-empty-consumer stall, repeat clean endurance runs, profile remaining waits and record CPU/GPU/upload costs.
- [ ] Qualify physical displayed pacing and continue monitoring residual tails; the two clean maxima are 12.8/11.5 ms, not proof of perfect scanout.
- [x] Audit tunnels and buried objects; capture a dedicated pristine-upstream corpus with 16 cases in eight views.
- [x] Compare the underground corpus and have an agent inspect all 128 case/view samples; preserve strict failures.
- [ ] Close proven terrain/ordering defects. Tunnel cutouts and underground-view controls remain unsupported, not approved divergences.
- [x] Deploy build112 with verified hashes and preimage backups.
- [ ] Obtain owner construction/camera feedback before accepting this checkpoint; missing visual behavior remains listed.

## Animation boundary

The existing looping scenery animation is intentional and uses a shared snapshot tick plus resident frame sequences, without per-object uploads. Stateful doors still use coarser publication, and ride/entity pose streams remain future work. The [animation state contract](vulkan-animation-state-contract.md) records the current implementation, the evidence and the separate hot-state design. A parked flat ride is an explicitly temporary lack of operating animation, not a newly accepted final divergence.

## Evidence in progress

`upstream-building-fixture-build-07/receipt.json` builds the diagnostic against pristine upstream `b80a4a84e92be8e07904b38d1032d0bb88280bb4`, with no production renderer input. `building-upstream-corpus-03/capture-receipt.json` and `track-specials-upstream-corpus-02/capture-receipt.json` pin executable, seed park and outputs. Each corpus has eight 3840×2160 views, fixed weather/clock and closed rides allocated through the normal owner API. The manifests record family, placement, loaded object identity and camera. The building corpus has 27 specimens; specials has 42. Earlier fixture failures and malformed reused-ride captures are retained, not used as final qualification.

The only owner-approved visual divergence remains the map's outer cliff skirt. Banner lettering, existing tree/rail and banner-pole occlusion gaps, physical supports, operating ride animation, riders and vehicles remain unwaived unless explicitly resolved and reviewed here.

## Build107 qualification and device reset

Build107 compiled with zero warnings/errors and passed all 135 focused tests with no Vulkan validation diagnostics (`building107-focused-01/summary.json`, 591.024 seconds). This includes damaged path additions, unchanged-snapshot animation with zero world uploads, and static body direction/ghost/recolour/removal/reuse checks.

The subsequent 4K EverythingPark endurance attempt (`performance-buildings107-12000-01`) failed during warmup with `vkGetFenceStatus(frame)` returning `VK_ERROR_DEVICE_LOST`. No completed timing report exists and no performance numbers are claimed. Windows recorded NVIDIA events153 followed by Display4101 (driver stopped responding and recovered) at 2026-09-24 04:49 UTC; the event XML is preserved beside the failed run. This was a recovered display-driver reset, not evidence of a new OS blue screen. Build107 was not deployed. GPU testing stopped for source/compiled-shader analysis before any retry.

The audit found large logical SPIR-V function-storage temporaries from repeated whole-array in/out helper arguments (flat builder45,436 bytes before driver optimization). This is a concrete compiler-input problem and a plausible workload contributor, not yet a proven reset cause or measured spill count. Corrections reduce the flat component bound16→8, use direct GPU append assignments, keep one private station assembly array, and simplify two-parent entrance ordering. The common linked-list sorter also receives a finite traversal budget and a CPU permutation regression. The next attempt must start with CPU/compile checks and a small original-art scene, then return to the serial 4K endurance workload only if those succeed. No driver settings or watchdog limits are changed.

## Corrected shader and external fixtures

Build108 passes 44 CPU preflight checks and the focused static-ride GPU lifecycle test without validation diagnostics. Its shader audit (`spirv-storage-build108.json`) reports24,708 logical Function bytes plus 964 Private bytes versus 93,764 Function bytes in 107; these are unoptimized declarations, not measured hardware allocation. Cold pipeline creation in the focused test remains slow (69.583 seconds including the test), so dynamic sorter/track loops receive explicit no-unroll hints for the next candidate.

Both108 original-art runs completed all eight4K views with clean Vulkan validation and unchanged inputs/artifacts: `building108-art-01` and `track-specials108-art-01`. Exact pixel comparison fails and remains a failure. Agents inspected static bodies, portals and station/curve specimens; maze local ordering and further missing Junior/Water S-bends were identified for correction. Physical supports, scrolling glyphs and cross-owner overlap remain open.

Review found two diagnostic defects, corrected before the next capture: the direct native art test cleared to index0 instead of index10 used by both actual main `DrawWorldScene` and upstream viewport rendering; and the external fixture reused partially reset ride slots, leaving stale flags/vehicle references that triggered entity-specific zoom snapping. The fixture now uses the normal `RideAllocateAtIndex` owner API. Fresh upstream captures `building-upstream-corpus-03` and `track-specials-upstream-corpus-02` come from zero-warning external build07. Earlier corpus02/01 comparisons are preserved but cannot qualify a clean no-vehicle static fixture. No production zoom rule was changed to accommodate the malformed fixture.

## Build110 qualification

Build109 stopped at a GLSL reserved identifier (`active`); no GPU execution occurred. Renaming it to `activeMask` produces build110 with unchanged compiler inputs throughout the build. All 46 CPU preflight checks pass in 2.879 seconds, including all 65,536 maze wall masks in four rotations against `PaintSessionArrange`, the expanded track rules, entrance ordering, publication and usage lifetime checks. The shared GPU maze ordering retains bounded traversal and integer scratch arrays.

The build110 shader audit reports 25,584 logical Function bytes plus 964 Private bytes, versus 93,764 Function bytes in 107. Dynamic traversal loops now request no unrolling. This is compiler-input evidence only; runtime and cold-start costs must still be measured.


## Build110 endurance result and remaining regression

`performance-buildings110-12000-01` completes the matched 3840×2160, VSync, 100 warmup + 12,000 measured tick workload. Inputs and final simulation checksum match build106; final census is 17,042 guests, 2,208 staff and 2,128 vehicles. No device-loss failure occurred. This run reaches 357.408 TPS and 144.006 application FPS, CPU Draw0.599173 ms and GPU2.412606 ms; p99 frame interval9.033 ms and maximum9.502 ms. Four3000-tick windows take8.335655,8.342336,8.323975 and8.573104 seconds. Application intervals do not establish monitor scanout cadence.

The former 97 ms pacing tail does not recur in this sample, but its cause is not proven resolved. CPU drawing regresses from build106's0.133015 ms to0.599173 ms. This is under investigation before deployment; the runner's pass means reproducible execution, not automatic performance or full visual acceptance. The final fullest window is only349.932 TPS. A separate instrumented attribution run is used to locate the draw cost and is not clean performance acceptance.

Fresh build110 original-art captures (`building110-art-01`, `track-specials110-art-01`) finish all16 views with clean validation and unchanged artifacts/corpora. Full-frame exact comparison still fails. All552 fixed specimen rectangles are retained for inspection; no pixel tolerance or new divergence waiver is applied. [Static rides](vulkan-static-ride-visual-review.md), [portals](vulkan-native-entrance-visual-review.md), and [track coverage](vulkan-native-track-coverage.md) record manual findings. Maze's full-geometry fixed rectangles match exactly in all eight views after the local ordering correction.


The attribution run `performance-buildings110-profile-01` completed all ticks with the same final checksum and wrote `profile.csv`. Its runner receipt remains failed because `--compare-run` pointed at a clean run: profiling inputs differ, so its TPS must not be compared as acceptance. Mean instrumented `PaintWindows` is0.579 ms, while `ViewportPaint` is0.060 ms and `MapPresentationSnapshot::Apply` is0.064 ms. `PaintWindows` owns the snapshot boundary too; the new ride-facts capture unconditionally rebuilt and copied255 vehicle colours and station vectors at each boundary. That data preparation is being narrowed to the static-body contract and compared before copying.

- [ ] Reduce cold shader/pipeline setup cost (about71 seconds in the focused build110 GPU test); steady-state timing excludes startup.


## Build111 publication fix

The producer now compares live graphical facts with the held immutable generation before rebuilding anything, and static-body colour storage is reduced from255 to the four schemes actually consumed. Unused vehicle scheme200 no longer republishes static catalogues; authoritative moving-vehicle appearance remains future entity-stream work. High station indices remain supported, with no tick-cache shortcut. All47 focused CPU checks pass in2.975 seconds, including station254 changes, removal/reuse and held generations. Build111 passes with unchanged compiler inputs. All24 shader binaries are identical to the manually reviewed build110 (`building111-shader-identity.json`); the next clean run validates the changed CPU producer in the full park.


Build111's clean12,000-tick run (`performance-buildings111-12000-01`) reaches358.794 TPS /143.368 application FPS, CPU Draw0.456803 ms /GPU2.417310 ms. CPU Draw drops23.8% from110 but remains above106. p99 is9.073 ms; maximum37.475 ms includes a34.002 ms simulation step, so pacing remains unresolved. The fullest3000-tick window is356.075 TPS. Final checksum and workload match106. `performance-buildings111-profile-01` is a successful separate attribution run: ride-facts capture alone averages401.47 µs inside545.50 µs PaintWindows. The remaining full255-station scan is therefore being replaced by mutation-owned graphical revision tracking; no visual shader change is needed.

## Build112 qualification

Station graphical state is now private and changed through owned setters. All construction, import, save, scripting and simulation callers have migrated. A shared graphical revision advances for actual position, height or direction changes, including bulk assignment, while no-op and simulation-only changes leave it unchanged. An unchanged snapshot boundary therefore avoids scanning 255 stations per ride. Build112 passes with unchanged compiler inputs. `building112-cpu-01` passes all 244 checks in 39.708 seconds, including station 254, bulk copy/move, height/null changes, held generations, save/import, pathfinding, ride ratings and JobPool. All 24 shaders match the manually reviewed build110 (`building112-shader-identity.json`).

The pacing investigation found long simulation wall times with comparatively little main-thread CPU activity. Both staff reservations and vehicle ratings use `JobPool::ParallelFor`: after the caller claims the remaining indices, it could still wait for queued empty consumers to be scheduled. The candidate retires only those unstarted consumers once all indices are claimed, retaining the barrier for every running consumer. An independent code review found no lost-work, lifetime or lock-order defect, and a blocked-worker regression targets this specific stall. This is a demonstrated avoidable wait in the code, not yet proof that it caused every historical long frame. The next clean run and separate attribution profile must establish its effect.

The [underground audit](vulkan-underground-rendering-audit.md) distinguishes normal terrain occlusion, tunnel apertures and explicit underground-view flags. External build08 and `underground-upstream-corpus-01` pin 16 isolated cases covering slopes, terrace entrances, buried stations/buildings and stacked paths. The native comparison and manual findings are recorded below. No missing portal, support or ordering defect is waived by the accepted outer-map skirt.

## Build112 repeated endurance results

Both fresh-process runs use 3840×2160, VSync, 100 warmup and 12,000 measured ticks, with the same park, camera, final census and entity checksum as build106. Receipts are `performance-buildings112-12000-01` and `-02`.

| Metric | Build111 | Build112 run 1 | Build112 run 2 |
|---|---:|---:|---:|
| TPS | 358.794 | 358.908 | 359.836 |
| Application FPS | 143.368 | 143.892 | 143.904 |
| CPU Draw, ms | 0.456803 | 0.155750 | 0.158657 |
| GPU frame, ms | 2.417310 | 2.363451 | 2.493294 |
| Frame interval p99, ms | 9.073 | 9.156 | 9.089 |
| Worst frame interval, ms | 37.475 | 12.784 | 11.452 |
| Longest simulation step, ms | 34.002 | 9.883 | 8.247 |

CPU Draw falls about 65–66% from111, or 74% from110. It is about 0.023–0.026 ms above106 despite the new static families; GPU cost rises with the additional visible work. Run1's four 3000-tick windows reach 359.912, 359.968, 359.920 and 355.867 TPS. Run2 windows take 8.335869, 8.332857, 8.336678 and 8.343155 seconds. The 34–97 ms tails do not recur in either run, but this does not prove all historical stalls resolved or qualify physical monitor scanout.

The separate successful `performance-buildings112-profile-01` attributes average ride-material capture to 10.846 µs, down from111's 401.47 µs (97.3% lower). Instrumented PaintWindows averages138.27 µs. JobPool::Wait averages37.49 µs over28,813 calls, maximum3.101 ms; PeepUpdateAll maximum8.646 ms and VehicleUpdateAll maximum3.978 ms. These include necessary waits for already-running work. Instrumented timing is diagnostic, not substituted for the clean runs. The targeted regression establishes that queued empty consumers no longer stall behind unrelated work; the three timing samples do not uniquely attribute every older spike to that mechanism.

An agent inspected the final112 full image and four native-scale comparisons against110. All 8,294,400 palette indices are identical; all 895 RGB differences come from animated palette entries243/244. Thus the station ownership change preserved the sampled geometry exactly. This does not waive the visual gaps already present in110.

`underground112-art-01` completes all eight views with clean Vulkan validation and unchanged inputs. Exact comparison fails. All128 samples have been manually reviewed: missing path/track tunnel openings and station apertures; shallow Cinema overpainting terrain but shallow shop being over-hidden; a narrow fully buried Cinema leak at rotation1; and raised-corner path/track errors at rotations1/2. The raw source order already places the surface before those equal-height path/track records, disproving a surface-ordinal-only fix. Tunnel request/cutout rules and geometric parent/terrain ordering need separate implementations. The [audit](vulkan-underground-rendering-audit.md) pins each finding and its next test.

## Upload traffic and deployment

The separate `performance-buildings112-uploads-01` lane completes the same 12,000 ticks/checksum. Over 4,798 submitted frames it records 347,017,712 world-buffer payload bytes and 12,504 world buffer-copy calls: about70.6 KiB/frame in2.61 batched calls/frame. Atlas traffic is only5,096 bytes across the run. There are no image readback or capture requests, allocation failures, lost samples or overflow. The asynchronous bounds check reads one4-byte status scalar per frame. These counters measure API payloads, not physical PCIe bandwidth or producer-side copies; startup residency is outside the measured warmup boundary.

`deployment-buildings-build112-01/receipt.json` verifies26 qualified files, with three changed files and their preimage backups, at `D:\Games\Independent\OpenRCT2Mod`. The build receipt SHA256 is `f5ddc4d02ea53fb3b7e11e563e43bf423f3bc37f2001cc808109c6d277b2e7ca`. No user save or configuration was changed, and the game was not launched automatically. The owner should test station/portal construction, terrain edits around shallow buildings and camera changes, with the documented underground omissions expected. Next correctness work is tunnel cutouts and geometric terrain/object ordering, followed by supports/text and the remaining static variants before full parity can be claimed.
