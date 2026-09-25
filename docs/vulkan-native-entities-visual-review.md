# Native entities: build160 visual review

## Build172b low-track dark support diagnosis

The owner's `codex-clipboard-592e1238-60c6-48ca-9e9a-d38ff7a0491f.png` is reproduced at the upper right of `obj/vulkan-parity/everything172b-entrances-01/entrances-r0/`. A broad enlargement initially hid the omission; the reviewer corrected that assessment after viewing `agent-low-track-shadow-detail.png` at 5x nearest-neighbour magnification (upstream left, native middle, changed upstream pixels right). The static rectangle `(800,12)-(865,70)` has 193 differing RGB pixels, all upstream colour `(23,35,35)`. Rectangle `(816,25)-(880,56)` contains 205 differences of that colour; all 205 lie on the decoded opaque mask of original G1 sprite3682 at its original raster origin. The large dark support structure at `(930,0)-(1024,125)` has zero differences across 11,750 pixels. Counts and sprite-mask intersections are recorded in `agent-shadow-evidence.json`.

This is ride78, Heartline Twister Bengal Tiger. Direct PARK decoding gives scheme0: main54 (`invisible`), additional25, supports55 (`voidBackground`). The apparent shadow is actual mine-support art recoloured dark, not a missing lighting or filter pass. The affected piece at world tile `(87,242)` has worldZ336, type15 (`down25ToFlat`), direction3; its flat terrain also has worldZ336. `HeartlineTwisterRCTrack25DegDownToFlat` delegates to the upward transition with direction1, producing rails21307/21311 and the mine transition3682. These captures load the same saved park without an explicit simulation advance; nearby vehicle pose differences are outside the selected static rectangles. This finding does not qualify other claimed shadows or moving poses.

The shared support constraint is the cause: `worldTrackUnderRailDepth` subtracts one world unit when support and own-rail anchors coincide. Here it changes336 to335, placing the support behind terrain336. The existing placed-art layer cannot cross a world-coordinate difference. The staged correction keeps `min(authored, ownRail)` and uses support layer1 below ordinary rail layer2 at equal contact. It preserves every raster origin and one constant depth per sprite. Explicit station-front rail1, fixture4 and shell8 overrides remain intact. Current generated recipes have at most seven direct children; ordinary rail2 plus those children and one reserved wooden transition remains at most10, below15. Station glass remains shell8 plus one child9.

The unexecuted candidate is `obj/vulkan-parity/shadow-support173-staging/candidate.patch`. It adds the actual-GPU test `GroundMineTransitionStaysAboveTerrainAndBelowItsRail`, using the original Heartline recipe and mine cursor at worldZ336, overlapping synthetic masks, four rotations and held frames. It also updates scalar expectations and checks generated child budgets. Production source is unchanged by this staged candidate. Fresh GPU execution and paired images are required before accepting it.



Reviewed the saved Everything Park `paths-r3` reference/native pair at 1024×768, rotation3. This is a single camera sample, not a whole-world parity qualification. The parent runner reports 12,783 differing pixels and no validation diagnostics. No game, GPU or build was run for this review.



The full pair, five overlapping broad strips, eight tight entity/vehicle crops and six isolated balloon/crowd crops were manually inspected. All diagnostic enlargements use nearest-neighbor sampling. Triple panels are reference / native / binary RGB difference. Crop rectangles are original-image half-open pixel coordinates. Region counts include background and props; overlapping counts must not be added.



## Findings



- Peep artwork, pose, clothing colours and held-balloon positions visually align across the crowd strips. Isolated blue, green and pink balloon/owner crops are byte-identical RGB (zero differing pixels). Two eastern overlapping peep groups retain 3 and 2 differing pixels respectively; the orange balloon/owner crop retains10 pixels at another peep's overlap. These are small ownership/occlusion differences, not evidence of missing animation banks. They remain failures, not tolerances.

- Two vehicles have complete matching-looking artwork/pose but incorrect foreground occlusion. In `vehicle-crest` the changed band crosses the front rail/body boundary; in `vehicle-station` the native car/riders cover rail/platform pixels which remain visible upstream. No universal vehicle-height change is justified by these images.

- Most larger crowd-region differences visibly follow benches, lamps, railings, fountains and track/support boundaries. They are not all attributable to entities. This review does not reclassify those existing static-object differences as accepted exceptions.



## Source diagnosis



`worldEmitEntitySpriteAt` derives one constant depth from the actual entity XYZ, with body/rider local layers. Raster placement uses the same XYZ with only the shared tile-facing-corner correction cancelled. It does not use vehicle bounding boxes. `worldVehicleEmit` preserves this anchor for ordinary bodies and riders. Native rail/platform components derive their own constant anchor from authored placement, except named overrides such as station-cover edges. Thus the visible rail/car failures can arise even with identical sprites: the two owners use different spatial anchors. Original vehicle painters arrange authored parent/child bounds against rail/platform parents. A correction needs explicit component contact/foreground anchors; restoring painter sorting, changing pixels' depth within a sprite, or globally lowering vehicles would violate the current design or hide another ordering error. Rail-authoring review is in progress; no guessed source fix was made.



## Evidence



Input PNG SHA-256:



- `upstream.png`: `6375cb5c3ebb7ba7f6011009d5c0171104ac252bd7ffee059b5424942e0e14a7`

- `native.png`: `b00dee07718f0f41e9de3dd47b709e88fd6f84f816573fca61e283df0385f5e6`

- `difference.png`: `6db32823fa588a00335cefb91e7cc627ebca3af4548cd6d08adaad97c8afa67e`



Inputs: `obj/vulkan-parity/everything160-matched-art-01/paths-r3/`. The native log records63,672 static sets,71,460 after peeps,401,400 after vehicles,402,012 total,189 rectangular atlas pages, and316,281,540 uploaded artwork bytes. These are captured admission values, not measured per-frame traffic.



Annotated crops and their SHA-256 inventory are under `obj/vulkan-parity/entities160-visual-review/`: `report.json` (broad strips), `entities.json` (tight crops), and `isolated.json` (balloon/crowd crops).



| Tight crop | Rectangle | Different RGB pixels | PNG SHA-256 |

|---|---|---:|---|

| vehicle-crest | `[648, 407, 738, 470]` | 140 | `59de62245e5011a7b0dfddd2957bc34ca1b41a7869582ab0d30b30c14b64353a` |

| vehicle-station | `[818, 516, 908, 579]` | 367 | `731c5f03a0fe1d6ee82e69a6f72d8797ca0f95c399c5d1799d784e1b76957a42` |

| balloon-crowd | `[164, 498, 254, 561]` | 275 | `adf8fe411913a611d311af83da1e94b963c07349a8977ac35cc009732f292004` |

| path-peeps | `[420, 330, 510, 393]` | 200 | `70f58197bf79881b6af9554cc32682f5ebd0991a14acd7cd2fd914074af124b3` |

| path-peeps-two | `[522, 337, 612, 400]` | 193 | `a7e4c2472042410026a36b570f5915647756ab4817b242ddb651a9a35f13b631` |

| east-crowd | `[880, 126, 970, 189]` | 50 | `8a186fd20cc28ffb3b134eebfb58a1d27b1aea71ac8b1ba8dd64bee0f26ef3fe` |

| fence-peeps | `[738, 280, 828, 343]` | 220 | `f2a0b044e045ecaacfcc8a27ab140acf2760b20928113ba9fb99b50725238e03` |

| dense-west | `[25, 543, 115, 606]` | 184 | `0682514f4033353ec6c1b9eb6a5b7f2a97cb952a4c377e2088ac97cabf12abc7` |

| east-peep-group | `[899, 144, 924, 176]` | 3 | `21b5d25f2cf693a6babe1abdd9fb6c3a083a7f1ba54e9f151961378e70544a2f` |

| east-balloon-group | `[931, 135, 955, 179]` | 2 | `ded6a44606c77827409e8f01732b634de13ba5876f0d2659f4b258450e629c92` |

| blue-balloon | `[560, 260, 580, 289]` | 0 | `a033df759a68b2f13134480258e6c63a25ff65e8d466c751ea3d3888331eb428` |

| green-balloon | `[413, 332, 431, 364]` | 0 | `e957e6da211ff293b46208ef65a8a25c8d34c771ae6807c72fc25342448ec1d5` |

| orange-balloon | `[490, 475, 510, 509]` | 10 | `4415084711e048a0974e2a680d79998031f5c43fd89ba09e17256e9776f4cc84` |

| purple-balloon | `[847, 699, 867, 731]` | 0 | `1d402048bb731271653bd14f5453d8e352456837f6e374d23ec574ca9e42ff2e` |



## Checkpoint review after restart (saved build160 images)

A second reviewer manually inspected the saved `flats-r3`, `glass-r3` and `wooden-tree-r3` full pairs, eight newly enlarged crops, and the earlier crest/station vehicle, blue-balloon and path-peep crops. This is an offline review of build160, not a claim that later checkpoint changes were captured. Paired enlargements are upstream left/native right; source pixels are enlarged 2x with nearest-neighbour sampling. Rectangles, exact RGB difference counts and PNG hashes are recorded in `obj/vulkan-parity/checkpoint162-visual-review/review.json`.

Highest-impact retained findings for the checkpoint:

1. **Chairlift station art is absent.** The long grey platform between entrance/exit buildings in `flats-r3` is missing in native. The `(280,515)-(535,695)` crop contains 3,864 differing RGB pixels. This is the known unauthored station/ownership branch, not an entity sprite or texture failure; separate Chairlift source work is not admitted to this checkpoint. It should remain explicitly listed as incomplete migrated track coverage.
2. **Vehicle/foreground contact still differs.** The crest and station vehicle crops retain complete car/rider artwork but place some front rail/platform pixels behind the vehicle. The glass and suspended-ride station crops also expose people/balloons in front of structural pixels that occlude them upstream. These are shared component-anchor relationships to resolve, not evidence for a global vehicle-height adjustment. No new guessed depth exception was added by this review.
3. **Support/rail intersections are not exact.** The wooden-loop/tree crop `(340,175)-(625,445)` has 3,166 differing RGB pixels and visibly different lower support framing/occlusion. The white wooden-turn crop `(700,220)-(995,435)` has 1,259 differences at supports, rails and train intersections. The purple rail crop has 822 differences, including a foreground support intersecting a rail ribbon. Supports are now present throughout these scenes; their authored structure and component ownership still need targeted qualification. The images alone do not identify whether every changed beam is omitted, incorrectly placed, or occluded, so no single cause is asserted for all of them.

The three large domes remain coherent above their floors and the carousel/circus bodies retain coherent outlines in these views. Their broad crops still differ (127 and 199 pixels, including nearby structures/people), so this is a visual improvement observation, not exact parity. The isolated blue balloon remains an exact match, while the path-peep crop confirms that larger local differences include fixtures and overlap boundaries. None of these discrepancies is reclassified as an accepted skirt divergence.

A still frame cannot qualify animation progression, construction ghosts, underground switching, or startup responsiveness. Those gates remain separate. Go-Karts authoring20 and the later large-3D text sibling-order change postdate these saved images and must be judged by their own build/tests and subsequent captures.


### Ground-contact support arch: source cause and bounded correction

The owner's additional closeup (`codex-clipboard-9947401b-2b25-44c9-bdfb-92219c4ff7d7.png`) isolates the repeated lower wooden arch clipped by flat grass. Ordinary `worldWoodenNext` column parts start at `(0,0,groundZ)`; the emitted terrain uses that same constant scalar. Both previously used local layer0. With hardware `LESS`, terrain wins the tie over the arch's ground-contact pixels, while the higher portion remains visible outside terrain coverage. This is a structural coplanar-layer omission, independent of ride family and sprite image ID.

The bounded correction assigns the existing placed-art layer1 to ordinary wooden and metal support parents. Their constant authored depth, raster origin and owned-rail cap remain unchanged; explicit `AsOrphan` rail children retain their original owner relation. It does not infer a nearest footprint corner or change any pixel's depth independently. The new GPU regression `WoodenTrackFootingWinsOwnTerrainTieWithoutMovingItsAnchor` uses an admitted original wooden-flat support recipe with deliberately coincident opaque footing/ground pixels, four rotations and a repeated snapshot. Compilation/device execution and a fresh paired arch closeup remain required; this source fix alone does not close the other support/rail discrepancies listed above.


### Build164 qualification of the ground-contact fix

The fresh Everything Park `paths-r3` and `glass-r3` upstream/native pairs were compared against saved build160. All 852 changed pixels in the paths view and all 3,083 changed pixels in the glass view now equal upstream exactly; neither view gains a new mismatch. The paths total decreases from12,783 to11,931. A reviewer manually inspected four changed path-support crops and three changed wooden-support crops at4x nearest-neighbour enlargement. Their lower vertical and diagonal beams are visibly restored at ground contact, matching the original coherent arches. This qualifies the bounded footing correction in these samples, not every support intersection.

The initial requested path rectangle `(755,528)-(889,592)` remains unchanged with398 differing pixels: its lower boundary excludes the newly restored beam below it, and it contains the existing vehicle/front-rail overlap. That distinct ordering defect remains open. The ground-contact GPU regression passes all four rotations; the root runner reports38 focused tests passing. Offline evidence, exact image hashes and pixel classifications are retained under `obj/vulkan-parity/checkpoint164-support-review/`. These captures still do not qualify underground behavior, all camera rotations, or missing Chairlift stations.


### Build164 Mini Coaster: invisible rails are saved park state

The owner's pink-pole screenshot was located as Everything Park ride239, `Mini Coaster 1`, ride type87, object `WCATC`, track style47. Its entrance is tile132,159 and exit140,159. A fresh matched-art upstream/native capture at XYZ4352,4800,336, zoom0, rotation3 reproduces the apparently absent circuit in **both** renderers. The complete paired view has5,830 differing RGB pixels; this is not a parity pass.

An offline decode of the unchanged PARK input proves why the rails vanish: colour scheme0 contains `[54,54,30]` for main/additional/supports, i.e. `Colour::invisible`, `Colour::invisible`, `Colour::brightPink`. All193 track elements on ride239 select scheme0. The original ordinary MiniRC rail images18738/18739 are valid44x23 RLE images in RCT2 G1, not version-sensitive G2 references. No replacement rail geometry or visibility override should be added for this saved scene.

Reproducible facts are in `obj/vulkan-parity/missing-pink-track239-verdict.json` and the complete193-element list in `missing-pink-track239.json`. The original park SHA-256 is `c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`. Within the decompressed PARK data, the unique ride name begins at22421537 and its four12-byte colour triplets begin at22421596; the record prefix identifies ride239/type87. The full capture and runtime/input receipt are in `obj/vulkan-parity/everything164-mini-01/`.

A reviewer manually inspected the full pair and nearest-neighbour3x crops under its `mini-r3/` folder: `station-review.png` (rectangle632,86–1019,340), `crowds-review.png` (180,160–425,420), and `vehicle-review.png` (0,310–180,500). Upstream is left and native is right. Pink poles and the intentionally invisible circuit agree in these samples. Real remaining differences include peeps clipped at the lower-left walking corner against grass/path pixels, people overlapping queue fences/station fixtures differently, and local train/support contacts. These remain ordering regressions, not accepted skirt differences; the intentional invisible colour does not excuse them.

Source review also found that the native world peep emitter uses rawXYZ for both raster placement and depth. Original `kPaintPeepBoundBox` explicitly uses `z+5` for ordering while retaining rasterZ unchanged, to place peeps above railway surfaces; the frozen/shared `peepProject` helper already preserves this fact. Restoring that **peep-specific authored depth anchor**, equally for body/accessories, is a structural candidate for the contact differences. It is not yet a validated explanation for every changed pixel and should not become a global entity-height bias. Qualification needs the paired crowd/station samples and a surface-contact GPU case in four rotations without shifting raster coordinates.


### Peep contact-anchor candidate after164

Build166 tested a separate authored depth position `(x,y,z+5)` for peep bodies and held accessories while preserving rawXYZ projection and clipping. Other entity families retained their previous depth positions. Its focused production-emitter GPU regression passed coincident Z+3 contact/Z+6 occluder cases, held rerenders and free-balloon isolation in four rotations. However, the candidate failed full-park qualification: the 4K benchmark lost the device during warmup, and a subsequent single offscreen Mini Coaster capture with synchronous publication also lost the device without validation diagnostics. The shader change and its associated regression were withdrawn from production; their exact patch and artifacts are retained under `obj/vulkan-parity/peep-contact166-withdrawn/`. A passing isolated test did not qualify this candidate for deployment.

### Build167: shader rollback and unchanged visual qualification

Build167 retains the CPU publication work with all 26 shipped SPIR-V files byte-identical to stable build165. Its single Mini Coaster offscreen capture completed with validation enabled. A reviewer manually inspected the complete native image and a 3x nearest-neighbour crowd comparison at rectangle `(180,160)-(425,420)`, upstream left/native right. The native image is pixel-identical to build164 (zero differing RGB pixels); the upstream comparison still has 5,830 differing pixels. Pink poles with saved invisible rails remain correct. The lower-left crowd contact clipping and fence/station overlap discrepancies remain open and visually unchanged. Evidence is in `obj/vulkan-parity/everything167-mini-01/mini-r3/`, including `crowds-review.png`.

Two failures with the changed166 shader, including synchronous publication, followed by this successful167 capture with the prior shader establish a strong association with the shader variant. They do not prove a compiler defect or identify the exact device-loss cause. Offline SPIR-V validation passed166, and the bounded source audit found no new descriptor/index/count change from the depth adjustment. Full 4K performance qualification is a separate root-runner gate; this image review makes no benchmark or general stability claim.

### Foreground railing contact candidate after167

The owner's four new closeups were manually inspected: `codex-clipboard-743dd03e-f8b5-4d6e-a2c4-f30af63acc14.png`, `d23aa49e-e6ca-4021-aae9-21a6949a9797.png`, `53f8cfde-ad44-4eed-8b6e-54534e3b3bcb.png`, and `fe3c059b-0b1d-4335-a217-fc809e7749a0.png` in the same temporary directory. The queue corner visibly places guest legs/bodies over its camera-facing white/blue fence. The other views show structure/vehicle contact and a separate building/sign overlap; these are required follow-up samples, not proof that one fence patch resolves every image.

The shared source error for foreground fences is the use of the image's far-end raster origin as the whole component's depth anchor. A path front fence drawn at `(0,28,z)` receives a scalar of `28+z`, behind a guest at the tile centre `(16,16,z)` with `32+z`. Station front fences use the same far-end convention. Their authored extent spans the corridor, but depth previously discarded that extent. The source also explicitly distinguishes `frontTrack`/`frontHandrail`, yet extraction previously discarded that semantic role.

The candidate gives explicitly foreground components one constant anchor at their authored nearest contact endpoint: each horizontal axis uses `origin + max(extent-1,0)`, with authored contact Z. Path front-edge fences and corner posts, station front fences/front opening posts, and source-named front track/handrail components opt in. Rear fences, floors, decks, shelter covers, sprite placement, and all entity depths retain their existing rules. This is a shared component rule, not a per-pixel depth field or global guest/vehicle bias; the withdrawn166 entity shader remains untouched.

All 78 Python extractor tests pass, including four-direction source provenance and parent/child art preservation. Regenerated track data retains 57 styles and 5,470 style/type combinations. A byte-level offline proof finds exactly 541 changed words in the 846,650-word decompressed catalog, all setting the new foreground bit in the existing colour-role word; every image, coordinate, bound, extent, parent, descriptor, row and part address is unchanged. The unchanged support sidecar therefore keeps its exact rail addresses. Evidence is in `obj/vulkan-parity/foreground-contact168-authoring/metadata-only-proof.json`. C++ corridor/endpoint tests are authored but not executed by this reviewer.

Qualification remains open: compile and validate the changed shader, inspect upstream/native queue, station and wooden-track closeups in all four rotations, explicitly check neighboring entrance and shelter overlap, and run the normal 4K benchmark gate. The earlier166 device losses make focused shader tests alone insufficient for admission. The persistent black-screen/UI corruption report is investigated separately; this ordering candidate does not claim to fix it.


### Build168 foreground contact: not yet qualified

Matched-art Everything Park captures are in `obj/vulkan-parity/everything168-foreground-01/`. A reviewer manually inspected full paths/queues pairs and enlarged nearest-neighbour crops in its `visual-review/` directory. The paths mismatch count rises from11,931 (164) to12,513 (168). Comparing changed pixels against the identical upstream frame gives3,036 corrected pixels,3,618 newly incorrect pixels, and465 changed pixels that still differ. Glass changes include1,189 corrections and2,599 newly incorrect pixels. These samples do not qualify the foreground candidate.

The clear structural regression is the front station railing appearing outside shelter walls. `glass-r3-change-00.png` shows the glass station at approximately `(500,372)-(652,492)`; `glass-r3-change-02.png` shows the white shelter at approximately `(244,468)-(396,588)`. Each image contains upstream,164,168 columns. The earlier two columns correctly hide the tan inner fence behind the station wall;168 draws that fence across the wall. This is not animation drift. `glass-r3-change-01.png` also exposes railing over the neighboring entrance edge.

Path crops `paths-r3-change-00.png`, `paths-r3-change-02.png` and `paths-r3-change-04.png` show the mixed outcome: nearer fencing can now cover guest bodies correctly, but fence segments also overtake edge-mounted bins and parts of lamp bases that upstream places in front. The queue pair still has visible station/floor/building discrepancies, so it is not a general parity pass. No production fix is inferred from the aggregate difference count alone.

The candidate applied a front-contact rule to rails independently of the other members of the same physical edge. A regular station fence now reaches local scalar62+7=69, whereas its existing front cover uses31+23=54 and the adjacent tile's booth front uses32+34=66. Blindly lifting the cover to85 would recreate the earlier neighboring-entrance regression. The next bounded experiment must give rails, edge fixtures and outer station shells a coherent contact anchor and small local roles, while preserving the next tile's precedence. If that cannot pass these paired closeups, withdraw the candidate rather than deploying new incorrect overlap.


### Build170 bounded follow-up: shared contact of an enclosing station footprint

The owner reviewed the enlarged shelter example and confirmed that its columns are inside the glass house supporting the roof: the wrapping shelter's contact is farther towards the camera than the inner railing. This is a spatial relationship, not a universal rule that all glass goes ahead of fences. Original `TrackPaintUtilDrawStationCovers2` separately authors the front cover as a complete32x32 station roof/front-wall component and makes its transparent art a child; front station fences are separate32x1 or1x32 components inside that footprint. A fence outside a different shelter would need its own contact and must not inherit this station-specific relationship.

The unqualified170 candidate puts the flat station's front rail and enclosing front shell at its near tile boundary `(32,32,baseZ)`, with local rail1/shell8 roles and glass child9. It preserves raster XYZ and does not add roof art height to the footprint's contact. The adjacent booth front at local scalar66 remains ahead of this contact64; the earlier endpoint-plus-art-height candidate reached69 for the fence and would reach85 if copied onto the shell. Original source-named track/handrail endpoints remain separate. Flat path front fixtures share the path's front contact with a local role4 above its rail1; normal/broken/full direction banks keep their original images. Sloped paths retain their pre168 authored anchors pending an explicit slope-contact derivation and visual qualification. Focused tests encode corridor occupant/rail/fixture/shell/adjacent booth relationships rather than asserting only one helper's numbers. Build, four-rotation closeups and performance remain required.

### Camera-stress169: concrete persistent-worker failure identified

The normal-speed pan/zoom reproduction in `obj/vulkan-parity/camera-stress169-01/benchmark.log` stops at camera step38/39 with `error=65536 firstDetail=306501`. `worldVehicleEmit` uses exactly that error for a missing admitted vehicle image and records image+1, identifying image306500. This is not a filter node capacity error or per-pixel list overflow. The renderer then retains its terminal failure until restart, explaining persistence. The root runner owns the vehicle admission/source-selection repair. No speculative filter pool expansion or compositor rewrite was performed; diagnostic bits now distinguish malformed filter input from genuine node capacity exhaustion for future investigation.


### Vehicle yaw wrap: deliberate correction of two original fallback bugs

Source inspection found a separate, concrete illegal-image selector: `VehiclePitchUp50BankedRight67` and `VehiclePitchDown50BankedLeft67` call their25-degree fallback using `(imageDirection - 2) % 32` at `VehiclePaint.cpp` lines3912 and4295. With the50-degree banked group absent, pitch52/roll10 or pitch55/roll5 at yaw0/1 produce signed yaw-2/-1. A flat-only fallback then addresses images before the selected car bank; other fallback groups select the preceding rank. The shared selector compiled as C++ copied this source defect, so its previous CPU oracle comparison also passed the bad result. The GPU behavior must be assessed from the emitted SPIR-V, as corrected below.

The171 candidate wraps native selector yaw transitions with `&31`, matching `Entity::Yaw::Add` and fixing the C++ shared implementation. The frozen legacy source remains unchanged. The oracle matrix covers all32 directions and adjusts only those two original C++ cases; a focused regression calls the original fallback target with `Yaw::Add(yaw,-2)` and checks bank containment. Source receipts are under `obj/vulkan-parity/vehicle-yaw-underflow170/`. Crucially, later offline disassembly found that build170c already compiled GLSL `%32` to `OpSMod`, which returns a nonnegative result for a positive divisor. Therefore the prior source-only inference of a GPU negative-index defect was incorrect: this change aligns the C++ and GPU contracts, and is not needed to fix the GPU's actual Mandarin missing-bank incident. See the [Khronos SPIR-V modulo definition](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpSMod) and `obj/vulkan-parity/spirv171-audit/`. Build171 subsequently encountered a device loss; its shader refactor is unqualified and must not be described as a successfully deployed correction.


## Build171 candidate: diagnosed black screen and immutable rider animation residency

The build170c camera tour reproduced the terminal worker failure with missing sprite306500. Its cold catalog report resolves that sprite to `rct2ww.ride.mandarin`, car slot836: object allocation306453..306503, body base306456,16 images per bank, one seating row, and the rider-animation flag. Admission had included only body plus one seating bank (32 images). The second animation frame addresses the omitted rider bank; the shader correctly rejected the missing lookup, and the worker then stopped. This is a catalog completeness defect, not evidence of compositor node exhaustion or a driver reset.

The installed JSON object definition is essential evidence: Mandarin uses legacy animation5 (the two-frame simple vehicle animation), one seating row, and51 images selected from `MANDARIN.DAT[0..34]` plus `[51..66]`. The original DAT alone has different animation/seat metadata. The new `CarEntry::getNumRiderImageBanks()` owns the immutable rider-art domain, including the swan animation's doubled indices and animal animation's fixed four-frame sequence. Its byte-sized result is captured once with the object catalog. Admission now includes exactly48 Mandarin body/rider images and still rejects any bank exceeding its owning object's allocation; no per-frame residency scan or arbitrary allowance was added.

All six rider-animation object banks in this Everything Park run were inventoried against their installed JSON: rowboat7 banks, swan boat3, canoe6, dhow6, Mandarin2, outrigger6. Only Mandarin needs additional residency here (16 images). The zero-animation-frame guard mirrors `Vehicle::UpdateAdditionalAnimation`, which returns before dispatch even for animal animation. Evidence: `obj/vulkan-parity/vehicle-yaw-underflow170/rider-animation-inventory170c.json` and `rider-animation-json-metadata.json`. The separate DAT metadata receipt demonstrates why using DAT metadata in isolation would be incorrect.

Focused source tests exercise the original vehicle painter for every Mandarin heading and both animation frames, verify the failing306500 image is admitted, exclude the next object's first image, and reject a truncated owning allocation. Further tests cover ordinary rows, swan index spacing, animal frames, zero-frame dispatch and byte-domain limits. Build171 passed its79-test gate, but its camera tour encountered a device loss before qualification; no runtime success is claimed for that candidate. The negative-yaw wrap fix above is a separate confirmed source bug, not the cause of this specific black-screen incident.


### Qualified-shader isolation after171 device loss

The corrected diagnostic used the171 executable and fixed rider catalog with all26 archived170c SPIR-V files independently hash-verified in its actual runtime data directory. The root runner reports the complete3000-tick camera tour passed in211.84 seconds. The earlier attempted environment override was ignored by the UI and did not isolate shaders; its repeat failure cannot be used to blame the CPU changes.

Production source now preserves the170c GPU vehicle selector: the integer `swing` local field and inline modulo expressions remain. A compile-time C++ adaptation uses `&31` to emulate the already-correct positive-divisor SPIR-V `OpSMod` behavior, retaining the expanded CPU oracle tests. The bool/helper refactor is withdrawn with the failed171 shader variant; this does not identify the boolean, the helper, or a compiler/driver defect as the cause. Typed booleans remain appropriate generally. This is a bounded qualification workaround, pending later isolated shader investigation. Root must still qualify the next complete build with its other changes.


## Build172b closeups and remaining contact errors

Paired Everything Park captures completed for the glass station at rotation 3 and the entrance/flat-ride cluster at all four rotations. Root and agent manually inspected enlarged samples. The station's internal rail stays behind its enclosing glass; the neighboring small entrance doorway is intact. The sampled Pirate Ship entrance wall/floor/fence contacts are improved, but this does not certify the moving ship-to-entrance overlap. The owner identified that separate remaining error: the ship body can still cover the entrance because its whole-ride near-corner depth is too coarse for that component. Keep the correction local to existing component metadata; do not redesign global depth for this checkpoint.

The larger glass station frontage still hides more brown support/track art than upstream; interior wireframe differences also remain unclassified. Water conceals most newly restored entrance support columns in this cluster, so exposed-support parity needs another view. Evidence: `obj/vulkan-parity/everything172b-glass-01/glass-r3/agent-glass-body-detail.png` and `everything172b-entrances-01/entrances-r*/agent-*-closeup.png`.

The owner's low-track shadow report is visually confirmed in the rotation-0 entrance view. An enlarged rectangle at screen pixels (760,0)..(875,90) shows curved dark underside art upstream that terrain covers or replaces in native; the smaller static rectangle (800,12)..(865,70) has 193 differing pixels. This is a diagnostic region, not a mask for a parity score. The broader first crop obscured the defect. Evidence: `everything172b-entrances-01/entrances-r0/agent-low-track-shadow-detail.png`. Emission, palette and depth diagnosis is pending; do not yet attribute it to absent original assets or a generic shadow feature.


## Build175b Pirate Ship contact candidate: rejected after multi-view validation

The bounded candidate moved only the Pirate Ship's common body contact32 world units back from the reserved five-tile end to the nearest three-tile platform. Its focused CPU tests passed: all five sequences and four directions retained one common anchor; animated parts preserved ownership; the sampled rotation0 body scalar lay above its base floor and below the entrance. Those tests did **not** prove visual correctness. They omitted the front platform's independent raster-derived depth and the changed hull/entrance ordering in other views.

Root captured upstream/native at all four rotations under `obj/vulkan-parity/everything175b-contacts-01`; the reviewer generated identical screen-rectangle nearest-neighbor crops against both upstream and172b. Rectangles are recorded in `agent-crop-screen-rects.json`. These are diagnostic closeups, not masking for a parity score.

- Rotation0: `entrances-r0/agent-entry-native.png` shows the left entrance roof and sign restored; its frame beams no longer cut through the facade. However, `agent-pirate-upstream-native.png` shows a new near platform plank/railing strip crossing the lower hull and front frame. The earlier checkpoint did not have that strip overdraw.
- Rotation1: `entrances-r1/agent-pirate-upstream-native.png` and `agent-pirate-before-after.png` show the foreground exit roof/wall newly hiding a large part of the swinging hull. Upstream and172b correctly draw the hull in front. Water spray partly covers the lower contact, but the solid roof-over-hull regression is clear.
- Rotation2: `entrances-r2/agent-entry-upstream.png` and `agent-entry-native.png` show the original front frame beam crossing the entrance sign/left wall upstream. The candidate incorrectly hides it behind that entrance. This is a static frame contact, not animation drift.
- Rotation3: `entrances-r3/agent-pirate-upstream-native.png` and `agent-pirate-before-after.png` show the near exit roof restored: the previous frame beam no longer crosses its roof/wall and this contact agrees with upstream. No additional platform/end-support regression was apparent in this sampled view. The native ship still lacks the visible upstream passenger overlay; that absence predates the anchor candidate and is not qualification of passenger parity. Improvements in rotations0/3 do not outweigh the demonstrated regressions in rotations0/1/2.

The exact rotation0 constraint explains why another blind body offset is insufficient. Ride22 occupies world tiles(102,234..238), worldZ320; entrance is(103,235), exit(103,237). The previous body scalar11207 becomes11175. Its nearest base floor is11168 and entrance front is11194. But the front platform strip is emitted at raster(24,0,+9) for odd direction (or(0,24,+9) for even), with no explicit contact override, giving11201. Therefore the sampled requirements body>strip11201 and body<entrance11194 cannot both be met by changing this common body scalar alone. Moreover, rotations1/2 require original ship/frame components ahead of particular entrance parts. Any later correction must account for those existing component contacts rather than promote or demote the entire body again.

The parent chose to withdraw the Pirate-only candidate and its insufficient new tests, retaining the known earlier overlap until a bounded correction passes these views. No new roles or offset cascade is being added for this checkpoint. The independent low-track underside/support fix is qualified separately. Exact Pirate candidate source remains archived in `obj/vulkan-parity/pirate-ship-contact-staging/`; this subsection records a failed visual validation despite passing CPU arithmetic, not a claim that production rendering is fixed.


## Final candidate176: keep the verified support correction

Candidate176 retains the ground-level support correction and withdraws the Pirate Ship experiment. The exact static low-track rectangle at screen pixels(800,12)..(865,70) now has **zero differing RGB pixels** against upstream. Its dark artwork is the original mine support sprite, not a lighting overlay: the old own-rail clamp had forced its scalar from336 to335 beneath terrain336. Equal scalar contact now uses separate support/rail local layers. Other world anchors, sprite raster positions and the global depth formula are unchanged. CPU checks cover the existing child-layer budget; the actual GPU test verifies terrain/support/rail overlap in all four rotations and held-frame behavior.

The final Pirate Ship region(43,48)..(406,260) is pixel-identical to172b after withdrawal. Its earlier entrance-beam defect remains, while the newly introduced hull/platform/exit regressions are absent.

The Lift view exposes restored brown wooden entrance columns and braces at every visible elevated landing. This confirms emission of the shared entrance support art. It is not full ordering parity: some lower booth roofs hide upper-tier columns that upstream places ahead, and nearby tree/rail contacts still differ. Agent and root both inspected the enlarged image. Evidence and exact rectangles: `obj/vulkan-parity/everything176-contacts-01/agent-visual-review.md`; full comparisons remain unmasked.
