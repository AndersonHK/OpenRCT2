# OpenRCT2 overhaul changelog

## 2026-07-10

### Upstream develop integration and directed-leg UI cleanup

Merge: manually integrate upstream `develop` through `a770ffc04e`. The new editor-scene ownership, ride colour/operations
layout, lowercase `TileElementType` names, sloped-path puke/litter correction, plugin save binding, networking fixes, graphics
round-trip work, translations, workflow maintenance, and documentation are retained. Fork routing, sampled ratings, private
save versions, transport platforms, spatial audio, route caches, and Vulkan sources remain at their intended owners. The
commit-by-commit decisions and seven conflict resolutions are recorded in the [upstream merge manifest](upstream-merge-manifest.md).

Interface: multi-station measurement pages no longer display the conservative ride-wide E/I/N envelope. That aggregate remains
an internal ride-list/value compatibility result only. The selected directed leg now uses the normal measurement convention:
white labels with black values on separate excitement, intensity, nausea, distance, duration, maximum/average speed, every
applicable G-force extreme, transport comfort, decoration, and fare row. This replaces the three compressed black-text lines
and makes the selected station pair visually consistent with ride-global construction/test facts.

Localisation: retain upstream English IDs `7033..7038` and move the fork-owned block to the contiguous `7039..7065` range.
Removed compressed/compatibility-summary strings are not carried forward as dead aliases.

Verification: normal Release source and fully enabled Vulkan/direct Release solution builds pass without compiler or linker
warnings. Focused merge/rating/routing/formatting coverage passes 204/204 tests and the full suite passes 452/452. Two warmed
EverythingPark runs reach 261.961 and 263.398 TPS with matching `72638ee2...` checksums and 3.699/3.692 millisecond medians.

### Transport rides as route services

Decision: guests now use transport rides as planned station-to-station journeys toward a concrete ride, shop, facility, or park-exit goal. A railway, monorail, chairlift, or lift is no longer selected opportunistically as an ordinary attraction, and the legacy rule that made free transports automatically acceptable is removed.

Routing: compare direct walking with walking to a station, expected queue/boarding time, every onboard segment through the selected alighting station, and the remaining walk, with every term expressed in milliseconds. Walking time uses a three-mph baseline adjusted by guest energy and slow-walk state; segment time uses measured seconds or a distance/speed fallback. Free, Discount, and Fair services have progressively stricter time thresholds. Precipitation triggers a fresh comparison, accepts any positive saving for paid non-extortive service, and favours sheltered onboard time. The direct walking direction and reachability result are reused when transport loses, avoiding a duplicate bounded search.

Performance: a transient ride-service cache validates once per tick, computes quality once per changed transport, precomputes every directed forward journey in quadratic station count, and exposes constant-time station-pair lookup. Queue delay, platform crowding, guest cash and vouchers remain live overlays rather than invalidating shared geometry and timing.

Integration: park exits and resolved entrances for attractions, advertised rides, first aid, toilets, cash machines, shops, and other facilities now converge on one destination-routing helper. Outside-park entry and spawn travel use the same helper without considering in-park transport.

Economy: transport value is led by segment distance, then multiplied by speed, comfort, and decoration. Comfort starts from a per-tick baseline and is reduced by vertical deviation, lateral G, and realised longitudinal G; comfort and decoration are accumulated proportional to distance. Journey-specific fares use four operator policies: Free at zero, Discount at half value, Fair at full value, and Extortive at twice value. The planner and entrance use the same journey calculator. Extortive service is considered only when walking is unreachable and no non-extortive service is usable; paying it reduces happiness and creates a dedicated thought.

Guest behavior: transport vehicles retain through-passengers across intermediate stations and unload them only at the selected destination. Completing a journey does not increment ordinary ride count or history and does not change favourite selection, satisfaction, or nausea. Planned transport remains available to leaving guests.

Capacity: Miniature Railway, Monorail, and Suspended Monorail stations now expose a real second-stage platform queue. When a
train physically clears the station, the next cohort leaves the external queue and walks to deterministic positions aligned
with that stopped consist's cars and seats. Capacity is the actual linked-consist seat total, not a station-tile estimate;
through-riders compact first, and staged guests bind FIFO only to real empty seats after arrival. Fare eligibility is rechecked
and payment is committed at that binding point. Closing or invalid stations recover staged guests through the exit, then the
entrance/requeue path, with falling reserved for missing geometry. Chairlift now stages against its native two-seat scalar
loading positions and physical station-clear transition. Lift remains just-in-time because its waypoint cabin reaches generic
departure completion only at the tower top; no-platform styles, other waypoint-loading vehicles, and coasters also remain
outside the adapter until their geometry and lifecycle provide an exact boundary.

Interface: the measurements tab now gives transport rides a service-quality panel instead of attraction ratings. It displays measured or estimated comfort, decoration bonus, average speed, and each station segment's time, distance, and fare value from the same shared transport metrics used by routing. The income tab shows the four proportional journey policies instead of a misleading single ride-wide ticket price.

Compatibility: private park version `60012` stores distance-weighted transport quality totals. Version `60013` adds the
selected alighting station and appended Free policy. Version `60014` adds platform guest substates; current saves reconstruct
the transient FIFO registry, while older-target exports serialize staged guests into a coherent station-exit approach. Exports
targeting older transport versions still clear the private route marker, and older saves load with a null destination, Fair
pricing, and no staged cohort.

### Directed station-leg measurements

Ratings: rides with multiple stations now publish rolling samples for the physical directed leg between the station just left
and the station actually reached. Excitement, intensity, nausea, duration, distance, speed, every G-force extreme, comfort,
decoration, and fare data therefore describe the journey a guest could experience rather than an artificial full-track Mobius
circuit. Multiple destinations from the same origin remain distinct endpoint pairs.

Interface: the measurements tab selects every observed `Station A -> Station B` pair by endpoint identity and displays that
leg's complete measurements. It is not limited to the first four stations or to an origin-only key. The legacy ride-list/value
rating remains a conservative compatibility envelope—minimum excitement and maximum intensity/nausea—and is refreshed only
after every station has at least one measured outbound leg, so a partially sampled Mobius ride is not presented as complete.

Transport composition: journeys through three or more stations use the directed measured service graph and sum the exact
adjacent-leg time, distance, and fare along the shortest-time route, with fare as the tie-break. The old station-wide estimate
is used only while a leg has no measurement.

Compatibility: private park version `60015` stores the directed endpoint histories and active leg accumulator state. Older
saves clear active samples on import so pre-leg partial circuits cannot contaminate the first new measurement; export to an
older target is non-mutating.

Details: [Transport ride routing rationale](transport-ride-routing-rationale.md)

Verification:

- `PathfindingTestBase.ReasonableMonorailIsChosenOverLongWalk`
- `PathfindingTestBase.RainRelaxesTheTransportTimeSavingThreshold`
- `PathfindingTestBase.FreeTransportMayWinAReasonableTimeTie`
- `PathfindingTestBase.ExtortiveTransportRequiresNoWalkingOrNonExtortiveAlternative`
- `PathfindingTestBase.TransportIsBoardedOnlyAsAPlannedRouteLeg`
- `PathfindingTestBase.PayingExtortiveTransportReducesHappinessAndCreatesThought`
- `PathfindingTestBase.PlannedTransportRouteIsIndependentOfRideInteractionState`
- `PathfindingTestBase.ChangingConcreteTargetInvalidatesPlannedTransportLeg`
- `RideRatings.TransportQualityIsDistanceWeightedAndGForcesReduceComfort`
- `RideRatings.TransportFareValueIsLedByDistanceAndModifiedByQuality`
- `RideRatings.TransportJourneyAccumulatesSegmentsAndUsesExactFareBuckets`
- `RideRatings.PlatformCapacityUsesActualConsistAndSafeLegacyFallback`
- `ParkFileMigration.TransportDestinationRoundTripsAndIsRemovedFromOlderTargets`

### Vulkan-first renderer foundation

Direction: OpenGL is now a visual-parity bridge rather than the target renderer. The new cross-platform path uses native Vulkan on Windows and Linux and Vulkan portability through MoltenVK on macOS. Obsolete hardware support is not a constraint; GPU-resident indexed assets, command streams, palette work, effects, clipping, culling, and composition are the intended ownership boundary.

Foundation: add backend-neutral GPU command and atlas structures plus a Vulkan device layer for SDL surface creation, portability enumeration, device and queue selection, swapchain negotiation, frames in flight, and reusable mapped upload rings. The backend remains behind a non-selectable gate until indexed-canvas and palette presentation can produce a correct frame, so the current change does not claim Vulkan visual parity prematurely.

Migration: retain the OpenGL weather and upload reductions as an interim reference, then delete OpenGL and CPU palette-conversion paths after Vulkan gameplay, screenshot, resize, transparency, weather, and macOS portability gates pass. The staged implementation and deletion criteria are documented in [Vulkan renderer migration](vulkan-renderer-migration.md).

Progress: the gated Vulkan command path now executes indexed lines; opaque solid, textured, masked, crosshatched, TTF, and
one-to-three-remap rectangles; deterministic depth-peeled transparency/blend composition; and ordered indexed rain/snow.
Remap and blend tables remain GPU-resident, depth and indexed colour survive every pass, and final palette presentation
selects a correct SDR format or a compatible 10-bit HDR10 BT.2020/PQ pair. HDR maps unchanged SDR sprite appearance to a
configurable paper-white level rather than making legacy art intrinsically brighter. The direct GPU drawing-context/atlas
path and visual SDR/HDR parity remain activation gates. Details:
[Vulkan renderer migration](vulkan-renderer-migration.md).

Integration: add fence-backed asynchronous indexed readback using persistent mapped frame rings, plus a disabled-by-default
Vulkan validation engine wired through configuration, drawing-engine factory selection, and window recreation. The validation
engine exercises palette, VSync, resize, presentation, and screenshot lifecycle by uploading the authoritative X8 canvas once
per frame. That full-canvas bridge is explicitly not the performance renderer; direct GPU command recording and atlas
residency must replace it before Vulkan TPS claims or default/UI activation.

Build quality: link the Vulkan loader import library by its full SDK path instead of adding the entire SDK library directory
ahead of project dependencies. This prevents the SDK's unrelated dynamic-CRT `SDL2-static.lib` from shadowing OpenRCT2's
static-CRT SDL library and removes the resulting Windows `LNK4098` warning without suppressing it.

### Measured pathfinding and vehicle hot loops

Performance: the warmed EverythingPark profiler identified guest direction searches and vehicle updates as the dominant CPU
work. Exact path-topology nodes now precompute thin-junction and adjacent wide/owned-queue classification, and one synchronous
`ChooseDirection` search reuses its current exact chunk view. Inexact, unmatched, shop/entrance-sensitive, or ghost-affected
layouts retain live tile-element behavior. Details: [Path topology cache](path-topology-cache.md).

Pathfinding: stable park exits and resolved ride/facility entrances now receive epoch-invalidated reverse distance fields.
The main thread freezes exact directed topology, the process-lifetime worker pool builds independent target fields, and the
main thread generation-checks and publishes them in deterministic order. Live permitted edges, queue ownership, guest
junction history, and the bounded heuristic fallback remain authoritative. Details:
[Shared destination route fields](shared-route-fields.md).

Verification: the clean 2,000-tick EverythingPark comparison improved from 165.895 to 184.352 TPS and from 5.919 to
5.054 milliseconds median tick time with an unchanged final checksum. The profiled comparison cut `ChooseDirection` time by
52%, cut `PeepUpdateAll` by 33.8%, and recorded no live thin-junction fallbacks in that park.

Verification: after shared reverse fields and visible transport-platform staging, two clean 2,000-tick runs measured
241.767 and 241.871 TPS with matching `2fc90d5f...` checksums and 3.868/3.851 millisecond median tick times. This is 31.2%
faster than the 184.352-TPS checkpoint and 45.8% faster than the 165.895-TPS baseline. The profiled run reduced
`ChooseDirection` to 201,038 microseconds and `PeepUpdateAll` to 880,203 microseconds; live ride-rating sampling is now the
largest isolated remaining vehicle cost. Details: [Shared destination route fields](shared-route-fields.md) and
[EverythingPark 320 TPS refactor plan](performance-320-tps-refactor-plan.md).

Verification: the station-less facility target/index and live-rating eligibility follow-up reached 254.297 and 256.592 TPS
in two clean 2,000-tick runs, with matching `182e7448...` checksums and 3.834/3.794 millisecond medians. The new profile cut
`ChooseDirection` to 81,798 microseconds but still attributed 1,189,440 microseconds to 280,975 eligible live-rating calls;
that measured inner sampler, not route selection, remains the next CPU target.

Verification: the exact directed-leg, route-cache ownership, spatial-index worklist, and save-migration checkpoint passes all
452 tests. Two clean 2,000-tick EverythingPark runs measured 251.729 and 251.123 TPS with matching `89b1134c...` checksums
and 3.719/3.720 millisecond medians. The faster run is 51.7% above the original 165.895-TPS baseline. A bounded 500-tick
profile attributes 1,358,188 microseconds to live rating sampling, including 1,082,148 microseconds in vehicle-environment
resolution; cold local-context construction is only 159,625 microseconds. Profiler scopes add clock, atomic, and stack
bookkeeping, so these totals identify relative ownership rather than predicting unprofiled TPS; environment-cache access is
still the next measured optimization target rather than guest routing.

Performance: a train-head update now reuses matching owning-ride and loaded vehicle-object pointers inside a scoped transient
context. Mismatched access and calls outside that update retain the original lookup, update order is unchanged, and nested
profiler scopes separate rating, measurement, station, motion, and sound costs. Typed entity iterators likewise retain their
registry reference and directly validate concrete type tags without replacing the mutation-safe entity lists. Details:
[EverythingPark 320 TPS refactor plan](performance-320-tps-refactor-plan.md) and
[Ride rating aggregate rationale](ride-rating-aggregate-rationale.md).

Performance: live ride-rating eligibility is now checked at the original vehicle-update sample point before non-head, ghost,
inactive-status, or non-normal-rating vehicles enter the sampler. Each eligible head traverses its consist once through the
shared deterministic seat summary, and synchronized-station adjacency is cached only for the same ride, tick, topology
generation, status, and departure flags. This follows the measured 1,081,990-microsecond/485,000-call hotspot; no throughput
gain is claimed until the checksum-matched EverythingPark rerun.

### Normal ride speed adjustment

The standalone excitement contribution from vehicle speed now follows `speed^1.5`, normalized so the established speed-90 baseline is unchanged. The formula is `90 * pow(speed / 90, 1.5) * rawScale / 5`; the hot path evaluates its equivalent `speed * sqrt(speed / 90)` to avoid a general-purpose `pow()` call per sampled vehicle tick. Intensity and nausea remain linear. This adds progressively more excitement above the baseline without disturbing the existing G-force curves or their distance-sensitive tick aggregation.

Verification: `RideRatings.VehicleSpeedTickScoringUsesPowerOnePointFiveForExcitement` covers zero, half, baseline, and double speed.

### Phoenix thought Easter egg

Addition: revive the removed “Nice ride! But not as good as the Phoenix…” guest thought as a rare fallback after an ordinary ride. It is considered only when the guest did not produce the existing “was great” response and has no other fresh thought about that ride, then succeeds on a deterministic 1-in-2048 scenario-RNG roll. Planned transport legs remain excluded.

Verification: `PlayTests.NiceRidePhoenixThoughtIsARareFallback` covers the winning roll, an ordinary losing roll, and suppression by another fresh ride-specific thought.

### World-space spatial audio and 7.1 mixing

Decision: replace viewport-bound, zoom-attenuated sound with a shared world-space listener model. One-shot effects, vehicles, and ride music now use continuous three-dimensional distance from an elevated virtual camera and remain eligible at every zoom level instead of disappearing at a screen rectangle or fixed tile boundary.

Correction: derive virtual camera height from the isometric viewport's visible ground footprint. Zooming toward an area lowers the listener, raises focused emitters, and increases their contrast over horizontally remote sources. Object `z` and camera `z` both participate in Euclidean distance and elevation.

Correction: replace the old 16-tile rolloff with a continuous distance curve and, after physical-system listening, compress its exponent to `0.8` (approximately 4.8 dB quieter per distance doubling). Proximity still orders sources generically, while rollercoasters and music remain useful deeper into the park. Vehicle voice priority starts with post-distance loudness rather than allowing raw train mass to dominate far-away sources.

Addition: Doppler pitch now comes from smoothed source-listener radial range rate, covering moving vehicles, a moving or zooming camera, and persistent one-shots. The effect is bounded to avoid fast-forward and teleport spikes; the legacy orientation-based vehicle pitch offset is no longer applied.

Decision: request 48 kHz 7.1 output in SDL's Windows-compatible `FL, FR, FC, LFE, BL, BR, SL, SR` order, with an automatic stereo retry when the selected endpoint refuses eight channels. Positional sources use constant-power speaker interpolation; non-positional stereo remains on the front pair, and LFE remains available for endpoint bass management.

Correction: replace repeated 16-bit `SDL_MixAudioFormat` additions with a floating-point mix bus, 6 dB of headroom, and a 0.95 full-scale peak limiter. The mixer now stores 8,192 channels and can select up to 2,048 vehicle emitters; ride music remains on its separate strongest-64 budget so it cannot consume the ride-vehicle pool.

Performance: replace linked channel storage and repeated vehicle-array scans with contiguous channels plus hashed id/slot lookup. Speaker-planar accumulation, a runtime-dispatched AVX2 positional kernel, direct stereo-to-positional resampling, and a fixed 1,024-frame callback give the already-dedicated SDL audio thread substantially more deadline margin. On EverythingPark, roughly 500-540 channels settled around 5.0 ms average / 7.0 ms worst against a 21.33 ms callback budget.

Tuning: vehicle volume bytes now retain their linear-amplitude meaning instead of sending half-scale track noise to roughly -41 dB. After the louder listening passes, mechanical audio is trimmed from 6x through 5x to 4.5x, rider screams from 4x to 3.5x, and other secondary vehicle cues from 2x to 1.75x.

Tuning: separate source-class rolloff exponents preserve one generic mathematical system without forcing every emitter to behave like the same physical source. Coaster, kart, and rider voices use exponent `1.1` (about 6.6 dB loss per distance doubling), world effects retain `0.8`, and amplified ride music uses `0.6` (about 3.6 dB per doubling). Music source strength rises from 2.5x through 3x and 4x to the final 5x listening calibration, while its camera-driven Doppler depth remains 5%.

Tuning: rain ambience now follows virtual listener height continuously. Its curve is shifted one camera-height step louder so zoom -1 receives the former closest-view strength, zoom -2 is louder still, and rain becomes progressively fainter as the camera rises.

Correction: ride-music culling now ranks on unclipped spatial gain instead of the final 0 dB-clamped playback volume. The 5x source calibration previously made many near and far rides tie, allowing ride iteration order to displace on-screen speakers. Newly selected sources can now start while retired voices finish their short fade, and reselected sources reverse that fade smoothly.

Correction: ride-vehicle audio now uses the closest physical car in each train as the acoustic point instead of always using its head car. This keeps long trains and distributed ride vehicles local when the visible part of the train is nearest the listener, without multiplying the same loop across every car.

Compatibility: 7.1 world effects now use a phantom front centre across FL/FR rather than routing straight-ahead sources solely to FC. This preserves side and rear positioning while avoiding a virtual-headset centre-channel downmix failure that can masquerade as proximity culling on Logitech GHub.

Verification: the Logitech endpoint advertises the standard `0x63F` 7.1 mask and accepted one WASAPI 48 kHz, eight-channel, 1,024-frame stream. EverythingPark selected all 291 vehicle emitters, mixed 337-365 vehicle channels per callback, and reported zero ride-channel or global admission failures. The callback measured 4.6 ms average / 7.6 ms worst against a 21.33 ms deadline. OpenRCT2 software-mixes logical voices into the eight-channel PCM stream before Windows or the headset driver receives it; there is no downstream 256-voice truncation boundary.

Resilience: handle SDL render-target and render-device reset events by rebuilding hardware-display textures, restoring the palette mapping, and invalidating the full frame. This is a best-effort recovery for Windows display-driver resets that previously left the simulation and audio alive behind a permanently black presentation surface.

Correction: replace the single non-positional crowd loop with an eight-sector diffuse surround field. Visible guests contribute to camera-relative directional clusters whose amplitude shares preserve the previous aggregate crowd loudness. The short positional purchase/cash-register cue receives a local 4 dB correction so it is not masked by that sustained ambience.

Correction: source retirement now fades over 350 ms. A crash no longer nulls the active ride-music track on the next game tick; the active track is allowed to finish, while the crash effect plays as an uncropped spatial one-shot.

Details: [Spatial audio overhaul](spatial-audio-overhaul.md)

Verification:

- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe` (`380` tests passed)
- Repeated 20-25 second `EverythingPark.park --verbose` runtime passes stayed stable, negotiated `48000 Hz`, `8 channels`, and 1,024-frame callbacks on the Logitech/Windows endpoint, and reported no audio callback errors.

### Main-menu responsiveness

Correction: completed aggregate rider samples now publish their rolling rating directly instead of calling the testing-only synchronous `UpdateRide()` helper. A symbolized live hang capture showed the title/main thread inside a whole-track close-proximity scan triggered when a train unloaded, while the independent audio callback kept playing. Track proximity, shelter, upkeep, and script-hook maintenance remain on the existing bounded incremental rating state machine, preventing ride-heavy title parks from monopolising a rendered frame.

Verification: the pre-fix title-menu monitor entered a sustained non-responsive state within 17-36 seconds and the debugger stack contained `RecordActiveRiderSamples -> UpdateRide -> ride_ratings_score_close_proximity_in_direction`. The corrected Release build stayed responsive for a continuous 50-second title-menu pass with no non-responsive samples; the full 380-test suite also passes.

## 2026-07-09

### Guest nausea and first aid behavior

Decision: guest nausea now behaves more like a condition that guests can respond to instead of only a hidden post-ride penalty. Sick thoughts and first-aid interest begin at nausea `128`, while the sick face and nauseous animation begin at `128 + 1` so the visual tiers stay explicit and easy to retune.

Tuning: ride nausea growth now scales across the full hunger bar, from `1x` at empty to `4x` at full, instead of only scaling the upper half of hunger and then doubling. Normal standing/walking nausea target decay is gentler, dropping by `1` per idle motive update instead of `2`.

Decision: sick guests now look for first aid over a range that scales with how sick they are. At nausea `128` they consider first aid within one tile; at nausea `255` they consider first aid within 128 tiles. The search is specified in tiles but compared using the engine's coordinate units.

Correction: guests who have already committed to a first-aid clinic do not try to sit on benches, and they are allowed to use that clinic when they arrive even if their nausea has fallen below the initial sick threshold. Guests can also use first aid opportunistically when they are already sick enough and bump into it.

Maintenance: the sick, very sick, and very-very-sick nausea tiers are now named constants, with display-start checks written as `threshold + 1`. The very sick thought remains intentionally separate from the face/animation tier and starts at the very-very-sick visual tier.

Verification:

- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.*`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows\deploy-local.ps1 -Configuration Release -Platform x64 -VCToolsVersion 14.44.35207`

## 2026-07-07

### Decoration visibility policy

Decision: outside-decoration scenery bonuses now use a ride-type visibility multiplier after local scenery diminishing returns. Fully enclosed `3d_cinema`, `motion_simulator`, and `circus` rides receive no outside-decoration bonus. `haunted_house` and `flying_saucers` receive half. `crooked_house` and `dodgems` receive one quarter. Mazes and other ride types keep the full local scenery score.

Reasoning: the local-context system was correctly measuring nearby scenery, but it treated all riders as if they could see outside equally well. Fully enclosed shows should not be excited by gardens they cannot see, while limited-visibility rides should get a reduced benefit instead of an all-or-nothing rule.

Correction: local-context line of sight now traces to both the bottom and top of a candidate object. The existing terrain-relative range gate is preserved, but a tall decoration, elevated path, or elevated foreign track can now count if its upper ray clears maze walls or other solid blockers. Low objects behind same-height maze walls remain blocked.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

### Fixed-ride scenery sampling

Decision: fixed rides with `BonusScenery` now sample their local scenery context from the ride footprint centre and derive normal flat-ride eye height from the ride descriptor clearance box. Tower-like rides still use their dynamic height, while enclosed rides and mazes keep low viewpoints.

Reasoning: flat rides such as the Haunted House revealed that the remaining fixed-ride scenery hook was still too legacy-shaped. Clearance-derived viewpoints make Ferris Wheel, Magic Carpet, Enterprise, and similar rides benefit from height without hand-maintaining each ride type, while footprint-centred sampling avoids judging a multi-tile ride from only its station tile.

Current balancing: the local scenery score remains uncapped and square-rooted. `BonusScenery` consumes that same uncapped score; there is no restored legacy cap in the fixed-ride adapter.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

## 2026-07-06

### Decoration diminishing returns

Decision: local scenery now uses an uncapped square-root diminishing-return curve. It takes about four times as much raw decoration to reach the former local scenery cap, and denser decoration can still add value beyond that point at a slower rate. Fixed-ride scenery modifiers use the same uncapped local scenery score.

Reasoning: decoration was too influential at modest density and then stopped mattering too early. The new curve keeps dense scenery worthwhile without making the first few visible items dominate ride stats.

Current balancing: raw scenery `1200` maps to the former 18-point local scenery contribution. Raw scenery `300`, the old scenery divisor, maps to 9 points, and raw scenery above `1200` continues growing by square root.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

### Ride and guest tuning

Decision: vehicle-sampled decoration excitement now scales by vehicle speed around a `90` speed baseline. A vehicle at `45` speed receives half of the scenery excitement for that tick, and a vehicle at `30` receives one third, while path and track proximity bonuses stay unscaled.

Decision: Boat Hire free-roam and unbanked guided-turn ticks now use a gentler `0.5/1/1` excitement/intensity/nausea distribution instead of the coaster-style `2/4/4` unbanked turn score.

Known issue: Boat Hire stats inflated during the recent ride-rating tuning work and remain too high. Skipping the generic vehicle accumulator path was tested and walked back, and reducing the Boat Hire free-roam score did not fully solve the inflation. The main suspect is the new decoration/local-context scoring, which will need a separate balancing pass.

Decision: visible water now counts as a lightweight surface decoration, using the same low raw scenery weight as mowed grass. This gives water features a small local scenery payoff without making lakes equivalent to dense scenery placement.

Tuning: normal guest generation now uses `$50,000` park value as the square-root baseline instead of `$40,000`, reducing arrivals for parks below the new baseline while preserving the same curve shape.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md), [Guest generation and park rating rationale](guest-generation-rating-rationale.md)

## 2026-07-05

### Height-aware ride scenery context

Decision: replace the direct 3x3 vehicle and maze decoration scans with a shared cached local-context query. The query now scales from 5x5 at ground level up to 15x15 for high ride samples, applies distance falloff, line-of-sight checks against solid vertical volumes, mild lower-scenery height penalties, and diminishing returns per context channel. Range is resolved through a downward sight ray toward each candidate tile, so hills extend vision over lower land, pits reduce vision toward higher land, and scenery on cliff tops above the rider is occluded. Flat-ride scenery modifiers also use this context from the station/start tile with an eye-height offset, so elevated flat rides benefit from increased sight range.

Reasoning: decoration and proximity bonuses are now accumulated where riders actually are, so the local query needs to represent what riders can plausibly see instead of acting like a tiny immediate-neighbour count. Caching by origin tile and ride id keeps the expensive visibility work away from per-vehicle hot paths. Test-mode circuits now keep producing rating samples after the first completed test, preserving the rolling average while letting scenery/context edits show up after later test runs.

Correction: fixed-ride scenery origins now use ride-specific eye heights for tall non-coaster rides such as observation towers, roto-drop rides, launched freefall rides, lifts, ferris wheels, and chairlifts. Mazes use a lower viewpoint and maze track now blocks line of sight, so maze walls behave as walls for local decoration visibility.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

### Ride rating sample save data

Decision: persist the ride rating raw accumulator plus active and recent rider/test samples in fork save version `60005`.

Reasoning: visible excitement/intensity/nausea ratings were already saved, but the new rolling sample cache was not. That meant an aggregate-rated ride loaded from a save could briefly recalculate from no samples and collapse to very low or zero ratings, then slowly recover as new riders rode it. New saves carry the rolling samples forward. Older saves clear the new sample fields but preserve the already-saved visible rating until a fresh rider/test sample exists.

### Boat Hire return routing rollback

Correction: remove the custom Boat Hire intervention that forced a boat to return when a passenger generated the normal "I want to get off" thought. Boat Hire already has return-to-station routing, and testing showed the reported failure was caused by blocked routing rather than missing get-off handling.

Reasoning: the added helper and steering override duplicated existing ride behavior and could mask the real problem when the station route is physically blocked.

Details: [Boat hire return rationale](boat-hire-return-rationale.md)

Verification:

- Removed the regression test that asserted the deleted intervention.
- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.*`

## 2026-07-04

### Guest off-path recovery

Decision: guests walking on surface tiles now do a small local search for a reachable footpath within three tiles before falling back to random grass wandering. The search is wall-aware, respects blocked surfaces, water, in-park surface movement, and the same height tolerance used by normal surface movement.

Reasoning: guests that get pushed or dropped just off the path network should visibly try to recover when a nearby path is accessible, while still using the original random surface wandering when no local rejoin route exists.

Details: [Guest surface path rejoin rationale](guest-surface-path-rejoin-rationale.md)

Verification:

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PathfindingTestBase.SurfaceGuestsStepTowardAdjacentPath`
- From `bin`: `.\tests.exe --gtest_filter=*Pathfinding*`
- From `bin`: `.\tests.exe`

### Mowed grass decoration

Decision: mowed grass now counts as lightweight decoration in ride scenery/proximity scans, per-tick vehicle and maze context scoring, and guest surroundings checks. Only surfaces that can actually grow grass and are in the `GRASS_LENGTH_MOWED` state count.

Reasoning: groundskeeper mowing should have a visible gameplay payoff instead of being cosmetic and staff-stat-only. Well-kept lawns now help nearby rides and guest scenery impressions through the same local scans that already reward decorations.

Details: [Mowed grass decoration rationale](mowed-grass-decoration-rationale.md)

Verification:

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=TileElementWantsFootpathConnection.MowedGrassCountsAsDecoration`
- From `bin`: `.\tests.exe --gtest_filter=TileElementWantsFootpathConnection.*`
- From `bin`: `.\tests.exe --gtest_filter=RideRatings.*`

## 2026-07-02

### Windows build setup

Decision: use `openrct2.proj` through MSBuild as the local build entry point and pin MSVC `14.44.35207`, because the older default MSVC `14.38.33130` links poorly against the downloaded dependency libraries on this machine.

Correction: default the local build helper to Release for playable builds. The earlier Debug default can explain severe lag in an empty park because it produces a much larger, unoptimized executable with debug artifacts.

Decision: add a local deployment helper for `D:\Games\Independent\OpenRCT2Mod` that builds Release and mirrors this fork's built `bin\data` assets instead of mixing the develop executable with the vanilla main-branch deployment's `data` directory.

Details: [Windows local build setup](windows-local-build.md)

### Ride excitement, intensity, and nausea

Decision: introduce a raw per-tick ride rating accumulator for normal ride rating calculations. Vehicle test measurements now sample track piece, velocity, G forces, shelter, and nearby decoration into running excitement, intensity, and nausea totals. Aggregate totals finalize through a square-root curve so doubled raw stats produce about 40% higher final stats.

Correction: aggregate ratings with tick samples now start from sampled raw totals only, with descriptor base ratings and additive legacy ride-wide bonuses excluded from the aggregate finalizer.

Correction: aggregate-rated rides without current samples no longer fall back to descriptor base ratings. A ride with no sampled rider/test data displays zero aggregate stats instead of hidden base stats.

Correction: completed rider/train samples now publish immediately into a rolling cache of the last twenty samples. The displayed ride rating is calculated from the average raw sample in that cache, so the GUI updates after rides are actually ridden while still smoothing one-off rider paths.

Correction: train rating samples now average the sampled contribution from every vehicle on the train for each tick instead of using only the lead vehicle. This lets rear and middle cars affect the rating without multiplying a train's raw stats by its car count.

Correction: completed test runs now publish their measured raw stat accumulator into the same rolling sample cache. Tested rides therefore display aggregate stats from the test run instead of staying at zero until guests ride them.

Correction: formal vehicle tests now accumulate excitement/intensity/nausea in the same active train sample slot used by live rider trains, keyed by the head vehicle as a phantom rider sample. In-progress test accumulators are no longer used for display, so the ride window keeps its existing rating during a test and updates only when a test train completes its circuit.

Correction: Maze no longer receives descriptor base stats or `BonusMazeSize`/`BonusScenery` post bonuses. Maze pathfinding now records per-guest active samples and publishes each completed exit path into the same rolling cache, so maze length contributes only through the paths guests actually walk.

Tuning: Maze capacity is now a three-mode policy instead of a literal rider-count setting. Normal allows one guest per maze tile, Overcrowding doubles that capacity while halving the raw accumulated rating stats before the square-root finalizer, and Sparse allows one guest per two tiles with a minimum of one while doubling the raw accumulated stats. Legacy numeric S4/S6 park imports normalise to Normal; track-design imports preserve `0`/`1`/`2` bucket values and otherwise normalise to Normal.

Tuning: per-tick G-force scoring now uses smooth curves informed by the old ride-wide G-force logic. `1.0G` vertical is neutral, `0.0G` is treated as exciting airtime, negative vertical G becomes progressively nastier, positive vertical G mostly feeds intensity, and lateral G has the steepest curve with the old `2.8G`/`3.1G` lateral penalties translated into a smooth severe range.

Tuning: the accumulator speed guard now floors speed at `1` instead of capping it at `90`, so speed continues to scale normally while zero-speed samples are guarded. Vehicle-object rating multipliers remain on the raw aggregate before the square-root finalizer, which is equivalent to applying the same ride-entry bonus to each sampled tick while preserving saved raw samples.

Details: [Ride rating aggregate rationale](ride-rating-aggregate-rationale.md)

### Guest generation and park rating

Decision: remove the active guest-count soft cap from normal guest generation. Park rating is now a smooth projection of average in-park guest happiness and happiness target, so crowding and queue pressure regulate future guests through happiness rather than a hardcoded suggested maximum.

Tuning: normal guest generation now scales geometrically with park value, using `$50,000` as the current baseline for the tuned spawn rate. A larger park attracts more guests when rating, pricing, awards, and entry value are otherwise equal, but square-root scaling keeps the growth in arrivals sublinear.

Tuning: park rating now applies exponentially to guest generation, doubling about every 100 rating points around the `700` reference point. Rating `700` is the healthy baseline, `800` generates about twice as many guests as `700`, `600` about half as many, and `500` about half of `600`.

Correction: guest generation keeps fractional probability through all modifiers and rounds any positive non-zero final chance up to `1`, so small parks with non-zero value no longer lose normal guest generation to integer truncation.

Verification: added direct guest-generation probability tests for the `500`/`600`/`700`/`800` rating curve and the tiny positive park-value case; `PlayTests.*` passes.

Research: real park calendars support the inherited March-through-October calendar as a temperate seasonal-park abstraction, but not as a universal calendar. Northern parks often close or reduce service outside spring-fall, while warm-climate parks and holiday-event parks may keep operating in winter.

Details: [Guest generation and park rating rationale](guest-generation-rating-rationale.md), [Real park seasonality research](real-park-seasonality-research.md)

### Ride admission pricing

Decision: replace direct ride admission price editing with a target policy: discount, fair price, or expensive. Normal ride admission prices now recalculate from ride value after ratings update, while shops, toilets, and photo/item prices remain direct controls.

Correction: expensive pricing targets the highest conservative price happy/value-tolerant guests should still ride for, staying below the hard refusal threshold instead of crossing just above the fair-price maximum.

Tuning: automatic ride-admission target prices are globally reduced to 70% before the `$20.00` cap is applied, so a former `$10.00` calculated price becomes `$7.00` and high-value rides hit the cap less easily.

Correction: guest ride-value perception now uses the same 70% scale when deciding whether a ride is discounted, expensive, or too overpriced to ride. This keeps the automatic target prices and guest willingness thresholds aligned.

Correction: guests now have an expensive-but-still-rideable ride thought for the upper half of the price band between fair price and outright refusal. The old target labels were also renamed from good deal/no effect/bad deal to discount/fair price/expensive.

Correction: the expensive ride thought now uses in-universe guest wording, "isn't worth the money", instead of exposing the internal ride-stat formula.

Decision: migrate runtime `money64` from `$0.10` units to `$0.01` units. This unlocks true cent precision for ride prices and other runtime money while preserving legacy tenth-based import, save, replay, and highscore compatibility through explicit conversion bridges.

Correction: legacy tenth-based authored values now convert at runtime boundaries for ride default admission prices, computed ride value, ride upkeep, object JSON prices, terrain-edge changes, water changes, and editor initial-cash clamps. This fixes the 10x-too-small running costs, ride tickets, and construction/landscaping prices caused by treating old tables as cent values.

Correction: legacy scenario objective currency conversion is now objective-aware. Money objectives scale from tenths to cents, while non-money payloads stored in the same field, such as minimum excitement for finish-five-coasters goals, remain raw.

Correction: currency text formatting now pads cent values below `$0.10`, so `$0.05` and `$0.01` display with two decimal digits instead of `0.5` or `0.1`.

Tuning: target-pricing margins now use `$0.05` minimums for discount, fair-price, and expensive pricing because the runtime money type can represent those values.

Details: [Ride pricing target rationale](ride-pricing-target-rationale.md), [Money cent precision rationale](money-cent-precision-rationale.md)

### Park entrance pricing

Decision: replace the park admission spinner with three automatic policies: charge the richest spawning guest, maximize admission profit across the scenario's spawn-cash distribution, or stay affordable to every spawning guest. The affordable policy is the new default for newly initialized parks.

Tuning: park entrance targets use the same 70% value debuff as ride admission targets before applying guest-cash and maximum-price caps, so a park needs more ride value before its entrance fee reaches the cap.

Compatibility: direct entrance-fee commands and legacy saves are preserved through an internal custom mode. Once a player selects one of the three policies, the computed policy owns the displayed and charged entrance fee.

Save format: fork-owned `.park` changes now use the private `60000+` version band instead of upstream's next sequential version. This prevents future upstream save versions from colliding with this mod's custom ride-pricing, cent-money, and park-entrance fields when upstream development is fetched later.

Details: [Park entrance pricing target rationale](park-entrance-pricing-target-rationale.md)

### Verification

Decision: verify the work against the local Windows compiler and the existing ride-rating fixture suite.

Completed checks:

- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=RideRatings.*:S6ImportExportBasic.*:S6ImportExportAdvanceTicks.*:*RideSetPriceAction*`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.*:FormattingTests.*:EntityImportTests.*:S6ImportExportBasic.*:S6ImportExportAdvanceTicks.*`
- From `bin`: `.\tests.exe --gtest_filter=Replay/ReplayTests.*`

The build succeeds with one existing non-fatal Roslyn `System.Memory` binding warning from `openrct2.proj`.
