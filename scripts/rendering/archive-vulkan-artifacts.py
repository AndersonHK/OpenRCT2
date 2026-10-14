"""Preserve historical Vulkan payloads in verified content-addressed ZIPs.

Preparation never deletes originals. Apply the resulting index with
apply-vulkan-archive.ps1. Metadata/source/input files and selected live references
stay in place; only identified generated intermediates lack an archived payload.
"""
import argparse
import csv
import concurrent.futures
import itertools
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
NAMESPACE = ROOT / "obj/vulkan-parity"
PROTECTED = {
    "reference-9a092745f3", "ordering187-fixtures", "slopes191-withdrawn",
    "slopes191-candidate-01", "trinity190-paired-01", "corkscrew190-paired-01",
    "lift198-paired-01", "lift198-texas-paired-01", "save-load-crash193-evidence",
}
LIVE_BINS = {"ui-frozen-build-24", "ui-build-36", "screenshot-link207"}
RAW = {".bin", ".indexed", ".rgba", ".idx"}
SOURCE = {".c", ".cpp", ".h", ".hpp", ".hxx", ".inl", ".inc", ".py", ".ps1",
          ".patch", ".glsl", ".comp", ".vert", ".frag", ".geom", ".bat", ".cmd"}


def digest_file(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def contained(path):
    path = Path(os.path.abspath(path))
    if path == NAMESPACE or NAMESPACE not in path.parents:
        raise ValueError("Path must be inside the Vulkan artifact namespace")
    for parent in [path, *path.parents]:
        if parent.exists():
            info = parent.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("Reparse point in artifact path: " + str(parent))
        if parent == NAMESPACE:
            break
    return path


def classify(relative):
    parts = Path(relative).parts
    child, tail = parts[0], parts[1:]
    suffix = Path(relative).suffix.lower()
    if child in PROTECTED or child.startswith("guest-shop207") or ".git" in parts or "user-images" in parts:
        return None
    if child in LIVE_BINS and tail and tail[0] == "bin":
        return None
    if suffix in SOURCE or suffix in {".park", ".sc6", ".sv6", ".sc4", ".sv4", ".td6"}:
        return None
    if suffix == ".lib" and Path(relative).name.startswith("libopenrct2"):
        return "discard-compiler"
    if suffix in {".obj", ".pch", ".iobj", ".ipdb", ".ilk"} and any(p in {"obj", "int"} for p in tail):
        return "discard-compiler"
    if suffix in {".exe", ".pdb", ".dll"}:
        return "binaries"
    # Preserve extracted source/test fixtures in place. Their modifications may
    # not exist in Git. Do not equate a source/data tree with a generated profile.
    if any(p in {"source", "src", "test", "resources"} for p in tail):
        return None
    if suffix in RAW:
        return "raw"
    if any(p in {"data", "object", "objects"} for p in tail):
        return "runtime"
    if suffix == ".png":
        return "captures"
    return None


def walk_files(root, excluded):
    pending = [root]
    while pending:
        directory = pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                path = Path(entry.path)
                info = entry.stat(follow_symlinks=False)
                if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                    continue
                if stat.S_ISDIR(info.st_mode):
                    if path != excluded:
                        pending.append(path)
                elif stat.S_ISREG(info.st_mode):
                    yield path, info


def selected_records(scope, output, keep):
    for path, info in walk_files(scope, output):
        if path.name == "exclusive.lock":
            raise RuntimeError("Private build cache lock exists: " + str(path))
        if any(saved == path or saved in path.parents for saved in keep):
            continue
        kind = classify(path.relative_to(NAMESPACE))
        if kind is not None:
            if any(c in str(path) for c in "\t\r\n"):
                raise ValueError("Control characters in an artifact path")
            yield path, info, kind


def hashed_records(scope, output, keep):
    # Bounded batches avoid submitting hundreds of thousands of Futures while
    # overlapping Windows tiny-file latency. Archive writes remain serial.
    records = iter(selected_records(scope, output, keep))
    def hashed(record):
        path, info, kind = record
        return path, info, kind, digest_file(path)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        while True:
            batch = list(itertools.islice(records, 256))
            if not batch:
                break
            yield from executor.map(hashed, batch)


def verify_archive(path):
    """Inflate every entry and verify SHA256, not merely ZIP CRCs."""
    count = 0
    with zipfile.ZipFile(path) as archive:
        for item in archive.infolist():
            if item.filename != Path(item.filename).name or len(item.filename) != 64:
                raise ValueError("Invalid content-addressed archive entry")
            h = hashlib.sha256()
            with archive.open(item) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    h.update(block)
            if h.hexdigest() != item.filename:
                raise ValueError("Archived SHA256 mismatch: " + item.filename)
            count += 1
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": digest_file(path),
            "bytes": path.stat().st_size, "verifiedUniquePayloads": count}


def prepare(output, scope=None, keep=()):
    output = contained(output)
    scope = contained(scope) if scope is not None else NAMESPACE
    keep = tuple(contained(path) for path in keep)
    if scope == output or output in scope.parents:
        raise ValueError("Scope cannot select the archive output")
    output.mkdir(parents=True, exist_ok=False)
    if not NAMESPACE.is_dir():
        raise ValueError("Artifact namespace not found")
    protected_executables = []
    for base in [ROOT / "bin", Path("D:/Games/Independent/OpenRCT2Mod")]:
        for name in ("openrct2.exe", "openrct2-cli.exe"):
            path = base / name
            if path.is_file():
                protected_executables.append({"path": str(path), "sha256": digest_file(path)})
    state = {"schema": 1, "status": "preparing", "root": str(ROOT), "scope": str(scope), "archives": [],
             "protectedExecutables": protected_executables, "protectedRoots": sorted(PROTECTED), "explicitKeep": [str(path) for path in keep],
             "preservation": "All source/patch/save/log files and selected cases remain in place. Other captures/runtime/binaries are losslessly archived; generated compiler products are discarded."}
    state_path = output / "archive-plan.json"
    state_path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    archives = {kind: zipfile.ZipFile(output / (kind + ".zip"), "w", zipfile.ZIP_DEFLATED,
                                    compresslevel=1, allowZip64=True)
                for kind in ("raw", "runtime", "captures", "binaries")}
    seen = {kind: set() for kind in archives}
    totals = {}
    roots = {}
    count = 0
    last_notice = time.monotonic()
    try:
        with (output / "payloads.tsv").open("w", encoding="utf-8", newline="") as index:
            writer = csv.writer(index, delimiter="\t", lineterminator="\n")
            writer.writerow(["path", "bytes", "mtimeNs", "sha256", "kind", "archive", "entry"])
            for path, info, kind, digest in hashed_records(scope, output, keep):
                relative = path.relative_to(NAMESPACE)
                archive_name = ""
                if kind in archives:
                    archive_name = (output / (kind + ".zip")).relative_to(ROOT).as_posix()
                    if digest not in seen[kind]:
                        copied_hash = hashlib.sha256()
                        with path.open("rb") as original, archives[kind].open(digest, "w", force_zip64=True) as stored:
                            for block in iter(lambda: original.read(1024 * 1024), b""):
                                stored.write(block)
                                copied_hash.update(block)
                        if copied_hash.hexdigest() != digest:
                            raise ValueError("Original changed during archive copy")
                        seen[kind].add(digest)
                after = path.stat()
                if after.st_size != info.st_size or after.st_mtime_ns != info.st_mtime_ns:
                    raise ValueError("Original changed during preparation: " + str(path))
                writer.writerow([path.relative_to(ROOT).as_posix(), info.st_size, info.st_mtime_ns,
                                 digest, kind, archive_name, digest if archive_name else ""])
                count += 1
                value = totals.setdefault(kind, {"files": 0, "bytes": 0})
                value["files"] += 1
                value["bytes"] += info.st_size
                root_stats = roots.setdefault(relative.parts[0] if len(relative.parts) > 1 else "[root-files]", {"files": 0, "bytes": 0})
                root_stats["files"] += 1
                root_stats["bytes"] += info.st_size
                if time.monotonic() - last_notice > 15:
                    print(json.dumps({"preparedFiles": count, "selectedGiB": sum(v["bytes"] for v in totals.values()) / 2**30}), flush=True)
                    last_notice = time.monotonic()
    finally:
        for archive in archives.values():
            archive.close()
    print("Verifying every unique archived payload against its SHA256", flush=True)
    state["archives"] = [verify_archive(output / (kind + ".zip")) for kind in archives]
    state.update({"status": "verified", "totals": totals, "roots": roots,
                  "indexSha256": digest_file(output / "payloads.tsv"), "selectedFiles": count})
    state_path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": "verified", "selected": totals, "archiveBytes": sum(v["bytes"] for v in state["archives"])}), flush=True)


def restore(output, relative, destination):
    output = contained(output)
    destination = contained(destination)
    if destination.exists():
        raise ValueError("Restore destination already exists")
    index = output / "payloads.tsv"
    compressed = False
    if not index.exists():
        index = output / "payloads.tsv.gz"
        compressed = True
    opener = gzip.open if compressed else open
    with opener(index, "rt", encoding="utf-8", newline="") as stream:
        for entry in csv.DictReader(stream, delimiter="\t"):
            if entry["path"] != relative:
                continue
            if not entry["archive"]:
                raise ValueError("Generated compiler product was discarded, not archived")
            archive_path = contained(ROOT / entry["archive"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            contained(destination.parent)
            h = hashlib.sha256()
            with zipfile.ZipFile(archive_path) as archive, archive.open(entry["entry"]) as stored, destination.open("xb") as restored:
                for block in iter(lambda: stored.read(1024 * 1024), b""):
                    restored.write(block)
                    h.update(block)
            if h.hexdigest() != entry["sha256"] or destination.stat().st_size != int(entry["bytes"]):
                raise ValueError("Restored payload failed verification")
            os.utime(destination, ns=(int(entry["mtimeNs"]), int(entry["mtimeNs"])))
            return h.hexdigest()
    raise ValueError("Original path not found in archive index")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scope", type=Path, help="Restrict preparation to this task's artifact directory; defaults to the historical namespace")
    parser.add_argument("--keep", type=Path, action="append", default=[], help="Keep this selected live bin/reference subtree expanded (repeatable)")
    parser.add_argument("--restore", help="Original repository-relative payload path")
    parser.add_argument("--destination", type=Path, help="New contained scratch file for restoration")
    args = parser.parse_args()
    if args.restore:
        if args.destination is None:
            parser.error("--restore requires --destination")
        print(restore(args.output, args.restore, args.destination))
    else:
        prepare(args.output, args.scope, args.keep)


if __name__ == "__main__":
    main()
