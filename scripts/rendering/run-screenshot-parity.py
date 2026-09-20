"""Compare real bounded screenshot commands in isolated frozen/software/Vulkan processes.

Exact indexed PNG, palette, PNG alpha and owned shader-output evidence only.
Divergences require agent visual inspection; this does not qualify presentation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback

from PIL import Image, ImageChops

WIDTH, HEIGHT = 640, 480
CASE_NAMES = tuple(f"{background}-r{rotation}z{zoom}" for background in ("ordinary", "transparent")
                   for zoom in (0, 1) for rotation in range(4))
BUFFER_LENGTHS = {"indexed.bin": WIDTH * HEIGHT, "palette.bin": 768, "rgba.bin": WIDTH * HEIGHT * 4}
SHADER_NAMES = {
    "indexed_line.frag.spv", "indexed_line.vert.spv", "indexed_palette.frag.spv", "indexed_palette.vert.spv",
    "indexed_rect.frag.spv", "indexed_rect.vert.spv", "indexed_sprite.vert.spv",
    "indexed_transparency_compose.frag.spv", "indexed_transparency_compose.vert.spv",
    "indexed_transparent_rect.frag.spv", "indexed_transparent_rect.vert.spv",
    "indexed_weather.frag.spv", "indexed_weather.vert.spv", "lightfx_accumulate.comp.spv",
    "rgba_scale.frag.spv", "balloon.vert.spv", "balloon_order.comp.spv", "world_surface.vert.spv",
    "world_surface_compact.comp.spv", "terrain_retained_emit.comp.spv", "terrain_columns.comp.spv"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def pin(path):
    return {"path": str(path.resolve(strict=True)), "sha256": sha(path)}


def ini(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


def child(base, name):
    result = (base / name).resolve(strict=True)
    require(base.resolve() in result.parents, "Receipt path escapes its root: " + str(result))
    return result


def png_buffers(path):
    with Image.open(path) as image:
        require(image.mode == "P" and image.size == (WIDTH, HEIGHT), "Invalid PNG mode/extent: " + str(path))
        indices = image.tobytes()
        palette = bytes(image.getpalette())
        transparency = image.info.get("transparency")
        require(transparency is not None, "Missing PNG tRNS: " + str(path))
        if isinstance(transparency, int):
            alpha = bytes(0 if index == transparency else 255 for index in range(256))
        else:
            require(isinstance(transparency, bytes) and len(transparency) <= 256, "Invalid tRNS table")
            alpha = transparency + bytes([255]) * (256 - len(transparency))
        require(alpha == bytes([0]) + bytes([255]) * 255, "PNG must make only palette index zero transparent")
        buffers = {"indexed.bin": indices, "palette.bin": palette, "rgba.bin": image.convert("RGBA").tobytes()}
        require(all(len(buffers[name]) == size for name, size in BUFFER_LENGTHS.items()), "Invalid PNG buffer lengths")
        return buffers


class Evidence:
    def __init__(self):
        self.files = {}
        self.trees = []

    def track(self, path, expected=None):
        value = pin(path)
        if expected is not None:
            require(value["sha256"] == expected, "Pinned input changed: " + str(path))
        previous = self.files.setdefault(value["path"], value["sha256"])
        require(previous == value["sha256"], "Input changed during preflight: " + str(path))
        return value

    def tree(self, directory, pattern="*", require_nonempty=True):
        directory = directory.resolve(strict=True)
        paths = sorted(p for p in directory.rglob(pattern) if p.is_file())
        require(paths or not require_nonempty, "Input tree is empty: " + str(directory))
        values = {p.relative_to(directory).as_posix(): self.track(p)["sha256"] for p in paths}
        self.trees.append((directory, pattern, set(values)))
        return values

    def audit(self):
        failures = []
        for name, expected in self.files.items():
            path = Path(name)
            if not path.is_file() or sha(path) != expected:
                failures.append("Input changed or disappeared during capture: " + name)
        for directory, pattern, expected in self.trees:
            actual = {p.relative_to(directory).as_posix() for p in directory.rglob(pattern) if p.is_file()}
            if actual != expected:
                failures.append("Input tree membership changed during capture: " + str(directory))
        return failures


def verify_reference(path, identity, evidence):
    receipt_pin = evidence.track(path / "summary.json")
    previous = read(path / "summary.json")
    require(previous.get("schema") == 2 and previous.get("status") == "pass" and not previous.get("failures"),
            "Reference requires a successful schema-2 receipt: " + str(path))
    if previous.get("renderer") == "frozen":
        require(previous.get("frozenBuildProvenance", {}).get("kind") == "frozen-screenshot-cli-build",
                "Frozen reference lacks qualified binary build provenance")
    for key in ("park", "assets", "referenceReceipt", "dataSha256", "fixture"):
        require(previous.get(key) == identity[key], "Reference inputs differ: " + key)
    cases = previous.get("cases", [])
    require(len(cases) == len(CASE_NAMES) and {c.get("name") for c in cases} == set(CASE_NAMES),
            "Reference must contain every unique fixture exactly once")
    for case in cases:
        require(case.get("exitCode") == 0 and not case.get("failures"), "Reference case failed")
        folder = path / case["name"]
        buffers = png_buffers(folder / "screen.png")
        for filename in ("screen.png", *BUFFER_LENGTHS):
            artifact = folder / filename
            expected = case["images"][filename]
            require(Path(expected["path"]).resolve() == artifact.resolve(), "Reference artifact path differs")
            evidence.track(artifact, expected["sha256"])
            if filename in buffers:
                require(artifact.read_bytes() == buffers[filename], "Reference raw buffer disagrees with PNG: " + str(artifact))
    return previous, receipt_pin


def validate_configured_report(report, renderer, devices, require_selection):
    production = report.get("productionFactory") or {}
    engine = 2 if renderer == "vulkan" else 0
    require(production.get("kind") == "configured" and production.get("configuredEngine") == engine
            and production.get("benchmarkOverride") is False
            and production.get("ownerCreated") is bool(devices)
            and production.get("deviceObservation") == "persistent-owner-created-state",
            "Production configured factory/loaded config/owner evidence differs")
    checks = production.get("enabledChecks")
    require(isinstance(checks, list) and (checks or not require_selection), "Production selection was not observed")
    require(all(check == {"configuredEngine": engine, "benchmarkOverride": False,
                          "enabled": renderer == "vulkan"} for check in checks),
            "Production selection disagrees with loaded configuration")


def verify_configured_build(receipt, provenance, root, evidence):
    require(all(receipt.get(key) == [] for key in (
        "sourceChangesDuringBuild", "dependencyChangesDuringBuild", "fixedBuildInputChangesDuringBuild",
        "runtimeDllCopyMismatch", "missingArtifacts")) and receipt.get("reuseReceiptUnchanged") is True,
        "Configured qualification requires complete stable build provenance")
    require(Path(receipt["sourceRoot"]).resolve(strict=True) == root, "Configured build must qualify this workspace")
    evidence.track(provenance.parent / "build.log", receipt["buildLogSha256"])
    evidence.track(Path(receipt["reuseReceipt"]["path"]), receipt["reuseReceipt"]["sha256"])
    for name, digest in receipt["sourceSha256"].items():
        prefix, relative = name.split("/", 1)
        require(prefix in ("source", "harness"), "Unknown build source prefix")
        evidence.track(child(root, relative), digest)
    require("source/src/openrct2-renderer/RenderServiceFactory.cpp" in receipt["sourceSha256"]
            and "harness/test/cli-parity/ScreenshotMain.cpp" in receipt["sourceSha256"],
            "Build must pin production factory and observation driver")
    for name, digest in receipt["dependencySha256"].items():
        evidence.track(child(root / "lib/x64", name), digest)
    for name, digest in receipt["fixedBuildInputSha256"].items():
        evidence.track(Path(name), digest)
    for name, digest in receipt["generatedProjectSha256"].items():
        evidence.track(child(provenance.parent, name), digest)


def configured_data_overlay(data, output, expected, evidence):
    overlay = output / "installed-data"
    shutil.copytree(data, overlay)
    # Verify the complete original copy now; pin the final installed tree after
    # intentional, receipt-qualified shader replacement and before any child runs.
    copied = {path.relative_to(overlay).as_posix(): sha(path)
              for path in sorted(overlay.rglob("*")) if path.is_file()}
    require(copied == expected, "Isolated installed assets differ from frozen data")
    return overlay


def install_shader(source, target, digest, evidence, frozen_hashes=None):
    require(source.name in SHADER_NAMES, "Unexpected qualified shader name: " + source.name)
    if target.exists():
        key = "shaders/vulkan/" + source.name
        require(frozen_hashes is not None and key in frozen_hashes
                and target.is_file() and sha(target) == frozen_hashes[key],
                "Existing shader destination is not the verified frozen copy: " + str(target))
    shutil.copyfile(source, target)
    evidence.track(target, digest)


def pin_installed_data(data, frozen_hashes, shader_hashes, evidence):
    expected = dict(frozen_hashes)
    expected.update({"shaders/vulkan/" + name: digest for name, digest in shader_hashes.items()})
    actual = evidence.tree(data)
    require(actual == expected, "Final installed data differs from frozen assets plus qualified shaders")
    return actual


def validate_report(folder, renderer, buffers, args):
    report_path = folder / "capture/report.json"
    report = read(report_path)
    count = int(renderer == "vulkan")
    require(report.get("schema") == 1 and report.get("fixture") == "screenshot-cli"
            and report.get("fixtureVersion") == 1 and report.get("mode") == renderer,
            "Wrong diagnostic report identity")
    require(report.get("exitCode") == 0 and report.get("error") == ""
            and report.get("serviceCreations") == count and report.get("deviceCreations") == count,
            "Service/device lifecycle contract failed")
    if args.factory == "configured":
        validate_configured_report(report, renderer, count, require_selection=True)
    else:
        require("productionFactory" not in report, "Diagnostic lane unexpectedly used configured factory")
    artifacts = {"report.json": pin(report_path)}
    if not count:
        require(report.get("capture") is None and report.get("assetState") is None,
                "Factory-free software unexpectedly reported an offscreen capture")
        return report, artifacts
    state = report["assetState"]
    require(state.get("rct1Required") is True and state.get("rct1CsgLoaded") is True
            and state.get("g1RecordCount") == 29294 and type(state.get("g1PayloadCount")) is int
            and 0 < state["g1PayloadCount"] <= 29294, "Required CSG/G1 assets were not proven loaded")
    require(Path(state["configuredRct1Path"]).resolve() == args.rct1.resolve()
            and Path(state["configuredRct2Path"]).resolve() == args.rct2.resolve(), "Loaded asset configuration differs")
    capture = report["capture"]
    require(capture.get("name") == "screenshot-cli" and capture.get("logicalExtent") == [WIDTH, HEIGHT]
            and capture.get("outputExtent") == [WIDTH, HEIGHT]
            and all(type(capture.get(key)) is int and capture[key] > 0
                    for key in ("submissionId", "targetId", "targetGeneration")), "Invalid owned capture identity/extent")
    require(capture.get("alphaPolicy") == "transparentIndexZero" and capture.get("alphaPolicyValue") == 2
            and capture.get("scaleQuality") == 0 and capture.get("indexedOutput") is True
            and capture.get("rgbaOutput") is True and capture.get("lightingEnabled") is False
            and capture.get("clearIndex") == 0 and capture.get("indexedBytes") == WIDTH * HEIGHT
            and capture.get("rgbaBytes") == WIDTH * HEIGHT * 4, "Invalid owned capture output contract")
    palette = capture["requestPalette"]
    require(capture.get("resultPalette") == palette and len(palette) == 256
            and all(isinstance(c, list) and len(c) == 4 and all(type(v) is int and 0 <= v <= 255 for v in c)
                    for c in palette), "Invalid request/result palette ownership")
    require(bytes(v for c in palette for v in c[:3]) == buffers["palette.bin"], "PNG palette differs from owned palette")
    for filename, key in (("screen.indexed", "indexed.bin"), ("screen.rgba", "rgba.bin")):
        artifact = folder / "capture" / filename
        require(artifact.read_bytes() == buffers[key], "Owned GPU output differs from PNG: " + filename)
        artifacts[filename] = pin(artifact)
    return report, artifacts


def execute(args, root, output, summary, evidence):
    verification_log = output / "reference-verification.log"
    with verification_log.open("w", encoding="utf-8") as log:
        result = subprocess.run([sys.executable, str(root / "scripts/rendering/verify-software-reference.py")],
                                stdout=log, stderr=subprocess.STDOUT, timeout=180)
    require(result.returncode == 0, "Frozen reference verification failed")
    summary["referenceVerificationLog"] = pin(verification_log)
    reference_path = root / "docs/vulkan-software-reference.json"
    reference_receipt = read(reference_path)
    frozen = root / reference_receipt["localReference"]
    park = frozen / "testdata/parks/small_park_with_ferris_wheel.sv6"
    data = frozen / "package/data"
    assets = {name: evidence.track(folder.resolve(strict=True) / "Data" / name)
              for folder, names in ((args.rct1, ("csg1.dat", "csg1i.dat")), (args.rct2, ("g1.dat",))) for name in names}
    data_hashes = evidence.tree(data)
    receipt = None
    if args.renderer == "frozen":
        require(args.oracle_source is None,
                "Source-only frozen binaries are unqualified; use --build-receipt from build-frozen-screenshot-oracle.py")
        require(args.build_receipt is not None,
                "Frozen lane requires --build-receipt from build-frozen-screenshot-oracle.py")
        provenance = args.build_receipt.resolve(strict=True)
        receipt = read(provenance)
        require(receipt.get("schema") == 1 and receipt.get("kind") == "frozen-screenshot-cli-build"
                and receipt.get("status") == "pass" and receipt.get("exitCode") == 0,
                "Successful qualified frozen CLI build required; extraction receipts are not binary provenance")
        require(all(receipt.get(key) == [] for key in ("sourceChangesDuringBuild", "dependencyChangesDuringBuild",
                    "generatedInputChangesDuringBuild", "fixedBuildInputChangesDuringBuild", "missingArtifacts"))
                and receipt.get("frozenInputsChangedDuringBuild") is False
                and receipt.get("runtimeDllCopyMismatch") is False
                and receipt.get("compilerConcurrency") == "serial",
                "Frozen CLI build lacks complete successful stability/serial-compiler checks")
        require(receipt.get("stageResults") == [{"name": "frozen-cli-driver", "exitCode": 0}],
                "Frozen CLI driver compilation did not pass")
        evidence.track(provenance.parent / "build.log", receipt["buildLogSha256"])
        frozen_inputs = receipt["frozenInputs"]
        require(frozen_inputs.get("referenceRevision") == reference_receipt["revision"]
                and frozen_inputs.get("sourceArchiveSha256") == reference_receipt["sourceArchive"]["sha256"],
                "Frozen build does not reference the accepted frozen archive/revision")
        evidence.track(frozen / "source.zip", frozen_inputs["sourceArchiveSha256"])
        require(frozen_inputs["frozenAssetSha256"] == {"package/data/" + name: value for name, value in data_hashes.items()},
                "Frozen build asset inventory differs from capture data")
        source = Path(receipt["sourceRoot"]).resolve(strict=True)
        require(root in source.parents and frozen_inputs["originalSourceSha256"], "Frozen source root/inventory is invalid")
        for name, expected in frozen_inputs["originalSourceSha256"].items():
            evidence.track(child(source, name), expected)
        for name, expected in frozen_inputs["receiptSha256"].items():
            evidence.track(Path(name), expected)
        for name, expected in receipt["generatedInputSha256"].items():
            generated = Path(name).resolve(strict=True)
            require(provenance.parent in generated.parents, "Frozen generated input escapes build directory")
            evidence.track(generated, expected)
        evidence.track(provenance.parent / "Cli.cpp", frozen_inputs["instrumentedCliSha256"])
        require("bin/openrct2-cli.exe" in receipt["artifactSha256"]
                and "bin/libopenrct2.lib" in receipt["artifactSha256"], "Frozen build artifact inventory is incomplete")
        for name, expected in receipt["artifactSha256"].items():
            evidence.track(child(provenance.parent, name), expected)
        # This builder's DLL keys include bin/, unlike the current diagnostic builder.
        require(receipt["runtimeDllSha256"] == {name: value for name, value in receipt["artifactSha256"].items()
                                               if name.endswith(".dll")}, "Frozen DLL/artifact receipt inventories disagree")
        for name, expected in receipt["runtimeDllSha256"].items():
            require(name.startswith("bin/"), "Frozen runtime DLL receipt uses an invalid relative path")
            evidence.track(child(provenance.parent, name), expected)
        library_path = Path(receipt["libraryReceipt"]).resolve(strict=True)
        evidence.track(library_path, receipt["libraryReceiptSha256"])
        library_receipt = read(library_path)
        require(set(receipt["reusedLibraries"]) == {"core"}, "Frozen CLI must reuse only the qualified software core")
        core_reuse = receipt["reusedLibraries"]["core"]
        core_hash = receipt["artifactSha256"]["bin/libopenrct2.lib"]
        require(library_receipt.get("status") == "pass"
                and not library_receipt.get("sourceChangesDuringBuild")
                and not library_receipt.get("dependencyChangesDuringBuild")
                and Path(core_reuse["sourceReceipt"]).resolve() == library_path
                and core_reuse["sourceReceiptSha256"] == receipt["libraryReceiptSha256"]
                and core_reuse["librarySha256"] == core_hash
                and library_receipt["artifactSha256"]["bin/libopenrct2.lib"] == core_hash,
                "Frozen binary does not retain the receipt-qualified software core")
        executable = provenance.parent / "bin/openrct2-cli.exe"
        summary["frozenBuildProvenance"] = {
            "kind": receipt["kind"], "referenceRevision": frozen_inputs["referenceRevision"],
            "sourceArchiveSha256": frozen_inputs["sourceArchiveSha256"], "coreSha256": core_hash,
            "libraryReceipt": evidence.track(library_path),
            "instrumentedCliSha256": frozen_inputs["instrumentedCliSha256"]}
        summary["executableProvenanceLimit"] = (
            "New receipt-qualified frozen-core CLI build with reviewed startup path plumbing; "
            "not the original accepted package executable. Build provenance does not establish pixel parity "
            "or independently observe loaded CSG in the factory-free frozen lane.")
    else:
        require(args.build_receipt is not None, "Current lanes require --build-receipt")
        provenance = args.build_receipt.resolve(strict=True)
        receipt = read(provenance)
        require(receipt.get("status") == "pass" and receipt.get("exitCode") == 0
                and not receipt.get("sourceChangesDuringBuild") and not receipt.get("dependencyChangesDuringBuild")
                and not receipt.get("missingArtifacts"), "Successful source/dependency-stable build required")
        if args.factory == "configured":
            verify_configured_build(receipt, provenance, root, evidence)
        require("bin/screenshot-parity.exe" in receipt["artifactSha256"], "Build does not qualify screenshot driver")
        for name, expected in receipt["artifactSha256"].items():
            evidence.track(child(provenance.parent, name), expected)
        for name, expected in receipt["runtimeDllSha256"].items():
            evidence.track(child(provenance.parent / "bin", name), expected)
        executable = provenance.parent / "bin/screenshot-parity.exe"
    runtime_hashes = evidence.tree(executable.parent, "*.dll", require_nonempty=False)
    expected_runtime = {name.removeprefix("bin/") if args.renderer == "frozen" else name: value
                        for name, value in receipt["runtimeDllSha256"].items()}
    require(runtime_hashes == expected_runtime, "Runtime DLL inventory differs from the qualified build")
    identity = {"renderer": args.renderer, "buildReceipt": evidence.track(provenance), "executable": evidence.track(executable),
                "runtimeDllSha256": runtime_hashes, "park": evidence.track(park),
                "assets": assets, "referenceReceipt": evidence.track(reference_path), "dataSha256": data_hashes,
                "fixture": {"version": 1, "width": WIDTH, "height": HEIGHT, "worldPosition": [336, 112, 144],
                            "caseNames": list(CASE_NAMES)}}
    if args.background_policy == "controlled":
        identity["fixture"].update({"version": 2, "backgroundPolicy": "config-false-explicit-cli-switch",
                                    "transparentScreenshotConfig": False})
    if args.factory == "configured":
        identity["factory"] = "configured"
        data = configured_data_overlay(data, output, data_hashes, evidence)
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()
           if not key.upper().startswith(("OPENRCT2_", "VK_"))}
    env.update({"OPENRCT2_DIAGNOSTIC_OFFSCREEN_SCREENSHOT": "1" if args.renderer == "vulkan" else "0",
                "OPENRCT2_ORACLE_DATA_PATH": str(data), "OPENRCT2_ORACLE_RCT1_PATH": str(args.rct1.resolve()),
                "OPENRCT2_ORACLE_RCT2_PATH": str(args.rct2.resolve())})
    if args.factory == "configured":
        env["OPENRCT2_CLI_CONFIGURED_FACTORY"] = "1"
        env.pop("OPENRCT2_DIAGNOSTIC_OFFSCREEN_SCREENSHOT")
    shader_hashes = {}
    if args.renderer == "vulkan":
        require(args.shader_build_receipt is not None, "Vulkan lane requires --shader-build-receipt")
        shader_receipt_path = args.shader_build_receipt.resolve(strict=True)
        shader_receipt = read(shader_receipt_path)
        require(shader_receipt.get("status") == "pass" and not shader_receipt.get("sourceChangesDuringBuild"),
                "Successful source-stable shader build required")
        identity["shaderBuildReceipt"] = evidence.track(shader_receipt_path)
        for name, digest in shader_receipt["sourceSha256"].items():
            if name.startswith("data/shaders/vulkan/"):
                require(receipt["sourceSha256"].get("source/" + name) == digest, "Driver/shader source receipts differ: " + name)
        shaders = data / "shaders/vulkan" if args.factory == "configured" else output / "shaders"
        shaders.mkdir(parents=True, exist_ok=args.factory == "configured")
        for name, expected in shader_receipt["artifactSha256"].items():
            if name.endswith(".spv"):
                source_shader = child(root, name)
                evidence.track(source_shader, expected)
                require(source_shader.name not in shader_hashes, "Duplicate shader basename")
                target = shaders / source_shader.name
                install_shader(source_shader, target, expected, evidence,
                               data_hashes if args.factory == "configured" else None)
                shader_hashes[target.name] = expected
        require(set(shader_hashes) == SHADER_NAMES, "Shader set does not match the qualified E5/B1 renderer")
        layer_settings = output / "vk_layer_settings.txt"
        layer_settings.write_text("khronos_validation.validate_sync = true\n"
                                  "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
                                  "khronos_validation.log_filename = stdout\n"
                                  "khronos_validation.report_flags = error,warn\n"
                                  "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
        summary["validationSettings"] = evidence.track(layer_settings)
        env.pop("VK_LAYER_ENABLES", None)
        env.update({"OPENRCT2_VULKAN_SHADER_DIRECTORY": str(shaders), "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                    "VK_LAYER_SETTINGS_PATH": str(output), "VK_LOADER_DEBUG": "error,warn,layer"})
        if args.factory == "configured":
            env.pop("OPENRCT2_VULKAN_SHADER_DIRECTORY")
    if args.factory == "configured":
        summary["installedDataSha256"] = pin_installed_data(data, data_hashes, shader_hashes, evidence)
    identity["shaderSha256"] = shader_hashes
    summary.update(identity)
    comparisons = []
    for path in args.compare_run:
        path = path.resolve(strict=True)
        previous, receipt_pin = verify_reference(path, identity, evidence)
        comparisons.append((path, previous))
        summary["comparisonReceipts"].append(receipt_pin)
    if args.fresh_repeat:
        require(any(all(previous.get(k) == v for k, v in identity.items()) for _, previous in comparisons),
                "Fresh repeat requires identical lane, executable, libraries, shaders and fixture inputs")
    config = "[general]\nrct1_path = " + ini(args.rct1.resolve()) + "\ngame_path = " + ini(args.rct2.resolve()) + "\n"
    if args.factory == "configured":
        config += "drawing_engine = " + ini("VULKAN" if args.renderer == "vulkan" else "SOFTWARE_HWD") + "\n"
    if args.background_policy == "controlled":
        config += "transparent_screenshot = false\n"
    for name in CASE_NAMES:
        folder = output / name
        folder.mkdir()
        profile = folder / "profile"
        profile.mkdir()
        (profile / "config.ini").write_text(config, encoding="utf-8")
        (folder / "config-input.ini").write_text(config, encoding="utf-8")
        env.update({"OPENRCT2_ORACLE_USER_PATH": str(profile), "OPENRCT2_CLI_PARITY_ARTIFACTS": str(folder / "capture")})
        png = folder / "screen.png"
        rotation, zoom = name[-3], name[-1]
        command = [str(executable), "screenshot", str(park), str(png), str(WIDTH), str(HEIGHT), "336", "112", "144", zoom, rotation]
        if name.startswith("transparent"):
            command.append("--transparent")
        case = {"name": name, "command": command, "exitCode": None, "failures": [], "comparisons": [],
                "configInput": pin(folder / "config-input.ini")}
        summary["cases"].append(case)
        try:
            with (folder / "capture.log").open("w", encoding="utf-8") as log:
                completed = subprocess.run(command, cwd=executable.parent, env=env, stdout=log,
                                           stderr=subprocess.STDOUT, timeout=180)
            case["exitCode"] = completed.returncode
            require(completed.returncode == 0, "Screenshot command failed")
            buffers = png_buffers(png)
            for filename, buffer in buffers.items():
                (folder / filename).write_bytes(buffer)
            case["images"] = {filename: pin(folder / filename) for filename in ("screen.png", *BUFFER_LENGTHS)}
            if args.renderer != "frozen":
                case["diagnostics"], case["diagnosticArtifacts"] = validate_report(folder, args.renderer, buffers, args)
            if args.renderer == "vulkan":
                lines = (folder / "capture.log").read_text(encoding="utf-8", errors="replace").splitlines()
                activated = any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in line for line in lines)
                diagnostics = [line for line in lines if any(term in line for term in
                              ("VUID-", "Validation Error", "Validation Warning", "SYNC-HAZARD"))]
                case.update({"validationActivated": activated, "validationMessages": diagnostics})
                require(activated and not diagnostics, "Validation activation absent or diagnostics emitted")
            for number, (reference, _) in enumerate(comparisons):
                differing = {}
                for filename, actual in buffers.items():
                    expected = (reference / name / filename).read_bytes()
                    require(len(actual) == len(expected) == BUFFER_LENGTHS[filename], "Reference buffer length changed")
                    stride = {"indexed.bin": 1, "palette.bin": 3, "rgba.bin": 4}[filename]
                    differing[filename] = sum(actual[i:i + stride] != expected[i:i + stride] for i in range(0, len(actual), stride))
                case["comparisons"].append({"reference": str(reference), "differingPixelsOrPaletteEntries": differing})
                if any(differing.values()):
                    case["failures"].append("Reference divergence: " + str(reference))
                    with Image.open(reference / name / "screen.png") as expected_image, Image.open(png) as actual_image:
                        diff = ImageChops.difference(expected_image.convert("RGBA"), actual_image.convert("RGBA"))
                        diff.convert("RGB").save(folder / f"difference-{number}-rgb.png")
                        diff.getchannel("A").save(folder / f"difference-{number}-alpha.png")
        except subprocess.TimeoutExpired:
            case["timedOut"] = True
            case["failures"].append("Screenshot command timed out after 180 seconds; partial artifacts retained")
        except Exception as error:
            case["failures"].append(type(error).__name__ + ": " + str(error))
        finally:
            if (folder / "capture.log").is_file():
                case["log"] = pin(folder / "capture.log")
            summary["failures"].extend(name + ": " + error for error in case["failures"])
            write_summary(output, summary, complete=False)
    require(len(summary["cases"]) == len(CASE_NAMES), "Missing fixture executions")
    if args.background_policy == "controlled":
        summary["backgroundPolicyDistinctions"] = []
        for zoom in (0, 1):
            for rotation in range(4):
                camera = f"r{rotation}z{zoom}"
                ordinary = (output / ("ordinary-" + camera) / "indexed.bin").read_bytes()
                transparent = (output / ("transparent-" + camera) / "indexed.bin").read_bytes()
                changes = sum(a != b for a, b in zip(ordinary, transparent))
                blank_to_zero = sum(a == 10 and b == 0 for a, b in zip(ordinary, transparent))
                summary["backgroundPolicyDistinctions"].append(
                    {"camera": camera, "differentIndices": changes, "opaqueBlankToTransparent": blank_to_zero})
                require(len(ordinary) == WIDTH * HEIGHT and len(transparent) == WIDTH * HEIGHT
                        and changes > 0 and blank_to_zero > 0,
                        "Controlled background fixture did not distinguish blank-tile policy: " + camera)


def write_summary(output, summary, complete):
    summary["status"] = "fail" if summary["failures"] else ("pass" if complete else "incomplete")
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--factory", choices=("diagnostic", "configured"), default="diagnostic",
                        help="configured delegates selection to the production factory using isolated loaded config")
    parser.add_argument("--background-policy", choices=("legacy-default", "controlled"), default="legacy-default",
                        help="controlled explicitly disables config transparency so --transparent is meaningful; uses distinct fixture identity")
    parser.add_argument("--renderer", choices=("frozen", "software", "vulkan"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--oracle-source", type=Path, help="Rejected legacy option: use a qualified frozen --build-receipt")
    parser.add_argument("--build-receipt", type=Path)
    parser.add_argument("--shader-build-receipt", type=Path)
    parser.add_argument("--rct1", type=Path, required=True)
    parser.add_argument("--rct2", type=Path, required=True)
    parser.add_argument("--compare-run", type=Path, action="append", default=[])
    parser.add_argument("--fresh-repeat", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    require(not output.exists() and root in output.parents, "Output must be a new workspace directory")
    output.mkdir(parents=True)
    evidence = Evidence()
    summary = {"schema": 2, "status": "incomplete", "renderer": args.renderer, "failures": [], "cases": [],
               "comparisonReceipts": [], "runnerSha256": sha(Path(__file__)), "freshRepeat": args.fresh_repeat,
               "scope": "Real bounded CLI indexed PNG, palette, PNG alpha and owned Vulkan shader output. No main-window presentation or performance qualification.",
               "loadedAssetEvidenceLimit": "Vulkan driver checks loaded CSG/G1 at factory creation. Frozen and factory-free software lack a post-load observation hook; supplied asset hashes/configuration do not independently prove their loaded CSG state."}
    completed = False
    write_summary(output, summary, complete=False)
    try:
        require(args.factory != "configured" or args.renderer != "frozen", "Frozen software cannot use configured factory")
        require(not args.fresh_repeat or args.compare_run, "Fresh repeat requires an identical prior renderer run")
        evidence.track(Path(__file__))
        execute(args, root, output, summary, evidence)
        completed = True
    except Exception as error:
        summary["failures"].append(type(error).__name__ + ": " + str(error))
        (output / "runner-error.log").write_text(traceback.format_exc(), encoding="utf-8")
    finally:
        if not completed and not summary["failures"]:
            summary["failures"].append("Runner interrupted before completing all fixture executions")
        try:
            summary["inputAuditFailures"] = evidence.audit()
            summary["failures"].extend(summary["inputAuditFailures"])
        except Exception as error:
            summary["failures"].append("Input audit failed: " + str(error))
        summary["inputSha256"] = evidence.files
        write_summary(output, summary, complete=True)
    print(json.dumps({"status": summary["status"], "cases": len(summary["cases"]),
                      "failures": summary["failures"], "output": str(output)}))
    raise SystemExit(bool(summary["failures"]))


if __name__ == "__main__":
    main()
