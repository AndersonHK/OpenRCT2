# Native GPU track definition coverage

This records **authoring05**, generated from the current track source after correcting switch-break handling. The ten focused Python authoring regressions passed in the root-run qualification. **Build102 passed all six `WorldTrackRulesTest` cases**, covering structural recipe validity, known Looping selection rules and runtime catalog residency. Original-art raster qualification remains pending. Counts below measure translated source definitions, not complete ride support or visual parity.

## Scope and provenance

- **47 of 81 styles** contain at least one translated track type; **3,227 style/type pairs** are admitted.
- The immutable table contains **437,654 words**, **97,764 selection rows**, **13,089 deduplicated components**, and **12,093 distinct original image IDs**. A row has at most **4 components**.
- The raw table is **1,750,616 bytes**; zlib storage is **102,773 bytes**. The generated C++ include is **539,757 bytes**. Decompression and structural validation occur once; no per-instance CPU image selection is introduced.
- Runtime selection uses raw track type, direction, sequence, chain/inversion/brake state, ride style and colours. Unsupported definitions emit no track components; they do not invoke a CPU painter.
- Atlas residency is restricted to the regular, inverted, covered and covered-inverted styles referenced by **present rides**, derived from their ride types through the same immutable RTD table used by the GPU. [GpuWorldTrackCatalog.h](../src/openrct2-renderer/gpu/GpuWorldTrackCatalog.h) enumerates those shared styles' recipe images once per catalog generation, deduplicates the IDs and retains the complete GPU recipe table. It does not enumerate placed track elements or select their images on the CPU. Required image or capacity failures still propagate.

The three build102 catalog regressions verify the complete Looping-style image subset (including duplicate and absent rides), no images for absent/invalid/unsupported rides, and both Flying regular/inverted styles. They also check that required allocation failures propagate. The other three tests cover independent original Looping image IDs and rotated bounds, table integrity, and invalid selection inputs. These are CPU structural/catalog tests, not raster comparisons. Original-art capture subsequently remained blocked by residency of all loaded scenery props; that separate catalog issue does not establish a track rendering result.

The present-ride residency policy does not yet cover a modified track element whose raw ride type references a style belonging to no present ride. Such cases need an authoritative shared type inventory before their asset residency can be claimed complete.

The source authoring tool is [extract-native-track-recipes.py](../scripts/rendering/extract-native-track-recipes.py); focused regressions are [test-native-track-extractor.py](../scripts/rendering/test-native-track-extractor.py). The generated table is loaded by [NativeTrackRecipes.cpp](../src/openrct2/drawing/NativeTrackRecipes.cpp), selected through [world_track_rules.glsl](../data/shaders/vulkan/world_track_rules.glsl), and consumed by [world_track_emit.glsl](../data/shaders/vulkan/world_track_emit.glsl).

Authoring evidence: `obj/vulkan-parity/native-track-recipe-authoring-05/coverage.json`, `native-track-recipes.bin`, and `NativeTrackRecipeData.inc`. These ignored artifacts can be reproduced into a fresh directory; the coverage JSON records every input source hash and rejection. It is not necessary to duplicate its repeated rejections here.

| Input/output | SHA-256 |
|---|---|
| Authoring script | `3cb75f7dbdaaa3f478b4a4090b751e2868f0b96cfb9fe1b60fa32af9a98884d6` |
| Raw recipe bytes | `ae3ddc9cef25cff8a6805c6b40793484ae68f2c2148bfca8288ed6845e7012c6` |
| Compressed recipe bytes | `2b7c7488875d0defc2e8146040bb90883e2b7b1e8f7a446f17dba44b96cbfff1` |
| Coverage JSON | `7f3eab5c8c5b44e765bc4e4e6a7f5834a7dc0903ac7ec5d773cf0833953b20c7` |
| Generated include | `9481cec2ce5eeaef15e03d86aee15b822b11e6f047a7a946cba4b6631c8413e6` |

The compressor version was zlib **1.2.11**. Reproduction requires matching the recorded source inputs; compressed output additionally depends on the recorded generator and compressor version. Earlier authoring02/04 outputs predate the switch-break correction and must not be used as qualified rendering evidence.

```powershell
& "C:/Program Files/Python39/python.exe" scripts/rendering/test-native-track-extractor.py
& "C:/Program Files/Python39/python.exe" scripts/rendering/extract-native-track-recipes.py --output obj/vulkan-parity/native-track-recipe-authoring-new
```

## Translated definitions

Style names and numeric values come from [TrackStyle.h](../src/openrct2/ride/TrackStyle.h); track type IDs refer to [TrackElemType.h](../src/openrct2/ride/ted/TrackElemType.h). Ranges are inclusive. A listed type means its admitted sequence/direction/state rows translated successfully. Empty sequence rows can still be intentional; a listed style is not a claim that every track piece, station, support or ride component renders.

| Style ID | Style | Types | Admitted track type IDs |
|---:|---|---:|---|
| 1 | `airPoweredVerticalCoaster` | 11 | 0, 18–21, 32–33, 99, 114, 125, 214 |
| 3 | `boatHire` | 6 | 16, 38, 42, 133–134, 141 |
| 4 | `bobsleighCoaster` | 42 | 0–4, 6, 9–10, 12, 15–33, 42–45, 87–94, 99, 216 |
| 5 | `carRide` | 17 | 0–15, 113 |
| 6 | `chairlift` | 3 | 0, 4, 10 |
| 11 | `compactInvertedCoaster` | 85 | 0, 5, 7, 11, 14, 16–23, 32–37, 40–49, 52–61, 99, 102–105, 126–131, 133–140, 142–153, 158–171, 216 |
| 12 | `corkscrewRollerCoaster` | 243 | 0, 4–37, 40, 42–49, 52–57, 59–65, 87–94, 99–100, 110–111, 115–116, 118–119, 121–122, 126–131, 133–140, 142–171, 174–181, 183–188, 191, 193–194, 207–208, 216–252, 267–282, 293–336, 341–348 |
| 14 | `dinghySlide` | 13 | 0, 4–15 |
| 15 | `dinghySlideCovered` | 13 | 0, 4–15 |
| 20 | `flyingRollerCoaster` | 255 | 0, 4–37, 40, 42–49, 52–57, 59–65, 87–94, 99–100, 110–111, 115–116, 118–119, 121–122, 126–140, 142–171, 174–188, 191, 206–208, 216–253, 267–336, 341–348 |
| 21 | `flyingRollerCoasterInverted` | 265 | 0, 4–40, 42–49, 52–57, 59–65, 87–94, 99–100, 102–105, 110–111, 115–116, 118–119, 121–122, 126–171, 174–192, 206–208, 216–253, 267–336, 341–348 |
| 23 | `ghostTrain` | 9 | 0–4, 9–10, 12, 99 |
| 24 | `goKarts` | 3 | 0, 50–51 |
| 26 | `heartlineTwisterCoaster` | 17 | 0, 4–15, 197–200 |
| 28 | `invertedHairpinCoaster` | 19 | 0, 5, 7, 11, 14, 42–43, 46–51, 62–65, 99, 216 |
| 29 | `invertedImpulseCoaster` | 21 | 0, 5–9, 11–15, 126–131, 249–252 |
| 30 | `invertedRollerCoaster` | 193 | 0, 5, 7, 11, 14, 16–23, 32–37, 40–49, 52–65, 99–100, 102–107, 109, 119, 121, 126–131, 133–140, 142–171, 174–181, 183–186, 207–208, 216–224, 249–252, 267–282, 293–336, 341–348 |
| 32 | `latticeTriangle` | 222 | 16–37, 40, 42–49, 56–65, 87–94, 99–100, 110–111, 115–116, 119, 122–123, 126–131, 133–140, 142–171, 174–186, 207–208, 216–252, 267–282, 293–336, 341–348 |
| 33 | `latticeTriangleAlt` | 222 | 16–37, 40, 42–49, 56–65, 87–94, 99–100, 110–111, 115–116, 119, 122–123, 126–131, 133–140, 142–171, 174–186, 207–208, 216–252, 267–282, 293–336, 341–348 |
| 35 | `layDownRollerCoasterInverted` | 72 | 0, 5, 7, 11, 14, 16–23, 32–37, 42–49, 99, 102–105, 133–140, 142–153, 158–171, 189–190, 192, 195, 216, 291 |
| 37 | `limLaunchedRollerCoaster` | 186 | 0, 4–37, 40, 42–49, 52–57, 59–65, 87–94, 99–100, 110–111, 115–116, 118–119, 121–122, 126–131, 133–140, 142–171, 174–181, 183–186, 207–208, 216–252, 267–282 |
| 38 | `logFlume` | 11 | 0, 4, 6, 9–15, 172 |
| 39 | `loopingRollerCoaster` | 186 | 0, 4–37, 40, 42–49, 52–57, 59–65, 87–94, 99–100, 110–111, 115–116, 118–119, 121–122, 126–131, 133–140, 142–171, 174–181, 183–186, 207–208, 216–252, 267–282 |
| 43 | `mineRide` | 68 | 0–4, 6, 9–10, 12, 15–33, 42–45, 87–94, 133–140, 142, 144, 147–148, 150, 153, 158–171 |
| 44 | `mineTrainCoaster` | 94 | 0, 4–39, 42–49, 87–94, 99, 114, 118–119, 121–122, 133–140, 142–153, 158–171, 216 |
| 46 | `miniHelicopters` | 10 | 0–4, 6, 9–10, 12, 15 |
| 47 | `miniRollerCoaster` | 126 | 0, 4–37, 42–49, 87–94, 99–100, 110–111, 115–116, 133–140, 142–153, 158–171, 209–210, 216–248 |
| 48 | `miniSuspendedCoaster` | 21 | 0, 4, 6, 9–10, 12, 15–17, 42–43, 133–136, 142, 144, 147–148, 150, 153 |
| 49 | `miniatureRailway` | 6 | 4, 6, 9–10, 12, 15 |
| 50 | `monorail` | 10 | 0–4, 6, 9–10, 12, 15 |
| 51 | `monorailCycles` | 4 | 0–3 |
| 53 | `multiDimensionRollerCoaster` | 90 | 0, 4–33, 42–45, 87–94, 99, 126–131, 133–153, 158–171, 187–188, 216, 253–254 |
| 56 | `reverseFreefallCoaster` | 2 | 0, 114 |
| 57 | `reverserRollerCoaster` | 19 | 0–4, 6, 9–10, 12, 15–17, 38–39, 42–43, 99, 211–212 |
| 58 | `riverRapids` | 2 | 50–51 |
| 61 | `sideFrictionRollerCoaster` | 36 | 0, 4–17, 38–39, 42–43, 99, 133–136, 142–153 |
| 65 | `splashBoats` | 13 | 0, 4–15 |
| 66 | `standUpRollerCoaster` | 226 | 0, 4–37, 40, 42–49, 56–65, 87–94, 99, 110–111, 115–116, 118–119, 121–122, 126–131, 133–140, 142–171, 174–181, 183–186, 207–208, 216–252, 267–282, 293–336 |
| 67 | `steelWildMouse` | 20 | 0, 4–15, 62–65, 99, 216, 256 |
| 68 | `steeplechase` | 26 | 0–4, 6, 9–10, 12, 15–17, 42–43, 99, 133–136, 142, 144, 147–148, 150, 153, 216 |
| 69 | `submarineRide` | 1 | 0 |
| 70 | `suspendedMonorail` | 15 | 0, 16–17, 42–43, 133–136, 142, 144, 147–148, 150, 153 |
| 71 | `suspendedSwingingCoaster` | 39 | 0, 5, 7, 11, 14, 16–17, 34–37, 42–43, 46–49, 99, 106–109, 133–136, 142–153, 216 |
| 76 | `twisterRollerCoaster` | 243 | 0, 4–37, 40, 42–49, 52–57, 59–65, 87–94, 99–100, 110–111, 115–116, 118–119, 121–122, 126–140, 142–171, 174–188, 191, 207–208, 216–252, 267–282, 293–336, 341–348 |
| 77 | `virginiaReel` | 4 | 0–3 |
| 79 | `woodenRollerCoaster` | 18 | 0, 4, 6, 9–10, 12, 15, 99–100, 183–186, 216, 271–274 |
| 80 | `woodenWildMouse` | 20 | 0–15, 62–65 |

## Remaining coverage and correctness limits

**Entire component families omitted:** physical supports, tunnels, station scenery, photo cameras and vehicles. Those omissions also apply to otherwise translated track pieces. Components currently follow their authored emission order; this table does not establish correct general track/terrain/prop occlusion.

**No translated definitions in 34 styles:** `_3DCinema`, `alpineCoaster`, `circus`, `classicStandUpRollerCoaster`, `classicWoodenRollerCoaster`, `classicWoodenTwisterRollerCoaster`, `crookedHouse`, `dodgems`, `enterprise`, `facility`, `ferrisWheel`, `flyingSaucers`, `hauntedHouse`, `hybridCoaster`, `juniorRollerCoaster`, `launchedFreefall`, `lift`, `magicCarpet`, `maze`, `merryGoRound`, `miniGolf`, `motionSimulator`, `multiDimensionRollerCoasterInverted`, `observationTower`, `rotoDrop`, `shop`, `singleRailRollerCoaster`, `spaceRings`, `spiralSlide`, `swingingInverterShip`, `swingingShip`, `topSpin`, `twist`, `waterCoaster`.

Major unresolved source constructs include:

- Original/classic asset availability (`IsCsgLoaded`) gates: the three classic style dispatchers each reject all 350 possible type IDs. Asset availability must become an explicit shared catalog fact rather than an assumed constant.
- Namespaced Alpine, Hybrid and SingleRail dispatchers, Junior/Water template dispatchers, and other template getters. These are parser/admission gaps, not evidence that the original art is absent.
- Cable-lift state: 15 types in each Lattice style, including flat track, reject `hasCableLift()`. Both styles have 222 translated types but cannot yet render an ordinary complete layout.
- Wooden colour helper semantics: `woodenRollerCoaster` has only 18 translated types, including flat; 76 candidate types reject `WoodenRCGetTrackColour<false>`. Classic wooden styles currently have none.
- Position-dependent support predicates, diagonal drawing helpers, station-specific track-type conditions and height-adjusting draw helpers remain unsupported. Unknown graphical calls are rejected rather than silently discarded.

The 21,090 `TrackPaintFunctionDummy` rejections largely describe nonexistent style/type combinations. They must not be interpreted as 21,090 missing real track pieces. Other rejection counts likewise count style/type pairs, not unique functions.

## Projection envelope

Across authoring05 components, sprite offsets are **X/Y −16…22** and **Z −16…34** relative to the tile/base height. Bounds origins are **X/Y −20…39**, **Z −16…227**; bounds extents are **X/Y 0…60**, **Z 0…160**. The minimum Z offset differs from authoring02 (−8). Culling must combine actual sprite metadata with these authored offsets; painter bounds alone do not describe the raster footprint.

The bounded first independent original-art fixture uses Looping flat and chain track. Its outcome must be reported separately from these translation counts. A passing narrow fixture would not qualify the other styles or establish whole-park rendering parity.
