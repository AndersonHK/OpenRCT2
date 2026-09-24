"""Run the first mixed-scene GPU raster falsification with pinned original-art input."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET


def load_helper(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


builder = load_helper('mixed_emission_builder', 'build-mixed-emission-probe.py')
capture = load_helper('mixed_capture', 'capture-mixed-fixture.py')
sha = capture.sha256
TEST = 'ExperimentalVulkanMixedFixtureTest.FrozenOriginalArtAllRotationsAndCameraOnlyZeroUpload'


def mixed_inputs(root, summary_path, probe_path, build):
    """Reusable read-only qualification for ordinary run-parity.py as well."""
    capture.require(build is not None and builder.stable_receipt(build), 'Mixed raster needs a stable test build')
    pinned = {}
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        digest = sha(path)
        capture.require(expected is None or digest == expected, 'Mixed input changed: ' + str(path))
        pinned[path] = digest
        return digest
    summary_path = summary_path.resolve(strict=True)
    probe_path = probe_path.resolve(strict=True)
    summary_digest, probe_digest = pin(summary_path), pin(probe_path)
    corpus_receipt = json.loads(summary_path.read_text(encoding='utf-8'))
    probe = json.loads(probe_path.read_text(encoding='utf-8'))
    capture.require(corpus_receipt.get('status') == 'pass' and corpus_receipt.get('accepted') is True
                    and corpus_receipt.get('inputsUnchanged') is True and corpus_receipt.get('failures') == []
                    and corpus_receipt.get('repeatDifferences') == [], 'Qualified fresh-repeat mixed corpus required')
    pin(Path(capture.__file__), corpus_receipt['runtimeInputSha256'][str(Path(capture.__file__).resolve())])
    # Pin the shared ABI, but keep the frozen executable as historical evidence.
    abi = root / 'test/mixed-parity/MixedFixture.h'
    abi_hash = pin(abi, corpus_receipt['runtimeInputSha256'][str(abi.resolve())])
    capture.require(builder.receipt_input(build, 'test/mixed-parity/MixedFixture.h') == abi_hash,
                    'Frozen fixture ABI and GPU ABI differ')
    directory = Path(corpus_receipt['corpus']).resolve(strict=True)
    capture.require(directory.parent == summary_path.parent and directory.name == 'corpus', 'Corpus root escaped receipt')
    coverage = capture.validate_corpus(directory, corpus_receipt['buildReceiptSha256'])
    capture.require(coverage['artifactSha256'] == corpus_receipt['corpusArtifactSha256'], 'Corpus bytes changed since capture')
    for name, digest in coverage['artifactSha256'].items(): pin(capture.filename(directory, name), digest)
    capture.require(probe.get('status') == 'pass' and probe.get('inputsUnchanged') is True
                    and set(probe.get('inputSha256', {})) == set(builder.INPUTS), 'Qualified complete mixed emitter receipt required')
    pin(Path(builder.__file__), probe['builderSha256'])
    pin(Path(__file__))
    for name, digest in probe['inputSha256'].items():
        capture.require(builder.receipt_input(build, name) == digest, 'Mixed shader/test build mismatch: ' + name)
    for name, digest in probe['artifactSha256'].items(): pin(capture.contained(probe_path.parent, name), digest)
    shader = probe_path.parent / builder.SHADER
    capture.require(shader.resolve() in pinned, 'Emitter SPIR-V missing from receipt')
    proof = {'corpusSummary': str(summary_path), 'corpusSummarySha256': summary_digest,
             'probeReceipt': str(probe_path), 'probeReceiptSha256': probe_digest,
             'shaderSha256': pinned[shader.resolve()], 'corpusSha256': coverage['artifactSha256'],
             'oracleBuildReceiptSha256': corpus_receipt['buildReceiptSha256'],
             'scope': 'Finite mixed raster hypothesis only; no runtime admission or performance qualification.'}
    return directory, shader, pinned, proof


def install_inputs(output, directory, shader, proof, pinned):
    isolated = output / 'mixed-corpus'
    isolated.mkdir()
    for name, digest in proof['corpusSha256'].items():
        destination = isolated / name
        shutil.copyfile(directory / name, destination)
        capture.require(sha(destination) == digest, 'Corpus changed while making isolated copy')
        pinned[destination] = digest
    emitter = output / builder.SHADER
    shutil.copyfile(shader, emitter)
    capture.require(sha(emitter) == proof['shaderSha256'], 'Emitter changed while making isolated copy')
    pinned[emitter] = proof['shaderSha256']
    return {'OPENRCT2_MIXED_CORPUS': str(isolated), 'OPENRCT2_MIXED_EMISSION_SPV': str(emitter),
            'OPENRCT2_MIXED_ARTIFACTS': str(output / 'mixed-samples')}


def validate_reports(output, directory):
    manifest = json.loads((directory / 'corpus.json').read_text(encoding='utf-8'))
    palette = (directory / 'palette.bin').read_bytes()
    reports, failures = {}, []
    previous_raw = None
    first = True
    for case in manifest['cases']:
        expected = (directory / case['expected']).read_bytes()
        expected_rgba = b''.join(palette[index * 4:index * 4 + 4] for index in expected)
        raw = (directory / case['peeps']).read_bytes()
        for repeat in range(2):
            name = case['name'] + '-' + str(repeat)
            folder = output / 'mixed-samples' / name
            expected_upload = (sum((directory / file).stat().st_size for file in
                                   ('statics.bin', 'definitions.bin', 'descriptors.bin', 'facts.bin'))
                               + len(raw) + len(manifest['assets']) * 96) if first else (len(raw) if raw != previous_raw else 0)
            previous_raw, first = raw, False
            try:
                status = (folder / 'status.bin').read_bytes()
                commands = (folder / 'commands.bin').read_bytes()
                capture.require(len(status) == 4128 and len(commands) == 1024 * 60, 'Missing/truncated mixed GPU buffers')
                error, count = struct.unpack_from('<2I', status)
                indirect = struct.unpack_from('<4I', status, 4112)
                capture.require(error == 0 and 0 < count <= 1024 and indirect == (4, count, 0, 0), 'GPU did not publish a valid mixed draw')
                capture.require(commands[count * 60:] == bytes([0xa5]) * (len(commands) - count * 60), 'GPU wrote past compact component tail')
            except (OSError, RuntimeError, KeyError, ValueError, struct.error) as error:
                failures.append(name + ': ' + str(error))
                count = None
            for layer, channels in (('indexed', 1), ('rgba', 4)):
                try:
                    sample = folder / layer
                    report = json.loads((sample / 'report.json').read_text(encoding='utf-8'))
                    # Failed comparisons are evidence too. Keep both layers and
                    # their reported counts even when byte verification fails.
                    reports[name + '/' + layer] = report
                    reference = (sample / 'software.bin').read_bytes()
                    actual = (sample / 'vulkan.bin').read_bytes()
                    capture.require(len(actual) == len(expected) * channels and len(actual) == len(reference), 'Truncated raster evidence')
                    observed_diff = sum(actual[offset:offset + channels] != reference[offset:offset + channels]
                                        for offset in range(0, len(actual), channels))
                    capture.require(report['differingPixels'] == observed_diff, 'Report pixel count disagrees with raster bytes')
                    if observed_diff:
                        failures.append(name + '/' + layer + ': Frozen/GPU raster mismatch (' + str(observed_diff) + ' pixels)')
                    capture.require(reference == (expected if channels == 1 else expected_rgba),
                                    'Compared reference is not the frozen case raster/palette')
                    capture.require(report['acceptedExceptions'] == []
                                    and report['runtimeAdmission'] is False and report['gpuError'] == 0
                                    and report['gpuComponents'] == count and report['rotation'] == case['rotation']
                                    and report['zoom'] == 0 and report['uploadedWorldBytes'] == report['expectedUploadBytes'] == expected_upload
                                    and report['oracleSourceReceiptSha256'] == manifest['oracleSourceReceiptSha256']
                                    and report['fixture'] == name and report['layer'] == layer
                                    and report['width'] == case['target'][2] and report['height'] == case['target'][3],
                                    'Mixed report mismatch or unsupported exception')
                    if repeat == 1: capture.require(report['uploadedWorldBytes'] == 0, 'Unchanged draw uploaded source state')
                    for image in ('software.png', 'vulkan.png', 'diff.png'):
                        capture.require((sample / image).is_file(), 'Missing durable visual evidence')
                except (OSError, RuntimeError, KeyError, ValueError, struct.error) as error:
                    failures.append(name + '/' + layer + ': ' + str(error))
    return reports, failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-receipt', type=Path, required=True)
    parser.add_argument('--mixed-corpus-summary', type=Path, required=True)
    parser.add_argument('--mixed-emission-probe-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=180)
    args = parser.parse_args()
    capture.require(args.timeout_seconds > 0, 'Timeout must be positive')
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {'schema': 1, 'status': 'fail', 'exitCode': None, 'timedOut': False, 'failures': [],
               'validationActivated': False, 'validationMessages': [], 'reports': {}}
    pinned, runtime, dlls = {}, None, []
    try:
        build_path = args.build_receipt.resolve(strict=True)
        if build_path.is_dir(): build_path /= 'receipt.json'
        pinned[build_path] = sha(build_path)
        build = json.loads(build_path.read_text(encoding='utf-8'))
        # Ordinary build artifacts are workspace-relative; isolated no-window
        # artifacts are receipt-directory-relative. Both use the same input ABI.
        no_window = build.get('windowedParityQualified') is False
        artifact_root = build_path.parent if no_window else root
        for name, digest in build['artifactSha256'].items():
            path = capture.contained(artifact_root, name)
            capture.require(sha(path) == digest, 'Test build artifact changed: ' + name)
            pinned[path] = digest
        executable = artifact_root / ('bin/tests-no-window.exe' if no_window else 'bin/tests.exe')
        capture.require(executable.resolve() in pinned, 'Test executable is not receipted')
        runtime = executable.parent
        dlls = sorted(str(path.resolve()) for path in runtime.glob('*.dll'))
        for name in dlls: pinned[Path(name)] = sha(Path(name))
        directory, shader, extra, proof = mixed_inputs(root, args.mixed_corpus_summary, args.mixed_emission_probe_receipt, build)
        pinned.update(extra)
        diagnostic_env = install_inputs(output, directory, shader, proof, pinned)
        shader_directory = artifact_root / 'bin/data/shaders/vulkan'
        for name in ('indexed_rect.vert.spv', 'indexed_rect.frag.spv', 'indexed_sprite.vert.spv'):
            path = shader_directory / name
            capture.require(path.resolve() in pinned, 'Raster shader missing from selected build receipt: ' + name)
        settings = output / 'vk_layer_settings.txt'
        settings.write_text('khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
                            'khronos_validation.log_filename = stdout\nkhronos_validation.report_flags = error,warn\n'
                            'khronos_validation.enable_message_limit = false\n', encoding='utf-8')
        pinned[settings] = sha(settings)
        diagnostic_env.update({'OPENRCT2_REQUIRE_VULKAN_TESTS': '1', 'OPENRCT2_VULKAN_SHADER_DIRECTORY': str(shader_directory),
                               'OPENRCT2_TEST_USER_DATA_PATH': str(output / 'profile'),
                               'VK_INSTANCE_LAYERS': 'VK_LAYER_KHRONOS_validation', 'VK_LOADER_DEBUG': 'layer',
                               'VK_LAYER_SETTINGS_PATH': str(output)})
        env = {key.upper() if os.name == 'nt' else key: value for key, value in os.environ.items()
               if not key.upper().startswith(('VK_', 'OPENRCT2_'))}
        env.update(diagnostic_env)
        xml, log = output / 'tests.xml', output / 'tests.log'
        command = [str(executable), '--gtest_filter=' + TEST, '--gtest_output=xml:' + str(xml)]
        summary.update({'command': command, 'diagnosticEnvironment': diagnostic_env, 'proof': proof,
                        'buildReceiptSha256': pinned[build_path], 'timeoutSeconds': args.timeout_seconds})
        with log.open('wb') as stream:
            try:
                result = subprocess.run(command, cwd=runtime, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
                summary['exitCode'] = result.returncode
            except subprocess.TimeoutExpired: summary['timedOut'] = True
        lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
        summary['validationActivated'] = any('Insert instance layer' in line and 'VK_LAYER_KHRONOS_validation' in line for line in lines)
        summary['validationMessages'] = sorted({line for line in lines if any(marker in line for marker in
            ('VUID-', 'SYNC-HAZARD', 'Validation Error', 'Validation Warning'))})
        tests = list(ET.parse(xml).getroot().iter('testcase')) if xml.is_file() else []
        if not (len(tests) == 1 and tests[0].get('classname') + '.' + tests[0].get('name') == TEST
                and tests[0].get('status') == 'run' and not any(tests[0].find(t) is not None for t in ('failure', 'error', 'skipped'))):
            summary['failures'].append('Mixed device test failed, skipped or was not discovered')
        summary['reports'], failures = validate_reports(output, directory)
        summary['failures'].extend(failures)
    except Exception as error:
        summary['failures'].append(type(error).__name__ + ': ' + str(error))
    finally:
        changed = []
        for path, digest in pinned.items():
            try: actual = sha(path)
            except OSError: actual = None
            if actual != digest: changed.append(str(path))
        if runtime is not None and sorted(str(p.resolve()) for p in runtime.glob('*.dll')) != dlls: changed.append('runtime DLL set')
        summary.update({'runtimeInputSha256': {str(k): v for k, v in pinned.items()}, 'changedInputs': changed,
                        'inputsUnchanged': not changed,
                        'artifactSha256': {p.relative_to(output).as_posix(): sha(p) for p in sorted(output.rglob('*')) if p.is_file()}})
        summary['status'] = 'pass' if (summary['exitCode'] == 0 and not summary['timedOut'] and not summary['failures']
            and not changed and summary['validationActivated'] and not summary['validationMessages'] and len(summary['reports']) == 64) else 'fail'
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'status': summary['status'], 'failures': summary['failures'], 'reports': len(summary['reports'])}))
    raise SystemExit(0 if summary['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()
