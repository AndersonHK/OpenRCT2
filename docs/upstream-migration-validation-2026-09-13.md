# Migration validation

## Batch 01 — after U010

Source checkpoint: `c9446e6d6f178155a8680fcdc6dda5fb34b70a59` (10 receipts; 351 remaining).

- Release x64 MSVC 14.44.35207, Vulkan enabled: `openrct2.proj` build passed, exit 0. Fresh `test/tests/tests.vcxproj` build passed with 0 warnings/errors. Logs: `obj/upstream-audit/batch-01-build-fixed.log` and `batch-01-tests-build.log`.
- `PlayTests.ArbitraryRideTypeCheatRejectsOutOfRangeTypes`: passed against the freshly rebuilt executable. Rejects both `RIDE_TYPE_COUNT` and 255, preserves type, allows valid type with the cheat, and rejects it without the cheat. Expected rejection diagnostics are present.
- Objects v1.7.11 downloaded and independently verified: 11,071,539 bytes, SHA-256 `74e73bbd012339511bb359dd0fbb148fda899790ae37d7d69861d88183b19452`.
- Initial build attempts exposed host `PATH`/`Path` duplication and restricted dependency network access. A scratch build launcher normalizes environment key casing; the authorized dependency download and subsequent complete build succeeded. No product workaround was added.
- `LanguagePackTest.*` and `Localisation.*`: all 9 tests passed (parser and legacy encoding coverage). Log: `obj/upstream-audit/batch-01-language-tests.log`.
- Test startup reports 419 loose-object/parkobj conflicts in local `bin/data/object`; no pre-update runtime comparison was captured to date their appearance. The existing object precedence is preserved; no local object directories were deleted. The action test still passed, but these conflicts limit claims about the exact local object presentation.

Still pending: Best Staff runtime eligibility boundary coverage and map-edge visual checks in four rotations. Generic language parser tests do not replace a translated UI inspection. Crash-report upload and non-Windows packaging were not exercised.
