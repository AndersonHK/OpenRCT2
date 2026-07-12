# Spatial audio camera and depth plan

## Status and scope

The first slice was implemented on 2026-07-12. It establishes a coherent isometric camera, separates source and camera motion for Doppler, preserves near-field distance differences, adds distance-dependent EQ, and maps surround direction in the pitched camera's coordinate system. Environmental reverb remains deliberately deferred to a second, experimental slice.

The existing spatial-audio overhaul remains the baseline. Voice budgets, floating-point bus accumulation, 7.1 endpoint negotiation, source-class distance curves, and final peak limiting remain in place unless measurements show that a change is necessary.

## Design decisions

1. A moving physical emitter receives full Doppler. Camera motion receives one reduced listener-Doppler factor for every positional source.
2. Nearby sources must retain their mathematical distance ordering after source-strength calibration; a hard unity gain clamp must not make ground-level and elevated track equally loud.
3. Distant sound receives frequency-dependent attenuation in addition to amplitude attenuation.
4. Audio direction is camera-relative. Because the camera looks down toward the park, the camera's pitched forward vector points toward the viewed ground. A source near the screen centre is therefore semantically in front, not underneath the listener.
5. Reverb and reflection modelling are a stretch goal for a second slice, after the direct path is calibrated and manually tested.

## Canonical orthographic camera

Orthographic rendering and a real 3D camera pose are compatible. Orthographic projection does not determine a unique camera distance, so OpenRCT2 must choose a canonical pose that is consistent with the legacy isometric view.

The shared camera state should contain:

- the terrain-intersected focus point under the viewport centre;
- world position;
- orthonormal forward, right, and up vectors;
- orthographic width and height;
- an equivalent perspective field of view used only to convert visible footprint into camera distance;
- smoothed linear and angular velocity;
- a discontinuity marker for park loads, teleports, and instant view changes.

The camera should be placed behind and above the focus along the exact null direction of the legacy isometric projection. Rotation changes that direction in quarter turns. Camera distance should be derived continuously from the visible world footprint and a tunable equivalent field of view:

```text
distance = visibleHalfExtent / tan(equivalentHalfFov)
position = focus - forward * distance
```

The existing 150-degree equivalent field of view is a starting datum, not a settled value. It should be calibrated with ground, elevated-track, and high-zoom listening scenes. Rendering can continue to use the exact legacy sprite projection while audio consumes the canonical pose. A later renderer refactor may consume the same pose without introducing perspective scaling.

Pose smoothing must be independent of simulation speed. Ordinary panning and zooming should use a short critically damped transition. A large discontinuity should reset velocity and Doppler instead of simulating an extreme camera movement. Quarter-turn rotation requires either a short pose transition or an explicit discontinuity; it must not move the listener around the park instantaneously and create a pitch sweep.

## Camera-relative surround direction

World height should not be interpreted as a separate speaker dimension before transforming into camera space. For a source position `source` and camera position `listener`:

```text
relative = source - listener
localRight   = dot(relative, cameraRight)
localForward = dot(relative, cameraForward)
localUp      = dot(relative, cameraUp)

azimuth   = atan2(localRight, localForward)
elevation = atan2(localUp, sqrt(localRight^2 + localForward^2))
```

The important semantic invariants are:

- The terrain focus is on the camera's forward ray and routes to the front image.
- A coaster or speaker near the upper centre of the viewport remains predominantly front because it is still in the camera's look direction.
- Raising an emitter along the visible isometric ray moves it closer to the camera and changes its smaller camera-local elevation component; it does not automatically move the sound away from the front speakers.
- Left and right screen displacement produces left and right camera-local direction.
- Side and rear speakers represent sources at large camera-relative angles or genuinely behind the implied camera. The lower half of the screen is not intrinsically the rear sound field.
- View rotation rotates the entire acoustic basis with the image.

Standard 7.1 has no height speakers, but that does not prevent coherent pitched-camera direction. The direct 7.1 bed should use camera-local azimuth, with the existing phantom front centre on FL/FR. Camera-local elevation remains useful for range validation and later, subtle spectral or reflection cues. A separate HRTF or spatial-audio endpoint is not required for this slice.

This model should make a high track piece aligned with the centre view ray materially closer and louder than a ground source projected into the same area. It also matches the player's visual semantics: the camera faces the park, so the park is primarily ahead.

## Separated Doppler motion

The current distance-delta calculation combines source and listener motion. Replace it with explicit source and listener velocity projected onto the source-listener ray.

For a unit vector `ray` from listener to source:

```text
listenerRadial = dot(listenerVelocity, ray)
sourceRadial   = dot(sourceVelocity, ray)

doppler =
    (speedOfSound + cameraDopplerStrength * listenerRadial)
    / (speedOfSound + sourceRadial)
```

Sign conventions must be verified by tests for approaching and receding motion. Existing smoothing, teleport rejection, and `0.75x` to `1.333x` safety bounds should remain initially.

Policy by source class:

| Source | Physical source motion | Camera motion |
| --- | --- | --- |
| Coaster and kart vehicles | 100% | reduced global factor |
| Rider screams attached to vehicles | 100% | reduced global factor |
| Static positional one-shots | none | reduced global factor |
| Ride music speakers | none | reduced global factor |
| Crowd and rain ambient fields | no point-emitter Doppler | none |

The current ride-music value of 5% should become the initial global camera-Doppler strength. It must be independently tunable because the correct source-motion result does not imply that camera motion will feel comfortable at the same scale.

Vehicle source velocity should come from actual world-position history, not only scalar ride speed. The closest-car emitter selection needs hysteresis or a motion reset when it changes cars so that switching the representative car does not create a false Doppler impulse.

## First slice: direct-path depth

### Preserve near-field distance ordering

Mechanical source calibration is currently multiplied into spatial gain before conversion to a channel volume whose maximum is unity. Several nearby sources can therefore saturate to the same value even when their distances differ substantially.

The first slice should carry a floating-point per-voice gain through the mixer instead of clipping it at the channel boundary. The existing floating-point accumulation, mix headroom, and final limiter remain responsible for output safety. Source-class calibration, authored sample level, distance gain, occlusion, and bus gain should remain separately inspectable in telemetry.

Acceptance conditions:

- Spatial gain remains strictly ordered for two otherwise identical sources at different 3D ranges.
- An elevated source between the canonical camera and ground focus is louder than the equivalent ground source by the predicted curve ratio.
- Raising source strength does not erase that ratio.
- Combined output remains below the limiter ceiling with hundreds of voices.

### Distance-dependent EQ

Amplitude loss alone does not make a source perceptually distant. The direct path should add smooth frequency-dependent attenuation:

- low frequencies fall off least;
- mid frequencies fall off moderately;
- high-frequency wheel hiss, speech consonants, and scream brightness fall off fastest;
- terrain or structural occlusion adds a stronger high- and mid-frequency reduction rather than only multiplying volume;
- coefficients interpolate so camera movement does not zipper or click.

Start with a small three-band model or a low-cost low-pass plus high-shelf representation. The curve should use continuous 3D propagation distance, not zoom levels or tile rings. Each source class may define how strongly it responds, but all classes should share the same physical distance input.

Filter parameters should update at a lower control rate and interpolate inside the callback. Filter state should use a structure-of-arrays layout suitable for scalar and AVX2 kernels. The callback must not allocate, query map geometry, or acquire a game-state lock.

## Second slice stretch goal: environmental reverb

Reverb is intentionally excluded from first-slice acceptance because its artistic balance requires direct listening after camera position, unsaturated gain, and distance EQ are stable.

The likely design is a small number of shared environment sends rather than a convolution reverb per voice:

- open park or open sky;
- dense ride structures and stations;
- enclosed or underground spaces;
- decorrelated surround returns shared by many sources.

Distance and occlusion may raise the wet-to-dry ratio, while total reflected energy should still respect how open the listener environment is. A high outdoor camera must sound exposed and distant, not like an indoor hall. Geometry analysis and reflection classification must run away from the audio callback, with slowly changing parameters passed to the mixer.

This slice should begin with isolated A/B prototypes and must be removable without disturbing the direct path.

## Validation plan

### Deterministic geometry tests

- Reconstruct the legacy isometric view ray for all four rotations.
- Confirm that the terrain focus lies on camera forward.
- Confirm that equal screen-centre rays remain front-biased at different source heights.
- Compare ground and elevated emitters that project into the same screen region.
- Verify that left/right projected displacement maps monotonically to camera-local azimuth.
- Verify that a lower-screen source is not labelled rear solely because of screen `y`.
- Verify continuous listener position and altitude across arbitrary viewport dimensions.

### Doppler tests

- Static camera plus approaching/receding vehicle: full source Doppler.
- Moving camera plus static source: reduced global camera Doppler.
- Moving camera plus moving vehicle: independently composed contributions.
- Music and static one-shots receive the same camera-motion factor.
- Representative-car changes, teleports, loads, and view rotations do not create impulses.

### Gain and EQ tests

- Near-field gain ordering survives source-strength calibration.
- High-band attenuation is continuous and monotonic with 3D distance.
- Low-band attenuation remains weaker than high-band attenuation.
- Occlusion transitions smoothly in gain and spectrum.
- Filter bypass at the reference distance is numerically neutral.

### Runtime and manual tests

- Use a controlled park containing ground track, very high supported track, tunnels, music, crowds, and static effects.
- Listen at every zoom and rotation, including panning past a fixed source and following a moving train.
- Stress EverythingPark with verbose group counts, output peaks, limiter gain, callback average/worst time, and channel-admission failures.
- Keep the worst measured callback comfortably below its 21.33 ms deadline; target less than half the deadline under the reference stress park before enabling the filters by default.

## Delivery boundaries

The first slice implementation now contains canonical camera geometry, separated Doppler, unsaturated distance ordering, camera-relative surround semantics, and distance EQ with deterministic coverage. A stable build and automated validation are required before deployment; perceived filter strength and source calibration still require manual listening. Reverb is not part of this completion criterion. It remains a documented second-slice experiment whose design may change based on the first listening results.

The reference EverythingPark implementation run mixed 487-514 channels with 478-506 active distance filters. Settled windows averaged approximately 5.6-5.7 ms and representative worst callbacks were 9.6-10.2 ms against a 21.33 ms deadline, with no vehicle start failures. This meets the first-slice performance target while keeping the callback filter loop allocation-free and all map work on the control side.
