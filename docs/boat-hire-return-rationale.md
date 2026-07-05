# Boat Hire return routing note

## Correction

The custom code that forced a Boat Hire vehicle to return when a guest generated the normal "I want to get off" thought has been removed.

Testing showed that the reported case was caused by the station route being physically blocked. Boat Hire already has return-to-station routing for boats; the added get-off hook duplicated existing behavior instead of fixing the blocked-route problem.

## Current behavior

Boat Hire return movement is left to the ride vehicle's existing logic. The guest thought remains just a thought; it no longer mutates the occupied boat's `lost_time_out`, and boat free-roam steering no longer has the deterministic return override added by the removed patch.

Blocked waterways or blocked station approaches should be handled as layout/routing problems rather than by forcing guest thought handling to override the ride's vehicle routing state.

## Removed code

- `src/openrct2/entity/Guest.cpp`
  - Removed `GuestRequestBoatHireReturnToStation`.
  - Removed the call from `Guest::tick128UpdateGuest`.
- `src/openrct2/ride/Vehicle.BoatHire.cpp`
  - Restored `Vehicle::UpdateBoatLocation` to the existing probabilistic return steering after `lost_time_out > 1920`.
- `src/openrct2/ride/Vehicle.h`
  - Removed `kBoatHireReturnToStationTimeout`.
- `test/tests/PlayTests.cpp`
  - Removed `BoatHireGuestWantingOffRequestsReturnToStation`.

## Verification

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.*`
