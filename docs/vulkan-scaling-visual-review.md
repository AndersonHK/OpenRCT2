# Scaling parity visual review

Reviewer: `screen_capture` implementation/review agent, 2026-09-19. Baseline: `obj/vulkan-parity/run-02/samples/Scaling`, build 05. All nine software/Vulkan/diff images for the three failing cases were opened and inspected individually before changing production scaling. The six tests compare frozen X8-generated 32×24 indices through actual SDL accelerated presentation and actual Vulkan final swapchain capture. No CPU filtering oracle or perceptual tolerance is used.

Reference SDL driver: **direct3d11**, flags 10, ARGB8888 streaming/target textures. Vulkan: **NVIDIA GeForce RTX 5070 Ti**, vendor 4318, device 11269, native driver version 2585198592, output format 44 (BGRA8 UNORM). Per-sample reports preserve all shader FNV-1a hashes, exact byte results, first mismatch and bounds. All three differing bounds cover the full image; alpha error is zero.

| Sample | Output | Different pixels | First difference | Maximum RGBA error | Manual inspection |
| --- | --- | ---: | --- | --- | --- |
| LinearInteger | 96×72 | 4,447 | (2,0) | 126,107,126,0 | SDL blends the vertical colour bands, softens checker boundaries and transitions between lower blocks. Vulkan retains hard three-pixel edges everywhere. Diff forms vertical edge stripes and a dense checker region. |
| LinearFractional | 93×71 | 5,579 | (1,0) | 130,124,130,0 | SDL's smooth band gradients and softened checker transitions differ broadly from Vulkan's uneven-width nearest blocks. Diff covers most band/checker pixels and the block boundaries. |
| SmoothNearestFractional | 93×71 | 2,463 | (2,0) | 111,101,111,0 | SDL retains broad flat interiors with narrow blended edges, as expected after integer nearest upscale then slight linear downscale. Vulkan has entirely hard edges. Diff is concentrated on these narrow boundaries and the checker intersections. |

NearestInteger, NearestFractional and SmoothNearestInteger had zero differing pixels. The latter is expected: its intermediate and final extents are both 96×72. None of the failures indicate a software glitch. All remain Vulkan defects; **zero exceptions and zero tolerance are accepted**.

The initial implementation plan is to render palette+LightFX into an RGBA8 UNORM image at logical size for linear mode, or `logical size × ceil(windowScale)` for smooth-nearest. That pass samples indices with nearest filtering. A second pass linearly samples RGBA display bytes and applies the existing final output encoding. Ordinary nearest rendering keeps its direct pass. This preserves palette semantics and uses actual GPU image filtering rather than reconstructing SDL interpolation on the CPU.

The [current UI](../src/openrct2-ui/UiContext.cpp) automatically selects nearest for integer window scale and smooth-nearest for fractional scale. Linear is an existing HardwareDisplay capability; this fixture exercises it explicitly. This work must preserve automatic selection and latch scale settings with each frame. More scale ratios, HiDPI, resizing/live changes, LightFX combinations and other devices remain required; these six isolated display tests do not establish complete park/UI printscreen parity or OS colour-management parity.

## Baseline SHA-256 provenance

Source files (before scaling production edits):

- `HardwareDisplayDrawingEngine.cpp`: `11EFE1C07F6FAA320C5480BF5963D5C56DA108829C7ACC6AD0C72B3A078FF55E`
- `indexed_palette.frag`: `B1BDDBC8EE27B524FF19EC8F1CC99FF277E787512B26206BAE5B09F30024164C`
- `VulkanScalingParityTests.cpp`: `83E05B9EE4D8A88774960D8ADE8110B72278D4860F9CF3A1C8A96FB5FC6465F6`

| Sample | File | SHA-256 |
| --- | --- | --- |
| LinearInteger | software.png | `D9ADCB969574935BDD66F10F45D1B0C058BDD6E1BAF37B1415105E59AC6A5805` |
| LinearInteger | vulkan.png | `68A5C57F7B921207F2C079087C66542C31BF9CF435B58BC868F9644EC83B4811` |
| LinearInteger | diff.png | `AE591FB0DEE3CA2A9E0A50E13C14B9E59E37A3F2961CA9B7FC0F18D415BA8DED` |
| LinearFractional | software.png | `B62460DD37A10AE9AAE169ED2538C3B5F0D36DAD933BCA229A35E670380E3AB9` |
| LinearFractional | vulkan.png | `2EB8DF39E0F36B3A068D7EE6EC7A7B56A2041BB53C81AA1C5F2D87F3F73692A3` |
| LinearFractional | diff.png | `1D68861D33C359491C805B88A68C60EC6B9D4C28244C306C2AD4B5F18115F50E` |
| SmoothNearestFractional | software.png | `6D5337DDB478A04E6F556902AE76E6E79A7C8DF17BB50D108DB35C38F17490FC` |
| SmoothNearestFractional | vulkan.png | `2EB8DF39E0F36B3A068D7EE6EC7A7B56A2041BB53C81AA1C5F2D87F3F73692A3` |
| SmoothNearestFractional | diff.png | `1AAEB32151858144745E9C3273526D36615E1D948F9779D2D984EBC7B7D026EF` |

Baseline artifacts must remain unchanged when subsequent runs are captured into separate directories.

## Corrected output review

Build 08 / `obj/vulkan-parity/run-04/samples/Scaling`: all six original scaling cases have **zero differing RGBA pixels**. The
same agent opened and inspected all three corrected software/Vulkan/diff triplets. LinearInteger and LinearFractional now
have the same smooth band/checker transitions as SDL. SmoothNearestFractional now has matching flat interiors and narrow
blended edges. All three diff images are entirely black. Their reference PNG SHA-256 hashes are unchanged from the baseline;
each corrected Vulkan PNG hash equals its reference hash exactly. This is an actual fix, not a tolerance or exception.

| Corrected sample | Software and Vulkan PNG SHA-256 | Diff PNG SHA-256 |
| --- | --- | --- |
| LinearInteger | `D9ADCB969574935BDD66F10F45D1B0C058BDD6E1BAF37B1415105E59AC6A5805` | `16108D89605408EA41E82B6FB4DE47C02EBA5406540093DD695EF1174CCB348C` |
| LinearFractional | `B62460DD37A10AE9AAE169ED2538C3B5F0D36DAD933BCA229A35E670380E3AB9` | `195BD0AB55B3BA95631B5ED91DA98F867A403F38BAD0655319498D6B53D9A608` |
| SmoothNearestFractional | `6D5337DDB478A04E6F556902AE76E6E79A7C8DF17BB50D108DB35C38F17490FC` | `195BD0AB55B3BA95631B5ED91DA98F867A403F38BAD0655319498D6B53D9A608` |

Follow-up coverage adds odd logical sizes, 0.75/1.25 window scales, arithmetic equivalent to a 2× HiDPI drawable, live mode
changes across every frame slot, intermediate-target replacement, simultaneous logical/physical resize and surface refresh.
HiDPI-ratio fixtures model the drawable ratio; they do not certify OS DPI-change events. A uniform LightFX case uses an exact
byte-addition oracle before actual SDL filtering to verify lighting precedes interpolation; it does not certify world light
generation/rasterization.

Build 10 / `obj/vulkan-parity/run-05/samples/Scaling` verifies all follow-ups: **32 reports across 12 cases, zero differing
RGBA pixels in every report**. That includes 12 physical outputs, 12 uniform-lighting outputs, six live frame-slot replacements,
one simultaneous logical/physical resize, and one surface-format refresh. These are exact checks with no accepted exceptions.
The full suite exposed unrelated publication and covered-zero primitive failures; those do not invalidate the individual
completed scaling measurements, and remain tracked by their owners.

## Main-screen index-zero alpha review

Build 12 / `obj/vulkan-parity/targeted-07/samples/Scaling/ScreenPaletteAllIndices/physical` preserves the failing baseline.
The agent opened the software, Vulkan and diff images before changing the conversion. The colour pattern agrees visually;
the diff isolates three 3-by-3 blocks along the left edge, corresponding to the repeated index-zero samples. Byte inspection
confirms the first pixel is `(0,0,255,255)` in SDL and `(0,0,255,0)` in Vulkan. Every SDL alpha is 255; Vulkan alpha is 0 or
255. The report counts **27 differing pixels**, maximum channel error `(0,0,0,255)`, bounds `(0,0)-(2,50)`.

This is a main-screen conversion bug, not an accepted divergence: `SDL_MapRGB` produces an opaque screen, including index
zero. `ConvertScreenPalette` now makes all 256 entries opaque. The indexed screenshot path still uses `WriteIndexedScreenshot`
and `ScreenshotDumpPNG` and is unchanged. Corrected output requires the next test run and visual review.

| Baseline evidence | SHA-256 |
| --- | --- |
| software.png | `B7D5E47E83972E1B55F598A9EB1ADE1C9C0254FF04DB696F36CC7D1961D68356` |
| vulkan.png | `B5E5A8004BD6C1B61194DD80083AD381C4C3CB3A35D38B66DBE7E18321DEE046` |
| diff.png | `1A5EF06D66024AFB9F5E963D141C2263BF5BB1286A2B217EDB3CC6787D4265EC` |
| VulkanScreenPalette.h before fix | `B622FEEB7A4CBA4DF6FA12110AD47EFDB6D68155BB81BCC00945AA07028B3574` |
| VulkanScalingParityTests.cpp | `9AB299DB57A89C21048718458250866124C93855771F2ABD7771B287FC951ABA` |

Build 13 / `obj/vulkan-parity/run-08/samples/Scaling/ScreenPaletteAllIndices/physical`: the agent opened all three corrected
images. The colour pattern remains unchanged and both outputs match; the diff is entirely black. The report confirms zero
differing RGBA pixels, with every maximum channel error zero. Both software and Vulkan PNG SHA-256 now equal the baseline
software hash `B7D5E47E83972E1B55F598A9EB1ADE1C9C0254FF04DB696F36CC7D1961D68356`; the black diff hash is
`16108D89605408EA41E82B6FB4DE47C02EBA5406540093DD695EF1174CCB348C`. This closes the alpha divergence with no exception.
