# Animation state in the native GPU renderer

The deployed object checkpoint (`fbbd7c977e`, build106) deliberately includes the existing looping scenery rules. It does not yet provide a complete ride/entity animation system. The static-building extension preserves this distinction: parked body composition uses raw direction, station topology and four static-body colour schemes. It intentionally does not publish the entire simulation vehicle-colour array or pretend to supply operating poses.

## What already runs from shared state

Small-scenery frame sequences, fountains and frame-driven animated walls select their images in `world_prop_rules.glsl` / `world_prop_emit.glsl`. Shared object materials contain the resident art and sequence metadata. Each frame supplies one coherent snapshot tick; the shader computes the sequence offset from that tick, the material's delay/mask and the instance's fixed phase. There is no per-object frame increment or object upload for these loops. Rendering the same snapshot again does not advance the simulation animation.

Clocks use snapshot hour/minute values. Those are deliberately separate from the simulation tick because the original clock art follows real time. Clock-driven peep actions remain simulation work. Palette animation is also separate from object pose; advancing palette state must not require copying world objects.

`VulkanWorldObjectLayerTest.StationaryAnimationUsesSceneTickWithoutSourceUploads` exercises a changing frame with unchanged source records and asserts no world-buffer copies or image uploads on the subsequent frame. The build101 focused suite passed this test. That is a scoped result, not proof for every future animated family.

## What remains coarse

Wall doors, on-ride photo timeouts and land-edge doors are simulation-controlled state transitions. Their state still changes in `MapAnimation.cpp`, marks the owning tile graphically dirty, and passes through the existing immutable chunk publication. This is correct ownership but still resends more raw state than necessary. It is not the intended final transport for large populations of moving/animating objects.

The renderer currently lacks the native vehicle, rider and general flat-ride pose streams. Static parked compositions are temporary missing-animation work, not accepted behavioral divergences. Do not turn a simulation-controlled mechanism into an independent shader clock merely to avoid uploading its state.

## Contract for the animation checkpoint

- [x] Keep art, sequences and material rules resident and shared across instances.
- [x] Derive purely visual looping sequences from one published tick plus stable instance phase; no full-object upload per frame.
- [ ] Add a compact, persistent hot-state buffer for authoritative vehicle/ride poses, separate from building/track/material records.
- [ ] Publish pose identity/generation and coherent source tick at the same snapshot boundary as other world facts. Held packets must retain the matching buffers through completion.
- [ ] Batch changed poses into contiguous transfers. Do not issue one allocation/API call per animated object.
- [ ] For independently timed visual sequences, publish sequence ID, start tick, initial phase, speed and run/pause state on transition; compute the current frame from these values. Prefer absolute tick arithmetic over mutable GPU counters, which complicate skipped frames, multiple viewports, replay and old snapshots.
- [ ] For simulation-controlled movement, publish authoritative position/orientation, primary/secondary animation state and restraints in the hot stream. Interpolation, if introduced, must be bounded by the published snapshots and must not read live simulation state.
- [ ] Treat start/stop, speed changes, breakdowns, doors, ride-slot reuse and rider occupancy as explicit transitions. Shared sequence metadata never means a shared mutable pose for unrelated instances.
- [ ] Verify pause/resume, delayed/repeated frames, multiple cameras, old held packets and object removal/reuse; compare original-art samples with upstream and have an agent inspect every divergent group.
- [ ] Measure upload bytes/calls, publication CPU time and 4K/12,000-tick pacing while animations change heavily. Preserve 360 TPS and approximately 144 FPS; no cached-screen repaint scheme.

The [update transport study](vulkan-gpu-state-update-strategy.md) proposes the sparse/dense transfer crossover and hot pose layout. Those proposals remain planning until implemented and measured. The static-building work must not claim that separation is complete simply because looping scenery already animates efficiently.
