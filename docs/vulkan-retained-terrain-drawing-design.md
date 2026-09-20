# Retained nonuniform terrain: executable drawing checkpoint

Status: the bounded executable checkpoint passes build46/run41 (781 tests),
the32-case frozen/fresh/current corpus, and all32 actual-device indexed/order
comparisons with clean validation and independent manual review. See the
[review](vulkan-retained-terrain-drawing-review.md). Initial world transfer is
46,080 bytes, then zero in31 camera/policy samples. Runtime admission, final
RGBA/UI, edited-state lifetime and performance qualification remain open.

The immutable scene is accepted `nonuniform-terrain-input-02`, fixture
`nonuniform-terrain-v1`, park SHA256
`66ab45181c2c3f383b2674eb823e45447da05d055ca533c94e3107463264f3cd`.
It has 1,024 tiles, heights 16/32/48/64, slopes 0–14, grass 0–6, two surface
materials and two edge materials. Use the existing frozen preparation and
round-trip receipts; do not regenerate another park or use input01.

## Ownership and retained inputs

Keep the approved shared renderer/service ownership. A terrain drawing domain
borrows DeviceContext and submission slots, uses its pipeline-cache lock, and
owns no additional device, queue, window or worker. FrameExecutor eventually
owns this component beside the existing native world/balloon components. The
diagnostic probe uses that same component with a graphics-only device.

The raw tile/material snapshot and emission pipeline from manifest `f87a6c92…`
are the starting inputs. Camera-only frames upload constants, not tile arrays,
sprite choices, world-space draw commands, visible lists or sort keys. All
emission, column scheduling, projection and ordering stay on the GPU. Frozen
CPU traces are test expectations only and never become production input buffers.

Core publication must not depend on renderer headers. Extend the existing
pointer-free SurfacePresentationRecord/change batch with raw slope, grass,
surface/edge object slot, base height and unsupported-category facts. Capture
these only when ConsumeMapPresentationChanges publishes a changed tile. Renderer
conversion of changed chunks maps object slots to immutable material-table IDs;
it does not select per-tile images. Maintain scene eligibility counts incrementally
at that boundary. Camera changes must not scan gameplay tiles. Preserve the
current base route until complete-terrain drawing passes qualification.

Publish object generation and asset generation with the material/sprite table.
Object replacement rebuilds that table even if slots or bytes happen to match.
Map reset changes epoch. Each published chunk/table is const-owned with no mutable
publisher aliases. Retain old snapshots and the corresponding atlas residency
until their submission retires; do not replace a GPU table that an older in-flight
command still references. Allocate table/buffer generations per submission slot
or retain versioned allocations. The current emission component's serialized
reuse contract alone does not prove concurrent old-frame retention.

## Ordering: reproduce columns before optimizing them

Frozen `Viewport.cpp::ConfigurePaintColumn` partitions the world target into
32-world-unit columns (`32 >> zoom` pixels for zoom 0/1). It sets a full aligned
column culling range even when the visible target clips the first/last column.
Each column has its own PaintSession. It is therefore incorrect to use one
row-major terrain order, one normalized AABB depth, or one global sorted list.

The GPU column pass must derive the frozen traversal from camera constants:

1. Align the column x and `WorldY - 16` as in PaintSessionGenerateRotate; convert
   to the starting tile, apply rotation/odd-rotation adjustment and tile flooring.
2. Visit the two terrain locations per vertical step in exactly their original
   insertion order, for `(WorldHeight + 2128) >> 5` steps. Entity-only visits are
   absent in this entity-empty scope. Read emitted tile ranges by tile ID; create
   outside-map blank parents through the same schedule, not a CPU list.
3. Apply frozen coarse tile/blank culling and original-image `imageWithinRT`
   before admitting parents. Culling uses original G1 width/offset metadata,
   not the resolved linked-zoom variant. Parent rejection also removes its rear
   attachments. Keep this separate from actual framebuffer sprite clipping.
4. Reproduce CreateNormalPaintStruct's rotation-dependent painter bounds and
   RemapPositionToQuadrant/clamping. Preserve reversed quadrant insertion order.
   Do not normalize negative endpoint deltas or replace z-length -1 with zero.
5. Execute the selected frozen legacy/stable quadrant arrangement. The relation
   is not established as a transitive sortable key. Start with one workgroup per
   column and a bounded lane performing the exact linked-index rearrangement;
   parallelize independent columns. Attachments are emitted after the parent in
   stored prepend order. Front cliff strips remain independent parents.
6. Produce GPU-only SpriteCommand instances and VkDrawIndirectCommand counts.
   Clip each column's output to its disjoint visible rectangle. Depth keys need
   preserve parent/attachment order within a column; reserve a disjoint category
   interval before later UI/non-world commands. Zero-depth compositor sentinel
   and the 2^22 key limit remain enforced.

The bounded exact arrange pass is a correctness implementation, not a Gate P
performance result. Retain column bins/order when their raw/camera dependencies
are unchanged; optimize quadrant-local work only after frozen trace equivalence.
Do not generalize this serial-per-column bound to large populated parks or claim
vsync/TPS improvement from compute readback tests. The final performance gate
requires measured GPU order cost, CPU render time, upload bytes and simulation
headroom on advancing large parks without diagnostic readback.

## Projection, atlas and source sampling

`terrain_sprite_geometry.glsl` is the initial production helper candidate for
parent projection/bounds and sprite geometry. It applies the camera-facing tile
corner, inverse-rotated local draw offset, then Translate3DTo2DWithZ's arithmetic
half-sum shift. Rear attachments use the parent's projected point plus attachedY;
their y offset is screen space and must never be added to world z.

The geometry helper preserves bitmap/RLE differences, legacy signed-16 wrapping,
linked-zoom division toward zero, minification source phase, clipping and final
texel offsets. Initial support is camera zoom 0/1 with effective zoom 0/1 and
coordinate shift 0/1. Its arithmetic mirrors the current qualified command
recorder; the independent expected pixels must come from frozen software, not
another copy of this helper. The qualified diagnostic terrain column entry point
includes it; ordinary runtime admission remains closed.

Build an immutable image-to-sprite-variant table once per object/asset generation:
original width/height/offset for painter culling, resolved width/height/offset,
effective zoom, coordinate shift, RLE flag, asset descriptor index and effects.
Enumerate possible material entries/shape offsets and all required edge strips
once from object metadata, not by traversing tiles each frame. Keep the image
index mapping deterministic and reject missing metadata before viewport admission.
Resolve and hold atlas slots through existing residency mechanisms; a generation
or atlas-slot change republishes the relevant table before dependent rendering.

Output the existing compact SpriteCommand ABI and reuse indexed_sprite.vert/
indexed_rect.frag and the existing atlas descriptor/palette bindings. Add a bounded
indirect sprite draw entry to the reusable pipeline rather than a second private
fragment shader/atlas implementation. A native terrain component must never map
emitted instances back to the CPU before drawing.

Covered-zero pixels need explicit coverage handling. The current ordinary recorder
may emit a second coverage operation; blindly setting an effects bit on one sprite
does not reproduce it. First capture the actual four-object/blank sprite metadata
and pixel-format census. For the first bounded draw, either prove every reachable
variant needs no separate coverage operation or implement the exact paired GPU
coverage draw. Any unsupported variant rejects the whole route before suppression.

## Background domain

The reserved 32×32 border is not the entire background. Frozen MapIsEdge routes
outside coordinates as well as the reserved perimeter to BlankTilesPaint. For
ordinary background, the GPU traversal above creates SPR_BLANK_TILE parents at
Z16 using their frozen bounds and coarse y rejection. Blank sprites participate
in the same column order. Transparent background suppresses those parents.
Do not use a screen fill, a fixed finite ring or a post-render mask: elevated
terrain can overlap these sprites and minification phases depend on coordinates.

Pin the exact MapIsEdge predicate and dimensions with the trace. Bound column
count, traversal steps, parents, attachments, and output instances from accepted
target/map/height/metadata limits. Validate all multiplications and depth capacity
before recording. Camera position outside that proven domain falls back wholesale
to the existing Vulkan command path before any CPU terrain paint is suppressed.

GPU counters must detect any overflow/invalid reference and emit zero indirect
draws, never partial terrain. Such a failure during diagnostics is a failed test;
it must not become silent success at runtime. Before runtime admission, a conservative
CPU preflight bound based on immutable scene aggregates and camera constants must
prove allocations sufficient without constructing world draw lists. Unexpected GPU
invariant failure is an explicit renderer failure, not a readback-driven per-frame
CPU fallback. Ordinary unsupported-state fallback remains entirely preflight.

## Next executable implementation slice

The applied drawing checkpoint contains a real shared-device compute-to-indirect
sprite pipeline and a passing32-case no-window indexed-pixel/order test. No runtime
caller or admission was changed. Both compute SPIR-V paths remain explicit
receipt-pinned diagnostic inputs; full/default suite runs now require the corpus
and column/emission compile receipts before launching tests.

The diagnostic corpus captures the linked core's actual Generate/Arrange and
PaintDrawStructs output, plus dual-background software-decoded assets. It requires
explicit smoothing=false and rejects masked/recoloured attachments. Corpus
generation requires accepted input02, byte-proved frozen core, a separate current
core, identical harness, immutable asset trees, and exact frozen/fresh-repeat/current
JSON equality. Both sort policies and background policies, rotations0..3 and
zooms0..1 use odd logical pixel phases. This is 32 cases, not an exhaustive camera
domain. Current software equality is a separate check, not the frozen oracle.

The first GPU test checks complete indexed buffers and each arranged parent's
image/bounds/screen/attachment count. Full GPU parent, command and indirect-status
buffers are retained (approximately 2.1 GiB across 32 cases). The runner checks
exact raster bytes, required sample counts, upload-zero camera changes, validation,
source/SPIR-V/corpus provenance, and before/after runtime hashes. It does not yet
prove unused-capacity poison guards, concurrent immutable generations, edited
terrain raster parity, final physical RGBA or main-window behavior. These gates
remain open below; passing the first drawing test must not enable admission.

Independent source review identified the inverse screen-to-map traversal used by
PaintSessionGenerate: DirectionFlipXAxis(CurrentRotation). The shader uses that
inverse only for traversal and retains CurrentRotation for projection/bounds.

Keep this as one drawing checkpoint, in this order:

- [x] Add a standalone frozen/current column-trace driver built against unchanged
  frozen core. It loads input02 and records actual PaintSession parent image IDs,
  projected points, bounds, quadrant IDs, parent order and attachment chains for
  every column before/after Arrange. Snapshot lists by pointer-to-index mapping;
  never mutate the frozen painter. Pin all used frozen sources and loaded assets.
- [x] Record actual terrain/edge/blank G1/linked-zoom metadata and decoded pixel
  census, with material image-base and selector-generation identity. Use the same
  frozen loaded object set as input02; do not substitute synthetic textures.
- [x] Extend the retained draw component with camera-generated column work and
  bounded parent/index buffers. Compare actual GPU schedule/bounds/order readbacks
  to the frozen traces at four rotations, zoom0/1, odd phases and outside-map
  background in the fixed32-case camera set. Broader boundary cameras remain open.
- [x] Add indirect compact-sprite drawing to the shared pipeline. Feed retained
  emission output plus immutable sprite tables directly, then read back the final
  indexed target in a no-window test. Require whole-image frozen equality for
  ordinary/transparent backgrounds; inspect every divergence manually.
- [ ] Repeat camera-only and one-tile/object-generation/epoch changes with retained
  old snapshots and slots. Require zero map/material uploads after warmup for
  unchanged/camera-only cases, exact changed ranges for edits, and unmutated older
  results. Keep full before/after buffers, revisions and resource identities.
- [ ] Only then integrate a distinct CompleteTerrain admission result into
  ViewportRender and FrameExecutor. Skip both terrain column generation and blank
  painting for accepted scenes; assert cpuTerrainPaintVisits == 0 and no CPU
  terrain sprite commands. Keep B1 interleaving closed for this entity-empty scope.
- [ ] Qualify actual main-window indexed/final physical RGBA captures and fresh
  repeats, manual visual review, validation and depth ordering under UI panels.
  E5 auxiliary rendering must use the same component and retained generation rules.

The first traces now pin the bounded sprite-format census, both ordering modes,
column working sets and four-rotation culling for the recorded cameras. Wider
camera/state coverage and production material/atlas generation hooks remain open.
These are concrete implementation/test inputs. No broader ownership/module
redesign is proposed beyond the approved service and existing publication boundary.
