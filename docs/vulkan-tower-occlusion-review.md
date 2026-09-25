# Tower vehicle occlusion review

Status: confirmed source and still-image ordering defect. No tower production patch has been implemented, and temporal validation remains pending. This review is separate from the device-loss investigation.

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
