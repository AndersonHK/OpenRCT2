# Migrated-layer regression checkpoint

Latest implementation and qualification status: [native entities checkpoint](vulkan-native-entities-checkpoint.md). Last deployed baseline: [native supports and sign text checkpoint](vulkan-supports-text-checkpoint.md). The user has now authorized peeps, vehicles, balloons and remaining effects; the earlier separate-category deferral below is historical.

The preserved implementation checkpoint is `802f28341e`, deployed build112. It provides native static rides, portals, station pieces and expanded tracks, with two successful 4K/12,000-tick runs at358.9–359.8 TPS and143.9 CPU draw attempts per second (GPU timing samples closely matched those attempts). User testing on2026-09-24 accepted the progress but identified the issues below. This is not full renderer acceptance or measured display scanout.

The next checkpoint closes behavior within migrated families before adding unrelated world categories. Previously parked flat-ride bodies now need their operating poses; this request supersedes that temporary deferral. Peep/general world vehicle migration remains a separate category, but existing vehicle-window previews must work.

## Required fixes and evidence

- [ ] First-launch startup: locate and reduce cold pipeline cost, keep the window responsive, show meaningful preparation status and record stage timings. Validate a genuinely cold launch and a warm restart; do not hide cold time inside benchmark warmup.
- [ ] Missing tracks: inventory rejected style/type recipes, add missing supported geometry and station pieces, compare against pristine upstream at four rotations and zooms0/1.
- [ ] Path bridge/tunnel construction: restore provisional ghosts, direction arrows and placement/height overlays. Test placement, cancellation, direction/slope changes, terrain edits and camera changes without stale state.
- [ ] Flat-ride animation: publish compact authoritative poses once per snapshot, keep shared art resident and batch uploads. Verify start/stop, pause, breakdown, ride removal/reuse and old held snapshots. Do not substitute an independent animation clock for simulation-controlled movement.
- [ ] Existing animated objects: audit migrated scenery, doors, clocks, track effects and other already-present families against upstream; distinguish deliberately deferred entities from lost behavior.
- [ ] Pre-made ride and selected-vehicle previews: restore auxiliary/secondary viewport output through the shared Vulkan service and preserve isolation from main snapshot publication.
- [ ] Tunnels: native path/track tunnel requests must cut cliff apertures and render portal art; cover slopes, low clearance and stacked entrances.
- [ ] Underground ordering: fix the separately proven shallow-building and raised-corner ordering failures. The16-case corpus disproves both a blanket buried-height cutoff and a surface-ordinal-only fix.
- [ ] Actual underground/inside view: compare the dark terrain grid, buried paths/tracks, station/entrance buildings and tower sections against pristine upstream. Use the same18-case scene in normal and inside views at four rotations and zooms0/1. Include a contextual selected-ride preview; tunnel-mouth appearance alone cannot satisfy this gate.
- [ ] Have agents manually inspect every divergent sample group. Preserve exact comparison failures and document unresolved gaps; only the previously accepted outer-map skirt is waived.
- [ ] Run CPU/lifecycle regressions and serial4K,100-warmup +12,000-tick EverythingPark timing with four3000-tick windows, matching simulation checksum and final image. Record startup separately, CPU/GPU time, pacing and batched upload costs.
- [ ] Deploy the qualified candidate for manual construction/camera/preview testing, then checkpoint the implementation and remaining explicit deferrals.

References: [build112 results](vulkan-native-static-buildings-checkpoint.md), [underground audit](vulkan-underground-rendering-audit.md), [animation contract](vulkan-animation-state-contract.md), [track coverage](vulkan-native-track-coverage.md).

## Implementation progress (not acceptance)

The feedback checkpoint is `a994e785b5`. Build113 is retained as failed compiler evidence: a Windows environment-function macro collision in the benchmark hook and a signed/unsigned test expectation. No source changed during that build, and no113 game or GPU test was run. The installed112 checkpoint remains the last qualified deployment.

| Area | Candidate implementation | Remaining acceptance work |
| --- | --- | --- |
| Startup | Worker compilation, event pumping, preparation panel/logs and bounded pipeline cache; cold12.410s/warm0.063s measured, corruption/event-pump tests pass | Human cold-start check and timing after new shaders |
| Track recipes | Authoring13:5039 style/piece pairs,57 styles; station/shop/tower metadata and photo helpers restored;31 extractor tests pass | Photo manual review and final candidate corpus |
| Tunnel mouths | GPU requests, apertures and original portal art; station/shop fixed rectangles exact in all eight normal/inside views | Remaining interior filter composition; final corpus |
| Flat mechanisms |80-byte authoritative pose per ride, resident shared art, batched updates; progression/pause/reuse tests and three pose galleries reviewed | Photo helper review, final performance and manual gameplay |
| Construction overlays | Owned raw selection/arrow snapshot, shader-selected art/palettes; original-art capture and publication tests | User construction/camera exercise on qualified deployment |
| Previews | Isolated map generations, persistent recorder revisions, nonblocking fair auxiliary admission and status retirement; lifecycle/selected-vehicle GPU checks pass | Preview-open benchmark and contextual screenshot |
| Underground order | Original parent/bounds arrangement runs on GPU; station/shop/aperture corrections visually reviewed | Shared-cache qualification, ordered mixed filters and recovered pacing |

Preview review identified three additional regressions to close before deployment: repeated auxiliary contexts reused revision numbers and could suppress needed uploads; terrain completion status was not retired between requests; and asynchronous previews could occupy the service while another preview or a synchronous caller needed it. These require lifecycle fixes, not visual tolerances.

The ordering work keeps image selection and world arrangement on the GPU. It reuses the original independent paint-column boundaries and bounds relationships; it must preserve atomic child groups, support the current zoom range, reject capacity overflow without partial pictures, and meet the existing frame-time budget. No new divergence is accepted by choosing that implementation.

Build114 passed98 focused CPU tests. Its small GPU runs passed the backend lifecycle and immutable native auxiliary-generation tests with no validation diagnostics; two fixture assertions need correction before rerun (new initial-buffer copy counts and a synthetic animation image-directory index). Original-art captures remain exact comparison failures, with remaining underground ordering visible. These are not accepted divergences.

The first114 EverythingPark performance attempt aborted during warmup when the resident sprite upload batch exceeded the128MiB frame ring. It produced no performance result. The separately logged cold pipeline preparation was16.737s (native world16.632s), and a690,539-byte device-specific cache was saved. A pending fix stages oversized resident-art admissions in one bounded temporary buffer owned by the submitting frame's fence, while ordinary uploads retain the existing ring. Validation must cover discard, slot reuse, retained rendering without re-upload and the full park before deployment.

Build115 is retained as failed test compilation (`Ride::vehicles` is a C array; the new pose-import assertion must use `std::size`). Build116 compiled C++ without warnings but failed shader compilation because the new entity-snapping flag attempted a read/modify/write through a write-only output buffer. Neither build ran GPU tests. The correction passes the flag into record construction before its first store. These failed receipts are preserved rather than relabeled as successful builds.

## Build117 findings and next acceptance gates

Build117 compiled successfully. All95 focused CPU tests passed. Six of seven initial GPU tests passed; the surface fixture had stale buffer-copy and original-column clipping expectations, corrected for the next build. The separate operating-body zoom-snap GPU test passed. These runs reported no Vulkan validation diagnostics. The oversized atlas admission now survives discard/retry/slot reuse and the full park's135,189,404-byte initial batch.

The4K/12,000-tick run maintained359.958 TPS with the same final simulation checksum, but **failed the performance objective**:4800 CPU draw attempts in33.337255s concealed only3493 GPU frame samples, with9.539ms mean GPU frame time (approximately105 GPU frames/s). Build112 averaged2.36–2.49ms. Build117 is not a144FPS checkpoint and must not be deployed as qualified. CPU drawing remained0.167ms per attempt; the new GPU arrangement needs profiling and optimization.

- [x] Measure submissions, accepted presents and fence completions independently of CPU draw attempts; exclude late drain frames from the interval FPS gate. Accepted presents are not measured compositor scanout.
- [ ] Attribute the GPU regression with optional stage timestamps, then rerun clean qualification at least143 accepted presents/s while retaining360TPS gameplay pacing.
- [x] Correct authored-background ordering while depth preserves foreground UI. Repeated/mixed filters remain a separate open defect.
- [x] Correct station openings, shop structure, deep tower aperture and zoom1 portal sampling in the inside/control corpus. No new visual exception is granted.

The original18-scene normal/inside corpus now has144 views per mode. Agent inspection of all288 build117 strips found the isolated cinema exact in all eight views, and stacked portals exact at zoom0; station openings, shop structures and tower apertures still need work. The inside backing was incorrectly black because its terrain filter ran before the authored background clear. The next candidate changes that execution order and includes a focused filter/background/foreground GPU regression test. See the underground audit for exact sample hashes and residual counts.

Startup preparation in the117 cold application-cache benchmark took12.410s (12.313s world pipeline compilation); a699,724-byte pipeline cache was saved. Warm restart timing and visible responsiveness remain explicit acceptance gates. Current installed build112 is unchanged.

Build118's GPU run passes14/16 tests with no validation diagnostics, including authored-background filtering and original common-parent order. One path fixture lacked whole-tile clearance metadata for an elevated path; the other expected a particular winner among97 coincident synthetic path parents. Moving queue ink to the first parent did not establish that assumption:119 still failed that one assertion. The capacity/reuse fixture now places one queue above the96 others and samples its exclusively covered row, leaving ordering to the separate original-arranger comparison. Both corrected path capacity/reuse and common-parent ordering tests pass in120 with no validation diagnostics.

The118 instrumented12,000-tick run confirms104.686 accepted presents/s,3494 submitted/completed frames and1306 unavailable packets out of4800 paint attempts. Stage means are1.490ms materialization,0.766ms column counting,0.158ms prefix,6.322ms arrangement/emission,0.039ms finalization and0.382ms rasterization. No timestamp sample was lost. The next experiment schedules independent columns in32-lane groups with bounded transposed quadrant storage; all linked ordering within a column is unchanged. Performance improvement is not assumed until measured.

The same118 run seeds the exact117 application pipeline cache: preparation is0.063s, native world0.028s. This validates reuse across a fresh process with hash-pinned cache provenance. The runtime now separately logs startup rather than hiding it in measured simulation warmup. Actual window responsiveness is covered by the focused Windows event-pump tests; a human cold-start check remains part of deployment review.

### Verified substeps

- [x] Preserve deployed112 and immutable failed/successful build receipts.
- [x] Add the actual underground/inside oracle and matching normal control:18 scenes, four rotations, two zoom levels.
- [x] Inspect all288 underground117 specimen views and retain exact failures.
- [x] Correct authored-background/filter order; focused GPU test passes in118.
- [x] Validate oversized resident-art staging through abandon/retry/fence reuse and full-park startup.
- [x] Validate cold application-cache and warm restart pipeline timing separately from simulation performance.
- [x] Manually inspect all288 corrected mechanism117 body samples;144 zoom1 body crops are RGB-exact without masks/tolerances. These sampled poses do not prove every possible animation state.
- [x] Distinguish actual submissions/accepted presents/completions from CPU paint attempts, and identify the GPU ordering bottleneck with stage timestamps.
- [x] Inspect119 station/shop/tower aperture and exact sprite-sampling corrections against the corpus; retain separate interior filter failures below.
- [ ] Recover at least143 accepted presents/s in the4K/12,000-tick workload with stable accepted-present intervals.
- [ ] Qualify the preview-open workload and user-facing construction/preview behavior before deployment.

### Builds119–120: correctness gains, rejected performance experiment

Build119 passes100 focused CPU checks and9/10 GPU checks (the path fixture described above was the only failure). All288 underground specimen groups were inspected: stations6/7 and shop10 are now indexed-exact at every tested camera in both normal and inside view. Tower apertures are restored; their remaining peripheral cliff strips project to the outer-map plane and belong to the previously accepted skirt. All648 mechanism/neighbour comparison strips and all48 full PNG/indexed outputs are byte-identical to117, preserving the corrected mechanism poses and sampling.

The32-lane arrangement experiment is rejected: its12,000-tick run retains359.951 TPS but only1009 accepted frames,33.123ms mean GPU time and41.8ms p99 accepted-present interval. Stage arrangement grows to27.824ms. The four-lane120 experiment also fails: its3000-tick diagnostic records42.820 accepted presents/s,16.543ms arrangement,28.8ms p99 accepted-present interval and30.182ms maximum. These measurements reject both configurations; lower occupancy and dependent memory access remain explanations to investigate, not hardware-counter findings. Build120's29 CPU and two GPU checks pass. Neither build is a qualified deployment; the3000-tick diagnostic does not substitute for the12,000-tick acceptance run.

The inside-view comparison also exposed a separate compositor defect: overlapping Darken1 terrain filters all read one unchanged background, so only one darkening survives. The remaining clean slope pixels become exact after another Darken1 lookup. Water, coloured glass and Dodgems roof filters use different operations and cannot generally commute. Ordered GPU filter composition is therefore required; a terrain-only brightness adjustment or waived pixel tolerance is not acceptable.

The migrated-animation audit found one additional real omission: track photo platforms were admitted while their camera/flash helper was discarded during authoring. The120 candidate expands a compact procedural photo marker on the GPU using the already captured timeout. Authoring13 supports5039 style/piece pairs across57 styles;31 extractor tests and the focused native CPU tests pass. A separate original-art photo corpus and visual qualification are pending. No independent animation clock or new per-object upload stream is introduced.

### Next candidate: exact ordering and bounded filter composition

- [x] Capture and manually inspect the original upstream photo corpus: normal/small camera families, timeout0/1/3, ordinary/ghost states, four rotations and two zooms. All96 fixed specimen rectangles match exactly (8,908,800 indexed pixels); native120 imports stored timeout values without patching them and has no validation diagnostics. Full-frame failures retain the documented exterior skirt. See [photo review](vulkan-onride-photo-visual-review.md).
- [ ] Validate the shared parent-node cache at511/512/513 nodes against original CPU arrangement at every rotation. Columns larger than512 retain complete ordering; no truncation is permitted.
- [ ] Validate ordered mixed palette/literal filters, opaque occlusion, viewport clipping, and bounded overflow on the GPU. Repeated underground darkening must compose correctly alongside water and glass.
- [x] Repeat125 underground/normal captures and manually inspect all288 groups. All349 identified interior filter pixels are corrected, no previously correct pixel regresses in any full inside image, and all144 normal-control strips are unchanged. Outer-map skirt remains the only accepted visual divergence; missing physical supports remain an explicitly deferred layer. Later shader/recipe changes still require comparison.
- [x] Capture actual underground main-window rendering with a contextual ordinary secondary preview in131. View flags, camera and unchanged simulation checksum are recorded; exact original-art correctness remains open.
- [ ] Measure the new compositor's cost, then qualify at4K/12,000 ticks with360TPS pacing and at least143 accepted presents/s. Record accepted-present interval percentiles and maximum separately from CPU attempt pacing.
- [ ] Commit the coherent corrected source and deploy only a candidate that passes the performance and correctness gates. Deployed112 remains the manual-test fallback.

The proposed filter compositor uses GPU fragment lists and ordered palette application, with no host draw list or per-object calls. Its bounded node pool and per-pixel limit must report overflow. Existing asynchronous error retirement does not prove an overflowing frame can never reach presentation before its fence is inspected; do not describe this as a guaranteed pre-presentation rejection. Normal-case tests and full-park runs must show no overflow.

Build121 compiled C++ without warnings but failed GLSL compilation because `filter` is a reserved word. It ran no GPU tests. Renaming that local variable to `isFilter` produces the successful122 receipt with no source mutations during compilation. SPIR-V storage audits pin both new compositor shaders and the shared column cache; the compositor shaders contain no private arrays. Runtime qualification follows separately.

A fresh track coverage audit also found ordinary omissions outside the previously sampled special pieces: Wooden curves/banks/helixes, Junior/Water sloped and eighth turns, several station families, and inverted slopes. Some whole recipes are discarded because support-only conditions are not understood by the offline authoring interpreter. These are active missing-rail defects, not waived support omissions; broader extraction and original-art fixtures are being added before calling track coverage complete.

Build122 passes six actual GPU regressions without validation diagnostics: mixed ordered filters, per-pixel/global limits, original child order, selected-vehicle terrain order, and511/512/513-parent exact comparison. Its shared-cache performance experiment still fails (3000 ticks:17.424ms arrangement,2.996ms counting). It is removed from the candidate. The three-slot queued filter/abandon/retry test also passes in123; this verifies compositor lifetime independently of the ordering optimization.

The attempted123 rollback comparison is invalid: copying the old shader preserved its old file timestamp, so MSBuild reused122's SPIR-V despite recording restored source hashes. Build124's first forced-build change still skipped an empty-output target. Neither receipt may support a source/binary-equivalence or restored-baseline performance claim. Build125 forces all shader compilation, requires an explicit successful-compilation marker, and verifies that the restored column SPIR-V hash equals118. Prior artifacts remain immutable and their limitations are recorded here. Qualification builds now rebuild shaders regardless of restored timestamps; normal incremental builds retain their usual behavior.

The verified1253000-tick profile records73.407 accepted presents/s,19.5ms p99 accepted-present interval and22.102ms maximum. Lifetime GPU stages are2.581ms materialization,1.572ms counting,7.200ms arrangement and1.127ms raster/composition. The shader rollback improves on the failed cache but remains below the deployment objective. This is a diagnostic, not the12,000-tick final gate.

Candidate126 replaces repeated sparse bounds lookups with range-checked compact bounds beside each GPU node, retaining full32-bit fallback and exact ordering. It passes the511/512/513-parent oracle, signed-boundary fallback oracle, and three-slot queued/abandoned compositor test with optional profiling enabled and no validation diagnostics. Its extra12MiB GPU workspace is a performance tradeoff to measure; no improvement is presumed. The optional32-byte per-frame diagnostic readback is confined to profiling and retired through existing fences, never enabled in clean acceptance runs.

## Physical depth replacement (owner-directed checkpoint)

Build128 successfully compiles the corrected startup, previews, animation, tunnel/filter work and authoring16 track data (5388 style/piece pairs). Its expanded64-piece original-art fixture also builds successfully. Captures remain in progress; this is a source checkpoint, not a qualified deployment. Installed112 is unchanged.

The126 compact-bounds experiment reaches approximately119 accepted presents/s in the3000-tick diagnostic, still below the144FPS objective. Parent-only shader optimization127 does not materially improve total throughput and has been reverted. Those observations reject further investment in rebuilding painter priorities every frame.

The next implementation removes the legacy column arranger from the production GPU path. Physical component depth derives from retained world coordinates, authored geometry and explicit coplanar layers. Hardware depth testing resolves opaque visibility; palette filters use that same per-fragment depth. Global camera projection changes need no object-state republishing. Panning must not change component depth. Image-only animation must not force an ordering rebuild.

- [x] Preserve the last compiled correctness candidate and immutable benchmark evidence before replacing ordering.
- [ ] Remove column counting, prefixing, arrangement, duplicate ordered buffers and their temporary metadata from the world pipeline.
- [ ] Run a physical-depth prototype with no legacy arrangement fallback; report its image defects explicitly.
- [ ] Model ground, upright art and finite-height components with appropriate depth geometry; preserve component-local coplanar layers and foreground UI.
- [ ] Compare normal, underground, track/ghost/water and animation samples against pristine upstream; have an agent inspect divergences.
- [ ] Separate persistent component topology from camera visibility and pixel-only animation; only graphical changes update retained facts.
- [ ] Re-measure at4K for at least3000 ticks, then qualify12000 ticks with360TPS and stable approximately144 accepted presents/s.

The physical-depth prototype is allowed to expose incomplete geometry while validating cost. Such failures are not new visual exceptions and cannot be called parity. The earlier legacy-arranger optimization checklist is superseded by this replacement.

## Current physical-depth implementation

The corrected-layer source checkpoint is `50f16fb7e5`. The main native world path now bypasses and deletes the legacy GPU parent-column arranger. Raw world state feeds physical depth geometry directly; the opaque z-buffer and per-pixel filter compositor share that depth. Approximately147.9MiB of redundant ordering buffers are removed per pipeline. See [physical depth checkpoint](vulkan-physical-depth-checkpoint.md) for measured controls and remaining geometry work.

- [x] Remove all four parent-column ordering dispatches, ordering shader and redundant metadata/workspace/ordered-copy buffers.
- [x] Use world-derived vertex depth and hardware occlusion, with explicit coplanar surface overlays and component-local layers.
- [x] Preserve foreground UI and verify camera panning with identical shifted pixels and no object-buffer re-upload.
- [x] Give cliff strips their actual tile-boundary vertical planes.
- [x] Restore selected-car direct GPU emission outside the deleted arranger; retain actual XYZ in the immutable packet and verify depth, zoom, pan, empty tiles and removal.
- [x] Run a matched3000-tick4K control:12878.441FPS/12.766ms GPU;129143.928FPS/3.169ms GPU; both359.82TPS and identical checksum. These are incomplete-image architecture measurements.
- [x] Manually inspect all1536 expanded track128 specimen groups and all288 normal/inside129 groups. Retain exact comparison failures.
- [x] Inspect all288 normal/underground131 groups after cliff/overlay corrections; retain the residual failures.
- [x] Complete expanded131 track review: all1536 groups accounted; station intersections and72 water-splash views remain failures.
- [x] Implement exact planar terrain depth and the corresponding sloped path deck plane; verify all slope/direction coordinate combinations.
- [x] Inspect final135 path correction: all16 changed ramp views improve;272 other normal/inside groups remain byte-identical132.
- [ ] Repair nonplanar terrain, sloped track, partially buried building geometry and track-water/station intersections; path railing/filter differences remain open.
- [ ] Replace approximate recipe depth-role inference, finite coplanar bias and operation-based filter ties with authored geometry/layer contracts where images expose errors.
- [ ] Complete persistent component topology; current raw state is retained, but GPU materialization still runs each frame.
- [ ] Close long-frame attribution and repeat clean12000-tick pacing checks; both131 runs exceed143 accepted presents/s but contain62.747ms and65.521ms presentation gaps; opt-in wait attribution is next.
- [ ] Qualify contextual underground main view plus ordinary selected-vehicle preview, original-art parity and user interaction before deployment.

Installed112 remains unchanged. The physical-depth candidate is an intentional architecture checkpoint with documented visual defects, not a claim of completed migration or a qualified deployment.

The133 opt-in12,000-tick trace reaches143.976 accepted presents/s and identifies roughly5ms waits after vehicle batch work is complete. It does not reproduce the earlier60ms stalls; their gate remains open. Diagnostic state is disabled in ordinary gameplay and clean qualification. Detailed evidence: [simulation wait attribution](vulkan-simulation-wait-attribution.md).

Final clean135:359.956TPS /143.743 accepted presents/s at4K for12000 ticks;0.166ms CPU draw /2.924ms GPU. All4792 frames accepted, same final checksum. P99 is9.2ms but maximum57.097ms, with a53.005ms simulation stall. Average throughput passes; stable-pacing and complete-art gates do not. Build135 passes22 focused CPU/GPU checks and19 Python parser tests, without Vulkan validation diagnostics. Source may be checkpointed with those explicit limitations; deployment remains unqualified.

An exact-build135 diagnostic repeat isolates12.114ms after vehicle group completion within a14.618ms simulation tick. This strengthens the completion/wakeup attribution; it does not close the clean53ms stall. See the wait-attribution document before proposing scheduler changes.

## Current manual-test installation

Owner-requested deployment on2026-09-24 installed build135/checkpoint `6e588a449b` at `D:/Games/Independent/OpenRCT2Mod` to investigate the known depth-geometry defects. All28 payload files were hash-verified;7 changed files have preserved backups. Receipt: `obj/vulkan-parity/deploy-physical-depth135-01/receipt.json`. This supersedes earlier installed112 status, but does not mark visual parity or pacing gates complete. No saves, objects or settings were changed.

## Owner correction: constant depth per sprite

The deployed135 close-up exposes sprite slicing caused by gradients across individual sprites. The current correction deletes terrain/billboard depth planes. Each authored sprite component receives a single constant depth; separate sprites supply separate depth units. The earlier mesh/finite-plane work items are superseded. Remaining anchor/layer errors must be fixed within this atomic-sprite model, without per-pixel gradients or rebuilding painter order. New implementation, close-up comparisons and4K performance validation are tracked in [constant-component depth](vulkan-constant-component-depth.md).

### Current checkpoint: constant component depth, build137

- [x] Delete per-sprite depth gradients and depth-role/terrain-plane inference. Each complete sprite has one depth, including its filter fragments.
- [x] Verify whole-sprite overlap in an actual GPU regression at two zooms, two camera positions and both near/far poses. Final137 selected-car and filter checks also pass without validation diagnostics.
- [x] Separate tunnel back/front depth anchors from their shared raster position; inspect all288 normal/underground groups against original art.
- [x] Preserve explicit Cinema/Carousel body anchors. Cinema improves across all captured rotations/zooms; Carousel's same source-derived change still needs independent visual coverage.
- [x] Deploy137 for the owner's manual testing: all28 files verified,3 replaced with backups, receipt `obj/vulkan-parity/deploy-constant-depth137-01/receipt.json`.
- [x] Record underground skirts as an owner-accepted divergence in addition to the previously accepted exterior skirt. Exact reports remain unmasked.
- [ ] Fix ordinary station/track-water ordering, partial shop coverage, underground grid/filter relationships and tower apertures. These remain failures, not accepted divergences.
- [ ] Replace finite local-layer bias that can cross neighbouring object depths; qualify independent equal-depth ties.
- [x] Run a clean4K/12000-tick137 check after the manual session:359.717TPS,143.977accepted presents/s,0.172msCPU draw,2.491msGPU; maximum presentation gap15.532ms. The earlier57ms stall did not recur; its cause remains open.
- [ ] Complete persistent component topology; GPU materialization still reconstructs components per frame from retained raw state.

Pathological partially buried Cinema is now explicitly **low priority** at the owner's request; it remains a known mismatch rather than an exception. Original-art parity, other migrated-layer interactions and the intermittent long simulation/presentation stall remain open. See [the native-resolution visual review](vulkan-constant-component-depth-review.md).


### Next candidate: supports, footprint anchors and sign text

Everything Park is the primary visual and performance sample at the owner's request. Inspect zoom0 closeups of the flat-ride row, queue signs, entrance styles and elevated paths/tracks. Do not substitute a downscaled full-park image for inspection of component order. Retain exact differences and separate unmigrated entities from regressions in migrated objects.

- [x] Author camera-relative nearest-footprint tile anchors for whole multi-tile ride bodies; keep floors/fences tile-local and depth constant over each sprite.
- [x] Separate entrance rear/front pane ownership and queue-banner pole anchors from common raster offsets.
- [x] Author native path box/pole and metalA/B support generators, immutable track support programs and shared nine-segment terrain/element state. These are implementation milestones; visual acceptance is pending.
- [x] Publish immutable banner/queue glyph columns only when text/assets change. Shader chooses scroll phase from the snapshot tick; no per-frame bitmap uploads.
- [x] Pass original metal-support oracle, sprite-font column/lifecycle tests and actual-GPU tick-only scrolling checks (139:60/61, utility-mask correction141:8/8; no Vulkan validation diagnostics). The production catalog v2 admission regression is covered explicitly.
- [ ] Inspect Everything Park zoom0 closeups against pristine upstream, including all camera rotations where anchors differ; fix any new support/deck intersections.
- [x] Implement streamed wooden A/B support rules and immutable authoring programs, including named rail ownership for steep-transition caps. Authoring passes58 Python checks; C++/GPU/visual qualification is pending.
- [ ] Complete station-specific supports, the six rejected authoring programs, generic helix helpers and static-ride support-state effects. General wooden/metal/path support implementation does not close these gaps.
- [ ] Extend shared scrolling text to wall/large scenery signs and entrance/park signs with their own original positioning semantics.
- [ ] Qualify TrueType text/hinting/font reload as well as sprite-font text; fix invalidation in the public hinting toggle.
- [ ] Re-run4K/12000tick performance and pacing with the new layers. Build137 numbers do not qualify this candidate.
- [ ] Deploy and preserve a commented checkpoint with the remaining failures.

### Everything Park close-up findings, build141

The four rotation3 zoom0 paired captures are in `obj/vulkan-parity/everything141-closeups-01`. Each image is1024x768 per renderer (upstream left, native right), with original assets and identical saved state/camera. Exact reports are unmasked:58,143 differing pixels in flats,106,022 in queues,167,943 in paths,41,320 in entrances. Missing guests/vehicles contribute to these totals; they do not excuse regressions in already migrated objects.

Root and an agent inspected these images directly. Metal supports and queue/banner text are now visible, and Cinema/Haunted House bodies no longer collapse behind their floors. New support/rail overlaps and pale checkerboard path surfaces remain failures. Wooden lattice is still absent in this binary; its source integration follows141. Platform fencing requires its own named physical-edge anchor, separate from both the floor raster offset and whole-body footprint anchor. The entrance-area camera does not contain the futuristic glass entrance, so it cannot qualify that specific fix.

The owner explicitly selected Everything Park over personal saves for this work. Future samples should use closer crops and additional cameras/rotations of this same park. Do not mark overall parity from a full-map thumbnail or from CPU rule tests alone.

Build141 clean4K/12000-tick control (`performance-support-text141-12000-01`):359.958TPS,143.743accepted presents/s,0.153msCPU draw,3.192msGPU, with all4792submitted frames accepted and the same final simulation checksum as137. GPU cost rises0.701ms from137 as support/text geometry returns. P99accepted interval remains9.2ms; the maximum is33.135ms alongside a30.642ms simulation tick. Average throughput passes; intermittent pacing is not fixed. Pipeline preparation took14.519s with a137 cache seed, outside measured ticks. This is still partial-world performance, not full migration acceptance.

The exact glass fixture is Everything Park ride360 (Mini Suspended Hoverboards), station object `rct2.station.abstract`: entrance tile94,117 and exit94,116. Camera3024,3744,336 at zoom0 targets both. Its adjacent queue banner is tile93,119. This is decoded from the saved park, not guessed from the screen; the source facts are retained in `obj/vulkan-parity/everything-glass-locator/decoded.json`.

Build142 passes69 focused tests (including wooden oracle and new fence/path/flat-contact contracts) with zero validation diagnostics. Six zoom0 rotation3 Everything Park views were captured in `everything142-closeups-01`; initial paired differences are55,987flats,106,022queues,84,180paths,36,236entrances,83,607glass and56,893wooden-tree. The large wooden coaster lattice is restored, and the near platform fences improve visibly. The path checker pattern is unchanged: the source-proven under-deck support-depth correction did **not** fix that appearance and must not be credited as doing so. The flat view's missing silver transport deck is a separate track omission, not a checkerboard-path example.

The glass capture distinguishes two issues: the small entrance/exit booth frames and panes look coherent in rotation3, but station shelter front/top panels are covered by their own platform/structure, and entrance text is blank. Candidate143 adds named front-eave anchors (roof and glass share one scalar, unchanged raster) and ride-entrance text from the existing immutable queue-name/closed columns. Other rotations and actual images remain required before accepting these changes. Curved/cross-element metal support intersections remain open; the qualified LoopingRC straight-flat contact anchor does not claim to fix them.

**Reference correction:** The141/142 upstream closeup profiles omitted `general.rct1_path`. An environment/platform path alone does not enable `GfxLoadCsg`. Native explicitly loaded CSG; upstream used fallback images. The checker tile is `rct1aa.footpath_surface.tiles_grey`, whose normal images are CSG43881..43931 and whose no-CSG fallback is TARMAC/ROAD. Thus the checker tiles are correct native artwork, not a path regression. The numerical closeup differences above are retained as diagnostic history but **do not qualify equal-asset pixel parity**. Re-run with explicit RCT1/RCT2 configuration for both renderers. Support/rail and station-shelter overlap findings remain visually concrete; the path support/deck scalar constraint is a separate source-derived fix and did not change the checker art.

Build142 clean4K/12000-tick control:359.959TPS,143.774accepted presents/s,0.141msCPU draw,3.274msGPU. All4793frames accepted; simulation checksum unchanged. Adding the wooden support layer costs another0.082msGPU relative to141. P99accepted interval9.1ms, maximum36.558ms alongside34.304ms simulation; the intermittent wait/pacing gate remains open. Pipeline preparation14.279s is outside measurement. Receipt: `performance-support-text142-12000-01/summary.json`.

Build143 compiles without warnings/errors and passes71 focused tests without Vulkan validation diagnostics. Matched-art captures now explicitly seed identical RCT1/RCT2 configuration and hash CSG/G1 inputs. Native loaded-CSG state is observed in its capture report; pristine upstream remains a path-plumbed binary without an `IsCsgLoaded` diagnostic, so the receipt states that limitation instead of fabricating a runtime observation.

Build 144 final qualification and deployment: the four enlarged glass-station comparisons confirm the r1/r2 roof-over-entrance regression is resolved. Roof, text and pane components retain constant scalar depths. Path/support gallery images use matched RCT1 art; blank front-row signs remain unclassified and must not be dismissed as deferred families. Detailed evidence and checklist: `vulkan-supports-text-checkpoint.md`.

The final 4K, 12,000-tick run achieves 359.960 TPS and 143.774 accepted presents/s, 0.151 ms CPU draw and 3.271 ms GPU frame. All 4,793 submissions were accepted; P99 accepted interval is 9.1 ms, maximum 24.101 ms, with a longest simulation batch of 22.196 ms. Startup pipeline preparation is 15.324 s outside measurement. Hidden presentation is not scanout qualification, and neither pacing nor full-render parity is closed. Build 144 is deployed to OpenRCT2Mod with 28 files verified and 5 backed-up replacements; deployment receipt `obj/vulkan-parity/deploy-supports-text144-01/receipt.json` has SHA256 `dc7c0201f09e7512d218589f2f895d2ac9145913c06d10d8a9347c67b9099710`. Saves, profiles and objects were untouched.
