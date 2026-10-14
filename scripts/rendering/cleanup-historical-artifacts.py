"""Prepare an explicit historical scratch deletion manifest; never deletes files.

Apply with cleanup-historical-artifacts.ps1. Only the 2026-10-14 audited families
are selected; receipts, patches, images, sources and chosen runnable references stay.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("retention", Path(__file__).with_name("artifact-retention.py"))
retention = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retention)

COMPILER = {".obj", ".pch", ".lib", ".pdb", ".iobj", ".ipdb", ".ilk", ".exe"}
BUILD_FAMILY = re.compile(r"(?:ui-build-\d+|ui-frozen-build-\d+|ui-cache-build-\d+|screenshot-link\d+)$")
KEEP_RUNNABLE = {"ui-frozen-build-24", "ui-build-36", "screenshot-link207"}


def terrain_passed(directory):
    try:
        cases = [c for c in ET.parse(directory / "tests.xml").getroot().iter("testcase")
                 if c.get("classname") == "VulkanRetainedTerrainDrawTest"]
        return len(cases) == 3 and all(c.get("status") == "run" and
            not any(c.find(tag) is not None for tag in ("failure", "error", "skipped")) for c in cases)
    except (OSError, ET.ParseError):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New evidence directory under obj/vulkan-parity")
    args = parser.parse_args()
    archive = retention.checked_output(args.output)
    archive.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[2]
    namespace = root / "obj/vulkan-parity"
    files, groups, preserved = [], {}, []
    for directory in sorted(namespace.iterdir()):
        name = directory.name
        if not directory.is_dir() or directory == archive:
            continue
        raw = None
        compiler = BUILD_FAMILY.fullmatch(name) is not None
        if re.fullmatch(r"run-\d+", name) and name != "run-62" and terrain_passed(directory):
            raw = directory / "terrain-draw-samples"
        elif re.fullmatch(r"terrain-draw-probe-\d+", name):
            summary = directory / "summary.json"
            if summary.is_file():
                proof = json.loads(summary.read_text(encoding="utf-8"))
                if proof.get("status") == "pass" and not proof.get("failures"):
                    raw = directory / "samples"
        if not compiler and raw is None and name != "incremental-ui-cache":
            continue
        retention.checked_output(directory)
        if raw is not None and raw.exists():
            retention.checked_output(raw)
        if name == "incremental-ui-cache" and list(directory.rglob("exclusive.lock")):
            raise RuntimeError("Private cache has a lock; do not clean it")
        # Preserve originals in place. Index and hash the compact top-level evidence
        # before deleting payloads, including cache identity/state and compiler probes.
        metadata = []
        for path in sorted(directory.iterdir()):
            if path.is_file() and not path.is_symlink() and path.suffix.lower() in {
                    ".json", ".xml", ".log", ".txt", ".patch", ".py", ".targets", ".vcxproj"}:
                metadata.append({"path": path.relative_to(root).as_posix(), "bytes": path.stat().st_size,
                                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        preserved.extend(metadata)
        selected = []
        for path in retention.regular_files(raw or directory):
            suffix = path.suffix.lower()
            if raw is not None:
                if path.name not in {"before.bin", "parents.bin", "commands.bin"}:
                    continue
                reason = "Repeated successful terrain capacity readback; run-62 retained in full"
            elif compiler:
                if suffix not in COMPILER:
                    continue
                if name in KEEP_RUNNABLE and "bin" in path.relative_to(directory).parts:
                    continue
                reason = "Superseded private compiler/link product; receipt and inputs remain in place"
            else:
                # Cache is explicitly marked never-evidence. Reset it before removing
                # only compiler products; source/patch/marker/state records remain.
                if suffix not in COMPILER:
                    continue
                reason = "Retired mutable UI compiler cache; state invalidated before deletion"
            info = path.stat()
            selected.append({"path": path.relative_to(root).as_posix(), "bytes": info.st_size,
                             "mtimeNs": info.st_mtime_ns, "reason": reason})
        if selected:
            files.extend(selected)
            groups[name] = {"files": len(selected), "bytes": sum(p["bytes"] for p in selected),
                            "retained": "Metadata, source/patches, shaders, images and all nonselected files stay at original paths"}
    # Preserve current build/deployment hashes, frozen oracle and original inputs.
    protected = []
    for base in [root / "bin", Path("D:/Games/Independent/OpenRCT2Mod")]:
        for name in ("openrct2.exe", "openrct2-cli.exe"):
            path = base / name
            if path.is_file():
                protected.append({"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    manifest = {"schema": 1, "root": str(root), "status": "planned", "files": files,
                "bytes": sum(p["bytes"] for p in files), "groups": groups,
                "preservedMetadata": preserved, "protectedExecutables": protected,
                "keep": ["run-62", "reference-9a092745f3", "ordering187-fixtures", "slopes191-withdrawn",
                         *sorted(KEEP_RUNNABLE)],
                "note": "Historical receipts remain unchanged. Removed raw artifacts cannot be reinspected; hashes in old receipts are historical."}
    destination = archive / "deletion-plan.json"
    destination.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(destination), "files": len(files), "GiB": manifest["bytes"] / 2**30,
                      "groups": len(groups)}, indent=2))


if __name__ == "__main__":
    main()
