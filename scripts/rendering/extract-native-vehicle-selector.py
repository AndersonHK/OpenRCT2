#!/usr/bin/env python3
"""Author immutable group-selection graph from the original vehicle selectors.

No game loading, paint calls, sprite decoding, or live-instance selection. The
bounded GPU interpreter chooses a group using raw pitch/roll and held metadata.
"""
import hashlib
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]


def body_at(text, start):
    start = text.index('{', start)
    level = 1
    end = start + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start + 1:end - 1]


def enum_names(path, name):
    source = path.read_text()
    body = body_at(source, source.index('enum class ' + name))
    body = re.sub(r'//[^\n]*', '', body)
    return [x.strip().split('=')[0].strip() for x in body.split(',') if x.strip()]


def generate():
    path = ROOT / 'src/openrct2/paint/vehicle/VehiclePaint.cpp'
    original = path.read_text()
    source = re.sub(r'//[^\n]*', '', original)
    functions = {}
    for m in re.finditer(r'(?:static )?void (VehiclePitch\w+)\(', source):
        functions[m[1]] = body_at(source, m.end())
    groups = enum_names(ROOT / 'src/openrct2/ride/CarEntry.h', 'SpriteGroupType')[:-1]
    rolls = enum_names(ROOT / 'src/openrct2/ride/Angles.h', 'VehicleRoll')[:-2]
    pitches = enum_names(ROOT / 'src/openrct2/ride/Angles.h', 'VehiclePitch')[:-2]
    # Template instantiation is finite and source-defined.
    cork = functions.pop('VehiclePitchCorkscrew')
    for rank in range(20):
        functions['VehiclePitchCorkscrew' + str(rank)] = cork.replace('corkscrewFrame', str(rank))
    for name in functions:
        functions[name] = re.sub(r'VehiclePitchCorkscrew<(\d+)>', r'VehiclePitchCorkscrew\1', functions[name])
    roots = re.search(r'PaintFunctionsByPitch\[\] = \{(.*?)\};', source, re.S)[1]
    roots = re.sub(r'VehiclePitchCorkscrew<(\d+)>', r'VehiclePitchCorkscrew\1', roots)
    roots = [v.strip() for v in roots.split(',') if v.strip()]
    assert len(roots) == len(pitches) == 61
    assert len(rolls) == 20 and len(groups) == 40
    nodes = []
    node_ids = {}
    resolving = set()

    def resolve(name, roll):
        key = name, roll
        if key in node_ids:
            return node_ids[key]
        assert key not in resolving, ('selector cycle', key)
        resolving.add(key)
        body = functions[name]
        # Bounding-box-only branches never affect sprite selection.
        if 'switch (GetPaintBankRotation(vehicle))' in body:
            switch = body_at(body, body.index('switch (GetPaintBankRotation(vehicle))'))
            matches = list(re.finditer(r'case VehicleRoll::(\w+):|default:', switch))
            targets = {}
            pending = []
            for i, match in enumerate(matches):
                pending.append(match[1] if match[1] else 'default')
                segment = switch[match.end():matches[i+1].start() if i+1<len(matches) else len(switch)]
                call = re.search(r'(VehiclePitch\w+)\(', segment)
                if call:
                    for label in pending:
                        targets[label] = call[1]
                    pending = []
            target = targets.get(rolls[roll], targets.get('default'))
            assert target, (name, roll)
            result = resolve(target, roll)
        else:
            calls = re.findall(r'(VehiclePitch\w+)\(session, vehicle,\s*(.*?),\s*z, carEntry, boundingBoxIndex\)', body, re.S)
            assert len(calls) <= 1, name
            fallback = resolve(calls[0][0], roll) if calls else -1
            fail_yaw = 0
            if calls:
                expression = calls[0][1].strip()
                if expression != 'imageDirection':
                    assert re.fullmatch(r'\(imageDirection [+-] [28]\) % 32', expression), (name, expression)
                    offset = re.search(r'([+-]) (\d+)', expression)
                    fail_yaw = int(offset[2]) * (-1 if offset[1] == '-' else 1)
            image = re.search(r'getSpriteOffset\(\s*SpriteGroupType::(\w+),\s*(\w+),\s*(\w+)\)', body)
            group = groups.index(image[1]) if image else -1
            rank = 0
            yaw = 0
            if image:
                rank = int(image[3]) if image[3].isdigit() else int(re.search(r'corkscrewFrame = (\d+)', body)[1])
                if image[2] != 'imageDirection':
                    assert image[2] == 'modifiedImageDirection' and '(imageDirection + 8) % 32' in body
                    yaw = 8
            delta = 0
            if 'carEntry--' in body:
                delta = 1 if 'VehicleFlag::carIsInverted' in body else 2
                if name == 'VehiclePitchDown75': delta = 3
                if name == 'VehiclePitchDown90': delta = 4
            if name == 'VehiclePitchFlatUnbanked': group = -2  # Optional cardinal restraints, then flat.
            assert group >= 0 or group == -2 or fallback >= 0, name
            if group >= 0:
                # Some original downward loop selectors test slopes90 while
                # addressing slopesLoop. Preserve the predicate separately from
                # the chosen image group instead of assuming they are identical.
                guard = re.search(r'groupEnabled\(SpriteGroupType::(\w+)\)', body)
                guard_group = groups.index(guard[1]) if guard else group
                group |= guard_group << 8
            row = (group, rank, yaw, delta, fallback, fail_yaw)
            try:
                result = nodes.index(row)
            except ValueError:
                result = len(nodes)
                nodes.append(row)
        node_ids[key] = result
        resolving.remove(key)
        return result

    starts = [resolve(name, roll) for name in roots for roll in range(20)]
    pitch_inverse = re.findall(r'VehiclePitch::(\w+)', re.search(r'PitchInvertTable\[\] = \{(.*?)\};', source, re.S)[1])
    roll_inverse = re.findall(r'VehicleRoll::(\w+)', re.search(r'RollInvertTable\[\] = \{(.*?)\};', source, re.S)[1])
    # Header8; starts61*20; node6; pitch/roll inverse tables.
    header = [0x56534C31, 1, len(starts), len(nodes), 8, 8+len(starts), 8+len(starts)+len(nodes)*6, 0]
    words = header + starts + [v & 0xffffffff for row in nodes for v in row]
    words += [pitches.index(v) for v in pitch_inverse] + [rolls.index(v) for v in roll_inverse]
    words[7] = len(words)
    output = '// Generated by scripts/rendering/extract-native-vehicle-selector.py.\n'
    output += '// VehiclePaint.cpp SHA256: ' + hashlib.sha256(path.read_bytes()).hexdigest() + '\n'
    output += '// Only immutable selection decisions; no world instances or paint commands.\n'
    output += '\n'.join(', '.join(str(x) + 'u' for x in words[i:i+12]) + ',' for i in range(0, len(words), 12)) + '\n'
    target = ROOT / 'src/openrct2/drawing/NativeVehicleSelector.inc'
    target.write_text(output)
    print(f'{len(nodes)} nodes; {len(words)} words; {len(starts)} pitch/roll entry points')


if __name__ == '__main__':
    generate()
