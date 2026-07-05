# Ride pricing target rationale

This change replaces direct ride admission-price editing in the ride income UI with a pricing policy. The policy stores what kind of value reaction the ride should target, then recalculates the admission price whenever ride value is refreshed from the current ratings.

## Targets

The target price is derived from `ride.value`, after applying the same paid-park-entry reduction used by guests when park entry and ride prices are both unlocked.

```text
discount   = just below 0.5 * guest-facing ride value
fair price = just below 1.0 * guest-facing ride value
expensive  = just below 2.0 * guest-facing ride value
```

Each target includes a conservative margin. Discount and fair-price targets leave a 10% buffer, and expensive pricing leaves a smaller 5% buffer because the intent is to approach the maximum amount happy/value-tolerant guests will still ride for, without crossing into the hard "too expensive" refusal threshold. All three targets use a minimum `$0.05` margin now that `money64` stores cent precision.

After the target and margin are calculated, the automatic ride-admission price is globally scaled to 70% before the normal min/max clamp is applied. This makes a ride that would previously target `$10.00` target `$7.00` instead, and it reduces how quickly high-value rides flatten against the `$20.00` admission cap.

Guests use the same 70% scale when judging ride admission prices. A `$10.00` ride value is therefore perceived as `$7.00` for price thresholds: discount thoughts happen at or below `$3.50`, expensive thoughts happen above `$10.50` through `$14.00`, and refusal begins above `$14.00`. The existing paid-entry reduction still applies before this scale.

The guest-facing thought text remains in-universe: discounted rides use the inherited "really good value" phrasing, and expensive-but-still-rideable prices now produce "isn't worth the money" rather than exposing the underlying ride-stat formula.

This makes the three targets distinct:

- Discount: maximises the price that still produces the discount thought band.
- Fair price: maximises price near base ride value without deliberately pushing into the expensive thought band.
- Expensive: maximises price near the rideable happy-guest ceiling, but stays below the refusal point.

## Automatic updates

`RideRatingsCalculateValue()` now calls `RideUpdateTargetPrice()` after updating `ride.value`. This keeps automatic prices tied to rating changes, age decay, same-type competition penalties, and aggregate ride-stat updates.

When a ride has no value yet, the current price is preserved. Once ratings produce a value, the selected target starts controlling admission price.

## Scope

Target pricing applies only to normal ride admission. Shops, facilities, toilets, and secondary/photo items retain direct price controls because their prices are item costs rather than ride admission value.

Existing save files load with the neutral/fair-price target. The target field was introduced in fork-private park-file version `60000`; cent-precision money begins at fork-private version `60001`; new saves now use fork-private version `60002` because park entrance pricing policies are also serialized. The high version band is intentional so future upstream OpenRCT2 save versions can continue from their own latest number without colliding with this mod's custom fields.

## Functions touched

- `RidePriceTarget`, `Ride::priceTarget`, `RideGetTargetPrice()`, `RideUsesTargetPricing()`, and `RideUpdateTargetPrice()` in `src/openrct2/ride/Ride.h` and `src/openrct2/ride/Ride.cpp`.
- `RideRatingsCalculateValue()` in `src/openrct2/ride/RideRatings.cpp`.
- `RideSetPriceAction` in `src/openrct2/actions/ride/RideSetPriceAction.h` and `src/openrct2/actions/ride/RideSetPriceAction.cpp`.
- Ride creation defaults in `src/openrct2/actions/ride/RideCreateAction.cpp`.
- Guest discount, expensive, and refusal price thresholds in `src/openrct2/entity/Guest.cpp`.
- The expensive ride thought string in `data/language/en-GB.txt`.
- Income-page UI controls in `src/openrct2-ui/windows/Ride.cpp`.
- Park-file ride serialization in `src/openrct2/park/ParkFile.h` and `src/openrct2/park/ParkFile.cpp`.
- Cent-money representation and legacy conversion details are covered in [Money cent precision rationale](money-cent-precision-rationale.md).
