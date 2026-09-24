# Vulkan renderer architecture

This is the canonical renderer contract. Historical measurements and abandoned designs belong in `docs/archive/`, not here.

The [Vulkan-only migration plan](vulkan-exclusive-migration-plan.md) tracks the parity evidence, auxiliary-target migration,
and software removal required to reach an exclusive Vulkan renderer. The contracts below describe the current implementation.

The graphical application now has one renderer: Vulkan. Its factory has no backend selector, old `drawing_engine` settings
are ignored and removed on save, and Options exposes no renderer menu. Graphical builds require Vulkan support; failure to
create a usable Vulkan device is an error. This early removal does not mean CPU rendering has been eliminated: ordinary
world preparation and X8 auxiliary callers are deprecated and tracked in the
[CPU retirement inventory](vulkan-cpu-rendering-retirement-inventory.md). Reference Software executes in separate pinned
source/build artifacts; upstream comparisons are independent of the active fork.

The replacement redraws the whole visible world each frame. Shared catalogs and raw instance state stay resident;
slow-changing appearance/topology and fast-changing pose/action data have separate update streams. Shaders select visuals,
expand components, transform and cull them, and resolve common world visibility. Nongraphical simulation state is not sent
to the renderer. No world framebuffer preservation, dirty screen chunks or lazy repainting is part of this architecture.

The owner additionally requires the final renderer to keep assets and world state resident in VRAM, submit bounded state deltas,
and perform expensive world transforms and graphical processing in shaders. The present CPU painter/command path is transitional.
Migration Gate P requires measured CPU/upload-bandwidth reductions, sustained VSync pacing and substantial large-park TPS headroom;
existing pixel parity or GPU rasterization alone does not establish that outcome. Shared offscreen-service ownership has been approved,
but a core service contract is only a foundation until the common Vulkan executor and all callers are migrated.

## Non-negotiable invariants

1. The main window renders one immutable `PresentationGeneration`. Map and entity snapshots advance together through
   `PresentationScene`; a viewport cannot acquire either domain independently.
2. Every admitted Vulkan packet is a complete replacement frame. The backend clears indexed colour and depth before drawing.
   Vulkan does not retain pixels, track dirty rectangles, acknowledge damage, or copy shifted viewport pixels.
3. Auxiliary render targets are isolated. Track-design previews, park previews, file previews, and giant screenshots render
   their temporary live state through their own drawing engine. They never acquire, publish, or mutate the main presentation
   scene.
4. An optimization may transfer one paint category to a direct GPU producer. Refusing that transfer leaves the existing paint
   category responsible inside the same generation and the same drawing engine. It must never switch the viewport to another
   renderer or expose mutable simulation storage.
5. A sealed render packet owns every byte and resource lease needed by the render worker. The worker never dereferences a map,
   entity, viewport, paint session, object registry, or caller-owned upload buffer.
6. Simulation does not wait for presentation fences. Explicit readback, resize, renderer shutdown, and debug validation are
   the only synchronous GPU boundaries.

## Ownership flow

```text
simulation mutation
  -> map/entity change capture
  -> PresentationScene
       -> immutable PresentationGeneration { map snapshot, entity snapshot }
  -> viewport paint preparation
       -> optional direct category submission
       -> indexed command recording for all remaining categories and UI
  -> self-contained RecordedFramePacket
  -> newest-frame mailbox
  -> render worker
  -> Vulkan upload, indexed passes, palette pass, present
```

`PresentationScene` has three public operations:

- `BeginFrame` admits at most one generation for a draw. Normal changes use the last completed background publication.
  Construction selection, Tile Inspector edits, and track-design placement synchronously publish current map changes because
  their visuals are deliberately short-lived.
- `ScheduleNext` captures owned map and entity changes after visible paint preparation and builds the next snapshots in the
  background. Repeated calls are harmless while work is already pending.
- `Reset` drains pending publication work and discards the entire generation at renderer or world lifecycle boundaries.

A map epoch change resets map and entity publication together. Park loading replaces more than tile storage, so prepared work
from the old epoch must not call paint code against new object, ride, or entity registries.

## Map publication and temporary maps

Map mutation owns presentation invalidation. Dirty tiles are deduplicated in a bitset and captured into an owned change batch.
`MapPresentationSnapshot` applies those batches copy-on-write: unchanged tile and surface chunks remain shared, while changed
chunks receive a new revision.

`StashMap` and `UnstashMap` form a complete temporary-map boundary. They save and restore tile storage, tile indexing, map size,
presentation epoch, reset state, dirty bits, and the dirty worklist. A track-design preview may freely build and paint its
temporary map without consuming or poisoning main-world publication state.

Auxiliary render targets intentionally use no `PresentationGeneration`. Scoped snapshot pointers are null, so ordinary map and
entity reads resolve against the temporary live state owned by that operation. Main-window targets always use the generation.
The drawing-engine identity on the render target is the boundary; opening a window is not a renderer lifecycle event.

## Complete-frame Vulkan contract

The Vulkan drawing engine ignores screen invalidation and reports that projected viewport invalidation can be skipped. It still
runs viewport updates, discards any provisional shift-strip commands they emit, begins the presentation generation, and records
`WindowDrawAll` for the full logical canvas.

The render worker may drop an older queued visual packet in favor of the newest one because packets are complete and carry all
versioned control state. Dropping a packet cannot leave holes from an older canvas. Texture residency leases are retired for
presented, superseded, failed, and shutdown packets.

The backend records passes in this order:

1. palette, lookup-table, LightFX resource, and texture uploads;
2. indexed colour and depth clear;
3. optional direct world-surface base;
4. indexed lines, opaque rectangles and sprites;
5. transparent indexed layers and composition;
6. weather;
7. LightFX;
8. indexed-palette conversion, including LightFX colour mixing;
9. optional RGBA scaling followed by SDR or opted-in HDR10 output encoding.

The indexed canvas, sprite atlas, remap/blend tables, and 256-entry palette remain authoritative. Ordinary drawing never expands
sprites into RGBA. The palette pass is the only routine conversion from an index to a colour. Nearest output uses a direct
palette-to-swapchain pass. Linear output first resolves into a logical-size RGBA8 UNORM target; smooth-nearest resolves into a
nearest-expanded target sized by `ceil(windowScale)`. Both then linearly sample those display-encoded colour bytes before final
output encoding. Palette indices are never interpolated. Per-frame-slot targets and descriptors are replaced only after that
slot's fence completes.

## Direct world-surface category

`IDrawingContext::DrawWorldSurfaceScene` is a category-transfer request, not a renderer switch. It returns `true` only after a
complete world-surface command has been recorded. `PaintSessionFlags::SurfaceBaseDrawn` then suppresses only the duplicate base
terrain quad; surface sides, water, fences, overlays, track, scenery, entities, and UI retain their existing producers.

The current direct category is deliberately narrow. It is accepted only for the main viewport when:

- the viewport has no surface-parent overlays that require existing paint ordering;
- selection, inspector, and track-designer transient states are inactive;
- every published surface base is valid, uniform in height, and independent of other tile elements, slopes, water, and fences;
- the entity snapshot is empty; and
- landscape smoothing is disabled.

These restrictions protect cross-category ordering while migration is incomplete. They do not start a legacy renderer. A
rejected transfer simply lets `PaintSurface` record the entire surface category into the normal GPU command stream.

`MapPresentationSnapshot::CanDrawSurfaceBaseIndependently` is reversible derived state. It maintains a count of blocking surface
records, so a temporary placement can disable the direct category and removal can enable it again without a world reset.

Surface snapshot chunks carry source revisions. Command recording converts only changed chunks into the GPU ABI and uses the
same revision as the immutable source; it does not retain opaque source snapshot pointers. Vulkan tracks uploaded revisions per
chunk and republishes only changed storage-buffer ranges.

Revision state advances while copy commands are recorded. If a frame is abandoned, `WorldSurfacePipeline` clears its recorded
revision state so the next accepted frame republishes all terrain and sprite-table buffers. Recorded-but-unsubmitted work is
never treated as resident.

## Command and resource ownership

`CommandDrawingContext` implements the indexed drawing contract by recording commands. Its CPU render-target allocation exists
only to preserve legacy sub-target pointer arithmetic; those bytes are not rasterized or uploaded as a framebuffer.

`GpuTextureCache` owns a persistent power-of-two indexed atlas. Image identity includes invalidation generations; cached TTF
surfaces use stable allocation-independent identities. A frame is sealed with owned first-use upload bytes and a residency lease.
Physical atlas slots cannot be reused until every packet referencing them retires.

Atlas and remap-table preparation decode bitmap/RLE asset bytes with `SpriteAssetDecoder`; they no longer invoke software
sprite drawing. The decoder preserves encoded-zero, transparency and source-remap rules independently of sprite placement,
clipping and compositing. Its bounded interface validates supplied byte spans; the trusted `G1Element` wrapper cannot validate
allocation length because that metadata is not available. Other CPU sprite-analysis and off-screen drawing consumers remain
separate migration work; removing the atlas dependency does not qualify all scenes or permit deleting the software renderer.

World-surface commands own immutable converted chunks and an immutable sprite table. LightFX packets own resolved light commands,
their palette, and an optional CPU-rasterized intensity image when GPU compute is unavailable. No command contains a pointer into
live gameplay state.

## Screenshots and auxiliary output

Normal screenshots are explicit synchronous readback of the latest successfully presented post-composition indexed canvas. A
request attaches to pending visual work or publishes a control packet when no visual packet is queued. Readback occurs on the
render worker after presentation and uses the existing indexed PNG path.

The backend also exposes an opt-in diagnostic SDR capture. `RequestFrameCapture` marks an acquired frame before submission;
the backend copies its actual final swapchain image into owned storage before presentation. `ReadbackFrameRgba(frameNumber)`
waits for that named frame and returns tightly packed RGBA8 plus physical/logical extents, scale settings, palette and swapchain
versions, and device/driver metadata. This includes LightFX, scaling and attachment encoding, but excludes the OS compositor,
display colour management and cursor. Captures expire on slot reuse, resize or abandonment. Ordinary frames perform no final
colour copy or readback. Unsupported transfer-source surfaces and HDR are rejected; the indexed screenshot API is unchanged.
The diagnostic upload-ring capacity must cover the captured image's byte size. Main-application screenshot plumbing does not
yet expose this diagnostic API.

Giant screenshots and preview images are different: they create independent X8 targets and render their own live or temporary
world. Their render-target identity keeps them outside the main `PresentationScene`. They must not call Vulkan frame lifecycle
methods or reuse main-window snapshots.

## Vulkan lifecycle and output

One render thread owns hot Vulkan lifecycle operations: resize, present-mode changes, palette updates, `BeginFrame`, submission,
presentation, readback, abandonment, and resource retirement. The UI thread samples drawable extent and scale quality and
publishes them with each immutable frame. Scale mode and `ceil(windowScale)` are consumed together with that frame's logical
and drawable extents. The existing UI chooses nearest for integer window scale and smooth-nearest for fractional scale; the
backend also implements the linear contract. A zero drawable skips acquisition without destroying the last valid swapchain.

SDR is default. HDR activates only when the user opts in and the surface advertises an approved 10-bit plus HDR10 ST2084 pair.
The final pass decodes sRGB, converts to BT.2020, applies the configured paper-white mapping, and encodes PQ. If the exact pair is
unavailable the renderer remains in SDR. Screenshots remain indexed SDR until their API can carry colour-space metadata.

Windows uses native Win32 Vulkan surface creation because the pinned SDL package lacks compiled Vulkan video hooks. Linux and
macOS use SDL Vulkan; macOS runs the same backend through MoltenVK. Output and correctness contracts are platform-independent.

## Rules for extending the renderer

- Add data to the immutable publication boundary before moving another paint category to direct GPU preparation.
- Keep eligibility local to the category being transferred. Never express incompatibility as a viewport-wide live-world or
  alternate-renderer mode.
- Prefer positive ownership state such as `SurfaceBaseDrawn` and `CanDrawSurfaceBaseIndependently` over ambiguous “fallback”
  switches.
- Treat auxiliary targets as separate owners. If a new preview needs temporary simulation state, isolate all corresponding
  publication state or keep that target outside publication.
- Update revision or residency state only when its failure and abandonment behavior is explicit.
- Keep packets complete, immutable, bounded, and disposable.
- Put benchmark logs, migration experiments, and superseded designs in `docs/archive/`; keep this document current and concise.

## Verification

Renderer changes require all of the following in proportion to risk:

- focused map-presentation, GPU-foundation, and temporary-map isolation tests;
- the complete unit test suite;
- a Release x64 build with Vulkan enabled;
- a hidden Vulkan EverythingPark benchmark with VSync disabled;
- interactive checks for track-design previews, prebuilt ride placement, construction ghosts, Tile Inspector, renderer resize,
  screenshots, weather, transparency, and multiple viewports when those paths change.

Performance reports must separate simulation TPS, command-preparation CPU time, GPU pass time, upload bytes, atlas misses, and
presented or superseded packet counts. Visual parity must compare both indexed pixels and actual final RGBA bytes. The scaling
fixture reads the accelerated SDL output target and Vulkan swapchain at identical extents; an indexed hash cannot certify
palette conversion, LightFX, filtering or output encoding. Save every divergent reference/result/diff sample and require an
agent's manual visual inspection. See the [scaling visual review](vulkan-scaling-visual-review.md) for the initial evidence and
the migration plan for remaining main-window and platform qualification.
