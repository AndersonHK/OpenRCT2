#!/usr/bin/env python3
"""Install tracked object sources from the pinned companion fork checkout."""
import argparse
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

PROVENANCE_FILE = ".fork-objects.json"


def object_id(path):
    if path.suffix.lower() == ".parkobj":
        with zipfile.ZipFile(path) as archive:
            definition = json.loads(archive.read("object.json"))
    else:
        definition = json.loads(path.read_text(encoding="utf-8-sig"))
    return definition.get("id") if isinstance(definition, dict) else None


def check_destination(source, destination, paths):
    """Reject competing definitions and stale managed files before any writes."""
    expected = {}
    relative_paths = {Path(name).relative_to("objects").as_posix() for name in paths}
    for name in paths:
        if Path(name).suffix.lower() not in (".json", ".parkobj"):
            continue
        identifier = object_id(source / name)
        if identifier:
            relative = Path(name).relative_to("objects").as_posix()
            if identifier in expected:
                raise ValueError(f"Duplicate object ID in companion source: {identifier}")
            expected[identifier] = relative

    marker = destination / PROVENANCE_FILE
    if marker.resolve().parent != destination:
        raise ValueError(f"Object provenance marker escapes install directory: {marker}")
    if marker.exists():
        previous = json.loads(marker.read_text(encoding="utf-8"))
        for name in previous["files"]:
            relative = Path(name)
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"Invalid object provenance path: {name}")
            if name not in relative_paths and (destination / relative).exists():
                raise ValueError(f"Stale previously installed fork asset: {destination / relative}. "
                                 "Review and move it aside, or use a clean destination.")

    visited = set()
    for directory, directories, files in os.walk(destination, followlinks=True):
        resolved = Path(directory).resolve()
        if resolved in visited:
            directories[:] = []
            continue
        visited.add(resolved)
        for name in files:
            path = Path(directory) / name
            relative = path.relative_to(destination).as_posix()
            if relative in relative_paths or path.suffix.lower() not in (".json", ".parkobj"):
                continue
            if name == PROVENANCE_FILE:
                continue
            identifier = object_id(path)
            if identifier in expected:
                raise ValueError(f"Competing object {identifier}: {path} conflicts with "
                                 f"{expected[identifier]}. Review and move it aside, or use a clean destination.")
    return sorted(relative_paths)


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def install(manifest, source, destination):
    spec = json.loads(manifest.read_text(encoding="utf-8"))["objects"]
    source = (source or manifest.parent / spec["checkout"]).resolve()
    revision = spec["revision"]
    if git(source, "rev-parse", "HEAD").decode().strip() != revision:
        raise ValueError(f"Objects checkout must be at {revision}: {source}. "
                         f"Use the companion fork {spec['repository']}.")
    if git(source, "diff", "HEAD", "--", "objects"):
        raise ValueError(f"Tracked objects have uncommitted changes: {source}")
    paths = list(filter(None, git(source, "ls-files", "-z", "--", "objects").decode("utf-8").split("\0")))
    destination = destination.resolve()
    installed_paths = check_destination(source, destination, paths)
    copies = []
    for name in paths:
        relative = Path(name).relative_to("objects")
        src = source / name
        dst = destination / relative
        resolved = dst.resolve()
        # Development installs may already link individual families to this checkout.
        if resolved == src.resolve():
            continue
        if destination not in resolved.parents:
            raise ValueError(f"Object destination escapes install directory: {dst}")
        if not dst.exists() or src.read_bytes() != dst.read_bytes():
            copies.append((src, dst))
    for src, dst in copies:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
    if destination != (source / "objects").resolve():
        destination.mkdir(parents=True, exist_ok=True)
        marker = destination / PROVENANCE_FILE
        marker.write_text(json.dumps({"revision": revision, "files": installed_paths}, indent=2) + "\n",
                          encoding="utf-8")
    print(f"Objects fork {revision}: {len(paths)} tracked files verified; "
          f"{len(copies)} copied to {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    try:
        install(args.manifest.resolve(), args.source, args.destination)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Cannot install fork objects: {error}\n")
