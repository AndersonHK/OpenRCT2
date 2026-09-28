# Large track-design ghost performance

Status: build204 implements and validates the bounded updates. Clean performance qualification and deployment remain pending an uncontended GPU window; this is not a claim that moving-ghost360TPS/144FPS is achieved.

## Reproduction and acceptance

The owner reported severe cursor-movement lag while selecting a large pre-built design in Everything Park. The attached example uses **Over the Top**. A stationary benchmark does not exercise this case: moving the placement tool repeatedly removes the previous ghost, queries the next location, installs the next ghost and publishes the resulting graphical state.

Acceptance requires a hidden, silent run with an actual valid ghost visible in the final screenshot, repeated placement movement, and one GPU workload at a time. The ordinary regression gate remains a 3840 x 2160 run with at least 3,000 measured ticks. Record separate tool-event cost, total TPS, accepted presentation rate and frame intervals; a drawing-only CPU number can omit work performed immediately before drawing.

## Root causes

1. **World-art admission depended on mutable ride-instance facts.** `CaptureWorldRideMaterials` includes ride station positions, colors and other per-instance facts. A ghost move changes these facts. `CommandDrawingContext` previously compared that snapshot pointer with `WorldSurfaceSpriteTable::sourceRideMaterials`, and any change rebuilt the entire world sprite table. This re-resolved terrain, paths, props, shared track artwork and entity artwork even when every required image was already resident. The resulting sprite revision also forced those tables to be uploaded again.
2. **A placement query demolished an empty temporary ride as though it were an established ride.** `TrackDesignAction::Query` creates a scratch ride to validate a design. It then invoked `RideDemolishAction`, whose normal cleanup scans the whole map, guest population, banners and park value. The placement tool can repeat that query for several candidate heights. None of those established-ride operations are needed for an empty query-owned slot.

`MapInvalidateTileFull` is not a whole-world renderer reset in this path. Presentation mutations enter a deduplicated tile worklist. Ghost status is established before topology notification, so ordinary ghost tracks do not invalidate routing topology as real track changes do.

## Implemented ownership changes

### Resident artwork

`WorldRideArtCoverage` describes admitted shared track types, flat-ride object/style pairs and station styles. Ride IDs, XYZ, colors and station counts are not artwork identities. A newly used ride slot or a removed ghost can reuse the same admitted artwork. Non-ride scenery usage is checked separately; disappearing instances do not evict their art.

A genuinely unadmitted dependency still enters the existing cold admission path. This change does not yet replace every cold rebuild trigger with a global append-only asset registry.

### Ride-instance publication

Each world scene owns an immutable `WorldRideCatalogGeneration`, separate from `WorldSurfaceSpriteTable`. It contains the current track, flat-ride and entrance instance metadata, including station coordinates and colors. Warm builders retain the admitted sprite addresses and reject any attempt to call image admission. Old queued frames continue to own their original metadata.

Vulkan tracks this publication independently from the sprite-table revision. A ride-instance update uploads the small track header and ride tail plus flat-ride/entrance metadata, without re-uploading terrain sprite sets or rebuilding peep/vehicle residency. The immutable track recipe, support and image-address banks remain resident; placing the variable ride table last lets its count change without relocating those banks. Cold and warm transfer regions are disjoint, and every new packet is validated against its owning artwork layout before recording transfers. The publication records its owning artwork revision; a mismatched packet fails before world-buffer uploads.

### Query-owned ride lifecycle

Scratch ride creation preserves normal query validation, but executes its initialization as an internal operation rather than advertising a temporary ride through plugin action-execute hooks. An RAII owner calls `Ride::remove()` on every query exit. Normal placement and its failure cleanup retain the full game-action behavior.

The native query branches cannot install tracks, scenery or vehicles: they dispatch `QueryNested`; the footpath connection pass returns immediately for query mode; queue chaining runs only for placement/track-preview modes. `Ride::remove()` clears the slot, station pre-queue state, transport-service bookkeeping, name/measurement ownership and used-range bookkeeping.

## Regression coverage

- [x] Unit coverage: moving/recoloring a resident ride performs zero image-admission callbacks.
- [x] Unit coverage: station-count changes, new ride slots, cancellation and reuse retain valid resident image addresses.
- [x] Unit coverage: warm track, flat-ride and entrance payloads equal freshly constructed payloads for equivalent current state.
- [x] Unit coverage: new unadmitted track/object/station dependencies are refused by the warm-art coverage check.
- [x] Unit coverage: held metadata remains unchanged after publication of a new generation.
- [x] GPU test added: warm track recoloring, ride-count growth, cancellation and restoration render the expected pixels while transferring only bounded instance ranges.
- [x] GPU test added: mixed artwork/instance generations are rejected before uploads; a subsequent matching packet renders the accepted image.
- [x] Query test added: repeated successful and failed scratch queries release their slot and preserve map elements, entities and park value.
- [x] Build204 has zero warnings/errors and unchanged source inputs; all72 selected checks passed across the two recorded test invocations.
- [x] Valid moving ghost visually verified in Everything Park on203 and204.
- [x] Build204 completed1777 measured cursor moves with exactly one cold catalog admission in the complete process.
- [ ] Ordinary 4K benchmark, at least 3,000 ticks, retains expected performance and frame pacing.
- [ ] Checkpoint/deployment receipt recorded.

Test and visual evidence are recorded below; benchmark acceptance remains separate.

## Remaining work and measurement limits

Track warm publication now copies and uploads exactly 64 header bytes plus 32 bytes per ride slot, independent of track recipe-bank size. Flat-ride and entrance metadata still copy their retained image-address maps; those smaller families are not yet split into separate immutable regions.

Other existing full-admission triggers include object/material generation changes, missing atlas leases and peep/vehicle artwork membership changes. In particular, membership removal can still rebuild those wider tables. They are not exercised merely by moving a track-design ghost and remain a broader asset-lifetime cleanup item.

The first diagnostic version applied its synthetic tool event before ordinary UI input, which could remove the ghost before rendering. Its timing alone is not valid renderer qualification. The corrected diagnostic applies movement after ordinary input and before snapshot capture. Build203 and later reset tool counters at the measurement boundary; tool time is outside the drawing-CPU timer. Compare the separately reported tool-event time with total TPS/frame pacing, and verify the screenshot contains the ghost.

## Qualification results

Build203 passed all68 focused tests. Its hidden3840x2160,3000-tick workload performed1250 cursor movements with confirmed ghost placement and an actual white Over the Top ghost visible in the final capture. Logs showed exactly one cold catalog admission. Its long run is **not a clean performance result**: another user application was consuming the GPU, confirmed after our processes exited. The compact track-instance change was subsequently validated in build204 below; the uncontended performance run remains pending. Do not copy stationary build199 numbers into this section as evidence for moving-ghost performance.


### Build204 evidence and remaining gates

- Build receipt: `obj/vulkan-parity/build-204/receipt.json`; archived runtime: `performance-entities-build204/snapshot.json`.
- `native204-ghost-air-01/tests.log` records four successful GPU cases: empty-catalog recovery, mixed-generation rejection, real track recolouring/slot growth/cancellation/restoration, and ground rail/support contact. The240-second harness timeout occurred while compiling the fifth shader fixture, with no assertion/validation failure before it. `native204-ghost-air-remaining/summary.json` then passed the remaining68 tests, including that fifth fixture. In total all72 selected checks passed; the original invocation itself remains recorded as a timeout, not a passing run.
- All82 extractor tests pass again on the final source.
- `ghost204-3000-validation-01/summary.json`: hidden/silent3840x2160,VSync,100warmup+3000measured ticks;1777synthetic cursor moves, successful placement samples, and final ghost manually inspected. Exactly one full catalog admission. No runtime diagnostics or changed immutable inputs. The final white ghost, normal world and UI are present.
- This run reports120.100TPS,71.139accepted presents/s,4.798ms drawing wall time,4.341ms mean tool update and3.777ms GPU time. These are **construction-stress diagnostics under concurrent user-application load**, not clean performance acceptance or an A/B speedup. Tool updates occur every rendered frame and rebuild actual provisional map pieces; their4.3ms residual is still material. No full-art rebuild or immutable-track-bank copy remains in that warm path.
- Build203's four-rotation Air comparisons qualify the unchanged shader/recipe art in204; real GPU tests additionally validate204's changed catalog layout. See `vulkan-air-powered-track-regression.md` for the small remaining contact patch and conditional-floor raster coverage gap.
- Before deployment: repeat ordinary4K/12000ticks against combined-source199 and moving-ghost4K/3000ticks with the other GPU workload absent. Maintain the360TPS gameplay cap. Build199 remains installed until this performance gate is checked.

The broader existing Lift/depth backlog is unchanged. This checkpoint does not waive those regressions or claim that every asset-catalog rebuild has been eliminated.
