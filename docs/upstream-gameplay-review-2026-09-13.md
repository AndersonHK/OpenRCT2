# Gameplay and legacy-design review — 2026-09-13

**Implementation authorization — 2026-09-13:** The owner approved every recommendation below. Historical “pending/open” wording describes the planning snapshot and is superseded by this approval. Follow the [migration log](upstream-migration-2026-09-13.md) for current status. The owner now requires historical, one-source-per-receipt integration with lightweight checks and periodic batch builds/tests, superseding the grouped/final-ancestry workflow and per-change build cadence below.

Read this before approving the catch-up. It separates changes to the game we play from refactors that could accidentally
restore an older design. Source boundaries and every incoming SHA are in the [commit ledger](upstream-commit-ledger-2026-09-13.md);
implementation order and decision details are in the [port plan](upstream-port-plan-2026-09-13.md).

**The ticket-age behavior you disliked is still present upstream.** Our fork uses a 1.5× value multiplier for rides younger
than five months and 1.2× for rides younger than thirteen months. Upstream instead adds 30 and 10 internal value units in
those bands. These are not £30/£10 price increases. Ticket prices consume the resulting value through the fork's pricing
policy. For a low-value ride, a fixed additive bonus is much stronger proportionally than a multiplier; that is the design
difference to preserve.

There is **no new upstream age-rule change in this incoming range**: `RideRatingsCalculateValue` is identical between the
common ancestor and target after removing whitespace. But `51649e5873` moves the whole legacy function into a namespace.
Taking its replacement body would restore the additive bonuses and drop fork-specific value/target-price handling.
[D11](upstream-port-plan-2026-09-13.md#d11) explicitly says to keep the mod's proportional bonuses. This is preservation of
an established choice, not a request to approve the rejected behavior again.

## Incoming gameplay changes to decide

All decisions below remain open. Recommendations can be accepted, changed or deferred individually.

| Decision / commits | What would change in play | Fork assessment and recommendation | Deferral consequence |
| --- | --- | --- | --- |
| [D02](upstream-port-plan-2026-09-13.md#d02) — `829dd93bd0` | Non-block rides can fit more cars/trains by losing the station end margin. Capacity can affect queues, throughput and revenue. | **Prefer keeping the fork margin initially.** Our station assignment and platform staging use that allowance too; the upstream edit covers only its own capacity owner. | RCT1 compatibility improvement remains partial. The separate zero-car import fixes can still land. |
| [D01](upstream-port-plan-2026-09-13.md#d01) — `1cc42279dc` | Stats and vertical graphs stop warning in red for negative G below -2.50G. | **Keep the current warning initially.** Our continuous force model still assigns increasing intensity/nausea to harsh negative G. This patch does not change that model. | No dependency requires removing the warning. Keep both global and directed-leg views consistent. |
| [D03](upstream-port-plan-2026-09-13.md#d03) — `2e43967fe5` | The clearance cheat permits water rides off water, and water/terrain changes that currently fail water-specific checks. | **Recommend adopting in cheat mode only.** Do not weaken ordinary placement or reintroduce old unsafe tile traversal. | Retain all three old water-specific action restrictions consistently. UI wording/changelog should not claim the broader cheat. |
| [D04](upstream-port-plan-2026-09-13.md#d04) — `d81c3a7f14` | A park with many staff but no entertainer/security no longer qualifies for Best Staff. | **Recommend adopting** the four-type requirement, unless count-only eligibility is preferred for the mod. The commit subject describes the bug, not the resulting rule. | Independent award-rule change; safe to defer without blocking other staff refactors. |
| [D05](upstream-port-plan-2026-09-13.md#d05) — `7cc48f5c14` | Dragging paths over hills produces connected slopes by default rather than disconnected terrain-following tiles. | **Recommend adopting**, with the fork's topology/cost/action boundaries retained. Keep Ctrl/Shift behavior and verify generated geometry. | Can defer slope generation while taking the same commit's stale-preview and modifier fixes. Record a partial port. |
| [D10](upstream-port-plan-2026-09-13.md#d10) — `5a814722e1`, then `11bf58dceb` | Walls get their own clear-tool mode; path additions can be cleared separately. Scripts using mask 1 stop clearing walls. | **Recommend upstream behavior**, after checking existing scripts. UI defaults preserve small-scenery-plus-walls, but script behavior is a compatibility decision. | Can delay new modes, but then adapt the following flag conversion and do not claim complete API-117 clear semantics. |
| [D15](upstream-port-plan-2026-09-13.md#d15) — `7aca955172` | Starting the recognized CD version of Okinawa Coast applies ownership/construction-rights corrections to specific tiles. | **Recommend adopting**, but this is a scenario rules change, not a cosmetic name correction. Preserve the hash-specific trigger and topology invalidation. | That scenario edition retains its old starting land rights. Name/schema fixes can still be ported separately. |
| [D08](upstream-port-plan-2026-09-13.md#d08) — `4872effb80` | Plugins gain a way to change a vehicle's track subposition. A script can alter motion/state even though no normal UI control is added. | **Recommend a fork-adapted setter** with upstream bounds/mutability contract and correct state/presentation repair. | API-122 writable property remains unsupported; document the capability rather than claiming full compatibility from the version number. |
| [D09](upstream-port-plan-2026-09-13.md#d09) — `e25c301668` | Plugins can navigate physical path connections, respecting options such as banners, queues and ghosts. | **Recommend upstream physical-path semantics.** It must not replace guest routing or promise the same result as our fare-aware transport planner. | Plugins needing this API cannot run unchanged. Existing guest routing is unaffected. |

The two other open choices are interaction/layout changes: [D06](upstream-port-plan-2026-09-13.md#d06) moves ride-type and
visibility cheats between tabs; [D07](upstream-port-plan-2026-09-13.md#d07) adopts the new HUD and theme structure. Neither
should change ride operations, money, simulation speed semantics or the fork's weather timing.

## Legacy fixes that need special treatment

| Incoming change | Why blindly applying it is wrong | Disposition |
| --- | --- | --- |
| `d7d8915065`, initial vertical-G reset | The fork already sets `previousVerticalG` to 1G and has additional longitudinal/sample reset logic. | **Code already satisfied.** Keep that code; handle historical changelog and fork protocol separately. |
| `51649e5873`, Ride/Vehicle namespace migration | Includes upstream's unchanged additive age bonuses and entire legacy rating/value implementation. | **Adapt names/ownership only.** Preserve proportional age bonus, cent conversion and `RideUpdateTargetPrice`; [D11](upstream-port-plan-2026-09-13.md#d11). |
| `7f879409ac`, `28ad2fcaa2`, `bb0b26e3f2`, `1e7fdd7ba6`, rating/RTD/mode refactors | Upstream still has legacy base ratings, post-test modifiers and `BonusMazeSize`; our live sample/maze model deliberately replaced them. Upstream descriptor defaults are not the mod's balance settings. | **Preserve the fork model.** Rename surviving symbols and keep removed modifiers removed; [D12](upstream-port-plan-2026-09-13.md#d12). |
| `f08bc61f03`, park flags; associated peep/station renames | Upstream flag edits are surrounded by population soft caps and its old park-rating/arrival algorithm. Our happiness-driven growth intentionally differs. | **Preserve fork algorithms.** Migrate flag access without reinstating upstream cap checks; [D13](upstream-port-plan-2026-09-13.md#d13). |
| `f08bc61f03`, finance flags; HUD and station/time refactors | Their source context uses upstream costs/time assumptions. An unchanged formula can still overwrite our cent precision or 40 TPS conversion. | **Preserve units and policies.** No new incoming finance redesign was found in the inspected finance/date/marketing delta; [D14](upstream-port-plan-2026-09-13.md#d14). |
| `4beb0ec8e0`, structured clearance arguments | Upstream's callback/traversal lacks the fork's erasure result and mutation-safe reference handling. | **Adapt the parameter structure only.** Keep compaction-aware construction logic and the latest object-placement fix. |
| `340bb07626`, missing vertical twists | This fixes a regression introduced by the incoming widget-visibility migration `d517f19ce2`. It does not add a new ride element or alter ratings. | **Take final correct visibility as a unit.** Do not port the temporary regression or describe this as a balance feature. |
| `df06376780`, indestructible track reporting | Upstream moves cheat interpretation out of the stored-property getter into removal permission. Other fork callers must retain their intended raw/effective meaning. | **Adopt separation after caller audit.** Normal removal permission should stay equivalent; Tile Inspector should show the actual stored flag. |
| `fdcda7be5d`, `8eb25bb02f`, `0dfc546c8b`, RCT1 zero cars | Correcting imported visible-car accounting is distinct from increasing station capacity. The fork reconstructs seats/platform state from these values. | **Recommend port**, validating imported occupied and proposed trains. Do not condition these fixes on accepting D02. |
| `e555c76500`, invalid ride types | Rejects out-of-range type IDs in a cheat action; does not disallow valid type changes. Our type changes also need mod cache/sample invalidation. | **Recommend port** of the guard while retaining fork invalidation. |
| `1423ab779a`, missing station object | Protects a legacy null dereference; replacing the whole station helper could discard our staging behavior. | **Recommend local null protection**, with fork station operations retained. |
| `af352f18ef`, random/cycle colour helpers | A helper extraction can accidentally switch simulation RNG to nondeterministic UI RNG, or replace a time conversion with `/32`. | **Adapt without RNG/timing change.** Mechanical groups should preserve simulation checksums. |

## Scope of the economic and simulation review

The following evidence applies to the frozen `69872010ae..15d4b5e933` upstream delta, not a claim that upstream and the mod
behave the same:

- `RideRatingsCalculateValue` was extracted at both upstream endpoints and compared after whitespace removal: identical.
  Its age bands differ from our current fork as described above. The namespace move is the sole incoming commit matching
  the age-table/summand search in that file.
- `Finance.cpp` changes park-flag representation; interest/wage/upkeep formula context remains upstream's existing code.
  `Date.cpp`, `Marketing.cpp` and `core/Money.hpp` have no net upstream change in this range. Preserve the mod's distinct
  implementations even where upstream does not modify their formulas.
- `Park.cpp` changes includes, flag/accessor representations and symbol spelling, including code surrounding the legacy
  guest cap. This is not a requested replacement for the mod's growth/park-rating algorithm.
- Rating/RTD migration changes many lines because of names and namespace indentation. Targeted review of those changes
  found the legacy implementation preservation traps above; it is not a license to copy whole files or use upstream rating
  fixtures as the mod's required numerical results.
- Other incoming simulation-facing items include award eligibility, water clearance, train capacity, imported car counts,
  scenery-clearing actions and script mutation. They are itemized above. Rendering/input/description fixes are listed in
  the ledger and should not silently change simulation state.
- The object asset manifest advances v1.7.10 → v1.7.11. The
  [tag comparison](https://github.com/OpenRCT2/objects/compare/v1.7.10...v1.7.11) reports two commits, with one net changed file:
  `objects/rct1/ride/rct1.ride.spinning_cars/object.json`, adding Catalan name/description strings. No gameplay-property change
  appears in that returned source diff. The [release notes](https://github.com/OpenRCT2/objects/releases/tag/v1.7.11) list a
  typo fix and translation sync. Comparison metadata and patch are retained in the JSON ledger. Packaged archive bytes
  still need hash verification during implementation; the archive itself was not downloaded in this pass.

No runtime balance test or installed park/plugin audit has been done. The ledger distinguishes targeted source review from
intent/path triage; its 361 rows are complete history coverage, not 361 finished semantic ports.

## Deferral policy

For any item the owner chooses to defer, record the source SHA(s), reason, retained mod behavior, dependencies, API/asset
impact and revisit trigger in the JSON `deferral` fields. Use separate dispositions for **implemented**, **already present**,
**adapted**, **intentionally omitted** and **deferred**. A deferred gameplay choice must not block an independent crash fix.

Do not label a commit "fixed" merely because upstream calls it a fix. First identify the failing behavior, confirm that the
same code path exists in the fork, and check whether its proposed result matches the mod's design. When the old path is gone,
record the fix as inapplicable/superseded with code evidence, rather than restoring that path just to fix it.

Catch-up has two separate meanings: history accounted for and features actually adopted. An ancestry-only merge can account
for explicitly accepted omissions, but cannot make deferred features implemented. Keep omissions visible in the release
notes and ledger. Until the owner accepts those omissions as the intended checkpoint, retain the outstanding range and do
not create a full-target ancestry receipt. Do not advertise complete plugin/API compatibility for deferred capabilities.
