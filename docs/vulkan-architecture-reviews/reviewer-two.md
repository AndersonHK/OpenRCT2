# Independent architecture vote — reviewer two

Date: 2026-09-20. Sealed before receiving any other review. Read Plan B first, then Plan A. No author preference, other reviewer votes, other design documents, or history were consulted. No builds, tests, or game execution were performed. These are architecture judgments, not measured performance results.

## Votes

| Candidate | Vote | Confidence |
| --- | --- | --- |
| Plan B: retained world sprite components | **Approve prototype** | Moderate-high that it is the best first experiment; low that its initial scalar ordering works universally |
| Plan A: orthographic world surfaces | **Reserve** | Moderate; valuable alternative, but depth acquisition and exact art preservation remain insufficiently demonstrated |

**Preferred first implementation: Plan B's bounded mixed-scene prototype**, with an explicit failure gate before broad family coverage. Confidence in that priority: approximately 70%. This is not approval to commit the whole renderer to constant-depth sprites. Plan A should be the next targeted experiment for demonstrated failure families, and could become the primary representation if those failures are widespread or costly to repair.

## Why this ordering

Both plans put the likely CPU and upload savings in the same place: retained assets and instances, compact authoritative publications, and GPU evaluation of graphical state. Choosing geometry instead of sprites does not itself decouple simulation, eliminate snapshot copying, or improve TPS. Neither document supplies measured evidence for 4K VSync or large-park throughput. Their shared publication and performance gates are therefore necessary regardless of representation.

Plan B begins closer to the required original artwork. Keeping the original indexed sprite samples, offsets, discrete frames, and transparent coverage avoids introducing a second asset representation before it is known to be needed. The first prototype is relatively cheap and falsifiable: determine whether independent component depths can produce correct occlusion across difficult mixed scenes. It can also exercise the shared resident-state pipeline without first constructing a broad depth-asset library.

Its scalar-depth hypothesis is nevertheless substantial. The existing paint arrangement compares both ends of 3D bounding extents, changes comparisons with rotation, and explicitly follows child/attachment relationships. That is evidence for component semantics, not a proof that one camera depth computed from an anchor can replace those semantics. A component that must be behind one overlapping component and in front of another can expose an inadequate depth rule; cyclic or pixel-dependent overlap cannot be fixed merely by picking a more convenient anchor. Local layer ties are reasonable for true local relationships, but should not become a growing global ordering scheme disguised as materials.

Plan A supplies a stronger geometric model for varying depth and partially overlapping surfaces. It is attractive for terrain and real volumes, and it could eliminate families of sprite-order exceptions. Its weakness is the input data: the inspected sprite asset structure provides image dimensions, offsets, flags, and zoom-image information, not a general surface-depth field. Bounding extents describe a coarse ordering envelope, not the visible surface at each opaque pixel. Mapping screen-authored art onto approximate geometry can change coverage, apparent position, or which pixels occlude. Depth-bearing billboards could preserve the bitmap samples, but still need correct depth per rotation/frame and a scalable source of that data. A successful hand-authored sample would demonstrate possibility, not coverage of the full object catalog.

Reserve means I would not make Plan A the first broad implementation now. It is not a rejection of geometric depth or a claim that all-sprite depth is sufficient. A small competing depth representation for the hardest fixture is justified within the discriminating experiment.

## Major failure modes

### Plan B

- Constant component depth orders tall scenery, terrain edges, supports, track, moving vehicles, or riders incorrectly even though the screen anchors are accurate. A rule that passes stationary scenes may pop at a slope transition or interpolation boundary.
- Reconstructing existing families requires increasingly many splits, ties, or exceptions. This can erase the simplicity advantage, create unstable ordering, and turn GPU expansion into an expensive substitute for CPU painting.
- Zoom-specific sprite substitution and entity pixel snapping are treated as generic texture scaling. The source already exposes distinct zoom behavior; exact reference pixels require retaining those semantics.
- A bounded GPU ordering fallback expands to large neighborhoods or repeated global work. It must be measured as part of B, not assumed free because it runs on the GPU.

### Plan A

- Proxy surfaces provide physically plausible depth but disagree with the visible original art. Geometric plausibility alone cannot authorize a parity difference.
- Correct depth requires extensive per-object, per-view, or per-animation authoring. Custom and unusual artwork makes a manually tuned catalog especially fragile.
- Geometry introduces rasterization seams, altered silhouettes or sampling, depth precision/tie problems, and incompatible pixel snapping. Screen-aligned art plus depth data can reduce some of these risks, but does not solve obtaining the depth data.
- Extra geometry, depth fetches, passes, or overdraw consume GPU bandwidth without corresponding CPU/TPS benefit. The more general visibility model is a correctness capability, not a performance result.

### Shared risks and missing proof

- Immutable revisions must include coherent map, ride/object dependencies, entity state, lifecycles, and generations. Published memory must remain valid through GPU use. A bounded queue, complete recoverable snapshots/deltas, and explicit handling of skipped revisions are needed so a slow renderer does not stall simulation or show progressively older state.
- GPU animation must distinguish purely visual phase from simulation-authoritative frame/state. Interpolation must not mix incompatible object generations or interpolate through creation/deletion and discrete transitions.
- Low uploads do not prove low CPU cost. Measure capture, dirty tracking, copies, allocations, preparation of discarded generations, and synchronization as well as submitted bytes. Camera changes should not trigger CPU world traversal or instance rebuilding.
- Destination-dependent palette operations and genuine translucency require explicit ordering/composition. An opaque depth-buffer proof is not full rendering parity; both plans correctly acknowledge this.
- Large-park TPS gains depend on how much current time belongs to rendering and publication. A renderer cannot remove simulation's own dominant work. A simulation-only diagnostic ceiling and equivalent workloads are essential.
- 4K VSync requires meeting the actual display refresh deadline and sustained pacing, not just a favorable mean GPU time or high uncapped FPS. Snapshot age, missed presents, tail latency, and GPU/CPU contention matter.

## Smallest discriminating experiment

Build one shared resident-state harness with a tiny adversarial scene, before expanding families. Include a sloped terrain boundary, an elevated track/support crossing, a vehicle moving from steep to level with its visible attachments, a tall tree, and a peep/balloon moving in front of and behind these structures. Arrange deliberate overlap; merely placing all families side by side would not discriminate the architectures.

For B, start with the stated world-coordinate scalar and strictly local attachment ties. For A, use the same color samples and projected coverage while supplying the least complex varying-depth surfaces or depth data for the overlapping families. Keep camera, inputs, palette, selection of image frames, and frozen references identical. Legacy output may serve as an offline oracle, but neither runtime candidate may consume its per-frame paint stream.

Exercise all four rotations, relevant zooms, and several movement phases around the transition, including integer snap boundaries. For every differing pixel, inspect component identity, proposed depth, and reference coverage/order. Record any legacy correction individually; do not accept all geometric disagreements as improvements. A raw image mismatch count is useful evidence but not an explanation.

The decisive comparison is the number and scope of failures, rules/splits required to repair B, and authored depth data/geometry and sample coverage changes required to repair A. One unfixable scalar-order fixture disproves universal B; one successful fixture does not establish universal B. Conversely, one manually corrected depth sprite does not establish affordable A. Preserve failing fixtures for subsequent comparisons.

If B passes with a compact, general rule set, expand its prototype. If its failures cluster in a few families, compare targeted depth-bearing representations against the cost of additional sprite decomposition. If failures are widespread, advance A's prototype and make asset-depth acquisition its explicit gate.

Only after this correctness discrimination, replicate populations and run the operating-park qualification at actual 3840×2160 for at least 3,000 measured ticks after warmup. Compare identical workloads and record publication CPU/copies, uploads, GPU stages, pacing, snapshot age, TPS, and the simulation-only ceiling. Include a brief renderer stall to verify simulation progress and bounded recovery. This separates the representation choice from the independently necessary publication architecture.

## Sources read

Design documents, in order:

1. `docs/vulkan-plan-b-retained-sprites.md`, complete.
2. `docs/vulkan-plan-a-geometric-world.md`, complete.

Production source only, scoped reads:

- `src/openrct2/paint/Paint.cpp`: bounding-box comparisons and sorting, drawing/attachment traversal, entity snapping, and child insertion; targeted search of related symbols.
- `src/openrct2/paint/Paint.h`: `AttachedPaintStruct`, bounding box, and `PaintStruct` definitions; targeted search of paint symbols.
- `src/openrct2/drawing/G1Element.h`: complete asset layout and flags.
- `src/openrct2/drawing/PresentationScene.h`: complete publication interface.
- `src/openrct2/drawing/PresentationGeneration.h`: complete immutable generation and camera definitions.
- `src/openrct2/drawing/RetainedBalloonScene.h`: complete retained record, revision, lifetime, and telemetry contracts.
- `src/openrct2/drawing/PresentationScene.cpp`: targeted publication/wait symbol search and `BeginFrame`/`ScheduleNext` implementations.
- `src/openrct2/paint/entity/Paint.Vehicle.cpp`: targeted symbol search only; no general vehicle implementation claim rests on that search.

Directory names were enumerated under the production paint and drawing trees to locate these files. No other document or reviewer output was read.
