"""Build the retained terrain column drawing compute entry point for diagnostic qualification, with strict receipts.

This never changes the production shader list or runs a game/test executable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


INPUTS = (
    'data/shaders/vulkan/terrain_surface_rules.glsl',
    'src/openrct2-renderer/gpu/TerrainSurfaceRules.h',
    'src/openrct2-renderer/gpu/RetainedTerrain.h',
    'src/openrct2-renderer/vulkan/VulkanTerrainEmissionPipeline.h',
    'src/openrct2-renderer/vulkan/VulkanTerrainEmissionPipeline.cpp',
    'test/tests/VulkanRetainedTerrainEmissionTests.cpp',
    'test/terrain-parity/FrozenTerrainEdgeOracle.inc',
    'test/terrain-parity/FrozenTerrainEdgeOracle.json',
    'test/terrain-parity/NonuniformTerrainRecipe.h',
    'data/shaders/vulkan/terrain_retained_emit.comp',
    'data/shaders/vulkan/terrain_sprite_geometry.glsl',
    'data/shaders/vulkan/peep_rules.glsl',
    'src/openrct2-renderer/gpu/PeepRules.h',
    'src/openrct2-renderer/gpu/PeepAssetGeneration.h',
    'src/openrct2-renderer/vulkan/VulkanPeepFieldPipeline.h',
    'src/openrct2-renderer/vulkan/VulkanPeepFieldPipeline.cpp',
    'data/shaders/vulkan/peep_fields.comp',
    'src/openrct2-renderer/gpu/RetainedTerrainDrawing.h',
    'src/openrct2-renderer/vulkan/VulkanTerrainDrawPipeline.h',
    'src/openrct2-renderer/vulkan/VulkanTerrainDrawPipeline.cpp',
    'src/openrct2-renderer/vulkan/VulkanRectPipeline.h',
    'src/openrct2-renderer/vulkan/VulkanRectPipeline.cpp',
    'test/tests/VulkanRetainedTerrainDrawTests.cpp',
    'data/shaders/vulkan/terrain_columns.comp',
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def receipt_input(receipt, name):
    candidates = [values[key] for section in ('sourceSha256', 'testSourceSha256')
                  for values in (receipt.get(section, {}),) for key in (name, 'source/' + name) if key in values]
    if not candidates or len(set(candidates)) != 1:
        raise RuntimeError('Build does not unambiguously pin input: ' + name)
    return candidates[0]


def stable_receipt(receipt):
    return receipt.get('status') == 'pass' and not any(receipt.get(key) for key in (
        'sourceChangesDuringBuild', 'testChangesDuringBuild', 'dependencyChangesDuringBuild', 'sdkChangesDuringBuild'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-build-receipt', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--glslc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=120)
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        raise SystemExit('Timeout must be positive')
    root = args.source_root.resolve(strict=True)
    build_path = args.test_build_receipt.resolve(strict=True)
    build_digest = sha(build_path)
    build = json.loads(build_path.read_text(encoding='utf-8'))
    if not stable_receipt(build):
        raise SystemExit('A successful, source-stable test build receipt is required')
    before = {name: sha(root / name) for name in INPUTS}
    for name, digest in before.items():
        if receipt_input(build, name) != digest:
            raise SystemExit('Test binary and compute probe inputs disagree: ' + name)
    compiler = args.glslc.resolve(strict=True)
    compiler_inputs = [compiler] + sorted(compiler.parent.glob('*.dll'))
    compiler_before = {str(path): sha(path) for path in compiler_inputs}
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shader = output / 'terrain_columns.comp.spv'
    verifier = root / 'scripts/rendering/verify-terrain-rule-oracle.py'
    verifier_before = sha(verifier)
    commands = [[os.sys.executable, str(verifier), '--root', str(root)],
                [str(compiler), str(root / INPUTS[-1]), '-o', str(shader)]]
    codes = []
    timed_out = False
    with (output / 'build.log').open('w', encoding='utf-8') as log:
        for command in commands:
            try:
                result = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                codes.append(None)
                break
            codes.append(result.returncode)
            if result.returncode != 0:
                break
    after = {name: sha(root / name) for name in INPUTS}
    compiler_after = {str(path): sha(path) for path in [compiler] + sorted(compiler.parent.glob('*.dll'))}
    stable = (before == after and compiler_before == compiler_after and verifier_before == sha(verifier)
              and sha(build_path) == build_digest)
    valid_shader = shader.is_file() and shader.stat().st_size >= 20 and shader.stat().st_size % 4 == 0
    if valid_shader:
        valid_shader = shader.read_bytes()[:4] == b'\x03\x02\x23\x07'
    passed = codes == [0, 0] and stable and valid_shader
    receipt = {'schema': 1, 'status': 'pass' if passed else 'fail', 'commands': commands, 'exitCodes': codes,
               'timedOut': timed_out, 'timeoutSeconds': args.timeout_seconds,
               'sourceRoot': str(root), 'inputSha256': before, 'inputsUnchanged': stable,
               'compilerSha256': compiler_before, 'verifierSha256': verifier_before,
               'testBuildReceipt': str(build_path), 'testBuildReceiptSha256': build_digest,
               'builderSha256': sha(Path(__file__)),
               'artifactSha256': {path.name: sha(path) for path in (output / 'build.log', shader) if path.is_file()},
               'scope': 'Retained column drawing compute shader compile for explicit diagnostic use; no GPU execution or terrain admission.'}
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': receipt['status'], 'exitCodes': codes}))
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
