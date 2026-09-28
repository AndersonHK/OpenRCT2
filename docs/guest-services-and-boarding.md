# Guest services, boarding and kart recovery

Implemented September 2026. The original investigation is retained in
[the gameplay plan](gameplay-improvements-plan-2026-09-27.md); maze accounting and the separate partial-load coaster
stall remain deferred.

## Restroom admission and automatic prices

Toilet need now compares cent admission with `need / 4`, correcting the leftover tenth-money multiplier. Minimum need
70, cash checks, free parks and the ignore-price cheat keep their existing behavior. Need 70 accepts 17 cents; maximum
need accepts 63 cents. Ordinary ten-cent prices therefore reproduce upstream's willingness boundaries.

`RideRating::UpdateValue` combines rating contributions before division and carries 1/1024-cent precision through age
and competition adjustments. `Ride::value` remains whole-cent money; `valueFraction` holds its fractional remainder.
Automatic pricing carries this precision through paid-entry reduction, pricing margins and the 70% income scale,
then rounds once to the nearest cent. Guest perceived value uses the same precision and final cent rounding.

`RidePriceRounding::nearestTenCents` retains optional nearest-ten-cent rounding **after all adjustments**, before the
normal admission clamp. It is disabled: all production callers default to `cent`. This is a seam for a possible future
setting, with no setting or UI added now. Manually entered prices, price-button steps and maze accounting are unchanged.

## Vehicle boarding

Ride JSON car entries accept `boardingDuration`, a finite number of simulation seconds from 0 to 30. Omission or zero
preserves immediate boarding; legacy DAT objects also default to zero. Parsing converts seconds to the nearest simulation
tick. Invalid values report an object property error.

The countdown begins after the passenger reaches the seat's boarding position, including waypoint-based boarding.
It runs once per simulation tick, independent of energy and movement speed. Passengers wait concurrently; paired seats
only commit once both passengers' countdowns have finished. Existing queue movement and dispatch waits are separate.
Position-frozen guests retain their countdown. The approved 1–3 second values have been authored in 279 companion ride
objects / 336 passenger car definitions; the [complete mapping and rationale](vehicle-boarding-duration-rationale.md)
documents all families and overrides. Seatless parts and building/mini-golf placeholders have no added delay.

## Crowded two-wide paths

Guests following an exact destination field keep moving forward when forward is an equally short route. On a flat,
non-queue two-wide straight, eight or more nearby/committed guests can trigger a lateral move if the other lane has at
least four fewer guests. Incoming commitments count immediately, and a deterministic guest-id/time phase lets only a
quarter of guests reconsider at once. Neither decision consumes scenario randomness.

A lane change requires two subsequent forward steps to be legal, an uninterrupted three-row two-wide corridor,
no directed restrictions, and a lower exact destination distance at the end of the maneuver. The guest commits to those
forward steps, then waits four further route decisions before reconsidering. Every step revalidates legality and distance;
a changed destination/topology abandons an invalid commitment. Junctions, slopes, queues and incomplete route fields keep
normal routing. These rules bound detours and prevent synchronized switching or loops at a two-wide-to-one-wide merge.

## Repairing vandalized furniture

Patrolling handymen with Empty bins enabled repair broken bins; mechanics with Fix rides enabled repair broken benches
and lamps. They notice furniture on their current path tile, stay within their patrol area and approach an exposed edge.
The single broken flag covers all furniture edges on that tile, so one job restores the tile's addition.

Work takes at least five simulation seconds for a bin and eight for a bench/lamp, followed by completion of the current
animation cycle. Bins use the empty-bin animation, benches the ground-fixing animation, lamps the standing-fixing animation.
Custom animation sets missing that action safely wait for the work duration. A deterministic reservation per tile, height
and object prevents duplicate workers. Reservations rebuild each tick, so pickup, dismissal, object removal and state changes
release claims. Orders and patrol eligibility are rechecked during work. Mechanics remain eligible for ride calls while
repairing furniture, so breakdowns and inspections can interrupt this opportunistic work.

## Kart 11

Reproduced in `Karts Test 4 - kart 11  is locked.park`, the newest manual save supplied locally. Go-Karts 11 had all 19
karts departing, but one retained a completed lap. Race station logic alternated between immediately declaring that kart
the winner and starting another race, repeatedly assigning start delays. `RideRaceInitVehicleSpeeds` now resets every
kart's lap count when the new race begins. The original save recovers without editing or resetting the ride: a 12,000-tick
headless run progresses through racing, unloading and another race. The old build left the karts stationary for the same run.
This is a race-mode defect; no claim is made that it fixes the reported partial-load rollercoaster stall.

## Persistence and compatibility

Fork park version 60017 adds fractional ride value, passenger boarding/lane state and staff repair target/time. Earlier
parks load with default zero fractional values and inactive new work state. New replay payloads use version 13 and reject
earlier replays, because guest/staff serialization and simulation decisions changed. Keep old park originals if they must
also be opened with an older fork executable. The repair reservation map is derived from staff state and is not serialized.

## Verification

Native regression tests in `PlayTests.cpp` cover restroom boundaries, fractional rating prices, optional final rounding,
paired boarding timers and stale-lap race starts. `GuestExactRoutingTests.cpp` exercises real guest movement through a
crowded two-wide corridor into a single-width merge and checks all three furniture repair animations and durations.
The latest manual kart save was also replayed headlessly.

Validation completed locally:

- Release x64 build: zero warnings, zero errors.
- Deployed to `D:\Games\Independent\OpenRCT2Mod` using `deploy-local.ps1 -SkipBuild`. Both executable hashes match
  the build; all 4,774 runtime data files, including the boarding definitions, match with no extra installed files.
- 294 targeted tests across gameplay, routing, ratings, station logic, money, entities, presentation and park migration:
  293 passed on the broad run. The remaining historical age-bonus assertion explicitly required ten-cent truncation;
  it was updated to check the same age multipliers with fractional precision and passed its focused rerun.
- New cases include horizontal and vertical corridors, a turn onto a single-width path, exclusive repair claims,
  cancellation after furniture removal, all three repair animations, paired timers, object-defined timer startup,
  current-version save round trips and previous-version defaults.
- Repeated 12,000-tick run of the original locked kart save with the final library: Kart 11 races, unloads and races again.
- Independent JSON audit: every one of the 279 objects / 336 passenger cars matches the approved mapping, with no positive
  duration on excluded placeholders or seatless parts.

The test logs and native diagnostic are under the ignored `artifacts/gameplay-plan` directory. Visual balancing across
every vehicle and a full manual play session are still useful follow-up tuning; these checks establish runtime behavior,
not a claim that every vehicle's boarding animation has been visually reviewed.

The development data directory already links RCT object families to the sibling checkout. With uncommitted object edits,
the pinned packaging installer deliberately refuses a clean distribution install. After dependencies are present, build
with `msbuild openrct2.sln /p:IsSolutionBuild=true /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`.
This uses those linked development objects without committing or changing the packaging policy.
