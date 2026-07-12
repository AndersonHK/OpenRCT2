# Ride audio loudness research

## Scope

This note compares real-world kart and roller-coaster sound at a common distance, then audits the default RCT2 vehicle samples and OpenRCT2's current playback path. It is a calibration study, not a claim that a game should reproduce environmental-noise measurements literally. The intended result is a defensible relative hierarchy: recreational go-karts should sound mechanically present at close range, but a whole kart ride should not become disproportionately loud merely because it owns many more emitters than a coaster.

The local sample measurements were made on 2026-07-12 against the GOG RCT2 `CSS1.DAT` with SHA-256 `09ECBC1BF1DFB5781914EB71777B354586DF7D622DD3041CB55D8EE3EA00E02F`. No asset pack was enabled. Asset packs can replace these samples, so their replacements need the same analysis separately.

## Measurement caveats

Environmental sound reports use several different quantities:

- `LAeq` is energy averaged over a stated period. It is useful for a continuously operating ride or race but suppresses short pass-by peaks.
- `LAFmax` is the maximum fast-time-weighted level during an event. It better represents the moment a train or kart passes, but it must not be compared numerically with `LAeq` as though they were the same measurement.
- `LWA` is source sound power, independent of receiver distance. It is useful for comparing source classes but is not sound pressure at the listener.

Where a source supplied sound pressure at another distance, this note reports a free-field point-source normalization:

```text
L2 = L1 - 20 log10(r2 / r1)
```

The common receiver distance is 30.48 m (100 ft), matching the available coaster measurements. This arithmetic excludes barriers, reflections, ground absorption, wind, source directivity, and the fact that a long track is not a stationary point source. Normalized values are therefore comparison aids, not new field measurements.

## Real-world evidence

### Measurements normalized to 30.48 m

| Source and operating condition | Published measurement | Normalized to 30.48 m | Interpretation |
| --- | ---: | ---: | --- |
| Steel coaster, Vortex | 72-78 dBA `LAeq`; 83-87 dBA `LAFmax`, measured at 100 ft | unchanged | A loud steel coaster is both a sustained mechanical source and an intermittent pass-by source. |
| Wooden coaster, Gold Striker | 68-72 dBA `LAeq`; 77-81 dBA `LAFmax`, measured at 100 ft | unchanged | The measured wooden coaster was quieter than the two measured steel coasters at this receiver. This cautions against deriving loudness from track material alone. |
| Steel coaster, Flight Deck | 73-78 dBA `LAeq`; 85-90 dBA `LAFmax`, measured at 100 ft | unchanged | The upper end of the measured coaster set. |
| Kart event on a main straight | 84 dBA `LAeq`, measured at 15 m | about 77.8 dBA `LAeq` | This multi-kart event overlaps the upper end of the measured steel-coaster equivalent levels and exceeds the wooden-coaster result. |
| One POP 1-class racing kart, soft ground | 100.2 dBA `LAFmax`, measured at 7.5 m | about 88.0 dBA `LAFmax` | A racing kart pass-by overlaps loud steel-coaster maxima. |
| One POP 1-class racing kart, hard ground | 103.0 dBA `LAFmax`, measured at 7.5 m | about 90.8 dBA `LAFmax` | Ground condition alone changed the reported maximum by 2.8 dB. |

The coaster values come from the City of Santa Clara's Great America environmental review, which reports ride measurements at 100 ft. The kart event values come from a Christchurch City Council planning record. The single-kart test is from the Danish Environmental Protection Agency's controlled motorsport measurement work.

These data do **not** support a blanket rule that karts are quiet. Racing karts can be as loud as, or louder than, roller coasters at equal distance. They do support a narrower distinction that matters to OpenRCT2: racing karts and speed-limited recreational hire karts are not interchangeable acoustic sources.

### Hire kart versus racing kart

A Western Australia Environmental Protection Authority assessment gives maximum A-weighted sound-power data for both classes under the same modelling methodology:

| Kart class | Straight | Braking | Difference on straight |
| --- | ---: | ---: | ---: |
| 125 cc Rotax racing kart | 121 dBA `LWA` | 110 dBA `LWA` | baseline |
| Hire kart | 101 dBA `LWA` | 94 dBA `LWA` | 20 dB lower |

A 20 dB source-power difference corresponds to a tenfold pressure-amplitude ratio and a hundredfold acoustic-power ratio. The report's hire-kart octave bands are concentrated at low frequencies, whereas the racing kart retains much more 500 Hz to 8 kHz energy. The small, speed-limited, bumper-to-bumper vehicles represented by RCT-style go-karts are semantically closer to the hire-kart class than a 125 cc racing kart.

The practical hierarchy is consequently conditional:

1. A single recreational kart should generally be below a full coaster pass-by at equal distance.
2. A field of many recreational karts is legitimately louder than one kart, but its total must be calibrated as a ride, not by applying a coaster-train gain independently to every kart.
3. A racing-kart attraction would justify a substantially stronger and brighter source than the default recreational go-kart ride.

## Default RCT2 sample audit

`SoundId::goKartEngine` maps directly to CSS1 entry 21. The measurements below use decoded 22.05 kHz, 16-bit mono PCM before OpenRCT2's runtime gain, distance filter, pitch resampling, speaker routing, headroom, and limiter.

`RMS dBFS` measures unweighted sample energy. `A-RMS dBFS` is an approximate frequency-domain A-weighted RMS, useful here only for relative comparison. `Edge / diff RMS` compares the final-to-first sample jump with the RMS of ordinary adjacent-sample differences: values much greater than 1 indicate an unusually severe raw loop seam.

| Sample | Duration | RMS dBFS | Peak dBFS | Approx. A-RMS dBFS | Energy below 500 Hz | Edge / diff RMS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic wooden track friction | 1.415 s | -12.18 | -0.36 | -13.85 | 32.3% | 0.27 |
| Classic friction | 1.449 s | -15.79 | -3.24 | -17.85 | 41.1% | 0.21 |
| **Go-kart engine** | **0.266 s** | **-15.73** | **-1.94** | **-25.19** | **94.4%** | **0.46** |
| Train track friction | 2.137 s | -18.05 | 0.00 | -20.93 | 57.8% | 2.24 |
| Water track friction | 1.893 s | -17.51 | -3.86 | -17.91 | 14.2% | 0.21 |
| Wooden track friction | 1.297 s | -13.84 | -2.56 | -14.46 | 13.9% | 0.26 |
| B&M track friction | 2.345 s | -15.86 | -4.28 | -17.94 | 35.4% | 0.00 |

The kart sample is not abnormally hot in isolation. It is 3.55 dB below the classic wooden friction loop in unweighted RMS, essentially equal to classic friction, and 2.32 dB above train friction. It is also not digitally clipped, and its direct loop-edge jump is modest relative to ordinary sample-to-sample changes.

Two properties are exceptional:

- At 266 ms, the kart recording is approximately five to eight times shorter than the listed coaster friction loops. Its repeating texture is therefore much easier to recognize as a sample, especially after close-range 3D audio makes it prominent.
- 94.4% of its spectral energy is below 500 Hz. Its approximate A-weighted RMS is 7-11 dB below most coaster friction samples even when unweighted RMS is similar. Speaker bass management and GHub processing can make that low-frequency engine bed feel strong without giving it the brighter detail that makes coaster motion spatially legible.

## In-game gain and voice-count comparison

The runtime currently applies one policy to every track/mechanical sample:

```text
per-emitter gain = (vehicle volume byte / 255) * 4.5 * spatial gain
```

The vehicle volume byte begins at 208 once speed reaches 1 mph and reaches 255 at roughly 24.5 mph. Before distance attenuation, the shared 4.5 multiplier adds about 11.3-13.1 dB of amplitude gain. This is applied equally to kart engines, lift chains, and coaster friction.

The important asymmetry is emitter ownership:

- A coaster has one mechanical emitter per train. Its closest physical car supplies the emitter position, but the cars do not each play another copy of the friction loop.
- Each go-kart is a one-car train and therefore owns its own engine emitter.

For mutually incoherent copies, `N` similar sources add approximately `10 log10(N)` dB. Eight karts add about 9.0 dB and sixteen add about 12.0 dB relative to one kart at the same distance. As a concrete default-sample comparison, one kart is 3.55 dB below classic wooden friction in unweighted RMS, but eight equal-distance karts become about 5.5 dB above one wooden-coaster train before rider screams and other cues are considered.

This is the most convincing explanation for “always strangely loud”: the default sample is not loud; the ride-level sum is. The final floating-point bus, 6 dB headroom, and peak limiter prevent numerical clipping, but a limiter cannot restore a sensible balance between source classes. A dense group of correlated, bass-heavy kart loops can also pull the limiter down for unrelated sounds at high output settings.

## Why the kart sound appears to cut off

The raw sample seam is unlikely to be the primary fault. The kart edge ratio is 0.46, while train friction and the Arrow lift loop measure 2.24 and 2.67 respectively. The mixer loops at the physical beginning and end of the sample with interpolation but no authored loop-point metadata or seam crossfade, so the kart's very short repeated phrase remains audible even without a large single-sample click.

There are three stronger lifecycle explanations:

1. Mechanical sound is disabled below 1 mph. Karts frequently slow sharply, collide, queue, or stop; crossing this threshold changes the target sound ID rather than continuously approaching an idle engine state.
2. The simulation envelope decrements the old source volume until it changes to `null`; the audio channel then receives a 350 ms stop fade. On restart, the simulation normally begins at one-quarter of the target volume, but a target volume of exactly 255 is a special case that begins immediately at 255. A fast kart can therefore re-enter at full source strength.
3. Selection and source-ID changes stop one looping channel and create another from sample offset zero. The 2,048-emitter budget makes global culling unlikely in ordinary parks, and selected vehicles receive a continuity bonus, but a restart still phase-resets a conspicuously short loop.

Together these produce a perceptual “cut-off/restart” even though the channel stop itself is faded and the PCM is intact.

## Calibration implications

The evidence supports a kart-specific calibration pass rather than another global vehicle multiplier:

1. **Calibrate at both emitter and ride level.** Retain individual nearby kart positions, but measure the summed loudness of 1, 4, 8, and 16 equal-distance karts against one- and two-train coasters. A partial per-ride energy compensation or a kart-specific source multiplier can prevent vehicle count from becoming an accidental loudness multiplier. A first listening range worth testing is 4-6 dB below the current per-kart mechanical calibration, not because real karts are always quiet, but because these are recreational hire karts and many play simultaneously.
2. **Replace or extend the loop.** A clean 1-2 second engine recording with authored loop points would reduce obvious repetition. A short equal-power crossfade at the loop seam can remove residual edge clicks, but crossfading the existing 266 ms phrase cannot hide its rapid timbral repetition by itself.
3. **Model idle-to-running continuously.** Keep the engine channel alive through brief low-speed events, with a quiet idle floor and continuous gain/pitch response. Add hysteresis if a hard activation threshold remains. Remove the full-volume restart special case from this path or give channel creation a short attack ramp.
4. **Use distance-dependent aggregation.** Nearby karts can remain distinct 3D emitters; distant karts can become one ride-level engine bed with energy derived from the active kart count. This preserves close spatial detail while avoiding dozens of nearly identical low-frequency loops and reducing callback work.
5. **Test the bus, not just files.** An offline deterministic mix should render kart/coaster fixtures at identical 3D coordinates, record pre-limiter peak/RMS, limiter gain, and per-speaker output, and assert that adding voices is monotonic without unexpected channel restarts. Manual GHub listening remains necessary because endpoint bass management is outside OpenRCT2.

The first data-owned calibration applies `frictionSoundGainDb: -6.0` to the shipped RCT1, RCT2, and Time Twister kart vehicle definitions. The generic parser converts that decibel value to linear amplitude; legacy DAT objects and definitions without the property retain 0 dB. This deliberately changes only kart engine/friction channels; coaster calibration and secondary rider sounds remain independent.

Race starts are staggered in simulation rather than in the mixer. When the station light turns green, every kart receives a random 1-40 tick launch hold in its existing serialized vehicle counter. Its target speed is already chosen, but velocity and acceleration remain zero until that hold expires. The engine sound therefore starts naturally when the kart actually moves, instead of delaying audio while an already-moving vehicle remains silent.

The channel-start investigation also found that new positional channels snapshotted unity gain, front direction, and filter bypass before receiving their actual spatial state. Their first callback consequently interpolated from those defaults, making distant starts far louder and brighter than their steady state. New world, vehicle, and ride-music channels now seed interpolation history after complete spatial configuration. Loop length and possible distance-dependent aggregation remain separate follow-up work.

## Sources

- City of Santa Clara, *California's Great America Theme Park Master Plan Draft Environmental Impact Report*, Table 3.4-5, ride measurements at 100 ft: [official city PDF](https://www.santaclaraca.gov/home/showpublisheddocument/47346/636112752829170000).
- Christchurch City Council, *Planning Committee Agenda, 5 September 2012*, Table 18, kart-event measurements and model calibration: [archived council PDF](https://archived.ccc.govt.nz/council/agendas/2012/september/planning5th/agenda.pdf).
- Danish Environmental Protection Agency, *Motor Racing Vehicles - Measurement Methods*, POP 1-class kart pass-bys at 7.5 m: [official measurement page](https://www2.mst.dk/udgiv/publications/2003/87-7614-016-4/html/bred31_eng.htm).
- Western Australia Environmental Protection Authority, *Environmental Noise Assessment*, Table 4-2, racing and hire-kart sound-power spectra: [official EPA PDF](https://www.epa.wa.gov.au/sites/default/files/Referral_Documentation/Appendix%201%20-%20Environmental%20Noise%20Assessment.pdf).
- Davis, Birdsong, and Cota, *Vibroacoustic study of circular cylindrical tubes in roller coaster rails*, field and laboratory evidence that track construction can change coaster noise by 10-15 dB: [Cal Poly repository record](https://digitalcommons.calpoly.edu/meng_fac/84/).

## Relevant OpenRCT2 paths

- `src/openrct2/audio/Audio.h`: sound IDs and the 2,048 vehicle-emitter capacity.
- `bin/data/object/rct2/audio/rct2.audio.base.rct2.json`: CSS1 sample mapping; go-kart engine is entry 21.
- `src/openrct2/ride/Vehicle.Sound.cpp`: 1 mph activation threshold and simulation gain envelope.
- `src/openrct2-ui/ride/VehicleSounds.cpp`: per-train emitter selection, shared 4.5 mechanical multiplier, pitch, priority, and channel lifecycle.
- `src/openrct2-ui/audio/AudioChannel.cpp`: 350 ms channel stop fade and raw loop wrapping.
- `src/openrct2-ui/audio/AudioMixer.cpp`: floating-point accumulation, 6 dB headroom, and final limiter.
