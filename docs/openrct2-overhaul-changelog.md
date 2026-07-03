# OpenRCT2 overhaul changelog

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

Correction: completed rider/train samples now publish immediately into a rolling cache of the last ten samples. The displayed ride rating is calculated from the average raw sample in that cache, so the GUI updates after rides are actually ridden while still smoothing one-off rider paths.

Correction: completed test runs now publish their measured raw stat accumulator into the same rolling sample cache. Tested rides therefore display aggregate stats from the test run instead of staying at zero until guests ride them.

Correction: Maze no longer receives descriptor base stats or `BonusMazeSize`/`BonusScenery` post bonuses. Maze pathfinding now records per-guest active samples and publishes each completed exit path into the same rolling cache, so maze length contributes only through the paths guests actually walk.

Details: [Ride rating aggregate rationale](ride-rating-aggregate-rationale.md)

### Guest generation and park rating

Decision: remove the active guest-count soft cap from normal guest generation. Park rating is now a smooth projection of average in-park guest happiness and happiness target, so crowding and queue pressure regulate future guests through happiness rather than a hardcoded suggested maximum.

Tuning: normal guest generation now scales geometrically with park value, using `$15,000` as the baseline for the tuned spawn rate. A larger park attracts more guests when rating, pricing, awards, and entry value are otherwise equal, but square-root scaling keeps the growth in arrivals sublinear.

Details: [Guest generation and park rating rationale](guest-generation-rating-rationale.md)

### Ride admission pricing

Decision: replace direct ride admission price editing with a target policy: good deal, no effect, or bad deal. Normal ride admission prices now recalculate from ride value after ratings update, while shops, toilets, and photo/item prices remain direct controls.

Correction: bad-deal pricing targets the highest conservative price happy/value-tolerant guests should still ride for, staying below the hard refusal threshold instead of crossing just above the neutral maximum.

Tuning: automatic ride-admission target prices are globally reduced to 70% before the `$20.00` cap is applied, so a former `$10.00` calculated price becomes `$7.00` and high-value rides hit the cap less easily.

Correction: guest ride-value perception now uses the same 70% scale when deciding whether a ride is good value or overpriced. This keeps the automatic target prices and guest willingness thresholds aligned.

Decision: migrate runtime `money64` from `$0.10` units to `$0.01` units. This unlocks true cent precision for ride prices and other runtime money while preserving legacy tenth-based import, save, replay, and highscore compatibility through explicit conversion bridges.

Correction: legacy tenth-based authored values now convert at runtime boundaries for ride default admission prices, computed ride value, ride upkeep, object JSON prices, terrain-edge changes, water changes, and editor initial-cash clamps. This fixes the 10x-too-small running costs, ride tickets, and construction/landscaping prices caused by treating old tables as cent values.

Correction: legacy scenario objective currency conversion is now objective-aware. Money objectives scale from tenths to cents, while non-money payloads stored in the same field, such as minimum excitement for finish-five-coasters goals, remain raw.

Correction: currency text formatting now pads cent values below `$0.10`, so `$0.05` and `$0.01` display with two decimal digits instead of `0.5` or `0.1`.

Tuning: target-pricing margins now use `$0.05` minimums for good-deal, no-effect, and bad-deal pricing because the runtime money type can represent those values.

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
