# Constant component depth: builds136–137 visual review

**Retain build137's portal anchors and Cinema body-anchor correction. Visual parity remains incomplete.** This is an independent manual review of saved original-art captures, not a new renderer execution. The contract remains one depth for the whole authored sprite component; the remaining findings concern anchor choice and component/filter relationships, not a request to restore gradients or painter arrangement. See [the depth contract](vulkan-constant-component-depth.md).

## Evidence and scope

All **288 build137 specimen groups** were manually viewed: 18 authored scenes, four rotations, zoom0/1, normal and underground-inside modes. Native-resolution sheets contain upstream on the left and native137 on the right, without resizing, recolouring, masks or tolerances. Before137, 120 build136 groups were manually inspected, prioritizing Cinema/shop, stations, towers and portals. Build136 evidence is preserved; it is not described as an exhaustive manual review.

Artifacts are under `obj/vulkan-parity/`:

- Original references: `underground-view-upstream-corpus-01` and `underground-view-control-upstream-corpus-01`. Their manifest SHA-256 values are respectively `374d2bbb8c25485a1b1362b409d7338b5c5d20c5406f38766b43e98310a61cc3` and `29b86a9225513cc8230f194cebe22a056490a17f775a2be19078d243cbd77dfb`.
- Root-produced captures/comparisons/strips: `underground-{view,control}137-{art,compare,specimens}-01`, with matching136 evidence retained.
- Review sheets and input/image hashes: `constant-component137-review/{view,control}/audit.json`. SHA-256: inside `94022c38e3645e68ae71ad0230057d30c33a923c6b49f57c7b7c3cfd36282ea9`; normal `cda323d90baecec3cfca6309c96ed4d4af686f9e1a2030ebb304f08d9f11f7be`.
- Reproducible offline sheet preparation: `constant-component137-review/prepare.py`; previous sheets remain in `constant-component136-review`.

Build137 changed72 inside rectangles and60 normal rectangles versus136. Every changed rectangle has a lower exact indexed mismatch count; no rectangle has a larger count. The overlapping-rectangle totals fall from180,736 to151,843 inside and55,699 to30,865 normal. These are **not unique full-frame pixel totals**, nor proof that every changed pixel improved. Exact full-image comparisons still fail. Ten inside and58 normal fixed rectangles are exact.

## Keep/revert findings

**Cinema body anchor: keep for the tested Cinema.** In136 the shallow Cinema was reduced to a dome cap, with its front white entrance almost hidden in rotations1/2. Build137 restores substantially more dome and the entrance/body in every camera. Normal shallow-Cinema mismatch counts, ordered rotations0–3, change from1,902/6,932/6,910/1,885 to873/3,037/3,019/859 at zoom0, and481/1,730/1,734/470 to223/762/752/213 at zoom1. Inside views improve in all eight cameras too. The fully buried Cinema remains exactly hidden in all eight normal rectangles; its eight inside rectangles also improve. No new visible Cinema regression was identified versus136. This corpus does not independently qualify the same named adjustment for Carousel.

The Cinema is still incorrect: a foreground terrain wedge remains in rotations0/3, while a horizontal cliff strip cuts through the dome in rotations1/2. Platform-edge details also differ. The owner has classified this pathological burial fixture as **low priority**, potentially outside normal placement, rather than waived its mismatch. The trial improves the source-authored constant anchor but does not finish its footprint/foreground-component relationships. Inspect `control/item08-all-views.png`, `view/item08-all-views.png` and the corresponding item09 sheets.

**Portal back/front anchors: keep.** The apparent closed grey faces in136 become open path/coaster mouths in137 at every captured facing. The normal fully buried station rectangle (item07) is now exact in all eight views. The shallow station (item06) is exact in six views, with25 pixels at rotation0/zoom0 and6 at rotation0/zoom1 remaining. The normal coaster-portal rectangle (item04) is exact in rotations1/2 at both zooms. Normal stacked portals (item15) retain only38/43/38/43 mismatches at zoom0 and7/11/7/11 at zoom1; these are retained failures, not a tolerance. Sheets item00/01/04/06/07/15 show the opening and surrounding geometry.

The shallow shop (item10) benefits from the portal correction: rotations1/2 show the counter instead of the grey backing. Its roof remains over-occluded by terrain in all rotations. Fully buried-shop normal rendering remains unchanged. These are separate body-anchor and portal relationships; no image displacement or blanket foreground priority is justified by this review.

## Remaining visible failures and limits

- Underground grids/filter coverage still differ across paths, railings, tracks and buildings. For example, item13 normal rectangles are exact in all eight views, while their underground counterparts visibly place extra grid/filter coverage across the rails. Constant opaque depth alone does not qualify these component/filter relationships.
- Deep-tower item17 retains its square opening, but native terrain/aperture components cover more of the lower exposed tower than upstream. The broad tower remains visible. The137 portal/body changes do not alter this case's normal result.
- Elevated-path item14 still lacks physical support columns. This pre-existing omitted geometry is distinct from depth-anchor failures and remains visible in the comparisons.
- The owner accepts **underground skirt rendering** as a deliberate divergence. Those pixels remain in the unchanged exact comparison counts; no mask or subtraction was added. This exception does not cover general underground grid/filter ordering across paths, tracks or buildings, normal-view boundary differences, or the Cinema/shop anchor failures. Small residual counts are not automatically classified as harmless sampling differences.

All18 groups in both modes were inspected, including slopes, equal-base corners, deeply buried track, both tower heights and stacked paths. This finite corpus does not cover Carousel, all flat rides, every station style, all track families, selected-car motion, water/glass combinations or arbitrary neighbouring footprints. Their existing captures/tests and the new performance lane remain separate gates. No shader, production source, reference fixture or comparison threshold was changed during this review.
