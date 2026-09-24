# Constant depth per authored sprite component

Status: implemented in build137; original-art and performance results are recorded below as qualification completes. The unit is the complete authored sprite component: its covered pixels share one depth. The previous horizontal, upright, terrain-slope and cliff-face depth gradients are deleted. No mesh, per-pixel geometric depth, painter arrangement or neighbour comparison is part of this contract. Constant depth is implemented; complete original-art ordering parity remains open.

## Minimal key

Keep a component's authored world-space depth anchor `A=(x,y,z)`. Rotate only its XY coordinates by the camera's quarter-turn rotation, producing `(X,Y)`, and use:

`D = X + Y + z`

Larger D is nearer. Map this scalar monotonically into the reserved world depth interval and give **all four vertices exactly the same hardware depth**. Clipping, camera pan, zoom, texture dimensions, transparent texels, image offsets and pixel snapping must not change D. Opaque coverage uses the hardware depth test; transparent holes do not acquire geometry. The existing palette-filter compositor must consume this same constant depth, without evaluating a plane from fragment position.

This key is consistent with the projection `u=Y-X`, `v=(X+Y)/2-z`: moving a point along `(1,1,1)` leaves its screen position unchanged and increases D. It does not prove that a single point describes every occlusion relationship encoded by the original art. Ambiguous components need an explicit authored depth anchor, not a hidden family priority or a revived bounds sorter.

The persistent graphical record can hold the unrotated anchor (or four rotation-specific scalar keys) and an authored local layer. Movement or relevant graphical mutations update that state. A global camera rotation selects/transforms retained values. Current native materialization still reconstructs components from raw state each frame; replacing that with retained component topology is separate, unfinished work. Removing arrangement must not be described as completing all mutation-only component storage.

## Source anchor audit

| Component source | Image placement facts | Constant-depth anchor contract / risk |
|---|---|---|
| Terrain top, water top | Tile and source height; camera-facing tile corner | Use the authored world point for that component. Slopes remain one whole sprite with one depth; no corner interpolation. Choosing one point is an approximation requiring slope/crossing evidence. |
| Terrain cliff strips | Independent strip height and camera-relative XY offsets | Each strip is a separate component. Keep its authored constant anchor; do not recover the old fixed-X/Y plane. Rear raster attachments are screen offsets, not extra world displacement. |
| Path surface/deck, railings and additions | Tile, baseZ and each recipe's x/y/z offset | Retain component offsets. The raw tile minimum or common path base alone loses distinct placement facts. Deck decals may explicitly share a parent's anchor with a local layer. |
| Scenery, walls, banners, entrances | Owner base plus each static recipe offset; glass/decals may be children | Component world points and child ownership must survive. G1 bitmap offsets are raster placement and must not influence depth. Some back/front art shares an image anchor despite representing different positions. |
| Track/station and static ride parts | Original authored image offsets, parent link, raw element baseZ; animation can change component offsets | Use explicit component anchors. Colour/remap roles, image IDs and painter box dimensions are not generic depth classifications. No automatic horizontal/upright role remains. |
| Selected auxiliary cars | Packet owns actual car XYZ, but image records use synthetic inverse-projected XY with Z=0 | Use actual car XYZ for depth, rotated directly. The synthetic point exists only to preserve exact image projection. It cannot stand in for world position or height. Current packet has car-level actual XYZ, not independently authored 3D anchors for every car subcomponent. |

The concrete static conversion is in [makeRecord / worldOffset](../data/shaders/vulkan/world_surface_compact.comp) and [terrainPaintTileOrigin / terrainRotateXY](../data/shaders/vulkan/terrain_sprite_geometry.glsl). For a camera-relative recipe offset `r`, the existing image anchor is:

`A.xy = terrainPaintTileOrigin(tile*32, rotation) + inverseCameraRotation(r)`

Then rotate `A.xy` once for D. Equivalently, use the facing-corner correction on `record.world.xy` before rotation. Do not add that correction twice. A tree's `(16,16)` recipe offset maps to the actual tile centre under all four rotations only when this conversion is preserved. In contrast, selected car XYZ is already an actual world point and must **not** receive the tile's +32 correction; its z must contribute to D. See [selected emission](../data/shaders/vulkan/world_selected_vehicle_emit.glsl) and [packet ABI](../src/openrct2-renderer/gpu/GpuSelectedVehiclePaint.h).

Other sources inspected: [prop emission](../data/shaders/vulkan/world_prop_emit.glsl), [track emission](../data/shaders/vulkan/world_track_emit.glsl), [flat ride emission](../data/shaders/vulkan/world_flat_ride_emit.glsl), [entrance emission](../data/shaders/vulkan/world_entrance_emit.glsl), [tunnel/edge emission](../data/shaders/vulkan/world_surface_tunnel.glsl), [vertex sampling](../data/shaders/vulkan/world_surface.vert), and [filter collect](../data/shaders/vulkan/world_filter_collect.frag)/[resolve](../data/shaders/vulkan/world_filter_resolve.comp).

## Concrete unresolved anchor cases

**Portal back/front:** `worldEmitTunnelRequest` emits two separate parent sprites with the same draw anchor—side offset `(30,0)` or `(0,30)` and the same height. Their original component origins differ: the front parent moves to the near edge 31, while the first starts at 0. A pure draw-anchor scalar collapses both and cannot place a path between them. Build137 explicitly retains these two source-authored **constant** anchors independently of raster placement through `worldSetComponentDepthAnchor`. This corrects the blocked opening seen in136; remaining exact portal/path differences are still failures.

**Multi-tile bodies:** an image drawn from the ride's centre can overlap terrain owned by its front footprint tiles. A centre-derived key can let that ground cover the lower portion of the body. The correction remains one constant for the entire authored sprite. Build137 uses the explicit body origin offset `(16,16)` from the named Cinema and Carousel source recipes, leaving image placement and base height unchanged. It improves every captured Cinema view against136, but does not make partial burial exact. Original painter bounds can be inverted, zero-area or compatibility-only, so their maximum is not inferred as a generic anchor for every family. Remaining anchor choices need original-art evidence.

**Children and attachments:** a screen-space attachment has no independent 3D point; inheriting its parent's anchor is justified. A child with an explicitly different world offset is not automatically the same case. Specify whether it is a coplanar decal or a separately positioned component. Local ordering must apply to whole sprites and must not become a global family rank. Selected car roots also share one car-level world anchor until more specific authored component points exist.

**Equal-depth and precision:** separate owners can have the same `X+Y+z`. A deterministic tie is necessary where their coverage overlaps, but source/output iteration order and palette number are not a proof of geometric correctness. The filter compositor's operation-value tie is deterministic yet palette-dependent. Preserve repeated filters; do not deduplicate them. The retained two-D32-ULP child bias is finite: at depthBase0, D=0/layer8 ties D=1/layer0, and layer9 wins. Layer255 is about31.9 world units. This is a concrete open defect in local-layer precision, not an infinitesimal bias. A replacement must preserve the foreground UI interval and explicit range limits.

**Range and UI:** the maximum supported map can already put the positive coordinate sum above 65536 before all height offsets. Keep an explicit checked scalar range, preserve the foreground UI interval, and reject unsupported overflow rather than silently clamping different components onto one depth. The existing ±131072 range is a candidate bound to verify against actual source/auxiliary limits, not an unconditional proof for every scripted coordinate.

“No sort” here means no rebuilding of world painter priorities or comparison of component bounds. Palette effects still require their bounded composition ordering. That compositor must use the component's constant depth consistently; its existence must not be hidden by claiming the whole renderer contains no ordering operation.

## Pending acceptance checklist

- [x] Remove gradient roles and plane payload dependencies; all four vertices receive the same scalar independently of clipping. Actual GPU overlap tests verify every covered pixel at two zooms and two camera positions, with both front/back poses.
- [ ] Verify all four rotations preserve authored offset conventions, especially tile-centred props and actual-XYZ selected cars.
- [x] Inspect all288 original-art normal/underground specimen groups at native resolution, including portals, slopes, stations, shops and towers. This completes review, not parity; retained failures are listed in the linked review.
- [ ] Inspect flat/sloped path surfaces, bridge decks, railings, additions, and terrain/path crossings at every captured rotation/zoom.
- [ ] Inspect all changed track, station, static ride, scenery, ghost and mechanism samples. Keep exact failures; no broad mask or tolerance waiver.
- [ ] Verify child/glass/decal order and unrelated nearby owners, including equal-depth ties and maximum admitted local layers.
- [ ] Verify selected-car normal/underground context, elevated pose, empty-tile coverage, removal and original image sampling.
- [ ] Verify foreground UI, viewport clipping, range/overflow and queued-frame filter composition.
- [x] Run the same 4K 12,000-tick workload; retain accepted-present counters, checksum, CPU/GPU timings and pacing tails. Earlier build135 throughput and visual evidence do not qualify this replacement.

The audit above informed the implementation. Build136 established the constant-depth contract; build137 adds explicit component anchors for the bounded portal and ride-body cases. Both build receipts report successful forced shader compilation, no source mutations during build and no missing artifacts.

## Build137 validation and manual deployment

Build136 passes six focused GPU tests: whole-sprite overlap, four-rotation foreground props, camera pan without object uploads, track recipe state, actual selected-car pose/removal, and terrain-filter/foreground composition. After the explicit anchor corrections, final build137 repeats and passes the whole-sprite overlap, selected-car and terrain-filter tests. No Vulkan validation diagnostics occur; artifacts remain unchanged. Evidence: `regressions136-constant-depth-01` and `regressions137-constant-depth-01` under `obj/vulkan-parity`.

The [independent visual review](vulkan-constant-component-depth-review.md) accounts for all288 final normal/underground groups. All132 rectangles changed by137 versus136 have lower exact mismatch counts, and none has a higher count. This is not proof of complete pixel parity; original-art comparisons still fail. Portal mouths improve; normal fully buried stations match exactly in all eight views. Partial shop burial, underground filter/grid ordering and tower-aperture relationships remain active. The owner accepts underground skirts as an intended divergence, and assigns the pathological Cinema burial fixture low priority. These decisions do not waive unrelated ordering defects.

At the owner's request, build137 was installed to `D:/Games/Independent/OpenRCT2Mod` on2026-09-24 for parallel manual testing. Deployment verifies all28 qualified payload files, changes3 and preserves backups. Receipt: `obj/vulkan-parity/deploy-constant-depth137-01/receipt.json`; build receipt SHA-256 `f9bac1d55bf9a5a892e868de0ec6a76ae48a0758e3751b415ffeb271ff538e76`. Saves, objects and settings are unchanged.

A fresh build137 3840×2160, V-sync144, 100-warmup/12,000-tick benchmark completed after the manual-test process closed. It measured **359.717 TPS, 143.977 accepted presents/s, 0.172 ms CPU draw and 2.491 ms GPU**. All4803 submissions were accepted, with no lost, discarded, unavailable or out-of-date presents. Presentation intervals: median7.2ms, p958.9ms, p999.2ms, maximum15.532ms. Simulation mean1.877ms; maximum8.662ms. Final populations:17,042 guests,2,208 staff,2,128 vehicles. Checksum `07d58eaefde6aa6d000000000000000000000000`.

Evidence: `obj/vulkan-parity/performance-constant-depth137-12000-01/summary.json`. This is partial-render throughput, not full parity. Build135's57.097ms gap did not recur in this sample; the cause is still unproven. The subsequent support/text/footprint work requires its own measurement.
