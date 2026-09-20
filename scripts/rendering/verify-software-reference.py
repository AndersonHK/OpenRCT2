"""Verify the immutable software package and guard its in-tree parity oracle."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


PROTECTED = (
    "src/openrct2-ui/drawing/engines/HardwareDisplayDrawingEngine.cpp",
    "src/openrct2/drawing/X8DrawingEngine.cpp",
    "src/openrct2/drawing/X8DrawingEngine.h",
    "src/openrct2/drawing/Drawing.Sprite.cpp",
    "src/openrct2/drawing/Drawing.Sprite.BMP.cpp",
    "src/openrct2/drawing/Drawing.Sprite.RLE.cpp",
    "src/openrct2/drawing/AVX2Drawing.cpp",
    "src/openrct2/drawing/SSE41Drawing.cpp",
    "src/openrct2/drawing/Line.cpp",
    "src/openrct2/drawing/Rectangle.cpp",
)


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", action="store_true", help="Verify every frozen file, not just manifest/archive")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    receipt = json.loads((root / "docs/vulkan-software-reference.json").read_text(encoding="utf-8"))
    reference = root / receipt["localReference"]
    failures = []
    for relative, expected in (("manifest.json", receipt["manifest"]), ("source.zip", receipt["sourceArchive"])):
        path = reference / relative
        if not path.is_file() or sha256(path) != expected["sha256"]:
            failures.append("Frozen reference changed/missing: " + relative)
    if failures:
        raise SystemExit("\n".join(failures))
    manifest = json.loads((reference / "manifest.json").read_text(encoding="utf-8"))
    if args.package:
        for relative, expected in manifest["files"].items():
            path = (reference / relative).resolve()
            if reference.resolve() not in path.parents or not path.is_file() or sha256(path) != expected["sha256"]:
                failures.append("Frozen file changed/missing: " + relative)
    for relative in PROTECTED:
        baseline = subprocess.check_output(["git", "show", receipt["revision"] + ":" + relative], cwd=root)
        path = root / relative
        # Working-tree CRLF conversion is not a renderer behavior change.
        if not path.is_file() or path.read_bytes().replace(b"\r\n", b"\n") != baseline.replace(b"\r\n", b"\n"):
            failures.append("Software parity oracle changed: " + relative)
    if failures:
        raise SystemExit("\n".join(failures))
    print(json.dumps({"revision": receipt["revision"], "protectedSources": len(PROTECTED),
                      "packageFilesVerified": len(manifest["files"]) if args.package else 0, "result": "pass"}))


if __name__ == "__main__":
    main()
