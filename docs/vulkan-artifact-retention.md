# Vulkan parity artifact retention

Parity work must leave bounded scratch storage and an auditable record of the result. Clean the task's own artifacts before reporting completion. Preserve independent frozen references, original failing saves and owner images, source patches, distinct unresolved failures, selected accepted performance controls/candidates, and relevant configuration, build/run/deployment receipts and logs.

The [part 2 cleanup](repository-storage-cleanup-part2-2026-10-14.md) expands finalization to raw captures, historical binaries and copied runtime data. The owner withdrew the strict 10 GB goal: preserve useful evidence while reducing redundant expanded artifacts. The automatic runner/build rules below still apply during a task; the finalization workflow handles its remaining artifacts.

## Automatic cleanup

`run-parity.py` and `run-terrain-draw-probe.py` now prune `before.bin`, `parents.bin` and `commands.bin` after successful qualification and the final input audit. They retain columns/status, indexed pixels, comparison images, reports, shaders and corpus inputs. A `scratch-retention-terrain.json` journal records each discarded relative path, byte count and SHA256. Hashes in earlier summaries describe the original run; discarded raw files are no longer available for inspection.

Use `--retain-raw-artifacts` for a selected full reference suite or an active guard/ABI investigation. Record that reason in the task's evidence. Failures retain every output automatically. Interrupted runs are not pruned. The dedicated terrain runner still reads and validates full buffers and guards before pruning; the full parity runner retains its existing, narrower qualification scope.

`build-current-ui-parity.py` and `build-screenshot-parity.py` remove their private `int` files after a passing receipt and input audit, and record them in `scratch-retention-build.json`. Runnable binaries, symbols, library reuse inputs, generated projects, receipts and logs remain. `--retain-build-intermediates` preserves successful compiler scratch for an active compiler investigation; failures always preserve it.

`build-incremental-ui-parity.py` retires files in obsolete private cache generations after successfully publishing its next state, while holding its exclusive cache lock. The current workspace remains reusable. The immutable output snapshot receives `scratch-retention-cache.json`; previous cache-generation hashes are historical evidence, not a promise that the retired workspace remains present. Empty scratch directories may remain; they do not retain payload bytes.

Automatic cleanup affects only the caller's selected scratch subtrees inside `obj/vulkan-parity`. It does not prune sibling runs or independent references. Containment checks reject root, external paths, traversal and reparse points. Descendant junctions and symlinks are never followed. Journals are written before deletion and marked incomplete if pruning fails; qualification receipts are not rewritten to hide missing evidence.

## Retiring historical build products

After a task's comparisons finish, retain the selected runnable oracle/candidate and any library reuse inputs still needed. Retire superseded private build products explicitly. Do not let each run accumulate another full source/runtime copy or every older intermediate. Keep one verified rollback with its matching executables, shaders, data and provenance.

The historical planner is scoped to the families examined in the October 14, 2026 audit:

```powershell
python scripts/rendering/cleanup-historical-artifacts.py --output obj/vulkan-parity/storage-cleanup-NEW-ID
.\scripts\rendering\cleanup-historical-artifacts.ps1 -Plan C:\absolute\repo\obj\vulkan-parity\storage-cleanup-NEW-ID\deletion-plan.json
```

The planner creates a concrete leaf-file manifest and metadata/path index without deleting anything. Review its selected roots and bytes first. Its historical keep list is specific to the audited machine; it is not an evergreen definition of the latest accepted build. The applier checks namespace/links, file sizes and modification times, retained metadata hashes, active parity processes and private-cache locks before deleting. It deletes leaf files with native PowerShell, never recursively traversing a directory. It invalidates retired mutable caches, preserving their old state as small metadata records, and records completed bytes/files and volume free space in `deletion-result.json`.

Current `bin`, normal `obj` intermediates, dependencies, Git history, source, test inputs, shared object checkouts and game installations are outside that cleanup's selection. During concurrent work, inspect build command lines too; a bare MSBuild process name cannot identify which repository it is building. Never clean a private cache that another build owns.

Keep historical metadata and unique inputs at their existing paths where practical. For moved evidence provide a path map. When raw payloads are discarded, add a retention notice to the affected historical artifact root and document it in the audit; an old artifact link must not imply that the full raw capture still exists.

## Checks

Run `python scripts/rendering/test-artifact-retention.py` after retention changes. It checks failure/reference preservation, successful payload pruning with hashes, runnable/library preservation, root/traversal rejection and actual Windows junction containment. Validate edited runner/builder command-line help and Python syntax. A tooling-only storage change does not need a game rebuild/deployment or establish new GPU parity/performance results.

## Task finalization and restoration

The scoped archive workflow prepares content-addressed ZIPs for raw captures, PNGs, copied runtime/object data and
historical executable/PDB/DLL payloads. Every unique archived entry is inflated and checked against its full SHA256
before the plan becomes applicable. Duplicate original paths map to the same preserved payload. All source/patch/save/log
files and selected live cases stay in place; only identified generated compiler products are recorded as discarded.

```powershell
python scripts/rendering/archive-vulkan-artifacts.py --scope obj/vulkan-parity/THIS_TASK --keep obj/vulkan-parity/THIS_TASK/bin --output obj/vulkan-parity/THIS_TASK-evidence
.\scripts\rendering\apply-vulkan-archive.ps1 -Plan C:\absolute\repo\obj\vulkan-parity\THIS_TASK-evidence\archive-plan.json
python scripts/rendering/archive-vulkan-artifacts.py --output obj/vulkan-parity/THIS_TASK-evidence --restore obj/vulkan-parity/THIS_TASK/path/to/capture.bin --destination obj/vulkan-parity/restore-scratch/capture.bin
```

Inspect the index before apply. Use scope to affect only the completed task, and use repeatable `--keep` paths to retain selected reusable bins/reference
inputs explicitly. Do not finalize another active task or a cache with an exclusive lock. The native PowerShell applier
verifies archives/index hashes, validates every original size/time and ancestor before removing leaf files, and records
removed bytes and free space. It does not traverse or delete directory links. Preserved byte identity does not establish
fresh GPU qualification, and archive restoration may be needed before historical runners can consume old path references.

Keep an `archived-retention.json` notice and catalog for affected roots. The index can be gzip-compressed after apply;
the restore helper accepts either form. Do not remove the archives/index while their payloads are the only copies. Clean
only newly created restore scratch after verification. Whole source-tree retirement remains a separate review because
modified drivers or data may be present only in an extracted tree.
