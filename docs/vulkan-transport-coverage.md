# Native transport coverage through checkpoint180
The current Miniature Railway implementation is the independent fresh family integrated for build180. It dispatches all27 original source types from raw GPU state. The older170/173 inventory and quarantine sections below are historical; their19 Miniature Railway source gaps do not describe the fresh180 implementation. Source admission is distinct from rendered state coverage.

The source inventory is in `obj/vulkan-parity/track-coverage169-inventory.json`. It compares every non-dummy getter result with the generated recipe table, excluding the independently native flat/tower families. Coverage means admitted source types, not proven pixel parity or complete runtime-state coverage.

## Implemented for this checkpoint

- [x] Lift tower base and section: native family24, separate back/front cage parents at the source bounds, all four entrance orientations, unconditional platform floor/fences, and vertical tunnel opening. Shared art is resident before poses arrive.
- [x] Chairlift station begin/middle/end: symbolic station marker8 dispatched on the GPU, original same-ride neighboring track checks at Z or Z-8, original station/fence/cage parts and ownership, and the complete four-frame bullwheel image sequence.
- [x] Chairlift bullwheel changes publish its raw rotation through the existing immutable ride-pose snapshot. They neither rebuild the image catalogue nor call the CPU painter.
- [x] Chairlift support operations retain rotated truss supports, source station colour, all-segment support state and general height. Tunnel direction follows the source axis.
- [x] Python source-authoring tests pass (79). New C++ recipe, residency and bounded-capacity tests are present; parent build/runtime qualification follows.
- [ ] Verify Lift and Chairlift closeups in all rotations, including roofs, glass, vehicles, endpoint wheels and underground transitions.

## Remaining source coverage gaps

At169,42 source-defined pairs were missing outside the independent flat owner. Lift2 and Chairlift3 are addressed here; the remaining37 are:

| Family | Missing source types | Cause |
| --- | ---: | --- |
| Miniature Railway |19| Rail/floor composition depends on whether source wooden support emission succeeds; crossings additionally depend on same-height paths and their edges. |
| Mini Golf |11| Hole/fence geometry and support-return-dependent composition. |
| Lattice Triangle Alt |3| Station source tests powered-launch mode. |
| Air Powered Vertical |3| Booster ghost/highlight state and two support-dependent slopes. |
| Reverse Freefall |1| Support-dependent slope. |

Miniature Railway needs an immutable semantic variant selected from GPU support state and raw neighboring/path state. Forcing the support boolean or merely suppressing the source branch would incorrectly choose floor ownership and break ground/elevated/crossing cases. Static source variant banks can share art residency while the shader owns the decision. This is the next bounded implementation; it is not yet done.

Monorail27/27, Suspended Monorail27/27 and Monorail Cycles10/10 already admit every source-defined type. Apparent omissions there require runtime/depth/culling comparison rather than assuming absent recipes.

## Everything Park closeups

Centers below derive from decoded ride entrances (Z336 by default), not from visual qualification. Use rotation3 first, then all rotations and zoom0/-1.

| Fixture | World center X,Y | Ride IDs |
| --- | --- | --- |
| Miniature Railway station rows |2592,2960 and2592,2672|480-487|
| Monorail station row |5520,4208|191-194|
| Suspended Monorail |3632,2832 and3920,2832|488-489|
| Chairlift station |4496,2496|513|
| Chairlift long run |5648,4288|195|
| Lift normal |4304,6352|197|
| Lift mine |5296,4688|490|
| Lift teleporter |2832,5840|491|

The decoded locator is `obj/vulkan-parity/everything-glass-locator/decoded.json`; ride-record scanning there is explicitly partial. These records do not replace a complete map parser.

## Matched170c visual review

`obj/vulkan-parity/everything170c-transport-01` contains paired upstream/native captures at rotation3. Enlarged `monorail-r3/agent-rail-closeup.png` shows the three gray beam routes continuous in both images; the absent upper rails and track through the fenced wooden station belong to Miniature Railway. Suspended Monorail's red/purple routes, curves, pillars and station rails are present. Moving vehicles differ between capture times, so those differences do not establish omissions.

`chairlift-r3/agent-center-closeup.png` identifies ride513's narrow white station underneath the large raised hill. Native floor, fence and roof are restored, but front white columns overdraw the front roof/fence compared with upstream. Station world position is(4528,2496,336); the entrance-centered(4496,2496,336) view is valid but obstructed. Rotation1 should expose it better. The height336 is confirmed by raw tile records.

`lift-r3/comparison.png` shows ride197's yellow cage and four levels restored at world(4304,6352,336). Wooden supports under neighboring raised entrance/path landings remain absent in this capture. Source inspection identifies a shared entrance support omission, not a Lift-body support recipe. The depth agent owns that correction. These are sampled findings, not whole-family parity claims.

## Miniature Railway candidate quarantined after173 device loss

The isolated candidate admits all27 Miniature Railway types, including the19 previously missing type/style pairs. Its rail and support catalogs total5492 supported pairs; all5465 previously supported non-MiniRail pairs remain identical. Build173 compiled and three small GPU tests passed, but the full Everything Park rotation3 capture returned VK_ERROR_DEVICE_LOST after compilation and atlas admission. The candidate is therefore **not qualified and is no longer in production**. All15 changed production files were restored byte-for-byte from the pre-integration172b backups; the two new shaders were archived and removed. The exact attempted source, corrected CPU test and restore hashes are under `obj/vulkan-parity/minirail-semantic-staging/quarantined-173` and `restored-174-manifest.json`. No failed-variant GPU retry is implied. Production retains the19 MiniRail gaps and37 total source-pair gaps listed above.

The quarantined candidate GPU evaluates one constant-time wooden-support acceptance result from the current tile-local support state, then selects the immutable semantic variant. Station floors explicitly belong to the source wooden-support parent. Same-height crossings choose gravel/grooves/insets from resident art using raw path edge state; CPU callers do not select individual instance images. Transparent insets use paletteDarken2. Source support-result variants share identical tunnel requests.

Proposed path-query difference (candidate only): native crossings directly ask whether any path exists at the track's world height. The old Paint.TileElement scan excludes the first element of a newly encountered height group. Native behavior removes that dependence on incidental element ordering. If this becomes visible in a sensible park, retain a matched comparison alongside this documented divergence.

CPU authoring validation:81 tests passed,1408 valid sequence/direction/support-result cases passed, and generated binary comparison found no non-MiniRail changes. Maximum expanded row is11/16 components. Build173 CPU tests had one invalid coverage assertion: source Diag25DegDownToFlat sequence0 intentionally emits only for direction3. The corrected test is archived; direct binary checks passed216 direction/support-state coverage cases and eight source-gated rows. The corrected C++ test was not rebuilt. Runtime stability diagnosis must precede another integration; matched closeups(all rotations, ground/elevated/station/crossing/underground/ghost/hidden supports) and4K performance qualification remain open. The candidate would leave18 source-pair gaps, but production still has37.


## Fresh Miniature Railway: build180 visual review

Implementation provenance: fresh authoring from commit`68b3880700` and the original `MiniatureRailway.cpp`, without consulting the quarantined family implementation. The checked-in author/test scripts are `scripts/rendering/author-native-railway.py` and `scripts/rendering/test-native-railway.py`. They cover27 track types,352 valid type/sequence/direction rows and704 support outcomes, with at most7 records per row. The immutable table is68,528bytes; raw support/path/station state selects its result on the GPU. Offline ownership, tunnel/state invariance, serialization and202,752 wooden-return cases pass.

Matched images are under `obj/vulkan-parity/everything180-contacts-01/miniature-railway-r{0,1,2,3}`. Each `comparison.png` places upstream on the left and native on the right. These are zoom0 station-row closeups from Everything Park, not dedicated exhaustive state fixtures.

| Rotation | Visually inspected findings |
| --- | --- |
|0| Ground-level railway straights and both visible return curves form continuous routes in both images. The exposed station track/floor strips beside the blue/white queue platforms are present in native, including the lower-left and upper station rows. No large missing rail segment or floor strip is visible in the exposed portions. Adjacent rides, roofs, trains and scenery obscure some portions. |
|1| Opposite-facing station rows and the broad return curves are continuous in native. The long wooden station/floor edges remain visible beside the queues, including the open upper-center/right station and the center station occupied by the red train. No new obvious rail/floor omission is visible. Train/smoke and neighboring ride animation differences are not evidence of track absence. |
|2| The unobstructed upper-right station strip, central red-train station, and lower foreground station retain their rail/floor edges in native. Exposed ground straights and the central/left return curves remain continuous. No whole-track segment or long station-floor omission is visible; trains cover substantial parts of two station tracks. |
|3| The lower-left open station track, lower foreground station and right-hand station retain their exposed rail/floor strips. Both central straight runs and the broad returns around the stations remain continuous. The red-train station is partly obscured by vehicles, while unobstructed portions agree in large geometry. No obvious missing rail segment or long floor strip is visible. |

All four rotation comparisons were inspected after their native captures completed. Ground flat/curve continuity and these station samples have positive visual evidence; this is a sampled visual pass, not pixel-exact parity or performance qualification. This review does **not** yet establish elevated wooden-support/floor composition, diagonal slope/transition floors, all27 types in each rotation, same-height path crossing gravel/groove/end-mask art, underground tunnel transitions, hidden/invisible supports, ghost/filter behavior, or roof/glass parity. Those require identifiable dedicated fixtures and matched captures; they remain explicit qualification gaps. Ordinary ground-level rail continuity must not be presented as proof of elevated floor ownership or crossing correctness.
