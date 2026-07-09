# Ride rating stat source manifest

This document lists the sampled aggregate-rating sources for rollercoasters and other tracked vehicles, mazes, and Boat Hire.
The intent is to tune the source coefficients directly. Do not add a post-aggregate scalar when a source-level coefficient can be
adjusted instead.

The final aggregate curve is still:

```text
displayed rating = sqrt((raw / 1000) / 1000) * 100
```

The first `/ 1000` removes `RideRating::kRideRatingAccumulatorRawScale`; the second `/ 1000` is
`kAggregatedRideRatingDivisor`. The knobs below operate before that final square-root curve.

## Rollercoaster and tracked-vehicle manifest

These sources are sampled from `RideRatingAccumulateTick()` in `src/openrct2/ride/Vehicle.cpp`, with the numeric helper
functions in `src/openrct2/ride/RideRatings.cpp`.

### Vehicle speed

| Source | Previous coefficient | Tuned coefficient | Direction |
| --- | ---: | ---: | --- |
| Speed score for excitement, intensity, nausea | `speed^2 * rawScale / 90` | `speed^2 * rawScale / 225` | down by 2.5 |

Boat Hire uses its own speed function and keeps the previous `/ 90` coefficient.

### G-force curves

Exponents are intentionally unchanged; only the curve amplitudes move.

| Source | Excitement | Intensity | Nausea | Direction |
| --- | ---: | ---: | ---: | --- |
| Airtime, `0G..1G` below neutral | `115 -> 46` | `32 -> 12.8` | `9 -> 3.6` | down by 2.5 |
| Negative vertical G | `80 -> 32` | `180 -> 72` | `50 -> 20` | down by 2.5 |
| Excess negative vertical G | none | `450 -> 180` | none | down by 2.5 |
| Normal positive vertical G | `62 -> 24.8` | `48 -> 19.2` | `20 -> 8` | down by 2.5 |
| Excess positive vertical G | `22 -> 8.8` | `240 -> 96` | none | down by 2.5 |
| Lateral G | `32 -> 12.8` | `22 -> 8.8` | `25 -> 10` | down by 2.5 |
| Severe lateral G | none | `320 -> 128` | `95 -> 38` | down by 2.5 |

### Track features

Values are raw-scale units per sampled tick before speed normalization. For example, `0.8` means
`0.8 * RideRating::kRideRatingAccumulatorRawScale`.

| Source | Excitement | Intensity | Nausea | Direction |
| --- | ---: | ---: | ---: | --- |
| Unbanked turn | `2 -> 0.8` | `4 -> 1.6` | `4 -> 1.6` | down by 2.5 |
| Banked turn | `3 -> 1.2` | `2 -> 0.8` | `2 -> 0.8` | down by 2.5 |
| Sloped turn | `2 -> 0.8` | none | `2 -> 0.8` | down by 2.5 |
| Helix | `5 -> 2.0` | `4 -> 1.6` | `5 -> 2.0` | down by 2.5 |
| Normal-to-inversion transition | `8 -> 3.2` | `9 -> 3.6` | `6 -> 2.4` | down by 2.5 |
| Downward track flag | `2 -> 0.8` | `1 -> 0.4` | none | down by 2.5 |
| Spinning tunnel | `4 -> 1.6` | `3 -> 1.2` | `6 -> 2.4` | down by 2.5 |
| Rapids or water splash | `5 -> 2.0` | `3 -> 1.2` | `2 -> 0.8` | down by 2.5 |
| Waterfall | `5 -> 2.0` | `2 -> 0.8` | none | down by 2.5 |
| Whirlpool | `4 -> 1.6` | `2 -> 0.8` | `3 -> 1.2` | down by 2.5 |
| Log flume reverser | `4 -> 1.6` | `5 -> 2.0` | `6 -> 2.4` | down by 2.5 |
| Sheltered or underground tick | `2 -> 0.8` | `1 -> 0.4` | `1 -> 0.4` | down by 2.5 |
| Synchronized dispatch tick | `3 -> 1.2` | `1 -> 0.4` | `1 -> 0.4` | down by 2.5 |

### Local context

The local-context classifier still produces the same `LocalContextScore` units. Only the tracked-vehicle conversion from
context points to raw per-tick score changes.

| Source | Previous raw per context point | Tuned raw per context point | Direction |
| --- | ---: | ---: | --- |
| Excitement context point | `1000` | `400` | down by 2.5 |
| Normal and vertical intensity context point | `1000` | `400` | down by 2.5 |
| Foreign-track side intensity point | `500` | `200` | down by 2.5 |
| Non-vertical nausea context point | `1000` | `400` | down by 2.5 |
| Vertical interaction and height-exposure nausea point | `1000 / 3` | `1000 * 2 / 15` | down by 2.5 |

The classifier inputs are unchanged: scenery, path proximity, bridge, near miss, path-inside-loop, same-tile foreign track,
same-tile own track, and height exposure still decide the same local-context point values.

## Maze manifest

Maze scoring is sampled per guest step in `RideRatingAccumulateMazeStep()` in `src/openrct2/entity/Guest.cpp`.
Each explicit step coefficient is halved directly.

| Source | Previous coefficient | Tuned coefficient | Direction |
| --- | ---: | ---: | --- |
| Base excitement for entering a maze tile | `20` | `10` | down by 2 |
| Excitement per available forward choice | `12` | `6` | down by 2 |
| Local-context excitement point | `1.0` | `0.5` | down by 2 |
| Exit-step excitement | `90` | `45` | down by 2 |
| Base intensity for entering a maze tile | `4` | `2` | down by 2 |
| Dead-end or forced-turn intensity | `5` | `2.5` | down by 2 |
| Local-context intensity point | `1.0` | `0.5` | down by 2 |
| Exit-step intensity | `4` | `2` | down by 2 |
| Local-context nausea point | `1.0` | `0.5` | down by 2 |

## Boat Hire manifest

Boat Hire keeps the pre-tuning vehicle speed and local-context conversion so the rollercoaster reduction does not pull it down.
Its bespoke free-roam turn source is the primary excitement tuning knob.

| Source | Previous coefficient | Tuned coefficient | Direction |
| --- | ---: | ---: | --- |
| Boat Hire speed score | `speed^2 * rawScale / 90` | unchanged | preserve |
| Boat Hire local-context excitement point | `1000` | unchanged | preserve |
| Boat Hire local-context intensity point | `1000`, foreign side track `500` | unchanged | preserve |
| Boat Hire vertical-context nausea point | `1000 / 3` | unchanged | preserve |
| Free-roam unbanked turn excitement over two ticks | `1000` | `1600` | up by 60% |
| Free-roam unbanked turn intensity over two ticks | `2000` | unchanged | preserve |
| Free-roam unbanked turn nausea over two ticks | `2000` | unchanged | preserve |

## Tuning plan

1. Keep the final square-root curve and aggregate divisor fixed.
2. Keep G-force curve powers fixed because their shape is currently acceptable.
3. Reduce tracked-vehicle raw sources by changing the source coefficients themselves: speed, G-force amplitudes, track-feature
   raw units, and local-context point-to-raw conversion.
4. Reduce maze raw output by halving each maze step coefficient individually, including local-context point conversion.
5. Raise only the Boat Hire free-roam excitement coefficient, leaving Boat Hire intensity, nausea, speed, and local context unchanged.
6. If later tuning is needed, edit the manifest row's source coefficient rather than adding a category-wide scalar.
