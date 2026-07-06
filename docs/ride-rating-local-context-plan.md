# Ride rating local context plan

## Decision

Ride rating context should be sampled where riders actually are, then added to the raw per-tick accumulator. Scenery, mowed grass, paths, nearby foreign track, and vertical path/track interactions should no longer be post-ride bonuses.

The context query is cached per origin tile and ride id. Vehicles and maze guests ask the origin they are currently sampling for a precomputed context score instead of scanning the map themselves on every tick.

## Height-based range

Ground-level samples use a 5x5 area. The radius grows every two height units above local ground:

- radius 2: 5x5
- radius 3: 7x7
- radius 4: 9x9
- radius 5: 11x11
- radius 6: 13x13
- radius 7: 15x15 maximum

Items lower than the rider receive a mild height penalty, but the expanded range should more than compensate for tall rides. This intentionally favours tall scenic rides such as chairlift-style rides, observation towers, free fall towers, and roto-drop rides.

The range is relative to the terrain under the thing being considered, not simply absolute map elevation or only the origin tile's surface. The query casts a simple vision ray from the rider origin toward each candidate tile. The first two tiles are treated as the immediate local area; after that, the ray slopes downward by two height units per tile. A candidate is visible only if its surface is at or below that ray and no intermediate terrain or solid object blocks it.

A flat plateau at high map elevation still uses the minimum range for riders travelling at that same plateau height; the range only expands toward lower terrain when the rider is elevated above that target terrain and can raycast outward toward the horizon. The reverse also matters: a rider high above the floor of a pit only gets the reduced range appropriate to the surrounding terrain if the surrounding terrain is close to the rider's height.

This gives the intended edge cases:

- A rider 3 height units above a tile that is itself 6 height units above the surrounding land can see outward as if 9 height units high when looking over the lower land.
- A rider 12 height units above a pit floor that is 9 height units below the surrounding land only sees the surrounding land as if 3 height units high.
- Decorations on top of a cliff higher than the rider's viewpoint are treated as occluded by that cliff terrain, even if their tile is inside the maximum cached square.

## Ride eye heights

Vehicle-sampled rides use each vehicle's current map height as the eye origin. Fixed/scenery-modifier rides need an explicit origin because they do not always have a representative moving vehicle in the rating path. Ground-level fixed rides use a small rider eye-height offset from the station tile. Mazes use a lower origin and maze track counts as an opaque wall for line of sight. Observation towers, roto-drop rides, launched freefall rides, lift rides, ferris wheels, and chairlifts use larger ride-specific offsets so their scenery context reflects their much higher passenger viewpoint.

## Visibility and proximity

The query treats ordinary track and supports as transparent. Solid items such as walls, entrances, scenery, and maze track block line of sight only if the sight ray passes through their vertical volume. A rider above the top of a solid item can see over it. Terrain is special: candidate tiles whose surface is above the downward sight ray are outside the contextual scenery range, and intermediate terrain blocks when the sight ray passes below the surface.

Scenery is raycast toward the element's top. Path and foreign-track proximity are scored as their own channels, not as generic decoration. If path or foreign track sits above or below the sampled origin on the same tile, it is scored as a distance-zero vertical interaction with a large bonus. This covers coasters threading through other rides and path bridges crossing track.

## Diminishing returns

Scenery applies an uncapped square-root curve after distance, height, and visibility weighting. Four times the old raw scenery divisor reaches the previous local scenery cap, and additional scenery keeps helping at a slower rate instead of stopping completely.

Path proximity, foreign-track proximity, and vertical interaction still use saturating curves because those bonuses represent nearby interactions that should taper to a practical local maximum.

## Cache ownership

The cache is owned by ride rating code and keyed by origin tile, origin height, and ride id. Construction and map edits dirty cached origins within the maximum seven-tile radius of the changed tile. Full map changes clear the cache. Each cache miss scans the maximum 15x15 square once, then filters each candidate tile by its terrain-relative range. This is intentionally biased toward doing expensive ray/scenery work during sparse construction-driven invalidation and lazy recomputation, rather than per vehicle tick.

## Functions touched

- `RideRating::GetLocalContextScore`: shared cached local context query.
- `RideRating::GetFixedRideLocalContextOrigin`: shared fixed-ride origin helper with ride-specific eye heights.
- `RideRating::InvalidateLocalContextCacheAround`: dirties cached origins near a changed tile.
- `RideRating::ClearLocalContextCache`: clears the context cache for map resets and broad invalidations.
- `RideRatingAccumulateVehicleTick`: samples cached local context from the vehicle's current origin.
- `RideRatingAccumulateMazeStep`: samples cached local context from the maze guest's current step.
- `ride_ratings_get_scenery_score`: flat rides keep their existing descriptor-driven ratings, but their scenery modifier now samples this local context from the ride station/start tile with a rider eye-height offset instead of using the old fixed scenery count.
- `Vehicle::TestReset` and station test-finish handlers: repeat test-mode circuits preserve the recent sample history while clearing the active circuit accumulator, so test runs continue to refresh ride stats after the first completed test.
- `MapInvalidateTile`, `MapInvalidateRegion`, `SetTileElements`, `TileElementInsert`, and wall-removal paths: cache invalidation hooks for construction, scenery, terrain, grass, wall occlusion, and map reset changes.
