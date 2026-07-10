# Upstream merge manifest

## Status and verification boundary

This manifest records the manual integration of `OpenRCT2/OpenRCT2` `develop` into this fork. It is intentionally explicit
about upstream content, fork-specific handling, and the reason for every decision so a nominally clean merge cannot hide a
discarded gameplay or performance change.

Current integration baseline:

- fork checkpoint: `5ea568c325514f3e2217d0b43e3f4f67350d86af`;
- common base: `0b0f1336b4d01e3cd6e5cd67b351ac46658b1e59`;
- user-refreshed upstream head: `a770ffc04eecd4b0156db43627660e5516d498b7`;
- divergence before the merge: 20 fork commits and 29 upstream commits;
- three-way preview: 117 upstream-changed files, 38 overlapping file blocks, 11 textual conflict hunks across six files,
  and one genuine modify/delete conflict in `Editor.cpp`.

The user refreshed `upstream/develop`, committed the fork checkpoint, and started `git merge --no-ff --no-commit
upstream/develop`. `MERGE_HEAD` is the expected `a770ffc04eecd4b0156db43627660e5516d498b7`. All seven conflict files now have
marker-free semantic worktree resolutions. The sandbox mounts `.git` read-only, so Git will continue to report the six content
conflicts and deleted `Editor.cpp` as unmerged until the user stages the reviewed worktree. Zero-behind remains an acceptance
gate until that staging and merge commit are complete.

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

The `Fork treatment` column records what the live merge worktree retained or changed relative to each upstream commit.

| Upstream commit | What it contains | Fork treatment | Why |
| --- | --- | --- | --- |
| `9ec5c48fe8` | Copyright-year update for GameScene files. | Retained verbatim. | Metadata-only; no fork behavior overlaps. |
| `5ecc78a556` | Initial `EditorScene` class and scene ownership. | Adopted `EditorScene` ownership and did not restore legacy globals. | The editor-scene boundary is the foundation for the following upstream series. |
| `ead32abd75` | Splits `clearFinances` from map clearing and clarifies landscape loading. | Adopted the split and moved the fork's initial-cash ceiling into `EditorScene::clearFinances()`. | Avoids coupling finance reset to editor map reset while preserving fork balance. |
| `b41cb2eaf3` | Adds `TrackDesignerScene` and `TrackManagerScene`. | Retained both scenes and their project wiring. | These replace legacy editor-mode branches and are prerequisites for later cleanup. |
| `fb6d596ada` | Moves more editor functions into `EditorScene`. | Adopted upstream ownership; the only fork delta in deleted `Editor.cpp`, the `10000.00_GBP` cash ceiling, was transplanted. | Choosing either whole side would have lost either the scene migration or the fork limit. |
| `7fec8707dd` | Makes track designer/manager scenes inherit `EditorScene`. | Retained the upstream hierarchy. | Prevents duplicated editor lifecycle logic. |
| `85bcdf6bf3` | Replaces editor load functions with scene switching. | Retained scene switching; save/load and private park-version paths remain on their merged owners. | Scene switching is now authoritative without bypassing fork persistence. |
| `8455d576f7` | Moves `SetAllLandOwned` into `EditorScene`. | Retained the new owner with no duplicate fork implementation. | Keeps editor state transitions cohesive. |
| `35180cbefe` | Folds the remaining legacy Editor unit into `EditorController`. | Accepted deletion of `Editor.cpp/.h`; no stale include or legacy editor entry point remains. | Retaining the deleted unit would create parallel editor implementations. |
| `bd3d24b181` | Documents `GameLoadInit` call expectations. | Retained the upstream lifecycle documentation. | Clarifies a lifecycle boundary without changing fork policy. |
| `75683c6014` | Fixes save-to-scenario conversion callback. | Retained the fix; the merged save-migration suite passes. | Functional bug fix intersects the fork's private park versions. |
| `d543de0807` | Prevents `GameLoadInit` from switching away from the editor unexpectedly. | Retained the workaround in the new scene lifecycle. | Required companion to the new scene lifecycle. |
| `cfadbe53e8` | Suppresses a GCC 15.2 false positive used by MinGW CI. | Retained only the narrow upstream diagnostic handling. | Cross-platform build hygiene without broad warning suppression. |
| `c50b46fc39` | Renames the scenario-editor scene accessor for clarity. | Adopted the rename throughout merged call sites. | Keeps the merged API unambiguous and avoids a compatibility alias. |
| `9ad3b333c7` | Upgrades the GitHub release action from v2 to v3. | Retained the workflow update. | CI maintenance, independent of runtime behavior. |
| `b4bdc95c62` | Allows guests to puke and litter on sloped paths through `Litter.cpp`, network compatibility, assets, and changelog updates. | Retained the inclusive path-height fix, assets, compatibility change, and tests. | Independent gameplay correction; it coexists with fork routing. |
| `229d35bce2` | Documents multi-column `ListViewItem` scripting shape. | Retained the declaration documentation. | API documentation improvement with no conflicting fork contract. |
| `33e10e4783` | Corrects IPv6 address handling in the plugin API. | Retained the network/API fix without changing fork networking policy. | Independent correctness fix. |
| `a46542d7dc` | Emits JSON sprite metadata when exporting a complete GX file. | Retained the exporter feature and CLI wiring. | Adds round-trip data without conflicting with fork simulation changes. |
| `5fbccd8cee` | Matches Sawyer-tool creation of G1 entries. | Retained the binary-format behavior and tests. | Required for faithful graphics round trips. |
| `9e59499360` | Adds the corresponding upstream changelog entry. | Retained it in the distribution changelog; fork history stays in the overhaul changelog. | Preserves both upstream release history and fork-specific history. |
| `30f5ab2ed2` | Merge commit for the GX/G1 round-trip series. | Preserved its ancestry through this merge without duplicating child patches. | The child commits above contain the actual code changes. |
| `c5d8c31829` | Merge commit for the editor-scenes series. | Preserved its ancestry and resolved the series as one structural migration. | Documents the upstream feature boundary. |
| `1ef6b7e941` | Adds save-game access to the plugin API through `Game.cpp`, `ScContext.hpp`, and declarations. | Retained the binding on the normal save path, which serializes private fork state. | A second serializer would diverge from ParkFile behavior. |
| `7393fbc661` | Adds group boxes to the ride colour tab. | Retained upstream group-box geometry and renumbered fork strings after the new upstream range. | `Ride.cpp` and English IDs are shared UI conflict surfaces. |
| `eee5ebdafd` | Simplifies two ride-window event functions. | Adopted the upstream helpers and kept fork behavior in those owners. | Avoids duplicated verbose event branches. |
| `1be4d6d94b` | Converts `TileElementType` and related code to current style. | Adopted lowercase enum names, including 19 fork-only references missed by the automatic merge, while retaining topology invalidation and rating semantics. | The broad mechanical change overlapped fork path/rating code and required semantic review. |
| `3f7eca60f2` | Reworks the ride operations tab. | Used upstream layout and effective settings as the base; restored maze capacity, transport status/policies, and directed-leg UI, then normalized its measurement rows. | Both sides intentionally redesigned the same window. |
| `a770ffc04e` | Merges updated Spanish and Dutch localisation into upstream. | Retained both translation files unchanged. | The English collision came from the ride UI commits, not these translations. |

## Resolved conflict clusters

The live merge produced 11 textual hunks across `en-GB.txt`, UI `Ride.cpp`, `Guest.cpp`, `RideRatings.cpp`, `ScTile.cpp`, and
`Footpath.cpp`, plus the `Editor.cpp` modify/delete case. The marker-free worktree resolution was reviewed as follows:

- editor ownership: accepted deletion of `Editor.cpp/.h`, retained all upstream scenes, and moved the fork's sole legacy delta,
  the cent-money `10000.00_GBP` initial-cash ceiling, into `EditorScene::clearFinances()`;
- ride UI: retained upstream colour/operations group layout, fork effective-operation and maze-capacity behavior, transport
  fare/platform/service panels, and the endpoint-keyed directed-leg selector; compact black leg text and the visible conservative
  summary were replaced by standard white-label/black-value rows;
- ratings and persistence: adopted lowercase enum names while retaining sampled and directed-leg aggregation, physical arrival
  publication, speed^1.5, bridge/decor scoring, local-context cache invalidation, private versions `60014/60015`, sanitation,
  and non-mutating older-target export;
- routing/world: retained the upstream sloped-path litter fix and lowercase enums while preserving transport planning, Phoenix
  fallback, topology classification/invalidation, live slope validation, and shared route-field preparation immediately before
  `PeepUpdateAll()`;
- enum migration: converted the 19 fork-only uppercase references that the automatic merge could not touch; no uppercase
  `TileElementType` use remains under `src` or `test`;
- localisation: retained upstream IDs `7033..7038`, moved 27 live fork strings into contiguous IDs `7039..7065`, and removed
  the obsolete compressed-leg and compatibility-summary aliases;
- project files: retained every upstream editor-scene source and every fork spatial-audio, timing, topology, route-cache,
  GPU, and Vulkan source exactly once. Both Visual Studio projects parse without duplicate compile/include entries.

## Final merge record

GitHub Desktop committed the resolved merge with the expected fork and upstream parents. The remaining follow-up diff contains
the post-resolution enum, string-ID, editor-owner, build-file, UI-documentation, and manifest refinements verified below.

- fetched upstream head: `a770ffc04eecd4b0156db43627660e5516d498b7`;
- fork checkpoint commit before merge: `5ea568c325514f3e2217d0b43e3f4f67350d86af`;
- merge commit: `17811d45bd7af2695ddfe9cae867d39ceba64a8a`;
- conflicts resolved: **seven files resolved and committed; no unmerged index entries or markers remain**;
- `git rev-list --left-right --count HEAD...upstream/develop`: **`21 0`**;
- Release build: **normal and fully enabled Vulkan/direct solution builds pass without compiler/linker warnings**;
- dependency refresh: **upstream replay `v0.0.96` download was unavailable in the sandbox; existing replay fixtures pass**;
- focused tests: **204/204 pass**;
- full tests: **452/452 pass**;
- EverythingPark checksum/TPS comparison: **261.961/263.398 TPS, 3.699/3.692 ms median, matching `72638ee2...`**;
- deployment: **pending**.
