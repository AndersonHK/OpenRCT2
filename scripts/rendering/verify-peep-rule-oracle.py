"""Read-only complete-artifact and frozen-source provenance check; no renderer execution."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


# The frozen painter only reads these colours. Accept exactly the reviewed ownership
# migration, then compare the ENTIRE normalized header to its original pinned hash.
# This is not a current-source hash allowance or a mask of arbitrary runtime changes:
# enum values, inherited fields, getter types/bodies and every other byte remain guarded.
OWNERSHIP_NORMALIZATION = {
    'src/openrct2/entity/Peep.h': (
        '''    private:
        Drawing::Colour _tShirtColour{};
        Drawing::Colour _trousersColour{};

    protected:
        void notifyAppearanceChanged();
        void notifyAnimationChanged();

    public:
        const Drawing::Colour& getTShirtColour() const noexcept { return _tShirtColour; }
        const Drawing::Colour& getTrousersColour() const noexcept { return _trousersColour; }
        void setClothingColours(Drawing::Colour shirt, Drawing::Colour trousers);
        void setTShirtColour(Drawing::Colour colour) { setClothingColours(colour, _trousersColour); }
        void setTrousersColour(Drawing::Colour colour) { setClothingColours(_tShirtColour, colour); }
''',
        '''        Drawing::Colour tShirtColour;
        Drawing::Colour trousersColour;
'''),
    'src/openrct2/entity/Guest.h': (
        '''    private:
        Drawing::Colour _balloonColour{};
        Drawing::Colour _umbrellaColour{};
        Drawing::Colour _hatColour{};

    public:
        const Drawing::Colour& getBalloonColour() const noexcept { return _balloonColour; }
        const Drawing::Colour& getUmbrellaColour() const noexcept { return _umbrellaColour; }
        const Drawing::Colour& getHatColour() const noexcept { return _hatColour; }
        void setAccessoryColours(Drawing::Colour balloon, Drawing::Colour umbrella, Drawing::Colour hat);
        void setBalloonColour(Drawing::Colour colour) { setAccessoryColours(colour, _umbrellaColour, _hatColour); }
        void setUmbrellaColour(Drawing::Colour colour) { setAccessoryColours(_balloonColour, colour, _hatColour); }
        void setHatColour(Drawing::Colour colour) { setAccessoryColours(_balloonColour, _umbrellaColour, colour); }
''',
        '''        Drawing::Colour balloonColour;
        Drawing::Colour umbrellaColour;
        Drawing::Colour hatColour;
'''),
}
# Guest was not in the original support manifest. Pin its original full header too,
# because its three accessor bodies now participate in the frozen oracle adapter.
ADDITIONAL_SUPPORT = {
    'src/openrct2/entity/Guest.h': '089de31fe441056bd1bc44be389db1c62b45b432478a29e1ee528ca9d926b509',
}
ACCESSOR_ADAPTER = '''#define tShirtColour getTShirtColour()
#define trousersColour getTrousersColour()
#define balloonColour getBalloonColour()
#define umbrellaColour getUmbrellaColour()
#define hatColour getHatColour()
#include "../peep-parity/FrozenPeepRules.inc"
#undef hatColour
#undef umbrellaColour
#undef balloonColour
#undef trousersColour
#undef tShirtColour'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_support(path, current, pinned):
    if digest(current) == pinned:
        return False
    replacement = OWNERSHIP_NORMALIZATION.get(path)
    if replacement is None:
        raise RuntimeError('Oracle support types/constants changed: ' + path)
    owned, original = (part.encode('utf-8') for part in replacement)
    if current.count(owned) != 1 or digest(current.replace(owned, original)) != pinned:
        raise RuntimeError('Oracle support differs beyond exact colour ownership migration: ' + path)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--candidate-root', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    candidate = (args.candidate_root or root).resolve()
    meta = json.loads((candidate / 'test/peep-parity/FrozenPeepRules.json').read_text(encoding='utf-8-sig'))
    oracle = (candidate / 'test/peep-parity/FrozenPeepRules.inc').read_bytes()
    if digest(oracle) != meta['oracleArtifactSha256']:
        raise RuntimeError('Complete frozen oracle artifact changed')
    sources = {}
    support = meta['supportSources'] | ADDITIONAL_SUPPORT
    normalized = []
    support_proof = {}
    for path, pinned in (meta['sources'] | support).items():
        raw = subprocess.run(['git', 'show', meta['frozenCommit'] + ':' + path], cwd=root,
                             check=True, capture_output=True).stdout.replace(b'\r\n', b'\n')
        if digest(raw) != pinned:
            raise RuntimeError('Frozen source mismatch: ' + path)
        sources[path] = raw.decode('utf-8')
        if path in support:
            current = (candidate / path).read_bytes().replace(b'\r\n', b'\n')
            migrated = check_support(path, current, pinned)
            if migrated:
                normalized.append(path)
            support_proof[path] = {'frozenSha256': pinned, 'currentSha256': digest(current),
                                   'exactOwnershipNormalization': migrated}
    if normalized:
        if set(normalized) != set(OWNERSHIP_NORMALIZATION):
            raise RuntimeError('Incomplete peep/guest colour ownership migration')
        test = (candidate / 'test/tests/PeepRulesTests.cpp').read_text(encoding='utf-8')
        if test.count(ACCESSOR_ADAPTER) != 1:
            raise RuntimeError('Frozen painter colour adapter is not the exact read-only getter mapping')
        # No other mapping/redefinition/include of these frozen field tokens may hide
        # behind the approved adapter block. Unrelated test code is build-receipt pinned.
        rest = test.replace(ACCESSOR_ADAPTER, '')
        colour_tokens = ('tShirtColour', 'trousersColour', 'balloonColour', 'umbrellaColour', 'hatColour')
        for line in rest.splitlines():
            directive = line.strip().split()
            if (len(directive) >= 2 and directive[0] in ('#define', '#undef')
                    and directive[1].split('(')[0] in colour_tokens):
                raise RuntimeError('Additional frozen colour preprocessor adapter')
        if 'FrozenPeepRules.inc' in rest:
            raise RuntimeError('Additional frozen painter include')
    for fragment in meta['fragments']:
        found = False
        for source in (sources[fragment['source']], sources[fragment['source']].replace('\n', '\r\n')):
            start = source.index(fragment['start'])
            end = source.index('{', start) + 1
            depth = 1
            while depth:
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            raw = source[start:end].encode('utf-8')
            found |= digest(raw) == fragment['sha256'] and raw in oracle
        if not found:
            raise RuntimeError('Frozen body mismatch: ' + fragment['start'])
    print(json.dumps({'status':'PASS','scope':'frozen provenance only','functions':len(meta['fragments']),
                      'oracleSha256':digest(oracle), 'supportQualification': support_proof,
                      'accessorAdapter': 'exact read-only getters' if normalized else 'original public fields'}, indent=2))


if __name__ == '__main__':
    main()
