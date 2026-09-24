# Native flat-ride bodies: next checkpoint plan

Original planning audit, 2026-09-23. The owner subsequently approved implementation. The [static buildings checkpoint](vulkan-native-static-buildings-checkpoint.md) tracks the implemented families and current qualification; the inventory below preserves the pre-implementation analysis. Operating ride animation remains separate.

## Current coverage and the missing input

The checked-in native track system selects authored rows from raw track type, sequence, direction and chain/inverted/brake flags. Its image map resolves fixed source image identities. The current `WorldRidePresentationRecord` supplies ride/object/station identities, track styles and four track colour schemes; it does not supply ride-object image ranges, vehicle body/trim colours, station entrance/exit positions, or flat-ride pose.

The authoring05 coverage receipt (`obj/vulkan-parity/native-track-recipe-authoring-05/coverage.json`, recipe SHA256 `ae3ddc9cef25cff8a6805c6b40793484ae68f2c2148bfca8288ed6845e7012c6`) reports zero supported style/type rows for every family below. Thus even static platforms/bodies are not supplied by those recipes. Rejection reasons include unhandled floor/station helpers and dynamic `ride.getRideEntry()` image expressions; zero coverage does not mean the art requires simulation animation.

| Family / style ID | Static structure source | State beyond static structure |
| --- | --- | --- |
| 3D Cinema / 0; Circus / 7 | `Cars[0].baseImageId + direction`, repeated footprint pieces with distinct bounds; mulch floor and rope fences | Vehicle lookup changes interaction ownership, not the body image |
| Crooked House / 13 | Four object images, footprint pieces with separate bounds; floor/fences | Vehicle interaction identity only for the static body |
| Haunted House / 25 | Four object body images; floor/fences | Optional zoom-0 child overlay selected by `flatRideAnimationFrame` |
| Spiral Slide / 64 | Object-relative offsets 0..19: directional body pieces, inner pieces, bases and fences | `slideInUse`, progress and shirt colour select rider overlays (20 + direction*46 + progress), zoom 0 |
| Dodgems / 16 | Fixed G1 floor, roof frame, roof darkening mask, perimeter fences | Cars are independent moving vehicles; station no-platforms affects structure |
| Flying Saucers / 22 | Fixed G1 floor and fences | Independent vehicles; station no-platforms and entrances affect structure |
| Merry-go-round / 42 | Object-relative body; finite parked frame, floor/fences | Primary frame, orientation, riders; control-failure vibration also reads breakdown flags/reason/modifier and vehicle current_time |
| Ferris Wheel / 19 | Fixed G1 rear/front supports around object-relative wheel body | Eight body frames per direction; rider overlays additionally use occupancy, rider on-ride state and paired shirt colours |
| Space Rings / 63 | Object-relative ring body per footprint segment | Up to four vehicle choices, station/train counts, per-train colour mode, signed frame and rider colours |
| Twist / 75 | Object-relative body; parked direction maps to a finite frame; floor/fences | Vehicle orientation/frame, rider pairs and zoom-dependent overlays |
| Enterprise / 17 | Object-relative body and fixed floor/fences | Frame plus vehicle orientation; rider overlays |
| Swinging Ship / 73 | Fixed G1 rear/front frame with object-relative ship between them | Signed swing frame and riders |
| Swinging Inverter Ship / 72 | Fixed G1 frame plus direction-dependent object body | Signed swing selects alternate body ranges and relative order |
| Magic Carpet / 40 | Fixed G1 frame/pendulums plus object-relative gondola | Swing controls both pendulum images and gondola XYZ; rider overlays |
| Top Spin / 74 | Object-relative back support, arm, seat, arm, front support | Arm/secondary-seat frames, restraint position, seat XYZ tables and riders |
| Motion Simulator / 52 | Object-relative cabin plus fixed G1 stairs/rail, direction-specific parent/child order | Restraint position or primary frame selects cabin image |
| Shop / 60; Facility / 18 | Object-relative buildings, optional fixed G1 foundations | Static; Facility includes extra door/base pieces and a different direction mapping |

Maze (41), Observation Tower (55), Launched Freefall (34) and Roto-Drop (59) also have zero recipe coverage. They should remain explicitly listed separately: maze wall construction and tower stacking/top-section adjacency are different rules from flat-ride footprint bodies. Tower vehicles belong to the later moving-vehicle work.

## Smallest coherent implementation

Reuse the raw unified track/object records and common world count/prefix/emission pipeline. Keep the existing record's ride ID, type, direction, sequence, station index, base/clearance Z, colours and ghost flags. Add a native flat-body dispatcher before ordinary track recipe lookup, with explicit supported ride-style/type pairs so a flat ride cannot emit both paths. Do not call a CPU painter, generate instance quads on CPU, append a draw-last overlay, or duplicate one whole ride body at every footprint tile.

Add an immutable ride-object asset catalogue indexed by object slot. Each entry needs owning allocation start/count, `Cars[0].baseImageId`, a supported native family/rule identifier, and the finite body-relative image offsets used by that family. These are object facts and shared rules, not instance-selected images. `RideObject::Load` establishes `images_offset` and car bases; `Object::GetNumImages()` supplies allocation extent. `numCarImages` alone is not a safe whole-object bound because seating rows and other car groups extend the allocation. Keep all bounds checks before resolving art.

Resolve only ride-object slots used by published track instances and only reachable static-frame/body/support art for the first checkpoint. Retain the immutable catalogue and atlas lease in the submitted packet through its fence. Reuse unchanged generations in O(1). An old held catalogue must reuse its already-resident lease or wait/reject before touching recycled live image IDs. Do not use the increased atlas ceiling as a substitute for usage filtering, and do not preload all animation/rider frames for the static checkpoint.

Extend per-ride graphical facts with body/trim/tertiary vehicle colours (including per-train colours for Space Rings if included), colour mode, station object presence/no-platforms, and station entrance/exit tile positions. Floors/fences can derive their image/edge decisions on GPU from footprint maps, raw station coordinates and flags. Preserve the original distinction between null station style and an explicit no-platforms style. A bounded immutable station table referenced from each ride avoids recomputing per-tile fence masks on CPU. Ride ID reuse, object reload and map epoch reset must replace these facts atomically with the source records.

The first useful tranche is Cinema, Circus, Crooked House, Haunted House's base body, Spiral Slide's static pieces, Shop/Facility, and Dodgems/Flying Saucers enclosures. Follow with complete parked compositions of the animated flats: supports plus empty seats/gondolas/bodies, using the original no-vehicle pose formulas. A parked pose while a real ride operates is an explicitly temporary static approximation, not current-pose parity. For Top Spin, for example, frame zero still has a -10 seat-height offset; drawing every piece at the same anchor is wrong even before animation.

Preserve each family's original parent/child relation and rear/body/front order. Large bodies intentionally appear in multiple footprint pieces with different paint bounds; deduplicating by image ID changes occlusion. Extend the sprite culling envelope from the actual shared recipe offsets (ships span +/-64; Top Spin moves its seat; slide sprites are tall), rather than assuming the current track part margin is sufficient. Integrate through the common tile/height/ordinal path and retain the current cross-object ordering limitation explicitly. Do not add special tree/track draw-last exceptions.

## Later animation inputs, kept separate from asset generations

A small per-ride/per-vehicle pose stream should carry identity/generation, on-track/vehicle-present flags, primary and secondary flat-ride frames, orientation and restraints. Space Rings needs multiple vehicle identities; slide progress is ride-owned rather than vehicle-owned. Publish coherent source ticks and authoritative state at the simulation boundary. Use dirty pose updates in a persistent GPU buffer; changing a frame must not rebuild the object catalogue, resident atlas or terrain/object source arena.

Do not derive simulation-controlled ride motion from the render clock. Preserve signed frame interpretation where the painter casts to int8, exact discrete lookup tables and zoom gates. Rider overlays need occupancy and garment colours; Ferris Wheel specifically also requires the rider's on-ride state. Those inputs should reuse the retained entity ownership contract rather than restore render-time live guest/vehicle scans. Breakdown vibration is its own small state-dependent transform. Pausing must freeze these state-driven animations, while cameras can still move independently.

## Qualification and limits

Use tiny original-art parks with each supported footprint, all four camera rotations and zooms 0/1; compare externally against the pristine renderer in the chosen static/no-vehicle state. Include alternative ride objects for the same family to detect accidentally hardcoded G1 IDs, normal/ghost/remap variants, no-platforms and entrance gaps, clear/reuse/reload, and held old snapshots. Add one tall body crossing another tile/object; the existing synthetic diagonal overlap does not prove original painter bounds sorting.

GPU tests should exercise static image lookup, finite offset selection, parent/child ordering, removal and ride-slot reuse, missing catalogue rejection, and retry after abandoned submission. Later animation tests should change pose without any material/source uploads, cover restraints and signed frames, pause/resume, and demonstrate correct old-held-packet ownership. Root should run the existing serial validation/art comparison/4K benchmark process before deployment. No TPS/FPS prediction follows from this source audit.

Physical support construction, scrolling text, maze/tower rules, moving cars/riders and general cross-object bounds arrangement remain separate incomplete work unless explicitly implemented and qualified. Existing object103 banner-post occlusion and tree-on-track ordering findings remain recorded correctness gaps; this plan neither fixes nor accepts them as permanent exceptions. The user-approved outer-map cliff skirt remains unchanged.

## Source inspected

- `scripts/rendering/extract-native-track-recipes.py`; authoring05 coverage receipt; `data/shaders/vulkan/world_track_emit.glsl`.
- `src/openrct2/world/WorldObjectPresentation.h`; `src/openrct2/object/RideObject.cpp`, `Object.h`; `src/openrct2/ride/CarEntry.h`, `TrackPaint.cpp`.
- Static body/pose sections of gentle painters: Circus, CrookedHouse, Dodgems, FerrisWheel, FlyingSaucers, HauntedHouse, MerryGoRound, SpaceRings, SpiralSlide.
- Static body/pose sections of thrill painters: 3dCinema, Enterprise, MagicCarpet, MotionSimulator, SwingingShip, SwingingInverterShip, TopSpin, Twist; Shop and Facility painters.

This inventory distinguishes directly read source from the separate coverage-only observations for maze/tower families.
