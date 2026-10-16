# Documentation library

This library describes the personal fork's behavior, subsystem contracts, development
procedures and retained evidence. Start with [architecture](ARCHITECTURE.md),
[status and evidence](STATUS.md), and [workflow](WORKFLOW.md).

| Task or question | Entry point |
|---|---|
| Build, deploy, validate or retain artifacts | [Development](development/README.md) |
| Change guest growth, services, boarding or decoration | [Gameplay](gameplay/README.md) |
| Change money, ride prices or park admission | [Economy](economy/README.md) |
| Change ride experience sampling or ratings | [Ratings](ratings/README.md) |
| Change walking, topology caches or transport journeys | [Pathfinding](pathfinding/README.md) |
| Change spatial sound, camera acoustics or loudness | [Audio](audio/README.md) |
| Change Vulkan rendering or investigate visual regressions | [Rendering](rendering/README.md) |
| Interpret save formats and simulation units | [Reference](reference/README.md) |
| Find dated acceptance, rejected designs or migration history | [Archive](archive/README.md) |

## Maintaining the library

Keep this level for general knowledge, navigation and workflow. Improve the existing
topic owner before creating a page; link every retained page and data file from its
collection index. Separate implemented contracts, proposals and dated evidence.
Archive unique superseded reasoning and acceptance records rather than keeping old
assignments beside current guides. Do not accumulate redirect stubs or backup copies.

Personal saves, one-off reports and raw traces belong in an ignored task directory
(this checkout uses `.tmp/` and `obj/`) or a requested external destination. A retained
benchmark should contain only the inputs, measurements and limits needed to support a
durable decision. Existing narrow rating and rendering evidence is indexed in the archive.

After moves, update links and script producer/consumer paths, then run:

```powershell
python scripts/check_docs.py --check-indexes
```

The checker validates local Markdown file targets, page reachability and collection links for data files. It skips
external URLs and ignored build/evidence paths, and does not certify heading anchors,
historical hashes, factual freshness or runtime behavior. See [workflow](WORKFLOW.md)
for evidence handling and [repository instructions](../AGENTS.md) for local rules.
