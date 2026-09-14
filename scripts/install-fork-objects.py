#!/usr/bin/env python3
"""Install tracked object sources from the pinned companion fork checkout."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path


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
    paths = git(source, "ls-files", "-z", "--", "objects").decode("utf-8").split("\0")
    destination = destination.resolve()
    copies = []
    for name in filter(None, paths):
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
    print(f"Objects fork {revision}: {len(paths) - 1} tracked files verified; "
          f"{len(copies)} copied to {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    try:
        install(args.manifest.resolve(), args.source, args.destination)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Cannot install fork objects: {error}\n")
