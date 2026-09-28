"""Build the existing no-window test partition using only receipt-verified libraries.

This never builds core, renderer, UI or dependency projects, fetches assets, or
runs tests. Windowed parity remains explicitly unqualified. The separate ordinary
build receipt pins the actual test inputs; the UI build receipt pins the reused
core/renderer libraries and their source, dependency, compiler and option inputs.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET


HELPER_PATH = Path(__file__).with_name("build-current-ui-parity.py")
SPEC = importlib.util.spec_from_file_location("ui_parity_builder", HELPER_PATH)
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)
tag, child = BUILD.tag, BUILD.child


def load_receipt(path):
    path = path.resolve(strict=True)
    if path.is_dir():
        path /= "receipt.json"
    receipt = json.loads(path.read_text(encoding="utf-8"))
    if receipt.get("status") != "pass" or receipt.get("sourceChangesDuringBuild"):
        raise RuntimeError("A successful, source-stable build receipt is required: " + str(path))
    if receipt.get("dependencyChangesDuringBuild"):
        raise RuntimeError("Receipt reports dependency changes: " + str(path))
    if BUILD.sha256(path.parent / "build.log") != receipt["buildLogSha256"]:
        raise RuntimeError("Receipt build log hash changed: " + str(path))
    return path, receipt


TEST_SHARED_INPUTS = ("test/terrain-parity/NonuniformTerrainRecipe.h",
                      "test/terrain-parity/FrozenTerrainEdgeOracle.inc",
                      "test/terrain-parity/FrozenTerrainEdgeOracle.json",
                      "test/terrain-parity/TerrainSurfaceRulesProbe.comp")


def test_manifest(source):
    paths = list((source / "test/tests").rglob("*"))
    paths.extend(source / name for name in TEST_SHARED_INPUTS)
    return {path.relative_to(source).as_posix(): BUILD.sha256(path)
            for path in sorted(paths)
            if path.is_file() and path.suffix.lower() != ".pdb"}


def changed(before, after):
    return sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))


def make_test_project(source, output):
    """Relocate actual metadata while retaining every source condition verbatim."""
    original = source / "test/tests/tests.vcxproj"
    project = ET.parse(original).getroot()
    excluded = []
    removed_references = []
    for group in project.findall(tag("ItemGroup")):
        for element in list(group):
            include = element.get("Include")
            if element.tag == tag("ProjectReference"):
                removed_references.append(include)
                group.remove(element)
                continue
            if include and element.tag in (tag("ClCompile"), tag("ClInclude"), tag("None")):
                absolute = (original.parent / include.replace("\\", "/")).resolve(strict=True)
                element.set("Include", str(absolute))
                if element.tag == tag("ClCompile"):
                    if "EnableVulkanWindowTests" in element.get("Condition", ""):
                        if element.get("Condition") != "'$(EnableVulkanWindowTests)'=='true'":
                            raise RuntimeError("Review new window-test condition before qualification: " + include)
                        excluded.append(include)
                    directory = output / "int/tests/objects" / absolute.parent.relative_to(source)
                    directory.mkdir(parents=True, exist_ok=True)
                    child(element, "ObjectFileName", str(directory) + os.sep)
    for element in project.findall(tag("Import")):
        value = element.get("Project", "")
        if "$" not in value:
            element.set("Project", str((original.parent / value.replace("\\", "/")).resolve(strict=True)))
    for group in project.findall(tag("ItemDefinitionGroup")):
        for event in list(group.findall(tag("PostBuildEvent"))):
            group.remove(event)
        for element in group.findall(tag("Link") + "/" + tag("AdditionalDependencies")):
            # Never search ordinary bin for these libraries, even if a copied input is missing.
            element.text = element.text.replace("libopenrct2.lib;", "$(OutDir)libopenrct2.lib;")
            element.text = element.text.replace("libopenrct2renderer.lib;", "$(OutDir)libopenrct2renderer.lib;")
    for element in project.iter(tag("LibraryPath")):
        element.text = element.text.replace("$(SolutionDir)bin;", "$(OutDir);")
    definitions = ET.Element(tag("ItemDefinitionGroup"))
    compiler = child(definitions, "ClCompile")
    pch = str(original.parent / "tests_pch.h")
    child(compiler, "PrecompiledHeaderFile", pch, Condition="'$(UsePCH)'=='true'")
    child(compiler, "ForcedIncludeFiles", pch, Condition="'$(UsePCH)'=='true'")
    project.insert(len(project) - 1, definitions)
    if not excluded or project.findall(".//" + tag("ProjectReference")):
        raise RuntimeError("Expected explicit window partition and zero project references")
    if not any(element.get("Name") == "ReportVulkanWindowCoverage" for element in project.findall(tag("Target"))):
        raise RuntimeError("Missing existing windowed-coverage disclosure target")
    path = output / "no-window-tests.vcxproj"
    BUILD.write_xml(path, project)
    audio_partitioned = any("OPENRCT2_TEST_NO_UI_AUDIO" in (element.text or "")
                            for element in project.iter(tag("PreprocessorDefinitions")))
    return path, {"uncompiledWindowedSources": excluded, "removedProjectReferences": removed_references,
                  "uncompiledUiAudioTests": ["AudioChannel.NonLoopingSourceCompletionOwnsChannelLifetimeState",
                                             "AudioMixer.RepeatedSampleVoicesRetainIndependentPlayback",
                                             "SpatialAudio.NewSpatialChannelStartsFromConfiguredState",
                                             "AudioChannel.FloatingPointGainPreservesValuesAboveUnity"] if audio_partitioned else [],
                  "uiBindings": False, "windowedParityQualified": False,
                  "testConditionsPreserved": True, "postBuildAssetCopy": "Local Python copy only",
                  "limitation": "Legacy common props retain SDL static-library/include metadata. No UI library or SDL window tests are linked; this is not SDL-free dependency packaging."}


def disable_compiler_parallelism(project_path):
    """Use the same item metadata as the isolated UI builder, preserving reuse equality."""
    project = ET.parse(project_path).getroot()
    for item in project.findall("./" + tag("ItemGroup") + "/" + tag("ClCompile")):
        option = item.find(tag("MultiProcessorCompilation"))
        if option is None:
            option = child(item, "MultiProcessorCompilation")
        option.text = "false"
    BUILD.write_xml(project_path, project)


def sdk_manifest(env):
    root = Path(env.get("VULKAN_SDK", "")).resolve()
    required = (root / "Include/vulkan/vulkan.h", root / "Lib/vulkan-1.lib")
    if any(not path.is_file() for path in required):
        raise RuntimeError("VULKAN_SDK must name a local SDK with Vulkan headers and x64 import library")
    return {str(path): BUILD.sha256(path) for directory in (root / "Include", root / "Lib")
            for path in sorted(directory.rglob("*")) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reuse-build", type=Path, required=True, help="Successful current UI build receipt")
    parser.add_argument("--test-source-receipt", type=Path, required=True, help="Successful ordinary build receipt")
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--msbuild", type=Path)
    parser.add_argument("--prepare-only", action="store_true", help="Verify inputs and generate projects without compiling tests")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    source = (args.source_root or workspace).resolve(strict=True)
    output = args.output.resolve()
    if output.exists() or workspace not in output.parents:
        raise SystemExit("--output must be a new directory inside the working workspace")
    output.mkdir(parents=True)
    receipt = {"schema": 1, "status": "fail", "windowedParityQualified": False,
               "sourceRoot": str(source), "commands": [], "stageResults": [],
               "builderSha256": {str(Path(__file__).resolve()): BUILD.sha256(Path(__file__)),
                                 str(HELPER_PATH.resolve()): BUILD.sha256(HELPER_PATH)}}
    exit_code = 1
    try:
        library_path, libraries = load_receipt(args.reuse_build)
        test_path, tests = load_receipt(args.test_source_receipt)
        before = BUILD.source_manifest(source, source)
        test_before = test_manifest(source)
        dependencies = BUILD.dependency_manifest(source)
        expected_tests = {key: value for key, value in tests["sourceSha256"].items() if key.startswith("test/tests/") or key in TEST_SHARED_INPUTS}
        if not expected_tests or test_before != expected_tests:
            raise RuntimeError("Test inputs differ from the ordinary build receipt: " + str(changed(expected_tests, test_before)))
        if BUILD.compile_inputs(before, True) != BUILD.compile_inputs(libraries["sourceSha256"], True):
            raise RuntimeError("Source inputs differ from the reused UI build; no library compilation fallback is allowed")
        # Test and library receipts must describe the same headers/implementation.
        # The UI builder historically includes MSVC's stray source-tree PDB in
        # its broad manifest; the ordinary source manifest correctly excludes it.
        # It remains verified by library reuse, but is not a test source input.
        disagreements = [key for key, value in libraries["sourceSha256"].items()
                         if key.startswith("source/") and not key.endswith(".pdb")
                         and tests["sourceSha256"].get(key[7:]) != value]
        if disagreements:
            raise RuntimeError("Test/library receipts describe different source inputs: " + str(disagreements))
        receipt.update({"libraryReceipt": str(library_path), "libraryReceiptSha256": BUILD.sha256(library_path),
                        "testSourceReceipt": str(test_path), "testSourceReceiptSha256": BUILD.sha256(test_path),
                        "sourceSha256": before, "testSourceSha256": test_before, "dependencySha256": dependencies})
        core = BUILD.make_library_project(source, source, output, True, ui=False)
        renderer = BUILD.make_library_project(source, source, output, True, ui=False, renderer=True)
        project, coverage = make_test_project(source, output)
        library_concurrency = libraries.get("compilerConcurrency", "project-default")
        if library_concurrency not in ("serial", "project-default"):
            raise RuntimeError("Unknown reused-library compiler concurrency: " + str(library_concurrency))
        if library_concurrency == "serial":
            for library_project in (core, renderer):
                disable_compiler_parallelism(library_project)
        # Only tests are built here. Core/renderer options reproduce their actual
        # receipt, so serial library reuse still passes exact metadata comparison.
        disable_compiler_parallelism(project)
        receipt["compilerConcurrency"] = "serial"
        receipt["reusedLibraryCompilerConcurrency"] = library_concurrency
        receipt["coverage"] = coverage
        env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
        sdk = sdk_manifest(env)
        receipt["sdkSha256"] = sdk
        msbuild = BUILD.find_msbuild(args.msbuild)
        variant = BUILD.build_variant(libraries["commands"])
        common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
                  "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:Breakpad=false",
                  "/p:PreferredToolArchitecture=x64", "/p:SolutionDir=" + str(source) + os.sep,
                  "/p:OutDir=" + str(output / "bin") + os.sep, "/p:EnableVulkan=true"]
        if variant.get("VCToolsVersion"):
            common.append("/p:VCToolsVersion=" + variant["VCToolsVersion"])
        toolchain = BUILD.compiler_identity(msbuild, core, common, env, output)
        receipt["toolchain"] = toolchain
        reuse, _ = BUILD.prepare_reuse(library_path, before, dependencies, toolchain, common, True, output)
        if set(reuse) != {"core", "renderer"}:
            raise RuntimeError("Both core and renderer must match all receipt/metadata/compiler/dependency checks; got " + str(sorted(reuse)))
        receipt["reusedLibraries"] = reuse
        receipt["generatedProjectSha256"] = {path.name: BUILD.sha256(path) for path in (core, renderer, project)}
        # Only this project is compiled. Core/renderer metadata above is for verification, never execution.
        command = [msbuild, str(project), *common, "/p:EnableVulkanWindowTests=false",
                   "/p:IntDir=" + str(output / "int/tests") + os.sep, "/p:TargetName=tests-no-window"]
        receipt["commands"] = [command]
        with (output / "build.log").open("w", encoding="utf-8") as log:
            log.write("WINDOWED_PARITY_COVERAGE=disabled\n")
            log.write("VERIFIED_LIBRARY_REUSE " + json.dumps(reuse) + "\n")
            log.write("COMMAND " + json.dumps(command) + "\n")
            log.flush()
            if args.prepare_only:
                result_code = 0
                log.write("PREPARE_ONLY: no compilation or test execution performed\n")
            else:
                result_code = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
                receipt["stageResults"].append({"name": "no-window-tests", "exitCode": result_code})
        # Copy only local pinned inputs, never invoke asset/dependency download targets.
        if result_code == 0:
            shutil.copytree(source / "test/tests/testdata", output / "bin/testdata")
            for path in sorted((source / "lib/x64/bin").glob("*.dll")):
                shutil.copy2(path, output / "bin" / path.name)
        receipt["sourceChangesDuringBuild"] = changed(before, BUILD.source_manifest(source, source))
        receipt["testChangesDuringBuild"] = changed(test_before, test_manifest(source))
        receipt["dependencyChangesDuringBuild"] = changed(dependencies, BUILD.dependency_manifest(source))
        receipt["sdkChangesDuringBuild"] = changed(sdk, sdk_manifest(env))
        receipt["artifactSha256"] = {path.relative_to(output).as_posix(): BUILD.sha256(path)
                                     for path in sorted((output / "bin").rglob("*")) if path.is_file()}
        expected = ["bin/libopenrct2.lib", "bin/libopenrct2renderer.lib"]
        if not args.prepare_only:
            expected.append("bin/tests-no-window.exe")
        receipt["missingArtifacts"] = [path for path in expected if path not in receipt["artifactSha256"]]
        passed = result_code == 0 and not receipt["missingArtifacts"] and not any(
            receipt[key] for key in ("sourceChangesDuringBuild", "testChangesDuringBuild",
                                    "dependencyChangesDuringBuild", "sdkChangesDuringBuild"))
        receipt["status"] = ("prepared" if args.prepare_only else "pass") if passed else "fail"
        receipt["exitCode"] = result_code
        exit_code = 0 if passed else 1
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        receipt["error"] = str(error)
    finally:
        if (output / "build.log").is_file():
            receipt["buildLogSha256"] = BUILD.sha256(output / "build.log")
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: receipt[key] for key in ("status", "error", "missingArtifacts") if key in receipt}))
    print("No-window build receipt:", output / "receipt.json")
    raise SystemExit(exit_code)


if __name__ == "__main__":
    main()
