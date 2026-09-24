"""Produce fixed, unmasked original-art specimen crops and exact indexed differences.

The full-frame compare-path-art result remains authoritative. These fixed rectangles
help manual inspection; they can overlap neighbours and do not waive any differences.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', action='append', help='Inspect selected completed views during a serial capture')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    manifest_path = args.corpus / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('status') != 'pass':
        raise ValueError('Incomplete upstream corpus')
    palette = (args.corpus / manifest['palette']).read_bytes()
    if len(palette) != 1024:
        raise ValueError('Palette size differs')
    rgb = [v for i in range(256) for v in (palette[i * 4 + 2], palette[i * 4 + 1], palette[i * 4])]
    reports = []
    inputs = {str(manifest_path): sha(manifest_path)}
    if args.case and not set(args.case).issubset({c['name'] for c in manifest['cases']}):
        raise ValueError('Unknown requested case')
    for case in manifest['cases']:
        if args.case and case['name'] not in args.case:
            continue
        width, height = case['width'], case['height']
        reference_path = args.corpus / case['referenceIndexed']
        native_path = args.samples / (case['name'] + '.indexed')
        expected, actual = reference_path.read_bytes(), native_path.read_bytes()
        if len(expected) != width * height or len(actual) != width * height:
            raise ValueError('Extent differs: ' + case['name'])
        inputs[str(reference_path)] = sha(reference_path)
        inputs[str(native_path)] = sha(native_path)
        reference = Image.frombytes('P', (width, height), expected)
        native = Image.frombytes('P', (width, height), actual)
        reference.putpalette(rgb)
        native.putpalette(rgb)
        zoom, rotation = case['zoom'], case['rotation']
        specimens = []
        for index, item in enumerate(manifest['objects']):
            x, y, z = item['x'] * 32, item['y'] * 32, item.get('baseZ', 64)
            if rotation == 1:
                x += 32
                x, y = y, -x
            elif rotation == 2:
                x, y = -x - 32, -y - 32
            elif rotation == 3:
                y += 32
                x, y = -y, x
            screen_x = ((y - x) >> zoom) - case['viewX']
            screen_y = ((((x + y) >> 1) - z) >> zoom) - case['viewY']
            # Fixed extent around the authored footprint origin, independent of pixel differences.
            rectangle = (max(0, screen_x - (160 >> zoom)), max(0, screen_y - (352 >> zoom)),
                         min(width, screen_x + (160 >> zoom)), min(height, screen_y + (112 >> zoom)))
            if rectangle[0] >= rectangle[2] or rectangle[1] >= rectangle[3]:
                raise ValueError('Specimen outside capture: ' + str(item))
            ref, candidate = reference.crop(rectangle), native.crop(rectangle)
            mask = bytes(255 if a != b else 0 for a, b in zip(ref.tobytes(), candidate.tobytes()))
            difference = Image.frombytes('L', ref.size, mask)
            stem = case['name'] + '-item' + str(index).zfill(2)
            strip = Image.new('RGB', (ref.width * 3, ref.height + 24), '#252525')
            label = f"{index}: {item['kind']} style={item.get('style', '-')} | upstream / native / exact diff"
            ImageDraw.Draw(strip).text((4, 4), label, fill='white')
            strip.paste(ref.convert('RGB'), (0, 24))
            strip.paste(candidate.convert('RGB'), (ref.width, 24))
            strip.paste(difference.convert('RGB'), (ref.width * 2, 24))
            strip.save(args.output / (stem + '.png'))
            specimens.append(strip)
            reports.append(dict(case=case['name'], specimen=index, object=item, rectangle=rectangle,
                                differentIndexedPixels=mask.count(255), comparedPixels=len(mask), image=stem + '.png'))
        for first in range(0, len(specimens), 3):
            rows = specimens[first:first + 3]
            sheet = Image.new('RGB', (max(p.width for p in rows), sum(p.height for p in rows)), '#252525')
            offset = 0
            for row in rows:
                sheet.paste(row, (0, offset))
                offset += row.height
            sheet.save(args.output / (case['name'] + '-page' + str(first // 3).zfill(2) + '.png'))
    summary = dict(schema=1, status='pass' if all(r['differentIndexedPixels'] == 0 for r in reports) else 'fail',
                   scope='Fixed rectangles, exact indexed pixels, no tolerance or masks; overlapping regions may count a pixel more than once; full-frame result remains separate',
                   inputs=inputs, specimens=reports)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=summary['status'], specimens=len(reports), output=str(args.output))))
    return 0 if summary['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
