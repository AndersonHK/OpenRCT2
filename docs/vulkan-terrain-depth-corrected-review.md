# Corrected terrain depth review

Reviewer: `balloon_visual`, 2026-09-19. This addendum to [the actual-camera failure review](vulkan-terrain-camera-corrected-review.md) covers the completed corrected terrain matrix: **48 passing processes and 156 captures**. The [machine-readable evidence](vulkan-terrain-depth-corrected-review.json) pins every summary, capture/report, raw indexed/RGBA/PNG hash, run/build receipt, actual camera pose, admission metadata, validation log and manually inspected image.

I opened the corrected frozen and Vulkan full-resolution PNGs for both `baseline-0` and `baseline-1`, the preserved failed Vulkan first frame, its amplified RGBA difference image and indexed difference mask. The former 240-pixel triangle of unfiltered grass at exclusive bounds `(0,608)-(30,623)` no longer overrides the bottom-left translucent guest panel. Its grass now receives the same filtering as the frozen reference. The grass apex near `(608,320)`, diagonal boundaries, fine texture, dark exterior, toolbar and both corner status panels also match visibly. No exception or tolerance is accepted.

`ui-camera-terrain-depth-vulkan-r1z0-01` and `ui-camera-terrain-depth-frozen-r1z0-01` pass. Both corrected frames are byte-identical to frozen in indexed and physical RGBA layers. Camera contract version 2 records the intended explicit position `(-672,-336)`, rotation 1, zoom 0 after warmup and after each paint. Vulkan frames 4 and 5 admit native world surfaces with 1,024 records. The Vulkan capture log shows the validation layer active; synchronization validation is configured and no validation-error, validation-warning, VUID or synchronization-hazard matches appear.

The preserved `ui-camera-terrain-vulkan-r1z0-01` remains a failed run with 240 differing pixels per layer in both frames. It is not reclassified. Corrected output comes from UI31 and shader build43; the frozen reference comes from frozen UI18, all pinned in the JSON.

## Completed matrix and visual coverage

All eight cases ran in frozen software, current software and Vulkan lanes, with two independent fresh-process repetitions. Seven cases capture two baseline frames per process; overlap captures two six-state sequences. All 156 raw indexed and physical RGBA outputs match their corresponding first frozen state exactly. Every recorded expected, post-warmup and post-paint camera pose matches camera contract version 2.

| Case | Actual pose: rotation / zoom / position | Vulkan admission | Manual observation |
| --- | --- | --- | --- |
| r0z0 | 0 / 0 / `(-480,-240)` | Native | Upward grass apex, diagonal edges, texture and translucent corner panels match. |
| r1z0 | 1 / 0 / `(-672,-336)` | Native | Rightward grass tip and repaired guest-panel filtering match. |
| r2z0 | 2 / 0 / `(-480,-432)` | Native | Downward apex, upper grass field and dark exterior match. |
| r3z0 | 3 / 0 / `(-288,-336)` | Native | Leftward tip, right grass field and lower-right panel filtering match. |
| r0z1phase | 0 / 1 / `(-959,-557)` | Native | Actual odd phase is preserved; zoomed diamond edges and finer texture match. |
| grid | 0 / 0 / `(-480,-240)` | Ordinary paint | Grid line spacing, intersections and clipped edges match. |
| smooth | 0 / 0 / `(-480,-240)` | Ordinary paint | Flat grass and boundaries match; this flat fixture does not exercise visible smoothing between different surfaces. |
| overlap | 0 / 0 / `(-480,-240)` | Native | Every window state matches in ordering, occlusion, clipping, text and borders. |

I opened representative first frozen/Vulkan pairs for every case and all six overlap sequence states: empty UI, research in front, finances in front, partially clipped finances, research only, and restored UI. The left-clipped finances labels and borders, research occlusion, and exposed terrain after restoration agree. Repeated captures and all fresh-process outputs are bound to those manually inspected states by both raw image hashes. There are no new differences or accepted exceptions.

All 16 Vulkan process logs show validation activation and no validation-error, validation-warning, VUID or synchronization-hazard matches; synchronization validation settings are pinned. Of the 52 Vulkan captures, 44 admit native terrain with 1,024 records, while the eight grid/smoothing controls explicitly use ordinary paint. Exact control pixels do not imply native support for those modes.

This completes visual qualification of the bounded corrected camera/depth matrix. Full terrain migration, zero CPU traversal, nonuniform and larger-park coverage, the separate B1 regression, global pixel parity and the VSYNC/CPU/bandwidth/TPS performance gate remain open.
