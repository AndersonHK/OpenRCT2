# B1 excluded-mode fallback visual review

Status: three bounded controls pass. Agent `/root/balloon_visual` manually inspected frozen/Vulkan image pairs for smoothing, hide-entities and path-issue mode. The matrix `ui-balloon-fallback-{frozen,software,vulkan}-{smooth,hide-entities,path-issues}-{01,02}` covers 18 processes and 36 captures on UI30/frozen18, including fresh-process repeats. Every capture matches its frozen case in indexed and physical pixels. Exact summary, image and telemetry hashes are in `vulkan-balloon-fallback-visual-review.json`.

| Control | Visible result | CPU balloon sprite calls per Vulkan capture | Native viewports / submissions | Source / sprite upload bytes |
| --- | --- | ---: | --- | --- |
| Smoothing enabled | Matching intact/popped grid and central overlapping balloons | 44 | 0 / 0 | 0 / 0 |
| Hide entities | No balloons; matching grass, map boundary and UI | 0 | 0 / 0 | 0 / 0 |
| Path-issue highlighting | No balloons; matching grass, map boundary and UI | 0 | 0 / 0 | 0 / 0 |

Vulkan explicitly attempts the diagnostic native family and requires fallback; publication admission is false in all 12 Vulkan captures. The zero-call hidden cases are expected behavior, so positive CPU sprite counts are not a fallback requirement. Smoothing exercises the ordinary CPU balloon paint path without duplicate native output. All Vulkan validation logs confirm the layer is active and contain no validation errors, warnings or synchronization hazards.

The saved camera contract settles to `[-480,32]`, rotation 0 / zoom 0, and remains equal to the expected pose after warmup and capture. The transient load position is separately recorded. Raw image hashes bind both repetitions and all fresh processes to the manually inspected case samples; no divergence or exception is accepted.

- [x] Smoothing fallback preserves visible balloons and exact pixels.
- [x] Hide-entities and path-issue controls preserve intentionally absent balloons.
- [x] All three renderer lanes, fresh repeats, explicit native-decline telemetry and manual visual review.
- [ ] Other excluded modes, broader cameras, mixed families, lifecycle pixels and native coverage expansion.

This is fallback correctness evidence. It does not expand B1 admission or close global Gate P, exclusive Vulkan migration, or large-park performance qualification.
