"""Materialize an unchanged frozen source tree for the independent UI capture driver.

Harness files are additions, never replacements for frozen sources. The diagnostic
build may instrument SDL presentation through compiler inputs; the receipt pins
those inputs separately from the original rendering and UI source.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--harness", type=Path, help="Capture driver directory; defaults to test/ui-parity")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    harness = (args.harness or root / "test/ui-parity").resolve(strict=True)
    if root not in output.parents or output.exists():
        raise SystemExit("Oracle extraction requires a new directory inside this workspace")
    inputs = sorted(path for path in harness.rglob("*") if path.is_file())
    if not inputs:
        raise SystemExit("Capture driver is empty")
    subprocess.run([sys.executable, str(root / "scripts/rendering/verify-software-reference.py")], check=True)
    reference = json.loads((root / "docs/vulkan-software-reference.json").read_text(encoding="utf-8"))
    archive = root / reference["localReference"] / "source.zip"
    output.mkdir(parents=True)
    originals = {}
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            target = (output / entry.filename).resolve()
            if output not in target.parents:
                raise SystemExit("Unsafe archive entry: " + entry.filename)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if target.exists():
                raise SystemExit("Duplicate archive entry: " + entry.filename)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read(entry))
            originals[entry.filename] = sha256(target)
    additions = {}
    for path in inputs:
        relative = Path("test/ui-parity") / path.relative_to(harness)
        target = output / relative
        if target.exists():
            raise SystemExit("Harness would overwrite frozen source: " + str(relative))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        additions[relative.as_posix()] = sha256(target)
    # Read-only dependency inputs. IsSolutionBuild=true prevents restore/download.
    link = output / "lib/x64"
    link.parent.mkdir(parents=True, exist_ok=True)
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    subprocess.run(["powershell", "-NoProfile", "-Command", "New-Item -ItemType Junction -Path "
                    + quote(link) + " -Target " + quote(root / "lib/x64") + " | Out-Null"], check=True)
    receipt = {
        "schema": 1, "kind": "frozen-ui-oracle", "referenceRevision": reference["revision"],
        "sourceArchiveSha256": sha256(archive), "originalSourceSha256": originals,
        "modifiedOriginalSources": [], "harnessSourceSha256": additions,
        "dependencyPath": str(root / "lib/x64"),
        "scope": "Unchanged frozen source plus independently pinned UI capture driver. Build receipt must record source-scoped presentation instrumentation, toolchain, dependency and executable hashes. This preparation alone establishes no pixel result.",
    }
    (output / "oracle-ui-source-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"sourceDirectory": str(output), "originalFiles": len(originals),
                      "harnessFiles": len(additions), "modifiedOriginalSources": []}))


if __name__ == "__main__":
    main()
