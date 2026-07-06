# Guest generation and park rating rationale

This change removes the direct guest-count soft cap from guest generation and makes park rating a smooth result of guest happiness instead of a bundle of fixed global bonuses and penalties.

## Model

Park rating is now derived from the average of each in-park guest's current happiness and happiness target. The target is included so queues, crowding, litter, vandalism, pathing problems, pricing complaints, nausea, and similar guest-experience effects can pull rating downward before current happiness fully catches up.

```text
rating = average((happiness + happinessTarget) / 2) / 255 * 999
```

If there are no guests in the park, rating returns a neutral `500`. The forced park rating cheat remains an explicit override.

## Guest generation

`CalculateGuestGenerationProbability()` now scales smoothly from park rating and park value. Park rating uses an exponential multiplier centered on the strong-but-normal `700` rating range, then park value applies the existing square-root scale:

```text
ratingScale = 2 ^ ((parkRating - 700) / 100)
valueScale = sqrt(currentParkValue / 50000.00)
spawnProbability = 850 * ratingScale * valueScale
```

At the `$50,000` park-value baseline, rating `600` produces about half the guests of rating `700`, rating `800` produces about twice the guests of rating `700`, and rating `500` produces about half the guests of rating `600`. This makes the `600` to `700` range the normal healthy range, supercharges parks above `700`, and craters arrivals as ratings fall below `600`.

Park value still gives diminishing returns so value growth does not explode arrivals linearly. For example, at the same rating, a `$200,000` park generates about twice as many normal guests as a `$50,000` park, while a `$12,500` park generates about half as many.

Guest generation now keeps its intermediate math in floating point until all rating, value, difficult-generation, entrance-fee, and award modifiers have been applied. A positive non-zero raw probability below `1` is rounded up to `1`, so small viable parks keep a tiny chance of generating guests instead of truncating to zero. A park value of `$0.00` still produces zero normal generation.

Guest generation no longer divides probability when the guest count exceeds `suggestedGuestMaximum`, and it no longer has the extra 52,000-guest throttle. This means population pressure feeds back through simulation: long queues and crowded paths reduce happiness, lower happiness reduces rating, and lower rating reduces future arrivals.

Entrance-fee penalties and award effects remain because they are direct economic or scenario effects rather than population caps. Difficult guest generation now reduces probability, but it does not stop guests at a suggested maximum.

`generateGuests()` now calls `GenerateGuest()` whenever the probability roll succeeds. Marketing campaign generation is unchanged.

## Verification

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.GuestGeneration*`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.*`

## Calendar and seasonality

OpenRCT2 still uses the inherited RCT2 eight-month operating year, March through October. Real park calendars make that a plausible abstraction for many temperate seasonal parks, but not a universal rule: some parks close or largely close in winter, some run Halloween or holiday-event shoulder seasons, and warm-climate parks may operate year-round or near year-round.

This change does not add a seasonality profile to guest generation. If the calendar is later expanded to twelve civil months, the extra months should probably be scenario/profile-driven rather than treated as full-service summer months. Research notes: [Real park seasonality research](real-park-seasonality-research.md).

## Legacy suggested maximum

`calculateSuggestedMaxGuests()` and `ParkData::suggestedGuestMaximum` remain as legacy UI/script compatibility surfaces. The scripting hook `parkCalculateGuestCap` is preserved, but its result no longer gates normal guest spawning.

## Functions touched

- `src/openrct2/world/Park.cpp`: `calculateSuggestedMaxGuests`, `CalculateGuestGenerationProbability`, `generateGuests`, `CalculateParkRating`.
- `src/openrct2/world/Park.h`: exposes `CalculateGuestGenerationProbability` alongside the other park calculation helpers so the curve can be tested directly.
- `src/openrct2/world/ParkData.h`: comments for `guestGenerationProbability` and `suggestedGuestMaximum`.

## Removed direct rating inputs

`CalculateParkRating()` no longer directly uses difficult park rating, guest-count range bonuses, happy-guest count thresholds, lost-guest thresholds, ride count/rating/uptime bonuses, litter counts, or casualty penalty. Those systems can still affect park rating indirectly when they change guest happiness.
