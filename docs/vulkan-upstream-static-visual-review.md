# Upstream/current static screenshot visual review

Reviewed 2026-09-23 by reviewer one. The four current Vulkan screenshots match the independently built upstream software screenshots exactly and look like populated, relevant tree/track/support scenes. No obvious ordering inconsistency was visible in these static views. This is a narrow successful comparison, not evidence that the original reported defect has been reproduced or fixed.

## Evidence identity

The immutable capture receipt is [`upstream-vulkan-static-02/summary.json`](../obj/vulkan-parity/upstream-vulkan-static-02/summary.json), SHA-256 `1d49620d3f7c541b29b6f067d85b7dd586364062e69c36b100c0f2f66eda3b27`. Its status remains `exact-match-pending-review`; this separate document records the completed manual review without changing the receipt.

- Upstream source revision: `b80a4a84e92be8e07904b38d1032d0bb88280bb4`.
- Upstream build receipt SHA-256: `965b40dff3363291dc99c4034b412d8d5148c4f4c6afdbda224988de3f32b025` (`upstream-screenshot-build-01/receipt.json`).
- Current screenshot build receipt SHA-256: `a72b2d4af56c5bbd39280a0965601149924251ea644ec978b7b3ba90acf0789e` (`vulkan-only-cli-build-01/receipt.json`).
- Shader build receipt SHA-256: `033ae748b4e1a81969a8c9c7dc7828049fbfa89714f81a83d5b4fcc35a3d35cd` (`build-66/receipt.json`).
- Runner SHA-256 recorded by the run: `7dfa79b873eb9c3a685b9165c361cad9434c9d39896318cb51247b9b32ac2a2a`.
- Unmodified `EverythingPark.park` SHA-256: `c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`.
- Every process used identical initial configuration bytes, SHA-256 `c6004c682870b115079a315ef1eb354896cb6600381a86bf8ea0cc35b94fdfd5`.

The camera is explicitly `(4944, 6736, 336)`, zoom 0, at rotations 0–3, with a 640×480 output and transparent screenshot configuration disabled. The prior locator associated this area with a birch on tile `(154,210)` and adjacent elevated wooden track on `(155,210)`, ride 157. No tree substitution or other world mutation was used here. Visual identification below concerns the displayed scene rather than independently proving those object IDs from pixels.

The frozen package supplies verified assets and runtime dependencies only. Its renderer binary and source are not the reference for this comparison. The upstream lane uses the separately built upstream CLI with path-only wrapper plumbing. Both lanes use the same data overlay, park and original-game art paths. Current diagnostics confirm loaded CSG/G1; the upstream wrapper has no independent post-load art observation hook.

## Manual inspection

I separately opened all eight full reference/candidate PNGs and all four amplified difference PNGs under [`comparisons`](../obj/vulkan-parity/upstream-vulkan-static-02/comparisons). Each pair is visually identical. All amplified differences are entirely black. The receipt reports zero differences in decoded indexed pixels, palette entries and RGBA pixels for every rotation; the reviewed reference and candidate PNG file hashes also agree.

| Rotation | Scene observed in both images | Indexed / palette / RGBA differences |
| --- | --- | --- |
| 0 | Large diagonal wooden lift structure, orange-railed track across the foreground, dense lattice supports, several birches, guests and balloons. Tree silhouettes overlap support columns and track; the tree/structure occlusion looks plausible. | 0 / 0 / 0 |
| 1 | Lift structure across the upper half, a curved elevated track across the middle, trees among supports, dark tower, red-roofed ride buildings and queues. Canopies, trunks and lattice intersections show no reference/candidate distinction or obvious isolated misordering. | 0 / 0 / 0 |
| 2 | Elevated wooden track and supports across the top and centre, central dark tower, trees on both sides, red-roofed buildings, paths and queues. This is another populated angle with meaningful tree/support overlaps, not an empty or loading frame. | 0 / 0 / 0 |
| 3 | Broad wooden banked curve and dense support field, birches at the right and lower edge, guests and balloons along the bottom path. Canopy silhouettes remain continuous where visible; foreground/background relationships look plausible at this scale. | 0 / 0 / 0 |

There were no differing regions requiring a local explanation or exception. This judgment does not assert that every overlap in the upstream image is intrinsically correct; it records plausible appearance and exact agreement for these four views.

## Reviewed image hashes

Paths below are relative to `obj/vulkan-parity/upstream-vulkan-static-02/comparisons/`. Hashes were independently read from the reviewed files after capture completion.

| Reviewed files | SHA-256 (each file) |
| --- | --- |
| `r0/reference.png`, `r0/candidate.png` | `617fe21e72a53515584b4ecb3dd18a2390ac4cb5fb8d47c046a1fa1ca0bbdfe7` |
| `r1/reference.png`, `r1/candidate.png` | `6041eda513abfcd4731dbacc1136ccd4e5fc4cb7deabf832ca84c09778bcf708` |
| `r2/reference.png`, `r2/candidate.png` | `8f6a4af6c0a1d9c2d59cd4182f74c166b916ce6fc466183912104baa7d23afd8` |
| `r3/reference.png`, `r3/candidate.png` | `764abe5e25800d8055baf20faa238963742ee9b5122fd400d3caa3f97f4d3a21` |
| `r0/diff-amplified.png`, `r1/diff-amplified.png`, `r2/diff-amplified.png`, `r3/diff-amplified.png` | `9347fe86f6b448518e30d671057b4ece3e7f2cf3906488168f63736a4c990bc0` |

## Runner audit and qualification

Read-only inspection covered `run-upstream-screenshot-comparison.py`, the reused evidence/PNG/build-provenance helpers in `run-screenshot-parity.py`, current `ScreenshotMain.cpp` diagnostics and `RenderServiceFactory.cpp`, and the current and archived upstream screenshot command/viewport parsing. The nine positional screenshot arguments correctly supply explicit X/Y/Z, zoom and rotation in both source versions. The configured current factory loads shaders from the isolated data root's `shaders/vulkan` directory; an environment shader override is not required for this factory. Current diagnostic validation uses its Vulkan-only schema rather than removed renderer-enum fields.

The first attempt stopped before launch because the upstream build has an empty runtime DLL inventory and the evidence helper's default required a nonempty tree. The second attempt uses `require_nonempty=bool(dlls)` while retaining exact inventory equality. This correctly permits the receipt-qualified statically linked upstream executable and does not waive DLL verification. No further contract or invocation blocker was identified for this positive-coordinate, fixed-size run.

All eight processes exited successfully. Each of the four current Vulkan cases recorded Khronos validation activation, zero validation messages, and valid owned indexed/RGBA capture diagnostics. Synchronization validation settings and inputs were pinned. The receipt reports no failures, including its final input audit. Upstream software receives no Vulkan environment controls.

Scope is limited to static offscreen screenshots of one park area, four rotations, zoom 0 and 640×480. It does not qualify moving vehicles, alternating order across simulation updates, other scenes, HDR, the Options UI, main-window presentation, 4K VSync, CPU/GPU bandwidth, or TPS. It does not establish that the original pink-canopy defect was fixed. No additional game, build, test or GPU execution was performed for this review, and no immutable capture artifact was edited.
