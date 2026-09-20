"""Build only the archived CLI driver against an exactly verified frozen UI core.

No core compilation fallback, source-tree edits, downloads or renderer changes.
This creates new binary provenance; it never certifies an existing CLI binary.
"""

import argparse
import ast
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile


HELPER_PATH = Path(__file__).with_name("build-current-ui-parity.py")
SPEC = importlib.util.spec_from_file_location("ui_parity_builder", HELPER_PATH)
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)
tag, child = BUILD.tag, BUILD.child

PLUMBING = """    // Oracle harness only: isolate profile and immutable asset paths before screenshot dispatch.
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_USER_PATH"))
        gCustomUserDataPath = path;
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_DATA_PATH"))
        gCustomOpenRCT2DataPath = path;
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_RCT1_PATH"))
        gCustomRCT1DataPath = path;
    if (const auto* path = std::getenv("OPENRCT2_ORACLE_RCT2_PATH"))
        gCustomRCT2DataPath = path;
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def changed(before, after):
    return sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))


def file_manifest(paths):
    return {str(path): BUILD.sha256(path) for path in paths if path.is_file()}


def check_frozen_inputs(workspace, source, plumbing_path):
    reference_path = workspace / "docs/vulkan-software-reference.json"
    reference = json.loads(reference_path.read_text(encoding="utf-8"))
    package = (workspace / reference["localReference"]).resolve(strict=True)
    archive = package / "source.zip"
    package_manifest_path = package / "manifest.json"
    if (BUILD.sha256(archive) != reference["sourceArchive"]["sha256"]
            or BUILD.sha256(package_manifest_path) != reference["manifest"]["sha256"]):
        raise RuntimeError("Frozen archive/package manifest changed")
    extraction_path = source / "oracle-ui-source-receipt.json"
    extraction = json.loads(extraction_path.read_text(encoding="utf-8"))
    plumbing_receipt = json.loads(plumbing_path.read_text(encoding="utf-8"))
    if (extraction.get("kind") != "frozen-ui-oracle" or extraction.get("modifiedOriginalSources") != []
            or extraction.get("referenceRevision") != reference["revision"]
            or extraction.get("sourceArchiveSha256") != reference["sourceArchive"]["sha256"]
            or plumbing_receipt.get("sourceArchiveSha256") != reference["sourceArchive"]["sha256"]
            or plumbing_receipt.get("referenceRevision") != reference["revision"]):
        raise RuntimeError("Extraction receipts do not describe the unchanged frozen reference")
    hashes = {}
    with zipfile.ZipFile(archive) as archived:
        for entry in archived.infolist():
            if entry.is_dir():
                continue
            path = (source / entry.filename).resolve(strict=True)
            if source not in path.parents or entry.filename in hashes:
                raise RuntimeError("Unsafe or duplicate frozen archive entry")
            data = archived.read(entry)
            hashes[entry.filename] = digest(data)
            if BUILD.sha256(path) != hashes[entry.filename]:
                raise RuntimeError("Original frozen source changed: " + entry.filename)
        original_cli = archived.read("src/openrct2-cli/Cli.cpp")
    if (hashes != extraction["originalSourceSha256"]
            or hashes != plumbing_receipt["originalSourceSha256"]):
        raise RuntimeError("Archive file inventory differs from extraction receipts")
    prepare = workspace / "scripts/rendering/prepare-frozen-oracle.py"
    assignments = [node for node in ast.walk(ast.parse(prepare.read_text(encoding="utf-8")))
                   if isinstance(node, ast.Assign)
                   and any(isinstance(target, ast.Name) and target.id == "plumbing" for target in node.targets)]
    if len(assignments) != 1 or ast.literal_eval(assignments[0].value) != PLUMBING:
        raise RuntimeError("Reviewed CLI environment instrumentation changed")
    original_text = original_cli.decode("utf-8").replace("\r\n", "\n")
    include = "#include <openrct2/Context.h>"
    marker = "    auto runGame = CommandLineRun(argv, argc);"
    if original_text.count(include) != 1 or original_text.count(marker) != 1:
        raise RuntimeError("Archived CLI startup markers changed")
    instrumented = original_text.replace(include, "#include <cstdlib>\n" + include).replace(marker, PLUMBING + marker).encode("utf-8")
    if (plumbing_receipt.get("instrumentation") != PLUMBING
            or plumbing_receipt.get("instrumentedFile") != "src/openrct2-cli/Cli.cpp"
            or plumbing_receipt.get("instrumentedSha256") != digest(instrumented)):
        raise RuntimeError("CLI candidate differs from the exact reviewed startup insertion")
    assets = {}
    manifest = json.loads(package_manifest_path.read_text(encoding="utf-8"))
    for name, expected in manifest["files"].items():
        if name.startswith("package/data/"):
            path = (package / name).resolve(strict=True)
            if package not in path.parents or BUILD.sha256(path) != expected["sha256"]:
                raise RuntimeError("Frozen runtime asset changed: " + name)
            assets[name] = expected["sha256"]
    if not assets:
        raise RuntimeError("Frozen runtime asset inventory is empty")
    evidence = {"referenceRevision": reference["revision"], "sourceArchiveSha256": BUILD.sha256(archive),
                "originalSourceSha256": hashes, "frozenAssetSha256": assets,
                "receiptSha256": file_manifest([reference_path, package_manifest_path, extraction_path, plumbing_path, prepare]),
                "archivedCliSha256": digest(original_cli), "instrumentedCliSha256": digest(instrumented)}
    return instrumented, evidence


def make_cli_project(source, output, cli):
    original = source / "src/openrct2-cli/openrct2-cli.vcxproj"
    project = ET.parse(original).getroot()
    compiled = []
    resources = []
    for group in project.findall(tag("ItemGroup")):
        for item in list(group):
            include = item.get("Include")
            if item.tag == tag("ProjectReference"):
                group.remove(item)
            elif include and item.tag == tag("ClCompile"):
                if include != "Cli.cpp":
                    raise RuntimeError("Review additional archived CLI translation unit: " + include)
                item.set("Include", str(cli))
                child(item, "MultiProcessorCompilation", "false")
                compiled.append(include)
            elif include and item.tag in (tag("ResourceCompile"), tag("ClInclude"), tag("None")):
                path = (original.parent / include.replace("\\", "/")).resolve(strict=True)
                if source not in path.parents:
                    raise RuntimeError("Archived CLI input escapes source root")
                item.set("Include", str(path))
                if item.tag == tag("ResourceCompile"):
                    resources.append(path)
    if compiled != ["Cli.cpp"] or resources != [source / "resources/OpenRCT2.rc"]:
        raise RuntimeError("Unexpected archived CLI compilation/resource inventory")
    for item in project.findall(tag("Import")):
        value = item.get("Project", "")
        if "$" not in value:
            item.set("Project", str((original.parent / value.replace("\\", "/")).resolve(strict=True)))
    for item in project.iter(tag("LibraryPath")):
        item.text = item.text.replace("$(SolutionDir)bin;", "$(OutDir);")
    links = project.findall(".//" + tag("Link") + "/" + tag("AdditionalDependencies"))
    if len(links) != 1 or links[0].text != "libopenrct2.lib;%(AdditionalDependencies)":
        raise RuntimeError("Archived CLI link contract changed")
    links[0].text = "$(OutDir)libopenrct2.lib;%(AdditionalDependencies)"
    path = output / "frozen-screenshot-cli.vcxproj"
    BUILD.write_xml(path, project)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reuse-build", type=Path, required=True, help="Successful frozen UI build receipt, e.g. UI17")
    parser.add_argument("--plumbing-receipt", type=Path, required=True, help="Reviewed prepare-frozen-oracle extraction receipt")
    parser.add_argument("--msbuild", type=Path)
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists() or workspace not in output.parents:
        raise SystemExit("Use a new output directory inside the workspace")
    output.mkdir(parents=True)
    receipt = {"schema": 1, "kind": "frozen-screenshot-cli-build", "status": "fail", "compilerConcurrency": "serial",
               "commands": [], "stageResults": [], "builderSha256": file_manifest([Path(__file__).resolve(), HELPER_PATH.resolve()])}
    exit_code = 1
    try:
        library_path = args.reuse_build.resolve(strict=True)
        if library_path.is_dir():
            library_path /= "receipt.json"
        libraries = json.loads(library_path.read_text(encoding="utf-8"))
        if (libraries.get("status") != "pass" or libraries.get("sourceChangesDuringBuild")
                or libraries.get("dependencyChangesDuringBuild")
                or BUILD.sha256(library_path.parent / "build.log") != libraries["buildLogSha256"]):
            raise RuntimeError("A successful, unchanged frozen UI build is required")
        source = Path(libraries["sourceRoot"]).resolve(strict=True)
        if workspace not in source.parents:
            raise RuntimeError("Frozen source root must be the receipt-named extraction inside this workspace")
        plumbing_path = args.plumbing_receipt.resolve(strict=True)
        cli_bytes, frozen_before = check_frozen_inputs(workspace, source, plumbing_path)
        before = BUILD.source_manifest(source, workspace)
        dependencies = BUILD.dependency_manifest(source)
        if BUILD.compile_inputs(before, False) != BUILD.compile_inputs(libraries["sourceSha256"], False):
            raise RuntimeError("Frozen source differs from reused core; no compilation fallback")
        variant = BUILD.build_variant(libraries["commands"])
        if variant.get("EnableVulkan") != "false":
            raise RuntimeError("Frozen software core receipt must disable Vulkan")
        core = BUILD.make_library_project(source, workspace, output, False, ui=False)
        if libraries.get("compilerConcurrency", "project-default") != "project-default":
            raise RuntimeError("This bounded builder requires the original project-default frozen core metadata")
        cli = output / "Cli.cpp"
        cli.write_bytes(cli_bytes)
        project = make_cli_project(source, output, cli)
        env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
        msbuild = BUILD.find_msbuild(args.msbuild)
        common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
                  "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:Breakpad=false",
                  "/p:PreferredToolArchitecture=x64", "/p:SolutionDir=" + str(source) + os.sep,
                  "/p:OutDir=" + str(output / "bin") + os.sep, "/p:EnableVulkan=false"]
        if variant.get("VCToolsVersion"):
            common.append("/p:VCToolsVersion=" + variant["VCToolsVersion"])
        toolchain = BUILD.compiler_identity(msbuild, core, common, env, output)
        reuse, _ = BUILD.prepare_reuse(library_path, before, dependencies, toolchain, common, False, output)
        if set(reuse) != {"core"}:
            raise RuntimeError("Frozen core failed exact source/dependency/compiler/project/binary reuse checks")
        generated_before = file_manifest([core, project, cli])
        fixed_paths = [Path(__file__).resolve(), HELPER_PATH.resolve(), library_path,
                       output / "bin/libopenrct2.lib"] + [Path(name) for name in toolchain["sha256"]]
        fixed_before = file_manifest(fixed_paths)
        command = [msbuild, str(project), *common, "/p:IntDir=" + str(output / "int/cli") + os.sep,
                   "/p:TargetName=openrct2-cli"]
        receipt.update({"sourceRoot": str(source), "sourceSha256": before, "dependencySha256": dependencies,
                        "frozenInputs": frozen_before, "libraryReceipt": str(library_path),
                        "libraryReceiptSha256": BUILD.sha256(library_path), "reusedLibraries": reuse,
                        "toolchain": toolchain, "generatedInputSha256": generated_before,
                        "fixedBuildInputSha256": fixed_before, "commands": [command]})
        with (output / "build.log").open("w", encoding="utf-8") as log:
            log.write("VERIFIED_FROZEN_CORE_REUSE " + json.dumps(reuse) + "\nCOMMAND " + json.dumps(command) + "\n")
            log.flush()
            # Original RC contains logo/icon.ico relative to its resource directory.
            result = subprocess.run(command, cwd=source / "resources", env=env, stdout=log, stderr=subprocess.STDOUT)
        receipt["stageResults"] = [{"name": "frozen-cli-driver", "exitCode": result.returncode}]
        if result.returncode == 0:
            for path in sorted((source / "lib/x64/bin").glob("*.dll")):
                shutil.copy2(path, output / "bin" / path.name)
        _, frozen_after = check_frozen_inputs(workspace, source, plumbing_path)
        receipt["frozenInputsChangedDuringBuild"] = frozen_before != frozen_after
        receipt["sourceChangesDuringBuild"] = changed(before, BUILD.source_manifest(source, workspace))
        receipt["dependencyChangesDuringBuild"] = changed(dependencies, BUILD.dependency_manifest(source))
        receipt["generatedInputChangesDuringBuild"] = changed(generated_before, file_manifest([core, project, cli]))
        receipt["fixedBuildInputChangesDuringBuild"] = changed(fixed_before, file_manifest(fixed_paths))
        receipt["artifactSha256"] = {path.relative_to(output).as_posix(): BUILD.sha256(path)
                                     for path in sorted((output / "bin").rglob("*")) if path.is_file()}
        receipt["runtimeDllSha256"] = {name: value for name, value in receipt["artifactSha256"].items() if name.endswith(".dll")}
        expected_dlls = {"bin/" + path.name: dependencies["bin/" + path.name]
                         for path in sorted((source / "lib/x64/bin").glob("*.dll"))}
        receipt["runtimeDllCopyMismatch"] = receipt["runtimeDllSha256"] != expected_dlls
        receipt["missingArtifacts"] = [name for name in ("bin/libopenrct2.lib", "bin/openrct2-cli.exe")
                                       if name not in receipt["artifactSha256"]]
        passed = result.returncode == 0 and not any(receipt[key] for key in (
            "frozenInputsChangedDuringBuild", "sourceChangesDuringBuild", "dependencyChangesDuringBuild",
            "generatedInputChangesDuringBuild", "fixedBuildInputChangesDuringBuild", "runtimeDllCopyMismatch", "missingArtifacts"))
        receipt["status"] = "pass" if passed else "fail"
        receipt["exitCode"] = result.returncode
        receipt["limits"] = "New isolated CLI build with frozen core and reviewed startup paths; no main UI or pixel parity acceptance. Licensed game assets are pinned by subsequent runs."
        exit_code = 0 if passed else 1
    except (OSError, ValueError, RuntimeError, KeyError, zipfile.BadZipFile) as error:
        receipt["error"] = str(error)
    finally:
        if (output / "build.log").is_file():
            receipt["buildLogSha256"] = BUILD.sha256(output / "build.log")
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: receipt[key] for key in ("status", "error", "missingArtifacts") if key in receipt}))
    raise SystemExit(exit_code)


if __name__ == "__main__":
    main()
