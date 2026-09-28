# Plan B implementation journal

The owner selected retained sprite components on September 23, starting from `ba5b9d8a92`. Keep experiments only when they demonstrate a meaningful improvement or a cleaner implementation that advances the approved architecture. The final performance requirement remains substantial improvement in ordinary parks, not merely a faster isolated shader.

The owner subsequently requested one long implementation turn followed by manual review. The app goal stays paused. This is an implementation checkpoint, not authorization to resume automatic work cycles or a claim that the migration is complete.

After reviewing the checkpoint, the owner explicitly ruled out lazy repainting and directed a rewrite of the CPU-driven world pipeline into GPU logic. Previous heavy-scene repaint experiments usually redrew most pixels and suffered stale-chunk failures. The updated plan therefore requires a fresh complete world redraw every frame, resident raw inputs/assets, no previous-world-pixel dependency, and worst-case dense moving-scene benchmarks. CPU-side graphics invalidation/cached paint expansion is removed from the proposed approach; raw-state publication optimizations remain conditional on measured cost and simple correctness. This documentation clarification changes no build64 source or installed binary.

## Baseline and attribution

EverythingPark, saved camera, scale one, actual 3840×2160, 144 Hz display, ordinary Turbo, 100 warmup and 3,000 measured ticks. These are hidden application draws, not scanout measurements. Both runs preserve the expected state census and checksum `9b7eee204b0471d7000000000000000000000000`.

| Build56 lane | TPS | Draws/sec | CPU draw | GPU frame |
|---|---:|---:|---:|---:|
| Clean (`plan-b-baseline-clean-01`) | 67.318 | 28.879 | 26.996 ms | 6.078 ms |
| Instrumented (`plan-b-baseline-profile-01`) | 25.723 | 12.527 | 72.721 ms | 2.088 ms |

The profiler substantially changes timing. Its inclusive worker totals are neither critical-path wall time nor additive percentages. Use only clean counterbalanced runs for improvement claims. GPU timing also varies between these runs; it is not a demonstrated regression caused by any candidate source change, since both use the same pinned build56 executable.

The instrumented run records 702,741 column preparations over 1,461 draws (481 per draw) and 21,272,107 guest painter calls. Serial column drawing accumulates 19.956 ms per draw; entity snapshot capture averages 0.456 ms and its worker-side index construction 0.826 ms; end-of-frame recording averages 0.355 ms. Guest painting alone accumulates 3.333 ms per draw across workers. This supports prioritizing retained common-world preparation, rather than expecting a peep image selector alone to remove the bottleneck.

## Shared ordering investigation

The current comparator and both sort variants match local upstream. A bounded regression demonstrates that legacy insertion can reverse two overlapping static components when a moving third component sorts in front: `[vehicle, support, tree]` becomes `[tree, support, vehicle]`. Upstream's existing optional stable variant retains `[support, tree, vehicle]` for that case. An initial candidate enabled that variant by default; the default change was subsequently withdrawn because real tree/support samples did not qualify it. Three tests retain the useful evidence across all rotations, movement, separating bounds and reversed ambiguous input order. No object-type priority is imposed.

This does not prove the supplied cherry-tree screenshot is fixed. Frozen-driver comparisons use EverythingPark birch/wooden-track locations `(154,210)` beside `(155,210)`, and `(189,150)` with taller track on the same tile. Both old/stable policies produce twelve deterministic captures per location. Independent visual review remains required before accepting the default change. Native terrain/balloon admission still declines stable sorting; changing that restriction requires its own GPU ordering proof.

The agent subsequently inspected all fourteen changed-region crops: 26 pixels in the adjacent-tree view and 49 in the taller-track view, all on guest overlaps. Trees/supports were identical. These samples therefore do **not** qualify the candidate default change. The next explicit test fixture substitutes an already-loaded cherry for the adjacent birch, preserves real track/vehicle state and records the mutation. Rotations 1 and 2 put the selected tree tile in front of the track tile. No exception is accepted for the unexplained guest differences.

Cherry attempt01 is rejected: the harness disposed the complete map publication after marking only one tile dirty. A cold publication then received that delta without a complete bootstrap, omitting most of the world and preserving stale loading-window pixels. Agent review caught this despite deterministic capture checks passing. Frozen driver24 preserves the complete publication; attempt02 restores the full world. The installed original JAPCHBLO art is yellow, confirmed by independent DAT decode and 1,660 matching opaque pixels at the expected target anchor; yellow does not imply a stale birch. The user's pink-tree scene still needs an exact representative overlap.

## Build and temporal controls

Build57 failed with MSVC C1060 (compiler heap exhaustion) in `S6Importer.cpp`, with no system crash or source mutation. Its compiler read log identifies `Hostx86/x64`. The ordinary build runner now selects `PreferredToolArchitecture=x64`, matching the existing isolated UI builder, while retaining `/m:1` and disabled `/MP`. The failed receipt is preserved; build58 is the serial retry.

Build58 completed the core and renderer, then rejected two new test assertions for discarding a `[[nodiscard]]` return value. Explicit `static_cast<void>` inside those throwing assertions fixes that warning without relaxing build settings. Build59 passes with zero warnings/errors and unchanged source hashes; peep probe build01 also passes. Full validation run49 is the corresponding execution gate.

Run49 passes all 821 tests from 92 suites, including 66 existing image-parity cases and both new actual-device peep tests, with no validation diagnostics. The seven pure motion-validator tests pass. Subsequent build60 contains additional publication/catalog and mixed-scene code and must qualify independently.

The mixed frozen producer build02 passes using the byte-qualified original core. `mixed-corpus-01/receipt.json` accepts two identical fresh-process runs, each with sixteen 192x128 original-painter cases (four rotations, two poses, two sort policies). Input includes 202 static owners, three raw peeps and original decoded assets. This proves repeatable reference input, not GPU parity or realistic-load performance. The GPU experiment shares the existing device, atlas and indexed pipeline, and tests one common depth stream plus zero source uploads on a repeated camera frame. Admission remains closed.

Presentation generations now carry the captured entity source tick. Current motion captures also record the immutable vehicle payload actually consumed. The motion validator rejects missing or differing consumed-state evidence, even if both live censuses match. Older frozen drivers lack this observation and cannot qualify moving comparisons until instrumented; static comparisons remain usable. This closes an evidence gap without treating stale-frame differences as approved rendering exceptions.

## Implementation gates

- [x] Record owner selection and reversal criterion.
- [x] Establish clean 4K baseline and separate CPU attribution.
- [x] Implement raw 96-byte peep records and explicit consumption of the existing dirty/lifecycle worklist; no extra steady-state population scan. Ordinary frames add small constant-time profile checks; graphical object mutations rebuild catalog facts.
- [x] Reject same-epoch resets that could erase entity/object tombstones; preserve immutable previous generations and strong-owned animation facts.
- [x] Implement shared shader selection/projection and frozen-painter oracle tests, including actual-device probe code.
- [x] Compile and execute the initial state, ordering and device tests with clean validation (build59/run49).
- [x] Integrate the explicit retained publication profile in the scene owner; cover deletion, identity reuse, skipped consumers, object catalog load/unload/reload and owner recreation.
- [ ] Apply and measure the staged stationary-appearance mutation hooks; qualify publication cost and asset image leases before live admission.
- [ ] Render retained static and dynamic components together with GPU visibility/order and exact indexed pixels.
- [ ] Independently inspect every ordering/pixel divergence and record a narrow corrected expectation where justified.
- [ ] Repeat clean counterbalanced 4K benchmarks of at least 3,000 measured ticks, plus sustained population growth and actual displayed pacing.
- [ ] Retain only justified changes; qualify and deploy a stable checkpoint.

The new contracts and shader tests are implementation work, not a completed runtime renderer or performance gain. Ordinary mixed parks still use CPU world painting until the shared visibility and publication gates pass. The exclusive-renderer, auxiliary, platform and removal requirements in the main migration checklist remain open.

## Integrated publication qualification

Build62 passes with zero warnings/errors. Its focused publication run passes all 35 tests. Full run50 executes 837 tests: 836 pass; only the new experimental mixed-scene raster test fails. The 66 existing image-parity cases pass, with clean Vulkan synchronization validation. The full run is recorded as **failed**, not relabeled as a passing qualification.

The explicit raw profile omits copied full guest/staff pools and their CPU spatial index. One authoritative publisher prepares the existing dirty/lifecycle worklist, then acknowledges it only after successful capture. Records carry identity generations; previous immutable snapshots and owned animation-rule facts survive newer publications. Ordinary frames do not scan the whole peep population for raw publication. Initial bootstrap and catalog changes are separately accounted work.

Three lifecycle corrections accompany the integration: a cold map publisher requests a complete bootstrap even if the prior producer already consumed its reset; retained publication epochs are unique across owner recreation; synchronous capture/scheduling failure forces coordinated entity/map reconstruction before exposing another generation. A background worker exception remains fail-closed and requires owner replacement; general in-place recovery is not claimed.

The optional object catalog is rebuilt at object mutation boundaries and read through an O(1) getter. Headless/no-graphics operation deliberately exposes no catalog. Owned rule facts do **not** yet lease sprite pixels or atlas allocations. The staged appearance mutation guards also remain unapplied until their cost can be measured with a benefiting renderer. These are explicit admission blockers, not optional cleanup.

## Mixed-scene falsification

`mixed-run-01` renders original terrain, path, tree, track, supports and three peeps together on the actual GPU. All 32 draws have clean validation, valid indirect commands, 46 visible components and intact output guards. Initial world-source upload is 54,144 bytes; the changed peep pose uploads 288 bytes; the other 30 camera/repeat samples upload zero world-source bytes. Atlas/remap initialization is separate. These numbers establish residency behavior in this finite fixture, not large-park throughput.

The initial bounds-origin depth scalar fails all sixteen frozen comparisons. Against the original legacy ordering, differing pixels by rotation are `[41,44,47,57]` for pose0 and `[41,44,43,57]` for pose1. Independent replay reproduces the GPU output exactly and agent inspection covers every distinct changed region. The principal error is an upper support beam drawn over its rail; other errors affect balloon and handyman/path contacts. This candidate is rejected for live use.

The frozen legacy and optional stable policies themselves disagree by 43 pixels at rotation3. Agent inspection identifies the stable policy drawing a rear support through a foreground guest and balloon. Legacy is the canonical reference for this bounded fixture; stable results remain negative controls. Neither accepting both incompatible policies nor hiding differences behind a pixel tolerance is valid. No new rendering exception has been approved.

A geometric plane-envelope refinement uses authored height for horizontal surfaces, physical anchors for upright sprites, and actual height intervals for finite support columns. This retains the original quads and art. Build63 / `plane-run-01` confirms the prediction on the device: canonical counts are `[0,0,6,5]` and `[0,0,2,5]`. All 64 indexed/RGBA buffers match independent predictions, with clean validation and unchanged inputs. The beam/rail and balloon errors are removed. An agent reviewed all eight canonical scene pairs and every differing region; the remaining handyman/path contact pixels are visually ambiguous, so no exception is accepted. The [review](vulkan-plan-b-plane-visual-review.md) and versioned `test/mixed-parity/experimental-plane` preserve the result and its strict failure. A failed scalar candidate does not prove that every scalar approach is impossible.

## Stable source checkpoint

Build64 passes with zero warnings/errors. Full production regression run51 passes **836/836 tests**, including 66 image-parity tests, with clean synchronization validation and unchanged inputs. The first run51 invocation stopped before launching tests because the terrain-column probe receipt pinned the earlier rectangle dependency; rebuilding that probe as build04 correctly binds it to the final host. No receipt was bypassed.

The mixed experiment is now named `ExperimentalVulkanMixedFixtureTest` and compiled only into diagnostic tests. Its pipeline, ABI and rejected emitter are under `test/mixed-parity`, outside the production renderer and shader lists. The ordinary runner records the exact excluded diagnostic by name and refuses unexpected expansion of that exclusion. The separate strict mixed runner remains available and remains unqualified; run51 is explicitly **not** a mixed-world parity pass. All pre-existing regression requirements remain enabled. Historical run50 and plane failures are retained unchanged.

The final source is left uncommitted for review. This is a tested implementation foundation, not completed Plan B or a deployed performance improvement. Remaining source-level limitations include fail-closed background worker errors, unmeasured catalog rebuild cost at graphical object mutations, and incomplete image leases/appearance hooks for live raw admission. The [performance-led rewrite boundary](vulkan-plan-b-retained-sprites.md#performance-led-rewrite-boundary--september-23-implementation-review) replaces further isolated fixture expansion as the next substantial milestone.

The final ordinary-path 4K check runs control56, candidate64, candidate64, control56, each for 100 warmup plus 3,000 measured ticks. All four match simulation census/checksum and observed 3840×2160/144 Hz. TPS is respectively 71.125, 74.524, 69.747, 71.533; CPU draw remains 25.806–26.664 ms. No substantial improvement is demonstrated. Full frame/GPU numbers and their limits are in the [checkpoint table](vulkan-plan-b-checkpoint-2026-09-23.md#final-4k-measurements). The consolidated receipt is `obj/vulkan-parity/plan-b-checkpoint-performance.json`.

The archived plane compiler also passes against build64. `plane-run-02` reproduces all 192 binary artifacts from the previously reviewed run01 exactly, including all 64 indexed/RGBA comparisons, with clean validation and unchanged inputs. Strict pixel qualification still fails as expected; relocation does not erase that result. Two optional headless-ceiling launches reject unsupported isolated-profile option placement before simulation; their logs are preserved, no ceiling is claimed, and no further launch touches the user's profile.
