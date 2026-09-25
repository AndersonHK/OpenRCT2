# Sloped paths and elevated rail contacts

Reported after deployed build186 (`df6cdf3075`). These are ordering regressions, not missing art. Each authored sprite still has one constant D32 depth; higher priority maps to smaller hardware depth. WorldXY means tile/subtile coordinates, not screen pixels.

## Reproduction and status

The initial paired screenshots use frozen Everything Park (`c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4`), original licensed art, unchanged simulation tick and camera. The camera matrix is retained in `scripts/rendering/fixtures/everything-depth-contacts.json`. The owner subsequently identified the red-track reports as the Looping Rollercoaster in Trinity Park and Corkscrew Rollercoaster 3 in Corkscrew Park; those saves must also be inspected. Tests are headless or use a hidden window and dummy audio. No automatic launch of the user's installation is involved.

| Case | Reproduction | Status |
| --- | --- | --- |
| Guests cover the camera-near sloped path railing | Pirate Ship queue tile(105,235), camera world(3376,7536,328), all four rotations; Rowing Boats queue tile(95,234), world(3056,7504,328), rotations0/2 | Remains open. Candidates187/190/191 rejected after visual review; production path contacts restored to186. |
| Elevated Corkscrew cuts behind queue fence | Ride185, type58 sequence2 at tile(184,229), baseZ384, rotation1; camera world(5904,7424,352) | Reproduced against upstream in186. Blanket authored-origin candidate fixed57 target pixels but regressed other vehicles, so was withdrawn. |
| Elevated red curve behind entrance/exit | Looping Rollercoaster in Trinity Park; Corkscrew Rollercoaster 3 in Corkscrew Park | Exact-save investigation in progress. Keep separate from the Everything Park proof. |
| Support in front of its own rail | User's fourth image; same Corkscrew area is a candidate | Check actual support ownership and its own-rail depth constraint; do not globally lower unrelated supports. |
| Selected train viewport is black | Ride window, Train1, Corkscrew Rollercoaster 3 in Corkscrew Park | Fixed native-world routing. Build190 passes the actual ride-window follow test over3,000 ticks at4K; inspected crop contains the train area and surrounding world. |
| Photo platform erases its own rail | Corkscrew Park ride16, tile(135,58), worldZ112 | Build193 passes four-rotation visual review. Source-owned platform floor uses layer1, rail retains layer2; world contacts do not move. |

The supplied image filenames are `codex-clipboard-c2d9714c-074b-456a-881e-a027e69cecc9.png`, `codex-clipboard-92d33353-ddc4-4ba8-9cba-e10e82057bc5.png`, `codex-clipboard-7f601691-85c5-4a95-94dc-0b4a958c4c14.png`, and `codex-clipboard-b963f64f-f1a6-455c-9759-c7e8da04bb7d.png`. Local originals and hashes are preserved under `obj/vulkan-parity/ordering187-fixtures/user-images`.

## Causes and bounded corrections

The path visitor explicitly excludes slopes from its foreground edge contact. Guests use actual subtile positions and the walking-surface height, while the railing retains its far-end raster origin. The withdrawn candidates used the actual near edge's surface endpoint, including the direction-dependent slope height. Their tests enumerated four slopes and four camera rotations against the simulation's actual `MapHeightFromSlope`, distinguishing interior guests from positions physically beyond the railing. Those checks did not constrain every overlapping fixture, so they were insufficient for acceptance.

Build187's tile-corner contact overpromoted fixtures. Its screenshots are diagnostic only: a subsequent build changed the original input hashes before the runner finished, and the runner correctly rejected the receipt. Build188 completed its capture with unchanged inputs, but blanket authored track origins clipped Dinghy Slide hulls and car wheels. Both candidates are rejected for deployment; improved aggregate pixel counts do not override these visually confirmed regressions.

Build190's isolated railing endpoint corrected guest overlap but cut attached lamp stems. Build191 gave the emitted near rail and only its own same-edge fixture a shared actual edge contact, with separate local layers. It repaired the Pirate Ship views but introduced372 wrong pixels in the Rowing Boats rotation2 view. An agent inspected all seven full comparisons and twelve enlarged changed-region triples. In particular, railing strokes cut previously unobstructed bins/figures at screen(130,647)-(165,671), and lamp assemblies at(7,733)-(38,768). Without component identities those images do not establish whether the independently anchored fixture belongs to another edge or another tile. The candidate is withdrawn, including its candidate-specific tests; patch archive: `obj/vulkan-parity/slopes191-withdrawn/candidate.patch`. Evidence: `slopes191-candidate-01/agent-review/metrics.json`. A source compile and90 passing CPU tests did not override this visual rejection.

The black ride window was a separate routing defect. `ViewportPaint` used `IsTerrainOnly()` to decide whether a secondary viewport needed an independent session. Complete native snapshots include peeps and therefore failed that condition. The window cleared its rectangle and then tried to use the main command stream's already occupied world slot. Routing now matches the command stream's `IsNativeOnly()` plus raw-map ownership contract. The same correction restores full main-target recording and the existing construction-selection snapshot. The ordinary auxiliary session still consumes a held generation; no CPU world-paint fallback was added.

The opt-in `run-render-performance.py --secondary-vehicle --secondary-ride 16` diagnostic opens the actual ride window, records target and follow-camera coordinates, and extracts the embedded viewport from the final main screenshot. Build190's Corkscrew Park run passes3,000 ticks with entity288 retained, a changed camera matching its live target, and178 colours in the inspected crop. Evidence: `obj/vulkan-parity/secondary190-corkscrew-3000-01/summary.json`. This workload includes ordinary auxiliary rendering and must not be substituted for the main-only performance baseline.

## Exact owner-save fixtures

Read-only source copies and hashes are recorded under `obj/vulkan-parity/ordering187-fixtures/exact-saves/receipt.json`:

- Trinity Islands Mod3: SHA256 `bc91778c504c08a9766be6d7473684b1c6f37d535df3d8a6b309df67a64782ae`, Looping ride18, camera world(1424,1104,264). Booths at tiles(44,33)/(44,35); offending geometry includes a large banked helix.
- Corkscrew Park: SHA256 `4cd3df3689109d2b376ce95a01066053f89b772b32df469fe0795d5929d35de1`, ride16, camera world(4336,1856,160). Booths at tiles(136,60)/(136,61); geometry includes a large half-loop.

The upstream oracle cannot directly read fork version60016. The test-only batch driver can export a new version61 copy after capturing the original, embedding packable custom objects and explicitly recording omitted fork fields. Both renderers then reload the same copy. Original-native and converted-native pixels match exactly in all four rotations of both parks. Paired receipts: `obj/vulkan-parity/trinity190-paired-01/summary.json` and `corkscrew190-paired-01/summary.json`. These reproduce remaining mismatches; they are not parity passes.

The next systematic step is the [bounded ordering-constraint proposal](vulkan-depth-constraint-fitting.md), using semantic owner pairs and held-out controls rather than adding a bias for every failed screenshot.

## Build193 qualification

- Build: zero warnings/errors,88 focused CPU tests pass. The new actual-recipe/D32 regression checks platform below rail and a centre car ahead in all four rotations. All79 source-extractor tests pass; only72 existing role words change, affecting on-ride-photo recipes across23 aliased track styles.
- Exact Corkscrew Park: four paired images and enlarged photo contact crops inspected. Compared with190, all4,568 changed pixels now match upstream; zero newly wrong pixels. This combined count includes rollback of the rejected slope candidate, not only the photo fix.
- Everything Park: all six path controls and four small Corkscrew controls are pixel-identical to deployed186. The glass-station view changes656 pixels, all newly matching upstream, with zero newly wrong pixels. Additional occupied/looping/railway/inverted views are retained as visual controls, not claims of full parity.
- Source investigation, original-save conversion and fitting proposal are diagnostic work. They introduce no draw-order solver or per-frame CPU tracing into gameplay.
- Final live viewport qualification: `obj/vulkan-parity/secondary193-corkscrew-3000-01/summary.json` passes3,000 ticks at4K with the actual ride16 window and correct follow camera. Its extracted viewport was personally inspected and contains the vehicle area and surrounding world. The main-only12,000-tick final screenshot was also inspected; neither capture shows a blank world or missing UI.

For the Corkscrew example, current rail contact1792 loses to queue-fence contact1808. The source's rail bounds origin(6,0,+24) gives contact1822. This is a lost authored XYZ offset. A blanket nearest-bounding-corner policy would move the rail much farther and could incorrectly cover vehicles riding it; that approach is not the candidate.

## Qualification checklist

- [x] Reproduce the elevated Corkscrew/fence mismatch in matched original/native images.
- [ ] Add a validated all-slope/rotation contact regression. Candidate tests passed but asserted insufficient relationships; archived with withdrawn changes.
- [x] Inspect paired sloped-path images and unchanged flat/station-glass controls; reject candidates that damage fixtures.
- [ ] Add real source-recipe regressions for rail/queue, rail/booth and own-support relationships.
- [x] Inspect the blanket track candidate in all four rotations, including occupied track and previously correct objects; reject its hull/wheel clipping.
- [x] Identify and reproduce the exact red-curve and support cases; retain them as open regressions with original/converted parity evidence.
- [x] Run the final4K12000-tick performance comparison and inspect its final image.193:351.056TPS,143.875 accepted presents/sec,0.643122ms CPU drawing,4.67468ms GPU; same entity checksum as186. Accepted-present p99/max9.0/9.5706ms; first application interval34.4603ms remains an open boundary outlier.
- [x] Commit qualified changes with remaining mismatches and deploy build193. `D:\Games\Independent\OpenRCT2Mod` contains29 verified files,3 changed, with replaced files backed up. Receipt: `obj/vulkan-parity/deploy-checkpoint193-01/receipt.json`. Shared Everything Park save preserved; the game was not automatically launched.
