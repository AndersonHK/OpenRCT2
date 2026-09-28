# Guest ordering at shops and facilities

## Cause and implementation

The Vulkan shop/facility recipes kept their raster tile origin as their depth
contact. A walking guest uses its actual world position. Guests approaching the
far side could therefore win depth testing against the entire building. The
original `Shop.cpp` and `Facility.cpp` painters instead order their authored
bounds against the guest's `Paint.Peep.h` bounds.

Shop counters now use the centre of their authored 2..29 footprint (summed XY
contact 31). A facility is explicitly a rear doorway panel, an occupant space,
and a foreground shell. All three share a ground-plane contact, with distinct
bounded local layers. Roof elevation is not added to this contact: doing so
incorrectly hides guests walking outside the building.

For visible peeps, the GPU checks the immutable object list in **their own tile**.
If their world-space bounds enter a facility, it places the whole peep sprite
group between the appropriate panels. Both body and accessory remain together.
Outside the footprint, above the roof, below the facility, or without a facility,
the existing guest depth is unchanged. Ghost facilities do not capture occupants.
The rear-panel relationship follows its original narrow bounds and the original
diagonal tie ordering. All contacts use world XY; screen pixel coordinates do not
determine depth.

This adds no CPU sorting, state publication, catalogue rebuild, texture upload or
neighbouring-tile search. It runs only in the GPU emission pass, after the peep's
visibility cull, once for its body/accessory group. Balloons, vehicles, path
railings, and other flat-ride families retain their previous rules.

## Original-renderer regression corpus

`test/object-parity/ObjectFixtureMain.cpp --guest-shops` is compiled against the
frozen, unchanged upstream core (`b80a4a84e92be8e07904b38d1032d0bb88280bb4`). It
copies original shop/facility objects and an actual walking guest from Everything
Park into a diagnostic save. Both renderers reload that exact save.

The fixture has 56 arrangements: two families, four authored directions, and
seven positions across each footprint. Four camera rotations produce 224
contacts per fixture. Optional diagnostic environment settings are:

- `OPENRCT2_GUEST_SHOP_LATERAL=-8|0|8`: off-centre walking lanes.
- `OPENRCT2_GUEST_SHOP_HAT=1`: an actual hat-wearing guest, including its accessory.
- `OPENRCT2_GUEST_SHOP_ELEVATION=0|32`: raised-building control.

These are static graphical poses, not a simulation test of visiting shops. No
player save is modified. `scripts/rendering/compare-guest-shop-contacts.py` counts
unmasked original pixels, records image hashes and per-contact errors, and
produces enlarged crops for inspection. An optional baseline reports corrected
and newly incorrect pixels separately.

## Qualification

Build 207 succeeds with zero warnings/errors; 54 focused rules, catalogue,
animation, peep-publication and path tests pass. All captures use Vulkan
synchronization validation, dummy audio and no visible window. Five fixture
variants cover 1,120 guest/building contacts. Root inspected enlarged original,
before and after crops, including the hat and raised-building cases.

| Fixture (four rotations each) | Corrected pixels vs 204 | Newly wrong pixels | Remaining difference from upstream |
| --- | ---: | ---: | --- |
| Centreline | 16,112 | 0 | None; exact full-frame match |
| Lateral -8 | 14,980 | 0 | One existing shop-edge pixel per view |
| Lateral +8 | 15,028 | 0 | One existing shop-edge pixel per view |
| Hat, lateral +8 | 11,800 | 0 | One existing shop-edge pixel per view |
| Raised 32 world units | 16,112 | 0 | Existing missing shop/facility foundations and supports |

The raised fixture has 63,058 remaining differing pixels per view. Before/after
comparison and enlarged crops confirm these support differences predate this
change; they remain a separate rendering backlog item. Do not describe this
fixture as complete pixel parity. The one-pixel shop-edge differences also
predate this change and are not hidden by a tolerance or mask.

Eight Everything Park views add 3,652 corrected pixels and zero newly wrong
pixels. They include Burger Bar 1 and Toilets 1 in all rotations, the occupied
Pirate Ship/Rowing Boats path areas, and the elevated Corkscrew control. The last
is pixel-identical to 204. Changes within the path-area views are shop/facility
corrections; this does not claim to resolve the separate railing backlog.

### Performance

Uncontended, serial, hidden/silent Everything Park runs at 3840x2160, VSync on,
100 warmup ticks and 12,000 measured ticks. The ordinary Turbo target remains
360 TPS. No other game, compiler or benchmark was running; initial GPU use was
0%. These are one paired measurement, not a statistical performance study.

| Build | TPS | Accepted presents/sec | CPU draw, ms | GPU frame, ms | Present interval p99 / max, ms |
| --- | ---: | ---: | ---: | ---: | --- |
| 204, before | 355.182 | 143.997 | 0.648228 | 4.700748 | 8.9 / 10.1301 |
| 207, after | 352.634 | 143.992 | 0.667786 | 4.762530 | 9.0 / 9.3815 |

TPS differs by -0.72%; measured GPU cost is +0.061782 ms. Both runs pass the
runner's checks and finish with entity checksum
`4e1d2382b99a04ac000000000000000000000000`. Root inspected the final 207 image.
Accepted presents measure queue acceptance, not display scanout. Known renderer
gaps remain; this is not full-world visual acceptance. Receipts:
`obj/vulkan-parity/performance-guest204-12000-01/summary.json` and
`obj/vulkan-parity/performance-guest207-12000-01/summary.json`.

Evidence is retained under `obj/vulkan-parity/guest-shop207-*`, with the original
fixture manifests under `guest-shop-oracle-*`. Build 205's simple facility offset
was rejected: it passed the centreline but failed the off-centre lanes. The
retained implementation uses containment and panel ownership instead.

## Deployment

Build 207 is deployed to `D:\Games\Independent\OpenRCT2Mod`: 29 qualified files
verified, four changed. The shared Everything Park save was preserved and no
game window was launched. Deployment receipt:
`obj/vulkan-parity/deploy-checkpoint207-01/receipt.json`.
All build source inputs were rechecked unchanged before deployment. The owner
confirmed the reported guest-ordering issues are solved and accepted the measured
performance cost. This checkpoint contains only the rendering fix, regression
coverage and its documentation; unrelated gameplay work is excluded.
