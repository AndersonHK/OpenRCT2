# Vulkan render telemetry implementation design

Source audit: `/root/screen_capture`, 2026-09-19. Upload accounting slice 1 is now
implemented after E2b in the shared renderer. Ordinary build32 and full run27
pass 727 tests with synchronization validation, including abandonment, padded
atlas transfer, direct vertex payload, slot reuse, readback and overflow cases.
The remaining producer/publication/timing proposals below are still open.
The ordinary frozen renderer and accepted binary stay unchanged.

The [ordinary application smoke](vulkan-upload-telemetry-smoke.json) passes with
400 submitted samples, no lost samples, allocation failures, overflow, capture
or readback. EverythingPark writes 1,527,327,700 bytes of draw commands across
400 frames: 3,818,319.25 bytes per frame, directly bound from host-visible memory.
Atlas host writes average 8,583.48 bytes and palette writes 1,024 bytes per frame.
These are API payload sizes; host-write and direct-binding counts describe the
same command data and must not be summed. Zero native-world bytes here indicates
the populated park uses the existing CPU command path, not successful retained
world rendering. Clean and enabled same-build smoke states/checksums agree;
five sustained pairs and overhead calibration remain outstanding.

The purpose is to measure progress toward resident world state and incremental
updates, not merely faster submission of CPU-generated draw lists. Keep the
existing clean TPS lane. Add an explicit aggregated telemetry lane with the
same normal game loop, workload and state checks; calibrate its overhead before
using its timings for decisions. No per-frame text/JSON, image readback, GPU
idle wait, new GPU-to-host buffer, global lock per command, or atomic increment
per entity is needed for the counters proposed here.

## Measurement contract

Use fixed-size, zero-initialized POD counters with `uint64_t` values and explicit
availability bits. Schema version 1 reports bytes in bytes, durations in integer
nanoseconds or documented microseconds, counts as integers and denominators
alongside ratios. An unavailable field is null/absent with a reason, never a
zero. Saturation/overflow makes the interval invalid and is reported.

Maintain two accounting domains:

1. **Producer interval work:** all state capture/build, viewport paint, command
   recording, decode and packet preparation executed in the measurement
   interval, including abandoned/replaced packets and generations never shown.
   These totals explain CPU cost per simulation tick and per admitted draw.
2. **Submitted frame work:** packet metadata plus backend work associated with
   one immutable frame number and committed queue submission. A fence-complete
   sample carries this through `Drawing::FrameTimings`. Count recorded-but-
   abandoned transfer commands separately, because they never reached the GPU.

Do not sum a snapshot's construction cost every time a frame retains it. A
generation can be reused across draws, and preparation can finish without being
presented. Its construction work is counted once in the producer domain; a
frame stores generation IDs/ages and whether it reused that generation. Do not
merge producer and submitted byte totals into a single unexplained number.

| Metric | Exact definition | Not implied |
| --- | --- | --- |
| `cpuExplicitCopyBytes` by category | Bytes passed to audited `memcpy` or known vector payload copy; count each real copy stage separately | Total CPU memory-bus traffic, allocator metadata or cache misses |
| `cpuConstructedPayloadBytes` | Logical records written/constructed, count × `sizeof(record)` | An extra memcpy or exact hardware write transactions |
| `ringPayloadAllocatedBytes` | Sum of successful `UploadRing::Allocate(size, alignment)` requested sizes | Bytes written, transferred, or resident VRAM |
| `ringAlignmentPaddingBytes` | Sum of `alignedOffset - previousCursor` for successful allocations | Payload |
| `ringHighWaterBytes` | Maximum cursor after allocations in that slot submission | Sum across slots or actual transfer bytes |
| `mappedHostWrittenBytes` by category | Exact successful CPU writes into mapped ring allocations at write sites | PCIe traffic; host-coherent memory still has data movement |
| `bufferTransferPayloadBytes` | Sum of submitted `VkBufferCopy::size` | Driver overhead, bandwidth on a hardware bus |
| `imageUploadTexelBytes` | Sum of copied image extents × layers × bytes/texel for the actual buffer-to-image regions | Row padding in the staged source or compressed storage traffic |
| `directHostVertexPayloadBytes` | Logical uploaded command payload bound directly as vertex input | Transfer-command bytes or number of GPU fetches |
| `directHostStoragePayloadBytes` | Logical mapped light command payload directly exposed as SSBO | Transfer-command bytes or actual repeated shader loads |
| `nonCoherentFlushRangeBytes` | API range flushed, including atom padding; resolve `VK_WHOLE_SIZE` to allocation extent | Bytes written; coherent devices legitimately report zero flush calls |
| `captureDeviceCopyBytes` / `readbackPayloadBytes` | Requested diagnostic image-to-image copy / explicit image-to-buffer readback payload | Ordinary renderer upload cost |

Count push-constant payload and command counts separately if wanted; driver
copying of push constants is not observable as ring traffic. GPU clears, native
compute output and render-target writes likewise are device operations, not CPU
upload bytes. Report logical clear area or dispatch dimensions separately;
never estimate their physical bandwidth from framebuffer dimensions.

## CPU capture, paint and packet insertion points

| Owning function/file | Proposed counters and aggregation point |
| --- | --- |
| `EntityRegistry.cpp`, `EntityStorage::Capture` | Increment pages copied and `pool.stride * kSlotsPerPage` immediately after each full-page memcpy; count occupied-mask bytes separately. This copies allocated pages, including unused slots, so entity count is not a substitute. Return/store one local aggregate per completed capture. |
| `EntityPresentationSnapshot::CaptureStorage` / `BuildCapturedStorage` | Separate capture wall time from build worker CPU elapsed time; count index reset bytes, occupied slots visited, valid entities indexed, spatial buckets built and bucket sort elements. Array fill is constructed/write payload, not memcpy. Do not time or atomically update per entity. |
| `world/Map.cpp`, `ConsumeMapPresentationChanges` | Reset versus delta batches, dirty tile records, copied tile element payload bytes, surface records resolved. Increment inside existing `copyTile` traversal without a second map walk. |
| `MapPresentationSnapshot::Apply` in `Map.cpp` | Count COW tile chunks, actual deep-copied tile-vector element payload, surface chunks cloned, changed elements assigned and surface records written. `Chunk` contains vectors: `sizeof(Chunk)` alone misses deep data. Compute payload during the already-required copy or maintain per-chunk payload totals; do not add a full-map summation pass for telemetry. |
| `drawing/PresentationScene.cpp`, map/entity publisher `Acquire`, `AcquireSynchronously`, `Schedule` | Capture/build jobs scheduled, reused fronts, synchronous waits, discarded pending generations and new allocations versus recycled snapshot storage. Measure generation preparation once. Reset and epoch transitions start a new identity, not a negative age. |
| `interface/Viewport.cpp`, `ViewportPaint`, `CreatePreparedViewportFrame`, `ViewportFillColumn`, `ViewportPaintColumn` | Viewport calls and clips, column count, calls to generate/arrange/paint, generation/arrangement/record elapsed times. Each column owns local counters; reduce after the existing `ParallelFor` completion barrier. Summed worker times can exceed wall time: publish both labels, never call their sum the critical-path time. |
| `paint/Paint.cpp`, `PaintSessionGenerate`, `PaintSessionArrange`, paint-struct allocation sites | Later optional attribution slice: generated parent/child/attachment records, arrange candidates/comparisons, tiles/entities visited and rejects. Increment existing per-session local counters in existing loops. Do not modify painter decisions or retain extra paint objects. First slice can use existing session storage counts where available without adding this fine instrumentation. |
| `VulkanDrawingEngine::PaintWindows` | Before `WindowUpdateAllViewports`, after its provisional drawing, and after `WindowDrawAll`: count provisional commands discarded by the current clear/reset path separately from final commands. Final stream length alone hides this work. Time viewport update, publication and full-window traversal as non-overlapping outer scopes. |
| `VulkanDrawingEngine::BeginDrawQueued` / `EndDrawQueued` | Reset packet-local counters, latch preparation start/end, count reused/new packets; at end record final lengths and logical sizes of every command batch, texture payloads, native references and LightFX payload. `SealFrame` and LightFX capture have separate subscopes. A packet move is not a deep command copy. |
| `GpuCommandStream.h`, `CommandBatch::allocate` / `reserve` | Optional growth/reallocation events and old live payload potentially relocated; `capacity()` here is `_storage.size()`, not vector allocation bytes. Report logical reserve separately from actual allocator capacity. Do not infer a memcpy merely from `resize`, which may initialize in-place. |
| `GpuTextureCache.cpp`, `QueueDecodedImage`, `QueueUpload`, `SealFrame` | Decode calls/input/output sizes and CPU time; exact `QueueUpload` memcpy bytes; **second copy** of `pending.pixels` into `TextureUpload` at `SealFrame`; copies of allocation/lease metadata separately. Cache lookup/hit/new allocation/invalidation/retirement counts and pending versus actual upload payload distinguish residency from redraw. |
| `VulkanDrawingEngine::CaptureLightFx` and owning LightFX snapshot creator | Count copied light commands and intensity bytes independently, latch selected GPU-command versus CPU-intensity route; CPU fallback rasterization time remains separate from backend upload/compute. |

Current command ABI sizes are asserted in `GpuCommandStream.h`: line 24,
rectangle 100, sprite 60, weather 28, light 32, sprite descriptor 16, native
surface source 40, native output 64 and surface sprite set 200 bytes. Use
`sizeof` in instrumentation and include a schema/ABI metadata record, not copied
magic constants. Command logical byte count measures CPU-generated draw-stream
volume even when vertices read directly from host-visible memory.

The publication code is shared by current software and Vulkan. Instrument it
with optional metadata/counters only and re-run exact parity; never change
capture scheduling, dirty state consumption or software paint behavior to
obtain measurements. Frozen executable counters remain unavailable. Comparable
ordinary-loop TPS exists on both binaries without retrofitting its source.

## Backend byte counters at actual write/copy sites

`Backend::Submit` owns a slot-local counter record initialized with the exact
frame number in `BeginFrame`. Pass a small local accumulator/reference through
`FrameToken` or explicit pipeline parameters, avoiding mutable globals. The
following categories must be separate even if all use the same upload ring.

| Category and insertion site | Host write and submitted transfer accounting |
| --- | --- |
| Screen palette: `RecordPendingPalette`, `StageUpload`, `IndexedResources::RecordPaletteUpload` | 1,024 host bytes and image texel bytes each time a frame slot's palette version needs updating. Slot warmup can require multiple uploads of one palette version; do not assume one globally. |
| Remap/blend lookup: `RecordPendingIndexTable` / `RecordIndexTableUpload` | Each actual 256×256 index table write and buffer-to-image payload; keep remap and blend separate. Dirty invalidation after abandon causes legitimate retry, counted in attempted and later committed domains. |
| Atlas pixels/descriptors: `RecordTextureUploads` / `RecordAtlasUpload` / `RecordSpriteDescriptorUpload` | Host pixel write is `sourcePitch * height`; image payload is `width * height` for R8. Source addressed footprint is `(height-1)*pitch + width` if needed; none equals padding-free staging in general. Descriptor adds 16 host bytes and 16 buffer-copy bytes per actual record. |
| Terrain deltas: `WorldSurfacePipeline::Record` changed revision loop | Every copied chunk is `kWorldSurfaceChunkWidth * sizeof(WorldSurfaceSourceRecord)` (currently 256×40=10,240 bytes), even a partly used final chunk. Count actual changed chunks/host memcpy/buffer-copy bytes. Reused chunks contribute zero transfer bytes. |
| Terrain sprite table: same function, `spritesChanged` branch | Actual table length × 200, host write plus buffer-copy payload when revision changes; no assumed per-frame upload. |
| Opaque rectangle/sprite, line, transparent rectangle, weather: each pipeline `Record` | After successful mapped memcpy, add exact batch bytes to its own category and direct-host-vertex payload. These bind the ring with `vkCmdBindVertexBuffers`, without `vkCmdCopyBuffer`; transfer payload for these categories is zero. Transparency uploads the batch once then uses it for multiple peels, so track peel/draw invocations separately instead of multiplying CPU bytes. |
| GPU LightFX commands: `LightFxPipeline::Record` | Actual command bytes mapped once; SSBO directly references that ring allocation. Count direct-host-storage payload and dispatch dimensions; no buffer-copy payload. Allocation failure and fallback are explicit outcomes. |
| LightFX palettes/falloffs/intensities: `Backend::RecordLightFx`, `RecordPendingLightFalloffs`, resource upload functions | Count each actual stage write and each executed resource-copy region. The GPU-attempt/fallback branch may record the light palette more than once; do not deduplicate reported API work by asset identity. CPU intensity uploads are width×height bytes only when that route is used. Falloff layers are 8×256×256 R8 bytes when dirty. |
| Ring mechanics: `UploadRing::Allocate`, `Reset`, `FlushWritten` in `VulkanDevice.cpp` | Requested payload, padding, allocation failures/failed request bytes, high-water, capacity and flush ranges. `Allocate` itself is not a write. Reset starts another accounting phase/slot; preserve its completed totals before resetting. |
| Diagnostic copy: `Backend::RecordFrameCapture` | Separate device-to-device copy extent×4 and request count. This copies swapchain output into a retained capture image, not host memory yet. |
| Screenshot/final/indexed readback: `Device::ReadbackImage` | Separate submitted image-to-buffer payload `width*height*bytesPerPixel`, destination CPU copy bytes and explicit wait duration. This reuses/resets the **upload** ring; counting ring allocation as upload would falsely claim a framebuffer upload. Attribute to an auxiliary/capture operation ID, not a reused ordinary-frame counter. |

Count copies where the validated `VkBufferCopy`/`VkBufferImageCopy` is emitted,
then move recorded totals to submitted totals only after successful queue
submission. `Backend::Present` can fail after submission: distinguish
`submitted`, `presentRequested`, `presentationFailed`, `completed` rather than
discarding real GPU work because presentation failed. If the current
`Device::EndFrame` interface hides whether submit succeeded, add an internal
submission outcome callback/result first; do not infer it from `Presented`
retirement. `AbandonFrame` reports host work plus unsubmitted recorded bytes and
does not mark them committed.

These are exact **API payload** counters, not a physical PCIe bandwidth meter.
Driver/hardware transport, coherent unified memory, caches, repeated shader
loads and compression require separate profiling tools. Label the distinction
in output and benchmark reports.

## Native/ordinary coverage and frame age

Record eligibility at both actual decision layers:

- `ViewportPaint` before the `DrawWorldSurfaceScene` call: main versus auxiliary
  view, main viewport identity, missing generation/context, overlay flags,
  selection/tool state and track-editor constraints. A grid flag prevents the
  recorder call entirely; recorder-only telemetry would miss the reason.
- `CommandDrawingContext::DrawWorldSurfaceScene`: absent map, already occupied
  scene slot/reentrancy, category interleaving/elevation eligibility, entities
  present, smoothing, failed sprite-set resolution and zero-index coverage.
  Preserve existing short-circuit semantics; use one primary rejection reason
  and optionally a cheap already-known reason bitmask, not a second full scan.
- On successful admission: dense records, cached valid interior records,
  retained/converted chunk counts, sprite-table revision/entry count, cached
  sprite sets resolved again and CPU scan count. Cache valid-record totals when
  chunks are converted; do not traverse every record again to count them.

Publish denominator counts: viewport attempts, native-admitted main viewport
calls, ordinary-only calls, native scene packets and ordinary world/UI command
counts. The native 32×32 fixture has 1,024 dense records but 900 drawable interior
records; dense count is not visible surface count. Overlay/native mixed packets
still issue ordinary commands. Scope world commands around `ViewportPaint`
using packet-local origin metadata; UI commands outside that scope and weather
have separate categories. Avoid doubling command structs solely for telemetry.
If callbacks/reentrancy temporarily switch scope, use RAII and an explicit stack.

Without new GPU readback, actual compacted visible-instance count and exact
native-pixel coverage remain unknown. CPU-known record count, dispatch group
count and indirect draw-call count are sufficient initial counters. Do not
claim a GPU culling ratio from submitted dense records or substitute camera
estimates for measured visibility.

`PresentationGeneration` currently has map/entity shared pointers but no
monotonic publication serial or simulation timestamp. Map epoch is a lifecycle
identity, not an age. Add diagnostic source-capture tick and monotonic time to
each preparation transaction and publication serial to its admitted generation;
latch those into `RecordedFramePacket` alongside frame number and existing
resize/palette/surface versions. A reused unchanged map can still describe the
new entity capture boundary: keep state-as-of tick distinct from last map
mutation tick, which is unavailable until explicitly tracked.

Record these CPU-clock durations as distributions with sample counts:

- capture-to-publication and source-tick age at publication;
- capture/publication-to-packet seal;
- packet seal-to-worker dequeue (mailbox wait);
- dequeue-to-successful submission;
- submission-to-fence harvest (observation latency, **not** exact GPU completion);
- prepared/published/reused/superseded/busy/failed/shutdown packet counts.

Never subtract GPU timestamps from CPU monotonic timestamps. Never describe
`vkQueuePresentKHR` return or fence harvest as a displayed-frame timestamp.
Count denied `CanBeginFrame` attempts separately from actual dropped packets;
one loop may ask repeatedly while simulation continues. Exclude nonvisual
readback/timing-boundary packets from visual denominators. Report epoch changes
and unavailable ages instead of cross-epoch subtraction/unsigned underflow.

## Timing and transport integration

`Drawing::FrameTimings` in `IDrawingEngine.h` already transports frame number,
CPU submit/present durations and GPU interval means. Add an optional fixed-size
`RenderWorkCounters` payload/availability marker and packet preparation summary;
keep it core-owned and free of Vulkan/SDL types. Main-view and future auxiliary
service samples need an explicit workload kind so thumbnail work is not folded
into main-screen FPS. Existing software implementations may leave it unavailable.

`Backend::PublishTimings` has a bounded ring and silently overwrites the oldest
sample on overflow. Add `lostCompletedSamples` and make telemetry acceptance
fail on any loss. Keep whole-interval totals independently where needed so a
presented-only ring cannot erase work done for failed/dropped frames. Merge
local totals at existing ownership handoffs, not through a new contended mutex
inside pixel/record loops.

At `Context::BeginIntegratedBenchmarkMeasurement`, existing
`DrainFrameTimings` discards warmup submissions before the timer starts. Extend
the same explicit boundary to counter snapshots. At finish, drain once outside
elapsed timing and aggregate only frame numbers tagged with the measured
interval ID. Publication jobs crossing a boundary require a declared policy:
tag at capture/start, retain their local counter result through completion, and
report crossing-job counts/time separately. Do not wait for extra background
work during routine frames to simplify accounting. Main-thread wall timings
and background task CPU elapsed sums cannot be added as elapsed latency.

The current timestamp order is `frameStart`, `uploadsComplete`,
`indexedDrawComplete`, `lightFxComplete`, `frameComplete`. `Backend::Submit`
writes `uploadsComplete` **before** `WorldSurfacePipeline::Record`, whose body
uploads changed records, runs compaction and draws. Thus native delta transfer
and native compute are included in today's `gpuDrawMicroseconds`. LightFX
palette/intensity transfers happen later in the light interval too.

In a separate calibrated slice, retain legacy outer intervals and add inner
timestamps around native transfer completion and native compute completion,
plus native raster completion if useful. Record the timestamp inside the
pipeline at the real boundary, not before/after its combined `Record` call.
Use the existing availability/valid-bit wrap handling and fence harvest; no
blocking query read per frame. Null intervals when native work is absent must
not reuse stale slot queries. Keep stage selection/ordering explicit: these
are queue timeline intervals including relevant barriers, not isolated engine
utilisation or additive overlapped unit busy times. More timestamps have
overhead and belong in the attribution lane until calibrated.

## Bounded implementation slices and checks

1. **Backend payload accounting.** Introduce core POD schema, slot-local ring
   statistics and category counters at the audited write/copy sites. Attach
   counters to completed frame timings; track submitted/abandoned/failed
   outcomes and timing-ring loss. No painter/publication edits yet. Synthetic
   runtime fixtures assert byte arithmetic and zero ordinary readback.
2. **Packet and recorder accounting.** Add final command sizes, provisional
   discarded work, atlas copies/cache events, LightFX routes, admission reasons
   and packet retirement totals. Existing byte-identical fixtures must retain
   their results. Add no extra decode or scene traversal for counters.
3. **Publication and paint attribution.** Per-capture/per-column local tallies,
   generation identity/times, COW versus bulk-copy payloads and outer scope
   timings. Keep shared software behavior frozen; exact three-way fixtures
   qualify metadata-only changes. Test async generation reuse/reset/coherence.
4. **GPU inner timing and interval export.** Add calibrated timestamp points;
   emit one versioned aggregated telemetry record after the terminal benchmark
   report or a dedicated sidecar. The ordinary default benchmark remains clean.
   Extend `run-render-performance.py` only for an explicit telemetry mode.

Required counter invariants:

- A steady warm resident frame with unchanged assets/world has zero atlas,
  terrain and lookup transfer payload, apart from documented per-slot palette
  initialization or LightFX per-frame data; ordinary CPU command writes can
  remain nonzero and must be visible as the remaining migration cost.
- One changed terrain chunk reports exactly 10,240 host and buffer-transfer
  bytes with today's ABI; changing camera alone reports zero terrain delta
  bytes. Epoch/resize/device rebuild reports full upload rather than pretending
  it is an incremental update.
- A padded atlas fixture with width less than pitch produces different staged
  host bytes and image texel bytes. Direct vertex/SSBO fixtures report nonzero
  host writes and zero buffer-transfer bytes for those same payloads.
- Ring payload + alignment padding equals high-water for a monotonic allocation
  phase; a failed allocation leaves cursor/counters consistent. Noncoherent
  flush bounds obey atom alignment and allocation bounds, with coherent flush
  calls explicitly zero.
- Abandon/retry counts attempted host work twice but committed transfers only
  on the successful queue submission. A present failure after submit still
  preserves submitted counters. Slot reuse, busy acquire and timing harvest
  cannot attach one frame's counters to another frame number.
- Ordinary no-readback frames have zero capture/readback requests and payload.
  Explicit RGBA capture reports its device copy plus separate host readback;
  indexed screenshot reports its own auxiliary bytes. Timing queries do not
  become image readbacks in the report.
- Reusing a generation for N draws does not multiply its construction bytes;
  discarded preparation still contributes producer work. Two parallel columns
  reduce exactly once. Overflow and lost samples fail strict accounting.
- Known terrain cases report native/ordinary reasons correctly: grid declines
  before recorder, smoothing declines inside it, native UI overlap reports both
  native terrain and ordinary UI commands. GPU visible count stays unavailable.
- Root-owned before/after pixel suites, validation and state-checksum runs pass;
  counters must not alter branches, async admission or resource lifetimes.

Use five interleaved paired trials of the same immutable build/config/workload
with telemetry disabled/enabled, followed separately by extra GPU timestamp
mode. Compare state endpoints, TPS, CPU draw/submit time, frame interval tails,
allocation high-water and sample coverage. Record absolute and relative
overhead plus run variability; set the acceptable overhead threshold before
using the new lane for performance conclusions. Do not assume scalar counters
are free or attribute an instrumentation regression to Vulkan architecture.

The performance parser must require telemetry schema/mode agreement for
telemetry comparisons, preserve old ordinary reports, validate integer totals
and denominators, reject lost/overflowed or cross-epoch samples, and distinguish
attempted from submitted byte fields. Report per logical tick, per constructed
packet and per submitted frame with those exact denominators. Never relabel
API payload bytes as physical bandwidth. A missing initial full entity checksum,
actual drawable/monitor evidence, or displayed cadence remains missing after
this instrumentation; these counters do not close those separate gates.
