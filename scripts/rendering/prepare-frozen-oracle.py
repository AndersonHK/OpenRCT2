"""Extract the frozen source into an isolated, profile-configurable CLI oracle.

The accepted package/archive are never edited. The sole source change in this
separate build is CLI startup plumbing for isolated paths, before command dispatch.
All software rasterizer, paint, scene, asset and screenshot code remains original.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if root not in output.parents or output.exists():
        raise SystemExit("Oracle extraction requires a new directory inside this workspace")
    subprocess.run([sys.executable, str(root / "scripts/rendering/verify-software-reference.py")], check=True)
    reference = json.loads((root / "docs/vulkan-software-reference.json").read_text(encoding="utf-8"))
    archive = root / reference["localReference"] / "source.zip"
    output.mkdir(parents=True)
    hashes = {}
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            target = (output / entry.filename).resolve()
            if output not in target.parents:
                raise SystemExit("Unsafe source archive entry: " + entry.filename)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            data = source.read(entry)
            hashes[entry.filename] = digest(data)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    relative = "src/openrct2-cli/Cli.cpp"
    cli = output / relative
    original = cli.read_text(encoding="utf-8")
    marker = "    auto runGame = CommandLineRun(argv, argc);"
    if original.count(marker) != 1:
        raise SystemExit("Frozen CLI startup changed; refusing an unreviewed instrumentation patch")
    plumbing = """    // Oracle harness only: isolate profile and immutable asset paths before screenshot dispatch.
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_USER_PATH"))
        gCustomUserDataPath = path;
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_DATA_PATH"))
        gCustomOpenRCT2DataPath = path;
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_RCT1_PATH"))
        gCustomRCT1DataPath = path;
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_RCT2_PATH"))
        gCustomRCT2DataPath = path;
"""
    changed = original.replace("#include <openrct2/Context.h>", "#include <cstdlib>\n#include <openrct2/Context.h>")
    changed = changed.replace(marker, plumbing + marker)
    cli.write_bytes(changed.encode("utf-8"))
    # Existing dependencies are build inputs only. IsSolutionBuild=true suppresses dependency downloads.
    link = output / "lib/x64"
    link.parent.mkdir(parents=True, exist_ok=True)
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    subprocess.run(["powershell", "-NoProfile", "-Command", "New-Item -ItemType Junction -Path "
                    + quote(link) + " -Target " + quote(root / "lib/x64") + " | Out-Null"], check=True)
    receipt = {
        "schema": 1, "referenceRevision": reference["revision"], "sourceArchiveSha256": digest(archive.read_bytes()),
        "originalSourceSha256": hashes, "instrumentedFile": relative,
        "instrumentedSha256": digest(cli.read_bytes()), "instrumentation": plumbing,
        "dependencyPath": str(root / "lib/x64"),
        "limits": "Rebuilt frozen-source CLI oracle with isolated-path startup plumbing, not the original accepted executable. No renderer changes. Does not capture main UI/SDL display.",
    }
    (output / "oracle-source-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("Prepared frozen-source oracle:", output)


if __name__ == "__main__":
    main()
