"""Build/run the explicit CPU-only 60000-peep producer microbenchmark.

The driver is outside ordinary tests. Build reuses an exact successful build-parity
core receipt, compiles one translation unit, and records its own inputs. Run never
acquires a renderer; it measures capture/allocation, not simulation or park TPS.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
HELPER = Path(__file__).with_name("build-current-ui-parity.py")
spec = importlib.util.spec_from_file_location("producer_build", HELPER)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
DRIVER = "test/peep-parity/PeepProducerBenchmark.cpp"
FIXTURE = "test/peep-parity/PeepCaptureFixture.h"


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def new_output(path):
    path = path.resolve()
    if path.exists() or ROOT not in path.parents:
        raise RuntimeError("Output must be a new directory inside the workspace")
    path.mkdir(parents=True)
    return path


def environment():
    # Prevent inherited graphics diagnostics and compiler injection from changing this lane.
    return {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()
            if not key.upper().startswith(("OPENRCT2_", "VK_"))
            and key.upper() not in ("CL", "_CL_", "LINK", "_LINK_")}


def verified_hashes(base, expected):
    actual = {name: build.sha256(base / name) for name in expected}
    changed = [name for name in expected if expected[name] != actual[name]]
    if changed:
        raise RuntimeError("Pinned input changed: " + ", ".join(changed[:10]))
    return actual


def producer_inputs(core_receipt):
    # Keep the ordinary receipt's complete src/data/build/test inventory, including shared fixtures.
    expected = core_receipt["sourceSha256"]
    if FIXTURE not in expected or not any(name.startswith("src/openrct2/") for name in expected):
        raise RuntimeError("Core receipt lacks required production/shared diagnostic inputs")
    result = verified_hashes(ROOT, expected)
    for path in (ROOT / DRIVER, ROOT / FIXTURE, Path(__file__), HELPER):
        result[path.relative_to(ROOT).as_posix()] = build.sha256(path)
    return result


def driver_project(output):
    child = build.child
    project = ET.Element(build.tag("Project"), {"ToolsVersion": "Current"})
    configurations = child(project, "ItemGroup", Label="ProjectConfigurations")
    configuration = child(configurations, "ProjectConfiguration", Include="Release|x64")
    child(configuration, "Configuration", "Release")
    child(configuration, "Platform", "x64")
    properties = child(project, "PropertyGroup", Label="Globals")
    child(properties, "ProjectGuid", "{DF935308-1498-40B9-A343-053013169E62}")
    child(properties, "ProjectName", "peep-producer-benchmark")
    properties = child(project, "PropertyGroup", Label="Configuration")
    child(properties, "ConfigurationType", "Application")
    child(project, "Import", Project=str(ROOT / "openrct2.common.props"))
    definitions = child(project, "ItemDefinitionGroup")
    compiler = child(definitions, "ClCompile")
    child(compiler, "PrecompiledHeader", "NotUsing")
    child(compiler, "MultiProcessorCompilation", "false")
    child(compiler, "AdditionalIncludeDirectories", str(ROOT / "src") + ";%(AdditionalIncludeDirectories)")
    child(compiler, "PreprocessorDefinitions", "ENABLE_VULKAN;%(PreprocessorDefinitions)")
    linker = child(definitions, "Link")
    child(linker, "AdditionalDependencies", "$(VulkanAdditionalDependencies)$(OutDir)libopenrct2.lib;%(AdditionalDependencies)")
    child(linker, "SubSystem", "Console")
    child(linker, "StackReserveSize", "8388608")
    child(child(project, "ItemGroup"), "ClCompile", Include=str(ROOT / DRIVER))
    child(project, "Import", Project=r"$(VCTargetsPath)\Microsoft.Cpp.targets")
    path = output / "peep-producer-benchmark.vcxproj"
    build.write_xml(path, project)
    return path


def build_driver(args):
    receipt_path = args.core_receipt.resolve(strict=True)
    core = json.loads(receipt_path.read_text(encoding="utf-8"))
    core_hash = build.sha256(receipt_path)
    if (core.get("status") != "pass" or core.get("exitCode") != 0
            or core.get("sourceChangesDuringBuild") or core.get("generatedInputChangesDuringBuild")):
        raise RuntimeError("A successful, source-stable ordinary core build receipt is required")
    command = core["command"]
    for required in ("/p:Configuration=Release", "/p:Platform=x64", "/p:EnableVulkan=true",
                     "/p:PreferredToolArchitecture=x64"):
        if required not in command:
            raise RuntimeError("Unsupported core variant: missing " + required)
    library = "bin/libopenrct2.lib"
    verified_hashes(ROOT, {library: core["artifactSha256"][library]})
    if build.sha256(receipt_path.parent / "build.log") != core["buildLogSha256"]:
        raise RuntimeError("Core build log changed")
    before = producer_inputs(core)
    dependencies = build.dependency_manifest(ROOT)
    output = new_output(args.output)
    (output / "bin").mkdir()
    shutil.copy2(ROOT / library, output / library)
    project = driver_project(output)
    project_hash = build.sha256(project)
    msbuild = build.find_msbuild(args.msbuild or Path(command[0]))
    common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
              "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:PreferredToolArchitecture=x64",
              "/p:SolutionDir=" + str(ROOT) + os.sep, "/p:OutDir=" + str(output / "bin") + os.sep,
              "/p:IntDir=" + str(output / "int") + os.sep, "/p:EnableVulkan=true"]
    for option in command:
        if option.startswith(("/p:VCToolsVersion=", "/p:Breakpad=", "/p:UseSharedLibs=")):
            common.append(option)
    env = environment()
    toolchain = build.compiler_identity(msbuild, project, common, env, output)
    invocation = [msbuild, str(project), *common]
    with (output / "build.log").open("w", encoding="utf-8") as log:
        log.write(json.dumps(invocation) + "\n")
        log.flush()
        result = subprocess.run(invocation, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    for path in sorted((ROOT / "lib/x64/bin").glob("*.dll")):
        shutil.copy2(path, output / "bin" / path.name)
    after = {name: build.sha256(ROOT / name) for name in before}
    changes = [name for name in before if before[name] != after[name]]
    dependency_changes = dependencies != build.dependency_manifest(ROOT)
    immutable = (core_hash == build.sha256(receipt_path) and project_hash == build.sha256(project)
                 and build.sha256(output / library) == core["artifactSha256"][library]
                 and all(build.sha256(Path(path)) == digest for path, digest in toolchain["sha256"].items()))
    exe = output / "bin/peep-producer-benchmark.exe"
    passed = result.returncode == 0 and not changes and not dependency_changes and immutable and exe.is_file()
    artifacts = {path.relative_to(output).as_posix(): build.sha256(path)
                 for path in sorted((output / "bin").iterdir()) if path.is_file()}
    receipt = {"schema": 1, "status": "pass" if passed else "fail", "exitCode": result.returncode,
               "kind": "peep-producer-standalone-build", "command": invocation,
               "coreReceipt": str(receipt_path), "coreReceiptSha256": core_hash,
               "coreLibrarySha256": core["artifactSha256"][library],
               "sourceSha256": before, "sourceChangesDuringBuild": changes,
               "dependencySha256": dependencies, "dependencyChangesDuringBuild": dependency_changes,
               "toolchain": toolchain, "immutableInputsUnchanged": immutable,
               "generatedProjectSha256": {project.name: project_hash}, "artifactSha256": artifacts,
               "buildLogSha256": build.sha256(output / "build.log"),
               "scope": "One standalone diagnostic TU linked to pinned production core; no benchmark executed",
               "coreDependencyProvenanceLimit": "Ordinary core receipt pins source/library, not original dependency/toolchain contents; this receipt pins dependencies/toolchain used for the standalone link"}
    write_json(output / "receipt.json", receipt)
    print(json.dumps({"status": receipt["status"], "receipt": str(output / "receipt.json")}))
    return 0 if passed else 1


def run_driver(args):
    receipt_path = args.build_receipt.resolve(strict=True)
    receipt_hash = build.sha256(receipt_path)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("status") != "pass" or receipt.get("kind") != "peep-producer-standalone-build":
        raise RuntimeError("Successful standalone producer build receipt required")
    artifact_dir = receipt_path.parent
    verified_hashes(artifact_dir, receipt["artifactSha256"])
    if build.sha256(artifact_dir / "build.log") != receipt["buildLogSha256"]:
        raise RuntimeError("Standalone build log changed")
    data = args.data.resolve(strict=True)
    # Initialization may load localization; peep state and catalog tokens are entirely synthetic.
    language = {path.relative_to(data).as_posix(): build.sha256(path)
                for path in sorted((data / "language").glob("*.txt"))}
    if not language:
        raise RuntimeError("--data must contain the runtime language directory")
    output = new_output(args.output)
    (output / "profile").mkdir()
    command = [str(artifact_dir / "bin/peep-producer-benchmark.exe"), "--output", str(output / "result.json"),
               "--profile", str(output / "profile"), "--data", str(data), "--iterations", str(args.iterations),
               "--warmup", str(args.warmup)]
    started = time.monotonic()
    timed_out = False
    with (output / "run.log").open("w", encoding="utf-8") as log:
        try:
            result = subprocess.run(command, cwd=artifact_dir / "bin", env=environment(),
                                    stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            timed_out, code = True, -1
    wall_seconds = time.monotonic() - started
    verified_hashes(artifact_dir, receipt["artifactSha256"])
    verified_hashes(data, language)
    if build.sha256(receipt_path) != receipt_hash:
        raise RuntimeError("Build receipt changed during benchmark")
    report_path = output / "result.json"
    report = json.loads(report_path.read_text(encoding="utf-8")) if report_path.is_file() else {}
    expected_cases = {"movement-only", "appearance-only", "animation-only", "motion-and-animation",
                      "all-groups-including-lifecycle"}
    passed = (code == 0 and report.get("status") == "pass" and report.get("population") == 60000
              and report.get("iterations") == args.iterations and report.get("warmup") == args.warmup
              and report.get("graphicsDisabled") is True and report.get("rendererAcquired") is False
              and len(report.get("cases", [])) == 5
              and {case["name"] for case in report["cases"]} == expected_cases
              and all(case.get("equivalent") and case.get("heldSnapshotAndLiveFactsUnchanged")
                      and case.get("everyIterationByteCompared") and case["checksums"] == case["expectedChecksums"]
                      and all(case[key]["samples"] == args.iterations for key in
                              ("raw96Capture", "directRegistryCapture", "raw96Destroy", "directRegistryDestroy"))
                      for case in report["cases"]))
    summary = {"schema": 1, "status": "pass" if passed else "fail", "exitCode": code, "timedOut": timed_out,
               "command": command, "wallSecondsIncludingInitializationAndValidation": wall_seconds,
               "buildReceipt": str(receipt_path), "buildReceiptSha256": receipt_hash,
               "resultSha256": build.sha256(report_path) if report_path.is_file() else None,
               "runLogSha256": build.sha256(output / "run.log"), "artifactSha256": receipt["artifactSha256"],
               "runtimeData": str(data), "languageSha256": language,
               "runtimeAssetScope": "Localization pinned; object repository initialization excluded from timing. No object/sprite-derived benchmark facts.",
               "host": {"platform": platform.platform(), "processor": platform.processor(),
                        "logicalCpuCount": os.cpu_count(), "affinity": "inherited", "priority": "inherited"},
               "scope": "Warm-cache frozen-worklist CPU producer/allocation cost only; no whole-park, simulation, consumer, upload or GPU gain claim"}
    write_json(output / "summary.json", summary)
    print(json.dumps({"status": summary["status"], "summary": str(output / "summary.json")}))
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="mode", required=True)
    compile_parser = commands.add_parser("build")
    compile_parser.add_argument("--core-receipt", type=Path, required=True)
    compile_parser.add_argument("--output", type=Path, required=True)
    compile_parser.add_argument("--msbuild", type=Path)
    run_parser = commands.add_parser("run")
    run_parser.add_argument("--build-receipt", type=Path, required=True)
    run_parser.add_argument("--output", type=Path, required=True)
    run_parser.add_argument("--data", type=Path, default=ROOT / "bin/data")
    run_parser.add_argument("--iterations", type=int, default=3000, choices=range(1, 3001), metavar="1..3000")
    run_parser.add_argument("--warmup", type=int, default=50, choices=range(0, 1001), metavar="0..1000")
    run_parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    raise SystemExit(build_driver(args) if args.mode == "build" else run_driver(args))


if __name__ == "__main__":
    main()
