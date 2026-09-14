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

## Progress: 23 / 361 source commits recorded

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
- **Fork receipt:** `357b09dc4b159203934e2cf22c14c27b472f5f68`.
- **Remaining:** 350 → 349.
- **Disposition:** manually-ported.
- **Manual changes:** Hide the meaningless operating-mode tweak for shopStall; simplify the shop preview image offset without changing its value.
- **Additional decisions / behavior:** Hide only the shopStall case; retain the fork maze-capacity branch and operation option limits. No changes to pricing or stall throughput.
- **Verification:** Inspected both upstream hunks and fork mode switch; inserted shop case without replacing default maze logic. Offset remains four. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Ride-window batch compile/UI checks at U016.

### U013 — `ea084b9828` — Keep colour tab visible for shops when ride type cheats are on

- **Source:** `ea084b982808d9b7e392a19598896a4cc92c3b33`.
- **Fork receipt:** `13c1801e0596db46b83d64314a9324a83238b912`.
- **Remaining:** 349 → 348.
- **Disposition:** manually-ported.
- **Manual changes:** Keep Operations and Appearance accessible with arbitrary ride type cheats, expose visibility for shops and rides without colour schemes, and select previews by shop category.
- **Additional decisions / behavior:** Approved D06 refinement: visibility follows cheat state, not recolourability. Retain normal tab restrictions when cheats are disabled and preserve fork gameplay controls.
- **Verification:** Inspected all source hunks and current tab/preview logic; manual edits keep the fork's unrelated layout and operation settings. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Facility preview correction is the next source; batch UI/compile checks at U016.

### U014 — `b9484d7f48` — Fix facility drawing

- **Source:** `b9484d7f485d27355b27d0d8e4d16dbfa4585988`.
- **Fork receipt:** `49fa37546a45c136eccb16ea688455fdda9b11fc`.
- **Remaining:** 348 → 347.
- **Disposition:** manually-ported.
- **Manual changes:** Draw facility previews using both building sprite pieces; retain single-image shop previews and colour remapping.
- **Additional decisions / behavior:** Presentation-only distinction follows guestsShouldGoInsideFacility. Use existing GfxDrawSprite ownership path, with no rendering-engine reintroduction or change to facility admission/service behavior.
- **Verification:** Inspected all source hunks and existing shop-category dispatch; caller only reaches building previews with a ride entry. Preserved null-image guard. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Ride-window batch compile and facility preview visual check.

### U015 — `89b9f7d64c` — Add changelog entries

- **Source:** `89b9f7d64c6c0f0ded7ba99ffae58c937daa647b`.
- **Fork receipt:** `ecd9dfd1bbdc52e6ea6ccb1e73d4f28b973c2778`.
- **Remaining:** 347 → 346.
- **Disposition:** manually-ported.
- **Manual changes:** Document the two approved ride-cheat tab moves in the development changelog.
- **Additional decisions / behavior:** Retain the source release-note wording at this historical step; no additional behavior change.
- **Verification:** Inspected and inserted both changelog lines. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Existing ride-window batch validation only.

### U016 — `7339f6eba6` — Merge pull request #26099 from AaronVanGeffen/ride-type-move

- **Source:** `7339f6eba68a38574ee4663fe803a90125a455b1`.
- **Fork receipt:** `543cbdfa279ef9fb12f6762c05bb8e715d21f622`.
- **Remaining:** 346 → 345.
- **Disposition:** merge-no-unique-source-change.
- **Manual changes:** Record the ride-cheat UI pull-request merge after its five source commits.
- **Additional decisions / behavior:** Successful empty remerge diff means no independent resolution changes. Preserve the ported fork string map and UI adaptations rather than taking the upstream aggregate tree.
- **Verification:** Inspected merge metadata and empty remerge diff using scratch object storage. Singleton ancestry and whitespace checked before receipt.
- **Pending / concerns:** Batch 02 build/resource and targeted widget/localization checks follow this receipt.

### U017 — `b8cf2f8c35` — Fix #25169: convert command strips packed objects (#26808)

- **Source:** `b8cf2f8c359a818debd359c1ae9966877548c29a`.
- **Fork receipt:** `9f0411fb13f741c9b5137ef1bc2cee21487e62be`.
- **Remaining:** 345 → 344.
- **Disposition:** manually-ported-with-fork-adaptation.
- **Manual changes:** Preserve packable custom objects by default in the convert command, add --strip-objects opt-out, and forward ExportObjectsList in the path exporter overload.
- **Additional decisions / behavior:** Preserve TargetVersion forwarding in both fork exporter overloads and private save version 60016. The opt-out affects object embedding, not required-object references or scenario reset rules. Retain existing GetPackableObjects selection/precedence; unsupported loose formats keep the existing packer behavior.
- **Verification:** Inspected all source hunks and both exporter overloads, packed-object chunk writer and conversion importer. Path and stream overloads now both forward object list plus fork target version. Batch 02 passed complete Release/Vulkan/resource build and 17 selected tests; evidence is in the validation doc. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Compile in next mechanical batch; custom-object conversion round-trip and --strip-objects comparison remain pending.

### U018 — `430bb95b63` — Rework includes in openrct2/object and openrct2/paint (#26852)

- **Source:** `430bb95b63541ae8d6c1610cf16b50ee3578365c`.
- **Fork receipt:** `2886403bc27edc170a91dc16422a5b5e65d2b6d9`.
- **Remaining:** 344 → 343.
- **Disposition:** manually-ported-with-fork-adaptation.
- **Manual changes:** Clean up object/paint include dependencies and forward declarations, preserving fork-required headers and implementations. Correct U008 network revision policy drift by restoring the independent fork revision to 4.
- **Additional decisions / behavior:** Retain Painter.cpp Guard.hpp for the fork paint-session assertion. ObjectManager retains its stable JobPool result publication and does not restore removed mutex/thread includes. Keep entity/map snapshot dependencies and VehiclePaint presentation lookup. No runtime statements change in the include port. Outside review identified that U008 release reset violated the approved independent revision policy: keep 0.5.4-andersonhk-4 and never adopt future upstream release resets; rationale is in the validation doc. This is a correction under existing approval, not a new product decision.
- **Verification:** Inspected include/forward-declaration deltas, including every track painter change, and compared fork overlap. Unchanged contexts were uniquely matched; divergent header contexts were edited explicitly. Source identity check confirms version 0.5.4, flavor andersonhk, revision 4. Whitespace and singleton ancestry checked before receipt.
- **Pending / concerns:** Compile the include and following widget/dropdown API migration as a batch; add any direct headers needed by fork consumers. Earlier conversion and visual validation debt remains recorded.

### U019 — `d517f19ce2` — Use flags for widget visibility instead of empty widget type (#26801)

- **Source:** `d517f19ce293d5d7190aac5dba0f2d2a71ac9559`.
- **Fork receipt:** `1d1c5bbb7e59f63c77ee54be5a2187338e28244a`.
- **Remaining:** 343 → 342.
- **Disposition:** adopt with fork behavior preservation.
- **Manual changes:** Port explicit widget visibility flags, hidden-input/draw guards and UI call sites. Adapt fork target-price controls, directed-leg selector, maze/block layouts and longitudinal-G graph.
- **Additional decisions / behavior:** Preserve additive network restrictions (client also hides fast-forward) so later rules cannot reveal editor/settings-hidden controls; retain debug preference. Preserve small-curve availability as curveVertical OR curveSmall, avoiding upstream overwrite. Keep update notice above title menu at y=0. Restore scenario tabs and guest-list filter buttons via visibility flags when revisiting. Retain purposeful widget type changes for target pricing/admission policy and forced-open park lights. No changes to ticket-age pricing, transport economics, ride measurements or maze capacity.
- **Verification:** Full 37-file upstream delta inspected; exact reviewed hunks applied and fork conflicts manually adapted. Mixed visibility/type usages audited. Added hidden-control representation/state regression test, not yet run. U018 complete build passed 0 warnings/errors, 79.19s (batch-03-includes-build.log). Diff whitespace and singleton ancestry checked by receipt helper.
- **Pending / concerns:** U019 compile and widget regression test deferred to next coherent UI checkpoint; interactive layout/input cases remain in validation log.

### U020 — `0284f9cda6` — Refactor dropdown flag to FlagHolder, reverse "stay open" flag (#26855)

- **Source:** `0284f9cda66206d8d99362f1dddf490dc0733640`.
- **Fork receipt:** `cde0dcdd1b37670964a6ebb70d8d05736d3f8d2e`.
- **Remaining:** 342 → 341.
- **Disposition:** adopt with fork call-site adaptation.
- **Manual changes:** Replace dropdown integer flags with FlagHolder and invert StayOpen into autoClose across input handling, dropdown implementation and callers.
- **Additional decisions / behavior:** Keep behavior equivalent: old StayOpen becomes empty flags; old zero becomes autoClose. Fork-only directed-leg selector remains open on first release; target fare and entrance-fee selectors retain autoClose. Touch enhancements still suppress autoClose. No pricing algorithm changes.
- **Verification:** Inspected complete source delta and each exact hunk context. Audited all dropdown argument positions across src/test for stale numeric flags; none remain. Checked old/new release polarity for all four stay-open/touch combinations. Singleton ancestry and whitespace checks in receipt helper.
- **Pending / concerns:** Build and widget regression run at UI checkpoint after U021; interactive dropdown click/release checks remain.

### U021 — `77c69cb55d` — Fix debug menu/button always visible in toolbar (#26854)

- **Source:** `77c69cb55d94e64f08a1978b49c0af99525a11a2`.
- **Fork receipt:** `f1c21f7e6598552653a24cd6d99328804682c5ee`.
- **Remaining:** 341 → 340.
- **Disposition:** already satisfied by U019 adaptation.
- **Manual changes:** No additional product changes: U019 retained debug toolbar visibility governed by debuggingTools.
- **Additional decisions / behavior:** Do not duplicate the same visibility assignment; upstream now fixes exactly the regression prevented during the prior refactor port.
- **Verification:** Inspected one-line source delta against current HideDisabledButtons; identical condition is present once. Receipt singleton/whitespace checks.
- **Pending / concerns:** UI batch build/test follows this receipt.

### U022 — `7df1cfe7c0` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `7df1cfe7c084d4b65b3ebfc1c0fc261c2bf135ca`.
- **Fork receipt:** `e7919f4dbafbd6ef5dedc7f90fe299240f2edbab`.
- **Remaining:** 340 → 339.
- **Disposition:** adopt.
- **Manual changes:** Add Dutch and Russian ride-type labels at upstream string ID 7039.
- **Additional decisions / behavior:** Fork private strings remain at 8000-8041; these additions fill the upstream label without collisions.
- **Verification:** Inspected both added strings and unique 7039 IDs. Singleton ancestry and whitespace checks.
- **Pending / concerns:** Generic parser tests at UI checkpoint; no native-language UI review claimed.

### U023 — `2ac48e6c42` — Rename members of MixerGroup

- **Source:** `2ac48e6c4233597efab90bfa11d88b0cc28e6d1a`.
- **Fork receipt:** `this entry’s unique Upstream-Commit trailer`.
- **Remaining:** 339 → 338.
- **Disposition:** adopt naming in fork mixer.
- **Manual changes:** Rename MixerGroup members and references to lower camel case, including fork vehicle channels.
- **Additional decisions / behavior:** Keep fork four-group ordering and values (sound, vehicle, rideMusic, titleMusic); do not restore upstream three-group mixer or its old callback/volume implementation. Spatial gains, channel lifecycle and vehicle group remain intact.
- **Verification:** Inspected complete upstream rename delta and fork mixer dispatch/volume paths; inverse identifier substitution reproduces each pre-port file exactly. UI checkpoint after U021 passed complete build 0 warnings/errors in 42.28s.
- **Pending / concerns:** This identifier port will compile at the next checkpoint. UI tests and audio regression checkpoint results logged separately.
