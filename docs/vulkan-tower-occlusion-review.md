# Tower vehicle occlusion review

Status: source-confirmed defect; a bounded tower-owned contact candidate is implemented for build 178. Compilation, temporal capture and external-geometry qualification remain pending. This review is separate from the device-loss investigation.

## Coordinate vocabulary

- **worldXY** means map rows and columns plus sub-tile offsets, expressed in world units. A tile spans 32 world units.
- **worldZ** means elevation in the map, expressed in world units.
- **screenXY** means projected pixel coordinates. It must not determine an object's depth.

Camera rotation can change the basis used to describe worldXY; it does not turn world coordinates into screen pixels. All depth equations below use worldXY and worldZ. Sprite raster offsets continue to determine screen placement independently.

## Confirmed visual evidence

`obj/vulkan-parity/everything170c-transport-01/agent-contact-review/tower-vehicle-170.png` is a manually inspected, four-times nearest-neighbor crop: upstream on the left, native on the right. The native observation cabin's rear roof covers the central shaft and leaves a black hole. The originals are `everything170c-transport-01/monorail-r3/upstream.png` and `native.png`.

This establishes an incorrect overlap at one pose. A still image does not establish the reported flicker's frequency or its complete range of affected heights. Sequential captures are still required.

## Source ownership lost in the native path

The original painters in `src/openrct2/paint/vehicle/Vehicle.LaunchedFreefall.cpp`, `Vehicle.ObservationTower.cpp`, and `Vehicle.RotoDrop.cpp` each author two independent body parents. Their raster positions are identical, but their camera-facing local world bounds differ:

| Component | Local worldXY offset from vehicle | Absolute worldZ of bounds | Horizontal extent |
|---|---|---|---|
| Rear body | `(-11, -11)` | `vehicle.worldZ + 1` | `2 × 2` |
| Front body | `(-5, -5)` | `vehicle.worldZ + 1` | `16 × 16` |

These values are authored local world bounds, not screenXY offsets. Launched Freefall and RotoDrop riders are children of the front body. Observation Tower has the two body parents without this rider loop.

In `data/shaders/vulkan/world_vehicle_emit.glsl`, `visitWorldVehicle` handles styles 2, 3, and 9 together. It calls `worldVehicleEmit` twice with the same vehicle worldXYZ, assigning local layers 1 and 2. The distinct rear and front spatial roles are absent. RotoDrop riders instead use the native `worldVehicleRotoRider` seat-ring approximation; that approximation also needs review against the original front-parent ownership when fixing the body.

The static side has a related omission. `worldTowerParts` in `data/shaders/vulkan/world_static_tower_maze_rules.glsl` authors shaft bounds at `(8, 8, segmentZ)`. Lift cage halves have separate bounds at `(2, 2, segmentZ)` and `(28, 28, segmentZ)`, matching the separate parents in `src/openrct2/paint/track/transport/Lift.cpp::PaintLiftCage`.

All these shaft and cage parts use `worldFlatAdd`, which sets `depthAnchor = 0`. `worldSetFlatBodyAnchor` in `data/shaders/vulkan/world_flat_ride_emit.glsl` only handles anchor roles 1 and 2. Consequently these authored bounds do not affect their depth. In particular, both Lift cage halves receive the same scalar and the same initial local layer even though their source split allows a cabin to pass between them.

## Exact current depth equations

`worldComponentDepth` in `data/shaders/vulkan/world_component_depth.glsl` computes the scalar `D = Rr(worldXY) + worldZ`, where:

| Camera rotation `r` | `Rr(x, y)` |
|---|---|
| 0 | `x + y` |
| 1 | `y - x` |
| 2 | `-x - y` |
| 3 | `x - y` |

`worldEmitEntitySpriteAt` in `data/shaders/vulkan/world_entity_emit.glsl` uses the vehicle's actual world position directly. Therefore:

```text
D_vehicle_rear  = Rr(vehicle.worldXY) + vehicle.worldZ
D_vehicle_front = Rr(vehicle.worldXY) + vehicle.worldZ
```

Only their local layers differ. The raster's tile-facing adjustment is explicitly cancelled for entity placement and does not change these scalar equations.

For the unanchored static shaft parts, `makeRecord` in `world_surface_compact.comp` retains tile worldXY and the part's raster worldZ. `worldCapturePaint` in `world_parent_capture.glsl` then applies `terrainPaintTileOrigin` before `worldComponentDepth`. For a tile origin `T = (32 * tileX, 32 * tileY)`, let `Cr(T)` be the rotated sum of that adjusted origin:

| Rotation | `Cr(T)` |
|---|---|
| 0 | `T.x + T.y` |
| 1 | `T.y - T.x - 32` |
| 2 | `-T.x - T.y - 64` |
| 3 | `T.x - T.y - 32` |

A segment emitted at world height `segmentZ` consequently has `D_segment = Cr(T) + segmentZ`. Base sections emit successive parts at base height, base height + 32, and base height + 64; later section elements use their own base heights. Lift rear and front cage parts at the same height have equal `D_segment`.

The scalar difference is therefore:

```text
D_vehicle - D_segment
    = Rr(vehicle.worldXY) - Cr(T) + vehicle.worldZ - segmentZ
```

For a vertically moving vehicle with fixed worldXY, this varies continuously with vehicle worldZ while the shaft's depth levels remain separated by 32 world units. Both body halves are on the same side of each segment except for possible local-layer ties. They cannot maintain a strict rear-body < shaft < front-body relationship throughout the travel. This is a concrete mechanism for changing overlaps; matching its exact visible period to the reported flicker still requires sequential captures.

Local layers cannot compensate for an incorrect scalar: `WORLD_COMPONENT_LAYER_MAX` is 15, and the depth encoding deliberately keeps every such layer below the next integer scalar. Restoring authored rear/front contacts is necessary, but a small worldXY adjustment alone does not prove correct ordering against every vertically overlapping shaft segment.

## Proposed typed ownership, pending review

Represent the tower's graphical assembly explicitly, with a stable owner reference and named component roles. A bounded design would contain:

- A `TowerVisualAssemblyId` or equivalent typed handle, including lifetime validation when its owner is replaced.
- An immutable assembly definition with its worldXY contact, base worldZ, and permitted component relationships.
- Distinct roles such as rear vehicle, shaft, front vehicle, and front-owned riders. Lift uses rear cage, cabin, and front cage.
- A vehicle instance reference to that definition, alongside its changing actual worldXYZ and animation state.

The owning ride or presentation module should define these relationships. Consumers should not infer them from image IDs, integer ranges, or the current vehicle pose. The existing cold `WorldRideStation` snapshot in `src/openrct2/world/WorldObjectPresentation.h` already stores station start worldXYZ and is a potential source for the assembly origin. That origin's relationship to each tower family must be verified. `VehiclePresentationRecord` currently lacks a ride or assembly reference; its car-object slot is insufficient because several rides can share the same object.

A shared assembly contact scalar plus bounded local roles could keep the internal rear/shaft/front relation invariant while actual worldZ continues to move the sprites in screenXY. Resolve ownership when construction or graphical membership changes, publish it with the immutable snapshot, and let shaders consume it. This requires no per-frame CPU paint replay or per-vehicle map scan. Each emitted sprite still receives one constant depth.

This is a candidate, not a qualified global depth rule. Flattening an entire shaft to one base-contact scalar may misorder elevated paths, crossing track, other towers, stacked floors, or caps. Those external constraints must remain correct as well as the assembly's internal relationships. Do not hide such failures with an arbitrary large bias or a generic peep-depth change. If the assembly scalar cannot satisfy the external cases, review the broader depth representation before implementation rather than accumulating per-ride exceptions.

## Qualification checklist

- [ ] Compare original body-parent and rider-child ownership for vehicle styles 2, 3, and 9, plus Lift cage and cabin components.
- [ ] Exercise all four camera rotations and representative zoom levels. Sample every worldZ phase around at least two 32-unit shaft boundaries, including top and bottom stops.
- [ ] Capture a fixed-camera sequential filmstrip. Verify stable cabin/shaft overlap and distinguish changing world state from any independent UI animation.
- [ ] Check empty and occupied RotoDrop vehicles, several rider angular phases, observation cabin animation, restraints, and Lift's rear/cabin/front ordering.
- [ ] Check elevated paths and track on both camera-near and camera-far sides at several worldZ heights, along with neighboring ground objects, raised decks, and shaft caps.
- [ ] Preserve sprite raster offsets and all non-occlusion pixels. Keep constant depth per sprite and avoid new per-frame CPU ordering work.
- [ ] After visual qualification, run the normal-speed camera tour and the 4K performance gate.

The evidence confirms loss of authored component ownership. The temporal reproduction and the assembly proposal's external-world correctness remain open.

## Build 178 candidate (not yet visually qualified)

The candidate applies to Observation Tower, Launched Freefall and RotoDrop (static families 20–22 / vehicle styles 2,3,9). Lift retains its existing behavior. The frozen source above describes the previous failure, not the new implementation.

`WorldTowerAssemblyContact` materializes the station's central worldXY contact and base worldZ into two previously reserved cold catalog words. `TrackAddStationElement` supplies the original single-piece station's base tile; the contact is its center (`startXY + (16,16)`). Moving vehicle records carry their owning ride ID in the unused high 16 bits of the existing status word, within the unchanged 128-byte snapshot record. Ride ownership is resolved through the coordinated immutable world/entity snapshot, never through worker reads of live game state.

All shaft segments share that stationary assembly contact. Rear body, shaft, front body use contact offsets -1,0,+1 world sub-unit. Each emitted sprite still has one constant depth. The cap remains a shaft child. The actual vehicle worldZ continues to control raster placement and cutaway. Its animation and current height cannot change which side of the shaft its two body parents occupy.

RotoDrop's former seat-ring sine/depth approximation was removed. Source-authored passenger selection is retained, and passenger images are children of the front body in original back-to-front order. At most 13 of the 49 visited angular slots are populated for any source pose; exhaustive CPU-rule coverage checks all 64 angular phases, four cardinal yaws and 0–32 passengers. This fits within the existing 15 local-child slots.

No animated state enters the cold catalog key, and the vehicle record size remains 128 bytes. This introduces neither a per-frame map scan nor CPU sorting. The source-level invariants do **not** prove external occlusion: flattening the vertical assembly to its foot contact must still be checked against elevated neighboring paths, track, floors and roofs. Do not promote this candidate to complete parity on the strength of CPU tests.

Fixture: EverythingPark ride 181 (`GDROP1`), central tile (113,197), center worldXY (3632,6320), baseZ 336; shaft segment bases 432..976. Capture a fixed camera near worldXYZ (3632,6320,500), including ring travel across 432/464/496 and top/bottom stops. Inspect all rotations, empty/occupied angular phases and original captures. The independent second RotoDrop is ride 510 near tile (143,227).

## Build 180 findings and 181 candidate

Manual inspection of `obj/vulkan-parity/everything180-contacts-01/roto-r0/comparison.png` found repeating bright ledges on the native shaft, absent upstream. The shared scalar had collapsed the original ascending order of overlapping shaft segments. This candidate was not accepted as parity.

The proposed eight-world-sub-unit shaft band was withdrawn before building because it could outrank a neighboring subtile contact. Reverse emission was considered but not implemented: that would restore a draw-order dependency.

The revised181 candidate keeps exactly one assembly scalar for every rear, shaft, cap, front and passenger component. A shared renderer-wide monotone D32 encoding reserves512 ULPs per existing world-contact scalar step, preserving the current coarse world/UI order. Tower roles use rear0, shaft body16+2*tier, optional cap+1, front144, passenger children145..157. The original uint8 tile-height domain (maximum255 multiplied by8 world units) contains at most64 tiers of32 world units. Consequently every source tier and cap fits strictly between rear and front, and all fine roles remain below the next world/subtile contact. Invalid/imported out-of-domain tiers clamp to the band's ends rather than escaping ownership. No scalar nudges, per-fragment depth or emission reordering remain.

The CPU tower invariant test exhausts that legal height domain, proving each previous cap precedes the next body and the highest cap precedes the front body. Global encoding tests separately establish that every local layer remains below the next world contact and follows the correct world/UI intervals. These do not prove visual parity: verify smooth segment joins and base/cap transitions in all rotations, plus raised external geometry. The shader change is pending qualification.

The reusable `test/cli-parity/TowerFilmstripDiagnostic.h` diagnostic is opt-in via `OPENRCT2_CLI_TOWER_FILMSTRIP=1` in the screenshot driver. It retains one context/service/device across real simulation ticks, logs ride181 poses to trajectory.jsonl, and captures bounded consecutive frames around the first ascent/descent crossings of worldZ432/464/496. The output argument must equal OPENRCT2_CLI_PARITY_ARTIFACTS; ordinary camera arguments are accepted. OPENRCT2_TOWER_MAX_TICKS defaults6000 and permits1..12000. It never changes a vehicle pose, ride state, or RNG. Only pause is temporarily cleared for one real gameStateUpdateLogic(false) call and restored afterward.

Each image explicitly captures one immutable auxiliary publication and asserts that its source tick and vehicle ID/ride/XYZ match the observed pose before rendering. It also verifies rendering did not change that pose or the tick. The report includes source ticks, poses, crossing direction and coverage. It fails qualification if all six crossings are not observed within the budget. Stops, every camera rotation, empty/occupied variants and all external geometry are separate outstanding coverage; six crossings alone do not complete this checklist. No timing from this readback-heavy diagnostic is a gameplay performance measurement.

## Build 182 static evidence

The bounded fine-depth encoding removes the repeating bright segment-top ovals in all four sampled RotoDrop camera rotations. An agent personally inspected the complete upstream/native pairs and nearest-neighbor crops under `obj/vulkan-parity/everything182-batch-02/roto-r0` through `roto-r3`. Each `agent-shaft-3way.png` shows upstream, failing180, and182 side by side: the shaft and base joins are smooth in182. The raised neighboring track/support crossings visible at the central shaft retain upstream front/behind relationships in these static samples.

The exact shaft-interior rectangle at screen pixels(501,210)..(520,436) has **zero differing RGB pixels in each rotation**,4294 pixels per rotation. `agent-tower-static-review.json` records that bounded measurement. It does not represent the full image: unrelated scene differences remain, and the sampled cabin is high in every view.

Higher graphical priority subtracts more positive D32 bit values and therefore produces a **smaller Vulkan depth**, as required by the LESS comparison. Tower fine roles stay subordinate to their shared world contact; they cannot move an object ahead of the next world/subtile contact.

- [x] Manually inspect static shaft seams and base joins in all four rotations of this fixture.
- [x] Confirm exact pixels inside the sampled unobstructed shaft region.
- [x] Inspect real-tick ascent/descent filmstrip across432/464/496 for ride181, rotation0, zoom0, empty vehicle (build186 evidence below).
- [ ] Qualify top/bottom stops, occupied/empty variants, further zooms and remaining external geometry.

Static evidence is promising but does not close the temporal checklist.

### Filmstrip185 first attempt

`obj/vulkan-parity/tower185-filmstrip-01` captured the initial image at tick3133831 successfully; it was personally inspected and retained a smooth shaft. The real simulation then advanced to tick3133832, vehicle5499 on ride181 moved from worldZ708 to709 and animation255 to0. The second image failed with `Vulkan upload ring has no room for world-surface sprite sets`. There was no qualified ascent/descent sequence, so the temporal checklist remains open.

The failure was traced to a staging preflight mismatch. `CaptureAuxiliaryMapPresentationSnapshot` intentionally allocates a new auxiliary map epoch per capture. The sprite catalog was reused, but `WorldSurfacePipeline::Record` resets its uploaded-sprite revision when that epoch changes. `NeedsSpriteAdmission` had checked only the sprite revision before that reset, so FrameExecutor omitted the bounded cold-admission staging burst. The shared reset predicate correction is being validated separately. Auxiliary epoch isolation and immutable publication ownership must be preserved; this failure is not evidence that vehicle animation changed asset-bank membership.


## Build 186 temporal evidence

The corrected staging preflight completed `obj/vulkan-parity/tower186-filmstrip-01`: 24 captures over 1280 real simulation ticks (3133831 through3135111), with one service/device and all six requested crossings recorded. Every captured publication tick equals its logged simulation tick. An agent personally inspected all24 labeled seat/shaft crops in `agent-review/contacts-1.png` through `contacts-3.png`, enlarged4x with nearest-neighbor sampling, and the three fixed-screen shaft sheets.

No repeating shaft ledges, lost front ring, or alternating rear/front ordering reversal was observed in these samples. The rear seats stay behind the shaft and the front ring/seat components remain ahead during descent through worldZ495,457,431 and ascent through432,464,496, including their captured neighboring frames. This also covers the initial animation255-to0 wrap and several rotating ascent poses. The visible lower-stop sample at worldZ342 has open restraints and retains the same ownership.

This is a bounded temporal check of RotoDrop ride181/entity5499 at rotation0, zoom0. All24 samples have **zero riders**. The upper reversal at worldZ978 is outside the camera, so its capture is not evidence of a correct visible top stop. No matched upstream temporal filmstrip was generated; the result establishes observed continuity rather than pixel-identical animation. Occupied passenger overlays, other rotations/zooms, the remaining tower families and every intervening pose remain unqualified. `agent-review/review.json` records exact crossing ticks and scope. Gameplay frame pacing and TPS are measured separately from this readback-heavy diagnostic.
