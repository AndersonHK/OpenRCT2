"""Run a provenance-pinned, no-readback ordinary-UI performance measurement.

Hidden smoke runs are the default. Even --visible does not establish displayed
cadence: application draw intervals and present API duration are not scanout.
Use a new workspace output directory for each process; never modify the oracle.
"""

import argparse
import configparser
import datetime
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
NUMBER = r"([0-9]+(?:\.[0-9]+)?)"
STATE_FIELDS = {
    "simulationTick": (r"simulation tick:\s+(\d+)", ("tick",)),
    "guests": (r"guests:\s+(\d+) \((\d+) inside, (\d+) outside\)", ("total", "inside", "outside")),
    "guestStates": (r"guest states:\s+(\d+) walking, (\d+) queued, (\d+) on ride", ("walking", "queued", "onRide")),
    "transportRoutes": (r"transport routes:\s+(\d+) active", ("active",)),
    "staffVehicles": (r"staff / vehicles:\s+(\d+) / (\d+)", ("staff", "vehicles")),
    "routeCache": (r"shared route cache:\s+(\d+) nodes, (\d+) targets, (\d+) direction / (\d+) distance entries, (\d+) single-ride targets \((current|fallback or stale)\)",
                   ("nodes", "targets", "directionEntries", "distanceEntries", "singleRideTargets", "status")),
}


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def json_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def checked_file(base, name, expected):
    path = (base / name).resolve(strict=True)
    if base.resolve() not in path.parents or sha256(path) != expected:
        raise ValueError("Changed or out-of-root artifact: " + str(path))
    return path


def file_inventory(directory):
    return {p.relative_to(directory).as_posix(): sha256(p) for p in sorted(directory.rglob("*")) if p.is_file()}


def qualify_reference():
    receipt_path = ROOT / "docs/vulkan-software-reference.json"
    receipt = read_json(receipt_path)
    frozen = (ROOT / receipt["localReference"]).resolve(strict=True)
    manifest_path = checked_file(frozen, "manifest.json", receipt["manifest"]["sha256"])
    checked_file(frozen, "source.zip", receipt["sourceArchive"]["sha256"])
    manifest = read_json(manifest_path)
    expected = {name: item["sha256"] for name, item in manifest["files"].items()}
    # Verify the complete accepted package, including objects and external images.
    for name, digest in expected.items():
        checked_file(frozen, name, digest)
    actual_package = file_inventory(frozen / "package")
    expected_package = {name[len("package/"):]: digest for name, digest in expected.items() if name.startswith("package/")}
    if actual_package != expected_package:
        raise ValueError("Frozen package file set/hash differs from the accepted manifest")
    return frozen, {"receiptPath": str(receipt_path), "receiptSha256": sha256(receipt_path),
                    "revision": receipt["revision"], "deployedSource": receipt["deployedSource"],
                    "manifestSha256": sha256(manifest_path), "packageSha256": actual_package}


def qualify_current(snapshot_path):
    snapshot_path = snapshot_path.resolve(strict=True)
    base = snapshot_path.parent
    snapshot = read_json(snapshot_path)
    receipt_path = checked_file(base, "receipt.json", snapshot["sourceBuildReceiptSha256"])
    receipt = read_json(receipt_path)
    if receipt["status"] != "pass" or receipt.get("sourceChangesDuringBuild") or receipt.get("missingArtifacts"):
        raise ValueError("Current ordinary build receipt is incomplete/unstable")
    artifacts = snapshot["artifactSha256"]
    if "bin/openrct2.exe" not in artifacts or not any(n.endswith(".spv") for n in artifacts):
        raise ValueError("Snapshot requires the ordinary UI executable and compiled shaders")
    for name, digest in artifacts.items():
        if receipt["artifactSha256"].get(name) != digest:
            raise ValueError("Snapshot/build receipt artifact disagreement: " + name)
        checked_file(base, name, digest)
    return base, {"snapshotPath": str(snapshot_path), "snapshotSha256": sha256(snapshot_path),
                  "receiptPath": str(receipt_path), "receiptSha256": sha256(receipt_path),
                  "sourceRevision": receipt["sourceRevision"], "sourceManifestSha256": json_hash(receipt["sourceSha256"]),
                  "artifactSha256": artifacts}


def one(text, pattern, label):
    matches = re.findall(r"^\s*" + pattern + r"\s*$", text, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError("Expected exactly one complete " + label + "; found " + str(len(matches)))
    return matches[0]


def numeric(text, pattern, names, label):
    values = one(text, pattern, label)
    if isinstance(values, str):
        values = (values,)
    result = dict(zip(names, (float(v) for v in values)))
    if any(not math.isfinite(v) or v < 0 for v in result.values()):
        raise ValueError("Invalid numeric metric: " + label)
    return result


def parse_log(text):
    # Windows console output may retain ANSI resets even when redirected.
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text).replace("\r\n", "\n")
    # Parse only the one terminal report; no ready/progress line may substitute.
    if text.count("Integrated UI benchmark:\n") != 1:
        raise ValueError("Missing or duplicate terminal integrated benchmark report")
    report = text.split("Integrated UI benchmark:\n", 1)[1]
    initial_header = "Initial simulation state:\n"
    final_header = "Final simulation state:\n"
    if report.count(initial_header) != 1 or report.count(final_header) != 1:
        raise ValueError("Missing or duplicate state checkpoint")
    metrics_text, states_text = report.split(initial_header)
    initial_text, final_text = states_text.split(final_header)
    checksum = one(final_text, r"Completed:\s+([0-9a-fA-F]{40})", "final entity checksum").lower()
    states = {}
    for name, section in (("initial", initial_text), ("final", final_text)):
        state = {}
        for field, (pattern, names) in STATE_FIELDS.items():
            values = one(section, pattern, name + " " + field)
            if isinstance(values, str):
                values = (values,)
            state[field] = {key: int(value) if value.isdigit() else value for key, value in zip(names, values)}
        if state["guests"]["total"] != state["guests"]["inside"] + state["guests"]["outside"]:
            raise ValueError("Inconsistent guest checkpoint")
        states[name] = state
    n = NUMBER
    metrics = {
        "renderer": one(metrics_text, r"renderer:\s+(software|vulkan)", "renderer"),
        "vsync": one(metrics_text, r"VSync:\s+(enabled|disabled)", "VSync"),
        "elapsedSeconds": float(one(metrics_text, r"elapsed:\s+" + n + r" s", "elapsed")),
        "logicalTicks": int(one(metrics_text, r"logical ticks:\s+(\d+)", "logical ticks")),
        "logicalTps": float(one(metrics_text, r"actual logical TPS:\s+" + n, "logical TPS")),
        "draws": numeric(metrics_text, r"draws / FPS:\s+(\d+) / " + n, ("count", "fps"), "draws/FPS"),
        "ui": numeric(metrics_text, r"message / window:\s+(\d+) pumps / (\d+) updates \(" + n + " / " + n + r" Hz\)",
                      ("messagePumps", "windowUpdates", "messageHz", "windowHz"), "UI rates"),
        "simulation": numeric(metrics_text, r"simulation time:\s+" + n + r" s \(" + n + r"%, " + n + r" us/logical tick\)",
                              ("seconds", "utilisationPercent", "meanMicrosecondsPerTick"), "simulation time"),
        "simulationBatches": numeric(metrics_text, r"simulation batches:\s+(\d+) \(" + n + r" us mean, " + n + r" ms longest; " + n + r" ms longest UI-bounded slice\)",
                                     ("count", "meanMicroseconds", "longestMilliseconds", "longestSliceMilliseconds"), "simulation batches"),
        "drawCpu": numeric(metrics_text, r"draw time:\s+" + n + r" s \(" + n + r"%, " + n + r" us/draw; includes presentation\)",
                           ("seconds", "utilisationPercent", "meanMicroseconds"), "draw CPU"),
    }
    missing = []
    display_observation = None
    if "drawable pixels:" in metrics_text or "monitor refresh:" in metrics_text:
        extents = tuple(int(v) for v in one(metrics_text,
            r"drawable pixels:\s+(\d+) x (\d+) initial, (\d+) x (\d+) final", "drawable pixels"))
        refresh = tuple(int(v) for v in one(metrics_text,
            r"monitor refresh:\s+(\d+) Hz initial, (\d+) Hz final", "monitor refresh"))
        display_observation = {"initialExtent": list(extents[:2]), "finalExtent": list(extents[2:]),
                               "initialRefreshHz": refresh[0], "finalRefreshHz": refresh[1],
                               "scope": "SDL physical output at measurement boundaries; not a scanout observation"}
    frame_pattern = r"frame intervals:\s+" + n + " ms p50, " + n + " ms p95, " + n + " ms p99, " + n + " ms max"
    if "frame intervals:" in metrics_text:
        metrics["applicationFrameIntervalsMs"] = numeric(metrics_text, frame_pattern, ("p50", "p95", "p99", "max"), "frame intervals")
    else:
        metrics["applicationFrameIntervalsMs"] = None
        missing.append("application frame intervals unavailable (fewer than two draws or unsupported binary)")
    optional = {
        "rendererCpu": ("renderer CPU", n + " us submit, " + n + r" us present mean \((\d+) fence-complete samples\)", ("submitMeanUs", "presentMeanUs", "samples")),
        "presentApi": ("present API call", n + r" us mean \((\d+) samples\)", ("meanUs", "samples")),
        "gpuFrame": ("GPU frame", n + r" us mean \((\d+) samples\)", ("meanUs", "samples")),
        "gpuPasses": ("GPU passes", n + " upload, " + n + " draw, " + n + " LightFX, " + n + " composite us mean", ("uploadMeanUs", "drawMeanUs", "lightFxMeanUs", "compositeMeanUs")),
    }
    for key, (label, pattern, names) in optional.items():
        if re.search(r"^\s*" + label + r":\s+unavailable\s*$", metrics_text, re.MULTILINE):
            one(metrics_text, label + r":\s+(unavailable)", label)
            metrics[key] = None
            missing.append(label + " explicitly unavailable")
        else:
            metrics[key] = numeric(metrics_text, label + r":\s+" + pattern, names, label)
    if metrics["elapsedSeconds"] <= 0 or metrics["logicalTicks"] <= 0 or metrics["draws"]["count"] <= 0:
        raise ValueError("Empty benchmark interval")
    if (states["final"]["simulationTick"]["tick"] - states["initial"]["simulationTick"]["tick"]) % (2 ** 32) != metrics["logicalTicks"]:
        raise ValueError("Checkpoint tick delta differs from measured logical ticks")
    # Printed values are rounded (elapsed 6dp; rates 3dp).
    for observed, count in ((metrics["logicalTps"], metrics["logicalTicks"]), (metrics["draws"]["fps"], metrics["draws"]["count"])):
        expected = count / metrics["elapsedSeconds"]
        tolerance = .00051 + count * .00000051 / (metrics["elapsedSeconds"] ** 2)
        if abs(observed - expected) > tolerance:
            raise ValueError("Printed rate inconsistent with elapsed/count")
    result = {"metrics": metrics, "states": states, "finalEntityChecksum": checksum,
              "missingMetrics": missing + ["initial entity checksum is not emitted", "actual drawable extent/selected monitor refresh is not emitted",
                                            "displayed presentation cadence is not measured", "CPU copied/upload byte counters are not emitted"]}
    if display_observation is not None:
        result["displayObservation"] = display_observation
        result["missingMetrics"].remove("actual drawable extent/selected monitor refresh is not emitted")
    if "Benchmark phase timing v1:" in metrics_text:
        payload = json.loads(one(metrics_text, r"Benchmark phase timing v1:\s+(\{.*\})", "benchmark phase timing"))
        validate_phase_timing(payload, metrics["logicalTicks"])
        result["phaseTiming"] = payload
    if "Benchmark simulation pacing:" in metrics_text:
        result["simulationPacing"] = one(metrics_text,
            r"Benchmark simulation pacing:\s+(ordinary Turbo 360 TPS target|uncapped headroom)", "simulation pacing")
    if "Upload telemetry v1:" in metrics_text:
        payload = json.loads(one(metrics_text, r"Upload telemetry v1:\s+(\{.*\})", "upload telemetry"))
        validate_upload_telemetry(payload)
        result["uploadTelemetry"] = payload
        result["missingMetrics"].remove("CPU copied/upload byte counters are not emitted")
        result["missingMetrics"].append("producer CPU copies, physical bandwidth, flush/high-water and generation age are not measured")
    return result


def qualify_final_screenshot(text, profile, output, result, width, height):
    from PIL import Image
    clean = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text).replace("\r\n", "\n")
    record = json.loads(one(clean, r"Final benchmark screenshot v1:\s+(\{.*\})", "final screenshot receipt"))
    if (record.get("schema") != 1 or record.get("outsideMeasurement") is not True
            or record.get("authoritativeStateUnchanged") is not True
            or type(record.get("completedFrameNumber")) is not int or record["completedFrameNumber"] <= 0
            or record.get("simulationTick") != result["states"]["final"]["simulationTick"]["tick"]
            or record.get("entityChecksum") != result["finalEntityChecksum"]
            or type(record.get("partialRender")) is not bool):
        raise ValueError("Final screenshot state/boundary receipt is inconsistent")
    if record.get("logicalExtent") != [width, height] or record.get("drawableExtent") != [width, height]:
        raise ValueError("Final screenshot target extent differs from the measured target")
    camera = record.get("camera")
    if (not isinstance(camera, dict) or set(camera) != {"viewPosition", "rotation", "zoom", "flags"}
            or not isinstance(camera["viewPosition"], list) or len(camera["viewPosition"]) != 2
            or any(type(v) is not int for v in camera["viewPosition"])
            or type(camera["rotation"]) is not int or not 0 <= camera["rotation"] <= 3
            or type(camera["zoom"]) is not int or type(camera["flags"]) is not int):
        raise ValueError("Final screenshot camera receipt is invalid")
    source = Path(record["path"]).resolve(strict=True)
    # PlatformEnvironment owns this exact production directory spelling.
    roots = [(profile / "screenshot").resolve()]
    if not any(parent in source.parents for parent in roots) or source.suffix.lower() != ".png":
        raise ValueError("Final screenshot escaped the isolated screenshot directory")
    screenshots = sorted(p for p in profile.rglob("*.png") if any(parent in p.resolve().parents for parent in roots))
    if screenshots != [source]:
        raise ValueError("Expected exactly one post-measurement production screenshot")
    with Image.open(source) as image:
        image.load()
        if image.format != "PNG" or image.size != (width, height) or image.mode != "P":
            raise ValueError("Final screenshot is not the expected indexed main canvas")
        indices_sha = hashlib.sha256(image.tobytes()).hexdigest()
        rgba_sha = hashlib.sha256(image.convert("RGBA").tobytes()).hexdigest()
    target = output / "final-benchmark.png"
    if target.exists():
        raise ValueError("Final screenshot destination already exists")
    shutil.copy2(source, target)
    if sha256(source) != sha256(target):
        raise ValueError("Final screenshot changed during copy")
    return {"receipt": record, "path": str(target), "sha256": sha256(target),
            "indexedSha256": indices_sha, "rgbaSha256": rgba_sha,
            "scope": "one new final main-canvas draw and indexed readback after measurement; no measured-loop readback; not scanout or original-renderer parity"}


def validate_display_observation(result, width, height):
    observation = result.get("displayObservation")
    if observation is None:
        raise ValueError("Required actual drawable/refresh evidence is unavailable in this executable")
    if observation["initialExtent"] != [width, height] or observation["finalExtent"] != [width, height]:
        raise ValueError("Actual physical drawable differs from requested benchmark extent")
    if observation["initialRefreshHz"] <= 0 or observation["initialRefreshHz"] != observation["finalRefreshHz"]:
        raise ValueError("Selected monitor refresh is unavailable or changed during measurement")


def validate_upload_telemetry(payload):
    integer_fields = ("schema", "attemptedFrames", "submittedFrames", "auxiliarySamples", "allocatedBytes", "alignmentBytes",
                      "allocationFailures", "captureRequests", "readbackRequests", "readbackBytes", "lostSamples")
    optional_fields = {"statusReadbackRequests", "statusReadbackBytes", "worldBufferCopyCalls"}
    if set(payload) - optional_fields != set(integer_fields) | {"attempted", "submitted", "overflow"}:
        raise ValueError("Unknown/missing upload telemetry schema fields")
    if any(type(payload[k]) is not int or not 0 <= payload[k] <= 2**64-1 for k in optional_fields if k in payload):
        raise ValueError("Invalid native status/copy telemetry integer")
    # Older receipts omit all three fields. Status transfers are bounded scalar safety checks, not image readbacks.
    if bool(payload.get("statusReadbackRequests", 0)) != bool(payload.get("statusReadbackBytes", 0)):
        raise ValueError("Native status request/byte coverage disagrees")
    if any(type(payload[k]) is not int or not 0 <= payload[k] <= 2**64-1 for k in integer_fields):
        raise ValueError("Invalid upload telemetry integer")
    if payload["schema"] != 1 or type(payload["overflow"]) is not bool or payload["overflow"]:
        raise ValueError("Unsupported/overflowed upload telemetry")
    if not 0 < payload["submittedFrames"] <= payload["attemptedFrames"]:
        raise ValueError("Upload telemetry lacks submitted-frame coverage")
    for name in ("attempted", "submitted"):
        rows = payload[name]
        if not isinstance(rows, list) or len(rows) != 6 or any(not isinstance(row, list) or len(row) != 5 for row in rows):
            raise ValueError("Upload byte matrix must have six categories and five metrics")
        if any(type(v) is not int or not 0 <= v <= 2**64-1 for row in rows for v in row):
            raise ValueError("Invalid upload byte matrix integer")
    for a, b in zip(payload["attempted"], payload["submitted"]):
        if any(y > x for x, y in zip(a, b)) or a[3] + a[4] > a[0] or b[3] + b[4] > b[0]:
            raise ValueError("Inconsistent attempted/submitted/direct-host byte accounting")
    if sum(row[0] for row in payload["attempted"]) > payload["allocatedBytes"]:
        raise ValueError("Mapped host writes exceed successful ring allocation payload")
    if any(payload[k] for k in ("allocationFailures", "lostSamples", "captureRequests", "readbackRequests", "readbackBytes")):
        raise ValueError("Telemetry measurement includes allocation failure, lost samples or capture/readback work")


def validate_phase_timing(payload, logical_ticks):
    phases = {"simulation", "tick", "messages", "ui", "drawBegin", "drawPaint", "drawEnd", "networkUpdate",
              "networkFlush", "schedulerWait", "drawInterval"}
    if (set(payload) != {"schema", "capacityPerPhase", "phases", "tickWindowSize", "tickWindowCapacity", "tickWindows", "scope"}
            or payload["schema"] != 1 or payload["capacityPerPhase"] != 16 or payload["tickWindowSize"] != 3000
            or payload["tickWindowCapacity"] != 16 or set(payload["phases"]) != phases):
        raise ValueError("Unsupported benchmark phase timing schema")
    def nonnegative(value):
        return type(value) in (int, float) and math.isfinite(value) and value >= 0
    for sample in payload["phases"].values():
        if set(sample) != {"count", "wallMs", "threadCycles", "threadCycleSamples", "worst"}:
            raise ValueError("Invalid phase sample fields")
        if any(type(sample[k]) is not int or sample[k] < 0 for k in ("count", "threadCycles", "threadCycleSamples")):
            raise ValueError("Invalid phase sample counters")
        if not nonnegative(sample["wallMs"]) or sample["threadCycleSamples"] > sample["count"]:
            raise ValueError("Invalid phase timing totals")
        if not isinstance(sample["worst"], list) or len(sample["worst"]) != min(sample["count"], 16):
            raise ValueError("Incomplete bounded worst-phase sample set")
        previous = math.inf
        for event in sample["worst"]:
            if set(event) != {"simulationTick", "offsetMs", "wallMs", "threadCycles", "threadCyclesAvailable", "requestedWaitMs"}:
                raise ValueError("Invalid phase event fields")
            if (type(event["simulationTick"]) is not int or not 0 <= event["simulationTick"] < 2**32
                    or type(event["threadCycles"]) is not int or event["threadCycles"] < 0
                    or type(event["threadCyclesAvailable"]) is not bool
                    or any(not nonnegative(event[k]) for k in ("offsetMs", "wallMs", "requestedWaitMs"))
                    or event["wallMs"] > previous or event["wallMs"] > sample["wallMs"] + 1e-6):
                raise ValueError("Invalid or unsorted phase event")
            previous = event["wallMs"]
    windows = payload["tickWindows"]
    if not isinstance(windows, list) or len(windows) != min(logical_ticks // 3000, 16):
        raise ValueError("Incomplete benchmark tick windows")
    for index, window in enumerate(windows):
        if (set(window) != {"startLogicalTick", "endLogicalTick", "simulationTick", "elapsedSeconds", "simulationSeconds", "draws", "drawSeconds"}
                or window["startLogicalTick"] != index * 3000 or window["endLogicalTick"] != (index + 1) * 3000
                or any(not nonnegative(window[k]) for k in ("elapsedSeconds", "simulationSeconds", "drawSeconds"))
                or window["elapsedSeconds"] <= 0 or type(window["draws"]) is not int or window["draws"] < 0):
            raise ValueError("Invalid benchmark tick window")


def quote_ini_string(value):
    # Match Config/IniWriter.cpp::WriteString, not shell or JSON escaping.
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


def make_config(args):
    config = configparser.ConfigParser(interpolation=None, strict=True)
    if args.config_seed:
        config.read_string(args.config_seed.read_text(encoding="utf-8-sig"))
    defaults = {
        "general": {"landscape_smoothing": "true", "language": "en-US", "uncap_fps": "true", "multithreading": "true",
                    "day_night_cycle": "false", "enable_light_fx": "false", "enable_light_fx_for_vehicles": "false",
                    "render_weather_effects": "true", "render_weather_gloom": "true",
                    "show_fps": "false", "always_show_gridlines": "false", "window_scale": "1.0", "infer_display_dpi": "false"},
        "interface": {"current_theme": "\"*RCT2\""},
    }
    for section, values in defaults.items():
        if not config.has_section(section):
            config.add_section(section)
        for key, value in values.items():
            if not config.has_option(section, key):
                config.set(section, key, value)
    forced = {"window_width": str(args.width), "window_height": str(args.height), "fullscreen_mode": "0", "default_display": str(args.display),
              "use_vsync": "true" if args.vsync else "false", "enable_hdr10_output": "false",
              "play_intro": "false", "edge_scrolling": "false", "autosave": "5", "last_version_check_time": "4102444800",
              # Context reloads configuration after CLI path processing. Both
              # inputs must agree so RCT1 object images stay linked at load.
              "rct1_path": quote_ini_string(args.rct1_path.resolve(strict=True)),
              "game_path": quote_ini_string(args.rct2_path.resolve(strict=True))}
    for key, value in forced.items():
        config.set("general", key, value)
    # Current source has one renderer. The external frozen binary is selected by
    # its explicit --benchmark-renderer argument; neither lane needs this old key.
    config.remove_option("general", "drawing_engine")
    stream = io.StringIO()
    config.write(stream)
    return stream.getvalue()


def host_info():
    result = {"platform": platform.platform(), "machine": platform.machine(), "processor": platform.processor(),
              "logicalCpuCount": os.cpu_count(), "python": platform.python_version()}
    if os.name == "nt":
        # This read-only identity remains available when the CIM service is denied.
        # It supplies neither physical core topology nor selected-display information.
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
                result["registryCpu"] = {
                    "source": "HKLM/HARDWARE/DESCRIPTION/System/CentralProcessor/0",
                    "name": winreg.QueryValueEx(key, "ProcessorNameString")[0].strip(),
                    "vendor": winreg.QueryValueEx(key, "VendorIdentifier")[0].strip()}
        except OSError as error:
            result["registryCpu"] = {"unavailable": str(error)}
    command = "[ordered]@{ cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors); gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,PNPDeviceID,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate); power=(powercfg /getactivescheme) } | ConvertTo-Json -Depth 5"
    try:
        proc = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True, timeout=30,
                              creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        result["systemReported"] = json.loads(proc.stdout) if proc.returncode == 0 else {"unavailable": proc.stderr.strip()}
        if proc.stderr.strip():
            result["collectionDiagnostics"] = proc.stderr.strip()
        result["missingObservations"] = []
        for key in ("cpu", "gpu"):
            if not result["systemReported"].get(key):
                result["missingObservations"].append(key + " CIM inventory unavailable")
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        result["systemReported"] = {"unavailable": str(error)}
    return result


def junction(source, destination):
    quote = lambda path: "'" + str(path).replace("'", "''") + "'"
    subprocess.run(["powershell", "-NoProfile", "-Command", "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path "
                    + quote(destination) + " -Target " + quote(source) + " | Out-Null"], check=True,
                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)


def compare_summary(reference_dir, candidate):
    path = reference_dir.resolve(strict=True) / "summary.json"
    reference = read_json(path)
    if reference.get("status") != "pass":
        raise ValueError("Comparison run did not pass")
    if sha256(path.parent / "benchmark.log") != reference["logSha256"]:
        raise ValueError("Reference benchmark log changed")
    for filename, key in (("launch.json", "launchSha256"), ("licensed-assets-before.json", "licensedAssetManifestBeforeSha256"),
                          ("licensed-assets-after.json", "licensedAssetManifestAfterSha256")):
        if sha256(path.parent / filename) != reference[key]:
            raise ValueError("Reference provenance record changed: " + filename)
    launch = read_json(path.parent / "launch.json")
    if launch["workload"] != reference["workload"] or launch["command"] != reference["command"]:
        raise ValueError("Reference launch workload disagrees with summary")
    if sha256(path.parent / "config-input.ini") != reference["workload"]["configInputSha256"]:
        raise ValueError("Reference configuration input changed")
    reparsed = parse_log((path.parent / "benchmark.log").read_text(encoding="utf-8", errors="replace"))
    if reparsed != reference["result"]:
        raise ValueError("Reference parsed metrics/state disagree with original log")
    if reference["workload"] != candidate["workload"]:
        raise ValueError("Comparison workload/config/asset/visibility/profile inputs differ")
    if reference["hostBefore"] != candidate["hostBefore"]:
        raise ValueError("Comparison host/display/driver/power environment differs")
    if reference["environment"] != candidate["environment"]:
        raise ValueError("Comparison renderer environment differs")
    for key in ("states", "finalEntityChecksum"):
        if reference["result"][key] != candidate["result"][key]:
            raise ValueError("Comparison simulation " + key + " differs")
    a, b = reference["result"]["metrics"], candidate["result"]["metrics"]
    return {"referenceSummary": str(path), "referenceSummarySha256": sha256(path), "stateAndWorkloadMatch": True,
            "logicalTpsRatio": b["logicalTps"] / a["logicalTps"], "logicalTpsPercentChange": (b["logicalTps"] / a["logicalTps"] - 1) * 100,
            "referenceMetrics": a, "candidateMetrics": b,
            "qualification": "Descriptive single-run comparison only; not Gate P acceptance or displayed cadence evidence"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("frozen-software", "current-vulkan"), required=True)
    parser.add_argument("--current-snapshot", type=Path, default=ROOT / "obj/vulkan-parity/performance-baseline-build26/snapshot.json")
    parser.add_argument("--rct1-path", type=Path, required=True)
    parser.add_argument("--rct2-path", type=Path, required=True)
    parser.add_argument("--park", type=Path)
    parser.add_argument("--config-seed", type=Path)
    parser.add_argument("--warmup-ticks", type=int, default=100)
    parser.add_argument("--ticks", type=int, default=3000)
    parser.add_argument("--vsync", type=int, choices=(0, 1), default=1)
    parser.add_argument("--width", type=int, default=3840)
    parser.add_argument("--height", type=int, default=2160)
    parser.add_argument("--require-display-evidence", action="store_true",
                        help="Fail unless observed physical extent matches the request and monitor refresh is stable")
    parser.add_argument("--display", type=int, default=0)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--visible", action="store_true", help="Show the benchmark window; external display trace still required")
    parser.add_argument("--attribution-profile", choices=("csv", "json"), help="Separate instrumented attribution lane, never clean acceptance")
    parser.add_argument("--compare-run", type=Path)
    parser.add_argument("--final-screenshot", action="store_true", help="Current-only one final main-canvas PNG after timing stops; excluded from measured workload")
    parser.add_argument("--uncapped-simulation", action="store_true", help="Current-only simulation headroom experiment; ordinary gameplay keeps its 360 TPS target")
    parser.add_argument("--upload-telemetry", action="store_true", help="Opt-in Vulkan API upload payload attribution; not clean TPS acceptance")
    args = parser.parse_args()
    output = args.output.resolve()
    if ROOT not in output.parents or output.exists():
        parser.error("Use a new output directory inside the workspace")
    if args.warmup_ticks < 0 or args.ticks <= 0 or args.ticks > 2147483647 or args.warmup_ticks > 2147483647:
        parser.error("Tick counts must fit signed 32-bit; warmup >= 0 and measured ticks > 0")
    if args.width < 640 or args.height < 480 or args.display < 0 or args.timeout <= 0:
        parser.error("Require width >= 640, height >= 480, display >= 0, timeout > 0")
    if args.final_screenshot and args.mode != "current-vulkan":
        parser.error("--final-screenshot requires a current binary with the post-measurement hook; frozen binaries remain unchanged")
    if args.uncapped_simulation and args.mode != "current-vulkan":
        parser.error("--uncapped-simulation requires current-vulkan")
    if args.upload_telemetry and args.mode != "current-vulkan":
        parser.error("--upload-telemetry requires current-vulkan; the frozen executable is not instrumented")
    output.mkdir(parents=True)
    summary = {"schema": 1, "status": "fail", "mode": args.mode, "failures": [], "qualification": "No Gate P acceptance claim",
               "runnerSha256": sha256(Path(__file__)), "startedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    try:
        frozen, reference = qualify_reference()
        current_root, current = (None, None) if args.mode == "frozen-software" else qualify_current(args.current_snapshot)
        summary["frozenReference"] = reference
        summary["currentBuild"] = current
        park = (args.park or frozen / "testdata/parks/EverythingPark.park").resolve(strict=True)
        park_hash = sha256(park)
        games = {"rct1": args.rct1_path.resolve(strict=True), "rct2": args.rct2_path.resolve(strict=True)}
        licensed = {name: file_inventory(path) for name, path in games.items()}
        for name, required in (("rct1", ("data/csg1.dat", "data/csg1i.dat")), ("rct2", ("data/g1.dat",))):
            if not set(required).issubset({p.lower() for p in licensed[name]}):
                raise ValueError("Missing required original-game assets in " + name)
        write_json(output / "licensed-assets-before.json", licensed)
        summary["licensedAssetManifestBeforeSha256"] = sha256(output / "licensed-assets-before.json")
        summary["licensedAssetPaths"] = {name: str(path) for name, path in games.items()}
        seed_hash = sha256(args.config_seed) if args.config_seed else None
        config_text = make_config(args)
        profile = output / "profile"
        profile.mkdir()
        (profile / "config.ini").write_text(config_text, encoding="utf-8")
        (output / "config-input.ini").write_text(config_text, encoding="utf-8")
        runtime = output / "runtime"
        runtime.mkdir()
        # Runtime dependencies come from the accepted full package, never PATH/bin leftovers.
        for path in (frozen / "package").iterdir():
            if path.is_file() and path.suffix.lower() == ".dll":
                shutil.copy2(path, runtime / path.name)
        executable_source = frozen / "package/openrct2.exe" if current is None else current_root / "bin/openrct2.exe"
        shutil.copy2(executable_source, runtime / "openrct2.exe")
        data = output / "data"
        data.mkdir()
        for path in (frozen / "package/data").iterdir():
            if path.name == "shaders":
                continue
            if path.is_dir():
                junction(path, data / path.name)
            else:
                shutil.copy2(path, data / path.name)
        shaders = data / "shaders"
        if (frozen / "package/data/shaders").is_dir():
            shutil.copytree(frozen / "package/data/shaders", shaders)
        else:
            shaders.mkdir()
        if current is not None:
            target = shaders / "vulkan"
            target.mkdir(exist_ok=True)
            for name in current["artifactSha256"]:
                if name.endswith(".spv"):
                    shutil.copy2(current_root / name, target / Path(name).name)
        renderer = {"frozen-software": "software", "current-vulkan": "vulkan"}[args.mode]
        command = [str(runtime / "openrct2.exe"), str(park), "--benchmark-ui", "--benchmark-renderer", renderer,
                   "--benchmark-vsync", str(args.vsync), "--benchmark-warmup-ticks", str(args.warmup_ticks), "--benchmark-ticks", str(args.ticks),
                   "--user-data-path", str(profile), "--openrct2-data-path", str(data),
                   "--rct1-data-path", str(games["rct1"]), "--rct2-data-path", str(games["rct2"])]
        if args.final_screenshot:
            command.append("--benchmark-final-screenshot")
        if args.uncapped_simulation:
            command.append("--benchmark-uncapped-simulation")
        if args.upload_telemetry:
            command.append("--benchmark-upload-telemetry")
        if args.visible:
            command.append("--benchmark-visible")
        if args.attribution_profile:
            command += ["--benchmark-profile", str(output / ("profile." + args.attribution_profile))]
        env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
        relevant = {k: v for k, v in env.items() if k.startswith(("SDL_", "VK_", "OPENRCT2_", "__GL_", "DRI_", "MESA_"))}
        if any(any(word in key.upper() for word in ("CAPTURE", "VALIDATION", "LAYER", "PROFILE", "PARITY", "DIAGNOSTIC")) for key in relevant):
            raise ValueError("Inherited capture/validation/profile environment is not permitted: " + ", ".join(sorted(relevant)))
        summary["environment"] = relevant
        summary["hostBefore"] = host_info()
        summary["workload"] = {"parkSha256": park_hash, "warmupTicks": args.warmup_ticks, "measuredTicks": args.ticks,
                               "vsync": args.vsync, "visible": args.visible, "profileMode": args.attribution_profile,
                               "configInputSha256": sha256(output / "config-input.ini"), "configSeedSha256": seed_hash,
                               "acceptedAssetManifestSha256": reference["manifestSha256"], "licensedAssetsSha256": json_hash(licensed),
                               "simulationSpeed": "ordinary Turbo", "camera": "saved park view; no input events injected"}
        if args.upload_telemetry:
            summary["workload"]["uploadTelemetry"] = 1
        if args.uncapped_simulation:
            summary["workload"]["simulationSpeed"] = "uncapped benchmark headroom (Turbo logical ticks)"
        summary["postMeasurementScreenshotRequested"] = args.final_screenshot
        summary["command"] = command
        summary["runtimeBefore"] = file_inventory(runtime)
        # Non-shader assets are junctions to the fully verified accepted package.
        summary["shaderBefore"] = file_inventory(shaders)
        summary["dataRootFilesBefore"] = {p.name: sha256(p) for p in data.iterdir() if p.is_file()}
        summary["profileBefore"] = file_inventory(profile)
        summary["configuredDisplay"] = {"width": args.width, "height": args.height, "displayIndex": args.display,
                                        "actualDrawableExtent": None, "selectedMonitorRefreshHz": None,
                                        "limitation": "CIM desktop display data, when available, is not an observation of the game's selected drawable"}
        write_json(output / "launch.json", summary)
        summary["launchSha256"] = sha256(output / "launch.json")
        start = time.monotonic()
        log_path = output / "benchmark.log"
        with log_path.open("w", encoding="utf-8") as log:
            try:
                result = subprocess.run(command, cwd=runtime, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout,
                                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                summary["exitCode"] = result.returncode
            except subprocess.TimeoutExpired:
                summary["exitCode"] = "timeout"
        summary["processElapsedSeconds"] = time.monotonic() - start
        summary["logSha256"] = sha256(log_path)
        summary["hostAfter"] = host_info()
        summary["runtimeAfter"] = file_inventory(runtime)
        summary["shaderAfter"] = file_inventory(shaders)
        summary["dataRootFilesAfter"] = {p.name: sha256(p) for p in data.iterdir() if p.is_file()}
        summary["profileAfter"] = file_inventory(profile)
        summary["parkSha256After"] = sha256(park)
        licensed_after = {name: file_inventory(path) for name, path in games.items()}
        write_json(output / "licensed-assets-after.json", licensed_after)
        summary["licensedAssetManifestAfterSha256"] = sha256(output / "licensed-assets-after.json")
        _, reference_after = qualify_reference()
        summary["acceptedReferenceUnchanged"] = reference_after == reference
        summary["currentBuildUnchanged"] = current is None or qualify_current(args.current_snapshot)[1] == current
        if summary["exitCode"] != 0:
            summary["failures"].append("Executable failed: " + str(summary["exitCode"]))
        for key in ("runtime", "shader", "dataRootFiles", "host"):
            if summary[key + "Before"] != summary[key + "After"]:
                summary["failures"].append(key + " changed during process")
        if not summary["acceptedReferenceUnchanged"] or not summary["currentBuildUnchanged"] or licensed_after != licensed or sha256(park) != park_hash:
            summary["failures"].append("Immutable runtime input changed")
        if sha256(Path(__file__)) != summary["runnerSha256"] or (args.config_seed and sha256(args.config_seed) != seed_hash):
            summary["failures"].append("Runner/config seed changed during process")
        if sha256(output / "config-input.ini") != summary["workload"]["configInputSha256"] or sha256(output / "launch.json") != summary["launchSha256"]:
            summary["failures"].append("Saved runtime input/launch record changed during process")
        text = log_path.read_text(encoding="utf-8", errors="replace")
        suspicious = [line for line in text.splitlines() if any(token in line.lower() for token in
                      ("vuid-", "sync-hazard", "validation error", "validation warning", "fallback images", "missing object", "could not", "failed to"))]
        if suspicious:
            summary["failures"].append("Runtime diagnostic requires investigation")
            summary["runtimeDiagnostics"] = suspicious
        summary["result"] = parse_log(text)
        expected_pacing = "uncapped headroom" if args.uncapped_simulation else "ordinary Turbo 360 TPS target"
        if summary["result"].get("simulationPacing", "ordinary Turbo 360 TPS target") != expected_pacing:
            summary["failures"].append("Requested/actual benchmark simulation pacing differs")
        if "phaseTiming" in summary["result"]:
            summary["phaseTimingInstrumentation"] = "Fixed top16 per main-thread phase; clock/thread-cycle sampling included in elapsed time; no loop I/O or full profiler; nested phases overlap"
        if args.final_screenshot:
            summary["finalScreenshot"] = qualify_final_screenshot(
                text, profile, output, summary["result"], args.width, args.height)
            if summary["finalScreenshot"]["receipt"]["partialRender"]:
                summary["qualification"] = "Partial GPU-only rendering throughput; omitted world categories prevent full-render performance acceptance"
        elif "Final benchmark screenshot v1:" in text:
            summary["failures"].append("Unexpected final screenshot in a no-capture process")
        if args.require_display_evidence:
            validate_display_observation(summary["result"], args.width, args.height)
        if "displayObservation" in summary["result"]:
            summary["observedDisplay"] = summary["result"]["displayObservation"]
        if ("uploadTelemetry" in summary["result"]) != args.upload_telemetry:
            summary["failures"].append("Requested/actual upload telemetry mode differs")
        if args.upload_telemetry and "uploadTelemetry" in summary["result"]:
            summary["uploadTelemetryDefinitions"] = {
                "categories": ["palette", "lookup", "atlas", "lightFx", "world", "commands"],
                "metrics": ["mappedHostWrittenBytes", "recordedBufferTransferBytes", "recordedImageTexelBytes", "directHostVertexPayloadBytes", "directHostStoragePayloadBytes"],
                "units": "API payload bytes; physical bandwidth is not measured",
                "statusReadback": "Optional statusReadbackRequests/statusReadbackBytes are scalar asynchronous safety checks, not images; absent legacy fields mean zero",
                "worldBufferCopyCalls": "Optional batched world vkCmdCopyBuffer call count; absent legacy field means zero",
                "scope": "backend only; producer CPU copies, full ring flush/high-water and generation age remain unavailable"}
        m = summary["result"]["metrics"]
        if m["renderer"] != renderer or m["vsync"] != ("enabled" if args.vsync else "disabled") or m["logicalTicks"] != args.ticks:
            summary["failures"].append("Actual renderer/VSync/tick count differs from requested workload")
        ready = "Integrated UI benchmark ready: renderer=" + renderer + ", VSync=" + ("enabled" if args.vsync else "disabled") + ", " + ("visible" if args.visible else "hidden") + " window, ordinary Turbo."
        if text.count(ready) != 1:
            summary["failures"].append("Actual visibility/ordinary Turbo startup was not confirmed")
        if args.attribution_profile:
            p = output / ("profile." + args.attribution_profile)
            if not p.is_file() or p.stat().st_size == 0:
                summary["failures"].append("Attribution profiler output missing/empty")
            else:
                summary["profilerSha256"] = sha256(p)
        elif "Integrated profiler enabled" in text:
            summary["failures"].append("Unexpected profiler activation in clean lane")
        if args.compare_run:
            summary["comparison"] = compare_summary(args.compare_run, summary)
            if summary.get("finalScreenshot", {}).get("receipt", {}).get("partialRender"):
                summary["comparison"]["qualification"] = "Matched simulation workload, partial candidate world rendering; TPS ratio is not an equivalent full-render gain or Gate P acceptance"
        summary["measurementLimitations"] = ["Hidden smoke cannot certify displayed pacing" if not args.visible else "Visible run still needs independent displayed-presentation trace",
                                             "No image readbacks during measurement; optional final image is after metrics freeze" if args.final_screenshot else "No image readbacks or diagnostic capture requests; ordinary main loop",
                                             "Single-run results do not establish a substantial TPS gain", "Initial checkpoint is a state census; initial entity checksum is unavailable"]
        if args.uncapped_simulation:
            summary["qualification"] += "; uncapped benchmark headroom, not ordinary 360 TPS gameplay pacing"
        if not summary["failures"]:
            summary["status"] = "pass"
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        summary["failures"].append(str(error))
    summary["finishedUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    write_json(output / "summary.json", summary)
    print(json.dumps({"status": summary["status"], "summary": str(output / "summary.json"), "failures": summary["failures"]}))
    return 0 if summary["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
