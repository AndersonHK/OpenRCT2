# Ride rating aggregate rationale

This change moves excitement, intensity, and nausea away from a purely post-test calculation and into raw per-tick samples gathered while vehicles test and while guests actually ride.

## Model

Each sampled vehicle tick contributes raw excitement, intensity, and nausea from:

- current track element descriptor and special track element type
- current velocity
- vertical and lateral G forces
- whether the sampled vehicle position is sheltered or underground
- local scenery, path, and nearby-ride context around the sampled tile
- synchronized station operation

Raw totals are stored in active rider/train/test samples. Completed samples are copied into a rolling cache of the last twenty rider/train/test results, and the displayed rating is calculated from the average raw sample before applying the geometric curve:

```text
rating = sqrt(raw / divisor) * 100
```

The initial divisor is `1000`, so doubling raw accumulated stats produces roughly `sqrt(2)` final growth, or about 40% higher final ratings. This is intentionally a first tuning point: the model now has the requested shape, while exact per-ride balance can be tuned by adjusting the per-tick weights and divisor.

The aggregate path does not seed ratings from `RideRatingsDescriptor::BaseRatings`. If an aggregate-rated ride has no samples, it displays zero aggregate stats rather than falling back to hidden base stats.

## Function flow

- `RideRatingAccumulator` in `src/openrct2/ride/Ride.h` stores raw totals and tick count for one active or completed sample.
- `Ride::activeRatingSamples` tracks currently running guest/train samples, and `Ride::recentRatingSamples` stores the last twenty completed samples used for display smoothing.
- `RideGetOrCreateActiveRatingSample()`, `RideAddRecentRatingSample()`, and `RideGetRecentRatingAccumulator()` in `src/openrct2/ride/Ride.cpp` manage active samples and the rolling average.
- `Vehicle::UpdateMeasurements()` in `src/openrct2/ride/Vehicle.cpp` samples the train's current per-car track pieces and G forces during test runs.
- `test_finish()` in `src/openrct2/ride/Vehicle.cpp` publishes the completed test accumulator into the rolling sample cache.
- `RideRatingUpdateLiveTrainSample()` in `src/openrct2/ride/Vehicle.cpp` samples normal passenger trains during live operation.
- `RideRatingPublishTrainSample()` in `src/openrct2/ride/Vehicle.Station.cpp` publishes a train sample when the train unloads.
- `RideRatingAccumulateTick()` converts that tick state into raw excitement, intensity, and nausea.
- `RideRatingTickIsSheltered()` and `RideRatingGetLocalContextScore()` move shelter, scenery, path, and nearby-ride effects into the sampled tick path.
- Train samples average the per-tick contributions of every vehicle on the train before adding that tick to the active train sample, so back cars and middle cars affect ratings without multiplying the ride duration.
- `test_reset()` and `InvalidateTestResults()` clear test and rider samples whenever test data is reset.
- `RideRating::RecordRiderSample()` in `src/openrct2/ride/RideRatings.cpp` records completed rider/train/test samples and immediately recalculates the displayed rating.
- `RideRatingsCalculate()` in `src/openrct2/ride/RideRatings.cpp` uses aggregate finalization for normal rides and mazes, preferring the rolling sample cache and falling back only to an in-progress formal test accumulator.
- `RideRatingsCalculateAggregated()` finalizes raw totals through the square-root curve.

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
- Pure multipliers, such as reversed-train and ride-entry multipliers, can only scale existing sampled raw totals; they cannot create stats from zero.
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
- `src/openrct2/ride/RideRatings.cpp`: `RideRating::RecordRiderSample`, `RideRatingsCalculate`, `RideRatingsRawToRating`, `RideRatingsRawDivide`, `RideRatingsRawApplyRideEntryMultipliers`, `RideRatingsRawApplyRequirement`, `RideRatingsRawApplyModifiers`, `RideRatingsCalculateAggregated`.
- `src/openrct2/ride/rtd/gentle/Maze.h`: removes post-hoc maze size and scenery bonuses from the Maze descriptor.
- `test/tests/RideRatings.cpp` and `test/tests/testdata/ratings/*.txt`: update the fixture expectations for aggregate-rated rides with no samples; the helper can regenerate fixtures with `OPENRCT2_UPDATE_RIDE_RATINGS=1`.

## Verification notes

The project builds with `/WX` using the local Windows build command. The ride-rating fixture tests now assert that aggregate-rated saved-park rides with no live/test samples do not receive legacy base stats.
