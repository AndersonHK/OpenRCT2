# Plan B: retained world sprite components

Status: candidate architecture for independent comparison, 2026-09-20. No implementation decision is made by this document. Compare with [Plan A](vulkan-plan-a-geometric-world.md). Both candidates share the same correctness and performance requirements.

## Required outcome

Simulation publishes authoritative state without preparing graphics. Assets and scene data stay resident in VRAM; shaders select animation, textures and graphical parts, transform them and determine visibility. The renderer consumes coherent immutable revisions independently of simulation. Preserve the original artwork and intended behavior, with exact reference pixels except explicitly recorded, manually inspected corrections. Qualify actual 3840×2160 output, at least 3,000 measured ticks after warmup, CPU/publication/upload cost, GPU time, simulation throughput and displayed VSync pacing. Ultimately route all rendering through Vulkan and remove the obsolete software path.

## Representation and frame

Represent the world using the existing artwork's sprite components. Retain static instance definitions and catalogs until their inputs change. Publish compact dynamic state for entities, lifecycle changes and animation. GPU stages select images, expand objects into components, calculate anchors/offsets, cull and draw batched instanced quads. Each atomic component initially uses one depth value across its opaque pixels, with defined local layer ties for attachments or coplanar art. The screen projection and pixel snapping match the current four camera rotations.

The first ordering hypothesis uses camera depth from world coordinates plus narrowly scoped layer ties. Tile and subtile coordinates are combined before projection/depth. It must be tested against existing component bounds and overlap behavior: the legacy renderer compares 3D extents and attaches children, which does not prove that an anchor-based scalar reproduces every intended ordering.

Alpha-tested holes leave background/depth untouched. A depth buffer composes admitted opaque components; genuine translucency and destination-dependent palette effects require explicit composition in either plan. Existing legacy commands are not a per-frame input. Static expansion may be cached on GPU, while camera changes update constants and GPU visibility. Picking maps visible components to stable simulation identities.

## Implementation sequence

- [ ] Define raw state/publication, asset ownership, stable IDs/generations and GPU instance/material contracts using the approved shared Vulkan service.
- [ ] Build a small world with terrain, paths, trees, elevated track/supports, peeps and balloons. Add a vehicle crossing a steep-to-level transition.
- [ ] Test component depth and local layer rules across four rotations, relevant zooms and interpolation phases. Do not assume the scalar is correct or silently fall back to a CPU painter.
- [ ] Compare frozen reference and intended ordering; manually inspect all differences. Confirm depth, alpha holes, colors, accessories and stable movement.
- [ ] Benchmark population scaling and an operating park at 4K, including publication, upload bytes, GPU stages, snapshot age and simulation-only diagnostic ceiling.
- [ ] Expand world families, effects, selection/editor modes, auxiliary targets and lifecycle/platform coverage.
- [ ] Make Vulkan exclusive only after parity and performance gates close.

## Expected benefits and uncertainties

Atomic components match the structure of existing paint output and can reuse the art without constructing new surface-depth assets. Batched quads and resident catalogs keep rasterization simple. Resident state and GPU visual evaluation remove repeated CPU paint preparation.

A single depth per component may fail when component extents or grouping affect ordering. Correcting this could require additional component decomposition, bounded GPU ordering logic, or adopting depth-bearing surfaces for some families. Any such expansion must be documented rather than hidden behind broad pixel tolerances. Existing splitting into components is supporting evidence, not proof of universal scalar ordering. No particular FPS/TPS result is promised.

## Decision gate

Proceed beyond the mixed-scene proof only if component ordering is visually correct without an expanding set of special cases, and measured CPU/publication/GPU cost supports the target. If the scalar fails, quantify the failing families and compare refinement cost against Plan A before broader implementation. Preserve evidence of failed ordering rules.
