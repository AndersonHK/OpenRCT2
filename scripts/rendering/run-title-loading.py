"""Measure real title-park loading in a fresh, receipt-qualified user profile.

Uses the bundled parks and original camera commands, shortening only the holds.
No synthetic simulation driver, readback during playback, or input automation.
The opt-in END hook requests ordinary context teardown. A timeout is a failure.
"""

import argparse
import configparser
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SEQUENCE = "loading-probe"


def support():
    path = Path(__file__).with_name("run-render-performance.py")
    spec = importlib.util.spec_from_file_location("render_performance_support", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare_sequence(archive, target, park_limit, loops, hold_ms):
    with zipfile.ZipFile(archive) as source:
        original = source.read("script.txt").decode("utf-8-sig")
        scenes = []
        for line in original.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            command = line.split(" ", 1)[0].upper()
            if command == "LOAD":
                scenes.append({"park": line[5:], "commands": [line], "holds": 0})
            elif scenes and scenes[-1]["holds"] < 2:
                if command in ("RESTART", "END"):
                    continue
                if command not in ("LOCATION", "ROTATE", "ZOOM", "SPEED", "WAIT"):
                    raise ValueError("Unsupported title command in the first two cameras: " + line)
                scenes[-1]["commands"].append("WAIT " + str(hold_ms) if command == "WAIT" else line)
                scenes[-1]["holds"] += command == "WAIT"
        if park_limit:
            scenes = scenes[:park_limit]
        if len(scenes) < 2 or any(s["holds"] != 2 for s in scenes):
            raise ValueError("Need at least two different parks, each with two original camera holds")
        commands = []
        metadata = []
        for loop in range(loops):
            for index, scene in enumerate(scenes):
                metadata.append({"loop": loop, "scene": index, "park": scene["park"],
                                 "loadCommand": len(commands), "commands": scene["commands"]})
                commands.extend(scene["commands"])
        commands.append("END")
        script = "# Isolated loading measurement; original scene/camera order, shorter holds.\n" + "\n".join(commands) + "\n"
        parks = {}
        with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_STORED) as output:
            output.writestr("script.txt", script)
            for name in dict.fromkeys(s["park"] for s in scenes):
                path = Path(name)
                if path.is_absolute() or path.name != name or path.suffix.lower() not in (".park", ".sv6", ".sc6"):
                    raise ValueError("Park must be a direct archive member: " + name)
                data = source.read(name)
                output.writestr(name, data)
                parks[name] = hashlib.sha256(data).hexdigest()
    return {"scenes": metadata, "parkSha256": parks, "commandCount": len(commands), "script": script,
            "loops": loops, "distinctParks": len(scenes), "holdMs": hold_ms,
            "allBundledParks": not park_limit}


def parse_report(text, fixture):
    # Console::WriteLine appends an ANSI colour reset even when stdout is a
    # redirected file. Remove SGR formatting only; retain marker contents and
    # sequence validation rather than accepting arbitrary trailing output.
    text = re.sub(r"\x1b\[[0-9;]*m", "", text)
    matches = re.findall(
        r"^Loading title: sequence=loading-probe command=(\d+) kind=(.*?) elapsed_ms=([0-9.]+)\s*$",
        text, re.MULTILINE)
    markers = [{"command": int(c), "kind": k, "elapsedMs": float(t)} for c, k, t in matches]
    if [m["command"] for m in markers] != list(range(fixture["commandCount"])):
        raise ValueError("Title command markers are incomplete, duplicated, or out of order")
    kinds = {"LOAD": "Load Park Command", "ZOOM": "Set Zoom Command",
             "LOCATION": "Set Location Command", "ROTATE": "Rotate View Command",
             "SPEED": "Set Speed Command", "WAIT": "Wait Command", "END": "End Command"}
    expected = [kinds[c.split(" ", 1)[0].upper()]
                for scene in fixture["scenes"] for c in scene["commands"]] + [kinds["END"]]
    if [m["kind"] for m in markers] != expected:
        raise ValueError("Title command kinds differ from the authored probe sequence")
    if any(b["elapsedMs"] < a["elapsedMs"] for a, b in zip(markers, markers[1:])):
        raise ValueError("Title command times are not monotonic")
    if markers[-1]["kind"] != "End Command":
        raise ValueError("Title playback did not reach its final END")
    scenes = []
    for index, item in enumerate(fixture["scenes"]):
        first = item["loadCommand"]
        end = fixture["scenes"][index + 1]["loadCommand"] if index + 1 < len(fixture["scenes"]) else len(markers) - 1
        waits = [m for m in markers[first:end] if m["kind"] == "Wait Command"]
        if len(waits) != 2:
            raise ValueError("A scene did not execute both camera holds")
        scenes.append({"loop": item["loop"], "scene": item["scene"], "park": item["park"],
                       "loadCommandWallMs": markers[first + 1]["elapsedMs"] - markers[first]["elapsedMs"],
                       "firstCameraToSecondCameraWallMs": waits[1]["elapsedMs"] - waits[0]["elapsedMs"],
                       "sceneWallMs": markers[end]["elapsedMs"] - markers[first]["elapsedMs"]})
    stages = [line for line in text.splitlines() if line.startswith(("Loading objects:", "Loading catalog:", "Loading atlas:"))]
    if not any(line.startswith("Loading objects:") for line in stages) or not any(line.startswith("Loading catalog:") for line in stages):
        raise ValueError("Missing object or native-world loading stage measurements")
    return {"markers": markers, "scenes": scenes, "stageLines": stages,
            "playbackWallMs": markers[-1]["elapsedMs"],
            "scope": "Application wall time; camera intervals include shortened holds, simulation and asynchronous rendering. Not scanout latency or GPU duration."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rct1-path", type=Path, required=True)
    parser.add_argument("--rct2-path", type=Path, required=True)
    parser.add_argument("--parks", type=int, default=2, help="First N real bundled parks; 0 selects the complete bundled loop")
    parser.add_argument("--loops", type=int, default=2)
    parser.add_argument("--hold-ms", type=int, default=1000)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--width", type=int, default=3840)
    parser.add_argument("--height", type=int, default=2160)
    parser.add_argument("--pipeline-cache-seed", type=Path)
    args = parser.parse_args()
    if args.parks == 1 or args.parks < 0 or not 2 <= args.loops <= 4 or not 500 <= args.hold_ms <= 5000:
        parser.error("Use >=2 parks (or 0 for all), 2..4 loops and 500..5000 ms holds")
    if not 60 <= args.timeout <= 1800 or not 640 <= args.width <= 4096 or not 480 <= args.height <= 2304:
        parser.error("Unsupported timeout or viewport dimensions")
    p = support()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {"status": "fail", "runnerSha256": p.sha256(Path(__file__)),
               "supportSha256": p.sha256(Path(__file__).with_name("run-render-performance.py"))}
    try:
        frozen, reference = p.qualify_reference()
        current_root, current = p.qualify_current(args.snapshot)
        source_receipt = json.loads((current_root / "receipt.json").read_text(encoding="utf-8-sig"))
        if "src/openrct2/TitleLoadingDiagnostic.h" not in source_receipt.get("sourceSha256", {}):
            raise ValueError("Refusing a build without the mandatory silent, hidden title-loading diagnostic")
        summary.update({"reference": reference, "current": current})
        runtime = output / "runtime"
        runtime.mkdir()
        for source in (frozen / "package").iterdir():
            if source.is_file() and source.suffix.lower() == ".dll":
                shutil.copy2(source, runtime / source.name)
        shutil.copy2(current_root / "bin/openrct2.exe", runtime / "openrct2.exe")
        data = output / "data"
        data.mkdir()
        for source in (frozen / "package/data").iterdir():
            if source.name == "shaders":
                continue
            if source.is_dir():
                p.junction(source, data / source.name)
            else:
                shutil.copy2(source, data / source.name)
        shader_dir = data / "shaders/vulkan"
        shader_dir.mkdir(parents=True)
        for name in current["artifactSha256"]:
            if name.endswith(".spv"):
                shutil.copy2(current_root / name, shader_dir / Path(name).name)
        profile = output / "profile"
        sequence_dir = profile / "sequence"
        sequence_dir.mkdir(parents=True)
        archive = frozen / "package/data/sequence/openrct2.parkseq"
        fixture = prepare_sequence(archive, sequence_dir / (SEQUENCE + ".parkseq"), args.parks, args.loops, args.hold_ms)
        summary["fixture"] = fixture
        summary["sourceSequenceSha256"] = p.sha256(archive)
        summary["probeSequenceSha256"] = p.sha256(sequence_dir / (SEQUENCE + ".parkseq"))
        games = {"rct1": args.rct1_path.resolve(strict=True), "rct2": args.rct2_path.resolve(strict=True)}
        summary["licensedAssets"] = {name: {"path": str(path), "files": p.file_inventory(path)} for name, path in games.items()}
        config = configparser.ConfigParser(interpolation=None)
        config["general"] = {"window_width": str(args.width), "window_height": str(args.height), "fullscreen_mode": "0",
                             "use_vsync": "true", "uncap_fps": "true", "multithreading": "true", "play_intro": "false",
                             "window_scale": "1.0", "infer_display_dpi": "false", "language": "en-US", "autosave": "5",
                             "last_version_check_time": "4102444800", "enable_hdr10_output": "false", "enable_light_fx": "false",
                             "rct1_path": p.quote_ini_string(games["rct1"].as_posix()),
                             "game_path": p.quote_ini_string(games["rct2"].as_posix())}
        config["interface"] = {"current_title_sequence": p.quote_ini_string(SEQUENCE), "random_title_sequence": "false"}
        stream = io.StringIO()
        config.write(stream)
        for destination in (profile / "config.ini", output / "config-input.ini"):
            destination.write_text(stream.getvalue(), encoding="utf-8")
        if args.pipeline_cache_seed:
            seed = args.pipeline_cache_seed.resolve(strict=True)
            files = sorted(seed.glob("*.bin"))
            if not files or any(pth.is_symlink() or not pth.is_file() or pth.stat().st_size > 64 * 1024 * 1024 + 48 for pth in files):
                raise ValueError("Invalid pipeline cache seed")
            target = profile / "vulkan-pipelines"
            target.mkdir()
            summary["pipelineCacheSeed"] = {pth.name: p.sha256(pth) for pth in files}
            for source in files:
                shutil.copy2(source, target / source.name)
                if p.sha256(target / source.name) != summary["pipelineCacheSeed"][source.name]:
                    raise ValueError("Pipeline seed changed while copying")
        command = [str(runtime / "openrct2.exe"), "--user-data-path", str(profile), "--openrct2-data-path", str(data),
                   "--rct1-data-path", str(games["rct1"]), "--rct2-data-path", str(games["rct2"])]
        env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
        summary["removedDiagnosticEnvironment"] = {key: value for key, value in env.items() if key.startswith("OPENRCT2_")}
        for name in list(env):
            if name.startswith("OPENRCT2_"):
                del env[name]
        env["OPENRCT2_LOADING_REPORT"] = "1"
        env["OPENRCT2_TITLE_LOADING_EXIT_AT_END"] = "1"
        summary["command"] = command
        summary["environmentOverrides"] = {key: env[key] for key in ("OPENRCT2_LOADING_REPORT", "OPENRCT2_TITLE_LOADING_EXIT_AT_END")}
        summary["graphicsEnvironment"] = {key: value for key, value in env.items()
                                          if key.startswith(("SDL_", "VK_", "__GL_", "DRI_", "MESA_"))}
        if env.get("SDL_VIDEODRIVER", "").lower() == "dummy":
            raise ValueError("Title measurement requires an ordinary native window")
        started = time.monotonic()
        timed_out = False
        with (output / "loading.log").open("wb") as log:
            process = subprocess.Popen(command, cwd=runtime, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                summary["exitCode"] = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()
                summary["exitCode"] = process.wait()
        summary["processWallSeconds"] = time.monotonic() - started
        text = (output / "loading.log").read_text(encoding="utf-8", errors="replace")
        summary["logSha256"] = p.sha256(output / "loading.log")
        if "Title loading diagnostic: hidden window, dummy audio" not in text or "Opened wasapi audio output" in text:
            raise RuntimeError("Silent, hidden diagnostic contract was not confirmed")
        if timed_out:
            raise RuntimeError("Title playback timed out; owned process terminated, no success claim")
        if summary["exitCode"] != 0 or re.search(r"failed with error|VK_ERROR_DEVICE_LOST|VUID-|Unable to load park", text):
            raise ValueError("Title process or graphics/loading validation failed")
        summary["measurements"] = parse_report(text, fixture)
        if p.sha256(runtime / "openrct2.exe") != current["artifactSha256"]["bin/openrct2.exe"]:
            raise ValueError("Runtime executable changed")
        for name, digest in current["artifactSha256"].items():
            if name.endswith(".spv") and p.sha256(shader_dir / Path(name).name) != digest:
                raise ValueError("Runtime shader changed: " + name)
        if any(p.file_inventory(path) != summary["licensedAssets"][name]["files"] for name, path in games.items()):
            raise ValueError("Licensed assets changed during playback")
        summary["status"] = "pass"
    except Exception as error:
        summary["error"] = str(error)
    p.write_json(output / "summary.json", summary)
    print(json.dumps({"status": summary["status"], "summary": str(output / "summary.json"), "error": summary.get("error")}))
    return 0 if summary["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
