# Independent architecture vote — reviewer one

Date: 2026-09-20. Read Plan A completely, then Plan B completely. This vote was formed without author preferences, other votes, other design documents, or history. No builds, tests, or game execution were performed. Approval below is approval of the bounded prototype, not approval of production replacement or proof of performance.

| Candidate | Vote | Confidence |
| --- | --- | --- |
| Plan A: orthographic world surfaces | approve prototype | Moderate |
| Plan B: retained world sprite components | approve prototype | Moderate |
| Preferred first implementation | Plan B | Moderate |

Plan B is the lower-cost first attempt at preserving the existing artwork because its primitive representation directly matches the existing images and their component structure. Its initial scalar-depth rule is a hypothesis, not an established solution. My preference is for a short attempt to falsify that rule, followed by the explicit decision gate, not an extended implementation that accumulates ordering exceptions. Plan A deserves a bounded comparison prototype because it can express within-component depth relationships that a constant-depth quad cannot. If those relationships are common and economical to encode with unchanged artwork, A can become the preferable architecture.

## Reasons and evidence

Both plans identify the right source of potential CPU and bandwidth savings: persistent assets and scene state, small authoritative updates, GPU visual evaluation, and coherent revisions independent of simulation. Neither geometry nor quads inherently produces greater simulation TPS. Publication overhead, simulation work, memory contention, and scheduling determine how much of the theoretical benefit survives. A higher uncapped rendering rate alone would not establish improved large-park TPS or smooth displayed 4K VSync pacing.

The production paint representation distinguishes image offsets from bounding-box offsets and dimensions. CheckBoundingBox compares multiple extent endpoints in a rotation-dependent relation. PaintDrawStruct recursively draws children and applies attachments in a prescribed group order. Consequently, an anchor alone does not carry all information used by the current compositor. That is concrete reason to challenge B's scalar rule, but does not demonstrate a particular visual failure without a scene. The large vehicle bounding-box table also illustrates that frame-dependent extents are meaningful existing data; it is neither a per-pixel depth asset nor proof that its values are physical surfaces.

A's general visibility model is attractive for crossings, slopes, and objects whose visible parts lie at different depths. However, the original bitmap silhouette and shading do not specify an unambiguous physical surface. Correct geometric occlusion and exact legacy compositing can disagree. A must preserve the image sampling and silhouette while supplying useful depth; simply applying the picture to a plausible mesh does not establish either property. The plan correctly makes asset cost and appearance changes an explicit gate.

B avoids initially creating those depth assets and keeps rasterization and art sampling straightforward. Existing component splits reduce its required granularity, but parent/child semantics and arbitrary extents can still defeat a globally chosen anchor-depth formula. A local tie must not quietly become a global object-family priority that fixes one overlap and breaks another.

The entity snapshot header shows immutable publication machinery exists, but comments and declarations alone do not establish coherent whole-world publication, bounded queues, low copying cost, or independent simulation under rendering stalls. Those are shared obligations, and no performance credit is awarded to either plan for merely naming them.

## Major failure modes

- A: depth-authoring cost across artwork, rotations, frames and custom objects; invented geometry clipping intended pixels; disagreement between physical occlusion and intended artist-authored layering; cracks and sampling changes at snapped or zoomed boundaries.
- B: ordering that depends on extents or groups rather than anchors; an indivisible sprite requiring different front/back relations across its pixels; tie instability during interpolation; unbounded decomposition or GPU ordering costs that erase simplicity and throughput benefits.
- Both: destination-dependent palette effects cannot generally use ordinary alpha blending; effects must preserve composition order. Overdraw, upload amplification, snapshot assembly, synchronization and queued obsolete revisions can consume the expected savings. Static cache invalidation must account for neighboring support/terrain dependencies, object replacement, rotations and editor state. GPU-resident presentation must not cause the simulation to wait for VSync or GPU completion.

## Smallest discriminating experiment

Use one shared frozen raw-state fixture and the same indexed artwork, palette pipeline, projection and pixel snapping for both candidates. Include a supported steep-to-level track transition with a moving vehicle, a crossing elevated path, one tall tree, and a peep with an accessory or balloon. Sweep the dynamic objects through foreground/background overlaps and tile boundaries; inspect all four rotations and representative zoom/interpolation phases. Keep a small, explicitly enumerated set of layer rules. Include a destination-dependent effect as a separate composition check so it cannot mask an ordering result.

For B, first attempt the stated scalar plus local ties. Preserve every mismatch and identify whether it requires different per-pixel depth, different component granularity, or an extent/group ordering relation. For A, supply the simplest depth-bearing representation for exactly those same families and record added assets, authoring effort, VRAM and unchanged-art pixel differences. A test-only reference compositor may establish expected images; neither candidate may consume per-frame legacy paint commands as its live input. Manually classify reference defects rather than widening pixel tolerances.

The discriminator is correct visibility and art preservation per unit of representation complexity. A passing mixed scene is not universal proof. If both pass, proceed with B unless A demonstrates a concrete benefit that justifies its asset work. If B fails and A fixes the same cases cheaply with unchanged art, prefer A. If both fail, reserve broader implementation and record why.

Then qualify the survivor at actual 3840×2160 over at least 3,000 measured ticks after warmup, including an operating large park, publication CPU time, upload bytes, GPU time, snapshot age, TPS versus simulation-only ceiling, and displayed frame pacing on stated hardware/refresh rate. Artificial population scaling is supplemental. Demonstrate continuing simulation while presentation is deliberately delayed, with bounded storage and coherent latest revisions.

## Sources inspected

- docs/vulkan-plan-a-geometric-world.md — full text, first.
- docs/vulkan-plan-b-retained-sprites.md — full text, second.
- src/openrct2/paint/Paint.h — first 245 lines, including paint structures and session data.
- src/openrct2/paint/Paint.cpp — targeted excerpts for creation, bounds comparison, sorting, drawing, children and attachments; including CheckBoundingBox and PaintDrawStruct.
- src/openrct2/entity/EntityPresentationSnapshot.h — full header.
- src/openrct2/paint/vehicle/VehiclePaint.cpp — opening vehicle bounding-box table excerpt only; no broader vehicle implementation claim.

Only this vote file was written. No plans or production files were modified.
