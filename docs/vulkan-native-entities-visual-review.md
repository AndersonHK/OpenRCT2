# Native entities: build160 visual review



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
