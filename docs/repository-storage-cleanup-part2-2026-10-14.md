# Vulkan artifact cleanup, part 2

The owner authorized cleanup of the remaining Vulkan artifacts and withdrew the strict 10 GB target. This pass preserves useful evidence, source changes and selected live references while retiring redundant expanded payloads. No game runtime source, current build/dependency tree, ordinary `obj` outputs, `artifacts`, crash dump or installed game was changed.

## Result

The checkout fell from **117.655 to 22.117 GiB**, a net reduction of **95.538 GiB**. `obj/vulkan-parity` fell from **108.831 to 13.291 GiB**. Sizes are link-excluding logical lengths; the scan still excludes the same **2,040 reparse points**. There is no enforced 10 GB target.

**576,389 expanded files / 100.787 GiB** were retired. Four content-addressed ZIPs preserve **573,913 original paths** as **25,513 unique payloads**, occupying **5.220 GiB**. **2,476 generated compiler products / 16.112 GiB** were discarded outright. The compressed original-path index and small metadata/notices account for the difference between gross retirement and net recovery.

| Selected contents | Expanded GiB retired | Preserved archive GiB | Original files |
| --- | ---: | ---: | ---: |
| captures | 4.999 | 1.550 | 89,004 |
| raw | 56.246 | 0.741 | 48,506 |
| binaries | 13.514 | 2.841 | 619 |
| runtime | 9.916 | 0.088 | 435,784 |
| discard-compiler | 16.112 | Discarded generated products | 2,476 |

The deletion phase observed 101.938 GiB of additional volume free space after the archives had already been written. That observation includes filesystem allocation effects and possible concurrent activity; it is not the net whole-operation figure. The logical before/after scan is the reported 95.538 GiB net reduction.

The machine-readable [cleanup result](repository-storage-part2-cleanup-2026-10-14.json) contains counts, archive hashes, verification results, the preserved executables and final top-level sizes. The [measurements](repository-storage-part2-measurements-2026-10-14.json) describe the pre-cleanup tree. The [original size-budget proposal](archive/repository-storage-part2-original-proposal-2026-10-14.md) is archived as superseded context.

## What remains useful

All inventoried files outside the deletion index remain in place: **166,050 retained Vulkan files** were checked against their original sizes and modification times. Source trees, source patches, saves, configuration, logs, reports and unknown/unclassified files were retained. Source/extracted test/resource content was not treated as disposable copied runtime data. No source-tree reconstruction assumptions were used to delete code.

The frozen `reference-9a092745f3` package remains expanded and unchanged. Selected runnable bins in `ui-frozen-build-24`, `ui-build-36` and `screenshot-link207` remain expanded with their reusable libraries/symbols. Original owner images/exact saves, the withdrawn slope patch and selected slope/Trinity/Corkscrew/Lift/save-loading cases remain directly available. The latest guest/shop207 cases remain expanded. All other selected captures, PNGs, runtime/object copies and historical executable/PDB/DLL payloads are retained byte-for-byte in archives rather than reduced to a pass/fail label.

Only recognizable generated project libraries and compiler intermediates lack an archived payload. Their hashes and original paths remain indexed; preserved source/configuration is needed to rebuild them. A historical receipt's artifact hash describes its original expanded output and is not proof that the old path is still present. Restoring a historical binary does not automatically restore every discarded library reuse input or establish replay qualification.

## Local archive and restoration

The local evidence directory is `obj/vulkan-parity/evidence-2026-10-14-pass2`:

- `raw.zip`, `captures.zip`, `runtime.zip`, `binaries.zip`: payloads named by their full SHA256, deduplicated within each category.
- `payloads.tsv.gz`: original relative paths, original sizes/times, SHA256, preservation/discard role and archive entry. The plain index was removed only after lossless gzip verification.
- `archive-plan.json`, `archive-result.json`, `index-retention.json`, `postcleanup-checks.json`: original selection, verification, removal and index records.
- `root-file-retention.json`: the four directly archived namespace-root files; affected directories have `archived-retention.json` notices.

The archives/index remain local and Git-ignored; no licensed artwork, saves or binaries were pushed. The original metadata receipts remain immutable. Restore only the needed payload into bounded scratch, inspect it, then clean that scratch:

```powershell
python scripts/rendering/archive-vulkan-artifacts.py --output obj/vulkan-parity/evidence-2026-10-14-pass2 --restore obj/vulkan-parity/run-62/terrain-draw-samples/guards/0/before.bin --destination obj/vulkan-parity/restore-scratch/before.bin
```

Restoration verifies full SHA256 and byte count and restores the original modification time. It supports the gzip index. **103,971 empty generated directories** were removed after retirement; source/Git/resource/test directories, links, protected cases and the private cache root were excluded from that pruning.

## Verification and future workflow

- Every unique archived payload was inflated and verified against SHA256 before deletion. Archive and index hashes were checked again by the native executor.
- All 576,389 selected originals are absent; all 166,050 unselected inventoried Vulkan files retain their original size/time. Current `bin` and installed `D:\Games\Independent\OpenRCT2Mod` executable/CLI hashes match the pre-cleanup values and each other.
- Full frozen-reference verification passes: 4,730 package files, its manifest/source archive and ten protected archived sources.
- The actual 66,029,600-byte run62 guard was restored twice, including using the compressed index: its SHA256 matches and every byte is `0xA5`. Verification scratch was removed.
- Four archive tests and seven existing retention tests pass, including a real native PowerShell apply, deduplicated restoration, archive-corruption rejection, explicit live-bin retention, Windows junction containment and subsequent incremental snapshot reuse. Edited Python entry points and PowerShell syntax/CLI checks pass.

`AGENTS.md`, [artifact retention](vulkan-artifact-retention.md) and the relevant runner/builder help now require scoped task finalization after tests/build comparisons. The prepare/apply/restore tools support `--scope` and repeatable `--keep` paths. Failures remain expanded during investigation; at task closure their useful bytes can be archived instead of accumulating full copied profiles indefinitely. Selected reusable bins and original inputs must be kept explicitly. No hard size cutoff may justify discarding unknown useful material.

This was storage/tooling work. It establishes preservation and cleanup, not fresh GPU parity, renderer acceptance or gameplay performance results; no rebuild or deployment was required.
