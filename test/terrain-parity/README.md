# Native-terrain input preparation

`NativeTerrainFixtureMain.cpp` uses public core APIs and no UI/renderer library.
The frozen build can `prepare` or `verify`; the current build is compiled as
verification-only. Preparation exports one new `.park`, after normalizing every
declared 32×32 surface, including its border, to world Z 16. Neither mode runs a
simulation tick. The runner reloads the same exported bytes in two additional
processes and rejects any canonical census difference.

Builds and runs are coordinated by the migration task owner. Example commands
from the repository root (choose unused output directories):

```powershell
python scripts/rendering/build-native-terrain-preparer.py --frozen --source-root obj/vulkan-parity/oracle-ui-source-02 --reuse-build obj/vulkan-parity/ui-frozen-build-02 --toolset-version 14.44.35207 --output obj/vulkan-parity/terrain-frozen-build-01
python scripts/rendering/build-native-terrain-preparer.py --enable-vulkan --reuse-build obj/vulkan-parity/ui-build-08 --toolset-version 14.44.35207 --output obj/vulkan-parity/terrain-current-build-01
python scripts/rendering/prepare-native-terrain-fixture.py --frozen-build obj/vulkan-parity/terrain-frozen-build-01 --current-build obj/vulkan-parity/terrain-current-build-01 --source-park obj/vulkan-parity/reference-9a092745f3/testdata/parks/small_park_with_ferris_wheel.sv6 --data obj/vulkan-parity/reference-9a092745f3/package/data --rct2 'D:/Games/GOG Games/RollerCoaster Tycoon 2 Triple Thrill Pack' --rct1 'D:/Games/GOG Games/RollerCoaster Tycoon Deluxe' --output obj/vulkan-parity/native-terrain-input-01
```

The optional reuse receipt must match every core input, dependency, compiler,
variant and generated compiler setting, plus the actual library hash. An
ineligible reuse falls back to compiling the core in the new output tree. The
frozen builder first checks archived source bytes; it never instruments them.
`--enable-vulkan` is accepted only for the current verifier and must match the
reused core's build variant. It enables the same compile definition in core and
driver; it does not initialize a Vulkan renderer in this headless program. The
frozen preparer always builds with `EnableVulkan=false`.
All generated projects, object files, executables and logs remain under output.

Only `manifest.json` with `accepted: true` is a qualified input. The manifest
includes exact park bytes/hash, all three process commands and logs, canonical
census hashes, both build receipts, loaded object sources and complete hashes
of asset trees (including external object images). Failed attempts preserve
their outputs with `accepted: false`. Export embeds timestamps, so regeneration
creates a different input requiring a new review; existing outputs are refused.

The census verifies all entity slots and every technical spatial bucket plus
the null bucket. It distinguishes the 1,024 declared elements from allocated
technical storage, records every object type/slot including holes, and compares
weather, date, RNG, camera and zero rides/spawns/entrances. Intransient object
tables are compared as initialized environment state, not as park-persisted
objects. Map animations have no public count accessor: preparation records its
explicit `MapAnimations::ClearAll`, and verifies a surface-only map, without
inventing a measured animation count.

This qualifies input data only. Main-UI native/fallback coverage, all three
renderer paths, repeatability, pixel comparisons and manual visual review still
follow [the fixture design](../../docs/vulkan-native-terrain-fixture-design.md).
