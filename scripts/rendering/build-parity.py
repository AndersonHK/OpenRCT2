"""Build the Windows Vulkan parity lane and retain a source/binary provenance receipt.

Run from any directory. The output directory must be new. Source mutations during
the build fail the receipt so a test binary cannot be attributed to the wrong tree.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def source_manifest(root):
    names = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=root
    ).decode("utf-8").split("\0")
    return {name: sha256(root / name) for name in sorted(set(names))
            # Standalone diagnostic drivers have their own isolated build receipts
            # and are not inputs of openrct2.proj or test/tests/tests.vcxproj.
            # Shared terrain fixtures and diagnostic shader inputs are bound to the real unit tests.
            if name and (not name.startswith(("test/ui-parity/", "test/terrain-parity/"))
                         or name in ("test/terrain-parity/NonuniformTerrainRecipe.h",
                                     "test/terrain-parity/FrozenTerrainEdgeOracle.inc",
                                     "test/terrain-parity/FrozenTerrainEdgeOracle.json",
                                     "test/terrain-parity/TerrainSurfaceRulesProbe.comp"))
            and (root / name).is_file() and (
                name.startswith(("src/", "test/", "data/", "cmake/"))
                or "/" not in name and Path(name).suffix in (".props", ".proj", ".targets", ".sln", ".txt", ".json"))}


def find_msbuild(explicit):
    if explicit:
        return str(explicit.resolve(strict=True))
    found = shutil.which("MSBuild.exe")
    if found:
        return found
    installer = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    vswhere = installer / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.exists():
        lines = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                         "Microsoft.Component.MSBuild", "-find", r"MSBuild\**\Bin\MSBuild.exe"],
                                        text=True).splitlines()
        if lines:
            return lines[0]
    raise SystemExit("MSBuild not found; provide --msbuild with the installed executable path")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--msbuild", type=Path)
    parser.add_argument("--toolset-version", help="Optional installed VCToolsVersion override")
    parser.add_argument("--serial-compile", action="store_true",
                        help="Disable compiler /MP through an output-local C++ targets hook; MSBuild already uses one node")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists():
        raise SystemExit("Refusing to overwrite build evidence: " + str(output))
    subprocess.run([os.sys.executable, str(root / "scripts/rendering/verify-software-reference.py")], check=True)
    command = [find_msbuild(args.msbuild), "openrct2.proj", "/m:1", "/nr:false",
               "/p:Configuration=Release", "/p:Platform=x64", "/p:EnableVulkan=true"]
    if args.toolset_version:
        command.append("/p:VCToolsVersion=" + args.toolset_version)
    output.mkdir(parents=True)
    generated = []
    if args.serial_compile:
        hook = output / "serial-compile.targets"
        # C++ projects can override an ItemDefinitionGroup from common.props.
        # Set evaluated item metadata immediately before the actual compiler target.
        hook.write_text("""<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Target Name="ParityDisableCompilerParallelism" BeforeTargets="ClCompile">
    <ItemGroup>
      <ClCompile>
        <MultiProcessorCompilation>false</MultiProcessorCompilation>
      </ClCompile>
    </ItemGroup>
    <Message Text="PARITY_COMPILER_CONCURRENCY=serial $(MSBuildProjectName)" Importance="high" />
  </Target>
</Project>
""", encoding="utf-8")
        generated.append(hook)
        command.append("/p:ForceImportAfterCppTargets=" + str(hook))
    generated_before = {path.name: sha256(path) for path in generated}
    before = source_manifest(root)
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
    with (output / "build.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
    generated_after = {path.name: sha256(path) for path in generated if path.is_file()}
    generated_changes = sorted(name for name in generated_before.keys() | generated_after.keys()
                               if generated_before.get(name) != generated_after.get(name))
    after = source_manifest(root)
    changes = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
    artifacts = [root / "bin/tests.exe", root / "bin/openrct2.exe", root / "bin/openrct2-cli.exe"]
    artifacts.extend(sorted((root / "bin/data/shaders/vulkan").glob("*.spv")))
    missing = [str(path) for path in artifacts if not path.is_file()]
    passed = result.returncode == 0 and not changes and not generated_changes and not missing
    receipt = {
        "schema": 1, "status": "pass" if passed else "fail", "exitCode": result.returncode,
        "command": command,
        "compilerConcurrency": "serial" if args.serial_compile else "project-default",
        "generatedInputSha256": generated_before, "generatedInputChangesDuringBuild": generated_changes,
        "builderSha256": sha256(Path(__file__)),
        "sourceRevision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "sourceSha256": before, "sourceChangesDuringBuild": changes,
        "sourceManifestScope": "Production src/data/build metadata and ordinary tests; standalone test/ui-parity and test/terrain-parity drivers use separate build receipts",
        "artifactSha256": {path.relative_to(root).as_posix(): sha256(path) for path in artifacts if path.is_file()},
        "missingArtifacts": missing, "buildLogSha256": sha256(output / "build.log"),
    }
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: receipt[key] for key in ("status", "exitCode", "sourceChangesDuringBuild", "missingArtifacts")}))
    print("Build receipt:", output / "receipt.json")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
