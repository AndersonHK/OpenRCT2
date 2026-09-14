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

## Progress: 205 / 361 source commits recorded

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
- **Fork receipt:** `7ef9085dd3c81489a5c79d2e72855c674c651a16`.
- **Remaining:** 339 → 338.
- **Disposition:** adopt naming in fork mixer.
- **Manual changes:** Rename MixerGroup members and references to lower camel case, including fork vehicle channels.
- **Additional decisions / behavior:** Keep fork four-group ordering and values (sound, vehicle, rideMusic, titleMusic); do not restore upstream three-group mixer or its old callback/volume implementation. Spatial gains, channel lifecycle and vehicle group remain intact.
- **Verification:** Inspected complete upstream rename delta and fork mixer dispatch/volume paths; inverse identifier substitution reproduces each pre-port file exactly. UI checkpoint after U021 passed complete build 0 warnings/errors in 42.28s.
- **Pending / concerns:** This identifier port will compile at the next checkpoint. UI tests and audio regression checkpoint results logged separately.

### U024 — `c80a0e168d` — Rename members of FileExtension

- **Source:** `c80a0e168d214a23fffe02a59e84319b1abb1bbe`.
- **Fork receipt:** `04df30180503c2d3ab5c97e7c8bc7f656c72dc4c`.
- **Remaining:** 338 → 337.
- **Disposition:** adopt.
- **Manual changes:** Rename FileExtension enum members and classifier/converter references without changing extension aliases or enum order.
- **Additional decisions / behavior:** Keep case-insensitive matching, .pob/.sea/.sv7/.td7 aliases, fork park version and U017 object-packing policy. Naming only.
- **Verification:** Inspected full source delta; all hunks uniquely matched. No old FileExtension member references remain in src/test. Batch04 UI build and 16 tests passed, details in validation doc.
- **Pending / concerns:** Next batch compile covers naming changes; custom-object conversion round trips remain pending.

### U025 — `a7881846e1` — Rename members of TunnelGroup

- **Source:** `a7881846e18921dec2e5b1e5109aeb53dd41469e`.
- **Fork receipt:** `74162ebe200b794c4cbfdbac349147dd3bc637e4`.
- **Remaining:** 337 → 336.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TunnelGroup members and all fork references to standard/square/inverted.
- **Additional decisions / behavior:** Keep numeric group values 0/1/2 and every fork terrain/paint path; apply identifier substitutions rather than replacing track painter bodies. Additional ClassicWoodenTwisterRollerCoaster.cpp and WoodenRollerCoaster.cpp references are included.
- **Verification:** All complete source parent/child file deltas mechanically verified to contain only the reviewed identifier substitutions (58 files); 60 fork paths updated. Receipt whitespace/singleton checks.
- **Pending / concerns:** Compile at next naming batch checkpoint; no tunnel sprite/layout changes intended.

### U026 — `e80faf5606` — Rename members of TunnelSubType

- **Source:** `e80faf5606b59b47b242fec9a5af562baafd64a2`.
- **Fork receipt:** `5601a0cb564e283023d1c715467ed9643de1a8e6`.
- **Remaining:** 336 → 335.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TunnelSubType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep all five numeric tunnel subtype values 0-4, painter heights and fork track support/layout calculations. Large track-paint diff is strictly a symbol rename, not tunnel geometry changes.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U027 — `abbbb251ee` — Rename members of CarEntryAnimation

- **Source:** `abbbb251eeafe49454a43fa2fc7a25c821ab2039`.
- **Fork receipt:** `1493a5714809c2421c6552968f6c42830b86e738`.
- **Remaining:** 335 → 334.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename CarEntryAnimation members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep animation dispatch order, frame/rate parameters and JSON lookup keys (including capitalized MultiDimension) unchanged. Preserve fork vehicle and restraint implementations.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U028 — `47c1bf9166` — Rename members of SpriteGroupType

- **Source:** `47c1bf91669e56e9dcd0d6ebc2c0f742715b98ce`.
- **Fork receipt:** `e6eb2367226557e399fde0578c0481e09741bc9c`.
- **Remaining:** 334 → 333.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename SpriteGroupType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve sprite group ordering, numeric indices, JSON names, frame multipliers and fork presentation snapshots. Existing cable-lift table comment spellings are renamed only, not interpreted as data fixes.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U029 — `b2375341bb` — Rename members of RideConstructionState

- **Source:** `b2375341bbc96db4288aea3f50375b9b301027fb`.
- **Fork receipt:** `022ea2310fdea6b954c6c3e96a8220bc55a80755`.
- **Remaining:** 333 → 332.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename RideConstructionState members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep all construction states and transitions, fork maze building controls, topology invalidation and safe mutation logic. No new construction behavior or disabled-widget bitmask is copied from legacy source bodies.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U030 — `2ed6bef643` — Rename members of VehicleTrackSubposition

- **Source:** `2ed6bef643c7217dcaaf4735dc4714789f140e1a`.
- **Fork receipt:** `0342b582ae16f9fe696f9dc852cf5074e0024896`.
- **Remaining:** 332 → 331.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename VehicleTrackSubposition members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Default becomes standard; keep serialized values, the two mini-golf value-9 aliases, vehicle movement tables and all fork boarding/path/physics logic. This is not the later approved subposition repair and does not introduce that behavioral change early.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U031 — `46dbc1cf20` — Rename members of ObjectiveStatus

- **Source:** `46dbc1cf203caa8b9d8253814e4a684b8aba2ac4`.
- **Fork receipt:** `276955b2f9702a8db7668b51e8a540c204b1e04c`.
- **Remaining:** 331 → 330.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ObjectiveStatus members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep objective thresholds, evaluation timing, success/failure side effects and fork park behavior; no gameplay difficulty changes are part of this rename.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U032 — `e1f2c365c1` — Rename members of PluginType

- **Source:** `e1f2c365c1f875daed2feafc641e646f7d162638`.
- **Fork receipt:** `7d13c4ef2f15a8f4c27575ffb6916974c514e399`.
- **Remaining:** 330 → 329.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename PluginType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep plugin metadata strings, lifecycle, hook restrictions and multiplayer start/distribution policy unchanged. C++ names only.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U033 — `0ab5cb5c49` — Rename members of ScConfigurationKind

- **Source:** `0ab5cb5c49753d3cf50e79c799fffea8fb6f7777`.
- **Fork receipt:** `00ab8534baf3817c1d5c950a813f17dc4bcf3fd5`.
- **Remaining:** 329 → 328.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ScConfigurationKind members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve plugin configuration namespaces, user/shared/park storage scope, ownership and error handling. No data migration or plugin API string rename.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U034 — `c728e66032` — Rename members of FileDialogType

- **Source:** `c728e66032040e58b76291c7521cb2776b774314`.
- **Fork receipt:** `5d41788fc1365b6e82aea60538a4839ec5e1c31b`.
- **Remaining:** 328 → 327.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename FileDialogType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep native open/save behavior and overwrite prompts across platforms. Names only; Windows build does not validate Linux/macOS runtime dialogs.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U035 — `9c5057a524` — Rename members of TileInspectorPage

- **Source:** `9c5057a524ac748dca642928c2594db1b8a099ef`.
- **Fork receipt:** `ace6742170ae135f5a5b6c23e13fce0bde8067df`.
- **Remaining:** 327 → 326.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TileInspectorPage members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Default becomes standard; page numbers, widget indices and tile element selection/edit behavior stay unchanged.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U036 — `8b9bde1f54` — Merge pull request #26856 from Gymnasiast/refactor/enum-class

- **Source:** `8b9bde1f548fd4d9c6ba269a45e47721ee9c0c27`.
- **Fork receipt:** `8a80e52197a864e6ee65ed6498035ac9fb1bc3e6`.
- **Remaining:** 326 → 325.
- **Disposition:** ancestry receipt only.
- **Manual changes:** No additional product changes; all enum-renaming parents were individually ported in U023-U035.
- **Additional decisions / behavior:** Do not copy merge tree over fork implementations.
- **Verification:** Inspected merge metadata and empty remerge diff. HEAD..source contains only this merge.
- **Pending / concerns:** Naming batch compile follows this receipt.

### U037 — `799002a820` — Fix colour buttons showing up for uncolourable shops with cheats active (#26858)

- **Source:** `799002a820822ad6f9551c1095878ad63f5336a0`.
- **Fork receipt:** `169f32a42e15a64187d2d2693d0b0aa83461b14b`.
- **Remaining:** 325 → 324.
- **Disposition:** adopt.
- **Manual changes:** Hide ineffective color controls for entries disabling the color tab when no station flags apply; improve shop/entrance preview and cheat visibility-button placement.
- **Additional decisions / behavior:** Keep the approved Appearance visibility feature for uncolorable shops; restrict the cheat-only track preview to shops and reposition the visibility button alongside entrance previews for rides without track colors. No shop pricing/service behavior changes. Existing caller validates ride entry before HasTrackColour.
- **Verification:** Inspected all four source hunks and current fork Appearance flow; exact contexts matched. Naming batch build passed 0 warnings/errors in 85.41s. Source/diff/ancestry checks.
- **Pending / concerns:** Interactive checks for uncolorable shops, entrance-only rides and toggling cheats; compile at next UI checkpoint. Batch05 test results recorded separately.

### U038 — `b782a7dcc9` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `b782a7dcc9da6a30b2626724604d380316f09ec3`.
- **Fork receipt:** `5975998dbe8b34d21ab753db8c79f23d854163fb`.
- **Remaining:** 324 → 323.
- **Disposition:** ported; companion-fork dependency correction.
- **Manual changes:** Add Spanish, Korean and Brazilian Portuguese ride-type labels at reconciled ID 7039. Correct U006 objects provenance: pin AndersonHK companion fork b2a5511cd7ff90646dd40c61f08cb27fa977e8f4 and install tracked assets through shared Python source installer in MSBuild, CMake/macOS and Android; document its separate four-receipt migration.
- **Additional decisions / behavior:** Localisation changes are text only; fork IDs remain 8000-8041. Owner clarified companion objects source: preserve all four data-owned -6 dB kart gains, remove uncommitted post-download workaround, and require pinned clean tracked source instead of silently downloading upstream assets. Objects migration has its own four-source ledger and does not alter engine source accounting. English typo regression skipped; actual exporter version 2.1.13 adopted. Full traceability, cleanup backup and local-only publication prerequisite are in docs/upstream-objects-migration-2026-09-13.md.
- **Verification:** Inspected all three actual localisation additions and unique IDs. Batch 06 full Windows build 0 warnings/errors; selected 45 tests passed with source-owned kart calibration and no 419 conflict warnings. Objects exporter compiled; CMake clean install and Windows linked-source install verified. See validation and objects correction docs.
- **Pending / concerns:** Full Android/macOS/Linux packaging, interactive Appearance cases and exporter pixel-output check remain pending. The pinned objects receipt is local and has not been published. Existing exporter net6/framework compatibility warnings documented.

### U039 — `a3f7b5d3b0` — Restore overriding widget type for construction bank/speed setting (#26863)

- **Source:** `a3f7b5d3b09f470dee3e1b28ba157e40e3ff95c4`.
- **Fork receipt:** `ef5ecc9e9b1d8141a101b6b89566eae1e940c93c`.
- **Remaining:** 323 → 322.
- **Disposition:** ported.
- **Manual changes:** Restore genuine bank-button versus brake/booster-spinner widget types before revealing shared controls; remove obsolete comment.
- **Additional decisions / behavior:** The hidden-state refactor does not replace widget type when the same slot serves different controls. Adopt this UI repair without changing banking eligibility, speed values, seat rotation or fork pressed/holdable state logic. Later separated-widget redesign remains for its own source commit.
- **Verification:** Inspected all three source hunks and both local branches. Six restored type assignments match the shared roles; source/diff and exact-one ancestry checked.
- **Pending / concerns:** Compile and interactive bank-to-brake/booster transitions at next coherent checkpoint.

### U040 — `d26b161668` — Move gCurrentWindowColours to Drawing.String.cpp

- **Source:** `d26b161668e42872287912b0de5e811a66c3f6a5`.
- **Fork receipt:** `a1ea0a886f247d5c16a130745d6331a87889472d`.
- **Remaining:** 322 → 321.
- **Disposition:** ported.
- **Manual changes:** Move the current three window text colours into Drawing.String ownership and qualify the window draw writes.
- **Additional decisions / behavior:** Storage duration, three palette indices and draw ordering are unchanged. All fork consumers were searched; no extra Vulkan snapshot or concurrency policy changes are introduced by this ownership move.
- **Verification:** Reviewed full four-file source delta and every fork reference. Unique-context edits and exact-one ancestry checked.
- **Pending / concerns:** Compile/link at the UI-header batch checkpoint.

### U041 — `4bf86c6e6c` — Move tile inspector constants to TileInspectorGlobals.h

- **Source:** `4bf86c6e6c96cb7a171bf51f9a6cadcdf3e178a6`.
- **Fork receipt:** `ea3d283a3c53226f739b0052d77db358f738af0f`.
- **Remaining:** 321 → 320.
- **Disposition:** ported.
- **Manual changes:** Move all 35 tile-inspector widget/page constants and their EnumUtils dependency into TileInspectorGlobals.h.
- **Additional decisions / behavior:** Keep each numeric index and enum expression byte-for-byte; this changes header ownership only, not inspector editing behavior. Every consumer already directly includes the owner header.
- **Verification:** Compared all 35 old/new constant lines exactly and searched every fork consumer/include. Source/diff and exact-one ancestry checked.
- **Pending / concerns:** Compile at UI-header batch checkpoint.

### U042 — `0f620b0714` — Remove Widget.h include from Window.h

- **Source:** `0f620b0714209970908c69d525173e77d2545189`.
- **Fork receipt:** `27b6d06a891b045062556de3670ce38e993a0fba`.
- **Remaining:** 320 → 319.
- **Disposition:** ported.
- **Manual changes:** Remove transitive Widget.h from Window.h and add explicit type includes/forward declarations in nine affected UI/core files.
- **Additional decisions / behavior:** Preserve fork core/Guard.hpp and all rendering logic. WidgetIndex remains uint16_t; no layout, flags, ownership lifetime or gameplay changes. WindowBase directly owns its Widget dependency.
- **Verification:** Reviewed all nine source paths; every source hunk matches unique fork context. Fork Guard.hpp and immutable presentation-frame publication remain present. Source/diff and exact-one ancestry checked.
- **Pending / concerns:** Compile will identify any fork-only transitive dependencies at UI-header checkpoint.

### U043 — `082994cd52` — Move widget index globals into WidgetIndexGlobals.h

- **Source:** `082994cd5266f2c3aac4fe09b5b265ac506e7e4c`.
- **Fork receipt:** `b3ae95f5337e4a285c52b1d85f5fe228f14b301e`.
- **Remaining:** 319 → 318.
- **Disposition:** ported.
- **Manual changes:** Move 20 shared widget-index constants into WidgetIndexGlobals.h under OpenRCT2; add direct includes at all source consumers.
- **Additional decisions / behavior:** All numeric values and cross-widget aliases are unchanged. Keep existing global-index static assertions and fork construction/pickup behavior; this is header/namespace ownership cleanup only.
- **Verification:** Inspected the full 17-file source delta; all 20 declarations compare exactly after indentation. Searched fork references; direct includes cover each consumer. Source/diff and exact-one ancestry checked.
- **Pending / concerns:** Compile at UI-header batch checkpoint.

### U044 — `bfd0a374ff` — Merge pull request #26867 from AaronVanGeffen/strip-window-header

- **Source:** `bfd0a374ff3ec4fc6a46d7839ae6424634475ce8`.
- **Fork receipt:** `a7d9dfac77243731a13d864e53564dbe83393a8f`.
- **Remaining:** 318 → 317.
- **Disposition:** merge receipt; already ported.
- **Manual changes:** Record the UI header refactor merge after U040-U043; no additional product delta.
- **Additional decisions / behavior:** The merge tree equals its second parent and contains no conflict-resolution behavior to port. Preserve the individually adapted fork changes.
- **Verification:** Inspected merge metadata, empty archived remerge diff and exact second-parent tree equivalence; exact-one ancestry checked.
- **Pending / concerns:** UI-header batch build pending.

### U045 — `302058221f` — Move WindowFlags into their own header (#26869)

- **Source:** `302058221f4c3c47e079f194c61ffc49f6f7bbf0`.
- **Fork receipt:** `075ebd0d7cc0221e721082e3052471c8e84d3efd`.
- **Remaining:** 317 → 316.
- **Disposition:** ported.
- **Manual changes:** Move WindowFlag/WindowFlags into WindowFlags.h, make Window.h use forward declarations, and supply explicit type includes at consumers. Add both new headers to the Visual Studio project.
- **Additional decisions / behavior:** The entire flag declaration and uint16_t storage alias are unchanged byte-for-byte. All 14 flag positions retain meanings, including create-only flags. No window placement, transparency, input or rendering policy changes.
- **Verification:** Reviewed all nine source-file deltas; compared the complete old/new enum and alias. Unique-context edits, diff and exact-one ancestry checked.
- **Pending / concerns:** Compile/link and focused tests at the next checkpoint will check fork transitive dependencies.

### U046 — `1423ab779a` — Fix #26811: Crash when a ride points to a non-existing station object

- **Source:** `1423ab779a5a47190f72e04449690e184efc8a36`.
- **Fork receipt:** `781ea6d87e8dda57533048964ba9a5b00b217294`.
- **Remaining:** 316 → 315.
- **Disposition:** ported.
- **Manual changes:** Skip the entrance dropdown caption when the ride station object is missing; add upstream crash-fix changelog line.
- **Additional decisions / behavior:** Adopt the null guard. It exits only the caption helper, leaving other ride-window drawing and all fork station capacity, staging, platform, routing and pricing models unchanged. Existing entrance preview/icon paths already guard missing objects.
- **Verification:** Inspected source hunks plus caption caller, helper boundary and adjacent preview/icon guards. Source/diff and exact-one ancestry checked.
- **Pending / concerns:** Full build and selected regression tests at the immediate UI checkpoint; missing-object interactive rendering remains unverified.

### U047 — `bcd7708dc1` — Merge pull request #26872 from Gymnasiast/fix/26811

- **Source:** `bcd7708dc1a1da55aa1f74e570e93ebeb0ec34c3`.
- **Fork receipt:** `20ada0ae25c0d20d767ed9baa383bd9a524205d8`.
- **Remaining:** 315 → 314.
- **Disposition:** merge receipt; already ported.
- **Manual changes:** Record the missing-station-object crash-fix merge; U046 contains its entire product delta.
- **Additional decisions / behavior:** No additional merge resolution or behavior change; retain the fork-adapted null guard.
- **Verification:** Inspected merge metadata, empty archived remerge diff and second-parent tree equivalence; exact-one ancestry checked.
- **Pending / concerns:** UI-header batch build and selected tests next.

### U048 — `40b7741b58` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `40b7741b58bfa1d165249a0ba53e3b1b97f58ec3`.
- **Fork receipt:** `2229c6e46acbede1dd46c7ad72f477bbd048a853`.
- **Remaining:** 314 → 313.
- **Disposition:** ported.
- **Manual changes:** Add Esperanto ride-type label at reconciled upstream ID 7039.
- **Additional decisions / behavior:** Single-parent localisation change despite merge-style subject. Text only; fork IDs 8000-8041 untouched.
- **Verification:** Inspected the one-line source patch and checked the translated ID is unique; exact-one ancestry checked.
- **Pending / concerns:** Language parser regression at the in-flight UI checkpoint.

### U049 — `26b28153bb` — Disentangle speed controls from the banking widgets (#26864)

- **Source:** `26b28153bbfdfe30ec74f1e13db3f17d89329a01`.
- **Fork receipt:** `ca39d23303ff95df98226c8d09d161a3f50df914`.
- **Remaining:** 313 → 312.
- **Disposition:** adapted.
- **Manual changes:** Separate bank controls from four dedicated brake/booster speed widgets and handlers; adopt 14-pixel spinner/dropdown defaults and seat-rotation spacing. Restore base layout and banking caption each update. Also close the documented objects-installer preflight gap and restore CMake help-text encoding.
- **Additional decisions / behavior:** Preserve fork small-curve visibility OR condition, speed bounds/increments/cheat semantics, selected-piece SetBrakeSpeed actions, construction state and pressed-bit logic. Avoid upstream stale layout/caption state when switching between seat-rotation or covered-piece modes by resetting geometry/text before conditional adjustments. New speed indices fit 64-bit masks. UI size change has no simulation/economics effect. Oversight objects follow-up: reject duplicate packed/loose IDs and stale previously managed paths before writes; preserve unrelated assets and document provenance. See objects correction log.
- **Verification:** Reviewed every upstream hunk; skipped only the directional cleanup hunk to retain the fork fix. Full Batch 08 Windows build passed; 18 selected regressions passed. Four installer fixtures and live companion-source validation passed. Batch 07 evidence also recorded.
- **Pending / concerns:** Interactive bank/speed/seat-rotation/covered-piece layouts and global spinner/dropdown appearance remain unverified. Subsequent source overlap fixes will be reviewed in order.

### U050 — `8bfe55af63` — Add Liquid Glass app icon for macOS 26+ (#26862)

- **Source:** `8bfe55af6378da68c0e48d9be3f716e7b8e9155b`.
- **Fork receipt:** `128e4489257bf8f1014fd991cf09faeeaab03d93`.
- **Remaining:** 312 → 311.
- **Disposition:** ported.
- **Manual changes:** Add macOS Liquid Glass icon catalog/source and bundle icon-name metadata, retain flat ICNS fallback, archive prior artwork, and include Assets.car in bundle resources.
- **Additional decisions / behavior:** Presentation/packaging only; existing macOS bundle and companion-objects source wiring retained. Reuse reviewed upstream artwork byte-for-byte; no regeneration or platform build requirement added. Omit upstream incidental extra blank line in CMake.
- **Verification:** Reviewed full source patch and Slide.png artwork. Confirmed fork assets matched source parent before replacement; all new assets byte-identical to source; archived SVG identical to the original Git blob (working-tree CRLF normalized); both ICNS headers/lengths and JSON/plist resource references valid. Exact-one ancestry checked.
- **Pending / concerns:** Native macOS 26 icon rendering, older-macOS fallback and bundle build cannot be validated on this Windows host.

### U051 — `687fa7181d` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `687fa7181df6747d544930250d6272f1deccd58b`.
- **Fork receipt:** `4486080d551c9da282583368b1c3521f2ec8e4c0`.
- **Remaining:** 311 → 310.
- **Disposition:** ported.
- **Manual changes:** Add French/Hungarian ride-type labels and Hungarian wording/token corrections.
- **Additional decisions / behavior:** Adopt all text corrections including malformed sausage-value thought opening quote; preserve valid STRINGID/STRING and formatting placeholders. The lost/stuck guest message changes wording only, with no pathfinding or thought-generation changes.
- **Verification:** Inspected every source hunk and checked unique 7039 IDs plus removal of the malformed token. Source/diff and exact-one ancestry checked.
- **Pending / concerns:** Parser tests at the next enum-refactor checkpoint.

### U052 — `e103cb11bb` — Rename members of DrawingEngine

- **Source:** `e103cb11bb7e116bbd48d7bf01c7a8163391b148`.
- **Fork receipt:** `7da651bc9d8a70220a1c285ef6f47bda8611d437`.
- **Remaining:** 310 → 309.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename DrawingEngine members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve the fork renderer set and config contract: none=-1, software=0, vulkan=2, count=3; do not reintroduce retired OpenGL. Include the fork-only Vulkan enum spelling and CLI/options/config references in the naming change. Keep persisted SOFTWARE_HWD, OPENGL legacy-to-software alias and VULKAN strings unchanged. Vulkan LightFX, window creation, frame ownership and crash-capture behavior stay as implemented in the fork.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U053 — `3fa93c3278` — Rename members of Weather::Type

- **Source:** `3fa93c32786eef367c5a851061e74f0e11c10ef3`.
- **Fork receipt:** `71aaa996c67e1580f65fc7c254c9e718aa7174d2`.
- **Remaining:** 309 → 308.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Weather::Type members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve weather values 0-8 and count=9, climate distributions, screenshot option offsets, force-weather bounds and scripting strings. Restrict unqualified Type substitutions to the Weather namespace files; unrelated Type enums remain untouched. Include fork pathfinding weather tests without changing routing behavior.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U054 — `068f68e4a9` — Rename members of Weather::EffectType

- **Source:** `068f68e4a910011f1c47485f3380153cefbaf9cd`.
- **Fork receipt:** `854299910df3b56cd5f668c8a7d6cdbfe0cd2813`.
- **Remaining:** 308 → 307.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Weather::EffectType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve effect values 0-4 and the complete weather-trait table, thunder probabilities, rain/storm audio trigger and RCT1 import defaults. Namespace-scoped naming only; no climate, sound gain or simulation changes.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U055 — `4dbb57a87a` — Rename members of Weather::Level

- **Source:** `4dbb57a87a851571e3ac0c9f41c30611f189b969`.
- **Fork receipt:** `9e3032d0d24f7d63a08fb4717c037aa2a337323b`.
- **Remaining:** 307 → 306.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Weather::Level members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep precipitation levels 0/1/2, trait rows, transition stepping and draw suppression unchanged. Scope short Level references to Weather files so unrelated logging or UI level enums are not modified. Fork Vulkan weather drawing stays intact.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U056 — `1b63339ef7` — Rename members of ScrollbarType

- **Source:** `1b63339ef7d64d45bdaf24dc289c0e0a2d5fd982`.
- **Fork receipt:** `82acab715bf93307496c03a4fe1d597c08ba8924`.
- **Remaining:** 306 → 305.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ScrollbarType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep scrollbar values and horizontal/vertical bit mappings, default vertical bars, JS strings, column width calculations and pointer behavior unchanged. No custom window API migration is required.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U057 — `1e2817a5e1` — Rename members of ColumnSortOrder

- **Source:** `1e2817a5e1a0b3d93711cf0f7e6530ac1a6d7fa1`.
- **Fork receipt:** `34bf25f0e7e9ac38d6aaf65bf53d2a4cf0d4a7fd`.
- **Remaining:** 305 → 304.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ColumnSortOrder members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve sort enum values and the ascending-descending-none cycle, unsorted item order, selected-cell resets, comparison logic and scripting strings. No sorting algorithm or user-visible API change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U058 — `8c3ad47b92` — Rename members of CustomToolbarMenuItemKind

- **Source:** `8c3ad47b92f67fbee1c4a3933545631263ff0006`.
- **Fork receipt:** `8992695c2b5498c8a683cf3225f5c55b690355aa`.
- **Remaining:** 304 → 303.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename CustomToolbarMenuItemKind members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Retain standard/toolbox categorization, plugin ownership and callback dispatch, alphabetic menu sorting and the intransient-plugin restriction. Naming only, including fork toolbar consumers.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U059 — `45007c1865` — Rename members of CursorID

- **Source:** `45007c1865e3380311e312f201935fc616cd3c20`.
- **Fork receipt:** `30ef750b2f985b899e645914080d8767e7441ba7`.
- **Remaining:** 303 → 302.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename CursorID members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep all 28 cursor slots, count=28 and undefined=0xFF; preserve sprite tables, cursor scaling, tool selection, object CURSOR_* tokens and scripting cursor strings. Use zzz consistently, correcting the upstream CursorData comment typo zZZ (comment only). Include all fork cursor references without changing viewport interaction.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U060 — `3a36fc1c0a` — Rename members of TitleScript

- **Source:** `3a36fc1c0aa5dc71148e3703ccc2ba51fffa41ea`.
- **Fork receipt:** `d8df9a70eb8369b55cb83f8887d0f3b57f703f7c`.
- **Remaining:** 302 → 301.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TitleScript members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve undefined=0xFF, wait=0 and all command numbers, scripting names, argument validation and integer casts. Title-sequence commands and playback timing are unchanged.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U061 — `959401a3a0` — Rename Guest::MazeType and its members

- **Source:** `959401a3a082177f645f73023ecd58a18544534c`.
- **Fork receipt:** `bb1798e6a4508f584718b5702d03bae6a8ce3d5b`.
- **Remaining:** 301 → 300.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename the local maze_type enum to MazeType and entrance_or_exit to entranceOrExit in Guest maze movement.
- **Additional decisions / behavior:** Keep fork RideRatingAccumulateMazeStep calls for hedge and exit transitions, all random edge selection, destination updates and maze capacity/rating models. This commit contains no maze traversal repair or gameplay change.
- **Verification:** Complete upstream parent/child file equals only the two reviewed token replacements. Fork port contains only those replacements, retaining rating accumulation calls. Exact-one ancestry checked. Batch 09 build and 56-test result for the preceding U060 checkpoint recorded, including the corrected fixture working directory.
- **Pending / concerns:** Compile and maze regressions at the next naming checkpoint.

### U062 — `0d0498a081` — Rename members of ReplayMode

- **Source:** `0d0498a0819134d2a80919f3e3bda5c3dc11891d`.
- **Fork receipt:** `775be3abdb92559187c244839abcfba8cbceb366`.
- **Remaining:** 300 → 299.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ReplayMode members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve replay modes 0-3, command/checksum tick scheduling, record/playback/normalisation transitions, diagnostics, snapshot comparison and fork timing. Naming only; no replay format or synchronization change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U063 — `2769a3ff8c` — Rename members of RecordType

- **Source:** `2769a3ff8cdf39f028666f5d6da65fb7ad392c70`.
- **Fork receipt:** `043e1849b1050db7678100fb00e0898dea745578`.
- **Remaining:** 299 → 298.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename RecordType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep normal/silent recording values 0/1, notice visibility, silent replacement behavior, checksum cadence and debug recording defaults. No change to replay contents, file names or capture policy. Also rename the stale RecordType::NORMAL default-argument comment left by upstream; source verification explicitly accounts for this comment-only correction.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U064 — `331dd2670a` — Rename members of AudioCodecKind

- **Source:** `331dd2670a7f15a9e980c00ca233f86c760fbe4b`.
- **Fork receipt:** `87181081ccd71bedde97b9de11362d46a4f13c60`.
- **Remaining:** 298 → 297.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename AudioCodecKind members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve codec values/order, FLAC/OGG/RIFF magic detection, source factories and unsupported-format exception. No decoding, audio routing, gain or source-lifetime change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U065 — `17f7713603` — Rename members of SoundType

- **Source:** `17f7713603dd806b21c89376797663c434feb8a5`.
- **Fork receipt:** `ea1c304157cb79a23a8e7482810df70e42a1ca6c`.
- **Remaining:** 297 → 296.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename SoundType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve the fork spatial-audio pipeline, authored linear-amplitude interpretation, track-noise calibration, object gain and source lifetime. Rename the fork-only track-noise gain branch too. Do not reintroduce the upstream legacy channel-rate/pan-volume implementation that this fork replaced.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U066 — `5a0aa69409` — Rename members of PixelDataKind

- **Source:** `5a0aa69409f312233bcfe4f95cf1203205b33e96`.
- **Fork receipt:** `74f43922a539132bd0a992f189d2e02a4219aadf`.
- **Remaining:** 296 → 295.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename PixelDataKind members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve raw/RLE/palette/PNG numeric kinds, JS strings, stride handling, PNG import mode and image buffer ownership. Naming only; no custom-image encoding or sprite-cache behavior change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U067 — `e89ab11a6a` — Rename members of PixelDataPaletteKind

- **Source:** `e89ab11a6a1e1fa0f04669da2b93ed895aae0c2b`.
- **Fork receipt:** `952627bca2ab606710ec8b0b124ea40b30d05551`.
- **Remaining:** 295 → 294.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename PixelDataPaletteKind members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve palette-kind values, keep/closest/dither JS strings, palette index retention and importer mode selection. No quantization or dithering algorithm change; fork image/rendering behavior stays intact.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U068 — `882467fe20` — Rename members of GuestList::TabId

- **Source:** `882467fe20a6446d58e39a91dbd56860e3f5934b`.
- **Fork receipt:** `a631f0646f36803842ff83de23996979d9e9039d`.
- **Remaining:** 294 → 293.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TabId members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Scope TabId renaming to GuestList only. Preserve tab indices, pagination, grouping refresh, animation periods and the fork visibility restoration when returning from a summary to individual guests.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U069 — `cb13ddba48` — Rename members of GuestList::GuestViewType

- **Source:** `cb13ddba4897d3ab96e26623636c9ded253ee454`.
- **Fork receipt:** `f6c2de3d944a2e8426ae45241e7cf6a12a36ae99`.
- **Remaining:** 293 → 292.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename GuestViewType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Keep actions/thoughts values, default view, thought freshness filtering, action formatting and group aggregation unchanged. Preserve fork guest-list UI behavior; no changes to thought generation or guest decisions.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U070 — `95cf306886` — Rename members of GuestList::GuestFilterType

- **Source:** `95cf3068869c314b2aea0cd683651e2b60e1dbbf`.
- **Fork receipt:** `ef6c6c80b82bec0f5e4e063599be4c2077550a30`.
- **Remaining:** 292 → 291.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename GuestFilterType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve filter values, ride and queue matching, wildcard thought arguments, labels and selection state. This is UI naming only; guest simulation and fork economics remain unchanged.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U071 — `275e6597a7` — Rename members of LandRightsMode

- **Source:** `275e6597a772858ad71294339303a397b884b796`.
- **Fork receipt:** `dd2128ca15b4617ac5ddfb14256dbefe917c7a6f`.
- **Remaining:** 291 → 290.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename LandRightsMode members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve seven mode values, ownership visibility table, buy-versus-sandbox mode transitions, action dispatch and ownership bit mappings. Land and construction-rights prices and restrictions are unchanged.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt. Prior U070 checkpoint: full build and 51 selected regressions passed; batch evidence recorded.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U072 — `546cd2dab6` — Rename members of ResizeDirection

- **Source:** `546cd2dab65d44aeadb3fc07a93e4cef7561f77b`.
- **Fork receipt:** `01463a05f289d3d7df8b2ff547b4fc027c866c0c`.
- **Remaining:** 290 → 289.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ResizeDirection members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Scope ResizeDirection to the map window only; MapGen has a separate enum handled by U073. Preserve both/x/y values, linked-axis selection, size clamps and MapChangeSizeAction arguments. No map resizing or land-loss policy change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U073 — `0c4322a982` — Rename members of ResizeDirection

- **Source:** `0c4322a98297de1b4edfaf7343b8651cd418f61f`.
- **Fork receipt:** `a632f702ce90ce9f688865a8531646b6f732b25b`.
- **Remaining:** 289 → 288.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ResizeDirection members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Scope ResizeDirection to MapGen; preserve size bounds, technical-versus-practical size offset of two, linked width/height and generator settings. No map-generation output or random-seed change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U074 — `c166b42a67` — Rename members of ListItemType

- **Source:** `c166b42a67a58492dbef5d2cfc32ad75c2e61534`.
- **Fork receipt:** `70e21da941dbacbe7e090b155597778fb82ffdcf`.
- **Remaining:** 288 → 287.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ListItemType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve scenario/heading values, row heights, hit testing, scenario locks and removal of empty category headings. Scenario progression and fork selection UI behavior are unchanged.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U075 — `8ae5221c6b` — Rename members of DisplayType

- **Source:** `8ae5221c6bdf72cad2f22c2c04fe2f25bbd7c14a`.
- **Fork receipt:** `503dbc5f54e783760010ff86e5dd1d08ed60dd84`.
- **Remaining:** 287 → 286.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename DisplayType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve raw-versus-units values, default display units, height formatting and clipping limits. No clipping or Vulkan rendering policy change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U076 — `6d8c65ba5f` — Rename members of Http::Status

- **Source:** `6d8c65ba5f349de80e04544fd6d6af369c3fbc5d`.
- **Fork receipt:** `7570bb48ff9fefb979369b7a8adfe245e12a2b43`.
- **Remaining:** 286 → 285.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Http::Status members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve status values 0/1/200/404, retry/cancellation handling, download success checks and server-list errors. Scope unqualified Status references to HTTP implementation files; no endpoints, network policy or object-source contract changes.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U077 — `45bc989f1b` — Rename members of Http::Method

- **Source:** `45bc989f1bf3e9c3986cd2221a73c95f5e1e0cac`.
- **Fork receipt:** `8986d7bfa96ca7f9c874cc23b6d38616fe479c66`.
- **Remaining:** 285 → 284.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Http::Method members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve method values and uppercase GET/POST/PUT wire strings across Android, WinHTTP and cURL. Request bodies, upload options, URLs and server heartbeat cadence are unchanged. No network requests are made by this source port.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U078 — `086b5ecabe` — Rename members of FlagType

- **Source:** `086b5ecabe73351cefca8f1464df0c7d318aa25e`.
- **Fork receipt:** `6f3221651a0ceac403b3c181c3833fb9fc018437`.
- **Remaining:** 284 → 283.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename FlagType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve normal/inverted polarity, missing-property behavior and all JSON property names/legacy aliases. Path queue/slope permissions and wall capability flags retain their exact meanings; companion object definitions need no data migration.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U079 — `25df52f1bf` — Rename members of ImageCatalogue

- **Source:** `25df52f1bf51d4be9516755289c4ddd9413bbe86`.
- **Fork receipt:** `9637f95d15aced9ee80c799d6bd1d7a4998845cb`.
- **Remaining:** 283 → 282.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ImageCatalogue members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve all six catalogue values and temporary/G1/G2/CSG/object index boundaries. Include applicable fork consumers; no sprite ownership, cache or Vulkan resource-lifetime changes.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U080 — `d0db940bde` — Rename members of ImportMode

- **Source:** `d0db940bdeca52191c1f0bdc81c0652d8c83a4b6`.
- **Fork receipt:** `6e62cb38cf7466450a7f5ad0f0110e3f55a29a66`.
- **Remaining:** 282 → 281.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ImportMode members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Default becomes standard with value 0; closest and dithering retain values 1/2. Preserve CLI mode strings, pixel error diffusion and metadata defaults. Include sprite-build regression call sites without changing expected binary output.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U081 — `1c4daab873` — Rename members of Palette

- **Source:** `1c4daab873f851be55eddd3e074be0bbda917922`.
- **Fork receipt:** `998cdc15f57e58c4ff3f2cd5aa9d63db9b68aa51`.
- **Remaining:** 281 → 280.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Palette members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve palette mode values, 8-bit keep-indices validation, pixel stride and offset handling, JSON keep token and PNG format selection. No colour remapping, resource regeneration or companion-object changes.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U082 — `e6cc15f033` — Rename members of PaletteIndexType

- **Source:** `e6cc15f033672dec277fb7553d0ad23e6276e684`.
- **Fork receipt:** `6ca16cce29bb9bc8db00065178667530bdb930e4`.
- **Remaining:** 280 → 279.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename PaletteIndexType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve palette classes and every reserved/remap range: 0-9, 230-239, 255 special; 243-254 primary; 202-213 secondary; 46-57 tertiary. Pixel mutability and nearest-colour behavior remain unchanged.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U083 — `b0e7d321e3` — Rename members of Qualifier

- **Source:** `b0e7d321e34a316a3127242129515cc39fb69573`.
- **Fork receipt:** `bc14c9f3580cbab0f607b89a3b2292ec5646ff4b`.
- **Remaining:** 279 → 278.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Qualifier members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Preserve entity/map light-key qualification, sampling counts, packed coordinates and fork LightFX/Vulkan ownership. Rename only the applicable enum symbols, without restoring upstream lighting code replaced by the fork.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U084 — `a850d78a7d` — Rename members of DuckState

- **Source:** `a850d78a7d5c23355b42e9c290e771c71043dc6c`.
- **Fork receipt:** `11ae635a477969c876dbda5ebd1743ca4cbb11fb`.
- **Remaining:** 278 → 277.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename DuckState members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Duck naming only: preserve state values 0-4, animations, random draws and seasonal departure probability, including fork entity presentation. Update five stale animation-table comments left upstream. Batch 11 build and all 16 selected sprite/import/scripting/widget tests passed through U083; no gameplay changes.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U085 — `a6175f8930` — Rename members of JumpingFountainType

- **Source:** `a6175f893014a1e18d41488a0a48f31dd125eb5e`.
- **Fork receipt:** `851e6dbe0e694bdec5ba6dc22049b9fbad889005`.
- **Remaining:** 277 → 276.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename JumpingFountainType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Water/snow fountain names only. Preserve water=0 and snow=1, animation flags/frames and RCT1/RCT2 import encoding.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U086 — `53f5a41a74` — Rename members of MusicNiceFactor

- **Source:** `53f5a41a74c3b746fad60b0b0b3e8172feb85173`.
- **Fork receipt:** `d5cd7af071fd8b09c16488258871748806ebb7e0`.
- **Remaining:** 276 → 275.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename MusicNiceFactor members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Music niceness names only. Preserve -1/0/1 values and guest nearby-music masks 1/2; retain fork spatial music implementation and guest happiness policy.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U087 — `9bd1e0c858` — Rename members of ObjectGeneration

- **Source:** `9bd1e0c858d9ae70bc700835c5245b7e121cd1cf`.
- **Fork receipt:** `27211dc0580cc39de5f06bc55f4429d46fc71d05`.
- **Remaining:** 275 → 274.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ObjectGeneration members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Object generation names only. Preserve DAT=0/JSON=1 wire values, JSON/DAT precedence, save version 60016 and fork network stream revision 4. No change to authoritative sibling objects source.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U088 — `76e75e687f` — Rename members of TunnelType

- **Source:** `76e75e687f36f9f2a325b147500af2b8e6e14f95`.
- **Fork receipt:** `44f3170c89ab2ac07c08d8a121b0acb1a1938ac1`.
- **Remaining:** 274 → 273.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TunnelType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Tunnel identifiers only: preserve numeric types 0-15, door types, surface fallback, sprite offsets and tunnel geometry. Correct sixteen stale table comments left upstream. Fork Vulkan painting and ghost topology retained.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U089 — `41164a7453` — Rename members of JuniorRCSubType

- **Source:** `41164a74533f4c5744d0b8e4dc86eb5364db842e`.
- **Fork receipt:** `11cdaefc1a1ecd4c00fc10502f410e7cde47ee8f`.
- **Remaining:** 273 → 272.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename JuniorRCSubType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Junior/water coaster paint subtype names only: preserve subtype values 1/2, chain image reuse, station brake conditions and track dispatch. No ratings or vehicle simulation change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U090 — `4502be0fb2` — Rename members of Plane

- **Source:** `4502be0fb2f2d96a7522b6eee8a76c69f0e986b1`.
- **Fork receipt:** `e219dbb097b49d7ec369ab7f1dc1931cb283b31c`.
- **Remaining:** 272 → 271.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename Plane members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Magic Carpet local Plane enum only: preserve back/front values and exact painter layering/parent-child order; do not rename unrelated Plane types.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U091 — `5ba0b8028c` — Rename members of PathSearchResult

- **Source:** `5ba0b8028ccb11d9aadd05f5f973fef740bcb3ea`.
- **Fork receipt:** `45426d71c1260eef7e9587400b443d0c9d74e8f5`.
- **Remaining:** 271 → 270.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename PathSearchResult members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Guest pathfinding result names only: preserve diagnostic strings, result order and search limits. Retain fork transport fare/time path choice, ghost exclusion and routing implementations; no legacy algorithm replacement. Update stale qualified identifiers in return-value comments; preserve ordinary prose.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U092 — `780a135087` — Rename members of RideComponentType

- **Source:** `780a135087ec76cbadd501afe326962e254e959b`.
- **Fork receipt:** `7cede9e610c2c91c0437f048f6e3201505c59d10`.
- **Remaining:** 270 → 269.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename RideComponentType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Ride component naming enum only: preserve component order, translated name indices and all ride NameConvention choices. Fork per-car/maze/directed-leg ratings and operating settings retained; no ride balance change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U093 — `f256f03fff` — Rename members of RideColourKey

- **Source:** `f256f03fff2dd6c4bd2178d0f163bc2844c49cde`.
- **Fork receipt:** `04a153056d2d2d94c075e4c1e7ccc79c5c626696`.
- **Remaining:** 269 → 268.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename RideColourKey members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Ride colour-key names only: preserve all eight indices and ride/shop/facility assignments, including fork-modified descriptors; no palette or financial change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U094 — `bd4a9eb0ef` — Rename members of TrackDesignCreateMode

- **Source:** `bd4a9eb0ef5b0e8d0dc098d7838a2d8fc820c54d`.
- **Fork receipt:** `052d74247f7f9fbeb13c0daeef96762167f42a63`.
- **Remaining:** 268 → 267.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TrackDesignCreateMode members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Track-design mode names only: Default becomes standard, Maze becomes maze; preserve 0/1 values, maze-specific export path and fork design/save extensions.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U095 — `7f879409ac` — Rename members of RatingsCalculationType

- **Source:** `7f879409acfa787215bfa66633823e2a754b9284`.
- **Fork receipt:** `72b586364afeca68b9ede762fc8bb2335e10cb43`.
- **Remaining:** 267 → 266.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename RatingsCalculationType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Approved D12: rename ratings calculation categories only. Preserve fork base ratings, sampled per-car and directed-leg/maze models, modifiers and coefficients. No upstream balancing data restored.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U096 — `28ad2fcaa2` — Rename members of RatingsModifierType

- **Source:** `28ad2fcaa2570ee2d2bc7c665c641b4168687cc0`.
- **Fork receipt:** `d8bbb8b475f5937fe1b6578a6cd30844493de7fe`.
- **Remaining:** 266 → 265.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename RatingsModifierType members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Approved D12: rename only existing fork rating modifiers and consumers, including tests. Fork intentionally deleted BonusMazeSize enum/function/descriptor entries: do not reintroduce them or upstream ordinal layout. Keep fork modifier order/values, all coefficients, negative-G warnings, per-car longitudinal-G and directed-leg/maze calculations.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U097 — `991fc7b4a1` — Rename members of RideConstructionWindowContext

- **Source:** `991fc7b4a17d606d5800b12575f12cab6133c577`.
- **Fork receipt:** `1d39f177d825b0f544eca1ba7acbcac0e1600b1f`.
- **Remaining:** 265 → 264.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename RideConstructionWindowContext members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Construction context names only: standard=0 and maze=1 retain window selection and fork widgets, layout fixes and operating settings.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U098 — `0d8af50f45` — Rename members of GameCommand

- **Source:** `0d8af50f45b0d4dffbcae25dd5ce63afcac89947`.
- **Fork receipt:** `6313a20bf868fca3fbf385dc1464622867c34318`.
- **Remaining:** 264 → 263.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename GameCommand members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** GameCommand identifiers only: retain every action ID/order, script action-name string, network permission group and server pause/quit restriction. Keep setRideVisibility and fork stream revision 4; no replay/wire format change. Batch 12 full build and 149 selected regressions passed through U097.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U099 — `9c830feef5` — Rename members of TrackDesignGameStateFlag

- **Source:** `9c830feef560cb2017223721112cf2b764d8ebac`.
- **Fork receipt:** `2daf82d6dd24306a0df5bb0517516b2681610123`.
- **Remaining:** 263 → 262.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename TrackDesignGameStateFlag members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Track design flags naming only: retain bit positions, availability warnings and scenery-toggle/research behavior.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U100 — `b229ff8f60` — Rename members of MiniGolfState

- **Source:** `b229ff8f607ac0d4543fcef6cdd4c7935eacbae8`.
- **Fork receipt:** `eb37bbc80c90126d9c9c11ad36f91943b3274f69`.
- **Remaining:** 262 → 261.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename MiniGolfState members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Mini-golf state names only, including MINI_GOLF_STATE macro arguments. Preserve all seven int16 state values, the unused Unk1 slot, every vehicle movement coordinate/frame and fork ride measurements. Complete source table verified against the selected substitutions.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U101 — `ae5dc4422a` — Rename members of MiniGolfAnimation

- **Source:** `ae5dc4422a3ccb5908c8c5037784ea712c0b35c8`.
- **Fork receipt:** `5b0d349882d2f32962c11d99dadf1fb527f27a98`.
- **Remaining:** 261 → 260.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename MiniGolfAnimation members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Mini-golf animation names only, including MINI_GOLF_ANIMATION macro arguments. Preserve nine animation indices, coordinates/frame tables, ball visibility, initial walk state and peep-ID-based handedness.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U102 — `b5a92d9296` — Rename members of BoatHireSubState

- **Source:** `b5a92d92968697a1645327e47d6cb33019cd3e5f`.
- **Fork receipt:** `eb7fecd241ff327543bb7ba7dfc93add732d1046`.
- **Remaining:** 260 → 259.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename BoatHireSubState members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Boat substate naming only: Normal becomes rowing=0, return-position state stays 1. Retain collision exemption, movement probabilities and fork vehicle motion.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U103 — `78fb8d73bb` — Rename members of ScenarioSource

- **Source:** `78fb8d73bb82f78b276b55399b393b8f1fe749fb`.
- **Fork receipt:** `05bea6ca18c140aca87d848dd0c3ce96f04f57c8`.
- **Remaining:** 259 → 258.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ScenarioSource members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Scenario source names only: preserve ten source IDs, ordering, scenario indexing, expansion detection and script API strings such as rct1_aa. Fork park version stays 60016.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U104 — `25fc422c44` — Rename members of IntroState

- **Source:** `25fc422c44b6298ea85a680bc44537464c59f517`.
- **Fork receipt:** `47796ba94619b34fbb61336b8b69236e3a6d5b98`.
- **Remaining:** 258 → 257.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename IntroState members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Intro state naming only: preserve 0-7 and special clear=254/finish=255 values, 80-tick wait, fade speeds and audio/click-skip lifecycle.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U105 — `38bce48524` — Rename members of ScatterToolDensity

- **Source:** `38bce4852403e43d47af72906fe53680f2ede126`.
- **Fork receipt:** `96efffcedd1825460551a9d951e3fb20995a49e3`.
- **Remaining:** 257 → 256.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename ScatterToolDensity members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Scatter density names only: preserve quantity multipliers 1/2/3 and default medium density with size 16; no scenery placement cost, randomness or density change.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.

### U106 — `a5892a2d17` — Enforce `enum class` member code style via Clang-Tidy

- **Source:** `a5892a2d173fce9dd838512bbe0151eae8b3a864`.
- **Fork receipt:** `ce868d41f6c665b6a349850e11c6157718b4eb33`.
- **Remaining:** 256 → 255.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add enum CamelCase and scoped enumerator camelBack clang-tidy rules.
- **Additional decisions / behavior:** Adopt style enforcement after individual renames. No runtime behavior change; existing warnings-as-errors policy retained.
- **Verification:** Actual four-line source delta reviewed and applied; whitespace and singleton ancestry checked.
- **Pending / concerns:** Clang-tidy is not on PATH; dedicated lint run remains unverified. Compile at naming checkpoint.

### U107 — `228c4bfb34` — Rename CursorNames to kCursorNames

- **Source:** `228c4bfb34d74729d67e4ba309a5ee5412f7851d`.
- **Fork receipt:** `8f8c39badff2656e712cc14543f71e4d3b72bcce`.
- **Remaining:** 255 → 254.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename CursorNames to kCursorNames at all four local sites.
- **Additional decisions / behavior:** Preserve all script cursor strings and indices, invalid-ID fallback and lookup behavior.
- **Verification:** Entire source patch reviewed; whitespace and singleton ancestry checked.
- **Pending / concerns:** Build with completed naming group.

### U108 — `205497a480` — Merge pull request #26876 from Gymnasiast/refactor/remaining-enum-class-members

- **Source:** `205497a4800f9cc1a06c8616318845e551f501d5`.
- **Fork receipt:** `8b96fd8f802f7c17479dc5380607a6145dbd8c3d`.
- **Remaining:** 254 → 253.
- **Disposition:** history receipt.
- **Manual changes:** Account for the completed enum-style branch merge; no additional source edit.
- **Additional decisions / behavior:** All constituent source changes already individually ported. Keep fork tree unchanged.
- **Verification:** Archived remerge delta empty; merge tree equals second-parent tree. Singleton ancestry checked.
- **Pending / concerns:** Build completed naming group next.

### U109 — `7f39bdebf5` — Remove openrct2/park includes

- **Source:** `7f39bdebf52fabf3bcb2a431cda2edbc9c83f914`.
- **Fork receipt:** `a08b88d1a2a3ebd3f8241616876abd9347bfcf2c`.
- **Remaining:** 253 → 252.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Remove seven unused park headers.
- **Additional decisions / behavior:** Retain ParkFile.cpp world/Park.h for fork entrance-fee target persistence and rct2/RCT2.h for directly referenced FootpathMapping/legacy limits. Preserve save version 60016 and custom recovery logic.
- **Verification:** Reviewed complete include-only delta and direct fork dependencies. Batch 13 build and 37 selected network/script/entity/widget tests passed through U108.
- **Pending / concerns:** Build header cleanup group next.

### U110 — `b3ef890f38` — Remove openrct2/peep includes

- **Source:** `b3ef890f383ea9af6904b079c906336f0b3ed4f0`.
- **Fork receipt:** `d543575a66cff59e73101f7c8d52763a65b80999`.
- **Remaining:** 252 → 251.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Narrow pathfinding header to Identifiers.h, remove unused forward declaration and two includes; format RideUseSystem header.
- **Additional decisions / behavior:** Keep bitset: fork transport routing uses destinationCandidateMask and walkAfterRideComputed. Header memory include already absent. No pathfinding or fare behavior changes.
- **Verification:** Actual patch and fork uses inspected; whitespace and singleton ancestry checked.
- **Pending / concerns:** Compile header group.

### U111 — `7f8fc39ff5` — Remove openrct2/platform includes

- **Source:** `7f8fc39ff533d965c7875101a833158196a518bb`.
- **Fork receipt:** `c270e7ae163fe8eeeebf1c8d4354b65d02339909`.
- **Remaining:** 251 → 250.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Remove unused Android/Emscripten/Linux/common platform headers.
- **Additional decisions / behavior:** Retain direct standard-library dependencies: Platform.Common.cpp uses strlen; Platform.h declares time_t APIs and a std::endian assertion. Keep cstring, ctime and bit despite proposed removal. No platform behavior change.
- **Verification:** Entire source delta and direct uses reviewed; whitespace and singleton ancestry checked.
- **Pending / concerns:** Windows compilation in next header checkpoint; non-Windows build unavailable here.

### U112 — `5c0caf461a` — Remove openrct2/rct1, openrct2/rct2 and openrct2/rct12 includes

- **Source:** `5c0caf461afb2acc4d704c817b284a2cec6b8467`.
- **Fork receipt:** `cc475ae5bbf43562fbf7ef321d38e26a3a8bc9c3`.
- **Remaining:** 250 → 249.
- **Disposition:** adopt with fork header dependencies retained.
- **Manual changes:** Apply RCT1/RCT2/RCT12 include narrowing and explicit type declarations at 19 paths.
- **Additional decisions / behavior:** Keep GuestPathfinding bit for direct std::popcount uses and S4Importer Map.h for map operations. Retain fork save/import recovery and topology invalidation; no logic or format changes.
- **Verification:** Reviewed source patch and adapted only header/declaration/whitespace blocks; fork-only neighboring includes preserved.
- **Pending / concerns:** Compile importer/header group and run migration/import regressions.

### U113 — `6323d95edc` — Remove openrct2/ride includes

- **Source:** `6323d95edc0a239031938ff8c20983f5894ecf7d`.
- **Fork receipt:** `552188767c33a2915ab44489d915354cc02e9111`.
- **Remaining:** 249 → 248.
- **Disposition:** adopt header cleanup with direct fork dependencies retained.
- **Manual changes:** Narrow ride headers, add direct declarations/includes to consumers, remove redundant headers and trailing whitespace across 160 paths.
- **Additional decisions / behavior:** Keep Vehicle.cpp Config.h for fork threaded rating updates and RideRatings.cpp TrackData.h for fork descriptor calls. Already-retained cstring/bit/ctime need no duplicate insertion. Keep TrackDesign.cpp primary self-include, removing only its duplicate. Preserve fork callbacks, station tick parameters, physics, ratings, rendering and all descriptors.
- **Verification:** All 160 fork token deltas restricted to includes/forward declarations/whitespace. Initial compile exposed missing TrackData declaration; restored it. Full rebuild and all 154 selected tests passed; see validation Batch 14.
- **Pending / concerns:** Non-Windows platform builds and interactive checks remain unverified.

### U114 — `53a81b1b45` — Merge pull request #26874 from Harry-Hopkinson/remove-even-more-includes

- **Source:** `53a81b1b45b95b0745fa6f3fb38162f8bef64d42`.
- **Fork receipt:** `f7ac048b68a050524e98ced537d51008bcc37a34`.
- **Remaining:** 248 → 247.
- **Disposition:** history receipt.
- **Manual changes:** Record completed header-cleanup branch merge; no additional source delta.
- **Additional decisions / behavior:** All constituent sources individually ported; retain manually adapted fork tree.
- **Verification:** Archived remerge empty and merge tree equals second parent. Singleton ancestry checked. Batch 14 build and 154 tests cover constituents.
- **Pending / concerns:** No additional merge-specific check required.

### U115 — `51649e5873` — Rework Ride and Vehicle headers into OpenRCT2 namespace (#26881)

- **Source:** `51649e5873c8441e8bfab995e3e4d1d5953417aa`.
- **Fork receipt:** `58609494400c74813795fe49aa9010321a5fddd7`.
- **Remaining:** 247 → 246.
- **Disposition:** adapt namespace refactor.
- **Manual changes:** Move Ride/Vehicle types, constants and functions into OpenRCT2; extract unchanged VehicleFlags header; update declarations/callers and correct Adjustment spelling/UI dropdown type collision.
- **Additional decisions / behavior:** D11/D12: preserve complete fork implementations, sampled/directed-leg ratings, longitudinal G, age multipliers 1.5/1.2, fares, station timing, physics and audio. Move fork-only rating/price/vehicle-station declarations too; retain native global CarEntry/TrackDesign types. Qualify legacy importer destination types while preserving source types. Restore PatrolArea declaration omitted during adaptation and qualify fork scripting/test callers; no global compatibility aliases.
- **Verification:** Actual upstream token delta inspected. Nine large fork implementation token comparisons prove only namespaces/qualification/spelling/include order changed. Header/17 flag values reviewed. Batch 15 full MSVC/Vulkan build passed 0 warnings/errors and 177 selected tests passed. Whitespace and singleton ancestry gates.
- **Pending / concerns:** Native non-Windows and interactive checks remain; no new owner decision.

### U116 — `4087c9c6b9` — Rework script binding includes (#26882)

- **Source:** `4087c9c6b9a6a6c6871dda823ee1473006902679`.
- **Fork receipt:** `592235f42d829b3083328e750d3dad6445481012`.
- **Remaining:** 246 → 245.
- **Disposition:** adapt header cleanup.
- **Manual changes:** Reorganize script binding includes and forward declarations; prefix two internal string arrays with k.
- **Additional decisions / behavior:** Retain cstring and utility for fork ScTile atomic tile replacement memcpy/move calls. Public script strings and all binding behavior unchanged; companion objects ownership unchanged.
- **Verification:** Full actual source diff reviewed; all 34 fork paths verified limited to includes, forward declarations and the two internal names. Whitespace/ancestry checks.
- **Pending / concerns:** Compile and scripting checkpoint after header group.

### U117 — `15b36d6ced` — Introduce dedicated header for window-related enum types (#26883)

- **Source:** `15b36d6ced4d227826a1ad4c218cd5fa5ae79835`.
- **Fork receipt:** `c8c6c49920b4eab3f688e75c6217947490dd6a33`.
- **Remaining:** 245 → 244.
- **Disposition:** adapt window type/header ownership.
- **Manual changes:** Extract WindowTypes.h, namespace WindowClass and related types/callbacks, move widget-index macro to its owning header, update includes and declarations, register new header in MSBuild.
- **Additional decisions / behavior:** All 14 moved definitions preserve values, field order and underlying types. Retain Window.h for fork replay camera operations. Remove obsolete global PromptMode forward declaration alongside fork DrawingEngine declaration; preserve benchmark configuration. Move Game.cpp using directive before Emscripten declarations. No gameplay, save-prompt, widget-index or class ID changes.
- **Verification:** Entire source diff inspected; moved definition bodies independently compared identical against fork HEAD. Whitespace and singleton ancestry gates. Build/test checkpoint pending. Batch 16 full MSVC/Vulkan build passed 0 warnings/errors; all 13 scripting/widget/network tests passed.
- **Pending / concerns:** Native Emscripten/non-Windows builds and interactive window/replay checks remain.

### U118 — `9c59a4c5e8` — Fix notation of two numbers

- **Source:** `9c59a4c5e8dde0d146ec6b501745a9a565c4efe0`.
- **Fork receipt:** `ebca1bd84d32a3341ec7f0f187ce215408300dde`.
- **Remaining:** 244 → 243.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Normalize two unsigned-zero suffixes in land dropdown index lookup.
- **Additional decisions / behavior:** 0U and 0u denote the same type/value; no ordering or selection behavior changes.
- **Verification:** Actual two-line source diff inspected; whitespace and singleton ancestry checks.
- **Pending / concerns:** Covered by next compile checkpoint.

### U119 — `536973e9e6` — Add GetFlagHolder() overload for normal/inverted flag

- **Source:** `536973e9e6aa355049831dabf306d916ff09464e`.
- **Fork receipt:** `5f69d7b4b691ebb476244c4ab4d5a9a842e8e48f`.
- **Remaining:** 243 → 242.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add tuple-based GetFlagHolder overload supporting normal/inverted JSON flags.
- **Additional decisions / behavior:** Same truth table as existing GetFlags: normal missing/false unset, true set; inverted missing/false set, true unset. Missing properties keep legacy false default. No caller changes in this source.
- **Verification:** Full source and existing FlagHolder zero initialization/GetFlags implementation inspected; whitespace and ancestry checks.
- **Pending / concerns:** Template instantiation and object consumers covered at next flag-group build.

### U120 — `ac8b290671` — Refactor WALL_SCENERY_FLAGS into enum class+FlagHolder

- **Source:** `ac8b29067165e49ae492a7c3d6fbd292c475371d`.
- **Fork receipt:** `2b47fc47c7c8c3a30f85862b80981d1f96f5d848`.
- **Remaining:** 242 → 241.
- **Disposition:** adapt wall flags.
- **Manual changes:** Replace wall scenery bit constants with scoped WallSceneryFlag/uint8 FlagHolder; convert predicates, DAT byte load and JSON reader.
- **Additional decisions / behavior:** Preserve eight legacy bit positions, byte storage, door timing, slope restrictions, colours and double-sided behavior. Keep inverted isAllowedOnSlope missing/false restriction and both isBanner/isDoubleSided plus hasTernaryColour aliases. Preserve secondary/tertiary-only colour normalization and all companion object data; no data migration needed.
- **Verification:** Inspected all 10 actual source diffs and current callers. Each has/hasAny/set maps to same old bit mask; all former wall flag identifiers absent. Whitespace/singleton ancestry checks.
- **Pending / concerns:** Build and object-load checks at flag-group checkpoint.

### U121 — `94a889e4c9` — Refactor SCROLL_FLAGS into enum class+FlagHolder

- **Source:** `94a889e4c9d6d719e4072270a327dd650cf2c4ec`.
- **Fork receipt:** `a0c1e1ce8275908d628b0334e2ce27c68e5d12b8`.
- **Remaining:** 241 → 240.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Convert scrollbar bit constants to ScrollFlag and uint16 FlagHolder; use named predicates/set/clear/unset.
- **Additional decisions / behavior:** Preserve eight bit positions, scroll offsets/ranges, wheel handling, drawing conditions and 16-bit storage. Release clears the same six pressed bits as old 0xFF11 mask, retaining visibility and upper bits.
- **Verification:** Full four-file source diff reviewed; exhaustive 65,536-value mask comparison passed; no old scroll identifiers remain; whitespace/ancestry checks.
- **Pending / concerns:** Build/widget checkpoint follows flag group; interactive scroll dragging remains pending.

### U122 — `c2c126da9c` — Refactor BTM_TOOLBAR_DIRTY_FLAGS into enum class+FlagHolder

- **Source:** `c2c126da9ce7ada8eedac10549d8fd4f07163669`.
- **Fork receipt:** `d1dce1682d9bae8be186268d328e1d87e82b68f0`.
- **Remaining:** 240 → 239.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Convert five bottom-toolbar dirty flags to typed uint8 FlagHolder and update all producers/consumers.
- **Additional decisions / behavior:** Retain bit positions 0-4, zero initial state, invalidation targets/order and clearing semantics. climate-to-weather and peep-to-guest names do not change simulation or refresh cadence.
- **Verification:** Actual four-file delta and every gToolbarDirtyFlags reference reviewed; no old identifiers remain; whitespace/ancestry gates.
- **Pending / concerns:** Build/widget checkpoint after merge receipt.

### U123 — `263cb53047` — Merge pull request #26884 from Gymnasiast/more-enum-refactor

- **Source:** `263cb53047ea432cf6bd80507095f69982065e42`.
- **Fork receipt:** `3bb88b05112330292f930290c863444128f5848a`.
- **Remaining:** 239 → 238.
- **Disposition:** history receipt.
- **Manual changes:** Record completed flag-refactor branch merge; no additional source delta.
- **Additional decisions / behavior:** Keep individually ported fork tree; no upstream whole-tree merge.
- **Verification:** Archived remerge is empty and tree equals second parent; singleton ancestry gate.
- **Pending / concerns:** Flag-group compile/test checkpoint starting.

### U124 — `5a814722e1` — Add Walls-only and Footpath Addition modes to Clear Scenery (#26877)

- **Source:** `5a814722e136db8fc168f06a4794f29c88ca31b9`.
- **Fork receipt:** `68226604c5923eb5429ff7cfe3ab27a42040f7f8`.
- **Remaining:** 238 → 237.
- **Disposition:** adopt approved D10 with fork protocol adaptation.
- **Manual changes:** Add separate walls and path-addition clear modes, icons/tooltips, API-117 mask documentation and UI controls; implement through existing nested removal actions. Add 32-mask serialization/query/execute regression.
- **Additional decisions / behavior:** D10 approved: bit 1 now small scenery only, walls bit 8, additions bit 16; UI defaults keep small scenery plus walls, no old-plugin shim. No tracked script callers and no installed JS/TS plugins found; embedded park scripts not scanned. Advance independent fork stream 4 to 5 rather than upstream 0 to 1. New label 7040 leaves fork IDs 8000-8041 intact. Preserve erasure-restart traversal, nested permissions/costs, coordinate-based staff claims and companion object pin. Addition removal mutates a field without tile erasure or topology replacement; existing bin work rechecks addition presence.
- **Verification:** Actual complete source diff inspected; binary icons exact source blobs. Batch 17 flag build plus 50 tests clear prior debt. Batch 18 full build 0 warnings/errors; 18 tests passed, including all 32 masks after serialization, nonmutating queries, cost agreement, adjacent wall erasure and ghosts. Whitespace/ancestry gates.
- **Pending / concerns:** Directed active staff-claim case, positive-cost insufficient-funds combinations, embedded park scripts and interactive buttons remain explicit validation debt.

### U125 — `42885ec965` — Update GitHub checkout action to v7 (#26885)

- **Source:** `42885ec9659d628a842f76d45be9c9430ba1a8fb`.
- **Fork receipt:** `e51a0764fe69912eebeddd8d176ea22bcc58c265`.
- **Remaining:** 237 → 236.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Update checkout action references to v7 in the three reviewed workflows.
- **Additional decisions / behavior:** Preserve fork workflow content, repository paths, triggers, tokens and permissions; no workflow dispatched or artifacts published.
- **Verification:** Actual source diff is version-reference substitutions only; existing job structures retained; whitespace/ancestry checks.
- **Pending / concerns:** Hosted CI execution unverified; no local gameplay impact.

### U126 — `d7d8915065` — Fixed bug where ride.previousVerticalG was reset to 0 instead of 100 (#26879)

- **Source:** `d7d89150658d8f6a654ce10cc8d598d844fe277c`.
- **Fork receipt:** `256db7e697e1d97276f3c3ab0e1ed2d7e19ede75`.
- **Remaining:** 236 → 235.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Record upstream changelog entry; reset-code fix is already satisfied in the fork.
- **Additional decisions / behavior:** Keep previousVerticalG at 1G, longitudinal reset at zero and sampled reset policy. No simulation change, so retain fork stream revision 5; do not copy upstream revision 2 or increment for a no-op. Changelog records upstream history, not a newly changed fork force model.
- **Verification:** Inspected source and current Vehicle::TestReset: exact 1G assignment already present. Prior ratings checkpoint covers this retained implementation. Whitespace/singleton ancestry checks.
- **Pending / concerns:** No new runtime check required for retained implementation.

### U127 — `f91e2d680b` — Remove openrct2/sawyer_coding includes

- **Source:** `f91e2d680b9daee427eac08d379a27038e6c9fd3`.
- **Fork receipt:** `d3a84032e00858486e9e5f1bd4eac16814c89cfc`.
- **Remaining:** 235 → 234.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Replace unused Sawyer coding headers with direct ownership includes; remove redundant IStream declaration.
- **Additional decisions / behavior:** Compression, chunk format and byte handling unchanged.
- **Verification:** All four source deltas inspected; includes/declaration only; whitespace/ancestry gates.
- **Pending / concerns:** Build/Sawyer checkpoint after header group.

### U128 — `f4ddda3f69` — Remove openrct2/scenario includes

- **Source:** `f4ddda3f6974938165d15571b7014c4c0c731636`.
- **Fork receipt:** `3a15714082b3b6f83482ced6de73f471f53719cf`.
- **Remaining:** 234 → 233.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rework scenario header dependencies and forward-declare ObjectiveType in repository header.
- **Additional decisions / behavior:** Preserve fork scenario startup, admission policy, objective checks and timing; no scenario gameplay change.
- **Verification:** Five fork paths verified include/forward-declaration-only; actual source reviewed and fork consumers inspected; whitespace/ancestry gates.
- **Pending / concerns:** Build checkpoint after header group.

### U129 — `f967ebc2d2` — Remove openrct2/scenes includes

- **Source:** `f967ebc2d239eb9cb5762c5c060aea59dd212775`.
- **Fork receipt:** `ea5e9a6da14b6da5e7ba35546c605ee9bbcc2f69`.
- **Remaining:** 233 → 232.
- **Disposition:** adopt applicable header cleanup.
- **Manual changes:** Remove unused scene/editor/title implementation includes.
- **Additional decisions / behavior:** Retain existing fork scene lifetimes, gameStateTick dispatch, editor startup and title behavior; no runtime code changes.
- **Verification:** All 12 source paths inspected and verified include-only against current fork; whitespace/ancestry gates.
- **Pending / concerns:** Build checkpoint after header group.

### U130 — `6c5c2079ce` — Remove openrct2/scripting includes

- **Source:** `6c5c2079ce16675a7996a426d707a6f4ccc2cbce`.
- **Fork receipt:** `4c6cf4bdbeb1b71010e587f0faa3a2029cf081c9`.
- **Remaining:** 232 → 231.
- **Disposition:** adapt script header cleanup.
- **Manual changes:** Remove unused script-binding includes/declarations and tidy prior namespace comment/blank line.
- **Additional decisions / behavior:** Retain Profiling.h for fork PROFILED_FUNCTION and cassert for live handle assertion. Preserve all fork script bodies, public API strings and entity lifecycle behavior.
- **Verification:** Thirty fork paths verified include/declaration/comment/whitespace-only; actual source and fork profiling/assert callers inspected; whitespace/ancestry gates.
- **Pending / concerns:** Build/scripting checkpoint after header group.

### U131 — `3c375c60ba` — Remove openrct2/windows includes

- **Source:** `3c375c60ba4cb354f8672e0aec10e7a585ce789a`.
- **Fork receipt:** `7823231e25a13abf729d9dbbb8ce4a486926d493`.
- **Remaining:** 231 → 230.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Remove unused Guard and map includes from Intent implementation/header.
- **Additional decisions / behavior:** Intent payload, dispatch and fork behavior unchanged.
- **Verification:** Actual two-file diff reviewed; no Guard/std::map consumers; whitespace/ancestry gates.
- **Pending / concerns:** Compile checkpoint after header group.

### U132 — `8cd44cc686` — Remove openrct2/world includes

- **Source:** `8cd44cc6861ed74658c7deeec81eaf5513e5f294`.
- **Fork receipt:** `112e9f4af4ea2611e8823f14ad2a000634baede6`.
- **Remaining:** 230 → 229.
- **Disposition:** adapt world header cleanup.
- **Manual changes:** Rework 46 world/tile/object consumer includes and forward declarations.
- **Additional decisions / behavior:** Retain Map.cpp Guard.hpp for mutation bounds assertions and TerrainSurfaceObject.h for snapshot surface rendering. Adapt ScTileElement insertion anchors to fork include set. Preserve all fork clearance erasure results, mutation/presentation ownership, topology, park economics/growth and weather logic; no runtime expression changed. Restore Footpath.cpp PathAdditionEntry.h for fork IsBin helper, identified by two compiler diagnostics.
- **Verification:** Complete source diff inspected; all 46 fork paths pass include/forward-declaration-only comparison. Whitespace and singleton ancestry checks. Batch 19 in progress. Batch 19 complete rebuild passed 0 warnings/errors; 61 selected tests passed.
- **Pending / concerns:** Native non-Windows and interactive checks remain; compile debt through U132 cleared.

### U133 — `8839956721` — Merge pull request #26886 from Harry-Hopkinson/remove-more-includes

- **Source:** `8839956721913ef48e6981477339a01f6f39e4f5`.
- **Fork receipt:** `93185c989637bf2091daf94963b91878744da82c`.
- **Remaining:** 229 → 228.
- **Disposition:** history receipt.
- **Manual changes:** Record header-cleanup branch merge; no new manual source change.
- **Additional decisions / behavior:** Preserve separately integrated fork tree. Merge difference from second parent is only U126 changelog, 1G reset and upstream stream bump; U126 already handled with fork stream 5 retained.
- **Verification:** Archived remerge empty; full three-file second-parent delta inspected and matches previously handled U126; singleton ancestry checks.
- **Pending / concerns:** Batch 19 build and 61 tests cover constituent ports.

### U134 — `11bf58dceb` — Refactor CLEARABLE_ITEMS into enum class+FlagHolder

- **Source:** `11bf58dceb588ec4e1d04fa2e247b55979cbb0bc`.
- **Fork receipt:** `ddd09a1ca588b2acae4fd2b98e084edabe1a4016`.
- **Remaining:** 228 → 227.
- **Disposition:** adapt clear flags.
- **Manual changes:** Replace clear bit constants with scoped ClearableItem/uint8 FlagHolder; update UI, action predicates, visitor and serializer; adapt fork regression raw-mask constructor.
- **Additional decisions / behavior:** Preserve approved D10 bit values and behavior, including independent walls/additions. Serialize and visit holder byte so wire payload width/meaning stays the same; retain fork stream revision 5. No compatibility shim or new gameplay change.
- **Verification:** Actual complete three-file source diff inspected; all old constants absent. Batch 20 full build passed 0 warnings/errors and three selected tests passed, including all 32 masks with serialization, query/execute, erasures and ghosts. Whitespace/ancestry checks.
- **Pending / concerns:** Existing U124 live-staff, positive-cost insufficient-funds, embedded-script and interactive debt remains.

### U135 — `a70874710a` — Refactor peep flags to enum class+FlagHolder

- **Source:** `a70874710a412bdb62f6179018ee72894aae9ed7`.
- **Fork receipt:** `a68f07dd91f6f145b4989f40f1e94e1a1393993b`.
- **Remaining:** 227 → 226.
- **Disposition:** adopt with fork-specific flag callers adapted.
- **Manual changes:** Convert peep flags to a uint32 FlagHolder with all 32 original bit positions; adapt actions, UI, scripts, snapshots, save/import and simulation callers. Port the eighteen Easter-egg conditional setters and bool return. Convert additional fork fare/routing and test callers and the optimized interaction mask.
- **Additional decisions / behavior:** Retain fork guest motives/happiness, fare/value and directed transport logic. Preserve short-circuit random calls and all freeze/tracking/lost-state behavior. Keep the RCT2 packed source PeepFlags field raw; serialize modern holder explicitly. The fork bulk park serializer uses holder in place. Upstream park-rating hunk has no surviving counterpart in the overwritten fork rating implementation and is not reintroduced. Check-name implementation already returns a boolean comparison. Existing 27 script flag strings remain unchanged. No network revision change (fork stays 5) or save-version change.
- **Verification:** Inspected actual source diff; all 32 old masks mapped to identical positions. Nine fork body-token comparisons pass, including additional fare/speed/interaction callers. Whitespace check passes; Batch 21 full Release x64 MSVC/Vulkan build 0 warnings/errors and 151 tests in eight suites pass.
- **Pending / concerns:** Interactive guest tracking and Easter-egg presentation plus native non-Windows builds remain unverified; standing migration validation debt remains.

### U136 — `f08bc61f03` — Refactor park flags into enum class+FlagHolder

- **Source:** `f08bc61f03fa135e2df0c472bfe3b0d5d22af1bd`.
- **Fork receipt:** `02dfaef98638a6f285a183645ab262172e24fae7`.
- **Remaining:** 226 → 225.
- **Disposition:** adopt typed flags with fork payment/rating callers retained.
- **Manual changes:** Convert 21 modern park flags to uint64 FlagHolder and 15 packed RCT1 flags to uint32 FlagHolder. Adapt 64 fork files across UI, simulation, actions, import/save, scripts and tests. Retain fork fee-target assignments and use typed test snapshots. Simplify conditional setters; preserve finance-window invalidations only outside the scenario editor.
- **Additional decisions / behavior:** Fork fare/time path choice, entry-price targets, income debuff, guest generation and rewritten park-rating algorithms remain unchanged. Extra fork-only combined price-unlock predicates use hasAny. RCT1 bit 13 remains separately named parkEntryLockedAtFree; source import semantics and RCT2 scenario-only no-money translation are preserved. Save payload remains uint64. Temporary flag backups now retain all 64 bits; unset preserves unused upper 32 bits instead of inadvertently clearing them through a uint32 complement. No fork-defined flag occupies those bits. Keep this preservation correction. No protocol/save-version bump; modern protocol remains 5. No deleted upstream rating/generation algorithms are restored.
- **Verification:** Inspected all actual source changes and complex branch/import contexts. All 21 modern and 15 RCT1 bit positions retained; 53 ordinary fork caller files pass normalized body-token comparison. Batch 22 final build 0 warnings/errors and 176 tests in nine suites pass; initial syntax failure and correction documented in validation log.
- **Pending / concerns:** Interactive editor/track-design backup restoration and native non-Windows builds remain unverified. Standing migration validation debt remains.

### U137 — `5aa3f85c56` — Merge pull request #26891 from Gymnasiast/more-enums

- **Source:** `5aa3f85c56c4bd68500380190d9deac357686423`.
- **Fork receipt:** `301e6cc84cd65f1759143910f24da130ed6c8fd3`.
- **Remaining:** 225 → 224.
- **Disposition:** history receipt.
- **Manual changes:** Record the enum/FlagHolder branch merge; all implementation deltas are already individually ported through U136.
- **Additional decisions / behavior:** No additional behavior or code changes. Retain all prior fork adaptations.
- **Verification:** Inspected merge parents, empty archived remerge diff, and empty tree delta against the second parent f08bc61f03. Batch 22 validation remains applicable.
- **Pending / concerns:** Standing migration validation debt remains.

### U138 — `2e43967fe5` — Fix: water rides ignore zero clearances, preventing adjacent terrain modifications and building them anywhere (#26816)

- **Source:** `2e43967fe5738d991623dba05ef85a8f5d24221c`.
- **Fork receipt:** `216701c39ac614a8b63b1f7fde6ce20fba03f764`.
- **Remaining:** 224 → 223.
- **Disposition:** adopt approved D03 in clearance-cheat mode only.
- **Manual changes:** Gate floating-structure terrain restrictions, water-height protection and water-only track placement constraints on disableClearanceChecks. Add the upstream changelog entry. Advance fork protocol revision 5 to 6. Add a directed query/execute regression for all three actions.
- **Additional decisions / behavior:** D03 was approved: ordinary construction rules remain unchanged. Keep missing-surface, ownership, parameter and support-limit checks, plus existing fork clearance traversal and element-erased handling. No upstream legacy traversal is copied. Retain fork protocol flavor and increment its revision rather than using upstream 2-to-3 values. Off-water rides remain an explicit cheat outcome; no simulation fallback or automatic water repair is introduced.
- **Verification:** Inspected actual five-file source diff and fork action bodies. Batch 23 full build passes after fixing a test-only iterator compile error; new water-rule matrix plus clear/network checks (3 tests) and map/path topology (29 tests) all pass. Whitespace check passes. Test evidence and limitations recorded in validation log.
- **Pending / concerns:** Interactive placement preview, running water rides off water and native non-Windows checks remain unverified; standing migration validation debt remains.

### U139 — `4c1cedbb08` — Refactor wall scenery flags 2 into enum class+FlagHolder

- **Source:** `4c1cedbb084cfa77b1523d593993d6fcdbdaca29`.
- **Fork receipt:** `03ba7ecf75fbbc4781a0d8963ae613fc5c37d23b`.
- **Remaining:** 223 → 222.
- **Disposition:** adopt secondary flags and separate validated door sound.
- **Manual changes:** Type wall flags2 with bits 0/3/4 preserved; split legacy door bits 1/2 into a separate sound field. Adapt JSON/DAT loading, painting, guest sight checks, map animation and vehicle door callers. Remove the obsolete getter translation unit/project entry. Add all-byte legacy and invalid-JSON import coverage.
- **Additional decisions / behavior:** Keep fork cent-money conversion and all guest sight/sound/animation behavior for valid objects. Validate sound indices consistently with TerrainEdgeObject: unsupported values become none rather than indexing beyond the three-entry audio arrays. Legacy invalid sound 3 is muted; invalid JSON values no longer alias to 0/1/2 through the old two-bit mask. Explicit none default/reset avoids stale values on repeated reads. Preserve XXWLBR03 door correction and isOpaque alias. Authoritative sibling objects audit found seven wall sound definitions all valid (plus nine terrain-edge definitions); no objects edit or pin change needed.
- **Verification:** Inspected actual nine-file source delta and fork consumers. Exhaustive import test for all 256 legacy bytes and nine JSON values passes, including flag independence and cent price. Batch 24 build 0 warnings/errors and 51 import/audio tests pass after correcting missing test context. No stale old flags/getter/project references remain; whitespace check passes.
- **Pending / concerns:** Interactive door rendering/audio and native non-Windows builds remain unverified; standing migration validation debt remains.

### U140 — `d4c67d9b8d` — Turn FrictionSound into enum class

- **Source:** `d4c67d9b8df443306e439c3c30e52100c9e041a2`.
- **Fork receipt:** `a122ba9e8c192d24e23ce511f71493c9ec88f8b9`.
- **Remaining:** 222 → 221.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Replace the nine unused global friction-sound constants with the uint8 FrictionSound enum class in Vehicle.h.
- **Additional decisions / behavior:** All numeric values remain 1,2,21,31,32,54,57,65,255. There are no old-constant consumers. Keep existing CarEntry audio identifiers, spatial audio and object-owned kart gain unchanged.
- **Verification:** Inspected the complete one-file source patch; searched all src/test consumers and verified numeric identity. Batch 24 audio/object validation is the prior checkpoint.
- **Pending / concerns:** Compile with the following vehicle type/rename batch; no new runtime behavior.

### U141 — `8b4b1c6561` — Refactor VEHICLE_VEHICLE_* to two enum classes

- **Source:** `8b4b1c6561b407359dc3198dfc09ee2737bc27d2`.
- **Fork receipt:** `d537a2ee5416017d38a6a40388ec36b78dd72db7`.
- **Remaining:** 221 → 220.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Split vehicle paint style and splash-effect identifiers into two uint8 enum classes; rename the two CarEntry fields and adapt JSON/DAT readers, draw dispatch, construction availability, defaults and numeric script getters across 13 files.
- **Additional decisions / behavior:** Preserve values 0-9 and 15-17 for paint styles, 1 and 10-14 for effects, JSON defaults 0/1, legacy spinner reset and the existing spinning-car object restriction. Default switch branches keep unrecognized values as no-op. Keep all fork CarEntry fields, costs, kart gain and render algorithms.
- **Verification:** Inspected complete 13-file source diff and every fork delta. Numeric/default/read widths are unchanged; no stale old field or enum callers remain (one historical comment retains its spelling). Whitespace check passes.
- **Pending / concerns:** Compile and regression-test with the following RTD/vehicle rename batch; interactive vehicle effects and native non-Windows remain unverified.

### U142 — `bb0b26e3f2` — Rename RTD constants

- **Source:** `bb0b26e3f2155d180cefce6cfe9e8fd0f35b294d`.
- **Fork receipt:** `38415e70f465f52fc2fba10991723207f128ccf3`.
- **Remaining:** 220 → 219.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 94 ride-type descriptor constants with the k prefix across 92 definition and registry files.
- **Additional decisions / behavior:** Preserve every fork descriptor initializer, ride-type ordering, sampled-rating setup, costs, heights, flags and callbacks. No upstream descriptor values are copied.
- **Verification:** Read source definition changes and verified every delta in all 92 actual source files as identifier substitution plus whitespace only. Applied only those 94 exact identifier tokens to 92 current fork files; all other bytes and initializer expressions remain unchanged. Whitespace check passes.
- **Pending / concerns:** Compile and test with the pending vehicle rename batch; U140-U142 build debt remains explicit.

### U143 — `5d3903209e` — More enum class conversions (plus a constexpr rename) (#26897)

- **Source:** `5d3903209e66820cdf129ed86c2a4a1f51ce8e4c`.
- **Fork receipt:** `ccb38d4f6ae6c7723b96147ac35f8ec885c51cb0`.
- **Remaining:** 219 → 218.
- **Disposition:** history receipt.
- **Manual changes:** Record the wall/vehicle enum and RTD rename merge; the implementation is already individually handled in U139-U142.
- **Additional decisions / behavior:** No further changes; retain fork invalid-door-sound validation and all vehicle/descriptor adaptations.
- **Verification:** Inspected both merge parents, empty archived remerge and empty tree delta against second parent bb0b26e3f2.
- **Pending / concerns:** U140-U142 compile/test checkpoint remains pending with the following vehicle rename batch.

### U144 — `5c0fc86397` — Rename CarEntry members to adhere to code style

- **Source:** `5c0fc863979b7c7b9a8675cf69d51661309c0033`.
- **Fork receipt:** `12959e5113028b03130aea9e2f8089caffd561c2`.
- **Remaining:** 218 → 217.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 31 CarEntry members and methods across 40 files, including fork-only platform waiting callers; retain all fork implementation expressions.
- **Additional decisions / behavior:** CarEntry numSeats/poweredAcceleration are distinct from live Vehicle num_seats/powered_acceleration. Preserve fork friction_sound_gain and serialized/JSON/scripting names. No gameplay or protocol change.
- **Verification:** All 40 source deltas and fork body tokens verified as identifier-only changes. Batch 25 full Release x64 MSVC/Vulkan build: 0 warnings/errors, 86.24 seconds; 204 tests in 12 suites passed in 28.828 seconds. Whitespace check passed.
- **Pending / concerns:** Interactive vehicle rendering and native non-Windows builds remain unverified; U140-U144 compile/test debt cleared by Batch 25.

### U145 — `a629158a4b` — Rename SpriteGroupNames to kSpriteGroupNames

- **Source:** `a629158a4bbe87b89b8d16f86e218ab23e0d8a24`.
- **Fork receipt:** `49df23eafd03e1acc90f9e8bca399f1d4d4a80d6`.
- **Remaining:** 217 → 216.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename SpriteGroupNames to kSpriteGroupNames in the definition and both JSON/scripting callers; retain all 40 group strings and order.
- **Additional decisions / behavior:** Pure internal constant rename; JSON keys and scripting property names unchanged.
- **Verification:** Inspected all three source file deltas and fork adaptations; static count assertion retained; whitespace check passed.
- **Pending / concerns:** Compile with the next coherent batch.

### U146 — `24869300df` — Rename VehicleSpriteGroup member function to adhere to code style

- **Source:** `24869300dfeb6d5f80587e3dfb5c934198eeccf2`.
- **Fork receipt:** `ec071472bc48ab54092d2c751bbceaa68a3dee4f`.
- **Remaining:** 216 → 215.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename VehicleSpriteGroup.Enabled to isEnabled and update all three call sites.
- **Additional decisions / behavior:** Internal method naming only; spritePrecision comparison and rendering/object/script behavior remain identical.
- **Verification:** Inspected complete four-file source patch and fork delta; all callers updated and whitespace check passed.
- **Pending / concerns:** Compile with the next coherent batch.

### U147 — `bd935d75c9` — Merge pull request #26899 from Gymnasiast/more-renames

- **Source:** `bd935d75c947eba5c4620e96cd978c25c9484468`.
- **Fork receipt:** `9df017697391c906e31dc6961cdefaf89e84d97a`.
- **Remaining:** 215 → 214.
- **Disposition:** history receipt.
- **Manual changes:** Record the CarEntry naming merge; source changes already handled individually in U144-U146.
- **Additional decisions / behavior:** No additional implementation changes or behavioral decisions.
- **Verification:** Both merge parents inspected; empty remerge and identical second-parent tree verified.
- **Pending / concerns:** U145-U146 compile checkpoint remains pending with next coherent batch.

### U148 — `a187bc9550` — Close #26827: Add map resize hook to scripting API (#26828)

- **Source:** `a187bc95503c9f35f224046e6bf945ab6adfac39`.
- **Fork receipt:** `74108ad686e00486e322d61a7c586310c23656a7`.
- **Remaining:** 214 → 213.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add API-118 map.resize subscription with targetSizeX/Y and shiftX/Y after completed resize/shift, fork topology reset, park size and UI updates. Add directed real-plugin regression.
- **Additional decisions / behavior:** Preserve notifications on successful shift-only/no-op actions and no query notifications. Retain mutable callback semantics. Add missing ENABLE_SCRIPTING guard; advance fork protocol 6 to 7 because subscribed plugins can change simulation state. Preserve map algorithms and park format 60016.
- **Verification:** Complete source/fork patch review; full Release x64 MSVC/Vulkan build 0 warnings/errors in 87.30 seconds; all 32 scripting/network/map topology tests passed in 1.790 seconds. Directed plugin checks payloads, completed size, single-player mutation, queries, expansion/shrink/shift/no-op and disposal.
- **Pending / concerns:** No-scripting build, multiplayer/replay hook synchronization, callback-visible topology internals, interactive map resizing and native non-Windows builds unverified. U145-U148 compile/test debt cleared by Batch 26.

### U149 — `4e576a04ad` — Fix forced portrait orientation on Android app launch (#26873)

- **Source:** `4e576a04ada64265e64abc4132a9ea6f06a8ebb6`.
- **Fork receipt:** `7c48c88804099dbe3718e33ce5f15d51caa8478a`.
- **Remaining:** 213 → 212.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Set Android launcher and game activities to sensorLandscape and remove SDL orientation-hint overrides, leaving the manifest to own orientation.
- **Additional decisions / behavior:** Adopt landscape-only startup and sensor-controlled landscape direction; runtime SDL portrait/resize hints no longer override the app policy. No gameplay change.
- **Verification:** Inspected complete two-file source patch and unchanged fork contexts. Manifest XML parses; both activities report sensorLandscape; whitespace check passed.
- **Pending / concerns:** Android build, launch and physical rotation checks unavailable on this Windows checkpoint.

### U150 — `5ca2c1845c` — Remove openrct2-ui/drawing includes

- **Source:** `5ca2c1845c5ef1d4a6dc79a0b980851b4bc668bc`.
- **Fork receipt:** `c0c5209dbdf1e62984f3bb6a11232816c9c5d2ef`.
- **Remaining:** 212 → 211.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Replace broad SDL.h with SDL_rwops.h and SDL_surface.h in BitmapReader. Review all 16 source paths; OpenGL-only files remain deleted. The omitted Vehicle.h ObjectEntryIndex alias is corrected with U151.
- **Additional decisions / behavior:** Do not resurrect the removed OpenGL backend. Correction after this receipt: Identifiers.h defines entity/ride identifiers, not ObjectEntryIndex; the initial skip rationale was mistaken. Add the reviewed uint16 alias in U151 to remove the transitive dependency. OpenGL directory exists but is empty; its source files are absent. Preserve Vulkan implementation.
- **Verification:** Complete source patch reviewed and live BitmapReader body unchanged after include normalization; whitespace check passed. Follow-up inspection corrected two inaccurate initial verification statements: the OpenGL directory is empty rather than absent, and ObjectEntryIndex is defined by ObjectTypes.h rather than Identifiers.h.
- **Pending / concerns:** Compile the live bitmap reader with next UI header batch; OpenGL targets deliberately unavailable.

### U151 — `e6be313f1d` — Remove openrct2-ui/input includes

- **Source:** `e6be313f1d925c83ab4055960b0cfc8611eddabf`.
- **Fork receipt:** `8079602451ba0bac7c038e3915c42b79bae57e85`.
- **Remaining:** 211 → 210.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Narrow UI input includes across four files. Retain direct SDL_gamecontroller.h and utility for fork controller ownership/input moves. Repair U150 omission by adding reviewed ObjectEntryIndex uint16 alias in Vehicle.h.
- **Additional decisions / behavior:** No runtime expression changes. Correct U150 mistaken note that Identifiers.h defines ObjectEntryIndex: it does not; ObjectTypes.h defines the alias. U150 journal now explicitly records the error and U151 correction. OpenGL directory is empty, not absent; deleted source files remain deleted.
- **Verification:** All four source patches inspected; five fork paths pass normalized body comparison allowing only include/forward declarations and the reviewed alias. Whitespace check passed.
- **Pending / concerns:** Compile UI input and prior BitmapReader/Vehicle.h cleanup at the next coherent batch.

### U152 — `d6863ccf99` — Remove openrct2-ui/interface includes

- **Source:** `d6863ccf998573fd6cecbf33272cf378523ea554`.
- **Fork receipt:** `67f46bb34e48ef39ac71c42ae6320de0311d23b2`.
- **Remaining:** 210 → 209.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Narrow UI interface headers and add direct Window.h includes to window consumers; forward declare RenderTarget/WindowBase/WindowClass where sufficient. Keep deleted OpenGL TextureCache absent.
- **Additional decisions / behavior:** Header ownership only. All fork UI bodies, cent-money graphs, ride/guest controls, Vulkan presentation and input behavior remain unchanged.
- **Verification:** Inspected complete 94-path source patch; selected 93 live paths. All modified fork paths pass normalized include/forward-declaration-only body proof; whitespace check passed.
- **Pending / concerns:** Compile with remaining UI include cleanup batch; native non-Windows and interactive UI remain unverified.

### U153 — `d9c423b1d6` — Merge pull request #26907 from Harry-Hopkinson/start-removing-ui-includes

- **Source:** `d9c423b1d6e7698e8435ab5d301ec52cf7ab5720`.
- **Fork receipt:** `313324f5f74cd62fd817d0aac4c0052c1c16e108`.
- **Remaining:** 209 → 208.
- **Disposition:** history receipt.
- **Manual changes:** Record the UI include merge; constituent changes handled in U150-U152 and Android first-parent changes in U149.
- **Additional decisions / behavior:** No new implementation delta; retain fork Vulkan and input adaptations plus documented U150 alias correction.
- **Verification:** Inspected both parents and empty remerge; second-parent delta is exactly the already-ported Android orientation change.
- **Pending / concerns:** UI include compile checkpoint pending.

### U154 — `bdbe9013cc` — Rename track motion functions and name globals

- **Source:** `bdbe9013ccbcfb36be7c3948f296da8dcd9a61c2`.
- **Fork receipt:** `b9e11e3a6a01a98699640b6d6e17a61b7eb89f94`.
- **Remaining:** 208 → 207.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 26 vehicle track-motion methods and global variables across 11 files, including fork-only callers.
- **Additional decisions / behavior:** Identifier changes only: preserve fork motion, acceleration, braking, station stopping, per-car/directed-leg calculations and rider-control expressions. Do not replace any implementation with upstream bodies.
- **Verification:** Complete actual source diff reviewed and all 11 source token streams verified as the 26 reviewed names only. All 11 fork deltas pass the same token proof after changed-line formatting; whitespace check passed.
- **Pending / concerns:** Batch 27 build for U150-U154 is running; targeted tests will follow before further C++ edits.

### U155 — `ac2515d483` — Make TileElement.h adhere to code style

- **Source:** `ac2515d483bb4e7f3d7e09cdce79a44653f1722e`.
- **Fork receipt:** `f420db6abd56fcaf88923b65611cf8eeb7ecf181`.
- **Remaining:** 207 → 206.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename eight TileElement members/helpers across 24 fork files, including Map.h insertion, copied-element banner cleanup, script replacement and test callers; retain derived-element accessors until their own source commits.
- **Additional decisions / behavior:** Naming only: 16-byte tile layout and padding widths/serialization unchanged. Preserve fork safe erase then copied-banner cleanup order, topology ownership and script replacement guards. Do not restore legacy raw pointer traversal.
- **Verification:** Complete 20-file source delta and all 24 fork body token streams verified against eight reviewed identifier mappings; scoped mixed accessors checked. Whitespace check passed. Batch 27 cleared U150-U154 with clean build and 147 tests.
- **Pending / concerns:** Tile-element naming compile/tests pending with following related commits; Batch 27 does not validate U155.

### U156 — `be98df4081` — Make BannerElement.h adhere to code style

- **Source:** `be98df4081f86c41453fb3e8f81f2b6b3cc1cc9a`.
- **Fork receipt:** `8ecda6556e9877e4cf394c9d0e69f37673884f2d`.
- **Remaining:** 206 → 205.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 11 BannerElement fields/methods in 25 fork files, including MapPathTopology and its direct banner fixtures.
- **Additional decisions / behavior:** Preserve the four-bit allowed-edge mask, ghost exclusion, banner identity, safe removal, and object lookup. Keep legacy RCT12 source accessors and global GetBanner unchanged; only modern destinations change.
- **Verification:** Complete 23-file source delta reviewed and source/fork token comparisons permit only the 11 reviewed names. Remaining uppercase allowed-edge uses confined to RCT12 and legacy importer source reads. Whitespace check passed.
- **Pending / concerns:** Compile and targeted tile/import/topology tests pending with following element renames.

### U157 — `4af2e9d1fd` — Make EntranceElement members adhere to code style

- **Source:** `4af2e9d1fdf10778792543314c2922f0553a23c1`.
- **Fork receipt:** `45743f848cfd898be0fef054ea30d36df02eaaec`.
- **Remaining:** 205 → 204.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 20 EntranceElement fields/accessors in 37 fork files, including topology edge construction and fixture insertion. Preserve separate path/track and legacy source accessors.
- **Additional decisions / behavior:** No layout, mask, sequence/station/ride association or movement changes. Preserve fork prepaid/platform boarding, topology and import destination ownership. Use receiver-specific changes where methods share names.
- **Verification:** Complete 35-file source token changes verified as reviewed names only; 37 fork file token comparisons passed. Inspected mixed importer destinations and scripting cases plus fork topology callers; whitespace check passed.
- **Pending / concerns:** Compile and targeted tile/import/topology tests pending with related element naming batch.

### U158 — `95025ba916` — Make LargeSceneryElement members adhere to code style

- **Source:** `95025ba9166d8f88589b9484342350e507a85ff8`.
- **Fork receipt:** `ecb04cea7677ed30cdc925a1c6a6f67f7797a971`.
- **Remaining:** 204 → 203.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 21 LargeSceneryElement fields/accessors across 26 fork files and remove the unused, undefined GetObject declaration.
- **Additional decisions / behavior:** Preserve colour storage, banner references, sequence numbers, accounted-bit semantics, clear-tool cost handling and fork clearance/erase guards. No new gameplay or object format behavior.
- **Verification:** Complete source token proof permits only the 21 reviewed identifiers plus exact obsolete declaration removal; 26 fork files pass the same body proof. Scoped importer and script branches preserved; whitespace check passed.
- **Pending / concerns:** Compile and tile/import/topology tests pending with related element naming batch.

### U159 — `2f9db0a5d4` — Make PathElement members adhere to code style

- **Source:** `2f9db0a5d48e9bc770c79ba3d647767e7efdb494`.
- **Fork receipt:** `7824f3ae20001f8b704255105360ba12987fa394`.
- **Remaining:** 203 → 202.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 61 PathElement fields/accessors in 49 fork files, including topology nodes, sampled path-adjacency ratings, test fixtures and the fork IsBin helper.
- **Additional decisions / behavior:** Preserve edge/corner masks, slope/queue/ride/station values, packed addition-status union and all fork routing/ratings/staff-bin behavior. Retain independent PathConstructFlag and legacy RCT12 names. Fork IsBin/HasFullBinSlot APIs remain their existing names.
- **Verification:** Complete 45-file source delta verified as the 61 reviewed identifiers only. All 49 fork file token streams pass identical-name-substitution proof after formatting. Additional direct topology/ratings/bin callers inspected; whitespace check passed.
- **Pending / concerns:** Batch 28 full build is running for U155-U159; tile/import/topology and gameplay regression tests follow before additional C++ edits.

### U160 — `d3c54532ca` — Make SmallSceneryElement members adhere to code style

- **Source:** `d3c54532cab94c7f69998f8fc54cfd98c025c17d`.
- **Fork receipt:** `0d6af1ab30e867cf690ba586750ce2cae230bfb4`.
- **Remaining:** 202 → 201.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 20 SmallSceneryElement members in 28 fork files. Carry three receiver corrections from Batch 28: retain WallElement.GetBanner and two RCT12 map-discovery getters.
- **Additional decisions / behavior:** Plant watering/age/withering, support masks, colour storage and legacy support-bit conversion remain identical. No ride-age or price changes. Earlier over-broad receiver renames were build errors, repaired without changing type contracts; detailed in Batch 28.
- **Verification:** Complete 28-file source delta verified as 20 identifier mappings only. All fork changes verified as those mappings plus three explicitly enumerated receiver repairs. Batch 28 clean rebuild and 227 tests cleared U155-U159; whitespace check passed.
- **Pending / concerns:** U160 compile/tests pending with surface/track/wall naming batch; prior Batch 28 does not validate these new small-scenery names.

### U161 — `25865dce31` — Make SurfaceElement members adhere to code style

- **Source:** `25865dce31965a58a010f386b4a632886bf86461`.
- **Fork receipt:** `eac6c509180a437f16dd650e05d6ea1207983221`.
- **Remaining:** 201 → 200.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 28 SurfaceElement members in 61 fork files, including fork scenery/pathfinding and regression callers. Reuse the existing surface-object getter in canGrassGrow.
- **Additional decisions / behavior:** The grass lookup simplification returns the identical loaded object through the same manager/index; preserve flags, randomness, growth timing and null behavior. Water-height units, ownership/fence masks, terrain slope, D03 cheat gates, sampled local-context ratings and packed tile layout unchanged.
- **Verification:** Complete 56-file source delta verified as reviewed names plus the exact three-line lookup simplification. All 61 fork files pass the same normalized token proof; fork-only callers inspected and whitespace check passed.
- **Pending / concerns:** Compile and regressions pending with remaining track/wall naming batch.

### U162 — `84f1946db8` — Make TrackElement members adhere to code style

- **Source:** `84f1946db82d9022ecd79d9edf878e0e2eb0710a`.
- **Fork receipt:** `50ab4378af4acb5b25b3a5fb53013f8fa862a887`.
- **Remaining:** 200 → 199.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 55 TrackElement fields/accessors plus the ride-type setter parameter across 100 fork files; include direct fixture callers while retaining Vehicle accessors.
- **Additional decisions / behavior:** Keep all union widths, photo/brake/maze storage, flag masks, station/seat rotation/door logic and fork per-car/directed-leg motion and ratings. Rename setter parameter to newRideType to avoid shadowing the renamed member. No gameplay or serialized-value changes.
- **Verification:** Complete 98-file source token streams verified against 56 reviewed identifier mappings. All 100 fork file token streams pass the same comparison after formatting; remaining uppercase track-type accessors reviewed as Vehicle/legacy types; whitespace check passed.
- **Pending / concerns:** Compile and regressions pending with following wall naming commit.

### U163 — `631804fd13` — Make WallElement members adhere to code style

- **Source:** `631804fd133aad292a75e1b5514558c1c975a50d`.
- **Fork receipt:** `060a4bf13790f2d6488316a62f71d0ad232cd26e`.
- **Remaining:** 199 → 198.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename 27 WallElement fields/accessors across 25 fork files, including the clear-tool fixture.
- **Additional decisions / behavior:** Preserve colour/banner storage, slope and animation bit packing, frame timing, door motion and audio dispatch. Keep U139 separate sound validation and all fork clearance/visibility rules.
- **Verification:** Complete 24-file source token streams and 25 fork file token streams verified as the 27 reviewed identifier substitutions only; whitespace check passed.
- **Pending / concerns:** Batch 29 build for U160-U163 running; gameplay/import/topology/audio tests will follow before further C++ changes.

### U164 — `e501376aa3` — Merge pull request #26914 from Gymnasiast/refactor/tile-element-code-style

- **Source:** `e501376aa3eab0b8c2ec1a6644aea2042e5d5372`.
- **Fork receipt:** `18b9091c7445d7af48c4dda8440366732e7d7d06`.
- **Remaining:** 198 → 197.
- **Disposition:** adopt applicable changes.
- **Manual changes:** History receipt for the tile-element naming merge; source tree equals its second parent and the previously inspected remerge diff is empty. Carry two B29 repair calls in fork Map.cpp terrain snapshots, GetSurfaceObject/GetParkFences to getSurfaceObject/getParkFences, and checkpoint batch validation.
- **Additional decisions / behavior:** No new source behavior or design decision. Preserve fork snapshot object-resolution ownership and all approved gameplay models; repair two missed U161 call-site spellings without aliases.
- **Verification:** Source parents reviewed, tree equality verified. B29 full Release x64 MSVC/Vulkan rebuild passed with 0 warnings/errors in 17.15 seconds; all 254 tests in 17 suites passed in 32.768 seconds. Initial four diagnostics and repairs are recorded in validation Markdown.
- **Pending / concerns:** Interactive rendering/editing and native non-Windows checks remain unverified; other standing validation debt is retained.

### U165 — `a80faffd5c` — Remove openrct2-ui/ includes

- **Source:** `a80faffd5ce231205c7f1957af9ecc1be4cd00a7`.
- **Fork receipt:** `0ebb380961b4dc012313144789e62e752046e192`.
- **Remaining:** 197 → 196.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Port all ten UI header-dependency cleanups: narrow SDL mouse/error/clipboard/events includes, forward-declare CursorID, remove unused includes/declarations and use direct core Viewport and Audio dependencies.
- **Additional decisions / behavior:** No behavior change. Fork Vulkan platform include and all UI/input/render ownership bodies are retained. UiScriptExtensions only takes a ScriptEngine reference, so the removed full engine header is not required there; provisional path functions and input dispatch retain their actual declaring headers.
- **Verification:** Reviewed all ten source patches and fork usages; normalized comparison verifies all ten fork deltas contain only includes, forward declarations and whitespace. Changed C++ lines formatted; whitespace and singleton ancestry checked by receipt helper.
- **Pending / concerns:** Compile and focused regression tests at the next UI-header checkpoint; interactive and non-Windows UI checks remain outstanding.

### U166 — `2c8891ca8b` — Remove openrct2-ui/ride includes

- **Source:** `2c8891ca8b2513ec9567d54619ffbb1f362ea44a`.
- **Fork receipt:** `92489d5a36efc4e3103e0ad23712d12f80b7e596`.
- **Remaining:** 196 → 195.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Port five ride UI/type header cleanups: explicit core Viewport/Track headers, construction forward declarations and direct ride-use identifier dependency; remove unused RideTypes dependencies.
- **Additional decisions / behavior:** Retain direct AudioMixer.h in fork VehicleSounds.cpp because the spatial playback code uses MixerGroup::vehicle and kMixerVolumeMax, even though Audio.h currently also exposes it. Add direct cstddef/cstdint to Construction.h for its size_t and enum underlying types. Fork spatial gains, Doppler, source selection, routing and construction behavior are unchanged.
- **Verification:** Reviewed all five source paths and fork definitions/usages. Normalized comparison proves five fork paths have only include/forward-declaration changes. Whitespace and exact-one ancestry gate apply.
- **Pending / concerns:** Compile and focused UI/audio regressions at the next header batch checkpoint; native non-Windows and interactive validation remain pending.

### U167 — `e75c1dde15` — Remove openrct2-ui/scripting includes

- **Source:** `e75c1dde1519770617b8fb49821cdb7911041c72`.
- **Fork receipt:** `8999239cc242d36211662f5a939dec8a02bfaf3a`.
- **Remaining:** 195 → 194.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Port eight scripting UI header cleanups, direct CustomListView implementation dependencies and forward-declared ScriptEngine/window/render types. ScWindow declares the existing WindowNumber int16_t alias locally.
- **Additional decisions / behavior:** No behavior or public API change. Script/plugin ownership, custom images, widgets and menu dispatch are unchanged; the WindowNumber alias matches Window.h and WindowTypes.h.
- **Verification:** Reviewed all source hunks; all eight pre-port fork files matched the source parent. Include/declaration-only proof passes after explicitly allowing the existing WindowNumber alias and namespace closing comments. Formatted changed lines and apply ancestry/whitespace receipt gates.
- **Pending / concerns:** Compile and scripting/widget/image regressions at the next UI-header batch checkpoint; interactive and non-Windows checks remain pending.

### U168 — `8a0507708f` — Remove openrct2-ui/title includes

- **Source:** `8a0507708f40f4856dd48e0c8ef6f3a975871a77`.
- **Fork receipt:** `035aacb55efb15c190d66d6383fbae02be2926eb`.
- **Remaining:** 194 → 193.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Clean up title-sequence includes in two files, use direct WindowBase.h and remove the unused IScenarioRepository forward declaration.
- **Additional decisions / behavior:** No change to title playback, scenario loading, simulation or rendering policy.
- **Verification:** Reviewed both complete source patches. Both fork files matched the source parent before editing; normalized include/declaration-only proof passes, changed lines formatted and receipt whitespace/ancestry gates apply.
- **Pending / concerns:** Build and focused regressions at the upcoming UI-header checkpoint; interactive title playback and native non-Windows remain unverified.

### U169 — `b95d24bb68` — Remove openrct2-ui/windows includes

- **Source:** `b95d24bb68218ca9a597063420e4b580592bebc9`.
- **Fork receipt:** `12ba72078c2fec4767cda2dc259ddf678b4b7cc2`.
- **Remaining:** 193 → 192.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Port window include cleanup across 72 changed fork paths, remove the redundant UI interface/Viewport.h forwarding header and its project/PCH references, use core viewport and direct rendering/localisation headers, narrow SDL keyboard dependency. Source Construction.h cstddef/cstdint and namespace/whitespace fixes were already covered in U166-U168.
- **Additional decisions / behavior:** No window behavior, geometry or gameplay change. Retain fork Vulkan/HDR controls, automatic admission pricing, ratings display, safe-erasure traversal and toolbar network visibility policy. The deleted viewport file contained only a forwarding include; no fork declarations or implementation were removed.
- **Verification:** Read all 76 source-file patches and inspected divergent fork window bodies/dependencies. Proof covers all 72 fork deltas as include/format-only, exact project-item removal and no consumers of the deleted forwarding header. Formatted changed C++ lines; receipt whitespace and singleton ancestry gates apply.
- **Pending / concerns:** Compile and focused UI/scripting/audio/gameplay checks in B30 immediately after this source receipt; interactive windows and native non-Windows remain unverified.

### U170 — `34e8149e93` — Merge pull request #26915 from Harry-Hopkinson/remove-final-openrct2-ui-includes

- **Source:** `34e8149e936a785acf9cbe3cdbf6934d222b9ac9`.
- **Fork receipt:** `548433401d8b41bd8f391b52c075a8878a5673f5`.
- **Remaining:** 192 → 191.
- **Disposition:** adopt applicable changes.
- **Manual changes:** History receipt for the final UI-includes merge; checkpoint B30 validation. The cached actual remerge diff is empty; first-parent delta consists of the U165-U169 header work and the other parent carries tile-element names already ported in U155-U163.
- **Additional decisions / behavior:** No new behavior or unresolved merge decision. Preserve the manually ported fork tree and existing gameplay/render ownership.
- **Verification:** Reviewed source parents, empty remerge probe and first/second-parent delta scopes. B30 full Release x64 MSVC/Vulkan build passed first attempt with 0 warnings/errors in 86.73 seconds; 172 selected tests passed in 17.523 seconds plus one image import test in 0.003 seconds. Corrected a nonmatching image test filter with a separate run.
- **Pending / concerns:** Interactive windows, title/audio operation and native non-Windows remain unverified; standing migration validation debt retained.

### U171 — `bc68828c7d` — Improve save field alignment in file browser window (#26916)

- **Source:** `bc68828c7dd07ac51eff12e133156a2f31c43024`.
- **Fork receipt:** `b56d43dbd3ef6cf8cd568aa8f786c832c82b0eea`.
- **Remaining:** 191 → 190.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Align save textbox and Save button using the standard button-face height, move the filename label accordingly, and adjust marketing button spacing; kButtonFaceHeight advances 12 to 14.
- **Additional decisions / behavior:** Adopt all source geometry changes. Additional reviewed effects: save controls have a two-pixel larger top-to-bottom span and the scroll area reserves 21 instead of 18 pixels; marketing button span/stride each grow one pixel. The shared constant moves the preview park-name text up two pixels. In the fork five-button ride graph row, graph/button tops move up two pixels and button bottoms stay fixed; preserve the longitudinal-G button, equal widths and graph data. No pricing, marketing cost/eligibility, save format or gameplay changes.
- **Verification:** Inspected all three source patches and every repository use of kButtonFaceHeight, including the fork graph layout. Changes match actual source deltas; coordinate arithmetic and affected consumers reviewed. Receipt whitespace and singleton ancestry gates apply.
- **Pending / concerns:** Build/widget regression at the next checkpoint; interactive file browser, marketing and five-button graph layout remain unverified.

### U172 — `cef7dd164b` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `cef7dd164ba8394ae0b7b43f961ded962e06ddb8`.
- **Fork receipt:** `bfd04b63296b4f2602d0a4959bbaea600b9eb373`.
- **Remaining:** 190 → 189.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Shorten Dutch STR_6678 from Bronbestand: to Bron: (source file to source), matching the single actual translation delta.
- **Additional decisions / behavior:** Text-only label correction. Preserve all fork custom IDs and other translations; no gameplay or object dependency change.
- **Verification:** Inspected complete one-line source diff and matching fork string. No format placeholders or string IDs changed; receipt whitespace and ancestry gates apply.
- **Pending / concerns:** Covered by normal language compilation at the next build; interactive Dutch label remains unverified.

### U173 — `d943171fae` — Create enum class for EntranceType

- **Source:** `d943171faefddb192ac3aa143270f449f447d2db`.
- **Fork receipt:** `3263b9bae218356612b7e000109804864e8092c3`.
- **Remaining:** 189 → 188.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Adopt uint8 EntranceType values 0/1/2 across modern and packed legacy elements, typed construction/ghost helpers, named callers and equivalent three-by-eight direction table. Adapt fork path-topology and frozen-route fields and test fixtures. Keep numeric scripting reads; clamp script object writes. Add a real-plugin bounds/topology test and B31 validation.
- **Additional decisions / behavior:** Adopt malformed-input clamp after existing uint32 conversion: above 2 becomes parkEntrance instead of byte truncation/wrap; -1 becomes 2, 2^32 becomes 0. Retain fork Invalidate(data, true) and all ghost/routing/payment rules. Independent network revision 7 to 8 prevents mixed simulation behavior; API118/save60016 unchanged. Valid UI toggle remains entrance/exit; invalid internal tool states now select entrance. No reinterpretation or repair of malformed imported bytes. Explicit array include and typed uint8 cache fields preserve dependencies/layout.
- **Verification:** Read all 34 source patches; 24 ordinary fork files pass enum-name/type-only proof and 16 structural/protocol/test files separately reviewed. Resolved a partially applied tool operation through exact remaining-delta inspection before build. B31 first-attempt full Release x64 MSVC/Vulkan build passed 0 warnings/errors in 86.62 seconds; 240 tests across 16 suites passed in 32.441 seconds, including the new 10-input public scripting/native value/direction/topology test.
- **Pending / concerns:** Interactive entrance tools and U171 layouts, multiplayer/replay synchronization and native non-Windows remain unverified. Full standing migration validation debt retained.

### U174 — `7b1b014fce` — Create enum class for EntranceSequence

- **Source:** `7b1b014fce773472ce2eef594bd8df7ef3ba9979`.
- **Fork receipt:** `c0636090860f48528ec53c6114e7bab94369e103`.
- **Remaining:** 188 → 187.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Adopt uint8 ParkEntranceSequence centre/left/right values 0/1/2 across element accessors, object image selection, importers, placement, removal and rendering. Preserve low-nibble storage and replace the placement loop with the same ordered three values. Adapt fork topology bounds and both test fixtures; expand the real-plugin test to 15 sequence inputs.
- **Additional decisions / behavior:** Adopt upstream sequence clamp exactly: after existing uint32 JS conversion, narrow to uint8 then clamp 0..2. Thus 3/16/255 become right(2), 256 remains centre(0), 257 remains left(1), and -1 becomes right. This differs from U173 object clamping before byte truncation and is explicitly retained for traceability. Preserve Invalidate(data, true), low-nibble importer preservation, existing >=8 topology rejection, ghost rules and action costs. Network revision 8 to 9 for changed script mutation; API118/save60016 unchanged. No normal placement or routing rule change.
- **Verification:** Read all 20 source patches and fork overlaps. Applied repeated identical footpath conditions explicitly twice per owner; restored both explicit default no-op switch branches skipped by the generic insertion helper. Reviewed all enum widths, masks, ordered placement coordinates and public numeric conversions; changed lines formatted and receipt gates apply. Expanded plugin regression checks numeric/native sequence, directions and topology.
- **Pending / concerns:** B32 compile and full targeted gameplay/import/topology/scripting regressions are pending across the following tile-flag batch. Interactive entrance placement and multiplayer/replay synchronization remain unverified.

### U175 — `4ebed48631` — Create enum class+FlagHolder for EntranceElementFlag

- **Source:** `4ebed48631c32af6ea32387bb5a8e10d38b56eb5`.
- **Fork receipt:** `7aa3675ceed20ed7532618aea860f7f2fbca1c17`.
- **Remaining:** 187 → 186.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Replace the private entrance legacy-path flag byte with FlagHolder<uint8_t, EntranceElementFlag>, retaining bit 0 and the existing path-index writes; replace three bit operations with has/set/unset.
- **Additional decisions / behavior:** Same one-byte storage, offset, packed element size, unknown-bit preservation and legacy/surface path choice. No gameplay, import translation or protocol change.
- **Verification:** Reviewed complete two-file source and fork diffs and FlagHolder storage/bit operations. Only bit 0 is defined and affected; all other bits remain untouched. Receipt whitespace and exact-one ancestry gates apply.
- **Pending / concerns:** Compile and import/topology/scripting regressions in B32 tile-flag batch; interactive/non-Windows debt retained.

### U176 — `b854b8d2f6` — Create enum class+FlagHolder for LargeSceneryElementFlag

- **Source:** `b854b8d2f612468239dfaa052ef19e7ad2a8012d`.
- **Fork receipt:** `c4ef1030819f49d399157b30dd6993d5d35a88ed`.
- **Remaining:** 186 → 185.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Type the private large-scenery accounted byte as FlagHolder<uint8_t, LargeSceneryElementFlag>; replace bit-0 test and conditional set/clear with has and set(flag,bool).
- **Additional decisions / behavior:** Preserve bit 0, unknown bits, packed layout and the accounted marker lifecycle. Whole-piece accounting, clear-tool costs and fork traversal/erasure rules do not change.
- **Verification:** Reviewed both actual source patches, fork deltas and the previously checked FlagHolder bool setter and bit masks. No caller or accounting algorithm is replaced; receipt whitespace/ancestry gates apply.
- **Pending / concerns:** B32 build and clearance/import/gameplay regressions pending; interactive and non-Windows debt retained.

### U177 — `1bf29b9afe` — Create enum class+FlagHolder for FootpathElementFlag

- **Source:** `1bf29b9afebe3c2cd7d7ddc11510ac30c5f2cbb0`.
- **Fork receipt:** `3b2263c3ede1b91d555ae941f6df5a81ec231fd9`.
- **Remaining:** 185 → 184.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Type the private path flags byte as FootpathElementFlags and convert tests/setters for sloped, queue banner, ghost addition, vehicle blockage, broken addition, legacy path and junction railings.
- **Additional decisions / behavior:** Preserve all seven bit positions 0..6, unused bit 7, one-byte storage, legacy surface/railings null handling and every setter result. Fork IsBin/HasFullBinSlot declarations and implementations remain. No path connection, fare routing, blockage, staff work, ghost or clearance rule change.
- **Verification:** Reviewed both full source diffs and all raw-flag consumers. Post-port PathElement.cpp matches this source commit; header differs only by the two existing fork bin-helper declarations. FlagHolder operations retain the original masks; ancestry and whitespace gates apply.
- **Pending / concerns:** B32 build and path/topology/clearance/import/gameplay regressions pending; interactive and non-Windows checks retained.

### U178 — `4d7866e3ad` — Create enum class+FlagHolder for SmallSceneryElementFlag

- **Source:** `4d7866e3ad4428270840a757022dc46b6ab18c2e`.
- **Fork receipt:** `d23c36a9287aad2265f76daf45905476b5b3c60b`.
- **Remaining:** 184 → 183.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Replace the private small-scenery support byte with SmallSceneryElementFlags and bit-0 has/set operations.
- **Additional decisions / behavior:** Preserve one-byte storage, bit 0, unknown bits, packed offsets, support rendering and all plant watering/age/withering behavior. Source void-return expression simply returns the void setter result; no value or control-flow change.
- **Verification:** Reviewed both source patches and the resulting fork diff. Correction recorded with U179: the U178 receipt initially overstated exact equality with upstream. The header matches, but the CPP correctly retains the existing fork MapInvalidateTileFull(sceneryPos) in watering/withering instead of upstream MapInvalidateTileZoom1. The flag delta itself matches source, and that fork rendering behavior was not changed. Existing layout assertions and B32 build/import checks cover storage; receipt whitespace/ancestry gates apply.
- **Pending / concerns:** B32 compile and targeted regressions pending; interactive support/plant rendering and native non-Windows remain unverified.

### U179 — `5af73fa31b` — Create enum class+FlagHolder for TrackTileElementFlag

- **Source:** `5af73fa31b36391daf3fca281fd978481673b684`.
- **Fork receipt:** `a7ced1c200cb55e206c22dc97009e55ff9340507`.
- **Remaining:** 183 → 182.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Replace the private track flags byte with TrackTileElementFlags and has/set operations for seven bits: chain, inverted, cable lift, highlight, green light, brake closed and indestructible. Correct U178 verification wording to acknowledge its retained fork watering redraw.
- **Additional decisions / behavior:** All seven bit positions 0..6, unknown bit 7, byte storage, packed maze/ride union and setter outcomes remain identical. The makeAllDestructible cheat remains in isIndestructible at this source step. No train physics, brakes, station, ratings or cheat behavior changes. U178 source code needed no repair; its existing MapInvalidateTileFull behavior was preserved, despite the original receipt overstating equality with upstream.
- **Verification:** Read both complete source patches and all flag consumers. Resulting TrackElement header/CPP match this source commit exactly; no old track flag names remain. FlagHolder bit semantics reviewed; receipt whitespace and singleton ancestry gates apply.
- **Pending / concerns:** B32 full build and gameplay/import/topology/scripting tests immediately after this receipt; interactive and non-Windows debt retained.

### U180 — `1eacb1eec8` — Fix formatting for MazeConstruction.cpp

- **Source:** `1eacb1eec8a512885e4e84d546fe2980fd6b5b58`.
- **Fork receipt:** `4b903899e27871ebda783c38f02be486158025aa`.
- **Remaining:** 182 → 181.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Reformat the maze entrance-placement callback exactly as upstream and checkpoint B32 validation.
- **Additional decisions / behavior:** Whitespace-only change: callback ordering, errors, audio, tool switching, ride completion and maze behavior remain identical.
- **Verification:** Reviewed the full source patch and matched unique fork context. Whole-file normalization confirms only whitespace differences; B32 preceding U174-U179 build passed first attempt 0 warnings/errors in 74.02 seconds and all 240 tests in 16 suites passed in 32.170 seconds. A line-oriented git -w comparison still reports wrapped lines, so whole-file normalization supplies the relevant proof.
- **Pending / concerns:** No runtime change in this receipt. Interactive/non-Windows and multiplayer/replay debt remains as recorded; U174-U179 compile/test debt cleared.

### U181 — `df06376780` — Move indestructible cheat check to caller

- **Source:** `df06376780f443c3038edbd14438cfcf66e48daf`.
- **Fork receipt:** `a67e1db0cfd917d653f5de7081cb331312e54fa7`.
- **Remaining:** 181 → 180.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Make TrackElement.isIndestructible report its stored flag, move the makeAllDestructible check to TrackRemoveAction.Query, remove unused GameState include and add the changelog entry. Add a real placed-track regression and B33 validation.
- **Additional decisions / behavior:** Adopt corrected Tile Inspector checkbox/toggle semantics while the cheat is active: protected track remains visibly marked and the raw flag can be cleared/restored. All actual getter consumers were checked; removal still rejects exactly stored=true and cheat=false. No fork-only permission gate is lost, and no pricing/physics/routing/save-format change. Network stays9 because action acceptance and execution for an identical payload are unchanged.
- **Verification:** Reviewed all three source diffs and every isIndestructible consumer. Release x64 MSVC/Vulkan build passed 0 warnings/errors, 14.37 seconds and final fixture rebuild 7.03 seconds. Initial run passed 146/147 in 18.430 seconds; repaired new test passed in 0.357 seconds after using immediate ExecuteNested instead of the queuing UI dispatcher. It checks four query/flag combinations, query non-mutation, raw toggles and actual removal/cost.
- **Pending / concerns:** Interactive Tile Inspector presentation and native non-Windows remain unverified. B33 compile and focused regression debt is cleared; standing migration debt retained.

### U182 — `3937aaa967` — Merge pull request #26917 from Gymnasiast/refactor/more-enums

- **Source:** `3937aaa967de8c61dccca1ee0b1f999c43d35230`.
- **Fork receipt:** `73751da44addb22c52b958688b42d3480ceb0077`.
- **Remaining:** 180 → 179.
- **Disposition:** history receipt.
- **Manual changes:** History receipt for the entrance/private-flag/indestructibility merge. No remerge-resolution delta; second-parent delta consists only of the U171 save/marketing/button-height changes and U172 Dutch string already ported.
- **Additional decisions / behavior:** No new behavior or decision. Preserve all manually adapted fork implementations and source-by-source dispositions.
- **Verification:** Reviewed merge parents, empty actual cached remerge diff and the four second-parent delta files. U173-U181 ports and B31-B33 checks cover both constituent branches. Singleton ancestry gate accounts for exactly one source commit.
- **Pending / concerns:** Existing interactive, non-Windows and multiplayer/replay debt remains; no new integration debt.

### U183 — `95b54aa4f3` — Fix bottom toolbar button invalidation (#26921)

- **Source:** `95b54aa4f3ad66c2d1e906e64aea5af695e09244`.
- **Fork receipt:** `7393218a28bf01df37f7fae2ea165cc80b02e9f4`.
- **Remaining:** 179 → 178.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Include hiddenButton widgets in hover invalidation alongside flatBtn widgets, and add the corresponding bottom-panel changelog entry.
- **Additional decisions / behavior:** Adopt redraw correction. hiddenButton is a widget type, distinct from the visibility flag; the change requests redraw when hover changes and does not expose hidden controls, change click permissions or alter simulation. Retain fork drawing publication and prior toolbar visibility policies.
- **Verification:** Reviewed the complete source and fork diffs; the mouse-input body delta exactly matches source. Existing lookup/preparation/invalidation ownership is unchanged. Receipt whitespace and exact-one ancestry gates apply.
- **Pending / concerns:** B34 build and widget/network regressions pending with the next permission fix; interactive bottom-panel hover redraw remains unverified.

### U184 — `27a9d4b3d5` — Fix #26903: no permission for making a ride visible or invisible (#26911)

- **Source:** `27a9d4b3d5e8488601e9d78daea56b64a3070221`.
- **Fork receipt:** `cb64f95e31ba14103e784a4bee1fb0433742c11b`.
- **Remaining:** 178 → 177.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Map setRideVisibility into the existing multiplayer cheat permission, add the changelog entry, advance independent fork network revision 9 to10 and add permission grant/revoke regression coverage.
- **Additional decisions / behavior:** Adopt upstream authorization fix: existing cheat-authorized groups can now show/hide rides; ordinary rideProperties permission alone remains insufficient. No new permission bit, group schema, single-player behavior or fork UI move. Preserve andersonhk stream flavor and apply +1 rather than copying upstream revision4; resulting stream0.5.4-andersonhk-10, API118/save60016.
- **Verification:** Reviewed all three source patches and actual NetworkActions/NetworkGroup lookup gates. New test verifies deny/default/ride-properties-only, grant cheat, existing cheat/date/freeze-rating permissions and revoke. B34 first-attempt Release x64 MSVC/Vulkan build passed 0 warnings/errors in14.77 seconds; all16 network/scripting/widget tests passed in0.836 seconds. U183-U184 compile/test debt cleared.
- **Pending / concerns:** Live multiplayer visibility action, interactive hover rendering and native non-Windows remain unverified; standing migration validation debt retained.

### U185 — `11547e998a` — Make enum class+FlagHolder for ownership flags and normalise them

- **Source:** `11547e998a4f0e6c3f2b0c05018b53ab6a024e41`.
- **Fork receipt:** `36efe1015daf62f520064d03681264973d2fb7b0`.
- **Remaining:** 177 → 176.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Apply typed normalized ownership across all 26 source paths, register the new header, document public plugin/action values, and add packed-byte, 256-transition action and real-plugin regression coverage.
- **Additional decisions / behavior:** Owner approved API 119 normalized external flags 1/2/4/8 with no legacy adapter and matching action wire format. Advance fork protocol 10 to 11, retain andersonhk and save 60016. Preserve !currentOwned && desiredOwned, excluding the upstream negated-conjunction pricing regression. Existing combined-right cost precedence, fork mutation/topology/render behavior and objects pin remain. See docs/upstream-migration-decision-u185.md for resolved decision and evidence.
- **Verification:** B35 first Release x64 MSVC/Vulkan build: 0 warnings/errors, 87.20s. All 245 selected tests in 16 suites pass, 33.819s, including normalized parameter/wire byte, query/execute costs for all 256 masks, packed ownership/fence preservation and API119 script getter/setter boolean behavior. Full source/diff review and removed-constant search completed.
- **Pending / concerns:** Compile/test debt cleared. Live multiplayer/replay, interactive ownership overlays, non-Windows and existing standing validation remain unverified.

### U186 — `db53fd3f7f` — Introduce SurfaceElement::hasOwnership()

- **Source:** `db53fd3f7f1aa07549db5f509973fc57a2c4a92f`.
- **Fork receipt:** `5000db0b9b332b9eb22a60511981412a362e055d`.
- **Remaining:** 176 → 175.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add SurfaceElement::hasOwnership(flag) forwarding to getOwnership().has(flag), and update the 11 reviewed source paths.
- **Additional decisions / behavior:** Forwarding-only refactor preserves ownership predicates, prices, normalized API119 numbers, packed saves, protocol11 and fork behavior. No additional owner decision.
- **Verification:** All actual source hunks reviewed; audit_u186.py verifies all 11 files differ only by the forwarding helper and equivalent caller spelling. Whitespace check passes. B35 provides the preceding ownership baseline.
- **Pending / concerns:** Compile and affected ownership/plugin checks deferred to the next coherent batch; standing interactive/network/non-Windows validation remains.

### U187 — `d2783e0564` — Rename some ‘OwnershipFlag’s

- **Source:** `d2783e05642247971350f8b8c3fc6383be7c46c1`.
- **Fork receipt:** `3ffd12141b33079ea80e494c1451b4b4fe37156b`.
- **Remaining:** 175 → 174.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Rename OwnershipFlag owned/forSale/constructionRightsAvailable to landOwned/landForSale/constructionRightsForSale in all 15 reviewed source files.
- **Additional decisions / behavior:** Names only; retain numeric bit positions, API119, protocol11, packed data and corrected U185 land-purchase predicate. Carry the renamed token into the nearby land-sales comment as well. No gameplay change.
- **Verification:** Actual source reviewed; port_u187.py --audit verifies all 15 changed files are exactly the scoped renames modulo whitespace. Removed-name search has no matches; diff whitespace check passes.
- **Pending / concerns:** U186-U187 compile and ownership/plugin checks due at the next batch; standing interactive/network/non-Windows validation remains.

### U188 — `26c10680d5` — Move two map ownership functions out of Map.h

- **Source:** `26c10680d59ad031eb9ca444b055850863f2f3bf`.
- **Fork receipt:** `e24adc33e799f7bb4288ecae214147c72a9cde07`.
- **Remaining:** 174 → 173.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Move eligibility to MapOwnership.cpp with TileElementsView traversal, localize the scenario ownership/fence helper with const span, relocate declarations and register the new compilation unit. Add explicit span include.
- **Additional decisions / behavior:** Preserve null-tile fallback, element order and ghost inclusion in eligibility; moved loop has no mutation. Scenario helper retains ownership and fence updates plus surrounding fork topology invalidation. No gameplay/API/wire/save changes; corrected U185 pricing remains.
- **Verification:** audit_u188.py verifies unchanged eligibility-body statements and scenario ownership/fence loop, exact current-source new file and unique MSBuild registration. CMake source glob covers new cpp. B36 first Release/Vulkan build 0 warnings/errors 74.83s; all 127 selected tests in 11 suites pass 21.987s. U186-U188 compile/test debt cleared.
- **Pending / concerns:** Standing interactive ownership/scenario UI, live multiplayer/replay and non-Windows validation remain unverified.

### U189 — `11da390704` — Merge pull request #26920 from Gymnasiast/refactor/ownership-flags

- **Source:** `11da390704950cd30f1275c392140480908183a0`.
- **Fork receipt:** `97b52a3a06bfe23dc007980671e0b1ae0e896f1e`.
- **Remaining:** 173 → 172.
- **Disposition:** history receipt.
- **Manual changes:** Record the ownership-refactor merge after individually porting U185-U188; no additional source delta.
- **Additional decisions / behavior:** Merge tree equals its second parent, so retain the manually adapted fork tree, approved API119/protocol11 boundary and U185 land-pricing correction. Do not reapply upstream merge tree.
- **Verification:** Actual merge and second-parent tree IDs both 52cae0e77e32b115d12c497953b629b344a725b5; second-parent diff empty; cached remerge patch zero bytes; only this source is newly reachable. B35 and B36 validate constituent changes.
- **Pending / concerns:** No new compile/test debt; standing validation limitations remain.

### U190 — `3d90510a63` — Shrink and reposition top toolbar, allowing rain to be drawn (#26936)

- **Source:** `3d90510a63d47510c6a91028616e7fb1186f1b56`.
- **Fork receipt:** `0b81367e16aee13cf6c3b63ab0bae7892de86a22`.
- **Remaining:** 172 → 171.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Shrink and reposition the centred top-toolbar window and use local button positions, allowing weather drawing beside it; restore full width in left/right mode. Include the source changelog entry.
- **Additional decisions / behavior:** Approved D07 HUD adoption. Check both x position and width before updating/invalidation: WindowResizeGuiScenarioEditor resets toolbar width, and a one-pixel resize can retain the same integer centre x. Width-only changes must repair the bounds too. Preserve all fork buttons, visibility/permissions, turbo controls and render publication; no gameplay/weather simulation change.
- **Verification:** Actual two-file patch and surrounding alignment/drawing/dropdown consumers reviewed; sprites and menus already add windowPos to local widget positions. Checked GUI resize width assignment and weather-window traversal. Scoped diff/whitespace checks pass.
- **Pending / concerns:** Compile/widget checks due at next UI batch. Centred/left-right toggling, one-pixel resize and rain rendering need interactive verification; standing validation remains.

### U191 — `182efeea9a` — Fix title windows not flashing when already open (#26935)

- **Source:** `182efeea9ae9c0f7e5f9ee28a20a2432fa0907d4`.
- **Fork receipt:** `7b719dc96c4e3d9e93ffd39475399ab8d4a852aa`.
- **Remaining:** 171 → 170.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Use BringToFrontByClass for already-open scenario, load/save and multiplayer windows on the title menu; add changelog entry.
- **Additional decisions / behavior:** Adopt edge-flash attention behavior while retaining existing close/open branches, pause/focus policy and single existing window. No gameplay change.
- **Verification:** Both actual source files reviewed, including window-manager BringToFrontByClass behavior and unchanged missing-window branches. Scoped diff and whitespace checks pass.
- **Pending / concerns:** U190-U191 compile/widget checks due at next batch; title-window flashing and toolbar/rain need interactive validation; standing limitations remain.

### U192 — `8ebb607fc4` — Fix #21632: Crash when loading custom image larger than 300 by 300 pixels (#26934)

- **Source:** `8ebb607fc4692deefdf5108538990c49a5cc06b6`.
- **Fork receipt:** `611589b1fb94a1e9bf62b2c2a3de88b127db4c38`.
- **Remaining:** 170 → 169.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Catch standard image-upload exceptions and replace PNG row-size assertions with runtime errors. Release JS pixel-buffer references on failure and allocate replacement pixels before freeing the old image. Add four small PNG fixtures, portable decoder/import tests and an actual UI-binding plugin recovery/leak regression test.
- **Additional decisions / behavior:** Keep the 300x300 limit and existing valid conversion behavior. Recoverable errors require cleanup of the acquired JS reference and preserving old image data on replacement allocation failure. Enable UI-binding tests only in the MSBuild configuration already linking the UI library; no extra UI dependency for CMake/core-only tests. No gameplay, API119, protocol11, save60016 or renderer-policy change.
- **Verification:** B37 initial and final Release/Vulkan builds pass, 0 warnings/errors, 22.53s and 10.42s. All 23 selected tests in five suites pass, 1.448s. Real plugin catches oversized/bad PNGs, retains previous image, has stable live-object count after 32 repeated errors and successfully uploads again; portable PNG/import errors and unchanged logo hash pass. U190-U192 compile/test debt cleared.
- **Pending / concerns:** Allocation failure reviewed but not injected. UI-binding test not enabled for CMake/core-only builds. Toolbar/rain/title attention interactive checks, live multiplayer/replay and standing non-Windows validation remain.

### U193 — `af352f18ef` — Create functions to get a random colour (#26939)

- **Source:** `af352f18ef113f77af6f6f8880168c15215804a3`.
- **Fork receipt:** `599c6e0cf12dca6c16a313acf328469f609b0168`.
- **Remaining:** 169 → 168.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Extract utility-random, scenario-random and tick-cycling colour helpers and replace the six reviewed source paths.
- **Additional decisions / behavior:** Keep deterministic ScenarioRandMax for guest merchandise/cheat colours, UtilRand for UI and intentional console desync, and the existing 32-tick shop preview cycle. At fork 40 TPS this remains 0.8s per colour; do not silently retime it. Same RNG draws, limits and invocation order; no gameplay or protocol change.
- **Verification:** All source hunks inspected. audit_u193.py expands each helper and proves all four caller files retain previous expressions modulo whitespace; Colour.h/cpp exactly match this reviewed source. Diff check passes.
- **Pending / concerns:** Compile/gameplay checks due at next batch; standing interactive/network/non-Windows validation remains.

### U194 — `8ebe3965a2` — Re-include unistd.h in Platform.Linux.cpp (#26940)

- **Source:** `8ebe3965a2f1e2540f8882a1d67e7b66f5470195`.
- **Fork receipt:** `24934aae0dd7a638ce733958b8a00c171507c491`.
- **Remaining:** 168 → 167.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Restore the direct unistd.h include in the guarded Unix/Haiku platform implementation.
- **Additional decisions / behavior:** Header-only platform repair; no change to fork build targets, locale paths or gameplay.
- **Verification:** Actual one-line source diff reviewed and applied exactly; diff/whitespace checks pass. Platform implementation remains excluded on Windows, Android, Apple and Emscripten by its existing guard.
- **Pending / concerns:** Native Unix/Haiku compilation remains unverified on this Windows host; U193 compile/gameplay batch still pending.

### U195 — `8dab2cce07` — Split off ParkInfoPanel and DateInfoPanel from GameBottomToolbar (#26919)

- **Source:** `8dab2cce07684fd6a3cbd8670bef546d0a86c4de`.
- **Fork receipt:** `86deabab870645ece1761848051a0578ec1a52d8`.
- **Remaining:** 167 → 166.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Manually split park and date/weather panels from news toolbar; update window classes, intents, shortcuts, themes, strings, project entries and resize ownership. Add actual UI-context HUD regression.
- **Additional decisions / behavior:** D07 adopted. Preserve cent-money and 30-second fork forecast, all 42 strings and spatial audio. Correct initial date right-edge gap, null dereference with absent panels, and title-resize early return that skipped centering/redraw. Adopt editor centering. Custom theme conversion awaits its later historical commit be908.
- **Verification:** B38 first build 87.83s and fixture rebuild 6.56s, zero warnings/errors. Initial 76/77 tests exposed duplicate progress-window fixture; repaired identity/setup, final 77/77 in 7.724s. Source audit confirms panel equivalence with documented adaptations, Game.cpp opens only and unchanged fork spatial audio. Diff check passed.
- **Pending / concerns:** Legacy theme conversion at later source; interactive HUD/news/hover/title/editor layouts, native non-Windows and standing multiplayer/replay checks remain. No new owner decision.

### U196 — `ab3065fbe1` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `ab3065fbe12a43e80a34c02ea46fb486c91d68c2`.
- **Fork receipt:** `6077ca4edd4f53c7242e83daf0004dbe90bc9a03`.
- **Remaining:** 166 → 165.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add Russian labels 7040 path additions, 7041 park information panel and 7042 date information panel.
- **Additional decisions / behavior:** Adopt current source translations in historical order; existing English IDs match and fork IDs 8000-8041 are untouched. No gameplay change.
- **Verification:** Read entire three-line source delta; resulting diff and blob identity 075c93f39d match source; diff check passed.
- **Pending / concerns:** No runtime test needed for localized labels; standing validation debt unchanged.

### U197 — `b5c1570b78` — Rename members of smaller entities (#26946)

- **Source:** `b5c1570b782dc1587a475045491f67c9c5dcb0ac`.
- **Fork receipt:** `b5d769173b00f50802680ec74b3e20e44fb509a8`.
- **Remaining:** 165 → 164.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Manually rename small-entity members and constants across all 42 source paths plus fork train tests; adapt fork registry/presentation guards, entity list and batched park serialization.
- **Additional decisions / behavior:** Preserve existing simulation/RNG/timing, layout and serialization order including duplicate legacy fields. Removed Paint declarations have no definitions/callers. Renamed particle serializers remain outside existing network checksum and snapshot coverage. No public script key, API119/protocol11/save60016 change.
- **Verification:** B39 first build 86.31s zero warnings/errors; all 240 tests in 13 suites pass in 24.216s. 43-file token proof, ten-file source equality with five documented pre-existing comment spellings; residual search and diff check pass.
- **Pending / concerns:** Standing interactive/native non-Windows/live multiplayer and replay limitations unchanged.

### U198 — `f5f4b522c7` — Replace some bottomToolbar invalidation with date/parkInfoPanel (#26948)

- **Source:** `f5f4b522c7438b6c2ad30f28a941c4d325116f49`.
- **Fork receipt:** `3c6def531a1805fda0d4398b712c7ce82e20a012`.
- **Remaining:** 164 → 163.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Retarget ten bottom-toolbar redraw requests to their new park/date panel owners across five files.
- **Additional decisions / behavior:** D07: money/no-money/loan changes invalidate park info; date cheats invalidate date info. Correct upstream ConsoleCommandForceDate targeting parkInfoPanel to dateInfoPanel, which actually draws the changed date. Keep remaining MapTooltip invalidation on news toolbar because it owns full-toolbar map tooltip rendering. No action values, simulation or weather timing change.
- **Verification:** Inspected entire five-file source delta and resulting ten-line replacement diff; audited remaining bottomToolbar invalidation and date drawing/action. Diff check passes.
- **Pending / concerns:** Batch compile/widget checks pending; interactive date/money redraw and standing limitations remain.

### U199 — `a74f7437b9` — Rename symbols of Peep (#26949)

- **Source:** `a74f7437b92694dab9fbd0d6374f566854c0f4fd`.
- **Fork receipt:** `e2b2eb90530ba8c82f4cb9dfac42931ba6725de8`.
- **Remaining:** 163 → 162.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Manually port Peep member and method renames across 55 source paths plus three fork-only test paths; preserve fork inline movement, transport/platform, registry and park migration implementations. Restore O04 missing park-rating HUD invalidation from U195.
- **Additional decisions / behavior:** 86 reviewed names and three anti-shadowing local renames; preserve all numeric/layout/gameplay contracts and unrelated legacy/rating/vehicle APIs. O04 corrects prior incomplete U195 intent migration: rating changes now invalidate parkInfoPanel independently. No API119/protocol11/save60016 change.
- **Verification:** B40 first build found three missed names from digit-separator scanner bug (five diagnostics); repaired scanner/references, final build 18.12s zero warnings/errors. All 252 tests in 14 suites pass in 33.886s. Source and 59-file fork token audits plus all five HUD intent-body comparisons pass; diff check passes.
- **Pending / concerns:** O04 redraw is source-verified, not observed with renderer; existing headless test cannot record dirty regions. Standing interactive/native non-Windows/live multiplayer/replay and later custom-theme conversion remain.

### U200 — `bb0215abb0` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `bb0215abb0e4a48cd71e5fc8b105ce664b49d59e`.
- **Fork receipt:** `17250edccdfb2b961549e65bfbc763b669a719b9`.
- **Remaining:** 162 → 161.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add IDs 7040-7042 in Esperanto, Korean and Dutch; apply 32 Polish spelling, grammar and punctuation corrections, including the broken STRINGID placeholder.
- **Additional decisions / behavior:** Translation-only. Polish STR_1636 fixes RINGID to STRINGID so the ride name formats correctly; other formatting tokens preserved. Correct U199 prose name-map count from 85 to measured 86 without changing its code or audit result.
- **Verification:** All four resulting files exactly match reviewed current source blobs. Checked every changed format-token sequence; only intended Polish STR_1636 differs. No new Polish IDs; three new IDs in each other language. Diff check passed.
- **Pending / concerns:** No runtime test required for translation-only delta; standing limitations unchanged.

### U201 — `840f1c4f91` — Move includes in scripting files inside ENABLE_SCRIPTING (#26950)

- **Source:** `840f1c4f9147ad328700377d93d90428a53d197a`.
- **Fork receipt:** `031f00dfd5327374b463e77069796d53c2b1d01b`.
- **Remaining:** 161 → 160.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Move includes for ScParticle, ScVehicle and ScRide inside ENABLE_SCRIPTING; add ScProfiler spacing.
- **Additional decisions / behavior:** Keep every include in its existing order when scripting is enabled; fork ScRide implementations remain unchanged. Disabled scripting no longer pulls these dependencies. No gameplay or public API change.
- **Verification:** Read full four-file delta, applied exact reviewed hunks and checked diff. All four files pass clang C++20 syntax checking with ENABLE_SCRIPTING undefined and no include paths.
- **Pending / concerns:** Enabled-scripting build and runtime checks at next batch; disabled full-project build remains standing debt.

### U202 — `8de0f10777` — Fix #25496: guest pickup button does not grey out when guest state changes (#26926)

- **Source:** `8de0f107774c4762496f84c2a0c2c544a380b1f1`.
- **Fork receipt:** `4ab943d26ceb888a63135f5a336454c2a1504fc6`.
- **Remaining:** 160 → 159.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Move guest DisableWidgets from resize-only path to common prepare-draw; add changelog and actual guest-window state-transition regression.
- **Additional decisions / behavior:** Preserve all pickup eligibility and fork platform policies; synchronize displayed availability without resize/reopen. Entering-ride platform/boarding remains disabled; walking/ordinary queuing remains enabled. Existing finance/debug tab predicates refreshed too.
- **Verification:** B41 production compiled; test fixture initially used subclass-only accessor via WindowBase (eight diagnostics). Corrected to public widget flag; final build 6.64s zero warnings/errors, all 49 tests in four suites pass in 7.671s. Actual GuestOpen test covers repeated transitions and three platform/entrance substates with unchanged dimensions.
- **Pending / concerns:** Rendered appearance and standing interactive/native non-Windows/full disabled-scripting/multiplayer/replay checks remain.

### U203 — `ad578a121d` — Merge Localisation/master into OpenRCT2/develop

- **Source:** `ad578a121d0a6ef633c3d36523626f69949fff5d`.
- **Fork receipt:** `e0f8987dcd718c028711eb16eb1f8071e3edac6c`.
- **Remaining:** 159 → 158.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add Brazilian Portuguese labels 7040 path additions, 7041 park info and 7042 date info.
- **Additional decisions / behavior:** Adopt current source labels in historical order, matching established English IDs. No gameplay change.
- **Verification:** Read entire three-line patch; final diff/blob d4c1fd5df0 matches source, diff check passes.
- **Pending / concerns:** No runtime test needed for localized labels; standing debt unchanged.

### U204 — `e25c301668` — feat: add PathNavigator (#26412)

- **Source:** `e25c301668db6ac1dd496e30463276f9905d07d5`.
- **Fork receipt:** `6fa1afb1d477afe0c1f5eb30008f50efb89dccb3`.
- **Remaining:** 158 → 157.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Add API120 PathNavigator/PathConnection physical-path plugin bindings and MSBuild registration. Port all 11 source paths; add per-tile structural reference revisions at core mutation owners and four actual plugin regression tests.
- **Additional decisions / behavior:** D09 approved: preserve upstream physical traversal, including nonreciprocal edges, first neighbor per direction, destination-only queue/wide filtering and ghost banner rules. Replace ambiguous same-height fallback with explicit stale-reference failure; live properties remain live and unaffected tiles survive resize. Revisions cover deferred/ghost edits, inspector reorder/type changes and map lifecycle independently of guest topology. Correct world-coordinate and nullable/lifetime declarations. Protocol11/save60016/fork fare, transport, seats and crowding unchanged. Detailed trace and failed-test correction in B42.
- **Verification:** Full 940-line source inspected; four new files equal source tokens plus enumerated lifetime changes; ScMap overload/options exact. B42 builds 39.50s and final 12.30s, zero warnings/errors. Initial focused 3/4 (overbroad resize test corrected); final 276/17 suites passed in 35.971s, including all four real API120 plugin tests.
- **Pending / concerns:** Interactive plugin visualization, live multiplayer/replay and native non-Windows/full disabled-scripting builds unverified; existing standing debt retained. No owner choice pending.

### U205 — `ac926d5550` — Fix #23872: Set load/save folder for parks loaded outside the game (#26927)

- **Source:** `ac926d5550b21addad8fc0630c0e528d931915e5`.
- **Fork receipt:** `this entry’s unique Upstream-Commit trailer`.
- **Remaining:** 157 → 156.
- **Disposition:** adopt applicable changes.
- **Manual changes:** Remember the absolute directory of a successfully continued local startup save for subsequent load/save dialogs, and add changelog.
- **Additional decisions / behavior:** Adopt source guard: only nonempty scenario save path matching startup path; failed loads, fresh scenarios and URL startup bypass it. Fork layered object directories untouched. No gameplay or save-format change.
- **Verification:** Inspected full two-file source and local startup/load branches; exact helper and success-branch call ported; diff check passes.
- **Pending / concerns:** Compile at next batch. Interactive file association/command-line folder persistence unverified; no external config-write claim.
