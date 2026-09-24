# Migrated-layer regression checkpoint

The preserved implementation checkpoint is `802f28341e`, deployed build112. It provides native static rides, portals, station pieces and expanded tracks, with two successful 4K/12,000-tick runs at358.9–359.8 TPS and143.9 FPS. User testing on2026-09-24 accepted the progress but identified the issues below. This is not full renderer acceptance.

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
- [ ] Have agents manually inspect every divergent sample group. Preserve exact comparison failures and document unresolved gaps; only the previously accepted outer-map skirt is waived.
- [ ] Run CPU/lifecycle regressions and serial4K,100-warmup +12,000-tick EverythingPark timing with four3000-tick windows, matching simulation checksum and final image. Record startup separately, CPU/GPU time, pacing and batched upload costs.
- [ ] Deploy the qualified candidate for manual construction/camera/preview testing, then checkpoint the implementation and remaining explicit deferrals.

References: [build112 results](vulkan-native-static-buildings-checkpoint.md), [underground audit](vulkan-underground-rendering-audit.md), [animation contract](vulkan-animation-state-contract.md), [track coverage](vulkan-native-track-coverage.md).
