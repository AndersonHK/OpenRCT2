# Plan B review checkpoint — September 23, 2026

Plan B now has an implemented retained peep publication foundation and actual-device shader selection tests. It does **not** yet have an admitted mixed-world renderer, a demonstrated gameplay speedup, or a qualified fix for the reported cherry/track flicker. The installed checkpoint52 is unchanged. The owner requested a manual review after this turn; the long-lived goal stays paused.

Build64 passes with zero warnings/errors. Run51 passes **836/836 production regression tests**, including 66 image-parity cases, with clean synchronization validation. One explicitly named experimental mixed-renderer test is excluded from that gate and run separately; it still fails exact parity. No existing regression requirement was disabled. Source changes are uncommitted for review.

| Work | State | Evidence / remaining requirement |
|---|---|---|
| Immutable raw peep state | Implemented, explicitly selected profile | 96-byte records, chunk sharing, generations and deletion tombstones; no selected images or screen coordinates in simulation records |
| Shared dirty publication | Implemented | One producer prepares and acknowledges existing dirty work; old snapshots survive skipped consumers and identity reuse |
| Animation rule ownership | Implemented | Immutable object facts, mutation-boundary catalog updates and reload generations; sprite/atlas lifetime leases remain open |
| GPU image selection and projection | Device-qualified, bounded | Frozen rule oracle plus actual shader readback; mixed pixels are a separate requirement |
| Snapshot lifecycle repairs | Implemented and tested | Complete cold map bootstrap, unique publication epochs, coordinated synchronous retry |
| Stationary appearance changes | Staged only | Mutation guards need cost measurement with an active benefiting renderer |
| Mixed world visibility/order | Experimental, rejected for admission | Initial scalar fails exact pixels; geometric refinement is evaluated separately |
| Reported cherry/track/vehicle ordering | Open | Tested tree scenes do not reproduce the reported canopy error; global stable-sort switch withdrawn |
| Large-park speedup / displayed pacing | Open | Existing clean baseline: 67.318 TPS and 28.879 application draws/sec at actual 4K; no Plan B speedup claim |
| Deployment | Held at checkpoint52 | No unqualified renderer prototype installed |

## Final 4K measurements

EverythingPark, identical saved camera/configuration/assets, actual 3840 × 2160 on the 144 Hz display, VSync on. Each run has 100 separate warmup ticks and 3,000 measured ticks; the four runs total **12,000 measured ticks** in control–candidate–candidate–control order. Initial/final simulation censuses and final checksum match in every run.

| Run | TPS | Application draws/sec | CPU draw | GPU frame | Frame interval p95 / p99 |
|---|---:|---:|---:|---:|---:|
| Build56 control 1 | 71.125 | 29.920 | 25.806 ms | 1.925 ms | 35.690 / 36.434 ms |
| Build64 candidate 1 | 74.524 | 28.915 | 26.330 ms | 2.917 ms | 35.909 / 36.305 ms |
| Build64 candidate 2 | 69.747 | 28.875 | 26.664 ms | 2.702 ms | 36.193 / 36.627 ms |
| Build56 control 2 | 71.533 | 28.828 | 26.581 ms | 2.898 ms | 36.202 / 36.593 ms |

**No substantial improvement is established.** Candidate TPS varies across the control range, and the ordinary CPU preparation remains expensive. GPU timing also changes across the two controls. Two samples per build and differing draw counts do not establish a causal speedup or absence of every performance regression. These hidden-window application intervals do not measure displayed VSync pacing. The retained mixed renderer is disabled in these runs, so they test the foundation's ordinary-path behavior, not Plan B performance.

The consolidated receipt is `obj/vulkan-parity/plan-b-checkpoint-performance.json`; each named run retains the exact binary, asset/configuration provenance and original log. The final executable/shader snapshot is `obj/vulkan-parity/performance-build64`. Optional headless ceiling attempts were rejected at command-line parsing because `simulate` does not accept the ordinary launcher's isolated-profile options; neither produced a simulation result or changed the user profile.

## Rendering result

The geometric diagnostic fixes the earlier beam-through-rail and balloon overlaps in its controlled original-art scene. Four of eight canonical views are pixel-identical; affected views retain 2–6 ambiguous foot/path contact pixels. An agent reviewed all scene pairs and differing regions, with no exception approved. The relocated test-only pipeline was rebuilt and rerun as `plane-run-02`: all 192 binary artifacts, including GPU/reference pixels and command/status buffers, match the already reviewed run01 exactly. This remains a strict failed diagnostic and is not installed.

The natural-tree/cherry views did not reproduce the owner's reported canopy defect. All twelve frames within each paused run were identical; small changes were between separate ordering-policy runs, not animation advancing during pause. The global stable-sort change was withdrawn because it does not fix those samples and damages an independent foreground guest/support overlap.

## What is retained and why

The publication work creates the approved boundary between simulation state and rendering: compact owned records and asset rules instead of copying complete guest/staff structures into the raw profile. It also fixes concrete publication reset failures. These are architecture and correctness improvements, independently useful before runtime admission. Ordinary rendering continues to use its existing profile.

The first mixed-scene depth formula is a rejected experiment. It must stay in explicit test tooling, outside the production renderer target. No family-wide “trees always win” rule, pixel tolerance or altered reference image is accepted. The existing optional stable sort also fails an independent foreground guest/support sample, so enabling it globally is not a fix.

The geometric experiment assigns original sprite art to bounded depth planes. Its finite fixture contains actual terrain, paths, a tree, track, supports and peeps. Success there would still require larger coverage, image leases, scalable GPU compaction, zoom/interpolation, effects and full park measurements before admission.

## Next acceptance sequence

- [x] Preserve frozen software source/assets and original-art reference production.
- [x] Implement owned raw peep publication and object-catalog lifecycle coverage.
- [x] Qualify GPU peep rules against the independent frozen oracle.
- [ ] Close exact mixed-scene ordering, with agent visual review of every divergence and explicit narrow exceptions only when justified.
- [ ] Reproduce the user's cherry/diagonal support and steep-transition vehicle cases against frozen/upstream references.
- [ ] Complete image leases and mutation hooks; measure publication cost without an additional population scan.
- [ ] Admit the complete supported scene through the existing shared Vulkan service, bypassing corresponding CPU painters exactly once.
- [ ] Demonstrate substantial improvement in counterbalanced clean actual-4K runs, each with at least 3,000 measured ticks and matching simulation state.
- [ ] Qualify displayed VSync pacing and sustained population growth separately from application draw cadence.
- [ ] Deploy only after renewed parity/lifecycle qualification; retain rollback.
- [ ] Complete remaining families, auxiliary callers and platforms, then remove software and obsolete compatibility paths.

The [execution journal](vulkan-plan-b-execution-2026-09-23.md) records failures and receipts. The [complete migration checklist](vulkan-exclusive-migration-plan.md) remains authoritative for the eventual exclusive Vulkan path.
