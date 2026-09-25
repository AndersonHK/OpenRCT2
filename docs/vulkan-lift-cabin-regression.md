# Lift cabin admission regression

The owner reported a missing moving cabin in the real Six Flags over Texas scenario, with the shaft and platforms still present. This is an asset-admission defect, separate from the save-loading frame ownership failure fixed in `95230ad7c7` and from component depth ordering.

## Cause and correction

Native vehicle capture used `CarEntry::isVisible()` to decide whether a car had world artwork. That method tests `tabRotationMask`, which controls the rotating ride-selection UI preview. All three Lift objects omit that mask but have valid world sprite groups. The native catalog consequently excluded their cabins. This predicate dates to entity migration commit `e132aaa338`; the recent loading fix did not introduce it.

`VehiclePresentationCar::Capture` now admits world artwork using the same enabled flat-group predicate as `RideObject::Load`. It retains immutable object allocation bounds and does not add a Lift-specific exception, change the structures, or alter depth rules. The regression test exercises capture, used-car residency and catalog admission, then checks original-painter body/passenger image requests across four rotations and four restraint states. A UI mask without world artwork remains insufficient for admission.

The expanded Everything Park check caught another latent assumption: the newly admitted Launched Freefall car owns 21 world images, while its generic group metadata describes 32. Specialized painters use authored image layouts. Their residency ranges must describe those actual accesses, while the owning-object allocation guard continues rejecting invalid requests. Build197's failed Everything Park capture is retained as evidence; passing the synthetic Lift test alone was insufficient to qualify deployment.

## Reproduction and visual evidence

All captures use isolated profiles, original licensed RCT1/RCT2 artwork, frozen upstream comparison, and silent offscreen rendering. Coordinates below are world XYZ, never screen coordinates. Camera rotation is varied through 0–3, zoom 0, output 768×768.

| Fixture | Camera world XYZ | Target |
| --- | --- | --- |
| Everything Park, ride197 | 4304,6352,432 | Standard Lift |
| Everything Park, ride490 | 5296,4688,432 | Mine Lift |
| Everything Park, ride491 | 2832,5840,432 | Teleporter |
| Six Flags over Texas, ride1 | 912,3824,1014 | Cabin entity94 at the upper stop |

The Texas scenario is `Scenarios/Six Flags over Texas.SC6`, not its separate “Build your own” variant. SHA256: `229cf5777d1b90034f3b711eed45c4d6ab9a368a466fd4db758a1eac21632a35`. The Lift occupies tile(28,119), with stations at worldZ112 and1008. Read-only locator metadata is preserved in `obj/vulkan-parity/lift-texas-decoded/locator.json`.

An agent manually inspected build196's standard Lift at all four rotations and the Mine Lift/Teleporter at rotation0. Standard and Teleporter cabins are visibly absent beside preserved structures. Mine rotation0 is heavily obscured, so that image alone does not establish cabin visibility. Build198's twelve Everything Park comparisons were all manually inspected: all three cabin variants are restored, while the structures remain present. Evidence: `obj/vulkan-parity/lift198-paired-01`. All four final Texas captures complete with clean validation and are byte-identical to the manually reviewed197 Texas images (`lift198-texas-paired-01`). These comparisons do not claim whole-scene pixel parity.

The remaining depth error is now easier to see: the standard cabin roof/front covers foreground shaft braces in rotations2/3; Mine Lift does likewise in rotations1/2/3; the Teleporter obscures a pale-green near brace at the lower platform in rotation0. Texas also has surrounding deck/railing discrepancies. The concrete source lead is that the two Lift cage parents have distinct authored rear/front bounds but no explicit depth anchors; `worldSetPaintBounds` deliberately does not derive depth from bounds. Their physical rear/front contacts therefore need explicit semantic ownership. This is a diagnosis for the next checkpoint, not an implemented or visually qualified ordering correction.

## Qualification checklist

- [x] Identify the omitted cabin separately from the intact structure and reproduce against upstream.
- [x] Replace UI visibility with world-art eligibility; add capture/residency/original-painter regression coverage.
- [x] Build197 and 33 focused tests pass; retain its failed Everything Park admission as unqualified evidence.
- [x] Build198 corrects only Freefall's proven21-image domain; other specialized unions remain unchanged. All33 focused tests pass, including undersized-allocation rejection and an oversized generic-car control.
- [x] Capture the final candidate's three Lift variants and exact Texas case; manually inspect the divergent samples. All16 captures complete with unchanged inputs and no Vulkan validation diagnostics.
- [ ] Resolve the Texas Lift/deck depth contacts separately: shaft/cage parts and surrounding observation-deck scenery still overlap differently from upstream. The owner explicitly requested this case remain in the comprehensive ordering backlog; cabin admission is not acceptance of its depth ordering.
- [x] Run a silent4K Everything Park performance check:100 warmup +12,000 measured ticks,360TPS cap, VSync,144Hz output. Receipt: `obj/vulkan-parity/performance-entities198-12000-01/summary.json`. The run completes with the expected `07d58eaefde6aa6d000000000000000000000000` simulation checksum and unchanged executable/shader inputs.
- [ ] Recover and qualify performance before treating this as a performance-approved deployment:228.279TPS,116.080 accepted presents/sec,1.057538ms mean CPU drawing,4.927622ms mean GPU; accepted-present p99<=26.8ms, maximum116.5007ms. All6102 submissions present and complete with zero discarded packets. This is below the objective and below196's315.279TPS/137.803 accepted presents/sec. Another session was working on an unrelated regression during this turn, but no controlled A/B or scheduler trace establishes the cause of this slowdown. Do not attribute it to that session or to this admission fix without measurement.
- [x] Record the requested source checkpoint and remaining regressions. The owner requested stopping at a checkpoint while another session handles pathfinding; its files and test-manifest changes are excluded from this graphics commit.
- [ ] Deploy after the next qualification. The manual-test installation remains at196;198 was not deployed or launched interactively.

Remaining scope: sloped-path railing/guest overlap, elevated helix/booth and large-loop contacts, Texas custom-scenery ordering, and sustained360TPS/144 accepted presents per second remain open. This fix does not waive those gaps or the broader pixel-parity requirement. Map/underground skirts retain their previously accepted status.
