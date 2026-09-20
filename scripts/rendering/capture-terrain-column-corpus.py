"""Capture frozen, fresh-repeat and current linked-core terrain order/pixels.

Preserves the accepted park. No GPU admission or Vulkan parity is claimed here.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

spec = importlib.util.spec_from_file_location('terrain_fixture', Path(__file__).with_name('prepare-native-terrain-fixture.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
sha = fixture.sha256
INPUT_MANIFEST_SHA = 'db5a14f2500b12314f3d883663b0b670693ead7df7ded37656feb3eeb058e163'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-manifest', type=Path, required=True)
    parser.add_argument('--frozen-build', type=Path, required=True)
    parser.add_argument('--current-build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=120)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if root not in output.parents or output.exists() or args.timeout_seconds <= 0:
        raise SystemExit('Output must be new and inside the workspace; timeout must be positive')
    output.mkdir(parents=True)
    summary = {'schema': 1, 'status': 'fail', 'failures': [], 'processes': [], 'cases': [],
               'scope': 'Frozen software order and indexed pixels, exact fresh repeat/current equivalence; no Vulkan proof'}
    pinned = {}
    roots = {}
    expected_assets = {}
    dll_sets = {}
    def pin(path, expected=None):
        path = path.resolve(strict=True)
        digest = sha(path)
        if expected is not None and digest != expected:
            raise RuntimeError('Pinned input differs: ' + str(path))
        pinned[str(path)] = digest
        return path
    try:
        pin(Path(__file__)); pin(Path(fixture.__file__))
        input_path = pin(args.input_manifest, INPUT_MANIFEST_SHA)
        accepted = json.loads(input_path.read_text(encoding='utf-8'))
        if accepted.get('accepted') is not True or accepted.get('fixture') != 'nonuniform-terrain-v1':
            raise RuntimeError('Accepted nonuniform input02 required')
        park = pin(Path(accepted['park']['path']), accepted['park']['sha256'])
        expected_census = json.loads(pin(input_path.parent / 'frozen-verify.canonical.json').read_text(encoding='utf-8'))
        roots = {name: Path(value).resolve(strict=True) for name, value in accepted['assetRoots'].items()}
        expected_assets = accepted['assetSha256']
        if {name: fixture.tree(path) for name, path in roots.items()} != expected_assets:
            raise RuntimeError('Accepted immutable asset trees changed')
        reference_path = pin(root / 'docs/vulkan-software-reference.json')
        reference = json.loads(reference_path.read_text(encoding='utf-8'))
        builds = {}
        for kind, requested in (('frozen', args.frozen_build), ('current', args.current_build)):
            path, receipt, executable = fixture.read_build(requested)
            pin(path)
            if any(receipt.get(key) for key in ('sourceChangesDuringBuild', 'dependencyChangesDuringBuild', 'testChangesDuringBuild', 'sdkChangesDuringBuild')):
                raise RuntimeError('Unstable trace build')
            proof = receipt.get('frozenReference')
            if kind == 'frozen':
                if not proof or proof['revision'] != reference['revision'] or proof['sourceArchiveSha256'] != reference['sourceArchive']['sha256']:
                    raise RuntimeError('Trace executable is not accepted frozen core')
                pin(root / reference['localReference'] / 'source.zip', proof['sourceArchiveSha256'])
            elif proof:
                raise RuntimeError('Current trace executable must use current core')
            for name, digest in receipt['artifactSha256'].items():
                artifact = (path.parent / name).resolve(strict=True)
                if path.parent not in artifact.parents:
                    raise RuntimeError('Trace artifact escaped receipt directory')
                pin(artifact, digest)
            actual_dlls = sorted(executable.parent.glob('*.dll'))
            dll_sets[str(executable.parent)] = [str(p.resolve()) for p in actual_dlls]
            for dll in actual_dlls:
                digest = receipt.get('runtimeDllSha256', {}).get(dll.name)
                if digest is None:
                    digest = receipt['artifactSha256'].get(dll.relative_to(path.parent).as_posix())
                if digest is None:
                    raise RuntimeError('Unreceipted trace runtime DLL')
                pin(dll, digest)
            builds[kind] = (receipt, pin(executable))
        for name in ('NativeTerrainFixtureMain.cpp', 'TerrainColumnTrace.h', 'NonuniformTerrainRecipe.h'):
            key = 'harness/test/terrain-parity/' + name
            digest = sha(pin(root / 'test/terrain-parity' / name))
            if any(builds[kind][0]['sourceSha256'].get(key) != digest for kind in builds):
                raise RuntimeError('Trace driver/header does not match both builds: ' + name)
        corpus = output / 'corpus'; corpus.mkdir()
        env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()
               if not k.upper().startswith(('VK_', 'OPENRCT2_'))}
        centres = ((0, 464), (-1024, -48), (0, -560), (1024, -48))
        for stable in range(2):
            for transparent in range(2):
                for zoom in range(2):
                    for rotation in range(4):
                        name = f'r{rotation}-z{zoom}-t{transparent}-s{stable}'
                        x = (centres[rotation][0] >> zoom) - 480 + 1
                        y = (centres[rotation][1] >> zoom) - 320 + 1
                        traces = []
                        for run in ('frozen', 'repeat', 'current'):
                            folder = output / (name + '-' + run); folder.mkdir()
                            profile = folder / 'profile'; profile.mkdir()
                            executable = builds['current' if run == 'current' else 'frozen'][1]
                            trace_path = folder / 'trace.json'; census_path = folder / 'census.json'
                            command = [str(executable), '--fixture', 'nonuniform-terrain-v1', '--mode', 'verify',
                                       '--park', str(park), '--profile', str(profile), '--census', str(census_path),
                                       '--trace', str(trace_path), '--trace-x', str(x), '--trace-y', str(y),
                                       '--trace-rotation', str(rotation), '--trace-zoom', str(zoom),
                                       '--trace-transparent', str(transparent), '--trace-stable', str(stable), '--trace-smoothing', '0']
                            for key, value in roots.items(): command.extend(['--' + key, str(value)])
                            process = {'case': name, 'run': run, 'command': command, 'exitCode': None}
                            summary['processes'].append(process)
                            with (folder / 'run.log').open('wb') as log:
                                result = subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                                                        timeout=args.timeout_seconds)
                            process['exitCode'] = result.returncode
                            if result.returncode != 0: raise RuntimeError('Trace process failed: ' + name + '-' + run)
                            report = json.loads(census_path.read_text(encoding='utf-8'))
                            fixture.validate_nonuniform_census(report['census'])
                            if report['census'] != expected_census: raise RuntimeError('Trace changed accepted fixture census')
                            trace = json.loads(trace_path.read_text(encoding='utf-8'))
                            if (trace['schema'] != 2 or trace['worldTarget'] != [x,y,960,640]
                                    or trace['rotation'] != rotation or trace['zoom'] != zoom
                                    or trace['transparent'] != bool(transparent) or trace['stableSort'] != bool(stable)
                                    or trace['landscapeSmoothing'] is not False or len(bytes.fromhex(trace['indexedHex'])) != 960*640
                                    or not any(c['parentsBeforeArrange'] for c in trace['columns'])):
                                raise RuntimeError('Trace camera, coverage or raster contract differs')
                            process['artifacts'] = {n: sha(folder / n) for n in ('run.log','trace.json','census.json')}
                            traces.append(trace)
                        if traces[0] != traces[1] or traces[0] != traces[2]:
                            raise RuntimeError('Frozen repeat/current order or indexed pixel divergence: ' + name)
                        filename = name + '.json'
                        (corpus / filename).write_bytes(fixture.canonical(traces[0]))
                        summary['cases'].append(filename)
                        print(json.dumps({'case': name, 'status': 'pass'}), flush=True)
        (corpus / 'corpus.json').write_text(json.dumps({'schema': 1, 'cases': summary['cases']}, indent=2) + '\n', encoding='utf-8')
        summary['corpusSha256'] = fixture.tree(corpus)
    except Exception as error:
        summary['failures'].append(type(error).__name__ + ': ' + str(error))
    finally:
        changes = []
        for path, digest in pinned.items():
            try: current = sha(Path(path))
            except OSError: current = None
            if current != digest: changes.append(path)
        try:
            if roots and {name: fixture.tree(path) for name, path in roots.items()} != expected_assets: changes.append('asset trees')
            for path, expected in dll_sets.items():
                if sorted(str(p.resolve()) for p in Path(path).glob('*.dll')) != expected: changes.append(path + '/DLL set')
        except OSError as error: changes.append(str(error))
        summary['runtimeInputSha256'] = pinned
        summary['changedInputs'] = changes
        summary['assetRoots'] = {name: str(path) for name, path in roots.items()}
        summary['assetSha256'] = expected_assets
        if not changes and not summary['failures'] and len(summary['cases']) == 32: summary['status'] = 'pass'
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if summary['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()
