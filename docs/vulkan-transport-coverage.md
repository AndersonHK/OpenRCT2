# Native transport coverage checkpoint 170

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
