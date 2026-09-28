"""Build a separate pinned upstream software screenshot CLI using local inputs only.

Archives pristine upstream source, builds a fresh core, and compiles an external
copy of Cli.cpp with only the reviewed environment-path insertion. Does not run
the game, build assets, download dependencies, or alter the frozen oracle.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import xml.etree.ElementTree as ET
import zipfile


REVISION = "b80a4a84e92be8e07904b38d1032d0bb88280bb4"
TOOLSET = "14.44.35207"
HELPER = Path(__file__).with_name("build-current-ui-parity.py")
SPEC = importlib.util.spec_from_file_location("upstream_build_helpers", HELPER)
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)
tag, child = BUILD.tag, BUILD.child
BUILDER_INPUTS = {str(path): BUILD.sha256(path) for path in (Path(__file__).resolve(), HELPER.resolve())}

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


def file_manifest(paths):
    return {str(path): BUILD.sha256(path) for path in paths if path.is_file()}


def changed(before, after):
    return sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))


def source_manifest(source):
    result = {}
    for directory, dirs, files in os.walk(source):
        # This one added junction exposes immutable local build dependencies;
        # its inputs have their own before/after manifest.
        if Path(directory) == source:
            dirs[:] = [name for name in dirs if name != "lib"]
        for name in files:
            path = Path(directory) / name
            result[path.relative_to(source).as_posix()] = BUILD.sha256(path)
    return result


def extract_pristine(archive, source):
    originals = {}
    with zipfile.ZipFile(archive) as bundle:
        for entry in bundle.infolist():
            target = (source / entry.filename).resolve()
            if source not in target.parents or stat.S_ISLNK(entry.external_attr >> 16):
                raise RuntimeError("Unsafe upstream archive entry: " + entry.filename)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if target.exists():
                raise RuntimeError("Duplicate upstream archive entry: " + entry.filename)
            target.parent.mkdir(parents=True, exist_ok=True)
            data = bundle.read(entry)
            target.write_bytes(data)
            originals[entry.filename] = hashlib.sha256(data).hexdigest()
    if not originals or source_manifest(source) != originals:
        raise RuntimeError("Pristine upstream extraction did not match the archive")
    return originals


def make_cli_project(source, output, wrapper):
    original = source / "src/openrct2-cli/openrct2-cli.vcxproj"
    project = ET.parse(original).getroot()
    compiled = []
    for group in project.findall(tag("ItemGroup")):
        for item in list(group):
            include = item.get("Include")
            if item.tag == tag("ProjectReference"):
                group.remove(item)
            elif include and item.tag == tag("ClCompile"):
                if include != "Cli.cpp":
                    raise RuntimeError("Unexpected upstream CLI translation unit: " + include)
                item.set("Include", str(wrapper))
                compiled.append(include)
            elif include and item.tag in (tag("ResourceCompile"), tag("ClInclude"), tag("None")):
                path = (original.parent / include.replace("\\", "/")).resolve(strict=True)
                if source not in path.parents:
                    raise RuntimeError("CLI project input escaped the source archive")
                item.set("Include", str(path))
    if compiled != ["Cli.cpp"]:
        raise RuntimeError("Unexpected upstream CLI compilation inventory")
    for item in project.findall(tag("Import")):
        value = item.get("Project", "")
        if "$" not in value:
            item.set("Project", str((original.parent / value.replace("\\", "/")).resolve(strict=True)))
    for item in project.iter(tag("LibraryPath")):
        item.text = item.text.replace("$(SolutionDir)bin;", "$(OutDir);")
    links = project.findall(".//" + tag("Link") + "/" + tag("AdditionalDependencies"))
    if len(links) != 1 or links[0].text != "libopenrct2.lib;%(AdditionalDependencies)":
        raise RuntimeError("Unexpected upstream CLI link contract")
    links[0].text = "$(OutDir)libopenrct2.lib;%(AdditionalDependencies)"
    path = output / "upstream-screenshot-cli.vcxproj"
    BUILD.write_xml(path, project)
    return path


def serialise_project(path):
    project = ET.parse(path).getroot()
    for item in project.findall("./" + tag("ItemGroup") + "/" + tag("ClCompile")):
        option = item.find(tag("MultiProcessorCompilation"))
        if option is None:
            option = child(item, "MultiProcessorCompilation")
        option.text = "false"
    # Remove dependency restoration entirely, in addition to IsSolutionBuild.
    for item in list(project.findall(tag("Import"))):
        if Path(item.get("Project", "").replace("\\", "/")).name == "openrct2.deps.targets":
            project.remove(item)
    if list(project.iter(tag("ProjectReference"))):
        raise RuntimeError("Generated project retains a dependency build reference")
    BUILD.write_xml(path, project)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New directory inside this workspace")
    parser.add_argument("--msbuild", type=Path)
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if os.name != "nt":
        raise SystemExit("This bounded builder requires Windows x64 MSBuild")
    if output.exists() or workspace not in output.parents:
        raise SystemExit("--output must be a new directory inside this workspace")
    output.mkdir(parents=True)
    source = output / "source"
    archive = output / "upstream-source.zip"
    receipt = {"schema": 1, "kind": "upstream-screenshot-oracle-build", "status": "fail",
               "referenceRevision": REVISION, "sourceRoot": str(source), "commands": [], "stageResults": [],
               "builderSha256": BUILDER_INPUTS,
               "compilerConcurrency": "serial", "toolsetVersion": TOOLSET, "modifiedOriginalSources": [],
               "limits": "Fresh upstream software CLI with path-only wrapper. No capture or parity result. Runtime park/art/profile inputs must be pinned by a later capture receipt."}
    originals = dependencies = fixed = None
    exit_code = 1
    try:
        git = shutil.which("git")
        if not git:
            raise RuntimeError("Local git executable is required; no fetch is performed")
        resolved = subprocess.check_output([git, "rev-parse", "--verify", REVISION + "^{commit}"], cwd=workspace, text=True).strip()
        if resolved != REVISION:
            raise RuntimeError("Pinned upstream commit is unavailable")
        command = [git, "archive", "--format=zip", "--output=" + str(archive), REVISION]
        subprocess.run(command, cwd=workspace, check=True)
        receipt["archiveCommand"] = command
        receipt["sourceArchiveSha256"] = BUILD.sha256(archive)
        originals = extract_pristine(archive, source)
        receipt["originalSourceSha256"] = originals
        dependency_root = workspace / "lib/x64"
        if not (dependency_root / "include").is_dir() or not any((dependency_root / "lib").glob("*.lib")):
            raise RuntimeError("Local lib/x64 include and library inputs are required; restore is disabled")
        dependencies = BUILD.dependency_manifest(workspace)
        if not dependencies:
            raise RuntimeError("Local dependency manifest is empty")
        receipt["dependencyPath"] = str(dependency_root)
        receipt["dependencySha256"] = dependencies
        link = source / "lib/x64"
        link.parent.mkdir(parents=True)
        quote = lambda value: "'" + str(value).replace("'", "''") + "'"
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path " + quote(link)
                        + " -Target " + quote(dependency_root) + " | Out-Null"], check=True)
        original_cli = (source / "src/openrct2-cli/Cli.cpp").read_bytes()
        text = original_cli.decode("utf-8").replace("\r\n", "\n")
        include, marker = "#include <openrct2/Context.h>", "    auto runGame = CommandLineRun(argv, argc);"
        if text.count(include) != 1 or text.count(marker) != 1:
            raise RuntimeError("Pinned CLI startup no longer matches the reviewed wrapper insertion")
        wrapper = output / "UpstreamCli.cpp"
        wrapper.write_bytes(text.replace(include, "#include <cstdlib>\n" + include).replace(marker, PLUMBING + marker).encode("utf-8"))
        receipt["wrapper"] = {"path": str(wrapper), "originalCliSha256": hashlib.sha256(original_cli).hexdigest(),
                              "sha256": BUILD.sha256(wrapper), "instrumentation": PLUMBING}
        core = BUILD.make_library_project(source, workspace, output, False, ui=False)
        cli = make_cli_project(source, output, wrapper)
        for project in (core, cli):
            serialise_project(project)
        msbuild = BUILD.find_msbuild(args.msbuild)
        env = {key.upper(): value for key, value in os.environ.items()}
        # Do not accept shell-injected compiler/linker flags or restore hooks.
        for key in ("CL", "_CL_", "LINK", "_LINK_", "OPENRCT2_CL_ADDITIONALOPTIONS",
                    "CUSTOMBEFOREMICROSOFTCOMMONPROPS", "CUSTOMAFTERMICROSOFTCOMMONTARGETS"):
            env.pop(key, None)
        common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
                  "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:Breakpad=false",
                  "/p:PreferredToolArchitecture=x64", "/p:VCToolsVersion=" + TOOLSET,
                  "/p:EnableVulkan=false", "/p:VcpkgEnabled=false", "/p:MultiProcessorCompilation=false",
                  "/p:SolutionDir=" + str(source) + os.sep, "/p:OutDir=" + str(output / "bin") + os.sep]
        toolchain = BUILD.compiler_identity(msbuild, core, common, env, output)
        if toolchain["properties"].get("VCToolsVersion") != TOOLSET:
            raise RuntimeError("MSBuild did not resolve the required toolset " + TOOLSET)
        receipt["toolchain"] = toolchain
        fixed_paths = [Path(__file__).resolve(), HELPER.resolve(), archive, wrapper, core, cli]
        fixed_paths.extend(Path(name) for name in toolchain["sha256"])
        fixed = file_manifest(fixed_paths)
        receipt["fixedBuildInputSha256"] = fixed
        receipt["generatedProjectSha256"] = {path.name: BUILD.sha256(path) for path in (core, cli)}
        with (output / "build.log").open("w", encoding="utf-8") as log:
            for name, project, target in (("core", core, "libopenrct2"), ("cli", cli, "upstream-screenshot-cli")):
                command = [msbuild, str(project), *common, "/p:IntDir=" + str(output / "int" / name) + os.sep,
                           "/p:TargetName=" + target]
                receipt["commands"].append(command)
                log.write("COMMAND " + json.dumps(command) + "\n")
                log.flush()
                # RC's icon paths are relative to the pristine resource directory.
                result = subprocess.run(command, cwd=source / "resources", env=env, stdout=log, stderr=subprocess.STDOUT)
                receipt["stageResults"].append({"name": name, "exitCode": result.returncode})
                if result.returncode:
                    raise RuntimeError(name + " compilation failed; see build.log")
        for path in sorted((dependency_root / "bin").glob("*.dll")):
            shutil.copy2(path, output / "bin" / path.name)
        exit_code = 0
    except (OSError, ValueError, RuntimeError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError, SystemExit) as error:
        receipt["error"] = str(error)
    finally:
        try:
            receipt["builderChangesDuringBuild"] = changed(BUILDER_INPUTS, file_manifest(Path(name) for name in BUILDER_INPUTS))
            if originals is not None:
                receipt["sourceChangesDuringBuild"] = changed(originals, source_manifest(source))
            if dependencies is not None:
                receipt["dependencyChangesDuringBuild"] = changed(dependencies, BUILD.dependency_manifest(workspace))
            if fixed is not None:
                receipt["fixedBuildInputChangesDuringBuild"] = changed(fixed, file_manifest(Path(name) for name in fixed))
            receipt["artifactSha256"] = {path.relative_to(output).as_posix(): BUILD.sha256(path)
                                         for path in sorted((output / "bin").rglob("*")) if path.is_file()}
            receipt["runtimeDllSha256"] = {name: digest for name, digest in receipt["artifactSha256"].items()
                                           if name.lower().endswith(".dll")}
            expected_dlls = {"bin/" + name.removeprefix("bin/"): digest for name, digest in (dependencies or {}).items()
                             if name.startswith("bin/") and "/" not in name[4:] and name.lower().endswith(".dll")}
            receipt["runtimeDllCopyMismatch"] = receipt["runtimeDllSha256"] != expected_dlls
            receipt["missingArtifacts"] = [name for name in ("bin/libopenrct2.lib", "bin/upstream-screenshot-cli.exe")
                                           if name not in receipt["artifactSha256"]]
            if any(receipt.get(key) for key in ("builderChangesDuringBuild", "sourceChangesDuringBuild", "dependencyChangesDuringBuild",
                                               "fixedBuildInputChangesDuringBuild", "runtimeDllCopyMismatch", "missingArtifacts")):
                exit_code = 1
            receipt["status"] = "pass" if exit_code == 0 else "fail"
            if (output / "build.log").is_file():
                receipt["buildLogSha256"] = BUILD.sha256(output / "build.log")
        except (OSError, ValueError) as error:
            exit_code = 1
            receipt["status"] = "fail"
            receipt["verificationError"] = str(error)
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: receipt[key] for key in ("status", "error", "missingArtifacts") if key in receipt}))
    print("Upstream build receipt:", output / "receipt.json")
    raise SystemExit(exit_code)


if __name__ == "__main__":
    main()
