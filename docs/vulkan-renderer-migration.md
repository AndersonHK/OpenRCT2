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
- `vulkan/VulkanDevice.*` owns SDL Vulkan-loader discovery, the instance and surface, physical-device selection, graphics and
  presentation queues, swapchain negotiation, three frames in flight, and per-frame persistently mapped upload rings.
- Device selection accepts integrated GPUs and portability devices but prefers discrete hardware. MoltenVK portability
  enumeration and the portability-subset device extension are enabled when advertised.
- Swapchain presentation supports FIFO VSync and mailbox/immediate uncapped modes, high-DPI drawable extents, minimisation, and
  deferred recreation.
- `vulkan/VulkanResources.*`, `VulkanPalettePipeline.*`, and `VulkanBackend.*` own the indexed atlas/canvases, palette upload,
  final palette pass, API-neutral frame/upload surface, and explicit capability gate. The backend can construct and present a
  cleared indexed canvas. `VulkanLinePipeline.*` now consumes the shared packed line-command ABI directly from the per-frame
  upload ring, depth-tests it into `R8_UINT`, and hands the result to the palette pass. Rectangle, transparency, weather, and
  readback commands remain rejected, so gameplay selection stays gated.
- SPIR-V loading is shared by the line and palette pipelines through `VulkanShader.*`; shader file validation and module
  creation are no longer duplicated in each pipeline.
- CMake and MSBuild compile Vulkan GLSL to SPIR-V with `glslc`. Packagers may provide matching checked-in `.spv` files beside
  the GLSL as a toolchain fallback. There is intentionally no runtime GLSL compiler: a Vulkan-capable build without either
  `glslc` or the complete precompiled shader set disables the foundation instead of failing later during renderer
  initialisation.
- `DrawingEngine::Vulkan` is reserved after the existing enum values, so future activation does not renumber old configuration.
  It is deliberately absent from configuration, the options UI, and the drawing-engine factory unless the internal
  `ENABLE_VULKAN_DRAWING_ENGINE` parity gate is defined.

## Staged implementation

### 1. Indexed composition and presentation

Complete the reserved `DrawingEngine::Vulkan` backend and an `IDrawingContext` that records the shared command stream. Add:

- an `R8_UINT` indexed canvas and depth image;
- device-local sprite-atlas arrays with a persistently mapped staging ring;
- one instanced opaque rectangle pipeline and one line pipeline;
- a palette-composition pass directly into the swapchain image;
- GPU weather generation from the shared region commands;
- asynchronous screenshot readback through a per-frame transfer buffer.

At this point Vulkan replaces the current OpenGL feature set while preserving exact palette indices and draw depth.

### 2. Texture residency without first-use stalls

The current OpenGL cache converts and uploads an image synchronously inside `DrawSprite`. Vulkan should instead:

- allocate large device-local atlas arrays once;
- keep stable image-to-atlas metadata in RAM;
- gather missing-image requests while recording a frame;
- copy all decoded pixels into the current frame upload ring;
- issue one transfer-command batch followed by one transfer-to-fragment barrier;
- retain two recent park-view working sets and prewarm the previous frame's set after a park load or renderer switch;
- reserve a separate transient atlas for TTF surfaces so normal sprite residency is never disturbed.

No GPU-to-CPU atlas readback is permitted. Growing an atlas allocates a new image and uses GPU image copies, then retires the old
allocation after the owning frame fence signals.

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

Replace OpenGL depth peeling with a Vulkan-specific indexed-colour composition strategy. Candidate implementations should be
benchmarked on EverythingPark rather than selected theoretically:

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
