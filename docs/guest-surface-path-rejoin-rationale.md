# Guest surface path rejoin rationale

## Gameplay problem

Guests that ended up off a footpath were using the surface movement fallback, which picked from a few random legal surface directions. When a valid path tile was nearby, this could make guests wander through grass instead of visibly trying to get back to the path network.

## Design decision

Guests standing on surface tiles now run a tiny local search before falling back to random wandering. The search looks for a reachable footpath within three tile moves and chooses the first step toward the nearest reachable path.

The search is deliberately local and route-free. It does not try to replace normal footpath routing, and it does not store a path. Once the guest steps onto a footpath tile, the existing path interaction and normal pathfinding take over again.

## Current balancing

- Search radius: three surface steps.
- Search shape: breadth-first by tile distance.
- Traversal rules: obeys wall checks, surface blocking, in-park surface movement, water checks, and the same small height-difference rule used by normal surface movement.
- Path detection: mirrors `Peep::PerformNextAction` by accepting path elements near the guest's current walking height.
- Fallback: if no reachable path is found, the previous random surface wandering behavior still runs.

The goal is to make nearby recovery feel intentional without adding a heavy pathfinder to every off-path tick.

## Functions touched

- `src/openrct2/peep/GuestPathfinding.cpp`
  - `GuestSurfacePathFinding`: now asks the local rejoin search for a direction before random wandering.
  - `GuestSurfaceFindPathRejoinDirection`: new bounded breadth-first search over nearby surface tiles.
  - `SurfacePathHasReachableFootpath`: checks whether a tile has a path element the guest can actually step onto from the current height.
  - `SurfacePathCanStepFromNode`: applies wall checks for a candidate surface step.
  - `SurfacePathTryMakeSearchNode`: validates surface traversal through non-path tiles.
  - `SurfacePathSearchVisited`: prevents the local search from revisiting tiles.

- `test/tests/Pathfinding.cpp`
  - `PathfindingTestBase.SurfaceGuestsStepTowardAdjacentPath`: verifies that a walking guest on a surface tile next to one reachable footpath chooses the path direction instead of random wandering.

## Verification

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PathfindingTestBase.SurfaceGuestsStepTowardAdjacentPath`
- From `bin`: `.\tests.exe --gtest_filter=*Pathfinding*`
- From `bin`: `.\tests.exe`
