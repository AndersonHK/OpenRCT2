# EverythingPark main UI visual review

Reviewed by `/root/decode_assets` on 2026-09-19. The initial captures are inadequate large-park coverage despite their old runner's `pass` status. The corrected saved-camera captures provide exact, bounded dense-park coverage. This is a fixture-input correction; no renderer exception is accepted.

## Initial capture: retain the match, reject the coverage claim

Runs `ui-{frozen,current,vulkan}-everything-r0z0-01` agree exactly on indexed bytes and physical RGBA. I viewed the frozen/Vulkan `baseline-0` reference/candidate/diff triplets at both layers. The physical images visibly contain a red RCT1-link warning, mostly empty background and only part of a tall track along the left edge. The diffs are black because both renderers received the same inadequate inputs.

All three capture logs report that the park requires linked RCT1 assets and fallback images will be used. The viewport uses rotation 0, zoom 0, view position `[2860,349]`. Excluding toolbar rows, index 10 occupies 537,974 of 556,800 pixels, about 96.6%. Its Vulkan packet contains 2,063 opaque sprite commands. These images cannot establish the intended dense-park scene or original-RCT1-asset parity.

The harness supplied the custom RCT1 path and recorded input file hashes, but did not put that path into the configuration field read by `GfxLoadCsg`. It also replaced the park's saved rotation/zoom without preserving a useful corresponding camera view. A correct renderer comparison cannot compensate for either input problem. Preserve the initial images and old summaries as history; do not rewrite their numeric result or accept their broad coverage claim.

## Corrected capture: saved view with verified CSG loading

Runs `ui-{frozen,current,vulkan}-everything-saved-02` use current UI build 15 and frozen UI build 12. The harness now assigns the explicit RCT1/RCT2 configuration paths before initialisation, fails if supplied RCT1 assets do not load, and records `assetState.rct1CsgLoaded=true` and `rct1Required=true`. `cameraMode=saved` preserves the park's rotation 3, zoom 2 and view position `[6218,-1330]`. The simulation tick remains 3,133,831 and the logical/physical canvas is 960×640.

I viewed both `baseline-0` and `baseline-1` physical and indexed reference/candidate/diff triplets, twelve PNGs in total. The screen now contains many varied rides, overlapping tracks and supports, paths, scenery, guests, raised land and water. The toolbar remains intact and the fallback warning is absent. Reference and candidate visually agree throughout; all diffs are uniformly black. The Vulkan packet records 62,651 opaque sprite commands, 55 opaque rectangles and 454 transparent rectangles. It uses ordinary scene paint, with `worldSurfaces=false`; this is not a native-terrain-admission claim.

I independently reread all twelve raw captures across the initial and corrected three-renderer groups. Every frame matches its corresponding frozen frame under the same input version at both layers. The corrected capture logs have no fallback-image warning; root reports Vulkan validation clean. The corrected two captures establish within-process repetition. Separate `ui-{frozen,current,vulkan}-everything-saved-03` runs now also pass against their corresponding run 02 with identical renderer build receipts (UI 15 / frozen UI 12; Vulkan shaders from build 29). I independently reread all six repeat captures: indexed and physical RGBA bytes equal both the corresponding run 02 and frozen run 02. Every repeat reports verified CSG loading and no fallback warning. This establishes independent fresh-process repetition for this saved view; a wider camera matrix remains open.

The [JSON receipt](vulkan-large-park-visual-review.json) pins all nine summary/build/log references, every raw indexed/RGBA/report hash, camera and asset metadata, and the eighteen manually inspected PNGs. Lossless native-resolution review sheets are under `obj/vulkan-parity/large-park-manual-review/`. The original and corrected input versions must not be compared as if their camera or asset selection were identical.

## Scope correction for earlier main UI evidence

Before current UI build 15 / frozen UI build 12, an RCT1 file hash in a receipt proved the file existed, not that the UI process loaded its CSG assets. EverythingPark now demonstrates that distinction directly. Earlier small-park, native-terrain and weather UI captures remain exact evidence for their recorded scene inputs; those reviewed images did not show this warning, but they do not thereby prove loaded original-RCT1-asset coverage. They need new references and fresh-process repetitions with the explicit loading assertion and `assetState` metadata before that stronger claim is made.

This caveat concerns the standalone main UI harness. Separate auxiliary scene or asset-corpus fixtures that explicitly verified their loaded assets retain their own recorded scope. Likewise, one corrected dense saved view does not qualify every EverythingPark camera, rotation, zoom, animated state, window composition, renderer mode, GPU or lighting/weather combination.

Fresh-process run 03 summary SHA-256 receipts:

- `obj/vulkan-parity/ui-frozen-everything-saved-03/summary.json`: `4ccd980b6560b0bbd111a1160bff8ba99fd6e54a3c38e0a0e1369a6bd7e97667`.
- `obj/vulkan-parity/ui-current-everything-saved-03/summary.json`: `66727d9ef46082dd3d220b2c4eff27deee4d9a38c35443cc03299d5d88deca49`.
- `obj/vulkan-parity/ui-vulkan-everything-saved-03/summary.json`: `ae777dd71ee2f07990ec3ec2ec08e2c0c40a29978497d99434a79debdba801fd`.
