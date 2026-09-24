"""Translate a restricted, explicit subset of track source rules into immutable GPU data.

This authoring tool parses source text; it never loads the game, calls a painter,
or captures a draw stream. Unknown graphics expressions reject a style/type.
Supports, tunnels, station scenery and vehicles are separate, omitted families.
The checked-in result is runtime input, with raw state selecting each row on GPU.
"""
import argparse
import ast
from collections import ChainMap
from functools import lru_cache
import hashlib
import json
import operator
from pathlib import Path
import re
import struct
import zlib


class Unsupported(ValueError):
    pass


def clean(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)


def tokens(text):
    return re.findall(r'0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|[A-Za-z_]\w*|::|->|<<|>>|&&|\|\||==|!=|<=|>=|\+=|-=|[^\s]', text)


def split_top(values, separator=','):
    out, start, depth = [], 0, 0
    for i, value in enumerate(values):
        if value in ('(', '[', '{'): depth += 1
        elif value in (')', ']', '}'): depth -= 1
        elif value == separator and depth == 0:
            out.append(values[start:i]); start = i + 1
    out.append(values[start:])
    return out


class Parser:
    def __init__(self, values):
        self.v, self.i = values, 0

    def group(self, opening='(', closing=')'):
        if self.v[self.i] != opening: raise Unsupported('expected ' + opening)
        self.i += 1; start = self.i; depth = 1
        while self.i < len(self.v):
            value = self.v[self.i]; self.i += 1
            if value == opening: depth += 1
            elif value == closing: depth -= 1
            if not depth: return self.v[start:self.i - 1]
        raise Unsupported('unterminated group')

    def statement(self):
        value = self.v[self.i]
        if value == '{':
            body = Parser(self.group('{', '}')); result = []
            while body.i < len(body.v): result.append(body.statement())
            return ('block', result)
        if value in ('if', 'switch'):
            self.i += 1
            if self.v[self.i] == 'constexpr': self.i += 1
            expression = self.group()
            if value == 'switch':
                body = Parser(self.group('{', '}')); cases = []; current = None
                while body.i < len(body.v):
                    if body.v[body.i] in ('case', 'default'):
                        label = body.v[body.i]; body.i += 1; start = body.i
                        while body.v[body.i] != ':': body.i += 1
                        current = (None if label == 'default' else body.v[start:body.i], [])
                        body.i += 1; cases.append(current)
                    else:
                        if current is None: raise Unsupported('statement before case')
                        current[1].append(body.statement())
                return ('switch', expression, cases)
            yes = self.statement(); no = None
            if self.i < len(self.v) and self.v[self.i] == 'else':
                self.i += 1; no = self.statement()
            return ('if', expression, yes, no)
        if value in ('for', 'while', 'do', 'goto'): raise Unsupported('control flow ' + value)
        start = self.i; depth = 0
        while self.i < len(self.v):
            value = self.v[self.i]; self.i += 1
            if value in ('(', '[', '{'): depth += 1
            elif value in (')', ']', '}'): depth -= 1
            if value == ';' and depth == 0: return ('expr', self.v[start:self.i - 1])
        raise Unsupported('unterminated statement')


def cxx_divide(a, b):
    return (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1)


BIN = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
       ast.FloorDiv: cxx_divide, ast.Mod: lambda a,b: a-cxx_divide(a,b)*b, ast.BitAnd: operator.and_,
       ast.BitOr: operator.or_, ast.BitXor: operator.xor, ast.LShift: operator.lshift,
       ast.RShift: operator.rshift}
CMP = {ast.Eq: operator.eq, ast.NotEq: operator.ne, ast.Lt: operator.lt,
       ast.LtE: operator.le, ast.Gt: operator.gt, ast.GtE: operator.ge}


@lru_cache(maxsize=None)
def scalar_ast(values):
    text = ' '.join(values)
    methods = {'hasChain': 'chain', 'isInverted': 'inverted', 'isBrakeClosed': 'brakeClosed'}
    for method, name in methods.items():
        text = re.sub(r'trackElement\s*\.\s*' + method + r'\s*\(\s*\)', name, text)
    text = re.sub(r'\b(TrackElemType|TrackStyle)\s*::\s*', '', text)
    text = re.sub(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b', r'\1', text)
    text = text.replace('&&', ' and ').replace('||', ' or ')
    text = re.sub(r'!(?!=)', ' not ', text).replace('/', '//').strip()
    try: return ast.parse(text, mode='eval').body
    except SyntaxError as e: raise Unsupported('expression ' + text) from e


def evaluate(values, env):
    if not values: raise Unsupported('empty expression')
    if values[0] == '{' and values[-1] == '}':
        return [evaluate(v, env) for v in split_top(values[1:-1]) if v]
    # C++ ternary is right associative; handle it before the safe scalar AST.
    depth = 0; question = None; nested = 0
    for i, value in enumerate(values):
        if value in ('(', '[', '{'): depth += 1
        elif value in (')', ']', '}'): depth -= 1
        elif depth == 0 and value == '?':
            if question is None: question = i
            else: nested += 1
        elif depth == 0 and value == ':' and question is not None:
            if nested: nested -= 1
            else:
                branch = values[question + 1:i] if evaluate(values[:question], env) else values[i + 1:]
                return evaluate(branch, env)
    node = scalar_ast(tuple(values))

    def walk(n):
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, bool)): return n.value
        if isinstance(n, ast.Name):
            if n.id in ('true', 'false'): return n.id == 'true'
            if n.id not in env: raise Unsupported('identifier ' + n.id)
            value = env[n.id]
            if isinstance(value, Deferred): return value.resolve(env)
            return value
        if isinstance(n, ast.Subscript):
            try: return walk(n.value)[walk(n.slice)]
            except (IndexError, TypeError) as e: raise Unsupported('array index') from e
        if isinstance(n, ast.BinOp) and type(n.op) in BIN: return BIN[type(n.op)](walk(n.left), walk(n.right))
        if isinstance(n, ast.UnaryOp):
            value = walk(n.operand)
            if isinstance(n.op, ast.USub): return -value
            if isinstance(n.op, ast.UAdd): return value
            if isinstance(n.op, ast.Invert): return ~value
            if isinstance(n.op, ast.Not): return not value
        if isinstance(n, ast.BoolOp):
            return all(walk(v) for v in n.values) if isinstance(n.op, ast.And) else any(walk(v) for v in n.values)
        if isinstance(n, ast.Compare) and len(n.ops) == 1 and type(n.ops[0]) in CMP:
            return CMP[type(n.ops[0])](walk(n.left), walk(n.comparators[0]))
        raise Unsupported('expression ' + ' '.join(values))
    return walk(node)


class Deferred:
    def __init__(self, value): self.value = value
    def resolve(self, env): return evaluate(self.value, env)


class ImageValue:
    def __init__(self, image, role): self.image, self.role = image, role


def image_value(values, env):
    text = ''.join(values)
    if text == 'session.TrackColours': return ImageValue(0, 0)
    if text == 'session.SupportColours': return ImageValue(0, 1)
    match = re.fullmatch(r'(.+)\.(WithIndex|WithIndexOffset)\((.*)\)', text)
    if match:
        base = image_value(tokens(match[1]), env); index = evaluate(tokens(match[3]), env)
        return ImageValue(index if match[2] == 'WithIndex' else base.image + index, base.role)
    if len(values) == 1 and values[0] in env:
        value = env[values[0]]
        if isinstance(value, ImageValue): return value
        if isinstance(value, Deferred): return image_value(value.value, env)
    raise Unsupported('image expression ' + text)


def declaration(values, env):
    if len(values) == 2 and values[0] == 'ImageId':
        env[values[1]] = None  # Reading before a branch assigns it is rejected.
        return True
    if '=' not in values: return False
    index = values.index('='); lhs, rhs = values[:index], values[index + 1:]
    if any(v.startswith('PaintAddImage') or v == 'TrackPaint' for v in rhs):
        raise Unsupported('draw-valued declaration requires explicit ownership')
    # First bracket belongs to the declared array, never to its initializer.
    if '[' in lhs: lhs = lhs[:lhs.index('[')]
    if not lhs or not re.fullmatch(r'[A-Za-z_]\w*', lhs[-1]): return False
    name = lhs[-1]
    if any(token in rhs for token in ('TrackColours', 'SupportColours', 'WithIndex', 'WithIndexOffset')) or (
            len(rhs) == 1 and isinstance(env.get(rhs[0]), ImageValue)):
        env[name] = image_value(rhs, env)
    elif len(lhs) == 1:
        env[name] = evaluate(rhs, env)
    else:
        env[name] = Deferred(rhs)
    return True


# These calls exclusively belong to explicitly omitted component families or
# painter bookkeeping. An unknown call is never silently discarded.
AUX = re.compile(r'^(?:Metal[AB]SupportsPaintSetup(?:Rotated)?|Wooden[AB]SupportsPaintSetup(?:Rotated)?|'
                 r'WoodenSupportsPrependTo|DrawSupportForSequence[AB]<[^>]+>|'
                 r'PaintUtil(?:SetSegmentSupportHeight|SetGeneralSupportHeight|SetVerticalTunnel|PushTunnel\w*)|'
                 r'TrackPaintUtil(?:DrawStation\w*|DrawSupports\w*|OnridePhotoPaint\w*)|DrawSupportsSideBySide)$')


def call(values):
    return cached_call(tuple(values))


@lru_cache(maxsize=None)
def cached_call(values):
    try: opening = values.index('(')
    except ValueError: return None
    if values[-1] != ')': return None
    return ''.join(values[:opening]), split_top(values[opening + 1:-1])


AUXILIARY_CACHE = {}


def auxiliary(node):
    if node is None: return True
    if node[0] == 'expr': return classify_auxiliary(node)
    key = id(node)
    if key not in AUXILIARY_CACHE: AUXILIARY_CACHE[key] = (node, classify_auxiliary(node))
    return AUXILIARY_CACHE[key][1]


def classify_auxiliary(node):
    if node is None: return True
    if node[0] == 'block': return all(auxiliary(n) for n in node[1])
    if node[0] == 'if': return auxiliary(node[2]) and auxiliary(node[3])
    if node[0] == 'switch': return all(auxiliary(n) for _, body in node[2] for n in body)
    values = node[1]
    # Control flow is never auxiliary, even inside a block containing only
    # omitted support calls: break/return can prevent later rail emission.
    return not values or bool(call(values) and AUX.fullmatch(call(values)[0]))


class Source:
    def __init__(self, path, text):
        self.path, self.text, self.functions, self.globals = path, clean(text), {}, {}
        self.errors = {}
        pattern = r'\b(?:void|TrackPaintFunction)\s+([\w:]+)\s*\(([^{};]*)\)\s*\{'
        for match in re.finditer(pattern, self.text):
            start = match.end() - 1; end = start + 1; depth = 1
            while depth and end < len(self.text):
                if self.text[end] == '{': depth += 1
                elif self.text[end] == '}': depth -= 1
                end += 1
            values = tokens(self.text[start:end])
            template = re.search(r'template\s*<\s*bool\s+(\w+)\s*>\s*(?:static\s+)?$', self.text[max(0,match.start()-100):match.start()])
            variants = ((match[1]+'<'+v+'>',[v if t==template[1] else t for t in values]) for v in ('false','true')) if template else [(match[1],values)]
            for name, body in variants:
                try: self.functions[name] = Parser(body).statement()
                except Unsupported as e: self.errors[name] = str(e)
        # Numeric constexpr tables (including sequence remaps). Other C++
        # declarations remain unavailable and reject the affected graphics rule.
        for match in re.finditer(r'(?:static\s+)?(?:constexpr|const)\s+(?:u?int\d+_t|ImageIndex|auto)\s+(\w+)\s*(?:\[[^;=]*\])?\s*=\s*([^;]+);', self.text):
            self.globals[match[1]] = Deferred(tokens(match[2]))
        for match in re.finditer(r'\benum[^{}]*\{(.*?)\}', self.text, re.S):
            previous = None
            for member in split_top(tokens(match[1])):
                if not member: continue
                name = member[0]
                value = member[2:] if len(member)>1 and member[1]=='=' else (['0'] if previous is None else [previous,'+','1'])
                self.globals[name] = Deferred(value); previous = name


class Translator:
    def __init__(self, root):
        self.root = root; self.constants = {}; self.sources = []; self.by_name = {}
        inputs = ['src/openrct2/SpriteIds.h', 'src/openrct2/ride/ted/TrackElemType.h', 'src/openrct2/ride/TrackStyle.h',
                  'src/openrct2/ride/TrackStyle.cpp', 'src/openrct2/ride/TrackPaint.h']
        inputs += ['src/openrct2/ride/TrackPaint.cpp', 'src/openrct2/paint/track/coaster/WoodenRollerCoaster.hpp']
        inputs += [str(p.relative_to(root)).replace('\\', '/') for p in sorted((root/'src/openrct2/paint/track').rglob('*.cpp'))]
        self.hashes = {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in inputs}
        self.enum(root/inputs[0]); self.enum(root/inputs[1]); self.type_count = self.constants['count']
        self.enum(root/inputs[2]); self.constants['kNumOrthogonalDirections'] = 4
        for path in inputs[4:7]:
            shared = Source(path, (root/path).read_text()); self.constants.update(shared.globals)
        for path in inputs[7:]:
            source = Source(path, (root/path).read_text()); self.sources.append(source)
            for name in source.functions:
                if name not in self.by_name: self.by_name[name] = source
                else: self.by_name[name] = None  # ambiguous names require namespace support
        style_text = clean((root/'src/openrct2/ride/TrackStyle.cpp').read_text())
        body = re.search(r'kPaintFunctionMap\[\]\s*=\s*\{(.*?)\}', style_text, re.S)[1]
        self.getters = [v.strip() for v in body.split(',') if v.strip()][:81]

    def enum(self, path):
        text = clean(path.read_text()); match = re.search(r'\benum[^{}]*\{(.*?)\}', text, re.S)
        previous = -1
        for member in split_top(tokens(match[1])):
            if not member: continue
            name = member[0]
            try:
                previous = evaluate(member[2:], self.constants) if len(member) > 1 and member[1] == '=' else previous + 1
                self.constants[name] = previous
            except Unsupported:
                # The following implicit enumerators are unavailable until a
                # new explicit, evaluable anchor restores the numeric sequence.
                previous = None
            except TypeError: previous = None

    def find(self, name, source=None):
        if source and name in source.functions: return source
        result = self.by_name.get(name)
        if result is None: raise Unsupported('function ' + name)
        return result

    def getter(self, name, track_type, depth=0):
        if depth > 8: raise Unsupported('getter cycle')
        source = self.find(name); node = source.functions[name]
        env = ChainMap(dict(trackType=track_type), self.constants)
        result = self.execute(node, env, source, [], 0, getter=True)
        if not isinstance(result, tuple) or result[0] != 'return': raise Unsupported('getter without return')
        expression = result[1]
        invoked = call(expression)
        if invoked: return self.getter(invoked[0], evaluate(invoked[1][0], env), depth + 1)
        function = ''.join(expression)
        if not re.fullmatch(r'[\w:]+(?:<(?:true|false)>)?', function): raise Unsupported('template getter')
        return self.find(function, source), function

    def execute(self, node, env, source, parts, depth, getter=False):
        kind = node[0]
        if not getter and auxiliary(node): return None
        if kind == 'block':
            for item in node[1]:
                flow = self.execute(item, env, source, parts, depth, getter)
                if flow: return flow
        elif kind == 'if':
            branch = node[2] if evaluate(node[1], env) else node[3]
            if branch: return self.execute(branch, env, source, parts, depth, getter)
        elif kind == 'switch':
            selected = evaluate(node[1], env); start = None; default = None
            for i, (label, _) in enumerate(node[2]):
                if label is None: default = i
                elif evaluate(label, env) == selected: start = i; break
            if start is None: start = default
            if start is not None:
                for _, body in node[2][start:]:
                    for item in body:
                        flow = self.execute(item, env, source, parts, depth, getter)
                        if flow == 'break': return None
                        if flow: return flow
        else:
            values = node[1]
            if not values: return None
            if values[0] == 'break': return 'break'
            if values[0] == 'return':
                if not getter and len(values) != 1: raise Unsupported('value return in paint function')
                return ('return', values[1:])
            if '=' in values:
                equal = values.index('='); lhs, rhs = values[:equal], values[equal + 1:]
                # This exact support bookkeeping assignment still executes its
                # RHS rail draw. Never treat an image-producing RHS as lazy data.
                if ''.join(lhs) == 'session.WoodenSupportsPrependTo':
                    invoked = call(rhs)
                    if not invoked or invoked[0] not in ('PaintAddImageAsParent','PaintAddImageAsParentRotated'):
                        raise Unsupported('support prepend pointer assignment')
                    self.execute(('expr',list(rhs)),env,source,parts,depth)
                    return None
            if declaration(values, env): return None
            invoked = call(values)
            if not invoked: raise Unsupported('statement ' + ' '.join(values[:12]))
            name, args = invoked
            if AUX.fullmatch(name): return None
            if name in ('TrackPaint<false>', 'TrackPaint<true>'):
                # WoodenRollerCoaster.hpp TrackPaint is exactly an authored
                # body parent and (modern style only) rails child. This adapter
                # translates that shared source helper, not a runtime painter.
                if len(args) != 6: raise Unsupported('wooden TrackPaint signature')
                image = args[2]; role = 'TrackColours' if name.endswith('<true>') else 'SupportColours'
                parent_args = [args[0],args[1],tokens('session.'+role+'.WithIndex('+''.join(image)+')'),args[4],args[5]]
                statement = ['PaintAddImageAsParentRotated','(']
                for i,a in enumerate(parent_args): statement += ([','] if i else []) + list(a)
                statement += [')']
                self.execute(('expr',statement),env,source,parts,depth)
                if name.endswith('<false>'):
                    child_args = list(parent_args); child_args[2] = tokens('session.TrackColours.WithIndex('+''.join(args[3])+')')
                    statement = ['PaintAddImageAsChildRotated','(']
                    for i,a in enumerate(child_args): statement += ([','] if i else []) + list(a)
                    statement += [')']
                    self.execute(('expr',statement),env,source,parts,depth)
                return None
            if name in ('PaintAddImageAsParent', 'PaintAddImageAsChild', 'PaintAddImageAsParentRotated', 'PaintAddImageAsChildRotated'):
                rotated = name.endswith('Rotated'); shift = int(rotated)
                direction = evaluate(args[1], env) if rotated else 0
                selected = image_value(args[1 + shift], env)
                image, role = selected.image, selected.role
                offset = list(evaluate(args[2 + shift], env)); box = evaluate(args[3 + shift], env)
                if len(box) == 3 and all(isinstance(v, int) for v in box): box = [list(offset), box]
                if len(offset) != 3 or len(box) != 2 or any(len(v) != 3 for v in box): raise Unsupported('bounds shape')
                box = [list(v) for v in box]
                if direction & 1:
                    offset[0], offset[1] = offset[1], offset[0]
                    for v in box: v[0], v[1] = v[1], v[0]
                if not 0 <= image < 0x7ffff: raise Unsupported('image range')
                parent = -1
                if 'Child' in name:
                    parent = next((i for i in range(len(parts)-1, -1, -1) if parts[i][-1] == -1), -1)
                    if parent < 0: raise Unsupported('child without parent')
                parts.append(tuple([image] + offset + box[0] + box[1] + [role, parent]))
                if len(parts) > 64: raise Unsupported('component capacity')
            else:
                target = self.find(name, source)
                if len(args) != 7: raise Unsupported('helper signature ' + name)
                self.paint(target, name, evaluate(args[2], env), evaluate(args[3], env), evaluate(args[4], env),
                           env['state'], parts, depth + 1)
        return None

    def paint(self, source, name, sequence, direction, height, state, parts, depth=0):
        if depth > 16: raise Unsupported('call depth')
        env = ChainMap({}, source.globals, self.constants)
        env.update(trackSequence=sequence, direction=direction, height=height, state=state,
                   chain=bool(state & 1), inverted=bool(state & 2), brakeClosed=bool(state & 4))
        self.execute(source.functions[name], env, source, parts, depth)


def build(args):
    translator = Translator(args.root)
    descriptors = [0] * (81 * translator.type_count * 3)
    rows, parts = [], []; part_sets = {}; row_sets = {}; coverage = []
    # Cache source functions shared by multiple styles/types, without runtime objects.
    cache = {}
    for style, getter in enumerate(translator.getters):
        print('Translating style %d/%d %s' % (style+1,len(translator.getters),getter), flush=True)
        style_report = dict(style=style, getter=getter, supported=[], rejected={})
        for track_type in range(translator.type_count):
            try:
                source, name = translator.getter(getter, track_type)
                key = (source.path, name)
                if key not in cache:
                    variants = []; errors = []
                    for state in range(8):
                        states = []
                        for sequence in range(16):
                            directions = []
                            for direction in range(4):
                                result = []
                                try:
                                    translator.paint(source, name, sequence, direction, 0, state, result)
                                    directions.append(tuple(result))
                                except (Unsupported, ZeroDivisionError, RecursionError) as e:
                                    directions.append(None); errors.append(str(e))
                            states.append(directions)
                        variants.append(states)
                    maximum = max((seq for state in variants for seq, directions in enumerate(state) if any(directions)), default=-1)
                    if maximum < 0: cache[key] = Unsupported(errors[0] if errors else 'no rail graphics')
                    elif any(row is None for state in variants for directions in state[:maximum + 1] for row in directions):
                        cache[key] = Unsupported(errors[0])
                    else:
                        mask = next(mask for mask in range(8) if all(variants[state] == variants[state & mask] for state in range(8)))
                        cache[key] = (variants, maximum + 1, mask)
                value = cache[key]
                if isinstance(value, Unsupported): raise value
                variants, sequences, mask = value
                packed_rows = []
                for state in range(8):
                    if state & ~mask: continue
                    for directions in variants[state][:sequences]:
                        for recipe in directions:
                            if recipe not in part_sets:
                                part_sets[recipe] = len(parts) // 12
                                for part in recipe: parts.extend(v & 0xffffffff for v in part)
                            packed_rows.extend((part_sets[recipe], len(recipe)))
                row_key = tuple(packed_rows)
                if row_key not in row_sets:
                    row_sets[row_key] = len(rows) // 2; rows.extend(packed_rows)
                offset = (style * translator.type_count + track_type) * 3
                descriptors[offset:offset+3] = [row_sets[row_key], sequences, mask]
                style_report['supported'].append(track_type)
            except (Unsupported, KeyError) as e:
                style_report['rejected'][str(track_type)] = str(e)
        coverage.append(style_report)
    header = [0x5452434b, 1, 81, translator.type_count, 8, 8 + len(descriptors), 8 + len(descriptors) + len(rows), 0]
    words = header + descriptors + rows + parts; words[7] = len(words)
    args.output.mkdir(parents=True, exist_ok=True)
    binary = struct.pack('<' + 'I' * len(words), *words)
    (args.output/'native-track-recipes.bin').write_bytes(binary)
    if not 8 <= len(words) <= 16 * 1024 * 1024: raise Unsupported('catalog word capacity')
    compressed = zlib.compress(binary, level=9)
    lines = ['// Generated by extract-native-track-recipes.py; do not edit.',
             'static constexpr uint32_t kNativeTrackRecipeWordCount = '+str(len(words))+'u;',
             'static constexpr uint8_t kNativeTrackRecipeCompressed[] = {']
    lines += ['    ' + ','.join('0x%02x'%v for v in compressed[i:i+24]) + ',' for i in range(0,len(compressed),24)]
    lines += ['};', '']
    (args.output/'NativeTrackRecipeData.inc').write_text('\n'.join(lines), encoding='utf-8')
    report = dict(schema=1, sourceSha256=translator.hashes, recipeSha256=hashlib.sha256(binary).hexdigest(),
                  authoringScriptSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  compressedSha256=hashlib.sha256(compressed).hexdigest(), compressedBytes=len(compressed),
                  generatorZlibVersion=zlib.ZLIB_RUNTIME_VERSION,
                  wordCount=len(words), rowCount=len(rows)//2, partCount=len(parts)//12,
                  supportedStyleCount=sum(bool(s['supported']) for s in coverage),
                  supportedStyleTypes=sum(len(s['supported']) for s in coverage), styles=coverage,
                  omittedFamilies=['supports','tunnels','station scenery','photo cameras','vehicles'],
                  scope='Static source translation; runtime row selection executes on GPU; no raster qualification')
    (args.output/'coverage.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('sourceSha256','styles')}))
    if not report['supportedStyleTypes']: raise Unsupported('no supported rules')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args())
