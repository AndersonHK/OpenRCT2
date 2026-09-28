# Terrain emission rules: staged implementation

Status: staged only, not built or executed. Nonuniform terrain admission remains closed. This is the next rule layer in [the nonuniform terrain design](vulkan-nonuniform-terrain-design.md), not a new rendering path or a parity qualification.

`data/shaders/vulkan/terrain_surface_rules.glsl` provides production scalar rules shared by GLSL and the C++ differential tests through `TerrainSurfaceEmission.h`. It rotates raw slope bits, selects the surface shape offset and material-table address, rotates neighbor coordinates, and derives front-face lower fillers, vertical strips, upper fillers and rear attachments from raw tile/neighbor heights. The helper produces a count and an indexed emission function, so a compute pass can prefix-sum counts and reject overflow before writing any output. It does not upload CPU-generated tile images, commands or sort keys.

The rule input contract is dry non-steep surfaces with no tunnels, underground mode, clipping-view policy, hidden base/vertical sides or overlays. Heights are multiples of 16 from 16 through 4064; the upper bound avoids overflowing the frozen painter's eight-bit corner heights. Scene eligibility must be checked separately. Missing neighbors use the frozen minimum-land corner height. These helpers do not interpret invalid inputs as scene admission: a caller must reject them before submitting work.

Each front-side emission carries its object-relative image entry, camera-relative world offset and original bounding-box size. Rear emissions carry screen-relative attachment offsets. Local call order is rear-left, rear-right, front-left, front-right; frozen prepend semantics produce rear-right then rear-left in the base's attachment list. Cross-tile parent sorting, clipping, linked zoom sprites, exact atlas sampling, covered-zero handling, map-edge blank tiles and outside-map background are still separate required work. Nothing here proves those behaviors or enables the CPU terrain bypass.

The test-only oracle retains byte-preserved function/table fragments from frozen commit `9a092745f38a714cf33629aaa005d078d473e48b`, with source and fragment hashes in `test/terrain-parity/FrozenTerrainEdgeOracle.json`. The original source files were checked against that Git commit with line endings normalized. The extracted side functions execute their actual branches and loops; only the two emission calls are intercepted. Rear attachment allocation and prepend behavior execute the frozen `PaintAttachToPreviousPS` body. No software renderer source is edited.

`TerrainSurfaceEmissionTests.cpp` adds five intended checks:

- Slope rotations, shape offsets, corner heights and neighbor coordinates against independent frozen functions/tables. This removes the earlier test's reliance on the new `RelativeSlope` function to calculate its own expected rotation.
- 65,536 height/slope/neighbor/rotation/side combinations, comparing exact emission count, image offset, world/attachment offset, bounds and local emission sequence. Heights 16/32/48/64 and all raw non-steep slopes 0–15 are covered. Slope 15 remains representation coverage; the gameplay fixture uses 0–14.
- Missing neighbors, the maximum supported height and rejected steep/misaligned/out-of-range inputs.
- Actual frozen rear-attachment linked-list traversal and the original four-call side sequence.
- Shader material addresses against the compiled object-selector layout, including distant grass selection. Existing material-rule tests remain the public object-selection differential oracle.

`test/terrain-parity/TerrainSurfaceRulesProbe.comp` is a diagnostic entry point using the same production include. It is staged for a future device readback test; it is not registered as a production shader and has not been compiled. Queries are three `ivec4` values (48 bytes). Results contain the 32-byte plan, 32-byte emission and four metadata integers (80 bytes). A future GPU test must verify those layouts and compare actual device output to the frozen traces; compiling the same source as C++ does not establish GPU correctness.

Root integration checklist:

- [ ] Verify all staged baseline/candidate hashes before applying.
- [ ] Register `TerrainSurfaceEmissionTests.cpp` in the coordinated test source lists; no shared build metadata is changed by this stage.
- [ ] Run `scripts/rendering/verify-terrain-rule-oracle.py` to verify the committed frozen-source fragments independently of the working oracle export.
- [ ] Compile and run the five `TerrainSurfaceEmissionTest.*` cases, then the appropriate existing parity suite.
- [ ] Compile the probe and add validated Vulkan output readback against the independent frozen traces.
- [ ] Integrate these rules into the complete terrain compute pass with bounds/capacity checks, exact ordering, sprite sampling and background coverage.
- [ ] Prove actual all-rotation/zoom/border/edited-state UI pixels and agent visual review before opening admission or removing CPU terrain paint visits.

No screenshot, framerate, CPU-bandwidth, TPS, native-admission or software-removal claim follows from this stage.


## Device probe follow-up (staged, not executed)

The follow-up preserves the original eight-file stage and adds two actual-device tests in the same test translation unit. `VulkanTerrainSurfaceRulesTest` creates a graphics-only shared `DeviceContext` and one `SubmissionSlots` domain, binds the separately compiled diagnostic shader, submits compute work, waits for its real fence and invalidates mapped memory before reading results. It never substitutes execution of the host-compiled helper for GPU output.

Input/output ABI checks require 48-byte queries and 80-byte results, with output emission and metadata offsets 32 and 64. The independent expected contract is 56 bytes per query: emission count, side, all eight emission fields and four metadata fields. Those expectations come from the frozen source trace and public object-selection oracle. Six private intermediate-plan fields are retained in the 80-byte raw output but are not claimed as independently verified semantics. Every record's output stride and checked field offsets are exercised through consecutive distinct queries.

The first device test covers all 65,536 neighbor cases and every emitted index. The second covers missing neighbors at maximum admitted height, all rotations/slopes/sides, invalid heights/slopes/rotations/sides/indices, and dispatch counts 0/1/63/64/65/127/128/129/2048. Each dispatch intentionally includes an extra workgroup. Prefix/output-tail canaries and the entire read-only input allocation must remain byte-identical. Input storage is padded so a count-check regression produces detectable output guard changes without intentionally requesting an out-of-bounds descriptor access.

Probe metadata validity is explicitly a bitmask: bit 0 means tile/neighbor input validity, bit 1 means side validity and bit 2 means the requested emission index exists. Invalid sides or indices therefore cannot masquerade as fully valid queries. Invalid output emissions are zero-filled; their plan count still reports a valid side's actual emission count.

Review correction: the provenance verifier now first verifies the exact full oracle artifact SHA recorded in the reviewed metadata, then verifies each frozen fragment against Git. Changed wrappers, extra macros, commented-out originals or altered active bodies fail even when unchanged fragments remain elsewhere in the file.

`build-terrain-rule-probe.py` compiles only this test shader with an explicit `--glslc` path. Its receipt pins the compiler/DLLs, all rule/oracle/probe/test inputs, verifier, command, binary and log, and requires those sources to match the provided test-build receipt. `run-terrain-rule-probe.py` accepts that receipt plus a matching no-window build receipt, copies the exact SPIR-V to a fresh directory and sets `OPENRCT2_TERRAIN_RULE_PROBE_SPV` explicitly. There is no default-path or stale-binary fallback. The runner requires both tests, nonzero dispatches and minimum query coverage, active synchronization validation without diagnostics, unchanged artifacts, and exact independent-contract bytes. It retains all queries, complete GPU results and expected/actual contracts.

The coordinated ordinary/no-window builders must guard these additional shared test inputs before building: `FrozenTerrainEdgeOracle.inc`, `FrozenTerrainEdgeOracle.json`, and `TerrainSurfaceRulesProbe.comp` under `test/terrain-parity/`. Existing source/data manifests already need to include the production GLSL and C++ helper files. This stage lists the changes but does not overwrite root-owned serial builder work. Add the new CPU/device test suites to required counts/filters after successful execution. The test shader stays outside the production shader source list and shipping shader count.

All device work described above is code staged for root execution. Neither shader compilation nor GPU parity is claimed yet. Production terrain admission, pixel parity, final ordering and performance gates remain open.


The device runner also writes a durable failed summary for timeout, malformed XML/properties or missing artifacts, and retains the available logs. Test execution is bounded by `--timeout-seconds` (180 by default); each diagnostic compiler/verifier command is bounded at 120 seconds by default. Before/after audits cover all receipt-listed build and probe artifacts, adjacent runtime DLL membership (including a valid empty/static set), executable, isolated SPIR-V, receipts, helper, runner and validation settings. These safeguards are staged implementation requirements, not a claim that this lane has run successfully.


Oracle attribution: edge emission counts/order/fields and slope/shape expectations use the pinned frozen fragments. Material-selector metadata instead calls the existing current `TerrainSurfaceObject::GetImageId` public API with an exhaustive synthetic object table; it is independent of the new shader/helper implementation but is not represented as copied frozen code.
