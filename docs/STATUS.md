# Status and evidence

This page routes to authoritative contracts and recorded work. It deliberately does
not duplicate build numbers, installed hashes, test counts or tuning values. Verify
the working tree and installed package before treating a dated receipt as current.

| Area | Current authority | Evidence and remaining work |
|---|---|---|
| Gameplay and economy | [Gameplay](gameplay/README.md), [economy](economy/README.md) | Each rationale states behavior and limits; [gameplay history](archive/gameplay/README.md) retains completed investigations. |
| Ride ratings | [Aggregate model](ratings/ride-rating-aggregate-rationale.md) and [stat sources](ratings/ride-rating-stat-source-manifest.md) | [Calibration history](archive/ratings/README.md) separates selected changes from unshipped alternatives. |
| Routing | [Shared route fields](pathfinding/shared-route-fields.md) and [transport](pathfinding/transport-ride-routing-rationale.md) | [Texas Giant regression](archive/gameplay/texas-giant-pathfinding-investigation.md) is a bounded reproduction, not general pathfinding acceptance. |
| Audio | [Spatial audio contract](audio/spatial-audio-overhaul.md) | [Camera/depth work](audio/spatial-audio-camera-depth-plan.md) distinguishes the implemented slice from deferred reverb. |
| Vulkan | [Architecture](rendering/vulkan-renderer-migration.md) and [migration work](rendering/vulkan-exclusive-migration-plan.md) | [Open regressions](rendering/README.md), [proposals](rendering/proposals/README.md), and [historical qualification](archive/rendering/README.md). |
| Upstream integration | Current source and [fork overview](../readme.md) | [Upstream archive](archive/upstream/README.md) records completed integration ranges, decisions and receipt identities. |
| Build and installation | [Windows build guide](development/windows-local-build.md) | Build/deployment receipts must identify the source, executables and complete data together. |

## Evidence boundaries

A proposal is not implementation. Source coverage is not pixel parity. Tests of
synthetic scoring do not simulate ride physics. A historical deployment is not proof
of the currently installed package. Application presentation timing does not certify
physical scanout or multiplayer behavior.

Archived instructions, open checkboxes and deployment claims apply to their recorded
checkpoint. Archiving does not close an unresolved regression or invalidate narrow
evidence. Local artifact links may require restoration using the
[retention procedure](development/vulkan-artifact-retention.md).

[Library](README.md)
