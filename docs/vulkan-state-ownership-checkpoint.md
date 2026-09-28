# State ownership replacement checkpoint

Qualified source checkpoint, 2026-09-23. This batch follows the owner's minimal graphical state and object-owned runtime requirements. [Build79](../obj/vulkan-parity/build-79/receipt.json) passes with zero warnings/errors; [run60](../obj/vulkan-parity/run-60/summary.json) passes all 867 tests, including 66 parity cases, with clean Vulkan validation and unchanged pinned inputs. The deployed checkpoint52 is unchanged. This is not native-world or performance completion.

## Removed and replaced

- Public writable Peep clothing and Guest accessory colour fields are removed. Owner methods synchronize the related values and publish only actual changes through one shared appearance notification. Gameplay, scripting, UI, original-format import, park save/load, snapshot diagnostics and drawing callers use the new interface. Save field order and colour types are preserved. Detached copies cannot enqueue changes for the corresponding live entity because the registry checks pointer identity.
- Immutable peep snapshots no longer store arrays of whole 96-byte records or expose a pointer to one. Lifecycle, motion, appearance and animation have independent shared storage and revision tracking. The production on-ride reader accesses only its needed groups. There is one representation, with temporary reconstructed values for diagnostics.
- A field consumer accumulates absolute values and tombstones since its acknowledged revision, including skipped publications and reused identities. Preparation does not acknowledge work; all groups commit under one revision. Independent review found that a recreated owner can restart sequence numbering under a newer epoch. Build77 includes the correction and populated/empty-reset regressions: sequence regression is rejected within the same epoch, while a newer epoch resets ownership.
- The scripting self-copy compatibility operation now runs on the shared Vulkan service, with bounded ordered compute slices. It preserves the old observable overlap recurrence without a CPU raster fallback. Its three contract/device/script tests pass; all fifteen saved script cases match exactly and pass [independent manual visual review](vulkan-custom-image-alias-visual-review.md). It remains separate from world rendering; large-image compatibility is still open.

## Costs and limits

Delta payload sizes are 48 bytes for motion, 24 for appearance, 44 for animation and 12 for deletion. Bootstrap/reuse uses 128 bytes. These are structure sizes, not measured GPU upload bytes. Resident scalar groups total 100 bytes per slot, plus revision metadata and ownership/directory overhead. Changed groups copy their 64-slot chunk; the outer directory has a fixed copy/initialization cost that field-byte metrics omit.

The authoritative producer still captures the full 96-byte record and compares groups. Its dirty notifications do not yet carry the final independent field categories. Legacy frame offsets and sprite bounds also remain captured instead of being fully derived from semantic animation state and shared definitions. Production publication does not yet supply the interpolation history exercised by the isolated history tests.

The GPU selector probe consumes the field schema, but ordinary world rendering does not yet upload/scatter these fields into an admitted native mixed-world pass. Terrain/peep ordering, catalog atlas ownership, the other world families, effects and picking remain required. No substantial TPS gain or displayed frame-pacing result has been demonstrated. Existing balloon counters do not represent the complete peep producer/publication cost.

## Ownership direction

The owner's Augustus reference separates immutable definitions, mutable state and owner-bound behavior modules. Apply that to state transitions and lifecycle registration/rebinding here. Remove the corresponding old interfaces and helper calls as each module takes ownership. Do not add a module that merely forwards to the legacy painter, or a full-population repair scan that compensates for missed object events.

GPU submissions hold coherent immutable data and catalog lifetimes, not live simulation-object references. Before runtime modules carry owner references, replace raw object-copy paths that would duplicate a binding to the old owner. Plain data access needs no extra abstraction; mutation methods exist to enforce real ownership/publication invariants.

## Qualification and remaining checklist

Build77 and `peep-rule-probe-build-04` pass. Probe03 stopped in provenance preflight because the original checker pinned the removed public colour interface; the corrected verifier accepts only the exact reviewed ownership transformation, normalizes it back to the full original header hashes, and keeps the frozen painter bodies unchanged. It does not relax pixel comparisons.

Run58 remains failed: its new late track-preview failure case aborted on `GfxGetG1Element called on headless instance` before the process could write its final test report. Build78 corrects the late-case fixture to load real sprite metadata while retaining headless operation and its CPU-only injected service. Run59 completes with 866/867 passing tests and clean validation: the remaining assertion expected no dirty notification after changing a guest's shirt. Build79 corrects that stale test to require exactly one changed guest, updated facts in the next publication and unchanged facts in the held snapshot. Run60 passes all 867 tests. Neither earlier receipt was rewritten.

The alias reports in run58, run59 and run60 are byte-identical (SHA-256 `04280a904e7ba4b66124b4ecd853be1d3f4c06ec740abbcf39b22effcb9b59cb`), so the independent review of run58's saved corpus applies to these same outputs. Expectations encode historical scan semantics; they are not fresh upstream execution. The experimental mixed-depth diagnostic remains separately failed and excluded by exact name, not waived.

- [x] Compile the migrated callers and apply the epoch-reset correction (build79, zero warnings/errors).
- [x] Pass import/save, publication/lifetime, device selector, nested-image/alias and late-preview-failure regressions with clean validation (run60).
- [x] Save and manually inspect all fifteen actual alias cases against their exact old-behavior expectations.
- [ ] Replace full producer capture and implement the production batched GPU consumer; remove CPU world preparation as whole families become supported.
- [ ] Measure complete CPU cost, real staged/uploaded bytes and ordinary dense 4K workloads over at least 3,000 measured ticks; qualify displayed VSync pacing separately.

The separately captured track-preview corpus is already exact against upstream and manually reviewed in [the preview review](vulkan-track-preview-visual-review.md). It predates this batch and cannot qualify its changed source.

The next replacement batch is direct changed-field capture from existing owner mutation categories, deleting production raw96 batches and the duplicate drain API. Then connect the actual batched GPU consumer and resident catalog ownership to the native world pass. Keep producer/copy/upload costs measurable; the current structure sizes alone are not a bandwidth result.
