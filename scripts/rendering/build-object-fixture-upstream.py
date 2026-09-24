"""Build a object fixture/reference driver against the pinned pristine upstream core.

Only the diagnostic is compiled. No upstream source, library or existing evidence
is modified. The driver is external to production and executes only when requested
separately by the caller.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HELPER = Path(__file__).with_name('build-track-preview-upstream.py')
SPEC = importlib.util.spec_from_file_location('object_upstream_build', HELPER)
H = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(H)
B = H.BUILD
U = H.UPSTREAM


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse-build', type=Path,
                        default=ROOT / 'obj/vulkan-parity/upstream-screenshot-build-01/receipt.json')
    args = parser.parse_args()
    out = args.output.resolve()
    H.require(ROOT in out.parents and not out.exists(), 'Output must be new and inside the workspace')
    out.mkdir(parents=True)
    receipt = dict(schema=1, kind='upstream-object-fixture-build', status='fail',
                   referenceRevision=H.REVISION, compilerConcurrency='serial', toolsetVersion=H.TOOLSET)
    base_path = args.reuse_build.resolve(strict=True)
    if base_path.is_dir():
        base_path /= 'receipt.json'
    base_hash = B.sha256(base_path)
    fixed = None
    passed = False
    try:
        base, source = H.verify_upstream(base_path, base_hash)
        receipt.update(reuseReceipt={'path': str(base_path), 'sha256': base_hash}, sourceRoot=str(source),
                       coreSha256=base['artifactSha256']['bin/libopenrct2.lib'])
        original = ROOT / 'test/object-parity/ObjectFixtureMain.cpp'
        driver = out / original.name
        shutil.copy2(original, driver)
        binary = out / 'bin'
        binary.mkdir()
        core = binary / 'libopenrct2.lib'
        shutil.copy2(base_path.parent / 'bin/libopenrct2.lib', core)
        H.require(B.sha256(core) == receipt['coreSha256'], 'Core copy differs')
        runtime = {}
        for name, digest in base['runtimeDllSha256'].items():
            path = base_path.parent / name
            H.require(path.parent == base_path.parent / 'bin' and path.suffix.lower() == '.dll', 'Invalid DLL path')
            dest = binary / path.name
            shutil.copy2(path, dest)
            H.require(B.sha256(dest) == digest, 'Runtime copy differs')
            runtime[dest.relative_to(out).as_posix()] = digest
        receipt['runtimeDllSha256'] = runtime
        project = U.make_cli_project(source, out, driver)
        U.serialise_project(project)
        msbuild = B.find_msbuild(None)
        env = {k.upper(): v for k, v in os.environ.items()}
        for key in ('CL', '_CL_', 'LINK', '_LINK_', 'OPENRCT2_CL_ADDITIONALOPTIONS',
                    'CUSTOMBEFOREMICROSOFTCOMMONPROPS', 'CUSTOMAFTERMICROSOFTCOMMONTARGETS'):
            env.pop(key, None)
        common = ['/m:1', '/nr:false', '/p:Configuration=Release', '/p:Platform=x64',
                  '/p:BuildProjectReferences=false', '/p:IsSolutionBuild=true', '/p:Breakpad=false',
                  '/p:PreferredToolArchitecture=x64', '/p:VCToolsVersion=' + H.TOOLSET,
                  '/p:EnableVulkan=false', '/p:VcpkgEnabled=false', '/p:MultiProcessorCompilation=false',
                  '/p:SolutionDir=' + str(source) + os.sep, '/p:OutDir=' + str(binary) + os.sep]
        identity = B.compiler_identity(msbuild, project, common, env, out)
        H.require(identity['properties'] == base['toolchain']['properties']
                  and identity['sha256'] == base['toolchain']['sha256'], 'Driver toolchain differs from core')
        receipt['toolchain'] = identity
        builders = [Path(__file__).resolve(), HELPER.resolve(), *H.BUILDERS]
        fixed = U.file_manifest([*builders, original, driver, project, core,
                                 *(out / n for n in runtime), *(Path(n) for n in identity['sha256'])])
        receipt['fixedBuildInputSha256'] = fixed
        receipt['diagnosticDriver'] = {'path': str(original), 'sha256': B.sha256(original), 'copy': str(driver)}
        command = [msbuild, str(project), *common, '/p:IntDir=' + str(out / 'int/driver') + os.sep,
                   '/p:TargetName=object-fixture']
        receipt['command'] = command
        with (out / 'build.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(command, cwd=source / 'resources', env=env, stdout=log, stderr=subprocess.STDOUT)
        receipt['exitCode'] = result.returncode
        H.require(result.returncode == 0 and (binary / 'object-fixture.exe').is_file(), 'Driver build failed')
        passed = True
    except Exception as error:
        receipt['error'] = str(error)
    finally:
        try:
            H.verify_upstream(base_path, base_hash)
            receipt['reuseReceiptUnchanged'] = True
            if fixed is not None:
                changes = U.changed(fixed, U.file_manifest(Path(n) for n in fixed))
                receipt['fixedBuildInputChangesDuringBuild'] = changes
                H.require(not changes, 'Driver build inputs changed')
            receipt['artifactSha256'] = {p.relative_to(out).as_posix(): B.sha256(p)
                                         for p in sorted((out / 'bin').rglob('*')) if p.is_file()}
            if (out / 'build.log').exists():
                receipt['buildLogSha256'] = B.sha256(out / 'build.log')
        except Exception as error:
            passed = False
            receipt['verificationError'] = str(error)
        receipt['status'] = 'pass' if passed else 'fail'
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': receipt['status'], 'receipt': str(out / 'receipt.json')}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
