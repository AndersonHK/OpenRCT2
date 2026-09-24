# Custom-image ordered alias visual review

Reviewed all 15 saved cases from [run58's alias report](../obj/vulkan-parity/run-58/samples/custom-image-alias/report.json). No visual divergence or malformed array was found: every immediate and final index equals its explicit expected index, with zero tolerance. The three alias tests completed successfully in [tests.log](../obj/vulkan-parity/run-58/tests.log). **Run58 overall failed** with exit code 3 after a later TrackDesignPreview fixture reached `GfxGetG1Element` while headless. This review does not turn that failed run into a passing suite or claim broad migration qualification.

The reference arrays encode the original top-to-bottom, left-to-right bitmap scan semantics from revision `ba5b9d8a92a3ae6ca98c570c7d23f6114c2a470e`. They are not outputs from a fresh upstream/software execution. Actual arrays were exported by the production script callback test after the shared Vulkan service completed, including the immediate read inside each callback and the final published image. The original report was left unchanged; no test, game, GPU capture or callback was rerun for this review.

The [analytical renderer](../obj/vulkan-parity/custom-image-alias-visual-review-01/render_review.py) checks every extent, array length and byte range, compares both actual arrays exactly, and emits 60 native indexed PNGs plus four comparison sheets. It uses the report's real 256-entry palette. Index zero is displayed over a checkerboard according to the recorded `transparentIndexZero` policy; raw palette RGBA entries remain unchanged in the source evidence. Enlargement uses nearest-neighbour sampling. Numeric annotations sit outside the image regions. No pixel correction, colour approximation or tolerance was applied.

I opened and visually inspected [sheet 1](../obj/vulkan-parity/custom-image-alias-visual-review-01/small-cases-1.png), [sheet 2](../obj/vulkan-parity/custom-image-alias-visual-review-01/small-cases-2.png), [sheet 3](../obj/vulkan-parity/custom-image-alias-visual-review-01/small-cases-3.png), and the [complete multiple-slice target](../obj/vulkan-parity/custom-image-alias-visual-review-01/multiple-slices-full.png). Each shows seed, expected, immediate and final output side by side.

| Case | Extent | Visual observation; immediate and final both match |
| --- | --- | --- |
| right | 4×1 | First index 20 propagates through the row; the earlier snapshot-copy pattern is absent. |
| left | 4×1 | Indices advance left and the last pixel stays 23. |
| down | 3×3 | The first row propagates into both lower rows. |
| up | 3×3 | Rows move upward and the final row is retained. |
| diagonal | 3×3 | Updated centre index 20 propagates to the bottom-right corner; surrounding pixels retain the expected pattern. |
| overwritten zero | 4×1 | Both transparent holes are replaced through forward propagation. |
| transparent source zero | 4×1 | The first transparent pixel remains a hole; index 21 propagates through the rest. |
| repeated remap | 4×1 | The sequence is `2,244,10,10`, including the second remap of a previously written value. |
| local clip | 4×1 | Only the clipped middle pixels change; the final index 23 remains. |
| negative local clip | 4×1 | Clipping advances the source and retains the final pixel. |
| signed16 placement | 4×1 | Wrapped placement produces the same propagation as the right-copy case. |
| nested switch | 4×1 | The propagated first three pixels remain 20 and the resumed outer drawing writes 24 last. |
| multiple scan slices | 128×130 | The seed's first column expands across every row; the entire expected/immediate/final image is index 20, with no residual stripe or slice boundary. |
| sprite offset +1 | 4×1 | Positive source metadata offset produces the right-copy recurrence. |
| sprite offset -1 | 4×1 | Negative source metadata offset produces the left-copy result. |

The multiple-slice case copies 16,510 pixels in three bounded dispatches and crosses row and packed-word boundaries. The full 128×130 target was inspected, not only a selected crop. All 15 cases have zero immediate mismatches and zero final mismatches in the [review manifest](../obj/vulkan-parity/custom-image-alias-visual-review-01/review-manifest.json).

Evidence SHA-256 pins:

| Artifact | SHA-256 |
| --- | --- |
| Original alias report | `04280a904e7ba4b66124b4ecd853be1d3f4c06ec740abbcf39b22effcb9b59cb` |
| Failed run58 summary | `a53a2c24c12515df6981c80ae9850d7d496b3aea811c4f35eff9437d01cef604` |
| Build77 receipt | `445e5cda7873aaff09ffb8d71c228c0a9794be8296a1f8910db719d26134bb2a` |
| Review manifest, pinning all 64 PNGs and the renderer script | `42846f77ad87297e5cdc028a1192c99a83897d0c3a49a2b30039ef14cac82376` |

This closes manual inspection of this finite saved alias corpus. It does not qualify every custom-image operation, all devices, the maximum 4M-pixel dispatch workload, large-image tiling, fresh upstream execution, performance, or deployment. The independent target-size/source-atlas limitations remain open, and this review alone is not full-suite qualification. Subsequently build79/run60 passed all 867 tests with clean validation; run59 and run60 reproduce the reviewed report byte for byte (the original report hash above). Earlier failed receipts remain unchanged.
