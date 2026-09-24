"""Run real surface-free Vulkan contracts in a fresh, receipt-pinned process.

This lane does not initialize a presentation host or claim image-rendering parity.
GPU absence, missing fixtures, skips and validation diagnostics fail the lane.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import xml.etree.ElementTree as ET


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


OFFSCREEN_FIXTURES = tuple(f"{kind}-{alpha}" for kind in ("clear", "owned") for alpha in range(3)) + (
    "offscreen-primitives", "owned-sprite-covered-zero")
# Current offscreen runtime; this is not a historical reference package inventory.
SHADER_NAMES = {
    "indexed_line.frag.spv", "indexed_line.vert.spv", "indexed_palette.frag.spv", "indexed_palette.vert.spv",
    "indexed_rect.frag.spv", "indexed_rect.vert.spv", "indexed_sprite.vert.spv",
    "indexed_transparency_compose.frag.spv", "indexed_transparency_compose.vert.spv",
    "indexed_transparent_rect.frag.spv", "indexed_transparent_rect.vert.spv",
    "indexed_weather.frag.spv", "indexed_weather.vert.spv", "lightfx_accumulate.comp.spv", "image_alias.comp.spv",
    "rgba_scale.frag.spv", "balloon.vert.spv", "balloon_order.comp.spv", "world_surface.vert.spv", "world_surface_compact.comp.spv", "world_parent_columns.comp.spv", "world_filter_collect.frag.spv", "world_filter_resolve.comp.spv", "terrain_retained_emit.comp.spv", "terrain_columns.comp.spv", "peep_fields.comp.spv"}


DEPTH_EXTENTS = ((2048, 1440), (2047, 1439))
WATER_DEPTHS = (0, 127, 1023, 8191, 16383, 32767, 65535, 131071)


def offscreen_report_contracts():
    contracts = {}
    for fixture in OFFSCREEN_FIXTURES:
        metadata = {}
        if fixture == "owned-sprite-covered-zero":
            metadata["mutatedSourceBeforeExecution"] = True
        elif fixture.startswith(("clear-", "owned-")):
            metadata["alphaPolicy"] = int(fixture.rsplit("-", 1)[1])
        for layer in ("indexed", "rgba"):
            contracts[f"{fixture}/{layer}/report.json"] = (64, 48, "software", metadata)
    for width, height in DEPTH_EXTENTS:
        fixture = f"exact-peel-depth-{width}x{height}"
        contracts[f"{fixture}/indexed/report.json"] = (width, height, "ordered-remap-oracle", {
            "referenceImageLabel": "ordered-remap-oracle", "resultImageLabel": "vulkan", "quadGroups": 96, "peels": 3,
            "scope": "Independent ordered indexed remap composition across exact command depths; no scene exception"})
    for depth in WATER_DEPTHS:
        contracts[f"water-overlay-adjacent-depth-{depth}/indexed/report.json"] = (2048, 1440, "water-order-oracle", {
            "referenceImageLabel": "water-order-oracle", "resultImageLabel": "vulkan", "waterMaskDepth": depth,
            "opaqueOverlayDepth": depth + 1, "transparentOverlaps": 1, "bounds": [161, 31, 193, 63],
            "scope": "Synthetic water parent then opaque overlay; compose comparison only, no second peel"})
    return contracts


def verify_offscreen_reports(output):
    failures, reports = [], {}
    base = output / "samples/offscreen"
    contracts = offscreen_report_contracts()
    expected = set(contracts)
    observed = {path.relative_to(base).as_posix() for path in base.rglob("report.json")}
    if observed != expected:
        failures.append({"missingReports": sorted(expected - observed), "unexpectedReports": sorted(observed - expected)})
    for relative in sorted(expected & observed):
        report_path = base / relative
        report = json.loads(report_path.read_text(encoding="utf-8"))
        fixture, layer, _ = relative.split("/")
        width, height, reference, metadata = contracts[relative]
        channels = 1 if layer == "indexed" else 4
        valid = (report.get("fixture") == fixture and report.get("layer") == layer
                 and report.get("fixtureVersion") == 1 and report.get("width") == width and report.get("height") == height
                 and report.get("differingPixels") == 0 and report.get("acceptedExceptions") == []
                 and report.get("maxChannelError") == [0, 0, 0, 0] and report.get("firstMismatch") is None
                 and report.get("boundsInclusive") is None and report.get("visualReview") == "not-required"
                 and all(report.get(key) == value for key, value in metadata.items()))
        hashes = {"report.json": sha256(report_path)}
        for filename in (reference + ".bin", "vulkan.bin", reference + ".png", "vulkan.png", "diff.png"):
            path = report_path.parent / filename
            if not path.is_file():
                failures.append({"report": relative, "missingArtifact": filename})
                valid = False
                continue
            hashes[filename] = sha256(path)
            if filename.endswith(".bin") and path.stat().st_size != width * height * channels:
                valid = False
        if hashes.get(reference + ".bin") != hashes.get("vulkan.bin"):
            valid = False
        if not valid:
            failures.append({"report": relative, "reason": "Report metadata, bytes or artifacts failed strict validation"})
        reports[relative] = hashes
    return failures, reports

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("device", "offscreen"), default="device")
    parser.add_argument("--shader-build-receipt", type=Path)
    parser.add_argument("--timeout-seconds", type=int, default=600)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    pins = {}
    summary = {"status": "fail", "mode": args.mode, "windowedParityQualified": False,
               "failures": [], "inputAuditFailures": [], "exitCode": None, "tests": 0,
               "validationMessages": [], "validationActivated": False}
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    def pin(path, expected=None):
        path = path.resolve(strict=True)
        actual = sha256(path)
        require(expected is None or actual == expected, "Pinned input changed: " + str(path))
        require(str(path) not in pins or pins[str(path)] == actual, "Conflicting input pin")
        pins[str(path)] = actual
        return actual
    def artifact(base, relative, expected):
        path = (base / relative).resolve(strict=True)
        require(base in path.parents, "Artifact escapes receipt root: " + relative)
        pin(path, expected)
        return path
    try:
        summary["runnerSha256"] = pin(Path(__file__))
        require(args.timeout_seconds > 0, "Timeout must be positive")
        receipt_path = args.build_receipt.resolve(strict=True)
        summary["buildReceipt"] = str(receipt_path)
        summary["buildReceiptSha256"] = pin(receipt_path)
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        require(receipt.get("status") == "pass" and not any(receipt.get(key) for key in (
            "sourceChangesDuringBuild", "testChangesDuringBuild", "dependencyChangesDuringBuild", "sdkChangesDuringBuild")),
            "A successful source-stable no-window build is required")
        require(receipt.get("windowedParityQualified") is False, "This runner requires the explicit no-window partition")
        build = receipt_path.parent
        for relative, expected in receipt["artifactSha256"].items():
            artifact(build, relative, expected)
        executable = build / "bin/tests-no-window.exe"
        summary["testExecutableSha256"] = pin(executable, receipt["artifactSha256"]["bin/tests-no-window.exe"])
        required_suites = {"VulkanOffscreenDeviceTest": 2, "VulkanSubmissionSlotsContractTest": 1,
                           "VulkanPresentationHostTest": 4, "VulkanOffscreenDeviceOwnerTest": 1}
        shader_hashes = {}
        summary["shaderBuildReceipt"] = None
        if args.mode == "offscreen":
            required_suites = {"VulkanOffscreenRenderTest": 7, "VulkanOffscreenServiceContract": 6,
                               "ScreenshotTilingTest": 5, "ViewportGenerationTest": 3}
            require(args.shader_build_receipt is not None, "Offscreen mode requires --shader-build-receipt")
            shader_receipt_path = args.shader_build_receipt.resolve(strict=True)
            summary["shaderBuildReceipt"] = {"path": str(shader_receipt_path), "sha256": pin(shader_receipt_path)}
            shader_build = json.loads(shader_receipt_path.read_text(encoding="utf-8"))
            require(shader_build.get("status") == "pass" and not shader_build.get("sourceChangesDuringBuild"),
                    "Shaders require a successful source-stable ordinary build receipt")
            source_root = Path(receipt["sourceRoot"]).resolve(strict=True)
            for name, digest in shader_build["sourceSha256"].items():
                if name.startswith("data/shaders/vulkan/"):
                    require(receipt["sourceSha256"].get("source/" + name) == digest,
                            "No-window library and shader source receipts disagree: " + name)
            shader_sources = {}
            for name, digest in shader_build["artifactSha256"].items():
                if not name.endswith(".spv"):
                    continue
                source = artifact(source_root, name, digest)
                require(source.name not in shader_hashes, "Ambiguous shader binary: " + source.name)
                shader_hashes[source.name] = digest
                shader_sources[source.name] = source
            require(set(shader_hashes) == SHADER_NAMES,
                    "Offscreen lane requires exactly the receipt-qualified shader inventory: " + str(len(SHADER_NAMES)))
            shaders = output / "shaders"
            shaders.mkdir()
            for name, source in shader_sources.items():
                target = shaders / name
                shutil.copyfile(source, target)
                pin(target, shader_hashes[name])
        summary["requiredSuiteCounts"] = required_suites
        summary["shaderSha256"] = shader_hashes
        command = [str(executable), "--gtest_filter=" + ":".join(name + ".*" for name in required_suites),
                   "--gtest_output=xml:" + str(output / "tests.xml")]
        summary["command"] = command
        env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
        for key in list(env):
            if key.startswith(("OPENRCT2_", "VK_")):
                env.pop(key)
        diagnostic_env = {"OPENRCT2_REQUIRE_VULKAN_TESTS": "1", "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                          "VK_LOADER_DEBUG": "layer", "VK_LAYER_SETTINGS_PATH": str(output),
                          "OPENRCT2_TEST_USER_DATA_PATH": str(output / "profile")}
        if args.mode == "offscreen":
            diagnostic_env.update({"OPENRCT2_VULKAN_SHADER_DIRECTORY": str(output / "shaders"),
                                   "OPENRCT2_VULKAN_PARITY_ARTIFACTS": str(output / "samples")})
        env.update(diagnostic_env)
        summary["diagnosticEnvironment"] = diagnostic_env
        settings = output / "vk_layer_settings.txt"
        settings.write_text(
            "khronos_validation.validate_sync = true\n"
            "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
            "khronos_validation.log_filename = stdout\n"
            "khronos_validation.report_flags = error,warn\n"
            "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
        pin(settings)
        with (output / "tests.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=executable.parent, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
        summary["exitCode"] = result.returncode
        lines = (output / "tests.log").read_text(encoding="utf-8", errors="replace").splitlines()
        diagnostics = sorted({line for line in lines if any(marker in line.lower() for marker in
            ("vuid-", "sync-hazard", "validation error", "validation warning", "validation performance warning"))})
        activated = any("Insert instance layer" in line and "VK_LAYER_KHRONOS_validation" in line for line in lines)
        summary["validationMessages"], summary["validationActivated"] = diagnostics, activated
        cases = list(ET.parse(output / "tests.xml").getroot().iter("testcase"))
        counts = {suite: sum(case.get("classname") == suite for case in cases) for suite in required_suites}
        incomplete = [case.get("classname", "") + "." + case.get("name", "") for case in cases
                      if case.get("status") != "run" or case.find("failure") is not None
                      or case.find("error") is not None or case.find("skipped") is not None]
        summary.update({"tests": len(cases), "suiteCounts": counts, "failedSkippedOrDisabled": incomplete})
        report_failures, reports = verify_offscreen_reports(output) if args.mode == "offscreen" else ([], {})
        summary.update({"reportFailures": report_failures, "reportSha256": reports})
        require(result.returncode == 0 and counts == required_suites and len(cases) == sum(counts.values())
                and not incomplete and not diagnostics and activated and not report_failures,
                "Required tests, strict reports or validation did not pass")
        summary["status"] = "pass"
    except Exception as error:
        summary["failures"].append(type(error).__name__ + ": " + str(error))
    finally:
        for name, expected in pins.items():
            try:
                if sha256(Path(name)) != expected:
                    summary["inputAuditFailures"].append(name)
            except OSError:
                summary["inputAuditFailures"].append(name)
        if summary["inputAuditFailures"]:
            summary["status"] = "fail"
        summary["inputSha256"] = pins
        summary["timeoutSeconds"] = args.timeout_seconds
        for name, key in (("tests.log", "logSha256"), ("tests.xml", "xmlSha256")):
            path = output / name
            summary[key] = sha256(path) if path.is_file() else None
        summary["executableUnchanged"] = ("testExecutableSha256" in summary and not any(
            name.endswith("tests-no-window.exe") for name in summary["inputAuditFailures"]))
        summary["shadersUnchanged"] = not any(name.endswith(".spv") for name in summary["inputAuditFailures"])
        summary["scope"] = ("Fresh process; eight real surface-free device ownership/transfer tests; no image parity claim"
                            if args.mode == "device" else
                            "Fresh no-window process; 21 tests and 26 exact indexed/RGBA reports, including independent depth/water controls. "
                            "No main UI, whole giant CLI, LightFX, native-world or performance qualification.")
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: summary[key] for key in ("status", "tests", "validationMessages", "failures")}))
    raise SystemExit(0 if summary["status"] == "pass" else 1)


if __name__ == "__main__":
    main()
