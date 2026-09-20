# Shared Vulkan service and offscreen migration

Status: ownership/module design **approved by the owner on 2026-09-19** ("Approve the shared service design"). Implementation may proceed with the shared module, one process device, separate main/pooled auxiliary resources and injected core/CLI interface. No caller migration is yet claimed by this document. Keep the frozen software oracle unchanged throughout these steps. Platform/package qualification remains in the migration plan rather than being decided here.

## Current seams and blockers

| Current owner / source | Concrete constraint |
| --- | --- |
| [`VulkanBackend.cpp`](../src/openrct2-renderer/vulkan/VulkanBackend.cpp), `Initialise` | Accepts an owned presentation host after E2a and still creates a device for this backend. A second backend currently means a second device, atlas and pipeline set. |
| [`VulkanDevice.cpp`](../src/openrct2-renderer/vulkan/VulkanDevice.cpp), `Initialise`, `CreateInstance`, `SelectPhysicalDevice`, `BeginFrame`, `EndFrame` | Creates a surface before selecting the physical device; requires graphics **and present** queues and `VK_KHR_swapchain`; couples slot acquisition, upload storage, fences and timestamps to swapchain acquisition/presentation. Merely accepting a null window is insufficient. |
| [`VulkanPlatform.cpp`](../src/openrct2-ui/drawing/engines/vulkan/VulkanPlatform.cpp) | Obtains WSI extensions, surfaces and drawable size through SDL/Win32, and loads the Vulkan library through SDL on non-Windows. The device layer cannot currently initialize without that platform adapter. |
| [`VulkanPalettePipeline.cpp`](../src/openrct2-renderer/vulkan/VulkanPalettePipeline.cpp), `RefreshSwapchain`, `Record` | Derives format, extent, framebuffers, encoding and generation from `Device` swapchain getters. The output render pass ends in `PRESENT_SRC_KHR`. Its existing RGBA8 scaling intermediate already demonstrates the reusable colour-output operation. |
| [`VulkanResources.h`](../src/openrct2-renderer/vulkan/VulkanResources.h), `IndexedResources` | Combines persistent atlas/descriptors/remap/blend/falloffs with three slots of target-sized colour/depth/transparency/light resources. Resizing this object for a thumbnail must not resize the main window's canvases or invalidate its recorded atlas references. |
| [`GpuCommandDrawingContext.cpp`](../src/openrct2-renderer/gpu/GpuCommandDrawingContext.cpp), [`GpuTextureCache.cpp`](../src/openrct2-renderer/gpu/GpuTextureCache.cpp) | Record commands and decode assets without SDL and call core drawing/font/presentation APIs. E1 gives these files a shared renderer library owner; core still must not link back into this implementation. |
| [`src/openrct2-renderer/CMakeLists.txt`](../src/openrct2-renderer/CMakeLists.txt) | E2b gives Vulkan execution and shader generation one shared library/target owner. UI supplies its SDL adapter; core remains a dependency, never a consumer of the concrete renderer. |
| [`src/openrct2-cli/CMakeLists.txt`](../src/openrct2-cli/CMakeLists.txt) | CLI currently links core and threads, not the UI executable. Linking core to the UI to reach Vulkan would introduce the wrong dependency direction. MSBuild needs the same new ownership boundary, not duplicate backend source lists. |

The reusable execution order is already centralized in `Backend::Submit`: lookup/texture uploads; indexed/depth initialization; optional world surfaces; lines; opaque rects/sprites; ordered transparency; weather; LightFX; palette/physical scaling. Preserve that order and the same shader binaries. `ReadbackLatestIndexedCanvas` chooses the composite canvas when necessary, but its “latest” identity is inadequate for multiple clients; offscreen results must name their submission and target generation.

## Proposed ownership boundary

Add a small abstract service contract to core, and move renderer implementation into a sibling `src/openrct2-renderer` module. The dependency graph is:

```text
UI application / CLI image commands / parity harness
        |                         |
        +---- renderer module ----+----> core contracts/assets/painters
        +------------------------------> core

core auxiliary caller -> abstract IRenderService supplied by application
UI SDL surface adapter -> renderer presentation interface
```

Core does not call a concrete Vulkan factory or depend on renderer implementation headers. Its context owns an injected service/factory through an abstract interface; application composition constructs the implementation lazily. Image-producing CLI commands receive the same factory as the UI. Simulation/server/non-rendering commands do not initialize a loader, device, surface or SDL video subsystem. A missing rendering service produces a specific image-operation error, never silent success or software fallback. Whether the release must also tolerate an entirely absent Vulkan loader library is a separate packaging contract: if required, use lazy function dispatch/module loading rather than an unconditional CLI loader import.

| Component | Ownership |
| --- | --- |
| Core `IRenderService`, offscreen request/result and session interfaces | Extents, palette/alpha contract, completion/error identity; no SDL or Vulkan types. Existing `IDrawingContext` remains the paint entry point during migration. |
| Renderer command recorder and texture residency | Move the present GPU recorder/cache/command ABI, with core asset/font dependencies. Do not duplicate or rewrite the recorder for thumbnails. |
| `DeviceContext` | Instance, physical/logical device, graphics queue, allocator, pipeline cache, capabilities and serialized submissions. No window required. |
| `PresentationAdapter` in UI | SDL/window lifetime, required WSI extensions/surface creation, present capability negotiation, swapchain, acquired images and presentation semaphores. |
| `RenderSession` / resource domain | Target generations, slot leases, palette/lookup snapshots, descriptor ownership and atlas residency. Main and auxiliary domains have distinct mutable recording state. |
| `FrameExecutor` | The existing backend pass sequence against explicit target/resources; no knowledge of main-window versus thumbnail versus CLI. |
| Output target | Indexed-only result, offscreen SDR RGBA image, or acquired swapchain image. Carries format, extent, encoding, image/view, initial/final layout and generation; the adapter controls presentation. |

One process-level service/device serves the main renderer and auxiliary work. Initially use a main residency domain and **one reusable auxiliary domain**, with a bounded pool of auxiliary targets. Sharing the device and pipeline cache is mandatory; sharing the mutable atlas between domains is not required for the first safe extraction. Do not allocate a full 64-layer atlas or three full-sized frame slots per thumbnail. A bounded auxiliary domain avoids that cost and avoids cross-recorder cache mutation while a main packet is still being recorded. Later atlas sharing needs explicit generation/lease tests rather than an unsynchronized shared `TextureCache`.

The UI should register its presentation requirements before lazy device creation, so a first thumbnail during UI startup cannot select a device/queue configuration that is unable to present later. CLI offscreen creation selects graphics capabilities without requiring WSI, swapchain support or a present queue. Attaching an incompatible surface to an existing offscreen-only context must return an explicit error/recreation request; never destroy a live shared device underneath outstanding jobs.

## Output and lifetime contract

Proposed high-level flow: `BeginOffscreen(spec)` returns an owned recording session; the caller paints synchronously into its drawing context; `Submit()` seals immutable commands and returns a named completion; `Wait()` returns owned image data or an error. UI code may expose a synchronous convenience wrapper around this flow, preserving existing APIs. GPU completion must not call gameplay/UI/script callbacks.

The spec names extent, initial contents, palette/alpha policy, requested indexed/RGBA outputs, optional lighting/scaling, and isolated snapshot policy. Initial contents are either an explicit clear index or owned existing indices. The main backend's unconditional clear-to-zero cannot implement editing an existing script image. RGBA screen opacity and auxiliary index-zero transparency remain separate contracts; do not apply `ConvertScreenPalette` indiscriminately to exports. Indexed PNG writing and asset compression remain CPU serialization, not CPU rasterization.

Each submission owns its command bytes, decoded uploads, palette/lookups, immutable snapshot references and residency leases until its fence completes. Returned pixels survive target reuse/service resize. Cancellation before submission releases leases once; cancellation after queue submission suppresses delivery but defers resource reclamation until completion. Reusing a target increments its generation; a completion for another generation is an error. Device loss/shutdown fails every queued or waiting result without returning an older image. A caller timeout does not free in-flight GPU resources.

Separate command-slot allocation from swapchain acquisition. Offscreen jobs obtain a slot/fence/upload allocation directly, record into owned images, and submit without image-available/render-finished presentation semaphores. The graphics queue still needs one host owner or explicit shared serialization. Keep the existing submitted-versus-recorded layout rollback rule when abandoning recording. Offscreen readback must use its job completion fence and dedicated/bounded staging storage; it must not reset an upload ring that belongs to another pending job. Existing readback uses a frame upload ring, so retaining its current 96 MiB ceiling is not a giant-screenshot solution.

Parameterize the palette/output pass to bind an explicit output view rather than querying swapchain state. For indexed-only output, stop after the final indexed canvas; for RGBA use the same palette, LightFX and scaling shaders into RGBA8 UNORM with transfer-readable final layout. For presentation the same pass receives the swapchain view and presentation layout. Keep integer-index sampling and byte-space colour interpolation unchanged. The offscreen path does not simulate display output by CPU palette expansion.

Nested auxiliary calls are expected while UI/main recording is active. CPU painting stays on the game/UI thread; only sealed jobs execute on the service worker. An auxiliary wait must hold no main recorder/cache/service queue lock and must not wait for the incomplete main packet. Auxiliary jobs are reliable/non-droppable; only the main presentation mailbox may supersede disposable frames. Reject blocking waits from the service worker itself. Bound auxiliary queue/memory use and schedule jobs fairly so thumbnail bursts do not indefinitely starve presentation.

Legacy `RenderTarget.bits` currently participates in subtarget/clipping pointer arithmetic. An offscreen adapter must provide valid owned address storage while those APIs remain, even when the bytes are not rasterized on CPU. Do not invent invalid pointers to save the address allocation. This adapter is temporary migration scaffolding; explicit target origin/stride can replace it after caller parity is established.

## Caller-specific preservation requirements

- [`Screenshot.cpp`](../src/openrct2/interface/Screenshot.cpp), `CreateRT`, `RenderViewport`, `ScreenshotGiant`: retain projected bounds, zoom/rotation/flags, transparent-background initialization, indexed palette PNG contract and synchronous completion/error messages. Large screenshots may exceed GPU image limits: implement deterministic tiled rendering/readback with unchanged global paint coordinates and correct clipped filtering/order, then assemble bytes. Validate tile seams against an untiled oracle. Failing existing legal giant screenshots solely because they exceed one GPU image is a support regression.
- [`ParkPreview.cpp`](../src/openrct2/park/ParkPreview.cpp), preview generation: retain fixed dimensions, selected camera and owned indexed `PreviewImage`; the later temporary G1 upload must continue to see those pixels. Reuse the auxiliary service domain across previews.
- [`TrackDesign.cpp`](../src/openrct2/ride/TrackDesign.cpp), `TrackDesignDrawPreview`: `StashMap`/temporary preview map/ride/`UnstashMap` span four rotations and contiguous 370×217 images. Record each target independently or as an explicit four-output job; do not publish the temporary map into main `PresentationScene`. Finish sealing/copying all CPU dependencies before restoring the map, and preserve cleanup on every error. GPU work must never follow mutable map/ride pointers after restore. Use synchronous completion initially; optimize only after isolation tests.
- [`CustomImages.cpp`](../src/openrct2-ui/scripting/CustomImages.cpp), `JSDrawCustomImage`: retain existing-image contents, resize/RLE conversion, clipping/text/filter ordering, callback completion, G1 replacement/invalidation and transparency flags. [`ScGraphicsContext.hpp`](../src/openrct2-ui/scripting/ScGraphicsContext.hpp) delegates draw/clear/clip through drawing APIs, but callbacks can invoke other image APIs or recursively draw; characterize intermediate observation and self-image reads before batching an entire callback. Flush/read back at observable boundaries if needed, never silently change script semantics. Publish completed CPU indices atomically to the asset registry and invalidate the correct residency generations after the job succeeds.
- [`DummyUiContext.cpp`](../src/openrct2/ui/DummyUiContext.cpp), `X8DrawingEngineFactory`: separate no-drawing behavior from image production. Rendering-independent work needs no fake canvas/renderer. Screenshot/image commands ask the injected service explicitly; do not make dummy-context construction eagerly create Vulkan.

## Minimal migration sequence and evidence

1. **Contract and lifetime fixtures.** Inventory/lock existing caller results and error behavior using the frozen oracle: normal/transparent/giant screenshots, repeated previews, all track rotations, nested/custom-image self reads and callback failures. Add service contract tests for submission IDs, cancellation, ownership, and no initialization on non-rendering commands. No production caller switches yet.
2. **Module extraction without behavior change (C01 first half).** Move recorder/backend implementation and shader build ownership once into the sibling renderer target; leave SDL adapter/frontend in UI. Inject a factory through core contracts and link UI/CLI at composition. Ensure no core-to-UI include/link and no duplicate pipeline copies. Re-run existing main/primitive/scaling/scene evidence after file moves.
3. **Device/presentation split (C01 completion).** Extract device/queue and slot ownership; pass surface requirements from the UI adapter. Main rendering still uses its existing WSI path, shaders and mailbox. Run validation plus acquire/abandon/resize/shutdown cases before adding surface-free output.
4. **Surface-free executor (C02).** Explicit target descriptors, indexed-only and RGBA output, initial-index upload, named fence/readback ownership. Test in a process with no SDL video initialization and no surface creation. Replay identical commands against windowed and offscreen targets, compare exact bytes, and visually inspect any divergence. Exercise interleaved differently sized/paletted targets and main/offscreen concurrency.
5. **First caller and pooling (C03/C04).** Migrate park previews first, then ordinary CLI screenshots using the same service. Assert one device initialization over hundreds of previews, bounded retained target/atlas memory, and unchanged main-frame residency. Keep legacy callers frozen until each independently passes.
6. **Large/temporary/script callers (C05–C08).** Add giant-image tiling; migrate four-view temporary track previews with scoped world cleanup; then script image editing with measured observable boundaries. Test nested invocation during an unsealed main draw, resize/device loss during waits, invalidation/reuse of the edited image, and temporary-world isolation. Retain exact external API semantics.
7. **Remove remaining auxiliary X8 factories only after caller gates pass.** Run CLI/non-rendering startup without display/device, all migrated image contracts, full final-screen fixtures and the software-free source/build dependency audit. This sequence alone does not authorize switching the default or deleting the frozen reference harness.

The approved ownership boundary is the injected core contract, sibling renderer module, one device with distinct main/pooled-auxiliary resource domains, and one serialized submission owner. Changing script observation, temporary-world publication or giant-image behavior remains a behavioral decision, not a mechanical backend substitution.

## First implementation slice: core contract and lazy ownership

`src/openrct2/drawing/RenderService.h/.cpp` now defines renderer-neutral requests, sessions, results, service/factory interfaces,
named completion state and a lazy context-owned holder. The request owns initial indices and an RGBA palette, specifies logical
and physical output extents, clear-versus-existing contents, alpha/scaling/lighting policy and indexed/RGBA output selection.
The session contract retains drawing-context/target access for transitional callers and requires valid owned address storage,
isolated temporary-world capture, reliable submission and immutable dependencies before submission returns.

Completion identity includes name, submission ID, target ID and target generation. Results own tightly packed bytes and remain
valid after completion objects or pooled targets are released. Wrong identity/generation or output size becomes a named error;
it cannot return a previous target's image. Wait timeouts are nonterminal, cancellation suppresses delivery, first terminal
outcome wins, and a registered submission worker cannot block waiting for its own pending result. **GPU fences, leases and
resource retirement remain the submission job's responsibility** after timeout/cancellation; this foundation has no GPU job
implementation and does not claim fence/device-loss/queue integration is complete.

`CreateContext` accepts an optional abstract factory as its fourth argument; existing call sites keep their defaults.
`IContext::GetRenderService()` is the explicit lazy image-operation entry point. Ordinary context construction/initialization
does not invoke the factory. Missing service and failed creation have typed errors; repeated failures do not repeatedly
initialize a device. Context shutdown retires an already-created service before unloading assets, and discards an unused factory
without initializing SDL video, a Vulkan loader or a device. No existing screenshot/preview/script caller has switched renderer.

`RenderServiceTests.cpp` covers request bounds/initial contents, owned result lifetime, stale generation, malformed output,
nonterminal timeout and waiter delivery, cancellation with an independently retained submitted-job lease, device-loss/shutdown
error delivery, worker self-wait rejection, unused lazy factory, creation-once/shutdown-once, missing/failed factories,
cross-thread/recursive creation rejection and shutdown during factory creation. These are contract tests, not simulated proof of
a Vulkan backend. Coordinated build 26/run 22 passed all 147 selected tests, including these 13 contract tests, with clean
validation; actual surface-free service implementation and device/resource integration tests remain outstanding.

The user's later performance requirement makes CPU-generated per-object paint commands transitional. The final renderer must
retain scene/assets in VRAM, consume state/delta updates and execute expensive per-world-object graphical work in shaders. This
compatibility session interface does not qualify that goal or require a permanent CPU paint stream. Backend extraction, retained
scene ownership and measured CPU/bandwidth/render/TPS gates remain outstanding architecture and performance work.
