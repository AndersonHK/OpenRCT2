# Upstream catch-up plan — 2026-09-13

**Implementation authorization — 2026-09-13:** The owner approved every recommendation below. Historical “pending/open” wording describes the planning snapshot and is superseded by this approval. Follow the [migration log](upstream-migration-2026-09-13.md) for current status. The owner now requires historical, one-source-per-receipt integration with lightweight checks and periodic batch builds/tests, superseding the grouped/final-ancestry workflow and per-change build cadence below.

This is a review and implementation plan, with a complete [commit ledger](upstream-commit-ledger-2026-09-13.md) and
[machine-readable ledger](upstream-commit-ledger-2026-09-13.json). The repository has been fetched and inspected; no incoming
source changes have been applied, no merge or commit has been created, and nothing has been deployed.

Start with the [gameplay and legacy-design review](upstream-gameplay-review-2026-09-13.md) for changes to balance, construction,
capacity and script behavior, plus explicit decisions preserving overwritten legacy implementations. Deferrals are first-class
ledger dispositions; upstream's use of the word "fix" does not establish that its behavior belongs in this mod.

## Target and scope

| Boundary | Frozen value |
| --- | --- |
| Fork `develop` and fetched `origin/develop` | `e874ceb7708974e00040419d30390686c52a5d31` |
| Last integrated upstream / common ancestor | `69872010ae6b0febcd1b9b6a53f49e54968ceecc` |
| Target `upstream/develop` | `15d4b5e933555913d216f4548f673cd55cfb0579` |
| Latest fetched stable tag | `v0.5.5`, `8694e3483690323b6a75fa7264b6c58116f51f31`, September 6 |
| Ahead / behind | 45 fork-only / 361 upstream-only commits |
| Net incoming delta | 1,818 paths; 42,139 insertions, 39,750 deletions |
| Direct-merge preview | 140 conflicted paths: 125 content, 15 modify/delete |

Use the development target because this fork and the previous integration follow `develop`. Its changelog already starts
0.5.6, although its `Version.h` still says 0.5.5. Do not call it a released 0.5.6 build. All "latest" claims here refer to the
fetch performed on September 13 in America/Los_Angeles (September 14 UTC). If the desired target changes to stable, regenerate
the range against the tag; do not use a date cutoff or merely remove September commits.

The 361 entries include 324 single-parent changes and 37 actual merges. Nineteen localisation commits and one trigonometry
rename have misleading "Merge" subjects but only one parent. Conversely, some feature-titled commits have two parents.
The ledger uses actual parentage. All true merges produced an empty remerge diff, so their constituent entries cover the
changes without a second patch application. All 324 single-parent commits have different patch identity from the fork;
that is not proof their behavior is missing.

The largest work is a broad enum/header/API migration (168 ledger entries), not 361 independent feature ports. Nevertheless,
clean textual application would miss important conflicts in behavior. A direct merge would also bring back edits to deleted
OpenGL files. Prefer manual ports in bounded groups, retaining upstream attribution in each fork commit and the ledger.

## Existing decisions to carry forward

The [August integration record](upstream-port-manifest-2026-08-01.md), current source and subsystem rationale documents establish
the following baseline. These are preservation requirements, not new questions for the owner.

- **Time and money:** 40 TPS conversions, daily/weekly finance and marketing semantics, cent precision, ride price targets and
  park entrance policies. Symbol renames must not substitute upstream constants or money scales.
- **Ratings:** per-tick sampled G/speed/context, per-car sampling, directed journey legs, longitudinal acceleration and maze
  sampling/capacity. Do not resurrect deleted `BonusMazeSize` behavior when renaming ratings modifiers.
- **Routing and boarding:** shared path fields, topology generations, live validation, transport fares/journey choice,
  prepaid platform waiting and exact-seat boarding. Ghost previews remain outside the guest routing graph.
- **Map edits:** the latest fork commit adds safe tile references and mutation handling. Preserve
  `ClearanceResult::elementErased`, compaction-aware removal and reference reacquisition after edits. Upstream clearance
  signature cleanup is not permission to replace these algorithms.
- **Presentation:** owned frame packets, newest-frame mailbox, dedicated Vulkan worker and three GPU frames in flight;
  world-space spatial audio and fork pacing. Preserve value ownership when moving drawing/palette/picked-up-peep APIs.
- **Persistence:** current private save version is **60016**, including transport shelter exposure. Previous documents ending
  at 60015 are not the current boundary. Retain older-target export without mutating the live park. Upstream `ParkFile.h`
  has no net change in this range; renames and importer edits still touch serialization consumers.
- **Multiplayer and build:** keep protocol flavor `andersonhk`, deterministic simulation and staff reservation ownership;
  first-party x86/x64 AVX2 minimum and existing floating-point settings. Keep Vulkan MSBuild/CMake sources and deploy pairing.
- **Boat Hire:** the [current rationale](boat-hire-return-rationale.md) explicitly removed the guest-thought forced-return
  override. Renaming its states must not restore that discarded behavior.

## Decisions for the owner

These recommendations are proposals, not accepted decisions. Only the affected feature needs to wait for a decision; the
independent migration and correctness work can proceed. Record the answer and date under the corresponding entry and in
the JSON ledger. Technical invariants and straightforward crash fixes are handled in later sections without separate votes.

| ID | Visible choice | Recommendation |
| --- | --- | --- |
| [D01](#d01) | Keep red warnings for harsh negative G, or follow upstream and remove them? | Keep the warning in this port; our ratings still penalize those forces. |
| [D02](#d02) | Permit longer trains in non-block modes by removing their station margin? | Keep current margin initially; adopt import corrections separately. |
| [D03](#d03) | Should the clearance cheat allow dry/mid-air water rides and water/terrain edits beneath them? | Adopt upstream behavior when the cheat is enabled. |
| [D04](#d04) | Require all four staff types for the Best Staff award? | Adopt upstream's four-type requirement. |
| [D05](#d05) | Automatically connect dragged terrain-following paths with slopes by default? | Adopt the new default, preserving Ctrl/Shift placement. |
| [D06](#d06) | Move ride-type and visibility cheats to upstream's new tabs? | Adopt the moves with our operations controls retained. |
| [D07](#d07) | Adopt the split park/date/news/status/editor HUD and migrated themes? | Adopt the new layout, preserving mod timing and controls. |
| [D08](#d08) | Let plugins change a vehicle's track subposition? | Adopt the bounded mutable API with fork state/presentation repair. |
| [D09](#d09) | Should the new plugin path navigator mean physical paths or our full guest routing? | Keep upstream physical-path semantics. |
| [D10](#d10) | Split walls from the plugin's small-scenery clear mask? | Adopt upstream's versioned API contract; audit existing park scripts first. |
| [D15](#d15) | Apply corrected land/construction rights to the CD Okinawa Coast scenario? | Adopt the hash-specific correction, subject to scenario preference. |

Four additional design decisions are recorded below as **retain established fork behavior**, not new approval requests:
[D11](#d11) proportional ticket-age value, [D12](#d12) sampled ratings/maze balance, [D13](#d13) happiness-based growth,
and [D14](#d14) money/time/pricing policy. Any proposed departure from them becomes a new owner choice before implementation.

<a id="d01"></a>
### D01 — Negative-G warnings

**Pending.** [Upstream `1cc42279dc`](https://github.com/OpenRCT2/OpenRCT2/commit/1cc42279dcbd40802fc0f60d1674e6fa962a697f)
removes the red negative-G statistic and vertical-graph highlighting below -2.50G. It changes display, not force calculation.
Our [rating model](ride-rating-aggregate-rationale.md) deliberately gives harsh negative G increasing intensity and nausea.
The current [Ride window](../src/openrct2-ui/windows/Ride.cpp) warns in both directed-leg and global measurements, plus graphs.

Keep the current warning for now. Removing it is a reasonable UI preference, but would remove feedback on a force the mod
still penalizes. A new severity scale tied to the continuous rating curve would be separate design work, not a prerequisite
for catching up. Whichever option is chosen, apply it consistently to both measurement views and graphs without changing
rating coefficients. Check a ride with minimum vertical G on either side of -2.50G.

<a id="d02"></a>
### D02 — Non-block train capacity

**Pending.** [Upstream `829dd93bd0`](https://github.com/OpenRCT2/OpenRCT2/commit/829dd93bd03ad79a4fd94803707f83dcc7831c4d)
subtracts the station end allowance only for block-sectioned rides. This can increase permitted cars/trains in ordinary play,
not just correct imported RCT1 parks. The fork's [Ride.cpp](../src/openrct2/ride/Ride.cpp) uses `kStationLengthEndAllowance`
both in maximum-car calculation and `RideGetMaxTrainsPerStation`, which also feeds multi-station train assignments.

Retain the existing margin initially. The alternative is to adopt the non-block relaxation consistently in both owners
after demonstrating that platform geometry and staging still work. Updating only the upstream-looking formula could make
the car-count UI disagree with the station allocator. Keep this choice separate from `fdcda7be5d`, `8eb25bb02f`, and
`0dfc546c8b`: those correct imported zero-car counts and should be ported either way. Verify shortest/longest stations,
boundary train lengths, one-car vehicles, block brakes before stations, station-to-station rides and exact-seat boarding.

<a id="d03"></a>
### D03 — Water-ride clearance cheat

**Pending.** [Upstream `2e43967fe5`](https://github.com/OpenRCT2/OpenRCT2/commit/2e43967fe5738d991623dba05ef85a8f5d24221c)
lets `disableClearanceChecks` bypass floating-structure, required-water and water-height restrictions in land, water and
track actions. The fork currently retains these water-specific restrictions even with the cheat enabled.

Adopt the broader cheat consistently across all three actions. Keeping the restrictions is the alternative if the mod
intentionally promises physical water validity even in cheat mode. Neither option should change normal construction.
Retain fork-safe removal and topology handling. Verify Query and Execute on land, elevated track, lowering/removing water,
changing surrounding terrain, and normal Boat Hire return routing; permitting a layout does not imply boats can navigate it.

<a id="d04"></a>
### D04 — Best Staff award eligibility

**Pending.** [Upstream `d81c3a7f14`](https://github.com/OpenRCT2/OpenRCT2/commit/d81c3a7f144148775f8b539dd4fe809c526e0c1b)
has an easy-to-misread subject. The actual patch **requires** handyman, mechanic, security and entertainer, in addition to
at least 20 staff and the existing guest/staff ratio. Current fork eligibility only checks counts.

Adopt the four-type requirement, or explicitly retain count-only eligibility if a park without entertainers/security should
qualify in this mod. This is an award rule; it does not replace deterministic handyman reservations. Check one missing type,
all four types, the 20-staff boundary and the guest-ratio boundary.

<a id="d05"></a>
### D05 — Connected drag paths

**Pending.** [Upstream `7cc48f5c14`](https://github.com/OpenRCT2/OpenRCT2/commit/7cc48f5c142b46d0f8a5ef50e7c9dac969fb530b)
introduces terrain sampling/filtering and slope generation for multi-tile drag placement without height modifiers. This
changes what gets built over hills. It also fixes stale provisional placement, modifier transitions and irregular slopes.

Adopt connected slopes as the default. The alternative is to keep existing terrain-following placement, taking the stale
preview fixes separately. A toggle is additional scope and not required by upstream. Apply placement through existing fork
actions so cent costs, mutation safety, connectivity invalidation and shared routing fields remain correct. Verify both
drag directions, spikes/valleys, queues, existing paths, obstruction failure, Ctrl/Shift switching, preview cancellation,
multiplayer permissions and guest traversal after construction.

<a id="d06"></a>
### D06 — Ride cheat layout

**Pending.** `c51e3a3e40`, `c35f71131f`, `ea084b9828` and `799002a820` move ride-type selection to operations and visibility
to appearance, with shop/stall exceptions. The fork's operations window also owns maze density, waiting rules, fares,
service details and directed-leg controls.

Adopt the new locations using the existing operations layout owner. The alternative is retaining the current locations
while taking validation/permission fixes. Never overwrite the whole Ride window. Verify shops with cheats on/off, absent
colour options, ordinary and powered-launch block modes, mazes, transports and narrow/expanded windows. Adopt invalid-type
validation and visibility permission fixes regardless of placement choice.

<a id="d07"></a>
### D07 — HUD and theme layout

**Pending.** The `8dab2cce07` / `212c99ae59` / `21e37e12f9` series separates park info, date info, news, RCT1-style status bar
and editor navigation; `be90806c80` migrates theme settings. Related fixes move rain around panels and correct
editor/multiplayer pause, speed and chat visibility.

Adopt the new layout and theme migration. Keeping the old layout means adapting many subsequent fixes back to a deleted
window owner and carrying a larger divergence. The fork's only direct change in `GameBottomToolbar.cpp` since the base is
the weather preview threshold: `GameTime::SecondsToTicks(30)` replaces upstream's 960 ticks. Move that rule into the new date
panel. Preserve cent-money formatting, turbo input responsiveness and all actual fork options when moving shared code.
Verify old custom themes, RCT1/RCT2 styles, news follow/dismiss, rain, resizing, editor modes, multiplayer and normal play.

<a id="d08"></a>
### D08 — Mutable vehicle subposition API

**Pending.** [Upstream `4872effb80`](https://github.com/OpenRCT2/OpenRCT2/commit/4872effb804d6bf7ec3e4cba7e4e46217321832d)
changes `Vehicle.subposition` from read-only to writable. Its setter checks mutability and enum bounds, updates track state
and removes the vehicle from interpolation. This is an advanced script mutation, not a new ordinary ride control.

Adopt the upstream numeric/mutability contract while using fork state and presentation repair. The alternative is keeping
the property read-only and documenting that API divergence. Audit what `UpdateTrackChange` already invalidates before adding
resets; do not clear passengers or accrued rating samples indiscriminately. Verify boundary/out-of-range values, forbidden
mutation context, occupied trains, staged boarding, directed-leg sampling and no stale Vulkan/tweener pose after mutation.

<a id="d09"></a>
### D09 — Plugin path-navigation meaning

**Pending.** [Upstream `e25c301668`](https://github.com/OpenRCT2/OpenRCT2/commit/e25c301668db6ac1dd496e30463276f9905d07d5)
adds `map` navigation bindings with path edges, banner restrictions, slopes and options for queues/wide/ghost paths. It does
not replace guest pathfinding and does not account for fare, destination eligibility, transport or live congestion.

Keep that physical-path contract so upstream plugins get the expected API. Making the same API silently mean the mod's
guest journey planner would produce different results and is not recommended; a separately named routing API can be future
work. Technical adaptation is still necessary: navigators retain tile coordinates and an element index; navigator lookup
can fall back after tile reordering while connections only validate the indexed element/type/height. On multiple paths at
one height this deserves an explicit identity test against our mutation model. Use safe live resolution or fail cleanly
after deletion/reordering, and document any behavior that intentionally differs. Do not let a ghost-inclusive plugin query
populate guest topology caches. Verify banners, queues, wide paths, slopes, duplicates, deletion, reorder, resize and shifts.

<a id="d10"></a>
### D10 — Clear-scenery plugin compatibility

**Pending.** `5a814722e1` separates walls into bit 8 and path additions into bit 16. Bit 1 becomes small scenery only;
the type declaration explicitly documents the pre-API-117 combined small-scenery/wall meaning. The normal UI defaults both
small scenery and walls on, so its opening behavior remains familiar. Existing scripts that send only bit 1 can behave
differently. The fork currently exposes API 116; the target reaches 122.

Adopt the upstream API contract and update any affected park scripts to request both bits. An explicit compatibility shim
for older plugins is the alternative if those scripts must run unchanged; it needs tests and a documented version rule,
not an unconditional OR that would defeat walls-only selection. Local park/plugin dependencies were not inventoried in this
planning pass. Verify each bit, combinations, insufficient funds, query/execute parity, path-addition removal while staff
hold service claims, and network serialization. Keep these semantics with the later ClearableItems FlagHolder conversion.

<a id="d11"></a>
### D11 — Keep proportional age value; reject accidental restoration

**Disposition: retain established fork design.** In the current `RideRatingsCalculateValue`, rides younger than five months
use 1.5x value and younger than thirteen months use 1.2x. Upstream's corresponding rows add 30 and 10 internal value units.
The upstream function is unchanged between the base and target after whitespace normalization; `51649e5873` moves it into
a namespace. This is an implementation collision with an older rejected design, not a newly proposed upstream age feature.

Port namespace/type changes around the fork function. Preserve the multiplier rows, cent conversion, other aging/competition
rules and target-price update. Do not copy the moved upstream body. Verify low- and high-value rides at months 4/5 and 12/13,
aging cheat on/off, and automatic price-target recalculation. Any request to adopt additive bonuses must be presented as a
new balance decision; this catch-up does not reopen the owner's earlier choice.

<a id="d12"></a>
### D12 — Keep sampled ratings and mod ride descriptors

**Disposition: retain established fork design.** `7f879409ac`, `28ad2fcaa2`, `bb0b26e3f2`, `51649e5873`, `97b91941eb` and
`1e7fdd7ba6` migrate rating types, descriptor names, namespaces, station fields and ride-mode sets. Their old/new upstream
trees still contain base-rating initialization, post-test modifiers and the maze-size modifier that the fork intentionally
removed or superseded.

Rename surviving fork symbols and preserve current sampled normal/maze paths, per-source coefficients, longitudinal G,
directed legs, decoration and available operating modes. Keep `BonusMazeSize` removed; do not restore a deleted helper to
make an upstream rename apply. If a future fix targets such a removed path, record a superseded/inapplicable disposition
with evidence. Verify sample-based numerical results and mode availability against the fork baseline, not legacy upstream
rating fixture values. New balance coefficients or a return to upstream aggregation require a separate owner decision.

<a id="d13"></a>
### D13 — Keep happiness-regulated guest growth and fork guest behavior

**Disposition: retain established fork design.** `f08bc61f03` changes park-flag representation in upstream's existing guest
cap/rating logic; related peep and station renames touch the surrounding owners. The fork retains a legacy cap estimate for
UI/scripts but deliberately does not use it to throttle arrivals. Current demand follows park value and sustained happiness.

Migrate flags and names on the fork implementation; do not reinstate upstream soft-cap checks, old park-rating penalties,
legacy transport ignorance or random-only surface recovery. Preserve nausea treatment and platform/transport states.
Verify arrival behavior above the old cap, happiness changes, targeted needs, fare-aware travel and surface recovery.
If an upstream fix assumes the removed guest algorithm, first decide whether its intended outcome still applies to our
model; do not restore the old model to obtain that outcome.

<a id="d14"></a>
### D14 — Keep cent precision, 40 TPS and price policies

**Disposition: retain established fork design.** The inspected upstream finance delta changes flag access; Date.cpp,
Marketing.cpp and Money.hpp have no net change in this range. That does not make upstream surrounding code safe to copy.
Preserve fork wage/interest/upkeep timing, marketing countdown, measured durations, admission/ride price targets, transport
fares and money conversion. Station/HUD refactors must carry actual durations and `GameTime` helpers to their new owners.

Test exact-cent amounts, time boundaries, marketing expiry, station waiting, and price policy output before/after the
mechanical port. In particular retain `GameTime::SecondsToTicks(30)` for weather preview when replacing the bottom toolbar.
Any new economic or temporal rule is an owner decision even if upstream labels it a fix.

<a id="d15"></a>
### D15 — Okinawa Coast CD starting ownership

**Pending.** `7aca955172` adds the CD scenario hash and a patch setting owned, available and construction-rights flags on
specific tiles. This extends existing corrections to another edition of the scenario; it changes starting buildable land
and purchase options, not only spelling or localization.

Recommend adopting the edition-specific correction. Retaining the original CD scenario rights is the alternative.
Keep hash matching and the existing scenario-start/import boundary; do not apply the patch indiscriminately to edited
saved parks. Verify the recognized CD scenario and unaffected hashes, rights purchase/buildability and fork topology
invalidation. Other scenario name/schema changes can land independently if this map-rule change is deferred.

## Porting sequence and review checkpoints

P0..P8 in the ledger are ownership groups, not nine monolithic commits. Use one commit for a coherent behavior change;
related renames can share a commit. Include the source SHAs and decision IDs in its message. Keep each checkpoint buildable.
Within a group follow parent-before-child dependencies, or deliberately implement the final target form and record every
intermediate SHA as covered by that final form. Do not apply both forms.

| Stage | Work | Completion gate |
| --- | --- | --- |
| 0 | Freeze refs, preserve current mod build/data and representative save copies; create `codex/upstream-port-2026-09` from the frozen fork for implementation. Capture current build/test/simulation baseline. | Refs match this document, clean source baseline, reproducible fixture/checksum record. |
| 1 | P1 collision map and P2 foundations: allocate fork string IDs, retain upstream meanings, migrate FlagHolder/enum/namespace/coordinate/entity APIs across all fork consumers. Include new headers/project entries with their owners. | No conflicting IDs or omitted fork callers; saved/script/action numeric encodings verified; Release build at bounded checkpoints. |
| 2 | Early correctness ports: packed-object filename sanitization, invalid ride types, missing station object, plugin image error handling, conversion object packing, request/plugin lifecycle and placement result/error fixes. | Focused reproduction succeeds; private save/export and mod object precedence unchanged. |
| 3 | P3 construction: structured clearance args on fork-safe traversal, ownership API normalization, indestructibility query semantics; then D03, D05, D10. | Mutation/topology regressions, ghost isolation, cost/permission parity and multiplayer actions pass. |
| 4 | P4/P5 ride and peep behavior: record already-present 1G fix, importer corrections, D01/D02/D04/D06, final construction widgets, peep action formatting and pickup state. | Ratings/boarding fixtures and UI cases pass; no altered fare/time/sample model. |
| 5 | P5/P6 HUD and rendering: D07, final focus/dropdown rules, owned picked-up peep, palette/screen/line split, custom sprites, remaps, land blending and patrol selector. | Vulkan/software-reference comparisons where supported; responsive turbo input; no mutable render pointers or restored OpenGL. |
| 6 | Remaining P7: D08/D09, resize hook, textbox caret, object/design/scenario lifecycle and final QuickJS registration. | Script API 117..122 contracts reconciled; object/park reload, temporary-map and presentation invalidation tests pass. |
| 6a | D15 scenario-rights choice; keep it separate from scenario name corrections. | Recognized scenario hash and starting rights verified; accepted deferral explicitly recorded. |
| 7 | Finish P1/P8 translations/assets/glyphs, metadata, platform fixes and include hygiene. Audit final API surface, protocol, build lists and release labels. | Asset hashes, language validation, clean builds and project-source coverage complete. |
| 8 | Full regression, evidence update, then ancestry receipt for the frozen upstream SHA. Keep deployment as a subsequent verified action. | Every source SHA has a port, accepted alternative or deliberate no-op with evidence; behind count then reaches zero. |

Some stages depend on a final owner introduced later in the raw upstream order. For example, palette-load fixes should use
the final Palette API, and bottom-toolbar fixes should land on the chosen final panels. Widget visibility and inverted
dropdown flags must be coherent with their callers before reviewing ride/HUD layouts. Do not postpone all header changes
until the end: foundational type changes are needed early, while include pruning is best finished after behavior owners
settle. P7 registration, glyph/Unicode and drawing-header moves similarly need their direct dependencies in the same checkpoint.

If choosing a smallest useful first implementation batch, start with filename sanitization, invalid ride-type rejection,
station-object null protection and plugin image error handling after the necessary local dependencies. These do not depend
on the pending gameplay preferences. Port object packing with explicit `TargetVersion` preservation next. Do not claim the
whole fork is current after this subset.

For deferred changes use the ledger's `deferral` fields: source SHA, reason, retained behavior, dependent commits and revisit
trigger. A partial port (for example preview fixes without new drag-slope generation) needs its remaining behavior listed.
Do not mark a deferred feature implemented or claim full API compatibility. An ancestry receipt may include accepted
intentional omissions only when the owner has chosen that as the checkpoint; otherwise leave the full-target receipt pending.

## Technical adaptations requiring care, not new product decisions

### String and sprite identity

The fork currently owns **42 English IDs, 7039..7080**. The new upstream tail occupies **7039..7063**, so there are **25 real
collisions**. For example, 7039 is our maze-capacity label and upstream's ride-type label; 7049 is our transport comfort and
upstream's status-bar title. Copying final English or inserting enum constants without remapping changes the wrong text.

Before installing new UI resources, produce an explicit old-ID → symbolic-name → new-ID map for all 42 fork strings, select
an unused range after checking the localization allocator/reserved limits, and update both `UiStringIds.h` and `StringIds.h`,
English definitions and any numeric consumers together. Preserve upstream IDs for translation compatibility. Check whether
any saved/script data uses those numeric IDs before finalizing the map. Copy final non-English translations only after this
mapping and preserve English fallback for untranslated fork strings. Check duplicate IDs, placeholders and encoding.
Sprite IDs also move: update the resource manifest and symbolic fork sprite consumers together.

Also retain the restored fork text at existing ID `STR_1651` and changed thought text such as `STR_1647`; the 42-string tail
is not the entirety of the English fork diff. `STR_1651` is absent from the base English file, so a simple set-difference
count finds 43 added definitions, of which 42 belong to the new 7039..7080 range.

### Mechanical migrations with semantic traps

- **Ownership normalization:** `11547e998a` changes `getOwnership()` from raw high-nibble bits to normalized `OwnershipFlags`;
  the setter still writes high-nibble tile storage. Update fork surface recovery, topology and scripting callers, preserving
  raw file layout and external numeric contracts.
- **Flag migrations:** peep, park, tile, ride mode, scroll, wall and viewport flags need holder access at serialization and
  bitwise boundaries. Renaming fork-only flags must not shift values, truncate holders or erase private state.
- **Entity/station renames:** include private save `ReadWriteFields` lists, snapshots/checksums, pool iteration, platform
  fields, station assignments, route fields, audio and tests. Upstream does not know these consumers exist.
- **Drawing enum and headers:** account for our Vulkan enum/config value and deleted OpenGL sources. Keep AVX2 behavior
  from the previous accepted decision; upstream's renamed drawing engine or removed includes do not replace that decision.
- **Clearance cleanup:** port `MapProposedConstructionInfo` while preserving the fork tri-state callback, safe references
  and erasure traversal. Check all placement actions and land-height removal, not just the new signature.
- **Peep descriptions:** `fe4e93cf0e` extracts action data/formatting. Carry `STR_WALKING_TO_PLATFORM_FOR` and
  `STR_WAITING_ON_PLATFORM_FOR` into both individual and grouped descriptions. Do not change guest state to fix capitalization.
- **Random colour helpers:** keep simulation RNG distinct from presentation RNG and preserve the existing call sequence.
  Check colour-cycle timing against 40 TPS rather than accepting a new `/32` helper wherever a fork conversion existed.

### Known satisfied or special cases

The 1G reset from `d7d8915065` already exists at `Vehicle.cpp:1489` in the frozen fork. Preserve its more extensive sample and
longitudinal reset logic; do not overwrite `test_reset`. Its historical changelog entry can be carried, but its upstream
network version bump is not the fork protocol policy.

Upstream resets stream revisions during releases and ends this range at version 0. Keep the `andersonhk` flavor and use a
fork-owned revision for the accepted new action/simulation semantics; revision 4 is the next proposed checkpoint after the
current 3. Never assign upstream's 0 or strip the flavor. New action masks, optional gameplay changes and plugin mutation
capabilities warrant explicit old/new client rejection and same-build determinism tests.

The target API advances 116 → 122. Reconcile declarations with implemented behavior; retain the fork's already-correct raw
`context.gameSpeed` contract and metric units. A version increase alone is not evidence all intermediate APIs were ported.

The target changes the objects archive to **v1.7.11**, SHA-256
`74e73bbd012339511bb359dd0fbb148fda899790ae37d7d69861d88183b19452`.
It does not change the replay archive in `assets.json`. Verify the actual downloaded bytes during implementation and retain
the fork's object-directory precedence. Do not infer availability from a URL or old August test counts.

The [object source comparison](https://github.com/OpenRCT2/objects/compare/v1.7.10...v1.7.11) reports two commits and one net
changed object JSON containing translation-string edits. No gameplay-property change appears in that returned diff. The
comparison metadata/patch is retained in the JSON ledger; packaged archive bytes still need the normal hash verification.

### Asynchronous lifecycle

`4cce29b5a9` stores request futures to avoid blocking when an ignored async result is destroyed. It is not a complete proof
of cancellation correctness. Test cancel, retry, a second Begin, closing the window and late callbacks, with lifetime owned
by the downloader/background-I/O mechanism. Do not place blocking network work in the synchronous simulation JobPool.

For picked-up peeps, carry the upstream API extraction into the fork's existing image/position/zoom snapshot. For plugin
images/water palettes, update cache/generation state through current owners before producing the next owned render packet.
For Splash Boats, check actual visible car count through fork presentation data instead of adding live simulation reads to
the renderer. Syntax that compiles can still violate these ownership boundaries.

## Verification and completion criteria

This pass verified the Git inventory, targeted high-risk source differences and isolated merge behavior. It did **not** build
or run the game. Ledger review levels distinguish 35 targeted code comparisons, 289 intent/path triage entries and 37 merge
probes. The triaged entries still need hunk review while porting. These are planning coverage counts, not completed ports or
an exhaustive runtime regression audit.

Use existing tests where they cover real contracts; add regression tests for changed behavior, not copies of implementation.
The [local build helper](../scripts/windows/build-local.ps1) is the current build authority: Release x64, toolset
14.44.35207, Vulkan enabled, bounded `/m:1` and `/nr:false`. Some older prose in `windows-local-build.md` shows an unrestricted
build; follow the current helper. Run tests from `bin` so relative test assets resolve. Confirm the discovered suite names
with `--gtest_list_tests` before selecting filters.

| Area | Required evidence before marking its rows complete |
| --- | --- |
| P1 resources | Complete ID relocation map, no collisions/duplicate definitions, compatible placeholders, glyph/resource regeneration and object archive hash. |
| P2 foundation | Fresh Release compile and affected test targets; no omitted fork symbols; flag/file/action round trips; PCH-disabled CMake/Linux or a clearly recorded unavailable check. |
| P3 map/actions | Existing map/path-topology and tile-element tests, consecutive removals/storage movement, preview cancellation, connected slopes, exact cent cost and deterministic network action results. |
| P4 simulation | Rating, maze, transport, station/seat, staff and imported vehicle cases; old save load/re-save and 60016 round trip; decision-specific behavioral expectations. |
| P5 UI/input | Small and enlarged UI, dynamic widgets, all ride/station modes, focus loss/return, mouse release outside, theme migration, editor/multiplayer visibility, normal and turbo speeds. |
| P6 presentation | Vulkan frame ownership and temporary-map suites; visual cases for held peep at all zoom levels, transparent sprites, remaps, palette changes, rain, surface edges and patrol water overlay; frame-time/per-pass evidence. |
| P7 plugins/files | Feature-specific scripts, invalid values, stopped/reloaded plugins, download cancellation, missing objects, converted packed objects, RCT1 imports and scenario/design previews; verify declared API behavior. |
| P8 release/build | MSBuild/CMake source entries exactly once, AVX2 target retained, appropriate platform checks, correct mod binary/data deployment pairing and release labels. |
| Full checkpoint | Full available tests, repeated deterministic EverythingPark runs, same-build multiplayer checks and measured performance against the frozen mod baseline. |

For each deterministic run record executable/source SHA, save hash, object set, warm-up/measured tick counts, checksum, TPS
and timing. Repeated runs of the same new build must agree. Across a behavior-changing port an old checksum may legitimately
change; explain it with the accepted decision and focused tests instead of automatically updating expectations. For purely
mechanical groups unchanged simulation checksums are the expected gate. Separate replay incompatibility attributable to
the mod's simulation from an unexplained regression; do not relabel upstream fixture failures as harmless without evidence.

Keep owner decisions and actual validation beside the related source SHAs. Leave genuinely unavailable platform/manual
checks pending and state them at handoff. The previous August "555 tests passed" result does not validate this target.

## Reproduction and final ancestry receipt

Inventory commands for the frozen comparison (no `rg` or `gh` required):

```powershell
git merge-base e874ceb7708974e00040419d30390686c52a5d31 15d4b5e933555913d216f4548f673cd55cfb0579
git rev-list --left-right --count e874ceb7708974e00040419d30390686c52a5d31...15d4b5e933555913d216f4548f673cd55cfb0579
git log --reverse --topo-order --format='%H %P %cI %s' e874ceb7708974e00040419d30390686c52a5d31..15d4b5e933555913d216f4548f673cd55cfb0579
git diff --stat 69872010ae6b0febcd1b9b6a53f49e54968ceecc 15d4b5e933555913d216f4548f673cd55cfb0579
git cherry e874ceb7708974e00040419d30390686c52a5d31 15d4b5e933555913d216f4548f673cd55cfb0579
```

The merge probe used `git merge-tree --write-tree --name-only --messages` with those two heads and a temporary
`GIT_OBJECT_DIRECTORY`, reading the repository object store as an alternate. Exit 1 means predicted conflicts, not a failed
integration attempt. The complete conflict path list is retained in both ledgers. No MERGE_HEAD or unmerged index was created.

Only after the implementation ledger is complete, decisions are recorded and the reviewed code is committed, record the
manual integration as ancestry. This command is **future work**, not a shortcut to porting:

```powershell
git merge -s ours --no-ff 15d4b5e933555913d216f4548f673cd55cfb0579 -m "Merge upstream/develop through 15d4b5e933 (manually integrated)"
git rev-list --left-right --count HEAD...15d4b5e933555913d216f4548f673cd55cfb0579
git status --short
```

The expected behind count against that frozen SHA is zero; the ahead count depends on actual port commits. Re-fetch before
calling the result current. If upstream advanced, append a separately reviewed range and retain the old evidence. Do not
replace the reviewed SHA in an ancestry-only merge with an unreviewed moving branch. Deployment then uses the existing local
Release/Vulkan build-and-data process after the acceptance evidence is complete.
