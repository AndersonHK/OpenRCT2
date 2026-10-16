# Go Kart experience calibration - 2026-10-16

> Historical record. Status, instructions and acceptance claims below apply to the recorded checkpoint. Start with the [current ratings guide](../../ratings/README.md) for ongoing work.


**Historical exploration.** The [equal-length multiplier recommendation](go-kart-multiplier-recommendation.md) supersedes
this report's recommendation. Equal-duration comparisons below cover different track lengths. The duration falloff and
bespoke model are not the current plan; they remain documented as unshipped experiments.

## Status and conclusion

This was a local scoring experiment against the then-installed `2.7` speed,
`2.7` lateral G, and `0.9` remaining-coefficient profile. The later selected speed/longitudinal-only follow-up is recorded in
[the multiplier recommendation](go-kart-multiplier-recommendation.md). The experimental kart perception model lives only in
`scripts/ratings/sample-vehicle-ratings.cpp`. No park, object definition, network protocol or save format was changed.

The existing generic curves reward steady high speed, decoration accumulation, sustained hard corners and coaster-style
airtime. Adjusting seven source multipliers changes their relative importance but cannot independently change enjoyment
and discomfort, recognize rapid throttle oscillation, or bound the benefit of an extremely long trip.

The preferred experimental candidate is `sustained_v4`: enjoyable driving comes primarily from manageable acceleration,
braking and cornering, with modest atmosphere and speed rewards. Airborne/harsh ticks suppress enjoyment and retain
discomfort. A brief signed acceleration history rejects tiny rapid oscillations. Diminishing excitement after one minute
prevents a ten-minute straight from beating a varied one-minute circuit in these probes.

These are subjective design targets, not measured human preferences. Synthetic inputs do not establish reachable vehicle
physics, live-game balance, frame pacing, or multiplayer correctness. This candidate is ready for implementation review,
not a claim that a new game model has been deployed.

## Player-facing intent

- Reward a circuit with corners that have useful approaches and exits, short acceleration zones and some recovery time.
- Speed should feel worthwhile, but doubling steady speed should not double the fun; steady cruising should add little nausea.
- Sustained acceleration out of a corner is more interesting than an endless straight. Extremely rapid changes should not
  earn repeated acceleration rewards merely because the instantaneous acceleration is nonzero.
- Moderate sideways load suggests engaging cornering. Sustained severe load should produce discomfort rather than a large
  excitement score. A constant-circle control should trail the varied circuit at equal duration.
- Mild terrain changes can add variety, but zero/negative vertical G and hard landings in an open kart should not receive
  the coaster airtime excitement reward. Reducing an airtime multiplier alone also reduces its discomfort, which is the
  wrong tradeoff for this target.
- Theming should improve atmosphere without making dense scenery or an elevated track the best driving experience.
- A longer trip can add enjoyment with diminishing returns, while prolonged exposure still adds intensity/nausea.

## Method and reproducibility

From a Release x64 checkout with dependencies and the core libraries already built:

```powershell
python scripts/ratings/kart-calibration.py
```

The script builds a separate native adapter using the repository's CLI project settings and MSVC `14.44.35207`, without
rebuilding or editing game projects. Production controls call the actual compiled `RideRating` speed, force and local-context
helpers; experimental candidates call the separate prototype. Inputs, native raw outputs, all candidate results, component
breakdowns, SHA256 receipts and the generated adapter project are written under ignored `.tmp/kart-calibration/`.

The source of each output is recorded as speed, forces, context and track-feature raw E/I/N, plus the total after the
prototype's comfort/duration adjustments. The adapter's unbanked-turn and descent constants mirror `Vehicle.cpp`; these
are simple descriptor probes, not a traversal of map geometry. Scenery and height inputs are already-classified context
points, so this study does not revalidate visibility, map scanning or decoration placement.

Every ordinary scenario is 60 seconds at 40 ticks/second. Longer controls are explicitly labelled. A flowing circuit repeats:
12 to 24 mph over 2 seconds; 3 seconds at 24 mph; braking to 12 mph over 1 second; 4 seconds turning at 12 mph and 0.6 lateral G.
The gentle variation uses 9 to 18 mph and 0.45 lateral G. Longitudinal G is calculated from consecutive fixed-point speed
samples using the game's integer formula, rather than assigning a physically inconsistent constant acceleration to a
constant-speed straight. Vertical/lateral loads are intentionally synthetic. The 36 mph, severe-force and negative-G
controls are stress cases, not claims about normal stock kart performance.

Internal rating speed is `abs(velocity) >> 16`; one unit corresponds to 2.25 displayed mph before display rounding. The
generic baseline `90` therefore corresponds to about 202.5 mph. In particular, generic speed/G coupling substantially
suppresses ordinary kart acceleration. Raising its multiplier enough to compensate exposes high-force exploits.

Displayed values use neutral ride-object multipliers and the existing square-root conversion. Each row represents one
completed synthetic trip, or the steady rolling result if all twenty recent trips were identical. Startup filtering begins
from zero. The Python-generated fractional filter history is checked against the native integer update for every distinct
request. Actual parked/station samples, opponents and vehicle collision physics are outside this experiment.

## Iterations

1. **Deployed control:** `0.9 / 2.7 / 0.9 / 0.9 / 2.7 / 0.9 / 0.9` for context, speed, longitudinal, vertical, lateral,
   airtime and coupling respectively.
2. **Coefficient-only trials:** reduce speed/context, raise acceleration, and reduce airtime. The `driver` trial makes
   harsh alternating brake/throttle score 9.36 excitement. It also still rewards severe constant cornering and hard landings.
3. **Kart-specific source curves:** saturating speed reward, moderate corner preference, separate vertical discomfort,
   bounded atmosphere, and a per-tick enjoyment reduction for harsh forces. `kart_v2` achieves the intended basic rankings,
   but small 18-to-18.3 mph oscillations every 0.05 seconds score 4.52 excitement, exceeding the flowing circuit's 3.05.
4. **Sustained acceleration:** add a 20-tick signed low-pass filter and a smooth low-force onset. `sustained_v3` reduces that
   tiny oscillation to 1.39 excitement, equal to steady 18 mph cruising, while the flowing circuit remains 3.00.
5. **Diminishing duration:** after 2400 sampled ticks, scale each extra excitement contribution by
   `(2400 / elapsedTicks)^2`. Intensity/nausea remain accumulated. A constant stimulus has a limiting excitement exposure
   equivalent to two minutes, before the final square root. `sustained_v4` brings the ten-minute straight from 4.67 to 2.03;
   the deployed generic control is 6.24.

## Preferred candidate values

These values apply to the **prototype curves**. Copying them into the current game's generic profile would produce a
different result and is not recommended.

| Source | Coefficient | Interpretation |
| --- | ---: | --- |
| Local context | 0.15 | Modest atmosphere; combined atmosphere capped at 36 classifier points |
| Speed | 0.50 | Saturating excitement; ordinary cruise has little nausea |
| Longitudinal G | 1.20 | Sustained acceleration/braking enjoyment; instantaneous force still determines discomfort |
| Lateral G | 0.60 | Best enjoyment at moderate load; severe load raises discomfort |
| Vertical G | 1.20 | Compression and negative G contribute discomfort, no positive excitement |
| Airtime | 1.20 | Loss of contact contributes discomfort, no positive excitement |
| Speed/G coupling | 0 | Prototype scores physical force directly; generic double speed scaling is bypassed |
| Acceleration history | 20 ticks | 0.5-second signed filter; fractional hundredths of G retained at scale 1024 |
| Extra-duration falloff | after 60 seconds | Further excitement ticks weighted by inverse elapsed-time squared |

For speed ratio `r = internalSpeed / 12`, unweighted speed raw units per tick are
`E = 4r/(1+r)`, `I = 0.4r^2`, `N = 0.03r^2`. The source coefficient applies before accumulation.
The lateral enjoyment curve is `4g/(g+0.25) * exp(-(g/0.85)^4)`. Longitudinal enjoyment uses
`g^2/(g^2+0.08^2)`, multiplied by 12 for acceleration or 6.6 for braking, then tapered with
`exp(-(g/0.40)^4)` or `exp(-(g/0.65)^4)` respectively. Here longitudinal enjoyment uses the sustained signed signal,
clamped to the current actual force with matching sign; it cannot keep rewarding acceleration after the vehicle is cruising.
The adapter records the remaining discomfort and comfort-gate formulas explicitly alongside these curves.

## Results

All cells below are excitement / intensity / nausea. The full seven-candidate component data is in
`docs/archive/ratings/go-kart-calibration-results.csv`; complete generated inputs and native-output evidence are in `.tmp/kart-calibration/`.

| Synthetic experience | Deployed generic model | Preferred prototype |
| --- | ---: | ---: |
| straight_18mph | 1.61 / 3.60 / 2.55 | 1.39 / 0.46 / 0.12 |
| straight_24mph | 1.97 / 4.02 / 2.85 | 1.48 / 0.58 / 0.15 |
| straight_36mph | 3.01 / 5.09 / 3.60 | 1.66 / 0.92 / 0.25 |
| flowing_circuit | 1.62 / 3.44 / 2.45 | 3.00 / 0.78 / 0.46 |
| gentle_circuit | 1.39 / 3.04 / 2.16 | 2.78 / 0.61 / 0.37 |
| stop_and_go | 1.05 / 2.33 / 1.65 | 2.13 / 1.80 / 1.15 |
| one_start_then_cruise | 1.93 / 3.96 / 2.80 | 1.87 / 0.57 / 0.16 |
| flowing_scenery | 2.27 / 3.44 / 2.45 | 3.07 / 0.78 / 0.46 |
| straight_scenery | 2.71 / 4.02 / 2.85 | 1.66 / 0.58 / 0.15 |
| straight_scenery_spam | 4.60 / 4.02 / 2.85 | 1.66 / 0.58 / 0.15 |
| straight_high | 2.64 / 4.39 / 2.93 | 1.48 / 0.92 / 0.29 |
| constant_corner_0.6G | 1.72 / 3.66 / 2.64 | 2.29 / 1.04 / 0.75 |
| constant_corner_0.4G | 1.68 / 3.65 / 2.62 | 2.34 / 0.89 / 0.67 |
| constant_corner_1.2G | 2.61 / 4.36 / 3.31 | 1.29 / 1.94 / 1.32 |
| constant_corner_2.5G | 4.30 / 8.46 / 6.20 | 0.00 / 5.14 / 3.52 |
| alternating_brake_throttle | 1.89 / 3.55 / 2.55 | 1.45 / 7.12 / 4.79 |
| gentle_throttle_oscillation | 1.61 / 3.60 / 2.55 | 1.39 / 0.65 / 0.25 |
| repeated_airtime_landings | 2.15 / 4.10 / 2.87 | 1.32 / 2.27 / 1.56 |
| repeated_negativeG_landings | 2.46 / 4.32 / 3.01 | 1.32 / 3.94 / 2.91 |
| straight_120seconds | 2.79 / 5.69 / 4.02 | 1.81 / 0.82 / 0.22 |
| straight_600seconds | 6.24 / 12.73 / 9.00 | 2.03 / 1.83 / 0.49 |

## Validation and implementation boundary

- 37,798 distinct native requests; seven profiles across 21 synthetic scenarios.
- Twelve explicit design checks passed: the flowing circuit beats five simpler/exploit controls; four harsh cases are less
  enjoyable and more intense/nauseating than the 24 mph straight; decoration saturates; height gives no excitement bonus;
  and tiny oscillation gives no extra excitement at displayed precision.
- Rebuilt the retained production baseline in Release x64 with zero warnings/errors; all 99 existing ride-rating tests passed.
- Installed GUI and CLI SHA256 hashes still match the previous deployment receipt. This study did not deploy its prototype.
- The 101-test exploratory game build was temporary; its prototype source edits were removed before the retained baseline
  was rebuilt. The final game changes remain those from the prior coefficient-only request.

The signed filter would need ride-owned per-vehicle state, reset at sample boundaries and saved/restored for deterministic
network joins and save continuation. Integrating it requires round-trip and multiplayer-state tests. The existing sampled
tick count can drive the duration falloff without a new history field. Live gameplay should then establish whether stock
karts actually visit the useful force/speed ranges and whether the track designs encouraged by the score look and feel right.

The prototype does not understand overtaking, racing position, voluntary versus forced braking, or overall layout rhythm.
A carefully timed repeated maneuver may still exploit it; passing these selected probes is not proof against every optimizer.
The atmosphere cap and one-minute falloff are deliberate taste choices that need park-level testing. Existing recent rating
samples would also retain the previous model until replaced or explicitly invalidated during a future rollout.

[Library](../../README.md) · [Collection](README.md)
