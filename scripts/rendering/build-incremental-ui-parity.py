"""Serial current-tree Vulkan UI builds with a private tracked cache and immutable snapshots.

This is opt-in. Existing isolated/frozen/reuse builders retain their behavior.
The first build is full. Only unambiguously newer C/C++ content edits may use
MSBuild's tracked dependencies; stale timestamps and all other input changes
start an empty generation. No source mtimes or historical evidence are modified.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import traceback
import uuid

HELPER = Path(__file__).with_name("build-current-ui-parity.py")
spec = importlib.util.spec_from_file_location("ui_build", HELPER)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def digest_json(value):
    return build.hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def manifest(directory):
    files = {}
    for path in sorted(directory.rglob("*")):
        require(not path.is_symlink(), "Private build trees must not contain symbolic links: " + str(path))
        require(directory.resolve() in path.resolve().parents, "Build tree entry resolves outside its root: " + str(path))
        if path.is_file():
            files[path.relative_to(directory).as_posix()] = build.sha256(path)
    return files


def diff(before, after):
    return sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))


def source_path(source, key):
    prefix, name = key.split("/", 1)
    require(prefix in ("source", "harness"), "Unknown source inventory prefix")
    path = (source / name).resolve(strict=True)
    require(source in path.parents, "Source input escapes workspace")
    return path


def choose_generation(cache, state, signature, before, source, now_ns):
    if not state or not state.get("usable"):
        return None, "No completed usable cache generation", []
    previous_snapshot = Path(state["snapshotDirectory"]).resolve(strict=True)
    require(source in previous_snapshot.parents and cache not in previous_snapshot.parents,
            "Prior immutable snapshot is outside its qualified workspace")
    require(manifest(previous_snapshot) == state["publishedSha256"], "Prior immutable snapshot changed; refusing cache reuse")
    previous_receipt = json.loads((previous_snapshot / "receipt.json").read_text(encoding="utf-8"))
    archived_state = previous_snapshot / "cache-output-state.json"
    require(previous_receipt.get("status") == "pass"
            and build.sha256(archived_state) == previous_receipt["incrementalCache"]["outputStateSha256"],
            "Archived cache-state proof does not match the prior successful receipt")
    require({key: value for key, value in state.items() if key != "publishedSha256"}
            == json.loads(archived_state.read_text(encoding="utf-8")),
            "Mutable cache source/timestamp metadata differs from archived qualification")
    work = Path(state["privateWorkspace"]).resolve(strict=True)
    require(work.parent == cache and re.fullmatch(r"generation-[0-9a-f]{32}", work.name), "Invalid private generation")
    require(manifest(work) == state["workspaceSha256"], "Private objects, tracking files or outputs changed outside the builder")
    if signature != state["signature"]:
        return None, "Compiler/options/environment/dependencies/generated metadata changed", []
    old = state["sourceSha256"]
    changes = diff(old, before)
    if old.keys() != before.keys():
        return None, "Source membership changed", changes
    extensions = {".c", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl", ".inc"}
    for key in changes:
        path = source_path(source, key)
        if path.suffix.lower() not in extensions or not key.startswith(("source/src/", "harness/test/ui-parity/")):
            return None, "Non-C/C++ input changed: " + key, changes
        stamp = path.stat().st_mtime_ns
        # Account for coarse timestamp comparisons. Copy-Item/copy2 preserved or
        # older times must never result in a falsely successful skipped compile.
        minimum = max(state["newestWorkspaceMtimeNs"], state["inputMtimeNs"][key]) + 2_000_000_000
        if stamp <= minimum or stamp > now_ns:
            return None, "Changed input has stale, preserved, future or ambiguous mtime: " + key, changes
    return work, "Verified tracked cache; all changed C/C++ inputs have unambiguously newer timestamps", changes


def prepare_object_directories(project_path, work, stage):
    require(stage in ("core", "renderer", "ui", "driver"), "Unknown private compilation stage")
    work = work.resolve(strict=True)
    for element in build.ET.parse(project_path).getroot().iter(build.tag("ObjectFileName")):
        text = (element.text or "").strip()
        require(text.endswith(("/", "\\")), "Expected directory-valued ObjectFileName metadata")
        # Existing project items can retain $(IntDir) metadata before the
        # generated absolute override. Expand only our explicit command-line
        # IntDir for directory creation; never rewrite the compiled XML.
        macro = "$(IntDir)"
        if text[:len(macro)].lower() == macro.lower():
            text = str(work / "int" / stage) + os.sep + text[len(macro):]
        require("$(" not in text and "%(" not in text and "@(" not in text,
                "Unsupported MSBuild expression in object directory")
        directory = Path(text.replace("\\", os.sep))
        require(directory.is_absolute(), "Object directory must be absolute or begin with $(IntDir)")
        directory = directory.resolve()
        require(work in directory.parents, "Generated object directory escapes private workspace")
        directory.mkdir(parents=True, exist_ok=True)


def execute(args, source, cache, output):
    before = build.source_manifest(source, source)
    dependencies = build.dependency_manifest(source)
    stages = [("core", build.make_library_project(source, source, output, True, False), "libopenrct2"),
              ("renderer", build.make_library_project(source, source, output, True, False, True), "libopenrct2renderer"),
              ("ui", build.make_library_project(source, source, output, True, True), "libopenrct2ui-parity"),
              ("driver", build.make_driver_project(source, source, output, True), "ui-parity")]
    for _, path, _ in stages:
        project = build.ET.parse(path).getroot()
        for item in project.findall("./" + build.tag("ItemGroup") + "/" + build.tag("ClCompile")):
            option = item.find(build.tag("MultiProcessorCompilation"))
            if option is None:
                option = build.child(item, "MultiProcessorCompilation")
            option.text = "false"
        build.write_xml(path, project)
    msbuild = build.find_msbuild(args.msbuild)
    common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
              "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:Breakpad=false",
              "/p:PreferredToolArchitecture=x64", "/p:SolutionDir=" + str(source) + os.sep,
              "/p:EnableVulkan=true"]
    if args.toolset_version:
        common.append("/p:VCToolsVersion=" + args.toolset_version)
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
    probe_common = common + ["/p:OutDir=" + str(output / "bin") + os.sep]
    toolchain = build.compiler_identity(msbuild, stages[0][1], probe_common, env, output)
    scripts = {str(path): build.sha256(path) for path in (Path(__file__).resolve(), HELPER.resolve())}
    metadata = {path.name: path.read_text(encoding="utf-8").replace(str(output), "@BUILD-WORKSPACE@")
                for _, path, _ in stages}
    signature = digest_json({"sourceRoot": str(source), "toolchain": {key: toolchain[key] for key in ("properties", "sha256")},
                             "options": common, "environmentSha256": digest_json(env), "scripts": scripts,
                             "dependencySha256": dependencies, "generatedMetadata": metadata})
    state_path = cache / "state.json"
    state = json.loads(state_path.read_text(encoding="utf-8")) if state_path.exists() else None
    write_json(output / "cache-input-state.json", state)
    work, reason, source_changes = choose_generation(cache, state, signature, before, source, time.time_ns())
    incremental = work is not None
    previous_snapshot = state.get("snapshotDirectory") if state and state.get("usable") else None
    previous_receipt_hash = build.sha256(Path(previous_snapshot) / "receipt.json") if previous_snapshot else None
    if work is None:
        work = cache / ("generation-" + uuid.uuid4().hex)
        work.mkdir()
    # Mark the mutable generation unusable before touching any project/output.
    # An interrupted/failed attempt cannot seed another incremental build.
    write_json(state_path, {"usable": False, "privateWorkspace": str(work), "attemptSnapshotDirectory": str(output)})
    cached_stages = []
    for name, path, target in stages:
        destination = work / path.name
        text = metadata[path.name].replace("@BUILD-WORKSPACE@", str(work))
        if not destination.exists() or destination.read_text(encoding="utf-8") != text:
            destination.write_text(text, encoding="utf-8")
        prepare_object_directories(destination, work, name)
        cached_stages.append((name, destination, target))
    common += ["/p:OutDir=" + str(work / "bin") + os.sep]
    # Bind the compiler query to the actual private projects used for compilation.
    actual_toolchain = build.compiler_identity(msbuild, cached_stages[0][1], common, env, output)
    require(all(actual_toolchain[key] == toolchain[key] for key in ("properties", "sha256")),
            "Compiler identity changed while selecting the private workspace")
    toolchain = actual_toolchain
    project_before = {path.name: build.sha256(path) for _, path, _ in cached_stages}
    commands, stage_results = [], []
    result_code = 0
    with (output / "build.log").open("w", encoding="utf-8") as log:
        log.write("CACHE " + json.dumps({"incremental": incremental, "reason": reason, "workspace": str(work)}) + "\n")
        for name, project, target in cached_stages:
            command = [msbuild, str(project), *common, "/p:IntDir=" + str(work / "int" / name) + os.sep,
                       "/p:TargetName=" + target]
            commands.append(command)
            log.write("\nCOMMAND " + json.dumps(command) + "\n")
            log.flush()
            result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
            stage_results.append({"name": name, "exitCode": result.returncode})
            if result.returncode:
                result_code = result.returncode
                break
    after = build.source_manifest(source, source)
    dependency_after = build.dependency_manifest(source)
    project_after = {path.name: build.sha256(path) for _, path, _ in cached_stages}
    fixed = {**scripts, **toolchain["sha256"]}
    fixed_changes = [name for name, digest in fixed.items() if not Path(name).is_file() or build.sha256(Path(name)) != digest]
    copy_mismatches = []
    for _, project, _ in cached_stages:
        shutil.copyfile(project, output / project.name)
        if (build.sha256(output / project.name) != project_before[project.name]
                or build.sha256(project) != project_before[project.name]):
            copy_mismatches.append(project.name)
    # Copy bytes, never hardlink. Compiler/PDB/tlog writes remain private.
    private_bin_before = manifest(work / "bin") if (work / "bin").exists() else {}
    if (work / "bin").exists():
        shutil.copytree(work / "bin", output / "bin")
        if manifest(output / "bin") != private_bin_before or manifest(work / "bin") != private_bin_before:
            copy_mismatches.append("private-bin-snapshot")
    runtime_dlls = {}
    runtime_mismatches = []
    for path in sorted((source / "lib/x64/bin").glob("*.dll")):
        destination = output / "bin" / path.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        runtime_dlls[path.relative_to(source / "lib/x64").as_posix()] = build.sha256(destination)
        if runtime_dlls[path.relative_to(source / "lib/x64").as_posix()] != dependencies.get("bin/" + path.name):
            runtime_mismatches.append(path.name)
    expected_bin = dict(private_bin_before)
    expected_bin.update({name.removeprefix("bin/"): digest for name, digest in runtime_dlls.items()})
    if (output / "bin").exists() and manifest(output / "bin") != expected_bin:
        copy_mismatches.append("final-bin-runtime-overlay")
    artifacts = [output / "bin" / name for name in
                 ("libopenrct2.lib", "libopenrct2renderer.lib", "libopenrct2ui-parity.lib", "ui-parity.exe")]
    missing = [str(path) for path in artifacts if not path.is_file()]
    changes, dependency_changes = diff(before, after), diff(dependencies, dependency_after)
    project_changes = diff(project_before, project_after)
    passed = (result_code == 0 and len(stage_results) == len(stages) and not changes and not dependency_changes
              and not missing and not project_changes and not fixed_changes and not runtime_mismatches and not copy_mismatches)
    receipt = {"schema": 1, "status": "pass" if passed else "fail", "exitCode": result_code,
               "sourceRoot": str(source), "harnessRoot": str(source), "commands": commands, "stageResults": stage_results,
               "compilerConcurrency": "serial", "driverCompilerConcurrency": "serial",
               "libraryCompilationPolicy": "tracked-private-cache", "reuseReceiptUnchanged": True,
               "sourceSha256": before, "sourceChangesDuringBuild": changes,
               "dependencySha256": dependencies, "dependencyChangesDuringBuild": dependency_changes,
               "generatedInputChangesDuringBuild": project_changes, "fixedBuildInputChangesDuringBuild": fixed_changes,
               "runtimeDllCopyMismatch": runtime_mismatches,
               "snapshotCopyMismatch": copy_mismatches, "privateBinBeforeCopySha256": private_bin_before,
               "missingArtifacts": missing, "toolchain": toolchain, "runtimeDllSha256": runtime_dlls,
               "artifactSha256": {path.relative_to(output).as_posix(): build.sha256(path) for path in artifacts if path.is_file()},
               "generatedProjectSha256": {path.name: build.sha256(output / path.name) for _, path, _ in stages},
               "buildLogSha256": build.sha256(output / "build.log"),
               "fixedBuildInputSha256": fixed,
               "incrementalCache": {"version": 1, "immutableSnapshot": True, "snapshotDirectory": str(output),
                   "privateWorkspace": str(work), "incremental": incremental, "decision": reason,
                   "inputStateSha256": build.sha256(output / "cache-input-state.json"),
                   "previousSnapshot": previous_snapshot, "previousReceiptSha256": previous_receipt_hash,
                   "changedSourceInputs": source_changes, "signatureSha256": signature,
                   "timestampPolicy": "Hash-changed C/C++ input mtime must exceed every prior private file and its own prior mtime by >2s and not be future; otherwise empty generation"},
               "instrumentation": "Only HardwareDisplayDrawingEngine.cpp force-includes OraclePresentHook.h; wrapper forwards real SDL_RenderPresent once. Source files and mtimes unchanged."}
    next_state = None
    if passed:
        private_files = manifest(work)
        next_state = {"usable": True, "privateWorkspace": str(work), "snapshotDirectory": str(output),
                   "workspaceSha256": private_files,
                   "newestWorkspaceMtimeNs": max((work / name).stat().st_mtime_ns for name in private_files),
                   "inputMtimeNs": {key: source_path(source, key).stat().st_mtime_ns for key in before},
                   "sourceSha256": before, "signature": signature}
        write_json(output / "cache-output-state.json", next_state)
        receipt["incrementalCache"]["outputStateSha256"] = build.sha256(output / "cache-output-state.json")
    write_json(output / "receipt.json", receipt)
    if next_state is not None:
        next_state["publishedSha256"] = manifest(output)
        write_json(state_path, next_state)
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-name", required=True)
    parser.add_argument("--msbuild", type=Path)
    parser.add_argument("--toolset-version")
    args = parser.parse_args()
    require(re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", args.cache_name), "Cache name must be lowercase letters/digits/hyphens")
    source = Path(__file__).resolve().parents[2]
    private_root = source / "obj/vulkan-parity/incremental-ui-cache"
    cache = private_root / args.cache_name
    output = args.output.resolve()
    require(not output.exists() and source in output.parents and private_root not in output.parents
            and output != private_root and output not in private_root.parents,
            "Output must be a fresh workspace directory outside every private cache")
    require(cache.resolve() == cache and private_root.resolve() == private_root, "Cache root must not be redirected")
    root_marker = private_root / "private-cache-root.json"
    root_identity = {"schema": 1, "kind": "private-ui-cache-namespace-never-evidence", "workspace": str(source)}
    if private_root.exists():
        require(root_marker.is_file() and json.loads(root_marker.read_text(encoding="utf-8")) == root_identity,
                "Refusing to write into an unrecognized existing private-cache namespace")
    else:
        private_root.mkdir(parents=True)
        write_json(root_marker, root_identity)
    marker = cache / "private-cache.json"
    identity = {"schema": 1, "kind": "mutable-ui-build-cache-never-evidence", "workspace": str(source)}
    new_cache = not cache.exists()
    if new_cache:
        cache.mkdir()
    else:
        require(marker.is_file() and json.loads(marker.read_text(encoding="utf-8")) == identity,
                "Refusing to write a lock into an unrecognized existing cache directory")
    lock = cache / "exclusive.lock"
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write("pid=" + str(os.getpid()) + "\noutput=" + str(output) + "\n")
        if not new_cache:
            require(json.loads(marker.read_text(encoding="utf-8")) == identity, "Unrecognized private cache marker")
        else:
            require({path.name for path in cache.iterdir()} == {"exclusive.lock"}, "Refusing to adopt an existing directory as cache")
            write_json(marker, identity)
        output.mkdir(parents=True)
        try:
            passed = execute(args, source, cache, output)
        except BaseException:
            (output / "builder-error.log").write_text(traceback.format_exc(), encoding="utf-8")
            # Only this fresh, still-publishing snapshot may be marked failed.
            # Never modify an earlier qualified snapshot or reuse a partial cache.
            receipt_path = output / "receipt.json"
            failed = json.loads(receipt_path.read_text(encoding="utf-8")) if receipt_path.exists() else {"schema": 1}
            failed.update({"status": "fail", "exitCode": 1,
                           "error": "Incremental build did not complete qualification/publication; see builder-error.log"})
            write_json(receipt_path, failed)
            write_json(cache / "state.json", {"usable": False, "failedSnapshotDirectory": str(output)})
            raise
        print(json.dumps({"status": "pass" if passed else "fail", "receipt": str(output / "receipt.json")}))
        raise SystemExit(0 if passed else 1)
    finally:
        lock.unlink()


if __name__ == "__main__":
    main()
