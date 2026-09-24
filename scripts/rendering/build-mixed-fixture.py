"""Build an isolated mixed original-art corpus producer against the frozen core.

No archived file is changed. --frozen requires byte-identical archived core
sources. --reuse-build accepts only a core with matching source, dependencies,
toolchain, variant, generated project metadata and library hash.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile


HELPER = Path(__file__).with_name("build-current-ui-parity.py")
spec = importlib.util.spec_from_file_location("ui_build", HELPER)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def inputs(source, harness):
    result = {key: value for key, value in build.source_manifest(source, harness).items()
              if key.startswith("source/")}
    result.update({"harness/" + path.relative_to(harness).as_posix(): build.sha256(path)
                   for path in sorted((harness / "test/mixed-parity").rglob("*")) if path.is_file()})
    recipe_header = harness / "test/mixed-parity/MixedFixtureRecipes.h"
    if not recipe_header.is_file():
        raise RuntimeError("Missing mixed fixture recipe header")
    return result


def frozen_proof(workspace, source):
    reference_path = workspace / "docs/vulkan-software-reference.json"
    reference = json.loads(reference_path.read_text(encoding="utf-8"))
    archive = workspace / reference["localReference"] / "source.zip"
    if build.sha256(archive) != reference["sourceArchive"]["sha256"]:
        raise RuntimeError("Frozen archive differs from accepted reference")
    checked = {}
    with zipfile.ZipFile(archive) as frozen:
        for item in frozen.infolist():
            if item.is_dir() or not (item.filename.startswith("src/") or item.filename.endswith(".props")):
                continue
            expected = hashlib.sha256(frozen.read(item)).hexdigest()
            actual = source / item.filename
            if not actual.is_file() or build.sha256(actual) != expected:
                raise RuntimeError("Frozen source was modified: " + item.filename)
            checked[item.filename] = expected
    if not checked:
        raise RuntimeError("No frozen sources checked")
    return {"revision": reference["revision"], "sourceArchiveSha256": build.sha256(archive),
            "referenceSha256": build.sha256(reference_path), "verifiedSourceSha256": checked}


def driver_project(source, harness, output, frozen, vulkan):
    child = build.child
    project = ET.Element(build.tag("Project"), {"ToolsVersion": "Current"})
    configurations = child(project, "ItemGroup", Label="ProjectConfigurations")
    configuration = child(configurations, "ProjectConfiguration", Include="Release|x64")
    child(configuration, "Configuration", "Release")
    child(configuration, "Platform", "x64")
    properties = child(project, "PropertyGroup", Label="Globals")
    child(properties, "ProjectGuid", "{77A2D71F-84B7-44D4-9077-25CF572A6E00}")
    child(properties, "ProjectName", "mixed-fixture-preparer")
    properties = child(project, "PropertyGroup", Label="Configuration")
    child(properties, "ConfigurationType", "Application")
    child(project, "Import", Project=str(source / "openrct2.common.props"))
    definitions = child(project, "ItemDefinitionGroup")
    compiler = child(definitions, "ClCompile")
    child(compiler, "PrecompiledHeader", "NotUsing")
    if frozen:
        child(compiler, "PreprocessorDefinitions", "MIXED_FIXTURE_FROZEN_ORACLE;%(PreprocessorDefinitions)")
    elif vulkan:
        child(compiler, "PreprocessorDefinitions", "ENABLE_VULKAN;%(PreprocessorDefinitions)")
    linker = child(definitions, "Link")
    child(linker, "AdditionalDependencies", "$(VulkanAdditionalDependencies)$(OutDir)libopenrct2.lib;%(AdditionalDependencies)")
    child(linker, "SubSystem", "Console")
    child(linker, "StackReserveSize", "8388608")
    sources = child(project, "ItemGroup")
    child(sources, "ClCompile", Include=str(harness / "test/mixed-parity/MixedFixtureMain.cpp"))
    child(project, "Import", Project=r"$(VCTargetsPath)\Microsoft.Cpp.targets")
    path = output / "mixed-fixture-driver.vcxproj"
    build.write_xml(path, project)
    return path


def reuse_core(path, before, dependencies, toolchain, common, output, vulkan):
    if not path:
        return None
    receipt_path = path.resolve(strict=True)
    if receipt_path.is_dir():
        receipt_path /= "receipt.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("sourceChangesDuringBuild") or receipt.get("dependencyChangesDuringBuild"):
        raise RuntimeError("Cannot reuse a core compiled during input mutations")
    successful, _ = build.successful_stages(receipt, receipt_path)
    matches = ("core" in successful
               and build.compile_inputs(receipt["sourceSha256"], vulkan) == build.compile_inputs(before, vulkan)
               and receipt["dependencySha256"] == dependencies
               and receipt["toolchain"]["sha256"] == toolchain["sha256"]
               and receipt["toolchain"]["properties"] == toolchain["properties"]
               and build.build_variant(receipt["commands"]) == build.build_variant([common]))
    if not matches:
        return None
    name = "ui-parity-core.vcxproj"
    old = receipt_path.parent / name
    if build.sha256(old) != receipt["generatedProjectSha256"][name]:
        raise RuntimeError("Reused core compiler metadata was altered")
    previous_workspace = build.receipt_build_workspace(receipt, receipt_path)
    old_text = old.read_text(encoding="utf-8").replace(str(previous_workspace), "@OUTPUT@")
    new_text = (output / name).read_text(encoding="utf-8").replace(str(output), "@OUTPUT@")
    if old_text != new_text:
        return None
    relative = "bin/libopenrct2.lib"
    original = receipt_path.parent / relative
    if build.sha256(original) != receipt["artifactSha256"][relative]:
        raise RuntimeError("Reused core bytes were altered")
    destination = output / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(original, destination)
    return {"sourceReceipt": str(receipt_path), "sourceReceiptSha256": build.sha256(receipt_path),
            "librarySha256": build.sha256(destination)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--reuse-build", type=Path)
    parser.add_argument("--msbuild", type=Path)
    parser.add_argument("--toolset-version")
    parser.add_argument("--frozen", action="store_true")
    parser.add_argument("--serial-compile", action="store_true", help="Disable compiler /MP and match serial UI core metadata for reuse")
    parser.add_argument("--serial-driver-only", action="store_true",
                        help="Require exact core reuse and compile only the trace driver serially")
    parser.add_argument("--enable-vulkan", action="store_true",
                        help="Current verification build only: match a Vulkan-enabled core receipt for reuse")
    args = parser.parse_args()
    if not args.frozen:
        parser.error("Mixed reference corpus production requires --frozen")
    if args.frozen and args.enable_vulkan:
        parser.error("--enable-vulkan is current-only; the frozen preparer always uses EnableVulkan=false")
    if args.serial_driver_only and (args.reuse_build is None or args.serial_compile):
        parser.error("--serial-driver-only requires --reuse-build and excludes --serial-compile")
    library_concurrency = "serial" if args.serial_compile else "project-default"
    reuse_receipt_path = None
    reuse_receipt_hash = None
    if args.serial_driver_only:
        reuse_receipt_path = args.reuse_build.resolve(strict=True)
        if reuse_receipt_path.is_dir(): reuse_receipt_path /= "receipt.json"
        reuse_receipt_hash = build.sha256(reuse_receipt_path)
        prior = json.loads(reuse_receipt_path.read_text(encoding="utf-8"))
        library_concurrency = prior.get("compilerConcurrency", "project-default")
        if library_concurrency not in ("serial", "project-default"):
            raise RuntimeError("Unsupported reusable core compiler policy")
    workspace = Path(__file__).resolve().parents[2]
    source = (args.source_root or workspace).resolve(strict=True)
    output = args.output.resolve()
    if output.exists() or workspace not in output.parents:
        raise SystemExit("--output must be a new directory inside the workspace")
    proof = frozen_proof(workspace, source) if args.frozen else None
    output.mkdir(parents=True)
    core = build.make_library_project(source, workspace, output, args.enable_vulkan, False)
    driver = driver_project(source, workspace, output, args.frozen, args.enable_vulkan)
    if library_concurrency == "serial" or args.serial_driver_only:
        for project_path in (core, driver):
            if project_path == core and library_concurrency != "serial": continue
            project = ET.parse(project_path).getroot()
            for item in project.findall("./" + build.tag("ItemGroup") + "/" + build.tag("ClCompile")):
                option = item.find(build.tag("MultiProcessorCompilation"))
                if option is None:
                    option = build.child(item, "MultiProcessorCompilation")
                option.text = "false"
            build.write_xml(project_path, project)
    before = inputs(source, workspace)
    dependencies = build.dependency_manifest(source)
    msbuild = build.find_msbuild(args.msbuild)
    common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
              "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:Breakpad=false",
              "/p:PreferredToolArchitecture=x64", "/p:SolutionDir=" + str(source) + os.sep,
              "/p:OutDir=" + str(output / "bin") + os.sep, "/p:EnableVulkan=" + str(args.enable_vulkan).lower()]
    if args.toolset_version:
        common.append("/p:VCToolsVersion=" + args.toolset_version)
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
    toolchain = build.compiler_identity(msbuild, core, common, env, output)
    reuse = reuse_core(args.reuse_build, before, dependencies, toolchain, common, output, args.enable_vulkan)
    if args.serial_driver_only:
        if reuse is None: raise RuntimeError("Driver-only build requires exact qualified core reuse")
        if build.sha256(reuse_receipt_path) != reuse_receipt_hash:
            raise RuntimeError("Reuse receipt changed during qualification")
    commands, stages = [], []
    result_code = 0
    with (output / "build.log").open("w", encoding="utf-8") as log:
        for name, project, target in (("core", core, "libopenrct2"), ("driver", driver, "mixed-fixture-preparer")):
            command = [msbuild, str(project), *common, "/p:IntDir=" + str(output / "int" / name) + os.sep,
                       "/p:TargetName=" + target]
            commands.append(command)
            if name == "core" and reuse:
                stages.append({"name": name, "exitCode": 0, "reused": reuse})
                log.write("REUSED core " + json.dumps(reuse) + "\n")
                continue
            log.write("\nCOMMAND " + json.dumps(command) + "\n")
            log.flush()
            result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
            stages.append({"name": name, "exitCode": result.returncode})
            if result.returncode:
                result_code = result.returncode
                break
    after = inputs(source, workspace)
    dependency_after = build.dependency_manifest(source)
    changes = sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))
    dependency_changes = sorted(key for key in dependencies.keys() | dependency_after.keys()
                                if dependencies.get(key) != dependency_after.get(key))
    runtime = {}
    for path in sorted((source / "lib/x64/bin").glob("*.dll")):
        destination = output / "bin" / path.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        runtime[path.name] = build.sha256(destination)
    artifacts = [output / "bin/libopenrct2.lib", output / "bin/mixed-fixture-preparer.exe"]
    missing = [str(path) for path in artifacts if not path.is_file()]
    reuse_receipt_unchanged = (reuse_receipt_path is None or
                               (reuse_receipt_path.is_file() and build.sha256(reuse_receipt_path) == reuse_receipt_hash))
    passed = result_code == 0 and not changes and not dependency_changes and not missing and reuse_receipt_unchanged
    receipt = {"schema": 1, "status": "pass" if passed else "fail", "exitCode": result_code,
               "enableVulkan": args.enable_vulkan,
               "compilerConcurrency": library_concurrency,
               "driverCompilerConcurrency": "serial" if args.serial_compile or args.serial_driver_only else "project-default",
               "libraryCompilationPolicy": "reuse-required" if args.serial_driver_only else "compile-or-reuse",
               "reuseReceiptUnchanged": reuse_receipt_unchanged,
               "recipeHeaderSha256": before["harness/test/mixed-parity/MixedFixtureRecipes.h"],
               "sourceRoot": str(source), "commands": commands, "stageResults": stages,
               "sourceSha256": before, "sourceChangesDuringBuild": changes, "dependencySha256": dependencies,
               "dependencyChangesDuringBuild": dependency_changes, "toolchain": toolchain,
               "artifactSha256": {path.relative_to(output).as_posix(): build.sha256(path) for path in artifacts if path.is_file()},
               "generatedProjectSha256": {path.name: build.sha256(path) for path in (core, driver)},
               "runtimeDllSha256": runtime, "buildLogSha256": build.sha256(output / "build.log"),
               "builderSha256": {str(path): build.sha256(path) for path in (Path(__file__), HELPER)},
               "frozenReference": proof, "missingArtifacts": missing,
               "instrumentation": "Public fixture main linked to unchanged core; no renderer or archived source edits."}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "reusedCore": reuse is not None, "receipt": str(output / "receipt.json")}))
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
