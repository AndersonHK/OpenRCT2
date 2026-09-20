"""Run real Vulkan parity tests with isolated state and auditable artifacts.

Any failed, disabled or skipped test fails the required lane. Missing test discovery
also fails; a binary without the new parity fixtures cannot report success.
"""

import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def terrain_probe_inputs(root, receipt_path, build, *,
                         builder_name="build-terrain-rule-probe.py", shader_name="TerrainSurfaceRulesProbe.comp.spv"):
    """Validate a diagnostic shader against the selected ordinary test build."""
    if build is None:
        raise SystemExit("Terrain compute probe receipts require --build-receipt")
    if build.get("status") != "pass" or any(build.get(key) for key in (
            "sourceChangesDuringBuild", "testChangesDuringBuild", "dependencyChangesDuringBuild",
            "sdkChangesDuringBuild", "generatedInputChangesDuringBuild")):
        raise SystemExit("Terrain rules require a passing, stable ordinary build receipt")
    if "bin/tests.exe" not in build.get("artifactSha256", {}):
        raise SystemExit("Terrain rules require an ordinary build receipt pinning bin/tests.exe")
    receipt_path = receipt_path.resolve(strict=True)
    receipt_hash = sha256(receipt_path)
    probe = json.loads(receipt_path.read_text(encoding="utf-8"))
    helper = root / "scripts/rendering" / builder_name
    helper_hash = sha256(helper)
    if probe.get("builderSha256") != helper_hash:
        raise SystemExit("Terrain probe builder changed since shader compilation")
    if probe.get("status") != "pass" or probe.get("inputsUnchanged") is not True:
        raise SystemExit("A passing, unchanged terrain probe receipt is required")
    # Read the builder's literal contract without executing a helper module.
    contracts = [ast.literal_eval(node.value) for node in ast.parse(helper.read_text(encoding="utf-8")).body
                 if isinstance(node, ast.Assign) and any(
                     isinstance(target, ast.Name) and target.id == "INPUTS" for target in node.targets)]
    if len(contracts) != 1 or set(probe.get("inputSha256", {})) != set(contracts[0]):
        raise SystemExit("Terrain probe input contract is incomplete or ambiguous")
    for name, digest in probe["inputSha256"].items():
        if build.get("sourceSha256", {}).get(name) != digest:
            raise SystemExit("Ordinary test binary and terrain shader inputs disagree: " + name)
    pinned = {receipt_path: receipt_hash, helper: helper_hash}
    for name, digest in probe.get("artifactSha256", {}).items():
        path = (receipt_path.parent / name).resolve(strict=True)
        if receipt_path.parent not in path.parents or not path.is_file() or sha256(path) != digest:
            raise SystemExit("Terrain probe artifact changed/missing: " + name)
        pinned[path] = digest
    shader = receipt_path.parent / shader_name
    if shader.resolve() not in pinned:
        raise SystemExit("Terrain probe receipt does not pin its SPIR-V binary")
    return shader, pinned, {"receipt": str(receipt_path), "receiptSha256": receipt_hash,
                            "builderSha256": helper_hash, "inputSha256": probe["inputSha256"],
                            "shaderSha256": pinned[shader.resolve()]}


def terrain_draw_corpus_inputs(root, summary_path, build):
    """Bind immutable frozen/current observations without regenerating them."""
    if build is None:
        raise SystemExit("Terrain draw corpus requires --build-receipt")
    summary_path = summary_path.resolve(strict=True)
    summary_hash = sha256(summary_path)
    corpus = json.loads(summary_path.read_text(encoding="utf-8"))
    cases = corpus.get("cases", [])
    if (corpus.get("schema") != 1 or corpus.get("status") != "pass"
            or corpus.get("changedInputs") != [] or corpus.get("failures") != []
            or len(cases) != 32 or len(set(cases)) != 32):
        raise SystemExit("Passing, unchanged 32-case frozen fresh-repeat/current corpus required")
    expected_names = {"corpus.json", *cases}
    if len(expected_names) != 33 or any(
            not isinstance(name, str) or Path(name).name != name or "/" in name or "\\" in name
            or not name.endswith(".json") for name in expected_names):
        raise SystemExit("Corpus filenames must be 32 distinct contained JSON files")
    if set(corpus.get("corpusSha256", {})) != expected_names:
        raise SystemExit("Terrain draw corpus receipt artifact set is incomplete")
    pinned = {summary_path: summary_hash}
    # Frozen producer executables are historical evidence, not runtime inputs
    # of this read-only consumer. Pin its current helper/recipe contract.
    for name in ("scripts/rendering/capture-terrain-column-corpus.py",
                 "scripts/rendering/prepare-native-terrain-fixture.py",
                 "test/terrain-parity/NonuniformTerrainRecipe.h"):
        path = (root / name).resolve(strict=True)
        digest = sha256(path)
        if corpus.get("runtimeInputSha256", {}).get(str(path)) != digest:
            raise SystemExit("Corpus producer input changed: " + name)
        if name.endswith("NonuniformTerrainRecipe.h") and build.get("sourceSha256", {}).get(name) != digest:
            raise SystemExit("Corpus raw-scene recipe differs from selected test binary")
        pinned[path] = digest
    directory = (summary_path.parent / "corpus").resolve(strict=True)
    if {path.name for path in directory.iterdir()} != expected_names:
        raise SystemExit("Terrain draw corpus directory artifact set changed")
    for name, digest in corpus["corpusSha256"].items():
        path = (directory / name).resolve(strict=True)
        if directory not in path.parents or not path.is_file() or sha256(path) != digest:
            raise SystemExit("Terrain draw corpus artifact changed: " + name)
        pinned[path] = digest
    manifest = json.loads((directory / "corpus.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("cases") != cases:
        raise SystemExit("Terrain draw corpus ordered case manifest differs")
    return directory, pinned, {"summary": str(summary_path), "summarySha256": summary_hash,
                               "cases": cases, "corpusSha256": corpus["corpusSha256"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New run directory; never overwritten")
    parser.add_argument("--rct2-path", type=Path)
    parser.add_argument("--rct1-path", type=Path)
    parser.add_argument("--build-receipt", type=Path, help="Bind this run to build-parity.py's immutable receipt")
    parser.add_argument("--terrain-rule-probe-receipt", type=Path,
                        help="Passing build-terrain-rule-probe.py receipt matched to the selected test build")
    parser.add_argument("--terrain-emission-probe-receipt", type=Path,
                        help="Passing build-terrain-emission-probe.py receipt matched to the selected test build")
    parser.add_argument("--terrain-column-probe-receipt", type=Path,
                        help="Passing build-terrain-column-probe.py receipt matched to the selected test build")
    parser.add_argument("--terrain-draw-corpus-summary", type=Path,
                        help="Passing capture-terrain-column-corpus.py frozen repeat/current 32-case summary")
    parser.add_argument("--validation", action="store_true", help="Require Khronos validation and synchronization checks")
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    fixture_manifest_path = root / "docs/vulkan-parity-fixtures.json"
    fixture_manifest_hash = sha256(fixture_manifest_path)
    fixture_manifest = json.loads(fixture_manifest_path.read_text(encoding="utf-8"))
    if ("VulkanTerrainSurfaceRulesTest" in fixture_manifest["requiredSuiteMinimumCounts"]
            and not args.terrain_rule_probe_receipt):
        raise SystemExit("Required VulkanTerrainSurfaceRulesTest needs --terrain-rule-probe-receipt")
    if ("VulkanRetainedTerrainEmissionTest" in fixture_manifest["requiredSuiteMinimumCounts"]
            and not args.terrain_emission_probe_receipt):
        raise SystemExit("Required VulkanRetainedTerrainEmissionTest needs --terrain-emission-probe-receipt")
    draw_required = "VulkanRetainedTerrainDrawTest" in fixture_manifest["requiredSuiteMinimumCounts"]
    if draw_required or args.terrain_column_probe_receipt or args.terrain_draw_corpus_summary:
        if not (args.build_receipt and args.terrain_emission_probe_receipt
                and args.terrain_column_probe_receipt and args.terrain_draw_corpus_summary):
            raise SystemExit("Required VulkanRetainedTerrainDrawTest needs --build-receipt, "
                             "--terrain-emission-probe-receipt, --terrain-column-probe-receipt "
                             "and --terrain-draw-corpus-summary")
    completion_required = "VulkanTerrainCompletionTest" in fixture_manifest["requiredSuiteMinimumCounts"]
    if completion_required and not args.build_receipt:
        raise SystemExit("Required VulkanTerrainCompletionTest needs --build-receipt with shipped terrain shaders")
    available_requirements = {"rct2-assets"} if args.rct2_path else set()
    required_reports = {
        (fixture, layer)
        for group in fixture_manifest["groups"]
        if set(group["requires"]).issubset(available_requirements)
        for fixture in group["fixtures"] for layer in group["layers"]
    }
    if output.exists():
        raise SystemExit("Refusing to overwrite prior parity evidence: " + str(output))
    subprocess.run([os.sys.executable, str(root / "scripts/rendering/verify-software-reference.py")], check=True)
    build_receipt = None
    pinned_inputs = {fixture_manifest_path: fixture_manifest_hash}
    if args.build_receipt:
        receipt_path = args.build_receipt.resolve(strict=True)
        receipt_hash = sha256(receipt_path)
        build_receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        pinned_inputs[receipt_path] = receipt_hash
        if build_receipt.get("status") != "pass":
            raise SystemExit("Build provenance receipt did not pass")
        for relative, expected in build_receipt["artifactSha256"].items():
            artifact = (root / relative).resolve()
            if root not in artifact.parents or not artifact.is_file() or sha256(artifact) != expected:
                raise SystemExit("Build artifact changed/missing: " + relative)
            pinned_inputs[artifact] = expected
    completion_shader_proof = {}
    if completion_required:
        for shader_name in ("terrain_retained_emit.comp.spv", "terrain_columns.comp.spv"):
            relative = "bin/data/shaders/vulkan/" + shader_name
            digest = build_receipt["artifactSha256"].get(relative)
            if digest is None:
                raise SystemExit("Completion tests require the shipped receipt-pinned shader: " + relative)
            # The build-artifact loop above pinned the exact files consumed by
            # OPENRCT2_VULKAN_SHADER_DIRECTORY, including before/after run audits.
            completion_shader_proof[relative] = digest
    probe_proof = None
    if args.terrain_rule_probe_receipt:
        probe_shader, probe_inputs, probe_proof = terrain_probe_inputs(root, args.terrain_rule_probe_receipt, build_receipt)
        pinned_inputs.update(probe_inputs)
    emission_proof = None
    if args.terrain_emission_probe_receipt:
        emission_shader, emission_inputs, emission_proof = terrain_probe_inputs(
            root, args.terrain_emission_probe_receipt, build_receipt,
            builder_name="build-terrain-emission-probe.py", shader_name="terrain_retained_emit.comp.spv")
        pinned_inputs.update(emission_inputs)
    draw_proof = None
    if args.terrain_column_probe_receipt:
        column_shader, column_inputs, column_proof = terrain_probe_inputs(
            root, args.terrain_column_probe_receipt, build_receipt,
            builder_name="build-terrain-column-probe.py", shader_name="terrain_columns.comp.spv")
        corpus_directory, corpus_inputs, corpus_proof = terrain_draw_corpus_inputs(
            root, args.terrain_draw_corpus_summary, build_receipt)
        pinned_inputs.update(column_inputs)
        pinned_inputs.update(corpus_inputs)
        draw_proof = {"columnProbe": column_proof, "corpus": corpus_proof}
    output.mkdir(parents=True)
    # Windows launchers may inherit both Path/PATH; canonicalize environment keys.
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
    # Diagnostic and Vulkan policy must be explicit: inherited VK_LAYER_DISABLES
    # can defeat validation even while layer activation appears in the log.
    for key in list(env):
        if key.startswith(("OPENRCT2_", "VK_")):
            env.pop(key)
    if probe_proof:
        isolated_shader = output / "TerrainSurfaceRulesProbe.comp.spv"
        shutil.copyfile(probe_shader, isolated_shader)
        if sha256(isolated_shader) != probe_proof["shaderSha256"]:
            raise SystemExit("Terrain probe changed during isolated copy")
        pinned_inputs[isolated_shader] = probe_proof["shaderSha256"]
        env["OPENRCT2_TERRAIN_RULE_PROBE_SPV"] = str(isolated_shader)
        env["OPENRCT2_TERRAIN_RULE_ARTIFACTS"] = str(output / "terrain-rule-samples")
    if emission_proof:
        isolated_shader = output / "terrain_retained_emit.comp.spv"
        shutil.copyfile(emission_shader, isolated_shader)
        if sha256(isolated_shader) != emission_proof["shaderSha256"]:
            raise SystemExit("Terrain emission probe changed during isolated copy")
        pinned_inputs[isolated_shader] = emission_proof["shaderSha256"]
        env["OPENRCT2_TERRAIN_EMISSION_SPV"] = str(isolated_shader)
        env["OPENRCT2_TERRAIN_EMISSION_ARTIFACTS"] = str(output / "terrain-emission-samples")
    if draw_proof:
        isolated_shader = output / "terrain_columns.comp.spv"
        shutil.copyfile(column_shader, isolated_shader)
        if sha256(isolated_shader) != column_proof["shaderSha256"]:
            raise SystemExit("Terrain column probe changed during isolated copy")
        pinned_inputs[isolated_shader] = column_proof["shaderSha256"]
        isolated_corpus = output / "terrain-draw-corpus"
        isolated_corpus.mkdir()
        for name, digest in corpus_proof["corpusSha256"].items():
            destination = isolated_corpus / name
            shutil.copyfile(corpus_directory / name, destination)
            if sha256(destination) != digest:
                raise SystemExit("Terrain draw corpus changed during isolated copy: " + name)
            pinned_inputs[destination] = digest
        env["OPENRCT2_TERRAIN_COLUMNS_SPV"] = str(isolated_shader)
        env["OPENRCT2_TERRAIN_DRAW_CORPUS"] = str(isolated_corpus)
        env["OPENRCT2_TERRAIN_DRAW_ARTIFACTS"] = str(output / "terrain-draw-samples")
    env.update({
        "OPENRCT2_REQUIRE_VULKAN_TESTS": "1",
        "OPENRCT2_VULKAN_PARITY_ARTIFACTS": str(output / "samples"),
        "OPENRCT2_TEST_USER_DATA_PATH": str(output / "profile"),
        "OPENRCT2_VULKAN_SHADER_DIRECTORY": str(root / "bin/data/shaders/vulkan"),
    })
    if args.validation:
        env["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
        env["VK_LOADER_DEBUG"] = "layer"
        env["VK_LAYER_SETTINGS_PATH"] = str(output)
        (output / "vk_layer_settings.txt").write_text(
            "khronos_validation.validate_sync = true\n"
            "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
            "khronos_validation.log_filename = stdout\n"
            "khronos_validation.report_flags = error,warn\n"
            "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
    licensed_assets = {}
    if args.rct2_path:
        path = args.rct2_path.resolve(strict=True)
        env["OPENRCT2_TEST_RCT2_PATH"] = str(path)
        env["OPENRCT2_PARITY_RCT2_PATH"] = str(path)
        for name in ("g1.dat",):
            asset = path / "Data" / name
            if not asset.exists():
                asset = path / "data" / name
            licensed_assets[name] = {"path": str(asset), "sha256": sha256(asset)}
    if args.rct1_path:
        path = args.rct1_path.resolve(strict=True)
        env["OPENRCT2_TEST_RCT1_PATH"] = str(path)
        for name in ("csg1.dat", "csg1i.dat"):
            asset = path / "Data" / name
            if not asset.exists():
                asset = path / "data" / name
            licensed_assets[name] = {"path": str(asset), "sha256": sha256(asset)}
    executable = root / "bin/tests.exe"
    executable_before = sha256(executable)
    if build_receipt and executable_before != build_receipt["artifactSha256"].get("bin/tests.exe"):
        raise SystemExit("Selected test executable no longer matches the build receipt")
    pinned_inputs[executable] = executable_before
    arguments = [str(executable), "--gtest_output=xml:" + str(output / "tests.xml")]
    if not args.full:
        arguments.append("--gtest_filter=*Vulkan*ParityTest.*:VulkanCoverageCacheTest.*:PublicationSnapshotParityTest.*:SpriteAssetDecoderTest.*:GpuFoundationTest.*:VulkanRuntimeIntegrationTest.*:VulkanDiagnosticCaptureTest.*:AssetMetadataCorpusParityTest.*:RenderServiceContract.*:RenderServiceLazyLifetime.*:ScreenshotTilingTest.*:ViewportGenerationTest.*:VulkanPresentationHostTest.*:VulkanOffscreenDeviceTest.*:VulkanOffscreenDeviceOwnerTest.*:VulkanSubmissionSlotsContractTest.*:RetainedBalloonSceneTest.*:RetainedBalloonHookTest.*:TerrainSurfaceRulesTest.*:TerrainPresentationBridgeTest.*:TerrainSurfaceEmissionTest.*:VulkanTerrainSurfaceRulesTest.*:VulkanRetainedTerrainEmissionTest.*:VulkanRetainedTerrainDrawTest.*:VulkanTerrainCompletionTest.*:VulkanOffscreenRenderTest.*:VulkanOffscreenServiceContract.*:VulkanBalloonPipelineTest.*:VulkanBalloonAdmissionTest.*")
    with (output / "tests.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(arguments, cwd=root / "bin", env=env, stdout=log, stderr=subprocess.STDOUT)
    validation_messages = []
    if args.validation:
        log_lines = (output / "tests.log").read_text(encoding="utf-8", errors="replace").splitlines()
        validation_markers = ("vuid-", "sync-hazard", "validation error", "validation warning",
                              "validation performance warning")
        validation_messages = sorted(set(line for line in log_lines
                                         if any(marker in line.lower() for marker in validation_markers)))
        if not any("Insert instance layer" in line and "VK_LAYER_KHRONOS_validation" in line for line in log_lines):
            validation_messages.append("Required validation layer activation was not confirmed by loader diagnostics")
    tests = []
    if (output / "tests.xml").exists():
        tests = list(ET.parse(output / "tests.xml").getroot().iter("testcase"))
    incomplete = [case.get("classname", "") + "." + case.get("name", "") for case in tests
                  if case.find("failure") is not None or case.find("error") is not None
                  or case.find("skipped") is not None or case.get("status") != "run"]
    parity_count = sum("Vulkan" in case.get("classname", "") and "ParityTest" in case.get("classname", "") for case in tests)
    reports = []
    for report in sorted((output / "samples").rglob("report.json")):
        reports.append({"path": report.relative_to(output).as_posix(), "sha256": sha256(report),
                        "data": json.loads(report.read_text(encoding="utf-8"))})
    samples = {path.relative_to(output).as_posix(): sha256(path)
               for path in sorted((output / "samples").rglob("*.png"))}
    shaders = {path.name: sha256(path) for path in sorted((root / "bin/data/shaders/vulkan").glob("*.spv"))}
    observed_reports = {(report["data"].get("fixture"), report["data"].get("layer")) for report in reports}
    missing_reports = sorted(required_reports - observed_reports)
    required_artifacts = fixture_manifest.get("requiredJsonArtifacts", [])
    missing_artifacts = []
    json_artifacts = {}
    for relative in required_artifacts:
        path = output / "samples" / relative
        if not path.is_file():
            missing_artifacts.append(relative)
        else:
            artifact_data = json.loads(path.read_text(encoding="utf-8"))
            json_artifacts[relative] = {"sha256": sha256(path), "data": artifact_data}
            if artifact_data.get("passed") is not True or artifact_data.get("objectCount", 0) == 0:
                missing_artifacts.append(relative + ": did not report a passing nonempty corpus")
    suite_counts = {}
    for case in tests:
        name = case.get("classname", "")
        suite_counts[name] = suite_counts.get(name, 0) + 1
    missing_test_counts = {
        name: {"required": count, "observed": suite_counts.get(name, 0)}
        for name, count in fixture_manifest["requiredSuiteMinimumCounts"].items()
        if suite_counts.get(name, 0) < count
    }
    new_sources = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "--", "src", "test", "data"],
        cwd=root, text=True,
    ).splitlines()
    input_changes = [str(path) for path, digest in pinned_inputs.items()
                     if not path.is_file() or sha256(path) != digest]
    terrain_artifacts = {path.relative_to(output).as_posix(): {"sha256": sha256(path), "bytes": path.stat().st_size}
                         for path in sorted((output / "terrain-rule-samples").rglob("*")) if path.is_file()}
    if probe_proof:
        probe_proof.update({"inputsUnchanged": not input_changes, "artifactSha256": terrain_artifacts,
                            "scope": "Full-suite execution and raw evidence only; the dedicated no-window terrain probe "
                                     "runner remains required for semantic artifact and dispatch coverage qualification."})
    emission_artifacts = {path.relative_to(output).as_posix(): {"sha256": sha256(path), "bytes": path.stat().st_size}
                          for path in sorted((output / "terrain-emission-samples").rglob("*")) if path.is_file()}
    if emission_proof:
        emission_proof.update({"inputsUnchanged": not input_changes, "artifactSha256": emission_artifacts,
                               "scope": "Full-suite execution and raw retained-emission evidence only; "
                                        "run-terrain-emission-probe.py remains mandatory for all 27 sample buffers, "
                                        "poison guards, frozen descriptor comparison and revision-upload qualification. "
                                        "No terrain pixels, ordering, runtime admission or performance qualification."})
    draw_artifacts = {path.relative_to(output).as_posix(): {"sha256": sha256(path), "bytes": path.stat().st_size}
                      for path in sorted((output / "terrain-draw-samples").rglob("*")) if path.is_file()}
    if draw_proof:
        draw_proof.update({"inputsUnchanged": not input_changes, "artifactSha256": draw_artifacts,
                           "scope": "Full-suite execution and raw retained-drawing evidence only; "
                                    "run-terrain-draw-probe.py remains mandatory for all 50 samples (32 frozen camera "
                                    "samples plus guards/generations), ordering, indexed pixels, ABI readbacks and retention checks. "
                                    "No runtime admission, final UI or Gate P performance qualification."})
    completion_reports, completion_report_failures = {}, []
    if completion_required:
        for name in ("retirement-error", "abandoned-status"):
            relative = "samples/terrain-completion/" + name + ".json"
            path = output / relative
            if not path.is_file():
                completion_report_failures.append(relative + ": missing")
                continue
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                valid = data.get("errorContract") is True and data.get("pixelParityClaim") is False
                if name == "retirement-error":
                    valid &= (data.get("latched") is True and data.get("statusReadbackBytes") == 240
                              and data.get("windowPresentationTested") is False
                              and "viewport=0" in data.get("message", "") and "error=1" in data.get("message", ""))
                else:
                    valid &= data.get("unsubmittedStatusDiscarded") is True and data.get("reusedSlotHealthy") is True
                completion_reports[relative] = {"sha256": sha256(path), "data": data}
                if not valid:
                    completion_report_failures.append(relative + ": status evidence differs")
            except (ValueError, TypeError, AttributeError) as error:
                completion_report_failures.append(relative + ": " + str(error))
    success = (result.returncode == 0 and len(tests) > 0 and parity_count > 0
               and not incomplete and not missing_reports and not missing_artifacts and not missing_test_counts
               and not validation_messages and not input_changes and not completion_report_failures)
    summary = {
        "schema": 1, "status": "pass" if success else "fail", "exitCode": result.returncode,
        "sourceRevision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "workingTreeDiffSha256": hashlib.sha256(subprocess.check_output(["git", "diff", "--", "src", "test", "data"], cwd=root, stderr=subprocess.PIPE)).hexdigest(),
        "provenanceNote": "Source fingerprints describe the working tree at report time; the binary hash identifies the executed build. Read the execution checklist for its build log.",
        "testBinarySha256": executable_before, "testBinarySha256After": sha256(executable) if executable.is_file() else None,
        "inputsChangedDuringRun": input_changes, "terrainRuleProbe": probe_proof,
        "terrainEmissionProbe": emission_proof, "terrainDrawProbe": draw_proof,
        "terrainCompletion": {"shaderSha256": completion_shader_proof, "reports": completion_reports,
                              "failures": completion_report_failures, "pixelParityClaim": False},
        "tests": len(tests), "parityTests": parity_count,
        "failedSkippedOrDisabled": incomplete, "licensedAssets": licensed_assets,
        "missingReports": missing_reports,
        "missingJsonArtifacts": missing_artifacts, "jsonArtifacts": json_artifacts,
        "missingTestCounts": missing_test_counts,
        "fixtureManifest": {"path": fixture_manifest_path.relative_to(root).as_posix(),
                            "sha256": fixture_manifest_hash},
        "untrackedSourceSha256": {path: sha256(root / path) for path in new_sources},
        "shaderSha256": shaders, "reports": reports, "imageSha256": samples,
        "visualReview": "Every divergent fixture requires a recorded agent visual review; passing tests do not approve exceptions.",
        "buildReceipt": {"path": str(receipt_path), "sha256": receipt_hash} if build_receipt else None,
        "validationRequested": args.validation, "validationDiagnostics": validation_messages,
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: summary[key] for key in ("status", "exitCode", "tests", "parityTests", "failedSkippedOrDisabled")}, indent=2))
    print("Evidence:", output)
    raise SystemExit(0 if success else 1)


if __name__ == "__main__":
    main()
