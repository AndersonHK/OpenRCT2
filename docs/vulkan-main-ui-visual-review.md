# Main UI capture visual review

This record covers actual default SDL backbuffer capture immediately before `SDL_RenderPresent`, including the main game
window and its toolbars. It does not certify OS compositor/cursor output. Images must be compared with the separately rebuilt
frozen-source UI oracle before they establish migration parity.

## Current software baseline: ui-run-01

On 2026-09-19 `/root/screen_capture` opened and manually inspected **both**
`obj/vulkan-parity/ui-run-01/captures/baseline-0/screen.png` and `baseline-1/screen.png` at their native 960×640 dimensions.

Both captures show the same fully painted small ferris-wheel park: a grass diamond with exposed vertical earth at the front,
a fence around the park, the yellow/grey ferris wheel at the left, entrance towers and red-roofed ride buildings, and dark
background outside the map. Actual UI is visible: pause/speed/save/zoom/view icons at the upper left, construction/management
icons at the upper right, the bottom-left money/guest panel (`$10,000.00`, `0 guests`), and bottom-right date/weather panel
(`March 3rd, Year 1`, `55°F`). The bottom map edge is clipped by the capture boundary and corner UI, consistently with the
chosen camera. This is a main-window capture, not a blank render target or a bare auxiliary viewport.

No loading/progress window, error dialog, tooltip, dropdown, selection highlight, FPS text or unexpected overlapping window
is visible. The substantial dark area above the small park is consistent with the recorded camera and map footprint; it is
not a missing viewport. There is no visible weather animation or lighting effect, as required by this baseline. The sparse
park and zero-guest UI mean these images do **not** establish populated-entity or overlapping-window coverage.

The reports agree on logical/physical extent 960×640, rotation 0, zoom 0, view position `(-504,-229)`, simulation tick 1215
and palette-effect frame 0. The accelerated SDL renderer is `direct3d11` (flags 10). The driver reports a paused paint-only
run with two warmup draws and no event/tick loop. Visual equality alone cannot prove input determinism; the fixture input
and state receipts remain required evidence.

Both PNG SHA-256 hashes are `AFD639F90470C19F3F0C0D16BF151E705EDD5FA21961354983422BE2BBF8F743`.
Both raw RGBA SHA-256 hashes are `1FF805362EFA6A06323DA6801E1250E3790855D0225E132F48057280BCB163AE`.
The capture runner reports exact repeatability. No divergent sample exists in this pair and no exception is accepted.
Frozen/current software comparison and Vulkan comparison are still pending; this review closes only the baseline's visual
sanity and repeat-capture inspection.

## First frozen/current/Vulkan comparison

The agent opened **all six** baseline-0/baseline-1 screen images from `ui-frozen-run-01`, `ui-current-run-02` and
`ui-vulkan-run-01`, plus both Vulkan physical-output diff images. The park, wheel/entrance/fences, toolbar icons and corner
status text agree throughout. Both diffs are entirely black. All three actual display paths produce the same baseline
RGBA hash recorded above, and both named Vulkan samples also match the frozen indexed hash
`70FD10E2544CD6468D01DC7007F5281A864D05D19BFE26FE3F23F54B5D7A21C5`. Each indexed and physical-RGBA comparison reports zero
differing pixels, with no exception or tolerance.

The Vulkan packet metadata confirms actual main-window recording: 44 opaque rectangles, 1,732 opaque sprites and 20
transparent rectangles; no native world surfaces, weather or LightFX. This establishes one ordinary main-UI baseline only;
it does not establish native-terrain admission, populated entities, changing/overlapping windows, or the wider fixture matrix.

`ui-vulkan-run-01` deliberately remains **failed** despite matching pixels: synchronization validation reports one
`SYNC-HAZARD-WRITE-AFTER-WRITE` for repeated writes to the shared sprite-descriptor buffer. The existing upload-end barrier
made descriptors readable by vertex shaders but did not order later transfer writes across frame slots. A buffer-specific
vertex-read/transfer-write to transfer-write barrier now precedes each upload batch. A runtime regression exercises repeated
descriptor uploads with and without a subsequent sprite draw, without per-frame idle waits. The original images/log/receipts
remain untouched; validation qualification awaits a new build/run.

Independent reviewer `/root/primitive_parity` also opened baseline-0 and confirmed the main park, toolbar/status landmarks and absence of loading/error overlays. Its recorded raw RGBA hash agrees with the pair above.

## Preliminary frozen window-family sanity review

Reviewer `/root/primitive_parity` manually opened all eight nonbaseline `screen.png` images produced by
`ui-frozen-overlap-01`, `ui-frozen-scroll-01` and `ui-frozen-text-01`. These runs are incomplete and remain failed.
The shared failure, `Main viewport lost during fixture`, occurs after the first fixture close because the harness calls
`IWindowManager::Cleanup()`. Source inspection shows that method closes every window, then culls them. The public
`WindowCullDead()` API only removes windows already marked dead and is the required harness correction. This is a fixture
failure, not a rendering divergence or an accepted software exception. The failed artifacts remain unchanged.

The paths below are relative to `obj/vulkan-parity/`. Each row names the manually viewed PNG and the SHA-256 of the
adjacent raw `screen.rgba` bytes.

| Sample PNG | Raw RGBA SHA-256 | Visual observation |
| --- | --- | --- |
| `ui-frozen-overlap-01/captures/research-front-0/screen.png` | `b4a9b0fba93f4ae78bd7ea4007ff810a0fbfd111dc4704c906591557cfd42de3` | Research correctly occludes the financial table while the finances frame and lower totals remain visible. |
| `ui-frozen-overlap-01/captures/finances-front-0/screen.png` | `f97dd0eacbe7c927b1be69ed0d83ba1290e5a007860575d0e7dc0b808c6a3924` | The financial window is now in front and fully covers the research window's rectangle. Its text, striped table and tab icons remain intact. |
| `ui-frozen-overlap-01/captures/partially-clipped-0/screen.png` | `935e08b4ffa71cc2139d2766a6ebd48b29046a21816606755f13d4d2930114bd` | The left edge of finances extends beyond the screen; labels and tabs clip at x=0, and a strip of research is visible on the right. |
| `ui-frozen-scroll-01/captures/scroll-top-0/screen.png` | `d2f111a49852bcdb5cf5d561dde240ecd45f68010c484656c60b0e3d99d455ce` | Actual shortcut rows, group separators, bindings, a vertical scrollbar and the Reset keys button are visible. |
| `ui-frozen-scroll-01/captures/scroll-partial-row-0/screen.png` | `419cc077a334e6810092c3fee6b80c37a3c19c63ae7fdf1169e2f46e03c04ca2` | The prescribed 37-pixel offset changes the upper row content, exposes lower shortcuts and moves the thumb; text clips at the list viewport. |
| `ui-frozen-scroll-01/captures/scroll-screen-clip-0/screen.png` | `792687ec1c1e8b5a117cb8f688a6d2f74b8baa0c4e3adfe0acbfaec770c9e0c2` | The list window has moved partly beyond the left screen edge; clipped labels, binding column and scrollbar remain coherent. |
| `ui-frozen-scroll-01/captures/scroll-resized-0/screen.png` | `1603a07151d7daaed039e06c778a61fa835235207e9ffc5de5a5e055a6454309` | The window and list viewport are wider and taller, additional lower rows are visible and the scrollbar has moved with the right edge. |
| `ui-frozen-text-01/captures/text-empty-0/screen.png` | `bc2dee2d38add88b659e06a265c5b140b8264c848b5d52b002ed09cfe244c64f` | The real text-input modal overlays research; its fixed title, wrapped description, empty inset field and OK/Cancel controls are visible. This run has not reached the nonempty text/caret steps. |

All three `empty-ui-0` raw captures have baseline SHA-256
`1ff805362efa6a06323da6801e1250e3790855d0225e132f48057280bcb163ae`. The visible park, toolbar and corner status panels
remain sensible in the eight inspected states. No Vulkan comparison, complete repetition, restoration result or parity
qualification is claimed for these incomplete family runs. Re-run both full sequences after the harness-only close fix,
then inspect any measured reference/candidate differences.

## Corrected full-invalidation family sequences

After replacing the two harness teardown calls with `WindowCullDead()`, all nine
`ui-{frozen,current,vulkan}-{overlap,scroll,text}-02` runs pass. Each run contains six states captured twice. Each current
software/Vulkan run compares all 12 captures with its frozen counterpart at both indexed and final RGBA layers: 24
comparisons per run, zero differing pixels and zero runner failures. Both repetitions and restored-screen assertions pass;
Vulkan validation is clean. These are full-invalidation composition fixtures, not yet incremental-damage evidence.

Reviewer `/root/primitive_parity` opened both frozen and Vulkan PNGs for all seven rows below (14 images), plus the actual
physical-output diff PNGs for text `wrapped-caret-end-0`, overlap `restored-0` and scroll `restored-0`. All three diffs are
entirely black. Relative capture paths follow
`obj/vulkan-parity/ui-{frozen,vulkan}-{family}-02/captures/{step}-0/screen.png`; hashes below are their matching raw RGBA bytes.

| Family / step | Raw RGBA SHA-256 | Manual observation |
| --- | --- | --- |
| text / wrapped-caret-start | `657bce0e1061e4a35787ad07b52da36e5858e4d5d34d2635a9e295180e4e700d` | The real dialog is taller than the empty control, the repeated sentence wraps into five visible lines, and the caret is at the start. Frozen and Vulkan text, inset field and underlying research window agree. |
| text / wrapped-caret-end | `64399627692fd90b2a87a703f6a7fa7b58a959f3366366a70db323b6127ec0a1` | The caret is now at the end of the last line; wrapping and dialog geometry are unchanged. Both renderers show the same caret and glyph positions. |
| text / submitted | `868f979ef88b8dfee51fcc27af9599b1a33d7332ce9ee0682141fecf36f3aa03` | The text dialog has closed, leaving the research window and the newly exposed park region intact. There are no stale field/button pixels. The harness also verifies exactly one successful callback with the prescribed text. |
| text / restored | `1ff805362efa6a06323da6801e1250e3790855d0225e132f48057280bcb163ae` | Both fixture windows are gone; the Ferris wheel, entrance, ground, fence, toolbars and corner panels return to the original baseline. |
| overlap / research-only | `868f979ef88b8dfee51fcc27af9599b1a33d7332ce9ee0682141fecf36f3aa03` | Closing the clipped finances window preserves research and fully reveals the park around it, without losing the main viewport. |
| overlap / restored | `1ff805362efa6a06323da6801e1250e3790855d0225e132f48057280bcb163ae` | No financial/research remnants remain; the full baseline scene is restored. |
| scroll / restored | `1ff805362efa6a06323da6801e1250e3790855d0225e132f48057280bcb163ae` | The resized/clipped shortcut window is gone, and the park beneath its full old rectangle is restored. |

The common restored indexed SHA-256 is `70fd10e2544cd6468d01dc7007f5281a864d05d19bfe26fe3f23f54b5d7a21c5`.
This closes the harness cleanup failure and qualifies the three prescribed full-invalidation sequences at the pinned
960-by-640 fixture configuration. No software exception or pixel tolerance is accepted. Incremental variants must separately
match these full-state images and their frozen incremental runs before damage/invalidation coverage can be marked complete.

## Incremental window sequences

All nine `ui-{frozen,current,vulkan}-{overlap,scroll,text}-incremental-01` runs pass, with two complete six-step sequences
per run. The harness forces full invalidation only for warmups and the first `empty-ui-0` capture. Later transitions,
including the entire second repetition, rely on public window-operation invalidations. Frozen incremental runs each have
24 exact indexed/RGBA comparisons with the corresponding full-invalidation states; current software and Vulkan each have
48 exact comparisons covering both the frozen incremental sequence and the full states. The runner reports zero differences,
zero failures and no Vulkan validation diagnostics. Repetition and final restoration assertions pass.

Reviewer `/root/primitive_parity` manually opened the second-repetition Vulkan PNGs for overlap `restored-1`, scroll
`scroll-screen-clip-1` and text `wrapped-caret-end-1`, plus each matching
`full-state-comparisons/{step}/physical-rgba/diff.png` in its run directory. The restored park has no old window pixels;
the scrolled window retains the intended left-edge/row clipping and exposes the background cleanly; the wrapped text and
end caret remain in their prescribed positions. All three inspected full-state diffs are completely black. Their pixels
match the previously reviewed full-state images (RGBA hashes `1ff805362efa6a06323da6801e1250e3790855d0225e132f48057280bcb163ae`,
`792687ec1c1e8b5a117cb8f688a6d2f74b8baa0c4e3adfe0acbfaec770c9e0c2`, and
`64399627692fd90b2a87a703f6a7fa7b58a959f3366366a70db323b6127ec0a1`, respectively).

This qualifies incremental damage behavior for these three specific sequences at 960-by-640. It does not qualify arbitrary
UI input, resize/DPI transitions or the still-pending native-terrain matrix. No exception or tolerance is introduced.
