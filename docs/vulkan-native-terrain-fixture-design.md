# Frozen entity-free native-terrain fixture

Status: frozen/current preparer builds and immutable input validation passed, 2026-09-19. Design reviewer `decode_assets`, implementation/reviewer `screen_capture`. The [pinned input receipt](vulkan-native-terrain-input.json) records exact agreement across frozen preparation and two fresh frozen/current reloads. The first native main-UI view also matches exactly; expanded view coverage is still in progress. It refines family 4 of [the main UI fixture expansion](vulkan-main-ui-fixture-expansion.md).

Use a small standalone console preparer linked against the **unchanged frozen core**, export one `.park` file, then pin that file and its census before any renderer comparison. The existing CLI has conversion, screenshot and simulation commands but no command that resets an imported park into an empty terrain fixture. `convert` preserves the imported world (except scenario-start behavior); simulation changes state. Neither produces the intended fixture on its own.

Two source details require explicit handling:

1. `gameStateInitAll(state, {32,32})` alone does **not** produce a native-eligible map. `MapInit` creates interior surfaces at base height 14 (world Z 112), then `MapRemoveOutOfRangeElements` resets boundary surfaces to minimum height 2 (world Z 16). `ConsumeMapPresentationChanges` publishes all 32×32 coordinates, including the border. `MapPresentationSnapshot::CanDrawSurfaceBaseIndependently` rejects any record whose base Z differs from its baseline. Normalize the entire declared 32×32 domain to **world Z 16** in the one-time preparer, including the boundary, using public surface setters. This agrees with normal boundary clearing and avoids depending on a raised border surviving later import cleanup.
2. The frozen `ParkFile.cpp` exporter writes wall-clock started/modified timestamps using `std::time(0)`. Two independently generated `.park` files need not have identical bytes. Export once and pin its exact SHA-256; compare canonical world census after each reload. Do not patch the frozen exporter, intercept its clock, or treat a different park SHA as the same accepted input. Regeneration produces a new fixture revision requiring review.

## Build and process isolation

Proposed additions, subject to the normal coordinated build window:

- `test/terrain-parity/NativeTerrainFixtureMain.cpp`: a public-API-only console program with `prepare` and `verify` modes.
- `scripts/rendering/build-native-terrain-preparer.py`: generate an isolated console project, compile the preparer against a pinned core and write build/source/dependency receipts.
- `scripts/rendering/prepare-native-terrain-fixture.py`: invoke preparation and verification in fresh processes/profiles, hash outputs, reject census differences and assemble the final artifact manifest.

The generated project can follow `src/openrct2-cli/openrct2-cli.vcxproj`: `ConfigurationType=Application`, console subsystem, inherited common properties and dependency linkage, with the new source file as its main and `libopenrct2.lib` as its core. Put generated project, objects and executable under a new `obj/vulkan-parity/native-terrain-preparation-01/` directory. Do not replace `Cli.cpp` or edit archived source files. Prefer a clean extraction of the reference source archive identified by `docs/vulkan-software-reference.json`, revision `9a092745f38a714cf33629aaa005d078d473e48b`. Reusing an existing frozen library is allowed only if its receipt verifies its exact unmodified core inputs, compiler configuration and dependency hashes; the CLI startup-plumbing tree is unnecessary.

Build the identical preparer source against current core separately for **verify mode only**. The frozen executable alone creates the accepted park. No UI library or SDL present hook is needed for preparation. Use an initialized headless context with graphics assets enabled so terrain objects and images are resolved; never enter `RunOpenRCT2`, a simulation loop, or a UI event loop. Each command runs in a fresh process with explicit data/RCT1/RCT2/profile paths. This also avoids cross-context QuickJS lifecycle hazards encountered in in-process tests.

## Exact preparation sequence

1. Verify the frozen source/build receipt, the existing `small_park_with_ferris_wheel.sv6` SHA, bundled data and original-game asset receipts. Refuse an existing output park path. The runner controls a new workspace output/profile directory and records the complete argument list.
2. Set `gOpenRCT2Headless=true`, `gOpenRCT2NoGraphics=false`; create the context, set its `IPlatformEnvironment` roots and initialize it. Pin locale and configuration with the same explicit profile inputs used for the UI comparison. Fail on missing assets or fallback-art use where original assets are required.
3. Use `ParkImporter::CreateS6(repository)`, `LoadSavedGame(source,false)`, `ObjectManager::LoadObjects(load.RequiredObjects)`, then `Import(getGameState())`. Record the loaded object list, preserving object type and slot, before resetting the world. This supplies the known climate/water/terrain resources as well as the original park's retained object set.
4. Call `gameStateInitAll(getGameState(), TileCoordsXY{32,32})`. This resets map, rides, entities, park, date, finance, news and weather while retaining loaded objects. Assert the object census remains unchanged. Keep simulation tick zero.
5. Resolve `rct2.terrain_surface.grass` and `rct2.terrain_edge.rock` through `IObjectManager::GetLoadedObjectEntryIndex(identifier)` and typed `GetLoadedObject`. Require valid loaded slots and matching identifiers; do not assume slot zero. If missing from the imported list, fail this version and revise its explicit object-list contract before trying again. Do not silently substitute another terrain object.
6. For every `(x,y)` with `0 <= x,y < 32`, require exactly one surface element and no additional element. Apply public setters: `setBaseZ(kMinimumLandZ)`, `setClearanceZ(kMinimumLandZ)`, `setSlope(kTileSlopeFlat)`, `setWaterHeight(0)`, `setGrassLength(GRASS_LENGTH_CLEAR_0)`, `setOwnership(kUnowned)`, `setParkFences(0)`, and the resolved surface/edge indices. Require non-ghost, visible surfaces. These setters prepare input data; they do not alter renderer eligibility rules. Any presentation state used for preparation diagnostics must be created only after this normalization (or refreshed through the public map invalidation/change mechanism).
7. Require every entity type to have count zero, all ride IDs null, no guest spawns and no park entrance records. Call `MapAnimations::ClearAll()` explicitly and record that operation; the animation containers have no public count accessor, so report their count as unobserved rather than inventing a measured zero. Surface-only tiles rule out animation-bearing map elements. Reset entity spatial indices/tweener as normal import preparation does. Pin/record current ticks, date and weather; no update calls follow. Explicitly set a stable English park name such as `Vulkan native terrain v1` if the inherited localized unnamed-park label would otherwise differ.
8. Set public saved camera fields to rotation 0, zoom 0 and projected center `Translate3DTo2DWithZ(0,{96,96,16}) = (0,80)`. Use a headless context without a main window so window state cannot overwrite these saved fields. The camera is near a map corner to expose boundary coverage at 640×480; the final UI review must confirm the actual visible edges before accepting version 1.
9. Produce the pre-export census described below. `ParkFileExporter exporter; exporter.ExportObjectsList = manager.GetPackableObjects(); exporter.Export(state, output, kParkFileSaveCompressionLevel);` Use the frozen `kParkFileCurrentVersion` (currently 60016 in the reference), not a value forced from the current tree. Record target version, compression level, exporter-source SHA and output size/SHA. Do not call `ScenarioSave`, which performs broader save-time preparation and window updates beyond this public exporter contract.
10. Exit. Run frozen `verify` in a second fresh process and current `verify` in a third. Each loads the **same exported bytes** with `ParkImporter::CreateParkFile`, loads its required objects, imports without ticks, and writes an independent canonical census. Reject any object/slot, tile, entity, camera or pinned-state mismatch. Verify mode never exports or modifies the park.

The output park is a local test input. It is not a renderer oracle and does not authorize any pixel exception. Bundled/licensed assets remain local immutable inputs; no upload or redistribution is part of this work.

## Canonical census and receipt

Write a stable JSON census with explicit zeros and a stable key/order convention. Hash the canonical UTF-8 JSON bytes with SHA-256 in the runner, rather than hashing native structs with padding/pointers. Retain individual records so a mismatch can be diagnosed.

| Part | Required fields and assertions |
| --- | --- |
| Provenance | Fixture ID/version, source park SHA/size, frozen revision and source archive SHA, preparer source SHA, core library/executable SHA, toolchain/dependency receipt, full command arguments and profile hash. |
| Assets | Original G1/CSG file SHA, bundled G2/fonts/palettes/tracks SHA, object repository revision, sorted loaded object `(type,slot,identifier,version)` records and source file SHA. Hash all files inside object directories/archives that supply images, or retain a decoded-image-content receipt plus the original referenced DAT/G1 hashes. JSON-only hashes are insufficient for externally referenced sprite data. |
| Declared map | `mapSize=[32,32]`, 1,024 coordinates, exactly one visible non-ghost surface per coordinate, total 1,024 elements in this domain, zero counts for every other element type. Map technical storage can contain surfaces outside the declared domain; report that separately and do not call it 1,024 total allocated elements. |
| Surface fields | Sorted `(x,y,baseZ,clearanceZ,slope,water,grass,ownership,fences,surfaceSlot,edgeSlot)` records; require a single base/clearance Z of 16, zero slope/water/fences, valid resolved grass/rock objects. Include border and interior subtotals so boundary heights cannot be omitted. |
| Entities | Iterate all `EntityId` slots with `EntityRegistry::tryGetEntity`, count every concrete `EntityType` including effects/litter/balloons, and require all zero. A guest/vehicle-only count is insufficient. Record spatial registry emptiness checks too. |
| Other state | Zero rides, spawns and entrances; exact tick/date/weather and saved camera; retained object table unchanged by reset. Record explicit `MapAnimations::ClearAll` and no subsequent updates; its private container count is not publicly observable. Record scenario RNG seed if available through its public export/read API; no logic tick is allowed to consume it. |
| Export | Frozen format version, compression setting, file byte count/SHA, export time interval and observed timestamp metadata if parsed. SHA identifies this particular immutable export. |
| Reload validation | Frozen and current verification executable receipts, process exits, each census SHA, per-field comparison result and final `accepted=true` only after exact agreement. |

Any missing asset, failed import, extra entity, unequal base elevation or mismatch is a setup failure. The output must remain unqualified until corrected and re-exported under a new version. The preparer should leave a failure receipt and logs rather than publish a partially accepted fixture manifest.

Two public-API census details are now confirmed in frozen source. `EntityRegistry::getEntityTileList({x*32,y*32})` observes every technical-map spatial bucket; loop the complete `kMaximumMapSizeTechnical` square, then call it once with `CoordsXY::setNull()` for the null bucket. All must be empty. Also, park serialization writes only `getTransientObjectTypes()` lists, including empty slots. The intransient `scenarioMeta` and `audio` lists are supplied by context initialization and retained by `LoadObjects`, not serialized by the park. Compare transient slots as park round-trip state and record intransient objects separately as pinned environment state; any environment disagreement must be diagnosed rather than attributed to lost park objects.

## Handoff to the UI harness owner

The primitive/UI agent owns UI changes. Pass the accepted park path/SHA plus camera and asset/census manifest; each frozen-software, current-software and Vulkan process must verify those inputs before its existing normal load path. No per-renderer world reset is permitted.

Proposed initial views use world focus `(96,96,16)` and `IWindowManager::SetMainView(projectedCenter,zoom,rotation)`. That API takes a **center**, then stores `savedViewPos` after subtracting half of viewport world dimensions; it does not directly set a top-left origin. Run the normal public viewport-position update used by the window paint path, then assert the actual view origin, zoom and rotation. Never “fix” a failed assertion by directly changing private viewport fields.

| Capture | Projected center | Zoom | Rotation | Required native coverage |
| --- | --- | ---: | ---: | --- |
| `native-r0-z0` | `(0,80)` | 0 | 0 | true |
| `native-r1-z0` | `(-192,-16)` | 0 | 1 | true |
| `native-r2-z0` | `(0,-112)` | 0 | 2 | true |
| `native-r3-z0` | `(192,-16)` | 0 | 3 | true |
| `native-r0-z1-phase` | `(1,83)` | 1 | 0 | true |
| `smoothing-control` | `(0,80)` | 0 | 0 | false |
| `gridlines-control` | `(0,80)` | 0 | 0 | false |
| `native-with-research` | `(0,80)` | 0 | 0 | true, plus nonzero ordinary UI commands |

For eligible captures use landscape smoothing off, overlay flags clear, empty selection/tool/inspector state and the main playing viewport. Retain and assert diagnostic `worldSurfaces=true`, positive `worldSurfaceRecordCount`, valid `worldEpoch` and named packet frame/version. A 32×32 map currently supplies 1,024 surface records; require that exact count once verified rather than accepting a token nonzero value. Terrain assets with encoded covered-zero pixels remain ineligible by current policy; that is an explicit setup failure for this native fixture, not permission to remove the guard.

Run each sequence twice in separate profiles. Compare full indexed and physical RGBA outputs against frozen software and current software, require exact repeatability, and manually inspect every divergent triplet before production fixes. The agent must also inspect the first exact candidate to confirm meaningful terrain texture, map-edge coverage and intended research-window overlap. If the proposed near-corner camera is clamped, calibrate the actual public camera once, document it in a new fixture version and rerun every renderer; do not accept differing camera inputs.

Later sloped/water terrain requires a separate design: current `requiresCategoryInterleaving` rejects slopes, water and fences, and unequal base elevations also reject native terrain. Such inputs can qualify fallback correctness but cannot qualify native drawing without an independently reviewed architecture change. Plain same-height material stripes could expand native coverage later if their actual assets pass the existing guards.

## Implementation checklist

- [x] Add preparer/build/runner sources; preserve frozen core source hashes. See [build/run instructions](../test/terrain-parity/README.md). Read-only archive verification checked 1,585 source/property files in `oracle-ui-source-02`; all match the accepted source archive.
- [x] Build frozen prepare/verify and current verify executables with complete receipts (`terrain-frozen-build-01`, `terrain-current-build-01`).
- [x] Export one versioned normalized terrain park and record pre-export census/SHA (`native-terrain-input-01`).
- [x] Reload identical bytes through frozen/current public importers and require exact census agreement (all three canonical census hashes identical).
- [x] Hand immutable artifact manifest to the UI harness owner; first admitted rotation-0/zoom-0 run and manual review passed. See [visual review](vulkan-native-terrain-visual-review.md).
- [ ] Add the eight explicitly named views/controls with required native/fallback coverage.
- [ ] Run both repetitions and all three renderer paths; inspect divergent and corrected samples.
- [ ] Update main UI fixture manifest and migration coverage only after measured success.

## Actual capture-driver camera arguments

The standalone UI driver's `--view-x`/`--view-y` arguments assign the projected **top-left** `viewport.viewPos` directly;
they do not call `SetMainView` or interpret the values as projected centers. Keep this existing baseline behavior. At the pinned
960-by-640 target, the design's rotation-0/1/2/3 projected centers convert to origins `(-480,-240)`, `(-672,-336)`,
`(-480,-432)` and `(-288,-336)` at zoom 0. At zoom 1, the view spans 1920-by-1280 projected units, so phased center `(1,83)`
corresponds to origin `(-959,-557)`. Capture report `viewPosition` is the actual origin and remains authoritative. Production
camera interaction/centering is a separate qualification; these direct fixture inputs do not claim to test it.
