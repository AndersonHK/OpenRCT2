# Vulkan-only selection visual review

Reviewed 2026-09-23 from saved artifacts in `obj/vulkan-parity/vulkan-only-options-01`. The reviewer opened the actual `empty-ui-0`, `options-display-0`, and `restored-0` PNGs with the image viewer; no application, build, or GPU work was launched for this review.

**Visual result: pass for the captured layout and restoration.** The Options display page has no renderer selector or software choice. Screen mode, disabled display resolution in windowed mode, window scale, frame-rate limit, Show FPS, Multithreading, and Enable HDR10 output (Vulkan) remain legible and aligned. Rendering and Behaviour groups fit without overlap, clipped labels, or a leftover renderer row. The window title, tabs, and close button are intact.

The underlying scene shows the Ferris wheel, entrance buildings, paths, boundary fencing, grass, map edges, and top/bottom toolbars without an obvious missing region or broken sprite composition. Closing Options restores the unobscured scene without a residual window rectangle. This is visual inspection of this scene, not an original-renderer parity claim.

Independent SHA-256 checks of the saved PNG files:

| Captures sharing identical PNG bytes | SHA-256 |
| --- | --- |
| `empty-ui-0`, `empty-ui-1`, `restored-0`, `restored-1` | `afd639f90470c19f3f0c0d16bf151e705edd5fa21961354983422be2bbf8f743` |
| `options-display-0`, `options-display-1` | `809f03e8ea1d864d6404a5fb203c79e24ed9aa3a282022b854ba6528b670b036` |

The runner's `summary.json` reports pass, no failures, exit code zero, and six captures. Its SHA-256 is `551925fbdcf9a8bb35e1e655da8a6e80c4209e8bd7d3d97a2017c9d792564d5a`. The capture summary reports zero repeat differences across two complete sequences within one process. These runner results are supporting evidence, separate from the manual inspection above.

Limits: the capture is 960×640 at window scale 1, rotation 0, zoom 0, with the RCT2 theme and English labels. It is paused, with paint-only warmup and no event/tick loop; weather, lighting, and the FPS overlay are disabled. The images show the controls but do not demonstrate interacting with them, HDR output, other languages/scales, fresh-process repetition, or sustained performance. The recorded world admission is ordinary paint, so this review does not establish retained-world GPU rendering or reduced CPU work.
