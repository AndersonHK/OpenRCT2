#!/usr/bin/env python3
"""Compare current-formula kart profiles at equal synthetic track length.

No production configuration is changed. The older adapter is used exclusively
with model=0, so all speed, force and context scoring calls production helpers.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HARNESS = runpy.run_path(str(Path(__file__).with_name("kart-calibration.py")))
PROFILES = {
    "current": (900, 2700, 900, 900, 2700, 900, 900),
    "conservative": (250, 1800, 3000, 900, 1500, 100, 900),
    "technical": (200, 1200, 3000, 900, 1000, 100, 500),
    "driver": (150, 1200, 5000, 900, 1000, 100, 300),
    "restrained": (200, 900, 3000, 500, 1000, 100, 500),
    "middle": (250, 1500, 3000, 900, 1500, 100, 700),
    "moderate_acceleration": (150, 1500, 2000, 900, 1250, 100, 900),
    "recommended": (150, 1500, 1000, 900, 1250, 100, 900),
    "high_acceleration": (150, 1500, 4000, 900, 1250, 100, 900),
}


def samples(segments):
    previous = round(segments[0]["start"] / 2.25 * 65536)
    for seg in segments:
        count = round(seg["seconds"] * 40)
        for i in range(count):
            mph = seg["start"] + (seg["end"] - seg["start"]) * (i + 1) / count
            velocity = round(mph / 2.25 * 65536)
            acceleration = velocity - previous
            numerator = acceleration * 100
            one_g = 642000 * 21 // 512
            longitudinal = (abs(numerator) + one_g // 2) // one_g * (1 if numerator >= 0 else -1)
            # Vehicle.cpp GetRealRideLengthDelta, with acceleration taken from the
            # synthetic realized velocity change; scale matches UnitConversion.cpp.
            legacy_distance = ((velocity + acceleration) >> 10) * 42
            distance_fixed = max(0, (legacy_distance * 158 + 213 // 2) // 213)
            previous = velocity
            key = (velocity >> 16, seg["vertical"], seg["lateral"], longitudinal,
                   seg["scenery"], seg["height"], int(seg["turn"]), int(seg["down"]))
            yield key, distance_fixed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".tmp/kart-length-calibration")
    parser.add_argument("--metres", type=float, default=600)
    parser.add_argument("--adapter", type=Path, help="Reuse a previously built native adapter")
    args = parser.parse_args()
    if args.metres <= 0:
        parser.error("--metres must be positive")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    adapter = args.adapter.resolve() if args.adapter else HARNESS["build_adapter"](out)
    scenarios = HARNESS["scenarios"]()
    segment = HARNESS["segment"]
    scenarios.update({
        "straight_lightness_0.5G": [segment(60, 24, vertical=50)],
        "straight_airtime_0G": [segment(60, 24, vertical=0)],
    })
    experiences = {name: list(samples(segments)) for name, segments in scenarios.items()}
    requests = {}
    for profile in PROFILES.values():
        for experience in experiences.values():
            for key, _ in experience:
                # Model 0: no prototype curves, filter, comfort gate or duration falloff.
                row = profile + (0,) + key + (0, 2400)
                requests.setdefault(row, len(requests))
    with (out / "inputs.txt").open("w") as stream:
        for row, index in requests.items():
            assert row[7] == 0, "Only current formulas are permitted in this study"
            stream.write(" ".join(map(str, (index,) + row)) + "\n")
    subprocess.run([str(adapter), str(out / "inputs.txt"), str(out / "native-scores.txt")], check=True)
    scores = {}
    for line in (out / "native-scores.txt").read_text().splitlines():
        values = list(map(int, line.split()))
        if len(values) != 17:
            raise RuntimeError("Unexpected adapter output shape")
        scores[values[0]] = values[1:16]
    if len(scores) != len(requests):
        raise RuntimeError("Missing native score rows")
    rows = []
    for profile_name, profile in PROFILES.items():
        for name, experience in experiences.items():
            totals = [0] * 15
            metres = sum(distance for _, distance in experience) / 65536
            if metres <= 0:
                raise RuntimeError("Cannot normalize a stationary-only trace by distance")
            factor = args.metres / metres
            for key, _ in experience:
                values = scores[requests[profile + (0,) + key + (0, 2400)]]
                totals = [a + b for a, b in zip(totals, values)]
            # Scale raw totals BEFORE the nonlinear conversion. This repeats the
            # trace's distance mix, without changing speed or acceleration magnitudes.
            rating = [math.floor(math.sqrt(value * factor / 1_000_000) * 100 + .5) / 100 for value in totals[12:15]]
            rows.append(dict(profile=profile_name, scenario=name, trace_metres=metres,
                             comparison_metres=args.metres, equivalent_seconds=len(experience) / 40 * factor,
                             excitement=rating[0], intensity=rating[1], nausea=rating[2],
                             **{f"{source}_raw_{channel}_per_metre": totals[s*3+c] / metres
                                for s, source in enumerate(("speed", "forces", "context", "features", "total"))
                                for c, channel in enumerate(("E", "I", "N"))}))
    with (out / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)
    by = {(r["profile"], r["scenario"]): r for r in rows}
    for profile in PROFILES:
        normal = by[profile, "straight_24mph"]
        for longer in ("straight_120seconds", "straight_600seconds"):
            for channel in ("excitement", "intensity", "nausea"):
                assert normal[channel] == by[profile, longer][channel], (profile, longer, channel)
    growth = []
    for scenario in ("straight_24mph", "flowing_circuit", "flowing_scenery"):
        reference = by["recommended", scenario]
        for metres in (600, 1200, 2400):
            values = {c: math.floor(math.sqrt(reference[f"total_raw_{short}_per_metre"] * metres / 1_000_000) * 100 + .5) / 100
                      for c, short in (("excitement", "E"), ("intensity", "I"), ("nausea", "N"))}
            growth.append(dict(scenario=scenario, metres=metres, **values))
    with (out / "length-growth.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=growth[0])
        writer.writeheader()
        writer.writerows(growth)
    paths = [Path(__file__).resolve(), Path(__file__).with_name("kart-calibration.py").resolve(),
             ROOT / "scripts/ratings/sample-vehicle-ratings.cpp", ROOT / "bin/libopenrct2.lib",
             ROOT / "src/openrct2/ride/RideRatings.cpp", ROOT / "src/openrct2/ride/Vehicle.cpp"]
    (out / "receipt.json").write_text(json.dumps(dict(
        metres=args.metres, profiles=PROFILES, scenarios=scenarios, native_requests=len(requests),
        model=0, neutral_object_multipliers=True, length_normalization_checks_passed=True,
        adapter_sha256=hashlib.sha256(adapter.read_bytes()).hexdigest(),
        sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}), indent=2))
    print(f"Equal {args.metres:g} synthetic track metres; production formulas; E/I/N")
    print("Scenario                       " + "  ".join(f"{p:>17}" for p in PROFILES))
    for name in scenarios:
        selected = [r for r in rows if r["scenario"] == name]
        print(f"{name:31}" + "  ".join(f"{r['excitement']:4.2f}/{r['intensity']:4.2f}/{r['nausea']:4.2f}".rjust(17) for r in selected))


if __name__ == "__main__":
    main()
