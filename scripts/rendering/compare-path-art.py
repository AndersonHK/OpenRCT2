"""Save exact indexed differences and review sheets for the external path-art corpus."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def child(root, name):
    path = Path(name)
    if path.name != name or name in ('', '.', '..'):
        raise ValueError('Expected a direct child filename')
    return root / name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True, help='world-path-art output directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((args.corpus / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('status') != 'pass' or len(manifest['cases']) != 8:
        raise ValueError('Incomplete external reference corpus')
    if {(c['rotation'], c['zoom']) for c in manifest['cases']} != {(r, z) for r in range(4) for z in range(2)}:
        raise ValueError('Reference camera coverage differs')
    palette = child(args.corpus, manifest['palette']).read_bytes()
    if len(palette) != 1024:
        raise ValueError('Reference palette size differs')
    rgb = [value for i in range(256) for value in (palette[i * 4 + 2], palette[i * 4 + 1], palette[i * 4])]
    reports = []
    for case in manifest['cases']:
        name, width, height = case['name'], case['width'], case['height']
        expected_file = child(args.corpus, case['referenceIndexed'])
        actual_file = child(args.samples, name + '.indexed')
        expected, actual = expected_file.read_bytes(), actual_file.read_bytes()
        if len(expected) != width * height or len(actual) != width * height:
            raise ValueError('Image extent differs: ' + name)
        mask = bytes(255 if a != b else 0 for a, b in zip(expected, actual))
        difference = Image.frombytes('L', (width, height), mask)
        bounds = difference.getbbox()
        difference.save(child(args.output, name + '-diff.png'))
        # Expand both indexed captures through the same pinned palette. Palette/alpha
        # differences in the independent PNG encoders must not masquerade as geometry.
        reference = Image.frombytes('P', (width, height), expected)
        candidate = Image.frombytes('P', (width, height), actual)
        reference.putpalette(rgb)
        candidate.putpalette(rgb)
        sheet = Image.new('RGB', (width * 2, height))
        sheet.paste(reference.convert('RGB'), (0, 0))
        sheet.paste(candidate.convert('RGB'), (width, 0))
        sheet.save(child(args.output, name + '-reference-left.png'))
        reports.append(dict(name=name, differentIndexedPixels=mask.count(255), bounds=bounds,
                            referenceSha256=sha(expected_file), actualSha256=sha(actual_file)))
    summary = dict(schema=1, status='pass' if all(r['differentIndexedPixels'] == 0 for r in reports) else 'fail',
                   corpusManifestSha256=sha(args.corpus / 'manifest.json'), cases=reports,
                   displayPaletteSha256=sha(child(args.corpus, manifest['palette'])),
                   scope='Exact indexed comparison; sheets are reference left, candidate right; manual review separate')
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary))
    return 0 if summary['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
