# U185 — ownership compatibility decision

Status: owner decision resolved; U185 implementation resumed from verified U184 HEAD `cb64f95e31ba14103e784a4bee1fb0433742c11b` on `codex/catching-up-to-upstream` (184/361, 177 remaining at resume). The receipt and validation log record subsequent progress. This shared working-tree note is available for the overseer; cross-thread messaging was not retried.

The owner decision relayed by the overseer authorizes upstream API 119 ownership numbers and the matching action format, with no legacy-number adapter. Standing guidance: plugins will target upstream, so take upstream compatibility when it is free and does not change intended gameplay. Normalize script getters/setters and action parameters/serialization to 1/2/4/8, document those values, advance fork protocol 10 to 11 with `andersonhk` retained, preserve saved high-nibble ownership/fences and save version 60016, and exclude the confirmed land-pricing regression. This supersedes the earlier generic external-number preservation recommendation for this source. The sections below retain the decision's evidence and alternatives for traceability.

Source: `11547e998a4f0e6c3f2b0c05018b53ab6a024e41`, “Make enum class+FlagHolder for ownership flags and normalise them” (26 files, 171 additions, 126 deletions). Next source remains U185. Existing pending-port.json belongs to completed U184: do not record it again.

## Resolved decision: external ownership numbers

The approved [port plan](upstream-port-plan-2026-09-13.md), under “Mechanical migrations with semantic traps”, says to preserve “raw file layout and external numeric contracts”. U185 preserves file layout but changes external numbers. D10's approved clear-scenery API change does not explicitly settle this separate contract.

| Meaning | Existing plugin/action number | Upstream API 119 number |
| --- | ---: | ---: |
| Unowned | 0 | 0 |
| Construction rights owned | 16 | 1 |
| Land owned | 32 | 2 |
| Construction rights for sale | 64 | 4 |
| Land for sale | 128 | 8 |

Flags can be combined. Upstream changes both the numeric surface `ownership` plugin property and the land-set-rights action's ownership parameter/serialization. It increments plugin API 118 to 119 without an ownership compatibility adapter. An old plugin assigning 32 would clear ownership because only the new low four bits survive. Keeping just the old getter would also be inconsistent.

Recommendation: explicitly adopt API 119 ownership numbers and the matching action format, document the plugin migration, and advance fork network protocol 10 to 11. Keep packed park-file ownership in the high nibble and save version 60016. This maintains consistent compatibility with upstream's new API. Existing ownership-writing plugins need updating; plugins embedded in users' parks have not been inventoried.

Alternative: retain legacy external numbers through boundary conversions while normalizing internal flags. This requires a compatibility policy for API-119 plugins and action parameters. Advertising unqualified API-119 ownership compatibility while retaining old numbers would be incorrect. A version-aware adapter is possible additional work, not implemented or tested here.

## Implementation decision: preserve land pricing

Upstream changes the land-purchase predicate from `!currentOwned && desiredOwned` to `!(currentOwned && desiredOwned)`. The surrounding branch already requires neither land nor construction rights to be owned. The new predicate therefore charges for certain changes between unowned/for-sale states without purchasing ownership.

Preserve `!currentOwnership.has(OwnershipFlag::owned) && _ownership.has(OwnershipFlag::owned)` in the port. This corrects an unintended refactor regression and retains existing economics. Ride-age pricing is unaffected.

A read-only arithmetic comparison of all 256 valid ownership-mask pairs, including the identical-state early return, found 12 differences: every difference turns a zero-cost transition into a land-price charge. For example, unowned to land-for-sale charges the full land price. Construction-right purchases still overwrite the provisional land charge with the construction price, following existing ordering. Evidence: `obj/upstream-audit/audit_u185_pricing.py` and `u185-pricing-audit.json`. This is arithmetic evidence from the inspected predicates, not a compiled action test.

## Implementation and validation requirements

After the owner resolves the external contract, port the reviewed typed ownership changes, register MapOwnership.h in the project, preserve the pricing predicate, and implement the chosen API/wire boundaries consistently. Preserve fence bits and high-nibble modern/RCT12 storage; RCT1/S6 import already uses the accessors. The current search found no direct ownership constants in tests or fork path-topology consumers; recheck after applying changes instead of assuming anticipated conflicts exist.

Add action query/execute regression coverage for ownership-cost transitions, packed ownership/fence preservation, and the selected script/action numeric contract. Preserve fork mutation/invalidation behavior. Build and run affected gameplay, imports, park migration, scripting, network and topology suites before recording U185. No U185 build/runtime test has run and no production source has been edited.

The companion objects fork remains pinned to `b2a5511cd7ff90646dd40c61f08cb27fa977e8f4`; this source needs no object-data change. No upstream-objects fallback is introduced. Existing interactive and non-Windows validation limitations remain as documented in the validation log.

## Follow-up inspection while the decision is pending

The next automatic goal continuation rechecked HEAD and the remaining count: unchanged, with no owner answer yet. A case-insensitive search of tracked JavaScript and TypeScript for `ownership` and `landsetrights` found declarations only in `distribution/scripting/openrct2.d.ts`, with no matching tracked plugin implementation. This does not inventory installed plugins or scripts embedded in park files and cannot establish that the breaking change is harmless.

The declaration file exposes both ownership values as bare numbers; LandSetRightsArgs links to SurfaceElement.h, where this source removes the old constants. Whichever boundary contract the owner selects, update these public property/argument comments to state the numeric flags explicitly and correct the reference. Upstream's 26-file patch omits that documentation change. This is additional concrete migration work identified by the follow-up inspection; production edits remain deferred until the decision is answered.

Goal status: blocked after the same unresolved owner decision persisted through three consecutive goal turns. HEAD and 177 remaining were reverified on the third turn. No approval was inferred from automatic continuation messages. Resume at U185 when the owner selects the external ownership contract; the full 361-source objective is unchanged.

Implementation checkpoint: the approved API/action format, protocol 11, public documentation and pricing correction are implemented. Batch 35 passed its first Release/Vulkan build (0 warnings/errors) and all 245 selected tests, including 256 actual ownership query/execute transitions, packed-byte coverage and a real API-119 plugin. See the validation log and U185 journal receipt for authoritative completion. Historical statements above about no production edits or a blocked decision describe the earlier pause only.
