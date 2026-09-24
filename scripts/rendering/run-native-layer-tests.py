"""Run focused native-layer regressions against receipt-qualified binaries, serially.

Preserves logs, XML, validation diagnostics and optional visual samples in a new
directory. This is a partial renderer check, not full-world pixel parity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FILTER = ':'.join((
    'VulkanWorldSurfaceLayerTest.*', 'VulkanWorldPathLayerTest.*',
    'VulkanWorldObjectLayerTest.*', 'VulkanWorldFilterQueuedTest.*', 'WorldPropCatalogTest.*', 'WorldTrackRulesTest.*',
    'WorldFlatRideRulesTest.*', 'WorldFlatRideCatalogTest.*', 'WorldFlatRideAnimationTest.*',
    'WorldEntranceRulesTest.*', 'WorldEntranceCatalogTest.*',
    'VulkanPipelineCacheTest.*', 'VulkanStartupTest.*', 'WorldSelectionTest.*',
    'SelectedVehicleSnapshotTest.*', 'SelectedVehiclePacketTest.*',
    'WorldPathPublicationTest.*', 'WorldObjectUsageTest.*', 'WorldPathRulesTest.*',
    'TerrainSurfaceRulesTest.FullMapShaderSteepCornersMatchAuthoritativeSlopeTable',
    'GpuFoundationTest.*WorldSurface*',
    'GpuFoundationTest.NativeTerrainReservesPainterDepthBetweenEarlierCommandsAndLaterUi',
    'VulkanRuntimeIntegrationTest.HiddenWindowExercisesBackendLifecycleAndIndexedReadback',
    'PublicationSnapshotParityTest.*', 'PublicationTaskTest.*', 'NativePeepPublicationTest.*',
    'TerrainPresentationBridgeTest.*', 'PlayTests.SchedulerWait*', 'PlayTests.MapAnimation*',
    'SpatialAudio.*', 'AudioChannel.*', 'AudioMixer.*',
))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True, help='Build receipt or its directory')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rct1-path', type=Path, required=True)
    parser.add_argument('--rct2-path', type=Path, required=True)
    parser.add_argument('--filter', default=DEFAULT_FILTER)
    parser.add_argument('--path-corpus', type=Path, help='External original-art corpus; select VulkanWorldPathArtTest.* explicitly')
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--quick-pipeline-compile', action='store_true', help='Diagnostic compiler A/B; never default production acceptance')
    parser.add_argument('--world-gpu-profile', action='store_true', help='Validate the optional GPU timestamp/bounds readback path')
    args = parser.parse_args()
    receipt_path = args.build.resolve(strict=True)
    if receipt_path.is_dir():
        receipt_path /= 'receipt.json'
    receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
    if receipt.get('status') != 'pass' or receipt.get('sourceChangesDuringBuild'):
        raise ValueError('Build is not qualified')
    inputs = {n: h for n, h in receipt['artifactSha256'].items()
              if n == 'bin/tests.exe' or n.endswith('.spv')}
    if 'bin/tests.exe' not in inputs or not any(n.endswith('.spv') for n in inputs):
        raise ValueError('Receipt lacks tests or shaders')
    if any(sha(ROOT / n) != h for n, h in inputs.items()):
        raise ValueError('Test binaries/shaders differ from the build receipt')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    corpus_hashes = {}
    if args.path_corpus:
        args.path_corpus = args.path_corpus.resolve(strict=True)
        corpus_hashes = {p.name: sha(p) for p in sorted(args.path_corpus.iterdir()) if p.is_file()}
        if 'manifest.json' not in corpus_hashes:
            raise ValueError('Path corpus has no manifest')
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('OPENRCT2_', 'VK_'))}
    env.update(OPENRCT2_REQUIRE_VULKAN_TESTS='1', OPENRCT2_TEST_USER_DATA_PATH=str(out / 'profile'),
               OPENRCT2_TEST_RCT2_PATH=str(args.rct2_path.resolve(strict=True)),
               OPENRCT2_TEST_RCT1_PATH=str(args.rct1_path.resolve(strict=True)),
               OPENRCT2_VULKAN_SHADER_DIRECTORY=str(ROOT / 'bin/data/shaders/vulkan'),
               VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation', VK_LAYER_SETTINGS_PATH=str(out),
               OPENRCT2_VULKAN_PARITY_ARTIFACTS=str(out / 'samples'))
    if args.path_corpus:
        env['OPENRCT2_PATH_ART_CORPUS'] = str(args.path_corpus.resolve(strict=True))
    if args.quick_pipeline_compile:
        env['OPENRCT2_VULKAN_QUICK_COMPILE'] = '1'
    if args.world_gpu_profile:
        env['OPENRCT2_VULKAN_PROFILE_WORLD'] = '1'
    (out / 'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\n', encoding='utf-8')
    command = [str(ROOT / 'bin/tests.exe'), '--gtest_filter=' + args.filter,
               '--gtest_output=xml:' + str(out / 'tests.xml')]
    failure = None
    code = None
    with (out / 'tests.log').open('w', encoding='utf-8') as log:
        try:
            code = subprocess.run(command, cwd=ROOT / 'bin', env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            failure = 'Test process exceeded timeout'
    tree = ET.parse(out / 'tests.xml') if (out / 'tests.xml').is_file() else None
    counts = tree.getroot().attrib if tree is not None else {}
    unchanged = all(sha(ROOT / n) == h for n, h in inputs.items())
    corpus_unchanged = not args.path_corpus or corpus_hashes == {
        p.name: sha(p) for p in sorted(args.path_corpus.iterdir()) if p.is_file()}
    diagnostics = [line for line in (out / 'tests.log').read_text(encoding='utf-8', errors='replace').splitlines()
                   if re.search(r'Validation Error|SYNC-HAZARD|VUID-', line)]
    passed = (code == 0 and unchanged and corpus_unchanged and int(counts.get('tests', 0)) > 0
              and counts.get('disabled') == '0' and counts.get('failures') == '0'
              and counts.get('errors', '0') == '0' and not diagnostics
              and tree is not None and not tree.findall('.//skipped'))
    summary = dict(status='pass' if passed else 'fail', validationDiagnostics=diagnostics,
                   quickPipelineCompile=args.quick_pipeline_compile,
                   worldGpuProfile=args.world_gpu_profile,
                   exitCode=code, failure=failure, counts=counts, buildReceiptSha256=sha(receipt_path),
                   runnerSha256=sha(Path(__file__)), command=command, artifactSha256=inputs,
                   corpusSha256=corpus_hashes, corpusUnchanged=corpus_unchanged,
                   artifactsUnchanged=unchanged, logSha256=sha(out / 'tests.log'),
                   scope='Focused native layer, snapshot and scheduler validation; no full-world parity claim')
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: summary[k] for k in ('status', 'exitCode', 'counts', 'artifactsUnchanged')}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
