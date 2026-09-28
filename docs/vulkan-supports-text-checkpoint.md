# Native supports and sign text checkpoint

Everything Park is the primary visual and performance sample. This checkpoint restores more original artwork while retaining the GPU-driven world path. It does not complete migration or pixel parity.

## Implemented

- [x] Path box/pole supports and metal/wooden tracked-ride supports execute native shader rules using shared tile-local terrain/support state. Original art remains resident; no CPU painter list or per-frame support bitmaps are introduced.
- [x] Whole multi-tile ride bodies use the nearest footprint tile for depth. Named platform fences, entrance panes, straight-flat rail contacts and station shelter edges have separate anchors. Every authored sprite retains one constant depth.
- [x] Ordinary banner, queue and ride-entrance text uses immutable glyph columns. Shader phase follows the snapshot tick. Text/font/status mutations publish changed state; scrolling alone does not upload new bitmaps.
- [x] Banner graphical fields have owner methods and a dirty-ID queue. Snapshot-held text generations remain immutable and reusable.
- [x] Original-rule tests cover path, metal and wooden supports, text columns/lifecycle, footprint anchors and support contacts. Actual GPU tests cover scrolling with no state upload and atomic whole-sprite depth.

## Visual validation

The previous 141/142 closeups used an upstream profile without a usable RCT1 config path. Upstream consequently selected asphalt fallback art where Vulkan correctly selected CSG checker tiles. Those old difference totals are invalid for equal-asset parity. The first 143 retry also exposed INI backslash escaping; it was stopped and preserved as a failed diagnostic.

`everything143-matched-art-02` corrects both profiles, pins CSG/G1 hashes, and captures the glass station and path/support gallery at all four camera rotations,1024x768 at zoom0. Native loaded-CSG state is recorded; the pristine upstream executable has path-only plumbing, so direct runtime CSG state is not claimed. Its fallback/load-error messages are now fatal to the capture runner. Root and agents inspected enlarged samples.

The checker tiles now agree. Wooden lattice returns with broadly correct placement; flat-body/platform fencing improves. Build 143 restored station panels but two rotations exposed overlap with the adjacent entrance. Build 144 removes the unsupported roof midpoint offset and gives entrance text its own authored height. Root and an agent inspected all four final views and enlarged entrance crops: the roof crossing the entrance band/frame is resolved, and the panes, text and glazing look coherent. Evidence: `obj/vulkan-parity/everything144-matched-art-01/manual-glass-review/README.md`. This is visual qualification of those components, not full-image parity.

An additional close review confirmed the beige ramps/deck faces are authored art visible upstream too; crowds obscure portions of them in the reference. They are retained as inspection crops, not asserted texture regressions.

Exact whole-image differences remain unmasked. Missing people/vehicles are still visible in these reports and do not excuse migrated-object regressions.

## Validation and performance

- Build142:69 focused tests passed, including original wooden-support oracle.
- Build143:71 focused tests passed with synchronization validation and no diagnostics.
- Build144:clean build,6 targeted entrance/roof/GPU scrolling checks passed without validation diagnostics. Earlier unchanged support/publication gates are retained.
- Authoring:58 Python tests passed; original rail include unchanged.

| 4K,12000 ticks, vsync144 | TPS | Accepted presents/s | CPU draw | GPU frame | Maximum accepted interval |
| --- | ---: | ---: | ---: | ---: | ---: |
|137, before supports/text |359.717|143.977|0.172ms|2.491ms|15.532ms|
|141, path/metal/text |359.958|143.743|0.153ms|3.192ms|33.135ms|
|142, wooden supports added |359.959|143.774|0.141ms|3.274ms|36.558ms|
|144, final station/text anchors |359.960|143.774|0.151ms|3.271ms|24.101ms|

These are partial-world throughput measurements. All submitted frames were accepted and simulation checksums agree. CPU draw variation is not claimed as a new optimization. The intermittent long frame still coincides with simulation stalls; throughput success does not close pacing. Build 144 accepted all 4,793 submissions, with a 9.1 ms P99 interval, zero lost/discarded/out-of-date frames, and final checksum `07d58eaefde6aa6d000000000000000000000000`. Its final state contains 17,042 guests, 2,208 staff and 2,128 vehicles; omitted graphical categories still limit performance acceptance. The hidden benchmark measures queue-accepted presents, not displayed scanout. Receipt: `obj/vulkan-parity/performance-support-text144-12000-01/summary.json`.

Startup pipeline preparation took 15.324 seconds using the previous build's cache seed; changed shaders still compiled. This is outside the measured ticks and is not a claim that first-launch responsiveness is solved.

## Manual-test deployment

- [x] Build 144 deployed to `D:\Games\Independent\OpenRCT2Mod`: 28 qualified files verified, 5 replaced with backups. The game was not launched; profiles, saves and objects were untouched.
- [x] Build inputs remained unchanged during compilation; final CPU/GPU targeted tests passed and source diff passes whitespace checks.
- [ ] Owner review of the newly deployed supports, entrances and scrolling signs.

Deployment receipt and backups: `obj/vulkan-parity/deploy-supports-text144-01/receipt.json`. The source checkpoint commit records the remaining regressions below.

## Remaining work

- [ ] Resolve metal/wooden support intersections with curved, sloped and cross-element rails. The named straight-flat fix is deliberately narrow.
- [ ] Complete six rejected support programs, station-internal/generic helix helpers and static-ride support-state effects.
- [ ] Complete wall/large-scenery and park-entrance scrolling text; qualify TrueType/hinting/font reload.
- [ ] Classify and fix the blank front-row gallery sign lettering in rotations 1/2. Nearby ordinary banner records should show the localized default name; do not assume all blank signs belong to deferred wall/large-scenery families.
- [ ] Finish the remaining track/station/water and underground filter/grid interactions. Accepted exterior/underground skirts remain intentional. Pathological buried Cinema remains low priority, not an accepted mismatch.
- [ ] Remove finite local-layer bias that can cross neighboring object depths and qualify independent equal-depth ties.
- [ ] Resolve intermittent simulation stalls and long presentation intervals; maintain 360 TPS / 144 accepted presents/s as layers return.
- [ ] Improve first-launch pipeline preparation and qualify visible startup responsiveness.
- [ ] Finish persistent GPU component topology; retained raw state currently still materializes components each frame.
- [ ] Add deferred general guests/vehicles and other outstanding entity/animation layers, then establish full-world parity and performance.

Detailed history and the wider checklist remain in [migrated-layer regressions](vulkan-migrated-layer-regressions.md).
