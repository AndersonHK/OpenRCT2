"""Export ONE frozen-core terrain input and verify it in two fresh processes.

Only a fully matching frozen/current census publishes accepted=true. A failed
attempt retains its park, census, command/log and receipts in a new output tree;
it cannot overwrite an accepted input. This script never changes renderer code.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import os
import importlib.util

_HELPER = Path(__file__).with_name("run-screenshot-parity.py")
_spec = importlib.util.spec_from_file_location("giant_prepare_evidence", _HELPER)
base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(base)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree(path):
    return {item.relative_to(path).as_posix(): sha256(item)
            for item in sorted(path.rglob("*")) if item.is_file()}


def canonical(value):
    return (json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def read_build(path):
    path = path.resolve(strict=True)
    if path.is_dir():
        path /= "receipt.json"
    receipt = json.loads(path.read_text(encoding="utf-8"))
    if receipt["status"] != "pass" or receipt.get("sourceChangesDuringBuild") or receipt.get("dependencyChangesDuringBuild"):
        raise RuntimeError("Preparer build is unqualified: " + str(path))
    for name, digest in receipt["artifactSha256"].items():
        if sha256(path.parent / name) != digest:
            raise RuntimeError("Preparer build artifact changed: " + name)
    for name, digest in receipt.get("runtimeDllSha256", {}).items():
        if sha256(path.parent / "bin" / name) != digest:
            raise RuntimeError("Preparer runtime DLL changed: " + name)
    return path, receipt, path.parent / "bin/giant-screenshot-preparer.exe"


def differences(a, b, path="census"):
    if type(a) is not type(b):
        return [path + ": type differs"]
    if isinstance(a, dict):
        result = []
        for key in sorted(a.keys() | b.keys()):
            if key not in a or key not in b:
                result.append(path + "." + key + ": missing")
            else:
                result.extend(differences(a[key], b[key], path + "." + key))
        return result
    if isinstance(a, list):
        if len(a) != len(b):
            return [path + ": length differs (" + str(len(a)) + ", " + str(len(b)) + ")"]
        return [difference for index, (left, right) in enumerate(zip(a, b))
                for difference in differences(left, right, path + "[" + str(index) + "]")]
    return [] if a == b else [path + ": " + repr(a) + " != " + repr(b)]


def validate_nonuniform_census(census):
    if census.get("fixture") != "giant-seams-v1" or census.get("recipeVersion") != 1 or census.get("mapSize") != [96, 96]:
        raise RuntimeError("Giant recipe identity differs")
    if census.get("ticks") != 0 or census.get("assetState") != {"rct1CsgLoaded": True, "rct1Required": True}:
        raise RuntimeError("Giant recipe ticks/loaded CSG differs")
    materials = census["terrainMaterials"]
    identifiers = {"surfaces": ["rct2.terrain_surface.grass", "rct2.terrain_surface.sand"],
                   "edges": ["rct2.terrain_edge.rock", "rct2.terrain_edge.wood_red"]}
    for kind, names in identifiers.items():
        if [v["identifier"] for v in materials[kind]] != names or len({v["slot"] for v in materials[kind]}) != 2:
            raise RuntimeError("Giant terrain materials differ")
    rows = census["surfaces"]
    if len(rows) != 9216 or census.get("declaredElements") != 9216:
        raise RuntimeError("Giant surface population differs")
    for index, tile in enumerate(rows):
        x, y = index % 96, index // 96
        interior = 0 < x < 95 and 0 < y < 95
        tower = x in (3, 92) and y in (3, 92)
        z = (512 if tower else 16 * (1 + (x // 3 + 2 * (y // 5)) % 12)) if interior else 16
        slope = (0 if tower else ((x % 4) + 4 * (y % 4)) % 15) if interior else 0
        water = 128 if interior and z <= 96 and (x // 7 + y // 9) % 3 == 0 else 0
        surface = (x // 2 + y // 3) % 2 if interior else 0
        edge = (x + y // 2) % 2 if interior else 0
        expected = {"x": x, "y": y, "baseZ": z, "clearanceZ": z, "slope": slope,
                    "water": water, "grass": (x + 3 * y) % 7 if interior else 0,
                    "ownership": 0, "fences": 0, "surfaceSlot": materials["surfaces"][surface]["slot"],
                    "edgeSlot": materials["edges"][edge]["slot"]}
        if any(tile.get(k) != v for k, v in expected.items()):
            raise RuntimeError("Giant tile recipe differs: " + str(index))
        raw = tile.get("rawElementBytes", "")
        if len(raw) != 32 or any(v not in "0123456789abcdef" for v in raw):
            raise RuntimeError("Missing exact surface bytes")
    balloons = census["balloons"]
    if balloons.get("count") != 800 or len(balloons.get("records", [])) != 800:
        raise RuntimeError("Giant balloon population differs")
    for i, actual in enumerate(balloons["records"]):
        cell, overlap = i // 2, i % 2; gx, gy = cell % 20, cell // 20; popped = int(i % 7 == 0)
        expected = [i, (5 + 4 * gx) * 32 + 15 + 3 * overlap, (5 + 4 * gy) * 32 + 17 + 3 * overlap,
                    256 + (gx + 2 * gy) % 5 * 24 + 2 * overlap, i % (5 if popped else 8), popped, i % 3, i % 54]
        if len(actual) != 12 or actual[:8] != expected:
            raise RuntimeError("Giant balloon state differs: " + str(i))
    return {"tiles": 9216, "balloons": 800, "baseHeights": sorted({v["baseZ"] for v in rows}),
            "waterTiles": sum(v["water"] > 0 for v in rows), "rawElementBytesPerTile": 16}


def giant_extents(census):
    # Independent scalar translation of pinned frozen GetGiantViewport for this
    # surface-only map. Water and entities do not participate in its top bound.
    def project(x, y, z, r):
        if r == 1: x, y = y, -x
        elif r == 2: x, y = -x, -y
        elif r == 3: x, y = -y, x
        return y - x, (x + y) // 2 - z
    result = {}
    for r in range(4):
        corners = [project(x * 32 + 16, y * 32 + 16, 0, r) for x in (1, 94) for y in (1, 94)]
        left, right = min(p[0] for p in corners) - 32, max(p[0] for p in corners) + 32
        top = min(project(t["x"] * 32 + 16, t["y"] * 32 + 16, t["baseZ"], r)[1]
                  for t in census["surfaces"] if 1 <= t["x"] <= 94 and 1 <= t["y"] <= 94) - 64
        bottom = max(p[1] for p in corners)
        for z in range(4):
            result[f"r{r}z{z}"] = {"viewPosition": [left, top], "extent": [(right-left) >> z, (bottom-top) >> z]}
        w, h = result[f"r{r}z0"]["extent"]
        if min(w, h) <= 2048 or w % 2048 == 0 or h % 2048 == 0:
            raise RuntimeError("Fixture does not cross both tile axes with partial final tiles")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frozen-build", type=Path, required=True)
    parser.add_argument("--current-build", type=Path, required=True)
    parser.add_argument("--source-park", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--rct2", type=Path, required=True)
    parser.add_argument("--rct1", type=Path)
    parser.add_argument("--fixture", choices=("giant-seams-v1",), default="giant-seams-v1")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if not 0 < args.timeout <= 900:
        parser.error("Timeout must be bounded between 1 and 900 seconds")
    workspace = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists() or workspace not in output.parents:
        raise SystemExit("--output must be a new directory inside the workspace")
    output.mkdir(parents=True)
    manifest = {"schema": 1, "fixture": args.fixture, "accepted": False, "processes": [],
                "runnerSha256": sha256(Path(__file__)), "startedUnixTime": time.time()}
    evidence = base.Evidence()
    evidence.track(Path(__file__))
    evidence.track(_HELPER)
    try:
        frozen_path, frozen, frozen_exe = read_build(args.frozen_build)
        current_path, current, current_exe = read_build(args.current_build)
        for receipt_path, build_receipt, executable in ((frozen_path, frozen, frozen_exe), (current_path, current, current_exe)):
            evidence.track(receipt_path)
            evidence.track(receipt_path.parent / "build.log", build_receipt["buildLogSha256"])
            for name, digest in build_receipt["artifactSha256"].items():
                evidence.track(base.child(receipt_path.parent, name), digest)
            if evidence.tree(executable.parent, "*.dll", require_nonempty=False) != build_receipt["runtimeDllSha256"]:
                raise RuntimeError("Preparer DLL membership differs")
            for name, digest in build_receipt["sourceSha256"].items():
                prefix, relative = name.split("/", 1)
                source_base = Path(build_receipt["sourceRoot"]) if prefix == "source" else workspace
                evidence.track(base.child(source_base, relative), digest)
            for name, digest in build_receipt["dependencySha256"].items():
                evidence.track(base.child(Path(build_receipt["sourceRoot"]) / "lib/x64", name), digest)
            for name, digest in build_receipt["generatedProjectSha256"].items():
                evidence.track(base.child(receipt_path.parent, name), digest)
            for name, digest in build_receipt["builderSha256"].items():
                evidence.track(Path(name), digest)
        reference_path = workspace / "docs/vulkan-software-reference.json"
        evidence.track(reference_path)
        reference = json.loads(reference_path.read_text(encoding="utf-8"))
        proof = frozen.get("frozenReference")
        if not proof or proof["revision"] != reference["revision"] or proof["sourceArchiveSha256"] != reference["sourceArchive"]["sha256"]:
            raise RuntimeError("Prepare executable was not built from accepted frozen source")
        if current.get("frozenReference"):
            raise RuntimeError("Current verifier must be a separately built current-core executable")
        driver_key = "harness/test/terrain-parity/GiantScreenshotFixtureMain.cpp"
        if frozen["sourceSha256"].get(driver_key) != current["sourceSha256"].get(driver_key) or driver_key not in frozen["sourceSha256"]:
            raise RuntimeError("Frozen/current preparer sources differ")
        balloon_key = "harness/test/ui-parity/BalloonFixtureState.h"
        if balloon_key not in frozen["sourceSha256"] or frozen["sourceSha256"].get(balloon_key) != current["sourceSha256"].get(balloon_key):
            raise RuntimeError("Frozen/current balloon census source differs or is absent")
        if args.fixture == "giant-seams-v1":
            recipe_key = "harness/test/terrain-parity/GiantScreenshotRecipe.h"
            if not args.rct1:
                raise RuntimeError("Nonuniform terrain requires --rct1")
            if recipe_key not in frozen["sourceSha256"] or frozen["sourceSha256"].get(recipe_key) != current["sourceSha256"].get(recipe_key):
                raise RuntimeError("Frozen/current nonuniform recipe header differs or is absent")
            manifest["recipeHeaderSha256"] = frozen["sourceSha256"][recipe_key]
        archive = workspace / reference["localReference"] / "source.zip"
        accepted_manifest = workspace / reference["localReference"] / "manifest.json"
        if sha256(archive) != proof["sourceArchiveSha256"] or sha256(accepted_manifest) != reference["manifest"]["sha256"]:
            raise RuntimeError("Accepted archive/manifest changed")
        original = json.loads(accepted_manifest.read_text(encoding="utf-8"))
        source_park = args.source_park.resolve(strict=True)
        park_hash = sha256(source_park)
        known = {value["sha256"] for name, value in original["files"].items()
                 if name.endswith("/small_park_with_ferris_wheel.sv6")}
        if park_hash not in known:
            raise RuntimeError("Source park does not match accepted frozen small-park input")
        asset_roots = {"data": args.data.resolve(strict=True), "rct2": args.rct2.resolve(strict=True)}
        if args.rct1:
            asset_roots["rct1"] = args.rct1.resolve(strict=True)
        if any(path == output or output in path.parents or path in output.parents for path in asset_roots.values()):
            raise RuntimeError("Output must not overlap an asset input tree")
        asset_hashes = {name: evidence.tree(path) for name, path in asset_roots.items()}
        evidence.track(source_park)
        evidence.track(archive)
        evidence.track(accepted_manifest)
        for name, expected in original["files"].items():
            if name.startswith("package/data/"):
                relative = name.removeprefix("package/data/")
                if asset_hashes["data"].get(relative) != expected["sha256"]:
                    raise RuntimeError("Bundled asset differs from accepted reference: " + relative)
        if not any(name.lower().endswith("g1.dat") for name in asset_hashes["rct2"]):
            raise RuntimeError("RCT2 original G1 asset is required")
        for required in ("g2.dat", "fonts.dat"):
            if required not in asset_hashes["data"]:
                raise RuntimeError("Missing bundled graphics: " + required)
        manifest.update({"frozenReference": {"revision": reference["revision"], "objectsRevision": reference["objectsRevision"],
                                             "sourceArchiveSha256": proof["sourceArchiveSha256"]},
                         "builds": {"frozen": {"receipt": str(frozen_path), "sha256": sha256(frozen_path),
                                               "executableSha256": sha256(frozen_exe)},
                                    "current": {"receipt": str(current_path), "sha256": sha256(current_path),
                                                "executableSha256": sha256(current_exe)}},
                         "preparerSourceSha256": frozen["sourceSha256"][driver_key],
                         "sourcePark": {"path": str(source_park), "sha256": park_hash, "bytes": source_park.stat().st_size},
                         "assetRoots": {name: str(path) for name, path in asset_roots.items()},
                         "assetSha256": asset_hashes})
        park = output / (args.fixture + ".park")
        reports = []
        processes = [("frozen-prepare", frozen_exe, "prepare"), ("frozen-verify", frozen_exe, "verify"),
                     ("current-verify", current_exe, "verify")]
        if args.fixture == "giant-seams-v1":
            processes.insert(2, ("frozen-verify-repeat", frozen_exe, "verify"))
        for name, exe, mode in processes:
            profile = output / (name + "-profile")
            profile.mkdir()
            census_path = output / (name + ".json")
            command = [str(exe), "--fixture", args.fixture, "--mode", mode, "--park", str(source_park if mode == "prepare" else park),
                       "--profile", str(profile), "--census", str(census_path)]
            for key, value in asset_roots.items():
                command.extend(["--" + key, str(value)])
            if mode == "prepare":
                command.extend(["--output", str(park)])
            process = {"name": name, "command": command, "startedUnixTime": time.time(),
                       "initialProfileSha256": tree(profile)}
            manifest["processes"].append(process)
            log_path = output / (name + ".log")
            environment = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()
                           if not key.upper().startswith(("VK_", "OPENRCT2_"))}
            process["exitCode"] = None
            try:
                with log_path.open("wb") as log:
                    result = subprocess.run(command, cwd=workspace, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
                process["exitCode"] = result.returncode
            except subprocess.TimeoutExpired:
                process["timedOut"] = True
                raise
            finally:
                process.update({"endedUnixTime": time.time(), "finalProfileSha256": tree(profile)})
                if log_path.is_file(): process["logSha256"] = sha256(log_path)
            if result.returncode != 0:
                raise RuntimeError(name + " failed; see its log")
            report = json.loads(census_path.read_text(encoding="utf-8"))
            if args.fixture == "giant-seams-v1":
                process["nonuniformCoverage"] = validate_nonuniform_census(report["census"])
            census_bytes = canonical(report["census"])
            canonical_path = output / (name + ".canonical.json")
            canonical_path.write_bytes(census_bytes)
            process.update({"reportSha256": sha256(census_path), "canonicalCensusSha256": sha256(canonical_path)})
            object_sources = []
            for item in report["objectSources"]:
                path = Path(item["path"]).resolve(strict=True)
                root_name = next((key for key, root in asset_roots.items() if root in path.parents), None)
                if root_name is None:
                    raise RuntimeError("Loaded object source is outside fully hashed asset roots: " + str(path))
                relative = path.relative_to(asset_roots[root_name]).as_posix()
                if relative not in asset_hashes[root_name]:
                    raise RuntimeError("Loaded object source is absent from asset receipt: " + str(path))
                object_sources.append({**item, "assetRoot": root_name, "relative": relative,
                                       "sha256": asset_hashes[root_name][relative]})
            process["loadedObjectSources"] = object_sources
            reports.append(report)
            if mode == "prepare":
                manifest["park"] = {"path": str(park), "sha256": sha256(park), "bytes": park.stat().st_size,
                                    "formatVersion": report["formatVersion"], "compressionLevel": report["compressionLevel"],
                                    "exportTimeInterval": [process["startedUnixTime"], process["endedUnixTime"]]}
            elif sha256(park) != manifest["park"]["sha256"]:
                raise RuntimeError("Verification modified the accepted park bytes")
        mismatches = {name: differences(reports[0]["census"], report["census"])
                      for (name, _, _), report in zip(processes[1:], reports[1:])}
        manifest["censusDifferences"] = mismatches
        if any(mismatches.values()):
            raise RuntimeError("Reload census differs; input remains unqualified")
        after_assets = {name: tree(path) for name, path in asset_roots.items()}
        if after_assets != asset_hashes or sha256(source_park) != park_hash:
            raise RuntimeError("Immutable input assets changed during preparation")
        for path, receipt, exe in ((frozen_path, frozen, frozen_exe), (current_path, current, current_exe)):
            if sha256(exe) != receipt["artifactSha256"]["bin/giant-screenshot-preparer.exe"]:
                raise RuntimeError("Executable changed during preparation")
        if sha256(frozen_path) != manifest["builds"]["frozen"]["sha256"] or sha256(current_path) != manifest["builds"]["current"]["sha256"]:
            raise RuntimeError("Build receipt changed during preparation")
        manifest["giantCameras"] = giant_extents(reports[0]["census"])
        manifest["accepted"] = True
        manifest["limits"] = ("Pinned world input only; no renderer parity claim. Intransient objects are compared as "
                              "environment inputs, not claimed as park-persisted slots. Map animation storage has no public count accessor.")
    except Exception as error:
        manifest["failure"] = str(error)
    finally:
        try:
            audit = evidence.audit()
        except Exception as error:
            audit = ["Input audit failed: " + str(error)]
        manifest["inputAuditFailures"] = audit
        manifest["inputSha256"] = evidence.files
        if audit:
            manifest["accepted"] = False
            manifest["failure"] = "Pinned preparation inputs changed: " + "; ".join(audit)
        manifest["endedUnixTime"] = time.time()
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"accepted": manifest["accepted"], "failure": manifest.get("failure"),
                      "manifest": str(output / "manifest.json")}))
    raise SystemExit(0 if manifest["accepted"] else 1)


if __name__ == "__main__":
    main()
