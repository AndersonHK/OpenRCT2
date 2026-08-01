# Upstream manual-port manifest — 2026-08-01

## Review boundary

This document began as the frozen manual-integration review and now also records the implementation accepted for deployment.
The commit ledger remains the reviewable statement of upstream intent; the implementation record below identifies the
fork-native treatment actually applied.

- Fork branch: `develop` at `284b1e6c872d0eea4726cefc388347da52b89b03`.
- Fetched upstream: `upstream/develop` at `69872010ae6b0febcd1b9b6a53f49e54968ceecc`.
- Common ancestor: `a85b40b3efe12090652584257ffdca4a9ee52d49`.
- Divergence: 42 fork-only commits and 23 upstream-only commits.
- Aggregate upstream delta: 277 files, 681 insertions, and 605 deletions.
- Patch identity: `git cherry develop upstream/develop` reports all 23 commits as patch-inequivalent.
- Raw applicability: six patches apply textually (`c6193cc23e`, `d4df0267b4`, `72ed2a896c`, `5421e5812f`,
  `d92b18f978`, and `727d107be7`); the other 17 need manual conflict resolution. Textual applicability is not approval
  to cherry-pick.

The previous integration record remains in [Upstream merge manifest](upstream-merge-manifest.md). This review begins after its
ancestry-only merge through `a85b40b3ef` and does not reopen its recorded gameplay decisions.

## Fork contracts that govern every port

Fork behavior is canonical unless an item below explicitly proposes changing it. In particular, an upstream implementation
must not silently undo the fork's 40 TPS time model, pricing and ratings, transport routing, station staging, map topology,
spatial audio, private save fields, or Vulkan presentation ownership.

The target architecture is not hypothetical. The fork already has:

- one persistent synchronous compute `JobPool`, separate from the cancellable background-I/O worker;
- immutable `RecordedFramePacket` ownership and a newest-frame mailbox;
- a dedicated Vulkan render worker;
- three GPU frames in flight (`kFramesInFlight = 3`); and
- complete-frame rendering in which simulation does not wait for presentation fences.

New work should extend those boundaries. Simulation work may be split into read-only proposal jobs followed by one deterministic
commit phase. Render work must be recorded as owned commands and must not leave live simulation, widget, sprite, or render-target
pointers for the render worker. GPU work should not add a main-thread wait except at an existing explicit readback, resize,
shutdown, or debug boundary.

## Resolved design decisions

The user approved the first four decisions on 2026-08-01 and added the fifth while reviewing commit 19.

1. **Handyman task ownership (`50d91c4e5f`)** — Build a per-tick deterministic service-reservation system, not upstream's
   repeated entity scans. Proposal generation will use stable staff partitions and deterministic random inputs without mutating
   shared simulation state. Each worker writes to thread-local, spatially sharded proposal buffers. After a barrier, independent
   shard reducers choose the lowest stable entity ID for each exact service key, and one stable-order commit phase applies the
   winners. A normal healthy park therefore distributes work across many tiny reducers with almost no contention. The adversarial
   case of 100 newly hired handymen on one tile becomes one bounded linear reduction in one shard, not 100 global scans or a hot
   shared lock. The service key must distinguish mowing tiles, exact watering targets, bins, and litter entities, and must include
   claims already held by staff travelling to or performing work.
2. **Fork multiplayer identity (`50d91c4e5f`)** — Add a fork-specific protocol flavour to `kStreamID` and maintain a fork-owned
   protocol revision, starting with the upstream version-3 semantic change. A bare bump from 2 to 3 would still let a same-version
   upstream binary appear compatible despite the fork's materially different deterministic simulation.
3. **Plugin `context.gameSpeed` contract (`0fc88ab16c`)** — Expose the raw value accepted by `gamesetspeed` (1 is normal, 2–4
   are normal UI speeds, and 5–8 are debugging/turbo values when enabled) and correct upstream's inaccurate
   `0=normal … 4=hyper` declaration comment. The getter and action API must use the same numeric contract.
4. **Ride operations layout (`d4df0267b4`)** — Extract a small pure `RideOperationsLayout`/row builder used by both prepare and
   paint paths, then express the powered-launch block-section row through it. The extraction remains scoped to operations rows
   and preserves every fork-specific maze, transport, fare, waiting, and directed-leg control. Do not extract a helper that is
   only a two-line forwarding stub; a helper must own a real reused layout rule or eliminate meaningful duplicated state changes.
5. **Global AVX2 target (`8ed1ac4c57`)** — Do not follow upstream's runtime scalar/SSE4.1/AVX2 selection model on x86. Record
   paired deterministic benchmarks and retain the target while SIMD kernels benefit without a material aggregate regression;
   compile every first-party x86
   and x64 translation unit—including tools and tests—with AVX2 as the minimum ISA. Non-x86 targets retain their native ISA
   because AVX2 does not exist there. The AVX2 target must not imply fast-math or relaxed floating-point behavior, and supported
   x86/x64 systems will require AVX2 rather than receiving a scalar fallback binary.

## Implementation record

All 23 upstream-only commits were accounted for against `69872010ae6b0febcd1b9b6a53f49e54968ceecc`:

- Commits 7 and 11 are an intentional no-op because the latter fully reverts the former.
- Localisation commits 2, 9, 13, 20, and 21 use the final upstream strings by stable ID; fork-only English IDs remain intact.
- The powered-launch layout uses one `BlockSectionRowLayout` rule shared by prepare and draw paths.
- Handyman work uses a generational, sparse, lock-free reservation table. Existing claims are folded through `JobPool` with
  deterministic lowest-entity-ID resolution; new claims are constant-time in stable execution order. The table normally avoids
  both global entity scans and per-tick clearing, while a 100-handyman collision resolves to one key and one winner.
- Multiplayer stream identity is fork-specific (`andersonhk`) at protocol revision 3, and player-list UI state is refreshed by
  an intent only after an applied list change.
- Plugin bindings expose raw `gamesetspeed` values, ride/station metrics, and the shared `PathElement` bin-full predicate.
- Picked-up peep image, position, and zoom are one owned draw-state value; rendering uses a local target descriptor so the caller
  and asynchronous GPU command stream are not mutated after submission.
- Include pruning and drawing-header ownership were ported across upstream files and adapted for the fork-only GPU command,
  texture-cache, and Vulkan consumers. Fork types that genuinely require completeness retain direct includes.
- Every first-party x86/x64 MSBuild and CMake target now has AVX2 as its minimum ISA; Android x86 follows the same rule and
  non-x86 targets remain native. Floating-point precision settings are unchanged. The deterministic headless comparison used
  `EverythingPark.park`, 500 warm-up ticks, and 2,000 measured ticks. Pre-AVX2 runs were 622.762, 624.159, and 628.814 TPS;
  post-AVX2 runs were 618.276, 620.430, 614.539, 627.647, and 619.831 TPS. All runs produced checksum
  `4556ef4c316e0cb7000000000000000000000000`. The aggregate result is within roughly one percent and does not demonstrate a
  simulation-wide gain; AVX2 is retained as the explicitly requested repository contract and removes dispatch/fallback overhead
  from the mask and spatial-audio kernels. Revisit with profile-guided per-subsystem measurements if the aggregate regression
  grows beyond noise on other machines.
- Final local validation used the Release x64 MSVC/Vulkan configuration and completed with zero compiler warnings or errors.
  All 555 automated tests passed. Three additional `EverythingPark.park` runs after the generational reservation-table
  optimisation measured 633.133, 627.402, and 621.868 TPS; every run produced checksum
  `4556ef4c316e0cb7000000000000000000000000`. Interactive renderer, multiplayer, and UI checks remain part of the requested
  manual regression pass against the deployed build.

## Commit-by-commit ledger

Every entry below is independently reviewable. “Port” means reimplement the behavior against the fork; it does not mean
cherry-pick the source commit.

### 1. `c6193cc23e` — Fix clang64 Windows build (#26787)

- **Upstream content:** Removes the unused `_finished` member from the Windows `FileWatcher`; only Linux reads it. One header,
  one line.
- **Fork assessment:** The fork has the same platform split and Windows never reads `_finished`. No renderer or worker ownership
  is involved.
- **Proposed treatment:** Port exactly. Keep Linux's stop flag and Windows' handle-based shutdown unchanged.
- **Verification:** Build the clang64 Windows target and compile `FileWatcher.cpp` on Windows and Linux configurations.
- **Risk/status:** Low; pending review.

### 2. `3174ecf8ac` — Localisation integration

- **Upstream content:** Improves Hungarian load-fraction wording and adds ride-style/operations group labels `STR_7033..7038`
  to Hungarian and Korean.
- **Fork assessment:** The raw patch conflicts only because later already-integrated localisation changed surrounding text. The
  identifiers retain their upstream meanings in the fork.
- **Proposed treatment:** Take the final upstream Hungarian and Korean strings by stable ID, not by patch context.
- **Verification:** Localisation parser/format validation plus a duplicate-ID scan.
- **Risk/status:** Low; pending review.

### 3. `d4df0267b4` — Correct powered-launch block-section label placement (#26786)

- **Upstream content:** Places the block-section count below the launch-speed control for
  `poweredLaunchBlockSectioned`, while ordinary block-sectioned modes retain the existing row position.
- **Fork assessment:** The affected operations tab contains intentional fork controls and is a known spaghetti-code collision
  surface. The behavior is required and the raw patch happens to apply, but duplicated cursor arithmetic remains fragile.
- **Proposed treatment:** Preserve the exact visual intent through the scoped row-layout extraction in decision 4. Do not change
  effective operating settings, maze capacity, transport status, fare/service panels, or directed-leg ratings.
- **Verification:** UI layout tests/snapshots for ordinary block mode, powered-launch block mode, and non-block launch mode at
  minimum and expanded window sizes; interactive click/tooltip checks.
- **Risk/status:** Medium; layout design approved, pending individual implementation review.

### 4. `50d91c4e5f` — Prevent duplicate handyman service tasks (#26639)

- **Upstream content:** Before mowing, watering, emptying a bin, or sweeping, checks whether another handyman is already
  servicing the same target. Watering scans all staff because the worker stands on an adjacent tile; the other tasks use tile
  entity lists. It also advances the upstream network stream from 2 to 3 and adds contributor/changelog entries.
- **Fork assessment:** The gameplay fix is valid, but repeated global scans are the wrong long-term ownership boundary and do not
  prepare this subsystem for parallel entity updates. Selection order must remain deterministic in replays and multiplayer.
- **Proposed treatment:** Implement decision 1's thread-local, spatially sharded proposal/reduction pipeline. Prepare random
  inputs in the established deterministic order, produce candidates without shared mutation, reduce shards independently in the
  pool, and apply winners in stable entity-ID order. Preserve mowing-decoration bonuses and all fork staff behavior. Apply
  decision 2 to network compatibility.
- **Verification:** Focused tests for each of four task types, adjacent watering, multiple candidates, reservation release,
  stable winner under reversed enumeration, replay checksum, multiplayer checksum, and a high-staff benchmark.
- **Risk/status:** High but bounded; reservation and protocol designs approved, pending individual implementation review.

### 5. `72ed2a896c` — Ignore group boxes during hit testing (#26781)

- **Upstream content:** Treats `WidgetType::groupbox` as a non-interactive overlay in `FindWidgetFromPoint`, so it cannot mask a
  control declared earlier in the widget array.
- **Fork assessment:** The fork uses the same overlay convention, including heavily modified ride windows. This is an input-only
  fix and is independent of rendering backend.
- **Proposed treatment:** Port the early-continue form. If practical, centralise the predicate as `widget.isHitTestVisible()` so
  future decorative widgets do not duplicate policy.
- **Verification:** Unit test overlapping groupbox/control order, hidden widgets, empty widgets, and dropdown-button remapping.
- **Risk/status:** Low; pending review.

### 6. `a6d9cebff9` — Deterministic cut-away height formatting (#26774)

- **Upstream content:** Materialises fixed-point values and applies `llround` around float-derived metric and imperial display
  heights, fixing negative platform-dependent output.
- **Fork assessment:** The intent is correct, but no floating point is necessary. In formatter units the three formulas reduce
  exactly to `gClipHeight * 5 - 70`, `gClipHeight * 75 - 1050`, and `gClipHeight * 25 - 350`.
- **Proposed treatment:** Use explicit integer helpers for height-units, metric hundredths, and imperial tenths. This is shorter,
  deterministic across platforms, and avoids rounding-mode dependence. Retain the changelog entry.
- **Verification:** Table-driven tests across negative, zero, half-step, and positive clip heights for all measurement formats.
- **Risk/status:** Low; pending review.

### 7. `8e64399c50` — Attempted malformed `mapRequest` decoding fix (#26555)

- **Upstream content:** Removed a packet-unpack check and added/rewrote network tests, authentication expectations, and test
  serialisation.
- **Fork assessment:** Upstream explicitly reverted this commit three commits later. Comparing its parent to the revert across
  `NetworkPacket.cpp`, `NetworkTests.cpp`, and the test CMake file yields zero net change.
- **Proposed treatment:** Do not port code or tests. Preserve the history entry in this manifest so the commit is not mistaken
  for an omission.
- **Verification:** The paired zero-diff check is the acceptance evidence.
- **Risk/status:** None; recommended reject as superseded, pending review.

### 8. `02c42246fe` — Reject non-positive marketing durations (#26789)

- **Upstream content:** Rejects `_numWeeks <= 0` and adds a negative-duration action test.
- **Fork assessment:** Required. In this fork a valid input is converted from purchased weeks to seven-day duration during
  execution; the validation belongs before multiplication, price calculation, and the cast to the stored byte.
- **Proposed treatment:** Port the guard and expand the focused test to zero, negative, one, maximum valid, and 256. Preserve the
  fork's daily countdown semantics and current pricing.
- **Verification:** Action query/execute tests, daily campaign expiry test, and network action serialisation round trip.
- **Risk/status:** Low; pending review.

### 9. `9f6aa8f3ef` — Correct English vehicle-limit tooltip (#26790)

- **Upstream content:** Changes “31 trains per ride” to “255 trains per ride”.
- **Fork assessment:** Correct: `Limits::kMaxTrainsPerRide` is 255. The fork's later English IDs `7039..7067` are unrelated and
  must remain untouched.
- **Proposed treatment:** Update `STR_5811` by ID. Pair it with commit 13's translated corrections during the localisation work
  package while retaining separate ledger attribution.
- **Verification:** Assert the documented number equals the code limit through a small localisation/constant regression test if
  the test harness can access both cheaply; otherwise parser validation and source assertion.
- **Risk/status:** Low; pending review.

### 10. `b7ec766a62` — Refresh multiplayer player list on join/leave (#26610)

- **Upstream content:** Broadcasts a new refresh intent after server/client player-list changes, moves player-count refresh out
  of general prepare-draw, and adds a window refresh entry point plus changelog entry.
- **Fork assessment:** The event-driven direction is right. The upstream free function only updates `numListItems`; a fork-native
  owner should also clamp selection, refresh scroll geometry when needed, and invalidate the window once. Network processing
  must not reach into UI objects directly.
- **Proposed treatment:** Add a typed player-list-changed intent. Let `MultiplayerWindow` own `refreshPlayerList()` and all
  derived UI state. Emit only after an actual applied list change, on the UI/main-thread intent boundary. Keep headless servers
  free of UI work.
- **Verification:** Join, leave, invisible server player, multiple lists in one tick, future-tick list, closed window, open window,
  headless server, and selection-removal tests.
- **Risk/status:** Medium; pending review.

### 11. `e56c1de947` — Revert malformed `mapRequest` decoding fix (#26797)

- **Upstream content:** Reverts commit 7 (`8e64399c50`) in full.
- **Fork assessment:** The pair has zero net tree effect in the three touched files.
- **Proposed treatment:** No code change. Record both commits separately, then mark the pair satisfied by deliberate no-op.
- **Verification:** Same zero-diff evidence as commit 7.
- **Risk/status:** None; recommended accept revert/no-op, pending review.

### 12. `5421e5812f` — Prune transitive header dependencies (#26798)

- **Upstream content:** Forward-declares `BackgroundWorker` in `Context.h`, removes `Window.h` from `Input.h`, trims
  `DataSerialiserTraits.h`, and adds the concrete includes at consumers.
- **Fork assessment:** Directionally valuable. The fork's `Context` now directly owns `JobPool` and `BackgroundWorker`, so its
  complete-type requirements differ from upstream and must be checked rather than copied.
- **Proposed treatment:** Fold into the final include-boundary pass after functional ports. Apply “include what you use” against
  the fork's actual owning fields and inline destructors; do not force a forward declaration where a complete member type is
  required.
- **Verification:** Clean/PCH-disabled builds for MSVC, clang64, and CMake; no unity-build-only success.
- **Risk/status:** Medium build risk; pending review.

### 13. `1fa9a9955b` — Localised vehicle-limit tooltip corrections

- **Upstream content:** Changes `STR_5811` from 31 to 255 trains in 25 non-English language files and removes one trailing blank
  line in Italian.
- **Fork assessment:** All 25 non-English files are unchanged by fork-only commits after the common ancestor, so their final
  upstream blobs can be adopted exactly. English is handled by commit 9.
- **Proposed treatment:** Copy the final upstream non-English blobs for this frozen range, with per-ID review for the four other
  localisation commits.
- **Verification:** Blob equality for the 25 non-English files, parser/format validation, and encoding/newline checks.
- **Risk/status:** Low; pending review.

### 14. `9de2d43fb6` — Rename “passtrough/trough” identifiers to “passthrough/through” (#26800)

- **Upstream content:** Corrects `RideMode::poweredLaunchPasstrough`, its string ID, and
  `PROXIMITY_PATH_TROUGH_VERTICAL_LOOP` across 17 files without changing enum values or behavior.
- **Fork assessment:** The fork still has all 25 `Passtrough` and three path-`TROUGH` references, but many surrounding ride and
  rating blocks are intentionally rewritten. A raw multi-file patch is unsafe; a symbol-aware rename is straightforward.
- **Proposed treatment:** Rename every live fork reference in one atomic compileable change. Preserve serialized numeric values,
  rating score slots, ride mode ordering, save compatibility, and all fork-specific formulas.
- **Verification:** Zero old-symbol search results, compile-time underlying-value assertions, save import/export, ride-mode UI,
  vehicle station behavior, and rating checksum tests.
- **Risk/status:** Medium due breadth, semantically low; pending review.

### 15. `d92b18f978` — Initialise `pathTypeIsLegacy` in park-entrance placement (#26805)

- **Upstream content:** Adds the missing `_pathTypeIsLegacy(pathTypeIsLegacy)` constructor initialiser. Without it, the parameter
  is serialized/queried but the member keeps its default value, which can select the wrong path object and make the path appear
  invisible.
- **Fork assessment:** The bug is present verbatim and intersects recent fork park-entrance UI work, making a regression test
  especially important.
- **Proposed treatment:** Port exactly and add a constructor/serialisation/action test for both legacy and modern path types.
- **Verification:** Focused action test plus interactive placement for each path representation.
- **Risk/status:** Low; pending review.

### 16. `0fc88ab16c` — Plugin automation helpers (#26675)

- **Upstream content:** Adds `context.gameSpeed`; ride `reliability`, `guestCount`, `isEmpty`, `incomePerHour`, and `profit`;
  station `queueTime`; and footpath-bin `isAdditionFull`, with TypeScript declarations, contributor, and changelog updates.
- **Fork assessment:** The APIs are useful and the fork's real-time queue/income/profit semantics should be exposed as canonical.
  The fork already reports plugin API version 116, so no version rollback/bump to the commit's historical 115 is needed. The
  upstream game-speed documentation is inconsistent with the actual action values. `isAdditionFull` correctly mirrors the bin
  edge/status rule.
- **Proposed treatment:** Port all eight read-only properties, using decision 3 for game speed. Keep money values as the existing
  script money representation, document `guestCount` as guests currently counted on the ride, and reuse one shared bin-full
  predicate between staff logic and scripting rather than duplicating bit arithmetic.
- **Verification:** Script binding tests for valid and stale wrappers, all speed values, real-minute queue time, fork-correct
  income/profit, empty/non-empty rides, non-bin additions, queues, broken/ghost/full/partial bins, and declaration/runtime parity.
- **Risk/status:** Medium; API contract approved, pending individual implementation review.

### 17. `1b48206334` — Remove unused core includes (#26804)

- **Upstream content:** Removes 148 lines of transitive include baggage across 57 core, command-line, object, and importer files,
  adding 23 direct includes where required.
- **Fork assessment:** Ten files overlap fork-only code, including `Context`, `Game`, replay, compression, and file watching. The
  commit is mechanical upstream but not mechanically portable after the fork added job-pool, timing, routing, and renderer hooks.
- **Proposed treatment:** Re-run include analysis on the final fork tree; use upstream's removals as a checklist. Keep every fork
  dependency explicit and prefer forward declarations only at pointer/reference boundaries.
- **Verification:** Same clean/PCH-disabled build matrix as commit 12, plus command-line and importer targets.
- **Risk/status:** Medium build risk; pending review.

### 18. `df2a210fd8` — Rework drawing includes (#26815)

- **Upstream content:** Shrinks `Drawing.h`, moves concrete includes to 84 consumers, forward-declares `RenderTarget`, and removes
  the implicit `NewDrawing.h` include.
- **Fork assessment:** This is useful dependency hygiene but it predates and knows nothing about the fork's GPU command context,
  Vulkan backend, frame packets, presentation snapshots, and direct world-surface path. Twenty-one touched files overlap fork
  code. Blind application could restore software-renderer coupling or hide GPU dependencies behind another umbrella header.
- **Proposed treatment:** Define fork-native boundaries: common drawing value types, command-recording API, software raster API,
  and GPU/Vulkan implementation. Make each consumer include its owner directly. `Drawing.h` should remain a small compatibility
  façade during migration, not become the GPU API. No renderer behavior changes in this pass.
- **Verification:** Full renderer build matrix, include-cycle check, PCH-disabled builds, X8/OpenGL compile where still supported,
  Vulkan compile, and no change in visual hashes or command counts.
- **Risk/status:** High build breadth, low intended runtime change; pending review.

### 19. `8ed1ac4c57` — Move SSE4.1/AVX2 mask ownership to sprite drawing (#26818)

- **Upstream content:** Moves SIMD mask declarations and runtime dispatch from general `Drawing.*` to `Drawing.Sprite.*` and
  adjusts implementation includes.
- **Fork assessment:** Reject upstream's runtime fallback direction for x86/x64. The current fork enables `-mavx2` only for
  `AVX2Drawing.cpp`, `/arch:AVX2`/`-mavx2` only for `AudioMixer.AVX2.cpp`, and compiles the remaining translation units to a lower
  ISA. That prevents whole-program optimisation and auto-vectorisation from treating AVX2 as a repository-wide invariant.
  Software sprite-mask ownership should still be narrow, and Vulkan ordinary sprite submission must remain independent of the
  CPU masking implementation.
- **Proposed treatment:** First build an otherwise identical global-AVX2 benchmark configuration. If it passes the decision-5
  performance gate, replace per-file AVX2 flags with one first-party x86/x64 architecture target consumed by every library,
  executable, tool, and test under MSVC and CMake. Make AVX2 the compile-time x86/x64 path, remove runtime scalar/SSE selection
  from production x86/x64 code, and document the new minimum CPU requirement. Keep scalar or architecture-native code only for
  non-x86 builds and for isolated correctness tests. Do not enable `-ffast-math`, relaxed `/fp`, or implicit nondeterministic
  reductions. Third-party prebuilt dependencies are outside this compilation policy.
- **Verification:** Compare lower-baseline and global-AVX2 builds made with identical compiler, linker, LTO, and floating-point
  settings. Run repeated paired EverythingPark simulation, frame-command preparation, software sprite-mask, audio-mixing, and
  representative CLI/import workloads. Accept the global target only when its confidence interval or repeated median separates
  from run-to-run noise in at least one meaningful workload, no important workload materially regresses, and deterministic
  checksums remain identical. Then verify compile-command coverage for every first-party x86/x64 target, AVX2 X8 pixel parity,
  Vulkan independence from software masking, and packaging/README CPU requirements. Record the longer clean build time but do
  not treat compile time alone as a rejection criterion.
- **Risk/status:** High compatibility/build-policy impact; fork direction approved, benchmark gate pending.

### 20. `910d112919` — Catalan localisation update

- **Upstream content:** Corrects half/quarter-helix terminology and fills missing Catalan strings `7023..7038`, including ride
  group labels.
- **Fork assessment:** No fork-only Catalan changes exist after the common ancestor.
- **Proposed treatment:** Adopt the final Catalan blob exactly.
- **Verification:** Blob equality, parser validation, and a font/glyph smoke test for the changed punctuation.
- **Risk/status:** Low; pending review.

### 21. `727d107be7` — Spanish ride-group labels

- **Upstream content:** Adds Spanish `STR_7033..7038` translations.
- **Fork assessment:** No fork-only Spanish changes exist after the common ancestor; the raw patch also applies cleanly.
- **Proposed treatment:** Adopt the final Spanish blob exactly.
- **Verification:** Blob equality and localisation validation.
- **Risk/status:** Low; pending review.

### 22. `ac6030f011` — Magnify a picked-up peep at zoomed-in viewport scales (#26839)

- **Upstream content:** Captures main-viewport zoom when picking up guests/staff, scales the held sprite at zoom -1/-2, adjusts
  invalidation bounds, and temporarily mutates the render target's zoom and pitch while drawing.
- **Fork assessment:** The feature is desirable, but shared global overlay fields and in-place render-target mutation are poor
  fits for immutable frame recording. The current GPU command context already records zoom, and the Vulkan backend already has
  three frames in flight; the held-peep command must own its computed transform when the packet is sealed.
- **Proposed treatment:** Introduce a small `PickedUpPeepOverlay` value/state owner on the UI/input side. Capture image, position,
  and clamped scale once; derive bounds through one pure function; record a sprite command with explicit transform/zoom using a
  local render-target view or dedicated scaled-sprite API. Do not leave the worker a pointer to overlay state or mutate the
  caller's render target.
- **Verification:** Bounds and placement tests at zoom -2, -1, and 0; guest/staff pickup; move/cancel; resize; multiple viewports;
  X8/Vulkan indexed-canvas parity; packet lifetime test after overlay mutation; no main-thread GPU wait.
- **Risk/status:** High because it crosses legacy and GPU drawing paths; pending review.

### 23. `69872010ae` — Reorder/remove interface, entity, management, and network includes (#26847)

- **Upstream content:** Removes 180 include lines and adds/reorders 54 across 82 files. It is the final include-hygiene pass in
  this frozen range.
- **Fork assessment:** Twenty-one files overlap fork-only entity, UI, finance, marketing, and network logic. Applying it before
  functional ports would create churn and make semantic reviews harder.
- **Proposed treatment:** Use as the final checklist after commits 12, 17, 18, and 19 have established the fork's real ownership
  boundaries. Preserve direct includes required by staff reservations, plugin predicates, typed UI intents, protocol flavour,
  and GPU command recording.
- **Verification:** Full clean/PCH-disabled cross-toolchain build, tests, and an include-order formatting pass.
- **Risk/status:** Medium build breadth; pending review.

## Manual-port work packages and order

Each behavior-bearing package should be a separate commit and review stop. The ledger IDs remain in commit messages so every
upstream commit is traceable even when several mechanical include commits are implemented as one fork-native boundary change.

1. **Freeze and safety tests:** Re-fetch, require the frozen head or open a new manifest, run a clean baseline build/test, and add
   failing focused tests before each behavior fix where practical.
2. **Small correctness fixes:** 1 (`FileWatcher`), 5 (groupbox hit testing), 6 (integer height formatting), 8 (marketing), and
   15 (park-entrance path type). Review each separately.
3. **Ride terminology and layout:** 14 (atomic symbol rename), then 3 (operations row ownership). This reduces conflicts in the
   most heavily modified UI/ride files before other work touches headers.
4. **Deterministic staff/networking:** 4 and decision 2. Land the reservation data model, behavior, protocol identity/revision,
   and deterministic tests as one coherent multiplayer change.
5. **Event-driven multiplayer UI:** 10, after protocol/network tests establish the data-change boundary.
6. **Plugin API:** 16, reusing the shared bin predicate from the staff package and fork-canonical time/finance data.
7. **GPU-native held-peep overlay:** 22, with explicit packet ownership and visual parity gates.
8. **Localisation and metadata:** 2, 9, 13, 20, and 21. Copy all 25 non-English final blobs exactly; merge English by stable ID
   so fork strings `7039..7067` remain canonical. Add the upstream contributor/changelog entries with their owning features.
9. **Header, drawing, and ISA ownership:** Benchmark and, if justified, establish commit 19's repo-wide x86/x64 AVX2 target;
   then complete 12, 17, 18, and 23 as the final dependency-hygiene campaign. Preserve separate ledger evidence for every
   upstream source commit.
10. **Superseded pair:** Record 7 and 11 as an approved no-op; do not resurrect their reverted tests or packet behavior.
11. **Final validation and ancestry:** Re-fetch. If upstream is still `69872010ae`, update this ledger with actual port commit IDs
   and evidence, then create an ancestry-only merge of the reviewed upstream head. If upstream advanced, review the new range
   before any ancestry merge.

## Verification gates

### Per change

- Focused unit/action/UI/script tests exercising the reported bug or API.
- `git diff --check` and a zero-old-symbol/marker scan where applicable.
- A clean compile of every directly affected target, not merely an incremental PCH build.
- One commit and one user review stop per behavior-bearing ledger entry unless the user explicitly groups them.

### Full checkpoint

- Release x64 MSVC build with Vulkan enabled.
- clang64 Windows build, specifically covering `FileWatcher` and header hygiene.
- Clean CMake build with precompiled headers disabled; Linux compile in CI or an equivalent available environment.
- Paired lower-baseline/global-AVX2 build and workload results, followed by a compile-command audit proving that every
  first-party x86/x64 translation unit receives the global AVX2 target if the benchmark gate passes.
- Complete unit, play, network, scripting, importer, replay, map-presentation, GPU-foundation, and temporary-map suites.
- Deterministic repeated EverythingPark runs: identical final checksum, with simulation TPS and median tick time reported.
- Multiplayer server/client deterministic run covering handyman arbitration and player join/leave.
- Vulkan benchmark with VSync disabled, reporting command-preparation CPU time, GPU pass time, upload bytes, atlas misses, and
  presented/superseded packets. No regression may be hidden by aggregate FPS.
- Indexed-canvas visual parity for the held-peep overlay and interactive checks for powered-launch layout, groupbox hit testing,
  negative cut-away heights, park-entrance paths, multiplayer player refresh, and picked-up guests/staff at all three scales.
- Localisation parser, placeholder, duplicate-ID, encoding, and final-blob checks.

### Ancestry completion

After every approved semantic port is committed and the manifest contains actual evidence:

```powershell
git fetch --prune upstream
git rev-parse upstream/develop
git merge -s ours --no-ff 69872010ae6b0febcd1b9b6a53f49e54968ceecc -m "Merge upstream/develop through 69872010ae (manually integrated)"
git rev-list --left-right --count HEAD...upstream/develop
git status --short
```

The expected behind count is zero only if the fetched head is still `69872010ae`. The ancestry-only merge is the final receipt,
never a substitute for the preceding source ports, tests, reviews, and manifest updates.
