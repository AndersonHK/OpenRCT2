"""Freeze an accepted local package before rebuilding; never reads a user profile.

The detailed manifest and private game assets stay in ignored local scratch. A small
receipt can be committed. Existing reference directories are never overwritten.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def record(path):
    return {"bytes": path.stat().st_size, "sha256": digest(path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--package-manifest", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    revision = subprocess.check_output(
        ["git", "rev-parse", args.revision + "^{commit}"], cwd=root, text=True
    ).strip()
    package = args.package.resolve(strict=True)
    original = json.loads(args.package_manifest.read_text(encoding="utf-8"))
    output = args.output.resolve()
    if output.exists() or args.receipt.exists():
        raise SystemExit("Refusing to replace an existing reference or receipt")
    # The packaging receipt records the exact accepted executables/assets. Verify
    # it before copying, then verify the independent copy before issuing a receipt.
    for relative, expected in original["files"].items():
        source = (package / relative).resolve(strict=True)
        if package not in source.parents:
            raise SystemExit("Package manifest escapes its root: " + relative)
        if digest(source) != expected["sha256"]:
            raise SystemExit("Accepted package changed: " + relative)
    source_revision = original["commit"]
    # Documentation and regression-only commits may follow the deployed receipt.
    subprocess.run(
        ["git", "diff", "--exit-code", source_revision, revision, "--", "src", "data"],
        cwd=root, check=True,
    )
    output.mkdir(parents=True)
    shutil.copytree(package, output / "package", symlinks=False)
    shutil.copytree(root / "test/tests/testdata", output / "testdata", symlinks=False)
    archive = output / "source.zip"
    subprocess.run(
        ["git", "archive", "--format=zip", "--output=" + str(archive), revision],
        cwd=root, check=True,
    )
    copied = {}
    for path in sorted(output.rglob("*")):
        if path.is_file():
            copied[path.relative_to(output).as_posix()] = record(path)
    for relative, expected in original["files"].items():
        if copied["package/" + relative]["sha256"] != expected["sha256"]:
            raise SystemExit("Reference copy differs: " + relative)
    manifest = {
        "schema": 1, "revision": revision, "deployedSource": source_revision,
        "objectsRevision": original["objectsRevision"], "files": copied,
        "profile": "Not copied; capture runs must use an isolated, explicitly recorded profile.",
        "originalGameAssets": "Not included; record hashes when licensed assets are selected for fixtures.",
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    receipt = {
        "schema": 1, "revision": revision, "deployedSource": source_revision,
        "objectsRevision": original["objectsRevision"],
        "localReference": output.relative_to(root).as_posix(),
        "manifest": record(manifest_path), "sourceArchive": record(archive),
        "fileCount": len(copied),
        "executables": {key: value for key, value in copied.items() if key.endswith(".exe")},
        "status": "Frozen package and source verified; scene goldens and original-game asset receipts still pending.",
    }
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
