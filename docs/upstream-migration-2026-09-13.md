# Upstream migration — 2026-09-13

Active implementation branch: `codex/catching-up-to-upstream`.
Frozen baseline: `e874ceb7708974e00040419d30390686c52a5d31`. Frozen target: `15d4b5e933555913d216f4548f673cd55cfb0579`.

## Authorization and execution contract

On 2026-09-13 the owner approved every recommendation in the gameplay review and port plan. D01 retains negative-G warnings; D02 retains the fork station margin; D11–D14 preserve the established fork models. All other recommended adoptions/adaptations are approved. These decisions do not need to be asked again.

The newer owner instruction supersedes the original grouped port plan: process each next source commit in parent-before-child ledger order, inspect it, manually port the applicable changes, then record exactly that source commit in ancestry. Verify the remaining count decreases by one. Do not push or deploy.

Validation cadence was clarified by the owner: use source/diff/ancestry checks per commit, targeted tests where behavior warrants them, and compile/fix/test at coherent batch checkpoints. Intermediate source commits can carry explicitly logged build/test debt; clear that debt before completion. No per-commit solution rebuild or deployment is required.

## Migration-wide implementation decisions

- Receipt commits use the explicitly staged, manually edited tree, with the previous fork HEAD as first parent and exactly one reviewed upstream SHA as second parent. `git commit-tree` and compare-and-swap `git update-ref` avoid starting a recursive merge or disturbing unrelated work. Active hooks/signing were checked before choosing this mechanism; none were configured. The source ancestry reachable beyond the old HEAD is checked to contain exactly one commit before writing each receipt.
- Every receipt includes an `Upstream-Commit` trailer. A commit cannot contain its own hash; the newest journal entry identifies its receipt by that unique trailer. The next receipt fills in the previous actual hash. Git history remains the authoritative immutable mapping.
- The four review artifacts are checkpointed with U001. Unrelated edits to the Vulkan architecture and exclusive-migration plan are left out of migration commits.
- Recommendations in historical review text are accepted. Later source steps must apply only their current commit delta, not prematurely copy final upstream blobs containing unreviewed later changes.

## Baseline validation

- Release x64 MSVC/Vulkan `openrct2.proj` build at the frozen source baseline passed: 0 warnings, 0 errors, 15.53 seconds. Log: `obj/upstream-audit/baseline-build.log` (local scratch, not committed). This does not establish a fresh test-suite baseline.

## Progress: 12 / 361 source commits recorded

A row with pending checks records source integration, not a claim that runtime validation passed. Receipt hashes are resolved from first-parent history; source-attributed checks and later batch evidence remain traceable.

### U001 — `e555c76500` — Prevent setting invalid (not unknown) ride types (#26826)

- **Source:** `e555c76500b49a84298d8827190e788512c760d8`.
- **Fork receipt:** `df0ec5b8f0456c50ce0040363c85d3d57aee0a60`.
- **Remaining:** 361 → 360.
- **Disposition:** manually-ported.
- **Manual changes:** Added the upstream out-of-range ride-type Query guard and changelog entry at the fork owner. Added a focused regression for rejected values, unchanged ride type, valid type query and cheat gating. Checkpointed the approved review documents and migration journal.
- **Additional decisions / behavior:** Preserved existing Execute invalidation, sampled ratings, operations and cheat behavior. Invalid values use upstream's disallowed result. No new owner choice. All planning recommendations are approved; initial review documents are included without unrelated Vulkan documentation edits.
- **Verification:** Inspected full upstream patch and fork action path; source/diff checks. Baseline Release/Vulkan build passed before source edits. Per-receipt ancestry gate requires exactly this one missing source commit.
- **Pending / concerns:** New focused PlayTests.ArbitraryRideTypeCheatRejectsOutOfRangeTypes and changed action await first batch test build/run; no runtime result claimed.

### U002 — `d81c3a7f14` — Fix #26842: Best staff award doesn't need one of each staff type (#26843)

- **Source:** `d81c3a7f144148775f8b539dd4fe809c526e0c1b`.
- **Fork receipt:** `f92a9caf94cfc9a665c9d0cc3034a8b53b401edc`.
- **Remaining:** 360 → 359.
- **Disposition:** manually-ported.
- **Manual changes:** Best Staff now requires all four staff types in addition to the existing minimum count and guest ratio. Added the upstream changelog entry and advanced the fork protocol revision from 3 to 4.
- **Additional decisions / behavior:** Applied approved D04. Preserved andersonhk network flavor. The award check performs one linear staff traversal when this award is evaluated; it does not alter per-tick handyman service reservations. No new owner choice.
- **Verification:** Inspected all three upstream file deltas; fork award logic matches the source intent while retaining protocol flavor. Source/diff checks and exact-one-source ancestry validation.
- **Pending / concerns:** Award count/type boundary runtime checks and compilation deferred to the first batch checkpoint, together with U001 targeted regression.

### U003 — `d181e640ac` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `d181e640ac5283c9530b0ceb26f447b503b83122`.
- **Fork receipt:** `713147f52983f3f799293e102601ce363bdb6376`.
- **Remaining:** 359 → 358.
- **Disposition:** manually-ported.
- **Manual changes:** Updated Swedish half/quarter-helix wording and added existing upstream IDs 7023..7038 from this source commit only.
- **Additional decisions / behavior:** The Swedish file matched the source parent exactly, so adopted the inspected resulting blob. No fork ID collision occurs at or below 7038. Did not copy later upstream translation changes.
- **Verification:** Exact source-parent blob match before edit and exact resulting source blob match after edit; existing-ID range reviewed. No code changed. Exact-one-source ancestry gate.
- **Pending / concerns:** Language parsing is included in the next batch checks; source encoding and exact bytes preserved.

### U004 — `a4f0d22988` — Fix #26288: Land bordering map edges does not blend at certain angles (#26844)

- **Source:** `a4f0d22988fd5c54cb67a142bb14c9be2c749b95`.
- **Fork receipt:** `68897c3b5878dd15f7fae2e9d42251c0fdce10aa`.
- **Remaining:** 358 → 357.
- **Disposition:** manually-ported.
- **Manual changes:** Use map coordinates and cardinal neighbor offsets in surface descriptors so map-edge terrain blends correctly at all camera rotations.
- **Additional decisions / behavior:** Presentation correction only: retained the fork surface cache and Vulkan ownership. Changed the coordinate base and its offsets together; no simulation terrain, path, or ownership mutations.
- **Verification:** Inspected full source patch and fork descriptor consumers. Manual diff matches the source hunks while retaining fork additions. Each rotation selects the four cardinal adjacent tiles; whitespace and exact-one-source ancestry checks pass before receipt.
- **Pending / concerns:** Compile at the next batch checkpoint; visual map-edge comparison at all four rotations remains pending.

### U005 — `775c4e7635` — Update backtrace for upcoming release

- **Source:** `775c4e76350382a59fbdcd5504ee9437cafc6bf1`.
- **Fork receipt:** `75891b0b9dd203b1b4e1493df8a044900f91dffa`.
- **Remaining:** 357 → 356.
- **Disposition:** manually-ported.
- **Manual changes:** Refresh the existing public Backtrace release token.
- **Additional decisions / behavior:** Keep the existing crash-reporting integration, destination and consent flow; this only updates its public upload token. Preserve the fork screenshot handling and removed OpenGL dependency. No report was sent during verification.
- **Verification:** Inspected the complete one-line upstream patch and fork Crash.cpp differences. Only the token changes; whitespace and singleton ancestry checks run before receipt.
- **Pending / concerns:** Compile at the batch checkpoint; external crash upload is not exercised.

### U006 — `9198ff13f4` — Update to objects v1.7.11

- **Source:** `9198ff13f4c9cfb0f5f786f30c6f3f7c502a1c4b`.
- **Fork receipt:** `a458814859a33b9f4ed3cf3e14c5563bd0946b3c`.
- **Remaining:** 356 → 355.
- **Disposition:** manually-ported.
- **Manual changes:** Update the object archive URL and SHA-256 together to objects v1.7.11.
- **Additional decisions / behavior:** The reviewed object release source comparison contains translation/typo changes, with no gameplay property change in the returned net patch. Keep local object precedence and all other asset pins. This receipt changes the manifest only, not a deployed installation.
- **Verification:** Inspected the full manifest patch and existing manifest; URL/hash pair matches this source commit. Earlier source review is captured in obj/upstream-audit/objects-compare.json. Whitespace and singleton ancestry checks run before receipt.
- **Pending / concerns:** Download and verify the archive hash at the dependency/batch checkpoint; manifest pin is not a claim of downloaded-byte verification.

### U007 — `cd44174f9b` — Merge branch 'develop'

- **Source:** `cd44174f9b04ee909309214fee35656a0d07f871`.
- **Fork receipt:** `956cb285b3f29f053ba21c6ee749c3c65715f965`.
- **Remaining:** 355 → 354.
- **Disposition:** merge-no-unique-source-change.
- **Manual changes:** Record the upstream develop-to-master merge; no additional source edits are required.
- **Additional decisions / behavior:** Both parents are already in fork ancestry. The merge has no unique resolution delta, so its first-parent aggregate diff must not replay already accounted changes or restore legacy fork implementations.
- **Verification:** Inspected merge parents and successful empty git show --remerge-diff using an isolated scratch object directory. git rev-list HEAD..source returns only this merge. Receipt checks exact one-less ancestry.
- **Pending / concerns:** No new validation debt; earlier port checks remain pending.

### U008 — `4a7ee146ca` — Release v0.5.4

- **Source:** `4a7ee146caab8888eb31e56a33c0559db89b17bd`.
- **Fork receipt:** `fe03c6d1f0703eddbd7770a1fbb674dbfe19f3c0`.
- **Remaining:** 354 → 353.
- **Disposition:** manually-ported.
- **Manual changes:** Advance release metadata to 0.5.4 and Android code 24, record upstream release notes, and reset the stream revision for the new release version.
- **Additional decisions / behavior:** Retain andersonhk in the network stream ID and all fork tick-clock/player-list logic. Resetting revision 4 to 0 is safe across the distinct 0.5.4 release string: it cannot match the prior 0.5.3 fork stream. Release notes describe upstream history; their legacy maze claim does not override the fork maze model. CI edits only update the version variable and do not trigger a release here.
- **Verification:** Inspected all eight source-file changes and fork differences. Explicit metadata substitutions preserve fork CI removal of OpenGL. Debian and appdata files matched their source parent before taking this commit's reviewed blobs. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Batch compile and network identity check; no cross-platform package build or deployment performed.

### U009 — `31760893d6` — Merge branch 'master' into develop

- **Source:** `31760893d61fa4fee2ed3fafc2f7db1cec6ec36d`.
- **Fork receipt:** `c9f5ebc4990c359142c032c061e9513977693f6e`.
- **Remaining:** 353 → 352.
- **Disposition:** merge-no-unique-source-change.
- **Manual changes:** Record the release master-to-develop merge; no new source delta.
- **Additional decisions / behavior:** The release changes are already manually ported in U008. Do not replay the merge aggregate diff over fork network/CI changes.
- **Verification:** Inspected merge parents and successful empty remerge diff in scratch object storage. Singleton ancestry and whitespace checked before receipt.
- **Pending / concerns:** No additional debt; earlier checks are scheduled at U010 checkpoint.

### U010 — `f475c5b949` — Start v0.5.5

- **Source:** `f475c5b94923c902580b2237c7ba54c04320dc92`.
- **Fork receipt:** `c9446e6d6f178155a8680fcdc6dda5fb34b70a59`.
- **Remaining:** 352 → 351.
- **Disposition:** manually-ported.
- **Manual changes:** Start the 0.5.5 development changelog section.
- **Additional decisions / behavior:** Version macros remain at the latest released version as upstream intends; this commit adds only a changelog heading.
- **Verification:** Inspected the complete three-line patch and manually inserted the section. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** First batch build and targeted tests follow this receipt.

### U011 — `c51e3a3e40` — Move ride type selection to operations tab; add visibility button to appearance tab

- **Source:** `c51e3a3e4000ddd0435304c4f0eb587cd8cc14a6`.
- **Fork receipt:** `27aa32a78764407a27ab28aaf9220915b067d4a0`.
- **Remaining:** 351 → 350.
- **Disposition:** manually-ported-with-fork-adaptation.
- **Manual changes:** Move ride type selection to Operations and visibility to Appearance, with the new icon. Relocate all 42 fork strings to 8000–8041 before adding upstream 7039. Preserve fork maze policy, block layout, price targets, directed-leg statistics and longitudinal-G warnings.
- **Additional decisions / behavior:** Approved D06. The collision map is docs/upstream-fork-string-map-2026-09-13.md: new ID = old ID + 961. The unused range is below the object allocator at 8192. No string-number save migration is needed: thought persistence uses PeepThoughtType and symbolic display lookup; no other numeric consumers were found in source/tests/script declarations. External scripts hardcoding private fork text IDs need the documented relocation. Preserve English fallback and fork changes to old IDs 1647/1651. Only this commit's visibility scope is ported; later shop/facility refinements follow in order.
- **Verification:** Inspected the full source patch, all changed fork UI areas and localization allocation/persistence. All 31 selected text hunks had unique exact context before edits. Verified unique English IDs, 42 relocated definitions, preserved text/placeholders, icon/manifest alignment and retained fork UI symbols. Batch 01 evidence for U001–U010 is in docs/upstream-migration-validation-2026-09-13.md: complete Release/Vulkan and test builds pass, action regression passes, nine localization tests pass, archive hash matches. Singleton ancestry and whitespace checked before receipt.
- **Pending / concerns:** Compile/resource regeneration and UI checks after this ride-tab sequence (U016). Best Staff boundary and terrain-edge visual coverage remain outstanding as recorded in the validation doc.

### U012 — `c35f71131f` — Hide operating mode 'tweak' option for shops and stalls

- **Source:** `c35f71131fed435a1f98cb31799d891e901bb5da`.
- **Fork receipt:** `this entry’s unique Upstream-Commit trailer`.
- **Remaining:** 350 → 349.
- **Disposition:** manually-ported.
- **Manual changes:** Hide the meaningless operating-mode tweak for shopStall; simplify the shop preview image offset without changing its value.
- **Additional decisions / behavior:** Hide only the shopStall case; retain the fork maze-capacity branch and operation option limits. No changes to pricing or stall throughput.
- **Verification:** Inspected both upstream hunks and fork mode switch; inserted shop case without replacing default maze logic. Offset remains four. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Ride-window batch compile/UI checks at U016.
