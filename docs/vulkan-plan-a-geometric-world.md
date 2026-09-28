# Plan A: orthographic world surfaces

Status: candidate architecture for independent comparison, 2026-09-20. No implementation decision is made by this document. Compare with [Plan B](vulkan-plan-b-retained-sprites.md). Both candidates share the same correctness and performance requirements.

## Required outcome

Simulation publishes authoritative state without preparing graphics. Assets and scene data stay resident in VRAM; shaders select animation, textures and graphical parts, transform them and determine visibility. The renderer consumes coherent immutable revisions independently of simulation. Preserve the original artwork and intended behavior, with exact reference pixels except explicitly recorded, manually inspected corrections. Qualify actual 3840×2160 output, at least 3,000 measured ticks after warmup, CPU/publication/upload cost, GPU time, simulation throughput and displayed VSync pacing. Ultimately route all rendering through Vulkan and remove the obsolete software path.

## Representation and frame

Represent the world as positioned surfaces under the existing four orthographic camera rotations. Terrain follows world slopes/heights; tracks, supports and scenery use simple geometry or depth-bearing sprite representations where needed. Reuse the indexed art and palette/remap materials. A surface supplies depth varying across its footprint; geometry or auxiliary depth data expresses volume where a flat sprite cannot.

Keep static surface instances and asset catalogs resident until edited. Publish compact dynamic state for entities, lifecycle changes and animation. GPU stages expand visible state into graphical parts, choose frames/materials and compute positions. Cull chunks and instances, rasterize opaque/alpha-tested surfaces with depth testing, then compose effects and UI. Transparent holes must not occlude. A visibility buffer for material/primitive identities is optional; conventional depth is sufficient for the first proof. Genuine translucency and destination-dependent palette effects require explicit composition in either plan.

Use the current projection and pixel snapping initially. Geometry determines cross-object visibility. Small material/decal tie rules are explicit; existing CPU painter order is not reproduced as a per-frame dependency. Picking maps visible parts to stable simulation identities. Camera movement does not rebuild world graphics on the CPU.

## Implementation sequence

- [ ] Define raw state/publication, asset ownership, stable IDs/generations and GPU instance/material contracts using the approved shared Vulkan service.
- [ ] Build a small world with terrain, paths, trees, elevated track/supports, peeps and balloons. Add a vehicle crossing a steep-to-level transition.
- [ ] Reuse art on simple surfaces first; establish where actual geometry or additional depth data is necessary. Test four rotations and relevant zoom/interpolation phases.
- [ ] Compare frozen reference and geometric expectations; manually inspect all differences. Confirm depth, alpha holes, colors, accessories and stable movement.
- [ ] Benchmark population scaling and an operating park at 4K, including publication, upload bytes, GPU stages, snapshot age and simulation-only diagnostic ceiling.
- [ ] Expand world families, effects, selection/editor modes, auxiliary targets and lifecycle/platform coverage.
- [ ] Make Vulkan exclusive only after parity and performance gates close.

## Expected benefits and uncertainties

Surface depth provides one geometric visibility model, including partially overlapping volumes and future features that need actual surfaces. It can avoid component-order exceptions. Resident state and GPU visual evaluation remove repeated CPU paint preparation.

The old bitmap art does not include general surface depth. Existing world bounds and split components help but may be insufficient to construct accurate depth-bearing representations automatically. Asset authoring, matching silhouettes and maintaining exact pixel appearance could dominate implementation. Additional geometry/depth work does not inherently improve throughput over a resident sprite implementation; measure it. No particular FPS/TPS result is promised.

## Decision gate

Proceed beyond the mixed-scene proof only if the reused art can supply adequate surface depth without unacceptable appearance changes or asset cost, and measured CPU/publication/GPU cost supports the target. Widespread appearance changes require an explicit owner decision. Preserve evidence of failed representations.
