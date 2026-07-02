# Ride pricing target rationale

This change replaces direct ride admission-price editing in the ride income UI with a pricing policy. The policy stores what kind of value reaction the ride should target, then recalculates the admission price whenever ride value is refreshed from the current ratings.

## Targets

The target price is derived from `ride.value`, after applying the same paid-park-entry reduction used by guests when park entry and ride prices are both unlocked.

```text
good deal  = just below 0.5 * guest-facing ride value
no effect  = just below 1.0 * guest-facing ride value
bad deal   = just below 2.0 * guest-facing ride value
```

Each target includes a conservative margin. Good-deal and no-effect prices leave a 10% buffer, with a minimum 0.10 or 0.20. Bad-deal pricing leaves a smaller 5% buffer with a minimum 0.10 because the intent is to approach the maximum amount happy/value-tolerant guests will still ride for, without crossing into the hard "too expensive" refusal threshold.

The requested 0.05 minimum price precision is blocked by the existing `money64` representation in `src/openrct2/core/Money.hpp`, which stores money as multiples of 0.10. Changing displayed and stored ride prices to true 0.05 increments would require a project-wide money representation and save/import migration rather than a local ride-pricing tweak.

After the target and margin are calculated, the automatic ride-admission price is globally scaled to 80% before the normal min/max clamp is applied. This makes a ride that would previously target `$10.00` target `$8.00` instead, and it reduces how quickly high-value rides flatten against the `$20.00` admission cap.

This makes the three targets distinct:

- Good deal: maximises the price that still produces the good-value band.
- No effect: maximises price near base ride value without deliberately pushing into the expensive-value band.
- Bad deal: maximises price near the rideable happy-guest ceiling, but stays below the refusal point.

## Automatic updates

`RideRatingsCalculateValue()` now calls `RideUpdateTargetPrice()` after updating `ride.value`. This keeps automatic prices tied to rating changes, age decay, same-type competition penalties, and aggregate ride-stat updates.

When a ride has no value yet, the current price is preserved. Once ratings produce a value, the selected target starts controlling admission price.

## Scope

Target pricing applies only to normal ride admission. Shops, facilities, toilets, and secondary/photo items retain direct price controls because their prices are item costs rather than ride admission value.

Existing save files load with the neutral/no-effect target. New saves persist the target in park-file version 62.

## Functions touched

- `RidePriceTarget`, `Ride::priceTarget`, `RideGetTargetPrice()`, `RideUsesTargetPricing()`, and `RideUpdateTargetPrice()` in `src/openrct2/ride/Ride.h` and `src/openrct2/ride/Ride.cpp`.
- `RideRatingsCalculateValue()` in `src/openrct2/ride/RideRatings.cpp`.
- `RideSetPriceAction` in `src/openrct2/actions/ride/RideSetPriceAction.h` and `src/openrct2/actions/ride/RideSetPriceAction.cpp`.
- Ride creation defaults in `src/openrct2/actions/ride/RideCreateAction.cpp`.
- Income-page UI controls in `src/openrct2-ui/windows/Ride.cpp`.
- Park-file ride serialization in `src/openrct2/park/ParkFile.h` and `src/openrct2/park/ParkFile.cpp`.
