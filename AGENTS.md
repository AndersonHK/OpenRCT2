# OpenRCT2 local instructions

Repository agent instructions, including the local Windows development/deployment defaults.

## Documentation library

- Start at `docs/README.md` and follow the topic indexes. Keep the top of `docs/` for general architecture, status,
  workflow and navigation; put subsystem contracts, procedures and reference data in the appropriate topic folder.
- Improve the existing canonical page before adding another. Every retained page and data file must be linked from its
  collection index. Link relevant source, related contracts and bounded evidence rather than duplicating mutable status.
- Lead with durable behavior and its status. Distinguish implemented contracts, proposals, rejected experiments, static
  inference and runtime evidence. Avoid one document per function, build or chat and long chronological diaries in current guides.
- Archive unique superseded reasoning, completed handoffs and dated acceptance evidence under `docs/archive/`, explicitly
  marked historical and linked to the current topic owner. Remove redundant prose after consolidation; avoid redirect stubs
  and repeated backups. Archiving a failure does not make it resolved.
- Personal saves, inspections, raw traces and generated task reports belong in an ignored task directory such as `.tmp/`
  or `obj/`, or the requested external destination. Retain only narrowly scoped benchmark/regression evidence needed for
  durable knowledge. Verify a destination is ignored before using it for ephemeral output.
- When moving pages, update incoming links, collection indexes and script producer/consumer paths. Preserve hash-bound
  historical JSON/CSV bytes and resolve their old documentation paths through `docs/development/document-relocations.json`.
  Do not rewrite immutable run/source snapshots. Run `python scripts/check_docs.py --check-indexes` before delivery;
  this checks file links and navigation, not heading anchors, factual freshness or runtime acceptance.

## Required deployment

For runnable game updates, completion includes building, validating and deploying the resulting version to
`D:\Games\Independent\OpenRCT2Mod`, unless the user explicitly asks to skip deployment.

- Use `scripts/windows/deploy-local.ps1` for deployment. After a successful build of the current changes, use `-SkipBuild`
  to deploy that validated build without rebuilding. Otherwise let the script build Release first.
- Deploy the matching executable, CLI executable and complete runtime data together. When changing companion object
  definitions, verify that the deployed objects include those changes; source edits alone are insufficient.
- The destination is **OpenRCT2Mod**. Do not deploy into the vanilla `D:\Games\Independent\OpenRCT2` installation.
- Verify deployment succeeded: compare the installed executable hashes with the build and check the deployed data,
  including changed object definitions. Report the destination and verification result in the final response.

## This machine's development build

The local toolchain is Visual Studio 2022, MSVC `14.44.35207`, Release x64. See `docs/development/windows-local-build.md`.
The development `bin/data/object` directories may link to the sibling object checkout. Its uncommitted changes cause the
pinned packaging installer to refuse installation. With dependencies already present, build the linked development setup
using `openrct2.sln` with `/p:IsSolutionBuild=true`, then deploy the validated output with `-SkipBuild`.

## Vulkan parity artifact retention

After every Vulkan parity task, clean up the scratch artifacts created by that task before calling it complete.
Follow `docs/development/vulkan-artifact-retention.md`; use the retention helpers and record reclaimed bytes and retained evidence.

- Preserve frozen references, original saves/owner images, source patches, distinct unresolved failures,
  selected accepted performance controls/candidates, build/run/deployment receipts, hashes, configuration and relevant logs.
- Successful parity runners prune full-capacity terrain `before.bin`, `parents.bin` and `commands.bin` only after
  validation and hash recording. Keep images, status/columns and reports. Failed or interrupted runs retain all evidence.
  Use `--retain-raw-artifacts` only for a selected reference or an investigation that needs complete readbacks;
  document the reason and retire redundant captures when the investigation ends.
- Successful private UI/screenshot builders remove their own `int` products by default. Preserve runnable outputs
  and library reuse inputs until the task's comparisons finish, then retire superseded build products using an explicit
  reviewed manifest. `--retain-build-intermediates` is for active compiler investigations, not routine retention.
- The incremental UI builder keeps only its active successful private cache generation after publication, under its lock.
  Never prune a cache while another build owns it.
- Recheck processes and changed files before cleanup. Validate absolute targets under this checkout, never traverse
  reparse points, and never blanket-delete `obj`, `bin`, `.git`, source, shared object checkouts or installed games.
- Keep a deletion/path-map receipt. Historical output hashes do not imply that discarded raw artifacts remain available.
  Update applicable docs when a historical evidence path loses its raw payloads. Do not accumulate complete backup trees.

## Task finalization for Vulkan evidence (part 2)

At the end of a Vulkan task, review the task's whole artifact directory, not only terrain capacity buffers.
There is no hard repository size target: preserve useful evidence first, then remove redundant expanded copies.

- Use `archive-vulkan-artifacts.py --scope <this-task-directory> --output <new-evidence-directory>` and
  `apply-vulkan-archive.ps1 -Plan <archive-plan.json>` for explicit task finalization. Never sweep another active task.
  Inspect the generated classification/index before applying, and use repeatable `--keep <selected-bin-or-reference>`
  arguments for task-specific live references that must stay expanded.
- Losslessly archive raw/indexed/RGBA captures, redundant PNGs, copied runtime/object data and historical executable/PDB
  pairs with SHA256 deduplication. Keep the original path map and restore helper. Archive verification must precede deletion.
- Keep source trees, unique source/patch files, original saves, configuration, logs, reports, owner images and selected
  unresolved-regression/reference cases. Generated project libraries and compiler intermediates may be discarded once
  their source/configuration is preserved; retain libraries still needed for an active task or selected reusable oracle.
- During investigation leave failed/interrupted outputs available. At task closure, review and archive their bytes rather
  than retaining every expanded copied profile indefinitely. Preserve distinct failure evidence, not merely a pass/fail label.
- Retire per-run data catalogs/profile copies only with verified payload preservation; never infer that an unknown input
  or a custom object is disposable. Shared link targets and installed packages remain outside cleanup.
- Record archive verification, selected/discarded bytes, original-path notices and final checkout size in applicable docs.
  Clean restore scratch before completion. Do not create a full uncompressed backup tree alongside the archives.
