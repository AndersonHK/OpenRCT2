# Development and documentation workflow

Start at the [library](README.md), read the topic owner and inspect the current source
and working tree. Preserve unrelated edits. Do not commit unless explicitly requested.
The repository's [agent instructions](../AGENTS.md) own local build, deployment and
artifact-retention requirements.

## Change and validate

Update the canonical contract with the code it explains. State what is implemented,
what remains proposed, what was measured and the limits of the evidence. Use targeted
tests for the changed behavior; do not promote helper-only checks to full gameplay,
visual or multiplayer acceptance.

Runnable game changes include a validated Release x64 build and deployment through
`scripts/windows/deploy-local.ps1` to `D:\Games\Independent\OpenRCT2Mod`.
After validating the current build, deploy with `-SkipBuild` and verify executable
hashes and the complete runtime data. See [Windows development](development/windows-local-build.md).
Documentation-only reorganization needs link, index and consumer-path validation;
it does not require a game rebuild or deployment.

## Keep history useful

Put specific contracts in topic folders. Archive completed handoffs, dated reviews
and superseded plans under an indexed collection with a link to their current owner.
Keep distinct unresolved failure evidence. Do not rewrite historical receipts to make
their old status or paths look current, and do not duplicate mutable status across pages.

The [documentation relocation map](development/document-relocations.json) maps original
paths to retained locations and records pre-move hashes. JSON and CSV evidence moved
byte-for-byte; paths inside those historical payloads retain their original meaning.
Resolve an old `docs/` path through that map when reading an older receipt. Markdown
navigation changed, so a historical Markdown hash identifies the original revision,
not the reorganized page. Git history supplies committed originals; the map is not
a backup of uncommitted content.

For new runs, tools consume the manifests under `docs/rendering/reference/`. Old
immutable run/source snapshots remain at their original artifact paths or in verified
archives; this documentation move does not rewrite or requalify them.

Run `python scripts/check_docs.py --check-indexes` after documentation edits. Also
check any changed script defaults against the moved files and inspect `git diff --check`.
For Vulkan artifact cleanup, follow [retention](development/vulkan-artifact-retention.md)
within the task's own scope. Never sweep another active task or the installed game.

[Library](README.md)
