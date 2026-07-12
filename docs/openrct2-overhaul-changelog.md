# OpenRCT2 overhaul changelog

This is the chronological engineering record for the personal fork. It records behavior changes, compatibility decisions,
important implementation corrections, and the evidence available at each checkpoint. The concise player-facing overview lives
in the [README](../readme.md); exact mechanics, architecture, benchmark methods, and build procedures live in the linked topic
documents. The upstream project's release history remains in [distribution/changelog.txt](../distribution/changelog.txt).

## 2026-07-12

### Pitched isometric acoustic camera and direct-path depth

The acoustic listener now occupies a canonical 3D pose on the null ray of the legacy orthographic isometric projection. The viewed terrain focus is camera-forward rather than world-down, so screen-centre and upper/lower-centre sources remain semantically in front while elevated track on the same view ray becomes physically closer and louder. All four rotations derive forward, right, and up from the renderer's exact projection basis.

Doppler now separates physical emitter velocity from listener velocity. Coaster, kart, and attached rider motion receives the full bounded source contribution, while camera movement supplies the same reduced 5% term to vehicles, ride music, and static positional effects. Loads, teleports, rotations, long gaps, and closest-car anchor changes reset motion instead of creating pitch impulses.

Positional source calibration and distance remain separate floating-point gains through the mixer, preserving near-field ordering above unity until the existing final limiter. A stateful interpolated low-pass now removes high-frequency detail continuously with 3D distance; vehicle detail responds slightly faster, amplified music more gently, and terrain occlusion lowers the cutoff as well as the direct level. Reverb remains deferred as the second-slice stretch goal.

### Independent 144 FPS deadline and 360 TPS Turbo definition

Offline Turbo now performs nine logical updates per 40 Hz batch, making 360 TPS an actual scheduler target rather than an
unreachable request above the former eight-update ceiling. Network play retains its established eight-update cadence. Turbo
sleeps until the earlier of its simulation and presentation deadlines; the old simulation-only sleep crossed the 144 Hz frame
deadline and produced an artificial 132 FPS plateau despite sub-millisecond rendering.

Diamond Heights now sustains 144.021 FPS and 354.877 TPS over 3,600 measured logical ticks. EverythingPark sustains 144.035 FPS
and 248.336 TPS. Both preserve deterministic fixed-run checksums. The latter remains simulation-bound: 3.193 ms per logical tick
already exceeds the 2.778 ms budget before its 1.422 ms CPU paint traversal is included, while Vulkan itself averages 0.101 ms.

### Retained Vulkan damage canvas

Vulkan retains the pre-weather indexed scene in device-local memory and rebuilds only acknowledged dirty regions. Damage is
retired only after presentation, so newest-frame replacement cannot discard an invalidation. Sparse scenes retain their prior
canvas; dense scenes cross over to one full traversal instead of repeatedly entering the viewport painter for fragmented damage.
Focused tests cover dropped packets, old acknowledgements, dense crossover, partial-damage readback, and retained pixels outside
the changed region.

The shared path route-field builder now records reverse-edge source directions during breadth-first traversal, removing the
second full node-and-edge scan while preserving deterministic lowest-direction ties.

## 2026-07-11

### Deterministic 360 TPS / 144 FPS audit

The combined target is not yet reached. A sustained 30-second EverythingPark Vulkan/VSync run produced 238.961 TPS and
143.923 FPS; the current scheduler also requests only eight logical updates per 40 Hz scene batch, capping ordinary Turbo at
320 TPS even on an unlimited CPU. A clean headless 2,000-tick run reached 551.972 TPS, while integrated simulation consumed
about 78% of wall time and GPU execution remained roughly 0.13-0.35 ms per frame. The limiting work is serialized simulation
and caller-side paint preparation on the main thread, not Vulkan execution.

The integrated harness now accepts `--benchmark-warmup-ticks` and `--benchmark-ticks`. Both are constrained to complete Turbo
batches, so comparisons start and finish at identical simulation states. Two 2,000-warm-up/2,000-measurement validation runs
produced the same `c241cc46...` checksum and exact population snapshots, exposing substantial ambient machine variance at
232.546 and 201.994 TPS instead of disguising it as a code delta. The 360 TPS goal requires a 2.778 ms mean simulation budget,
an immutable visual snapshot/parallel paint boundary, and further deterministic guest/vehicle hot-path work before Turbo is
raised from eight to nine updates per batch.

### Retained Vulkan scene foundation

A measured graphics profile confirms that CPU scene reconstruction, rather than Vulkan execution, is the principal integrated
cost. Over 512 fixed logical ticks, viewport-column construction accumulated 18.076 seconds of worker CPU time, tile paint
setup 6.223 seconds, and entity paint setup 4.456 seconds; whole GPU frames remained near 0.124 ms. The more-than-twofold
headless throughput gap is therefore treated as the governing signal, while differences below ten percent are not used to
justify isolated hot-path edits.

Ordinary opaque sprites now use compact per-instance commands and persistent GPU asset descriptors. Atlas layer and origin are
uploaded once with the resident sprite allocation instead of repeated in every ordinary draw command, establishing the
indirection needed for persistent scene instances. The entity registry separately owns generation-safe visual identities and a
deduplicated final-state worklist. Creation, authoritative movement, removal, immediate ID reuse, and whole-park reset publish
pointer-free records; presentation-only tween movement does not. This is the first boundary for a latest-only visual worker,
not yet the final retained world renderer: tile and entity records still need one shared GPU ordering path before the repeated
CPU painter traversal can be removed safely.

The Release validation build passes all 525 tests, including a real Vulkan upload/retained-descriptor/readback sequence. A
fixed 2,000-tick EverythingPark row retained checksum `c241cc46...`, sustained 144.055 FPS and 248.800 TPS, with 0.096 ms mean
GPU frames. The TPS difference from prior fixed-state rows is inside the observed machine variance and is not credited as an
optimization. Removing the deprecated OpenGL implementation and completing the consolidation work leaves the fork at roughly
25,499 net C++ lines relative to current upstream, including new untracked source files and excluding documentation. This is
below the 29,000-line housekeeping target.

### Refresh-paced Turbo presentation

Removed Turbo's explicit 15 FPS presentation throttle. The scheduler now yields between completed logical simulation updates,
uses SDL's current display refresh rate as an anchored frame deadline, and services messages, mouse/window input, completed
background work, UI state, and painting before returning to the next deterministic update. The eight-update Turbo batch and
40 Hz scene housekeeping are unchanged. Presentation and simulation now use independent deadlines. Turbo discards scheduler
lateness caused by rendering, the OS, or a long tick instead of accumulating catch-up debt and alternating between fast and
slow bursts; an over-budget simulation batch simply continues at the sustainable throughput.

Vulkan VSync now prefers MAILBOX presentation when the device exposes it, matching the render worker's one-pending newest-frame
mailbox and preventing a second stale FIFO from forming in the swapchain. FIFO remains the required synchronized fallback.
The integrated benchmark now reports message/window cadence, frame-interval p50/p95/p99/max, scene-batch duration, and the
longest UI-bounded simulation slice. Two warmed five-second EverythingPark runs on a 144 Hz display both produced 144.018 FPS,
with 264.897/265.265 logical TPS, 156-159 message pumps per second, 7.100/7.111 ms median frame intervals, and sub-11.4 ms maxima.
This is an intentional shift from the former 316.830 TPS at 13.334 FPS toward smooth input and presentation.
`--benchmark-visible` runs the same timed benchmark in an ordinary visible window and exits automatically, providing the missing
compositor/playability row without changing the user's renderer, VSync, fullscreen, cursor, audio, or configuration settings.

SDL controller handles now use RAII ownership. The periodic/hotplug rescan closes old handles before replacing them, and
shutdown closes the remaining devices instead of leaking references and repeatedly reopening them every five seconds.
The Release x64 Vulkan build is warning-clean, all 519 tests pass, and focused
refresh-normalization and Vulkan present-mode tests cover the new policies.

The final warmed EverythingPark Vulkan/VSync run used a three-second warm-up and ten-second measurement. It sustained
250.173 logical TPS and 144.069 FPS; frame intervals measured 7.270/8.456/9.310 ms at p50/p95/p99 with a 10.463 ms maximum.
Mean GPU frame time was 124.006 microseconds, while simulation averaged 3.123 ms per logical tick. This is the current
smooth-Turbo checkpoint; earlier 265 TPS figures predate the present transport-routing workload and are retained only as history.

### Upstream synchronization

Manually integrated upstream `develop` through `6903d5310e`: all 22 localisation files from `609a8cb46a` remain byte-identical,
and the newer gameplay commit prevents underground guests from choosing the watch-ride diversion through a shared map-height
query. Its replay `v0.0.97` asset manifest and distribution changelog are retained exactly. The ancestry merge remains for the
user after this dirty-worktree checkpoint is committed; the detailed review and expected Git state are recorded in the
[upstream merge manifest](upstream-merge-manifest.md).

The next two upstream commits are also integrated manually. Typed `TileElementsView` scans replace legacy tile-pointer loops
throughout UI, construction, import, pathfinding, and world code while retaining fork routing and persistence owners. Land-height
scenery removal uses a compaction-aware loop because deleting through the upstream range iterator could skip adjacent scenery.
Upstream's maze intensity patch targets a legacy modifier absent from this fork; that dead modifier path is removed, sampled
maze ratings and fixtures remain authoritative, and the network stream advances to version 2. The ancestry-only merge command
and commit-by-commit dispositions are recorded in the [upstream merge manifest](upstream-merge-manifest.md).

### Station platform boarding regression

Reworked transport and eligible roller-coaster station staging around immutable platform assignments. When a train departs, its
stopped consist supplies one deterministic car/seat marker for every seat. Pair-loaded cars contribute one complete pair per
pass across the consist, so partial cohorts spread along the full train. A guest keeps that same car, seat, destination, and
platform position until boarding succeeds or the assignment is explicitly released.

Arrival no longer prepares a boarding plan, compacts assignments, or restarts platform walks. A train first finishes unloading
and then publishes itself through `RideStation::TrainAtStation`. Each staged guest can bind only the exact vehicle seat already
shown by the platform marker. Successful binding consumes the single station assignment; failure leaves it intact for the next
train. A continuing transport rider occupying that exact seat therefore delays only that guest without reshuffling the platform.

The fixed vehicle passenger array still uses `num_peeps` and `next_free_seat` as its authoritative active/reserved prefix.
Inactive tail ids left by established unloading are reusable and do not count as duplicate ownership. Every car must finish
unloading before the train can publish for boarding, including the zero-prefix case.

Roller-coaster staging remains station-local and requires entrance and exit on the two opposite lateral platform edges. Same-side
and malformed longitudinal layouts retain ordinary queue boarding; transport stations deliberately bypass this coaster-only
geometry gate. Platform guests complete the inward entrance waypoint before walking along the established car loading line, so
they stay inside the entrance opening and platform fence.

An incoming follower closes new bindings on the front train before empty, minimum-time, or minimum-load waits are evaluated.
Guests already bound finish their walk, while unbound staged guests retain their exact positions for the follower. Paired-seat
rollback and departure continue to use the vehicle's authoritative reservation prefix, so maximum-wait and
`leave when another train arrives` cannot strand a half pair or abandon a bound walker.

Save reconstruction now restores a platform assignment by its saved car/seat identity instead of deriving a car-major slot
number. Current-version saves therefore preserve the pair-balanced platform order. Older targets still receive the established
station-exit recovery state because they cannot represent the new platform substates.

The runtime regression drives a real continuous-circuit coaster through natural departure, return, unloading, station
publication, exact-seat binding, approach, and final boarding. On every simulation tick it asserts that all staged car, seat,
slot, and destination fields remain unchanged, then verifies the vehicle passenger entries and final `onRide` states. The full
Release x64 suite contains 517 passing tests.



### Fork-wide invariant and code-quality consolidation

Refactored the fork additions around explicit mutation boundaries and removed 2,877 net lines of C++ while
retaining the implemented gameplay. Transport-service construction is now invalidation-owned: ride construction, entrance/exit
placement, rating publication, status changes, and the single broken-down transition mark a service dirty. Readers rebuild only
dirty services instead of hashing every transport ride and every measured leg once per simulation tick. The station spatial index
is authoritative between those transitions, while queue time, fare policy, weather, and crowding remain live overlays. Scripted
station mutations use the same invalidation boundary and clear staged platform geometry before changing station coordinates.

Station platforms now use one ride/station-indexed transient state rather than an unordered map plus a second copy of the train
consist. Ride vehicle configuration owns consist shape; platform capture owns seat geometry; and save import is the explicit
boundary that validates and reconstructs staged guests. Normal boarding no longer carries branches for a changed consist,
missing live train entity, invalid car index, or impossible seat identity. Those conditions are engine corruption and fail through
runtime assertions instead of silently sending guests through another recovery path. Platform recovery and requeueing share one
cleanup method, and the former partial/full train-summary pair is one complete consist traversal.

Platform FIFO order is stored directly as slot indices instead of a second sequence number, repeated minimum scans, and an
arrival-time sort. Guest teardown and pickup release platform ownership through the existing `RemoveFromRide` lifecycle boundary;
normal deletion, scripting deletion, cheats, and pickup therefore cannot leave stale guest ids behind. A cancelled reservation
frees its fixed platform slot, while a successful binding atomically transfers that assignment to the vehicle passenger array.

The same pass consolidated Vulkan result handling, graphics-pipeline construction, upload staging, and backend configuration;
removed callback-side audio telemetry; selected scalar/AVX2 spatial mixing once during mixer initialisation; collapsed route-cache
indexes and topology publication helpers; and removed duplicate ride-rating scans and measurements-row formatting. Explicit
Vulkan startup errors, strict fullscreen behavior, SDL device negotiation, save corruption checks, and unsupported station/object
boundaries remain deliberate guards.

A completed second function-by-function audit removes another 1,911 net C++ lines, taking the fork delta from 31,911 to 30,000. One-call
entity/tween/rating/audio/pricing wrappers are either inlined or replaced by their owning state, repeated UI-frame and platform
test setup is shared, and station save reconstruction reuses the live platform geometry builder. The audit also removes one guest
traversal from save repair, one consist traversal from every ordinary unloading tick, repeated topology-generation reads,
per-candidate topology validation, repeated transport-exit distance searches, and the Vulkan backend's unused asynchronous
readback bookkeeping. Transport journey scratch storage is fixed to the engine's station limit, platform boarding uses one
station-owned FIFO assignment relationship, and ride-stat UI plus regression fixtures share their established row and
geometry builders. The 30,000-line gate is met without deleting distinct regression coverage or compressing code mechanically.

Vulkan now keeps the CPU FreeType cache's immutable coverage surfaces in a bounded persistent atlas. Repeated cached text emits
no upload, allocation, or retirement work; the obsolete transient-bitmap path is removed. The entity registry's existing ordered
membership bitmap now also owns free ids, with a 16-word non-empty summary replacing sorted-vector insertion and erasure. A
representative 20,000-cycle allocator churn benchmark was 2.02-2.06 times faster with the same allocation checksum and reduced
allocator storage from 131,070 to 8,328 bytes. Audio channel completion now has one lifetime state, and the real-time callback
mixes and removes completed channels in one traversal.

The combined Vulkan-enabled Release x64 build is warning-clean and all 520 tests pass. Two independent 2,000-tick warm-up plus
2,000-tick EverythingPark runs produced the same `93d0bf66ac3305c3000000000000000000000000` checksum at 633.359 and
630.453 TPS. A hidden five-second Vulkan/VSync run sustained 143.746 FPS, with 7.296 ms median and 11.995 ms maximum frame
intervals; its roughly 14,000-guest state reached 249.159 logical TPS while presentation consumed 25.8% of wall time. The
headless result remains the CPU simulation ceiling; the integrated result is the relevant smooth-Turbo measurement.

The final hunk-by-hunk review removed the retired OpenGL backend, the X8 full-canvas Vulkan bridge, synchronous Vulkan mode,
redundant renderer gates, dead drawing/window wrappers, duplicate atlas residency state, and repeated entity/routing/rating
helpers. This pass deletes 5,069 net C++ lines. Relative to the current upstream head, the fork now has a 24,385-line net
C++ delta, comfortably below the 29,000-line ceiling while retaining the full regression suite.

### Vulkan cold-start and exclusive-fullscreen errors

Vulkan surface and swapchain creation now happens only after the configured window mode has been applied and the real drawable
size is known. Exclusive fullscreen no longer discards the display mode's format and refresh rate: SDL selects the closest full
mode and installs it explicitly before entering exclusive fullscreen. The requested mode remains strict; failures do not switch
silently to borderless, windowed, or another renderer.

The former `LOG_FATAL` plus `exit(1)` path has been replaced by an exception carrying the rejected resolution, refresh rate, and
SDL/Windows error. The application boundary logs that error, shows it in a native message box when a UI exists, returns failure,
and lets the renderer worker, Vulkan device, and SDL window destruct normally. Drawable-size inference also handles Vulkan
without assuming an SDL renderer exists. The first strict cold EverythingPark launch then exposed a zero-sized fallback sprite
that software rendering treats as a no-op but the GPU cache tried to allocate. GPU command recording and cache enqueue now skip
zero-area G1 elements while preserving explicit errors for genuinely oversized images. A clean isolated run completed five
seconds of Vulkan rendering, 1,448 logical ticks, and 62 presentations before orderly exit. The gated Release build completes
with zero warnings and the full test suite passes 520/520.

## 2026-07-10

### Turbo 320 locality, routing, scheduler, and Vulkan checkpoint

Performance: corrected a ride-context cache hash that discarded tile X and Y after cumulative 64-bit shifts. The cache is now
a bounded 65,536-set, four-way exact table with deterministic FIFO replacement, full key/generation validation, and no node
allocation or rehash growth. The accepted 262,144-entry size preserves the hot set; 65,536 entries churned and 524,288 entries
hurt late working-set locality, so both rejected sizes and their measurements remain documented. Viewport-only map selection,
virtual-floor, patrol-overlay, and block-brake redraws no longer invalidate ride-rating context entries; content-changing map
operations retain the local generation invalidation.

Scheduler and routing: Turbo batches update presentation audio only after the final logical tick while retaining simulation-side
sound/RNG cadence. Offline games skip empty network facade work, with the network tick clock reset on successful client/server
startup and reconnect. The 1-in-128 peep maintenance traversal avoids a mask operation per entity. Wide-path maintenance no
longer invalidates connectivity-only reverse fields or committed transport routes. Transport comparison reuses its already
computed walking direction and committed boarding queue-end goal, and computes guest walking speed once per planning pass.
The station query API retains only the planner's bounded-box and explicit all-service operations; unused radius/fallback
wrappers and their per-candidate branch are removed, and callers consume their reusable buffers directly.
When an offline queued speed action changes cadence during a logical batch, the old-speed batch now ends after that update so
input and the newly selected speed take effect without up to seven ordinary-Turbo or 127 debug-speed updates of transition latency.
Delayed variable frames build interpolation endpoints only around their final drawable tick instead of rescanning all visible
entities around every catch-up tick. If that tick changes into fixed-frame Turbo, its unused pre-tick snapshot is discarded at the
already-authoritative post-tick positions, avoiding a post-tick visible-entity scan and redundant restore. Exact multi-entrance
route selection resolves the guest's source path node once for the ordered candidate set, retains station order for ties, and
preserves the geometric fallback when a field is stale or unreachable.
Leaving Turbo on a throttled non-draw frame now defers presentation to the immediately following unthrottled variable frame instead
of forcing a full fixed-frame paint and then painting again on the next outer iteration. Tick, action, input, window-update, and
audio ordering are unchanged.

Ratings: nonlinear speed scoring is computed once per sampled train instead of once per car. Transport service samples dirty
journey costs in place without removing unchanged station endpoints from spatial indexes. Leaf profiler scopes used for the
diagnostic pass were removed from the production per-car loop. Train-wide speed, nonlinear speed score, G-force coupling
numerator, and repeated longitudinal-G inputs are now prepared or memoized once while car-specific force curves and exact
integer rounding remain unchanged. The accumulator retains its exact post-profile/pre-speed local score, so an unchanged
environment does not repeat context decomposition and coefficient division, and each dirty directed leg averages its rolling
sample ring once for both measurements and quality. A ride's first sampled linked train reserves its exact current car count,
avoiding one-at-a-time cold vector growth without retaining capacity for configured trains that never become sampled.

Presentation and stations: entity interpolation retains only visible entities that actually moved, so entering Turbo restores
moving coordinates instead of every visible candidate; the empty fixed-to-variable restore is skipped. Supported ordinary
roller-coaster stations now reuse the exact stopped-consist platform pre-queue, while transport routing, fares, and crowding
remain transport-only. Typed entity iteration dereferences registry slots from its membership invariant and advances to an
already selected live id without repeating the bitset search; removal-ahead still takes the mutation-safe fallback. Temporary
tween/restore positions no longer dirty the authoritative spatial index, removing a redundant cross-tile remove/reinsert pass
when changing between variable presentation and fixed-frame Turbo.

Vulkan: gated renderers can skip a busy frame instead of blocking the simulation/UI caller. Acquired frames have a safe
abandonment path, swapchain resources are destroyed in dependency order, and direct first-use texture bytes are owned by the
sealed command stream. Stable allocation serials and cache-owned residency leases defer atlas-slot reuse until presentation or
failure retirement. The Vulkan renderer now has a bounded newest-frame mailbox, recyclable command arena, worker-owned
backend submission, and complete failure/shutdown lease retirement. Logical and physical extents travel separately; SDL drawable
size is sampled only by the UI thread, zero/minimized surfaces retain their old swapchain, and Vulkan-capable builds enable the
ownership-safe worker by default. LightFX composition consumes immutable frame-owned palette
data and compact 32-byte clipped light commands. The direct path clears and atomically accumulates those commands
in an `R32_UINT` Vulkan image; unsupported format, extent, or compute limits retain the CPU-intensity fallback. Barriers cover
zero-light frames, untouched pixels, and atomic read-modify-write visibility, while command-only capture no longer allocates a
full-screen CPU lightmap. HDR10 is an opt-in Vulkan setting; compatible swapchains use BT.2020/PQ and publish D65/paper-white
mastering metadata through `VK_EXT_hdr_metadata` when the extension is present, while unsupported displays remain in SDR.
The bundled Windows SDL lacks its compiled Vulkan video-driver hooks, so the platform boundary now obtains the SDL-owned HWND,
creates `VK_KHR_win32_surface` directly, and uses the DPI-aware client rectangle for drawable pixels. Linux and macOS retain
their SDL Vulkan WSI path. Optional per-frame-slot timestamp pools measure uploads, indexed drawing, LightFX, final composition,
and total GPU time. Results are converted with the device timestamp period and published only after the owning frame fence is
complete, so integrated benchmark sampling never adds a hot-path query wait. A fixed 256-record completion ring and caller-owned
reusable collection buffer avoid allocator churn; per-draw benchmark consumption retains every
measurement frame until collection, while explicit render-worker timing boundaries drain and discard warm-up work before the
measured interval and harvest its in-flight tail afterward. The CPU report names the isolated `vkQueuePresentKHR` wall time as
present-call time rather than implying it is exclusively a display wait.
Graphics-derived remap, blend, and LightFX lookup tables are no longer captured during Vulkan device initialisation, which runs
before base graphics are loaded. Their one-time capture is deferred until the first draw and versioned through the worker packet;
the readiness flag is published only after capture succeeds so an exception remains retryable.

Verification: Release core, Vulkan UI, CLI, game, and tests compile
with warnings treated as errors. The final renderer/timing/boundary slice passes 46/46 focused checks, the earlier broad
GPU/topology/network/pathfinding/station/entity/rating selection remains 204/204, and the full suite passes 517/517. The
hidden Win32 Vulkan lifecycle/readback fixture passes on an RTX 5070 Ti with the
Khronos validation layer enabled. Two independent 2,000-warm-up/500-measurement EverythingPark windows reach 590.596 and
588.405 TPS with the same `6088da79...` checksum. Two independent 8,000-warm-up population-pressure windows reach 382.847
and 403.916 TPS with the same `405ee291...` checksum, 2.468/2.406 millisecond medians, and identical state snapshots. Both
deep runs clear the Turbo 320 budget by more than 19%. The shared route footprint is 6,663
nodes, 543 targets, 3,611,346 direction entries,
the same number of retained distance entries, and 517 single-ride targets. A profiled repeat keeps the checksum and reaches
363.146 TPS despite instrumentation; its largest scopes are peep updates (912.247 ms/500 ticks), vehicle updates (406.739 ms),
live rating samples (188.307 ms), and direction selection (35.733 ms).

The final clean-binary revalidation remains deterministic while the workstation is under a different background load:
2,000-warm-up runs reach 567.108 and 546.548 TPS with matching `6088da79...` checksums; 8,000-warm-up runs reach 385.418
and 373.469 TPS with matching `405ee291...` checksums. All four remain above Turbo 320. The new hidden full-UI matrix reaches
median logical rates of 318.324 TPS for software, 318.323 for OpenGL, and 318.400 for Vulkan at about 13.4 presented FPS.
Vulkan needs a median 1.797 ms complete CPU draw but only 139.091 us of GPU time per presented frame; its median GPU pass
split is 17.154 us upload, 116.354 us indexed drawing, 0.596 us LightFX, and 5.142 us composition. With VSync enabled it
reaches 316.830 TPS. The benchmark leaves the user's configuration hash and timestamp unchanged.

### Upstream develop integration and directed-leg UI cleanup

Merge: manually integrate upstream `develop` through `a770ffc04e`. The new editor-scene ownership, ride colour/operations
layout, lowercase `TileElementType` names, sloped-path puke/litter correction, plugin save binding, networking fixes, graphics
round-trip work, translations, workflow maintenance, and documentation are retained. Fork routing, sampled ratings, private
save versions, transport platforms, spatial audio, route caches, and Vulkan sources remain at their intended owners. The
commit-by-commit decisions and seven conflict resolutions are recorded in the [upstream merge manifest](upstream-merge-manifest.md).

Interface: multi-station measurement pages no longer display the conservative ride-wide E/I/N envelope. That aggregate remains
an internal ride-list/value compatibility result only. The selected directed leg now uses the normal measurement convention:
white labels with black values on separate excitement, intensity, nausea, maximum/average speed, every applicable G-force
extreme, transport comfort, decoration, and fare row. Ride-global length and time remain in the construction/test facts instead
of being repeated per leg. Transport is the deliberate exception: its leg time and distance remain because its service page
has no ride-global construction summary. This replaces the three compressed black-text lines without moving unrelated facts,
the window no longer reserves height for the rows that were removed, and the save-design control stays below the separator.

Vehicles: ordinary non-block continuous-circuit rides with multiple stations distribute initial trains among stations that can
physically hold the selected consist instead of stacking every train behind the first station. The station-fit rule is shared
with the existing train-count limit, and special operating modes retain their established placement paths.

Localisation: retain upstream English IDs `7033..7038` and move the fork-owned block to the contiguous `7039..7065` range.
Removed compressed/compatibility-summary strings are not carried forward as dead aliases.

Verification: direct Release core, Vulkan UI, game, CLI, and test builds pass without compiler or linker warnings. Focused
merge/rating/routing/formatting coverage passes 204/204 tests, the station allocator passes 5/5, and the full suite passes
457/457. Two warmed EverythingPark runs reach 261.961 and 263.398 TPS with matching `72638ee2...` checksums and 3.699/3.692
millisecond medians.

### Transport rides as route services

Decision: guests now use transport rides as planned station-to-station journeys toward a concrete ride, shop, facility, or park-exit goal. A railway, monorail, chairlift, or lift is no longer selected opportunistically as an ordinary attraction, and the legacy rule that made free transports automatically acceptable is removed.

Routing: compare direct walking with walking to a station, expected queue/boarding time, every onboard segment through the selected alighting station, and the remaining walk, with every term expressed in milliseconds. Current shared route fields now supply exact path distance for the final walk, the resolved boarding queue end, and station-exit egress; unavailable/inexact fields retain the geometric estimate, while an exact unreachable access or egress leg rejects that candidate. Walking time uses a three-mph baseline adjusted by guest energy and slow-walk state; segment time uses measured seconds or a distance/speed fallback. Free, Discount, and Fair services have progressively stricter time thresholds. Precipitation triggers a fresh comparison, accepts any positive saving for paid non-extortive service, and favours the measured sheltered time of the selected directed journey rather than a ride-wide shelter fraction. The direct walking direction and reachability result are reused when transport loses, avoiding a duplicate bounded search.

Performance: a transient ride-service cache validates once per tick, computes quality once per changed transport, precomputes every directed forward journey in quadratic station count, and exposes constant-time station-pair lookup. Shared destination fields retain four additional bytes of distance per exact path-node/target pair plus a compact entrance-source index, trading bounded RAM for constant-time walking-cost probes without new per-guest path searches. At the recorded 3,611,346-entry EverythingPark footprint, the distance payload is about 13.8 MiB before vector overhead. This slice has no independent TPS claim pending the warmed EverythingPark benchmark and memory report. Queue delay, platform crowding, guest cash and vouchers remain live overlays rather than invalidating shared geometry and timing.

Integration: park exits and resolved entrances for attractions, advertised rides, first aid, toilets, cash machines, shops, and other facilities now converge on one destination-routing helper. Ordinary ride/facility and first-aid target choice prefer the shortest reachable exact field instead of Manhattan distance; an advertisement retains its specified ride, and synchronized-station selection keeps its established tie rule. Outside-park entry and spawn travel use the same helper without considering in-park transport.

Advertising: ride-specific advertising and free-ride voucher campaigns now exclude transport services from the attraction picker and authoritative campaign action. Legacy or imported campaigns that name a transport no longer assign it as an attraction target or grant a ride voucher; advertisements for ordinary attractions retain their exact target and may still route through transport to reach it.

Economy: transport value is led by segment distance, then multiplied by speed, comfort, and decoration. Comfort starts from a per-tick baseline and is reduced by vertical deviation, lateral G, and realised longitudinal G; comfort and decoration are accumulated proportional to distance. Journey-specific fares use four operator policies: Free at zero, Discount at half value, Fair at full value, and Extortive at twice value. The planner and entrance use the same journey calculator. Extortive service is considered only when walking is unreachable and no non-extortive service is usable; paying it reduces happiness and creates a dedicated thought.

Guest behavior: transport vehicles retain through-passengers across intermediate stations and unload them only at the selected destination. Completing a journey does not increment ordinary ride count or history and does not change favourite selection, satisfaction, or nausea. Planned transport remains available to leaving guests.

Capacity: Miniature Railway, Monorail, and Suspended Monorail stations now expose a real second-stage platform queue. When a
train physically clears the station, the next cohort leaves the external queue and walks to deterministic positions aligned
with that stopped consist's cars and seats. Capacity is the actual linked-consist seat total, not a station-tile estimate;
through-riders compact first, and staged guests bind FIFO to the exact visible car/seat they reserved rather than running the
ordinary random car and next-seat selection again. The arriving car sequence and capacity must still match the captured consist,
and each reserved seat must be the next contiguous empty seat after through-rider compaction. An occupied exact seat leaves the
guest at that marker for the next train, while vehicle/configuration changes clear staging through the existing lifecycle. Fare
eligibility is
rechecked and payment is committed only after successful physical binding. Closing or invalid stations recover staged guests
through the exit, then the entrance/requeue path, with falling reserved for missing geometry. Chairlift stages against its native two-seat scalar
loading positions and physical station-clear transition. Lift remains just-in-time because its waypoint cabin reaches generic
departure completion only at the tower top; no-platform styles and other waypoint-loading vehicles remain outside the adapter.
Ordinary roller-coaster stations now share the same physical staging contract when they expose a visible platform and scalar
car loading positions. Their capacity is exactly the captured stopped train, using the existing consist and seat layout rather
than a station-tile estimate. This does not opt coasters into transport routing, fares, or overcrowding policy.

Interface: the measurements tab now gives transport rides a service-quality panel instead of attraction ratings. It displays measured or estimated comfort, decoration bonus, average speed, and each station segment's time, distance, and fare value from the same shared transport metrics used by routing. The income tab shows the four proportional journey policies instead of a misleading single ride-wide ticket price.

Compatibility: private park version `60012` stores distance-weighted transport quality totals. Version `60013` adds the
selected alighting station and appended Free policy. Version `60014` adds platform guest substates; current saves reconstruct
the transient FIFO registry, while older-target exports serialize staged guests into a coherent station-exit approach. Exports
targeting older transport versions still clear the private route marker, and older saves load with a null destination, Fair
pricing, and no staged cohort.

### Directed station-leg measurements

Ratings: rides with multiple stations now publish rolling samples for the physical directed leg between the station just left
and the station actually reached. Excitement, intensity, nausea, duration, distance, speed, every G-force extreme, comfort,
decoration, and fare data therefore describe the journey a guest could experience rather than an artificial full-track Mobius
circuit. Multiple destinations from the same origin remain distinct endpoint pairs.

Interface: the measurements tab selects every observed `Station A -> Station B` pair by endpoint identity and displays that
leg's complete measurements. It is not limited to the first four stations or to an origin-only key. The legacy ride-list/value
rating remains a conservative compatibility envelope—minimum excitement and maximum intensity/nausea—and is refreshed only
after every station has at least one measured outbound leg, so a partially sampled Mobius ride is not presented as complete.

Transport composition: journeys through three or more stations use the directed measured service graph and sum the exact
adjacent-leg time, sheltered time, distance, and fare along the shortest-time route, with fare and then greater shelter as tie-breaks. The old station-wide estimate
is used only while a leg has no measurement.

Compatibility: private park version `60015` stores the directed endpoint histories and active leg accumulator state. Older
saves clear active samples on import so pre-leg partial circuits cannot contaminate the first new measurement; export to an
older target is non-mutating.

Shelter and capacity: private park version `60016` adds distance-weighted sheltered exposure to rating samples. Route planning
uses a once-per-tick generation of the strict full-queue-and-full-platform predicate: uncommitted walkers reconsider service
availability, while committed guests directly retain any selected boarding station that remains usable.

Details: [Transport ride routing rationale](transport-ride-routing-rationale.md)

Verification: focused pathfinding, ride-rating, and save-migration tests cover reasonable time savings, rain, fare bands,
committed routes, destination changes, distance-weighted comfort, journey composition, consist-sized platform capacity,
proportional transport value, and compatibility round trips.

### Vulkan-first renderer foundation

Direction: the duplicate OpenGL backend has been retired. The cross-platform path uses native Vulkan on Windows and Linux and Vulkan portability through MoltenVK on macOS; old `OPENGL` configurations migrate to the hardware-presented software renderer. GPU-resident indexed assets, command streams, palette work, effects, clipping, culling, and composition are the intended ownership boundary.

Foundation: add backend-neutral GPU command and atlas structures plus a Vulkan device layer for SDL surface creation, portability enumeration, device and queue selection, swapchain negotiation, frames in flight, and reusable mapped upload rings. Vulkan-capable builds expose the direct renderer and render worker by default; builds without the required SDK retain the software renderer.

Migration: Vulkan now always uses direct command recording; the X8 canvas-upload bridge and its routine full-frame CPU transfer have been deleted. The staged implementation and remaining deletion criteria are documented in [Vulkan renderer migration](vulkan-renderer-migration.md).

Progress: the Vulkan command path executes indexed lines; opaque solid, textured, masked, crosshatched, TTF, and
one-to-three-remap rectangles; deterministic depth-peeled transparency/blend composition; and ordered indexed rain/snow.
Remap and blend tables remain GPU-resident, depth and indexed colour survive every pass, and final palette presentation
selects a correct SDR format or a compatible 10-bit HDR10 BT.2020/PQ pair. HDR maps unchanged SDR sprite appearance to a
configurable paper-white level rather than making legacy art intrinsically brighter. Screenshots use an explicit synchronized
indexed readback boundary; ordinary presentation performs no CPU framebuffer upload or readback. Details:
[Vulkan renderer migration](vulkan-renderer-migration.md).

Build quality: link the Vulkan loader import library by its full SDK path instead of adding the entire SDK library directory
ahead of project dependencies. This prevents the SDK's unrelated dynamic-CRT `SDL2-static.lib` from shadowing OpenRCT2's
static-CRT SDL library and removes the resulting Windows `LNK4098` warning without suppressing it.

### Measured pathfinding and vehicle hot loops

Performance: the warmed EverythingPark profiler identified guest direction searches and vehicle updates as the dominant CPU
work. Exact path-topology nodes now precompute thin-junction and adjacent wide/owned-queue classification, and one synchronous
`ChooseDirection` search reuses its current exact chunk view. Inexact, unmatched, shop/entrance-sensitive, or ghost-affected
layouts retain live tile-element behavior. Details: [Path topology cache](path-topology-cache.md).

Pathfinding: stable park exits and resolved ride/facility entrances now receive epoch-invalidated reverse distance fields.
The main thread freezes exact directed topology, the process-lifetime worker pool builds independent target fields, and the
main thread generation-checks and publishes them in deterministic order. Live permitted edges, queue ownership, guest
junction history, and the bounded heuristic fallback remain authoritative. Details:
[Shared destination route fields](shared-route-fields.md).

Verification: the clean 2,000-tick EverythingPark comparison improved from 165.895 to 184.352 TPS and from 5.919 to
5.054 milliseconds median tick time with an unchanged final checksum. The profiled comparison cut `ChooseDirection` time by
52%, cut `PeepUpdateAll` by 33.8%, and recorded no live thin-junction fallbacks in that park.

Verification: after shared reverse fields and visible transport-platform staging, two clean 2,000-tick runs measured
241.767 and 241.871 TPS with matching `2fc90d5f...` checksums and 3.868/3.851 millisecond median tick times. This is 31.2%
faster than the 184.352-TPS checkpoint and 45.8% faster than the 165.895-TPS baseline. The profiled run reduced
`ChooseDirection` to 201,038 microseconds and `PeepUpdateAll` to 880,203 microseconds; live ride-rating sampling is now the
largest isolated remaining vehicle cost. Details: [Shared destination route fields](shared-route-fields.md) and
[EverythingPark 320 TPS refactor plan](performance-320-tps-refactor-plan.md).

Verification: the station-less facility target/index and live-rating eligibility follow-up reached 254.297 and 256.592 TPS
in two clean 2,000-tick runs, with matching `182e7448...` checksums and 3.834/3.794 millisecond medians. The new profile cut
`ChooseDirection` to 81,798 microseconds but still attributed 1,189,440 microseconds to 280,975 eligible live-rating calls;
that measured inner sampler, not route selection, remains the next CPU target.

Verification: the exact directed-leg, route-cache ownership, spatial-index worklist, and save-migration checkpoint passes all
452 tests. Two clean 2,000-tick EverythingPark runs measured 251.729 and 251.123 TPS with matching `89b1134c...` checksums
and 3.719/3.720 millisecond medians. The faster run is 51.7% above the original 165.895-TPS baseline. A bounded 500-tick
profile attributes 1,358,188 microseconds to live rating sampling, including 1,082,148 microseconds in vehicle-environment
resolution; cold local-context construction is only 159,625 microseconds. Profiler scopes add clock, atomic, and stack
bookkeeping, so these totals identify relative ownership rather than predicting unprofiled TPS; environment-cache access is
still the next measured optimization target rather than guest routing.

Performance: a train-head update now reuses matching owning-ride and loaded vehicle-object pointers inside a scoped transient
context. Mismatched access and calls outside that update retain the original lookup, update order is unchanged, and nested
profiler scopes separate rating, measurement, station, motion, and sound costs. Typed entity iterators likewise retain their
registry reference and directly validate concrete type tags without replacing the mutation-safe entity lists. Details:
[EverythingPark 320 TPS refactor plan](performance-320-tps-refactor-plan.md) and
[Ride rating aggregate rationale](ride-rating-aggregate-rationale.md).

Performance: live ride-rating eligibility is now checked at the original vehicle-update sample point before non-head, ghost,
inactive-status, or non-normal-rating vehicles enter the sampler. Each eligible head traverses its consist once through the
shared deterministic seat summary, and synchronized-station adjacency is cached only for the same ride, tick, topology
generation, status, and departure flags. This follows the measured 1,081,990-microsecond/485,000-call hotspot; no throughput
gain is claimed until the checksum-matched EverythingPark rerun.

### Normal ride speed adjustment

The standalone excitement contribution from vehicle speed now follows `speed^1.5`, normalized so the established speed-90 baseline is unchanged. The formula is `90 * pow(speed / 90, 1.5) * rawScale / 5`; the hot path evaluates its equivalent `speed * sqrt(speed / 90)` to avoid a general-purpose `pow()` call per sampled vehicle tick. Intensity and nausea remain linear. This adds progressively more excitement above the baseline without disturbing the existing G-force curves or their distance-sensitive tick aggregation.

Verification: `RideRatings.VehicleSpeedTickScoringUsesPowerOnePointFiveForExcitement` covers zero, half, baseline, and double speed.

### Phoenix thought Easter egg

Addition: revive the removed “Nice ride! But not as good as the Phoenix…” guest thought as a rare fallback after an ordinary ride. It is considered only when the guest did not produce the existing “was great” response and has no other fresh thought about that ride, then succeeds on a deterministic 1-in-2048 scenario-RNG roll. Planned transport legs remain excluded.

Verification: `PlayTests.NiceRidePhoenixThoughtIsARareFallback` covers the winning roll, an ordinary losing roll, and suppression by another fresh ride-specific thought.

### World-space spatial audio and 7.1 mixing

Decision: replace viewport-bound, zoom-attenuated sound with a shared world-space listener model. One-shot effects, vehicles, and ride music now use continuous three-dimensional distance from an elevated virtual camera and remain eligible at every zoom level instead of disappearing at a screen rectangle or fixed tile boundary.

Correction: derive virtual camera height from the isometric viewport's visible ground footprint. Zooming toward an area lowers the listener, raises focused emitters, and increases their contrast over horizontally remote sources. Object `z` and camera `z` both participate in Euclidean distance and elevation.

Correction: replace the old 16-tile rolloff with a continuous distance curve and, after physical-system listening, compress its exponent to `0.8` (approximately 4.8 dB quieter per distance doubling). Proximity still orders sources generically, while rollercoasters and music remain useful deeper into the park. Vehicle voice priority starts with post-distance loudness rather than allowing raw train mass to dominate far-away sources.

Addition: Doppler pitch now comes from smoothed source-listener radial range rate, covering moving vehicles, a moving or zooming camera, and persistent one-shots. The effect is bounded to avoid fast-forward and teleport spikes; the legacy orientation-based vehicle pitch offset is no longer applied.

Decision: request 48 kHz 7.1 output in SDL's Windows-compatible `FL, FR, FC, LFE, BL, BR, SL, SR` order, with an automatic stereo retry when the selected endpoint refuses eight channels. Positional sources use constant-power speaker interpolation; non-positional stereo remains on the front pair, and LFE remains available for endpoint bass management.

Correction: replace repeated 16-bit `SDL_MixAudioFormat` additions with a floating-point mix bus, 6 dB of headroom, and a 0.95 full-scale peak limiter. The mixer now stores 8,192 channels and can select up to 2,048 vehicle emitters; ride music remains on its separate strongest-64 budget so it cannot consume the ride-vehicle pool.

Performance: replace linked channel storage and repeated vehicle-array scans with contiguous channels plus hashed id/slot lookup. Speaker-planar accumulation, a runtime-dispatched AVX2 positional kernel, direct stereo-to-positional resampling, and a fixed 1,024-frame callback give the already-dedicated SDL audio thread substantially more deadline margin. On EverythingPark, roughly 500-540 channels settled around 5.0 ms average / 7.0 ms worst against a 21.33 ms callback budget.

Tuning: vehicle volume bytes now retain their linear-amplitude meaning instead of sending half-scale track noise to roughly -41 dB. After the louder listening passes, mechanical audio is trimmed from 6x through 5x to 4.5x, rider screams from 4x to 3.5x, and other secondary vehicle cues from 2x to 1.75x.

Tuning: separate source-class rolloff exponents preserve one generic mathematical system without forcing every emitter to behave like the same physical source. Coaster, kart, and rider voices use exponent `1.1` (about 6.6 dB loss per distance doubling), world effects retain `0.8`, and amplified ride music uses `0.6` (about 3.6 dB per doubling). Music source strength rises from 2.5x through 3x and 4x to the final 5x listening calibration, while its camera-driven Doppler depth remains 5%.

Tuning: rain ambience now follows virtual listener height continuously. Its curve is shifted one camera-height step louder so zoom -1 receives the former closest-view strength, zoom -2 is louder still, and rain becomes progressively fainter as the camera rises.

Correction: ride-music culling now ranks on unclipped spatial gain instead of the final 0 dB-clamped playback volume. The 5x source calibration previously made many near and far rides tie, allowing ride iteration order to displace on-screen speakers. Newly selected sources can now start while retired voices finish their short fade, and reselected sources reverse that fade smoothly.

Correction: ride-vehicle audio now uses the closest physical car in each train as the acoustic point instead of always using its head car. This keeps long trains and distributed ride vehicles local when the visible part of the train is nearest the listener, without multiplying the same loop across every car.

Compatibility: 7.1 world effects now use a phantom front centre across FL/FR rather than routing straight-ahead sources solely to FC. This preserves side and rear positioning while avoiding a virtual-headset centre-channel downmix failure that can masquerade as proximity culling on Logitech GHub.

Verification: the Logitech endpoint advertises the standard `0x63F` 7.1 mask and accepted one WASAPI 48 kHz, eight-channel, 1,024-frame stream. EverythingPark selected all 291 vehicle emitters, mixed 337-365 vehicle channels per callback, and reported zero ride-channel or global admission failures. The callback measured 4.6 ms average / 7.6 ms worst against a 21.33 ms deadline. OpenRCT2 software-mixes logical voices into the eight-channel PCM stream before Windows or the headset driver receives it; there is no downstream 256-voice truncation boundary.

Resilience: handle SDL render-target and render-device reset events by rebuilding hardware-display textures, restoring the palette mapping, and invalidating the full frame. This is a best-effort recovery for Windows display-driver resets that previously left the simulation and audio alive behind a permanently black presentation surface.

Correction: replace the single non-positional crowd loop with an eight-sector diffuse surround field. Visible guests contribute to camera-relative directional clusters whose amplitude shares preserve the previous aggregate crowd loudness. The short positional purchase/cash-register cue receives a local 4 dB correction so it is not masked by that sustained ambience.

Correction: source retirement now fades over 350 ms. A crash no longer nulls the active ride-music track on the next game tick; the active track is allowed to finish, while the crash effect plays as an uncropped spatial one-shot.

Details: [Spatial audio overhaul](spatial-audio-overhaul.md)

Verification: the Release build and all 380 tests passed. Repeated EverythingPark runtime checks remained stable with 48 kHz,
eight-channel output and no callback errors on the Logitech/Windows endpoint.

### Main-menu responsiveness

Correction: completed aggregate rider samples now publish their rolling rating directly instead of calling the testing-only synchronous `UpdateRide()` helper. A symbolized live hang capture showed the title/main thread inside a whole-track close-proximity scan triggered when a train unloaded, while the independent audio callback kept playing. Track proximity, shelter, upkeep, and script-hook maintenance remain on the existing bounded incremental rating state machine, preventing ride-heavy title parks from monopolising a rendered frame.

Verification: the pre-fix title-menu monitor entered a sustained non-responsive state within 17-36 seconds and the debugger stack contained `RecordActiveRiderSamples -> UpdateRide -> ride_ratings_score_close_proximity_in_direction`. The corrected Release build stayed responsive for a continuous 50-second title-menu pass with no non-responsive samples; the full 380-test suite also passes.

## 2026-07-09

### Guest nausea and first aid behavior

Decision: guest nausea now behaves more like a condition that guests can respond to instead of only a hidden post-ride penalty. Sick thoughts and first-aid interest begin at nausea `128`, while the sick face and nauseous animation begin at `128 + 1` so the visual tiers stay explicit and easy to retune.

Tuning: ride nausea growth now scales across the full hunger bar, from `1x` at empty to `4x` at full, instead of only scaling the upper half of hunger and then doubling. Normal standing/walking nausea target decay is gentler, dropping by `1` per idle motive update instead of `2`.

Decision: sick guests now look for first aid over a range that scales with how sick they are. At nausea `128` they consider first aid within one tile; at nausea `255` they consider first aid within 128 tiles. The search is specified in tiles but compared using the engine's coordinate units.

Correction: guests who have already committed to a first-aid clinic do not try to sit on benches, and they are allowed to use that clinic when they arrive even if their nausea has fallen below the initial sick threshold. Guests can also use first aid opportunistically when they are already sick enough and bump into it.

Maintenance: the sick, very sick, and very-very-sick nausea tiers are now named constants, with display-start checks written as `threshold + 1`. The very sick thought remains intentionally separate from the face/animation tier and starts at the very-very-sick visual tier.

Verification: the Release build, focused gameplay tests, and local deployment completed successfully.

## 2026-07-07

### Decoration visibility policy

Decision: outside-decoration scenery bonuses now use a ride-type visibility multiplier after local scenery diminishing returns. Fully enclosed `3d_cinema`, `motion_simulator`, and `circus` rides receive no outside-decoration bonus. `haunted_house` and `flying_saucers` receive half. `crooked_house` and `dodgems` receive one quarter. Mazes and other ride types keep the full local scenery score.

Reasoning: the local-context system was correctly measuring nearby scenery, but it treated all riders as if they could see outside equally well. Fully enclosed shows should not be excited by gardens they cannot see, while limited-visibility rides should get a reduced benefit instead of an all-or-nothing rule.

Correction: local-context line of sight now traces to both the bottom and top of a candidate object. The existing terrain-relative range gate is preserved, but a tall decoration, elevated path, or elevated foreign track can now count if its upper ray clears maze walls or other solid blockers. Low objects behind same-height maze walls remain blocked.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

### Fixed-ride scenery sampling

Decision: fixed rides with `BonusScenery` now sample their local scenery context from the ride footprint centre and derive normal flat-ride eye height from the ride descriptor clearance box. Tower-like rides still use their dynamic height, while enclosed rides and mazes keep low viewpoints.

Reasoning: flat rides such as the Haunted House revealed that the remaining fixed-ride scenery hook was still too legacy-shaped. Clearance-derived viewpoints make Ferris Wheel, Magic Carpet, Enterprise, and similar rides benefit from height without hand-maintaining each ride type, while footprint-centred sampling avoids judging a multi-tile ride from only its station tile.

Current balancing: the local scenery score remains uncapped and square-rooted. `BonusScenery` consumes that same uncapped score; there is no restored legacy cap in the fixed-ride adapter.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

## 2026-07-06

### Decoration diminishing returns

Decision: local scenery now uses an uncapped square-root diminishing-return curve. It takes about four times as much raw decoration to reach the former local scenery cap, and denser decoration can still add value beyond that point at a slower rate. Fixed-ride scenery modifiers use the same uncapped local scenery score.

Reasoning: decoration was too influential at modest density and then stopped mattering too early. The new curve keeps dense scenery worthwhile without making the first few visible items dominate ride stats.

Current balancing: raw scenery `1200` maps to the former 18-point local scenery contribution. Raw scenery `300`, the old scenery divisor, maps to 9 points, and raw scenery above `1200` continues growing by square root.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

### Ride and guest tuning

Decision: vehicle-sampled decoration excitement now scales by vehicle speed around a `90` speed baseline. A vehicle at `45` speed receives half of the scenery excitement for that tick, and a vehicle at `30` receives one third, while path and track proximity bonuses stay unscaled.

Decision: Boat Hire free-roam and unbanked guided-turn ticks now use a gentler `0.5/1/1` excitement/intensity/nausea distribution instead of the coaster-style `2/4/4` unbanked turn score.

Known issue: Boat Hire stats inflated during the recent ride-rating tuning work and remain too high. Skipping the generic vehicle accumulator path was tested and walked back, and reducing the Boat Hire free-roam score did not fully solve the inflation. The main suspect is the new decoration/local-context scoring, which will need a separate balancing pass.

Decision: visible water now counts as a lightweight surface decoration, using the same low raw scenery weight as mowed grass. This gives water features a small local scenery payoff without making lakes equivalent to dense scenery placement.

Tuning: normal guest generation now uses `$50,000` park value as the square-root baseline instead of `$40,000`, reducing arrivals for parks below the new baseline while preserving the same curve shape.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md), [Guest generation and park rating rationale](guest-generation-rating-rationale.md)

## 2026-07-05

### Height-aware ride scenery context

Decision: replace the direct 3x3 vehicle and maze decoration scans with a shared cached local-context query. The query now scales from 5x5 at ground level up to 15x15 for high ride samples, applies distance falloff, line-of-sight checks against solid vertical volumes, mild lower-scenery height penalties, and diminishing returns per context channel. Range is resolved through a downward sight ray toward each candidate tile, so hills extend vision over lower land, pits reduce vision toward higher land, and scenery on cliff tops above the rider is occluded. Flat-ride scenery modifiers also use this context from the station/start tile with an eye-height offset, so elevated flat rides benefit from increased sight range.

Reasoning: decoration and proximity bonuses are now accumulated where riders actually are, so the local query needs to represent what riders can plausibly see instead of acting like a tiny immediate-neighbour count. Caching by origin tile and ride id keeps the expensive visibility work away from per-vehicle hot paths. Test-mode circuits now keep producing rating samples after the first completed test, preserving the rolling average while letting scenery/context edits show up after later test runs.

Correction: fixed-ride scenery origins now use ride-specific eye heights for tall non-coaster rides such as observation towers, roto-drop rides, launched freefall rides, lifts, ferris wheels, and chairlifts. Mazes use a lower viewpoint and maze track now blocks line of sight, so maze walls behave as walls for local decoration visibility.

Details: [Ride rating local context plan](ride-rating-local-context-plan.md)

### Ride rating sample save data

Decision: persist the ride rating raw accumulator plus active and recent rider/test samples in fork save version `60005`.

Reasoning: visible excitement/intensity/nausea ratings were already saved, but the new rolling sample cache was not. That meant an aggregate-rated ride loaded from a save could briefly recalculate from no samples and collapse to very low or zero ratings, then slowly recover as new riders rode it. New saves carry the rolling samples forward. Older saves clear the new sample fields but preserve the already-saved visible rating until a fresh rider/test sample exists.

### Boat Hire return routing rollback

Correction: remove the custom Boat Hire intervention that forced a boat to return when a passenger generated the normal "I want to get off" thought. Boat Hire already has return-to-station routing, and testing showed the reported failure was caused by blocked routing rather than missing get-off handling.

Reasoning: the added helper and steering override duplicated existing ride behavior and could mask the real problem when the station route is physically blocked.

Details: [Boat hire return rationale](boat-hire-return-rationale.md)

Verification: the obsolete regression expectation was removed, the test target built, and the gameplay suite passed.

## 2026-07-04

### Guest off-path recovery

Decision: guests walking on surface tiles now do a small local search for a reachable footpath within three tiles before falling back to random grass wandering. The search is wall-aware, respects blocked surfaces, water, in-park surface movement, and the same height tolerance used by normal surface movement.

Reasoning: guests that get pushed or dropped just off the path network should visibly try to recover when a nearby path is accessible, while still using the original random surface wandering when no local rejoin route exists.

Details: [Guest surface path rejoin rationale](guest-surface-path-rejoin-rationale.md)

Verification: the Release game and test targets built, the focused off-path recovery case passed, and the complete pathfinding
and full test suites passed.

### Mowed grass decoration

Decision: mowed grass now counts as lightweight decoration in ride scenery/proximity scans, per-tick vehicle and maze context scoring, and guest surroundings checks. Only surfaces that can actually grow grass and are in the `GRASS_LENGTH_MOWED` state count.

Reasoning: groundskeeper mowing should have a visible gameplay payoff instead of being cosmetic and staff-stat-only. Well-kept lawns now help nearby rides and guest scenery impressions through the same local scans that already reward decorations.

Details: [Mowed grass decoration rationale](mowed-grass-decoration-rationale.md)

Verification: the Release game and test targets built, and the focused decoration, footpath-connection, and ride-rating suites
passed.

## 2026-07-02

### Windows build and deployment

Established a reproducible Release build and local deployment workflow for the fork. Fork binaries and data are deployed
together so a playable build cannot accidentally mix these changes with assets from a different branch. Machine-specific
toolchain selection, commands, and deployment paths remain in the dedicated setup document.

Details: [Windows local build setup](windows-local-build.md)

### Ride excitement, intensity, and nausea

Decision: introduce a raw per-tick ride rating accumulator for normal ride rating calculations. Vehicle test measurements now sample track piece, velocity, G forces, shelter, and nearby decoration into running excitement, intensity, and nausea totals. Aggregate totals finalize through a square-root curve so doubled raw stats produce about 40% higher final stats.

Correction: aggregate ratings with tick samples now start from sampled raw totals only, with descriptor base ratings and additive legacy ride-wide bonuses excluded from the aggregate finalizer.

Correction: aggregate-rated rides without current samples no longer fall back to descriptor base ratings. A ride with no sampled rider/test data displays zero aggregate stats instead of hidden base stats.

Correction: completed rider/train samples now publish immediately into a rolling cache of the last twenty samples. The displayed ride rating is calculated from the average raw sample in that cache, so the GUI updates after rides are actually ridden while still smoothing one-off rider paths.

Correction: train rating samples now average the sampled contribution from every vehicle on the train for each tick instead of using only the lead vehicle. This lets rear and middle cars affect the rating without multiplying a train's raw stats by its car count.

Correction: completed test runs now publish their measured raw stat accumulator into the same rolling sample cache. Tested rides therefore display aggregate stats from the test run instead of staying at zero until guests ride them.

Correction: formal vehicle tests now accumulate excitement/intensity/nausea in the same active train sample slot used by live rider trains, keyed by the head vehicle as a phantom rider sample. In-progress test accumulators are no longer used for display, so the ride window keeps its existing rating during a test and updates only when a test train completes its circuit.

Correction: Maze no longer receives descriptor base stats or `BonusMazeSize`/`BonusScenery` post bonuses. Maze pathfinding now records per-guest active samples and publishes each completed exit path into the same rolling cache, so maze length contributes only through the paths guests actually walk.

Tuning: Maze capacity is now a three-mode policy instead of a literal rider-count setting. Normal allows one guest per maze tile, Overcrowding doubles that capacity while halving the raw accumulated rating stats before the square-root finalizer, and Sparse allows one guest per two tiles with a minimum of one while doubling the raw accumulated stats. Legacy numeric S4/S6 park imports normalise to Normal; track-design imports preserve `0`/`1`/`2` bucket values and otherwise normalise to Normal.

Tuning: per-tick G-force scoring now uses smooth curves informed by the old ride-wide G-force logic. `1.0G` vertical is neutral, `0.0G` is treated as exciting airtime, negative vertical G becomes progressively nastier, positive vertical G mostly feeds intensity, and lateral G has the steepest curve with the old `2.8G`/`3.1G` lateral penalties translated into a smooth severe range.

Tuning: the accumulator speed guard now floors speed at `1` instead of capping it at `90`, so speed continues to scale normally while zero-speed samples are guarded. Vehicle-object rating multipliers remain on the raw aggregate before the square-root finalizer, which is equivalent to applying the same ride-entry bonus to each sampled tick while preserving saved raw samples.

Details: [Ride rating aggregate rationale](ride-rating-aggregate-rationale.md)

### Guest generation and park rating

Decision: remove the active guest-count soft cap from normal guest generation. Park rating is now a smooth projection of average in-park guest happiness and happiness target, so crowding and queue pressure regulate future guests through happiness rather than a hardcoded suggested maximum.

Tuning: normal guest generation now scales geometrically with park value, using `$50,000` as the current baseline for the tuned spawn rate. A larger park attracts more guests when rating, pricing, awards, and entry value are otherwise equal, but square-root scaling keeps the growth in arrivals sublinear.

Tuning: park rating now applies exponentially to guest generation, doubling about every 100 rating points around the `700` reference point. Rating `700` is the healthy baseline, `800` generates about twice as many guests as `700`, `600` about half as many, and `500` about half of `600`.

Correction: guest generation keeps fractional probability through all modifiers and rounds any positive non-zero final chance up to `1`, so small parks with non-zero value no longer lose normal guest generation to integer truncation.

Verification: added direct guest-generation probability tests for the `500`/`600`/`700`/`800` rating curve and the tiny positive park-value case; `PlayTests.*` passes.

Research: real park calendars support the inherited March-through-October calendar as a temperate seasonal-park abstraction, but not as a universal calendar. Northern parks often close or reduce service outside spring-fall, while warm-climate parks and holiday-event parks may keep operating in winter.

Details: [Guest generation and park rating rationale](guest-generation-rating-rationale.md), [Real park seasonality research](real-park-seasonality-research.md)

### Ride admission pricing

Decision: replace direct ride admission price editing with a target policy: discount, fair price, or expensive. Normal ride admission prices now recalculate from ride value after ratings update, while shops, toilets, and photo/item prices remain direct controls.

Correction: expensive pricing targets the highest conservative price happy/value-tolerant guests should still ride for, staying below the hard refusal threshold instead of crossing just above the fair-price maximum.

Tuning: automatic ride-admission target prices are globally reduced to 70% before the `$20.00` cap is applied, so a former `$10.00` calculated price becomes `$7.00` and high-value rides hit the cap less easily.

Correction: guest ride-value perception now uses the same 70% scale when deciding whether a ride is discounted, expensive, or too overpriced to ride. This keeps the automatic target prices and guest willingness thresholds aligned.

Correction: guests now have an expensive-but-still-rideable ride thought for the upper half of the price band between fair price and outright refusal. The old target labels were also renamed from good deal/no effect/bad deal to discount/fair price/expensive.

Correction: the expensive ride thought now uses in-universe guest wording, "isn't worth the money", instead of exposing the internal ride-stat formula.

Decision: migrate runtime `money64` from `$0.10` units to `$0.01` units. This unlocks true cent precision for ride prices and other runtime money while preserving legacy tenth-based import, save, replay, and highscore compatibility through explicit conversion bridges.

Correction: legacy tenth-based authored values now convert at runtime boundaries for ride default admission prices, computed ride value, ride upkeep, object JSON prices, terrain-edge changes, water changes, and editor initial-cash clamps. This fixes the 10x-too-small running costs, ride tickets, and construction/landscaping prices caused by treating old tables as cent values.

Correction: legacy scenario objective currency conversion is now objective-aware. Money objectives scale from tenths to cents, while non-money payloads stored in the same field, such as minimum excitement for finish-five-coasters goals, remain raw.

Correction: currency text formatting now pads cent values below `$0.10`, so `$0.05` and `$0.01` display with two decimal digits instead of `0.5` or `0.1`.

Tuning: target-pricing margins now use `$0.05` minimums for discount, fair-price, and expensive pricing because the runtime money type can represent those values.

Details: [Ride pricing target rationale](ride-pricing-target-rationale.md), [Money cent precision rationale](money-cent-precision-rationale.md)

### Park entrance pricing

Decision: replace the park admission spinner with three automatic policies: charge the richest spawning guest, maximize admission profit across the scenario's spawn-cash distribution, or stay affordable to every spawning guest. The affordable policy is the new default for newly initialized parks.

Tuning: park entrance targets use the same 70% value debuff as ride admission targets before applying guest-cash and maximum-price caps, so a park needs more ride value before its entrance fee reaches the cap.

Compatibility: direct entrance-fee commands and legacy saves are preserved through an internal custom mode. Once a player selects one of the three policies, the computed policy owns the displayed and charged entrance fee.

Save format: fork-owned `.park` changes now use the private `60000+` version band instead of upstream's next sequential version. This prevents future upstream save versions from colliding with this mod's custom ride-pricing, cent-money, and park-entrance fields when upstream development is fetched later.

Details: [Park entrance pricing target rationale](park-entrance-pricing-target-rationale.md)

### Verification

The Windows build completed successfully. Ride-rating, price-action, gameplay, formatting, entity-import, S6 import/export,
advance-tick, and replay suites passed. The build retained one pre-existing non-fatal Roslyn `System.Memory` binding warning.
