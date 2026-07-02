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

Tuning: normal guest generation now scales geometrically with park value, using `$11,000` as the baseline for the tuned spawn rate. A larger park attracts more guests when rating, pricing, awards, and entry value are otherwise equal, but square-root scaling keeps the growth in arrivals sublinear.

Details: [Guest generation and park rating rationale](guest-generation-rating-rationale.md)

### Ride admission pricing

Decision: replace direct ride admission price editing with a target policy: good deal, no effect, or bad deal. Normal ride admission prices now recalculate from ride value after ratings update, while shops, toilets, and photo/item prices remain direct controls.

Correction: bad-deal pricing targets the highest conservative price happy/value-tolerant guests should still ride for, staying below the hard refusal threshold instead of crossing just above the neutral maximum.

Tuning: automatic ride-admission target prices are globally reduced to 80% before the `$20.00` cap is applied, so a former `$10.00` calculated price becomes `$8.00` and high-value rides hit the cap less easily.

Constraint: true `$0.05` ride-price precision is not represented by the current `money64` type, which stores money in `$0.10` units. That request is documented as requiring a broader money-format migration.

Details: [Ride pricing target rationale](ride-pricing-target-rationale.md)

### Verification

Decision: verify the work against the local Windows compiler and the existing ride-rating fixture suite.

Completed checks:

- `msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207`
- From `bin`: `.\tests.exe --gtest_filter=RideRatings.*:S6ImportExportBasic.*:S6ImportExportAdvanceTicks.*:*RideSetPriceAction*`

The build succeeds with one existing non-fatal Roslyn `System.Memory` binding warning from `openrct2.proj`.
