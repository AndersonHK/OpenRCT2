# Object-defined boarding durations

Research and initial tuning recommendation, 2026-09-27; values subsequently approved by the user and authored in the sibling
object repo (279 ride objects, 336 passenger car definitions). The development object directories are linked to that checkout.
See [engine semantics and validation](guest-services-and-boarding.md). These remain initial game tuning, not a claim of
comprehensive visual play-testing across every vehicle.

## What the duration represents

`properties.cars[].boardingDuration` is an additional **per-passenger settling time in simulation seconds**, starting after the passenger reaches the seat's boarding position. When `cars` is an object rather than an array, the property belongs directly inside that object. It represents sitting down, arranging feet and settling into the restraint. Walking to the seat and the ride's existing dispatch wait remain separate. Passengers should settle concurrently; a train with 24 passengers and a two-second duration must not acquire a 48-second delay.

The recommended 1–3 seconds are deliberately compressed game tuning, not measured real boarding or safety-check durations. Capacity, number of seats and ride intensity alone should not determine this delay. A higher-speed train can have the same accessible seating as a slower train. Differences should follow the vehicle's seating geometry, not its cosmetic theme.

## Evidence and inventory

The sibling `OpenRCT2-objects/objects` inventory contains **340 ride objects**, of which **287 have at least one car with `numSeats > 0`**. This includes building and mini-golf placeholders. Excluding those eight objects leaves **279 actual passenger-vehicle objects** for this recommendation. Both standalone `*.json` files and directory-based `object.json` files were inspected. Cars include seatless locomotives, tenders and invisible secondary vehicle parts; these must not receive a positive duration just because their parent ride carries passengers.

External manufacturer information supports the distinctions, but supplies no directly transferable 1–3 second timing:

- Wiegand describes an Alpine Coaster sled for two adults with individual belts and a belt lock. This supports treating it as a low seat with a fastening step, rather than an instant walk-on vehicle. [Wiegand Alpine Coaster](https://www.wiegandslide.com/en/products/alpine-coaster.html)
- B&M distinguishes conventional seated, floorless, inverted and face-down flying positions. The additional game delay for the latter groups is a design inference from those positions. [B&M coaster families](https://www.bolliger-mabillard.com/coasters)
- B&M's Surf Coaster uses a standing position and a distinct restraint system. This supports giving standing vehicles their own upper-duration tier; it does not establish a measured boarding time for the older RCT stand-up models. [B&M Surf Coaster](https://www.bolliger-mabillard.com/surf-coaster)

## Complete family mapping

Apply the family value below to every actual passenger car (`numSeats > 0`) of the matching `properties.type`, then apply the object-specific overrides in the next section. All RCT1, RCT2, expansion and OpenRCT2 variants inherit the same family rule. Object IDs take precedence over type where several seating styles share track compatibility. A future unlisted family should be reviewed rather than silently assigned a value.

`properties.type` may be a string or an array. Normalize it to an array, then use the **first type** for the base family lookup. Validate that any additional types have the same duration; if they disagree, require an explicit object-ID override rather than silently taking the maximum tier. Track compatibility does not itself make the physical seat harder to board. The current inventory has seven array-valued objects: `openrct2.ride.alpine_coaster`, `openrct2.ride.hybrid_coaster`, `openrct2.ride.modern_twister`, `openrct2.ride.single_rail_coaster`, `rct2dlc.ride.zpanda`, `rct1.ride.inverted_trains` and `rct2.ride.rckc`. Six are singleton arrays. Only `rct1.ride.inverted_trains` has multiple types (`compact_inverted_rc`, `inverted_rc`), both recommending 2.5 seconds, so no additional override is needed.

| Seconds | Families (`properties.type`) | Rationale |
| --- | --- | --- |
| 1.0 | `chairlift` | Open bench and a quick sit; preserve the cadence of continuously moving chairs. |
| 1.0 | `lift`, `observation_tower` | Step into a roomy cabin; most positioning is already represented by approach movement. |
| 1.5 | `miniature_railway`, `monorail`, `suspended_monorail` | Accessible passenger seating, with a modest settling pause after walking into the car. |
| 1.5 | `car_ride`, `ghost_train`, `mini_helicopters`, `dodgems`, `flying_saucers` | Small, generally accessible seated vehicles; enough time to sit and place feet without dominating a gentle ride's cycle. |
| 1.5 | `classic_mini_rc`, `junior_rc`, `mini_rc`, `side_friction_rc`, `wooden_wild_mouse` | Simple conventional seats; lower initial tuning than larger restrained trains. |
| 1.5 | `log_flume` | Step over the side into a low boat, balanced against the short station cadence. |
| 1.5 | `ferris_wheel`, `merry_go_round`, `twist`, `motion_simulator` | Ordinary cabin/bench or compact flat-ride seating; existing loading waypoints already represent the longer walk around the platform. |
| 2.0 | `go_karts`, `monster_trucks`, `monorail_cycles`, `steeplechase`, `alpine_rc` | Low cockpit, higher step, straddling or explicit seat-belt positioning. Karts should visibly pause before becoming occupied. |
| 2.0 | `boat_hire`, `dinghy_slide`, `river_rafts`, `river_rapids`, `splash_boats`, `water_coaster`, `submarine_ride` | Low boat seating or enclosed entry merits a deliberate settling step. Themed boats inherit the same timing. |
| 2.0 | `bobsleigh_rc`, `classic_wooden_rc`, `classic_wooden_twister_rc`, `corkscrew_rc`, `giga_rc`, `hybrid_rc`, `hyper_twister`, `hypercoaster`, `lim_launched_rc`, `looping_rc`, `lsm_rc`, `mine_ride`, `mine_train_rc`, `reverser_rc`, `single_rail_rc`, `spinning_wild_mouse`, `spiral_rc`, `steel_wild_mouse`, `twister_rc`, `virginia_reel`, `wooden_rc` | Standard restrained seated-coaster baseline. Spinning and articulated cars use the same value unless their seating position changes. |
| 2.0 | `swinging_ship`, `magic_carpet` | Seated row and restraint settling, without a distinct posture change. |
| 2.5 | `compact_inverted_rc`, `inverted_hairpin_rc`, `inverted_impulse_rc`, `inverted_rc`, `mini_suspended_rc`, `suspended_swinging_rc`, `vertical_drop_rc`, `air_powered_vertical_rc`, `reverse_freefall_rc` | Elevated, suspended, tightly enclosed or strongly restrained seats get a small extra positioning allowance. |
| 2.5 | `enterprise`, `launched_freefall`, `roto_drop`, `swinging_inverter_ship`, `top_spin` | Enclosed individual cars or substantial restraint positioning. |
| 3.0 | `classic_stand_up_rc`, `stand_up_rc`, `flying_rc`, `lay_down_rc`, `multi_dimension_rc`, `heartline_twister_rc`, `space_rings` | Unusual standing, prone, rotating-seat or body-enclosing positions receive the largest short delay. |

## Object-specific overrides

These IDs share a broader track family but have a clearly different boarding posture. The same duration applies to every passenger-carrying car variant within the object, including reversed visual variants.

| Seconds | Object IDs | Reason |
| --- | --- | --- |
| 2.5 | `rct1aa.ride.floorless_twister_trains`, `rct2.ride.bmfl` | Floorless variant of the ordinary `twister_rc` family. |
| 3.0 | `rct1aa.ride.stand_up_twister_trains`, `rct2.ride.bmsu`, `rct2ww.ride.surfbrdc` | Standing/surfing posture despite `twister_rc` track compatibility. |
| 3.0 | `rct1.ride.swinging_lay_down_cars`, `rct2.ride.skytr` | Lay-down seating despite `mini_suspended_rc` track compatibility. |

Useful review examples are `rct2.ride.kart1`, `rct1.ride.go_karts`, `rct2tt.ride.1920racr` and `rct2tt.ride.cavmncar` at 2.0 seconds; `openrct2.ride.alpine_coaster` at 2.0; `openrct2.ride.modern_twister` at 2.0; `rct2.ride.bmair` at 3.0; and `rct2.ride.clift1`/`clift2` at 1.0.

## Zero-duration exceptions and continuous loaders

- Keep `mini_golf` at zero even though its golfer pseudo-car has a seat. Do not delay a guest at each course transition as though boarding a vehicle.
- Keep the building placeholders `3d_cinema`, `circus`, `crooked_house` and `haunted_house` at zero. Their admission/room occupancy is not an actual car-boarding operation. A motion simulator has a physical passenger cabin and therefore retains its separate 1.5-second recommendation.
- `maze`, `spiral_slide`, shops and facilities are not vehicle boarding. Leave them without this property. Likewise omit it from every seatless car variant, locomotive, tender or visual helper.
- Missing properties and legacy DAT objects should retain a zero-duration compatibility default. Explicit zero should disable the feature. The authoring rules above must not become an engine-side hard-coded ride-type default.
- A continuous-circuit operating mode is not necessarily a continuously moving station. Do not suppress the delay on all continuous-circuit coasters. The chairlift's continuously moving loading operation is a special case; use the minimum one second and verify that the engine holds a valid boarding reservation for the full delay.
- Test log flumes, rapids, rafts, boat hire, chairlifts and the Ferris wheel independently. A delay must not be restarted every update, reapplied at each loading waypoint or bypassed by a moving vehicle. If a loading mechanism cannot safely accommodate the delay, explicitly leave that object's value zero until that mechanism is supported and record the exception here rather than claiming that it has been tuned.

## Validation before adopting the values

Check that every current nonexcluded passenger family appears exactly once in the table, all overrides match existing IDs, and only positive-seat cars receive a positive value. At 40 simulation ticks per second, the five tiers are exactly 40, 60, 80, 100 and 120 ticks, avoiding fractional-tick ambiguity. Validate these against the engine's authoritative tick constant when implementing.

An inventory check normalizing both `type` and `cars` to arrays confirmed **279 eligible objects and 336 positive-seat car definitions**. All 77 primary passenger families appear once in the mapping; no family is missing. Multi-type objects are counted once per object, not once per compatible type.

Exercise both seats of paired cars, independently boarding seats, multi-car trains, partial platform loads, flat-ride loading waypoints and save/load during the pause. Observe concurrent completion and dispatch, not just the elapsed timer. Compare a short gentle ride, the kart test and a busy coaster before and after tuning. The values are initial defaults; throughput and animation observations may justify adjusting individual objects within the same 1–3 second range.
