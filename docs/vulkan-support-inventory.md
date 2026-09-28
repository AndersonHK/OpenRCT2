# Vulkan migration support inventory

Inspected from this checkout on 2026-09-19. This records repository configuration and local evidence, not a claim that every CI job currently succeeds or that all upstream platforms are the fork owner's intended release scope. Preserve existing targets until scope is resolved.

## Configured products and outstanding qualification

| Product | Repository evidence | Vulkan migration status |
| --- | --- | --- |
| Windows Win32, x64, ARM64 | `.github/workflows/ci.yml` Windows matrix produces portable archives, installers and symbols. Win32 explicitly retains a Windows 7 compatibility build. | Only Release x64 on RTX 5070 Ti is locally tested. `openrct2.vulkan.props` auto-enables only Win32/x64 and supplies no ARM64 library directory. ARM64 packaging and compatibility/device minima are unresolved; do not silently drop them. |
| Linux x86_64 | CI portable Ubuntu noble/resolute and Debian bookworm/trixie artifacts, plus AppImage. Additional debug/disabled-feature builds exist. | Require Vulkan headers/shader compilation at build time and loader/device at graphical runtime. No Linux pixel/validation/package evidence yet. Non-rendering CLI must remain device-free. |
| macOS x64/ARM64 universal application | CI builds both architectures, combines them and lists the universal artifact as a release dependency. | CMake discovers Vulkan/MoltenVK; device code enables portability extensions when advertised. Actual MoltenVK bundling, formats, shader parity and runtime availability remain unqualified. |
| Android armeabi-v7a, arm64-v8a, x86_64 | CI builds these ABIs and APK; release depends on Android. Gradle minSdk24/targetSdk36 and dedicated native CMake build. | Dedicated Android CMake does not inherit the desktop Vulkan target setup automatically. Loader linkage, surface creation, shader assets and device limits need explicit implementation/qualification. API level alone is not proof that existing devices meet the renderer's full requirements. |
| Emscripten browser artifact | CI builds and uploads `build/www`. It is not an explicit release prerequisite, but release downloads all available artifacts. Desktop UI CMake explicitly excludes Vulkan for Emscripten. | Release-blocking scope/deployment decision for an exclusive Vulkan objective. Do not replace it with another graphics API or remove it without a deliberate product decision. |
| Non-rendering CLI/server/tools | `RootCommands.cpp` exposes simulation, park information, conversion, object scanning, sprite tooling and headless host operation as well as screenshots. | Audit each command's actual call graph. Asset decode/export is allowed CPU data work; scene/sprite compositing must migrate. A simulation/server command must not initialize a Vulkan device merely because the shared core links rendering facilities. |

CI release configuration establishes existing distribution intent, not proof of Vulkan enablement: current CMake may silently disable the renderer when dependencies are absent. Required Vulkan jobs must assert backend availability and required test/capture counts. No remote jobs were launched by this inventory.

## Current device admission and allocations

The current implementation requests Vulkan 1.1. `VulkanDeviceContext.cpp::ScorePhysicalDevice` shares rendering-format/device admission between windowed and graphics-only contexts; only windowed contexts require presentation queues, swapchain support and colour-attachment surface usage. These checks describe present code; they are not yet a final support contract.

| Facility | Current requirement / behavior | Remaining work |
| --- | --- | --- |
| Sprite atlas | 2048×2048, 64 layers of R8_UINT; optional zero-coverage companions consume slots only for assets needing them. | Validate memory/slot pressure on target device classes and custom objects. The pinned 30,475-asset census has no covered-zero assets; that does not constrain user content. |
| Indexed rendering | Optimal R8_UINT sampled, colour attachment, transfer source/destination; R16_UINT sampled/colour attachment. | Query every actual usage consistently and test failure diagnostics. |
| Depth/order | D32_SFLOAT depth attachment, sampled and transfer destination. | Verify deep ordering limits and required formats on each backend/device. |
| Palette/scaling | RGBA8_UNORM sampled, colour attachment, transfer destination and linear filtering. Palette expansion precedes physical filtering. | Verify SDR output format/channel/encoding on multiple devices and actual UI. |
| Native terrain | Admission requires multiDrawIndirect, at least128 compute invocations/X workgroup size, sufficient X group count for maximum map blocks, and4096 bytes shared memory. | Check queue compute capability and all storage-buffer/indirect limits against actual workloads. Do not turn a native-terrain limitation into lost terrain features; conservative ordinary Vulkan painting is already valid. |
| GPU LightFX | R32_UINT storage image with atomic, sampled and transfer-destination features;16×16 compute local size; at least15999 Z workgroups. | Current unsupported/resize cases can use a CPU intensity raster fallback. Remove it only after providing a Vulkan alternative or explicitly resolving stricter device requirements. Canvas dimensions and allocation limits need validation. |
| Upload resources | Three in-flight frame slots, default96MiB upload ring per slot. | Measure actual committed/peak memory, persistent atlas and scene buffers, and low-memory failure behavior. This is an allocation configuration, not a measured budget. |
| Timestamp queries | Optional and disabled if query pool creation or queue support is unavailable. | Preserve rendering without profiling support. |
| HDR | Optional surface-format/colour-space selection and metadata support. | Keep separate from exact SDR baseline; qualify output and transitions rather than using HDR as a blanket parity exception. |
| Diagnostic final capture | Opt-in transfer-source swapchain usage, supported SDR format, frame-owned readback. | Failure to obtain a named required sample fails the lane. This capability is not required for ordinary rendering and does not replace OS/display qualification. |

## Bounded local evidence

**Latest ownership checkpoint:** build34/run29 passes737 tests with synchronization validation. A fresh no-window test process performs actual GPU transfers through two submission domains on one shared device and passes six contracts; the production renderer CMake target compiles16 translation units and17 shaders without SDL include/link metadata. Sixty main-window captures remain pixel-identical. See [pinned qualification](vulkan-shared-device-qualification.json). This adds surface-free device execution evidence, not an offscreen image service, final application packaging or cross-platform support.

`obj/vulkan-parity/build-14/receipt.json` pins Release x64 binaries/shaders/source; run-09 passes105 focused tests and run-10 passes680 total tests. Required Vulkan execution and Khronos synchronization validation were enabled; both runs have zero skipped/disabled tests, missing reports or Vulkan validation diagnostics. Device: NVIDIA GeForce RTX5070 Ti, driver616.92; Vulkan runtime1.4.351, validation SDK1.4.350.0. All physical pixel evidence so far is SDR on this device. Tests do not establish minimum-hardware performance, actual full UI parity, other platforms, display-free offscreen rendering or software-free builds.

- [x] Inventory configured platform/ABI products and release relationships from repository files.
- [x] Record current backend checks separately from proposed final support requirements.
- [ ] Resolve fork release scope without implicitly deleting Android/browser/ARM64 support.
- [ ] Validate queue, format, descriptor/storage/indirect, memory and canvas limits comprehensively.
- [ ] Record maximum image/map output contracts and tiled offscreen limits.
- [ ] Qualify real packages/devices for every retained platform and hardware class.
- [ ] Assert device-free non-rendering CLI/server execution in the final architecture.

These items refine A02–A04/E02–E06 in the migration checklist. They do not close those gates.
