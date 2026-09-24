# Animation state in the native GPU renderer

The deployed object checkpoint (`fbbd7c977e`, build106) deliberately includes the existing looping scenery rules. It does not yet provide a complete ride/entity animation system. The static-building extension preserves this distinction: parked body composition uses raw direction, station topology and four static-body colour schemes. It intentionally does not publish the entire simulation vehicle-colour array or pretend to supply operating poses.

## What already runs from shared state

Small-scenery frame sequences, fountains and frame-driven animated walls select their images in `world_prop_rules.glsl` / `world_prop_emit.glsl`. Shared object materials contain the resident art and sequence metadata. Each frame supplies one coherent snapshot tick; the shader computes the sequence offset from that tick, the material's delay/mask and the instance's fixed phase. There is no per-object frame increment or object upload for these loops. Rendering the same snapshot again does not advance the simulation animation.

Clocks use snapshot hour/minute values. Those are deliberately separate from the simulation tick because the original clock art follows real time. Clock-driven peep actions remain simulation work. Palette animation is also separate from object pose; advancing palette state must not require copying world objects.

`VulkanWorldObjectLayerTest.StationaryAnimationUsesSceneTickWithoutSourceUploads` exercises a changing frame with unchanged source records and asserts no world-buffer copies or image uploads on the subsequent frame. The build101 focused suite passed this test. That is a scoped result, not proof for every future animated family.

## What remains coarse

Wall doors, on-ride photo timeouts and land-edge doors are simulation-controlled state transitions. Their state still changes in `MapAnimation.cpp`, marks the owning tile graphically dirty, and passes through the existing immutable chunk publication. This is correct ownership but still resends more raw state than necessary. It is not the intended final transport for large populations of moving/animating objects.

General travelling vehicles and seated-rider overlays still lack native entity streams. The mechanism-body batch below now supplies operating flat-ride poses, pending compilation and original-art qualification. Remaining parked or absent moving entities are missing work, not accepted behavioral divergences. Do not turn a simulation-controlled mechanism into an independent shader clock merely to avoid uploading its state.

## Applied mechanism batch, qualification pending

The next checkpoint adds `WorldRidePoseSnapshot` at the same owner boundary as map capture. It contains source tick, map/entity epochs and immutable 80-byte raw records keyed by ride ID. The first four vehicle slots carry entity identity/generation, primary/secondary animation frames, orientation and restraints; the first slot also carries the carousel timer only when control-failure vibration needs it. Space Rings has exactly four original independently drawn segments. Every other migrated mechanism body reads only the first vehicle, matching the original painters; this is not a population cap for travelling vehicles.

The static object/station/colour catalogue stays shared while poses change. Its original-art residency set now includes complete body sequences rather than only the parked frames. A separate persistent buffer at binding 17 receives one allocation/copy call containing coalesced changed ride ranges. Repeated or paused content retains its record pointer and performs no pose-buffer upload. Submission cancellation invalidates the uploaded-pose cache; old held map generations retain their own raw records. Source tick and epoch mismatch is rejected before recording.

The producer currently polls the bounded ride table and at most four mechanism references per supported ride at publication, compares a reusable scratch table and copies it only on change. It does not scan the world or full entity population, choose images, call painters, or dirty static tile chunks for pose changes. This is an interim bounded producer; mutation-owned sparse pose notifications and measured publication cost remain separate acceptance work.

| Migrated family/group | Current animation behavior |
| --- | --- |
| Merry-Go-Round, Ferris Wheel, Space Rings, Twist, Enterprise | Original simulation frame/orientation rules choose resident body art on GPU. Carousel control-failure vibration uses its captured timer and breakdown facts. |
| Swinging Ship, Swinging Inverter Ship | Signed simulation swing frames select the original directional body sequence. |
| Magic Carpet, Top Spin | GPU applies original integer displacement tables and arm/seat frame rules; Top Spin restraints retain the original open-seat sequence. |
| Motion Simulator, Haunted House | Captured mechanism frame/restraints choose original cabin or zoom 0 animated overlay. |
| Spiral Slide | Ride-owned occupancy/progress/shirt colour drives the original zoom 0 sliding figure, including progress 46/47 handling. |
| 3D Cinema, Circus, Crooked House, shops, facilities, Maze | The original building painters do not select a changing mechanism-body frame; their static exterior remains appropriate. |
| Dodgems, Flying Saucers | Building floors/roofs remain present. Independently moving cars are still missing and need the vehicle entity stream. |
| Observation Tower, Launched Freefall, Roto-Drop | Static towers remain present. Moving gondolas/cars are still missing and need vehicle position/pitch/orientation publication. |
| Seated riders on other mechanisms | Still missing; body animation is not a claim of rider/occupancy parity. |
| Small-scenery fountains/frame sequences, animated wall loops | Existing shader tick/material/phase selection remains unchanged. |
| Clocks, wall doors, photo timers, land-edge doors | Clock hands use captured hour/minute. Authoritative doors/photo transitions still dirty their tile; raw selectors and supported track-rule coverage determine visible state. |
| Large scenery, banners, portal signs | Large static tile bodies have no general simulation frame selector in their original painter. Scrolling glyph/sign generation remains missing; it is not replaced with a fake shader clock. |

Authored CPU tests cover held pose generations, repeat/wrapped/skipped ticks, entity-slot reuse, ride removal/replacement, four independent ring poses, unchanged material identity, signed ship frames, mechanism displacement tables, restraints, zoom gates and complete resident body ranges. They have not yet been executed for this batch. `ObjectFixtureMain --animated-buildings [0|1|2]` supplies three separately saved, deterministic original-painter pose corpora with raw pose metadata and no simulation ticks. Compilation, actual GPU comparison/manual review, driver stability and 4K performance qualification are pending. No new deployment or parity claim follows from implementation alone.

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


Build114 original-art pose review and the identified entity-origin zoom correction are recorded in [the mechanism visual review](vulkan-flat-animation-visual-review.md). All three pose corpora remain pixel-parity failures pending corrected captures; the review does not qualify deployment.

## Animation audit and photo correction for checkpoint120

The corrected117 mechanism review is complete in [the mechanism visual review](vulkan-flat-animation-visual-review.md): all three authored phases, four rotations and both zooms were inspected. The 144 zoom1 body crops match; full-frame comparisons still fail on separately recorded missing layers/boundaries. This supersedes the earlier statement above that corrected mechanism captures were pending, without claiming every possible frame or seated-rider parity.

A source audit of the already migrated scenery found no additional selector defect: fountain/Cupid/Goo periods, frame-offset delays and cogwheel phase exemption, captured clock hour/minute, animated wall cadence and all wall-door direction/backwards frame tables follow the original painters. Simulation-controlled wall/land doors still publish on owning-tile mutation; this does not make their transport granular. Large-scene scrolling glyphs remain missing. Unsupported track effects are not implicitly qualified by this audit.

The audit did find a concrete omission in **admitted on-ride-photo pieces**. Raw timeout was already captured, but the authoring extractor discarded the camera/sign helper while retaining the track/platform. The120 source batch preserves a shared procedural photo marker with authored direction, small/normal art family and relative height. The GPU uses the raw timeout (zero versus any nonzero value), chooses resident camera/flash art, and emits the original three black-remapped parents in source order, including ghost remapping. `Paint2` also preserves its square tunnel request. No extra instance image choice, producer field, per-frame catalogue update or wall-clock animation was added.

Thirty-one extractor regressions and a CPU GLSL compile preflight passed before source freeze. Added C++ tests check independently frozen source image/offset tables for all directions, normal/small art and timeout0/1/2/3/255, recipe marker admission and complete art residency. Authoring13, the main build, GPU execution and original-art visual qualification remain the parent task's next gates. The external `--photo-states` fixture records twelve normal/small Ã— timeout0/1/3 Ã— ordinary/ghost specimens in eight camera views. No photo parity or deployment claim follows from these source checks.

The responsive-startup/cache audit found no concrete blocker. Existing CPU tests exercise off-UI-thread work, failure propagation with title restoration, actual native-window message responsiveness, truncated/corrupt cache data and driver/device/UUID mismatches. Those tests do not by themselves qualify visible progress, cold-start speedup, user-close cancellation or persisted-cache merge performance; those remain runtime evidence requirements.

Build120 original-art photo review is now complete: all96 fixed specimen rectangles match and all32 comparison pages were manually inspected. [The photo review](vulkan-onride-photo-visual-review.md) records the preserved full-frame skirt failures, scope limits, immutable hashes and independent authoring12→13 drawable-preservation audit. This supersedes the pending photo qualification statement above only for those samples.
