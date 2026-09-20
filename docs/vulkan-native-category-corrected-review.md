# Corrected native category visual review

Status: corrected balloon review complete and renewed on UI32; the corrected terrain camera/depth matrix is [separately reviewed](vulkan-terrain-depth-corrected-review.md). Reviewed by `/root/balloon_visual`; exact evidence hashes and telemetry are in `vulkan-native-category-corrected-review.json`. No renderer exception is introduced.

UI28 run `ui-vulkan-gpu-balloon-v2-baseline-02` passes both captures against frozen `ui-frozen-balloon-v2-baseline-01`: zero differing indexed or physical pixels. Manual inspection of the corrected comparison samples shows matching intact/popped balloon sprites, the central overlapping group, textured grass, map clipping, and toolbar/status UI. Khronos validation activation is recorded and the capture log contains no VUID, synchronization hazard, validation error or validation warning.

| Capture | Native viewports | Submitted | CPU balloon sprite calls | Scene upload bytes | Sprite upload bytes | Epoch / sequence | GPU revision |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| baseline-0 | 1 | 1 | 0 | 0 | 0 | 5 / 4 | 2 |
| baseline-1 | 1 | 1 | 0 | 0 | 0 | 5 / 5 | 2 |

Both submitted epoch/sequence pairs match the named admitted 24-balloon publication. The terrain category reports native admission. These steady paused captures demonstrate that the corrected bounded balloon path preserves pixels without duplicate CPU balloon sprite calls or repeated scene/sprite uploads. They do not establish zero total CPU work: the diagnostic census and other rendering work remain outside those counters.

The fresh baseline run `baseline-03`, full-paint overlap runs `overlap-02/03`, and incremental overlap runs `overlap-incremental-02/03` also pass. Together the six runs cover 52 captures: four baseline captures and 48 overlap captures. All match in both raw pixel layers, have one admitted/submitted native viewport, zero CPU balloon sprite calls, zero scene/sprite upload bytes, and matching publication epoch/sequence. Overlap sequences run from 4 through 15, epoch 5 / GPU revision 2. Each fresh-process repeat matches its corrected predecessor; incremental runs additionally match frozen full-paint overlap. All six validation logs are active and clean.

The agent inspected all five distinct reference/candidate image pairs: unobstructed scene, research window in front, finances window in front, partially clipped finances window, and research-only window. Window ordering, text, border clipping, and exposed balloons match without stale areas or extra balloon pixels over windows. Exact PNG hashes in the JSON bind repeated samples to these manually inspected images; additional fresh baseline and incremental clipped samples were also viewed.

Global Gate P remains open. This fixture admits a constrained legitimate balloon family on flat terrain at rotation 0 / zoom 0; it does not qualify arbitrary entities, terrain, camera states, frame time, sustained vsync, or large-park TPS.

The failed `ui-vulkan-gpu-balloon-v2-baseline-01` evidence is preserved. Its pixels matched while it recorded 44 CPU balloon sprite calls. The fixed per-tile reset also affected `SurfaceBaseDrawn`, so previous flat-native terrain screenshots prove composed equality only and cannot establish native-only surface correctness. The required corrected terrain cases, fresh repeats and manual views have since completed in the separate camera/depth review; the historical evidence is not reclassified.

The subsequent terrain attempt `ui-vulkan-native-flags-terrain-r0z0-01` is not qualified: its selected older reference lacks required camera/asset metadata. A separate harness finding also leaves earlier explicit odd-phase and rotation-offset coverage unqualified because warmup could restore the main window's saved camera position. Corrected terrain references and candidates must assert the intended post-warmup camera. The B1 cases reviewed here use the intended saved-camera mode and are not claims about those explicit camera poses.

- [x] Corrected B1 baseline 02: two captures, exact pixels, zero CPU balloon sprite calls, zero steady uploads, manual sample review.
- [x] B1 baseline fresh-process repeat 03.
- [x] B1 overlap 02 and fresh repeat 03, with manual review.
- [x] B1 overlap-incremental 02 and fresh repeat 03, with manual review.
- [x] All eight corrected terrain native/control cases, fresh repeats, and manual review, recorded separately.

## E5/depth checkpoint renewal

UI32 passes another **six processes and 52 captures**: `ui-e5-native-balloon-{baseline,overlap,overlap-incremental}-{01,02}`. Independent receipt review confirms every PNG, indexed buffer and physical RGBA buffer equals the corresponding frozen baseline/full-overlap image, including fresh-process repeats and incremental painting. All six logs show validation activation without validation errors, warnings, VUIDs or synchronization hazards.

Every capture has exactly one admitted and submitted native viewport, 24 admitted balloons, matching publication/upload epoch and sequence, GPU revision 2, **zero CPU balloon sprite calls, zero steady scene upload bytes and zero sprite upload bytes**. The camera remains the intended saved pose `(-480,32)`, rotation 0 / zoom 0, verified after warmup and paint by camera contract version 2. The earlier loaded transient `(-480,3775)` is not mistaken for the captured target.

I directly opened five new/frozen image pairs: unobstructed balloons, research in front, finances in front, partially clipped finances, and research only. Intact/popped balloon shapes, the central overlapping group, terrain texture, window occlusion, clipped text/borders and exposed balloons agree. No stale terrain or balloon pixels appear over windows. Every repeated capture is hash-bound to one of these manually inspected states in the JSON renewal.

This renews bounded B1 parity after the E5 service and terrain-depth changes. It introduces no exception and does not qualify arbitrary entities, terrain features, sustained VSYNC, CPU/bandwidth headroom or large-park TPS; global Gate P remains open.
