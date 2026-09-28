"""Create an isolated runner overlay; text generation only."""
from pathlib import Path
stage=Path(__file__).resolve().parent;root=next(p for p in Path(__file__).resolve().parents if (p / 'scripts/rendering/run-mixed-parity.py').is_file() and (p / 'src/openrct2').is_dir())
text=(root/'scripts/rendering/run-mixed-parity.py').read_text()
def change(old,new):
    global text
    if text.count(old)!=1:raise RuntimeError('Runner source drift: '+old[:80])
    text=text.replace(old,new)
change('Path(__file__).with_name(filename)',"(next(p for p in Path(__file__).resolve().parents if (p / 'scripts/rendering/run-mixed-parity.py').is_file() and (p / 'src/openrct2').is_dir())) / 'scripts/rendering' / filename")
change("sha = capture.sha256",'''sha = capture.sha256
plane_spec = importlib.util.spec_from_file_location('plane_builder', Path(__file__).with_name('build-plane-probe.py'))
plane_builder = importlib.util.module_from_spec(plane_spec)
plane_spec.loader.exec_module(plane_builder)''')
change("and set(probe.get('inputSha256', {})) == set(builder.INPUTS)","and probe.get('model') == 'plane-envelope-v1'\n                    and set(probe.get('inputSha256', {})) == set(plane_builder.INPUTS)")
change("pin(Path(builder.__file__), probe['builderSha256'])",'''pin(Path(plane_builder.__file__), probe['builderSha256'])
    for path, digest in probe['runtimeInputSha256'].items(): pin(Path(path), digest)''')
change("'scope': 'Finite mixed raster hypothesis only; no runtime admission or performance qualification.'",''' 'model': 'plane-envelope-v1',
             'rasterShaderSha256': {name: probe['artifactSha256'][name] for name in
                                  ('indexed_rect.vert.spv', 'indexed_rect.frag.spv', 'indexed_sprite.vert.spv')},
             'comparisonPolicy': {'legacy': 'Canonical original painter, zero tolerance',
                                  'stable': 'Negative control; not an alternative canonical target'},
             'baseDepthHypothesis': 'rotated-bounds-origin-x+y+z',
             'overlayDepthHypothesis': 'plane-envelope-v1 supersedes the base scalar shader with paired compiled shaders',
             'scope': 'Finite diagnostic plane envelope only; no runtime admission or performance qualification.' ''')
change("    return {'OPENRCT2_MIXED_CORPUS': str(isolated)",'''    raster = output / 'mixed-plane-shaders'
    raster.mkdir()
    for name, digest in proof['rasterShaderSha256'].items():
        destination = raster / name
        shutil.copyfile(shader.parent / name, destination)
        capture.require(sha(destination) == digest, 'Plane raster shader changed during isolated copy')
        pinned[destination] = digest
    return {'OPENRCT2_MIXED_DEPTH_MODEL': 'plane-envelope-v1', 'OPENRCT2_MIXED_CORPUS': str(isolated)''')
change("and report['runtimeAdmission'] is False and report['gpuError'] == 0", "and report['depthHypothesis'] == 'plane-envelope-v1'\n                                    and report['runtimeAdmission'] is False and report['gpuError'] == 0")
change("parser.add_argument('--mixed-emission-probe-receipt', type=Path, required=True)","parser.add_argument('--plane-probe-receipt', type=Path, required=True)")
change('root = Path(__file__).resolve().parents[2]',"root = next(p for p in Path(__file__).resolve().parents if (p / 'scripts/rendering/run-mixed-parity.py').is_file() and (p / 'src/openrct2').is_dir())")
change('args.mixed_emission_probe_receipt, build)', 'args.plane_probe_receipt, build)')
change("shader_directory = artifact_root / 'bin/data/shaders/vulkan'", "shader_directory = output / 'mixed-plane-shaders'")
change("        summary.update({'runtimeInputSha256':",'''        canonical = {name: value for name, value in summary['reports'].items() if '-legacy-' in name}
        controls = {name: value for name, value in summary['reports'].items() if '-stable-' in name}
        summary['canonicalLegacy'] = {'reports': len(canonical),
            'differingPixels': {name: value.get('differingPixels') for name, value in canonical.items()}}
        summary['stableNegativeControls'] = {'reports': len(controls),
            'differingPixels': {name: value.get('differingPixels') for name, value in controls.items()},
            'policy': 'Preserved negative-control outputs; never approve a pixel exception to merge policies.'}
        summary.update({'runtimeInputSha256':''')
(stage/'run-plane-parity.py').write_text(text)
print('Staged isolated runner; no game/device/test execution.')
