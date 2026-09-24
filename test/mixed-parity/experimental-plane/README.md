# Diagnostic plane envelope: exact retained-art experiment

Staged shader overlay using the existing mixed pipeline, shared device, atlas, upload ring,60-byte sprite command buffer, status/indirect buffers and original corpus. No new renderer/service/device, meshes, textures, CPU draw list or selected-image stream is introduced. No production shader is replaced. Runtime admission remains closed.

## Geometric derivation

Let camera-rotated world coordinates be X,Y,Z. Isometric projection is `u=Y-X`, `v=(X+Y)/2-Z`. Use the same depth `D=X+Y+Z` for all components:

* A horizontal surface at authored height h has `D_horizontal(v)=2v+3h`.
* A vertical billboard through a fixed ground anchor with `s=X+Y` has `D_vertical(v)=1.5s-v`.
* Upright sprite pixels below their authored base become a contact surface: `D=max(D_vertical,D_horizontal(baseZ))`. This keeps original pixels that depict feet, trunk contact or shadows, instead of burying them below terrain.
* A finite vertical column occupies the interval `[baseZ,baseZ+span)`. Its final unit centre is `baseZ+span-0.5`; its envelope is `min(max(D_vertical,D_horizontal(baseZ)),D_horizontal(baseZ+span-0.5))`. Source support segments have inclusive bounds sizes5,9,15, hence spans6,10,16. This is the explicit discrete segment interpretation; it has not been generalized to arbitrary authored art.

Terrain/path/rail planes use authored draw Z, not the compatibility bounds' artificial +1 path Z. Tree anchors use their authored local drawing offset transformed from the camera-facing tile corner; peep anchors are raw x/y/z; support axes use their authored local offset. Body/accessory share their plane, with existing stable component tie order. No per-family depth offsets or fitted coefficients are used.

The bounded GPU classifier recognizes broad flat parts (`sx>0 && sy>0 && sz<=min(sx,sy)`), zero-width finite columns (`sx==0 && sy==0 && sz>=0`), and other upright billboards. It reproduces only this finite recipe's semantic roles. A live implementation should publish an explicit authored primitive role rather than infer arbitrary scenery semantics from aspect ratios.

## Artifact evidence and remaining ties

The artifact-only original-order replay reproduces all16 original indexed rasters exactly. The finite-envelope prediction yields canonical legacy phase0 rotations0..3 `[0,0,6,5]` differing pixels and phase1 `[0,0,2,5]`. Stable controls are `[0,0,6,48]` and `[0,0,2,48]`; their r3 additional43 pixels are the known stable-vs-legacy policy disagreement.

All18 residual canonical pixels are equal-depth contact ties between handyman and path at Z32. They are not tolerated or accepted. The current owner ordinal gives the handyman precedence while original legacy order gives the path precedence. A pairwise rule using whether the vertical anchor lies inside the horizontal surface's actual footprint removes those particular ties but creates11/1 pixels in other integer-lattice captures (16/3 with physical pixel centres). Thus there is **no complete generic tie solution in this slice**. Do not change to a family-rank offset or raise the path plane to33: the latter introduces additional errors. Manual review and a principled coplanar contact relation remain necessary.

Full alternative-candidate counts and mismatch pairs are in `obj/vulkan-parity/mixed-depth-plane-review.json`; source derivation is in `mixed-depth-plane-review.py`. Prediction buffers are under `mixed-depth-plane-predictions/`, with `.physical-centre-finite-support.indexed.bin` matching the staged shader model. This rejects particular depth hypotheses, not all possible scalar or plane orderings.

## Paired shader ABI

Only the diagnostic shaders interpret this encoding:

* `SpriteCommand.depth`: `(relativeAnchorSum+2048)*1024+owner*4+part`, where `relativeAnchorSum=s-2*(cameraWorldY-clipY)`.
* palettes high byte: authored baseZ; lower24 bits retain ordinary remap rows.
* effects high byte: low2 bits plane kind0 horizontal/1 upright/2 finite; upper6 bits span. Lower24 effects/colour bits are unchanged.
* Raster location9 is a flat `ivec4` carrying those facts and the stable tie. The unused ordinary rectangle vertex shader outputs kind3, preserving its original fragment depth and keeping the shared pipeline interface valid.

Fragment depth is computed at pixel centre. In framebuffer coordinates subtracting the common `2*(cameraWorldY-clipY)` from D gives equivalent ordering. Four times D uses only integers: horizontal `d4=8*pixelY+12*baseZ`; vertical `d4=6*relativeAnchorSum-4*pixelY`; pixelY has half-unit centres. Encode `(d4+8192)*1024+tie` and map to `1-(encoded+1)/2^24`. This retains quarter-unit depth before a10-bit tie. All encoded integers and normalized D32 values are exactly representable within the guarded range.

The diagnostic emitter requires clip origin0, viewport at most256x256, baseZ0..127, anchor sum[-1024,1023], span<=63 and finite segment end<=128. It rejects attached static parts. Those guards keep the encoding in range; they explicitly exclude general world/4K admission. The original192x128 zoom0 corpus fits unchanged.

## Host and execution

The shared host already includes the two late-fragment depth synchronization scopes and the diagnostic metadata environment switch, first qualified in build63. The pipeline/emitter now reside under test/mixed-parity; the suite is ExperimentalVulkanMixedFixtureTest and is excluded from the ordinary gate. The fragment writes `gl_FragDepth`; the clear-to-depth and external subpass dependencies include both early and late fragment tests.

Root owns all compilation/device execution:

```
python test/mixed-parity/experimental-plane/build-plane-probe.py --test-build-receipt <current-isolated-host-build>/receipt.json --glslc <installed-glslc> --output <fresh-probe-directory>
python test/mixed-parity/experimental-plane/run-plane-parity.py --build-receipt <current-isolated-host-build>/receipt.json --mixed-corpus-summary obj/vulkan-parity/mixed-corpus-01/receipt.json --plane-probe-receipt <fresh-probe-directory>/receipt.json --output <fresh-run-directory>
```

The builder pins the unchanged base shader/host source receipt, overlay generator, all4 overlay shaders, compiler/DLL inputs and compiled SPIR-V. The runner rechecks those inputs and the accepted corpus, copies shaders into an isolated raster directory, requires validation and synchronization checks, supplies `OPENRCT2_MIXED_DEPTH_MODEL=plane-envelope-v1`, and retains both passing and failed reports. Its proof explicitly identifies the overridden base scalar and the overlay model. Canonical legacy reports and stable negative controls have separate summaries. The existing C++ experiment still compares all16 and therefore returns failure; no result is silently approved and no comparison tolerance is introduced.

## Cost and limitations

The prototype adds no CPU world upload bytes or command stride, and no device allocations beyond the existing pipeline. GPU emission adds a few arithmetic/classification operations per component. Fragment work adds integer affine evaluations, min/max, and depth encoding after indexed coverage/remap. Writing fragment depth can reduce early depth rejection and increase overdraw cost; this experiment is not a performance measurement.

A general implementation needs explicit primitive semantics, slopes and curved rails, contact/surface relationships, wider coordinate/depth budgets, stable identity ties, catalog lifetime, and general rotations/zoom/clip coverage. Ground contact shadows are not uniquely recoverable from a colour-only sprite. Compatibility peep bounds also are not a physical body height: clamping every upright billboard to those bounds regresses the balloon/support overlap. Keep this experiment outside production admission until those contracts are established.

## Preserved checkpoint

The pre-isolation build63 plane-run-01 exercised all32 GPU samples and64 layer reports. All32 indexed buffers exactly match the physical-centre-finite-support artifact prediction; all32 RGBA buffers equal its frozen-palette expansion. Validation activated, reported no messages, GPU statuses were healthy, inputs remained unchanged, and every repeated sample uploaded zero world source bytes. Canonical legacy retains18 differing pixels across8cases; stable r3 retains its43-pixel policy disagreement in addition to5 contact ties. No exception was accepted. `reference-result.json` preserves counts and hashes without copying licensed sprite pixels.

This versioned archive regenerates the same semantic overlay after test-only source relocation and selects the renamed experimental suite. Its new paths/source hashes require a fresh compile receipt; historical build63 evidence is not a claim that a migrated binary has been executed. Root owns any future build/GPU qualification.

Artifact-only reproduction uses `analyse-structure.py --corpus <accepted-corpus> --output <analysis-directory>`, followed by `analyse-planes.py --corpus <accepted-corpus> --output <analysis-directory>`. These scripts never launch the game or GPU. They retain failed hypotheses as evidence; they are not production rendering inputs.
