# Park preview Vulkan visual review

Reviewed 2026-09-23 from `obj/vulkan-parity/auxiliary-preview-upstream-01`. The reviewer opened the full 250×200 `comparisons/r0/reference.png`, `candidate.png`, and `diff-amplified.png` directly. No build, game, or GPU work was launched for this review.

**Manual result: pass for this populated entrance-camera preview.** Both images show the same entrance arch, crowded paths and guests, planted scenery, ride structures, yellow track, ground and map edge. The scene is populated rather than an empty or uniformly cleared target. No visible displaced sprites, missing foreground, palette difference, or clipping discrepancy appears between the two images. The amplified difference is uniformly black.

The runner independently reports zero differing indexed pixels, palette entries, and RGBA pixels, with no failure and no nonzero difference bounds. The current lane invokes the actual `generatePreviewFromGameState` entry point and serializes its returned `PreviewImage` indices for inspection. Its service request is named `park-preview` and requests indexed output only.

The folder label `r0` is a single-case label, **not the actual camera rotation**. The production diagnostic records:

| Field | Value |
| --- | --- |
| Park | `EverythingPark.park` |
| Camera source | First park entrance, offset by `(16,16,32)`, with reversed entrance direction |
| World position | `(3984,8112,368)` |
| View position | `(-12346,1496)` |
| Actual rotation / zoom | `1` / `1` |
| Extent / flags | `250×200` / `0` |
| Source tick before and after | `3133831` |
| External reference revision | `b80a4a84e92be8e07904b38d1032d0bb88280bb4` |

Artifact SHA-256 values observed at review time:

| Artifact | SHA-256 |
| --- | --- |
| `comparisons/r0/reference.png` | `e9d09bdbfa2cb2b1a4ac98e936ed45213daef7a9bf5a927f3fba78c34ae48bfe` |
| `comparisons/r0/candidate.png` | `e9d09bdbfa2cb2b1a4ac98e936ed45213daef7a9bf5a927f3fba78c34ae48bfe` |
| `comparisons/r0/diff-amplified.png` | `57a9334fb7c341c261ef710fa8aec9ca9c20aa27a5b38137c4f94c693245ee33` |
| `summary.json` (status `exact-match-pending-review`) | `acfff6a2e5b0ec35289974c37c09506b426ed17150642493ab6c9ba636faca65` |

Limits: this is one static entrance-derived screenshot and does not qualify the main-viewport camera branch, other parks/rotations, animated transitions, failure recovery, save serialization, presentation pacing, TPS, or retained-world GPU preparation. Current loaded CSG/G1 observation and clean synchronization-validation activation are runner requirements; the external upstream process has path-only asset plumbing. The reference and current source/build/art receipts remain the runner's provenance authority. This review does not replace their checks or introduce a pixel exception.
