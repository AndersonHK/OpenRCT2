

<p align="center">
  <a href="https://openrct2.io">
    <img src="https://raw.githubusercontent.com/OpenRCT2/OpenRCT2/develop/resources/logo/icon_x128.png" style="width: 128px;" alt="OpenRCT2 logo"/>
  </a>
</p>

<h1 align="center">OpenRCT2 - Personal Mod Fork</h1>

<h3 align="center">A personal gameplay and economy mod fork of OpenRCT2, an open-source re-implementation of RollerCoaster Tycoon 2.</h3>

---

## Personal fork notice

This repository is a fork built for my personal use. It is not the official OpenRCT2 project, and it intentionally changes some core gameplay and economy behaviour.

If you are looking for normal OpenRCT2, the correct upstream repository is [OpenRCT2/OpenRCT2](https://github.com/OpenRCT2/OpenRCT2), and the official downloads are at [openrct2.io](https://openrct2.io). You should use those unless you are specifically after this personal mod fork.

Although this fork is built for my own use, I hope parts of it may prove useful to other modders, or even as ideas that can be adapted back into core OpenRCT2. The original OpenRCT2 licence still applies; see [licence.txt](licence.txt).

The rest of this README still includes the upstream OpenRCT2 project information so the fork stays easy to understand in context. The section below is the important part if you are trying to understand what makes this fork different.

## High-level mod changes

### Park audio is world-space and surround-aware

Ride music, vehicle noise, crashes, and other world effects are now positioned from their three-dimensional distance and direction relative to an elevated virtual camera. Its height comes from the isometric viewport's visible ground footprint, so zooming toward an area makes focused sources louder and horizontally remote sources comparatively fainter. Object height matters, and continuous source-class curves replace screen cutoffs and fixed distance rings: coaster and rider sounds remain local, while amplified ride music carries much farther across the park. Visible crowds form a diffuse directional field rather than one global front-channel loop.

Moving vehicles and camera motion also produce a smoothed, bounded Doppler shift from their relative radial movement. The mixer prefers 48 kHz 7.1 output and falls back to stereo when the selected device cannot provide it. It mixes into a floating-point bus with headroom and peak limiting, stores thousands of channels, uses planar AVX2 mixing with scalar fallback, prioritizes up to 2,048 vehicle emitters, and keeps ride music to the strongest 64 sources so distant music remains present without becoming an indistinct wall of songs. Crashed rides can finish the active music track rather than losing it on the next tick.

More detail, including Logitech GHub setup and the exact channel layout: [Spatial audio overhaul](docs/spatial-audio-overhaul.md).

### Ride stats are sampled from actual rides

Excitement, intensity, and nausea are no longer meant to be mostly post-processed from a ride-wide recipe. For aggregate-rated rides, the mod samples the ride while it is testing and while guests actually ride it, adding raw stat contributions from velocity, G-forces, track pieces, shelter, nearby scenery, paths, nearby rides, and synchronized operation.

The reason for this change is to make ratings feel more like a property of the trip the guest experienced. A coaster that is twice as long should collect roughly twice as much raw rating material, but the final displayed score should have diminishing returns rather than doubling outright.

Current balancing uses a square-root finalizer, roughly `sqrt(raw / 1000) * 100`. Train ticks average the contribution of every vehicle on the train, then completed rider, train, and test-run samples are kept in a rolling cache of the last twenty samples. On rides with multiple stations, those samples are published for the physical directed station-to-station leg that was actually travelled; the measurements tab can select every observed origin/destination pair instead of presenting a misleading full-track circuit. Speed and the five G-force extrema belong to the selected leg, while ride time and length remain global instead of being duplicated in both sections. Transport legs retain their time and distance because transport rides do not show the global construction summary. The ride-list compatibility value is not replaced until every station has supplied at least one outbound leg. G-force scoring uses smooth curves informed by the original ride-wide thresholds: 0G airtime is fun, negative G and high positive G become harsher, and lateral G grows the fastest so a brief high-speed unbanked turn can matter more than a longer mild turn. Decoration excitement is scaled by vehicle speed, so slow rides do not inflate their scenery stats simply by lingering near the same objects. Decoration visibility also depends on the ride type: fully enclosed shows receive no outside-decoration bonus, partly visible enclosed rides receive reduced bonuses, and mazes now treat their walls as sight blockers while still allowing tall or elevated objects to be seen over them. Aggregate-rated rides with no samples display zero aggregate stats instead of hidden base ratings. Mazes are handled the same way: they gain stats from the paths guests actually walk, not from a flat maze-size bonus.

More detail: [Ride rating aggregate rationale](docs/ride-rating-aggregate-rationale.md).

### Transport rides are part of guest routing

Guests now consider railways, monorails, chairlifts, and lifts as complete station-to-station journeys when travelling to a ride, shop, facility, or park exit. A journey through a transport with more than two stations composes the exact measured directed legs between adjacent stops, including their time, distance, comfort, decoration, and fare. Guests can remain aboard through intermediate stations, no longer board a transport merely because it is free, and do not count completing transport as an ordinary attraction visit.

Route choice compares milliseconds of walking with walking to a station, expected queue and all onboard segment times, and the remaining walk. Free, Discount, and Fair pricing use progressively stricter time thresholds; rain triggers a new comparison and increasingly favours sheltered transport. Extortive service is a last-resort connection only when neither walking nor non-extortive transport works. Park exits and all ride/facility entrance targets share this destination-routing path. Journey value is led by segment distance, then modified by actual average speed, distance-weighted G-force comfort, and sampled decoration quality; the measurements tab displays those service stats and the income tab exposes the four proportional fare policies.

Rail transport stations also use a visible second-stage queue. After a train physically clears the station, up to one actual
consist-load of guests leaves the external queue and waits at car/seat-aligned platform positions. They retain FIFO order,
bind only after an arriving train has unloaded and exposed a real empty seat, and pay only at that point. Through-riders take
their seats first; excess staged guests remain for the next service. Miniature Railway, Monorail, and Suspended Monorail use
this system now, and Chairlift uses its native two-seat loading positions with the same visible staging contract. Lift retains
just-in-time boarding because its waypoint cabin does not expose a trustworthy platform-clear event.

More detail: [Transport ride routing rationale](docs/transport-ride-routing-rationale.md).

### Performance work is measured against EverythingPark

Turbo's `320` simulation ticks per second require a `3.125 ms` tick budget, so this fork now separates actual TPS from
rendered FPS and includes a warmed CLI benchmark with latency percentiles and profiler export. Guest pathfinding caches stable
path adjacency and thin-junction classification by edited map chunk; exact ordinary layouts avoid repeated neighbouring-tile
scans while unusual or preview-affected layouts retain the original live fallback. Vehicle updates reuse matching ride and
loaded vehicle-object pointers only for the lifetime of one synchronous train-head update, and typed entity iteration avoids
reacquiring global state for every list element.

Guests walking to park exits and resolved ride or facility entrances also share deterministic reverse route fields built
from exact path topology. Fields build in parallel from a main-thread snapshot and publish in stable target order. Live
banners, queue ownership, junction history, transport policy, and other guest-specific decisions still gate each proposed
step, with the bounded heuristic retained whenever a field is missing, stale, inexact, or unsuitable. Two repeated
EverythingPark runs at the shared-field checkpoint reached 241.767 and 241.871 TPS. After the station-less facility fast path
and live-rating eligibility gate, two runs reached 254.297 and 256.592 TPS. The exact directed-leg/save/cache checkpoint
measured 251.729 and 251.123 TPS. After the reviewed upstream integration and hot-path cleanup, two runs reached 261.961 and
263.398 TPS with matching `72638ee2...` checksums and 3.699/3.692 ms medians. The faster run is 58.8% above the original
165.895-TPS baseline, although the remaining rating-environment lookup still dominates vehicle time. The detailed performance
plan records the full latency and profiler breakdown.

The Vulkan renderer remains gated while it grows toward visual parity. It now executes the complete indexed line, opaque,
masked, remapped, transparency/blend, and ordered rain/snow command stream before final palette presentation. Swapchain
selection can prefer a 10-bit HDR10 BT.2020/PQ output with an SDR-preserving paper-white mapping and falls back to the
ordinary SDR path when the display or driver does not expose a compatible format. The direct drawing context records GPU
commands into a generation-aware persistent atlas without routine full-frame upload or readback; ordered `CopyRect`, the
synchronous screenshot adapter, deterministic line coverage, device-loss recovery, and visual SDR/HDR parity still block
normal user selection.

An opt-in Vulkan validation engine now exercises palette, resize, VSync, presentation, and fence-backed asynchronous indexed
readback across the normal factory/window lifecycle. It temporarily uploads the authoritative X8 canvas once per frame, so
that bridge is excluded from performance acceptance; only the direct GPU command/atlas path may enter renderer performance
acceptance after the remaining parity and lifecycle gates pass.

More detail: [EverythingPark 320 TPS refactor plan](docs/performance-320-tps-refactor-plan.md),
[Path topology cache](docs/path-topology-cache.md), [Shared destination route fields](docs/shared-route-fields.md),
[Vulkan renderer migration](docs/vulkan-renderer-migration.md), and the reviewed
[upstream merge manifest](docs/upstream-merge-manifest.md).

### Guest growth is regulated by happiness instead of a soft cap

Normal guest generation no longer directly slows down just because the park has passed a suggested guest maximum. Park rating is now calculated smoothly from the average of guest happiness and guest happiness target, so long queues, crowded paths, bad pricing, litter, nausea, and similar problems reduce future demand through guest experience.

The reason for this change is to make park population pressure emerge from the simulation. A successful park should attract more guests, but if the park cannot absorb them, the resulting crowding and unhappiness should naturally pull the rating and arrival rate down.

Current balancing treats park rating `700` as the healthy baseline, doubles or halves guest generation for roughly every 100 rating points above or below that, then scales arrivals by `sqrt(parkValue / 40000)`. That means high-rating parks can accelerate hard, low-rating parks crater, and larger parks still attract more guests with diminishing returns. Small parks with any positive value keep a tiny non-zero generation chance instead of rounding down to nothing.

More detail: [Guest generation and park rating rationale](docs/guest-generation-rating-rationale.md).

### Nausea is treated as an active guest need

Guest nausea is no longer only a hidden post-ride value that occasionally produces vomiting or a thought. Sick guests begin responding earlier, with first-aid interest starting at the sick threshold and the visible sick face/animation following one point later.

The reason for this change is to make nausea legible as a condition the park can manage. Very full guests now gain more ride nausea across the whole hunger bar, sick guests will commute farther to first aid as their nausea gets worse, and guests already heading to first aid will not abandon that intent just because they are briefly calm enough to sit down.

Current balancing starts first-aid interest at nausea `128`, scales first-aid search from one tile at that threshold to 128 tiles at maximum nausea, and keeps first-aid use valid once a guest has committed to the clinic.

More detail: [OpenRCT2 overhaul changelog](docs/openrct2-overhaul-changelog.md).

### Guests try to recover onto nearby paths

Guests that end up off a footpath no longer rely purely on random grass wandering. When standing on a surface tile, they first look for a reachable footpath within three tiles and step toward the nearest one they can access.

The reason for this change is to make off-path behavior look less broken. If a guest is only a tile or two away from the path network, they should usually try to rejoin it instead of meandering deeper into the park lawn.

Current balancing keeps this intentionally local: the search radius is three tiles, it respects walls, blocked surfaces, water, ownership, and height differences, and the original random wandering remains the fallback when no nearby path is reachable.

More detail: [Guest surface path rejoin rationale](docs/guest-surface-path-rejoin-rationale.md).

### Mowed grass matters as decoration

Mowed grass now counts as nearby decoration for ride scenery checks, per-tick ride context, maze path samples, and guest scenery impressions.

The reason for this change is to give groundskeeper mowing a real gameplay purpose. A tidy lawn beside a ride or path should make the area feel more cared for, rather than being purely visual.

Current balancing treats each growable mowed-grass tile as one lightweight decoration item. Grass that is merely short, growing, clumped, underwater, non-grass terrain, or ghosted does not count.

More detail: [Mowed grass decoration rationale](docs/mowed-grass-decoration-rationale.md).

### Money uses cent precision

Runtime money now uses `$0.01` precision instead of the old `$0.10` precision. This affects ride prices, guest cash, park finances, scenario money, save data, replay compatibility paths, and money formatting.

The reason for this change is partly practical and partly balance-related. The pricing changes need smaller increments than ten cents, and the game had several legacy tables and import paths that would become ten times too small if their old tenth-based values were treated as cents without conversion.

Current balancing keeps legacy authored values compatible by converting old tenth-based money at runtime boundaries. Ride price buttons still step by `$0.10` for convenience, but typed prices and commands can use exact cent values. Target-pricing margins currently use `$0.05` minimums.

More detail: [Money cent precision rationale](docs/money-cent-precision-rationale.md).

### Time measurements use real 40 TPS conversions

Real-time displays and real-time gameplay settings now use `40` game ticks per second as the canonical clock. Seconds, minutes, hours, and bought advertising weeks are no longer allowed to silently use legacy `32` TPS shortcuts, `2048`-tick pseudo-minutes, calendar-day queue approximations, unrelated "per hour" denominators, or the old inflated ride-length scale.

The reason for this change is to make the same time word mean the same thing across ride duration, station waiting time, queue time, customer and income rates, air time, inspection time, running costs, loan interest, marketing weeks, guest time in park, and speed-versus-length reporting.

Current balancing keeps the existing RCT operating calendar for months and monthly finance periods, but real-time conversions go through explicit helpers. A week is seven calendar days when a feature is actually sold or graphed as weeks. Ride length is now corrected at the stored stat level to match the horizontal scale implied by park area and vehicle speed, while height-scale inconsistencies are documented as future work.

More detail: [Time measurement fix ledger](docs/time-measurement-fix-ledger.md).

### Ride admission is target-based and globally toned down

Normal ride admission pricing is no longer just a direct price field. Rides can target one of three value bands: discount, fair price, or expensive. Prices are recalculated from the ride's current value after ratings update.

The reason for this change is to make ride pricing easier to manage while making money less automatic. In vanilla-style play it is easy to push strong rides to the `$20.00` cap; this fork tries to keep profitable pricing possible while lowering the ceiling on effortless income.

Current balancing applies a 70% global scale to automatic ride prices before the normal cap. The guest-side discount, expensive, and refusal thresholds use the same 70% scale, so pricing targets and guest reactions stay aligned. A `$10.00` guest-facing ride value is treated as `$7.00` for those thresholds.

More detail: [Ride pricing target rationale](docs/ride-pricing-target-rationale.md).

### New ride ticket-price bonus is proportional again

New rides once again get their early ticket-price value bonus as a multiplier instead of a flat amount. Rides under five months old receive a `1.5x` value multiplier, and rides under thirteen months old receive a `1.2x` value multiplier before normal age decay and same-type competition penalties apply.

The reason for this change is to keep the new-ride bonus proportional to the ride itself. A flat bonus over-rewards weak low-value rides and barely matters for strong high-value rides, while the multiplier keeps the bonus readable across the full ride-value range.

Current balancing restores the older OpenRCT2 multiplier behaviour after upstream reverted the table to vanilla-style `+30` and `+10` flat bonuses in September 2025. The restored behaviour is covered by `RideRatings.NewRideValueBonusUsesMultiplier`.

### Park entrance pricing is policy-based

Park entrance admission now has three visible policies. `Richest guest` charges up to the richest guest spawn-cash amount and maximizes income per admitted guest. `Max profit` searches the scenario's guest cash distribution for the fee that maximizes total admission revenue after unaffordable guests leave. `All guests` stays affordable to the poorest spawning guest and is the default.

The reason for this change is to make the entrance fee a clear strategic choice rather than a single magic number. A park can chase high margin, high total gate income, or universal affordability.

Current balancing applies the same 70% value debuff used by ride admission pricing before entrance caps are applied. That means a park needs more ride value before it can justify the same gate fee, and high-value parks hit the maximum entrance fee later.

More detail: [Park entrance pricing target rationale](docs/park-entrance-pricing-target-rationale.md).

### Save compatibility is intentionally fork-private

This fork uses a private `.park` save-version band starting at `60000` for its custom fields, rather than taking upstream OpenRCT2's latest save version and adding one.

The reason for this change is future-proofing. Upstream OpenRCT2 will keep advancing its own save format, and I may want to fetch those changes later. Using a private high-numbered band reduces the chance that an upstream save-version bump is mistaken for this fork's custom ride pricing, cent-money, or park entrance data.

Current compatibility should be treated as mod-specific. Saves from this fork may not load correctly in official OpenRCT2, and official future saves may need merge work if upstream changes the same systems.

More detail: [OpenRCT2 overhaul changelog](docs/openrct2-overhaul-changelog.md).

---

![Still from the v0.5.0 title sequence](https://github.com/user-attachments/assets/fa893cc8-1484-4751-94be-4ead00a6c8f9)


---

### Download
| Latest release                                                                                                       | Latest development build |
|----------------------------------------------------------------------------------------------------------------------|--------------------------|
| [![OpenRCT2.io](https://img.shields.io/github/v/release/OpenRCT2/OpenRCT2.svg?color=green)](https://openrct2.io/download/release/latest) | [![OpenRCT2.io](https://img.shields.io/github/last-commit/OpenRCT2/OpenRCT2/develop?color=green)](https://openrct2.io/download/develop/latest) |

---

### Chat
Chat takes place on Discord. You will need to create a Discord account if you don't yet have one.

If you want to help *make* the game, join the developer channel.

If you need help, want to talk to the developers, or just want to stay up to date then join the non-developer channel for your language.

If you want to help translate the game to your language, please stop by the Localisation channel.

| Language | Non Developer | Developer | Localisation | Asset Replacement |
| -------- | ------------- | --------- | ------------ | ----------------- |
| English | [![Discord](https://img.shields.io/badge/discord-%23openrct2--talk-blue.svg)](https://discord.gg/ZXZd8D8) </br> [![Discord](https://img.shields.io/badge/discord-%23help-blue.svg)](https://discord.gg/vJABqGGTEt) | [![Discord](https://img.shields.io/badge/discord-%23development-yellowgreen.svg)](https://discord.gg/fsEwSWs) | [![Discord](https://img.shields.io/badge/discord-%23localisation-green.svg)](https://discord.gg/sxnrvX9) | [![Discord](https://img.shields.io/badge/discord-%23open--graphics-b00b69.svg)](https://discord.gg/aM2Pchscnp) </br> [![Discord](https://img.shields.io/badge/discord-%23open--sound--and--music-b00b69.svg)](https://discord.gg/tuz3QBBWJf)
| Nederlands | [![Discord](https://img.shields.io/badge/discord-%23nederlands-orange.svg)](https://discord.gg/cQYSXzW) | | |

---

# Contents
- 1 - [Introduction](#1-introduction)
- 2 - [Downloading the game (pre-built)](#2-downloading-the-game-pre-built)
- 3 - [Building the game](#3-building-the-game)
- 4 - [Contributing](#4-contributing)
  - 4.1 - [Bug fixes](#41-bug-fixes)
  - 4.2 - [New features](#42-new-features)
  - 4.3 - [Translation](#43-translation)
  - 4.4 - [Graphics](#44-graphics)
  - 4.5 - [Audio](#45-audio)
  - 4.6 - [Scenarios](#46-scenarios)
- 5 - [Policies](#5-policies)
  - 5.1 - [Code of conduct](#51-code-of-conduct)
  - 5.2 - [Code signing policy](#52-code-signing-policy)
  - 5.3 - [Privacy policy](#53-privacy-policy)
- 6 - [Licence](#6-licence)
- 7 - [More information](#7-more-information)
- 8 - [Sponsors](#8-sponsors)

---

# 1. Introduction

**OpenRCT2** is an open-source re-implementation of RollerCoaster Tycoon 2 (RCT2). The gameplay revolves around building and maintaining an amusement park containing attractions, shops and facilities. The player must try to make a profit and maintain a good park reputation whilst keeping the guests happy. OpenRCT2 allows for both scenario and sandbox play. Scenarios require the player to complete a certain objective in a set time limit whilst sandbox allows the player to build a more flexible park with optionally no restrictions or finance.

RollerCoaster Tycoon 2 was originally written by Chris Sawyer in x86 assembly and is the sequel to RollerCoaster Tycoon. The engine was based on Transport Tycoon, an older game which also has an equivalent open-source project, [OpenTTD](https://openttd.org). OpenRCT2 attempts to provide everything from RCT2 as well as many improvements and additional features, some of these include support for modern platforms, an improved interface, improved guest and staff AI, more editing tools, increased limits, and cooperative multiplayer. It also re-introduces mechanics from RollerCoaster Tycoon that were not present in RollerCoaster Tycoon 2. Some of those include; mountain tool in-game, the *"have fun"* objective, launched coasters (not passing-through the station) and several buttons on the toolbar.

---

# 2. Downloading the game (pre-built)

OpenRCT2 requires original files of RollerCoaster Tycoon 2 to play. It can be bought at either [Steam](https://store.steampowered.com/app/285330/RollerCoaster_Tycoon_2_Triple_Thrill_Pack/) or [GOG.com](https://www.gog.com/game/rollercoaster_tycoon_2). If you have the original RollerCoaster Tycoon and its expansion packs, you can [point OpenRCT2 to these](https://github.com/OpenRCT2/OpenRCT2/wiki/Loading-RCT1-scenarios-and-data) in order to play the original scenarios.

[Our website](https://openrct2.io/download) offers portable builds and installers with the latest versions of the `master` and `develop` branches. There is also a [launcher](https://openrct2.io/download/launcher) available for Windows, macOS and Linux that will automatically update your build of the game so that you always have the latest version.

Alternatively to using the launcher, for most Linux distributions, we recommend the [latest Flatpak release](https://flathub.org/apps/details/io.openrct2.OpenRCT2). When downloading from Flathub, you will always receive the latest updates regardless of which Linux distribution you use.

Some Linux distributions offer native packages:
* Arch Linux: [openrct2](https://archlinux.org/packages/extra/x86_64/openrct2/) latest release (`extra` repository) and, alternatively, [openrct2-git](https://aur.archlinux.org/packages/openrct2-git) (AUR)
* Gentoo (main portage tree): [games-simulation/openrct2](https://packages.gentoo.org/packages/games-simulation/openrct2)
* NixOS: [openrct2](https://github.com/NixOS/nixpkgs/blob/master/pkgs/by-name/op/openrct2/package.nix)
* openSUSE OBS: [games/openrct2](https://software.opensuse.org/download.html?project=games&package=openrct2)
* Ubuntu PPA (nightly builds): [`develop` branch](https://launchpad.net/~openrct2/+archive/ubuntu/nightly)

Some \*BSD operating systems offer native packages:
* FreeBSD: [games/openrct2](https://www.freshports.org/games/openrct2)

---

# 3. Building the game
- [Building OpenRCT2 on Linux](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-Linux)
- [Building OpenRCT2 on macOS using CMake](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-macOS-using-CMake)
- [Building OpenRCT2 on Windows](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-Windows)
- [Building OpenRCT2 on Windows Subsystem for Linux](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-Windows-Subsystem-for-Linux)
- [Building OpenRCT2 on MSYS2 MinGW](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-MSYS2-MinGW)

---

# 4. Contributing
OpenRCT2 uses the [gitflow workflow](https://www.atlassian.com/git/tutorials/comparing-workflows#gitflow-workflow). If you are implementing a new feature or fixing a bug, please branch off and perform pull requests to ```develop```. ```master``` only contains tagged releases, you should never branch off this.

Please read our [contributing guidelines](https://github.com/OpenRCT2/OpenRCT2/blob/develop/CONTRIBUTING.md) for information.

## 4.1 Bug fixes
A list of bugs can be found on the [issue tracker](https://github.com/OpenRCT2/OpenRCT2/issues). Feel free to work on any bug and submit a pull request to the develop branch with the fix. Mentioning that you intend to fix a bug on the issue will prevent other people from trying as well.

## 4.2 New features
Please talk to the OpenRCT2 team first before starting to develop a new feature. We may already have plans for or reasons against something that you'd like to work on. Therefore contacting us will allow us to help you or prevent you from wasting any time. You can talk to us via Discord, see links at the top of this page.

## 4.3 Translation
You can translate the game into other languages by editing the language files in ```data/language``` directory. Please join discussions in the [#localisation channel on Discord](https://discordapp.com/invite/sxnrvX9) and submit pull requests to [OpenRCT2/Localisation](https://github.com/OpenRCT2/Localisation).

## 4.4 Graphics
You can help create new graphics for the game by visiting the [OpenGraphics project](https://github.com/OpenRCT2/OpenGraphics). 3D modellers needed!

## 4.5 Audio
You can help create the music and sound effects for the game. Check out the [OpenMusic](https://github.com/OpenRCT2/OpenMusic) repository and drop by our [#open-sound-and-music channel on Discord](https://discord.gg/9y8WbcX) to find out more.

## 4.6 Scenarios
We would also like to distribute additional scenarios with the game, when the time comes. For that, we need talented scenario makers! Check out the [OpenScenarios repository](https://github.com/PFCKrutonium/OpenRCT2-OpenScenarios).

---

# 5. Policies

## 5.1 Code of Conduct

We have a [Code of Conduct](CODE_OF_CONDUCT.md) that applies to all OpenRCT2 projects. Please read it.

## 5.2 Code signing policy

We sign our releases with a digital certificate provided by SignPath Foundation.

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

Signed releases can only be done by members of the [development team](https://github.com/OpenRCT2/OpenRCT2/blob/develop/contributors.md#development-team).

## 5.3 Privacy policy

See [PRIVACY.md](PRIVACY.md) for more information.

---

# 6. Licence
**OpenRCT2** is licensed under the GNU General Public License version 3 or (at your option) any later version. See the [`licence.txt`](licence.txt) file for more details.

---

# 7. More information
- [GitHub](https://github.com/OpenRCT2/OpenRCT2)
- [OpenRCT2.io](https://openrct2.io)
- [Facebook](https://www.facebook.com/OpenRCT2)
- [RCT subreddit](https://www.reddit.com/r/rct/)
- [OpenRCT2 subreddit](https://www.reddit.com/r/openrct2/)
- OpenRCT2 plug-ins
    - [Plug-in directory (unofficial)](https://openrct2plugins.org)
    - [Plug-in development documentation](https://github.com/OpenRCT2/OpenRCT2/blob/develop/distribution/scripting/scripting.md)

## Similar Projects

| [OpenLoco](https://github.com/OpenLoco/OpenLoco) | [OpenTTD](https://github.com/OpenTTD/OpenTTD) | [openage](https://github.com/SFTtech/openage) | [OpenRA](https://github.com/OpenRA/OpenRA) |
|:------------------------------------------------:|:----------------------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------------------------:|
| [![icon_x128](https://user-images.githubusercontent.com/604665/53047651-2c533c00-3493-11e9-911a-1a3540fc1156.png)](https://github.com/OpenLoco/OpenLoco) | [![](https://github.com/OpenTTD/OpenTTD/raw/850d05d24d4768c81d97765204ef2a487dd4972c/media/openttd.128.png)](https://github.com/OpenTTD/OpenTTD) | [![](https://user-images.githubusercontent.com/550290/36507534-4693f354-175a-11e8-93a7-faa0481474fb.png)](https://github.com/SFTtech/openage) | [![](https://raw.githubusercontent.com/OpenRA/OpenRA/bleed/packaging/artwork/ra_128x128.png)](https://github.com/OpenRA/OpenRA) |
| Chris Sawyer's Locomotion | Transport Tycoon Deluxe | Age of Empires 2 | Red Alert |

# 8. Sponsors

Companies that kindly allow us to use their stuff:

| [DigitalOcean](https://www.digitalocean.com/)                                                                                                                     | [JetBrains](https://www.jetbrains.com/)                                                                                                        | [Backtrace](https://backtrace.io/)                                                                                                        | [SignPath](https://signpath.org/)                                                                                  |
|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| [![do_logo_vertical_blue svg](https://user-images.githubusercontent.com/550290/36508276-8b572f0e-175c-11e8-8622-9febbce756b2.png)](https://www.digitalocean.com/) | [![jetbrains](https://github.com/user-attachments/assets/0d1cf25e-706d-4e3a-96ee-157fbf2cf0c0)](https://www.jetbrains.com/) | [![backtrace](https://user-images.githubusercontent.com/550290/47113259-d0647680-d258-11e8-97c3-1a2c6bde6d11.png)](https://backtrace.io/) | [![Image](https://github.com/user-attachments/assets/2b5679e0-76a4-4ae7-bb37-a6a507a53466)](https://signpath.org/) |
| Hosting of various services                                                                                                                                       | CLion and other products                                                                                                                       | Minidump uploads and inspection                                                                                                           | Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).                                                                                                       |
