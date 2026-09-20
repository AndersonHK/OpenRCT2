# Main publication defect review

Reviewer: `/root/decode_assets`, 2026-09-19. Baseline: build 13, `obj/vulkan-parity/run-08/samples/MainPublication_EntityMove`. Both targets use the frozen X8 rasterizer. The reference viewport reads live state; the result viewport uses the context's actual main `PresentationScene`. This isolates publication from Vulkan rasterization or UI presentation.

The fixture imports the small Ferris wheel park at tick 1215, camera rotation 0 / zoom 0, 640×480, view position (-544,-160). It creates a red balloon at (336,112,240), paints a main frame and completes that frame's pending entity preparation. It then moves the balloon to (400,176,240), updates the spatial index, resets interpolation and requests a synchronous main publication with a transient map-selection flag. No simulation step occurs.

Manual inspection of both indexed and RGBA live/published/difference triplets shows the published balloon remaining above the wheel at its old location, while the live reference places it 64 screen pixels lower over the wheel. The difference consists of the old and new balloon silhouettes; the surrounding wheel, entrance, fences and terrain match. Both layers differ at **154 pixels**, inclusive bounds **(316,124)–(323,206)**. Maximum RGBA error is (232,219,219,0).

| Baseline artifact | SHA-256 |
| --- | --- |
| `indexed/live.bin` | `721759BDB37B9C9FDF4F839A5AD63D1CE09889DAC34F668CAA32A4049D5BB100` |
| `indexed/published.bin` | `2920BDDBB59DD5EB710531A79AD98311BAC23B08276CEFFD874295B00F1CC840` |
| `rgba/live.bin` | `2E12FA5C738829F5718C680D2A33CB2A7AA4FEC81B262122D2A81F2661CC0DD4` |
| `rgba/published.bin` | `6ED10DA52F321B053EAD9A90FBCBB3F38C4E6AEB5576C0FE4A38A7B6C368E457` |

The confirmed shared-path defect is synchronous map acquisition paired with entity bytes prepared before the movement. The correction must match the **entire live reference image exactly**, not ignore the old/new balloon regions. This is a narrowly documented correction to a stale publication result seen by software as well as Vulkan; it is not a blanket software-reference exception, a raster tolerance or permission to alter unrelated pixels. The fixture JSON records the original asset hashes.

The existing publishers now capture current entity storage for synchronous acquisition, admit asynchronous map/entity changes together, prevent scheduling a newer map beside pending older entities, and recycle entity storage only when uniquely owned. Separate data tests also demonstrate retained-generation mutation; those state tests do not justify any additional pixel exception.

## Corrected output: build 14 / run-09

The reviewer opened all six corrected indexed/RGBA live/published/difference PNGs. The red balloon occupies the new, lower position in both outputs, and both difference images are black. Both reports contain **zero differing pixels**. The complete corrected indexed buffer has SHA-256 `721759BDB37B9C9FDF4F839A5AD63D1CE09889DAC34F668CAA32A4049D5BB100`, and its complete RGBA buffer has SHA-256 `2E12FA5C738829F5718C680D2A33CB2A7AA4FEC81B262122D2A81F2661CC0DD4`: exactly the previously recorded live-reference hashes. The reference did not change to accommodate the fix.

This closes the specific stale synchronous-publication defect, recorded as `PUBLICATION-001-stale-synchronous-entity` in the exception ledger. Run-09 passes all six publication-data tests as well as the visual fixture. The deterministic single-worker test gates old entity preparation, attempts a second newer map capture, and verifies admission and scheduling preserve a whole captured state. Retained-generation ownership passes its separate data regression; no separate image exception is accepted for that contract repair. Main Vulkan UI rendering, queued packet lifetime and broader temporal/world-transition cases remain unqualified by this X8 publication fixture.
