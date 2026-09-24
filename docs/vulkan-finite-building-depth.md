# Finite buried building depth: source-backed design boundary

Read-only investigation against build131 artifacts and current authored painters. No production or candidate shader mutation, build, GPU, or tests were executed for this investigation.

## What source actually owns

`src/openrct2/paint/track/thrill/3dCinema.cpp` Paint3dCinemaDome emits one complete ride-car image at base+3 with camera-relative offsets. Six sequence cases emit the same image with offsets compensating the owner tile. Its bounding box starts offset+16 and is24×24×47, while Paint3dCinema sets general support height base+128. The separately authored floor is TrackPaintUtilPaintFloor at base height. The47-high bounds therefore cannot be treated as an exact physical dome/podium volume; they exist to arrange sprites. The image alpha silhouette contains multiple faces and curved dome surface without a depth channel.

`src/openrct2/paint/track/shops/Shop.cpp` emits a complete object sprite at tile origin/base, with ordering bounds(2,2,base)+(28,28,clearance−base−3). If supports exist, a plank foundation is parent and the body is child. This establishes the floor elevation, but does not establish a universal roof/wall mesh for every ride-entry shop image.

`data/shaders/vulkan/world_static_tower_maze_rules.glsl` retains actual authored tower segments at base,base+32,base+64, and separate platform sprites. Tower bounds2×2 do not describe the raster base/platform extent. Existing world_flat_ride_emit classifies short thick bounds as horizontal; a tower body's small2×2 bounds stay upright. The source supplies reliable segment elevations and platform role, not a depth surface for the whole decorative base sprite.

## Confirmed failure mechanism and bounded candidate

Current upright depth is an infinite image-anchor plane D=1.5S−V. Pixels visually representing the front floor or foot of a building lie below the projected anchor and are interpreted as having a physical Z below the known authored floor. Foreground terrain then rejects them, or underground cliff filters recolour them. The131 cinema white wall/podium loss is consistent with this mechanism; it is not proof that all dome/wall depth is correct above the floor.

A concrete bounded improvement would intersect that upright proxy with the known lower floor boundary:

- Upright part: D=1.5S−V, valid above the split.
- Floor part: D=2V+3h, valid below the split.
- Split world-screen height V0=.5S−h; together D=max(upright,floor).

This does not impose family rank or depend on camera panning/order. It keeps the art alpha and sampling intact. It is still an approximate finite-foot model because the source does not describe the real wall/roof/dome surfaces. It must not be called exact building geometry. A top clamp based on the47-pixel cinema ordering height is specifically unjustified and should not be added.

## Exact integration shape if the proxy is selected

Keep the64-byte component payload. Emit two GPU component instances of the same art with two geometric clip flags in spare depth bits: upper upright half and lower horizontal floor half. Preserve the original full sprite bounds for texture coordinates; clamp only geometry Y to the projected split so sampling does not shift. Encode a camera-independent anchor sum and authored floorZ (existing reserved payload needs an explicit schema because reserved.y currently owns local-child counters). Parent/child local layers apply to both halves identically; no second child increment for a geometric split. Count and emission must both produce two records or zero after ordinary art admission. Never read or sort another object's state.

Splitting is necessary with early fragment depth tests: evaluating max only at a quad's four vertices would interpolate the wrong depth across the internal kink. Writing gl_FragDepth in the filter collector would invalidate its current early-Z contract. An explicit two-half quad retains rasterized affine depth and early tests, with at most twice the affected body vertices and no added CPU draw calls.

An alternate clean representation is a per-component small physical mesh descriptor authored alongside sprite rules: floorZ, role/face type, footprint rectangle/convex polygon, roof plane(s), and mapping from sprite screen regions to faces. Shader constructs/rasterizes those physical regions while sampling the same original image. Cinema requires a dome plus podium-specific authoring; generic shop objects require object-level proxy metadata or an author-approved fallback. This is the route to exact geometry rather than reinterpreting painter bounds.

## Qualification and remaining evidence

Use original-art cases8–11 inside and ordinary at everyrotation/zoom0–1. Require the recovered front floor to remain hidden when the whole building is actually buried; a successful visible podium alone is insufficient. Include a foreground wall crossing the body, elevated building floor, and camera pan with no changed owner state. Tower cases16–17 separately prove base/platform clipping; do not automatically apply a cinema workaround to them. Test opaque/filter order and paired-half texture sampling at negative/odd camera coordinates. Root alone executes runtime qualification.

Until physical roles or the explicit proxy are selected, there is no safely derivable exact finite building shader patch from existing fields. This design preserves the concrete missing-information boundary rather than staging a guessed bounds box as an exact correction.
