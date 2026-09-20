"""Capture all small-park cameras using the isolated frozen-source CLI oracle.

Requires Pillow. This is indexed auxiliary viewport evidence, not main UI capture.
Optional comparison checks every byte against both outputs of a parity run.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from PIL import Image


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rct2-path", type=Path, required=True)
    parser.add_argument("--rct1-path", type=Path, required=True)
    parser.add_argument("--compare-run", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = args.oracle_source.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        raise SystemExit("Refusing to overwrite oracle evidence: " + str(output))
    subprocess.run([sys.executable, str(root / "scripts/rendering/verify-software-reference.py")], check=True)
    provenance_path = source / "oracle-source-receipt.json"
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    for relative, expected in provenance["originalSourceSha256"].items():
        if relative == provenance["instrumentedFile"]:
            expected = provenance["instrumentedSha256"]
        if sha256(source / relative) != expected:
            raise SystemExit("Frozen-source oracle changed after extraction: " + relative)
    reference_receipt = json.loads((root / "docs/vulkan-software-reference.json").read_text(encoding="utf-8"))
    reference = root / reference_receipt["localReference"]
    output.mkdir(parents=True)
    executable = source / "bin/openrct2-cli.exe"
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
    env.update({"OPENRCT2_ORACLE_USER_PATH": str(output / "profile"),
                "OPENRCT2_ORACLE_DATA_PATH": str(reference / "package/data"),
                "OPENRCT2_ORACLE_RCT1_PATH": str(args.rct1_path.resolve(strict=True)),
                "OPENRCT2_ORACLE_RCT2_PATH": str(args.rct2_path.resolve(strict=True))})
    comparisons = []
    for zoom in (0, 1):
        for rotation in range(4):
            fixture = f"SmallParkTransparent_R{rotation}_Z{zoom}"
            folder = output / fixture
            folder.mkdir()
            png = folder / "frozen.png"
            command = [str(executable), "screenshot", str(reference / "testdata/parks/small_park_with_ferris_wheel.sv6"),
                       str(png), "640", "480", "336", "112", "144", str(zoom), str(rotation), "--transparent"]
            with (folder / "capture.log").open("w", encoding="utf-8") as log:
                subprocess.run(command, cwd=source / "bin", env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            with Image.open(png) as capture:
                if capture.mode != "P" or capture.size != (640, 480):
                    raise SystemExit("Unexpected frozen screenshot format")
                indexed = capture.tobytes()
                # PNG's transparent-index export contract differs from opaque window presentation.
                # Expand the exact captured palette RGB with opaque alpha; do not mask any pixels.
                palette = capture.getpalette()
                rgba = bytes(value for index in indexed for value in (*palette[index * 3:index * 3 + 3], 255))
            (folder / "indexed.bin").write_bytes(indexed)
            (folder / "opaque-rgba.bin").write_bytes(rgba)
            case = {"fixture": fixture, "command": command, "pngSha256": sha256(png),
                    "indexedSha256": hashlib.sha256(indexed).hexdigest(),
                    "opaqueRgbaSha256": hashlib.sha256(rgba).hexdigest()}
            if args.compare_run:
                differences = {}
                for layer, expected, stride in (("indexed", indexed, 1), ("rgba", rgba, 4)):
                    for renderer in ("software", "vulkan"):
                        actual = (args.compare_run / "samples" / fixture / layer / (renderer + ".bin")).read_bytes()
                        if len(actual) != len(expected):
                            raise SystemExit("Unexpected parity sample extent: " + fixture)
                        differences[layer + "/" + renderer] = sum(
                            actual[i:i + stride] != expected[i:i + stride] for i in range(0, len(actual), stride))
                case["differingPixels"] = differences
            comparisons.append(case)
    report = {"schema": 1, "referenceRevision": provenance["referenceRevision"],
              "sourceReceiptSha256": sha256(provenance_path), "binarySha256": sha256(executable),
              "scope": "Frozen-source auxiliary CLI indexed image and exact opaque palette expansion. Excludes main UI, physical scaling and LightFX.",
              "cases": comparisons}
    (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    failures = [case["fixture"] for case in comparisons if any(case.get("differingPixels", {}).values())]
    print(json.dumps({"captured": len(comparisons), "differingFixtures": failures, "evidence": str(output)}))
    raise SystemExit(1 if failures else 0)


if __name__ == "__main__":
    main()
