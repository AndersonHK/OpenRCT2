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

Standalone vehicle speed uses a power `1.5` excitement curve for every sampled vehicle ride, including Boat Hire. The curve is normalized at speed `90`, so the established baseline remains unchanged while faster vehicles gain progressively more excitement. Intensity and nausea remain linear:

| Rating | Coefficient |
| --- | ---: |
| Excitement | `90 * pow(speed / 90, 1.5) * rawScale / 5` |
| Intensity | `speed * rawScale / 4` |
| Nausea | `speed * rawScale / 8` |

There is no constant excitement term. Ride profiles multiply these raw values before accumulation.

### G-force curves

The tuned curves use lower amplitudes and higher powers. Mild forces therefore contribute less, while sustained high-force track separates itself more strongly after speed coupling.

| Source | Excitement | Intensity | Nausea | Direction |
| --- | ---: | ---: | ---: | --- |
| Airtime, `0G..1G` below neutral | `42 * G^2.15` | `11.5 * G^2.25` | `3.4 * G^1.85` | full airtime remains exciting |
| Negative vertical G | `28 * G^2.50` | `64 * G^2.80` | `18 * G^2.50` | restraint force compounds rapidly |
| Excess negative vertical G | none | `160 * G^3.00` | none | intensity-only excess term |
| Normal positive vertical G | `20 * G^4.25` | `16 * G^4.75` | `7 * G^3.50` | suppress mild load, reward strong valleys |
| Excess positive vertical G | `7.5 * G^2.75` | `84 * G^3.15` | none | separate over-limit term |
| Lateral G | `10.5 * G^3.25` | `7.5 * G^4.25` | `9 * G^3.40` | mild turns fall away quickly |
| Severe lateral G | none | `112 * G^3.15` | `34 * G^2.80` | compounds beyond `2G` |
| Positive longitudinal G | `16 * G^2.20` | `5.5 * G^2.35` | `4.5 * G^2.45` | acceleration favors excitement |
| Negative longitudinal G | `7 * G^2.15` | `7 * G^2.50` | `8 * G^2.60` | braking favors discomfort |

Longitudinal G is calculated from consecutive realised head-train speed magnitudes, so direct launches and brakes are included while constant-speed vertical and lateral forces do not leak into it.

### Ride profiles and speed/G coupling

Profile coefficients use per-mille integers where `1000` is `1.0`.

| Profile | Local | Speed | Longitudinal | Vertical | Lateral | Airtime | Coupling |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Default | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 |
| Roller coaster | 1000 | 300 | 1000 | 2000 | 500 | 1000 | 500 |
| Go Karts | 1000 | 3000 | 1000 | 1000 | 3000 | 1000 | 1000 |

After source multipliers, all G channels use `r * max(0, 1 + c * (r - 1))`, where `r = speed / 90` and `c` is the coupling coefficient. At default coupling, speeds `45`, `90`, and `180` produce per-tick factors `0.25`, `1`, and `4`.

### Track features

Values are raw-scale units per sampled tick before speed normalization. For example, `0.8` means
`0.8 * RideRating::kRideRatingAccumulatorRawScale`.

| Source | Excitement | Intensity | Nausea | Direction |
| --- | ---: | ---: | ---: | --- |
| Unbanked turn | `2 -> 0.8` | `4 -> 1.6` | `4 -> 1.6` | down by 2.5 |
| Banked turn | `3 -> 1.2` | `2 -> 0.8` | `2 -> 0.8` | down by 2.5 |
| Sloped turn | `1.2` | none | `1.2` | modest feature bonus |
| Helix | `4.0` | `3.2` | `4.0` | restored special-track weight |
| Normal-to-inversion transition | `6.4` | `7.2` | `4.8` | strong loop/inversion transition |
| Downward track flag | `2.0` | `1.2` | none | repeated drop contribution |
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
| Normal excitement or intensity context point | `1000` | `400` | restrained scenery/path weight |
| Near-miss, loop, vertical-track, or height-exposure point | `400` | `800` | restore structural ride interaction |
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

Boat Hire shares the vehicle speed source. Its bespoke free-roam turn source and local-context conversion remain separate tuning knobs.

| Source | Previous coefficient | Tuned coefficient | Direction |
| --- | ---: | ---: | --- |
| Boat Hire speed score | `speed^2 * rawScale / 90` | shared normalized `speed^1.5` source | reworked |
| Boat Hire local-context excitement point | `1000` | unchanged | preserve |
| Boat Hire local-context intensity point | `1000`, foreign side track `500` | unchanged | preserve |
| Boat Hire vertical-context nausea point | `1000 / 3` | unchanged | preserve |
| Free-roam unbanked turn excitement over two ticks | `1000` | `1600` | up by 60% |
| Free-roam unbanked turn intensity over two ticks | `2000` | unchanged | preserve |
| Free-roam unbanked turn nausea over two ticks | `2000` | unchanged | preserve |

## Tuning plan

1. Keep the final square-root curve and aggregate divisor fixed.
2. Keep mild-force amplitudes low and use curve powers to separate genuinely strong force from ordinary movement.
3. Tune tracked vehicles at their sources: profile coefficients, G curves, track-feature raw units, and local-context conversion.
4. Reduce maze raw output by halving each maze step coefficient individually, including local-context point conversion.
5. Raise only the Boat Hire free-roam excitement coefficient, leaving Boat Hire intensity, nausea, speed, and local context unchanged.
6. If later tuning is needed, edit the manifest row's source coefficient rather than adding a category-wide scalar.
