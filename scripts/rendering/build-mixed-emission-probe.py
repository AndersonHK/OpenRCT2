"""Compile the diagnostic mixed-scene emitter against a source-stable test receipt."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

INPUTS = (
    'test/mixed-parity/shaders/mixed_fixture_emit.comp',
    'data/shaders/vulkan/peep_rules.glsl',
    'data/shaders/vulkan/terrain_sprite_geometry.glsl',
    'src/openrct2/drawing/RetainedPeepState.h',
    'src/openrct2-renderer/gpu/PeepRules.h',
    'test/mixed-parity/MixedFixture.h',
    'src/openrct2-renderer/gpu/RetainedTerrainDrawing.h',
    'test/mixed-parity/VulkanMixedFixturePipeline.h',
    'test/mixed-parity/VulkanMixedFixturePipeline.cpp',
    'src/openrct2-renderer/vulkan/VulkanRectPipeline.cpp',
    'test/tests/VulkanMixedFixtureTests.cpp',
    'test/tests/VulkanParityTestSupport.h',
)
SHADER = 'mixed_fixture_emit.comp.spv'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def receipt_input(receipt, name):
    candidates = [values[key] for section in ('sourceSha256', 'testSourceSha256')
                  for values in (receipt.get(section, {}),) for key in (name, 'source/' + name) if key in values]
    if not candidates or len(set(candidates)) != 1:
        raise RuntimeError('Build does not unambiguously pin input: ' + name)
    return candidates[0]


def stable_receipt(receipt):
    return receipt.get('status') == 'pass' and not any(receipt.get(key) for key in (
        'sourceChangesDuringBuild', 'testChangesDuringBuild', 'dependencyChangesDuringBuild', 'sdkChangesDuringBuild',
        'generatedInputChangesDuringBuild'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-build-receipt', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--glslc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=120)
    args = parser.parse_args()
    if args.timeout_seconds <= 0: raise SystemExit('Timeout must be positive')
    root = args.source_root.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'schema': 1, 'status': 'fail', 'exitCode': None, 'timedOut': False, 'failures': [],
              'scope': 'Diagnostic mixed compute compile only; no GPU execution or live admission.'}
    pins = {}
    compiler = None
    compiler_dlls = []
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        digest = sha(path)
        if expected is not None and digest != expected: raise RuntimeError('Input differs: ' + str(path))
        pins[str(path)] = digest
        return digest
    try:
        pin(Path(__file__))
        receipt_path = args.test_build_receipt.resolve(strict=True)
        if receipt_path.is_dir(): receipt_path /= 'receipt.json'
        digest = pin(receipt_path)
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        if not stable_receipt(receipt): raise RuntimeError('Successful source-stable test build required')
        before = {name: pin(root / name, receipt_input(receipt, name)) for name in INPUTS}
        compiler = args.glslc.resolve(strict=True)
        pin(compiler)
        compiler_dlls = sorted(str(p.resolve()) for p in compiler.parent.glob('*.dll'))
        for path in compiler_dlls: pin(Path(path))
        shader = output / SHADER
        command = [str(compiler), '-I', str(root / 'data/shaders/vulkan'), str(root / INPUTS[0]), '-o', str(shader)]
        report.update({'command': command, 'inputSha256': before, 'testBuildReceipt': str(receipt_path),
                       'testBuildReceiptSha256': digest, 'builderSha256': sha(Path(__file__)),
                       'sourceRoot': str(root), 'timeoutSeconds': args.timeout_seconds})
        with (output / 'build.log').open('wb') as log:
            try:
                result = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
                report['exitCode'] = result.returncode
            except subprocess.TimeoutExpired:
                report['timedOut'] = True
        valid = shader.is_file() and shader.stat().st_size >= 20 and shader.stat().st_size % 4 == 0
        report['validSpirv'] = valid and shader.read_bytes()[:4] == b'\x03\x02\x23\x07'
    except Exception as error:
        report['failures'].append(type(error).__name__ + ': ' + str(error))
    finally:
        changed = []
        for name, digest in pins.items():
            try: after = sha(Path(name))
            except OSError: after = None
            if after != digest: changed.append(name)
        if compiler is not None and compiler_dlls != sorted(str(p.resolve()) for p in compiler.parent.glob('*.dll')):
            changed.append('compiler DLL set')
        report.update({'runtimeInputSha256': pins, 'changedInputs': changed, 'inputsUnchanged': not changed,
                       'artifactSha256': {name: sha(output / name) for name in ('build.log', SHADER) if (output / name).is_file()}})
        report['status'] = 'pass' if (report['exitCode'] == 0 and not report['timedOut'] and not changed
                                    and not report['failures'] and report.get('validSpirv')) else 'fail'
        (output / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'status': report['status'], 'failures': report['failures']}))
    raise SystemExit(0 if report['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()
