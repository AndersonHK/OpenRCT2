# Upstream merge manifest

> Latest review: the next 23-commit manual-port range through `69872010ae` is inventoried in
> [Upstream manual-port manifest — 2026-08-01](upstream-port-manifest-2026-08-01.md). That document is a pending review plan;
> the changes it lists have not been implemented or ancestry-merged.

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
| `609a8cb46a` | Bot integration of Localisation/master: updates station-style label formatting and ride-window group labels in 22 non-English language files. | Copied all 22 file blobs exactly from `upstream/develop`; retained the fork-owned English strings and runtime behavior separately. | The translated IDs `7033..7038` describe the already-adopted upstream ride UI, while the fork range `7039..7067`, English text, and code are untouched. |
| `6903d5310e` | Prevents guests from watching rides while underground, adds a shared underground-location query, records the change, and advances replay assets to `v0.0.97`. | Applied the gameplay guard and shared map helper at their current fork owners; copied `assets.json` and the upstream distribution changelog exactly. | The helper is independent of transport routing and topology caches, while the guest guard belongs in the established watch-ride decision path. |

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

## Follow-up upstream localisation checkpoint

After the merge above, `upstream/develop` advanced by one commit to
`609a8cb46a4f182f2f818f8463f2526e5c722900` (`Merge Localisation/master into OpenRCT2/develop`). Its parent is the previously
integrated `a770ffc04eecd4b0156db43627660e5516d498b7`; despite the subject, this is a one-parent bot integration commit. It changes
22 non-English files with 55 insertions and 23 deletions:

- `ca-ES`, `cs-CZ`, `da-DK`, `de-DE`, `eo-ZZ`, `es-ES`, `fi-FI`, `fr-FR`, `gl-ES`, `hu-HU`, `it-IT`, `ja-JP`, `ko-KR`,
  `nb-NO`, `nl-NL`, `pl-PL`, `pt-BR`, `ru-RU`, `sv-SE`, `uk-UA`, `vi-VN`, and `zh-TW`;
- the substantive changes remove embedded colour/colon formatting from translated station-style label `STR_6275` and add or
  complete the upstream ride-window group labels `STR_7033..STR_7038` (track style, maze style, shop style, vehicle style,
  operating mode, and wait and load); a few files contain only a subset of those updates or trailing-newline normalization.

All 22 worktree files hash to the exact blobs at `609a8cb46a`. The upstream commit does not contain `en-GB.txt`, C++ source,
generated tables, project files, save-format changes, or tests. It therefore has no semantic conflict with the fork's English
`STR_7039..STR_7067` range or its transport, rating, routing, UI, and persistence behavior. This checkpoint deliberately adopts
the translation files verbatim rather than introducing per-language fork divergence.

The current ancestry report is `23 1` from `git rev-list --left-right --count HEAD...upstream/develop`. The exact blob comparison
establishes content equivalence for these 22 files, but it does **not** establish merged ancestry. The build, test, and
EverythingPark results in the preceding final merge record belong to the earlier checkpoint and do not validate the current
uncommitted worktree. No build, test, simulation run, deployment, staging, or commit was performed for this documentation pass.

After the user reviews and commits the intended current worktree as a checkpoint, complete the ancestry integration in this
order:

```powershell
git merge --no-ff --no-edit upstream/develop
git rev-list --left-right --count HEAD...upstream/develop
git status --short
```

The expectation above was superseded when `upstream/develop` advanced again before the checkpoint was committed. The next
section records that additional review.

## Underground ride-watching follow-up checkpoint

`upstream/develop` now points to `6903d5310e305b4902338b2151454aec67838278`, whose parent is the already reviewed
localisation checkpoint `609a8cb46a4f182f2f818f8463f2526e5c722900`. The new commit contains five bounded changes:

- `Guest.cpp` rejects the optional watch-ride diversion when the guest's next path location is below the surface;
- `Map.cpp` and `Map.h` add `MapIsLocationUnderground(const CoordsXYZ&)`, defined by surface clearance Z exceeding location Z;
- `distribution/changelog.txt` records upstream issue `#26638`; and
- `assets.json` advances replay fixtures from `v0.0.96` to `v0.0.97` with the upstream URL and SHA-256 digest.

The worktree applies the upstream gameplay guard after the same path-tile resolution used by the source commit. The shared map
helper is placed beside the existing map ownership queries and does not duplicate the fork's topology or path-height helpers.
The asset manifest and distribution changelog hash to the exact blobs at `6903d5310e`; all 22 localisation files continue to
hash to the exact blobs at `609a8cb46a`. Existing fork changes in `Guest.cpp`, `Map.cpp`, and `Map.h` remain otherwise intact.

Validation of this uncommitted checkpoint compiled the changed core sources and linked both `tests.exe` and the Release
`openrct2.exe`. All 19 locally available replay tests pass, and the full suite passes 520/520. The sandbox could not download
the new `v0.0.97` replay archive, so those newly published fixture bytes remain an explicit post-merge dependency check; the
tests above used the previously installed replay set. `git diff --check` reports no whitespace errors.

The current ancestry report is `23 2` from `git rev-list --left-right --count HEAD...upstream/develop`. Content equivalence does
not establish ancestry. After the user commits this reviewed checkpoint, complete the merge and verify it with:

```powershell
git merge --no-ff --no-edit upstream/develop
git rev-list --left-right --count HEAD...upstream/develop
git status --short
```

With the currently fetched references, the expected divergence after that checkpoint and merge commit is `25 0`. Re-fetch and
review again if `upstream/develop` advances before the merge.

## Tile-view and maze-rating follow-up checkpoint

`upstream/develop` now points to `a85b40b3efe12090652584257ffdca4a9ee52d49`, two commits beyond the previously integrated
`6903d5310e305b4902338b2151454aec67838278`. Both commits were reviewed and integrated manually into the current fork
checkpoint; no merge, cherry-pick, staging, or commit was performed in the live repository.

### `4aa32fd978` — migrate tile scans to `TileElementsView`

The typed/range-based scans were adopted in all 18 upstream-touched files. Ten otherwise clean files match the upstream
blobs exactly: UI `Construction.cpp`, `Banner.cpp`, `Footpath.cpp`, and `TileInspector.cpp`; `ClearAction.cpp`,
`TrackRemoveAction.cpp`, `Balloon.cpp`, `Screenshot.cpp`, `Chairlift.cpp`, and world `Banner.cpp`. The redundant null check in
`WallPlaceAction.cpp` was removed because a `TileElementsView` iterator never yields null.

The remaining files retain fork behavior around the migrated scans:

- `Guest.cpp` changes only the maze-station tile scan and leaves platform staging, exact-seat boarding, and sampled maze
  movement untouched;
- `ParkFile.cpp`, `S4Importer.cpp`, and `S6Importer.cpp` adopt typed track-element loops while preserving private save versions,
  transport state, sampled ratings, and import repair;
- `ScenarioPatcher.cpp` preserves the fork's `MapTopology` invalidation context;
- `GuestPathfinding.cpp` migrates only `FootpathElementDestInDir`; transport routing, frozen topology, route fields, and exact
  destination logic remain on their fork owners;
- `LandSetHeightAction.cpp` deliberately does not use upstream's erase-during-range-for hunk. `TileElementRemove` compacts the
  tile array, after which the view iterator would increment past the element shifted into the erased address. The fork uses an
  explicit compaction-aware loop that keeps the same address after removal and therefore removes consecutive scenery correctly.

### `a85b40b3ef` — maze intensity divisor

The network stream version advances from `1` to upstream version `2`, and the upstream distribution changelog entry remains as
upstream release history. The arithmetic patch and three legacy rating-fixture edits are intentionally superseded rather than
copied. This fork's `MazeRTD` has no `BonusMazeSize` modifier: maze ratings come from guest movement samples and the aggregate
capacity scale. The unreachable `BonusMazeSize` enum value, dispatch case, declaration, and helper were deleted instead of
maintaining a corrected-but-dead formula. Fork rating fixtures remain unchanged because upstream's lower intensity values
describe the removed post-hoc modifier, not the fork's sampled maze model.

### Verification and ancestry handoff

The Release x64 Vulkan solution rebuilt successfully after sanitising stale generated PCH state, all 519 tests pass, and
`git diff --check` reports no whitespace errors. No compiler, test, or MSBuild process remains running.

Before ancestry reconciliation, `git rev-list --left-right --count upstream/develop...HEAD` remains `2 28`: content review does
not create parentage. A normal recursive merge is inappropriate for this checkpoint because it would attempt to reintroduce the
deleted maze modifier/fixtures and would conflict at the intentionally compaction-safe land-height loop. After committing the
reviewed worktree, record the two already-integrated commits with an ancestry-only merge:

```powershell
git merge -s ours --no-ff upstream/develop -m "Merge upstream/develop through a85b40b3ef (manually integrated)"
git rev-list --left-right --count upstream/develop...HEAD
git status --short
```

With the currently fetched references, the expected divergence is `0 30`: zero upstream commits behind, the existing 28 fork
commits plus the checkpoint and ancestry merge ahead. Re-fetch and review again if `upstream/develop` advances before running
the merge.
