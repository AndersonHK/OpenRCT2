# Park entrance pricing target rationale

Park admission now uses a pricing policy instead of a fixed player-edited spinner. The three visible policies are:

- `Richest guest`: charge the most money any newly spawned guest can have in the current scenario.
- `Max profit`: choose the price that maximizes admission revenue across the scenario's four equally likely spawn-cash samples. Guests whose starting cash is below the admission fee leave instead of entering, so the calculation tests every meaningful cash tier.
- `All guests`: charge no more than the least money any newly spawned guest can have. This is the default for newly initialized parks.

All three policies are also capped by the park's computed entrance value and by `kMaxEntranceFee`. The park entrance value is `totalRideValueForMoney * 0.7`, matching the ride-admission debuff. This means a park that previously reached a given entrance target at `$10.00` of value now needs about `$14.29` of value to reach that same target, and high-value parks hit the admission cap later.

The spawn-cash samples mirror `Guest::generate`: scenario cash minus `$10.00`, scenario cash, scenario cash plus `$10.00`, and scenario cash plus `$20.00`, clamped at zero. Scenarios with guest initial cash set to `$0.00` retain the existing special case where guests spawn with `$50.00`.

The park window exposes only the three policy options. A hidden `custom` policy exists to preserve older saves, scenario-editor fixed fees, scripts, and legacy game actions that still send a literal entrance fee. Selecting any visible policy replaces that custom fee with the computed target price.

Functions touched:

- `Park::GetEntranceFee`, `Park::GetEntranceFeeForTarget`, and `Park::UpdateEntranceFee` in `src/openrct2/world/Park.cpp`: compute the current gate price from the selected policy and current park value.
- `calculateGuestGenerationProbability` in `src/openrct2/world/Park.cpp`: compares entrance fee against the debuffed entrance value rather than raw ride value.
- `ParkSetEntranceFeeAction` in `src/openrct2/actions/park`: accepts encoded policy targets while preserving literal-fee commands as custom pricing.
- `ParkFile` park chunk serialization in `src/openrct2/park/ParkFile.cpp`: saves the selected entrance policy from fork-private park-file version `60002` onward.
- `ParkWindow` in `src/openrct2-ui/windows/Park.cpp`: replaces the admission spinner with the three-option dropdown and displays the computed current price for each option.
