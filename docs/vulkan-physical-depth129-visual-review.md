# Physical world depth prototype 129: visual review

Build129 demonstrates the new hardware-depth path, but does not have original-art visual parity. This review covers saved artifacts only; the reviewer ran no game, GPU workload, test or build. It does not qualify later130/131 changes.

## Scope and evidence

Manually inspected all288 specimen triplets:18 fixed cases ×4 rotations ×2 zooms ×2 modes (underground and ordinary control). All96 original three-case pages were assembled into16 nearest-neighbour contact sheets and viewed; the cinema and raised-corner examples were also inspected at original resolution. Contact sheets establish shape and major occlusion coverage, not an assertion that every differing pixel was individually classified. Each original item contains upstream/native/exact-diff panels. The summaries remain exact failures with no masks, tolerance or accepted new exceptions.

Artifacts live under `obj/vulkan-parity/physical-depth129-underground-review/`:16 viewed contact sheets; `specimen-hashes.json` pins all288 original item PNGs; `page-hashes.json` pins all96 source pages; `comparison-counts.json` records each fixed rectangle's exact reference mismatch count for125 and129. Rectangles overlap, so counts must not be interpreted as unique full-frame pixels.

| Evidence | SHA256 |
|---|---|
| underground-view129-specimens-01/summary.json | 753f6092ed0866d923fe4a220239a88cf24295c72256f39775be7c71f8a4d3ca |
| underground-control129-specimens-01/summary.json | 38497ec255e97907f637c9ea689238653f4662a12e96ab2c53eb555c0e839721 |
| specimen-hashes.json | 1a804cb088aea4b9d513c053e9824af6ae94be015f6e7b7f4b7f0a050298983e |
| page-hashes.json | 94c9411bd8e2f11c1ca208d1b83f55e9d5338c897f58e41d18c9b3480d3a78c7 |
| comparison-counts.json | b02c7dd812515a42cfe8bef6ae8617ca63005cdfcd5a414deb83dcef8a376b64 |

## Findings by case family

- **0–1, flat paths through terraces:** portals and broad terrace geometry are present in both modes/all views. Path ends and portal/terrain intersections differ. Interior issues are distinct from the previously accepted exterior cliff skirt visible at some crop edges.
- **2–3, path ramp and path under slope:** rail/path fragments and exposed soil wedges differ. The slope is still assigned the prototype's flat draw-height depth plane, so its visible sloping pixels do not have matching physical height.
- **4–5, flat and ascending coaster:** ordinary control loses substantial exposed flat rails and some ramp intersections. Underground retains the broad rail geometry but has changed terrain/filter overlap. Coplanar land and rail planes both had layer0 in129; this is a concrete explanation for flat-rail erasure, not an explanation for all sloped intersections.
- **6–7, partially and fully buried stations:** station bodies remain visible in underground mode, with changed edge/deck intersections. Both were exact in all125 inside/control fixed rectangles;129 introduces visible differences. General station or tunnel parity is not established.
- **8–9, partially and fully buried cinema:** the dome is present, but terrain/filter geometry obscures too much of the lower white building and podium. Inside r1z0 item08 was exact in125;129 visibly substitutes terrain over its white front wall and floor. Ordinary r1/r2 also lose building areas that extend beyond the foreground terrace. This is a new depth-model defect, not the exterior skirt or missing source art. Across8 inside views, item08 mismatch counts rise9,930→36,900; control800→20,830. Counts include prior peripheral differences.
- **10–11, shop:** the partially buried shop loses much of its visible cap/base in ordinary and underground views. Fully buried ordinary shop remains hidden as expected; the inside view introduces new structure/filter differences.
- **12–13, equal-base path/track on raised corners:** clear loss of rails/path surface and changed slope/soil overlap. These combine coplanarity and the flat-depth approximation for sloped terrain. The corresponding125 inside track rectangles were exact;129 totals16,978 differing pixels over8 views. Ordinary control totals21,415.
- **14–15, terrace-top and stacked paths:** surface fragments disappear into terrain, while elevated path spans remain recognizable. Missing physical supports were already an open limitation and are distinguishable from the newly missing deck pixels.
- **16–17, shallow/deep towers:** shafts and broad platforms remain recognizable, but platform/grid/cliff intersections differ. Some ordinary control views show small rectangular ground openings unlike upstream. These are internal geometry differences; they cannot all be assigned to the accepted exterior skirt.

The dark underground backing remains present. This review did not find a return of the old missing-background failure. It does not test UI windows or selected vehicles.

## Concrete next corrections

The already-staged130 coplanar overlay correction targets flat rails/decks on land, and the true X/Y cliff-face patch targets edge depth. Neither has been visually verified by these129 images. Wait for131 captures before deciding which residuals remain.

The next independent geometry correction should use the owned raw slope/corner heights to construct piecewise planar terrain depth. A single horizontal plane at drawZ cannot describe raised-corner terrain. The cinema/shop failure needs finite building footprint/face depth: current upright art-anchor planes extend across the whole sprite, while original body art contains floor, walls and overhang. Explicit authored faces or a finite footprint-derived proxy can be tested against cases8–11 without introducing a family draw-last priority. Legacy paint bounds are not automatically a physical mesh; any proxy remains a hypothesis until the same saved corpus demonstrates it.

## Real-park comparison

Viewed the full3840×2160 benchmark images and three side-by-side crops under `obj/vulkan-parity/physical-depth129-visual-review/`: flat-track rows, front cliff and slope/crossing. The128 comparison is the prior native renderer with the matching source recipe set, not a newly captured upstream oracle. Many ground tracks and paths disappear into grass in129; elevated silhouettes broadly remain. Top and bottom UI remain unobscured in these screenshots. The thin tall foreground cliff needle already exists128, so this depth prototype did not create it. The nearby yellow crossing rail changes from continuous128 to fragmented129, consistent with coplanarity.

| Benchmark PNG | SHA256 |
|---|---|
| performance-control128-world-profile-01/final-benchmark.png | 273bf31c9fa67ebe066a42d898b2abf270446a32834fb7c73ac6735ba4db4e7d |
| performance-physical-depth129-world-profile-01/final-benchmark.png | 3e1b1df0f1f40c1e487af54a2a0a9b3641792e1c310f22c2a3c2afd81b921ced |

Performance results belong to the root's measured receipts. This visual review makes no acceptance claim for moving vehicles, animation, every original-art object, exact world occlusion, or persistent owner-dirty component generation.

## Follow-up131: cliff faces and coplanar overlays
The completed131 inside/control capture set was compared against129 and125. All288 original-art triplets were covered again through16 manually viewed contact sheets;42 item PNGs are byte-identical129 and246 changed. The saved `physical-depth131-underground-review/comparisons.json` records every case and `hashes.json` pins all288 item images. These are complete-case exact mismatch counts; no changed pixels were waived. No source changes or runtime execution were performed during this review.
Flat ground rails/path decks visibly recover in131. The ordinary-control flat coaster-through-terrace count falls4,900→1,582, matching-path-ramp7,672→1,616, and stacked paths15,507→3,624 (sums across8 views, overlapping rectangles possible). This supports the coplanar overlay correction. It does not establish exact cliff/portal or slope geometry.
The remaining image failures for this checkpoint are:
- **Sloped terrain and raised-corner intersections:** items3,12,13 in every rotation/zoom. Ordinary r0/r3 show changed triangular soil/grass wedges; inside views show cliff/grid strips in front of the buried path/track. Flat rail recovery is clear, but the slope still has a flat depth plane.
- **Cinema and shop body geometry:** items8–10 remain visibly wrong. The white cinema lower body/podium remains covered by terrain/filter grids, especially inside r1/r2 and ordinary r1/r2. A dark podium patch is now exposed in some131 ordinary views while the wall is still absent. The fully buried inside cinema item9 also retains the lower-body loss. These require finite structure/footprint depth rather than another coplanar layer.
- **Stations and tunnel mouths:** items0,1,4–7 retain small portal/deck/terrain intersection differences; partly/fully buried station rectangles were exact125 and remain inexact131. Their broad geometry is present.
- **Terrace-top and stacked path support/edge details:** items14/15 recover much surface area but have remaining edge intersections; original physical support pixels remain absent in native output. The support omission is distinct from recovered deck geometry.
- **Tower bases:** items16/17 retain platform/grid differences inside and small rectangular terrain openings in ordinary view. The shafts are present; there is no basis to classify all internal differences as the accepted outer-map skirt.
- **Thin terrain outline seams:** some131 views introduce long one-pixel diagonal outlines in fixed rectangles. They are visible in exact-diff panels and remain unqualified; the new true-face0/32 boundary differs from original raster attachment offsets, and a coordinate-level seam check is required before attributing them entirely to outer crop edges.
The dark underground backing remains intact. No new narrow patch was applied: the clearly identified coplanar defect is improved, while remaining slopes and finite buildings require explicit geometric data rather than a guessed family priority. The raw-slope piecewise plane and finite building-face proposals remain the next targeted work.
### Exact residual count inventory
Each row sums8 original-art comparisons. Counts include peripheral differences where fixed rectangles overlap the exterior; they do not measure unique full-frame errors.
|Mode|Item|125|129|131|
|---|---:|---:|---:|---:|
|view|0|14895|20669|18697|
|view|1|9930|16904|14721|
|view|2|9930|15140|13945|
|view|3|9930|22729|25114|
|view|4|9930|14517|12938|
|view|5|0|3560|1523|
|view|6|0|6286|5582|
|view|7|0|6387|5309|
|view|8|9930|36900|37898|
|view|9|0|13433|12068|
|view|10|0|8628|6457|
|view|11|113|4351|2234|
|view|12|9930|30025|30720|
|view|13|0|16978|18014|
|view|14|4182|11134|8602|
|view|15|0|10436|8322|
|view|16|11409|22455|20816|
|view|17|5064|17803|15298|
|control|0|1200|5671|2192|
|control|1|800|6256|1974|
|control|2|800|7672|1616|
|control|3|800|11657|11367|
|control|4|800|4900|1582|
|control|5|0|901|624|
|control|6|0|3879|3057|
|control|7|0|1749|2404|
|control|8|800|20830|19315|
|control|9|0|1497|655|
|control|10|0|5745|5637|
|control|11|137|137|905|
|control|12|800|25911|15577|
|control|13|0|21415|11574|
|control|14|4790|11046|7594|
|control|15|0|15507|3624|
|control|16|3349|6585|6917|
|control|17|2656|4931|5731|

131 review inventory hashes:
- `comparisons.json`: `31a8ccab6036590115b233945b91fbe3d53e0b2d19c58815af3977d52d8189d8`
- `hashes.json`: `8410abef163e43ef228d0e841190b00cb0504f321952ad546050cfdefa3bf762`
