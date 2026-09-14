# Stable-baseline migration review

Reviewed against stable tag `stable-before-upstream-2026-09-13` (`e874ceb7708974e00040419d30390686c52a5d31`). Frozen review checkpoint: U210, `123d6bb6af4202c520a98801915d4f6de6a83814`. This accounts for **210/361 source commits, with 151 remaining**. The worker continued beyond this checkpoint during review. Findings below concern committed code at this fixed point unless explicitly described as pending.

## Assessment

The combined migration remains aligned with the mod's design. Most of its apparent size comes from renames, typed flags, namespace changes and header moves. It is not a wholesale replacement of the fork's gameplay or rendering implementations. The comparison touches 991 files, including 922 source files and 20 test-related files. Of 374 source paths originally changed by the fork, 128 remain byte-identical to the stable checkpoint.

At the frozen checkpoint there was one new concrete integration issue, O05 below. It was subsequently corrected in U212 and verified in Review 17. The main remaining risk is behavior at boundaries between systems, especially temporary rendering state and persistent plugin references. Compilation and rename proofs alone do not establish those contracts. This is an interim review, not certification that the branch is ready to replace the stable build.

## Fork design preserved

| Area | Evidence against the stable checkpoint | Assessment |
| --- | --- | --- |
| Ride age and value | The complete `RideRatingsCalculateValue` function has identical tokens, including the 1.5x/1.2x age rows, duplicate-ride adjustment, money conversion and target-price update. | The rejected upstream additive age-price model has not returned. |
| Sampled ratings | `RideRatingGetLocalContextEnvironment` and `RideRatingPrepareLocalContextScore` are token-identical. Earlier incremental reviews checked descriptor renames and preserved private profiles. | The sampled/context model remains the owner of ratings. This is stronger evidence than matching a few constants, but not an exhaustive behavioral proof of every ride. |
| Park growth | `CalculateParkRating` is token-identical and derives rating from average guest happiness. `CalculateGuestGenerationProbability` retains its numerical formula; its changed condition is the typed difficult-generation flag. | No legacy population cap has been reinstated into the arrival formula. |
| Money and time | `Money.hpp` and `GameTime.hpp` are byte-identical. Target pricing retains directed transport-journey handling and numerical policy. The moved weather preview uses `SecondsToTicks(30)`. | Cent precision and 40 TPS remain consistent across the inspected boundaries. |
| Stations and transport | Station passenger handling changes are names only. The route cache changes its entrance type and comparisons without replacing its routing algorithm. Incremental reviews preserve station margins, exact seats, platform substates and directed fares. | The public physical-path API does not replace the guest journey planner. |
| Rendering and audio | All 28 Vulkan files, `PresentationGeneration.h` and core GPU command/mailbox files remain byte-identical. Reviewed audio mixer changes are enum spelling. | Upstream UI changes continue through the fork's presentation architecture. Live visual/audio testing is still needed. |
| Save and network contracts | The private park format remains 60016. Reviewed packed fields retain widths and order. Network flavor remains `andersonhk`, independently advanced to revision 11; plugin API is 120. | Upstream API adoption is separated from private save and network identity. These numbers alone do not prove live multiplayer compatibility. |

Approved gameplay changes remain identifiable rather than hidden in refactors: the four-staff-type award rule, expanded clearance-cheat behavior, separate scenery-clear masks, and ownership API normalization are documented decisions. The ownership port explicitly excludes upstream's land-pricing predicate regression. UI fixes such as pickup availability and wall action result height preserve their underlying eligibility, placement and cost rules.

## O05 — preview activity invalidates unchanged live path references

**Closed in U212 (`65c3da47955524f76f43367da8f3dbee9dfdc692`), independently verified in Review 17.** The following describes the defect at the frozen checkpoint. U204 adds structural tile revisions to prevent a stale navigator from silently referring to a different same-height path. That purpose is sound. However, it also permanently invalidated live references around temporary visual substitutions:

- Construction-button painting calls `DrawTrackPieceHelper`, which temporarily replaces technical tiles near world coordinates `(4096, 4096)` using `ScopedTileIndexOverride`. These include real tile `(128, 128)`. Exiting the scope restores the same live elements but assigns another revision.
- `TrackDesignDrawPreview` uses `StashMap`/`UnstashMap`. Both reset all revisions, so viewing a track-design preview expires every retained live navigator on that client.

Consequently, an unchanged park can return different navigator results depending on local UI activity. A multiplayer script retaining references could also observe different validity between clients; an actual multiplayer desynchronization has not been reproduced.

Requested repair: restore the original live reference identity after balanced visual overrides and map stash/restore, while invalidating references acquired to temporary preview elements. Keep the revision allocator monotonic, handle nested tile scopes, and preserve genuine structural-edit invalidation. Add focused lifetime regressions. No gameplay design decision or user approval is needed for this correction.

Resolution: U212 saves/restores live tile revisions with scoped overrides and the stashed map, without rewinding the allocator. Reviewed regression coverage exercises all five construction-preview coordinates, nested and repeated scopes, retained connections, temporary-reference expiry, genuine replacement and full-map preview restoration. The unfiltered Windows checkpoint after this correction passed 585 tests in 50 suites, with no failures/errors/disabled tests. That full run preceded U212's later downloader changes; the completed combined U212 state separately passed 108 selected tests. Actual rendered preview and live multiplayer behavior remain unverified.

The broader documented lifetime rule remains conservative: real insertion/deletion/reordering anywhere on the same tile invalidates a navigator. That differs from upstream's ambiguous same-height fallback and is disclosed in the plugin declarations. Its implementation should stay independent of guest routing caches and serialized state.

## Dependency and implementation cohesion

The objects correction now follows the owning repository: `assets.json` pins companion fork commit `b2a5511cd7ff90646dd40c61f08cb27fa977e8f4`, and build/install paths use its tracked source assets. The installer rejects competing packed object IDs and stale managed files before writing; it does not reapply hardcoded kart calibration to an upstream download. The four kart settings remain data-owned. Content diffs in the companion checkout are empty; its numerous stat-cache status entries do not represent content edits in the sampled files, which match their committed bytes.

The dependency pin is still local according to the migration record. A fresh checkout elsewhere is not release-ready until that revision is published and the documented companion checkout is available. No push or deployment was requested or performed.

The UI migration is intentionally incomplete in historical order. Park/date panels are present, while the later news/status restructuring and custom-theme conversion are due at U338/U340. Interim theme appearance should not be mistaken for the final product. The missing park-rating redraw (O04) is corrected in U199, with all five relevant intent bodies independently matching source afterward.

Earlier corrections reveal a useful pattern: protocol reset policy, wrong asset provenance, an omitted redraw and now preview/reference lifetime each require reasoning across owners. Small, locally plausible patches and passing targeted tests did not catch all of them. Continue inspecting dependencies, mutation boundaries and behavior after temporary state is restored.

## Evidence and remaining validation

The oversight record verifies historical singleton ancestry and source trailers through U210. The latest seven receipts pass independently. U206's moved coordinate headers match source; remaining changes are includes, forward declarations and unchanged `Direction = uint8_t` aliases, with fork owning-header dependencies retained. U205/U207/U208/U210 are localized startup-directory, attention, displayed quote and result-coordinate changes.

Across changed test files, no existing test case names disappeared and 22 cases were added. This is a completeness signal, not proof that no assertion changed. Inspected recent logs show 276, 279 and 99 selected tests passing in B42/B43/B44; these overlap and must not be summed as distinct coverage. The review did not run competing builds while the worker was active.

Requested the next coherent checkpoint after O05 for one full available Windows suite, retaining lightweight per-commit validation. Before calling the result stable, consolidate and exercise the outstanding representative cases: custom-theme migration, actual HUD/rain/input behavior, same-build multiplayer and replay, save/import round trips, and a clean install using the pinned companion assets. Native non-Windows and full disabled-scripting builds remain explicit gaps. A representative longer park simulation and performance comparison against the stable build would address evidence that short targeted tests cannot supply.

No further owner decision was identified in this review. Continue the authorized migration; O05's committed correction is now verified, with remaining validation gaps tracked above.
