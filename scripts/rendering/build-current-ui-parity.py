"""Build an isolated actual-UI frozen oracle or Vulkan candidate without editing renderer sources.

--source-root selects the current or extracted frozen tree. --harness-root selects
the identical test/ui-parity driver and SDL present hook. All objects, libraries,
generated build metadata and evidence stay in a new --output directory.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET


NS = "http://schemas.microsoft.com/developer/msbuild/2003"
ET.register_namespace("", NS)


def has_viewport_diagnostics(source):
    header = source / "src/openrct2/interface/ViewportPaintDiagnostics.h"
    return (header.is_file() and
            "#define OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS_VERSION 1" in header.read_text(encoding="utf-8"))


def tag(name):
    return "{" + NS + "}" + name


def child(parent, name, text=None, **attributes):
    element = ET.SubElement(parent, tag(name), attributes)
    element.text = text
    return element


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_manifest(source, harness):
    paths = []
    for directory in (source / "src", source / "data"):
        paths.extend(path for path in directory.rglob("*") if path.is_file())
    paths.extend(path for path in source.iterdir() if path.is_file()
                 and path.suffix in (".props", ".proj", ".txt", ".json"))
    result = {"source/" + path.relative_to(source).as_posix(): sha256(path) for path in sorted(set(paths))}
    result.update({"harness/" + path.relative_to(harness).as_posix(): sha256(path)
                   for path in sorted((harness / "test/ui-parity").rglob("*")) if path.is_file()})
    return result


def dependency_manifest(source):
    dependency = source / "lib/x64"
    return {path.relative_to(dependency).as_posix(): sha256(path)
            for directory in (dependency / "include", dependency / "lib", dependency / "bin")
            if directory.exists() for path in sorted(directory.rglob("*")) if path.is_file()}


def compiler_identity(msbuild, project, common, env, output):
    command = [msbuild, str(project), *common,
               "/getProperty:VCToolsInstallDir,VCToolsVersion,WindowsSdkDir,WindowsTargetPlatformVersion,PlatformToolset"]
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (output / "compiler-properties.log").write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise RuntimeError("Cannot identify compiler; see compiler-properties.log")
    properties = json.loads(result.stdout[result.stdout.index("{"):])["Properties"]
    compiler_bin = Path(properties["VCToolsInstallDir"]) / "bin/Hostx64/x64"
    files = [Path(msbuild)] + [compiler_bin / name for name in ("cl.exe", "link.exe", "lib.exe", "c1xx.dll", "c2.dll")]
    if any(not path.is_file() for path in files):
        raise RuntimeError("Expected compiler identity input is absent: " + str(files))
    return {"properties": properties, "sha256": {str(path): sha256(path) for path in files}, "query": command}


def compile_inputs(manifest, vulkan):
    # Software-only library builds never compile or embed runtime GLSL/SPIR-V assets.
    return {name: digest for name, digest in manifest.items() if name.startswith("source/")
            and (vulkan or not name.startswith("source/data/shaders/"))}


def vulkan_only_source(source):
    header = source / "src/openrct2/drawing/IDrawingEngine.h"
    return re.search(r"(?m)^\s*#\s*define\s+OPENRCT2_VULKAN_ONLY\s+1\s*$",
                     header.read_text(encoding="utf-8")) is not None


def capture_instrumentation(source, vulkan):
    project = ET.parse(source / "src/openrct2-ui/libopenrct2ui.vcxproj").getroot()
    sdl_hook = any(Path(item.get("Include", "").replace("\\", "/")).name == "HardwareDisplayDrawingEngine.cpp"
                   for item in project.iter(tag("ClCompile")))
    descriptions = []
    if sdl_hook:
        descriptions.append("HardwareDisplayDrawingEngine.cpp force-includes OraclePresentHook.h; wrapper forwards real SDL_RenderPresent once.")
    if vulkan:
        descriptions.append("Vulkan UI and driver enable OPENRCT2_VULKAN_DIAGNOSTICS for named frame capture.")
    if not sdl_hook:
        descriptions.append("No SDL software presentation hook is injected into the UI library.")
    return " ".join(descriptions) + " Source files and mtimes unchanged."


def build_variant(commands):
    keys = ("Configuration", "Platform", "Breakpad", "EnableVulkan", "PreferredToolArchitecture", "VCToolsVersion")
    variants = []
    for command in commands:
        variants.append({key: next((item.split("=", 1)[1] for item in command
                                   if item.startswith("/p:" + key + "=")), None) for key in keys})
    if any(variant != variants[0] for variant in variants):
        raise RuntimeError("Inconsistent compiler variant across recorded build commands")
    return variants[0]


def successful_stages(receipt, receipt_path):
    if "stageResults" in receipt:
        return {stage["name"] for stage in receipt["stageResults"] if stage["exitCode"] == 0}, None
    # Compatibility for the first harness build, whose receipt predates explicit
    # per-stage status. Preserve that receipt and attest only the verified log's
    # unambiguous successful MSBuild summaries, matched to its exact commands.
    log_path = receipt_path.parent / "build.log"
    if sha256(log_path) != receipt["buildLogSha256"]:
        raise RuntimeError("Previous build log no longer matches its receipt")
    segments = re.split(r"(?m)^COMMAND (\[.*\])\r?$", log_path.read_text(encoding="utf-8"))
    successful = set()
    evidence = []
    for index, name in enumerate(("core", "ui", "driver")):
        if index >= len(receipt["commands"]) or 2 * index + 2 >= len(segments):
            break
        command = json.loads(segments[2 * index + 1])
        block = segments[2 * index + 2]
        if command != receipt["commands"][index]:
            raise RuntimeError("Previous log command differs from receipt")
        success = block.count("Build succeeded.") == 1 and "Build FAILED." not in block and "0 Error(s)" in block
        if success:
            successful.add(name)
            evidence.append({"name": name, "exitCode": 0, "evidence": "One MSBuild success summary, zero errors, exact command match"})
    return successful, {"sourceReceipt": str(receipt_path), "sourceReceiptSha256": sha256(receipt_path),
                        "buildLogSha256": receipt["buildLogSha256"], "stageResults": evidence}


def receipt_build_workspace(receipt, receipt_path):
    cache = receipt.get("incrementalCache")
    if cache is None:
        return receipt_path.parent
    private = Path(cache["privateWorkspace"]).resolve()
    root = Path(receipt["sourceRoot"]).resolve() / "obj/vulkan-parity/incremental-ui-cache"
    if (cache.get("version") != 1 or cache.get("immutableSnapshot") is not True
            or Path(cache["snapshotDirectory"]).resolve() != receipt_path.parent.resolve()
            or private.parent.parent != root or not re.fullmatch(r"generation-[0-9a-f]{32}", private.name)
            or receipt.get("compilerConcurrency") != "serial" or not receipt.get("commands")):
        raise RuntimeError("Invalid incremental build snapshot/workspace declaration")
    for command in receipt["commands"]:
        if (Path(command[1]).resolve().parent != private
                or "/p:OutDir=" + str(private / "bin") + os.sep not in command):
            raise RuntimeError("Incremental receipt command does not use its declared private workspace")
    return private


def prepare_reuse(path, before, dependencies, toolchain, common, vulkan, output):
    if path is None:
        return {}, None
    receipt_path = path.resolve(strict=True)
    if receipt_path.is_dir():
        receipt_path /= "receipt.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    previous_workspace = receipt_build_workspace(receipt, receipt_path)
    if receipt.get("sourceChangesDuringBuild") or receipt.get("dependencyChangesDuringBuild"):
        raise RuntimeError("Cannot reuse libraries from a build with source/dependency mutations")
    successful, attestation = successful_stages(receipt, receipt_path)
    if attestation is not None:
        (output / "reused-stage-attestation.json").write_text(json.dumps(attestation, indent=2) + "\n", encoding="utf-8")
    matches = (compile_inputs(receipt["sourceSha256"], vulkan) == compile_inputs(before, vulkan)
               and receipt["dependencySha256"] == dependencies
               and receipt["toolchain"]["sha256"] == toolchain["sha256"]
               and receipt["toolchain"]["properties"] == toolchain["properties"]
               and build_variant(receipt["commands"]) == build_variant([common]))
    reuse = {}
    for name, library, project_name in (
            ("core", "libopenrct2.lib", "ui-parity-core.vcxproj"),
            ("renderer", "libopenrct2renderer.lib", "ui-parity-renderer.vcxproj"),
            ("ui", "libopenrct2ui-parity.lib", "ui-parity-library.vcxproj")):
        if not matches or name not in successful:
            continue
        if not (output / project_name).is_file() or project_name not in receipt["generatedProjectSha256"]:
            continue
        if name == "ui" and receipt["sourceSha256"].get("harness/test/ui-parity/OraclePresentHook.h") != before.get("harness/test/ui-parity/OraclePresentHook.h"):
            continue
        old_project = receipt_path.parent / project_name
        if sha256(old_project) != receipt["generatedProjectSha256"][project_name]:
            raise RuntimeError("Previous generated compiler metadata changed: " + str(old_project))
        # Only relocation of the isolated output directory is ignored. Every
        # compiler metadata field, include, source path and hook must still match.
        old_metadata = old_project.read_text(encoding="utf-8").replace(str(previous_workspace), "@OUTPUT@")
        new_metadata = (output / project_name).read_text(encoding="utf-8").replace(str(output), "@OUTPUT@")
        if old_metadata != new_metadata:
            continue
        relative = "bin/" + library
        expected = receipt["artifactSha256"].get(relative)
        original = receipt_path.parent / relative
        if expected is None or not original.is_file() or sha256(original) != expected:
            raise RuntimeError("Reusable library is absent or its bytes changed: " + str(original))
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, destination)
        reuse[name] = {"sourceReceipt": str(receipt_path), "sourceReceiptSha256": sha256(receipt_path),
                       "librarySha256": expected}
    return reuse, attestation


def find_msbuild(explicit):
    if explicit:
        return str(explicit.resolve(strict=True))
    found = shutil.which("MSBuild.exe")
    if found:
        return found
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.exists():
        candidates = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                               "Microsoft.Component.MSBuild", "-find", r"MSBuild\**\Bin\MSBuild.exe"],
                                              text=True).splitlines()
        if candidates:
            return candidates[0]
    raise SystemExit("MSBuild not found; supply --msbuild")


def write_xml(path, document):
    ET.indent(document, space="  ")
    ET.ElementTree(document).write(path, encoding="utf-8", xml_declaration=True)


def make_library_project(source, harness, output, vulkan, ui, renderer=False):
    original = source / ("src/openrct2-renderer/libopenrct2renderer.vcxproj" if renderer else
                         "src/openrct2-ui/libopenrct2ui.vcxproj" if ui else "src/openrct2/libopenrct2.vcxproj")
    project = ET.parse(original).getroot()
    for group in project.findall(tag("ItemGroup")):
        for element in list(group):
            include = element.get("Include")
            if element.tag == tag("ProjectReference") or ui and element.tag == tag("ClCompile") and include == "Ui.cpp":
                group.remove(element)
                continue
            if include and element.tag in (tag("ClCompile"), tag("ClInclude"), tag("ResourceCompile")):
                absolute = (original.parent / include.replace("\\", "/")).resolve()
                element.set("Include", str(absolute))
                if element.tag == tag("ClCompile"):
                    # A directory output lets MSBuild batch /MP compilations while separating duplicate basenames.
                    object_directory = output / "int" / ("renderer" if renderer else "ui" if ui else "core") / "objects" / absolute.parent.relative_to(source)
                    object_directory.mkdir(parents=True, exist_ok=True)
                    child(element, "ObjectFileName", str(object_directory) + os.sep)
                    if ui and absolute.name == "HardwareDisplayDrawingEngine.cpp":
                        child(element, "PrecompiledHeader", "NotUsing")
                        child(element, "ForcedIncludeFiles", str(harness / "test/ui-parity/OraclePresentHook.h"))
                    if ui and not vulkan and absolute.name == "UiContext.cpp":
                        # Frozen source declares drawingEngine outside #ifdef ENABLE_VULKAN.
                        # Keep its source unchanged and suppress only this software-only unused-local warning.
                        child(element, "DisableSpecificWarnings", "4189;%(DisableSpecificWarnings)")
    for element in project.findall(tag("Import")):
        path = element.get("Project", "")
        if "$" not in path:
            element.set("Project", str((original.parent / path.replace("\\", "/")).resolve()))
    # Use the absolute PCH path without adding the source directory to /I: core's
    # Limits.h would otherwise shadow the C runtime limits.h on Windows.
    definitions = ET.Element(tag("ItemDefinitionGroup"))
    compile_options = child(definitions, "ClCompile")
    pch = original.parent / ("openrct2ui_pch.h" if ui else "openrct2_pch.h")
    if not renderer:
        child(compile_options, "PrecompiledHeaderFile", str(pch), Condition="'$(UsePCH)'=='true'")
        child(compile_options, "ForcedIncludeFiles", str(pch), Condition="'$(UsePCH)'=='true'")
    if vulkan and ui:
        child(compile_options, "PreprocessorDefinitions", "OPENRCT2_VULKAN_DIAGNOSTICS;%(PreprocessorDefinitions)")
    if not ui and not renderer and has_viewport_diagnostics(source):
        child(compile_options, "PreprocessorDefinitions", "OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS;%(PreprocessorDefinitions)")
    project.insert(len(project) - 1, definitions)
    path = output / ("ui-parity-renderer.vcxproj" if renderer else "ui-parity-library.vcxproj" if ui else "ui-parity-core.vcxproj")
    write_xml(path, project)
    return path


def make_driver_project(source, harness, output, vulkan):
    project = ET.Element(tag("Project"), {"ToolsVersion": "Current"})
    configurations = child(project, "ItemGroup", Label="ProjectConfigurations")
    configuration = child(configurations, "ProjectConfiguration", Include="Release|x64")
    child(configuration, "Configuration", "Release")
    child(configuration, "Platform", "x64")
    properties = child(project, "PropertyGroup", Label="Globals")
    child(properties, "ProjectGuid", "{A8C051B0-5011-4F63-A784-A4129110B862}")
    child(properties, "ProjectName", "ui-parity")
    properties = child(project, "PropertyGroup", Label="Configuration")
    child(properties, "ConfigurationType", "Application")
    child(project, "Import", Project=str(source / "openrct2.common.props"))
    definitions = child(project, "ItemDefinitionGroup")
    compiler = child(definitions, "ClCompile")
    child(compiler, "PrecompiledHeader", "NotUsing")
    if vulkan:
        child(compiler, "PreprocessorDefinitions", "ENABLE_VULKAN;OPENRCT2_VULKAN_DIAGNOSTICS;%(PreprocessorDefinitions)")
        child(compiler, "AdditionalIncludeDirectories", r"$(VULKAN_SDK)\Include;%(AdditionalIncludeDirectories)")
    if has_viewport_diagnostics(source):
        definitions_node = compiler.find(tag("PreprocessorDefinitions"))
        if definitions_node is None:
            child(compiler, "PreprocessorDefinitions", "OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS;%(PreprocessorDefinitions)")
        else:
            definitions_node.text = "OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS;" + definitions_node.text
    linker = child(definitions, "Link")
    renderer_library = "$(OutDir)libopenrct2renderer.lib;" if (source / "src/openrct2-renderer/libopenrct2renderer.vcxproj").is_file() else ""
    child(linker, "AdditionalDependencies", "$(VulkanAdditionalDependencies)$(OutDir)libopenrct2.lib;$(OutDir)libopenrct2ui-parity.lib;" + renderer_library + "%(AdditionalDependencies)")
    child(linker, "SubSystem", "Console")
    child(linker, "StackReserveSize", "8388608")
    sources = child(project, "ItemGroup")
    for name in ("UiParityMain.cpp", "SdlCapture.cpp"):
        child(sources, "ClCompile", Include=str(harness / "test/ui-parity" / name))
    child(project, "Import", Project=r"$(VCTargetsPath)\Microsoft.Cpp.targets")
    path = output / "ui-parity-driver.vcxproj"
    write_xml(path, project)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--harness-root", type=Path)
    parser.add_argument("--msbuild", type=Path)
    parser.add_argument("--toolset-version")
    parser.add_argument("--enable-vulkan", action="store_true")
    parser.add_argument("--serial-compile", action="store_true", help="Disable compiler /MP in every generated project; MSBuild already uses one node")
    parser.add_argument("--serial-driver-only", action="store_true",
                        help="Require exact library reuse and compile only the driver serially; preserve prior library metadata")
    parser.add_argument("--reuse-build", type=Path, help="Receipt or build directory whose matching libraries may be reused")
    args = parser.parse_args()
    if args.serial_driver_only and (args.reuse_build is None or args.serial_compile):
        parser.error("--serial-driver-only requires --reuse-build and cannot be combined with --serial-compile")
    library_concurrency = "serial" if args.serial_compile else "project-default"
    reuse_receipt_path = None
    reuse_receipt_hash = None
    if args.serial_driver_only:
        reuse_receipt_path = args.reuse_build.resolve(strict=True)
        if reuse_receipt_path.is_dir():
            reuse_receipt_path /= "receipt.json"
        reuse_receipt_hash = sha256(reuse_receipt_path)
        prior_receipt = json.loads(reuse_receipt_path.read_text(encoding="utf-8"))
        library_concurrency = prior_receipt.get("compilerConcurrency", "project-default")
        if library_concurrency not in ("serial", "project-default"):
            raise RuntimeError("Unsupported reusable library compiler concurrency policy")
    workspace = Path(__file__).resolve().parents[2]
    source = (args.source_root or workspace).resolve(strict=True)
    harness = (args.harness_root or workspace).resolve(strict=True)
    if vulkan_only_source(source) and not args.enable_vulkan:
        parser.error("Vulkan-only source requires --enable-vulkan for the UI capture build")
    output = args.output.resolve()
    if output.exists() or workspace not in output.parents:
        raise SystemExit("--output must be a new directory inside the working workspace")
    for path in (source / "src/openrct2/libopenrct2.vcxproj", source / "lib/x64",
                 harness / "test/ui-parity/OraclePresentHook.h"):
        if not path.exists():
            raise SystemExit("Required build input is absent: " + str(path))
    output.mkdir(parents=True)
    core = make_library_project(source, harness, output, args.enable_vulkan, ui=False)
    renderer = None
    if (source / "src/openrct2-renderer/libopenrct2renderer.vcxproj").is_file():
        renderer = make_library_project(source, harness, output, args.enable_vulkan, ui=False, renderer=True)
    ui = make_library_project(source, harness, output, args.enable_vulkan, ui=True)
    driver = make_driver_project(source, harness, output, args.enable_vulkan)
    stages = [("core", core, "libopenrct2")]
    if renderer is not None:
        stages.append(("renderer", renderer, "libopenrct2renderer"))
    stages.extend((("ui", ui, "libopenrct2ui-parity"), ("driver", driver, "ui-parity")))
    if library_concurrency == "serial" or args.serial_driver_only:
        for stage_name, project_path, _ in stages:
            if stage_name != "driver" and library_concurrency != "serial":
                continue
            project = ET.parse(project_path).getroot()
            # Item metadata overrides imported and project-wide defaults, including
            # common.props /MP. Apply to every translation unit, including the PCH.
            for item in project.findall("./" + tag("ItemGroup") + "/" + tag("ClCompile")):
                option = item.find(tag("MultiProcessorCompilation"))
                if option is None:
                    option = child(item, "MultiProcessorCompilation")
                option.text = "false"
            write_xml(project_path, project)
    before = source_manifest(source, harness)
    msbuild = find_msbuild(args.msbuild)
    common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
              "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:Breakpad=false",
              "/p:PreferredToolArchitecture=x64",
              "/p:SolutionDir=" + str(source) + os.sep,
              "/p:OutDir=" + str(output / "bin") + os.sep,
              "/p:EnableVulkan=" + str(args.enable_vulkan).lower()]
    if args.toolset_version:
        common.append("/p:VCToolsVersion=" + args.toolset_version)
    commands = []
    result_code = 0
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
    toolchain = compiler_identity(msbuild, core, common, env, output)
    dependency_before = dependency_manifest(source)
    reuse, _ = prepare_reuse(args.reuse_build, before, dependency_before, toolchain, common, args.enable_vulkan, output)
    if args.serial_driver_only:
        required_reuse = {name for name, _, _ in stages if name != "driver"}
        if set(reuse) != required_reuse:
            raise RuntimeError("Driver-only build requires exact reuse of every library; missing: "
                               + ", ".join(sorted(required_reuse - set(reuse))))
        if sha256(reuse_receipt_path) != reuse_receipt_hash:
            raise RuntimeError("Reuse receipt changed during qualification")
    stage_results = []
    with (output / "build.log").open("w", encoding="utf-8") as log:
        for name, project, target in stages:
            command = [msbuild, str(project), *common, "/p:IntDir=" + str(output / "int" / name) + os.sep,
                       "/p:TargetName=" + target]
            commands.append(command)
            if name in reuse:
                stage_results.append({"name": name, "exitCode": 0, "reused": reuse[name]})
                log.write("REUSED " + name + " " + json.dumps(reuse[name]) + "\n")
                log.flush()
                continue
            log.write("\nCOMMAND " + json.dumps(command) + "\n")
            log.flush()
            result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
            stage_results.append({"name": name, "exitCode": result.returncode})
            if result.returncode:
                result_code = result.returncode
                break
    after = source_manifest(source, harness)
    changes = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
    dependency_after = dependency_manifest(source)
    dependency_changes = sorted(name for name in dependency_before.keys() | dependency_after.keys()
                                if dependency_before.get(name) != dependency_after.get(name))
    runtime_dlls = {}
    # Release dependencies are statically linked in the current pinned package.
    # If a pinned package supplies runtime DLLs, copy only those inputs to this isolated bin.
    for path in sorted((source / "lib/x64/bin").glob("*.dll")):
        destination = output / "bin" / path.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        runtime_dlls[path.relative_to(source / "lib/x64").as_posix()] = sha256(destination)
    artifacts = [output / "bin" / name for name in ("libopenrct2.lib", "libopenrct2ui-parity.lib", "ui-parity.exe")]
    if renderer is not None:
        artifacts.append(output / "bin/libopenrct2renderer.lib")
    missing = [str(path) for path in artifacts if not path.is_file()]
    reuse_receipt_unchanged = (reuse_receipt_path is None or
                               (reuse_receipt_path.is_file() and sha256(reuse_receipt_path) == reuse_receipt_hash))
    passed = result_code == 0 and not changes and not dependency_changes and not missing and reuse_receipt_unchanged
    receipt = {
        "schema": 1, "status": "pass" if passed else "fail", "exitCode": result_code,
        "sourceRoot": str(source), "harnessRoot": str(harness), "commands": commands,
        "stageResults": stage_results,
        "compilerConcurrency": library_concurrency,
        "driverCompilerConcurrency": "serial" if args.serial_compile or args.serial_driver_only else "project-default",
        "libraryCompilationPolicy": "reuse-required" if args.serial_driver_only else "compile-or-reuse",
        "reuseReceiptUnchanged": reuse_receipt_unchanged,
        "sourceSha256": before, "sourceChangesDuringBuild": changes, "missingArtifacts": missing,
        "dependencySha256": dependency_before, "dependencyChangesDuringBuild": dependency_changes,
        "runtimeDllSha256": runtime_dlls, "toolchain": toolchain,
        "artifactSha256": {path.relative_to(output).as_posix(): sha256(path) for path in artifacts if path.is_file()},
        "generatedProjectSha256": {path.name: sha256(path) for _, path, _ in stages},
        "buildLogSha256": sha256(output / "build.log"),
        "instrumentation": capture_instrumentation(source, args.enable_vulkan),
    }
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: receipt[key] for key in ("status", "exitCode", "sourceChangesDuringBuild", "missingArtifacts")}))
    print("UI capture build receipt:", output / "receipt.json")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
