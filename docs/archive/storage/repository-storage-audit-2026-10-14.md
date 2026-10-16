# Repository storage audit and executed cleanup — 2026-10-14

> Historical record. Status, instructions and acceptance claims below apply to the recorded checkpoint. Start with the [current development guide](../../development/README.md) for ongoing work.


The initial read-only audit and proposal below are preserved as the before snapshot. The owner subsequently authorized cleanup, script/agent-rule changes, a detailed commit and push. See the executed cleanup section for the measured result. No game runtime source or installed package was changed.

The follow-up [executed part 2 cleanup](repository-storage-cleanup-part2-2026-10-14.md) records the later result. The original size-target proposal below is historical; the owner withdrew the strict 10 GB target.

## Measurement

Scanned `C:\Users\imper\Documents\GitHub\OpenRCT2`, including hidden files and `.git`, without following directory or file reparse points. Regular-file lengths total **295,873,659,814 bytes = 295.87 GB = 275.55 GiB**, across **813,695 files**. Sizes below use GiB (1,073,741,824 bytes). The companion [directory inventory](repository-storage-directory-sizes-2026-10-14.csv) gives directory rollups through depth three, file counts, modification-date ranges and documentation references.

These are logical file lengths, not an NTFS allocation/free-space measurement. Hard links, compression, sparse files and allocation rounding can affect physical recovery. Three checked large dumps had one link each and ordinary archive attributes; that is sample evidence, not a whole-tree allocation audit. Link targets are excluded: test profiles contain many links to shared runtime assets, which must not be counted repeatedly or deleted through a link. Modification times describe contents and can retain original asset timestamps; age alone is not proof that a directory is disposable.

| Location | GiB | Files | Interpretation |
| --- | ---: | ---: | --- |
| `obj/vulkan-parity` | 266.73 | 763,095 | 96.80% of the entire checkout: accumulated renderer qualification outputs, private builds and profiles |
| Other `obj` contents | 4.74 | 13,640 | Ordinary build intermediates, upstream audit checkout and a crash dump |
| `artifacts` | 1.97 | 18,438 | Mostly the Texas routing private build (1.62 GiB) |
| `bin` | 0.92 | 477 | Current runnable build, symbols and local runtime files; linked files excluded |
| `lib` | 0.65 | 758 | Downloaded build dependencies |
| `.git` | 0.37 | 4,705 | History is a minor contributor; `git count-objects` reports 311.42 MiB packed and 59.01 MiB loose objects |
| Everything else | 0.17 | 12,582 | Source, docs, tests, resources, scripts and root files |

The Vulkan evidence tree has **2,160 immediate child directories containing regular files**. Its newest scanned file timestamps are September 28, 2026; this is an accumulation of prior experiments, not just the current build. No OpenRCT2, tests or MSBuild process was returned by the process check during the audit; recheck before any later cleanup.

## What actually occupies the space

| Artifact family | GiB | Finding |
| --- | ---: | --- |
| Numbered `run-*` directories | 126.67 | Repeated parity runs; terrain draw readbacks dominate |
| `ui-build-*` | 21.50 | Historical private UI compilation outputs |
| `screenshot-link*` | 13.04 | Historical screenshot compilation/link outputs |
| `terrain-draw-probe-*` | 10.26 | Standalone terrain probes, also carrying large readbacks |
| `ui-frozen-build-*` | 8.43 | Multiple frozen UI oracle builds; retain an independent usable reference |
| `giant-cli-vulkan-*` | 5.31 | Giant screenshot runs and private runtime copies |
| `incremental-ui-cache` | 4.79 | Multiple generations of compiler cache |
| `ui-cache-build-*` | 3.73 | Additional historical UI build outputs |
| `upstream-building-fixture-build-*` | 3.24 | Independent upstream fixture compilation outputs |
| `vulkan-only-giant-*` | 2.66 | More giant screenshot runs |
| `giant-cli-software-*` | 2.25 | Software giant screenshot comparison runs |
| `giant-cli-frozen-*` | 2.08 | Frozen giant screenshot comparison runs |

Across the whole checkout, `.bin` files occupy **146.13 GiB**, `.lib` 37.74 GiB, `.pdb` 16.94 GiB, `.pch` 11.28 GiB, `.exe` 11.05 GiB and `.obj` 8.68 GiB. Raw `.indexed`/`.rgba` captures together occupy 17.57 GiB; PNGs occupy 6.14 GiB. These extension totals overlap the directory totals and must not be added to them. Runtime `data` subtrees within Vulkan experiment directories account for about 9.76 GiB of regular-file contents; shared link targets were excluded.

### The largest source: repeated terrain buffer dumps

`run-44` through later runs frequently occupy about 6.5 GiB each. In `run-62`, **6.225 GiB of its 6.511 GiB** is `terrain-draw-samples`; the rest includes image comparisons, fixture corpus, other probes and receipts. Across 21 numbered runs, `terrain-draw-samples` occupies **118.104 GiB**.

`scripts/rendering/run-terrain-draw-probe.py` validates 50 samples. Per sample it expects:

- `columns.bin`: 2,080 bytes;
- `parents.bin`: 34,078,720 bytes;
- `commands.bin`: 31,948,800 bytes;
- `before.bin`: 66,029,600 bytes, entirely the guard byte `0xA5`.

Those four files alone total 132,059,200 bytes (125.94 MiB) per sample, or 6.15 GiB for 50 samples before images/reports. Most allocated capacity is checked for untouched guard bytes. This is diagnostic capacity readback, rather than meaningful new scene content in every byte.

Checked `terrain-draw-samples/guards/0/before.bin` in runs 59, 60 and 62. All three are separate 66,029,600-byte files with SHA256 `46d374fe91e1e4ac7c1600e62779b03b63e77e594ceaca36bdc78bac756322d8`. This establishes duplication for those files only; the other GPU outputs were not exhaustively hashed for equivalence. Run59's summary reports failure; run62 reports pass. Preserve failed-run metadata as well as successful receipts.

## Proposed cleanup order

### 1. Preserve evidence before removing payloads

Create one compact evidence archive, with a path map from old artifact paths to retained locations. Do not duplicate the entire 266 GiB tree as a backup. Record each selected deletion's original relative path, byte count, reason, retained replacement and any existing hashes. Hash retained files; the existing run summaries already record many payload and input hashes. Extracting metadata does not make a removed raw output independently re-inspectable: mark it explicitly as discarded.

Keep these classes:

- Build/run `receipt.json`, `summary.json`, test XML and relevant logs; deployment receipts and existing preimage hashes; source revision, working-tree diff fingerprint, untracked-source hashes, build commands/tool versions, shader hashes, validation settings and failures.
- Exact test configuration, command line, camera, resolution, tick/seed, asset/object revisions and manifests. Keep original failing saves and converted copies with conversion receipts; do not rely on a current autosave to reproduce an old failure.
- One full independent frozen software/original-art reference package and its source archive: `reference-9a092745f3` is only **0.174 GiB**, documented by `docs/rendering/reference/vulkan-software-reference.json`. Preserve the upstream oracle source/driver and required object/assets, including externally linked dependencies or their verified provenance.
- Current `bin` and runtime data, the actual deployed-version provenance and one usable rollback package with matching executables/shaders/data. Documentation's build199 checkpoint and build204 receipt do not alone identify September 27's current `bin`; verify current hashes before choosing a rollback. Historical `build-*` folders can be tiny receipts rather than binaries: build199 and build204 are each under 0.4 MiB.
- Original images and exact saves in `ordering187-fixtures` (**6.58 MiB**), withdrawn patches such as `slopes191-withdrawn/candidate.patch`, and paired reference/candidate/difference crops for open guest/shop, sloped-rail, support, vehicle and Lift cage/deck defects. Consult the current depth/contact and lift documents before pruning them.
- For performance milestones, keep one complete counterbalanced control/candidate experiment: its exact input, binaries/shaders, configuration, timing/profile rows, initial/final census/checksum, receipt and final screenshot. Preserve distinctions between headless tests, application draw cadence and displayed pacing.

Do not keep every JSON or PNG indiscriminately. The tree contains 287,086 JSON files and 365,780 PNGs, including generated object/profile material and repeated images. Retain evidence by role and case, not just extension. Likewise, preserve ignored patches and scripts that may be the only record of an uncommitted experiment before discarding a private build directory.

### 2. First pass: remove repeated terrain payloads

Keep one complete 50-case terrain suite, provisionally `run-62/terrain-draw-samples`, with its corpus/probe provenance, shaders, input hashes and receipts. Confirm it remains the selected oracle for the desired historical ABI. Preserve every other run's outcome and failure details; retain any distinct failing-case output before pruning its raw dump directory.

Removing the other 20 numbered-run terrain draw payloads would recover **111.879 GiB**, minus any separately retained failure samples. This calculation is 118.104 GiB total less run62's 6.225 GiB, not a hash-proven declaration that every output is identical. The full reference suite can be compressed after a verified round trip; no compression ratio was measured here.

Keep representative complete outputs for normal drawing, empty/overflow rejection, inactive-column guards, capacity limits and generation/reuse transitions. A single pretty screenshot does not preserve those distinct test purposes.

### 3. Second pass: historical compilation products and extra probes

The `ui-build-*`, `ui-frozen-build-*`, `ui-cache-build-*` and `screenshot-link*` families total **46.706 GiB**. Keep their receipts and source patches, one usable independent UI oracle, any explicitly selected regression executable, and current dependencies. Remove superseded `int`, `.obj`, `.pch`, `.lib`, link scratch and redundant runnable snapshots after that check.

`incremental-ui-cache` adds **4.794 GiB** and `terrain-draw-probe-*` adds **10.259 GiB**. Retain probe receipts and unique diagnostics, and establish that the full terrain suite selected above covers the retained ABI before removing standalone readback copies. Recreating caches costs build time; it does not usually require preserving every cache generation.

Together with the first pass, these named non-overlapping candidates have a **173.64 GiB gross ceiling**, before retained references/failures/metadata. A practical initial target is approximately **150–170 GiB reclaimed**, leaving roughly **106–126 GiB**. This is a planning range, not a verified deletion manifest or guaranteed physical free-space increase.

### 4. Further pruning after the first passes

Deduplicate giant screenshots, repeated frozen/current/upstream captures, private runtime copies and large-park experiment profiles. Keep one canonical sample per distinct fixture/camera/rotation/zoom/render mode, plus all evidence for unresolved failures. Use content hashes to recognize actual duplicates, and retain both sides of comparisons that differ. Latest passing samples can supersede routine intermediate captures; historical failure/fix pairs retain explanatory value.

Remove the Texas private build in `artifacts/texas-routing-build` (**1.617 GiB**) after retaining its receipts, relevant build/deployment logs, autosave/scenario inputs, regression analyses and candidate source patches. Keep one actual rollback rather than all installation backups. The rest of `artifacts` is only 0.36 GiB and is lower priority.

Other optional candidates are ordinary rebuildable `obj` intermediates and the **1.440 GiB `obj/world-surface-hang.dmp`**. Keep the dump if the hang is still under investigation; otherwise first retain stack/exception/module details and matching executable/PDB identifiers. No dump analysis was performed in this audit. Preserve `lib` dependencies and current `bin` initially to avoid unnecessary downloads and rebuilding.

Further cleanup may push total recovery above 200 GiB, but the remainder has not been classified finely enough to promise that. The measured first-pass candidates already address most of the avoidable weight.

## Execution safeguards and future retention

Before deletion, recheck active processes, inventory changes, current deployment hashes and an explicit keep list. Resolve every deletion target to an absolute path inside this checkout; reject root, source/Git directories and reparse targets. Inspect links and remove link entries without traversing their shared targets. Use native PowerShell file operations end to end. Do not blanket-delete `obj`, run `git clean -xfd`, prune history, or touch either installed game directory for this storage cleanup.

The docs currently reference hundreds of historical Vulkan artifact roots (the text scan found 813 distinct first path components, including files and partial prefixes). Update references or supply the archive path map and annotate pruned raw evidence so old links do not silently imply that full artifacts remain. Retained receipts record historical results; they are not fresh validation of today's source.

For future runs, a worthwhile tooling change would retain compact status/active buffer ranges normally, plus hashes and summarized guard validation, and emit full-capacity readbacks only for failures or an explicit diagnostic option. That requires changing the harness and verifying that overflow/tail guards remain checked; it is not implemented by this report. Keep one shared hash-addressed frozen corpus, bounded private build/cache generations, selected accepted milestones and complete unresolved-failure inputs. Preserve source patches before retiring any standalone build.

After an approved cleanup, measure the same link-excluding logical totals and actual volume free space, verify the evidence archive and path map, and report both retained size and reclaimed space. Cleanup of artifacts alone does not require a game build/deployment.

## Executed cleanup

The approved cleanup removed **20,792 regular files / 169,549,094,712 bytes (157.905 GiB)** from 132 historical artifact roots. A fresh scan found **117.655 GiB** remaining, compared with the original 275.554 GiB. The measured volume free-space increase during deletion was **157.939 GiB**; concurrent SR2020 builds were active, so the free-space delta is an observation rather than exclusive attribution to this operation. Their command lines were checked, and no active OpenRCT2/parity process or cache lock blocked cleanup.

| Selected products | GiB removed |
| --- | ---: |
| Retired mutable UI cache | 4.744 |
| Numbered terrain runs | 98.390 |
| Historical UI/screenshot builds | 44.686 |
| Standalone terrain probes | 10.085 |

The selection was more conservative than the gross proposal. Repeated numbered-run terrain readbacks were selected only where all three terrain tests were recorded as run/passing in XML; standalone probes had passing summaries. Runs without that proof and the complete run62 suite remain. Other test failures, all comparison images/status buffers, source/patch material and historical metadata remain in place. Historical compiler products were removed from the audited UI/screenshot families; runnable bin outputs in ui-frozen-build24, ui-build36 and screenshot-link207 remain. The retired incremental cache was explicitly invalidated, with its original small state records retained.

The detailed local manifest/path map and result are `obj/vulkan-parity/storage-cleanup-2026-10-14-final/deletion-plan.json` and `deletion-result.json`. Each affected original root now has `historical-retention.json` explaining that raw buffers/compiler products were discarded. The tracked [cleanup summary](repository-storage-cleanup-2026-10-14.json) records every selected root's bytes/counts, before/after totals, metadata verification and executable hashes. The original directory CSV remains the pre-cleanup snapshot.

Post-cleanup checks:

- Every selected deletion path is absent, and all **694 indexed metadata files** still match their pre-cleanup hashes.
- The full frozen reference package passes verification: **4,730 files**, its manifest/source archive and ten protected archived source files.
- Current `bin/openrct2.exe` and `bin/openrct2-cli.exe` hashes are unchanged and match the installed `D:\Games\Independent\OpenRCT2Mod` executables. No rebuild or deployment was needed for these tooling changes.
- **Seven retention tests pass**, covering failure/full-reference preservation, successful pruning/hash journaling, runnable/library retention, root/traversal rejection, an actual Windows junction and subsequent incremental-cache snapshot reuse. Python syntax/CLI and PowerShell parser checks cover the edited entry points.

`AGENTS.md` and [Vulkan artifact retention](../../development/vulkan-artifact-retention.md) now require task cleanup. The parity runners automatically discard successful full-capacity terrain buffers only after checking them and recording hashes; failed/interrupted runs retain evidence. Successful private UI/screenshot builders remove their own intermediates, while the incremental builder retires obsolete generations under its lock before sealing the next immutable snapshot. Explicit flags retain diagnostic scratch when needed. Runtime renderer behavior and qualification criteria were not changed, and this cleanup does not establish new GPU parity or gameplay performance results.

[Library](../../README.md) · [Collection](README.md)
