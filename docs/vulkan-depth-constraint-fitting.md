# Learn bounded component offsets from observed ordering

The proposed shorthand is useful: **A is behind B at this opaque overlap**, paired with the two components' world contacts and native depth values. Hundreds of these relations can narrow the allowed offsets much more reliably than tuning a whole screenshot's pixel-difference count. The result remains one constant depth per authored sprite. No pixel-dependent depth, CPU painter sorting, or learned work runs during gameplay.

This document records an implementable diagnostic extension, not a claim that the current renderer has already been fitted. World XY always means tile/sub-tile coordinates; screen coordinates are used only to find overlapping art and show the reviewer where a relation matters.

## What already exists

| Existing hook | Available evidence | Limitation |
| --- | --- | --- |
| `test/mixed-parity/MixedFixtureWorld.h`, `Paint()` | Frozen-core `PaintSessionGenerate` / `PaintSessionArrange`, then `.paint.json` columns containing ordered parent/child components with image, screen origin and bounds | Small finite fixture; does not emit palette flags, stable owner identities or current native depths |
| `test/mixed-parity/MixedFixtureCapture.h` | Actual decoded indexed sprite masks, expected indexed frames and pinned asset metadata in `corpus.json` | Explicitly rejects covered-zero sprites; ordinary finite fixture only |
| `test/mixed-parity/experimental-plane/analyse-structure.py` | Existing artifact-only ordered replay, component matching and wrong-winner pair counts | Its historical scalar candidates are not the current native shader and must not be represented as such |
| `test/terrain-parity/TerrainColumnTrace.h` | Schema 2 before/after arrangement identities, map positions, attachments, decoded masks and indexed oracle frame | Deliberately rejects children, recolours and blends; cannot simply be applied to a full park |
| `src/openrct2-ui/drawing/engines/vulkan/VulkanDiagnosticCapture.h` | Named frame, captured output, held publication and scene identity | No current per-component owner/depth dump |
| `data/shaders/vulkan/world_parent_capture.glsl`, `worldCapturePaint()` | Final constant contact in `OutputRecord.reserved.x`, local layer packed in `depth`; actual emitted asset/geometry in the same record | Requires a diagnostic readback and owner/recipe identity sidecar |

`scripts/rendering/extract-depth-order-observations.py` now reads the **existing finite-mixed-v1 corpus**, without launching the game, using the compiler, or opening a device. Example:

```powershell
python scripts/rendering/extract-depth-order-observations.py --corpus obj/vulkan-parity/mixed-corpus-01/corpus --output obj/vulkan-parity/depth-constraint-study/observations.json
```

It emits compact `behind`, `inFront`, mask-overlap count and example-screen-pixel records, plus component facts and input hashes. It follows the actual frozen order inside each 32-pixel column, and only reports pairs for which the front member is topmost in the decoded masks. Identical image/screen/bounds twins make a column ambiguous instead of inventing an identity. Missing assets and reversed pair observations are explicit diagnostics. Same-colour intersections are retained because precedence comes from the trace, not from guessing at RGB differences.

On the existing corpus and its separately captured repeat: **16 cases, 768 observations, zero incomplete mask cases, zero reversed pairs** in each. These cases cover four rotations, two entity poses and both sort policies; they are not 768 independent semantic examples. Every observation is marked `review: pending`. The omitted palette/blend metadata means mask observations alone cannot certify final composition, and this script deliberately does not fit current GPU parameters.

## Connect the real renderers before fitting

1. Extend the **test-only frozen oracle**, following `MixedFixtureWorld::Paint()`, to trace the requested park/camera without changing production software rendering. Include `PaintStruct.Element` converted to tile plus element ordinal, `PaintStruct.Entity.id`, parent/child/attachment relation, complete `ImageId` recolour/blend/mask state, authored bounds, and original draw position. These fields are available in `src/openrct2/paint/Paint.h`; serialize stable values, never pointers. Export asset identifier plus object-local sprite index: absolute image numbers can differ between independently loaded installations.
2. Extend the named diagnostic capture with a **bounded readback of the records actually emitted by the current GPU**. `VulkanWorldSurfacePipeline` owns `_visibleRecords`, which currently has storage/vertex usage but no transfer-source usage or diagnostic getter. A diagnostic-enabled allocation must explicitly add transfer-source usage, record the compute-write to transfer-read barrier, copy the valid range into dedicated readback storage, and wait for the same submission before reading it. Preserve the existing normal-frame allocation/recording path when capture is disabled. Do not read a buffer that a later frame may overwrite.
3. Add an optional parallel diagnostic identity record at the emission sites in `world_materialize.glsl` and entity emitters: source kind, tile/element ordinal or entity ID, recipe/component index, parent owner, semantic role and chosen contact. Capture world epoch, tick/tween sample, camera, atlas generation and shader hashes. The `WorldSurfaceRecord` ABI in `GpuCommandStream.h` already gives the actual raster geometry, asset and packed depth. Avoid changing its production size just to carry diagnostic names.
4. Join components by owner, asset identity, authored component and projected placement; represent parent chains explicitly. Mark unmatched, duplicate, differently posed or differently textured components as **coverage/state discrepancies**, not ordering constraints. Inspect these separately; an offset cannot repair missing track art.
5. Replay the frozen trace with its exact clipping, alpha/mask and palette operations and require byte-identical indexed output before trusting its winner IDs. Replay native records with the current constant-depth encoding and require agreement with the captured native image too. Where filter/glass/shadow operations require multiple contributors, retain the ordered contributor stack instead of mislabelling it as one opaque winner.

The readback is a diagnostic implementation task with GPU synchronization consequences; it is not present merely because the proposal describes it. Keep it out of performance runs. Existing screenshot comparison still supplies the human-visible evidence while it is developed.

## Turn a visible ordering relation into a bounded constraint

For components in the same world depth domain, use the exact integer equivalent of current D32 ordering:

```text
K(component) = 512 * contactScalar + localLayer
larger K is closer; smaller positive D32 bits are closer
```

The common depth base cancels. This follows `world_component_depth.glsl` and `indexed_depth.glsl`; it does not approximate depth with screen Y or change the current world-coordinate weighting. Preserve local layer 0..255 and existing parent/child limits. Comparisons across distinct viewport/pass depth domains need their pass rules as well, and should initially be excluded.

With the geometry/contact policy fixed, let `delta[role]` be a bounded additive correction to K. A reviewed relation **A behind B** becomes:

```text
K_A + delta[role_A] + 1 <= K_B + delta[role_B]
delta[role_A] - delta[role_B] <= K_B - K_A - 1
```

These are difference constraints. A small offline graph solver can determine feasibility and allowed intervals without an external numerical package. Fix a reference role to zero; enforce role bounds as additional edges. Report an inconsistent cycle with its specimen IDs and crops instead of silently accepting a least-squares compromise. After feasibility, prefer the smallest maximum correction, then the fewest changed roles. Never accept arithmetic that overflows the valid depth range or lets a local tie layer cross into the next contact.

Choose semantic contact geometry **before** trying offsets. A slope railing belongs at its physical edge and corresponding slope height; it cannot be modelled as a generic tile-front corner if a guest or trash can can stand outside that edge. Where the current contact itself is wrong, enumerate a few physically justified policies (authored origin, actual edge endpoint, complete building footprint), then fit bounded layers separately for each. Do not fit an arbitrary coefficient per sprite image, ride instance, park, camera rotation, or frame. Opposite camera orientations should transform the same world geometry.

An infeasible set is informative: it may expose an incorrect owner/contact, a missing sprite split, a bad oracle sample, or a genuinely inadequate role classification. Inspect the conflicting examples before adding another branch. A global scalar cannot reproduce a legacy relation that reverses by screen column for the very same two atomic components; flag that contradiction explicitly.

Two current candidate reviews illustrate why the controls matter. The broad `(32,32)` near-corner path policy overreached the actual railing edge; `(31,27)` / `(27,31)` contacts leave room for objects physically beyond that edge. Separately, the rejected build188 raw-track-origin candidate corrected 57 targeted pixels but clipped Dinghy hull/car-wheel art elsewhere. Those are useful training and held-out specimen pairs: the goal is a policy that satisfies both relations, not a better score on the originally reported crop. Candidate corrections remain subject to their own image and benchmark qualification.

## Corpus and acceptance

- Begin with sloped near/far path rails, bins/lamps/benches, guests inside and outside their edge; include the glass shelter columns, so a railing correction cannot silently cover its surrounding glass.
- Add the actual reported saves: **Looping Rollercoaster in Trinity Park** and **Corkscrew Rollercoaster 3 in Corkscrew Park**. Include rail versus support, entrance and queue fence, all four camera rotations, plus the already located Everything Park corkscrew.
- Hold out entire parks/ride families and complete rotations/pose sequences. Adjacent screenshots of the same geometry are correlated; scattering their pixels between train and validation provides no useful generalization evidence.
- Include already correct floor/entrance, elevated track, inverted track, Roto Drop ascent, Pirate Ship, terrain/skirt and parking-space controls. An intentionally accepted skirt divergence is labelled and excluded from fitting, not treated as an error to erase.
- Give each distinct semantic owner-pair/specimen a capped weight; show affected-pixel counts separately. A large lawn overlap must not outweigh every small railing. Group repeated animation frames and repeated tiles, and report violations by semantic family and held-out park, not only aggregate pixel totals.
- Order evidence comes from the matching trace plus actual overlapping coverage. Same-colour RGB is not evidence for either ordering, and an image difference alone does not distinguish wrong art from wrong depth. Human review approves the semantic relation, especially where upstream has an acknowledged glitch.
- For every changed or still-violated relation, produce enlarged nearest-neighbour oracle/native crops, owner IDs, world contacts, K values and before/after inequalities. An agent must visually inspect these; automatically improving a score is insufficient.
- Only publish the small, named contact/layer constants that pass the held-out images. No trace, constraint graph, dynamic order computation, training code or extra per-frame transfers enters gameplay. Re-run the normal hidden, silent 4K benchmark for at least 3,000 ticks (12,000 for the heavy park) with diagnostics disabled.

## Checklist

- [x] Locate the existing trace, sprite-mask and ordered-replay formats.
- [x] Add a strictly offline observation extractor and run it on the real corpus and repeat.
- [ ] Add full-park oracle identities and exact composition metadata in a test-only producer.
- [ ] Add bounded, same-submission native component/identity capture.
- [ ] Verify both replays and component matches against captured images.
- [ ] Approve semantic relations in the reported parks and correct controls.
- [ ] Solve bounded role constraints; expose contradictions and allowed intervals.
- [ ] Inspect changed and violated pairs in held-out specimens, then qualify performance.
