# Gameplay improvements: investigation and implementation plan

Historical investigation and plan, 2026-09-27. Restrooms, precision/optional rounding, boarding, two-wide movement and staff
repairs have since been implemented; mechanics also repair lamps. Boarding tuning was approved and authored in the sibling
object repo. See [implementation details and compatibility](guest-services-and-boarding.md). Maze accounting remains deferred,
including every maze in the latest kart save. The separate Kart 11 dispatch defect is fixed; the coaster stretch goal is deferred.
The findings and proposed steps below are retained as the original investigation, not a list of outstanding implementation work.

## Findings and evidence

### Restroom admission

`Guest.cpp`, `GuestShouldGoToShop()`, still compares `RideGetPrice(ride) * 40` with the guest's 0–255 toilet need. The locally available `upstream/develop` uses that expression with tenth-based money. This fork stores cents, so the comparison is ten times too strict: maximum need accepts only six cents, rejecting the ordinary ten-cent minimum button increment. This explains apparent refusal to pay anything, although typed prices of one through six cents can still pass the need test.

Use `priceInCents * 4 > toiletNeed` (or an equivalently unit-explicit helper), preserving the minimum need of 70, affordability, no-money handling and the ignore-price cheat. At need 70, 17 cents passes and 18 cents fails; at need 255, 63 cents passes and 64 cents fails. Existing tenth-denominated prices retain upstream's acceptance boundaries without rounding arbitrary cent prices back to tenths. Add focused boundary and actual facility-admission tests.

### Price precision and retained optional rounding

`RideRatingsCalculateValue()` computes three separately truncated rating contributions into `money32`, applies age and competition adjustments in those legacy units, then calls `ToMoney64()`. Lost fractions cannot be recovered by that final conversion. `RideGetTargetPrice()` adds more divisions for paid entry, policy margins and the 70% scale. This is a confirmed source of coarse steps, rather than a currency-formatting issue.

User clarification: rounding to the nearest ten cents was intended only after all price adjustments. Correct that ordering, leave the feature implemented and documented, but disabled. Do not enable it or introduce a settings UI in this implementation.

Plan:

1. Sum weighted rating contributions with 64-bit, deterministic sub-cent fixed-point/rational arithmetic before division. Carry adequate fractional precision through age, competition, paid-entry reduction, policy boundaries/margins and global scale. Do not pass intermediate values through legacy `money16`/`money32` conversions.
2. Make pricing and guest value judgments consume a shared calculation, avoiding divergent admission and pricing thresholds. Preserve cent-valued `ride.value` for its existing consumers; if pricing needs additional fractional precision, derive it from the shared value calculation rather than introducing an unrelated floating-point value.
3. Finalize once to the nearest cent by default. Retain a named final-price quantizer with `cent` and `nearestTenCents` modes; select `cent`. Ten-cent mode uses nearest rounding, with nonnegative half-ties upward, after all price adjustments. Enforce legal bounds and free-price semantics after quantization so rounding cannot violate invariants.
4. Document the dormant ten-cent mode as a future configurable setting. Keep manual prices and the existing ten-cent plus/minus button step separate from automatic rounding.
5. Test fine rating sweeps, monotonicity between intentional age/competition boundaries, paid entry, each policy, minimum margins, free prices, caps and ten-cent half-ties. Precision does not promise that every stat increment changes the price by exactly one cent: legitimate rating or age changes can still move it farther.

### All mazes in the latest kart save

Read `C:/Users/imper/Documents/OpenRCT2/save/Karts Test 1.park` through `Karts Test 4.park` using the engine. Save 1 has no mazes; saves 2 and 3 have four; save 4 has eight. The latest starts at tick 617831. Saves retain ten 30-second customer-count buckets and accumulated profit, not a full historical monetary series per maze. Earlier save snapshots are observations, not a continuous history.

An isolated native diagnostic used the existing Release simulation library (built 2026-09-25), an isolated user directory, a fixed scenario seed and 144000 logical ticks (one nominal simulation hour). No source park was written. Money below is in display currency units, not raw cents.

| Maze | Age at save, months | Saved ticket | Saved profit/hour | Saved total profit | Next-hour actual profit change | Mean displayed profit/hour during run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 23 | 0.03 | -25.33 | -73.82 | -29.01 | -27.65 |
| 2 | 23 | 0.10 | 65.51 | 200.18 | 58.00 | 59.79 |
| 3 | 23 | 0.03 | -22.45 | -41.25 | -17.52 | -16.13 |
| 4 | 22 | 0.10 | 58.31 | 167.22 | 42.10 | 44.18 |
| 5 | 18 | 0.10 | 34.31 | 202.00 | 62.60 | 62.09 |
| 6 | 18 | 0.17 | 112.79 | 207.28 | 88.30 | 89.99 |
| 7 | 4 | 0.17 | 224.99 | 105.00 | 109.28 | 110.29 |
| 8 | 4 | 0.31 | 220.79 | 93.12 | 103.77 | 105.43 |

Displayed means are sampled every 1200 ticks, not exact tick integrals. All mazes have 3.10 upkeep per half-month payment, equivalent to approximately 54.49/hour. The existing upkeep conversion agrees with the actual half-month finance cadence; there is no evidence here for changing its denominator.

Mazes 7 and 8 are much younger than mazes 1–6, explaining why their current hourly rates can be higher while lifetime totals are lower. Maze 7's ticket fell from 0.17 to 0.10 during the diagnostic; Maze 8's changed from 0.31 through other values to 0.24. Maze 6 temporarily changed from 0.17 to 0.10 and returned to 0.17; mazes 1–5 held their prices throughout the sampled run. Recent throughput, age changes, history-window lag and the phase of lump-sum upkeep payments all matter. The measured hour does **not** establish a persistent reversed ranking or a maze-specific charging defect.

There is nevertheless a confirmed accounting-design mismatch: `Ride::calculateIncomePerHour()` multiplies recent customer/item counts by **current** prices, whereas `GuestPayRideAdmission()`, shop/facility sales and `FinancePayRideUpkeep()` update `totalProfit` from **actual** transactions. This retroactively reprices past customers and can count vouchers/free riders as paying customers. It also cannot represent journey-specific transport fares accurately.

Plan:

1. Before fixes, preserve a fresh-build reproduction for all eight mazes. Measure price changes, paid/free admissions, receipt totals, customer increments, upkeep and total-profit deltas on matching intervals. Keep the original save unchanged.
2. Add cent-valued receipt/contribution buckets alongside the existing customer history. Record real paid admission, facility sales, item margins and photos at their transaction sites; vouchers/free rides contribute zero. Keep customer counts independent.
3. Calculate recent income from those receipts, never `count * current price`. Continue displaying profit as recent income minus the correctly normalized running-cost rate. Document this as a recent operating rate, rather than a lifetime average or an exact forecast.
4. Compare cash reconciliation using actual upkeep charges over the measured interval. A smoothed hourly operating rate and lump-sum upkeep payments differ near interval boundaries; tests must account for that explicitly. Do not fabricate old receipts from current ticket prices when loading older saves. Track observed history duration and warm it up transparently.
5. Cover constant and changing prices, vouchers, no-money parks, closed/open transitions, shops with two products, photos, transport fares, save/load and accounting reconciliation. A synthetic stable maze pair must agree in ranking once history is mature and intervals are matched.

Local diagnostic evidence (ignored artifacts): `artifacts/gameplay-plan/MazeAccounts.cpp`, `run.ps1`, `maze-accounts.csv`, `saved-maze-snapshots.csv`, `run.log`, and `snapshots-run.log`. The first log records the successful hour run; an outer shell's subsequent relative log-path read failed after the script changed directory, not the diagnostic. The script now restores its working directory. Its default filter selects the saved snapshots; choose `GameplayPlanDiagnostic.MazeAccounts` to repeat the hour run.

## Vehicle boarding delay

Relevant seams: `CarEntry`, `RideObject::ReadJsonCar()`, `Guest::updateRideApproachVehicle()`, `updateRideEnterVehicle()`, waypoint loading and the station-platform reservation flow. Current boarding hides the guest and increments vehicle occupancy once approach and seat-order checks pass; there is no per-car boarding-duration field.

Add a per-car JSON property, proposed `boardingDuration` in seconds, converted to simulation ticks by the loader. Accept finite, bounded nonnegative values; zero explicitly disables it. Missing JSON fields and legacy DAT definitions default to zero for compatibility. Author small nonzero values in the sibling `OpenRCT2-objects` passenger-car definitions, starting from 1.0 second for ordinary boarding and tuning continuously loading vehicles separately. Vehicle-less rides and pseudo-vehicles such as mini-golf need no artificial delay.

The wait begins on reaching the actual boarding position, including waypoint-based approaches, before the guest disappears into the car. Each passenger has an independent timer; passengers can board concurrently. Paired seats must both complete their own waits before the existing paired commit runs. Do not inadvertently multiply the configured delay by every seat, charge admission twice, or replace station dispatch waiting with this delay.

Use simulation ticks/deadlines, not the number of speed-gated guest update calls. Preserve seat ownership and the existing `num_peeps`/`next_free_seat` departure invariant. Revalidate closure, breakdown, vehicle removal/change and platform recovery; clear pending boarding state on cancellation. Save/load midway through boarding must preserve the remaining delay deterministically.

Tests: missing/zero/custom duration, malformed values, single seats, paired seats, multi-car trains, waypoint rides, platform-staged trains, continuously loading rides, cancellation and save/load. Check the kart test visually for plausible timing. Update the sibling object documentation and normal object installation workflow; preserve its extensive existing local artwork changes.

## Two-wide path movement

The exact destination fields currently return one direction with a fixed tie-break. This funnels guests onto identical shortest routes. The recent Texas Giant fix deliberately gives exact routes precedence over wide-path shortcuts; retain that ordering and its regression tests.

Implement lane choice as a bounded refinement of a known exact route:

1. Prefer continuing forward among legal, equally short choices. Use a stable per-guest tie-break at corridor entry to spread otherwise equivalent choices. Every ordinary step still decreases the exact distance to the current concrete target.
2. For a genuinely crowded flat, non-queue straight corridor, allow a short committed maneuver: one lateral step followed by two forward steps. Accept it only after validating every connection/banner/elevation and proving the endpoint has smaller exact target distance than the starting tile. Restrict the initial implementation to clear parallel corridors; do not initiate the maneuver at a junction, terminal, merge, slope, queue entrance or crossing.
3. Retain the small maneuver until completed rather than reconsidering lane choice every tile. Continue forward on the selected lane when that is a legal distance-decreasing option. Return to normal destination routing at a merge or T-junction. A side step may temporarily increase distance, but every completed maneuver must strictly reduce it; uncontrolled sideways wandering is never permitted.
4. Require a meaningful occupancy advantage, use per-guest staggered eligibility, count committed arrivals in destination-lane load, and impose a forward-distance cooldown. These rules prevent all guests responding to the same snapshot and oscillating together. Start with at least four forward tiles between discretionary lane changes and tune against fixtures.
5. Key occupancy by actual path node and height, not just XY. Use bounded local reads or a shared per-tick snapshot with deterministic commitment arbitration. Do not rebuild destination fields for crowd changes or run a fresh park-wide search per guest. Preserve the active transport/queue destination and all live path permissions.
6. Cancel safely on connectivity-epoch changes, destination changes, pickup or state changes. With unavailable/inexact route information, use existing routing rather than speculating about lateral reachability.

Acceptance fixtures: long two-wide straights, bidirectional traffic, three-wide areas, blocked lanes, 2-to-1 merges, T-junctions in every orientation, terminal queues, stacked paths, banners, live edits and the existing Texas Giant reproduction. Measure arrivals, travel time, maximum local density, lane switching and loop/stall detection. Demonstrate both reduced concentration and eventual destination arrival. Congestion cannot disappear when demand exceeds the capacity of the remaining one-wide exit.

## Staff repair of vandalized path furniture

`Staff::updatePatrollingFindBin()` explicitly skips broken bins. `updatePatrolling()` starts local service tasks only for handymen; mechanics currently use separate ride repair states. Path furniture uses a broken flag on the **path addition as a whole**, rather than separate damage flags for individual edge sprites.

Add a dedicated path-addition repair task with approach, work and completion phases:

- A patrolling handyman encountering a broken bin on the current path tile walks to an exposed bin edge and repairs it. A patrolling mechanic does the same for a broken bench. Use existing bin/bench interaction offsets and face the furniture. No park-wide job search is needed.
- Honor patrol boundaries and the relevant existing work orders (empty bins for handymen, repairs for mechanics). Assigned ride breakdown/inspection work has priority; do not divert a mechanic already answering a ride call. Ensure a new ride assignment can cancel local work safely where needed.
- Use the handyman's existing bin-handling animation; that animation object does not contain mechanic repair sprites. Use the mechanic's existing low-level repair animation for benches. Loop an appropriate action for a short fixed work duration, provisionally 5 simulation seconds for bins and 8 for benches, then validate visually and tune against short ride-repair actions. Use elapsed simulation ticks, independent of staff walking speed, and complete cleanly at an animation boundary.
- Reserve each repair target once, including path height and addition identity. Extend the existing service-reservation preparation safely for mechanics and stacked paths; arbitration must not depend on worker scheduling. Reconstruct reservations from saved task state rather than saving a transient global map.
- Revalidate that the same path addition still exists, is not a ghost and remains broken before completion. Deletion/replacement, pickup, dismissal, another repair or ride reassignment cancels the task. Avoid retaining raw tile pointers across updates.
- On completion, clear the addition's broken flag and invalidate its rendered appearance through the ordinary path mutation/publication mechanisms. One task repairs the whole addition, matching the existing damage representation. Preserve bin fullness so normal emptying can follow; do not silently count repairs as bins emptied or rides fixed.
- Add suitable staff status text. Custom animation objects missing the selected action need a defined safe fallback. Test repair completion, duration, competing workers, removal/replacement, stacked paths, cancellation, save/load and visible animation/render refresh.

## Implementation order, compatibility and documentation

1. Preserve the all-maze baseline and add focused money/restroom regressions.
2. Fix restroom units and calculation precision; retain disabled final ten-cent quantization.
3. Replace inferred monetary history with actual receipt history and reconcile it against lifetime accounting.
4. Add object-defined boarding duration and author sibling definitions.
5. Implement bounded lane choice, keeping route-progress tests as a release gate.
6. Add local staff furniture repair and validate its animation/publication path.
7. Run relevant native suites, the kart-save diagnostic, save/load checks and a fixed-tick large-park performance comparison. Then build Release and perform targeted visual checks. Deployment is not part of this planning turn.

Persistent boarding, lane commitment, staff-task and monetary-history state requires a coherent fork save-version extension beyond the current 60016, plus entity snapshot/replay handling and checksums. Assign final version numbers at implementation time, as other work may advance them. Older saves initialize new state safely; preserve legacy money conversion boundaries. Deterministic simulation behavior must match across worker counts and replay/save-load boundaries. Do not imply that old replays can reproduce newly changed simulation rules without a compatibility policy.

Update `money-cent-precision-rationale.md`, `ride-pricing-target-rationale.md`, `time-measurement-fix-ledger.md`, `shared-route-fields.md`, `transport-ride-routing-rationale.md` and relevant save-format documentation with implemented behavior and evidence. Add concise boarding/furniture-service documentation and sibling JSON documentation where no suitable existing document exists. Existing historical statements should be marked superseded rather than erased indiscriminately. Preserve all unrelated workspace changes and do not commit unless asked.
