# Native GPU track definition coverage

This records **authoring09**, the expansion after deployed build106. **22 focused Python authoring regressions pass**. Build110 compiled successfully and all46 focused CPU tests passed, including all nine track-rule/catalog tests. Build110 original-art review against corrected specials corpus02 is recorded below; the eight whole-frame comparisons still fail. Build107 compiled the preceding authoring06 code but subsequently hit DEVICE_LOST during runtime qualification; no raster pass is claimed for that batch or this expansion. Counts describe translated source definitions, not complete rides or visual parity. Prior build102 passed six structural/catalog tests. Build103 rendered eight original-art views but retained track/tree overlap differences; those results do not qualify the new station code.

## Scope and provenance

- **56 of 81 styles, 4,916 style/type pairs** (authoring05: 47 / 3,227).
- **653,670 words, 157,160 rows, 21,191 deduplicated components, 18,555 original image IDs**. Station platforms and loaded station shelters are additional shared assets.
- Raw table 2,614,680 bytes; zlib 162,201 bytes, compressor 1.2.11. Decompression and validation happen once; runtime selection remains GPU work.
- Selection includes type, direction, sequence, chain, inversion, brake closure, cable lift, green light, CSG availability and station-platform availability. Dependencies are evaluated and packed during authoring.
- Station markers expand on GPU into bases/platforms/fences and loaded shelters/glass. Raw entrance/exit positions remove fence segments. No-platform styles suppress the group. Looping (39), Lattice (32) and Wooden (79) admit all three station types.
- Each row must fit 16 expanded components and 12 parents, checked during authoring and runtime loading. Current data maxima are 4 raw components, 11 conservatively expanded components and 9 parents. Generic component anchor offsets span XY [-32, 32], Z [-16, 34]. Owner-local parent/child sorting does not establish cross-object correctness.
- Atlas residency follows styles referenced by present rides, once per catalog generation through immutable RTD variants. CSG image residency follows loaded CSG state. Required failures propagate; no per-instance CPU painter/draw stream is used.

Producer: [extract-native-track-recipes.py](../scripts/rendering/extract-native-track-recipes.py). Its [tests](../scripts/rendering/test-native-track-extractor.py) cover control flow, original Heartline geometry, station marker parameters/platform branches, new state bits, colour roles, independent quarter-helix numeric geometry and all reverse mappings, Junior/Water chain and brake distinctions, and array pointer offsets. Runtime: [NativeTrackRecipes.cpp](../src/openrct2/drawing/NativeTrackRecipes.cpp), [GpuWorldTrackCatalog.h](../src/openrct2-renderer/gpu/GpuWorldTrackCatalog.h), [world_track_rules.glsl](../data/shaders/vulkan/world_track_rules.glsl), [world_track_emit.glsl](../data/shaders/vulkan/world_track_emit.glsl), [world_track_station.glsl](../data/shaders/vulkan/world_track_station.glsl).

Failed authoring07 log is preserved; it stopped before outputs on array-pointer arithmetic. Corrected08 uses a bounded source-array suffix and a regression for that case.

Evidence: `obj/vulkan-parity/native-track-recipe-authoring-09/{coverage.json,native-track-recipes.bin,NativeTrackRecipeData.inc}`. Coverage pins every input and individual rejection. Earlier 02/04 definitions predate the switch-break correction and are not qualified rendering evidence.

| Input/output | SHA-256 |
|---|---|
| Authoring script | `21a32f377157662c39665bcda5785d674f9944e3e9180e3d3e6e16e7936ae3ba` |
| Raw recipes | `b5e7f9af43e8be371c55c3c86b753430d110d7565ef96a790edee96163861eca` |
| Compressed recipes | `2692e237dcd2e3db404a5481aa32317915ae8c92df1374abaa8d772a160b3575` |
| coverage.json | `df50e2765908366e2a7e499238469b927dd48bf988467017802b5cbe23dc3c73` |
| NativeTrackRecipeData.inc | `899617452cff4b3ed02a22d00c38d98101308c3a4ca0e93b9c270a52f2d9e098` |

```powershell
& "C:/Program Files/Python39/python.exe" scripts/rendering/test-native-track-extractor.py
& "C:/Program Files/Python39/python.exe" scripts/rendering/extract-native-track-recipes.py --output obj/vulkan-parity/native-track-recipe-authoring-new
```

## Translated definitions

Type IDs refer to [TrackElemType.h](../src/openrct2/ride/ted/TrackElemType.h); ranges are inclusive. Empty rows can be intentional. Admission does not mean supports, tunnels, vehicles or all surrounding geometry render.

| Style ID | Style | Types | Admitted track type IDs |
|---:|---|---:|---|
| 1 | `airPoweredVerticalCoaster` | 14 | 0-3, 18-21, 32-33, 99, 114, 125, 214 |
| 2 | `alpineCoaster` | 92 | 0-4, 6, 9-10, 12, 15-39, 42-49, 87-94, 99, 102-109, 133-142, 144, 147-148, 150, 153, 158-171, 178-181 |
| 3 | `boatHire` | 13 | 1-3, 16-17, 38, 42-43, 133-136, 141 |
| 4 | `bobsleighCoaster` | 45 | 0-4, 6, 9-10, 12, 15-33, 38-39, 42-45, 87-94, 99, 114, 216 |
| 5 | `carRide` | 19 | 0-15, 42-43, 113 |
| 6 | `chairlift` | 3 | 0, 4, 10 |
| 8 | `classicStandUpRollerCoaster` | 228 | 0-43, 46-49, 56-65, 87-99, 102-111, 114-116, 118-119, 121-122, 126-131, 133-136, 141-157, 174-181, 183-186, 207-208, 216-252, 267-282, 293-338 |
| 9 | `classicWoodenRollerCoaster` | 102 | 0-15, 34-43, 46-49, 95-100, 110-111, 115-116, 118-119, 121-122, 133-136, 141-153, 183-186, 216-248, 271-274 |
| 10 | `classicWoodenTwisterRollerCoaster` | 102 | 0-15, 34-43, 46-49, 95-100, 110-111, 115-116, 118-119, 121-122, 133-136, 141-153, 183-186, 216-248, 271-274 |
| 11 | `compactInvertedCoaster` | 95 | 0-3, 5, 7, 11, 14, 16-23, 32-49, 52-61, 95-99, 102-105, 114, 126-131, 133-140, 142-153, 158-171, 216 |
| 12 | `corkscrewRollerCoaster` | 266 | 0-49, 52-65, 87-100, 102-111, 114-116, 118-119, 121-122, 126-131, 133-171, 174-181, 183-188, 191, 193-194, 207-208, 216-252, 267-282, 293-338, 341-348 |
| 14 | `dinghySlide` | 18 | 0-15, 38-39 |
| 15 | `dinghySlideCovered` | 15 | 0, 4-15, 38-39 |
| 20 | `flyingRollerCoaster` | 278 | 0-49, 52-65, 87-100, 102-111, 114-116, 118-119, 121-122, 126-171, 174-188, 191, 206-208, 216-253, 267-338, 341-348 |
| 21 | `flyingRollerCoasterInverted` | 281 | 0-49, 52-65, 87-100, 102-111, 114-116, 118-119, 121-122, 126-171, 174-192, 206-208, 216-253, 267-338, 341-348 |
| 23 | `ghostTrain` | 11 | 0-4, 9-10, 12, 42-43, 99 |
| 24 | `goKarts` | 3 | 0, 50-51 |
| 26 | `heartlineTwisterCoaster` | 20 | 0-15, 197-200 |
| 27 | `hybridCoaster` | 222 | 0-4, 6-10, 12-39, 42-49, 87-100, 102-111, 114-116, 118-119, 121-122, 126-131, 133-153, 158-171, 174-182, 207-208, 216-252, 275-282, 293-339, 349 |
| 28 | `invertedHairpinCoaster` | 22 | 0-3, 5, 7, 11, 14, 42-43, 46-51, 62-65, 99, 216 |
| 29 | `invertedImpulseCoaster` | 24 | 0-3, 5-9, 11-15, 126-131, 249-252 |
| 30 | `invertedRollerCoaster` | 204 | 0-3, 5, 7, 11, 14, 16-23, 32-49, 52-65, 95-100, 102-109, 114, 119, 121, 126-131, 133-140, 142-171, 174-181, 183-186, 207-208, 216-224, 249-252, 267-282, 293-336, 341-348 |
| 31 | `juniorRollerCoaster` | 93 | 0-39, 42-45, 62-65, 87-94, 99-100, 114, 141-171, 216, 337-338 |
| 32 | `latticeTriangle` | 260 | 0-49, 56-65, 87-100, 102-111, 114-116, 118-119, 121-123, 126-131, 133-171, 174-186, 207-208, 216-252, 267-282, 293-338, 340-348 |
| 33 | `latticeTriangleAlt` | 257 | 0, 4-49, 56-65, 87-100, 102-111, 114-116, 118-119, 121-123, 126-131, 133-171, 174-186, 207-208, 216-252, 267-282, 293-338, 340-348 |
| 35 | `layDownRollerCoasterInverted` | 85 | 0-3, 5, 7, 11, 14, 16-23, 32-39, 42-49, 95-99, 102-105, 114, 133-153, 158-171, 189-190, 192, 195, 216, 291, 337-338 |
| 37 | `limLaunchedRollerCoaster` | 207 | 0-49, 52-65, 87-100, 102-111, 114-116, 118-119, 121-122, 126-131, 133-171, 174-181, 183-186, 207-208, 216-252, 267-282 |
| 38 | `logFlume` | 17 | 0-4, 6, 9-15, 38-39, 114, 172 |
| 39 | `loopingRollerCoaster` | 209 | 0-49, 52-65, 87-100, 102-111, 114-116, 118-119, 121-122, 126-131, 133-171, 174-181, 183-186, 207-208, 216-252, 267-282, 337-338 |
| 43 | `mineRide` | 79 | 0-4, 6, 9-10, 12, 15-33, 38-39, 42-45, 87-94, 102-109, 133-142, 144, 147-148, 150, 153, 158-171 |
| 44 | `mineTrainCoaster` | 100 | 0-39, 42-49, 87-94, 99, 114, 118-119, 121-122, 133-153, 158-171, 216, 337-338 |
| 45 | `miniGolf` | 6 | 4, 6, 9-10, 12, 15 |
| 46 | `miniHelicopters` | 10 | 0-4, 6, 9-10, 12, 15 |
| 47 | `miniRollerCoaster` | 145 | 0-39, 42-49, 87-100, 102-111, 114-116, 133-153, 158-171, 209-210, 216-248 |
| 48 | `miniSuspendedCoaster` | 27 | 0-4, 6, 9-10, 12, 15-17, 38-39, 42-43, 133-136, 141-142, 144, 147-148, 150, 153 |
| 49 | `miniatureRailway` | 8 | 4, 6, 9-10, 12, 15, 135-136 |
| 50 | `monorail` | 21 | 0-4, 6, 9-10, 12, 15-17, 42-43, 141-142, 144, 147-148, 150, 153 |
| 51 | `monorailCycles` | 8 | 0-3, 16-17, 38-39 |
| 53 | `multiDimensionRollerCoaster` | 103 | 0, 4-33, 38-39, 42-45, 87-94, 99, 102-109, 114, 126-131, 133-153, 158-171, 187-188, 216, 253-254, 337-338 |
| 56 | `reverseFreefallCoaster` | 5 | 0-3, 114 |
| 57 | `reverserRollerCoaster` | 19 | 0-4, 6, 9-10, 12, 15-17, 38-39, 42-43, 99, 211-212 |
| 58 | `riverRapids` | 2 | 50-51 |
| 61 | `sideFrictionRollerCoaster` | 39 | 0-17, 38-39, 42-43, 99, 133-136, 142-153 |
| 62 | `singleRailRollerCoaster` | 256 | 0-39, 42-49, 56-65, 87-99, 102-111, 114-116, 118-119, 121-122, 126-131, 133-171, 174-181, 183-186, 207-208, 216-252, 267-282, 293-339, 341-349 |
| 65 | `splashBoats` | 19 | 0-15, 38-39, 114 |
| 66 | `standUpRollerCoaster` | 248 | 0-49, 56-65, 87-99, 102-111, 114-116, 118-119, 121-122, 126-131, 133-171, 174-181, 183-186, 207-208, 216-252, 267-282, 293-338 |
| 67 | `steelWildMouse` | 22 | 0, 4-15, 50-51, 62-65, 99, 216, 256 |
| 68 | `steeplechase` | 35 | 0-4, 6, 9-10, 12, 15-17, 38-39, 42-43, 99, 106-109, 133-136, 141-142, 144, 147-148, 150, 153, 216, 337-338 |
| 69 | `submarineRide` | 4 | 0-3 |
| 70 | `suspendedMonorail` | 21 | 0-3, 16-17, 38-39, 42-43, 133-136, 141-142, 144, 147-148, 150, 153 |
| 71 | `suspendedSwingingCoaster` | 45 | 0-3, 5, 7, 11, 14, 16-17, 34-39, 42-43, 46-49, 99, 106-109, 133-136, 141-153, 216 |
| 76 | `twisterRollerCoaster` | 266 | 0-49, 52-65, 87-100, 102-111, 114-116, 118-119, 121-122, 126-171, 174-188, 191, 207-208, 216-252, 267-282, 293-338, 341-348 |
| 77 | `virginiaReel` | 4 | 0-3 |
| 78 | `waterCoaster` | 93 | 0-39, 42-45, 62-65, 87-94, 99-100, 114, 141-171, 216, 337-338 |
| 79 | `woodenRollerCoaster` | 104 | 0-15, 34-43, 46-49, 95-100, 110-111, 115-116, 118-119, 121-122, 133-136, 141-153, 170-171, 183-186, 216-248, 271-274 |
| 80 | `woodenWildMouse` | 22 | 0-15, 50-51, 62-65 |

## Remaining limits

Supports, tunnels, vehicles and photo cameras remain omitted. Narrow/pier station recipes now cover AirPowered, BoatHire, Hybrid, ReverseFreefall, SplashBoats and Submarine stations. Junior and Water Coaster each admit 93 types, preserving distinct chain and station-brake artwork. Source-backed quarter-helix maps and TED reversal admit 128 additional combinations. Other template families remain incomplete; the unsupported MultiDimensionInverted function-table getter is a separate issue. Other rejected definitions include complex helper structures and map-dependent graphical branches. Unsupported recipes emit nothing. Flat rides, towers and maze geometry use a separate native module and are not counted here.

Original-art qualification is still required for the expansion. The bounded owner-local sorter does not establish cross-tile or tree/track ordering. Modified raw track ride types that disagree with all present ride types need an authoritative residency policy. CSG availability is catalog state and must remain stable for its lifetime.

The post107 shader candidate removes aggregate station-array `inout` calls. One private array per invocation is reset for each track visit; station helpers mutate that owner directly. This targets excessive compiler-generated array copies. Build108 subsequently completed both eight-view corpora with clean validation and no driver reset. That result does not prove the cause of the107 device loss or qualify all rendering workloads.

## Build108 regular-station visual review

Manually inspected all24 specimens23/24/25 (Looping39, Lattice32, Wooden79; rotations0-3, zoom0/1) in `obj/vulkan-parity/building108-specimens-01`. The root-run eight-view capture completed with clean validation and no driver reset. Station rails, bases, platforms, fences, colours and portal overlap appear coherent against the external upstream images; no station geometry correction was justified by these specimens.

All eight Looping/Lattice zoom1 specimen rectangles compare exactly (zero indexed differences), as does Wooden rotation1/zoom1. Zoom0 differences lie on entrance name lettering; Wooden rectangles additionally include outer-map background differences in rotations0/2/3. Whole rectangles and whole frames therefore remain failing comparisons where those differences occur. This is not a waiver or a complete track parity claim. The specimens do not qualify sheltered, inverted, narrow or pier stations, nor changing lights/ghost state.

Specimen summary SHA-256: `957217ae1e66186bce983aca767a023d7e40ef6e94ba1082303e36d8b05e766a`.


## Build108 specials review and authoring09 correction

The eight-view external corpus covers 42 specimens: eight Looping quarter-helixes, six narrow/pier station families, and Junior/Water flat, chain, slopes, transitions, quarter-curves, S-bends, brakes, boosters and stations. Manual representatives across rotations0-3 and zoom0/1 in `track-specials108-specimens-01` separated original rail geometry from omitted support columns, entrance text and off-map background. Reviewed helix rails and narrow/pier platform layouts remain visually coherent; this does not establish exact pixel parity or changing station-state correctness.

The review found real missing rails: Junior/Water S-bends38/39 and transitions6/9. They are not excused as supports. Authoring09 fixes Python negative-index wraparound, which falsely extended the source S-bend sequence domain beyond its four tiles, and accepts narrowly validated support-only local declarations plus source-pinned tunnel enum bookkeeping. The GPU still selects immutable original-art rows. Added regressions check all four S-bend sequences/directions, reject invalid sequence4, preserve subtype/chain differences, and forbid the auxiliary-block classifier from hiding graphics or outer-state writes. All nine C++ rule tests passed in the root-run build110 focused suite.

Authoring09 adds28 style/type combinations and removes none from08. Besides Junior/Water transitions and S-bends, the same index correction restores S-bends in styles14/15/38/65. The include is compiled in build110 (build109 stopped on an unrelated GLSL reserved identifier); its original-art review is recorded below. Existing108 failing evidence is retained without masking or tolerances. Physical supports, tunnels and entrance scrolling text remain unimplemented by this track module.

The old fixture reused incompletely reset Ride storage. Root corrected its normal ride allocation and captured fresh upstream corpus02: the old images can diagnose missing immutable rail recipes but do not qualify all station flags, fences or lights. Root also corrected the test's nontransparent clear colour from index0 to upstream index10; old off-map differences remain in the saved evidence. Special-specimen summary SHA-256: `7718aacccfc624d219555d71d2352eb779851908eac972f646ef1ff400c1333b`.


## Corrected reference and build110 checks

`track-specials-upstream-corpus-02` is the current external reference:42 specimens, eight3840x2160 views (four rotations, zoom0/1), no simulation ticks. The producer uses normal ride allocation so vehicle IDs, flags and station state start initialized. Its `manifest.json` SHA-256 is `36db0a98d2cfea440f690ce532ecdecab295de69a508cd8f1c2c0d8e04b61530`; exported `objects.park` SHA-256 is `51aa00cf21e024c012518b61c8160044f2289ee12f7fe311b68b7c9306257e63`. Corpus01/108 differences remain historical diagnostics rather than the final state reference.

`building110-cpu-01/summary.json` reports46/46 passing focused tests, zero failures/errors, and unchanged binaries. This includes the nine track tests plus the other native module rules; it is CPU evidence, not raster or endurance acceptance. Summary SHA-256: `a1d535db8fef6f27f935e29da514380c45eec94337abc33b6853c9f269edbb22`. Native images against corpus02 were assessed separately below, without treating omitted supports or backgrounds as exact matches.


## Build110 complete specials visual review

Reviewed every one of the112 specimen pages:42 specimens across all four rotations and zoom0/1 (336 comparisons) in `track-specials110-specimens-01`, against corrected upstream corpus02. For coverage, all pages were arranged into14 contact sheets under `manual-overview`; native-size follow-ups checked Junior/Water left/right S-bends, both transition directions, a pier and a narrow station. This is manual visual evidence, not an exhaustive classification of individual changed pixels.

| Specimens | Observed result in all eight views |
|---|---|
| 0-7: Looping quarter-helixes102-109 | Rail segments join coherently; banking, colour and orientation match visually. No missing helix section found. |
| 8-13: Hybrid, AirPowered, ReverseFreefall, SplashBoats, BoatHire, Submarine stations | Visible rails, platforms, fences and portal overlap match visually. Physical metal/wood supports remain absent. |
| 14-27: Junior | Previously absent S-bends23/24 and transitions19/20 now render complete original rail shapes. Flat/chain, slopes, quarter-curves, brakes, booster and station remain visually coherent. |
| 28-41: Water Coaster | Previously absent S-bends37/38 and transitions33/34 now render complete original rail shapes. Other sampled pieces remain visually coherent. |

No additional track image-selection, component-offset, remap-colour or ordering defect was identified in these pages. The visible residuals are omitted support columns/scaffolding, entrance scrolling-name glyphs, and the outer terrain boundary/skirt. These remain real whole-image differences. The old large background-clear mismatch is gone; a thin edge discrepancy remains. Supports can meet the underside of a rail, so their residuals are not being removed with a broad rail-region tolerance.

Strict whole-frame indexed differences remain rotation0/1/2/3: **244223 /245192 /244985 /244589 at zoom0**, and **63006 /63293 /63156 /63080 at zoom1**. No mask, ignored-pixel region or tolerance was added. These isolated static elevated specimens do not qualify arbitrary cross-track intersections, all station object styles, dynamic station lights or moving vehicles.

Evidence hashes: `track-specials110-specimens-01/summary.json` SHA-256 `6acfde3749f690be9b49bc828f130e0f5c46320e01529f815ce9c4153289d15e`; `track-specials110-compare-01/summary.json` SHA-256 `9c6dc04dcf41ccf9b3d505327474d15f39d259b8f529df34749fa6867ee30773`. No source changes followed this review.

## Build112 final park visual regression check

Manually compared the full 3840x2160 final captures from `performance-buildings112-12000-01` and `performance-buildings110-12000-01`, then four side-by-side crops at native pixel scale: static rides, western stations, eastern stations and upper tall curves. Supported station rails/platforms, static bodies, track curves and their placement remain visually unchanged. No new clipping, projection or remap-colour defect was identified. Crop evidence and bounds are saved under `obj/vulkan-parity/building112-park-review`.

Both PNGs store **exactly the same 8,294,400 palette indices** (direct byte comparison, zero differing indices). Their indexed SHA-256 is `64bccacfb3afe1df188801626a1d99bb9ecb79341dbd3bc4067c5ee7f383da51`. Only palette entries 243/244 differ; these explain all 895 differing decoded RGBA pixels. This is exact preservation of the captured indexed picture, not an exact RGBA match or an upstream-renderer parity claim.

The receipts agree on rotation 3, zoom 2, view position `[458,-4370]`, simulation tick 3145931, publication source tick 3145930, entity checksum `07d58eaefde6aa6d000000000000000000000000`, workload inputs and the complete recorded shader hash map. Capture occurs after measurement and leaves authoritative state unchanged. Build112 PNG SHA-256: `d6e4ee7ba3e42cf166b5395ea9bbca795029075169ac4f2f87cbdea4af7248bd`; build110 PNG SHA-256: `6f718a1b489176d131c017ce6c95ba052edd49b4d76ccc6af3ff7f108eb9534f`.

This checks one final camera after 12000 ticks across the CPU station-publication change. It does not qualify other cameras, motion, display pacing or omitted world categories. Existing support/tunnel/vehicle omissions and previously recorded original-art differences remain open.
