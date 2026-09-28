"""Run receipt-pinned terrain compute readback in a fresh no-window process."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET


spec = importlib.util.spec_from_file_location('terrain_probe_builder', Path(__file__).with_name('build-terrain-rule-probe.py'))
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
sha = builder.sha
CASES = {
    'ActualDeviceEmissionsMatchFrozenNeighboursAtEveryRotationAndSlope': 65536,
    'InvalidMissingMaximumAndDispatchBoundaryQueriesPreserveGuards': 33000,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-receipt', type=Path, required=True)
    parser.add_argument('--probe-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=180)
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        raise SystemExit('Timeout must be positive')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {'schema': 1, 'status': 'fail', 'exitCode': None, 'timedOut': False,
               'timeoutSeconds': args.timeout_seconds, 'failures': [], 'reports': {},
               'validationActivated': False, 'validationMessages': [],
               'scope': 'Actual no-window compute and mapped readback; edge emissions and slope/shape against frozen source, '
                        'material selectors against the existing public object API, and explicit input-validity/ABI checks. '
                        'Six private intermediate-plan fields are retained in raw output but are not an independent semantic contract. '
                        'No terrain pixels, cross-tile ordering, production admission or performance qualification.'}
    runtime_inputs = {}
    def pin(path, expected=None):
        path = path.resolve(strict=True)
        digest = sha(path)
        if expected is not None and digest != expected:
            raise RuntimeError('Pinned input changed: ' + str(path))
        runtime_inputs[str(path)] = digest
        return digest
    try:
        build_path = args.build_receipt.resolve(strict=True)
        build_digest = pin(build_path)
        build = json.loads(build_path.read_text(encoding='utf-8'))
        if not builder.stable_receipt(build) or build.get('windowedParityQualified') is not False:
            raise RuntimeError('A successful source-stable no-window build receipt is required')
        for name, digest in build['artifactSha256'].items():
            path = (build_path.parent / name).resolve(strict=True)
            if build_path.parent not in path.parents:
                raise RuntimeError('No-window artifact escaped its build directory: ' + name)
            pin(path, digest)
        probe_path = args.probe_receipt.resolve(strict=True)
        probe_digest = pin(probe_path)
        probe = json.loads(probe_path.read_text(encoding='utf-8'))
        pin(Path(builder.__file__), probe['builderSha256'])
        pin(Path(__file__))
        if probe.get('status') != 'pass' or probe.get('inputsUnchanged') is not True:
            raise RuntimeError('A successful immutable diagnostic probe receipt is required')
        if set(probe['inputSha256']) != set(builder.INPUTS):
            raise RuntimeError('Probe input set is incomplete or ambiguous')
        for name, digest in probe['inputSha256'].items():
            if builder.receipt_input(build, name) != digest:
                raise RuntimeError('No-window test and probe shader inputs disagree: ' + name)
        shader_name = 'TerrainSurfaceRulesProbe.comp.spv'
        for name, digest in probe['artifactSha256'].items():
            path = (probe_path.parent / name).resolve(strict=True)
            if probe_path.parent not in path.parents:
                raise RuntimeError('Probe artifact escaped its compile directory: ' + name)
            pin(path, digest)
        shader_hash = probe['artifactSha256'][shader_name]
        shader = output / shader_name
        shutil.copyfile(probe_path.parent / shader_name, shader)
        pin(shader, shader_hash)
        executable = build_path.parent / 'bin/tests-no-window.exe'
        executable_hash = pin(executable, build['artifactSha256']['bin/tests-no-window.exe'])
        # Every actual adjacent DLL must appear in the qualified build receipt;
        # a static build with no adjacent DLLs is valid. Audit the set again later.
        runtime_dlls = sorted(executable.parent.glob('*.dll'))
        for path in runtime_dlls:
            relative = path.relative_to(build_path.parent).as_posix()
            if relative not in build['artifactSha256']:
                raise RuntimeError('Unreceipted adjacent runtime DLL: ' + relative)
            pin(path, build['artifactSha256'][relative])
        summary['runtimeDllPaths'] = [str(path.resolve()) for path in runtime_dlls]
        summary['runtimeDirectory'] = str(executable.parent.resolve())
        xml = output / 'tests.xml'
        log = output / 'tests.log'
        command = [str(executable), '--gtest_filter=VulkanTerrainSurfaceRulesTest.*', '--gtest_output=xml:' + str(xml)]
        # An inherited layer-disable setting can leave the layer loaded while
        # silently disabling validation. Use only the explicit diagnostic policy.
        env = {key.upper() if os.name == 'nt' else key: value for key, value in os.environ.items()
               if not key.upper().startswith(('VK_', 'OPENRCT2_'))}
        settings = output / 'vk_layer_settings.txt'
        settings.write_text(
            'khronos_validation.validate_sync = true\n'
            'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
            'khronos_validation.log_filename = stdout\n'
            'khronos_validation.report_flags = error,warn\n'
            'khronos_validation.enable_message_limit = false\n', encoding='utf-8')
        pin(settings)
        diagnostic_env = {'OPENRCT2_REQUIRE_VULKAN_TESTS': '1', 'OPENRCT2_TERRAIN_RULE_PROBE_SPV': str(shader),
                          'OPENRCT2_TERRAIN_RULE_ARTIFACTS': str(output / 'samples'),
                          'OPENRCT2_TEST_USER_DATA_PATH': str(output / 'profile'),
                          'VK_INSTANCE_LAYERS': 'VK_LAYER_KHRONOS_validation', 'VK_LOADER_DEBUG': 'layer',
                          'VK_LAYER_SETTINGS_PATH': str(output)}
        env.update(diagnostic_env)
        summary.update({'command': command, 'diagnosticEnvironment': diagnostic_env,
                        'buildReceipt': str(build_path), 'buildReceiptSha256': build_digest,
                        'probeReceipt': str(probe_path), 'probeReceiptSha256': probe_digest,
                        'shaderSha256': shader_hash, 'executableSha256': executable_hash,
                        'runnerSha256': sha(Path(__file__)), 'helperSha256': sha(Path(builder.__file__))})
        with log.open('w', encoding='utf-8') as stream:
            try:
                result = subprocess.run(command, cwd=executable.parent, env=env, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
                summary['exitCode'] = result.returncode
            except subprocess.TimeoutExpired:
                summary['timedOut'] = True
                summary['failures'].append('Compute test process exceeded bounded timeout and was terminated')
        lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
        summary['validationMessages'] = sorted({line for line in lines if any(marker in line for marker in (
            'VUID-', 'SYNC-HAZARD', 'Validation Error', 'Validation Warning'))})
        summary['validationActivated'] = any('Insert instance layer' in line and 'VK_LAYER_KHRONOS_validation' in line for line in lines)
        cases = list(ET.parse(xml).getroot().iter('testcase')) if xml.is_file() else []
        names = [case.get('name') for case in cases]
        if len(cases) != 2 or set(names) != set(CASES):
            summary['failures'].append('Expected exactly both device tests')
        for case in cases:
            name = case.get('name')
            if case.get('classname') != 'VulkanTerrainSurfaceRulesTest' or case.get('status') != 'run' or any(
                    case.find(tag) is not None for tag in ('skipped', 'failure', 'error')):
                summary['failures'].append('Failed, skipped or unexpected test: ' + str(name))
            props = {entry.get('name'): entry.get('value') for entry in case.findall('./properties/property')}
            count = int(props.get('terrainQueryCount', '0'))
            dispatches = int(props.get('terrainDispatchCount', '0'))
            if name not in CASES or count < CASES.get(name, 0) or dispatches == 0:
                summary['failures'].append('Insufficient actual dispatch coverage: ' + str(name))
                continue
            if name.startswith('ActualDevice') and int(props.get('terrainNeighbourCases', '0')) != 65536:
                summary['failures'].append('Neighbor cases are incomplete')
            paths = {filename: output / 'samples' / name / filename for filename in (
                'queries.bin', 'gpu-results.bin', 'expected-contract.bin', 'actual-contract.bin')}
            sizes = {'queries.bin': 48, 'gpu-results.bin': 80, 'expected-contract.bin': 56, 'actual-contract.bin': 56}
            if any(not path.is_file() or path.stat().st_size != count * sizes[key] for key, path in paths.items()):
                summary['failures'].append('Missing/truncated device buffers: ' + name)
                continue
            hashes = {filename: sha(path) for filename, path in paths.items()}
            if hashes['expected-contract.bin'] != hashes['actual-contract.bin']:
                summary['failures'].append('Frozen trace and actual GPU output differ: ' + name)
            summary['reports'][name] = {'queries': count, 'dispatches': dispatches, 'artifactSha256': hashes}
    except (OSError, RuntimeError, ValueError, KeyError, ET.ParseError) as error:
        summary['failures'].append(type(error).__name__ + ': ' + str(error))
    finally:
        changes = []
        for name, digest in runtime_inputs.items():
            try:
                actual = sha(Path(name))
            except OSError:
                actual = None
            if actual != digest:
                changes.append(name)
        if 'runtimeDirectory' in summary:
            current_dlls = sorted(str(path.resolve()) for path in Path(summary['runtimeDirectory']).glob('*.dll'))
            if current_dlls != sorted(summary['runtimeDllPaths']):
                changes.append('adjacent runtime DLL set')
        summary['runtimeInputSha256'] = runtime_inputs
        summary['changedRuntimeInputs'] = changes
        summary['inputsUnchanged'] = not changes
        for filename in ('tests.log', 'tests.xml'):
            path = output / filename
            summary[filename + 'Sha256'] = sha(path) if path.is_file() else None
        passed = (summary['exitCode'] == 0 and not summary['timedOut'] and not summary['failures']
                  and summary['validationActivated'] and not summary['validationMessages'] and not changes
                  and set(summary['reports']) == set(CASES))
        summary['status'] = 'pass' if passed else 'fail'
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'status': summary['status'], 'tests': len(summary['reports']), 'failures': summary['failures']}))
    raise SystemExit(0 if summary['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()
