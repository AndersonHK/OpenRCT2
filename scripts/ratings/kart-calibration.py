#!/usr/bin/env python3
"""Compare production scoring and experimental kart models on synthetic experiences.

Windows: first build Release x64, then run this script. All generated inputs,
native-adapter build products and reports stay in the supplied ignored output dir.
These are synthetic scoring probes, not a vehicle-physics or gameplay simulation.
Experimental models are confined to sample-vehicle-ratings.cpp; this script never
edits the game's profile, saves, object files or installed package.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
CHANNELS = ("local", "speed", "longitudinal", "vertical", "lateral", "airtime", "coupling", "model")
PROFILES = {
    "deployed": (900, 2700, 900, 900, 2700, 900, 900, 0),
    "less_speed": (300, 600, 6000, 300, 1500, 100, 0, 0),
    "driver": (100, 150, 20000, 100, 2000, 0, 0, 0),
    "kart_v1": (200, 600, 1000, 1000, 700, 1000, 0, 1),
    "kart_v2": (150, 500, 1200, 1200, 600, 1200, 0, 1),
    "sustained_v3": (150, 500, 1200, 1200, 600, 1200, 0, 2),
    "sustained_v4": (150, 500, 1200, 1200, 600, 1200, 0, 3),
}


def build_adapter(out):
    ns = "http://schemas.microsoft.com/developer/msbuild/2003"
    ET.register_namespace("", ns)
    tag = lambda name: "{" + ns + "}" + name
    tree = ET.parse(ROOT / "src/openrct2-cli/openrct2-cli.vcxproj")
    project = tree.getroot()
    for group in list(project):
        for item in list(group):
            if item.tag in (tag("ResourceCompile"), tag("ProjectReference")):
                group.remove(item)
            elif item.tag == tag("ClCompile") and "Include" in item.attrib:
                item.set("Include", str(ROOT / "scripts/ratings/sample-vehicle-ratings.cpp"))
    for item in project.iter():
        if item.tag == tag("Import") and "common.props" in item.get("Project", ""):
            item.set("Project", str(ROOT / "openrct2.common.props"))
        if item.tag == tag("SolutionDir"):
            item.text = str(ROOT) + "\\"
        if item.tag in (tag("ProjectName"), tag("RootNamespace"), tag("TargetName")):
            item.text = "sample-vehicle-ratings"
        if item.tag == tag("ProjectGuid"):
            item.text = "{B7B1654E-4450-49A6-B604-694AFF67BCB0}"
        if item.tag == tag("ProgramDatabaseFile"):
            item.text = "$(OutDir)sample-vehicle-ratings.pdb"
    group = ET.Element(tag("PropertyGroup"))
    for name, value in (("OutDir", str(out) + "\\"), ("IntDir", str(out / "int") + "\\")):
        ET.SubElement(group, tag(name)).text = value
    project.insert(len(project) - 1, group)
    path = out / "sample-vehicle-ratings.vcxproj"
    tree.write(path, encoding="utf-8", xml_declaration=True)
    with (out / "adapter-build.log").open("w") as log:
        subprocess.run([
            "msbuild", str(path), "/m:1", "/nr:false", "/p:Configuration=Release",
            "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207", "/p:EnableVulkan=true",
            "/p:IsSolutionBuild=true", "/verbosity:minimal",
        ], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    return out / "sample-vehicle-ratings.exe"


def segment(seconds, start, end=None, lateral=0, vertical=100, scenery=0, height=0, turn=False, down=False):
    """Speed in displayed mph; forces in hundredths of G; scenery/height are classifier points."""
    return dict(seconds=seconds, start=start, end=start if end is None else end, lateral=lateral,
                vertical=vertical, scenery=scenery, height=height, turn=turn, down=down)


def scenarios():
    flowing = [segment(2, 12, 24), segment(3, 24), segment(1, 24, 12),
               segment(4, 12, lateral=60, turn=True)] * 6
    hard = [segment(.25, 12, 24), segment(.25, 24, 12)] * 120
    bumps = [segment(2, 24), segment(.25, 24, vertical=0), segment(.25, 24, vertical=250)] * 24
    negative = [segment(2, 24), segment(.25, 24, vertical=-100), segment(.25, 24, vertical=350)] * 24
    gentle = [segment(2, 9, 18), segment(3, 18), segment(1, 18, 9),
              segment(4, 9, lateral=45, turn=True)] * 6
    stop_go = [segment(1, 0, 18), segment(1, 18), segment(.5, 18, 0), segment(1.5, 0)] * 15
    gentle_oscillation = [segment(.05, 18, 18.3), segment(.05, 18.3, 18)] * 600
    return {
        "straight_18mph": [segment(60, 18)],
        "straight_24mph": [segment(60, 24)],
        "straight_36mph": [segment(60, 36)],
        "flowing_circuit": flowing,
        "gentle_circuit": gentle,
        "stop_and_go": stop_go,
        "one_start_then_cruise": [segment(4, 0, 24), segment(56, 24)],
        "flowing_scenery": [dict(s, scenery=18) for s in flowing],
        "straight_scenery": [segment(60, 24, scenery=18)],
        "straight_scenery_spam": [segment(60, 24, scenery=90)],
        "straight_high": [segment(60, 24, height=16)],
        "constant_corner_0.6G": [segment(60, 18, lateral=60, turn=True)],
        "constant_corner_0.4G": [segment(60, 18, lateral=40, turn=True)],
        "constant_corner_1.2G": [segment(60, 24, lateral=120, turn=True)],
        "constant_corner_2.5G": [segment(60, 24, lateral=250, turn=True)],
        "alternating_brake_throttle": hard,
        "gentle_throttle_oscillation": gentle_oscillation,
        "repeated_airtime_landings": bumps,
        "repeated_negativeG_landings": negative,
        "straight_120seconds": [segment(120, 24)],
        "straight_600seconds": [segment(600, 24)],
    }


def ticks(segments):
    previous = round(segments[0]["start"] / 2.25 * 65536)
    history = 0
    for seg in segments:
        count = round(seg["seconds"] * 40)
        for i in range(count):
            mph = seg["start"] + (seg["end"] - seg["start"]) * (i + 1) / count
            velocity = round(mph / 2.25 * 65536)
            # Same integer rounding as CalculateLongitudinalG, using consecutive realized velocities.
            delta = (velocity - previous) * 100
            one_g = 642000 * 21 // 512
            longitudinal = (abs(delta) + one_g // 2) // one_g * (1 if delta >= 0 else -1)
            previous = velocity
            key = (velocity >> 16, seg["vertical"], seg["lateral"], longitudinal, seg["scenery"], seg["height"])
            yield key, seg["turn"], seg["down"], history
            history = next_history(history, longitudinal)


def next_history(history, longitudinal):
    delta = min(1000, max(-1000, longitudinal)) * 1024 - history
    # C++ integer division truncates toward zero.
    return history + (abs(delta) // 20) * (1 if delta >= 0 else -1)


def request_row(profile, tick, sample):
    key, turn, down, history = sample
    duration_tick = max(2400, tick + 1) if profile[-1] == 3 else 2400
    return profile + key + (int(turn), int(down), history, duration_tick)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".tmp/kart-calibration")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    adapter = out / "sample-vehicle-ratings.exe" if args.skip_build else build_adapter(out)
    experiences = {name: list(ticks(parts)) for name, parts in scenarios().items()}
    requests = {}
    for profile in PROFILES.values():
        for experience in experiences.values():
            for tick, sample in enumerate(experience):
                row = request_row(profile, tick, sample)
                requests.setdefault(row, len(requests))
    with (out / "inputs.txt").open("w") as stream:
        for row, index in requests.items():
            stream.write(" ".join(map(str, (index,) + row)) + "\n")
    subprocess.run([str(adapter), str(out / "inputs.txt"), str(out / "native-scores.txt")], check=True)
    scores = {}
    for line in (out / "native-scores.txt").read_text().splitlines():
        values = list(map(int, line.split()))
        scores[values[0]] = values[1:]
    if len(scores) != len(requests):
        raise RuntimeError("Native evaluator did not return every tick")
    for row, index in requests.items():
        if scores[index][-1] != next_history(row[-2], row[len(CHANNELS) + 3]):
            raise RuntimeError("Python history does not match the native integer filter")
    rows = []
    for profile_name, profile in PROFILES.items():
        for name, experience in experiences.items():
            totals = [0] * 15
            for tick, sample in enumerate(experience):
                values = scores[requests[request_row(profile, tick, sample)]][:15]
                totals = [a + b for a, b in zip(totals, values)]
            raw = totals[12:15]
            # Neutral ride-entry multiplier; output in displayed units, not hundredths.
            rating = [math.floor(math.sqrt(value / 1_000_000) * 100 + .5) / 100 for value in raw]
            rows.append(dict(profile=profile_name, scenario=name, seconds=len(experience)/40,
                             excitement=rating[0], intensity=rating[1], nausea=rating[2],
                             **{f"{source}_raw_{channel}": totals[s*3+c] for s, source in enumerate(
                                 ("speed", "forces", "context", "features", "total")) for c, channel in enumerate(("E", "I", "N"))}))
    with (out / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)
    candidate = {r["scenario"]: r for r in rows if r["profile"] == "sustained_v4"}
    checks = {}
    for control in ("straight_24mph", "straight_scenery_spam", "constant_corner_0.4G", "stop_and_go", "straight_600seconds"):
        checks["flowing_beats_" + control] = candidate["flowing_circuit"]["excitement"] > candidate[control]["excitement"]
    for hazard in ("constant_corner_2.5G", "alternating_brake_throttle", "repeated_airtime_landings", "repeated_negativeG_landings"):
        checks[hazard + "_less_fun_more_intense"] = (
            candidate[hazard]["excitement"] < candidate["straight_24mph"]["excitement"]
            and candidate[hazard]["intensity"] > candidate["straight_24mph"]["intensity"]
            and candidate[hazard]["nausea"] > candidate["straight_24mph"]["nausea"])
    checks["tiny_oscillation_not_rewarded"] = candidate["gentle_throttle_oscillation"]["excitement"] <= candidate["straight_18mph"]["excitement"] + .01
    checks["decoration_saturates"] = candidate["straight_scenery_spam"]["excitement"] == candidate["straight_scenery"]["excitement"]
    checks["height_no_excitement_bonus"] = candidate["straight_high"]["excitement"] == candidate["straight_24mph"]["excitement"]
    (out / "design-checks.json").write_text(json.dumps(checks, indent=2))
    if not all(checks.values()):
        raise RuntimeError("Candidate fails design targets: " + str([key for key, passed in checks.items() if not passed]))
    evidence = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in (
        ROOT / "src/openrct2/ride/RideRatings.cpp", ROOT / "src/openrct2/ride/RideData.h",
        ROOT / "src/openrct2/ride/Vehicle.cpp", ROOT / "bin/libopenrct2.lib",
        Path(__file__).resolve(), ROOT / "scripts/ratings/sample-vehicle-ratings.cpp")}
    (out / "inputs.json").write_text(json.dumps({"profiles": dict(zip(PROFILES, [dict(zip(CHANNELS, p)) for p in PROFILES.values()])),
        "scenarios": scenarios(), "adapter_sha256": hashlib.sha256(adapter.read_bytes()).hexdigest(), "sha256": evidence}, indent=2))
    print("Scenario                       " + "  ".join(f"{p:>17}" for p in PROFILES))
    for name in experiences:
        selected = [row for row in rows if row["scenario"] == name]
        print(f"{name:31}" + "  ".join(f"{r['excitement']:4.2f}/{r['intensity']:4.2f}/{r['nausea']:4.2f}".rjust(17) for r in selected))
    print(f"{len(requests)} distinct native ticks evaluated; {len(checks)} design checks passed. E/I/N; neutral object multipliers. Output: {out}")


if __name__ == "__main__":
    main()
