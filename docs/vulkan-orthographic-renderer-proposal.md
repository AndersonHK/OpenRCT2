# Proposal: a new orthographic world renderer

## Recommendation

Build a new Vulkan world renderer around **3D world instances, an orthographic camera, GPU culling, and a depth/visibility buffer**. Port the game's visible behavior and artwork into that renderer. Do not reproduce the old sequence of CPU paint functions as its architecture.

The simulation already has positions, heights, dimensions, directions, ride state and animation state. It should publish those facts. The GPU should decide which graphical elements represent them, select their textures and animation frames, position their vertices, and resolve which surfaces are visible. Terrain, scenery and moving vehicles belong to one geometric scene; a vehicle does not become a separate foreground layer merely because it is animated.

This is the owner's proposed **phenotype port**: preserve what the player sees and how it responds, while replacing how the image is produced. The >1,000 FPS example expresses how light the intended GPU workload should be, rather than establishing a separate acceptance threshold. The practical goals are sustained VSync pacing, low CPU and transfer cost, and substantially more simulation throughput.

This document proposes the next direction. No wholesale renderer replacement has been applied. Today's working checkpoint and unfinished experiments are recorded in [the handoff](vulkan-post-checkpoint-feedback.md).

## Why this is worth doing

The current ordinary Vulkan path still asks the CPU to generate world paint structures and sprite commands. Its worker executes those commands asynchronously, but simulation and visual preparation still compete on the main thread. Moving submission to a worker does not remove that preparation.

The observed 4K baseline makes the distinction concrete: Vulkan spends about **27 ms per frame in CPU drawing and 1.67 ms on the GPU**. A scheduling experiment increased frame production slightly while cutting TPS by more than half. It was withdrawn. That is an architectural bottleneck, not evidence that the GPU is struggling with the park's triangles.

A new renderer avoids making the old painter's call graph, dirty rectangles and linked-list ordering permanent requirements. It can retain assets and instances in VRAM, reuse static geometry, and evaluate visual state directly in shaders. Existing Vulkan device ownership, resource retirement, asset decoding, palette/remap support, offscreen targets and test infrastructure remain useful components.

## The ownership boundary

| Owner | Responsibilities |
|---|---|
| Simulation | Game rules, authoritative transforms, relationships, construction/removal, ride and guest state, animation state and deterministic tick progression. |
| Publication | Immutable state revisions with stable IDs/generations, changed chunks and object references. Preserve lifecycle changes even when a displayed frame is skipped. No texture choice, screen projection or painter sorting. |
| Renderer CPU | Asset loading and one-time visual catalogs, bounded upload scheduling, command submission, resource lifetime and presentation. It does not visit every object each frame to build graphical commands. |
| GPU | Visual instance expansion, animation/texture selection, vertex offsets, interpolation, projection, visibility, depth, materials, palette/remaps, lighting and final composition. |

A suitable world-facing interface is conceptually `PublishWorldState(revision)` plus `Render(camera, settings)`. A per-frame interface containing CPU-selected sprite IDs, projected coordinates or painter depths would reintroduce the old bottleneck.

Assets and static instances remain resident. Camera movement updates camera constants. Simulation changes update compact state. Object catalogs change when objects load, unload or change revision. The normal frame does not upload a CPU-rendered image or rebuild a complete CPU vertex stream.

Simulation and rendering have independent clocks. The renderer consumes a coherent completed revision, uses authoritative position history for interpolation, and never changes live entity positions to draw an intermediate frame. Bounded buffers must permit an old displayed frame to be dropped without losing cumulative state or allowing an in-flight GPU read to observe overwritten memory.

## The frame

1. Apply accumulated state changes to resident GPU storage. Retire obsolete resources only after their submissions complete.
2. Cull scene chunks and instances against the orthographic view. Expand the visible raw instances into their graphical parts on the GPU.
3. Choose animation frames, orientation variants, materials, colors and accessory elements from resident visual catalogs. Compute their 3D vertices and bounds.
4. Rasterize opaque and alpha-tested surfaces into depth and visibility/material information. Transparent texels do not write depth. Which surface wins follows geometry and depth, not CPU draw order.
5. Resolve the visible palette/material and lighting. Handle genuinely translucent or destination-remapping effects with a GPU composition method that preserves their intended behavior.
6. Composite UI and present with bounded latency and measured VSync pacing. Screenshots and previews use the same renderer with other cameras/targets.

Frustum culling removes off-camera work. Occlusion culling can additionally reject hidden instances using a conservative hierarchical depth representation. It must not treat transparent holes in a tree or support structure as solid occluders. A full visibility buffer storing primitive/material IDs is an option for deferred shading and picking; the first proof can use a conventional depth buffer without waiting for that optimization.

## Reusing the art with 3D vertices

The proposed camera is a real projection of world coordinates. The existing projection already defines the intended screen anchors after rotation:

```text
screenX = worldY - worldX
screenY = floor((worldX + worldY) / 2) - worldZ
```

Match that framing, vertical scale, pixel snapping, zoom behavior and the four existing rotations initially. An arbitrary camera preset may change the art's apparent proportions. Smooth rotation or a different projection is a separate product choice, not necessary to gain the new architecture.

The existing artwork can remain indexed, nearest-sampled textures. The representation can differ by object family:

| Element | Initial geometric representation |
|---|---|
| Terrain and water | World-space surfaces using actual heights and slopes. |
| Tracks and supports | Geometry or depth-bearing impostors derived from track shape, orientation, height and support dimensions, using the existing appearance. |
| Guests, staff and many animated objects | World-anchored textured instances; GPU animation and direction selection, with explicit geometric placement of accessories. |
| Trees, scenery and buildings | Textured surfaces, simple geometry or depth impostors selected according to the object's volume and required overlap behavior. |

The important experiment is **what depth the existing art supplies**. A flat quad has a surface; a bitmap of a tree or a curved track depicts a volume. Existing world dimensions give us a strong starting point for geometric proxies, but do not describe every branch or rail inside a texture. Start with the simplest world-space representation that works. Where necessary, split visual parts, add a depth impostor, or use actual geometry. These are asset/geometry decisions; they should not become special CPU painter-order rules.

A proxy box should not simply occlude every pixel inside its bounds. Alpha-tested artwork supplies the silhouette; depth must agree with the represented visible surface. The first cherry-tree/support/vehicle experiment should decide how much additional depth information is actually needed.

This approach does **not** require replacing every sprite with newly authored 3D artwork at the outset. Fully modeled assets are a possible later choice, with a much larger art and visual-compatibility scope.

## Port behavior, not functions

Create a small declarative visual specification for each family: the simulation facts it consumes, graphical parts it can produce, asset-table layout, material behavior and geometric meaning. Implement that specification in shaders. Old functions provide observations and reference outputs; their control flow does not become the new renderer's structure.

Examples:

- A guest's action, direction, animation phase, clothing colors and carried object determine the body and accessory instances. The CPU publishes those states; shaders select the images and place the parts.
- A track element's type, orientation, height and ride properties determine its visible track/support geometry and material. Those facts persist until construction or relevant state changes.
- Water, glass and remap effects retain their intended compositing behavior. A depth buffer resolves opaque visibility; it does not, by itself, perform translucent blending or an indexed-color destination transform.

Retain gameplay and picking semantics deliberately. A visibility buffer can return an object/part identity for selection, while simulation still validates the resulting action. Ghost placement, cutaways, underground views, hiding categories, lighting, weather, rotated views, UI overlap and export paths need explicit behavior coverage.

## First proof and decision gates

### 1. Prove the replacement boundary

- [ ] Create a separate world-renderer entry point consuming immutable simulation state and resident asset catalogs.
- [ ] Display a terrain patch, intersecting tracks/supports, foreground trees and a moving vehicle in the same depth-tested scene.
- [ ] Include the reported cherry-tree/diagonal-support combination and a steep-to-level vehicle transition, with all four camera rotations.
- [ ] Let the GPU select images and produce vertex offsets. Assert that this path calls no legacy world painter, CPU projection loop or CPU sort.
- [ ] Confirm alpha holes, intersection depth, palette colors and stable movement visually. Identify whether simple surfaces suffice or depth impostors are necessary.

**Decision:** if depth-bearing reused art works, expand that representation. If it does not, decide explicitly between adding geometric/depth asset information and accepting a revised appearance. Do not conceal the mismatch with a collection of ordering exceptions.

### 2. Prove the intended scale

- [ ] Expand to guests/staff and a representative mixture of static park structures, retaining everything unchanged in VRAM.
- [ ] Test a populated operating park and a controlled large instance workload, including the owner's approximate 60,000-guest/hundreds-of-rides case where feasible.
- [ ] Run at observed 3840×2160 for at least 3,000 measured ticks after warmup.
- [ ] Measure CPU simulation time, publication/copy cost, upload bytes, GPU stages, visible-instance counts, snapshot age and displayed frame pacing separately.
- [ ] Show that camera-only frames require no per-object CPU rebuild or bulk image upload, and that simulation advances independently of presentation.

**Decision:** qualify the architecture using throughput at equivalent visual work and actual displayed pacing. A higher TPS obtained by rendering fewer frames is not the desired result.

### 3. Complete the behavior inventory

- [ ] Port remaining terrain, paths, track styles, scenery, vehicles, peep states and special visual effects by specification.
- [ ] Integrate editor/construction views, selection, overlays, weather, lighting and material composition.
- [ ] Route all screenshots, previews, minimaps and scripting image callers through the new GPU architecture as appropriate.
- [ ] Qualify object reload, resize, device loss, shutdown, memory limits, loader packaging and supported platforms.
- [ ] Switch the production default only after the new renderer is complete; then remove the old world pipeline and software path from production.

## What parity means for this proposal

The pre-migration software reference remains evidence for the original art, placement, animation and UI behavior. The currently deployed software renderer is not canonical merely because it is software. Known depth/order glitches should have a correct geometric expectation, supported by the world arrangement and manually inspected samples.

Preserve pixel identity where the new representation can and should reproduce the intended original result. Record every intentional correction explicitly; never use a whole-image tolerance to hide unresolved changes. If a chosen 3D representation requires widespread visual differences beyond bug fixes, that would revise the current parity objective and requires an explicit owner decision. A fresh implementation changes the means, not automatically the promised appearance.

The lowest-risk next step is the small mixed-scene proof above, alongside the frozen reference and current playable checkpoint. It directly tests the depth/visibility premise and the simulation/GPU ownership boundary before committing to either a complete rewrite or further expansion of the existing painter-based path.
