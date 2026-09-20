# Presentation publication audit

Audited 2026-09-19 against the accepted post-catch-up renderer sources for migration gap **G11**. The findings below describe the pre-fix baseline. Build 14 / run-09 implements the bounded correction and passes six publication-data tests plus the manually reviewed main-publication pixel fixture. Software and Vulkan both use this publication path; changing it changes their shared scene input and cannot by itself prove renderer parity.

## Original findings

1. **Map and entity completion are admitted independently.** In [PresentationScene.cpp](../src/openrct2/drawing/PresentationScene.cpp), `MapPresentationPublisher::Acquire` and `EntityPresentationPublisher::Acquire` each test their own task group's `IsComplete()`. `BeginFrame` calls both separately and wraps the returned pointers in a new `PresentationGeneration`. One worker may finish between the two checks; a shared wrapper does not establish a shared source revision.
2. **Independent scheduling can capture different source states.** Each publisher's `Schedule` returns when its own pending group exists. `PresentationScene::ScheduleNext` calls both regardless of whether the other is busy. A fast publisher may capture a later state while the slow publisher still holds an older batch. Gating admission alone would therefore be insufficient.
3. **Synchronous map publication does not synchronously capture entities.** `BeginFrame(..., synchronousMapPublication=true)` drains and refreshes only the map, then calls the ordinary entity `Acquire`. Entities refresh synchronously only when they have no front snapshot, including after a map reset. [Viewport.cpp](../src/openrct2/interface/Viewport.cpp) requests this mode for construction selection, Tile Inspector and the track-design placement tool. Those cases can pair current transient geometry with older entity positions.
4. **Entity recycling does not check retained ownership.** Entity `Acquire` casts its previous const front to mutable `_recycle`; `Schedule` reuses that object without checking whether older generations still own it. [EntityPresentationSnapshot.cpp](../src/openrct2/entity/EntityPresentationSnapshot.cpp) forwards `CaptureStorage` into registry storage capture and `BuildCapturedStorage` rewrites lookup/spatial storage. A caller retaining generation A across later publications can therefore observe A change. Current viewport paint waits for its column work, which reduces exposure, but does not satisfy the advertised immutable-generation ownership contract in [PresentationGeneration.h](../src/openrct2/drawing/PresentationGeneration.h).
5. **Published generations lack a source stamp.** `PresentationGeneration` owns only map/entity pointers. Map epochs distinguish world replacement; they do not identify the tick/draw/interpolation source for a paired publication. A diagnostic cannot currently demonstrate coherent source state from generation metadata alone.

The original audit alone did not establish affected pixels or justify an exception. The subsequent [visual review](vulkan-publication-visual-review.md) reproduces the stale synchronous entity result (154 pixels) and records its exact whole-frame corrected reference and run-09 closure. No extra exception is inferred from the retained-generation data test.

## Implemented correction

The existing map/entity publishers remain owned by `PresentationScene`; no new rendering service or pending-generation type was introduced. `BeginFrame` holds the whole front generation until both publishers are ready, and `ScheduleNext` refuses a new capture while either component is pending. An unchanged map retains its immutable front while the corresponding entity capture advances. Synchronous publication drains prior entity work and captures current entity storage at the same main-thread boundary as the current map. Recycling requires unique ownership. Epoch reset barriers and auxiliary-preview isolation remain intact.

This is a shared scene-input correction, separate from Vulkan rasterization. The external frozen-source oracle and baseline artifacts remain unchanged. The published-generation source-stamp enhancement and broader transition/temporal validation below remain open; the fix does not manufacture a stamp from pointer ownership.

## Contract evidence and remaining checklist

- [x] Retain generation A through B and C and verify its entity location/orientation and map height stay unchanged after subsequent captures.
- [ ] Extend retention coverage to type replacement, complete entity bytes, spatial lists and actual queued Vulkan frame packets.
- [ ] Gate one member of a pending pair with explicit task synchronization. Completing only the other member must leave the entire front generation unchanged. Release the gate and verify both advance together. Exercise both completion orders; do not use sleeps.
- [x] Change only entities while the map is unchanged, then verify the captured entity state advances with unchanged map data; a subsequent paired capture catches up to the newer live state.
- [x] Keep entity preparation pending behind an explicit single-worker gate through another `ScheduleNext` call. The newer map cannot capture independently; the whole original generation stays admitted while pending, then the coherent captured state advances.
- [x] Apply a transient map edit and entity movement together, request synchronous publication, and assert both reflect the current source boundary. The separate main-viewport movement fixture verifies exact visible correction.
- [ ] Replace the world epoch with pending work. Verify neither old map nor old entities enter the new generation and retained old generations remain readable.
- [ ] Repeat `BeginFrame` with the same draw count and paint secondary viewports. Confirm all consumers receive the identical admitted generation/source stamp.
- [ ] Render auxiliary temporary maps/previews between main frames. Confirm they neither consume main map changes nor replace its generation, preserving existing tile-revision restoration tests.
- [ ] Create replay samples for unequal completion, construction ghosts, interpolation and park reload. Save frozen software/current Vulkan/corrected-source outputs and inspect every divergence manually before approving any exception.

[PublicationSnapshotParityTests.cpp](../test/tests/PublicationSnapshotParityTests.cpp) now exercises initial synthetic/all-concrete-entity and imported-park capture, completed changes, synchronous coherence, retained ownership and deterministically gated scheduling. Existing entity/map snapshot tests remain complementary. The broader unchecked cases above are not claimed complete.
