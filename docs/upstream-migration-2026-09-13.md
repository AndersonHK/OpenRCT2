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

## Progress: 91 / 361 source commits recorded

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
- **Fork receipt:** `this entry’s unique Upstream-Commit trailer`.
- **Remaining:** 271 → 270.
- **Disposition:** adopt naming only.
- **Manual changes:** Rename PathSearchResult members and all applicable fork references using the individually reviewed name map.
- **Additional decisions / behavior:** Guest pathfinding result names only: preserve diagnostic strings, result order and search limits. Retain fork transport fare/time path choice, ghost exclusion and routing implementations; no legacy algorithm replacement. Update stale qualified identifiers in return-value comments; preserve ordinary prose.
- **Verification:** Complete source parent/child deltas verified to contain only the reviewed identifier substitutions and whitespace. Fork changes use only these substitutions; enum order/values retained. Singleton ancestry and whitespace checked per receipt.
- **Pending / concerns:** Compile at next coherent naming batch checkpoint.
