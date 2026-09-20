# Retained nonuniform terrain: first bounded slice

Status: **independent foundation applied but not yet compiled; preparer integration staged only; no new terrain admission**. Author `decode_assets`, 2026-09-19. The goal is GPU-owned terrain selection, projection and edge construction from retained map records. Existing flat-terrain pixel qualification does not establish this scope.

## Smallest useful scene contract

Use an entity-free 32×32 park with **exactly one visible, non-ghost surface element per tile**. Every tile has no water or park fences. The central 16×16 region contains adjacent world elevations 16/32/48/64, all fifteen canonical non-steep slopes 0–14, named grass lengths 0–6, grass/sand surfaces and rock/red-wood edges. The surrounding band and reserved map border stay flat at Z16. This creates actual facing cliffs, unequal neighbour corners, rear-edge attachments, both filler directions, material transitions and nonzero sprite offsets. It is not a single isolated surface over an otherwise empty frame.

The first admission predicate excludes steep/diagonal slopes, entities of every type, additional tile elements, water, fences, tunnels, underground/height/ownership/construction overlays, selection/inspector/patrol drawing, smoothing and incompatible viewport flags. Invalid/missing assets or unqualified covered-zero/format combinations retain the whole existing Vulkan paint path. Admission is scene-wide and independent of the current camera: rotating must not change which route is selected. General populated-world support remains a later requirement.

These four terrain objects are already present in the original immutable native fixture's loaded-object census: `rct2.terrain_surface.grass`, `rct2.terrain_surface.sand`, `rct2.terrain_edge.rock`, `rct2.terrain_edge.wood_red`. Resolve identifiers through public object APIs and pin actual slots, object files, referenced image files and original G1 bytes. Existing presence is evidence of availability, not a substitute for a new fixture receipt or proof that every resulting sprite format is supported.

## Audit findings that prevent a simple admission change

1. `Map.cpp::ConsumeMapPresentationChanges` currently stores four detailed and four distant **already chosen ImageIds** per surface; slopes/water/fences and other tile elements set `requiresCategoryInterleaving`. `MapPresentationSnapshot` also blocks unequal base heights. Removing these guards would not implement the missing drawing rules.
2. `GpuCommandDrawingContext::DrawWorldSurfaceScene` converts changed 256-record chunks and retains their revisions. `VulkanWorldSurfacePipeline` uploads only changed chunks and sprite-table revisions. This ownership mechanism is reusable, but surface ImageId selection should move to static object tables plus raw tile fields. Camera movement must not republish tile records.
3. `Viewport.cpp` still calls `ViewportFillColumn`/`ViewportPaintColumn` after native base admission. `Paint.Surface.cpp` skips only the base sprite; it still examines neighbours and paints sides. That is a base optimization, not the user's no-per-frame-CPU-terrain-paint endpoint. The new complete-terrain route must bypass those terrain visits entirely, including map-border blank tiles.
4. Each bottom side compares two own and two neighbour corner heights. It may emit an asymmetric lower filler, a run of vertical strips and a top filler. Rear sides above ground use entry offsets 30–35 and attach at screen-relative Y `(baseHeight - firstCornerHeight)*16`. They are not ordinary world-positioned cliff strips.
5. `PaintAttachToPreviousPS` prepends attachments. The software calls top-left then top-right; their actual traversal order must be preserved. Base-plus-attachments is a sorting unit; lower edges are independent parents with different bounding boxes. A tile row-major depth counter is not proven sufficient for nonuniform heights.
6. The current native vertex/compute shaders duplicate the RLE minification phase. They omit the ordinary recorder's bitmap/RLE distinction and covered-zero representation. Real surface/edge sprite metadata and linked zoom variants must be audited. Exact offsets, coordinate-shift truncation and negative view phase are part of the pixel contract.
7. Border tiles use `SPR_BLANK_TILE` at Z16 in the frozen painter, regardless of their stored surface; locations outside the declared map can also contribute blank tiles. The complete route needs a GPU-generated border/background domain or an equivalent exact background algorithm. Keeping a per-frame CPU blank-tile stream would leave this slice incomplete.

Sources: [map publication](../src/openrct2/world/Map.cpp), [snapshot contract](../src/openrct2/world/MapPresentationSnapshot.h), [recorder](../src/openrct2-renderer/gpu/GpuCommandDrawingContext.cpp), [GPU pipeline](../src/openrct2-renderer/vulkan/VulkanWorldSurfacePipeline.cpp), [compute shader](../data/shaders/vulkan/world_surface_compact.comp), [vertex shader](../data/shaders/vulkan/world_surface.vert), [frozen terrain semantics](../src/openrct2/paint/tile_element/Paint.Surface.cpp), [object selection](../src/openrct2/object/TerrainSurfaceObject.cpp). The software painter is read-only throughout this work.

## Staged independent foundation

`obj/vulkan-parity/nonuniform-terrain-staged/manifest.json` pins candidates and baseline source evidence. The three foundation candidates were subsequently applied byte-identically with build registration, pinned by `obj/vulkan-parity/terrain-foundation-application.json`; no test execution is claimed yet. A separate preparer stage corrects the unexported gameplay recipe to canonical slopes 0–14 and named grass lengths 0–6. Raw selector 7 and slope 15 remain explicitly labelled representation cases in the rule tests.

| Candidate | Purpose | Evidence still required |
| --- | --- | --- |
| `src/openrct2-renderer/gpu/TerrainSurfaceRules.h` | Plain immutable std430-sized shape/edge data and a 576-byte normal-surface material selector table per object. Ordered wildcard specials compile once per object revision. No frame commands or pixels are produced. | Compile; public object-rule differential tests; frozen trace coverage for shape/edge offsets and attachment order; eventual GPU readback of actual rule results. |
| `test/tests/TerrainSurfaceRulesTests.cpp` | Compare corner tables to the unchanged public slope rules and 2,736 material/offset combinations to `TerrainSurfaceObject::GetImageId`; verify recipe coverage and reserved border. | Coordinated build/run. These CPU data tests do not prove GPU pixel parity. |
| `test/terrain-parity/NonuniformTerrainRecipe.h` | Pure deterministic 1,024-tile preparation recipe, reused by frozen prepare and both verify modes. | Integration into the existing preparer after balloon recipe work; export once and pin; independent frozen/current round-trip census. |

The material lookup indexes `(grass-or-distant, rotation, tile parity)`. It contains only selected entry numbers; the shader uses the raw tile slope to select shape offset and combines the object entry with its resident sprite metadata. CPU compilation is object/asset initialization, not a per-tile or per-frame graphics decision. Descriptor recolour and atlas/zoom-chain metadata must be captured at the same immutable object revision.

The initial staged edge table states corner pair selection, rotation-0 neighbour offsets, bottom-face world offsets and rear attachment entry offsets. It deliberately does not claim to implement strip emission, sort order or actual rendering. No changes to `FrameExecutor`, shared submission services, live GPU command ABI or renderer build metadata are part of this foundation.

## Retained implementation contract after foundation proof

Publish compact pointer-free tile facts: base Z, raw slope, grass length, surface/edge material IDs and validity/admission bits. Use copy-on-write chunks/revisions and the existing epoch reset contract. Keep material rules and asset/zoom descriptors in immutable tables keyed by object generation. A terrain edit changes its own tile record; the compute shader reads adjacent records directly, so unchanged neighbour records need not be rebuilt into draw lists. If derived bounds or eligibility summaries are cached, their exact dependency invalidation must be tested.

The compute stage rotates slopes, looks up shape/grass/variation entries, reads neighbour heights, emits base/attached rear edges/bottom strips and derives visibility/projection. It must preserve bounded output capacity with explicit overflow rejection before submitting a partial scene. Emitted device buffers stay GPU-owned. The CPU may submit camera/view/clip constants and changed tile/table ranges; it may not construct visible tile sprite commands, projected lists or terrain sort keys each frame.

Ordering requires a separate proof. Export the unchanged frozen `PaintSession` parent bounds and attachment traversal in a standalone diagnostic harness, then compare the GPU's emitted records/order for neighbouring height/slope pairs before relying on final screenshots. Either prove a bounded deterministic ordering rule for this restricted family or implement the required dependency/order handling on the GPU. Do not copy the software's per-frame sorted list to the GPU or excuse overlaps with a tolerance.

After the complete terrain path returns an explicit accepted result, `ViewportRender` can skip CPU terrain column preparation/painting for that scene while preserving non-world UI paint. Record diagnostic `cpuTerrainPaintVisits=0`, positive base/edge counts, retained tile/table upload bytes and named generation identity. This is a new complete-terrain contract; reusing `SurfaceBaseDrawn` alone cannot express it. Coordinate this integration with B1 retained entities and E5 auxiliary service ownership before changing shared files.

## Immutable fixture and proof sequence

1. Extend the standalone frozen preparer with a new recipe ID `nonuniform-terrain-v1`. Apply the staged tile recipe using public setters; clearance equals base Z as in the unchanged public `LandSetHeightAction`, including sloped surfaces, no simulation ticks follow, and all entities/rides/spawns/entrances/animations remain empty. Preserve the original flat fixture and balloon recipes.
2. Export once with unchanged frozen core. Pin park SHA, source/build/asset receipts and full tile/object census; verify the identical file in two fresh frozen and one fresh current process. Compare every field and retain explicit zero entity counts. Do not regenerate independently per renderer.
3. Start with actual main UI around world focus `(512,512,48)` at all four rotations and zooms 0 and 1, including odd projected camera phase. Save the actual projected origin in the input receipt. Add one near-border view per rotation so blank-tile policy is visible, not hidden by a center-only crop. Smoothing/steep/entity controls must explicitly fall back.
4. Capture frozen software, current software and Vulkan from fresh profiles with loaded CSG assertion and identical assets/camera/ticks. Require complete indexed and final physical RGBA equality, same-build separate-process repeats, native non-vacuity and zero CPU terrain paint visits. Review every divergent group manually before changing production, and review corrected and representative passing samples afterward.
5. Qualify retention using separate deterministic states: unchanged replay and camera-only movement must upload zero map/material bytes after warmup; one elevation/slope/material edit must change only its retained delta range and its precise GPU-visible neighbour effects; object replacement or map reset must invalidate the correct generations. Compare edited output against unchanged frozen painting of identical state. Retain old output/generation handles while new changes arrive to detect accidental mutation.
6. Pin admitted tile count, base/edge strip counts, material and sprite-format census, input revisions, CPU visit counters and world upload bytes. Diagnostic capture is allowed only for evidence. A readback-free advancing/camera workload remains necessary later for Gate P performance qualification.

## Checklist

- [ ] Apply and build the independent staged rules/recipe tests after hash review.
- [ ] Add loaded grass/sand/rock/red-wood metadata corpus and exact sprite-offset/format/zoom-chain receipts.
- [ ] Integrate frozen recipe preparation and pin one round-trip-verified mixed-height park.
- [ ] Obtain frozen parent/attachment/edge trace and all-rotation neighbour-pair rule evidence.
- [ ] Implement bounded GPU edge emission, exact source sampling, ordering and background; prove actual GPU outputs.
- [ ] Add complete-scene admission with zero CPU terrain paint visits; keep unsupported states on existing Vulkan fallback.
- [ ] Complete all-rotation/zoom/border/edited-state pixels, fresh repeats and manual review.
- [ ] Verify retained deltas and camera-only zero map uploads; then extend toward steep terrain, water, overlays and populated-world interleaving.

Every checkbox stays open until its stated evidence exists. This slice neither closes general terrain parity nor permits software deletion.
