# Vulkan renderer architecture

This is the renderer's current ownership guide. The graphical factory selects
Vulkan directly; native world rendering, UI command recording and auxiliary image
jobs share renderer infrastructure. Full feature parity and CPU-painter retirement
remain separate work tracked by the [migration index](vulkan-exclusive-migration-plan.md).
The [earlier contract](../archive/rendering/designs/vulkan-renderer-migration-earlier-contract.md)
preserves the superseded partial-terrain admission rules and transitional architecture.

## Ownership and source boundaries

| Responsibility | Source owner | Contract |
|---|---|---|
| Main-window state publication | [PresentationScene](../../src/openrct2/drawing/PresentationScene.h) | Admit at most one complete immutable generation per draw; map and entity state share a source tick. |
| Generation contents | [PresentationGeneration](../../src/openrct2/drawing/PresentationGeneration.h) | Owned snapshots outlive recording and asynchronous consumption; workers do not read mutable simulation storage. |
| Graphical factory | [DrawingEngineFactory](../../src/openrct2-ui/drawing/engines/DrawingEngineFactory.hpp) | Construct the Vulkan drawing engine; backend selection is not a migration fallback. |
| Command recording and native scene admission | [GpuCommandDrawingContext](../../src/openrct2-renderer/gpu/GpuCommandDrawingContext.cpp) | Record owned resources and scene state; an admitted native scene owns its declared world scope. |
| GPU execution | [VulkanBackend](../../src/openrct2-renderer/vulkan/VulkanBackend.cpp) and [shaders](../../data/shaders/vulkan/) | Clear and draw the complete frame from retained state and recorded commands. |
| Atlas residency | [GpuTextureCache](../../src/openrct2-renderer/gpu/GpuTextureCache.h) | Resource generations and frame retirement govern reuse. |
| Auxiliary image jobs | [Configured service factory](../../src/openrct2-renderer/RenderServiceFactory.cpp) and [service contract](../../src/openrct2/drawing/RenderService.h) | Lazily create a graphics-enabled service with loaded assets and an owned device boundary. |

## Publication and lifetime

`PresentationScene::BeginFrame` admits a generation and captures/queues its
replacement at the owner boundary before window traversal. Map and entity state
must not advance independently. Native terrain may use the previous complete
generation while preparation is pending, including interactive map edits.
Bootstrap and lifecycle recovery are explicit synchronization boundaries.
`ScheduleNext` and `Reset` remain part of the scene's lifecycle interface.

Keep workers and submitted packets independent of live map, entity, ride and object
registries. World replacement must retire work and resources from the old lifetime
before reusing their identities. The [save-loading regression](vulkan-save-loading-regression.md)
records why progress-window frames and atlas retirement need explicit ownership.

## World rendering and depth

World assets and graphical state remain resident; updates publish owned state
rather than a complete CPU-painted framebuffer. Every rendered world frame clears
and redraws its visible scope. Camera changes and palette changes are distinct from
world-state mutation. The [retirement inventory](vulkan-cpu-rendering-retirement-inventory.md)
distinguishes deleted ordinary viewport preparation from remaining painter consumers,
test oracles and transitional bridges.

Each authored sprite component uses one constant depth derived from its world
contact. Follow the [component-depth contract](vulkan-constant-component-depth.md)
and [contact regressions](vulkan-depth-contact-regressions.md); do not revive the
archived per-pixel plane/mesh proposals. Definition counts and family admission do
not certify overlap correctness or all operating animation states.

## Auxiliary output and validation

The [Vulkan service](../../src/openrct2-renderer/vulkan/VulkanRenderService.h)
supports separate image jobs and a graphics-only composition path. A supplied UI
device provider must use the existing owner and must not create a fallback device
when that owner is unavailable. The configured factory rejects missing graphics
context/assets or a build without the Vulkan service. Auxiliary target ownership
must remain separate from the main-window presentation generation.

Use the [reference collection](reference/README.md) for frozen input identity and
parity rules. Indexed pixels, palette-expanded output, physical captures, lifecycle
tests, runtime throughput and display pacing qualify different boundaries. A source
audit is not renewed runtime evidence. [Historical reviews](../archive/rendering/README.md)
retain their exact build and fixture limits; [migration work](vulkan-exclusive-migration-plan.md)
routes to outstanding qualification.

[Library](../README.md) · [Collection](README.md)
