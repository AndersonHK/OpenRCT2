# Next shared-renderer extraction slices

This is the proposed code sequence following the approved [offscreen ownership design](vulkan-offscreen-service-design.md).
The core contract, named completion and lazy Context seam passed build 26/run 22, including all 13 contract tests. They do
not yet create a renderer. **E1, E2a and E2b have scoped Windows build/full-suite/windowed parity evidence below. The explicit no-window build lane remains a separate qualification.** No completed extraction slice yet implements an offscreen renderer.

## E1 — give the existing device-independent recorder one library owner

Created `src/openrct2-renderer/CMakeLists.txt` and `src/openrct2-renderer/libopenrct2renderer.vcxproj`. This library
contains the three existing recorder/cache/ordering translation units and their eight headers. Moved the following files
once from `src/openrct2-ui/drawing/engines/gpu/` into `src/openrct2-renderer/gpu/`:

- `GpuCommandDrawingContext.{h,cpp}`, `GpuTextureCache.{h,cpp}`, `GpuTransparencyDepth.{h,cpp}`.
- `GpuAtlas.h`, `GpuBackend.h`, `GpuCommandStream.h`, `GpuFrameMailbox.h`, `GpuWeatherDrawer.h`.

These files include standard/core headers, and no SDL or Vulkan implementation headers. Keep all algorithms, command layouts,
atlas behavior, resident-generation caches and public signatures identical. Preserve the legacy `OpenRCT2::Ui::Gpu` namespace
in this mechanical slice to avoid mixing an ownership move with a broad namespace/API rewrite; its spelling does not create
a UI link dependency. Rename it to `OpenRCT2::Renderer::Gpu` with the later Vulkan namespace cleanup, and record the temporary
namespace debt explicitly. Do not retain copies or forwarding implementation files under the UI tree.

Update every UI Vulkan include currently using `../gpu/...`, plus test includes in `GpuFoundationTests`, `VulkanParityTests`,
`VulkanParityTestSupport`, `VulkanRuntimeIntegrationTests`, `VulkanScalingParityTests`, `VulkanSceneParityTests`,
`VulkanSpatialLightParityTests` and `VulkanTtfParityTests`, to the shared canonical path. Renderer-internal local includes stay
local. The core service header does **not** include these implementation headers.

Build changes in the same slice:

1. Root `CMakeLists.txt` currently uses `include` for core, CLI and UI, in that order. Include the new renderer project after
   core and before consumers, regardless of `DISABLE_GUI`. Define `OpenRCT2::renderer` as the library alias, with a dependency
   on core and the normal project compile checks/include root. The initial recorder target needs no SDL or Vulkan linkage.
2. UI CMake must link the new target; physical removal prevents its recursive source glob from owning these files. Test CMake
   removes direct `GpuTextureCache.cpp`, `GpuTransparencyDepth.cpp` and conditional `GpuCommandDrawingContext.cpp` compilation,
   and links the target. Keep the existing Vulkan backend test source list unchanged until E2.
3. Add the renderer project/configuration mappings to `openrct2.sln`. UI/test MSBuild projects remove those `ClCompile` entries
   and reference the renderer project. Ensure the Windows final executable receives the library transitively or explicitly
   through `src/openrct2-win/openrct2-win.vcxproj`; inspect actual link command/map rather than relying on static-library
   transitivity assumptions. The core project must never reference the renderer/UI project. `openrct2.proj` delegates to the
   solution, so it should need no new independent source list.
4. Extend `scripts/rendering/build-current-ui-parity.py` with a receipt-tracked renderer-library stage and explicit driver
   link input when the chosen source tree contains the new project. Its generated UI library must not compile the moved
   sources. Frozen trees keep their original three-stage core/UI/driver graph; detect this from the selected source layout,
   not from the current checkout. Include the new project's inputs/artifact hash/stage exit code in reuse validation.
5. Ordinary CLI rendering composition remains unchanged in E1: do not inject a factory that has no implementation. Linking
   the recorder into every simulation-only executable is unnecessary at this point.

E1 acceptance: build with Vulkan enabled and disabled; run device-free recorder/cache/contract tests and the current required
Vulkan parity suite; re-run representative actual-main UI baseline/window/native-terrain fixtures against frozen receipts.
Inspect the compile/link graph to prove each recorder TU has one library owner, and compile a small renderer-header consumer
without any SDL/Vulkan include directory. Shader hashes must remain unchanged. There is no new rasterization or offscreen
behavior in E1, so no duplicate mock rasterizer or new pixel algorithm tests are warranted.

E1 implementation checkpoint:

- [x] Move all eleven recorder files without changing their bytes, algorithms or legacy namespace. Move evidence: `obj/vulkan-parity/e1-source-move-proof.json`.
- [x] Give the three translation units one shared library owner in CMake/MSBuild, with explicit final UI/test link inputs.
- [x] Update active includes/source links; retain historical archive and evidence paths.
- [x] Track the isolated renderer stage, generated project, library hash and reuse validation only for source trees with the new layout.
- [x] Verify the ordinary enabled build: build 28 artifacts are identical to build 27; source graph checks show one owner for each moved translation unit and no SDL/FT header dependency.
- [x] Verify device-free and full current tests: run 24 passed all 722 tests.
- [x] Verify actual UI parity: UI 13 passed all 13 E1 regression runs (baseline, three incremental window families, night lighting, and eight native-terrain states), exact indexed/final RGBA with clean Vulkan validation.
- [x] Preserve frozen pixel provenance through receipt metadata renewal: [vulkan-e1-frozen-reference-renewal.json](vulkan-e1-frozen-reference-renewal.json) records unchanged pixel proof.
- [ ] Complete the broader cross-platform enabled/disabled build matrix and standalone header-consumer compilation; these remain qualification limits beyond the scoped E1 evidence above.

## E2 — invert the SDL boundary, then move the existing Vulkan implementation

Do this as two compiling checkpoints, with the same windowed parity tests between them.

E2a status: root applied all 16 staged candidate files after checking original hashes; application evidence is
`obj/vulkan-parity/e2a-staged/applied.json`. Build 29 passed with zero warnings/errors. Full run 25 passed all 724 tests in 67 suites, including both host-lifetime tests, with no skips/disabled tests/missing reports/validation errors. UI 14 passed all 15 `ui-e2a-vulkan-*` actual-UI runs (baseline, three incremental window families, lighting, eight native-terrain cases and two weather families), exact with clean validation. [vulkan-e2a-ui-qualification.json](vulkan-e2a-ui-qualification.json) pins the summary hashes. E2b was applied from the guarded 40-operation manifest. Build 30 passed with zero warnings/errors; full run 26 passed all 725 tests without failures/skips/disabled cases. Parent-run UI 16 qualification passed all 18 corrected loaded fixtures plus the saved EverythingPark comparison, exact with clean validation. Its independent applied source/build audit is `obj/vulkan-parity/e2b-staged/applied-independent-audit.json`. No E3 change is applied.

**E2a: presentation-host injection while files remain in place.** Introduce a renderer-owned Vulkan-only presentation-host
interface with owned required-extension names, surface creation/destruction and drawable-extent queries. Implement it in the
existing UI `VulkanPlatform.{h,cpp}` (or rename that pair to `SdlVulkanPresentationHost` in the same change). Its instance holds
the SDL/window lifetime requirement. Before E2a, `Backend::Initialise` rejected a null `nativeWindow`, cast it to `SDL_Window*`,
and passed that to `Device::Initialise`; E2a replaces that dependency with an explicitly supplied host at the backend factory.
`Device` calls the host, rather than static UI platform functions. Update every backend construction in the frontend and
runtime/parity tests together. Preserve present mode, surface creation order, extension negotiation, HDR metadata, acquisition,
readback and the recently corrected semaphore/descriptor synchronization behavior.

The host contract lives in the renderer module, never in core. It may use Vulkan handles; it must not mention `SDL_Window`.
Library loading must also stop depending on SDL before claiming a surface-free implementation. Initially the existing windowed
host can retain its platform-specific loader behavior; E3 supplies the explicit offscreen loader route. A UI host's surface
must outlive all associated swapchain jobs, and a failed incompatible attachment must not destroy an active shared device.

**E2b: move code ownership without changing execution.** Once the shared backend has no UI dependency, move all Vulkan
implementation files except `VulkanDrawingEngine.{h,cpp}`, `VulkanDiagnosticCapture.h`, the SDL platform adapter and
`VulkanScreenPalette.h` to `src/openrct2-renderer/vulkan/`. The moved set is:

- `VulkanBackend`, `VulkanDevice`, `VulkanResources`, `VulkanShader` and all seven pipeline pairs: Line, Rect, WorldSurface,
  Transparency, Weather, LightFx and Palette (there is no separate sprite pipeline class).
- `VulkanCommandLayouts.h`, `VulkanSurfaceFormat.h` and the new presentation-host interface.

Keep actual-screen opacity conversion in the UI frontend; it must not become the auxiliary alpha policy. Keep diagnostics
attached to frontend packets and adapt only their type includes. The shared backend's existing diagnostic readback can remain
available to the frontend, but ordinary frames must not gain readbacks.

Move Vulkan SDK/shader discovery and the single `openrct2-vulkan-shaders` generation/install owner from UI CMake to renderer
CMake. This is essential when `DISABLE_GUI=ON`; today SDK discovery does not even run without the UI project. Preserve the
existing shader source paths and output/package locations. UI/tests/CLI can depend on the one generated shader target; never
compile private copies of the shader list into each consumer. Audit the MSBuild `openrct2.vulkan.props`/data-project ownership
before changing it: shared activation stays consistent and one data target continues to own shader outputs.

Test CMake drops `OPENRCT2_VULKAN_BACKEND_SOURCES` recompilation and links the renderer library. Its SDL runtime fixtures need
only the real SDL host adapter, not the entire UI implementation. MSBuild tests can likewise stop linking the whole UI library
solely to obtain backend classes after explicitly supplying that adapter. Update the isolated UI/terrain harness link receipts
when their chosen source tree has the renderer project. Frozen oracle source/build semantics remain unchanged.

E2 acceptance includes source/dependency audit (shared renderer contains no UI/SDL includes), unchanged SPIR-V hashes, one backend
object owner, exact current parity corpus, named UI diagnostics, and validation-clean acquire/abandon/resize/capture tests.
Do not describe a null-host rejection as offscreen support: this step still presents through the existing device implementation.

## E3 — split device/slots and presentation before exposing an offscreen factory

The source-backed ownership split is the following. Refactor one row at a time behind the existing windowed frontend.

| Owner | Move from current code | Lifetime and boundary |
| --- | --- | --- |
| `DeviceContext` | `Device` instance/physical-device/logical-device/graphics-queue/pipeline-cache and format/device capability checks | One lazy service owner; selects graphics-only for headless, or the registered UI requirements before device creation. No window, swapchain, image index or logical canvas size. |
| `SubmissionSlots` | `Device::FrameResources` command buffers, upload rings, completion fences and timestamp pools | Lease independent of swapchain acquisition; queue submission serialized by the service. A slot is reusable only after its own submission fence, never after a caller timeout. |
| `PresentationSession` | Surface, present queue, swapchain/images/views, image-available semaphores, per-image present-ready semaphores, acquire/recreate/present state, HDR settings | Holds `DeviceContext` and UI host references; presentation-image retirement remains distinct from command-slot retirement. Surface incompatibility returns an error. |
| `ResidencyDomain` | `IndexedResources` atlas, sprite descriptors, remap/blend/falloff textures, committed/recorded layout state; recorder/cache residency | Main domain plus one reusable auxiliary domain. No resize of an auxiliary target may discard the main atlas or in-flight descriptor generations. |
| `TargetResources` | Indexed/depth/composite/transparent/light images and palette snapshots currently stored in per-frame arrays inside `IndexedResources` | Explicit target ID/generation, bounded pool and slot lease. Main resources remain their existing three slots initially; auxiliary work does not allocate a main-sized atlas per request. |
| `FrameExecutor` | Current `Backend::Submit` pass ordering, upload helpers and pipeline use | Records into an explicit target/domain and submission slot. Has no “latest presented” identity or SDL knowledge. Accepts current compatibility commands and can consume retained scene handles/delta batches later. |

First separate a submission-slot token from `FrameToken`'s `imageIndex/extent`. Preserve the existing windowed acquire and submit
sequence with an adapter that combines the two tokens. Then permit graphics-only device selection: queue completeness no longer
requires present support for a headless service, and `VK_KHR_swapchain` is requested only by presentation requirements. The loader
must initialize without SDL video; do not silently add a hidden window. Optional support for a completely absent loader *library*
remains the packaging contract and must not be claimed merely because device creation is lazy.

Only after those tests pass, separate target-sized resources and change `PalettePipeline::RefreshSwapchain/Record` to accept an
explicit output descriptor (view, format, extent, encoding, initial/final layout and generation). The main adapter passes its
acquired swapchain image with `PRESENT_SRC_KHR`; an owned SDR output image will end transfer-readable. Keep the same palette,
scaling, lighting and ordered transparency shaders. Descriptor sets and framebuffer caches are keyed to leased target generations,
not globally refreshed under another in-flight job. Preserve recorded-versus-submitted layout rollback on abandon.

E3 evidence must include actual graphics-only device initialization in a process which never initializes SDL video, zero surfaces
created, one device shared by sequential submission domains, slot/target generation tests, and validation-clean resource reuse.
Device-free tests can validate extension/queue requirement selection and target-pool lease accounting with injected capability
records; they cannot establish real GPU barriers, image ownership or parity. Keep those assertions in runtime tests.

## E4 — first real service implementation, without migrating gameplay callers

Implement a renderer `IRenderServiceFactory` and service/session using **the extracted recorder and executor**. Begin with one
reusable bounded auxiliary domain, indexed and SDR RGBA output, explicit clear/existing-index upload and named fence readback.
The service owns the single `DeviceContext`; sessions own addressable clipping storage and immutable request/palette/lookup
snapshots; submitted jobs independently own all GPU leases until their fences complete. Connect completion to the existing
core identity/timeout/cancellation/error contract. Reject unsupported requests explicitly while adding capabilities; do not
substitute CPU palette expansion or the frozen software renderer.

The first runtime tests are surface-free, not another windowed screenshot harness:

- Render the same small fixed commands through the existing windowed executor and a surface-free target; compare exact indexed
  and RGBA bytes, then frozen-reference sample images. Exercise both opaque-screen and transparent-index-zero alpha policies.
- Start from owned indices, edit only a clipped rectangle/glyph/filter region, and assert untouched bytes remain. This is needed
  before script image migration; clear-to-zero alone does not satisfy the contract.
- Alternate extents, palettes and target generations; retain old result bytes while reusing targets. Require named mismatches
  to fail and memory/device creation counters to remain bounded across hundreds of requests.
- Interleave main/auxiliary jobs on one device, including an auxiliary synchronous wait while a main packet remains unsealed.
  The worker must not wait for that incomplete main packet or hold a main-recorder mutex. Test cancellation, timeout, shutdown,
  abandon and injected device-loss delivery while retaining submitted leases until safe retirement.

Keep park previews, giant screenshots and scripts on their existing paths until the corresponding caller-specific frozen gates
pass. Once E4 is qualified, application composition can inject the real factory into UI and image-producing CLI contexts. The
CLI's `Cli.cpp` currently creates a plain context and `CommandLineRun` may execute commands before that branch; audit each image
command's actual context creation instead of assuming changing this one `main` call injects every CLI operation.

## Guardrails for retained GPU scene work (Gate P)

E1–E4 establish ownership and auxiliary rendering, not final performance completion. Do not expose `FrameCommandStream` as the
permanent core/world API or make service results require per-frame readback. Preserve the existing world-surface chunk revisions,
VRAM buffers and atlas generation leases through extraction. `FrameExecutor`/`ResidencyDomain` must have service/session lifetime,
with targets providing views into that persistent state. Their interface can add typed retained-world/entity handles and dirty
delta batches without replacing the service or allocating a device per camera.

Measure uploaded bytes, asset uploads, descriptor updates, scene-buffer revision changes, command reconstruction cost and resource
allocations at the shared executor boundary. An unchanged resident scene/camera-only update must not be forced through a full
CPU paint reconstruction by the new ownership API. Preserve the compatibility recorder for measured UI/auxiliary use and parity
comparison while Gate P moves repeated world transforms/culling/ordering into shaders. The module extraction must not reset or
re-upload retained terrain on each output target, and pooling must not trade CPU savings for unbounded VRAM duplication.

After E1 passes, the next review should use its actual build/link receipt and fresh parity results to choose the E2a host interface
patch. Avoid starting E2/E3 file moves while another agent is correcting a measured renderer gap in those files.

## E2 source/build audit after E1 build 27

E1 ordinary build 27 passed with zero warnings/errors; its full parity rerun and new actual-UI build remain parent-owned gates. This audit changes no implementation and does not authorize skipping those gates.

The exact E2b existing-file move is **24 files / 11 translation units**: `VulkanBackend`, `VulkanDevice`, `VulkanResources`, `VulkanShader`, `VulkanLinePipeline`, `VulkanRectPipeline`, `VulkanWorldSurfacePipeline`, `VulkanTransparencyPipeline`, `VulkanWeatherPipeline`, `VulkanLightFxPipeline`, and `VulkanPalettePipeline` pairs, plus `VulkanCommandLayouts.h` and `VulkanSurfaceFormat.h`. The six existing UI-owned files remain `VulkanDrawingEngine.{h,cpp}`, `VulkanPlatform.{h,cpp}`, `VulkanDiagnosticCapture.h`, and `VulkanScreenPalette.h`. Add only the renderer-owned presentation-host header before this move. Preserve current namespaces through the compiling boundary; namespace cleanup is independent debt.

### E2a concrete control boundary

- Add a Vulkan-only `PresentationHost` contract under `src/openrct2-renderer/vulkan/`. Its required extension names are owned strings. Its operations acquire/release loader access, create/destroy a surface, and query drawable extent. No core or GPU-neutral header acquires an SDL/Vulkan dependency.
- Change `CreateBackend()` to accept an owned presentation host. `Backend` retains it before its `Device` member, so device disposal precedes host destruction. `Device` can borrow that host during its lifetime. The UI host borrows `SDL_Window`, whose owner must outlive backend disposal; retaining a wrapper does not extend window lifetime.
- Remove the `BackendConfig::nativeWindow` field and the backend's SDL cast together with all known callers. The frontend, primitive/runtime/scaling/scene/spatial-light/TTF tests supply the SDL host explicitly. Tests have seven creation sites across six files, plus the frontend. Runtime tests contain two sites. This is an internal call-site conversion, not a new global renderer or eager device creation.
- `Device::Initialise` retains current extent validation, loader acquisition, instance/surface/device selection and exception cleanup ordering. Replace only the four `Platform` calls for loader load/unload, extension query and surface creation; route surface destruction through the same host. Preserve portability/color-space extension augmentation and loader release after instance destruction. Null hosts fail before any GPU work. Backend construction remains device-free.
- Keep `GetRequiredSdlWindowFlags` and UI window access in the platform adapter. Keep framebuffer size delivery and resize-generation behavior unchanged; do not combine this with swapchain/device separation or synchronization changes.
- Meaningful narrow tests: constructing/discarding a backend with a fake host makes no loader/GPU calls; a host that throws during loader acquisition is released exactly once without pretending a loader lease was acquired; null-host rejection. Existing real-device lifecycle tests prove the successful route. Failure after instance creation still requires actual-device validation.

### Build and diagnostic details

CMake currently discovers Vulkan and generates all **17** Vulkan shaders inside UI. Move that discovery and the single shader target into renderer CMake before UI/CLI consumers. Add the eleven backend sources only when enabled, expose Vulkan headers/`ENABLE_VULKAN` to consumers of Vulkan headers, and link `Vulkan::Vulkan`. Core retains only its existing build definition where needed; it never links renderer. The initial recorder target's public core dependency inherits `DISABLE_TTF`, networking/scripting flags and sanitizer options. A recursive source-include audit traversed 52 recorder/core headers and found only standard/compiler external headers: **no SDL or FreeType includes**. `TTF.h` exposes opaque font types and a core-owned raster structure, so GUI-disabled recorder builds do not need SDL/FT include directories. Enabled/disabled compilation remains a separate gate from this source audit.

Once discovery moves, `DISABLE_GUI=ON` may enable Vulkan even though SDL discovery previously ran only in UI. Split SDL-window runtime/parity fixture registration from device-free foundation tests; require SDL only when those windowed fixtures are selected. Do not transitively add SDL to renderer to make the old test list compile. Device-free Vulkan format/present-policy tests can use shared headers without SDL. Later offscreen tests must form a separate surface-free group. Required windowed suites must fail configuration/missing prerequisites explicitly rather than silently disappear.

Test CMake removes backend recompilation and adds only the SDL platform adapter source for windowed tests. MSBuild tests can stop linking the entire UI archive for backend symbols after explicitly compiling/linking that same adapter; the header-only diagnostic request tests still include `VulkanDiagnosticCapture.h` without pulling in the frontend. The named capture API, packet fields, result/coverage metadata and `OPENRCT2_VULKAN_DIAGNOSTICS` remain frontend-owned. No diagnostic-only macro is required by the shared backend, and normal rendering still performs no readbacks.

The isolated UI builder already chooses three frozen stages or four current stages from the selected source tree. E2 changes the current renderer project's source list, Vulkan includes/defines and linker input, not stage count. Its existing manifest/project/library hashes invalidate affected reuse. `OPENRCT2_VULKAN_DIAGNOSTICS` remains on the cloned UI project/driver, not renderer. Frozen-source projects retain original includes and source ownership. The native-terrain preparer links core only and needs no renderer library merely to export/reload fixtures.

MSBuild `openrct2.vulkan.props` already supplies common enablement and final `vulkan-1.lib` input. Add conditional Vulkan definitions/include paths to the new renderer project. Keep the existing data-project/`openrct2.targets` shader owner; do not import its compilation targets into renderer and create duplicate shader producers.

A packaging issue is visible in the existing CMake graph: normal shader compilation writes to the binary data directory, but root installation copies source `data/` and does not explicitly install `VULKAN_SHADER_OUTPUTS`. E2 should explicitly install the generated output list after source-data installation (or exclude source SPIR-V from the source-data copy) so a generated shader cannot be omitted or overwritten by stale source SPIR-V. Preserve the current macOS bundle output path for GUI bundles; GUI-disabled operation must use the ordinary data path even if a bundle option is configured. This is a build/package correction within the approved module extraction, not a rendering behavior change.

No new architecture decision beyond the approved service design was identified. E2 remains windowed-only; sharing one actual device across sessions and surface-free execution are E3 work. Retained world-state/resource caches stay in the shared module and must not be rebuilt per auxiliary request.

## E3a staged checkpoint and first actual shared-device slice

### Explicit no-window qualification before E3a

The first direct MSBuild attempt is preserved at `obj/vulkan-parity/e2b-no-window-01`. Passing `BuildProjectReferences=false`
did not prevent metadata traversal into dependency projects; a global intermediate directory collided across projects, and
the inherited environment contained both `PATH` and `Path`. This failed attempt supplies no no-window qualification.

`scripts/rendering/build-no-window-tests.py` generates an isolated copy of the actual test project. It retains source conditions
and `EnableVulkanWindowTests=false`, removes all four project references, uses separate object/PCH/output directories, normalizes
Windows environment names, and copies test assets locally. It never builds core/renderer/UI/dependency projects or downloads
assets. Both core and renderer must pass the existing UI builder's source/dependency/compiler/options/generated-metadata/library
hash checks. A successful ordinary build receipt independently pins every test input. Only generated `.pdb` files are excluded
from test-source comparison. Any mismatch fails before test compilation; there is no fallback source rebuild.

The root-owned qualification uses current UI 18/build 32 receipts, so its label is **E2b plus telemetry/font changes**, not a
reconstruction of untouched build 30. Example invocation (output must be new):

```powershell
python scripts/rendering/build-no-window-tests.py --output obj/vulkan-parity/e2b-no-window-03 --reuse-build obj/vulkan-parity/ui-build-18/receipt.json --test-source-receipt obj/vulkan-parity/build-32/receipt.json
```

The receipt lists the six uncompiled windowed fixtures and explicitly sets `windowedParityQualified=false`. The builder does
not run tests. Legacy common MSBuild properties still name SDL's static library and include directories; this lane verifies
the absence of UI bindings/window fixtures, not SDL-free dependency packaging or full parity. Full parity remains the required
default windowed test lane.

For the narrower actual module compile boundary, `scripts/rendering/prepare-sdl-free-renderer-check.py` accepts that successful
or prepared no-window receipt, a new output directory, and the local VS-bundled `cmake.exe` path. It emits configure/build
commands without executing them. The generated project includes the real renderer `CMakeLists.txt`, asserts all fourteen
translation units and the shader target are selected, and supplies no SDL include/library metadata. A tiny core target adapter
publishes repository headers plus the verified core archive; the consumer only checks public declarations. This can qualify
renderer/header compilation and shader generation without SDL, but not core dependency packaging, the whole GUI-disabled
application, actual GPU execution, or image parity. Root executed this optional check from the verified
`e2b-no-window-prepared-04` inputs: configure/build passed, all fourteen production renderer translation units and seventeen
shaders compiled, and every generated SPIR-V file matched ordinary build 32. The generated renderer inputs contain only the
repository source root and Vulkan SDK, with core/Vulkan link inputs. Source/generated hashes stayed stable.
[vulkan-sdl-free-renderer-compile.json](vulkan-sdl-free-renderer-compile.json) pins the compile qualification. No GPU/device or
surface-free rendering was executed.

The no-window03 test link exposed four SDL audio implementation tests mixed into `SpatialAudioTests.cpp`. The staged partition
guards those four with `OPENRCT2_TEST_NO_UI_AUDIO` only for explicit no-window metadata, reports the omitted coverage, and keeps
all twenty-three core spatial tests. Normal default coverage retains all twenty-seven tests unchanged. The two build metadata
candidates are combined with the independent retained-balloon test registration at
`obj/vulkan-parity/no-window-audio-balloon-staged/manifest.json`; the test-source candidate remains in
`obj/vulkan-parity/no-window-audio-staged/manifest.json`. Failed no-window03 is retained as failure evidence, not test acceptance.

After guarded application, no-window05 passed using current UI21 libraries and build33 test inputs. Its fresh focused run,
`obj/vulkan-parity/e2b-no-window-run-01`, passed all 75 selected cases: 41 foundation, two host, three diagnostic completion,
six retained-balloon and twenty-three core spatial-audio tests, with no skips. This closes that scoped no-window link/test
qualification; legacy SDL dependency metadata and broader platform/package limits still apply.

### Submission token seam

`obj/vulkan-parity/e3a-staged/manifest.json` and `e3a.patch` stage sixteen source modifications against applied E2b. They are not applied. `SubmissionToken` now describes only command-buffer/slot/upload access; windowed `FrameToken` adds the acquired image and physical extent. Six indexed/world/transparency/weather/light recorders and timestamp recording accept the former; final palette presentation accepts the latter. Static inverse checks confirm existing Device, Backend and Palette algorithms are unchanged apart from field adaptation, and the six recorder bodies differ only in parameter type. This is the first compiling seam, not surface-free execution or safe submission lease implementation.

That original stage is superseded for application by `obj/vulkan-parity/e3a-telemetry-staged/manifest.json`, rebased onto the
qualified live telemetry source. The telemetry pointer belongs to `SubmissionToken`; submit marking, upload/write counters,
and backend attachment/detachment remain unchanged. All sixteen files pass inverse adaptation checks against current originals,
ignoring whitespace only. Parent applied it with the retained-balloon foundation/audio partition using the guarded receipt
`obj/vulkan-parity/e3a-balloon-audio-application.json`. Build33 and validation full run28 passed all 733 tests. The module still
has no surface-free rendering path at this applied checkpoint.

### E3b shared-device implementation and qualification

`obj/vulkan-parity/e3b-staged/manifest.json` stages fifteen files against that applied checkpoint: a shared `DeviceContext`, an
independent loader lease, extracted `SubmissionSlots`, windowed facade adapters, build registration, and four new tests. The
graphics-only factory requests no WSI/swapchain/HDR support and invokes no SDL. Two independent slot domains can retain one
physical/logical device and submit real transfer work through its shared queue synchronization. Owner/generation checks reject
cross-domain and stale tokens; an incomplete fence wait does not release resources or advance generations.

The new runtime fixture verifies actual GPU fill/readback through both domains, shared-device lifetime after the creator and
one domain are released, and correct data after reuse. Required Vulkan mode must fail instead of skipping. The stage's static
audit inversely compares eleven windowed methods, every upload-ring method, query harvesting and extracted resource creation;
shaders are unchanged. The guarded stage was applied. Build34 and validation full run29 passed all 737 tests. A fresh no-window contract run passed six cases, including both real GPU two-domain cases, with validation active and clean. UI22/build34 passed nine representative frozen-reference regressions (60 captures) exactly. [vulkan-shared-device-qualification.json](vulkan-shared-device-qualification.json) pins this scoped evidence. No offscreen indexed/RGBA executor or
concrete core service implementation is claimed at E3b; see the staged README for linked-loader packaging, teardown synchronization, borrowed
facade aliases, and remaining platform/lifetime gates.

The E3b implementation followed this reviewed sequence:

1. Move the existing instance, physical/logical device, graphics queue, pipeline cache, memory/format capabilities and device-selection code out of `VulkanDevice` into `VulkanDeviceContext.{h,cpp}`. `Device` becomes the compatibility window/submission adapter holding a shared context. Preserve current physical-device scoring, required formats and Vulkan API version. Do not duplicate backend/pipeline execution.
2. Make instance/device requirements explicit before first creation. Graphics-only selection needs a graphics queue and rendering formats, and requests no WSI/swapchain/HDR extensions or present queue. UI composition registers WSI extension and surface requirements before lazy creation. Surface creation is the existing two-phase bootstrap: create instance, create/probe initial presentation surface, select compatible device, then retain surface in its presentation owner. Roll back surface before instance on any failure.
3. Give context lifetime an independent loader lease. Graphics-only initialization must never call SDL video/load/surface functions. Retaining a borrowed window host solely to keep a shared loader alive is unsafe if auxiliary work outlives the main window. Preserve platform loader/provider consistency, especially the current SDL non-Windows/MoltenVK route. The existing direct Vulkan import still means a completely absent loader library is a packaging limitation; lazy device creation alone does not remove it.
4. Split command-pool/buffer/upload/fence/timestamp ownership into `SubmissionSlots` referencing that context. Window image-available and present-ready semaphores remain presentation resources. Move queue external synchronization to the shared context or single service executor, with waits outside the short queue-submission lock. A submission timeout/cancel cannot return its slot or target generation early. Existing `FrameToken` bridges this to current windowed acquire/present behavior until callers are migrated.
5. Add one required surface-free runtime fixture in the explicit no-window lane: initialize in a process that never initializes SDL video; create zero surfaces; share the same physical/logical device across two independently owned submission-slot domains; submit bounded transfer work, wait actual fences, and verify reuse/shutdown under validation. This proves device/slot ownership only. Indexed/RGBA parity through the existing executor comes after explicit target separation, not through a new test rasterizer.

After this device/slot slice is qualified, separate target-sized resources from residency, make palette output image/layout/generation explicit, and implement the existing core `IRenderService` contract using the same executor. `RenderCompletion` delivery state must remain independent of submitted-job ownership. Service shutdown drains jobs before releasing shared context resources; immutable request palettes and output identities survive pooled target reuse. Main/auxiliary residency domains remain persistent and bounded, and camera-only changes must not reset shared world/atlas residency.

Risks that must be resolved in implementation, within the approved architecture: a device created for headless-only extensions may be incompatible with a later UI surface (return a precise compatibility failure, never silently recreate it under in-flight jobs); physical device/surface bootstrap rollback; loader ownership independent of SDL window lifetime; shared-queue synchronization and command-pool thread affinity; per-image semaphore retirement distinct from per-slot fence retirement; and target/descriptor generation leases. None justifies widening E3a into a shader or rendering rewrite. Telemetry staging touches the same token fields: its pointer belongs in `SubmissionToken`, while pipeline counter bodies keep their existing `frame.*` spelling.


### E4 qualified indexed executor and bounded service

`obj/vulkan-parity/e4-staged/manifest.json` records the applied executable checkpoint. The existing indexed upload/draw/LightFX
sequence moves once into `VulkanFrameExecutor`; main presentation delegates to it. Resources/slots gain bounded counts with
unchanged main defaults. The palette pass accepts an explicit RGBA8 output attachment and transfer-readable final layout.
`VulkanRenderService` implements the approved core contract using one persistent auxiliary cache, one independent slot and the
injected shared device. A lazy graphics-only provider exists for future CLI composition; no production caller/default changes.

The eight tests cover frozen primitive/covered-zero sprite indexed and RGBA equality, three alpha policies, owned initial
indices, source mutation after sealing, pooled target generations, independent main recording, timeout/cancellation retirement,
and no-GPU unused/cancelled session behavior. Build37/full run32 passed all 750 tests; a fresh no-window offscreen run passed all eight cases with active, clean validation. Thirteen main-UI regressions passed 93 captures exactly. The SDL-free renderer check compiled all eighteen translation units and all seventeen SPIR-V outputs matched build37. [Offscreen service qualification](vulkan-offscreen-service-qualification.json) and [SDL-free module qualification](vulkan-sdl-free-renderer-compile-e4.json) pin this evidence. Its README records the exact initial limits:
no nested auxiliary admission, LightFX/native-world snapshots, giant tiling, automatic dynamic-image invalidation, compatibility
drawing-engine adapter, application factory installation or migrated caller. These remain required migration work, not accepted
parity exceptions. Main defaults stay three frame targets and sixty-four atlas layers; auxiliary defaults use one target set,
four layers, a 32 MiB upload ring and a four-million-pixel cap, omitting eager maximum-world buffer allocation.
