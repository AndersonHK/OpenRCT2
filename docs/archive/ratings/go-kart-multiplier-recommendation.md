# Go Kart multiplier recommendation - equal-length comparison

> Historical record. Status, instructions and acceptance claims below apply to the recorded checkpoint. Start with the [current ratings guide](../../ratings/README.md) for ongoing work.


## Selected follow-up

The user selected only speed `1.5` and longitudinal G `1.0` for deployment. Context stays `0.9`, vertical G `0.9`, lateral G
`2.7`, airtime `0.9`, and coupling `0.9`. The broader recommendation below remains deferred. Tables and the experiment's
`current` profile label preserve the historical baseline used in the comparison; they are not live installation metadata.

This follow-up was built in Release x64 with zero warnings/errors, passed all 99 ride-rating tests, and was deployed to
`D:\Games\Independent\OpenRCT2Mod`. Both installed executable SHA256 hashes match the build; the runtime-data mirror
comparison reported no differences. The installed CLI version smoke check passed. Receipts are in
`.tmp/kart-speed-longitudinal-20261016/verification.json` alongside the build, test and deployment logs.

## Scope and correction

This supersedes the recommendation in [the earlier equal-duration experiment](go-kart-experience-calibration.md).
That experiment compared different distances and expanded into a bespoke scoring model. Its duration falloff is withdrawn
as a recommendation: a longer ride should generally earn more accumulated stats. The current request is a recommendation
using existing vehicle formulas, with only limited shared-formula suggestions. No game source, installed package or object
definition was changed in this pass.

## Recommended current-formula profile

| Source | Baseline tested | Recommended | Reason |
| --- | ---: | ---: | --- |
| Local context | 0.90 | **0.15** | Atmosphere remains useful; reduce decoration and height dominance |
| Speed | 2.70 | **1.50** | Retain speed as a substantial source without the kart-specific near-triple weighting |
| Longitudinal G | 0.90 | **1.00** | Normal weighting; large increases mostly reward abrupt acceleration/braking |
| Vertical G | 0.90 | **0.90** | Preserve existing compression/negative-G discomfort; no extra vertical thrill emphasis |
| Lateral G | 2.70 | **1.25** | Modest kart cornering emphasis rather than a large sustained-turn reward |
| Airtime | 0.90 | **0.10** | Greatly reduce coaster-style lightness reward, accepting the shared-channel limitation |
| Speed/G coupling | 0.90 | **0.90** | Keep unchanged: reducing this coefficient actually boosts force contributions below baseline speed |

In the existing per-mille representation these are `150, 1500, 1000, 900, 1250, 100, 900`.
They are a conservative starting point, not a mathematically optimal or empirically measured human-preference fit.
Unlike the earlier prototype's numbers, these values apply directly to the current `SampledRideRatingProfile`.

## Method

Run:

```powershell
python scripts/ratings/kart-length-calibration.py
```

The script uses the native adapter exclusively with `model=0`. It calls the compiled production speed, G-force and
local-context helpers. No experimental curves, comfort gates, acceleration filters or duration falloff participate.
The descriptor-only unbanked turn/down bonuses are the same copied constants documented by the earlier harness.

Each synthetic trace supplies speeds at 40 ticks/second. Longitudinal force follows consecutive realized fixed-point
velocities. Synthetic distance uses `GetRealRideLengthDelta`'s formula with acceleration set to that realized velocity
change, including the `158/213` ride-length scale. This is a distance estimate for the constructed trace, not a loaded
park's measured geometry; synthetic vertical/lateral G and preclassified scenery inputs are not a physics simulation.

The comparison scales raw source totals by `600 / traceMetres` before the final square-root conversion. This compares
equal 600 m portions with the same mix of driving conditions. It does not stretch time or change the acceleration magnitudes.
Fractional repetition of the trace is allowed; it is an average layout-composition comparison rather than a newly constructed
600 m circuit with exact station endpoints. Results use neutral object multipliers.

All nine candidate profiles passed the repeated-straight control: the 60-, 120- and 600-second straight traces produce
identical ratings when normalized to the same length. A separate length-growth table preserves the normal reward for
actually making the circuit longer. Source-level speed and G quantization are retained, so equal-length speed intensity and
nausea are close rather than perfectly identical at every nominal speed.

## Results at equal 600 m

All cells are excitement / intensity / nausea. Full nine-candidate results and per-metre source contributions are in
[go-kart-length-calibration-results.csv](go-kart-length-calibration-results.csv).

| Synthetic experience | Installed profile | Recommended profile |
| --- | ---: | ---: |
| Straight at 24 mph | 1.73 / 3.53 / 2.50 | 1.29 / 2.63 / 1.86 |
| Flowing circuit | 1.67 / 3.55 / 2.53 | 1.25 / 2.66 / 1.89 |
| Flowing circuit with normal scenery | 2.34 / 3.55 / 2.53 | 1.42 / 2.66 / 1.89 |
| Straight with extreme scenery | 4.04 / 3.53 / 2.50 | 1.97 / 2.63 / 1.86 |
| Elevated straight | 2.32 / 3.85 / 2.58 | 1.44 / 2.71 / 1.88 |
| Constant moderate cornering, 0.6 G | 1.75 / 3.71 / 2.68 | 1.32 / 2.79 / 2.03 |
| Constant severe cornering, 2.5 G | 3.78 / 7.42 / 5.44 | 2.64 / 5.18 / 3.80 |
| Violent brake/throttle alternation | 1.91 / 3.60 / 2.59 | 1.56 / 2.73 / 1.99 |
| Continuous zero-G stress probe | 2.13 / 3.59 / 2.52 | 1.36 / 2.64 / 1.87 |
| Repeated airtime and hard landings | 1.89 / 3.60 / 2.52 | 1.45 / 2.71 / 1.89 |

The equal-length control changes the original interpretation: the installed straight is 1.73 excitement and the flowing
circuit 1.67, already close. The recommendation makes them 1.29 and 1.25. Normal scenery raises the flowing circuit from
1.25 to 1.42, about 14%, versus 1.67 to 2.34, about 40%, under the installed profile.

Increasing only longitudinal weighting from 1.0 to 4.0 changes the flowing circuit from 1.25 to 1.26 excitement, but the
violent alternating brake/throttle trace from 1.56 to 2.21. Raising the multiplier does not efficiently reward ordinary
acceleration under these curves. Trials with lower speed/G coupling also increased harsh corner and braking rewards.

The recommendation intentionally does not force the flowing circuit to beat every control through a large compensating
coefficient. Extreme decoration still helps, severe lateral G still earns excitement as well as discomfort, and compression
after airtime still earns vertical excitement. These are limitations of the present formulas.

## Longer rides retain more stats

For the same steady 24 mph trace with the recommended profile:

| Synthetic length | Excitement | Intensity | Nausea |
| --- | ---: | ---: | ---: |
| 600 m | 1.29 | 2.63 | 1.86 |
| 1200 m | 1.83 | 3.73 | 2.63 |
| 2400 m | 2.58 | 5.27 | 3.73 |

Doubling a repeated layout doubles its raw totals and increases final ratings by approximately the square root of two.
This is the existing behavior. Length normalization is only an experimental comparison method, not a proposed game rule.

## Two optional shared-formula follow-ups

Neither is implemented or required to try the recommended profile.

1. **Soften only acceleration excitement's exponent**, tentatively from `2.2` toward `1.5`, preserving the value at 1 G and
   leaving intensity/nausea curves unchanged. That would give ordinary acceleration more relative weight without using a
   large multiplier that disproportionately rewards violent changes. It would affect all vehicle types and needs separate
   coaster/transport testing; it is not part of the numeric results above.
2. **Expose an optional object-level `airtimeExcitementMultiplier`**, defaulting to 1.0, in the shared source calculation.
   A kart object could use 0 while preserving airtime's intensity/nausea. The current `Airtime` profile coefficient scales all
   three channels together, so the recommended 0.10 also suppresses discomfort from lightness. An excitement-only modifier
   separates that tradeoff without a kart-specific scoring model. Object overrides should retain ride-type defaults when
   absent. If added, restore an appropriate ordinary airtime profile weight before applying the new excitement-only factor.

## Evidence and boundaries

- Nine profiles, 23 synthetic traces, 783 distinct native requests, and 54 repeated-length equality checks.
- Generated inputs, native scores, profile definitions, component totals, length-growth rows and SHA256 receipts:
  `.tmp/kart-length-calibration/`.
- Current production formulas only; no gameplay test or empirical claim about human preferences.
- The older prototype and its evidence remain available as historical exploration, not the current implementation plan.
- At the end of the experiment no game changes had been made. The later selected follow-up is recorded above.

[Library](../../README.md) · [Collection](README.md)
