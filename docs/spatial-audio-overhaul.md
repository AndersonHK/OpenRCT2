# Spatial audio overhaul

The implemented camera/depth stage and the deferred environmental-reverb stretch goal are documented in [spatial-audio-camera-depth-plan.md](spatial-audio-camera-depth-plan.md). Real-world ride comparisons, default CSS1 measurements, and the go-kart loop/lifecycle investigation are documented in [ride-audio-loudness-research.md](ride-audio-loudness-research.md).

## Goal

Audio should describe the park around the camera instead of acting like a side effect of what happens to be visible in the current viewport. Leaving a source a few pixels outside the viewport should not silence it, while zooming toward an area should produce a materially stronger near-versus-far sound field.

The listener is a pitched virtual camera behind and above the viewport's world-space focus point on the exact isometric view ray. Sound sources use their map `x`, `y`, and `z` distance from that camera, while its forward/right/up basis defines camera-relative direction. Zoom has no special volume multiplier or cutoff; it only changes the camera geometry by changing the world-space footprint visible in the viewport.

## World model

The shared spatial model in `src/openrct2/audio/SpatialAudio.*` is used by one-shot world sounds, vehicle sounds, and ride music.

- The centre ray is intersected with terrain to find the focus `x`, `y`, and ground `z`.
- The projected viewport dimensions estimate the visible ground-plane radius using the isometric projection: sideways radius is `width / (2 * sqrt(2))` and forward radius is `height / sqrt(2)`.
- That radius is converted to a canonical camera distance through a 150-degree equivalent field of view. The camera is placed along the null ray of the legacy isometric projection, preserving the previous focus-distance baseline while giving the orthographic view a coherent 3D pose. Zooming in shrinks the footprint and moves the listener toward the focus; zooming out moves it away.
- Source distance is the Euclidean distance `sqrt(dx^2 + dy^2 + dz^2)` from that elevated listener. A coaster crest, ground-level station, and underground train therefore have genuinely different distances and elevation angles.
- Outside a small reference radius, all sources use a continuous power curve. Ordinary world effects use `(referenceDistance / distance)^0.8`, approximately 4.8 dB quieter per distance doubling. Vehicle loops and rider screams use exponent `1.1`, approximately 6.6 dB per doubling, so they form a tighter local sound field. Amplified ride music uses exponent `0.6`, approximately 3.6 dB per doubling, preserving a faint park-wide music bed at extreme zoom. Distance, camera height, source height, occlusion, and direction remain shared; only the physically plausible source-class rolloff changes.
- There are no zoom branches, screen rectangles, distance rings, or maximum-tile cutoff in this curve. Every emitter receives a continuous gain; voice budgets retain the strongest contributors when the park contains more candidates than can usefully be mixed.
- Underground sources retain the old obstruction concept as a separate 25% occlusion gain, rather than using a zoom multiplier.
- Direction is transformed through the pitched camera basis before 7.1 routing. The viewed focus and vertically displaced upper/lower-centre sources remain front-biased; side and rear speakers represent large camera-relative angles or sources genuinely behind the implied camera.

The key zoom invariant is relative rather than hardcoded: reducing the visible footprint makes an emitter at the focus louder and increases its gain ratio over a horizontally remote emitter. This is covered directly by tests using two arbitrary projected viewport sizes, not named game zoom levels.

## Motion and Doppler

Persistent world one-shots, ride music, and vehicle loops are re-evaluated as the listener moves. Source and listener velocities are tracked independently and projected onto their connecting ray. Physical vehicle motion receives full Doppler, while camera panning and zooming use a global 5% listener contribution for every positional source. Static ride speakers and world one-shots therefore receive only reduced camera Doppler; moving vehicles combine full source motion with the reduced camera term.

The Doppler ratio uses `(speedOfSound + 0.05 * listenerRadialVelocity) / (speedOfSound + sourceRadialVelocity)`: approaching sources rise in pitch and receding sources fall. Park coordinates are calibrated to an acoustic propagation speed of 96 tiles per second. Source velocity, listener velocity, and the final ratio are smoothed, limited to `0.75x` through `1.333x`, and reset after a teleport, view rotation, long update gap, or closest-car emitter-anchor change. The old orientation-derived vehicle `dopplerShift` value remains serialized for compatibility but no longer drives playback pitch.

Direct-path depth also includes a continuously changing low-pass filter. Clear sources inside the reference distance bypass it; high frequencies progressively fall with 3D distance, vehicle detail rolls off slightly faster, ride music slightly slower, and occlusion lowers the cutoff in addition to reducing amplitude. Coefficients interpolate across the callback and filter state remains on the channel, with no map queries or allocation on the audio thread.

## Output and mixing

The SDL output path requests 48 kHz, eight-channel audio first. SDL's 7.1 order is `FL, FR, FC, LFE, BL, BR, SL, SR`, which also matches the DirectSound order expected by Windows surround endpoints. If an output device rejects the 7.1 open, OpenRCT2 logs the reason and retries in stereo.

Positional mono content uses constant-power interpolation between the nearest available speakers. On 7.1 output, side and rear positions remain distinct, while straight-ahead world effects use a phantom centre across front-left and front-right. This avoids depending on the centre-channel downmix of virtual-surround headsets such as Logitech GHub while preserving a stable front image. The physical centre is left available for future dialogue/UI use. The LFE channel is deliberately left to endpoint bass management: sending full-range samples directly to it would be incorrect without a dedicated low-pass crossover. Non-positional stereo content such as title music remains on the front-left and front-right channels.

The old callback repeatedly added complete 16-bit streams with `SDL_MixAudioFormat`. SDL warns that repeated use clips when more than two streams overlap. The replacement callback accumulates every source into a floating-point bus, keeps 6 dB of mix headroom, and applies an immediate-attack, 0.75-second-release peak limiter with a 0.95 full-scale ceiling before converting once to 16-bit output.

The output callback already runs on SDL's dedicated audio thread, separate from simulation and rendering. A second producer thread was considered after raising voice counts, but would add a ring-buffer handoff and another buffer of latency. The measured callback still has comfortable deadline margin after the data-path changes below, so this build keeps one dedicated audio thread and reports average/worst callback time in verbose mode.

For high voice counts, active channels are contiguous rather than a linked list, vehicle continuity/slot lookup uses hash sets and maps rather than repeated full-array scans, and the floating-point bus is planar by speaker. The positional hot loop has a runtime-dispatched AVX2 kernel with scalar fallback. Canonical 22.05 kHz stereo sources are downmixed and pitch-resampled directly into positional mono, bypassing a separate `SDL_ConvertAudio` and second resampling pass for every voice. Windows requests 1,024-frame callbacks so the measured deadline is 21.33 ms rather than accepting a backend-reduced 480-frame callback.

## Voice and lifecycle policy

- Up to 2,048 vehicle sound emitters are selected primarily by post-distance loudness, then source activity, mass, motion, and continuity. Each may have track and secondary noise, allowing more than 4,000 vehicle channels before weather, crowds, UI, and one-shots.
- Ride-music selection ranks on unclipped spatial gain, so nearby speakers remain ordered by true distance. Source calibration and distance now remain separate floating-point gains through the mixer instead of saturating at the per-channel unity boundary. Its strongest-64 budget remains deliberately separate from the much larger ride-vehicle pool.
- The final mixer stores up to 8,192 simultaneous channels. Retiring music voices may finish their 350 ms fade without blocking newly important nearby sources, and a source that regains priority reverses that fade smoothly rather than restarting.
- Vehicle sounds have their own mixer group and verbose telemetry. The callback reports allocated and actually mixed voices by group, per-speaker output peaks, channel-admission failures, and timing. On the Logitech endpoint, SDL negotiated a single WASAPI 48 kHz 7.1 stream: hundreds of logical OpenRCT2 voices are software-summed into eight PCM channels before Windows or GHub sees them, so no kernel/hardware voice-count culling occurs.
- Each train remains one logical mechanical/secondary emitter, but its acoustic point is the closest physical car to the listener rather than always the head car. Long trains and distributed flat-ride vehicles therefore remain local when any part of the train is close to the viewed area without duplicating a sample across every car.
- Vehicle volume bytes are interpreted as linear amplitude (`128` is approximately -6 dB), with a 4.5x mechanical-source calibration. Rider screams receive a dedicated 3.5x calibration and other secondary vehicle cues receive 1.75x. These small close-range reductions combine with the steeper vehicle curve to fade coaster and kart activity more quickly outside the viewed area.
- Ride music uses its shallower long-range curve with a 5x amplified-speaker source strength, a strongest-64 voice budget, and the same global 5% camera-Doppler contribution as other positional sources so it remains audible at extreme zoom without taking capacity from ride-vehicle detail.
- Rain is a height-relative ambient field rather than a point source. Its continuous inverse-height curve is shifted one camera-height step louder: zoom -1 now receives the former closest-view gain, zoom -2 rises by another roughly 4.5 dB, and each doubling of virtual listener height reduces it by about 4.5 dB thereafter.
- Visible guests are accumulated into eight camera-relative angular sectors. Each sector drives a phase-staggered spatial crowd-ambience voice whose amplitude share follows its guest density; the shares preserve the old aggregate loudness instead of multiplying it by eight, while dense groups can now occupy distinct front, side, and rear speakers.
- The positional purchase/cash-register cue receives a local 4 dB source correction so it remains legible over the sustained crowd field without raising unrelated world effects.
- Position and volume changes interpolate across an audio buffer.
- A stopped source fades for 350 ms instead of disappearing at the next callback.
- A ride that closes because it crashed is allowed to finish its active music track instead of nulling it on the following game tick. Crash effects themselves remain ordinary one-shot spatial sounds and play to completion.

## Logitech GHub and Windows

Enable surround for the Logitech playback device in GHub, then select that device in OpenRCT2 or leave OpenRCT2 on the Windows default device. Run `openrct2.exe --verbose` to inspect negotiation. Native surround is active when the log contains a line like:

```text
Opened audio output at 48000 Hz with 8 channels
```

If the endpoint is currently exposed as stereo, OpenRCT2 will log the failed 7.1 attempt and continue with stereo output. Re-enabling the GHub surround endpoint and restarting OpenRCT2 lets the engine negotiate eight channels again.

SDL channel layout and mixing behavior are documented in the official [SDL_AudioSpec](https://wiki.libsdl.org/SDL2/SDL_AudioSpec) and [SDL_MixAudioFormat](https://wiki.libsdl.org/SDL2/SDL_MixAudioFormat) references.

## Verification

Pure tests cover the generic, vehicle, and music rolloff ordering and exact long-range values, continuous monotonic attenuation, absence of a distance boundary, the zoomed-in focus-versus-horizontal-distance invariant, the isometric centre ray and all rotations, elevated same-ray sources, screen-vertical front semantics, camera and object height, occlusion, separated source/listener Doppler and discontinuity rejection, continuous distance/occlusion filter targets, floating gains above unity, constant-power speaker gains, phantom-centre 7.1 routing, ride-music priority, legacy volume-envelope conversion, and limiter attack/release.

Runtime validation should also load a ride-heavy park with `--verbose` and confirm that the process remains stable, reports the expected channel count, and produces no audio callback errors. Because automated tests cannot listen to a physical speaker layout, final channel placement, perceived roll-off, and GHub virtualization still require an audible pass on the target system.

On the July 10 EverythingPark stress passes, 269-291 continuous-distance vehicle emitters plus the directional crowd field produced roughly 478-500 concurrent channels. The final compressed-rolloff build mixed steady callbacks in about 4.7 ms average, with a measured 10.6 ms worst callback against a 21.33 ms deadline. This validates hundreds of real park voices with substantial scaling margin; the 8,192 channel container is a capacity ceiling, not a claim that every possible 8,192-voice scene fits one callback on all CPUs.

The July 12 camera/depth pass exercised 487-514 concurrent channels, of which 478-506 used distance filters. Removing per-sample rounded clamps and divisions from the stable one-pole loop reduced settled callback windows to about 5.6-5.7 ms average, with representative 9.6-10.2 ms worst callbacks against the same 21.33 ms deadline. No vehicle channel starts failed, and the limiter remained inactive at the configured output level.
