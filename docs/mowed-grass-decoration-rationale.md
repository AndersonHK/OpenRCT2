# Mowed grass decoration rationale

## Gameplay problem

Mowing grass used to be cosmetic and staff-stat-only. Groundskeepers could spend time keeping lawns tidy, but that care did not affect guest or ride systems that already reward attractive surroundings.

## Design decision

Mowed grass now counts as a lightweight decoration item wherever this fork's gameplay code evaluates nearby decoration or scenery:

- ride-wide scenery rating scans
- ride proximity scenery checks
- per-tick ride rating context scans for vehicles
- per-step maze rating context scans
- guest surroundings scans for the "Great scenery!" thought

The rule only applies to surface tiles that can actually grow grass and whose visible grass state is `GRASS_LENGTH_MOWED`. Other clear, growing, clumped, non-grass, or ghost elements do not count.

## Current balancing

Each mowed grass tile counts as one decoration item, the same unit used by existing small and large scenery counts. This makes well-kept lawns matter without adding a new parallel stat or requiring new UI.

The practical effect is intentionally local. Mowed grass helps most when it is near ride stations, track, maze paths, or guest walking areas, because those are the places already sampled by the existing decoration scans.

## Functions touched

- `src/openrct2/world/Scenery.cpp`
  - `TileElementCountsAsDecoration`: new shared helper for real scenery objects and mowed grass surfaces.
- `src/openrct2/ride/RideRatings.cpp`
  - `ride_ratings_score_close_proximity`: treats adjacent mowed grass surfaces as scenery proximity.
  - `ride_ratings_get_scenery_score`: counts mowed grass in station-area scenery totals.
- `src/openrct2/ride/Vehicle.cpp`
  - `RideRatingGetLocalContextScore`: counts mowed grass in per-tick ride context.
- `src/openrct2/entity/Guest.cpp`
  - `GuestAssessSurroundings`: counts mowed grass toward guest scenery thoughts.
  - `RideRatingGetMazeLocalContextScore`: counts mowed grass in maze guest-path samples.
- `test/tests/TileElements.cpp`
  - `MowedGrassCountsAsDecoration`: verifies only growable mowed grass counts as decoration.
- `test/tests/testdata/ratings/EverythingPark.park.txt`
  - Updates the two affected spiral-slide expectations after nearby mowed grass increased their scenery-driven excitement.

## Verification

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=TileElementWantsFootpathConnection.MowedGrassCountsAsDecoration`
- From `bin`: `.\tests.exe --gtest_filter=TileElementWantsFootpathConnection.*`
- From `bin`: `.\tests.exe --gtest_filter=RideRatings.*`
