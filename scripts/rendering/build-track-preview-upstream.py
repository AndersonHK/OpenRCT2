"""Build only the shared track-preview diagnostic against a pinned pristine upstream core.

Local inputs only. Never rebuilds the core, restores dependencies, runs the game,
or modifies upstream source. A fresh workspace output directory is required.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[2]
UPSTREAM_HELPER = Path(__file__).with_name("build-upstream-screenshot-oracle.py")
SPEC = importlib.util.spec_from_file_location("track_upstream_helpers", UPSTREAM_HELPER)
UPSTREAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPSTREAM)
BUILD = UPSTREAM.BUILD
REVISION = "b80a4a84e92be8e07904b38d1032d0bb88280bb4"
TOOLSET = "14.44.35207"
BUILDERS = [Path(__file__).resolve(), UPSTREAM_HELPER.resolve(), UPSTREAM.HELPER.resolve()]
BUILDER_HASHES = UPSTREAM.file_manifest(BUILDERS)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def verify_files(root, manifest):
    require(bool(manifest), "Required input manifest is empty")
    for name, digest in manifest.items():
        path = (root / name).resolve(strict=True)
        require(path.is_file() and BUILD.sha256(path) == digest, "Input hash mismatch: " + str(path))


def verify_upstream(receipt_path, expected_receipt_hash):
    require(BUILD.sha256(receipt_path) == expected_receipt_hash, "Upstream receipt changed")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    require(receipt.get("status") == "pass" and receipt.get("kind") == "upstream-screenshot-oracle-build"
            and receipt.get("referenceRevision") == REVISION, "Wrong or unsuccessful upstream build receipt")
    require(receipt.get("compilerConcurrency") == "serial" and receipt.get("toolsetVersion") == TOOLSET,
            "Upstream core has a different compiler policy")
    for key in ("modifiedOriginalSources", "builderChangesDuringBuild", "sourceChangesDuringBuild",
                "dependencyChangesDuringBuild", "fixedBuildInputChangesDuringBuild",
                "runtimeDllCopyMismatch", "missingArtifacts"):
        require(key in receipt and not receipt[key], "Upstream receipt reports missing/changed inputs: " + key)
    require(any(stage.get("name") == "core" and stage.get("exitCode") == 0
                for stage in receipt["stageResults"]), "Upstream core stage did not pass")
    base = receipt_path.parent
    source = Path(receipt["sourceRoot"]).resolve(strict=True)
    require(source == base / "source", "Unexpected upstream source location")
    require(BUILD.sha256(base / "build.log") == receipt["buildLogSha256"], "Upstream build log changed")
    archive = base / "upstream-source.zip"
    require(BUILD.sha256(archive) == receipt["sourceArchiveSha256"], "Upstream source archive changed")
    originals = receipt["originalSourceSha256"]
    archived = {}
    with zipfile.ZipFile(archive) as bundle:
        for item in bundle.infolist():
            if item.is_dir():
                continue
            require(item.filename not in archived, "Duplicate upstream source archive entry")
            archived[item.filename] = hashlib.sha256(bundle.read(item)).hexdigest()
    require(archived == originals and UPSTREAM.source_manifest(source) == originals,
            "Upstream source/archive no longer equals pristine originalSourceSha256")
    require(BUILD.dependency_manifest(source) == receipt["dependencySha256"], "Upstream local dependencies changed")
    verify_files(base, receipt["artifactSha256"])
    verify_files(base, receipt["generatedProjectSha256"])
    wrapper = Path(receipt["wrapper"]["path"])
    require(wrapper.resolve() == base / "UpstreamCli.cpp"
            and BUILD.sha256(wrapper) == receipt["wrapper"]["sha256"], "Original CLI wrapper changed")
    verify_files(Path(), receipt["toolchain"]["sha256"])
    return receipt, source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reuse-build", type=Path,
                        default=ROOT / "obj/vulkan-parity/upstream-screenshot-build-01/receipt.json")
    parser.add_argument("--msbuild", type=Path)
    args = parser.parse_args()
    require(os.name == "nt", "This builder requires Windows x64 MSBuild")
    output = args.output.resolve()
    require(ROOT in output.parents and not output.exists(), "Output must be a new directory inside this workspace")
    output.mkdir(parents=True)
    receipt = {"schema": 1, "kind": "upstream-track-preview-build", "status": "fail", "driver": "track-preview",
               "referenceRevision": REVISION, "compilerConcurrency": "serial", "toolsetVersion": TOOLSET,
               "commands": [], "stageResults": [], "scope": "Shared diagnostic linked against pristine upstream core; no capture or parity claim."}
    fixed = None
    base_path = None
    base_hash = None
    success = False
    try:
        base_path = args.reuse_build.resolve(strict=True)
        if base_path.is_dir():
            base_path /= "receipt.json"
        base_hash = BUILD.sha256(base_path)
        base, source = verify_upstream(base_path, base_hash)
        receipt.update({"sourceRoot": str(source), "reuseReceipt": {"path": str(base_path), "sha256": base_hash},
                        "originalSourceSha256": base["originalSourceSha256"],
                        "sourceArchive": {"path": str(base_path.parent / "upstream-source.zip"),
                                          "sha256": base["sourceArchiveSha256"]},
                        "dependencySha256": base["dependencySha256"],
                        "coreProvenance": {"libraryPath": str(base_path.parent / "bin/libopenrct2.lib"),
                                           "librarySha256": base["artifactSha256"]["bin/libopenrct2.lib"],
                                           "buildLogSha256": base["buildLogSha256"],
                                           "command": base["commands"][0]}})
        original_driver = ROOT / "test/track-preview-parity/TrackPreviewMain.cpp"
        driver = output / "TrackPreviewMain.cpp"
        shutil.copy2(original_driver, driver)
        require(BUILD.sha256(driver) == BUILD.sha256(original_driver), "Driver changed during copy")
        receipt["sourceSha256"] = {original_driver.relative_to(ROOT).as_posix(): BUILD.sha256(original_driver)}
        receipt["diagnosticDriver"] = {"path": str(original_driver), "sha256": BUILD.sha256(driver), "copy": str(driver)}
        binary = output / "bin"
        binary.mkdir()
        copied_core = binary / "libopenrct2.lib"
        shutil.copy2(base_path.parent / "bin/libopenrct2.lib", copied_core)
        require(BUILD.sha256(copied_core) == receipt["coreProvenance"]["librarySha256"], "Core changed during copy")
        runtime = {}
        for name, digest in base["runtimeDllSha256"].items():
            path = base_path.parent / name
            require(path.parent == base_path.parent / "bin" and path.suffix.lower() == ".dll", "Unexpected runtime DLL path")
            destination = binary / path.name
            shutil.copy2(path, destination)
            require(BUILD.sha256(destination) == digest, "Runtime DLL changed during copy")
            runtime[destination.relative_to(output).as_posix()] = digest
        receipt["runtimeDllSha256"] = runtime
        project = UPSTREAM.make_cli_project(source, output, driver)
        UPSTREAM.serialise_project(project)
        msbuild = BUILD.find_msbuild(args.msbuild)
        env = {key.upper(): value for key, value in os.environ.items()}
        for key in ("CL", "_CL_", "LINK", "_LINK_", "OPENRCT2_CL_ADDITIONALOPTIONS",
                    "CUSTOMBEFOREMICROSOFTCOMMONPROPS", "CUSTOMAFTERMICROSOFTCOMMONTARGETS"):
            env.pop(key, None)
        common = ["/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64",
                  "/p:BuildProjectReferences=false", "/p:IsSolutionBuild=true", "/p:Breakpad=false",
                  "/p:PreferredToolArchitecture=x64", "/p:VCToolsVersion=" + TOOLSET,
                  "/p:EnableVulkan=false", "/p:VcpkgEnabled=false", "/p:MultiProcessorCompilation=false",
                  "/p:SolutionDir=" + str(source) + os.sep, "/p:OutDir=" + str(binary) + os.sep]
        toolchain = BUILD.compiler_identity(msbuild, project, common, env, output)
        require(toolchain["properties"] == base["toolchain"]["properties"]
                and toolchain["sha256"] == base["toolchain"]["sha256"], "Driver toolchain differs from pinned core")
        receipt["toolchain"] = toolchain
        require(UPSTREAM.file_manifest(BUILDERS) == BUILDER_HASHES, "Builder helpers changed during preparation")
        fixed_paths = [*BUILDERS, original_driver, driver, project, copied_core,
                       *(output / name for name in runtime), *(Path(name) for name in toolchain["sha256"])]
        fixed = UPSTREAM.file_manifest(fixed_paths)
        receipt["fixedBuildInputSha256"] = fixed
        receipt["builderSha256"] = BUILDER_HASHES
        receipt["generatedProjectSha256"] = {project.name: BUILD.sha256(project)}
        command = [msbuild, str(project), *common, "/p:IntDir=" + str(output / "int/driver") + os.sep,
                   "/p:TargetName=track-preview-parity"]
        receipt["commands"] = [command]
        with (output / "build.log").open("w", encoding="utf-8") as log:
            log.write("REUSED " + json.dumps(receipt["coreProvenance"]) + "\nCOMMAND " + json.dumps(command) + "\n")
            log.flush()
            # The unchanged upstream resource file resolves its icon paths from this directory.
            result = subprocess.run(command, cwd=source / "resources", env=env, stdout=log, stderr=subprocess.STDOUT)
        receipt["stageResults"] = [{"name": "core", "exitCode": 0, "reused": True},
                                   {"name": "driver", "exitCode": result.returncode}]
        require(result.returncode == 0, "Track preview driver build failed; see build.log")
        require((binary / "track-preview-parity.exe").is_file(), "Track preview executable missing")
        success = True
    except Exception as error:
        receipt["error"] = str(error)
    finally:
        try:
            if base_path is not None and base_hash is not None:
                verify_upstream(base_path, base_hash)
                receipt["reuseReceiptUnchanged"] = True
            if fixed is not None:
                changes = UPSTREAM.changed(fixed, UPSTREAM.file_manifest(Path(name) for name in fixed))
                receipt["fixedBuildInputChangesDuringBuild"] = changes
                require(not changes, "Driver build inputs changed")
            receipt["artifactSha256"] = {path.relative_to(output).as_posix(): BUILD.sha256(path)
                                         for path in sorted((output / "bin").rglob("*")) if path.is_file()}
            if (output / "build.log").is_file():
                receipt["buildLogSha256"] = BUILD.sha256(output / "build.log")
        except Exception as error:
            success = False
            receipt["verificationError"] = str(error)
        receipt["status"] = "pass" if success else "fail"
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "receipt": str(output / "receipt.json")}))
    raise SystemExit(0 if success else 1)


if __name__ == "__main__":
    main()
