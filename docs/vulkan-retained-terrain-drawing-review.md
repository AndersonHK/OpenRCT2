# Retained mixed-height terrain: bounded device drawing review

Reviewed on 2026-09-19 by the independent `balloon_visual` agent. **Pass for the 32 diagnostic indexed drawing cases; runtime admission remains closed.** No divergence or exception was accepted.

Evidence: `obj/vulkan-parity/terrain-column-corpus-01` contains 32 camera/policy cases, each captured in frozen, fresh frozen repeat, and current linked-core processes (96 processes). `terrain-draw-probe-01` contains the corresponding 32 actual GPU outputs. The companion JSON binds all 8,369 independently verified input, asset, receipt, corpus, log, image and raw-buffer hashes.

I manually opened both the frozen and Vulkan PNG for every sample 0â€“31: 64 images covering four rotations, zooms 0/1, ordinary/transparent background, and stable sorting off/on. The mixed-height plateaus, sloping triangles, exposed cliff faces, grass detail, overlapping edges, outside-map border and cropped terrain match in every pair. These images display palette indices as grayscale; their appearance is intentionally not the game's final colour palette.

The independent data audit confirmed:

- All 32 Vulkan indexed buffers equal the frozen raster byte for byte (960Ã—640), and every frozen/Vulkan PNG pair is byte-identical. Frozen, repeat and current trace content is equal in every case.
- Every active GPU column has a successful status and consistent indirect draw counts. Walking each GPU parent list reproduces the frozen arranged sequence, image identity, bounds, screen position and attachment count without cycles or missing parents.
- Each image contains 50â€“74 distinct indices and substantial visible terrain. This is meaningful drawing coverage, not an empty equal frame. There are 16 distinct rasters: stable sorting changes recorded ordering while preserving the final image in this fixture.
- Transparency is meaningfully exercised: ordinary and transparent variants differ by 29,322 pixels at zoom 0 and 384,001 pixels at zoom 1, for every rotation. Both variants match their own frozen references.
- The first sample transfers 46,080 bytes of retained world/material/sprite metadata; all 31 subsequent camera/policy samples transfer zero world bytes. The same immutable source generation is reused.
- The actual device run passes its one 32-sample test. Khronos validation is activated, synchronization validation is enabled through the pinned layer settings, and the log contains no validation warnings or errors.

This proves the bounded GPU column ordering and indexed raster for accepted `nonuniform-terrain-input-02`, including its real camera offsets. It does not qualify final RGBA, window composition, production map/material publication, production atlas residency, native UI admission, general terrain completeness, large-park performance, vsync, TPS, or Gate P. The diagnostic atlas uses frozen decoded assets. The original actual GPU run used one process with 32 retained-state samples; the additional fresh process repeat below now corroborates it. The reference capture has fresh process repeats. Invalid-input and capacity controls remain a separate checkpoint.

## Independent fresh device repeat

The `screenshot_runner` agent independently audited `terrain-draw-probe-02` against `terrain-draw-probe-01` and the preserved original review. All 32 indexed outputs are byte-identical to their frozen rasters and first GPU outputs; every parent, command and column buffer also repeats exactly. All 84 runtime input pins match after normalizing only the run-directory name. Rehashing 672 input/evidence files confirmed the recorded identities, including both run logs and summaries. Both runs pass their one 32-sample test with activated synchronization validation and no validation warnings/errors. Each process uploads 46,080 world bytes for its first sample and zero for the remaining 31.

Nine fresh representative Vulkan PNGs (samples 0, 3, 4, 7, 9, 10, 15, 16, 31) were manually opened at native resolution, covering all rotations and both zoom/background/sorting modes. Their mixed-height plateaus, slopes, textured cliffs, border and crop remain coherent; no new visual divergence or exception was found. The first review's manual inspection of all 32 frozen/Vulkan pairs remains preserved. Exact command/parent/column bytes extend its frozen ordering proof transitively; this repeat audit did not duplicate that semantic parser.

The original review JSON (`fb0c9ed55fc6ac6906aab6520563cab0b0f7e1cff77d84142f675018ad262b77`) and Markdown are preserved under `obj/vulkan-parity/terrain-drawing-repeat-review/original-review.*`. The additive receipt is `repeat-audit.json` in that directory and is hash-bound by the companion review JSON. This remains bounded diagnostic indexed/order evidence: production admission, final RGBA, CPU/bandwidth/TPS performance, and Gate P are still open.
