# Upstream merge manifest

## Status and verification boundary

This manifest records the manual integration of `OpenRCT2/OpenRCT2` `develop` into this fork. It is intentionally explicit
about upstream content, fork-specific handling, and the reason for every decision so a nominally clean merge cannot hide a
discarded gameplay or performance change.

Current preparation baseline:

- fork head: `f2482fb79729965149871f61059c8a42156532e3`;
- common base: `0b0f1336b4d01e3cd6e5cd67b351ac46658b1e59`;
- user-refreshed upstream head: `a770ffc04eecd4b0156db43627660e5516d498b7`;
- available divergence: 19 fork commits and 29 upstream commits;
- three-way preview: 117 upstream-changed files, 38 overlapping file blocks, 11 textual conflict hunks across six files,
  and one genuine modify/delete conflict in `Editor.cpp`.

The user refreshed `upstream/develop` outside the Codex sandbox and confirmed this is the current head. The merge has **not
yet been executed** because the current sandbox mounts `.git` read-only. The zero-behind claim therefore remains an acceptance
gate, not a completed claim. After the source checkpoint is verified, the user will create the checkpoint commit and start a
no-auto-commit upstream merge; Codex will resolve the resulting working-tree conflicts, then the user will stage and commit
the reviewed merge.

Pre-merge source checkpoint evidence:

- normal Release and fully enabled Vulkan/direct Release configurations build without warnings;
- all 452 tests pass;
- two 2,000-tick EverythingPark runs produce the same `89b1134c...` checksum at 251.729 and 251.123 TPS;
- `git diff --check` reports no whitespace error (line-ending notices only).

This evidence belongs to the fork checkpoint before the merge. It must be repeated after conflict resolution and is not
evidence that upstream ancestry has already been integrated.

## Resolution policy

1. Take upstream structural refactors and bug fixes as the new base rather than preserving obsolete fork structure.
2. Reapply fork behavior at the new ownership boundary, especially sampled ratings, transport routing/platform staging,
   path caches, private park-file bridges, spatial audio, and Vulkan work.
3. Resolve generated/localisation tables by stable identifiers and meanings, not by choosing an entire side.
4. Preserve upstream tests and add fork tests after conflict resolution; never resolve a fixture conflict by deleting coverage.
5. Record the actual resolution and verification evidence below before claiming zero commits behind.

## Commit-by-commit integration ledger

The `Fork treatment` column is currently the reviewed plan. It must be rewritten in past tense with the actual files and
conflict decisions after the merge.

| Upstream commit | What it contains | Fork treatment | Why |
| --- | --- | --- | --- |
| `9ec5c48fe8` | Copyright-year update for GameScene files. | Take verbatim. | Metadata-only; no fork behavior overlaps. |
| `5ecc78a556` | Initial `EditorScene` class and scene ownership. | Take upstream structure; relocate any fork editor hooks rather than restoring globals. | The editor-scene boundary is the foundation for the following upstream series. |
| `ead32abd75` | Splits `clearFinances` from map clearing and clarifies landscape loading. | Take semantics; reapply fork initialization only at the separated call sites if still required. | Avoids coupling finance reset to editor map reset. |
| `b41cb2eaf3` | Adds `TrackDesignerScene` and `TrackManagerScene`. | Take upstream classes and project wiring. | These replace legacy editor-mode branches and are prerequisites for later cleanup. |
| `fb6d596ada` | Moves more editor functions into `EditorScene`. | Take upstream ownership; manually transplant fork changes from removed `Editor.cpp`. | `Editor.cpp` is deleted upstream but modified in the fork, so choosing either whole side would lose work. |
| `7fec8707dd` | Makes track designer/manager scenes inherit `EditorScene`. | Take upstream hierarchy. | Prevents duplicated editor lifecycle logic. |
| `85bcdf6bf3` | Replaces editor load functions with scene switching. | Take upstream flow; verify fork save/load and park-version hooks after relocation. | Scene switching is now authoritative, while fork persistence behavior must survive it. |
| `8455d576f7` | Moves `SetAllLandOwned` into `EditorScene`. | Take upstream location and reapply only semantic fork deltas. | Keeps editor state transitions cohesive. |
| `35180cbefe` | Folds the remaining legacy Editor unit into `EditorController`. | Accept deletion of `Editor.cpp/.h`; port fork code to controller/scene owners. | Retaining the deleted unit would create parallel editor implementations. |
| `bd3d24b181` | Documents `GameLoadInit` call expectations. | Take verbatim unless nearby fork comments require consolidation. | Clarifies a lifecycle boundary without changing fork policy. |
| `75683c6014` | Fixes save-to-scenario conversion callback. | Take upstream fix and run conversion/save migration tests. | Functional bug fix intersects the fork's private park versions. |
| `d543de0807` | Prevents `GameLoadInit` from switching away from the editor unexpectedly. | Take upstream workaround and verify editor scene persistence. | Required companion to the new scene lifecycle. |
| `cfadbe53e8` | Suppresses a GCC 15.2 false positive used by MinGW CI. | Take the narrow diagnostic handling. | Cross-platform build hygiene; do not broaden warning suppression. |
| `c50b46fc39` | Renames the scenario-editor scene accessor for clarity. | Take rename throughout fork call sites. | Keeps the merged API unambiguous and avoids a compatibility alias. |
| `9ad3b333c7` | Upgrades the GitHub release action from v2 to v3. | Take workflow change. | CI maintenance, independent of runtime behavior. |
| `b4bdc95c62` | Allows guests to puke and litter on sloped paths through `Litter.cpp`, network compatibility, assets, and changelog updates. | Take upstream fix verbatim and validate slope behavior. | Independent gameplay correction; it does not overlap the fork's guest/pathfinding implementation. |
| `229d35bce2` | Documents multi-column `ListViewItem` scripting shape. | Take declaration change. | API documentation improvement with no conflicting fork contract. |
| `33e10e4783` | Corrects IPv6 address handling in the plugin API. | Take upstream network/API fix and retain fork networking behavior. | Independent correctness fix. |
| `a46542d7dc` | Emits JSON sprite metadata when exporting a complete GX file. | Take feature and CLI wiring. | Adds round-trip data without conflicting with fork simulation changes. |
| `5fbccd8cee` | Matches Sawyer-tool creation of G1 entries. | Take upstream binary-format behavior and tests. | Required for faithful graphics round trips. |
| `9e59499360` | Adds the corresponding upstream changelog entry. | Take into the upstream distribution changelog; keep fork overhaul changelog separate. | Preserves both upstream release history and fork-specific history. |
| `30f5ab2ed2` | Merge commit for the GX/G1 round-trip series. | Retain ancestry through the upstream merge; do not duplicate child patches. | The child commits above contain the actual code changes. |
| `c5d8c31829` | Merge commit for the editor-scenes series. | Retain ancestry after resolving the series as one structural migration. | Documents the upstream feature boundary. |
| `1ef6b7e941` | Adds save-game access to the plugin API through `Game.cpp`, `ScContext.hpp`, and declarations. | Take API binding and let it reuse the fork's normal ParkFile serializer. | Script saves must serialize private fork state through the existing save path; no duplicate serializer is needed. |
| `7393fbc661` | Adds group boxes to the ride colour tab. | Take upstream layout, then reapply fork ride tabs/status without overwriting group-box geometry. | `Ride.cpp` is a major UI conflict surface. |
| `eee5ebdafd` | Simplifies two ride-window event functions. | Adopt upstream helpers and port fork behavior into them. | Avoids retaining duplicated verbose event branches. |
| `1be4d6d94b` | Converts `TileElementType` and related code to current style. | Take type/codestyle migration, then reapply fork tile/path semantics and tests. | Broad mechanical changes overlap fork pathfinding; semantics must be reviewed, not resolved wholesale. |
| `3f7eca60f2` | Reworks the ride operations tab. | Use upstream tab as the layout base; manually restore transport fare policy, platform status, service measurements, and per-leg ratings UI. | Both sides intentionally redesign the same window, so this requires a semantic merge. |
| `a770ffc04e` | Merges updated Spanish and Dutch localisation into upstream. | Take both translation updates. | This commit does not cause the English ID collision; that comes from the upstream ride colour/operations UI commits. |

## Known conflict clusters

The three-way preview against the committed fork reports 38 overlapping file blocks. Only 11 textual hunks across
`en-GB.txt`, UI `Ride.cpp`, `Guest.cpp`, `RideRatings.cpp`, `ScTile.cpp`, and `Footpath.cpp` plus the `Editor.cpp`
modify/delete case require marker resolution. Every other overlap still requires semantic review. The highest-risk clusters are:

- `src/openrct2/Editor.cpp/.h`, scene manager, and editor scenes: port the fork's cent-money `10000.00_GBP` initial-cash
  ceiling to `EditorScene::clearFinances()`, then accept upstream deletion/migration;
- `src/openrct2-ui/windows/Ride.cpp`: upstream colour/operations layout plus fork transport/rating UI;
- `src/openrct2/park/ParkFile.cpp`: upstream include/enum cleanup plus committed private versions through `60013`; re-audit
  uncommitted platform `60014` and per-leg `60015` after the source checkpoint is committed;
- `src/openrct2/ride/Ride.cpp`, `RideRatings.cpp`, and `Vehicle.cpp`: upstream cleanups plus sampled/per-leg ratings and
  transport station behavior;
- `Guest.cpp`, `Peep.cpp`, `GuestPathfinding.cpp`, `Footpath.cpp`, and `Map.cpp`: slope bug/codestyle plus fork routing caches;
- tile-element headers and scripting bindings: migrate all 32 fork-added uppercase `TileElementType` uses across ten files,
  especially fork-only `MapPathTopology.cpp`, while preserving topology invalidation hooks;
- `data/language/en-GB.txt` and `UiStringIds.h`: retain upstream IDs `7033..7038`, move the fork's 16 colliding strings to a
  fresh contiguous block after the refreshed upstream maximum, and update every consumer;
- Visual Studio project files: only `libopenrct2.vcxproj` overlaps the committed preview; union upstream editor-scene sources
  with fork scheduling/audio/topology sources, then reapply uncommitted route-cache and Vulkan source entries.

The operations-tab conflict uses upstream layout as the base, then restores effective operation settings, maze-capacity
captions, transport fare/service panels, platform status, and per-leg ratings. `RideRatings.cpp` takes upstream lowercase enum
names while retaining fork decoration scoring, bridge-qualified paths, removal of legacy same-tile path scoring, sampled
aggregation, and cached fixed-ride context. `GameState.cpp` must preserve upstream editor-window ownership, fork total-tick
counting, and `PrepareSharedRouteFields()` immediately before `PeepUpdateAll()`.

## Final merge record

Complete this section only after Git access is restored and the live upstream head is merged.

- fetched upstream head: **pending**;
- fork checkpoint commit before merge: **pending**;
- merge commit: **pending**;
- conflicts resolved: **pending**;
- `git rev-list --left-right --count HEAD...upstream/develop`: **pending; must end with right side `0`**;
- Release build: **pending**;
- focused tests: **pending**;
- full tests: **pending**;
- EverythingPark checksum/TPS comparison: **pending**;
- deployment: **pending**.
