"""Qualify bounded real CLI lifecycle and rejection paths with receipt-pinned E5 inputs.

Configured mode uses the production factory through an observation wrapper; the default remains diagnostic.
The opt-in giant-v2 contract requires a complete separately captured and manually reviewed giant parity corpus.
Legacy contract output remains version 1 and is retained only for historical qualification.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback

HELPER = Path(__file__).with_name("run-screenshot-parity.py")
spec = importlib.util.spec_from_file_location("screenshot_parity", HELPER)
parity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parity)
require, read, pin, child = parity.require, parity.read, parity.pin, parity.child
CASE_NAMES = ("help", "version", "missing-park", "giant-rejected", "oversized-rejected")
GIANT_V2_CASE_NAMES = ("help", "version", "missing-park", "oversized-rejected", "unsupported-zoom", "missing-shader")
GIANT_CASE_NAMES = tuple(f"{background}-r{rotation}z{zoom}" for background in ("ordinary", "transparent")
                         for rotation, zoom in [(r, z) for z in (0, 1) for r in range(4)] + [(0, 2), (0, 3)])
GIANT_BUFFERS = ("indexed.bin", "palette.bin", "alpha.bin", "rgba.bin")
GIANT_IDENTITY = ("renderer", "factory", "buildReceipt", "executable", "runtimeDllSha256", "shaderBuildReceipt",
                  "shaderSha256", "park", "assets", "referenceReceipt", "dataSha256", "fixture")


def validate_giant_summary(value):
    require(value.get("schema") == 3 and value.get("kind") == "giant-cli-parity" and value.get("status") == "pass"
            and value.get("failures") == [] and value.get("inputAuditFailures") == [], "Passing giant corpus required")
    require(value.get("renderer") in ("frozen", "software", "vulkan"), "Unknown giant corpus renderer")
    require(value["renderer"] == "frozen" or value.get("factory") == "configured", "Production giant factory required")
    cases = value.get("cases", [])
    require(len(cases) == len(GIANT_CASE_NAMES) and {c["name"] for c in cases} == set(GIANT_CASE_NAMES),
            "All 20 giant cases required exactly once")
    references = {str(Path(p["path"]).resolve().parent) for p in value.get("comparisonReceipts", [])}
    require(len(references) == len(value.get("comparisonReceipts", [])), "Duplicate giant comparison receipts")
    for case in cases:
        require(case.get("exitCode") == 0 and case.get("failures") == [], "Giant case failed")
        comparisons = case.get("comparisons", [])
        require(len(comparisons) == len(references)
                and {str(Path(c["reference"]).resolve()) for c in comparisons} == references,
                "Giant case did not compare every declared reference")
        require(all(c.get("differingPixelsOrEntries") == dict.fromkeys(GIANT_BUFFERS, 0) for c in comparisons),
                "Giant corpus contains pixel/palette/alpha divergence")
        require(not case.get("divergenceRegions") and not case.get("manualReview", {}).get("divergenceReviewRequired"),
                "Giant divergence remains unresolved")
        if value["renderer"] == "vulkan":
            require(case.get("validationActivated") is True and case.get("validationMessages") == [],
                    "Giant validation was absent or unclean")
    distinctions = value.get("backgroundPolicyDistinctions", [])
    require(len(distinctions) == 10 and {d["camera"] for d in distinctions}
            == {name.removeprefix("ordinary-") for name in GIANT_CASE_NAMES if name.startswith("ordinary-")}
            and all(type(d.get("differentIndexedPixels")) is int and d["differentIndexedPixels"] > 0 for d in distinctions),
            "Both distinct giant background policies must be qualified")


def verify_giant_qualification(args, root, summary, evidence):
    helper = root / "scripts/rendering/run-giant-screenshot-parity.py"
    evidence.track(helper)
    module_spec = importlib.util.spec_from_file_location("giant_lifecycle_reference", helper)
    giant = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(giant)
    primary_path = args.giant_run.resolve(strict=True) / "summary.json"
    primary_pin = evidence.track(primary_path)
    primary = read(primary_path)
    require(primary.get("runnerSha256") == pin(helper)["sha256"], "Current giant corpus must use the reviewed current runner")
    validate_giant_summary(primary)
    for artifact in (primary["park"], primary["fixture"]["inputManifest"]):
        evidence.track(Path(artifact["path"]), artifact["sha256"])
    require(primary.get("renderer") == "vulkan" and primary.get("freshRepeat") is True,
            "A qualified Vulkan giant fresh repeat is mandatory")
    for key in ("buildReceipt", "executable", "runtimeDllSha256", "shaderBuildReceipt", "shaderSha256",
                "assets", "referenceReceipt", "dataSha256", "factory"):
        require(primary.get(key) == summary.get(key), "Lifecycle/giant corpus identity differs: " + key)
    records = {}
    visiting = set()

    def visit(path, expected):
        path = path.resolve(strict=True)
        require(path not in visiting, "Cyclic giant comparison graph")
        evidence.track(path, expected)
        if path in records:
            return
        require(len(records) + len(visiting) < 6, "Expected exactly two fresh processes per giant renderer")
        visiting.add(path)
        value, receipt_pin = giant.verify_reference(path.parent, primary, evidence)
        validate_giant_summary(value)
        # Historical receipts describe their captured sources; current source bytes may legitimately have changed.
        # Revalidate immutable executables/libraries and captured output, rather than claiming hermetic rebuild proof.
        build_pin = value["buildReceipt"]
        build_path = Path(build_pin["path"])
        evidence.track(build_path, build_pin["sha256"])
        build = read(build_path)
        require(build.get("status") == "pass" and build.get("exitCode") == 0, "Giant binary build was not successful")
        require(build.get("artifactSha256"), "Giant binary artifact manifest missing")
        for name, digest in build["artifactSha256"].items():
            evidence.track(child(build_path.parent, name), digest)
        evidence.track(Path(value["executable"]["path"]), value["executable"]["sha256"])
        for case in value["cases"]:
            folder = path.parent / case["name"]
            log_pin = case["log"]
            require(Path(log_pin["path"]).resolve() == (folder / "capture.log").resolve(), "Giant log path differs")
            evidence.track(folder / "capture.log", log_pin["sha256"])
            if value["renderer"] != "frozen":
                extent = primary["fixture"]["cameras"][case["name"].split("-")[-1]]["extent"]
                buffers = {name: (folder / name).read_bytes() for name in GIANT_BUFFERS}
                report, artifacts, tiles = giant.validate_tiles(folder, value["renderer"], buffers, extent, args)
                require(report == case["diagnostics"] and artifacts == case["diagnosticArtifacts"] and tiles == case["tiles"],
                        "Recorded giant tile evidence differs from actual artifacts")
                for artifact in artifacts.values():
                    evidence.track(Path(artifact["path"]), artifact["sha256"])
            if value["renderer"] == "vulkan":
                lines = (folder / "capture.log").read_text(encoding="utf-8", errors="replace").splitlines()
                require(any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in line for line in lines)
                        and not validation_diagnostics(lines), "Giant log validation absent or unclean")
        for reference in value["comparisonReceipts"]:
            visit(Path(reference["path"]), reference["sha256"])
        records[path] = (value, receipt_pin)
        visiting.remove(path)

    visit(primary_path, primary_pin["sha256"])
    require(len(records) == 6, "Giant qualification requires six independent complete process captures")
    for renderer in ("frozen", "software", "vulkan"):
        pair = [(path, value) for path, (value, _) in records.items() if value["renderer"] == renderer]
        require(len(pair) == 2 and sum(value.get("freshRepeat") is True for _, value in pair) == 1,
                "Each giant renderer requires one initial process and its fresh repeat")
        repeat = next(value for _, value in pair if value.get("freshRepeat") is True)
        initial_path, initial = next((path, value) for path, value in pair if value.get("freshRepeat") is not True)
        require(all(repeat.get(key) == initial.get(key) for key in GIANT_IDENTITY), "Giant repeat identity differs")
        require(any(Path(p["path"]).resolve() == initial_path for p in repeat["comparisonReceipts"]),
                "Fresh giant repeat did not compare its identical initial process")
    # Compare all four buffers across every process, independently of previously recorded comparison counts.
    for name in GIANT_CASE_NAMES:
        expected = {k: next(c for c in primary["cases"] if c["name"] == name)["images"][k]["sha256"] for k in GIANT_BUFFERS}
        for value, _ in records.values():
            actual = {k: next(c for c in value["cases"] if c["name"] == name)["images"][k]["sha256"] for k in GIANT_BUFFERS}
            require(actual == expected, "Giant cross-process buffers differ: " + name)
    review_path = args.giant_review.resolve(strict=True)
    review_pin = evidence.track(review_path)
    review = read(review_path)
    require(review.get("schema") == 1 and review.get("kind") == "giant-cli-final-visual-review"
            and review.get("status") == "pass" and review.get("manualReviewComplete") is True
            and review.get("exceptions") == [] and review.get("unresolvedDivergences") == []
            and review.get("reviewedCaseNames") == list(GIANT_CASE_NAMES), "Final complete giant visual review required")
    actual_receipts = sorted((str(Path(p["path"]).resolve()), p["sha256"]) for p in review["qualifiedSummaryReceipts"])
    required_receipts = sorted((str(path), receipt["sha256"]) for path, (_, receipt) in records.items())
    require(actual_receipts == required_receipts, "Visual review must cover these exact six qualified process receipts")
    summary["giantQualification"] = {"primary": primary_pin, "review": review_pin,
        "processReceipts": [receipt for _, receipt in records.values()], "casesPerProcess": 20,
        "pixelExceptions": [], "scope": "Frozen/current/Vulkan fresh pairs, indexed/palette/alpha/RGBA and bounded tile ownership"}


def validation_diagnostics(lines):
    return [line for line in lines if any(term in line.lower() for term in
            ("vuid-", "sync-hazard", "validation error", "validation warning", "validation performance warning"))]


def missing_shader_overlay(folder, base_env, evidence):
    original = Path(base_env["OPENRCT2_ORACLE_DATA_PATH"]).resolve(strict=True)
    source_shader = original / "shaders/vulkan/indexed_rect.vert.spv"
    require(source_shader.is_file(), "Missing-shader control needs an initially qualified shader")
    original_manifest = evidence.tree(original)
    overlay = folder / "missing-shader-data"
    shutil.copytree(original, overlay, ignore=lambda directory, names:
                    [source_shader.name] if Path(directory).resolve() == source_shader.parent else [])
    expected = {name: digest for name, digest in original_manifest.items() if name != "shaders/vulkan/" + source_shader.name}
    actual = evidence.tree(overlay)
    require(actual == expected and not (overlay / "shaders/vulkan" / source_shader.name).exists(),
            "Missing-shader overlay must omit exactly one qualified shader")
    return overlay, {"kind": "one-missing-shader", "relativePath": "shaders/vulkan/" + source_shader.name,
                     "qualifiedSource": evidence.track(source_shader), "overlay": str(overlay), "sha256": actual}



def preflight(args, root, output, summary, evidence):
    evidence.track(Path(__file__))
    evidence.track(HELPER)
    verifier = root / "scripts/rendering/verify-software-reference.py"
    evidence.track(verifier)
    with (output / "reference-verification.log").open("w", encoding="utf-8") as log:
        checked = subprocess.run([sys.executable, str(verifier)], stdout=log, stderr=subprocess.STDOUT, timeout=180)
    summary["referenceVerificationLog"] = pin(output / "reference-verification.log")
    require(checked.returncode == 0, "Frozen software reference verification failed")
    reference_path = root / "docs/vulkan-software-reference.json"
    reference = read(reference_path)
    summary["referenceReceipt"] = evidence.track(reference_path)
    frozen = root / reference["localReference"]
    park = frozen / "testdata/parks/small_park_with_ferris_wheel.sv6"
    data = frozen / "package/data"
    summary["park"] = evidence.track(park)
    summary["dataSha256"] = evidence.tree(data)
    summary["assets"] = {name: evidence.track(folder.resolve(strict=True) / "Data" / name)
                         for folder, names in ((args.rct1, ("csg1.dat", "csg1i.dat")),
                                               (args.rct2, ("g1.dat",))) for name in names}
    receipt_path = args.build_receipt.resolve(strict=True)
    summary["buildReceipt"] = evidence.track(receipt_path)
    receipt = read(receipt_path)
    require(receipt.get("status") == "pass" and receipt.get("exitCode") == 0,
            "Successful E5 diagnostic screenshot build required")
    require(all(receipt.get(key) == [] for key in (
        "sourceChangesDuringBuild", "dependencyChangesDuringBuild", "fixedBuildInputChangesDuringBuild",
        "runtimeDllCopyMismatch", "missingArtifacts")) and receipt.get("reuseReceiptUnchanged") is True,
        "Build must include passing source/dependency/copied-input stability checks")
    source = Path(receipt["sourceRoot"]).resolve(strict=True)
    require(source == root, "Lifecycle lane requires the current workspace's qualified build")
    evidence.track(receipt_path.parent / "build.log", receipt["buildLogSha256"])
    evidence.track(Path(receipt["reuseReceipt"]["path"]), receipt["reuseReceipt"]["sha256"])
    for name, digest in receipt["sourceSha256"].items():
        prefix, relative = name.split("/", 1)
        require(prefix in ("source", "harness"), "Unknown source-manifest prefix")
        evidence.track(child(source, relative), digest)
    for name, digest in receipt["dependencySha256"].items():
        evidence.track(child(source / "lib/x64", name), digest)
    for name, digest in receipt["fixedBuildInputSha256"].items():
        evidence.track(Path(name), digest)
    for name, digest in receipt["generatedProjectSha256"].items():
        evidence.track(child(receipt_path.parent, name), digest)
    if args.factory == "configured":
        parity.verify_configured_build(receipt, receipt_path, root, evidence)
    for name, digest in receipt["artifactSha256"].items():
        evidence.track(child(receipt_path.parent, name), digest)
    for name, digest in receipt["runtimeDllSha256"].items():
        evidence.track(child(receipt_path.parent / "bin", name), digest)
    executable = receipt_path.parent / "bin/screenshot-parity.exe"
    require("bin/screenshot-parity.exe" in receipt["artifactSha256"], "Receipt does not pin the diagnostic CLI")
    runtime = evidence.tree(executable.parent, "*.dll", require_nonempty=False)
    require(runtime == receipt["runtimeDllSha256"], "Runtime DLL inventory differs from the build")
    summary.update({"executable": evidence.track(executable), "runtimeDllSha256": runtime})
    shader_receipt_path = args.shader_build_receipt.resolve(strict=True)
    summary["shaderBuildReceipt"] = evidence.track(shader_receipt_path)
    shader_receipt = read(shader_receipt_path)
    require(shader_receipt.get("status") == "pass" and not shader_receipt.get("sourceChangesDuringBuild"),
            "Successful stable shader build required")
    for name, digest in shader_receipt["sourceSha256"].items():
        if name.startswith("data/shaders/vulkan/"):
            require(receipt["sourceSha256"].get("source/" + name) == digest, "CLI/shader source receipts differ")
    if args.factory == "configured":
        data = parity.configured_data_overlay(data, output, summary["dataSha256"], evidence)
    shaders = data / "shaders/vulkan" if args.factory == "configured" else output / "shaders"
    shaders.mkdir(parents=True, exist_ok=args.factory == "configured")
    shader_hashes = {}
    for name, digest in shader_receipt["artifactSha256"].items():
        if name.endswith(".spv"):
            original = child(root, name)
            evidence.track(original, digest)
            require(original.name not in shader_hashes, "Duplicate shader basename")
            copied = shaders / original.name
            parity.install_shader(original, copied, digest, evidence,
                                  summary["dataSha256"] if args.factory == "configured" else None)
            shader_hashes[original.name] = digest
    require(set(shader_hashes) == parity.SHADER_NAMES, "Shader inventory differs from the current receipt-qualified renderer")
    evidence.tree(shaders)
    summary["shaderSha256"] = shader_hashes
    if args.factory == "configured":
        summary["installedDataSha256"] = parity.pin_installed_data(data, summary["dataSha256"], shader_hashes, evidence)
    settings = output / "vk_layer_settings.txt"
    settings.write_text("khronos_validation.validate_sync = true\n"
                        "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
                        "khronos_validation.log_filename = stdout\n"
                        "khronos_validation.report_flags = error,warn\n"
                        "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
    summary["validationSettings"] = evidence.track(settings)
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()
           if not key.upper().startswith(("OPENRCT2_", "VK_"))}
    env.update({"OPENRCT2_DIAGNOSTIC_OFFSCREEN_SCREENSHOT": "1", "OPENRCT2_ORACLE_DATA_PATH": str(data),
                "OPENRCT2_ORACLE_RCT1_PATH": str(args.rct1.resolve()),
                "OPENRCT2_ORACLE_RCT2_PATH": str(args.rct2.resolve()),
                "OPENRCT2_VULKAN_SHADER_DIRECTORY": str(shaders), "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                "VK_LAYER_SETTINGS_PATH": str(output), "VK_LOADER_DEBUG": "error,warn,layer"})
    if args.factory == "configured":
        env["OPENRCT2_CLI_CONFIGURED_FACTORY"] = "1"
        env.pop("OPENRCT2_DIAGNOSTIC_OFFSCREEN_SCREENSHOT")
        env.pop("OPENRCT2_VULKAN_SHADER_DIRECTORY")
    return executable, park, env


def case_command(name, executable, park, png, folder):
    if name in ("help", "version"):
        command = [str(executable), "--" + name]
    elif name in ("giant-rejected", "unsupported-zoom"):
        command = [str(executable), "screenshot", str(park), str(png), "giant",
                   "4" if name == "unsupported-zoom" else "0", "0"]
    else:
        require(name in ("missing-park", "oversized-rejected", "missing-shader"), "Unknown lifecycle case")
        input_park = folder / "deliberately-missing.park" if name == "missing-park" else park
        require(name != "missing-park" or not input_park.exists(), "Missing-park control path unexpectedly exists")
        # One row above the default 4,194,304-pixel pool: rejected before target allocation or submission.
        width, height = (2049, 2048) if name == "oversized-rejected" else (640, 480)
        command = [str(executable), "screenshot", str(input_park), str(png), str(width), str(height),
                   "336", "112", "144", "0", "0"]
    return command


def run_case(name, args, output, executable, park, base_env, evidence):
    folder = output / name
    folder.mkdir()
    profile = folder / "profile"
    profile.mkdir()
    config = "[general]\nrct1_path = " + parity.ini(args.rct1.resolve()) + "\ngame_path = " + parity.ini(args.rct2.resolve()) + "\n"
    if args.factory == "configured":
        config += "drawing_engine = " + parity.ini("VULKAN" if args.renderer == "vulkan" else "SOFTWARE_HWD") + "\n"
    (profile / "config.ini").write_text(config, encoding="utf-8")
    (folder / "config-input.ini").write_text(config, encoding="utf-8")
    png = folder / "screen.png"
    expected_services, expected_devices = {"oversized-rejected": (1, 0), "missing-shader": (1, 1)}.get(name, (0, 0))
    success = name in ("help", "version")
    message = {"missing-park": "Failed to load park.",
               "giant-rejected": "Giant screenshots require tiled offscreen rendering",
               "oversized-rejected": "Offscreen target exceeds the configured bounded target pool",
               "unsupported-zoom": "Giant screenshot zoom is unsupported",
               "missing-shader": "Unable to open Vulkan shader:"}.get(name)
    command = case_command(name, executable, park, png, folder)
    env = dict(base_env, OPENRCT2_ORACLE_USER_PATH=str(profile), OPENRCT2_CLI_PARITY_ARTIFACTS=str(folder / "capture"))
    case = {"name": name, "command": command, "exitCode": None, "failures": [],
            "expectedSuccess": success, "expectedServices": expected_services, "expectedDevices": expected_devices,
            "expectedDiagnostic": message, "configInput": evidence.track(folder / "config-input.ini")}
    try:
        if name == "missing-shader":
            overlay, fault = missing_shader_overlay(folder, base_env, evidence)
            env["OPENRCT2_ORACLE_DATA_PATH"] = str(overlay)
            case["controlledInputFault"] = fault
        with (folder / "capture.log").open("w", encoding="utf-8") as log:
            completed = subprocess.run(command, cwd=executable.parent, env=env, stdout=log,
                                       stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
        case["exitCode"] = completed.returncode
        require(completed.returncode == (0 if success else 1), "Unexpected CLI process exit code")
        report = read(folder / "capture/report.json")
        case["report"] = report
        require(report.get("schema") == 1 and report.get("fixture") == "screenshot-cli"
                and report.get("fixtureVersion") == 1 and report.get("mode") == args.renderer, "Wrong diagnostic report")
        require(report.get("exitCode") == completed.returncode and report.get("error") == "",
                "Command did not finish through its expected real dispatcher/handler path")
        require(report.get("serviceCreations") == expected_services and report.get("deviceCreations") == expected_devices,
                "Unexpected service/device creation")
        if args.factory == "configured":
            parity.validate_configured_report(report, args.renderer, expected_devices,
                                              require_selection=name in ("giant-rejected", "oversized-rejected", "unsupported-zoom", "missing-shader"))
            if name in ("help", "version", "missing-park"):
                require(report["productionFactory"]["enabledChecks"] == [],
                        "Non-render or missing-park command unexpectedly evaluated render selection")
        else:
            require("productionFactory" not in report, "Diagnostic lane unexpectedly used configured factory")
        require(report.get("capture") is None, "Lifecycle/rejection command produced a successful output claim")
        if not expected_services:
            require(report.get("assetState") is None, "Device-free command unexpectedly invoked the factory")
        else:
            state = report.get("assetState") or {}
            require(state.get("rct1Required") is True and state.get("rct1CsgLoaded") is True
                    and state.get("g1RecordCount") == 29294 and state.get("g1PayloadCount", 0) > 0,
                    "Rejection did not reach the loaded-asset service boundary")
            require(Path(state["configuredRct1Path"]).resolve() == args.rct1.resolve()
                    and Path(state["configuredRct2Path"]).resolve() == args.rct2.resolve(), "Loaded asset paths differ")
        text = (folder / "capture.log").read_text(encoding="utf-8", errors="replace")
        require(not message or message in text, "Expected explicit rejection diagnostic is absent")
        if name == "missing-shader":
            require("indexed_rect.vert.spv" in text, "Failure did not identify the deliberately missing shader")
    except subprocess.TimeoutExpired:
        case["timedOut"] = True
        case["failures"].append("CLI timed out; child killed and partial evidence retained")
    except Exception as error:
        case["failures"].append(type(error).__name__ + ": " + str(error))
    finally:
        lines = (folder / "capture.log").read_text(encoding="utf-8", errors="replace").splitlines() if (folder / "capture.log").is_file() else []
        diagnostics = validation_diagnostics(lines)
        active = any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in line for line in lines)
        case.update({"validationActivated": active, "validationMessages": diagnostics})
        if diagnostics:
            case["failures"].append("Vulkan validation diagnostics emitted")
        report = case.get("report") or {}
        if report.get("deviceCreations", 0) > 0 and not active:
            case["failures"].append("Created device without confirmed validation activation")
        forbidden = [path for path in folder.rglob("*") if path.is_file() and
                     (path == png or (folder / "capture" in path.parents and path.suffix in (".png", ".indexed", ".rgba")))]
        case["unexpectedOutputs"] = [str(path) for path in forbidden]
        if forbidden:
            case["failures"].append("Unexpected PNG or owned render output")
        case["artifacts"] = {path.relative_to(folder).as_posix(): pin(path)
                             for path in sorted(folder.rglob("*")) if path.is_file() and profile not in path.parents}
    return case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", choices=("legacy-v1", "giant-v2"), default="legacy-v1")
    parser.add_argument("--giant-run", type=Path, help="Qualified giant Vulkan fresh-repeat directory (v2 only)")
    parser.add_argument("--giant-review", type=Path, help="Final manual review JSON covering the exact six-process corpus")
    parser.add_argument("--factory", choices=("diagnostic", "configured"), default="diagnostic")
    parser.add_argument("--renderer", choices=("software", "vulkan"), default="vulkan")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--shader-build-receipt", type=Path, required=True)
    parser.add_argument("--rct1", type=Path, required=True)
    parser.add_argument("--rct2", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, default=180)
    args = parser.parse_args()
    require(1 <= args.timeout_seconds <= 300, "Timeout must be between 1 and 300 seconds")
    require(args.factory == "configured" or args.renderer == "vulkan", "Diagnostic lifecycle requires Vulkan")
    require(args.contract != "giant-v2" or (args.factory == "configured" and args.renderer == "vulkan"
            and args.giant_run is not None and args.giant_review is not None),
            "Giant v2 requires configured Vulkan and both --giant-run/--giant-review")
    require(args.contract == "giant-v2" or (args.giant_run is None and args.giant_review is None),
            "Giant evidence arguments require the giant-v2 contract")
    case_names = GIANT_V2_CASE_NAMES if args.contract == "giant-v2" else (CASE_NAMES if args.renderer == "vulkan" else CASE_NAMES[:3])
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    require(not output.exists() and root in output.parents, "Output must be a new workspace directory")
    output.mkdir(parents=True)
    evidence = parity.Evidence()
    summary = {"schema": 1, "status": "incomplete", "cases": [], "failures": [], "caseNames": list(case_names),
               "factory": args.factory, "renderer": args.renderer,
               "timeoutSeconds": args.timeout_seconds,
               "scope": "Real CLI help/version and pre-submission rejection paths. Configured mode loads renderer selection "
                        "from isolated config; software giant/large screenshots retain their existing supported behavior and are "
                        "not rejection controls. Expected device count is zero for these controls; successful GPU output is "
                        "qualified separately by run-screenshot-parity.py. No Vulkan-loader or performance qualification."}
    if args.contract == "giant-v2":
        summary.update({"schema": 2, "kind": "screenshot-cli-lifecycle", "contract": "giant-v2",
                        "scope": "Production CLI lifecycle with exact separately qualified giant corpus, ordinary pool rejection, "
                                 "unsupported giant zoom and deliberate missing-shader cleanup; no pixel exceptions."})
    parity.write_summary(output, summary, complete=False)
    try:
        executable, park, env = preflight(args, root, output, summary, evidence)
        if args.contract == "giant-v2":
            verify_giant_qualification(args, root, summary, evidence)
        for name in case_names:
            case = run_case(name, args, output, executable, park, env, evidence)
            summary["cases"].append(case)
            summary["failures"].extend(name + ": " + failure for failure in case["failures"])
            parity.write_summary(output, summary, complete=False)
        require(len(summary["cases"]) == len(case_names), "Incomplete lifecycle cases")
    except Exception as error:
        summary["failures"].append(type(error).__name__ + ": " + str(error))
        (output / "runner-error.log").write_text(traceback.format_exc(), encoding="utf-8")
    finally:
        if len(summary["cases"]) != len(case_names):
            summary["failures"].append("Runner did not complete every required lifecycle case")
        try:
            summary["inputAuditFailures"] = evidence.audit()
            summary["failures"].extend(summary["inputAuditFailures"])
        except Exception as error:
            summary["failures"].append("Input audit failed: " + str(error))
        summary["inputSha256"] = evidence.files
        parity.write_summary(output, summary, complete=True)
    print(json.dumps({"status": summary["status"], "cases": len(summary["cases"]), "failures": summary["failures"]}))
    raise SystemExit(bool(summary["failures"]))


if __name__ == "__main__":
    main()
