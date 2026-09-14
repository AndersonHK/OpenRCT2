# Upstream migration handoff — 2026-09-14

All **361/361** source commits are integrated or explicitly retained/deferred under the approved decisions, in historical parent-before-child order. Remaining ancestry against frozen upstream target `15d4b5e933555913d216f4548f673cd55cfb0579`: **0**.

Branch: `codex/catching-up-to-upstream`. Final source receipt: **`c6560d2679cd7871ba583236fd9515bb81955511`** (U361). The following handoff commit changes documentation only; this source receipt identifies the tested code. No new owner decision interrupted the final steps. Zero behind records reviewed ancestry, not a claim of identical upstream gameplay.

## Traceability and decisions

- [Commit ledger](upstream-commit-ledger-2026-09-13.md) and its [machine-readable mapping](upstream-commit-ledger-2026-09-13.json): all 361 source SHAs, dispositions and receipt mappings.
- [Migration journal](upstream-migration-2026-09-13.md): additional decisions, behavioral consequences and source-specific validation for every port. Historical pending checks are superseded by later batch results.
- [Validation record](upstream-migration-validation-2026-09-13.md): batch evidence, corrections, fixture limits and retained technical debt.
- [Approved gameplay review](upstream-gameplay-review-2026-09-13.md) and [port plan](upstream-port-plan-2026-09-13.md): D01–D15 recommendations remain approved.
- [Companion objects migration](upstream-objects-migration-2026-09-13.md): four objects receipts, data ownership and installation provenance.

The full receipt audit checked 361 unique sources in exact ledger order, exactly two parents per receipt, the correct source as second parent, and exactly one new source reachable beyond each old first parent. This includes merge commits rather than counting first-parent history alone. Evidence: `obj/upstream-audit/final-receipt-audit.json`; audit helper: `obj/upstream-audit/final_receipt_audit.py`.

Key retained behavior includes multiplicative ride-age pricing (1.5x below five months, 1.2x below thirteen months), negative-G warnings (D01/U349), station end allowance (D02/U334), sampled/per-car and directed-leg ratings, happiness-based park growth, transport journey pricing, prepaid platform states and exact seats. Money stays in cents and simulation time at 40 TPS. The fork's Vulkan presentation and spatial audio owners remain in use.

The latest additional choices worth reviewing are recorded under U338/U340 (HUD lifecycle and legacy theme defaults/aliases), U345/U346 (platform status/group labels and absent-ride formatter arguments), U351–U353 (import-only dummy-car translation), and U361 (shared widget prototypes change JavaScript own-property reflection). U284's textbox null-session/null-buffer guard remains. These do not replace the fork's simulation models. All 44 fork English IDs occupy 8000–8043, with the allocator boundary at 8192.

## Final Windows build and tests

Release x64, MSVC **14.44.35207**, `EnableVulkan=true`, serial MSBuild via `openrct2.proj`.

| Check | Result | Local evidence |
| --- | --- | --- |
| Final source solution build | Passed; 0 warnings, 0 errors; 16.68 s | `obj/upstream-audit/batch-98-build.log` |
| Unfiltered test suite | **616 tests, 53 suites; all passed; 0 failures/errors/disabled; 57.077 s** | `obj/upstream-audit/batch-98-full-tests.log` and `.xml` |
| Solution build at final receipt | Passed; 0 warnings, 0 errors; 2.37 s; outputs up to date | `obj/upstream-audit/final-committed-build.log` |
| Complete receipt/order audit | 361 singleton source receipts; 0 remaining | `obj/upstream-audit/final-receipt-audit.json` |
| Pinned companion assets | 4,454 installed tracked files byte-equal to companion checkout; no tracked object content diff | `obj/upstream-audit/final-artifacts.json` |

The full test run exercised the final source before its receipt was written. No source changed afterward; the post-receipt solution build confirms its outputs are current. The receipt and handoff add documentation/ancestry, so another identical full run was not needed. Existing tests and new regressions cover actual JS/UI bindings, HUD/theme conversion, save/import, ratings, routing/topology, GPU command foundations and audio. A passing headless fixture does not prove native GPU presentation. The log contains denied writes to the sandbox-external user config/index; real-profile persistence is not established by this run.

Reproduction from the repository root uses `python -X utf8 obj/upstream-audit/build_batch.py openrct2.proj <log-path>`. Run `bin/tests.exe` with working directory `bin`, no filter, and `--gtest_output=xml:<output-path>`. The helper and detailed logs are local audit scratch, not packaged source dependencies.

The CLI reports version **0.5.5**, network **0.5.5-andersonhk-11**, plugin API **122**, park file version **60016**, minimum **57**. This MSBuild version output does not embed a source SHA; the source revision/build evidence and binary hashes below establish artifact identity.

## Executable identity and freshness

All paths are relative to `C:\Users\imper\Documents\GitHub\OpenRCT2`. The final build confirms these B98 outputs remain up to date at the final source receipt.

| File | Bytes | Last modified (UTC, 2026-09-14) | SHA-256 |
| --- | --- | --- | --- |
| `bin/openrct2.exe` | 21,312,000 | 14:33:07.927 | `1c997f3b030d096388f1e30358102094b587cf3ef8bedee3f72cd89f31fe791e` |
| `bin/openrct2-cli.exe` | 16,840,704 | 14:33:14.918 | `b698f7705dc23468312f306c71658884b0c8523b8d3e7775f3178337fd62e333` |
| `bin/tests.exe` | 24,629,248 | 14:33:13.229 | `f29db045d3df6df0702be708e2f11cd81a513ee0d6916222e6ae9ae01d86318b` |

## Companion objects

Checkout: `C:\Users\imper\Documents\GitHub\OpenRCT2-objects`, branch `codex/catching-up-to-upstream`, revision **`b2a5511cd7ff90646dd40c61f08cb27fa977e8f4`**. Zero behind its frozen target `046ae328114fe347bee6f9f90561cd8bc24ce787`. `assets.json` pins this fork revision and sibling checkout. All 4,454 tracked object files were rechecked against `bin/data/object` at handoff. The four kart friction gains remain data-owned at -6 dB; the nine dummy-car mappings used by the import change each have two empty cars in this pin.

The development object families alias the companion source tree: deployment must materialize their files. Do not mirror/delete through those aliases or replace the fork with an upstream archive. The installer verifies provenance and rejects conflicting assets. The pin is local/unpublished; availability to another fresh clone still requires publication. No push was performed.

## Remaining validation and separate debt

These are explicit limits of automated completion, not newly blocked port decisions:

- Native Vulkan UI/HUD/theme appearance, translated text, rain/water/palette and plugin image drawing, patrol selection above/below water, window dragging/dropdown interaction, editor chat visibility, and physical keyboard/gamepad focus transitions.
- Same-build multiplayer/replay, real user-profile persistence, representative long park simulation and performance comparison, and final owner manual testing before merge.
- Original S4 end-to-end imports and placement/operation of translated TD4 designs. The actual TD4/TD4AA stream fixture covers all nine affected objects plus controls, but does not place trains in an original park.
- Non-Windows and complete disabled-scripting builds. Windows scripting, UI and Vulkan builds pass. The objects exporter had five pre-existing dependency/target-framework warnings at its earlier checkpoint; pixel output was not independently rendered.
- Pre-existing `PatrolArea::clear` tile-count inconsistency (U308/B79): vectors clear without resetting the count. A separate behavior repair needs staff clear/repopulate/removal regression coverage and protocol assessment.
- Pre-existing `RideSetSettingAction` mode-boundary issue (U350/B96): unchecked values at least 64 can reach an invalid shift; the show-all cheat bypasses membership rejection. Preserve current policy during this typed-flags port; repair separately with action-boundary tests and protocol assessment.

## Overseer handoff

The worker source loop and required final automated validation are complete. The overseer owns independent final review, deployment/rollback to `D:\Games\Independent\OpenRCT2Mod`, and revision of `docs/vulkan-exclusive-migration-plan.md`; the owner owns manual acceptance before merge. The overseer reports review through U360 with no new blocking drift; U361 and this final evidence are ready for its next read.

Deployment has not been performed by this worker. The overseer has already identified stale base-install docs and blind `/MIR` in the old deploy-local script and will package current repository docs/declarations and materialized pinned objects with rollback separately. No branch was pushed or merged. Existing parent/overseer changes in Vulkan and oversight documents remain outside this worker's commits. Outbound messages were harness-blocked; this document is the durable completion notice for the scheduled review.
