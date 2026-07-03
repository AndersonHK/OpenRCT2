# Guest generation and park rating rationale

This change removes the direct guest-count soft cap from guest generation and makes park rating a smooth result of guest happiness instead of a bundle of fixed global bonuses and penalties.

## Model

Park rating is now derived from the average of each in-park guest's current happiness and happiness target. The target is included so queues, crowding, litter, vandalism, pathing problems, pricing complaints, nausea, and similar guest-experience effects can pull rating downward before current happiness fully catches up.

```text
rating = average((happiness + happinessTarget) / 2) / 255 * 999
```

If there are no guests in the park, rating returns a neutral `500`. The forced park rating cheat remains an explicit override.

## Guest generation

`calculateGuestGenerationProbability()` now scales smoothly from park rating and park value. The previous rating-derived spawn rate is treated as the rate for a `$15,000` park value, then adjusted by a square-root park-value factor:

```text
valueScale = sqrt(currentParkValue / 15000.00)
spawnProbability = ratingProbability * valueScale
```

This keeps the tuned arrival rate around `$15,000`, lets larger parks draw more guests when happiness and pricing are equal, and gives diminishing returns so park value growth does not explode arrivals linearly. For example, a `$60,000` park generates about twice as many normal guests as a `$15,000` park, while a `$3,750` park generates about half as many.

Guest generation no longer divides probability when the guest count exceeds `suggestedGuestMaximum`, and it no longer has the extra 52,000-guest throttle. This means population pressure feeds back through simulation: long queues and crowded paths reduce happiness, lower happiness reduces rating, and lower rating reduces future arrivals.

Entrance-fee penalties and award effects remain because they are direct economic or scenario effects rather than population caps. Difficult guest generation now reduces probability, but it does not stop guests at a suggested maximum.

`generateGuests()` now calls `GenerateGuest()` whenever the probability roll succeeds. Marketing campaign generation is unchanged.

## Legacy suggested maximum

`calculateSuggestedMaxGuests()` and `ParkData::suggestedGuestMaximum` remain as legacy UI/script compatibility surfaces. The scripting hook `parkCalculateGuestCap` is preserved, but its result no longer gates normal guest spawning.

## Functions touched

- `src/openrct2/world/Park.cpp`: `calculateSuggestedMaxGuests`, `calculateGuestGenerationProbability`, `generateGuests`, `CalculateParkRating`.
- `src/openrct2/world/ParkData.h`: comments for `guestGenerationProbability` and `suggestedGuestMaximum`.

## Removed direct rating inputs

`CalculateParkRating()` no longer directly uses difficult park rating, guest-count range bonuses, happy-guest count thresholds, lost-guest thresholds, ride count/rating/uptime bonuses, litter counts, or casualty penalty. Those systems can still affect park rating indirectly when they change guest happiness.
