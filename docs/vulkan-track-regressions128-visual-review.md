# Build 128 curated track baseline visual review

Reviewed 24 completed original-art views: 64 specimens, four rotations, zooms 0/1, with transparent water, opaque water, and underground-inside mode. This records build 128 at checkpoint `50f16fb7e5`, before the physical-depth replacement. It does not qualify builds 129 or later.

The restored track bodies are visibly present throughout the reviewed specimens. I found no additional whole-piece omission or broad water-filter/tint defect. Exact comparisons still **fail** in all three corpora. Physical supports and portal scrolling text remain missing; the accepted technical-border skirt is also visible. None of those differences were removed from the comparisons.

## Evidence and review coverage

References are `track-regressions-upstream-corpus-02`, `track-regressions-opaque-upstream-corpus-01`, and `track-regressions-inside-upstream-corpus-01`, under `obj/vulkan-parity`. Native runs use the corresponding `track-regressions128-art-01`, `track-regressions-opaque128-art-01`, and `track-regressions-inside128-art-01` artifacts. Their completed captures report no Vulkan validation diagnostics; this is separate from the failing raster comparisons.

I opened all 72 overview sheets enumerated in `track-regressions128-review/review-pages.json`: 32 transparent sheets covering 512 groups, 32 inside sheets covering 512 groups, and eight sheets covering the 36 opaque groups with different strip PNGs. The other 476 opaque strip PNGs are byte-identical to the corresponding transparent strips. The different opaque groups comprise all eight views of water specimens 61–63, plus neighboring-water intersections in specimens 46–48 and 55. This reuse is evidence deduplication, not a pixel tolerance or exclusion.

I also opened all 24 `water-{transparent,opaque,inside}-r{0..3}-z{0..1}.png` detail sheets. Each contains the original upstream/native/exact-difference triptychs for specimens 61, 62, and 63, giving 72 water comparisons. Zoom 1 is enlarged with nearest-neighbor sampling only. The captured pixels and original reports were not modified.

The fixed rectangles overlap and sometimes clip the ends of long multi-sequence pieces. Neighboring specimens often appear in a rectangle, especially beside stations and water channels. Therefore this is a visual review of the supplied 1,536 specimen groups, not proof that each whole piece or every pixel matches. The overview sheets retain complete supplied reference/native regions but are downscaled for presentation; individual station, curve, and water triptychs were used to inspect suspicious details.

## Findings

| Specimens | Observation |
| --- | --- |
| 0–16, 49–50 | Wooden and Classic Wooden turns, banks, banked curves, helixes and diagonal shapes appear with the expected red/yellow rails and wooden body layers. Tall white physical support frameworks are absent in native output. |
| 17–32 | Junior and Water sloped three-tile curves and eighth transitions are visible in all camera views; no newly omitted rail family was identified. |
| 33–36 | Multidimension station rails and Wild Mouse station/curves are present. The Multidimension covers-only station does not have the ordinary solid platform in the upstream reference either. Missing portal support structures and scrolling glyphs remain visible differences. |
| 37–48, 51–58 | Compact/Inverted/Multidimension slopes, banks, chain variant, diagonal flat/brake specimens and Classic Stand-up diagonal transitions are visible. Large differences in some diagonal rectangles come from neighboring elevated station structures, not the diagonal rails themselves. This does not waive smaller residual pixels. |
| 59–60 | Log Flume curved channel bodies and water are visible with matching overall position and shape. Physical supports remain absent. |
| 61–63 | All three Wooden water-splash channels retain their rail/channel silhouettes and water in transparent, opaque and inside modes, at both zooms and all rotations. No channel-wide wrong palette, missing water fill, doubled filter, or dropped rail section was apparent. Small difference specks occur along support-contact boundaries; they remain unresolved pixel differences rather than being declared exact. |

The inside views show the dark ground and green grid. The restored tracks remain visible against that ground; no recurring black interior or repeated-filter darkening was identified in these elevated track specimens. These scenes do not replace the separate buried-track/tunnel corpus.

The narrow exterior skirt remains especially prominent in inside-mode boundary rectangles. Its user-authorized difference must not be extended to rail, filter, support, portal, or interior-terrain differences.

The saved RGB localization diagnostic finds 96 groups with changed red/yellow-like reference pixels. That heuristic also selects brown portal supports and neighboring buildings, so it is not a count of missing rails and is not an acceptance mask. Visual review did not identify a new whole-piece rail omission from those regions.

## Exact results retained

All 512 fixed rectangles in each mode have at least one differing indexed pixel. Summed differences are 1,441,131 transparent, 1,437,555 opaque, and 1,778,260 inside. These are overlapping-rectangle sums, not distinct full-frame pixel totals. The full-frame reports and all residual differences remain authoritative and unchanged.

This finite corpus covers authored static pieces and the chosen chain/brake flags. It does not establish running-train behavior, every track state/type, all physical supports, arbitrary intersections, or later physical-depth correctness.

## Immutable receipt hashes

All paths below are relative to `obj/vulkan-parity` and hashes are SHA-256. Specimen summaries pin their manifest and source indexed-buffer inputs.

| Artifact | SHA-256 |
| --- | --- |
| `track-regressions128-specimens-01/summary.json` | `eb66b9eeba31a01df02ce3910bc57876ed15f428bcbe2751bb6742bb3bcd4e57` |
| `track-regressions-opaque128-specimens-01/summary.json` | `9d1c8eb98ffe176954e685af95d283848f2b9d8c7cf37501d96c702f39830aa5` |
| `track-regressions-inside128-specimens-01/summary.json` | `6848e5d20f85a02511dfc41fdcabe21c17af901b731e68e7c13c8f8bb67edf68` |
| `track-regressions128-review/review-pages.json` | `6ff266b5c760c68b965d707ad680e31221ff15483aab8b2a72f61a95dbc60044` |
| `track-regressions128-review/colour-localization.json` | `bc33a1297a07eec2cdce9de408fa6d3a2cd08c4a1e7b27edb485fc805cf2001a` |

Only saved artifact inspection and offline image composition were performed for this review. No game, build, GPU test, or source edit was executed.
