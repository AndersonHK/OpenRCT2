# OpenRCT2 overhaul changelog

## 2026-07-05

### Boat Hire return routing rollback

Correction: remove the custom Boat Hire intervention that forced a boat to return when a passenger generated the normal "I want to get off" thought. Boat Hire already has return-to-station routing, and testing showed the reported failure was caused by blocked routing rather than missing get-off handling.

Reasoning: the added helper and steering override duplicated existing ride behavior and could mask the real problem when the station route is physically blocked.

Details: [Boat hire return rationale](boat-hire-return-rationale.md)

Verification:

- Removed the regression test that asserted the deleted intervention.
- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.*`

## 2026-07-04

### Guest off-path recovery

Decision: guests walking on surface tiles now do a small local search for a reachable footpath within three tiles before falling back to random grass wandering. The search is wall-aware, respects blocked surfaces, water, in-park surface movement, and the same height tolerance used by normal surface movement.

Reasoning: guests that get pushed or dropped just off the path network should visibly try to recover when a nearby path is accessible, while still using the original random surface wandering when no local rejoin route exists.

Details: [Guest surface path rejoin rationale](guest-surface-path-rejoin-rationale.md)

Verification:

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=PathfindingTestBase.SurfaceGuestsStepTowardAdjacentPath`
- From `bin`: `.\tests.exe --gtest_filter=*Pathfinding*`
- From `bin`: `.\tests.exe`

### Mowed grass decoration

Decision: mowed grass now counts as lightweight decoration in ride scenery/proximity scans, per-tick vehicle and maze context scoring, and guest surroundings checks. Only surfaces that can actually grow grass and are in the `GRASS_LENGTH_MOWED` state count.

Reasoning: groundskeeper mowing should have a visible gameplay payoff instead of being cosmetic and staff-stat-only. Well-kept lawns now help nearby rides and guest scenery impressions through the same local scans that already reward decorations.

Details: [Mowed grass decoration rationale](mowed-grass-decoration-rationale.md)

Verification:

- `msbuild test\tests\tests.vcxproj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=TileElementWantsFootpathConnection.MowedGrassCountsAsDecoration`
- From `bin`: `.\tests.exe --gtest_filter=TileElementWantsFootpathConnection.*`
- From `bin`: `.\tests.exe --gtest_filter=RideRatings.*`

## 2026-07-02

### Windows build setup

Decision: use `openrct2.proj` through MSBuild as the local build entry point and pin MSVC `14.44.35207`, because the older default MSVC `14.38.33130` links poorly against the downloaded dependency libraries on this machine.

Correction: default the local build helper to Release for playable builds. The earlier Debug default can explain severe lag in an empty park because it produces a much larger, unoptimized executable with debug artifacts.

Decision: add a local deployment helper for `D:\Games\Independent\OpenRCT2Mod` that builds Release and mirrors this fork's built `bin\data` assets instead of mixing the develop executable with the vanilla main-branch deployment's `data` directory.

Details: [Windows local build setup](windows-local-build.md)

### Ride excitement, intensity, and nausea

Decision: introduce a raw per-tick ride rating accumulator for normal ride rating calculations. Vehicle test measurements now sample track piece, velocity, G forces, shelter, and nearby decoration into running excitement, intensity, and nausea totals. Aggregate totals finalize through a square-root curve so doubled raw stats produce about 40% higher final stats.

Correction: aggregate ratings with tick samples now start from sampled raw totals only, with descriptor base ratings and additive legacy ride-wide bonuses excluded from the aggregate finalizer.

Correction: aggregate-rated rides without current samples no longer fall back to descriptor base ratings. A ride with no sampled rider/test data displays zero aggregate stats instead of hidden base stats.

Correction: completed rider/train samples now publish immediately into a rolling cache of the last twenty samples. The displayed ride rating is calculated from the average raw sample in that cache, so the GUI updates after rides are actually ridden while still smoothing one-off rider paths.

Correction: train rating samples now average the sampled contribution from every vehicle on the train for each tick instead of using only the lead vehicle. This lets rear and middle cars affect the rating without multiplying a train's raw stats by its car count.

Correction: completed test runs now publish their measured raw stat accumulator into the same rolling sample cache. Tested rides therefore display aggregate stats from the test run instead of staying at zero until guests ride them.

Correction: Maze no longer receives descriptor base stats or `BonusMazeSize`/`BonusScenery` post bonuses. Maze pathfinding now records per-guest active samples and publishes each completed exit path into the same rolling cache, so maze length contributes only through the paths guests actually walk.

Details: [Ride rating aggregate rationale](ride-rating-aggregate-rationale.md)

### Guest generation and park rating

Decision: remove the active guest-count soft cap from normal guest generation. Park rating is now a smooth projection of average in-park guest happiness and happiness target, so crowding and queue pressure regulate future guests through happiness rather than a hardcoded suggested maximum.

Tuning: normal guest generation now scales geometrically with park value, using `$40,000` as the baseline for the tuned spawn rate. A larger park attracts more guests when rating, pricing, awards, and entry value are otherwise equal, but square-root scaling keeps the growth in arrivals sublinear.

Tuning: park rating now applies exponentially to guest generation, doubling about every 100 rating points around the `700` reference point. Rating `700` is the healthy baseline, `800` generates about twice as many guests as `700`, `600` about half as many, and `500` about half of `600`.

Correction: guest generation keeps fractional probability through all modifiers and rounds any positive non-zero final chance up to `1`, so small parks with non-zero value no longer lose normal guest generation to integer truncation.

Verification: added direct guest-generation probability tests for the `500`/`600`/`700`/`800` rating curve and the tiny positive park-value case; `PlayTests.*` passes.

Research: real park calendars support the inherited March-through-October calendar as a temperate seasonal-park abstraction, but not as a universal calendar. Northern parks often close or reduce service outside spring-fall, while warm-climate parks and holiday-event parks may keep operating in winter.

Details: [Guest generation and park rating rationale](guest-generation-rating-rationale.md), [Real park seasonality research](real-park-seasonality-research.md)

### Ride admission pricing

Decision: replace direct ride admission price editing with a target policy: discount, fair price, or expensive. Normal ride admission prices now recalculate from ride value after ratings update, while shops, toilets, and photo/item prices remain direct controls.

Correction: expensive pricing targets the highest conservative price happy/value-tolerant guests should still ride for, staying below the hard refusal threshold instead of crossing just above the fair-price maximum.

Tuning: automatic ride-admission target prices are globally reduced to 70% before the `$20.00` cap is applied, so a former `$10.00` calculated price becomes `$7.00` and high-value rides hit the cap less easily.

Correction: guest ride-value perception now uses the same 70% scale when deciding whether a ride is discounted, expensive, or too overpriced to ride. This keeps the automatic target prices and guest willingness thresholds aligned.

Correction: guests now have an expensive-but-still-rideable ride thought for the upper half of the price band between fair price and outright refusal. The old target labels were also renamed from good deal/no effect/bad deal to discount/fair price/expensive.

Correction: the expensive ride thought now uses in-universe guest wording, "isn't worth the money", instead of exposing the internal ride-stat formula.

Decision: migrate runtime `money64` from `$0.10` units to `$0.01` units. This unlocks true cent precision for ride prices and other runtime money while preserving legacy tenth-based import, save, replay, and highscore compatibility through explicit conversion bridges.

Correction: legacy tenth-based authored values now convert at runtime boundaries for ride default admission prices, computed ride value, ride upkeep, object JSON prices, terrain-edge changes, water changes, and editor initial-cash clamps. This fixes the 10x-too-small running costs, ride tickets, and construction/landscaping prices caused by treating old tables as cent values.

Correction: legacy scenario objective currency conversion is now objective-aware. Money objectives scale from tenths to cents, while non-money payloads stored in the same field, such as minimum excitement for finish-five-coasters goals, remain raw.

Correction: currency text formatting now pads cent values below `$0.10`, so `$0.05` and `$0.01` display with two decimal digits instead of `0.5` or `0.1`.

Tuning: target-pricing margins now use `$0.05` minimums for discount, fair-price, and expensive pricing because the runtime money type can represent those values.

Details: [Ride pricing target rationale](ride-pricing-target-rationale.md), [Money cent precision rationale](money-cent-precision-rationale.md)

### Park entrance pricing

Decision: replace the park admission spinner with three automatic policies: charge the richest spawning guest, maximize admission profit across the scenario's spawn-cash distribution, or stay affordable to every spawning guest. The affordable policy is the new default for newly initialized parks.

Tuning: park entrance targets use the same 70% value debuff as ride admission targets before applying guest-cash and maximum-price caps, so a park needs more ride value before its entrance fee reaches the cap.

Compatibility: direct entrance-fee commands and legacy saves are preserved through an internal custom mode. Once a player selects one of the three policies, the computed policy owns the displayed and charged entrance fee.

Save format: fork-owned `.park` changes now use the private `60000+` version band instead of upstream's next sequential version. This prevents future upstream save versions from colliding with this mod's custom ride-pricing, cent-money, and park-entrance fields when upstream development is fetched later.

Details: [Park entrance pricing target rationale](park-entrance-pricing-target-rationale.md)

### Verification

Decision: verify the work against the local Windows compiler and the existing ride-rating fixture suite.

Completed checks:

- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=RideRatings.*:S6ImportExportBasic.*:S6ImportExportAdvanceTicks.*:*RideSetPriceAction*`
- From `bin`: `.\tests.exe --gtest_filter=PlayTests.*:FormattingTests.*:EntityImportTests.*:S6ImportExportBasic.*:S6ImportExportAdvanceTicks.*`
- From `bin`: `.\tests.exe --gtest_filter=Replay/ReplayTests.*`

The build succeeds with one existing non-fatal Roslyn `System.Memory` binding warning from `openrct2.proj`.
