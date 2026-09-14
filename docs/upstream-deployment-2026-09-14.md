# Upstream catch-up deployment — 2026-09-14

Deployed at **2026-09-14 14:40:04 UTC** to **`D:\Games\Independent\OpenRCT2Mod`** for owner manual testing.

- Tested source: `c6560d2679cd7871ba583236fd9515bb81955511` (U361), branch `codex/catching-up-to-upstream`.
- Frozen upstream target: `15d4b5e933555913d216f4548f673cd55cfb0579`; all 361 source receipts independently verified, **zero remaining**.
- Subsequent worker handoff commit `8617004e871a1d464d9fd7e7064b782141a94c70` changes documentation only.
- Historical stable tag: `stable-before-upstream-2026-09-13` → `e874ceb7708974e00040419d30390686c52a5d31`.
- Rollback copy: **`D:\Games\Independent\OpenRCT2Mod\backup-before-upstream-20260914-073936`**. The older `backup-before-deploy-20260702-063438` remains untouched.

## Validation and artifact identity

The final Release x64/MSVC 14.44.35207/Vulkan build passed with zero warnings/errors. The unfiltered suite passed **616 tests in 53 suites**, with zero failures, errors, disabled or skipped tests, in 57.077 seconds. This includes 41 GPU foundation tests and the actual hidden-window Vulkan lifecycle/indexed-readback test, which passed in 0.627 seconds. A post-receipt solution build also passed and confirmed the outputs remained current; no executable source changed afterward. See the [worker handoff](upstream-migration-handoff-2026-09-14.md) and [validation record](upstream-migration-validation-2026-09-13.md).

| Installed executable | Bytes | SHA-256 |
| --- | --- | --- |
| `openrct2.exe` | 21,312,000 | `1c997f3b030d096388f1e30358102094b587cf3ef8bedee3f72cd89f31fe791e` |
| `openrct2-cli.exe` | 16,840,704 | `b698f7705dc23468312f306c71658884b0c8523b8d3e7775f3178337fd62e333` |

The installed CLI was executed successfully with `--version`: **0.5.5**, network **0.5.5-andersonhk-11**, plugin API **122**, park version **60016**, minimum **57**. This version output does not embed the source SHA; build evidence and executable hashes establish source/artifact identity. Breakpad is disabled in this build.

## Packaging and rollback

Prepared a separate package containing **4,703 files**, then checked every staged hash and every existing managed file before deployment. Copied and hash-verified **4,677 previous files** into the rollback directory before overwriting the installation. Verified every installed package file afterward. No destination-only data files required removal, and no user-profile files, saves or config were part of this deployment.

The package uses current repository changelog, readme, scripting documentation and `openrct2.d.ts`, rather than the older base installation's copies. It contains data and the two runtime executables, not test executables, build logs or development libraries. All four development object-family junctions were materialized as ordinary files; the package contains no reparse points and does not depend on the checkout at runtime.

Objects remain pinned to the companion fork at **`b2a5511cd7ff90646dd40c61f08cb27fa977e8f4`**. Independently compared all **4,454 tracked object files** against the source checkout and checked the installed provenance marker and object-ID collision rules. No competing definitions or stale managed files were found. All four installed kart definitions retain nested `frictionSoundGainDb: -6`; the nine affected RCT1 import objects retain two empty cars. No asset patch or upstream-source substitution was used.

Local detailed evidence lives under `obj/upstream-audit`: `deployment-manifest.json` contains current/previous file hashes, `deployment-result.json` records the completed destination/backup, and `oversight-final-receipts.json` contains the independent 361-receipt audit. `prepare-deployment.py` and `deploy-verified-package.ps1` record the exact packaging/copy procedure. These are audit scratch, not runtime dependencies. The verified staged package remains at `obj/upstream-audit/deploy-package-c6560d2679`.

Rollback can restore the previous managed files from the backup and reconcile the 26 newly added paths using the previous/current manifests. Preserve unrelated user data and existing backup directories during any rollback; do not blindly mirror or delete the installation tree.

## Manual acceptance and next work

The owner accepted the deployed version as stable on **2026-09-14** after manual testing and the [engine/rumble audio audit](audio-regression-audit-2026-09-14.md). The catch-up was then merged into **`develop`** (this fork's main branch) by fast-forward from `e874ceb770` to **`f69067effaa97669b7b8d5ba5512dfa1ab346ea1`**. That checkpoint adds documentation and the repeated-sample test, with no production-code changes from the deployed receipt; redeployment was unnecessary. No push was performed. The [Vulkan exclusive migration plan](vulkan-exclusive-migration-plan.md) now records this accepted starting checkpoint; its implementation/parity gates remain open.

Retain these checks for further regression coverage; owner acceptance does not claim that each case below was individually exercised:

1. Existing parks: proportional age pricing, negative-G warnings, station capacities, sampled ratings and prepaid platform/seat behavior.
2. HUD/themes: news transitions, hidden/restored toolbars while paused, RCT1 status bar, old custom themes, enlarged/translated text and editor controls.
3. Rendering/plugins: water palettes, vehicle preview recolours, plugin custom images/caret behavior, patrol selection above/below water, and focus loss/regain with held keyboard/gamepad input.
4. Original RCT1 saves and wooden-mouse/other affected track designs: car counts, placement and operation; compare longer park performance and same-build multiplayer/replay when applicable.

Passing automated tests and indexed GPU readback do not establish full native visual parity or the absence of gameplay regressions. Real-profile persistence, original S4/placed TD4 scenarios, cross-platform builds and longer performance runs remain outside the completed evidence. The two pre-existing patrol-count/invalid-mode-boundary issues remain separately documented in the handoff; neither was silently changed by the migration.
