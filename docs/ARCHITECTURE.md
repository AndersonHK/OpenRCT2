# Architecture and ownership

This fork changes gameplay, economy, audio and rendering while retaining OpenRCT2's
simulation and platform structure. The [repository overview](../readme.md) explains
player-visible changes; topic guides own the detailed contracts.

| Responsibility | Contract | Source owner |
|---|---|---|
| Simulation and guest services | [Gameplay](gameplay/README.md) | [Simulation core](../src/openrct2/) |
| Money and pricing | [Economy](economy/README.md) | [Management](../src/openrct2/management/) and [rides](../src/openrct2/ride/) |
| Ride experience samples | [Ratings](ratings/README.md) | [RideRatings.cpp](../src/openrct2/ride/RideRatings.cpp) |
| Map topology and shared walking fields | [Pathfinding](pathfinding/README.md) | [World state](../src/openrct2/world/) and [entities](../src/openrct2/entity/) |
| Spatial sound | [Audio](audio/README.md) | [Audio core](../src/openrct2/audio/) and [UI audio](../src/openrct2-ui/audio/) |
| Presentation and Vulkan execution | [Rendering](rendering/README.md) | [Shared renderer](../src/openrct2-renderer/) and [UI drawing](../src/openrct2-ui/drawing/) |
| Save compatibility and time units | [Reference](reference/README.md) | [Park serialization](../src/openrct2/park/) |

Read the subsystem contract before changing a boundary. Simulation state, immutable
presentation state, GPU execution and recorded captures have different lifetimes.
Compiled source, installed runtime data, a bounded fixture pass and live gameplay are
different evidence. Use [status](STATUS.md) to find the relevant record.

[Library](README.md)
