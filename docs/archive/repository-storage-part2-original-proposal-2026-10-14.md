Historical proposal. The owner later withdrew the strict size target. See the [executed Vulkan cleanup](../repository-storage-cleanup-part2-2026-10-14.md) for the current scope and results.

# Storage cleanup, part 2: below 10 GB

**Planning only. No further artifacts were deleted, no retention code or agent rules were changed, and no commit/push was made in this turn.** This supersedes part 1's conservative retention choices for a future approved cleanup. The goal is a working checkout plus a small evidence collection, rather than preserving the directory contents of every historical experiment.

The hard limit is **10,000,000,000 bytes**, not 10 GiB. The proposed retained budget is **7.3 GiB = 7.84 GB**, leaving about 2.16 GB below that limit. From the current 117.655 GiB, that means removing approximately **110.36 GiB more**, around 94% of what remains. Installed games and external/shared object checkouts are outside this operation; relocating 100 GB to another backup directory is not the proposed solution.

## Revised authorization

The owner subsequently authorized cleanup of the remaining Vulkan artifacts while withdrawing the strict size target and small-archive budget. The implementation preserves useful bytes through verified deduplicated archives, retains selected live evidence/source trees, and leaves ordinary build outputs, the crash dump, `artifacts`, dependencies and installed games outside this pass. The budget below remains the original proposal, not the applied deletion policy. Final measured results will be recorded separately.

## Why part 1 was insufficient

Part 1 removed 157.905 GiB from a narrow set of repeated passing terrain buffers and historical UI/screenshot compiler products. It intentionally kept other historical builds, captures, copied runtime profiles, failed/incomplete runs and one complete raw terrain suite. That reduced the checkout to 117.655 GiB but left almost the entire long tail of experiment directories.

A fresh link-excluding scan finds **126,331,523,838 bytes / 117.655 GiB**, **793,077 files** and **2,040 excluded reparse points**. The remaining `obj/vulkan-parity` is **108.831 GiB**. These mutually exclusive categories account for it:

| Remaining Vulkan contents | GiB | Action |
| --- | ---: | --- |
| Raw `.bin`, `.indexed`, `.rgba`, `.idx` captures/readbacks | 56.44 | Losslessly archive selected complete diagnostics; discard the other raw outputs after indexing outcomes |
| Private compiler/runnable products | 30.62 | Keep three selected usable oracle/candidate bins and selected performance snapshots; discard other libraries, executables, symbols and intermediates |
| Runtime/object data copies | 10.93 | Preserve the canonical dependency/assets and unique required custom objects once; remove per-run copies and extracted image catalogs |
| PNG images outside runtime/object data | 5.16 | Retain current unresolved cases and a bounded accepted corpus; discard routine repeated captures and generated previews |
| Mixed metadata, sources and inputs | 4.68 | Compress metadata, deduplicate actual input files, preserve unique source changes, retire extracted reproducible source copies |
| Other | 1.01 | Classify before deletion; preserve unique inputs/code, discard generated payloads |

Outside that tree, `artifacts` is 1.974 GiB and other `obj` contents are 4.740 GiB. Current `bin`, `lib`, `.git`, source, tests, resources, docs and root files together are only **2.110 GiB**. We can keep those intact.

The [measurement JSON](../repository-storage-part2-measurements-2026-10-14.json) records the fresh scan, disjoint categories and preservation measurements. Logical file lengths exclude linked targets and do not constitute a physical allocation audit.

## Measurements that make a small archive credible

Read-only ZIP compression was measured with `ZIP_DEFLATED`, level 1, writing to a counting stream. It did not create or extract archives or perform an archive round-trip check.

| Preservation set | Original | Measured ZIP / unique contents |
| --- | ---: | ---: |
| Entire `run-62`, all 2,109 regular files | 6.511 GiB | **82.27 MiB ZIP** (86,263,637 bytes) |
| All 5,954 immediate historical metadata files | 1.211 GiB | **250.24 MiB ZIP** (262,397,653 bytes) |
| All 1,929 Vulkan save/track/patch files examined | 755.95 MiB | **20.77 MiB**, 125 unique full-file SHA256 values |

The metadata set includes immediate `.json`, `.log`, `.xml`, `.txt`, `.csv`, `.patch`, `.py` and `.ps1` files, rather than every JSON in object catalogs or profile copies. Relevant nested reports/logs must also be kept with the selected cases. The input deduplication covers `.park`, `.sc6`, `.sv6`, `.sc4`, `.sv4`, `.td6` and `.patch`; it is not a deduplication claim about every custom object or source file.

The complete run62 suite can therefore remain fully inspectable without occupying 6.5 GiB expanded. Identical saves need one stored payload with all original names, paths and receipts mapped to it. Distinct original and converted saves remain distinct objects when their hashes differ.

## Retained-size budget

These are selection limits, not claims that the proposed packages already exist. Enforce each cap during packaging and remeasure the complete checkout. If a category exceeds its cap, first retire superseded accepted cases, repeat captures and extra build generations. Do not silently discard unique uncommitted code, original failing inputs or unresolved failure evidence to hit a number.

| Retained category | Budget, GiB | Scope |
| --- | ---: | --- |
| Current working tree, `.git`, complete current `bin` and `lib` | 2.12 | Measured at 2.110 GiB; preserve ordinary source/data/test/resource files and dependency links |
| Original crash dump | 1.45 | Keep `obj/world-surface-hang.dmp` without requiring its diagnosis to finish first |
| Frozen software reference package/source archive | 0.18 | Keep `reference-9a092745f3` intact; measured 0.174 GiB |
| Three selected runnable oracle/candidate bins | 1.00 | `ui-frozen-build-24/bin`, `ui-build-36/bin`, `screenshot-link207/bin`, including pinned libraries, symbols and required runtime inputs |
| Compressed historical metadata and path indexes | 0.30 | Measured top-level ZIP is 0.244 GiB; preserve originals byte-for-byte inside archives |
| Compressed complete terrain reference and selected failing diagnostics | 0.30 | Entire run62 needs 0.080 GiB; use remaining budget for distinct failures/ABI cases after compression measurement |
| Deduplicated saves/tracks/patches | 0.05 | Measured examined unique set is 0.0203 GiB; include unique additional task inputs |
| Current regression/accepted visual corpus | 1.00 | Full images, paired originals/native outputs, indexed/palette evidence where needed, coordinates/configuration and nested review reports |
| Selected performance experiments and one rollback | 0.50 | Exact executable/shader/input provenance, timing rows, checksums and final images; one usable rollback, not every deployment backup |
| Unique historical source changes/custom requirements | 0.10 | Preserve source diffs/additions and any unique small custom dependencies; count large essential objects in the relevant fixture budget |
| Index/NTFS allocation/temporary growth margin | 0.30 | Allow small-file allocation overhead and new compact metadata |
| **Total** | **7.30** | **7.84 GB**, below the strict 10 GB limit |

This keeps the crash dump and current build, and does not depend on deleting Git history or installed assets. Normal build intermediates outside `bin` are rebuildable and do not fit the retained set. Their removal means the next ordinary solution build will recompile; `bin` remains usable in the meantime.

## Concrete evidence to keep

Keep the independent frozen reference unchanged, with its manifest, source archive and original package. Retain the selected runnable bins at their original paths where practical so existing artifact receipts can still bind to them. Do not preserve every extracted frozen/upstream source checkout: archive the unique driver/additions/diff plus the exact base revision or pinned source archive instead. Establish that reconstruction is possible before removing a source tree; a receipt's commit hash alone is not enough if modifications exist only there.

Keep `ordering187-fixtures` in full, including owner images and original/converted exact saves; `slopes191-withdrawn/candidate.patch`; the reviewed slope failure; the Trinity/Corkscrew paired cases; Lift admission/cage/deck comparisons; save-loading crash evidence; and the latest guest/shop control corpus. Initial measurements excluding copied runtime/profile/bin/source subtrees show:

| Current case set | Relevant evidence measured |
| --- | ---: |
| `guest-shop207*` | 229.02 MiB, versus 0.722 GiB for the complete directories |
| `slopes191-candidate-01` | 15.89 MiB |
| `trinity190-paired-01` and `corkscrew190-paired-01` | 20.05 MiB combined |
| `lift198-paired-01` and `lift198-texas-paired-01` | 31.56 MiB combined |
| Three selected recent performance runs | About 24 MiB each outside profiles |

Those exclusions are sizing aids, not a deletion rule: an isolated profile or `source` subdirectory can contain a unique input or log. Inspect and extract it first. The listed mandatory visual cases leave substantial room inside the 1 GiB visual budget for other distinct open failures and representative accepted families. Keep all rotations/positions that establish each selected result; do not reduce an unresolved multi-view defect to one screenshot.

For performance, retain matched control/candidate measurements such as the guest204/guest207 pair, their exact binary/shader provenance and the representative terrain baseline. Keep original timing rows, census/checksum, configuration, input/camera and final image. Superseded exploratory profiles and 200 historical executable snapshots are not necessary to explain the selected results.

Preserve all top-level historical outcomes, including failures and incomplete runs, in compressed metadata archives. Keep complete raw evidence for the selected unresolved cases and compressed diagnostic examples. Other historical failures may retain their outcome, input identity and explanatory report without every buffer or copied profile. The index must explicitly distinguish those cases from fully retained/replayable evidence.

## Execution sequence for approval

1. **Freeze the keep set.** Refresh the inventory and active-task/build check. Create a keep manifest with original relative paths, hashes, preservation role, archive entry and whether evidence remains complete, compressed, metadata-only or discarded. Confirm current executable/dependency/object inputs and the rollback selection. Make a list of unique changes in every historical source tree/private driver before proposing its removal.
2. **Package first.** Create the complete terrain ZIP, metadata ZIPs and content-addressed unique input store inside a new compact evidence area. Package semantic families separately enough to extract one regression without restoring the entire corpus. Keep a small catalog in docs; large local licensed-art/evidence archives remain ignored. Use existing shared dependencies and record their identity rather than copying them into every package.
3. **Verify preservation.** Test archive CRCs and compare a streaming SHA256 of every archived entry with its original. Verify the indexed original paths/counts, original/converted save identities, required custom objects, selected binary/library/shader hashes and package capacities. CRC alone is insufficient. Confirm selected source reconstruction and at least one fixture replay from the curated package before deleting the old tree. Expand only one package into bounded scratch space, then retire that scratch.
4. **Delete by the keep manifest.** Remove all non-kept regular payloads under `obj/vulkan-parity` and retired `artifacts` profiles/builds. This is whole-tree selection by explicit retained role, replacing part 1's narrow filename-family selection. Remove ordinary compiler intermediates under `obj` while preserving the dump, compact evidence and unique inputs. Retire the 1.617 GiB Texas private build after extracting its reports and unique source changes. Delete redundant source/runtime copies and old deployment backups after the retained originals and rollback verify. Do not traverse junctions or remove shared targets.
5. **Retire old expanded paths safely.** Record compressed/metadata-only replacements in the catalog and update important documentation links. Historical receipts stay immutable; archive/location metadata lives beside them or in the catalog. Remove empty historical directories and obsolete cache state only after confirming they contain no unique files or links still needed. Do not change the existing installed games.
6. **Enforce the final limit.** Verify absence of all selected discarded payloads and presence/hash validity of the keep set. Measure logical lengths and actual allocation with links counted once or excluded consistently. Completion requires **less than 10,000,000,000 bytes**, with the 7.84 GB budget as the working target. If selected evidence exceeds the budget, stop before destructive pruning of unique material and report which accepted extras or optional snapshots need retirement.

The archive originals coexist only during verified migration. Delete those originals after verification; do not leave an additional complete backup tree behind. The expected net recovery is approximately 110 GiB. Exact recovered bytes require the finalized keep/deletion manifests.

## Rules/tooling that need changing in part 2

The current automatic rules prevent some repeat growth but preserve every failed run and most private runnable outputs indefinitely. The next implementation should add a **task-finalization** step:

- During diagnosis, failed/interrupted outputs remain untouched. At task closure, preserve the selected complete failure/input/report set, archive or index other failures, and remove their redundant copied runtime/capacity buffers. No unattended age-based deletion of unresolved failures.
- Successful routine captures are compacted into selected images/reports and hashes. Accepted milestones use the curated corpus; routine prior successes do not each become permanent full runs.
- Private builders may retain library reuse inputs during a task, but finalization keeps only the selected oracle/candidate bins and at most one active cache generation. Explicitly retained libraries/symbols count toward the size budget.
- Store fixtures/assets once and reuse read-only links. Collect any unique save, source patch, custom object or profile log before removing a profile. Do not infer that all profile files are disposable.
- Add catalog-aware archive verification/extraction and cleanup to the retention tooling; the existing part-1 historical executor must not simply be pointed at arbitrary additional files. Default planning/dry-run remains separate from apply.
- Update `AGENTS.md` with the strict repository size target and required task finalization after the approved curated set is established. Routine task closure should restore the settled checkout below 10 GB; any temporary full build/diagnostic excursion should be reported and cleaned before completion.

The cost of this plan is loss of immediate access to most old expanded builds and intermediate screenshots. Selected evidence remains inspectable, historical outcomes remain documented, and dropped raw outputs are labeled honestly. There is enough measured compression/deduplication headroom to keep the current game build, dependencies, Git history, crash dump, independent references and current regressions within the target.
