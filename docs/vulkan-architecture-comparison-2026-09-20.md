# Renderer alternatives, independent votes and historical lessons

This records the two requested plans and independent review as of September 20. **Subsequent decision, September 23:** the owner selected Plan B for implementation, with measured substantial improvement and reversal of unsuccessful experiments. The [fresh historical software benchmark](vulkan-precatchup-software-benchmark-2026-09-20.md) reports ten passing runs and 48,000 measured ticks.

## The two plans

| Candidate | Shared architecture | Distinguishing visibility representation |
|---|---|---|
| [A: orthographic world surfaces](vulkan-plan-a-geometric-world.md) | Immutable simulation publication, resident GPU state/assets, shader visual decisions, GPU culling, independent presentation | Depth-bearing surfaces, geometry or depth impostors express visibility within a component. |
| [B: retained world sprite components](vulkan-plan-b-retained-sprites.md) | The same ownership, residency, shader and presentation requirements | Reuse atomic sprite components with a depth value per component; test camera-depth/layer ordering before expanding coverage. |

The performance opportunity common to both is removing repeated CPU world painting, command construction and unnecessary publication copies. Geometry alone does not increase TPS. B potentially costs less in asset conversion; A potentially handles overlaps that cannot be expressed by atomic component order. Both preserve exact artwork except specifically reviewed corrections. Neither is a license to introduce broad appearance differences or repeat expensive whole-world copying.

## Blind review protocol and first votes

Three new agents received no conversation history, author preference or other reviewers' results. They read the same two neutral candidate documents, could inspect production source, and were instructed not to read other design/history documents or communicate with each other. Reviewer two received B first; reviewers one and three received A first. Each wrote a separate result before the coordinator received all votes. These are independent assessments using the same model family, not independent empirical performance tests or a statistically calibrated panel.

| Reviewer | Plan A | Plan B | First implementation |
|---|---|---|---|
| One | Approve bounded prototype | Approve bounded prototype | B, moderate confidence |
| Two | Reserve | Approve bounded prototype | B, approximately 70% confidence |
| Three | Approve bounded prototype | Approve bounded prototype | B, moderate confidence |

All three distinguish choosing the first falsifiable experiment from committing the entire renderer. They question whether an anchor-based scalar can replace legacy extent comparisons, and whether depth-bearing geometry can preserve bitmap appearance without substantial asset work. The result is **3/3 for testing B first**, not 3/3 proof that B is correct or fastest.

Original reviews are preserved under `obj/vulkan-parity/architecture-votes-20260920/reviewer-{one,two,three}.md`. The accompanying vote receipt pins their bytes and the exact documents reviewed.

Tracked copies: [reviewer one](vulkan-architecture-reviews/reviewer-one.md), [reviewer two](vulkan-architecture-reviews/reviewer-two.md), [reviewer three](vulkan-architecture-reviews/reviewer-three.md). [Vote receipt](vulkan-architecture-votes-2026-09-20.json).

## Full historical reading and implications

After the first votes, the owner specifically requested a full reading of [the archived performance history](archive/performance-320-tps-refactor-history.md). The coordinator read the document end to end. Each reviewer was then independently asked to read it in full and submit a separate history addendum, without seeing the other votes or author preference. Initial votes remain unchanged as artifacts; later assessments are reported separately.

All three completed the full 1,265-line reading and retained their original votes. Their second assessments strengthen the publication-cost and historical-reuse gates; they do not claim that the history establishes scalar-depth correctness. Addenda: [one](vulkan-architecture-reviews/reviewer-one-history-addendum.md), [two](vulkan-architecture-reviews/reviewer-two-history-addendum.md), [three](vulkan-architecture-reviews/reviewer-three-history-addendum.md).

This history already describes much of the shared target architecture. The next implementation should reuse and complete existing work rather than treat resident sprites, immutable scenes or GPU compaction as new discoveries.

| Historical evidence | Consequence for both plans |
|---|---|
| Software median 318.324 TPS at 13.397 FPS; later Vulkan results include 360.032 TPS at 144.013 FPS over 3,600 ticks and 347.182 TPS at 144.016 FPS over 10,800 ticks | Preserve renderer, scheduling, state boundaries and configuration when comparing. The software recollection is supported, but later high-FPS rows are separate Vulkan evidence. Resolution is not established for those historical rows. |
| A full-map redraw snapshot prototype reached only 139.278 TPS / 36.406 FPS | Never introduce a full-map clone as the new publication contract. |
| Compact guest/staff and spatial-index prototypes reached 295.455 or 297.101 TPS; other full/compact entity copies also regressed | Emit compact changes from authoritative owners; no extra whole-population scan or dense spatial-index rebuild every frame. Measure publication cost before broad shader expansion. |
| Copying legacy entity/ride storage cost about 3 ms before asynchronous recording; the prototype was removed | Moving work to a thread is insufficient if snapshot capture consumes the saved CPU budget. |
| Retained pixel damage could acknowledge a different generation and leave square-clipped moving sprites; prepared paint-list reuse later failed construction ghosts | Retain world data and assets. Draw complete coherent visible generations. Do not restore cross-generation dirty pixels or stale paint lists to recover benchmark numbers. |
| Existing mailbox, frame slots, upload ring, resident atlas, instanced passes, compute LightFX and dense terrain compaction | Reuse approved service/resource ownership and qualified mechanisms. Do not recreate a second renderer service or device. |
| Terrain-only admission deliberately excluded mixed scenes without a shared world ordering model | Peeps cannot be a draw-last overlay. The first mixed scene must test paths, supports, trees and moving entities together. |
| Later warmup windows grew the park and changed cache/locality behavior | Include a sustained warm interval, initial/final state censuses and per-run checksums; do not compare different population windows as identical workloads. |
| Extra fine-grained profiler scopes themselves taxed hot loops | Keep throughput runs uninstrumented; collect attribution separately. |

The journal contains evolving and superseded implementations. Historical prose identifies lessons and source candidates; it does not prove that every described optimization is still active. Source verification remains required before reuse.

## Shared revised entry gates

- [ ] Map current implementations of publication, typed entity ownership, residency, compaction and mailbox admission against the accepted and rejected historical experiments.
- [x] Establish present-day pre-catchup and current software performance with pinned source/assets, actual 4K extent and at least 3,000 measured ticks. Eight scale-one runs and two separate sustained 1.75× scale runs pass; source rebuild and dependency-provenance limits are explicit. Keep older time-based results separate.
- [ ] Attribute current CPU time and copy/upload traffic in a separate run before assigning expected TPS gains to peeps or any other family.
- [ ] Prove compact mutation-owned publication without a full-world scan, including skipped frames, deletion, identity reuse, epochs and asset lifetime.
- [ ] Test both visibility representations on identical mixed-scene inputs. Start with the smaller component representation if selected, but preserve the surface alternative for demonstrated failures.
- [ ] Measure throughput at equivalent visual work and independently verify displayed VSync pacing; application draw counts are not scanout counts.
- [ ] Expand only after correctness and performance evidence justify the next family; retain the existing migration parity/auxiliary/platform/removal gates.
