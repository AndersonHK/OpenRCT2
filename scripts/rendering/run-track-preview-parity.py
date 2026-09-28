"""Compare actual track-preview output from separately compiled upstream and Vulkan processes."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import traceback
import zipfile

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
HELPER = Path(__file__).with_name("run-upstream-screenshot-comparison.py")
spec = importlib.util.spec_from_file_location("track_reference_helpers", HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
base = helper.SHOTS
sha, read, require = base.sha, base.read, base.require
CASES = ("flat1", "flat35", "flat55")
EXTENT = (370, 217)


def qualify_upstream(receipt, path, evidence):
    require(receipt.get("kind") == "upstream-track-preview-build" and receipt.get("driver") == "track-preview"
            and receipt.get("referenceRevision") == helper.UPSTREAM_REVISION
            and receipt.get("compilerConcurrency") == "serial" and receipt.get("toolsetVersion") == "14.44.35207"
            and receipt.get("reuseReceiptUnchanged") is True
            and receipt.get("fixedBuildInputChangesDuringBuild") == [], "Wrong upstream driver provenance")
    require(receipt.get("stageResults") == [{"name": "core", "exitCode": 0, "reused": True},
                                           {"name": "driver", "exitCode": 0}], "Incomplete upstream driver build")
    reused_path = Path(receipt["reuseReceipt"]["path"]).resolve(strict=True)
    evidence.track(reused_path, receipt["reuseReceipt"]["sha256"])
    reused = read(reused_path)
    require(reused.get("status") == "pass" and reused.get("kind") == "upstream-screenshot-oracle-build"
            and reused.get("referenceRevision") == helper.UPSTREAM_REVISION
            and reused.get("compilerConcurrency") == "serial" and reused.get("toolsetVersion") == "14.44.35207",
            "Unqualified original upstream core")
    for key in ("modifiedOriginalSources", "builderChangesDuringBuild", "sourceChangesDuringBuild",
                "dependencyChangesDuringBuild", "fixedBuildInputChangesDuringBuild", "runtimeDllCopyMismatch",
                "missingArtifacts"):
        require(key in reused and not reused[key], "Unstable original upstream build: " + key)
    require([stage.get("name") for stage in reused["stageResults"]] == ["core", "cli"]
            and all(stage.get("exitCode") == 0 for stage in reused["stageResults"]), "Incomplete original upstream build")
    source = (reused_path.parent / "source").resolve(strict=True)
    require(Path(reused["sourceRoot"]).resolve(strict=True) == source
            and Path(receipt["sourceRoot"]).resolve(strict=True) == source, "Upstream source location differs")
    originals = reused["originalSourceSha256"]
    require(originals and receipt["originalSourceSha256"] == originals, "Original source manifests differ")
    archive = reused_path.parent / "upstream-source.zip"
    require(Path(receipt["sourceArchive"]["path"]).resolve(strict=True) == archive
            and receipt["sourceArchive"]["sha256"] == reused["sourceArchiveSha256"], "Source archive provenance differs")
    evidence.track(archive, reused["sourceArchiveSha256"])
    archived = {}
    with zipfile.ZipFile(archive) as bundle:
        for entry in bundle.infolist():
            if entry.is_dir():
                continue
            require(entry.filename not in archived, "Duplicate upstream archive member")
            archived[entry.filename] = hashlib.sha256(bundle.read(entry)).hexdigest()
    require(archived == originals, "Upstream archive differs from original source manifest")
    # lib/x64 is the separately pinned local dependency junction added after git archive.
    live = evidence.tree(source)
    require({name: digest for name, digest in live.items() if not name.startswith("lib/")} == originals,
            "Upstream source tree differs from its pristine archive")
    dependencies = {}
    for directory in ("include", "lib", "bin"):
        folder = source / "lib/x64" / directory
        if folder.exists():
            dependencies.update({directory + "/" + name: digest
                                 for name, digest in evidence.tree(folder, require_nonempty=False).items()})
    require(dependencies and dependencies == reused["dependencySha256"] == receipt["dependencySha256"],
            "Upstream dependencies differ from the compiled core")
    evidence.track(reused_path.parent / "build.log", reused["buildLogSha256"])
    wrapper = reused_path.parent / "UpstreamCli.cpp"
    require(Path(reused["wrapper"]["path"]).resolve(strict=True) == wrapper, "Original CLI wrapper path differs")
    evidence.track(wrapper, reused["wrapper"]["sha256"])
    for owner, folder in ((reused, reused_path.parent), (receipt, path.parent)):
        require(owner["generatedProjectSha256"] and owner["artifactSha256"], "Empty upstream build inventory")
        for key in ("generatedProjectSha256", "artifactSha256", "runtimeDllSha256"):
            for name, digest in owner[key].items():
                evidence.track(base.child(folder, name), digest)
    core = reused_path.parent / "bin/libopenrct2.lib"
    provenance = receipt["coreProvenance"]
    require(Path(provenance["libraryPath"]).resolve(strict=True) == core
            and provenance["librarySha256"] == reused["artifactSha256"]["bin/libopenrct2.lib"]
            and provenance["buildLogSha256"] == reused["buildLogSha256"]
            and provenance["command"] == reused["commands"][0]
            and receipt["artifactSha256"]["bin/libopenrct2.lib"] == provenance["librarySha256"],
            "Diagnostic did not reuse the receipt-qualified core")
    require(receipt["runtimeDllSha256"] == reused["runtimeDllSha256"], "Upstream runtime DLL inventory differs")
    require(receipt["toolchain"]["properties"] == reused["toolchain"]["properties"]
            and receipt["toolchain"]["sha256"] == reused["toolchain"]["sha256"], "Upstream toolchain differs")
    for key, manifest in (("toolchain", receipt["toolchain"]["sha256"]),
                          ("builder", receipt["builderSha256"]), ("fixed inputs", receipt["fixedBuildInputSha256"])):
        require(manifest, "Empty upstream " + key + " inventory")
        for name, digest in manifest.items():
            evidence.track(Path(name), digest)
    diagnostic = receipt["diagnosticDriver"]
    driver_hash = diagnostic["sha256"]
    driver = ROOT / "test/track-preview-parity/TrackPreviewMain.cpp"
    copy = path.parent / "TrackPreviewMain.cpp"
    require(Path(diagnostic["path"]).resolve(strict=True) == driver
            and Path(diagnostic["copy"]).resolve(strict=True) == copy
            and receipt["sourceSha256"] == {driver.relative_to(ROOT).as_posix(): driver_hash},
            "Shared diagnostic source provenance differs")
    evidence.track(copy, driver_hash)
    return driver_hash


def qualify(path, current, evidence):
    path = path.resolve(strict=True)
    evidence.track(path)
    receipt = read(path)
    require(receipt.get("status") == "pass" and (not current or receipt.get("exitCode") == 0),
            "Passing driver build required")
    require(not any(receipt.get(key) for key in (
        "sourceChangesDuringBuild", "dependencyChangesDuringBuild", "fixedBuildInputChangesDuringBuild",
        "builderChangesDuringBuild", "missingArtifacts", "runtimeDllCopyMismatch")), "Unstable driver build inputs")
    evidence.track(path.parent / "build.log", receipt["buildLogSha256"])
    if current:
        require(receipt.get("driver") == "track-preview", "Wrong current diagnostic entrypoint")
        base.verify_configured_build(receipt, path, ROOT, evidence)
        driver_hash = receipt["sourceSha256"]["harness/test/track-preview-parity/TrackPreviewMain.cpp"]
    else:
        driver_hash = qualify_upstream(receipt, path, evidence)
    evidence.track(ROOT / "test/track-preview-parity/TrackPreviewMain.cpp", driver_hash)
    require("bin/track-preview-parity.exe" in receipt["artifactSha256"], "Missing qualified diagnostic executable")
    for name, digest in receipt["artifactSha256"].items():
        evidence.track(base.child(path.parent, name), digest)
    return receipt, path.parent / "bin/track-preview-parity.exe", driver_hash


def execute(args, output, summary, evidence):
    for path in (Path(__file__), HELPER, helper.HELPER):
        evidence.track(path)
    current, current_exe, driver = qualify(args.current_build_receipt, True, evidence)
    upstream, upstream_exe, reference_driver = qualify(args.upstream_build_receipt, False, evidence)
    require(driver == reference_driver, "Both processes must compile the identical diagnostic source")
    summary["driverSha256"] = driver
    summary["referenceRevision"] = upstream["referenceRevision"]
    package, data, summary["assetPackage"] = helper.install_assets(output, args.shader_build_receipt, current, evidence)
    evidence.track(args.park)
    summary["art"] = {"rct1": evidence.tree(args.rct1), "rct2": evidence.tree(args.rct2)}
    settings = output / "vk_layer_settings.txt"
    settings.write_text("khronos_validation.validate_sync = true\n"
                        "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
                        "khronos_validation.log_filename = stdout\n"
                        "khronos_validation.report_flags = error,warn\n"
                        "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
    evidence.track(settings)
    env = {k.upper() if os.name == "nt" else k: v for k, v in os.environ.items()
           if not k.upper().startswith(("OPENRCT2_", "VK_"))}
    env.update({"OPENRCT2_ORACLE_DATA_PATH": str(data), "OPENRCT2_ORACLE_RCT1_PATH": str(args.rct1),
                "OPENRCT2_ORACLE_RCT2_PATH": str(args.rct2)})
    decoded = {}
    for lane, receipt, executable in (("upstream-software", upstream, upstream_exe),
                                      ("current-vulkan", current, current_exe)):
        folder = output / lane
        runtime, profile, captures = folder / "runtime", folder / "profile", folder / "captures"
        runtime.mkdir(parents=True)
        profile.mkdir()
        shutil.copyfile(executable, runtime / executable.name)
        evidence.track(runtime / executable.name, sha(executable))
        dlls = {name.removeprefix("bin/"): digest for name, digest in receipt["runtimeDllSha256"].items()}
        for name, digest in dlls.items():
            source = executable.parent / name
            evidence.track(source, digest)
            shutil.copyfile(source, runtime / name)
        require(evidence.tree(runtime, "*.dll", require_nonempty=bool(dlls)) == dlls, "Runtime DLL inventory differs")
        config = ("[general]\nrct1_path = " + base.ini(args.rct1) + "\ngame_path = " + base.ini(args.rct2)
                  + "\nlandscape_smoothing = true\nday_night_cycle = false\nenable_light_fx = false\n")
        (profile / "config.ini").write_text(config, encoding="utf-8")
        (folder / "config-input.ini").write_text(config, encoding="utf-8")
        evidence.track(folder / "config-input.ini")
        process_env = dict(env, OPENRCT2_ORACLE_USER_PATH=str(profile))
        if lane == "current-vulkan":
            process_env.update({"VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                                "VK_LAYER_SETTINGS_PATH": str(output), "VK_LOADER_DEBUG": "error,warn,layer"})
        command = [str(runtime / executable.name), str(captures), str(args.park)]
        item = {"lane": lane, "command": command, "failures": []}
        summary["processes"].append(item)
        try:
            with (folder / "capture.log").open("w", encoding="utf-8") as log:
                result = subprocess.run(command, cwd=runtime, env=process_env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=args.timeout,
                                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            item["exitCode"] = result.returncode
            require(result.returncode == 0, "Track preview process failed")
            report = read(captures / "report.json")
            item["report"] = report
            require(report.get("schema") == 1 and report.get("fixture") == "track-design-preview"
                    and report.get("fixtureVersion") == 1 and report.get("status") == "pass"
                    and report.get("renderer") == ("vulkan" if lane == "current-vulkan" else lane),
                    "Wrong diagnostic report")
            assets = report["assetState"]
            require(assets.get("rct1Required") is True and assets.get("rct1CsgLoaded") is True
                    and assets.get("g1RecordCount") == 29294 and 0 < assets.get("g1PayloadCount", 0) <= 29294
                    and Path(assets["configuredRct1Path"]).resolve() == args.rct1
                    and Path(assets["configuredRct2Path"]).resolve() == args.rct2, "Required original art not proven")
            require([case["name"] for case in report["cases"]] == list(CASES), "Incomplete preview recipe inventory")
            for case in report["cases"]:
                exclusions = [] if lane == "current-vulkan" else ["constructionRide", "drawingPreview"]
                require(case["restorationContractPassed"] is True and case["restorationExclusions"] == exclusions,
                        "Preview restoration contract differs")
                differences = sorted(key for key in case["before"] if case["before"][key] != case["after"][key])
                require(set(case["before"]) == set(case["after"])
                        and sorted(case["restorationDifferences"]) == differences
                        and case["restored"] == (not differences) and set(differences) <= set(exclusions),
                        "Preview did not restore the live world")
                require(case["extent"] == list(EXTENT) and case["trackType"] == "splash-boats"
                        and case["flatElementCount"] == int(case["name"][4:]) and case["placeScenery"] is False,
                        "Preview recipe differs")
                require(case["expectedZoom"] == {"flat1": 1, "flat35": 2, "flat55": 3}[case["name"]],
                        "Preview zoom recipe differs")
                rows = []
                require(len(case["rotations"]) == 4, "Exactly four preview rotations required")
                palette = (captures / case["name"] / "palette.bin").read_bytes()
                for rotation in range(4):
                    path = captures / case["name"] / ("r" + str(rotation) + ".png")
                    buffers = base.png_buffers(path, EXTENT)
                    require(palette == buffers["palette.bin"], "Palette artifact differs from rotation PNG")
                    require(any(buffers["indexed.bin"]), "Preview must contain visible track pixels")
                    require(case["rotations"][rotation] == {
                        "rotation": rotation, "file": case["name"] + "/r" + str(rotation) + ".png",
                        "nonzeroPixels": sum(value != 0 for value in buffers["indexed.bin"])},
                        "Preview rotation evidence differs")
                    decoded[lane, case["name"], rotation] = buffers
                    rows.append(buffers["indexed.bin"])
                require((captures / case["name"] / "preview.indexed").read_bytes() == b"".join(rows),
                        "PNG indices differ from the actual returned preview buffer")
            if lane == "current-vulkan":
                require(report.get("serviceCreations") == 1 and report.get("deviceCreated") is True,
                        "Preview must use one shared configured service/device")
                require(len(report["sessions"]) == 12 and len(report["completions"]) == 12,
                        "Incomplete service job inventory")
                identities = set()
                for index, (session, completion) in enumerate(zip(report["sessions"], report["completions"])):
                    case, rotation = CASES[index // 4], index % 4
                    name = "track-design-preview-" + str(rotation)
                    require(session == {"name": name, "extent": list(EXTENT), "indexedOutput": True,
                                        "rgbaOutput": False, "lightingEnabled": False,
                                        "sourceTick": report["cases"][index // 4]["before"]["tick"]},
                            "Service request differs")
                    identity = (completion["submissionId"], completion["targetId"], completion["targetGeneration"])
                    require(all(type(value) is int and value > 0 for value in identity)
                            and identity not in identities and completion["identityMatches"] is True
                            and completion["name"] == name and completion["logicalExtent"] == list(EXTENT)
                            and completion["outputExtent"] == list(EXTENT)
                            and completion["indexedBytes"] == EXTENT[0] * EXTENT[1] and completion["rgbaBytes"] == 0,
                            "Service completion differs")
                    identities.add(identity)
                    owned = base.child(captures, completion["file"]).read_bytes()
                    require(owned == decoded[lane, case, rotation]["indexed.bin"],
                            "Preview does not match service-owned output")
                lines = (folder / "capture.log").read_text(encoding="utf-8", errors="replace").splitlines()
                active = any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in line for line in lines)
                messages = [line for line in lines if any(token in line.lower() for token in
                            ("vuid-", "sync-hazard", "validation error", "validation warning", "validation performance warning"))]
                item.update({"validationActivated": active, "validationMessages": messages})
                require(active and not messages, "Vulkan validation missing or diagnostics emitted")
        except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
            item["failures"].append(type(error).__name__ + ": " + str(error))
        summary["failures"].extend(lane + ": " + error for error in item["failures"])
    for case in CASES:
        for rotation in range(4):
            keys = [(lane, case, rotation) for lane in ("upstream-software", "current-vulkan")]
            if not all(key in decoded for key in keys):
                summary["failures"].append("Missing pair: " + case + "/r" + str(rotation))
                continue
            a, b = [decoded[key] for key in keys]
            counts = {name: sum(a[name][i:i + stride] != b[name][i:i + stride] for i in range(0, len(a[name]), stride))
                      for name, stride in (("indexed.bin", 1), ("palette.bin", 3), ("rgba.bin", 4))}
            folder = output / "comparisons" / case / ("r" + str(rotation))
            folder.mkdir(parents=True)
            for lane, label in (("upstream-software", "reference"), ("current-vulkan", "candidate")):
                shutil.copyfile(output / lane / "captures" / case / ("r" + str(rotation) + ".png"), folder / (label + ".png"))
            delta = ImageChops.difference(Image.frombytes("RGBA", EXTENT, a["rgba.bin"]),
                                         Image.frombytes("RGBA", EXTENT, b["rgba.bin"]))
            channels = delta.split()
            visible = Image.merge("RGB", tuple(ImageChops.lighter(c, channels[3]) for c in channels[:3]))
            visible.save(folder / "diff.png")
            visible.point(lambda value: min(255, value * 8)).save(folder / "diff-amplified.png")
            summary["comparisons"].append({"case": case, "rotation": rotation, "differingPixelsOrEntries": counts})
            if any(counts.values()):
                summary["failures"].append("Exact divergence: " + case + "/r" + str(rotation))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("output", "current-build-receipt", "upstream-build-receipt", "shader-build-receipt", "park", "rct1", "rct2"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    output = args.output.resolve()
    require(ROOT in output.parents and not output.exists() and 0 < args.timeout <= 600, "New workspace output/bounded timeout required")
    for name in ("park", "rct1", "rct2"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    output.mkdir(parents=True)
    evidence = base.Evidence()
    summary = {"schema": 1, "kind": "track-preview-upstream-parity", "status": "incomplete", "failures": [],
               "processes": [], "comparisons": [], "manualVisualReview": "pending",
               "scope": "Actual TrackDesignDrawPreview, three synthetic track lengths and four rotations; no world/performance qualification."}
    try:
        execute(args, output, summary, evidence)
    except Exception as error:
        summary["failures"].append(type(error).__name__ + ": " + str(error))
        (output / "runner-error.log").write_text(traceback.format_exc(), encoding="utf-8")
    summary["failures"].extend(evidence.audit())
    summary["inputSha256"] = evidence.files
    summary["outputSha256"] = {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}
    summary["status"] = "fail" if summary["failures"] else "exact-match-pending-review"
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": summary["status"], "failures": summary["failures"], "output": str(output)}))
    raise SystemExit(bool(summary["failures"]))


if __name__ == "__main__":
    main()
