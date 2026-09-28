# Spatial LightFX parity fixture

Status: bounded read-only proposal, inspected 2026-09-19. Uniform-light scaling fixtures prove the colour-combination order,
but do not prove light discovery, projection, occlusion, clipping, overlapping falloffs or GPU accumulation.

## Production entry points and state

[`LightFX.h`](../src/openrct2/drawing/LightFX.h) exposes `Add3DLightMagicFromDrawingTile`, entity-keyed `Add3DLight`, kiosk/shop
and vehicle helpers, `GetPalette`, `CaptureBakedFalloffs`, `CaptureFrameSnapshot` and the resolved-command CPU rasterizer.
Map-light insertion converts tile plus offsets to world coordinates and projects with `GetCurrentRotation`; entity insertion
deduplicates by entity/id. These are real source APIs, not a framebuffer injection seam.

Natural light producers already exist in the tiny ferris-wheel park baseline: ride entrance/exit and park entrance painting
call `Add3DLightMagicFromDrawingTile` in [`Paint.Entrance.cpp`](../src/openrct2/paint/tile_element/Paint.Entrance.cpp). The ride
entrance adds a lantern3 source above the building and direction-dependent lantern2 sources. Validate nonzero resolved lights
rather than assuming the loaded park triggers these paths. Lamp path additions use `PATH_ADDITION_FLAG_LAMP`, unbroken state
and unconnected path edges in [`Paint.PathAddition.cpp`](../src/openrct2/paint/tile_element/Paint.PathAddition.cpp).
Shop/kiosk track paint, lit staff and selected vehicle types provide later source families.

`LightFx::IsAvailable()` combines backend availability with `config.general.enableLightFx`; vehicles have an additional
`enableLightFxForVehicles` setting. Set these before context initialization: X8 allocates its lighting buffers during resize
only when lighting is enabled. Do not switch only the candidate after loading. Both renderers already mark lighting available.

[`Palette.cpp`](../src/openrct2/drawing/Palette.cpp), `UpdatePalette`, invokes `LightFx::ApplyPaletteFilter` and publishes the
dark palette; `LightFx::GetPalette()` supplies the light palette. Set `gDayNightCycle` to an explicit value (start with 1.0),
pin `gPaletteEffectFrame`, `Weather::gLightningFlash`, `weatherCurrent.temperature` and `weatherCurrent.level`, and keep
simulation paused. Disable automatic day/night advancement and precipitation initially. After selecting these inputs, use
the existing `LoadPalette()` once at a documented point in **both** drivers to refresh the full dynamic palette, then use the
normal `Painter::Paint`/`UpdatePaletteEffects` sequence. Merely setting night after the load does not refresh every colour.

## Stateful behavior that affects repeatability

[`LightFX.cpp`](../src/openrct2/drawing/LightFX.cpp), `ResolveLightFrame`, updates viewport settings, swaps light lists, prepares
occlusion and resolves falloffs. Its zoom state includes a delayed value. Light lists retain/linger entries, and light
pollution feeds the subsequent palette. `ApplyPaletteFilter` has function-static wetness, fogginess and light-pollution
adaptation updated on palette-entry calls. Pausing ticks does **not** freeze these paint-driven state changes.

Consequently, the baseline harness's “two adjacent captures must match” rule is not a valid general LightFX invariant. Use
identical fresh-process input and paint sequences, and compare the **same named paint ordinal** across frozen software,
current software and Vulkan. Repeat each complete process sequence twice and require exact equality at corresponding
ordinals. Preserve a multi-frame sequence to test the temporal behavior itself; do not reset private static state, add extra
palette calls, or wait an arbitrary wall-clock duration to force equality. Record the exact warmup count and each palette
refresh/camera/light transition. First determine a short fixed warmup sequence from evidence; do not assume two paints cover
zoom/list initialization for every variant.

`CaptureFrameSnapshot` is not a read-only inspection function: it calls `ResolveLightFrame`, which advances this state.
Never call it a second time from the harness to obtain diagnostic metadata. The software display calls `RenderToTexture`
once, while Vulkan captures once at the end of recording; preserve those seams. `GetPalette` and copying `gPalette` are safe
read-only ways to record palette hashes.

## First bounded actual-UI fixture

1. Extend the shared UI driver with explicit lighting/night/vehicle-light settings and deterministic paint-ordinal capture.
   Build exactly that driver revision with frozen and current source. Keep the actual SDL pre-present hook and named Vulkan
   packet capture; no new frozen-renderer hook is necessary for the first fixture.
2. Load the unchanged hashed ferris-wheel park, main viewport rotation 0/zoom 0/camera from the existing baseline. Set night
   1.0 and the same fixed weather fields in each process. Enable LightFX before initialization, keep precipitation/FPS off,
   and perform the same full palette refresh and paint-only warmup sequence. Force full invalidation initially so software
   discovers the same visible lamps rather than relying on previous dirty-region history.
3. Capture named night frames at fixed ordinals and preserve raw indexed and actual final RGBA data. Require actual resolved
   lights, spatially varying lit output and a successful GPU-lighting path. Compare frozen/current software first, then
   frozen/Vulkan. All channels must match exactly; every divergent image group gets manual inspection before a fix or exception.
4. Repeat in a fresh process and require ordinal-by-ordinal equality. Keep a separately captured day/unlit control as a
   diagnostic. Lighting should affect colour output; indexed differences need investigation, but do not assume disabling
   LightFX is guaranteed to leave every painter behavior unchanged without measuring the control.

Add candidate-only diagnostic packet fields without changing rendering: resolved light count/type histogram, resolved-command
hash, light-palette hash, falloff hash, whether CPU intensity was attached, and the **actual selected execution route**
(GPU accumulation versus uploaded CPU map). Record them from the already-owned packet/backend decision. The current
`lightFxEnabled` flag alone does not prove GPU execution: [`VulkanBackend.cpp`](../src/openrct2-renderer/vulkan/VulkanBackend.cpp),
`RecordLightFx`, can use an uploaded CPU intensity fallback. A capability flag alone is also insufficient after resize.
The eventual exclusive Vulkan contract requires the GPU route, so the required compute fixture must fail if fallback is used.

A minimal first run needs no new image readback API. Final RGBA comparison is authoritative, with indexed/palette/command
metadata localizing differences. For later failure diagnosis, capture the candidate's actual R32 accumulation (and its
effective saturated intensity) through an opt-in named GPU readback; do not label a CPU reconstruction as GPU evidence.

## Follow-up spatial cases, in order

- **Natural entrance sources, four rotations and zoom 0/1/2:** verify the same park/source placement projects correctly,
  including the source-size changes and delayed zoom history. Use separate process sequences or explicitly matched camera
  transitions; assert nonzero coverage for each case.
- **Overlap and saturation:** add a shared driver-only `InjectFixtureLights` call between `Painter::Paint` and `EndDraw`.
  Use public `Add3DLightMagicFromDrawingTile` with explicit world tile/offset/type entries and distinct map keys, identically
  in frozen/current runs. This covers real resolution/occlusion and the final display; it is labeled controlled light input,
  not evidence that a particular scenery object emits that light. Several coincident/nearby distinct sources must produce
  nonuniform falloffs and saturated overlap. Do not use the internal `z == 0x7FFF` screen-coordinate convention.
- **Clipping/occlusion:** move the camera so known sources cross all viewport edges/corners, then add a fixed occluder saved
  in one hashed park loaded by all binaries. Preserve toolbar/UI and viewport-origin alignment. Require both lit and unlit
  regions, and inspect edge seams and occlusion contours manually.
- **All eight falloffs and varied intensities:** a separate backend fixture can use public
  `ResolveLightCommandForCanvas`/`RasterizeResolvedLightCommands` as the frozen mathematical reference against GPU compute
  for lantern0–3 and spot0–3, including odd extents and edge clipping. This isolates accumulation arithmetic, but cannot replace
  actual-UI source discovery/frozen-display comparison.
- **Temporal/interaction families:** source removal/linger, palette/night changes, resize, overlapping UI windows,
  fractional scaling, then actual lamps/shops/staff/vehicles and lightning. Record and compare every transition ordinal,
  including the immediate resize frame where a CPU fallback currently may be attached.

Keep the first implementation to one real entrance-lit night sequence and its fresh-process repeat. This resolves the
largest evidence gap before expanding into controlled emitters or changing LightFX production behavior.
