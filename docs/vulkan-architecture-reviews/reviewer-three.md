# Sealed independent architecture vote: reviewer three

Date: 2026-09-20. This review was made without author preferences, other votes, project history, or other design documents. No builds, tests, or game execution were performed. This is an architectural judgment, not measured parity or performance evidence.

## Votes

| Candidate | Vote | Confidence |
| --- | --- | --- |
| Plan A: orthographic world surfaces | approve prototype | Moderate |
| Plan B: retained world sprite components | approve prototype | Moderate |
| First implementation | Plan B, limited to its falsifiable mixed-scene proof | Moderate |

Approval means permission to test the stated hypothesis and stop at its decision gate. Neither plan is yet approved for a world-wide migration or removal of the reference renderer.

Plan B should go first because it preserves the native representation of the artwork and gives the less expensive route to discovering whether the principal obstacle is visibility. It does not need reconstruction of missing surface depth before it can show informative failures. This preference is not an endorsement of a universal anchor-depth formula: that formula is a central unproven hypothesis, and source inspection gives concrete reasons to doubt its universal applicability.

Plan A deserves a bounded prototype because varying depth can express visibility changes across one object that a constant-depth quad cannot express. Terrain slopes are an especially plausible application. Its ability to solve that class of failure is valuable even if only selected families ultimately use surfaces. However, the existence of physically reasonable geometry does not establish exact pixel parity with artwork designed for ordered compositing.

## Evidence and reasoning

Both plans correctly separate simulation publication from visual expansion, retain resources, and require immutable coherent revisions. These shared choices, not the difference between surfaces and sprites, offer the main route to lower CPU preparation and transfer cost. Both still need a complete account of visual input dependencies and asset lifetimes. Neither establishes the CPU publication cost or how all map, object, and entity revisions become coherent.

The inspected paint ordering code compares rotated three-dimensional bounding extents and retains explicit child/attachment relationships. An object's anchor alone omits information used by this algorithm. This does not prove scalar ordering must fail in every relevant scene, nor that the legacy algorithm expresses perfect intended geometry; it does rule out treating the old component split as proof that scalar depth is sufficient. Local attachment ties also must not accidentally change ordering against unrelated objects.

The inspected G1 element definition stores bitmap dimensions, offsets, flags and zoom-image information, without general surface-depth data. Plan A therefore has a genuine reconstruction/authoring problem. Bounds are ordering metadata, not evidence of the per-pixel shape represented by the art. Adding geometry can change which visible pixels win even when the sampled colors and silhouettes are individually identical. Per-view depth assets may be necessary, and matching separate zoom sprites increases the validation burden.

Surface paint excerpts show slope-dependent masks, neighbor-dependent pattern composition and tunnel/edge parts. Both plans must model these dependencies; retaining a tile without invalidating its affected neighbors would leave stale visuals. The peep image-selection function demonstrates compact state-driven frame lookup that seems suitable for resident catalogs, but does not establish coverage of the complete peep or vehicle pipeline.

The existing entity snapshot header offers evidence of immutable incremental presentation storage, but its concrete entity layouts and copy-on-write structures do not by themselves establish a compact GPU contract or whole-world independence. I did not audit its implementation.

Improved large-park TPS remains conditional. Removing rendering work can remove a bottleneck; it cannot exceed the simulation-only ceiling or automatically accelerate pathfinding and other simulation work. GPU residency also does not eliminate per-tick changes, publication bookkeeping, buffering or contention. Actual 4K VSync pacing must be measured at the selected display refresh rate; GPU duration alone is insufficient.

## Major failure modes

- **A:** Incorrect or costly inferred depth for trees, curved track, vehicles and art with apparent volume; pixel coverage or sampling changes; expensive per-asset/per-view depth maintenance; physically plausible occlusion conflicting with intended art composition. Excess geometry or depth layers can also add fill and memory cost without a CPU advantage over B.
- **B:** Extent-based ordering or child groups cannot be represented by the chosen scalar; intersecting components require incompatible order at different pixels; tiny depth biases become global ordering exceptions or flicker during movement. Refinements may expand into many special cases, complex GPU ordering, or depth surfaces and erase the initial simplicity.
- **Both:** Stale/incorrect state after editing or asset replacement; ID reuse across in-flight revisions; simulation stalls behind renderer consumption; full-world copying disguised as incremental publication; unbounded revision queues; excessive expansion/overdraw at 4K. Palette filtering, genuine translucency, terrain masks, alternate zoom art and picking require their own correctness coverage.

## Smallest discriminating experiment

Implement a small identical resident fixture in both representations, sharing indexed assets, camera projection, palette handling, state publications and capture logic. Include a sloped terrain tile with neighbor blending, an elevated track/support section, a vehicle crossing a steep-to-level transition, and one tree or tall scenery object overlapping a moving peep with an accessory and balloon. Sweep motion continuously through overlaps rather than choosing one attractive still frame. Capture all four rotations, representative zooms and interpolation phases.

First compare exact pixels against frozen reference images and inspect every discrepancy against intended visibility. Record each needed decomposition, special tie, geometry/depth asset and authoring effort. In particular, find whether B needs different front/back order in different opaque regions of one component, and whether A can fix that while retaining the original pixels. A single counterexample can disprove a universal scalar; one passing scene cannot prove universality. Do not conceal failures with a CPU ordering fallback or broad image tolerance.

Only after that correctness comparison, replicate the fixture to probe scaling and run the required operating-park qualification for at least 3,000 measured ticks after warmup. Report publication CPU time, transferred bytes, render CPU time, GPU duration, snapshot age, simulation TPS and displayed frame intervals at real 3840x2160. Include camera motion, edit/lifecycle events and intentionally slower rendering to verify simulation continues independently with bounded memory. Compare with the simulation-only diagnostic ceiling. Stop and reassess representation cost before extending either prototype to more families.

## Sources actually read

- Entire `docs/vulkan-plan-a-geometric-world.md`.
- Entire `docs/vulkan-plan-b-retained-sprites.md`.
- Selected source excerpts from `src/openrct2/paint/Paint.cpp`: bounds construction, bounding-box comparison, arrangement, child drawing and attachment creation.
- Entire `src/openrct2/paint/Boundbox.h`.
- Entire `src/openrct2/drawing/G1Element.h`.
- Entire `src/openrct2/entity/EntityPresentationSnapshot.h`.
- Entire short `src/openrct2/paint/entity/Paint.Peep.cpp`.
- Selected matching excerpts from `src/openrct2/paint/entity/Paint.Vehicle.cpp` and `src/openrct2/paint/tile_element/Paint.Surface.cpp`.

A tracked production-path listing was used to locate these sources. No other document, vote, history or implementation was read.
