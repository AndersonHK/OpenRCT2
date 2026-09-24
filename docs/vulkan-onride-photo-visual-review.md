# On-ride photo original-art review, build120

All **96 photo specimens match exactly** in the fixed, unmasked indexed rectangles: twelve specimens, four rotations, zoom0/1, totaling 8,908,800 compared pixels (rectangles can overlap). I manually inspected all 32 pages `objects-r{0..3}-z{0..1}-page{00..03}.png` under `obj/vulkan-parity/photo-states120-specimens-01`, including the upstream/native/difference columns for every specimen.

The normal Looping and small Wooden photo signs/cameras have the original positions, directional faces and heights. Timeout0 shows the camera; timeout1 and3 show the original flash, including directional partial occlusion. Ordinary colours, construction-ghost remapping, rail/platform geometry and camera/track overlap agree at both zooms. No remaining camera, flash, platform, body or sampling defect was found in these samples. Native import assertions verify the saved raw timeout before rendering; these are original upstream painter references, not shader-derived expected images.

Full-frame parity still **fails**: 187,056 differing indexed pixels per zoom0 view and 48,608 per zoom1 view. I also inspected the full r0 comparisons at both zooms. The visible difference is the technical-map boundary/outer cliff skirt (native textured edge versus original black boundary), outside the photo specimens. The difference-mask hash is identical across all four rotations at each zoom, independently of specimen placement. This is the previously user-accepted skirt behavior; full-frame failure evidence remains intact and no pixels, masks or tolerances were altered. The native capture receipt reports no validation diagnostics. This review does not qualify every ride style, tunnel interaction, vehicle-triggered timeout transition or whole-park rendering.

## Authoring12 to13 preservation

An independent binary-table comparison checked 214,276 prior style/type/sequence/direction/distinct raw-state rows. After removing the new photo marker and tunnel metadata and renumbering surviving parent references, **all previous drawable recipes are unchanged**: image IDs, offsets, bounds, sizes, colour roles and parent relationships. No previously admitted style/type was lost. Supported style/type pairs increase 5036â†’5039: onRidePhoto/type114 for styles9,10,79 (classic Wooden, classic Wooden Twister, Wooden).

Photo markers occur in 1,664 previously supported rows. Those same rows gain the original `Paint2` square tunnel request; no other type has tunnel metadata changes. Sixty-four Splash Boats rows already had a square request from the called flat-track painter and now correctly contain its second source-authored request from `Paint2`. This is preserved source call order, not an inferred extra portal. The source check is `PaintSplashBoatsTrackOnRidePhoto` in `src/openrct2/paint/track/water/SplashBoats.cpp`, which calls both helpers.

The audit report is `obj/vulkan-parity/photo-states120-review/authoring12-to13.json`, SHA256 `79fbe0bbd61cfd0f5c0025afb9802c3806db284c3daf942f01bebea701b4049b`. This is definition preservation, not a raster proof for all 5039 admitted combinations.

## Immutable evidence

All 96 specimen strips, 32 reviewed pages, source/capture/comparison summaries and independent audits are pinned by `obj/vulkan-parity/photo-states120-review/reviewed-artifacts.json`, SHA256 `946b1857f7341667a856791fe6835a8c5e69464b433512fa3df860684842b383`.

| Artifact | SHA256 |
| --- | --- |
| `photo-states-upstream-corpus-01/manifest.json` | `930b9e46e7a474ba5d9c4863600a9203bb2b44b5b85c613083f277d28bc65d8f` |
| `photo-states120-art-01/summary.json` | `f48a23ab6b63eda5c879065ef0b86fb8de68ebd0571b9e662562d57c169011b2` |
| `photo-states120-compare-01/summary.json` | `5a87c268f1baaf9dbe0aa6de9f72a613a774731aa5d72c230f3339a497bdc2d5` |
| `photo-states120-specimens-01/summary.json` | `e9bedf33b6bbdc869296374199ed7e7f7c1f86d85e60e7d61036bb5a0b9a1078` |
| authoring12 `native-track-recipes.bin` | `cfbc6fd005a6322afd8c62de9ee784afd322b8898ec61f7060a0272126d6a4b7` |
| authoring13 `native-track-recipes.bin` | `8559e931d3b1f02ab97fa211cdcecd445899b6f0fe6853bbb012a28c1be761d1` |

No source changes or GPU executions were made during this review.
