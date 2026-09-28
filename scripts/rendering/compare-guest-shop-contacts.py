"""Inspect the upstream --guest-shops corpus in paired batch screenshot captures.

This checks pixels produced by the actual renderers, not a reimplementation of
their ordering formula. An optional baseline distinguishes old divergences from
newly incorrect pixels. Screenshots are never masked or altered before counting.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def project(x, y, z, rotation):
    x, y = ((x, y), (y, -x), (-x, -y), (-y, x))[rotation]
    return y - x, (x + y) // 2 - z


def compare(reference, candidate, baseline=None):
    errors = sum(p != (0, 0, 0) for p in ImageChops.difference(reference, candidate).getdata())
    result = {'differentPixels': errors}
    if baseline is not None:
        triples = zip(reference.getdata(), candidate.getdata(), baseline.getdata())
        changed = improved = regressed = 0
        for ref, new, old in triples:
            changed += new != old
            improved += new == ref and old != ref
            regressed += new != ref and old == ref
        result.update(changedPixels=changed, correctedPixels=improved, newlyWrongPixels=regressed)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True, help='Original fixture manifest.json')
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    fixture = json.loads(args.fixture.read_text())
    assert fixture['fixture'] == 'original-guest-shop-contacts-v1' and fixture['status'] == 'pass'
    summary_path = args.capture / 'summary.json'
    summary = json.loads(summary_path.read_text())
    assert summary['status'] == 'capture-complete' and summary['inputsUnchanged']
    report = {'fixture': str(args.fixture.resolve()), 'fixtureSha256': sha(args.fixture),
              'captureSummarySha256': sha(summary_path), 'frames': [], 'contacts': []}
    examples = []
    for case in summary['cases']:
        camera = case['camera']
        name, rotation, zoom = camera['name'], camera['rotation'], camera['zoom']
        assert zoom == 0, 'Contact inspection requires unscaled source pixels'
        folder = args.capture / name
        paths = [folder / 'upstream.png', folder / 'native.png']
        if args.baseline:
            # Different original poses/assets would invalidate newly-wrong counts.
            assert sha(args.baseline / name / 'upstream.png') == sha(paths[0])
            paths.append(args.baseline / name / 'native.png')
        images = [Image.open(p).convert('RGB') for p in paths]
        report['frames'].append({'name': name, 'images': {str(p.resolve()): sha(p) for p in paths},
                                 **compare(*images)})
        cx, cy = project(*camera['worldXYZ'], rotation)
        for obj in fixture['objects']:
            sx, sy = project(obj['x'] * 32 + 16, obj['y'] * 32 + 16, obj['baseZ'], rotation)
            sx += camera['width'] // 2 - cx
            sy += camera['height'] // 2 - cy
            rectangle = [sx - 60, sy - 104, sx + 60, sy + 32]
            assert rectangle[0] >= 0 and rectangle[1] >= 0
            assert rectangle[2] <= camera['width'] and rectangle[3] <= camera['height']
            crops = [im.crop(rectangle) for im in images]
            counts = compare(*crops)
            report['contacts'].append({'camera': name, 'ride': obj['ride'], 'kind': obj['kind'],
                                       'direction': obj['direction'], 'phase': obj['phase'],
                                       'guestXYZ': obj['guestXYZ'], 'rectangle': rectangle, **counts})
            if rotation == 0 and (counts['differentPixels'] or obj['phase'] == 3):
                examples.append((f"{obj['kind']} d{obj['direction']} phase{obj['phase']}: {counts['differentPixels']}px", crops))
    # Unsmoothed, labelled crops for manual inspection; metrics above use originals.
    width = 240 * (3 if args.baseline else 2)
    sheet = Image.new('RGB', (width, len(examples) * 292), '#303030')
    draw = ImageDraw.Draw(sheet)
    for row, (label, crops) in enumerate(examples):
        draw.text((2, row * 292), label + ' | upstream, candidate' + (', baseline' if args.baseline else ''), fill='white')
        for col, crop in enumerate(crops):
            sheet.paste(crop.resize((240, 272), Image.Resampling.NEAREST), (col * 240, row * 292 + 20))
    sheet.save(args.output / 'contacts.png')
    report['scope'] = 'Fixed original poses; visual ordering corpus, not simulation traversal or whole-game parity.'
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'frames': [{k: v for k, v in frame.items() if k != 'images'} for frame in report['frames']],
                      'contacts': len(report['contacts'])}))


if __name__ == '__main__':
    main()
