"""Compare four explicit static upstream/software and current/Vulkan screenshots.

Uses the frozen package only for immutable assets and runtime dependencies. The
reference renderer comes from the separately built pinned upstream CLI. Executes
eight isolated processes serially; exact differences remain failures for review.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import traceback

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
HELPER = Path(__file__).with_name("run-screenshot-parity.py")
SPEC = importlib.util.spec_from_file_location("screenshot_evidence", HELPER)
SHOTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SHOTS)
require, read, sha, child = SHOTS.require, SHOTS.read, SHOTS.sha, SHOTS.child
UPSTREAM_REVISION = "b80a4a84e92be8e07904b38d1032d0bb88280bb4"


def qualify_build(path, upstream, evidence):
    path = path.resolve(strict=True)
    evidence.track(path)
    receipt = read(path)
    require(receipt.get("status") == "pass", "Build receipt did not pass: " + str(path))
    for key in ("sourceChangesDuringBuild", "dependencyChangesDuringBuild", "fixedBuildInputChangesDuringBuild",
                "runtimeDllCopyMismatch", "missingArtifacts"):
        require(key in receipt and not receipt[key], "Unqualified build input: " + key)
    evidence.track(path.parent / "build.log", receipt["buildLogSha256"])
    if upstream:
        require(receipt.get("kind") == "upstream-screenshot-oracle-build"
                and receipt.get("referenceRevision") == UPSTREAM_REVISION
                and receipt.get("compilerConcurrency") == "serial"
                and receipt.get("modifiedOriginalSources") == []
                and receipt.get("builderChangesDuringBuild") == [], "Wrong upstream build provenance")
        require([s.get("name") for s in receipt["stageResults"]] == ["core", "cli"]
                and all(s.get("exitCode") == 0 for s in receipt["stageResults"]), "Incomplete upstream build")
        source = Path(receipt["sourceRoot"]).resolve(strict=True)
        for name, digest in receipt["originalSourceSha256"].items():
            evidence.track(child(source, name), digest)
        evidence.track(path.parent / "upstream-source.zip", receipt["sourceArchiveSha256"])
        evidence.track(Path(receipt["wrapper"]["path"]), receipt["wrapper"]["sha256"])
        for name, digest in receipt["generatedProjectSha256"].items():
            evidence.track(child(path.parent, name), digest)
        executable_name = "bin/upstream-screenshot-cli.exe"
    else:
        SHOTS.verify_configured_build(receipt, path, ROOT, evidence)
        header = ROOT / "src/openrct2/drawing/IDrawingEngine.h"
        require(re.search(r"(?m)^\s*#\s*define\s+OPENRCT2_VULKAN_ONLY\s+1\s*$",
                          header.read_text(encoding="utf-8")), "Current build must be Vulkan-only")
        executable_name = "bin/screenshot-parity.exe"
    require(executable_name in receipt["artifactSha256"], "Missing qualified screenshot executable")
    for name, digest in receipt["artifactSha256"].items():
        evidence.track(child(path.parent, name), digest)
    for name, digest in receipt["runtimeDllSha256"].items():
        evidence.track(child(path.parent / "bin", name.removeprefix("bin/")), digest)
    return receipt, child(path.parent, executable_name)


def install_assets(output, shader_path, current, evidence):
    reference_path = ROOT / "docs/vulkan-software-reference.json"
    evidence.track(reference_path)
    reference = read(reference_path)
    frozen = child(ROOT, reference["localReference"])
    manifest_path = frozen / "manifest.json"
    evidence.track(manifest_path, reference["manifest"]["sha256"])
    manifest = read(manifest_path)
    expected = {name.removeprefix("package/"): item["sha256"] for name, item in manifest["files"].items()
                if name.startswith("package/")}
    require(evidence.tree(frozen / "package") == expected, "Immutable package differs from its accepted manifest")
    # No frozen source archive, library or executable is used as the rendering oracle.
    data = output / "data"
    shutil.copytree(frozen / "package/data", data)
    data_expected = {name.removeprefix("data/"): digest for name, digest in expected.items() if name.startswith("data/")}
    require({p.relative_to(data).as_posix(): sha(p) for p in data.rglob("*") if p.is_file()} == data_expected,
            "Copied assets differ from the immutable package")
    shader_path = shader_path.resolve(strict=True)
    evidence.track(shader_path)
    shaders = read(shader_path)
    require(shaders.get("status") == "pass" and not shaders.get("sourceChangesDuringBuild")
            and not shaders.get("missingArtifacts"), "Unqualified shader build")
    shader_sources = {name: digest for name, digest in shaders["sourceSha256"].items()
                      if name.startswith("data/shaders/vulkan/")}
    require(shader_sources, "Shader build has no source provenance")
    for name, digest in shader_sources.items():
        require(current["sourceSha256"].get("source/" + name) == digest, "Current/shader source disagreement: " + name)
        evidence.track(child(ROOT, name), digest)
    installed = {}
    for name, digest in shaders["artifactSha256"].items():
        if not name.endswith(".spv"):
            continue
        source = child(ROOT, name)
        evidence.track(source, digest)
        require(source.name not in installed, "Duplicate shader basename")
        destination = data / "shaders/vulkan" / source.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        installed[source.name] = digest
        data_expected["shaders/vulkan/" + source.name] = digest
    require(installed, "No receipt-qualified shaders")
    require(evidence.tree(data) == data_expected, "Installed data differs from verified art and qualified shaders")
    return frozen / "package", data, {"receipt": SHOTS.pin(reference_path), "manifest": SHOTS.pin(manifest_path),
                                       "role": "immutable data and runtime dependencies only", "shaderSha256": installed}


def camera_origin(position, rotation, extent, zoom):
    x, y, z = position
    x, y = ((x, y), (y, -x), (-x, -y), (-y, x))[rotation]
    return [y - x - (extent[0] << zoom) // 2, ((x + y) >> 1) - z - (extent[1] << zoom) // 2]


def validate_current(folder, buffers, args, rotation):
    report = read(folder / "capture/report.json")
    fixture = {"screenshot-cli": "screenshot-cli", "capture-image": "screenshot-capture-image",
               "park-preview": "screenshot-park-preview"}[args.current_entrypoint]
    require(report.get("schema") == 1 and report.get("fixture") == fixture
            and report.get("fixtureVersion") == 1 and report.get("mode") == "vulkan"
            and report.get("exitCode") == 0 and report.get("error") == ""
            and report.get("serviceCreations") == 1 and report.get("deviceCreations") == 1,
            "Current Vulkan service/capture identity failed")
    factory = report.get("productionFactory", {})
    checks = factory.get("enabledChecks")
    require(factory.get("kind") == "configured" and factory.get("renderer") == "vulkan"
            and factory.get("ownerCreated") is True
            and factory.get("deviceObservation") == "persistent-owner-created-state"
            and isinstance(checks, list) and checks
            and all(c == {"renderer": "vulkan", "enabled": True} for c in checks),
            "Current capture did not use the Vulkan-only production factory")
    assets = report["assetState"]
    require(assets.get("rct1Required") is True and assets.get("rct1CsgLoaded") is True
            and assets.get("g1RecordCount") == 29294 and 0 < assets.get("g1PayloadCount", 0) <= 29294
            and Path(assets["configuredRct1Path"]).resolve() == args.rct1
            and Path(assets["configuredRct2Path"]).resolve() == args.rct2, "Required loaded original art was not proven")
    extent = [args.width, args.height]
    if args.current_entrypoint != "screenshot-cli":
        meta = report["captureImage" if args.current_entrypoint == "capture-image" else "parkPreview"]
        require(meta["extent"] == extent and meta["zoom"] == args.zoom and meta["transparent"] is args.transparent
                and type(meta["tickBefore"]) is int and meta["tickBefore"] == meta["tickAfter"]
                and Path(meta["copiedOutput"]).resolve() == (folder / "screen.png").resolve(),
                "Auxiliary capture extent, tick or output differs")
        require(len(meta["worldPosition"]) == 3 and all(type(v) is int for v in meta["worldPosition"])
                and type(meta["rotation"]) is int and 0 <= meta["rotation"] <= 3
                and meta["viewPosition"] == camera_origin(meta["worldPosition"], meta["rotation"], extent, args.zoom),
                "Auxiliary camera projection differs from explicit upstream camera")
        if args.current_entrypoint == "capture-image":
            require(meta["api"] == "CaptureImage" and meta["giant"] is False
                    and meta["worldPosition"] == [args.x, args.y, args.z] and meta["rotation"] == rotation
                    and meta["sourceTick"] == meta["tickBefore"] and meta["flags"] == ((1 << 19) if args.transparent else 0),
                    "CaptureImage must use the requested camera")
            produced = (folder / "profile/screenshot/capture-image.png").resolve()
            require(Path(meta["productionOutput"]).resolve() == produced
                    and produced.read_bytes() == (folder / "screen.png").read_bytes(),
                    "Compared PNG is not the production CaptureImage output")
        else:
            require(meta["api"] == "generatePreviewFromGameState" and meta["cameraSource"] == "first-park-entrance"
                    and meta["flags"] == 0 and meta["sourceTick"] == meta["tickBefore"]
                    and meta["cliEquivalentWorldPosition"] == meta["worldPosition"]
                    and meta["cliEquivalentViewPosition"] == meta["viewPosition"], "Park preview camera contract differs")
    if args.current_entrypoint == "capture-image":
        expected = [(x, y, min(2048, args.width-x), min(2048, args.height-y))
                    for y in range(0, args.height, 2048) for x in range(0, args.width, 2048)]
        require(report["capture"] is None and len(report["tiles"]) == len(expected)
                and len(report["tileBegins"]) == len(expected)
                and report["sessions"] == {"begun": len(expected), "retired": len(expected), "live": 0, "peak": 1},
                "CaptureImage tile/session lifecycle differs")
        submissions = set()
        for ordinal, ((x, y, width, height), tile, begin) in enumerate(zip(expected, report["tiles"], report["tileBegins"])):
            name = "screenshot-cli-giant-tile-" + str(ordinal)
            validate_owned(tile, buffers["palette.bin"], [width, height], name, False)
            require(tile["ordinal"] == ordinal and tile["completionIdentityMatches"] is True
                    and tile["tickAtReadback"] == meta["tickBefore"]
                    and begin == {"name": name, "extent": [width, height], "tick": meta["tickBefore"],
                                  "liveSessions": 1, "completedBeforeBegin": ordinal, "retiredBeforeBegin": ordinal},
                    "CaptureImage tile ordering or source tick differs")
            require(tile["submissionId"] not in submissions, "CaptureImage reused a submission identity")
            submissions.add(tile["submissionId"])
            raw = (folder / "capture" / (name + ".indexed")).read_bytes()
            require(len(raw) == width * height, "Tile output extent differs")
            for row in range(height):
                offset = (y + row) * args.width + x
                require(raw[row*width:(row+1)*width] == buffers["indexed.bin"][offset:offset+width],
                        "CaptureImage PNG differs from owned tile output")
        require({p.name for p in (folder / "capture").glob("*.indexed")}
                == {"screenshot-cli-giant-tile-" + str(i) + ".indexed" for i in range(len(expected))},
                "Unexpected CaptureImage tile artifact")
    else:
        rgba = args.current_entrypoint == "screenshot-cli"
        validate_owned(report["capture"], buffers["palette.bin"], extent,
                       "screenshot-cli" if rgba else "park-preview", rgba)
        require((folder / "capture/screen.indexed").read_bytes() == buffers["indexed.bin"], "Owned indices differ from PNG")
        require((folder / "capture/screen.rgba").read_bytes() == (buffers["rgba.bin"] if rgba else b""),
                "Owned RGBA differs from requested output")
    return report


def validate_owned(capture, png_palette, extent, name, rgba):
    require(capture.get("name") == name and capture.get("logicalExtent") == extent
            and capture.get("outputExtent") == extent
            and all(type(capture.get(k)) is int and capture[k] > 0 for k in ("submissionId", "targetId", "targetGeneration"))
            and capture.get("alphaPolicy") == "transparentIndexZero" and capture.get("alphaPolicyValue") == 2
            and capture.get("scaleQuality") == 0 and capture.get("indexedOutput") is True
            and capture.get("rgbaOutput") is rgba and capture.get("lightingEnabled") is False
            and capture.get("clearIndex") == 0 and capture.get("indexedBytes") == extent[0] * extent[1]
            and capture.get("rgbaBytes") == (extent[0] * extent[1] * 4 if rgba else 0), "Owned output contract differs")
    palette = capture["requestPalette"]
    require(capture.get("resultPalette") == palette and len(palette) == 256
            and bytes(v for c in palette for v in c[:3]) == png_palette, "Owned palette differs from PNG")


def execute(args, output, summary, evidence):
    evidence.track(Path(__file__))
    evidence.track(HELPER)
    upstream, upstream_exe = qualify_build(args.upstream_build_receipt, True, evidence)
    current, current_exe = qualify_build(args.current_build_receipt, False, evidence)
    package, data, summary["assetPackage"] = install_assets(output, args.shader_build_receipt, current, evidence)
    summary["referenceRevision"] = upstream["referenceRevision"]
    summary["park"] = evidence.track(args.park)
    summary["originalArt"] = {"rct1": evidence.tree(args.rct1), "rct2": evidence.tree(args.rct2)}
    for root, names in ((args.rct1, ("csg1.dat", "csg1i.dat")), (args.rct2, ("g1.dat",))):
        for name in names:
            evidence.track(root / "Data" / name)
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()
           if not key.upper().startswith(("OPENRCT2_", "VK_"))}
    env.update({"OPENRCT2_ORACLE_DATA_PATH": str(data), "OPENRCT2_ORACLE_RCT1_PATH": str(args.rct1),
                "OPENRCT2_ORACLE_RCT2_PATH": str(args.rct2)})
    layer_settings = output / "vk_layer_settings.txt"
    layer_settings.write_text("khronos_validation.validate_sync = true\n"
                              "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
                              "khronos_validation.log_filename = stdout\n"
                              "khronos_validation.report_flags = error,warn\n"
                              "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
    summary["validationSettings"] = evidence.track(layer_settings)
    config = ("[general]\nrct1_path = " + SHOTS.ini(args.rct1) + "\ngame_path = " + SHOTS.ini(args.rct2)
              + "\ntransparent_screenshot = false\nlandscape_smoothing = true\nday_night_cycle = false\n"
                "enable_light_fx = false\nenable_light_fx_for_vehicles = false\n")
    decoded = {}
    preview_camera = None
    lanes = [("upstream-software", upstream, upstream_exe), ("current-vulkan", current, current_exe)]
    if args.current_entrypoint == "park-preview":
        lanes.reverse()  # Discover the production entrance camera before constructing the independent CLI command.
    for lane, receipt, executable in lanes:
        runtime = output / lane / "runtime"
        runtime.mkdir(parents=True)
        shutil.copyfile(executable, runtime / executable.name)
        evidence.track(runtime / executable.name, sha(executable))
        dlls = {name.removeprefix("bin/"): digest for name, digest in receipt["runtimeDllSha256"].items()}
        for name, digest in dlls.items():
            evidence.track(package / name, digest)
            shutil.copyfile(package / name, runtime / name)
        require(evidence.tree(runtime, "*.dll", require_nonempty=bool(dlls)) == dlls,
                "Runtime dependencies differ from build receipt")
        for rotation in args.rotations:
            folder = output / lane / ("r" + str(rotation))
            profile = folder / "profile"
            profile.mkdir(parents=True)
            (profile / "config.ini").write_text(config, encoding="utf-8")
            (folder / "config-input.ini").write_text(config, encoding="utf-8")
            evidence.track(folder / "config-input.ini")
            process_env = dict(env, OPENRCT2_ORACLE_USER_PATH=str(profile))
            if lane == "current-vulkan":
                process_env.update({"OPENRCT2_CLI_CONFIGURED_FACTORY": "1",
                                    "OPENRCT2_CLI_PARITY_ARTIFACTS": str(folder / "capture"),
                                    "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                                    "VK_LAYER_SETTINGS_PATH": str(output), "VK_LOADER_DEBUG": "error,warn,layer"})
                if args.current_entrypoint != "screenshot-cli":
                    process_env["OPENRCT2_CLI_CAPTURE_IMAGE" if args.current_entrypoint == "capture-image"
                                else "OPENRCT2_CLI_PARK_PREVIEW"] = "1"
            position, camera_rotation = [args.x, args.y, args.z], rotation
            if lane == "upstream-software" and args.current_entrypoint == "park-preview":
                require(preview_camera is not None, "Production park preview camera was not qualified")
                position, camera_rotation = preview_camera["worldPosition"], preview_camera["rotation"]
            command = [str(runtime / executable.name), "screenshot", str(args.park), str(folder / "screen.png"),
                       str(args.width), str(args.height), *(str(v) for v in position), str(args.zoom), str(camera_rotation)]
            if args.transparent:
                command.append("--transparent")
            case = {"lane": lane, "rotation": rotation, "command": command, "environment": process_env,
                    "configInputSha256": sha(folder / "config-input.ini"), "failures": []}
            # Record only task-owned environment controls, never unrelated host secrets.
            case["environment"] = {k: v for k, v in process_env.items() if k.startswith(("OPENRCT2_", "VK_"))}
            summary["cases"].append(case)
            try:
                with (folder / "capture.log").open("w", encoding="utf-8") as log:
                    result = subprocess.run(command, cwd=runtime, env=process_env, stdout=log, stderr=subprocess.STDOUT,
                                            timeout=args.timeout,
                                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                case["exitCode"] = result.returncode
                require(result.returncode == 0, "Screenshot process failed")
                buffers = SHOTS.png_buffers(folder / "screen.png", (args.width, args.height))
                for name, value in buffers.items():
                    (folder / name).write_bytes(value)
                decoded[lane, rotation] = buffers
                if lane == "current-vulkan":
                    case["diagnostics"] = validate_current(folder, buffers, args, rotation)
                    if args.current_entrypoint == "park-preview":
                        preview_camera = case["diagnostics"]["parkPreview"]
                        summary["fixture"]["productionCamera"] = preview_camera
            except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
                case["failures"].append(type(error).__name__ + ": " + str(error))
            if lane == "current-vulkan":
                log_path = folder / "capture.log"
                lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines() if log_path.is_file() else []
                activated = any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in line for line in lines)
                messages = [line for line in lines if any(term in line.lower() for term in
                            ("vuid-", "sync-hazard", "validation error", "validation warning", "validation performance warning"))]
                case.update({"validationActivated": activated, "validationMessages": messages})
                if not activated or messages:
                    case["failures"].append("Validation activation absent or diagnostics emitted")
            case["artifactSha256"] = {p.relative_to(folder).as_posix(): sha(p) for p in folder.rglob("*") if p.is_file()}
            summary["failures"].extend(lane + "/r" + str(rotation) + ": " + f for f in case["failures"])
    for rotation in args.rotations:
        require(all((lane, rotation) in decoded for lane in ("upstream-software", "current-vulkan")),
                "Cannot compare missing decoded rotation " + str(rotation))
        a, b = decoded["upstream-software", rotation], decoded["current-vulkan", rotation]
        counts = {name: sum(a[name][i:i + stride] != b[name][i:i + stride] for i in range(0, len(a[name]), stride))
                  for name, stride in (("indexed.bin", 1), ("palette.bin", 3), ("rgba.bin", 4))}
        folder = output / "comparisons" / ("r" + str(rotation))
        folder.mkdir(parents=True)
        for lane, label in (("upstream-software", "reference"), ("current-vulkan", "candidate")):
            shutil.copyfile(output / lane / ("r" + str(rotation)) / "screen.png", folder / (label + ".png"))
        rgba = [Image.frombytes("RGBA", (args.width, args.height), v["rgba.bin"]) for v in (a, b)]
        difference = ImageChops.difference(*rgba)
        channels = difference.split()
        visible = Image.merge("RGB", tuple(ImageChops.lighter(c, channels[3]) for c in channels[:3]))
        visible.save(folder / "diff.png")
        visible.point(lambda value: min(255, value * 8)).save(folder / "diff-amplified.png")
        indexed_mask = bytes(255 if x != y else 0 for x, y in zip(a["indexed.bin"], b["indexed.bin"]))
        Image.frombytes("L", (args.width, args.height), indexed_mask).save(folder / "indexed-diff-mask.png")
        summary["comparisons"].append({"rotation": rotation, "differingPixelsOrPaletteEntries": counts,
                                       "rgbaBoundsExclusive": visible.getbbox(), "manualVisualReview": "pending"})
        if any(counts.values()):
            summary["failures"].append("Exact pixel/palette divergence at rotation " + str(rotation))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("output", "upstream-build-receipt", "current-build-receipt", "shader-build-receipt", "park", "rct1", "rct2"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("x", "y", "z"):
        parser.add_argument("--" + name, type=int, required=True)
    parser.add_argument("--width", type=int, default=SHOTS.WIDTH)
    parser.add_argument("--height", type=int, default=SHOTS.HEIGHT)
    parser.add_argument("--zoom", type=int, choices=range(4), default=0)
    parser.add_argument("--rotations", type=int, nargs="+", choices=range(4), default=[0, 1, 2, 3])
    parser.add_argument("--transparent", action="store_true")
    parser.add_argument("--current-entrypoint", choices=("screenshot-cli", "capture-image", "park-preview"),
                        default="screenshot-cli")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    require(args.timeout > 0, "Timeout must be positive")
    require(0 < args.width <= 4096 and 0 < args.height <= 4096, "Bounded screenshot extent must be at most 4096 per axis")
    require(len(set(args.rotations)) == len(args.rotations), "Duplicate rotations")
    if args.current_entrypoint == "park-preview":
        require((args.width, args.height, args.zoom) == (250, 200, 1) and not args.transparent
                and len(args.rotations) == 1, "Park preview compares one discovered entrance camera at 250x200 zoom1")
    output = args.output.resolve()
    require(ROOT in output.parents and not output.exists(), "Use a new output directory inside the workspace")
    for name in ("park", "rct1", "rct2"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    output.mkdir(parents=True)
    evidence = SHOTS.Evidence()
    summary = {"schema": 1, "kind": "upstream-current-static-screenshot-comparison", "status": "incomplete",
               "failures": [], "cases": [], "comparisons": [], "manualVisualReview": "pending",
               "fixture": {"extent": [args.width, args.height], "worldPosition": [args.x, args.y, args.z],
                           "zoom": args.zoom, "caseLabels": args.rotations, "transparentScreenshot": args.transparent,
                           "currentEntrypoint": args.current_entrypoint,
                           "cameraPolicy": "production-entrance-discovery" if args.current_entrypoint == "park-preview" else "explicit"},
               "scope": "Serial static image processes; no presentation, motion, TPS or broad parity acceptance.",
               "loadedArtEvidenceLimit": "Upstream has path-only plumbing; loaded CSG/G1 is observed only in current diagnostics.",
               "validationScope": "Current Vulkan requires Khronos synchronization validation activation and clean logs in every process; upstream software receives no Vulkan environment controls."}
    try:
        execute(args, output, summary, evidence)
    except Exception as error:
        summary["failures"].append(type(error).__name__ + ": " + str(error))
        (output / "runner-error.log").write_text(traceback.format_exc(), encoding="utf-8")
    finally:
        summary["failures"].extend(evidence.audit())
        summary["inputSha256"] = evidence.files
        summary["status"] = "fail" if summary["failures"] else "exact-match-pending-review"
        summary["outputSha256"] = {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": summary["status"], "failures": summary["failures"], "output": str(output)}))
    raise SystemExit(bool(summary["failures"]))


if __name__ == "__main__":
    main()
