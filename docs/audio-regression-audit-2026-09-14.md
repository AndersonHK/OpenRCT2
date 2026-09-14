# Post-migration engine and rumble audio audit

The owner reported quieter audio and intermittently absent kart engines/coaster rumble after the upstream deployment. The investigation found no catch-up regression in simultaneous-sample admission, vehicle selection, or source loudness calibration. It did not reproduce the owner's exact scene or establish the cause of the perceived dropouts.

## Comparison boundaries

- Source baseline: `stable-before-upstream-2026-09-13` (`e874ceb7708974e00040419d30390686c52a5d31`). Deployed migration source: `c6560d2679cd7871ba583236fd9515bb81955511`.
- Actual previous executable/data: `D:\Games\Independent\OpenRCT2Mod\backup-before-upstream-20260914-073936` (reports 0.5.3).
- Actual new executable/data: `D:\Games\Independent\OpenRCT2Mod` (reports 0.5.5).
- Runtime fixture: repository `test/tests/testdata/parks/EverythingPark.park`, saved camera, ordinary gameplay, software renderer, native WASAPI output. These were short diagnostic runs, not synchronized audio recordings or a performance acceptance benchmark.

## Source and data findings

All 20 changed files in the core/UI audio directories, VehicleSounds.cpp, Vehicle.Sound.cpp, RideAudio.cpp, and the audio object/sample-table files are equivalent after accounting for reviewed include edits, enum/member renames, and the Vehicle.Sound.cpp namespace wrapper. In particular, the mixer admission and playback loops, resampling, gain application, limiter, channel lifecycle, and vehicle emitter selection have no functional delta against the stable tag. TrainManager's changes are includes and the entity lookup rename. Vehicle sound generation retains its thresholds and envelopes.

The loudness adjustments remain:

| Sound class | Retained calibration |
| --- | --- |
| Mechanical vehicle audio | 4.5x times the authored linear volume and per-object friction gain |
| Rider screams | 3.5x |
| Other secondary vehicle sounds | 1.75x |
| Ride music | 5.0x, with its separate distance curve |
| Four shipped kart definitions | `frictionSoundGainDb: -6.0` |
| Mixer | 0.5 headroom factor and the existing peak limiter |

Compared all 2,505 object JSON files shared with the actual deployment backup, recursively inspecting sound/audio/volume/gain properties: zero differences. This includes sample identifiers, sound ranges, double-frequency flags, and the four kart gain properties. All three deployed `.parkap` asset archives have identical SHA-256 hashes to the backup. Both executables report SDL 2.32.8. No runtime libraries were separately deployed as DLLs in that manifest.

Repeated uses of one source get independent channels and playback cursors. Vehicle continuity is keyed by vehicle ID, not SoundId, so identical engines on different karts/trains are not deduplicated. One train still uses one mechanical and one secondary emitter, spatially anchored to its closest car; that is pre-existing behavior.

The stable version is not literally unlimited:

- 8,192 total mixer channels.
- 2,048 selected vehicle emitters, each with mechanical and secondary channels.
- 64 selected ride-music sources, with retiring fades allowed to finish.

Commit `8298716b84` (July 10, "Spatial Sound Rework") introduced the 8,192 mixer ceiling, raised vehicle emitters from 14 to 2,048, and raised music sources from 32 to 64. These ceilings were already present before the catch-up. They do not impose a separate same-sample quota.

## Verification

Added `AudioMixer.RepeatedSampleVoicesRetainIndependentPlayback`: admits 1,024 concurrent looping channels from one source, checks independent read positions, and verifies that retiring/replacing one voice leaves another playing. It exercises the real mixer admission and channel implementation without requiring a device; it does not render or listen to 1,024 voices.

Release x64 test build succeeded using MSVC 14.44.35207 and existing migration libraries. Focused run: **27 tests passed**, including the new mixer test and the existing channel lifetime, attenuation, gain, kart-object calibration, start-state and limiter tests. Initial build attempts encountered build-environment cancellation and duplicate PATH/Path variables; the successful invocation normalized the child environment. The test harness reported sandbox-denied user object-index writes; no success writing that index is claimed.

The corrected native runs both negotiated **WASAPI 48 kHz, eight channels, 1,024 frames**:

| Observation | Previous deployment | New deployment |
| --- | --- | --- |
| Callback voice counts sampled every five seconds | 462-535 | 462-535 |
| Vehicle voices actually processed by the mixer | 319-356 | 319-356 |
| Vehicle candidate selection | All candidates in every snapshot | All candidates in every snapshot |
| Vehicle start failures | 0 in all nine snapshots | 0 in all twelve snapshots |
| Limiter gain | 1.000 in all nine callback snapshots | 1.000 in all eleven callback snapshots |

These results rule out a small legacy voice cap in this fixture and show no limiter reduction during the sampled windows. Snapshot counts are not a per-sample waveform comparison or proof against every intermittent dropout. The runs have different lengths and are not sample-synchronized.

Profiles were isolated under `obj/upstream-audit/audio-regression/{before,after}`, inheriting original-game paths and the sound section only. The owner's current master volume was 5%, with effects/music at 100%; both comparisons used those settings. Their historical value is not established. The owner's actual profile, settings, saves and installed executables were not modified. Only processes started for these checks were stopped.

Two setup pitfalls are retained in the evidence rather than mistaken for game regressions:

1. `--benchmark-ui` deliberately uses a dummy audio context. The initial `before.log` run therefore provides no playback evidence and was discarded from the audio comparison.
2. The first real-audio profiles lacked `openrct2.audio.additional.parkobj`, which exists in the owner's normal `Documents\OpenRCT2\object` directory but is absent from both deployment data trees. Both executables consequently failed some sound starts. Copying that same existing pack into both isolated profiles eliminated those failures. The corrected `*-complete.log` runs are the comparison above; `*-live.log` records the incomplete-profile runs. This is also a portability dependency to retain when moving the installation to a clean profile. Two unrelated unsupported-codec messages remain in both runs; these were not vehicle admission failures and their source was not identified here.

Local raw evidence: `obj/upstream-audit/audio-regression/` contains `source-comparison.json`, `asset-comparison.json`, `runtime-comparison.json`, `tests.log/xml`, and the before/after stdout/stderr captures. Build output is `obj/upstream-audit/audio-regression-build.log`.

## Decisions and remaining uncertainty

- Preserve all existing gains, voice ceilings, and emitter semantics. No production audio code or deployed binary changed during this investigation.
- Do not raise global volume or remove limits merely to mask the report: no evidence links either to the catch-up.
- Existing mechanical sound generation drops its target sample below approximately 1 mph and restarts it as motion resumes. Kart starts also retain their authored launch stagger, and the short engine loop/stop-restart behavior is documented in `ride-audio-loudness-research.md`. These can produce intermittent engine sound but are not a proven explanation for this report.
- The exact owner scene and physical listening comparison remain unverified. Further reproduction should use the affected save, the same camera/zoom and speed, and the normal supplemental sound pack, then correlate missing engines with velocity and per-vehicle channel state.
- After this investigation, the owner double-checked the game, accepted the deployed catch-up as stable, and authorized merging it on 2026-09-14. They consider perceived balance a possible distance/camera/directional tuning matter. Keep that tuning separate from the accepted catch-up and Vulkan rendering work; no new audio behavior was requested or applied here.
