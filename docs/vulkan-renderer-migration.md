# Vulkan-first renderer migration

## Target

The renderer must stop competing with the 320-TPS simulation target. Turbo asks the simulation for one logical tick every
3.125 ms, so presentation must be asynchronous, bounded, and disposable: when the GPU has not consumed an old visual frame,
the renderer should drop or replace that visual work instead of delaying simulation.

The end state is a Vulkan renderer on Windows and Linux, with the same Vulkan command path running through MoltenVK on macOS.
SDL2 owns the native window and creates the platform surface. This avoids three independent window-system implementations and
uses the portability mechanisms already documented by SDL for Apple platforms.

OpenGL remains a compatibility and visual-reference backend during migration. It is not the architecture to extend after
Vulkan reaches parity.

## Foundation now present

- `gpu/GpuCommandStream.h` defines backend-neutral, tightly laid-out line, rectangle, transparency, weather, and texture-upload
  commands. OpenGL consumes these structures now, so Vulkan can consume the same ordering without duplicating paint traversal.
- `gpu/GpuCommandDrawingContext.*` records that ABI directly from `IDrawingContext`, including clipping, zoom sprites, masks,
  remaps, indexed transparency, glyphs, TTF layers and stable depth sequence. Its render-target allocation preserves legacy
  sub-target pointer offsets only; those bytes are never rasterised or uploaded as a canvas.
- `gpu/GpuTextureCache.*` owns persistent power-of-two atlas slots, image generations and queued first-use uploads. Normal
  sprites and palette-rendered glyphs upload only on first use or invalidation. Invalidations requested during recording are
  applied after that frame's command and upload streams are complete, so an atlas slot referenced earlier in the frame cannot be
  replaced underneath it. TTF bitmaps receive unique transient slots for the frame instead of cycling synthetic image ids.
- `vulkan/VulkanDevice.*` owns SDL Vulkan-loader discovery, the instance and surface, physical-device selection, graphics and
  presentation queues, swapchain negotiation, three frames in flight, and per-frame persistently mapped upload rings.
- Device selection accepts integrated GPUs and portability devices but prefers discrete hardware. MoltenVK portability
  enumeration and the portability-subset device extension are enabled when advertised. Activation requires Vulkan 1.1, a
  colour-attachment-capable surface, the exact sampled/attachment/transfer features used by `R8_UINT`, `R16_UINT`,
  `D32_SFLOAT`, and `R8G8B8A8_UNORM`, and limits sufficient for the 2048-pixel, 64-layer atlas.
- Swapchain presentation supports FIFO VSync and mailbox/immediate uncapped modes, high-DPI drawable extents, minimisation, and
  deferred recreation.
- `vulkan/VulkanResources.*`, `VulkanPalettePipeline.*`, and `VulkanBackend.*` own the indexed atlas/canvases, palette and remap
  uploads, final palette pass, API-neutral frame/upload surface, and explicit capability gate. `VulkanLinePipeline.*` and
  `VulkanRectPipeline.*` consume the shared packed command ABI directly from the per-frame upload ring. Lines clear the indexed
  colour and depth targets; opaque sprite, mask, crosshatch, solid-fill, TTF, and one/two/three-remap rectangles then load those
  targets and preserve command depth. `VulkanTransparencyPipeline.*` uses the established overlap bound to depth-peel indexed
  transparent rectangles into `R16_UINT`, applies remap/blend tables into ping-pong `R8_UINT` canvases, and preserves recorder
  order. `VulkanWeatherPipeline.*` then writes the legacy rain/snow pattern directly into the final indexed canvas. Readback and
  direct drawing-context code are present, while production renderer selection remains gated.
- SPIR-V loading is shared by all Vulkan pipelines through `VulkanShader.*`; shader file validation and module creation are no
  longer duplicated in each pipeline.
- CMake and MSBuild compile Vulkan GLSL to SPIR-V with `glslc`. Packagers may provide matching checked-in `.spv` files beside
  the GLSL as a toolchain fallback. There is intentionally no runtime GLSL compiler: a Vulkan-capable build without either
  `glslc` or the complete precompiled shader set disables the foundation instead of failing later during renderer
  initialisation.
- MSBuild derives `EnableVulkan`, both drawing-context gates, shader discovery, and Vulkan link paths once in
  `openrct2.vulkan.props`, shared by the core, UI, executable, and data projects. Requesting the direct context without the
  Vulkan drawing engine is a build error. Native Windows Vulkan is currently enabled only for Win32 and x64; Windows ARM64
  remains intentionally outside this activation boundary until its SDK library and runtime path are validated.
- On Windows, `openrct2-win` links `$(VulkanLibraryDir)\vulkan-1.lib` explicitly. The Vulkan SDK's whole `Lib` directory must
  not be prepended to general library search paths: that can select the SDK's `/MD` SDL2 ahead of the repository's
  `SDL2-static.lib` and create an unrelated runtime-library conflict.
- `DrawingEngine::Vulkan` is reserved after the existing enum values, so future activation does not renumber old configuration.
  `ENABLE_VULKAN_DRAWING_ENGINE` now exposes an explicit validation-only factory/configuration path. Normal builds keep it off.
- Indexed readback copies the final post-transparency/weather canvas into the current persistently mapped frame ring. Consumers
  poll by request id without waiting; unread results are harvested before their fenced frame slot is reused. Host-visible
  coherent memory is preferred but not required: non-coherent rings use atom-aligned flushes before queue submission and
  invalidation only after the owning frame fence completes. Host access to frame fences is serialised for
  render-thread/consumer safety. Presentation itself still has blocking waits described below.
- The validation drawing-engine bridge currently uses the established X8 indexed context and uploads its canvas through the
  current frame's staging ring.
  This exercises Vulkan window creation, resize, palette, swapchain, presentation and screenshot lifecycle without changing the
  default renderer. It remains the fallback when `ENABLE_VULKAN_DIRECT_DRAWING_CONTEXT` is off. Enabling that second gate, which
  is also off by default, selects the direct recorder and persistent atlas path with no routine framebuffer upload or readback.
  The direct path redraws a complete command list and does not advertise dirty-region `CopyRect` support yet.
- Resize rejects an active frame, waits once for the device, harvests completed readbacks, then rebuilds canvas-dependent
  pipelines and descriptors as one lifecycle. A failed rebuild leaves the backend explicitly not ready instead of allowing a
  partially rebuilt frame to begin.

The compile-time gates are still required. The current drawing engines call `BeginFrame` on the caller and the device waits with
an infinite timeout for both the frame-slot fence and swapchain image acquisition. That is suitable for validation bring-up, but
it is not the asynchronous, disposable presentation contract described by this document: a render thread, immutable visual
snapshot handoff, non-blocking frame-slot policy, and superseded-frame dropping must land before production activation. An
explicit active-frame cancellation path is also still required so upload exhaustion or command-recording failure cannot leave an
acquired swapchain image and semaphore stranded.

## Indexed colour and output contract

The `R8_UINT` canvas, `R8_UINT` sprite atlas, 256-by-256 remap and blend tables, and 256-entry RGBA8 palette remain the source of truth.
Opaque drawing never expands sprites to RGBA and never changes their palette indices. The last fullscreen pass is the only place
where an index becomes an output colour.

SDR is the default. The device first requests an advertised `B8G8R8A8_UNORM` or `R8G8B8A8_UNORM` surface with the normal sRGB
colour space, preserving the legacy encoded palette bytes directly. If only an sRGB attachment is available, the palette pass
decodes the palette to linear values so the attachment's fixed-function sRGB encoding produces the same final bytes.

`Hdr10IfAvailable` is an internal backend policy, not a user-facing setting. It selects HDR only when the surface advertises a
10-bit `A2B10G10R10`/`A2R10G10B10` and `HDR10_ST2084` pair. The palette pass then decodes sRGB, converts linear sRGB primaries to
BT.2020, scales the established palette white to the configured paper-white luminance, and applies the ST 2084 transfer function.
If that exact pair is unavailable, initialisation continues in SDR. The indexed source data is never promoted or rewritten.

Windows and Linux use native Vulkan. macOS uses this same Vulkan resource and shader path through MoltenVK; there is no separate
Metal renderer or Metal-specific colour-composition implementation. HDR availability is therefore a runtime surface capability,
not an OS assumption.

Production HDR10 activation follows the existing abstraction rather than adding a second renderer:

1. retain SDR as the default and expose `Hdr10IfAvailable` only after Vulkan visual parity;
2. report both HDR10 surface support and the active swapchain mode, including changes after monitor moves or swapchain recreation;
3. publish display mastering and content-light metadata through `VK_EXT_hdr_metadata` when the device advertises it, without
   treating metadata support as permission to use an unadvertised colour-space/format pair;
4. validate SDR fallback, paper-white mapping, BT.2020/PQ output, 10-bit gradients, palette animation, screenshots, and live
   monitor transitions on native Vulkan and MoltenVK;
5. keep screenshots explicitly tagged or converted to SDR until the readback API carries colour-space metadata.

## Staged implementation

### 1. Indexed composition and presentation

The backend, validation factory path, direct `IDrawingContext` recorder, persistent atlas cache and asynchronous indexed readback
are present behind two compile-time gates. The validation bridge retains its full-canvas upload as an independent lifecycle and
comparison fallback; the direct gate does not invoke it. Immediate parity work includes visual comparison coverage, an explicit
screenshot request adapter, and evidence that full-list command recording is preferable before adding GPU damage metadata;
production activation also depends on the non-blocking frame handoff and cancellation gates above. The
synchronous `IDrawingEngine::Screenshot()` contract uses the bridge's already-authoritative CPU canvas; the direct recorder
currently returns no stale screenshot and must schedule the asynchronous Vulkan readback for that explicit consumer.

### 2. Texture residency without first-use stalls

The direct Vulkan cache now gathers missing images during command recording, owns stable generation-aware atlas metadata, and
copies pending pixels into the current frame upload ring immediately before submission. The device-local 64-layer array is
allocated once, and each frame's queued misses are emitted inside one transfer/barrier section. Transient TTF allocations share
the atlas image but are retired after their frame stream is assembled; persistent entries remain pinned through any same-frame
invalidation. Follow-up work should:

- retain two recent park-view working sets and prewarm the previous frame's set after a park load or renderer switch;
- measure whether TTF traffic warrants a separate transient atlas so normal sprite residency is never disturbed.

No GPU-to-CPU atlas readback is permitted. Layer exhaustion is an explicit activation failure rather than a hidden atlas resize
or CPU round-trip; measurements should determine whether the fixed reserve changes before renderer activation.

### 3. GPU visibility, transforms, and sorting

Move the expensive visible-world preparation out of per-sprite CPU calls:

- upload compact tile, entity, and paint-session records into storage buffers;
- cull tiles and entities in compute against each viewport rectangle;
- expand paint records into indirect rectangle commands in compute;
- radix-sort opaque records by depth and stable sequence on GPU;
- use a second indirect buffer for masks, remaps, water, and translucent records;
- preserve window and widget commands as an inexpensive CPU-generated overlay stream.

Simulation state remains authoritative and deterministic. GPU ordering affects presentation only and must use the existing stable
paint sequence as its final tie-breaker.

### 4. Transparency and palette effects

The parity implementation deliberately retains bounded depth peeling first: it shares the current overlap estimator, renders
one deterministic indexed layer at a time, and composes through the remap and blend tables without leaving indexed colour. This
provides a comparison baseline before choosing a more ambitious replacement. Candidate replacements must be benchmarked on
EverythingPark rather than selected theoretically:

- per-pixel linked lists in storage buffers;
- bounded per-pixel fragment arrays sized from the current transparency-depth estimator;
- depth-sorted indirect transparent commands with palette lookup in a subpass.

Weather gloom, lightning, palette animation, smooth scaling, and light effects become full-screen compute or fragment passes.
These passes operate on resident images and small uniform blocks; they must not trigger CPU canvas conversion or full-frame bus
uploads.

### 5. Parallel frame preparation

Use one render thread for Vulkan submission and resource retirement. Worker threads may build independent viewport/window command
ranges in per-thread arenas, followed by a stable concatenation step. The simulation thread publishes an immutable visual
snapshot and continues. At Turbo, the render thread consumes the newest available snapshot and discards superseded ones.

The simulation must never wait for a presentation fence except during resize, renderer shutdown, screenshot capture, or explicit
debug validation.

## Parity and deletion gates

Delete CPU and legacy paths only after Vulkan comparison captures cover normal, rain, snow, night, lightning, transparency,
multiple viewports, zoom levels, screenshots, and UI scaling.

After indexed composition parity:

- delete HardwareDisplay palette conversion and its 32-bit shadow buffer;
- delete OpenGL-only weather composition and shader files;
- remove the macOS CPU `CopyRect` readback fallback;
- remove per-frame OpenGL buffer orphaning and GL state wrappers.

After GPU paint preparation parity:

- delete per-sprite `CalculateClipping` work and its cache;
- remove synchronous `GetOrLoadImageTexture` calls from draw recording;
- remove CPU weather-pixel storage and restoration;
- remove CPU viewport sprite expansion for world layers, retaining only the deterministic paint-record producer;
- retire OpenGL as a selectable backend once Vulkan/MoltenVK coverage is established on Windows, Linux, Intel Mac, and Apple
  Silicon.

## Verification

Performance testing uses the EverythingPark Turbo workload and reports simulation TPS separately from render-thread time. Record:

- actual simulation TPS and 320-TPS percentage;
- CPU time producing visual snapshots and command streams;
- GPU frame time by pass;
- upload bytes, atlas misses, and staging-ring high-water marks;
- submitted, culled, and indirectly drawn command counts;
- frames presented, skipped, and superseded;
- p50, p95, and p99 presentation latency.

Visual validation should hash the indexed canvas before palette application where possible. RGBA comparison allows a zero-difference
mode and an annotated tolerance mode for platform rasterisation differences. Neither benchmark is allowed to block the simulation
on VSync.
