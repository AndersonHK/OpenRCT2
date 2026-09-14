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

## Progress: 3 / 361 source commits recorded

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
- **Fork receipt:** `this entry’s unique Upstream-Commit trailer`.
- **Remaining:** 359 → 358.
- **Disposition:** manually-ported.
- **Manual changes:** Updated Swedish half/quarter-helix wording and added existing upstream IDs 7023..7038 from this source commit only.
- **Additional decisions / behavior:** The Swedish file matched the source parent exactly, so adopted the inspected resulting blob. No fork ID collision occurs at or below 7038. Did not copy later upstream translation changes.
- **Verification:** Exact source-parent blob match before edit and exact resulting source blob match after edit; existing-ID range reviewed. No code changed. Exact-one-source ancestry gate.
- **Pending / concerns:** Language parsing is included in the next batch checks; source encoding and exact bytes preserved.
