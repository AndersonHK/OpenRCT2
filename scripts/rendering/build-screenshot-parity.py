"""Build only the diagnostic real-screenshot CLI driver against receipt-qualified libraries.

Requires an already successful current Vulkan UI build with byte-identical core
and renderer inputs. No network, source rebuild, UI library, or ordinary bin mutation.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

HELPER = Path(__file__).with_name('build-current-ui-parity.py')
spec = importlib.util.spec_from_file_location('ui_build', HELPER)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def inputs(source):
    result = build.source_manifest(source, source)
    result.update({'harness/' + path.relative_to(source).as_posix(): build.sha256(path)
                   for path in sorted((source / 'test/cli-parity').rglob('*')) if path.is_file()})
    return result


def changed(before, after):
    return sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))


def file_manifest(paths):
    return {str(path): build.sha256(path) for path in paths if path.is_file()}


def driver_project(source, output):
    child = build.child
    project = ET.Element(build.tag('Project'), {'ToolsVersion': 'Current'})
    group = child(project, 'ItemGroup', Label='ProjectConfigurations')
    config = child(group, 'ProjectConfiguration', Include='Release|x64')
    child(config, 'Configuration', 'Release')
    child(config, 'Platform', 'x64')
    group = child(project, 'PropertyGroup', Label='Globals')
    child(group, 'ProjectGuid', '{7A331975-D17B-4B03-9AE9-198227BA432B}')
    child(group, 'ProjectName', 'screenshot-parity')
    group = child(project, 'PropertyGroup', Label='Configuration')
    child(group, 'ConfigurationType', 'Application')
    child(project, 'Import', Project=str(source / 'openrct2.common.props'))
    definitions = child(project, 'ItemDefinitionGroup')
    compiler = child(definitions, 'ClCompile')
    child(compiler, 'PrecompiledHeader', 'NotUsing')
    child(compiler, 'PreprocessorDefinitions', 'ENABLE_VULKAN;%(PreprocessorDefinitions)')
    child(compiler, 'AdditionalIncludeDirectories', r'$(VULKAN_SDK)\Include;%(AdditionalIncludeDirectories)')
    linker = child(definitions, 'Link')
    child(linker, 'AdditionalDependencies', '$(VulkanAdditionalDependencies)$(OutDir)libopenrct2.lib;$(OutDir)libopenrct2renderer.lib;%(AdditionalDependencies)')
    child(linker, 'SubSystem', 'Console')
    child(linker, 'StackReserveSize', '8388608')
    group = child(project, 'ItemGroup')
    child(group, 'ClCompile', Include=str(source / 'test/cli-parity/ScreenshotMain.cpp'))
    child(project, 'Import', Project=r'$(VCTargetsPath)\Microsoft.Cpp.targets')
    path = output / 'screenshot-driver.vcxproj'
    build.write_xml(path, project)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse-build', type=Path, required=True)
    parser.add_argument('--msbuild', type=Path)
    parser.add_argument('--toolset-version')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists() or source not in output.parents:
        raise SystemExit('--output must be a new directory inside the workspace')
    output.mkdir(parents=True)
    core = build.make_library_project(source, source, output, True, ui=False)
    renderer = build.make_library_project(source, source, output, True, ui=False, renderer=True)
    driver = driver_project(source, output)
    reuse_receipt_path = args.reuse_build.resolve(strict=True)
    if reuse_receipt_path.is_dir():
        reuse_receipt_path /= 'receipt.json'
    reuse_receipt_before = build.sha256(reuse_receipt_path)
    reuse_receipt = json.loads(reuse_receipt_path.read_text(encoding='utf-8'))
    if reuse_receipt.get('status') != 'pass' or any(reuse_receipt.get(key) for key in (
            'sourceChangesDuringBuild', 'dependencyChangesDuringBuild', 'generatedInputChangesDuringBuild')):
        raise SystemExit('A successful, unchanged UI build receipt is required')
    reuse_log = reuse_receipt_path.parent / 'build.log'
    if build.sha256(reuse_log) != reuse_receipt['buildLogSha256']:
        raise SystemExit('Reused UI build log no longer matches its receipt')
    concurrency = reuse_receipt.get('compilerConcurrency', 'project-default')
    if concurrency not in ('serial', 'project-default'):
        raise SystemExit('Unknown prior compiler concurrency policy')
    if concurrency == 'serial':
        # Match the library metadata exactly; prepare_reuse still verifies every
        # generated field/hash. The single new driver translation unit is serial too.
        for project_path in (core, renderer, driver):
            project = ET.parse(project_path).getroot()
            for item in project.findall('./' + build.tag('ItemGroup') + '/' + build.tag('ClCompile')):
                option = item.find(build.tag('MultiProcessorCompilation'))
                if option is None:
                    option = build.child(item, 'MultiProcessorCompilation')
                option.text = 'false'
            build.write_xml(project_path, project)
    before = inputs(source)
    dependencies = build.dependency_manifest(source)
    msbuild = build.find_msbuild(args.msbuild)
    common = ['/m:1', '/nr:false', '/p:Configuration=Release', '/p:Platform=x64',
              '/p:BuildProjectReferences=false', '/p:IsSolutionBuild=true', '/p:Breakpad=false',
              '/p:PreferredToolArchitecture=x64', '/p:SolutionDir=' + str(source) + os.sep,
              '/p:OutDir=' + str(output / 'bin') + os.sep, '/p:EnableVulkan=true']
    if args.toolset_version:
        common.append('/p:VCToolsVersion=' + args.toolset_version)
    env = {key.upper() if os.name == 'nt' else key: value for key, value in os.environ.items()}
    toolchain = build.compiler_identity(msbuild, core, common, env, output)
    reuse, _ = build.prepare_reuse(args.reuse_build, before, dependencies, toolchain, common, True, output)
    if set(reuse) != {'core', 'renderer'}:
        raise SystemExit('Both core and renderer must match the successful receipt, toolchain and generated metadata exactly')
    # Freeze generated compiler metadata and actual copied link inputs before use.
    fixed_paths = [core, renderer, driver, Path(__file__), HELPER, reuse_log]
    fixed_paths.extend(Path(path) for path in toolchain['sha256'])
    if any(build.sha256(Path(path)) != digest for path, digest in toolchain['sha256'].items()):
        raise SystemExit('Compiler inputs changed after compiler identification')
    for name, library in (('core', 'libopenrct2.lib'), ('renderer', 'libopenrct2renderer.lib')):
        copied = output / 'bin' / library
        if build.sha256(copied) != reuse[name]['librarySha256']:
            raise SystemExit('Reused library changed during isolated copy: ' + library)
        fixed_paths.append(copied)
    fixed_before = file_manifest(fixed_paths)
    command = [msbuild, str(driver), *common, '/p:IntDir=' + str(output / 'int/driver') + os.sep,
               '/p:TargetName=screenshot-parity']
    with (output / 'build.log').open('w', encoding='utf-8') as log:
        log.write('REUSED ' + json.dumps(reuse) + '\nCOMMAND ' + json.dumps(command) + '\n')
        log.flush()
        result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
    runtime = {}
    runtime_copy_mismatches = []
    for path in sorted((source / 'lib/x64/bin').glob('*.dll')):
        destination = output / 'bin' / path.name
        shutil.copy2(path, destination)
        runtime[path.name] = build.sha256(destination)
        if runtime[path.name] != dependencies.get('bin/' + path.name):
            runtime_copy_mismatches.append(path.name)
    after = inputs(source)
    dependency_after = build.dependency_manifest(source)
    changes = changed(before, after)
    dependency_changes = changed(dependencies, dependency_after)
    fixed_changes = changed(fixed_before, file_manifest(fixed_paths))
    artifacts = [output / 'bin' / name for name in ('libopenrct2.lib', 'libopenrct2renderer.lib', 'screenshot-parity.exe')]
    missing = [str(path) for path in artifacts if not path.is_file()]
    reuse_receipt_unchanged = build.sha256(reuse_receipt_path) == reuse_receipt_before
    passed = (result.returncode == 0 and not changes and not dependency_changes and not missing
              and reuse_receipt_unchanged and not fixed_changes and not runtime_copy_mismatches)
    stages = [{'name': name, 'exitCode': 0, 'reused': reused} for name, reused in reuse.items()]
    stages.append({'name': 'driver', 'exitCode': result.returncode})
    receipt = {'schema': 1, 'status': 'pass' if passed else 'fail', 'exitCode': result.returncode,
               'sourceRoot': str(source), 'commands': [command], 'stageResults': stages,
               'compilerConcurrency': concurrency, 'reuseReceiptUnchanged': reuse_receipt_unchanged,
               'reuseReceipt': {'path': str(reuse_receipt_path), 'sha256': reuse_receipt_before},
               'fixedBuildInputSha256': fixed_before, 'fixedBuildInputChangesDuringBuild': fixed_changes,
               'sourceSha256': before, 'sourceChangesDuringBuild': changes, 'dependencySha256': dependencies,
               'dependencyChangesDuringBuild': dependency_changes, 'toolchain': toolchain,
               'artifactSha256': {path.relative_to(output).as_posix(): build.sha256(path) for path in artifacts if path.is_file()},
               'generatedProjectSha256': {path.name: build.sha256(path) for path in (core, renderer, driver)},
               'runtimeDllSha256': runtime, 'runtimeDllCopyMismatch': runtime_copy_mismatches,
               'buildLogSha256': build.sha256(output / 'build.log'),
               'builderSha256': {str(path): build.sha256(path) for path in (Path(__file__), HELPER)},
               'missingArtifacts': missing,
               'scope': 'Real screenshot dispatcher with diagnostic injected service; default factory-free branch unchanged. No main window, no full parity claim.'}
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': receipt['status'], 'receipt': str(output / 'receipt.json')}))
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
