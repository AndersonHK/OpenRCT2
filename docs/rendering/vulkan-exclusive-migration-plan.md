# Vulkan migration work and acceptance

The graphical application selects Vulkan directly. This does not establish full
visual parity, retirement of every CPU painter, or performance acceptance. The
[architecture](vulkan-renderer-migration.md) owns the implementation contract;
the [historical migration record](../archive/rendering/checkpoints/vulkan-migration-history.md)
preserves the original gap IDs, gates, decisions and build-specific qualification.
Its old checkboxes and deployment statements are not a current task queue.

## Work owners

| Boundary | Owner and remaining verification |
|---|---|
| CPU world preparation and legacy dependencies | [Retirement inventory](vulkan-cpu-rendering-retirement-inventory.md): inspect surviving callers and actual build inputs before deleting a dependency. |
| Native family admission | [Track coverage](vulkan-native-track-coverage.md) and [transport coverage](vulkan-transport-coverage.md): translated definitions are distinct from qualified rendered states. |
| Component ordering | [Depth contract](vulkan-constant-component-depth.md), [depth contacts](vulkan-depth-contact-regressions.md), [guest services](vulkan-guest-service-ordering.md), and [lift cabins](vulkan-lift-cabin-regression.md). Retain distinct unresolved overlap cases. |
| Missing track and placement responsiveness | [Air Powered track](vulkan-air-powered-track-regression.md) and [moving track ghosts](vulkan-track-ghost-performance.md). The ghost report records pending clean performance qualification/deployment; a bounded test pass does not close it. |
| Startup and world replacement | [UI-first startup](vulkan-ui-first-startup.md) and [save-loading ownership](vulkan-save-loading-regression.md). Validate lifecycle boundaries separately from ordinary frames. |
| Unqualified extensions | [Proposals](proposals/README.md): check source and explicit status before resuming. |
| Platform and oracle scope | [Reference methods and manifests](reference/README.md). A local Windows pass does not qualify every configured platform. |

## Acceptance boundaries

Use pinned independent inputs and exact comparisons under the
[fixture manifest](reference/vulkan-parity-fixtures.json) and
[exception ledger](reference/vulkan-parity-exceptions.json). Record manual visual
review for the actual reference/candidate outputs. Source coverage, isolated GPU
fixtures, complete scene captures and live interaction are separate claims.

Measure simulation throughput with real rendering, CPU preparation, GPU execution,
state/upload bytes and frame pacing separately. Compare identical inputs and
settings with one GPU workload at a time. The moving-ghost workload must actually
move a valid placement; a stationary park does not qualify that regression.
Application presentation intervals do not prove physical scanout.

The final retirement boundary requires source/caller and build/link audits,
qualified image-producing callers, lifecycle tests and the actual final package.
Deleting a selector or passing a helper suite is insufficient. Detailed original
gates remain in the historical record for traceability, with subsequent decisions
and each work owner's later evidence taking precedence.

For a runnable update, follow [build/deployment](../development/windows-local-build.md)
and verify both installed executables and matching runtime data. Retain scoped
evidence and finalize artifacts under the [retention policy](../development/vulkan-artifact-retention.md).
This index reports documentation status; it does not requalify or redeploy a build.

[Library](../README.md) · [Collection](README.md)
