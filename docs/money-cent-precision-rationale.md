# Money cent precision rationale

This change migrates runtime `money64` values from tenths of a pound to cents. The goal is to let ride prices, guest cash, park finances, and displayed currency values use true `$0.01` precision while preserving compatibility with older saves, imports, replays, and highscores that were authored in `$0.10` units.

## Representation

`money64` is now `fixed64_2dp`, and `_GBP`/`ToMoney64FromGBP()` scale by 100. This makes `1.00_GBP` store as `100`, `0.05_GBP` store as `5`, and `0.01_GBP` store as `1`.

`money16` and `money32` remain legacy one-decimal formats. They are still used by old RCT1/RCT2 data, import structures, and compatibility file paths. `ToMoney64(money16/money32)` multiplies by 10, and `ToMoney16(money64)` rounds cents back to tenths when a legacy writer needs that shape.

Currency formatting pads fractional cent values to two digits, including amounts below `$0.10`. Without that padding, `0.05_GBP` would display as `0.5`, which is ambiguous once cents are real runtime values.

## Compatibility

Park files use fork-private version `60001` as the cent-money boundary. Version `60001` stores `money64` in cents. Older park-file versions are read and written through conversion helpers that multiply legacy values by 10 on read and divide by 10 on write. The conversion covers park cash, loans, fees, histories, objectives, ride prices, ride value/profit/upkeep, guest paid/cash fields, and money-effect sprites.

The fork-owned save-format changes intentionally start at `60000` rather than at upstream's latest version plus one. This keeps upstream's normal sequential versions free for future OpenRCT2 merges; when upstream later adds `64`, `65`, and so on, those numbers should not be interpreted as this mod's custom fields. New saves currently use `.park` version `60002` because park entrance pricing policies add a saved non-money field after the cent-money migration.

Classic importers and object loaders still receive legacy tenth-based values. RCT1/RCT2 import paths, scenario index values, scenery object prices, banner prices, and path addition prices now convert those values into cent-based runtime money as they enter the modern model.

Scenario objective currency is a union with non-money objective data in legacy formats. Importers and `.park` compatibility paths therefore use `ObjectiveNeedsMoney()` before converting that field. Money goals are multiplied from tenths to cents, while minimum-excitement goals keep their raw rating value. The scenario index cache version is bumped so stale cached objective rows are rebuilt under the corrected conversion.

Scenario highscores were bumped to file version 3. Version 1 `money32` and version 2 legacy `money64` company values are converted into cent money when loaded, while new highscores write cent-based `money64`.

Replay files were bumped to version 12. Older replay snapshots convert guest paid/cash fields and money-effect values from tenths to cents after loading. Old replay checksum comparison still has one unavoidable compatibility wrinkle: entity checksums include serialized money fields. During pre-version-12 playback, guest money is temporarily projected back to legacy units for checksum calculation, and any remaining legacy checksum mismatch is logged once instead of failing playback.

## Gameplay fixes

Guest shop and cash logic had several raw numeric comparisons that assumed tenths. Those now use `_GBP`, `ToMoney64()`, or `ToMoney16()` so value judgments, satisfaction, generated cash, and shop happiness continue to mean the same thing after the storage scale changed.

Ride target pricing now uses `$0.05` minimum margins for discount, fair-price, and expensive targets. Because the runtime money type can represent cents, the previous `$0.10` precision limitation no longer applies.

Ride descriptors intentionally keep their old tenth-based `DefaultPrices` and `UpkeepCosts` tables. `RideCreateAction` converts default admission prices with `ToMoney64()`, `RideRatingsCalculateValue()` stores computed ride value as cent money, and `RideComputeUpkeep()` converts its final legacy upkeep calculation once. This preserves the old authored tables while preventing new rides, automatic target prices, and running costs from becoming 10x too small.

Object JSON prices for banners, path additions, walls, and terrain surfaces are also legacy tenth-based values, matching their DAT importers. Those JSON readers now convert through `ToMoney64()`. Raw landscaping constants for terrain-edge and water changes were replaced with `_GBP` literals, and the scenario-editor initial-cash clamp was updated to the same cent scale.

Ride-window plus/minus buttons still step by `$0.10`; typed prices and commands can use exact cent values. This keeps the classic button ergonomics while preserving the new `$0.01` base unit.

## Functions touched

- `src/openrct2/core/Money.hpp` and `src/openrct2/core/FixedPoint.hpp`: define cent-based `money64`, literals, and legacy conversion helpers.
- `src/openrct2/localisation/Currency.cpp` and `src/openrct2/localisation/Currency.h`: update exchange rates, parsing, and formatting comments for cent money.
- `src/openrct2/park/ParkFile.h` and `src/openrct2/park/ParkFile.cpp`: add the fork-private version `60001` cent-money boundary and read/write conversion helpers for legacy park-file money fields.
- `src/openrct2/rct1/S4Importer.cpp`, `src/openrct2/rct2/S6Importer.cpp`, `src/openrct2/scenario/ScenarioObjective.cpp`, `src/openrct2/scenario/ScenarioRepository.cpp`, and object loaders under `src/openrct2/object/`: convert only money objective fields and legacy import/object prices into cent money.
- `src/openrct2/GameStateSnapshots.h`, `src/openrct2/GameStateSnapshots.cpp`, and `src/openrct2/ReplayManager.cpp`: convert pre-cent replay snapshots and preserve old replay playback compatibility.
- `src/openrct2/entity/Guest.cpp`: convert raw shop, satisfaction, and generated-cash thresholds to the new money scale.
- `src/openrct2/actions/ride/RideCreateAction.cpp`, `src/openrct2/ride/Ride.cpp`, and `src/openrct2/ride/RideRatings.cpp`: convert legacy ride descriptor prices, ride value, and upkeep to cent money at runtime boundaries, and lower target-pricing minimum margins to `$0.05`.
- `src/openrct2/actions/terraform/SurfaceSetStyleAction.cpp`, `src/openrct2/actions/terraform/WaterSetHeightAction.cpp`, `src/openrct2/Editor.cpp`, and `src/openrct2-ui/windows/Ride.cpp`: update remaining raw money constants and ride-price button step behavior.
- `test/tests/FormattingTests.cpp`, `test/tests/MoneyTests.cpp`, `test/tests/PlayTests.cpp`, and test project files: cover cent parsing/formatting, legacy conversion helpers, ride-create default price conversion, and direct cent price preservation.
