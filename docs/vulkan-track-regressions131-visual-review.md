# Build131 track physical-depth visual review

This is a failed correctness comparison with identified regressions, not deployment qualification. The physical-depth change preserves most isolated track artwork in this finite corpus, but loses water-splash rails and changes station/entrance occlusion. No exception is authorized by this review.

## Evidence and coverage

Reviewed the completed build131 transparent, opaque-water and underground-inside corpora against both their original upstream references and build128. Each mode contains 64 specimens across four rotations and zooms0/1: 512 groups per mode, 1,536 total. The upstream sources are `track-regressions-upstream-corpus-02`, `track-regressions-opaque-upstream-corpus-01`, and `track-regressions-inside-upstream-corpus-01`, under `obj/vulkan-parity`.

All 68 sheets listed in this directory's `pages.json` were opened and manually inspected. Each panel is reference / build128 / build131. The offline script compares complete saved specimen PNG bytes before deduplication: 302 groups per mode (906 total) are unchanged from the previously completed128 review. Of the 630 changed groups, 174 opaque-mode reference/old/new comparisons duplicate transparent-mode evidence exactly. The remaining 456 unique changed groups were inspected on 30 transparent, eight opaque and 30 inside sheets. This accounts for all1,536 groups without treating approximate similarity as identity.

The panels retain the existing full specimen rectangles. Zoom1 is enlarged2x with nearest-neighbour sampling; zoom0 remains native in the generated sheets. The image viewer resized some large sheets for display. Rectangles sometimes contain neighboring specimens or clip a long multi-sequence piece, so their mismatch totals are neither independent pixel totals nor proof about every pixel of a whole piece. No background mask, tolerance, pixel edits or new rendering execution was used.

## Actionable findings

1. **Water-splash composition regresses in all72 target views.** Specimens61/62/63 (Classic Wooden9, Classic Wooden Twister10 and Wooden79, type117), all four rotations, both zooms and all three modes now have a rectangular water surface covering the near rail/fence and substantial parts of the end ramps. Those parts remain visible in the upstream reference and build128. The far fence usually survives, making the channel look like an open rectangular trough. This is lost previously rendered artwork, not deferred support structure or the accepted technical-border skirt. Clear examples are `transparent-objects-r0-z0-04.png`, `transparent-objects-r3-z0-03.png`, and every opaque sheet. Inside mode has the same loss, so the issue is not confined to transparent-water configuration. Neighboring splash pieces also appear within other specimens' rectangles; those appearances must not be counted as additional faulty track families.

2. **Stations and neighboring portals acquire depth/occlusion errors.** Specimens33 (Multi53 station) and34 (WildMouse67 station) change in every camera/zoom and mode. Station rail/platform strips extend across entrance wall faces that the reference and128 correctly cover; the near platform/fence relationship also changes. Rotation3 zoom0 is especially clear in `transparent-objects-r3-z0-01.png`: the platform cuts through the front building. The analogous inside sheet repeats it. This requires a component-surface/occlusion correction, not an omission waiver for physical supports or sign glyphs.

3. **Smaller changed rail-boundary/contact pixels remain unqualified.** Curves, banks, helixes and diagonals generally retain their visible bodies, colour and orientation. The changed sheets do not show another broad whole-piece disappearance. Small boundary/contact changes remain in the exact comparisons; the overview inspection does not establish that each is correct. Some normal-view mismatch reductions come from map-edge pixels and should not be described as improved rail parity. Inside grids remain visible and their large pre-existing skirt/background differences remain; no new broad interior-black failure was observed in these sheets.

Physical supports and entrance text remain absent where already recorded for128. The accepted outer skirt is only that specific prior visual exception. It does not excuse either new regression above. Correcting these issues needs fresh matched captures; neither build132 nor later changes are assessed here.

## Exact rectangle counts

All512 rectangles in each mode retain nonzero differences from the original reference. Counts include overlapping rectangles, supports, background and known omissions; they are diagnostic totals, not an acceptance score.

| Mode | Build128 differing indexed pixels | Build131 | Changed groups with more / fewer differences |
|---|---:|---:|---:|
| Transparent | 1,441,131 | 1,519,010 |136 /74|
| Opaque water |1,437,555|1,514,537|136 /74|
| Underground inside |1,778,260|1,879,815|210 /0|

## Immutable identities

SHA-256 values for the evidence manifests (paths relative to `obj/vulkan-parity`):

| File | SHA-256 |
|---|---|
|`track-regressions131-specimens-01/summary.json`|`859e978c63d59d1a8a9ed5e85e121b255c427e48927d66c4448dbc66cfe350f4`|
|`track-regressions-opaque131-specimens-01/summary.json`|`e3b3334267866f509570eff755d1b8b20c0797bfc03a5bc64d8dfd141db8e36e`|
|`track-regressions-inside131-specimens-01/summary.json`|`76c9d6a1a95296294211e07f269b1cba58c9d9fe4c145c08ad324dd19c559d16`|
|`track-regressions131-review/comparison.json`|`8c65224bb9c1132e17eccd41af57ddb4499bf75ace37f373e5b84efa297a6c7f`|
|`track-regressions131-review/pages.json`|`e444dfd31aff6d5d17b57f673b2340afa562d9c4f602432f8a443aa80669091f`|

The128 baseline review and its receipt identities remain in `track-regressions128-review/vulkan-track-regressions128-visual-review.md`. Saved capture and comparison inputs were not altered. Only offline review sheets, analysis JSON and this staged document were created.
