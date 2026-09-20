#!/usr/bin/env python3
"""Verify frozen terrain rule fragments against the immutable Git reference.

Read-only; no build, renderer, device, screenshot or parity test is executed.
The checked-in fragment text preserves checkout CRLF; Git content is compared
after line-ending normalization only. Source hashes still pin the exact bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--candidate-root', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    candidate = (args.candidate_root or root).resolve()
    metadata = json.loads((candidate / 'test/terrain-parity/FrozenTerrainEdgeOracle.json').read_text(encoding='utf-8'))
    oracle = (candidate / 'test/terrain-parity/FrozenTerrainEdgeOracle.inc').read_bytes()
    if hashlib.sha256(oracle).hexdigest() != metadata['oracleArtifactSha256']:
        raise RuntimeError('Complete frozen oracle artifact changed, including wrappers/interception macros')
    commit = metadata['frozenCommit']
    sources = {}
    for check in metadata['sourceCommitChecks']:
        relative = 'src/openrct2/' + check['source'].split('/src/openrct2/', 1)[1]
        content = subprocess.run(['git', 'show', commit + ':' + relative], cwd=root,
                                 check=True, capture_output=True).stdout
        if hashlib.sha256(content).hexdigest() != check['frozenGitBlobSha256']:
            raise RuntimeError('Frozen Git source hash changed: ' + relative)
        sources[check['source']] = content.decode('utf-8').replace('\r\n', '\n')
    for fragment in metadata['fragments']:
        content = sources[fragment['source']]
        # Slice each complete serialization, not an already-extracted LF fragment.
        # A marker beginning with LF ("\n/**") can leave its preceding CR in
        # the pinned Windows fragment. Normalize-then-slice loses that byte.
        alternatives = []
        for serialized in (content, content.replace('\n', '\r\n')):
            start = serialized.index(fragment['start'])
            end = serialized.index(fragment['endBefore'], start)
            alternatives.append(serialized[start:end].encode('utf-8'))
        pinned = next((v for v in alternatives if hashlib.sha256(v).hexdigest() == fragment['fragmentSha256']), None)
        if pinned is None or pinned not in oracle:
            raise RuntimeError('Frozen fragment mismatch: ' + fragment['start'])
    print(json.dumps({'status': 'PASS', 'scope': 'frozen source provenance only',
                      'commit': commit, 'sourceCount': len(sources),
                      'fragmentCount': len(metadata['fragments']),
                      'oracleSha256': hashlib.sha256(oracle).hexdigest()}, indent=2))


if __name__ == '__main__':
    main()
