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
    return path, receipt, path.parent / "bin/native-terrain-preparer.exe"


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
    if census.get("fixture") != "nonuniform-terrain-v1" or census.get("recipeVersion") != 1 or census.get("mapSize") != [32, 32]:
        raise RuntimeError("Nonuniform fixture identity/map extent mismatch")
    if census.get("assetState") != {"rct1CsgLoaded": True, "rct1Required": True}:
        raise RuntimeError("Nonuniform fixture did not load required CSG assets")
    materials = census.get("terrainMaterials", {})
    expected = {"surfaces": ["rct2.terrain_surface.grass", "rct2.terrain_surface.sand"],
                "edges": ["rct2.terrain_edge.rock", "rct2.terrain_edge.wood_red"]}
    for kind, identifiers in expected.items():
        records = materials.get(kind, [])
        if [r["identifier"] for r in records] != identifiers or len({r["slot"] for r in records}) != 2:
            raise RuntimeError("Nonuniform terrain object census mismatch: " + kind)
    rows = census.get("surfaces", [])
    if len(rows) != 1024:
        raise RuntimeError("Nonuniform terrain must retain all 1024 declared surfaces")
    for index, tile in enumerate(rows):
        x, y = index % 32, index // 32
        central = 8 <= x < 24 and 8 <= y < 24
        z = 16 * (1 + ((x // 4 + 2 * (y // 4)) % 4)) if central else 16
        slope = ((x % 4) + 4 * (y % 4)) % 15 if central else 0
        grass = (x + 3 * y) % 7 if central else 0
        surface = (x // 2 + y // 2) % 2 if central else 0
        edge = (x + y // 2) % 2 if central else 0
        wanted = {"x": x, "y": y, "baseZ": z, "clearanceZ": z, "slope": slope, "grass": grass,
                  "water": 0, "ownership": 0, "fences": 0,
                  "surfaceSlot": materials["surfaces"][surface]["slot"],
                  "edgeSlot": materials["edges"][edge]["slot"]}
        if any(tile.get(key) != value for key, value in wanted.items()):
            raise RuntimeError("Nonuniform raw terrain fields differ at tile " + str(index))
        raw = tile.get("rawElementBytes", "")
        if len(raw) != 32 or any(c not in "0123456789abcdef" for c in raw):
            raise RuntimeError("Missing exact 16-byte surface element census at tile " + str(index))
    if any(census["entityTypeCounts"]) or census["nonemptySpatialBuckets"] != 0:
        raise RuntimeError("Nonuniform terrain must remain entirely entity-free")
    return {"tiles": len(rows), "baseHeights": sorted({t["baseZ"] for t in rows}),
            "slopes": sorted({t["slope"] for t in rows}), "grassLengths": sorted({t["grass"] for t in rows}),
            "surfaceSlots": sorted({t["surfaceSlot"] for t in rows}), "edgeSlots": sorted({t["edgeSlot"] for t in rows}),
            "rawElementBytesPerTile": 16}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frozen-build", type=Path, required=True)
    parser.add_argument("--current-build", type=Path, required=True)
    parser.add_argument("--source-park", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--rct2", type=Path, required=True)
    parser.add_argument("--rct1", type=Path)
    parser.add_argument("--fixture", choices=("native-terrain-v1", "balloon-static-v1", "balloon-static-v2", "nonuniform-terrain-v1"), default="native-terrain-v1")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists() or workspace not in output.parents:
        raise SystemExit("--output must be a new directory inside the workspace")
    output.mkdir(parents=True)
    manifest = {"schema": 1, "fixture": args.fixture, "accepted": False, "processes": [],
                "runnerSha256": sha256(Path(__file__)), "startedUnixTime": time.time()}
    try:
        frozen_path, frozen, frozen_exe = read_build(args.frozen_build)
        current_path, current, current_exe = read_build(args.current_build)
        reference_path = workspace / "docs/vulkan-software-reference.json"
        reference = json.loads(reference_path.read_text(encoding="utf-8"))
        proof = frozen.get("frozenReference")
        if not proof or proof["revision"] != reference["revision"] or proof["sourceArchiveSha256"] != reference["sourceArchive"]["sha256"]:
            raise RuntimeError("Prepare executable was not built from accepted frozen source")
        if current.get("frozenReference"):
            raise RuntimeError("Current verifier must be a separately built current-core executable")
        driver_key = "harness/test/terrain-parity/NativeTerrainFixtureMain.cpp"
        if frozen["sourceSha256"].get(driver_key) != current["sourceSha256"].get(driver_key) or driver_key not in frozen["sourceSha256"]:
            raise RuntimeError("Frozen/current preparer sources differ")
        balloon_key = "harness/test/ui-parity/BalloonFixtureState.h"
        if balloon_key not in frozen["sourceSha256"] or frozen["sourceSha256"].get(balloon_key) != current["sourceSha256"].get(balloon_key):
            raise RuntimeError("Frozen/current balloon census source differs or is absent")
        if args.fixture == "nonuniform-terrain-v1":
            recipe_key = "harness/test/terrain-parity/NonuniformTerrainRecipe.h"
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
        asset_hashes = {name: tree(path) for name, path in asset_roots.items()}
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
        if args.fixture == "nonuniform-terrain-v1":
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
            with log_path.open("wb") as log:
                result = subprocess.run(command, cwd=workspace, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            process.update({"exitCode": result.returncode, "endedUnixTime": time.time(),
                            "logSha256": sha256(log_path), "finalProfileSha256": tree(profile)})
            if result.returncode != 0:
                raise RuntimeError(name + " failed; see its log")
            report = json.loads(census_path.read_text(encoding="utf-8"))
            if args.fixture == "nonuniform-terrain-v1":
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
            if sha256(exe) != receipt["artifactSha256"]["bin/native-terrain-preparer.exe"]:
                raise RuntimeError("Executable changed during preparation")
        if sha256(frozen_path) != manifest["builds"]["frozen"]["sha256"] or sha256(current_path) != manifest["builds"]["current"]["sha256"]:
            raise RuntimeError("Build receipt changed during preparation")
        manifest["accepted"] = True
        manifest["limits"] = ("Pinned world input only; no renderer parity claim. Intransient objects are compared as "
                              "environment inputs, not claimed as park-persisted slots. Map animation storage has no public count accessor.")
    except Exception as error:
        manifest["failure"] = str(error)
    finally:
        manifest["endedUnixTime"] = time.time()
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"accepted": manifest["accepted"], "failure": manifest.get("failure"),
                      "manifest": str(output / "manifest.json")}))
    raise SystemExit(0 if manifest["accepted"] else 1)


if __name__ == "__main__":
    main()
