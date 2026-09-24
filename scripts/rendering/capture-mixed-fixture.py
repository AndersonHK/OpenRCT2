"""Capture the finite mixed original-painter corpus twice with pinned inputs.

Only exact fresh-process repeats publish accepted=true. Failure keeps logs,
partial corpus and receipts. This runner never builds or enables a renderer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def tree(root):
    root = Path(root).resolve(strict=True)
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise RuntimeError('Input tree contains a symbolic link: ' + str(path))
        if path.is_file():
            if root not in path.resolve(strict=True).parents:
                raise RuntimeError('Input file escaped its root: ' + str(path))
            result[path.relative_to(root).as_posix()] = sha256(path)
    return result


def contained(root, name):
    path = (root / name).resolve(strict=True)
    if root.resolve() not in path.parents:
        raise RuntimeError('Receipt artifact escaped its root: ' + str(name))
    return path


def filename(root, name):
    if not isinstance(name, str) or not name or name in ('.', '..') or Path(name).name != name or '/' in name or '\\' in name:
        raise RuntimeError('Corpus filename must be a contained leaf')
    return contained(root, name)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_corpus(root, build_digest):
    root = Path(root).resolve(strict=True)
    manifest = json.loads((root / 'corpus.json').read_text(encoding='utf-8'))
    require(manifest.get('schema') == 1 and manifest.get('oracle') == 'frozen-software-viewport'
            and manifest.get('recipe') == 'finite-mixed-v1' and manifest.get('zoom') == 0
            and manifest.get('clearIndex') == 0, 'Unsupported mixed corpus identity/policy')
    require(manifest.get('oracleSourceReceiptSha256') == build_digest, 'Corpus cites another frozen build')
    require(set(manifest.get('categories', [])) == {'terrain', 'path', 'tree', 'track', 'support', 'peep'},
            'Mixed corpus omits a required family')
    require(manifest.get('sortPolicies') == [False, True] and manifest.get('terrainWindow') == [9, 9, 22, 22]
            and manifest.get('entitySnapshot') == 'null-fresh-owner-state', 'Unexpected frozen fixture state policy')
    files = {'corpus.json', 'statics.bin', 'definitions.bin', 'descriptors.bin', 'facts.bin', 'remap.bin', 'palette.bin'}
    expected_sizes = {'statics.bin': 202 * 32, 'definitions.bin': 28 * 272, 'remap.bin': 65536, 'palette.bin': 1024}
    for name, size in expected_sizes.items():
        require((root / name).stat().st_size == size, 'Unexpected fixture ABI length: ' + name)
    descriptors = (root / 'descriptors.bin').read_bytes()
    facts = (root / 'facts.bin').read_bytes()
    require(len(descriptors) == 64 and 0 < len(facts) <= 65536 * 16 and len(facts) % 16 == 0,
            'Truncated or oversized peep catalog ABI')
    previous = -1
    end = 0
    for d in struct.iter_unpack('<8I', descriptors):
        obj, generation, groups, offset, base, count, r0, r1 = d
        require(obj > previous and generation == 1 and 0 < groups <= 256 and offset == end
                and base < 0xffffffff and 0 < count <= 0xffffffff - base and r0 == r1 == 0,
                'Invalid immutable peep descriptor')
        previous, end = obj, offset + groups * 37
        require(end * 16 <= len(facts), 'Peep fact span exceeds buffer')
        for image, valid, reserved0, reserved1 in struct.iter_unpack('<4I', facts[offset * 16:end * 16]):
            require(valid in (0, 1) and reserved0 == reserved1 == 0
                    and ((valid == 0 and image == 0) or (valid == 1 and base <= image < base + count)),
                    'Invalid immutable peep fact')
    require(end * 16 == len(facts), 'Unused trailing peep facts')
    assets = manifest.get('assets', [])
    require(0 < len(assets) <= 4096, 'Missing or oversized real asset table')
    images = set()
    for asset in assets:
        image, width, height = asset['image'], asset['width'], asset['height']
        require(isinstance(image, int) and 0 <= image < 0xffffffff and image not in images
                and 0 < width <= 2048 and 0 < height <= 2048 and asset['coveredZeroPixels'] == 0
                and asset['flags'] & ~55 == 0, 'Unsupported or duplicate original asset')
        pixels = bytes.fromhex(asset['decodedIndexedHex'])
        require(len(pixels) == width * height, 'Truncated original asset pixels')
        require(-32768 <= asset['xOffset'] <= 32767 and -32768 <= asset['yOffset'] <= 32767,
                'Invalid original G1 offsets')
        images.add(image)
    definitions = (root / 'definitions.bin').read_bytes()
    for offset in range(0, len(definitions), 272):
        key, generation, rotation, count = struct.unpack_from('<4I', definitions, offset)
        require(generation == 1 and rotation == (offset // 272) % 4 and 0 < count <= 4,
                'Invalid finite static recipe header')
        for part in range(count):
            require(struct.unpack_from('<I', definitions, offset + 16 + part * 64)[0] in images,
                    'Finite static recipe image missing from atlas')
    wanted = {f'phase{phase}-r{rotation}-{sort}' for phase in range(2) for rotation in range(4)
              for sort in ('legacy', 'stable')}
    cases = manifest.get('cases', [])
    require(len(cases) == 16 and {case['name'] for case in cases} == wanted, 'Incomplete or duplicate 16-case corpus')
    for case in cases:
        name = case['name']
        rotation = int(name.split('-')[1][1:])
        target = case['target']
        require(case['rotation'] == rotation and len(target) == 4 and target[2:] == [192, 128], 'Invalid case camera')
        require(case['peeps'] == name + '.peeps.bin' and case['expected'] == name + '.indexed.bin',
                'Unexpected case binary mapping')
        raw = filename(root, case['peeps']).read_bytes()
        expected = filename(root, case['expected']).read_bytes()
        require(len(raw) == 3 * 96 and len(expected) == 192 * 128 and any(expected), 'Missing case raw state/raster')
        records = list(struct.iter_unpack('<24I', raw))
        require(len({r[3] for r in records}) == 3 and sum(bool(r[23] & 2) for r in records) == 1
                and all(r[7] == r[9] == 1 and r[23] & 1 and r[23] & ~0xff03 == 0 for r in records),
                'Invalid finite fixture peep identities/flags')
        trace_name = name + '.paint.json'
        trace = json.loads(filename(root, trace_name).read_text(encoding='utf-8'))
        require(trace['oracleOnly'] is True and trace['gpuInput'] is False and trace['rotation'] == rotation
                and trace['stableSort'] == name.endswith('-stable') and trace['columns'], 'Missing independent painter trace')
        files.update((case['peeps'], case['expected'], trace_name))
    hashes = tree(root)
    require(set(hashes) == files, 'Unexpected or missing corpus files')
    return {'cases': len(cases), 'assets': len(assets), 'staticOwners': 202, 'rawPeeps': 3,
            'factRows': len(facts) // 16, 'artifactSha256': hashes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frozen-build', type=Path, required=True)
    parser.add_argument('--source-park', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--rct2', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=180)
    args = parser.parse_args()
    require(args.timeout_seconds > 0, 'Timeout must be positive')
    workspace = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    require(not output.exists() and workspace in output.parents, 'Output must be a fresh workspace directory')
    output.mkdir(parents=True)
    report = {'schema': 1, 'status': 'fail', 'accepted': False, 'processes': [], 'failures': [],
              'startedUnixTime': time.time(), 'timeoutSeconds': args.timeout_seconds,
              'scope': 'Pinned finite original-art input and independent software raster; no GPU parity claim.'}
    pinned, asset_roots, asset_before, runtime_directory, dll_names = {}, {}, {}, None, []
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        digest = sha256(path)
        require(expected is None or digest == expected, 'Pinned input changed: ' + str(path))
        pinned[str(path)] = digest
        return digest
    try:
        pin(Path(__file__))
        build_path = args.frozen_build.resolve(strict=True)
        if build_path.is_dir(): build_path /= 'receipt.json'
        build_hash = pin(build_path)
        build = json.loads(build_path.read_text(encoding='utf-8'))
        require(build.get('status') == 'pass' and build.get('enableVulkan') is False
                and not build.get('sourceChangesDuringBuild') and not build.get('dependencyChangesDuringBuild')
                and build.get('reuseReceiptUnchanged') is True, 'Frozen producer build is unqualified')
        for name, digest in build['artifactSha256'].items(): pin(contained(build_path.parent, name), digest)
        exe = build_path.parent / 'bin/mixed-fixture-preparer.exe'
        pin(exe, build['artifactSha256']['bin/mixed-fixture-preparer.exe'])
        runtime_directory = exe.parent
        dlls = build['runtimeDllSha256']
        dll_names = sorted(path.name for path in runtime_directory.glob('*.dll'))
        require(dll_names == sorted(dlls), 'Unreceipted or missing adjacent runtime DLL')
        for name, digest in dlls.items(): pin(filename(runtime_directory, name), digest)
        for name, digest in build['generatedProjectSha256'].items(): pin(filename(build_path.parent, name), digest)
        for name, digest in build['builderSha256'].items(): pin(Path(name), digest)
        for name, digest in build['sourceSha256'].items():
            if name.startswith('harness/test/mixed-parity/'):
                pin(workspace / name.removeprefix('harness/'), digest)
        required_sources = {'MixedFixtureMain.cpp', 'MixedFixtureWorld.h', 'MixedFixtureCapture.h', 'MixedFixtureRecipes.h', 'MixedFixture.h'}
        require(required_sources <= {Path(name).name for name in build['sourceSha256'] if name.startswith('harness/test/mixed-parity/')},
                'Build does not pin complete fixture source')
        reference_path = workspace / 'docs/vulkan-software-reference.json'
        pin(reference_path)
        reference = json.loads(reference_path.read_text(encoding='utf-8'))
        proof = build['frozenReference']
        require(proof['revision'] == reference['revision'] and proof['sourceArchiveSha256'] == reference['sourceArchive']['sha256'],
                'Build is not the accepted original painter')
        pin(workspace / reference['localReference'] / 'source.zip', reference['sourceArchive']['sha256'])
        accepted_path = workspace / reference['localReference'] / 'manifest.json'
        pin(accepted_path, reference['manifest']['sha256'])
        accepted = json.loads(accepted_path.read_text(encoding='utf-8'))
        park = args.source_park.resolve(strict=True)
        park_hash = pin(park)
        known = {v['sha256'] for k, v in accepted['files'].items() if Path(k).name.lower() == park.name.lower()}
        require(park.name.lower() == 'everythingpark.park' and park_hash in known, 'Donor park is not pinned EverythingPark.park')
        asset_roots = {'data': args.data.resolve(strict=True), 'rct2': args.rct2.resolve(strict=True)}
        for root in asset_roots.values():
            require(root.is_dir() and root != output and output not in root.parents and root not in output.parents,
                    'Asset tree overlaps output')
        asset_before = {key: tree(root) for key, root in asset_roots.items()}
        for name, expected in accepted['files'].items():
            if name.startswith('package/data/'):
                require(asset_before['data'].get(name.removeprefix('package/data/')) == expected['sha256'],
                        'Bundled data differs from frozen reference: ' + name)
        require(all(name in asset_before['data'] for name in ('g2.dat', 'fonts.dat'))
                and any(Path(name).name.lower() == 'g1.dat' for name in asset_before['rct2']), 'Missing original graphics assets')
        report.update({'buildReceipt': str(build_path), 'buildReceiptSha256': build_hash,
                       'sourcePark': str(park), 'sourceParkSha256': park_hash,
                       'assetRoots': {k: str(v) for k, v in asset_roots.items()}, 'assetSha256': asset_before,
                       'frozenRevision': proof['revision']})
        completed = []
        for ordinal in range(2):
            profile = output / f'profile-{ordinal}'
            profile.mkdir()
            corpus = output / ('corpus' if ordinal == 0 else 'repeat-corpus')
            log_path = output / f'capture-{ordinal}.log'
            command = [str(exe), '--park', str(park), '--data', str(asset_roots['data']), '--rct2', str(asset_roots['rct2']),
                       '--profile', str(profile), '--output', str(corpus), '--oracle-receipt-sha256', build_hash]
            process = {'command': command, 'startedUnixTime': time.time(), 'exitCode': None, 'timedOut': False}
            report['processes'].append(process)
            env = {key.upper() if os.name == 'nt' else key: value for key, value in os.environ.items()
                   if not key.upper().startswith(('VK_', 'OPENRCT2_'))}
            with log_path.open('wb') as log:
                try:
                    result = subprocess.run(command, cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT,
                                            timeout=args.timeout_seconds)
                    process['exitCode'] = result.returncode
                except subprocess.TimeoutExpired:
                    process['timedOut'] = True
            process.update({'endedUnixTime': time.time(), 'logSha256': sha256(log_path), 'profileSha256': tree(profile)})
            require(process['exitCode'] == 0 and not process['timedOut'], 'Frozen capture failed or timed out; see ' + log_path.name)
            coverage = validate_corpus(corpus, build_hash)
            process['coverage'] = coverage
            completed.append(coverage)
        require(completed[0] == completed[1], 'Fresh-process frozen corpus differs')
        report.update({'corpus': str(output / 'corpus'), 'corpusArtifactSha256': completed[0]['artifactSha256'],
                       'repeatDifferences': [], 'coverage': {k: v for k, v in completed[0].items() if k != 'artifactSha256'}})
    except Exception as error:
        report['failures'].append(type(error).__name__ + ': ' + str(error))
    finally:
        changes = []
        for name, digest in pinned.items():
            try: actual = sha256(Path(name))
            except OSError: actual = None
            if actual != digest: changes.append(name)
        if runtime_directory is not None and sorted(p.name for p in runtime_directory.glob('*.dll')) != dll_names:
            changes.append('adjacent DLL set')
        for key, before in asset_before.items():
            try:
                if tree(asset_roots[key]) != before: changes.append('asset tree: ' + key)
            except (OSError, RuntimeError): changes.append('asset tree: ' + key)
        report.update({'runtimeInputSha256': pinned, 'changedRuntimeInputs': changes, 'inputsUnchanged': not changes,
                       'endedUnixTime': time.time()})
        report['accepted'] = not report['failures'] and not changes and 'corpusArtifactSha256' in report
        report['status'] = 'pass' if report['accepted'] else 'fail'
        (output / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'status': report['status'], 'failures': report['failures'], 'changedInputs': changes,
                          'receipt': str(output / 'receipt.json')}))
    raise SystemExit(0 if report['accepted'] else 1)


if __name__ == '__main__':
    main()
