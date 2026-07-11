# Ride rating aggregate rationale

This change moves excitement, intensity, and nausea away from a purely post-test calculation and into raw per-tick samples gathered while vehicles test and while guests actually ride.

## Model

Each sampled vehicle tick contributes raw excitement, intensity, and nausea from:

- current track element descriptor and special track element type
- current velocity
- vertical, lateral, and longitudinal G forces
- whether the sampled vehicle position is sheltered or underground
- local scenery, path, and nearby-ride context around the sampled tile
- synchronized station operation

Raw totals are stored in active rider/train/test samples. Completed samples are copied into a rolling cache of the last twenty rider/train/test results, and the displayed rating is calculated from the average raw sample before applying the geometric curve:

```text
rating = sqrt(raw / divisor) * 100
```

The initial divisor is `1000`, so doubling raw accumulated stats produces roughly `sqrt(2)` final growth, or about 40% higher final ratings. Balance should be tuned by adjusting the per-source weights documented in `docs/ride-rating-stat-source-manifest.md`, not by adding post-aggregate category scalars.

The aggregate path does not seed ratings from `RideRatingsDescriptor::BaseRatings`. If an aggregate-rated ride has no samples, it displays zero aggregate stats rather than falling back to hidden base stats.

## G-force scoring

Per-tick G-force scoring now treats different accelerations as different ride sensations instead of one generic deviation from normal gravity:

- `1.0G` vertical is neutral because it is the normal resting load.
- `1.0G` down to `0.0G` is airtime, mostly excitement with moderate intensity and light nausea.
- negative vertical G starts with the full airtime effect, then adds steeper intensity and nausea as restraint pressure gets harsher.
- positive vertical G adds some excitement, but mostly intensity and nausea, following the old weighting where high positive G was more forceful than fun.
- lateral G uses the steepest curve. Mild sideways force is tolerable, but the curve rises quickly enough that a faster vehicle through the same curve can score higher even if it spends fewer ticks in that curve.
- longitudinal G is the signed change between consecutive realised head-train speed magnitudes. Acceleration is weighted toward excitement, while braking is less exciting and more uncomfortable. Direct launch, brake, and chain-speed assignments are observed even when they bypass the vehicle's `acceleration` field; constant-speed curves still produce no longitudinal G.

All G channels are speed-normalized at the `90` baseline, then receive a smooth speed/G coupling factor. Standalone excitement uses `90 * pow(speed / 90, 1.5)`, so speed `90` retains its previous value; standalone intensity and nausea remain linear. The superlinear excitement curve gives genuinely fast travel a modest additional thrill while the stronger physical consequences still belong to the force channels.

## Sampled ride profiles

`RideTypeDescriptor::SampledRatings` supplies source-level per-mille coefficients for local context, speed, longitudinal G, vertical G, lateral G, airtime, and speed/G coupling. Track-feature bonuses deliberately remain outside the profile.

The roller-coaster profile uses `0.3` standalone speed, `2.0` vertical G, `0.5` lateral G, and `0.5` speed/G coupling. Go Karts retain `3.0` speed and lateral G. The lower coaster speed source keeps short flat layouts restrained, while the softer coupling lets force, special track, and vertical context matter at ordinary coaster speeds. All unspecified coefficients remain `1.0`.

The old ride-wide G-force code is used as a calibration reference. Its `2.8G` and `3.1G` lateral thresholds are no longer hard cliffs, but the smooth curve is already severe near those landmarks: excitement tapers while intensity and nausea continue to compound.

## Function flow

- `RideRatingAccumulator` in `src/openrct2/ride/Ride.h` stores raw totals and tick count for one active or completed sample.
- `Ride::activeRatingSamples` tracks currently running guest/train samples, and `Ride::recentRatingSamples` stores the last twenty completed samples used for display smoothing.
- `RideGetOrCreateActiveRatingSample()`, `RideAddRecentRatingSample()`, and `RideGetRecentRatingAccumulator()` in `src/openrct2/ride/Ride.cpp` manage active samples and the rolling average.
- `Vehicle::UpdateMeasurements()` in `src/openrct2/ride/Vehicle.cpp` samples the train's current per-car track pieces and G forces during test runs.
- formal vehicle tests use the head vehicle id as a phantom rider sample in the same active sample cache used by live rider trains.
- `test_finish()` in `src/openrct2/ride/Vehicle.cpp` marks the test complete, then publishes the completed phantom train sample into the rolling sample cache.
- `RideRatingUpdateLiveTrainSample()` in `src/openrct2/ride/Vehicle.cpp` samples normal passenger trains during live operation. Its caller rejects non-head, ghost, inactive-status, and non-normal-rating vehicles before entering the profiled path.
- `RideRatingPublishTrainSample()` in `src/openrct2/ride/Vehicle.Station.cpp` publishes a train sample when the train unloads.
- `RideRatingAccumulateTick()` converts that tick state into raw excitement, intensity, and nausea.
- `RideRatingTickIsSheltered()` and `RideRatingGetLocalContextScore()` move shelter, scenery, path, and nearby-ride effects into the sampled tick path.
- Every vehicle keeps an independent accumulator for its complete lap. Completed vehicle samples are combined only when the train finishes, so nonlinear speed and G curves are evaluated before cars are averaged.
- `test_reset()` and `InvalidateTestResults()` clear test and rider samples whenever test data is reset.
- `RideRating::RecordRiderSample()` in `src/openrct2/ride/RideRatings.cpp` records completed rider/train/test samples and directly publishes the rolling aggregate rating. It does not synchronously run the testing-only whole-track scan; proximity, shelter, upkeep, and script-hook maintenance stay on the bounded incremental update state machine.
- `RideRating::RecordActiveRiderSample()` in `src/openrct2/ride/RideRatings.cpp` publishes completed active samples for both live trains and phantom test trains.
- `RideRatingsCalculate()` in `src/openrct2/ride/RideRatings.cpp` uses aggregate finalization for normal rides and mazes from completed rolling samples only; in-progress formal test accumulators are deliberately not displayed.
- `RideRatingsCalculateAggregated()` finalizes raw totals through the square-root curve.

### Vehicle-update lookup lifetime

Live and test sampling run inside the ordinary head-vehicle update and share its owning ride and loaded vehicle object.
`Vehicle::Update()` resolves those two objects once and installs a transient scoped lookup context. Rating, station,
motion, and sound helpers retain their normal accessor calls, but matching ride ids and object subtypes reuse the resolved
pointers instead of repeatedly entering the game-state ride vector and object manager. A helper querying a different ride
or subtype follows the original lookup path.

The live-rating gate is evaluated at the update site, before the profiled sampler call. The previous placement invoked the
sampler for every vehicle and rejected almost all 485,000 calls inside it. Eligible heads now build one const
`TrainSeatSummary`, which supplies stable linked-car order and rider presence to the per-car accumulator loop. Train test
publication and active-sample clearing use the same summary traversal instead of maintaining separate linked-car walkers.
For synchronized operation, adjacent-station discovery is cached per ride for the current simulation tick and map-topology
generation; ordinary rides still short-circuit without consulting that cache. The cache is transient, affects no saved state,
and recomputes after either the tick or topology generation changes. Ride service/status invalidation also clears it explicitly,
while status and departure flags are part of the cache key, so a close/reopen or operating-setting edit cannot reuse an earlier
eligibility result within the same tick.

This is deliberately not a persistent rating cache. The context is restored when the head update returns, is never
serialized, and carries no independent invalidation generation. Correctness depends only on the pre-existing vehicle-tick
rule that its owning ride and loaded ride object remain alive for the duration of that synchronous update. Rating sample
order, per-car force calculation, accumulator totals, rolling publication order, and train mutation order are unchanged.
Nested profiler scopes cover eligible `RideRatingUpdateLiveTrainSample()` calls and `Vehicle::UpdateMeasurements()` so
actual rating work can be separated from station, track-motion, and sound time without charging a profiler scope to every
non-head vehicle in the EverythingPark benchmark.

The first checksum-matched rerun reduced sampler entries from `485,000` to `280,975` over 500 measured ticks, but eligible
sampling still used `1,189,440` microseconds. Local-context construction accounted for `154,018` microseconds, so it is not
the dominant explanation for the remaining `4.23` microseconds per eligible call. The next pass therefore profiles and
optimizes consist traversal, active-sample lookup, and per-car force/environment work without reducing tick cadence or
changing accumulator order.

A later 500-tick profile isolated `RideRatingResolveVehicleEnvironment` at `999,433` microseconds across `582,586` calls,
while the `18,647` cold local-context builds accounted for only `149,584` microseconds. The cache-hit path was rescanning up
to nine spatial-generation chunks after any unrelated map invalidation. Invalidation now propagates one generation to the
small set of origin chunks whose seven-tile context can overlap the changed chunk. A vehicle cache hit consequently reads
one origin-chunk generation instead. This moves bounded work from the per-vehicle path to map mutation and is conservative:
it can rebuild an unaffected origin near a chunk edge, but it cannot preserve context affected by the changed tile.

The shared exact-key cache also requires map position to influence bucket selection. Its original shift-concatenation hash
shifted X and Y out of the final 64-bit word, creating progressively longer same-ride/same-height collision chains as new
track tiles were sampled. Field-by-field multiplication now mixes both coordinates and the remaining key fields without
changing key equality or the spatial generation stored with each value. A hash collision can still only cause an equality
check; it cannot return another tile's environment or bypass invalidation.

The shared cache is bounded independently of saved samples. The validated 65,536-set, four-way candidate holds at most
262,144 environments and measured `412.822` TPS after a 2,000-tick warmup and `314.538` TPS after a 4,000-tick warmup. An
earlier 16,384-set candidate was rejected because its 65,536-entry limit caused enough replacement churn to regress the hot
window without improving the late one.

The accepted table therefore remains 65,536 sets and 262,144 maximum entries. Empty ways fill first; a full set uses deterministic
FIFO replacement. Hits do not alter replacement order. A replaced or generation-stale slot is rebuilt through the same
local-context scan, while each active accumulator retains its current one-entry environment cache. This caps shared context
memory and removes node-map rehashing without changing rating results, tick order, or park serialization. A final 131,072-set,
524,288-entry trial was rejected: it improved the 2,000-warmup result slightly to `415.031` TPS but reduced the 4,000-warmup
result to `289.782` TPS with matching checksums, showing that its larger fixed footprint harmed late working-set locality.

Capacity is unchanged, but set storage is now structure-of-arrays: a validity mask and FIFO cursor precede four contiguous keys,
four spatial generations, and four environment payloads. The expected 64-bit set size falls from 360 to 312 bytes, reducing the
fixed table from about 22.5 MiB to 19.5 MiB; a size guard rejects later layout growth beyond that bound. One four-way probe records
both an exact-key match and the first invalid way. Store therefore reuses that result instead of scanning the keys again and then
scanning for an empty slot. At the recorded 19,627 cold builds per 500 ticks, a full-set miss previously visited up to twelve slots
and can now visit four, avoiding up to 157,016 slot visits. A matching stale-generation way is still overwritten in place, an
invalid way still fills before replacement, a full miss still advances deterministic FIFO, and a hit still leaves FIFO unchanged.
Clearing the table invalidates the masks and resets the cursors; inaccessible old keys and payloads have no semantic effect.
A focused public-behavior test changes nearby scenery without issuing spatial invalidation, confirms the stored value remains in
use, clears the cache, and then observes the rebuilt value. This covers mask-based clearing without exposing replacement internals.

After that change, the final instrumented run reported `1,082,148` microseconds for the same `582,586` environment calls and
`159,625` microseconds for `19,627` cold builds. That wall-time variation is not treated as a regression claim: every enabled
`PROFILED_FUNCTION` scope performs clock reads, atomic sample updates, and thread-local stack bookkeeping, and nested totals
include that instrumentation. The clean profiler-disabled runs are the acceptance measure; the child scopes are used only to
identify relative ownership.

The attribution profile used a deliberately hierarchical child signature: consist/rider checks ran once per eligible head,
and accumulator lookup, force, environment, and score helpers ran in stable linked-car order. Those counters established the
empty-train, sampled-train, and per-car call ratios without adding simulation state. After that ratio and environment
ownership were recorded, the leaf helper scopes were removed from the production hot loop. `PROFILED_FUNCTION` performs an
acquire load even while disabled and clock, atomic, sample-ring, and thread-local stack work while enabled. The outer live
sampler and sampled-train scopes remain available; another leaf investigation should use a bounded diagnostic branch rather
than permanently taxing every rating car and station consist traversal.

The audit did not add a second consist or accumulator cache. Live sampling already builds the consist only once in the
travelling/departing/arriving status path; the station-dispatch summary belongs to the mutually exclusive
`waitingForPassengers` path, so a head-update cache would have no live-rating hit to reuse. Active samples already have a
direct entity-id to `(ride, vector index)` reference which is validated before use; only a stale reference takes the linear
recovery scan. Keeping that existing index avoids pointer lifetime hazards when the active-sample vector grows.

This optimization deliberately leaves ownership at the vehicle accumulator boundary. It adds no second ride-wide cache and
does not assume that one complete circuit is the publication unit. Multi-station and Mobius rides partition the ordered ticks
at physical station arrivals and publish directed station-to-station legs; transport planning composes any required adjacent
legs into one journey.

Within a sampled train, immutable descriptor inputs are now derived at their narrowest valid lifetime. Boat-hire and
transport classification are resolved once per train. Each car resolves its track type and descriptor once; that same
descriptor feeds G-force curvature and sampled track-feature scoring. Normalized train speed and the standalone speed tuple
are computed once per train and reused by every car. This removes repeated square roots and lookup/arithmetic only: the same ticks are sampled, cars
are visited in the same order, longitudinal G still uses each accumulator's immediately previous sampled train velocity,
the local-context cache remains accumulator-owned, and all integer score formulas and publication order are unchanged.

Longitudinal G now also has a one-train-call memo for the pure calculation. Its key is the exact previous train velocity read
from the current car's accumulator; the current velocity is fixed by the enclosing train sample. Cars with the same history
reuse the value, while a first-history car still skips the calculation and a divergent history recomputes it before that car's
accumulator is updated exactly where it was before. The recorded 582,586 car accumulations and at most 279,531 train-head calls
put the fully warm duplicate opportunity at roughly 303,055 or more calculations per 500 ticks, subject to first-history and
divergent samples. Independent per-car history, force totals, car order, and publication semantics remain unchanged.

Speed-dependent G-force coupling is prepared at the same train lifetime. A fresh deep-window profile contains 191,241 sampled-
train accumulations inside 248,335 live-sample calls; the per-car leaf count is intentionally unavailable because those diagnostic
scopes were removed. The older attribution capture's 582,586 per-car calls is not combined arithmetically with the fresh count.
Normalized speed and the coupling numerator use only the shared train speed and immutable sampled-rating profile, so they are
computed once and passed through stable car order. Raw force scores remain per car, and the prepared application retains the
original component-by-component multiply/divide order. The direct pure API delegates through the same preparation path; tests pin
the numerator at half, baseline, double, stopped, and clamped inputs and compare prepared and direct score tuples.

Each sampled train also owns one transient G-force score memo. Because its profile and speed context are fixed at construction,
the lookup compares only the car's finalized vertical, lateral, and longitudinal integers. The longitudinal value is read after
the independent accumulator-history correction, so differently signed or divergent histories cannot alias. Exact hits reuse the
pure `TickScore`; misses execute the established curve, coefficient, and coupling operations and replace the entry. This state is
stack-local, consumes no RNG, and never crosses a train call or publication boundary. Tests compare repeated neutral hits, positive
and negative longitudinal misses, a severe-force edge, and return to the evicted neutral tuple against direct scoring.

Each accumulator's one-entry environment cache also prepares its local-context score through the profile-coefficient division once
per exact environment, scoring mode, and profile. The prepared tuple is stored immediately before speed scaling: every sampled tick
still multiplies and divides each component by that tick's normalized speed in the original order. Consequently a speed-only change
reuses the base without moving an integer-rounding boundary. Installing a different cold-built or shared-cache environment invalidates
the base; unchanged spatial generation preserves it. Tracked and boat-hire scoring cannot alias, and the original profile coefficient
is the key even though the scorer clamps it. Explicit accumulator clear invalidates both layers. This is transient runtime state only;
saved totals, transport-quality accumulation, car order, and sample publication are unchanged. Focused tests compare cached and direct
tuples across speed, mode, coefficient, clear, shared expansion, and nearby-map invalidation.

The shared table also omits four redundant `LocalContextScore` aggregates from its payload. Excitement, intensity, nausea, and
`verticalInteraction` are finalized from scenery, path, foreign-track, bridge, near-miss, loop, vertical-track, own-track, and
height-exposure components. Cold construction and shared-hit expansion call the same finalizer, while accumulator-owned runtime
caches retain the complete public environment. This changes the expected payload from 56 to 40 bytes and a four-way set from
312 to 248 bytes, reducing the accepted-capacity table from 19.5 MiB to 15.5 MiB. Capacity, FNV bucket choice, full-key equality,
origin generation, validity masks, and deterministic FIFO are untouched. A second independent runtime cache in the focused test
forces shared expansion and compares the complete reconstructed score plus shelter with the cold result.

## Multi-station leg ownership

For a ride with more than one station, a completed sampled unit is the physical departure station to the physical arrival
station, not a numerically inferred `station n -> station n + 1` range and not necessarily a complete circuit. Each active
car accumulator records the head vehicle's `current_station` when its first tick is sampled. `UpdateArriving()` supplies the
station index read from the actual arrival track element when braking/arrival completes. The failed-station-brakes branch
also resolves and assigns that track element before publication rather than reusing the departure station. Pass-through
trains publish at the same boundary and immediately begin a fresh accumulator on their next departing tick, so through-riders contribute one
complete sample to every leg they traverse without being averaged into a fictitious full-track trip.

Completed histories are sparse and sorted by the ordered `(origin, destination)` edge. Each directed edge stores a
twenty-sample rolling history and a finalized excitement/intensity/nausea tuple. Opposite shuttle directions and a middle
station with two physical successors therefore remain independent instead of replacing or blending one another. Single-station rides continue to use only
the existing ride-wide history, so their rating path and fixtures do not allocate or consult leg state.

Materializing a measured transport edge now averages that edge's rolling history once and derives distance/time/speed,
distance-weighted sheltered exposure, and comfort/decoration from the same immutable accumulator. Previously those consumers each traversed the same twenty-sample
ring, so transport-service freshness validation and a following dirty-service rebuild repeated the aggregation twice per edge.
All measurement conversions, distance-weighted quality divisions, clamping, fare rounding, fallback behavior, directed-edge
order, and journey composition remain unchanged. This adds no stored cache or invalidation lifetime and does not affect park files.

The Measurements page selects an explicit `Station A to B` edge. Its finalized ratings, maximum and average speed, and
vertical/lateral/longitudinal G extrema use the same white-label/black-value rows as ordinary ride measurements. Ride length
and time remain global for non-transport rides and are not repeated on every leg. Transport retains measured leg distance and
duration because its service page intentionally has no ride-global construction summary. Drops, inversions, airtime totals,
holes, and other construction/test facts that are not yet captured as unambiguous leg events remain in the existing test
fields and are labeled ride-global; they are not copied onto every leg.

The Measurements window provides a deterministic `Station A to B` selector ordered by the directed endpoint pair. Every
measured leg is selectable, including fifth and later edges on bidirectional shuttles, and the selected leg expands its E/I/N,
maximum/average speed, and G extrema. Transport legs additionally show their measured comfort/decoration and the exact time,
distance, and fare used by route planning. Selection is retained by endpoint identity, so inserting or refreshing an earlier sorted edge does
not silently switch the displayed physical leg.

### Multi-station initial train placement

Vehicle creation for an ordinary non-block continuous circuit first resolves every station origin by ride, station index,
height, and track-origin sequence. It walks the completed circuit to recover physical station order rather than assuming that
station-array order matches the track. The canonical start remains first and the remaining stations are ordered backward around
the circuit, matching the `prev_vehicle_on_ride` / `next_vehicle_on_ride` ring used by collision detection.

The number of consists that fit each platform comes from the same `RideGetMaxTrainsPerStation()` calculation used by
`Ride::updateMaxVehicles()` to limit the ride's train count. Allocation gives each station one consist before using a second
slot, then groups all trains assigned to a station so its shared remaining-distance chain and physical collision neighbours stay
contiguous. If the full requested allocation or every exact station anchor cannot be resolved, creation falls back to the
established first-station path. Block sections, cable lifts, station-to-station/chairlift, shuttle, launch, race, dodgem, and
other special placement modes remain on their existing dedicated paths.

Legacy ride lists, value calculation, sorting, scripting, and other consumers still require one tuple. Their compatibility
summary is deliberately conservative: excitement is the minimum measured directed-edge excitement, while intensity and nausea
are the maxima. It replaces the previous/null ride-wide tuple only after every station has at least one measured outbound edge;
incomplete coverage cannot make an unmeasured portion disappear. Because that tuple may not describe any particular leg, the
Measurements page does not present it as another experienced trip. Guest admission, satisfaction, and nausea calculations use
the selected boarding station's leg tuple when it exists and fall back to the compatibility tuple only before that leg has
measurements.

Park format `60015` stores origin/destination indices on active accumulators and the sparse completed leg histories. Format
`60016` adds distance-weighted sheltered exposure so rain routing can rank the selected journey rather than a ride-wide proxy. Older
saves load with no leg histories and retain their existing ride-wide tuple until new physical legs are observed. Older-target
exports omit the new fields and histories directly in the serializer; no live accumulator or history is cleared or rewritten
to create the export. Because an older reader cannot distinguish adjacent legs, that export writes empty active/recent sample
buffers and preserves the already serialized conservative tuple instead of allowing the old full-circuit aggregator to blend
leg samples later. Construction/test invalidation clears all leg histories, while an ordinary close preserves completed
history but clears interrupted active samples so a close/reopen cannot join two journeys.

## Maze handling

The Maze is a useful edge case because it does not use vehicle test measurements. The previous descriptor treated it as a flat ride with base ratings and then added size and scenery bonuses in post, which meant maze pieces could create stats without a rider actually traversing them.

The Maze now bypasses the flat-ride base/modifier path and uses the aggregate finalizer. Its descriptor keeps its historical base fields for compatibility with the descriptor shape, but the aggregate path does not read them for mazes.

Maze sampling happens in `Guest::updateRideMazePathfinding()`:

- each active maze guest can claim an active sample slot while it pathfinds
- each hedge or entrance/exit step contributes raw excitement and intensity
- step contribution is based on local choice complexity, nearby scenery/path context, and whether the step exits the maze
- the sample is published once the guest reaches the exit, then folded into the last-twenty-sample rolling average
- high traffic does not inflate ride stats because completed paths are averaged for display rather than summed ride-wide
- editing maze track clears active and recent samples so the next rider traversal retests the changed layout

`BonusMazeSize` and the old maze scenery modifier were removed from the Maze descriptor. Size now matters only when it produces more sampled rider steps.

## Modifier migration

The aggregate path treats old ride-wide modifiers differently:

- Track length, duration, speed, G-force, turns, drops, shelter, synchronization, proximity, and scenery bonuses are skipped in the post modifier pass because those effects are already present in sampled ticks.
- Additive train length, reversals, holes, train count, operation options, downward launch, launched freefall mode, go-kart race, and similar legacy ride-wide bonuses are skipped until they can be represented as sampled tick effects.
- Pure multipliers, such as reversed-train and ride-entry multipliers, can only scale existing sampled raw totals; they cannot create stats from zero. Ride-entry multipliers use the legacy `value * multiplier >> 7` scale and are applied to raw totals before the square-root finalizer, which is equivalent to applying the same vehicle-object bonus to every sampled tick for a ride.
- Requirements are translated into raw-level divisors or gates with comments in `RideRatingsRawApplyRequirement()`, so the old hardcoded requirements are documented where the formula now acts.
- The existing non-aggregate path remains for flat rides, stalls, and other non-aggregate rating types. Aggregate-rated normal rides and mazes without samples no longer use the legacy fallback path.

## Functions touched

- `src/openrct2/ride/Ride.h`: `RideRatingAccumulator`, `Ride::ratingAccumulator`, `Ride::activeRatingSamples`, `Ride::recentRatingSamples`.
- `src/openrct2/entity/Guest.cpp`: `RideRatingGetMazeLocalContextScore`, `RideRatingAccumulateMazeStep`, `Guest::updateRideMazePathfinding`.
- `src/openrct2/actions/ride/MazePlaceTrackAction.cpp`: clears sampled maze ratings after non-ghost maze placement.
- `src/openrct2/actions/ride/MazeSetTrackAction.cpp`: clears sampled maze ratings after non-ghost maze edits.
- `src/openrct2/ride/Vehicle.cpp`: `RideRatingTickIsSheltered`, `RideRatingGetLocalContextScore`, `RideRatingAccumulateTick`, `RideRatingUpdateLiveTrainSample`, `Vehicle::UpdateMeasurements`, `test_finish`, `test_reset`.
- `src/openrct2/ride/Vehicle.Station.cpp`: `RideRatingPublishTrainSample`, `Vehicle::UpdateUnloadingPassengers`.
- `src/openrct2/ride/Ride.cpp`: active/recent rating sample helpers, `InvalidateTestResults`.
- `src/openrct2/ride/RideRatings.h` and `src/openrct2/ride/RideRatings.cpp`: source curves for airtime, vertical, lateral, and longitudinal G; profile and speed/G coupling application; ride-entry multipliers; completed-sample recording; and aggregate finalization.
- `src/openrct2/ride/rtd/gentle/Maze.h`: removes post-hoc maze size and scenery bonuses from the Maze descriptor.
- `test/tests/RideRatings.cpp` and `test/tests/testdata/ratings/*.txt`: update the fixture expectations for aggregate-rated rides with no samples; the helper can regenerate fixtures with `OPENRCT2_UPDATE_RIDE_RATINGS=1`.

## Verification notes

The project builds with `/WX` using the local Windows build command. The ride-rating fixture tests now assert that aggregate-rated saved-park rides with no live/test samples do not receive legacy base stats. Focused G-force tests assert that airtime is exciting, negative vertical G is nastier than airtime, positive vertical G mostly feeds intensity, and lateral G grows superlinearly.
