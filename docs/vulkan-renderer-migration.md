# Vulkan-first renderer migration

## Target

The renderer must stop competing with the 360-TPS simulation target. Presentation must be asynchronous, bounded, and
disposable: when the GPU has not consumed an old visual frame,
the renderer should drop or replace that visual work instead of delaying simulation.

The end state is a Vulkan renderer on Windows and Linux, with the same Vulkan command path running through MoltenVK on macOS.
SDL2 owns the native window. Linux and macOS use SDL's Vulkan WSI bridge; Windows uses a narrow native adapter over SDL's HWND
because the repository's current static SDL dependency has no Windows Vulkan video-driver hooks. This keeps one renderer and
one window owner while preserving SDL's portability mechanisms on Apple platforms.

The duplicate OpenGL backend has been retired. Old `OPENGL` configurations migrate to the SDL hardware-presented software
renderer, while Vulkan retains explicit numeric value 2 to avoid renumbering existing configurations.

## Foundation now present

- `gpu/GpuCommandStream.h` defines backend-neutral, tightly laid-out line, rectangle, transparency, and weather commands.
  First-use texture uploads carry owned indexed pixels beside their atlas metadata; they are intentionally not a packed native
  ABI because they must survive a recorder/render-thread handoff before Vulkan chooses a staging-ring offset.
- `gpu/GpuCommandDrawingContext.*` records that ABI directly from `IDrawingContext`, including clipping, zoom sprites, masks,
  remaps, indexed transparency, glyphs, TTF layers and stable depth sequence. Its render-target allocation preserves legacy
  sub-target pointer offsets only; those bytes are never rasterised or uploaded as a canvas.
- `gpu/GpuTextureCache.*` owns persistent power-of-two atlas slots, image generations and queued first-use uploads. Normal
  sprites, palette-rendered glyphs, and cached TTF surfaces upload only on first use or invalidation. TTF atlas residency is
  keyed by an allocation-independent surface identity and bounded to the CPU TTF cache's 256-entry working set. Invalidations
  requested during recording are
  applied after that frame's command and upload streams are sealed. Every resolved normal sprite, mask, glyph, and TTF bitmap
  records the allocation in a cache-owned residency lease. Invalidated and retired slots become reusable only after all
  leases referencing their allocation serial retire, so a recycled atlas coordinate cannot change beneath a sealed frame.
- `vulkan/VulkanDevice.*` owns platform Vulkan-loader discovery, the instance and surface, physical-device selection, graphics and
  presentation queues, swapchain negotiation, three frames in flight, and per-frame persistently mapped upload rings.
- Device selection accepts integrated GPUs and portability devices but prefers discrete hardware. MoltenVK portability
  enumeration and the portability-subset device extension are enabled when advertised. Activation requires Vulkan 1.1, a
  colour-attachment-capable surface, the exact sampled/attachment/transfer features used by `R8_UINT`, `R16_UINT`,
  `D32_SFLOAT`, and `R8G8B8A8_UNORM`, and limits sufficient for the 2048-pixel, 64-layer atlas.
- Swapchain presentation prefers MAILBOX for tear-free newest-frame VSync, falls back to mandatory FIFO, and uses Immediate for
  uncapped mode. It also supports high-DPI drawable extents, minimisation, and deferred recreation.
- `vulkan/VulkanResources.*`, `VulkanPalettePipeline.*`, and `VulkanBackend.*` own the indexed atlas/canvases, palette and remap
  uploads, final palette pass, API-neutral frame/upload surface, and explicit capability gate. `VulkanLinePipeline.*` and
  `VulkanRectPipeline.*` consume the shared packed command ABI directly from the per-frame upload ring. Lines clear the indexed
  colour and depth targets; opaque sprite, mask, crosshatch, solid-fill, TTF, and one/two/three-remap rectangles then load those
  targets and preserve command depth. `VulkanTransparencyPipeline.*` uses an exact coordinate-compressed overlap sweep to depth-peel indexed
  transparent rectangles into `R16_UINT`, applies remap/blend tables into ping-pong `R8_UINT` canvases, and preserves recorder
  order. `VulkanWeatherPipeline.*` then writes the legacy rain/snow pattern directly into the final indexed canvas. Readback and
  direct drawing-context code are present.
- Direct-renderer device initialisation deliberately does not inspect graphics-derived lookup data. OpenRCT2 creates the drawing
  engine before loading base graphics and initialising LightFX, so remap, blend, and baked-light tables are captured once at the
  first real `BeginDraw`. A versioned presentation snapshot transfers that one-time upload
  to the backend-owning worker; capture failures leave readiness false and retry safely on the next draw.
- SPIR-V loading is shared by all Vulkan pipelines through `VulkanShader.*`; shader file validation and module creation are no
  longer duplicated in each pipeline.
- CMake and MSBuild compile Vulkan GLSL to SPIR-V with `glslc`. Packagers may provide matching checked-in `.spv` files beside
  the GLSL as a toolchain fallback. There is intentionally no runtime GLSL compiler: a Vulkan-capable build without either
  `glslc` or the complete precompiled shader set disables the foundation instead of failing later during renderer
  initialisation.
- MSBuild derives `EnableVulkan`, renderer and worker activation, shader discovery, and Vulkan link paths once in
  `openrct2.vulkan.props`, shared by the core, UI, executable, and data projects. A usable Vulkan build enables the renderer and
  worker by default; explicitly requesting them without the SDK is a build error. Native Windows Vulkan is currently enabled only for Win32 and x64; Windows ARM64
  remains intentionally outside this activation boundary until its SDK library and runtime path are validated.
- On Windows, `openrct2-win` links `$(VulkanLibraryDir)\vulkan-1.lib` explicitly. The Vulkan SDK's whole `Lib` directory must
  not be prepended to general library search paths: that can select the SDK's `/MD` SDL2 ahead of the repository's
  `SDL2-static.lib` and create an unrelated runtime-library conflict.
- The Windows dependency bundle currently comes from OpenRCT2/Dependencies v41 and contains static SDL 2.32.8 built with only
  its core vcpkg feature; `SDL_VIDEO_VULKAN` is disabled. The binary therefore cannot be repaired by changing `SDL_config.h`.
  `VulkanPlatform.*` omits `SDL_WINDOW_VULKAN`, obtains the SDL window's HWND through `SDL_GetWindowWMInfo`, creates
  `VK_KHR_win32_surface` directly, and measures the per-monitor-DPI-aware client rectangle for the swapchain extent. Other
  platforms continue to call SDL's Vulkan loader, extension, surface, and drawable-size APIs. The long-term dependency-only
  alternative is to request `sdl2[vulkan]` in OpenRCT2/Dependencies, publish a new multi-architecture bundle, and then update
  this repository's dependency URLs and integrity hash together; no existing v41 artifact provides that replacement.
- Cold startup now establishes the configured window mode before creating the Vulkan surface and swapchain, then seeds the
  drawing engine with the actual logical and drawable dimensions. Exclusive fullscreen is strict: the configured width and
  height are resolved to a complete SDL display mode including pixel format and refresh rate, installed with
  `SDL_SetWindowDisplayMode`, and verified after `SDL_SetWindowFullscreen`. Failure is an explicit startup error; it does not
  silently substitute desktop fullscreen, windowed mode, or another renderer. A top-level application boundary logs and displays
  the exact exception while preserving normal renderer-worker, device, and window destruction instead of calling `exit(1)`.
- DPI inference no longer assumes every renderer owns an SDL renderer. Vulkan queries its platform drawable extent, and the
  one-shot setting is cleared only after a valid physical/logical size ratio was obtained.
- Zero-area G1 elements are valid no-op placeholders in the software renderer. The direct GPU recorder now rejects them before
  emitting a rectangle command, and the texture cache repeats that guard before rasterisation/allocation. It still fails loudly
  for positive-sized images larger than the atlas. This distinction removed the first-draw failure on EverythingPark image
  `268807` (`0x0`) without widening the persistent atlas or hiding malformed oversized assets.
- `DrawingEngine::Vulkan` remains after the existing enum values, so activation does not renumber old configuration. A build
  with usable Vulkan dependencies exposes the factory path by default.
- GPU timing uses one optional timestamp-query pool per frame slot. Five timestamp points divide uploads, indexed drawing,
  LightFX, and final palette composition while also reporting total GPU frame time. Results retain their frame-slot ownership,
  handle queue-counter wrapping through `timestampValidBits`, and convert ticks with the physical device's `timestampPeriod`.
  The backend reads them without `WAIT_BIT` only after the existing slot fence is complete, normally just before that slot is
  reused, and publishes the merged CPU/GPU sample through the drawing-engine abstraction. Unsupported renderers and Vulkan
  queues report timing as unavailable rather than introducing a synchronous query readback. Routine benchmark collection takes
  completed samples from a fixed 256-record backend ring into caller-owned reusable storage without touching Vulkan or allocating
  after warm-up. The ring may overwrite stale samples only when no consumer has polled for 256 completed presentations; the
  integrated benchmark polls after every measured draw. Warm-up and measurement endpoints use an explicit
  render-worker mailbox boundary: only there does the worker wait idle, harvest every submitted frame-slot tail, and return the
  remaining samples so work cannot cross benchmark phases. CPU timing also reports the duration of the `vkQueuePresentKHR` call
  itself; it is deliberately named present-call time rather than claiming that all driver time in that call is a display wait.
- Direct screenshots are the sole synchronous readback adapter. They present an attached pending visual first when necessary,
  wait for its frame slot, copy the final indexed canvas through that slot's host-visible ring, and pass the owned bytes to the
  established indexed PNG writer. This preserves the established SDR palette image even when the presentation swapchain uses
  HDR10. As with the X8 screenshot contract, the indexed capture precedes display-only per-pixel LightFX composition. It does not
  add a routine framebuffer readback or make presentation itself synchronous.
- The Vulkan drawing engine always uses the direct recorder and persistent atlas path; the former X8 canvas-upload bridge and
  its full-frame CPU-to-GPU transfer contract have been deleted. The direct path redraws a complete command list and deliberately
  does not advertise dirty-region `CopyRect` support. Existing
  viewport scrolling calls `CopyRect` only when the engine advertises dirty optimisations, so this is not an activation blocker
  for the full-redraw command path.
- Resize rejects an active frame, waits once for the device, then rebuilds canvas-dependent
  pipelines and descriptors as one lifecycle. A failed rebuild leaves the backend explicitly not ready instead of allowing a
  partially rebuilt frame to begin.

The dependency gate remains required. Frame acquisition has explicit blocking and `SkipIfBusy` policies, and the drawing engine
selects the latter. A busy frame-slot fence or swapchain image therefore skips presentation instead of waiting on
the simulation/UI caller. An explicit abandonment path drains the acquired image semaphore, invalidates the swapchain, and keeps
persistent atlas uploads pending for retry when recording fails. Resize, shutdown, recovery and explicit screenshots remain the
intentional blocking boundaries.

Swapchain recreation is now coordinated by the backend rather than destroying image views inside frame acquisition. After queued
work completes, the palette pass releases its framebuffers and render-pass objects first; only then does the device replace the
old views. This ordering applies equally to resize, VSync changes, out-of-date surfaces, HDR/SDR transitions and abandonment.

Atlas uploads are now staged and recorded in one traversal. The backend no longer allocates a temporary new-texture vector or
walks every upload twice per frame, and palette, LightFX, and index-table transfers share one synchronization/copy path.
Logical-device setup uses its fixed two-family queue array directly, backend-owned timestamp operations avoid recursive locking,
and the remaining device lock is a normal mutex. These changes keep upload ownership explicit while reducing CPU preparation for
the command stream; atlas pixels remain in GPU-local memory after their first successful upload.

This is not yet the complete asynchronous contract: paint traversal and direct command recording still run on the caller. A
semantic visual snapshot and stable worker-range concatenation must still land before paint preparation itself can leave that
thread. Skipping GPU acquisition prevents presentation back-pressure, but it does not by itself remove CPU paint preparation for
a frame that is ultimately skipped.

The retained-scene migration now has two ownership foundations. Ordinary opaque sprite commands reference a persistent GPU
asset descriptor rather than repeating atlas geometry in every command; the descriptor is uploaded transactionally with the
atlas allocation and remains valid for the frame-residency lease. Separately, `EntityRegistry` publishes owned, sorted visual
changes identified by `{ reset epoch, entity id, slot generation }`. Creation, authoritative movement, removal, same-tick ID
reuse, and whole-park reset therefore coalesce at the registry instead of requiring a renderer to pull every entity or trust an
immediately reusable ID. Presentation-only tween movement is deliberately excluded. These records do not yet replace world
paint: entities and terrain share the same painter ordering, so their eventual persistent buffers must enter one unified
world-space sort rather than an incorrect entity-only overlay.

The next boundary is a frozen main-viewport scene: resolve all live entity/tile visibility while the paint session is owned,
then publish pointer-free world records to a latest-only worker. That worker may cull, sort, and construct indirect draws while
simulation continues; a missed deadline reuses the previous completed visual state. Scene reset, object unload, renderer
switch, screenshot, resize, and shutdown remain explicit drain or generation-discard boundaries.

### Refresh-paced scheduler integration

Turbo no longer feeds the renderer from a 15 FPS throttle after an indivisible offline scene batch. The scheduler yields
only between completed deterministic logical updates, checks an anchored deadline derived from SDL's current display refresh
rate, and pumps window input and UI work before recording a due frame. This caps the CPU producer to the monitor's long-run
cadence. The one-pending `LatestFrameMailbox` replaces stale CPU packets, while MAILBOX present mode applies the same
newest-frame policy to the swapchain queue. The render worker remains the exclusive owner of Vulkan submission and any
driver-side `vkQueuePresentKHR` blocking.

On the 144 Hz validation display, two warmed EverythingPark/VSync runs produced 144.018 frames per second with one completed
renderer timing sample per produced frame. GPU work averaged 702.754/704.099 us and complete caller-side paint/presentation
averaged about 1.74 ms. Logical throughput was 264.897/265.265 TPS because the new policy deliberately spends about 25% of wall
time producing 721-722 frames instead of 67. The hidden benchmark cannot certify visible compositor behavior, and a logical
compositor row is therefore available through `--benchmark-visible`. A logical tick longer than 6.94 ms remains non-preemptible.
A hard UI guarantee across such a tick requires an immutable visual snapshot
between a simulation producer and the UI/command-recording consumer; that is the next architectural boundary, not a reason to
mutate live game state concurrently.

The July 11 consolidation checkpoint retains that presentation contract: a five-second hidden Vulkan/VSync run produced 720
draws at 143.746 FPS and 249.159 logical TPS. Frame intervals were 7.296 ms p50, 8.552 ms p95, 9.518 ms p99, and 11.995 ms
maximum. GPU work averaged 122.139 us per frame; renderer submission and presentation averaged 134.889 and 223.967 us,
respectively. Simulation still consumed 73.6% of wall time, so the dominant remaining smooth-Turbo opportunity is a safe visual
snapshot boundary and further simulation-side parallelism rather than present-call tuning.

The retained-canvas checkpoint adds a device-local indexed base image containing the scene after opaque/transparent world
composition and before weather. Presented-frame serials acknowledge damage; superseded or failed latest-frame packets do not.
Sparse frames restore that base, clear and rebuild dirty regions, save the updated base, and then apply frame-local effects.
Dense damage crosses over to one complete traversal when more than half the 64-by-64 damage cells are dirty, avoiding repeated
viewport setup on EverythingPark. Camera copies currently request a complete redraw until an ordered GPU blit is introduced.

The same checkpoint fixes the independent presentation deadline that caused a 132 FPS plateau on otherwise idle 144 Hz runs.
Diamond Heights now sustains 144.021 FPS and 354.877 TPS with 0.170 ms complete CPU draw time and 0.047 ms GPU time. EverythingPark
sustains 144.035 FPS and 248.336 TPS; its CPU scene traversal is 1.422 ms while its GPU frame is only 0.101 ms. These figures make
the remaining ownership boundary explicit: Vulkan raster and presentation already have ample headroom, while the main thread
still discovers, clips, sorts, and emits most visible world commands every frame.

## Current CPU/GPU ownership audit

The retired validation bridge performed the full X8 software raster and copied the whole indexed canvas into the upload ring.
The production direct path removes that routine canvas upload and keeps the sprite atlas, indexed canvases, depth, remap/blend tables, transparency layers,
weather output and palette conversion resident on the GPU.

The remaining direct-path CPU costs are paint traversal and clipping, command allocation, first-use sprite, palette-glyph, and
TTF rasterisation/upload in `GpuTextureCache`, the transparency overlap-depth estimate, and initial remap-table
construction. These are presentation-only and do not alter deterministic simulation, but they remain on the simulation/UI
caller; command publication removes only the backend work, not this traversal.
Weather is already a compact command plus GPU pass and does not require CPU pixel storage. LightFX now has an explicit immutable
boundary: after paint traversal, the caller resolves viewport lag, live-map occlusion, and the legacy light sprites into compact
resolved-light commands plus an owned light palette. A one-byte-per-pixel intensity image is materialised only for the CPU
fallback. That snapshot travels with the command packet, so the render worker never reads the global light lists, map, entities,
viewport, or palette state. Vulkan consumes the commands or optional fallback intensity image and per-frame light palette,
reproduces the legacy integer `MixLight` operation in the final palette shader, and only then applies
the existing SDR attachment or BT.2020/PQ HDR10 output transform. LightFX therefore adds no RGBA canvas conversion and no
readback. CPU light-list and occlusion resolution remain, while Vulkan now replaces the per-pixel CPU lightmap raster in the
direct path. When compute capability is active, packets are command-only and do not allocate or fill a
logical-screen-sized CPU intensity vector. The legacy resolver materialises that vector explicitly for the X8 display path,
unsupported-device fallback, resize handoff, and focused parity validation.

TrueType text remains rasterised once by the existing FreeType cache, but its immutable one-byte coverage surface now stays in
the Vulkan indexed atlas across frames. A repeated `w` by `h` cached string therefore moves `w*h` bytes to VRAM once instead of
once per draw. Each avoided repeat also removes two intermediate owned-vector copies plus the mapped-ring copy, so steady-state
CPU copy work falls by `3*w*h` bytes per draw. The GPU cache uses the TTF cache's monotonic surface identity rather than a raw
allocator address and retains at most 256 inactive entries; frame-residency leases still prevent an evicted slot from being
reused beneath a sealed or in-flight packet.

The snapshot now also carries that compact resolved-light stream without changing the authoritative CPU intensity result. Each
32-byte command contains the clipped destination origin and extent, the exact offset and row stride into one of the eight legacy
baked falloff textures, the light type, and the post-occlusion intensity. Even the legacy small-window behaviour is represented:
the source stride is the clamped stride actually used by the CPU loop rather than an assumed native texture width. No command
retains an entity, tile, viewport, paint-session, or global-list reference.
The CPU fallback raster now consumes this resolved command record itself to produce the authoritative intensity pixels, rather
than continuing from separate local geometry variables. The compact boundary and fallback therefore cannot drift on clipping,
source addressing, stride, or intensity scaling without changing the same code path.

The exact GPU raster stage uses an `R32_UINT` frame-slot-local storage accumulator cleared per frame, one z-dispatch slice per
resolved light, and integer atomic adds of the legacy contribution (`falloff` at intensity 255, otherwise
`falloff * (1 + intensity) >> 8`). The eight baked CPU falloff arrays are copied once into an owned 256-by-256 array texture before
the worker starts. The palette pass clamps the accumulated value to 255 before the unchanged legacy `MixLight` operation.
Integer colour attachments cannot provide the required saturating overlap blend directly, while a pixel-times-all-lights
fragment loop scales poorly; atomic unsigned addition is order-independent and the maximum 15,999-light sum cannot overflow
32 bits. The selected physical device must expose optimal-tiling `R32_UINT` storage-image atomics, sampling, and
transfer-destination support, the exact combined image usage, and the requested logical extent. The accumulator images and
compute pipeline are not created otherwise (including on MoltenVK implementations which do not expose those capabilities), so
renderer initialisation retains the CPU-intensity fallback. The fixed 16-by-16 compute group is checked against both per-axis and
total-invocation limits. The 15,999 z-dispatch count is also checked against the selected device limit; it is below Vulkan's
required minimum of 65,535. The accumulator clear is made visible to both the atomic read-modify-write stage and the final fragment
sampler; a zero-light frame transitions directly from its transfer clear to fragment sampling without a fictitious compute write.
The backend accepts an explicitly materialised one-byte intensity payload as a fallback, but never silently performs CPU
rasterisation from the render worker; a command-only packet whose compute resources cannot be consumed fails and follows normal
frame cancellation/recovery.

## Indexed colour and output contract

The `R8_UINT` canvas, `R8_UINT` sprite atlas, 256-by-256 remap and blend tables, and 256-entry RGBA8 palette remain the source of truth.
Opaque drawing never expands sprites to RGBA and never changes their palette indices. The last fullscreen pass is the only place
where an index becomes an output colour.

SDR is the default. The device first requests an advertised `B8G8R8A8_UNORM` or `R8G8B8A8_UNORM` surface with the normal sRGB
colour space, preserving the legacy encoded palette bytes directly. If only an sRGB attachment is available, the palette pass
decodes the palette to linear values so the attachment's fixed-function sRGB encoding produces the same final bytes.

The user-facing `Enable HDR10 output (Vulkan)` option selects the `Hdr10IfAvailable` backend policy; it is off by default and
is available for the compiled Vulkan drawing engine. HDR activates only when the surface advertises a 10-bit
`A2B10G10R10`/`A2R10G10B10` and `HDR10_ST2084` pair. The palette pass then decodes sRGB, converts linear sRGB primaries to
BT.2020, scales the established palette white to the configured paper-white luminance, and applies the ST 2084 transfer function.
If that exact pair is unavailable, initialisation continues in SDR. The indexed source data is never promoted or rewritten.
If the surface exposes neither one of the supported 8-bit formats in the normal sRGB colour space nor an opted-in exact HDR10
pair, Vulkan initialisation fails with an explicit error. It does not silently select software or alter the requested window
mode. Legacy SDR bytes are never submitted to an HDR-only or otherwise unsupported colour-space pairing.

Windows and Linux use native Vulkan. macOS uses this same Vulkan resource and shader path through MoltenVK; there is no separate
Metal renderer or Metal-specific colour-composition implementation. HDR availability is therefore a runtime surface capability,
not an OS assumption.

HDR10 activation follows the existing abstraction rather than adding a second renderer:

1. retain SDR as the default and expose the opt-in only when Vulkan is compiled and selected;
2. report both HDR10 surface support and the active swapchain mode after swapchain creation or recreation, including an explicit
   refresh when SDL reports that the window moved to a different display even if its drawable extent did not change;
3. publish display mastering and content-light metadata through `VK_EXT_hdr_metadata` when the device advertises it, without
   treating metadata support as permission to use an unadvertised colour-space/format pair;
4. validate SDR fallback, paper-white mapping, BT.2020/PQ output, 10-bit gradients, palette animation, screenshots, and live
   monitor transitions on native Vulkan and MoltenVK;
5. keep screenshots explicitly tagged or converted to SDR until the readback API carries colour-space metadata.

The current HDR slice negotiates the exact 10-bit/HDR10 surface pair, performs sRGB-to-BT.2020 plus PQ conversion in the palette
pass, and optionally enables `VK_EXT_hdr_metadata`. After every HDR swapchain creation the device publishes a D65 mastering
description whose minimum/maximum luminance and content-light levels match the configured paper-white mapping rather than
inventing unavailable monitor limits. A real SDL display-index transition now crosses the drawing-engine lifecycle abstraction and
marks the Vulkan swapchain stale; both the synchronous bridge and render-worker path renegotiate format and colour space at their
next normal frame boundary without treating the move as a logical resize. Metadata support is optional on native Vulkan and
MoltenVK and never turns an unavailable HDR surface into a failed SDR initialisation. Indexed screenshot/readback output remains
explicitly SDR and still needs a colour-space-aware API before an HDR readback can be exposed.

The final palette pass writes straight alpha. Swapchain creation therefore prefers opaque composition, then
`POST_MULTIPLIED`, and uses inherited composition only as a final compatible fallback. A surface that advertises only
`PRE_MULTIPLIED` composition is rejected rather than falsely declaring straight RGB to be premultiplied.

Vulkan-enabled test builds also include a device-optional runtime integration fixture. It creates only a hidden SDL-owned
window and skips cleanly when video, WSI, shaders, or backend initialisation are unavailable. On a usable device it exercises
the production backend's resource uploads, dual compute/CPU LightFX snapshot, frame-slot rotation, explicit surface refresh,
resize, presentation, and synchronous indexed readback without assuming that HDR10 is available.

The v41 Windows dependency bundle contains SDL 2.32.8 built without `SDL_VIDEO_VULKAN`; changing its generated header cannot
add the missing compiled video-driver hooks. `VulkanPlatform` therefore owns WSI: Windows omits `SDL_WINDOW_VULKAN`, obtains
the SDL window's `HWND`/`HINSTANCE`, enables `VK_KHR_surface` plus `VK_KHR_win32_surface`, creates the surface directly, and
uses the per-monitor-DPI-aware client rectangle as the drawable extent. Linux and macOS retain SDL's Vulkan loader, extension,
surface, and drawable-size APIs. A future dependency refresh may enable the vcpkg SDL Vulkan feature, but renderer correctness
no longer depends on that package change. The hidden fixture passes this Win32 path on the local Vulkan device with the Khronos
validation layer enabled.

## Staged implementation

### 1. Indexed composition and presentation

The backend, direct `IDrawingContext` recorder, persistent atlas cache, and explicit synchronous screenshot adapter are present.
Direct screenshots capture the
last successfully presented post-transparency/weather indexed canvas, then use the legacy indexed PNG path and palette. With the
the render worker, screenshot capture attaches to pending visual work or publishes a control packet when none exists;
the backend-owning worker is allowed to block the caller only for that explicit capture. Normal visual packets remain disposable.
Immediate parity work is now visual comparison coverage and evidence that full-list command recording is preferable before
adding GPU damage metadata. Production activation also depends on interactive lifecycle validation of the non-blocking frame
handoff and cancellation paths above.

### 2. Texture residency without first-use stalls

The direct Vulkan cache now gathers missing images during command recording and owns stable generation-aware atlas metadata.
Sealing a frame copies only its touched pending pixels into the owned direct-frame stream, with no offset or pointer into
caller/backend memory, and returns a cache-owned residency token. Vulkan assigns staging-ring offsets only while submitting that
stream. Successful retirement commits only that token's persistent uploads; failed retirement leaves them pending for retry
without rerasterising. Transient uploads leave the pending set at seal time, while their physical slots remain pinned until token
retirement. Allocation serials distinguish later reuse of the same layer and slot. The device-local 64-layer array remains
allocated once, and each frame's queued misses are emitted inside one transfer/barrier section. Follow-up work should:

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
The LightFX final mix is now part of the palette fragment pass and uses frame-slot-local palette and intensity images, so palette
animation cannot overwrite resources still sampled by an older in-flight frame. The direct path uses the bounded
integer compute accumulator and immutable baked-falloff resource described above. Unsupported optional compute capabilities use
the explicitly materialised CPU intensity payload; neither path performs an RGBA canvas conversion or GPU-to-CPU transfer during
ordinary presentation.

### 5. Parallel frame preparation

One render thread owns Vulkan submission and resource retirement. The UI thread publishes a finished command packet; mutable
simulation and UI state never cross that boundary. A later semantic snapshot can let workers
build independent viewport/window ranges in per-thread arenas, followed by stable concatenation. At Turbo, presentation consumes
the newest available visual packet and discards superseded ones.

The direct stream's draw commands and first-use upload bytes are self-contained in a `RecordedFramePacket`. A bounded,
backend-neutral mailbox retains one newest packet and one returned packet for command-arena reuse. Publishing replaces stale
visual work instead of queuing it. Each packet carries its atlas residency token plus complete, versioned logical extent,
present-mode, and palette state, so replacing a packet cannot lose an intervening control change. A synchronous screenshot
request attaches to the newest pending visual packet, follows it if that disposable visual is replaced, and runs immediately
after that frame is presented. If no visual is pending, the mailbox publishes a control-only packet that reads the latest
successfully presented canvas. This preserves visual-before-capture ordering without adding a general frame queue.

The direct worker consumes those packets and exclusively owns hot backend resize, present-mode, palette,
`BeginFrame`, submit, present, readback, and abandonment calls. Presented, busy, superseded, failed, rejected-at-shutdown, and
pending-at-shutdown packets all retire their leases explicitly. Worker failure stops publication, retires the one pending packet,
and is reported on the UI caller at the next recording boundary. An attached screenshot is an explicit permitted blocking
boundary: the worker drains prior GPU work, presents its packet, and only then reads the indexed canvas. Routine visual packets
remain non-blocking and disposable.

The render worker is the sole Vulkan lifecycle path. The UI thread samples the physical Vulkan drawable extent during initialisation, explicit resize, and a
lightweight per-frame change check. Recorded packets carry that extent separately from the logical indexed canvas. Device
swapchain creation and recreation consume only the stored value; neither `RecreateSwapchain` nor variable-extent selection calls
SDL. A zero drawable marks a minimized or unavailable surface, leaves the old swapchain intact, and skips acquisition until a
later UI sample publishes a non-zero extent. Initial platform loader/extension/surface creation remains on the UI thread;
Windows uses native WSI while other platforms use SDL Vulkan. Shutdown joins the worker before backend and window destruction.
CMake uses `ENABLE_VULKAN`; pinned Windows MSBuild auto-detects the SDK or accepts `EnableVulkan=true`. There are no separate
renderer, direct-context, or worker gates.

The simulation must never wait for a presentation fence except during resize, renderer shutdown, screenshot capture, or explicit
debug validation.

## Parity and deletion gates

Continue deleting CPU legacy paths only after Vulkan comparison captures cover normal, rain, snow, night, lightning,
transparency, multiple viewports, zoom levels, screenshots, and UI scaling.

After indexed composition parity:

- delete HardwareDisplay palette conversion and its 32-bit shadow buffer;
- remove the macOS CPU `CopyRect` readback fallback;

After GPU paint preparation parity:

- delete per-sprite `CalculateClipping` work and its cache;
- remove synchronous `GetOrLoadImageTexture` calls from draw recording;
- remove CPU weather-pixel storage and restoration;
- remove CPU viewport sprite expansion for world layers, retaining only the deterministic paint-record producer;

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
