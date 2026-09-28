# Offscreen Vulkan service visual review

Agent `/root/decode_assets`, 2026-09-19. All eight run-31 offscreen fixtures match the frozen X8 primitive oracle exactly in logical indices and palette-expanded RGBA. I manually inspected all sixteen reference/Vulkan/difference triplets and independently compared the complete raw buffers. The [receipt](vulkan-offscreen-visual-review.json) pins the source test, full-run summary, every raw buffer/report/PNG and the reviewed sheets. Run 31 reports 750 passing tests; this visual review covers only its eight offscreen image fixtures.

The six `clear-{0,1,2}` and `owned-{0,1,2}` cases exercise opaque, palette-alpha and transparent-index-zero output with either an explicit clear or an owned patterned initial indexed buffer. Clipped solid rectangles, checker fills, zero-index fill, diagonal lines and a nested crop agree. Alpha-bearing outputs were viewed against a neutral background, while independent raw checks include every alpha byte. The receipt retains the actual alpha-value sets, transparent-pixel counts and partially transparent pixel counts. All difference images are black.

`offscreen-primitives` produces the same complete output while an independent main submission slot and recorder remain active on the shared device. Its surrounding test checks that the main recorder and slot are untouched; the image alone does not establish that ownership contract. `owned-sprite-covered-zero` retains the original 17×13 multicolour sprite, including covered zeros, after the source bytes and sprite metadata are mutated and the recording session is destroyed before worker execution. Both layers match the software image taken before mutation.

The sheets in `obj/vulkan-parity/offscreen-manual-review/` contain the complete 64×48 buffers, enlarged four times with nearest-neighbour sampling for inspection. PNG alpha is composited over `#555555` only for the review sheet. Original files are preserved, and exact raw comparison avoids accepting differences hidden by compositing. The automatic report field `visualReview: not-required` is preserved; this separate agent review receipt records the actual inspection.

No tolerance, mask or renderer exception is needed. These results qualify the listed surface-free service outputs and their explicit palette/alpha contracts. They do not qualify complete offscreen park scenes, production screenshot/preview caller migration, all drawing methods, main-window presentation or throughput. Resize/retained-result and cancellation behavior remain separate test assertions rather than additional visually reviewed sample groups.

## Post-shutdown-ordering rerun

Build 37/run 32 passes 750 tests with validation. The sixteen offscreen reports are now mandatory. An independent audit compared all thirty-two software/Vulkan raw buffers from run 32 directly to the corresponding manually reviewed run-31 buffers: every byte is identical. The receipt pins the new summary, reports and buffers. These unchanged images retain the visual conclusions above; this does not broaden the fixture scope or substitute image equality for the separate shutdown lifetime tests.

## Fresh no-window process

`e4-no-window-offscreen-01` passes all eight offscreen tests in the separately built `e4-no-window-07` executable, with validation active and no messages. Its sixteen reports and thirty-two raw software/Vulkan buffers independently match the reviewed run-32/run-31 buffers byte for byte. The JSON receipt pins this process and its build receipt; unchanged visual conclusions carry forward without claiming additional scenes or caller migration.
