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


STATE_BITS = {'chain':1, 'inverted':2, 'brakeClosed':4, 'cable':8, 'csgLoaded':16, 'greenLight':32, 'hasPlatforms':64}
STATION_PART = 0xfffffffe
STATION_CALLS = ('TrackPaintUtilDrawStation','TrackPaintUtilDrawStation2','TrackPaintUtilDrawStationInverted',
                 'TrackPaintUtilDrawNarrowStationPlatform','TrackPaintUtilDrawPier')


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
    methods = {'hasChain': 'chain', 'isInverted': 'inverted', 'isBrakeClosed': 'brakeClosed',
               'hasCableLift': 'cable', 'hasGreenLight': 'greenLight', 'getTrackType': 'trackType'}
    for method, name in methods.items():
        text = re.sub(r'trackElement\s*\.\s*' + method + r'\s*\(\s*\)', name, text)
    text = re.sub(r'IsCsgLoaded\s*\(\s*\)', 'csgLoaded', text)
    for function,offset in (('DirectionNext',1),('DirectionPrev',3),('DirectionReverse',2)):
        text=re.sub(function+r'\s*\(\s*(\w+)\s*\)',r'((\1 + '+str(offset)+') & 3)',text)
    text = re.sub(r'\b(TrackElemType|TrackStyle|JuniorRCSubType)\s*::\s*', '', text)
    text=re.sub(r'PaintSegment\s*::\s*','PaintSegment_',text)
    text=re.sub(r'TunnelSubType\s*::\s*','TunnelSubType_',text)
    text = re.sub(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b', r'\1', text)
    text = text.replace('&&', ' and ').replace('||', ' or ')
    text = re.sub(r'!(?!=)', ' not ', text).replace('/', '//').strip()
    try: return ast.parse(text, mode='eval').body
    except SyntaxError as e: raise Unsupported('expression ' + text) from e


def evaluate(values, env):
    if not values: raise Unsupported('empty expression')
    junior=re.fullmatch(r'JuniorRCGetSubTypeOffset<JuniorRCSubType::(junior|waterCoaster)>\(trackElement\)', ''.join(values))
    if junior: return (1 if junior[1]=='junior' else 2) if evaluate(['chain'],env) else 0
    flags=call(values)
    if flags and flags[0]=='EnumsToFlags':
        result=0
        for arg in flags[1]: result |= 1 << evaluate(arg,env)
        return result
    if values[0] == '{' and values[-1] == '}':
        result=[evaluate(v, env) for v in split_top(values[1:-1]) if v]
        if len(result)==2 and isinstance(result[0],list) and len(result[0])==2 and isinstance(result[1],int):
            return result[0]+[result[1]]
        return result
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
            if n.id in STATE_BITS and '_dependencies' in env: env['_dependencies'].add(STATE_BITS[n.id])
            value = env[n.id]
            if isinstance(value, Deferred): return value.resolve(env)
            if value is None: raise Unsupported('uninitialised local '+n.id)
            return value
        if isinstance(n, ast.Subscript):
            try:
                array,index=walk(n.value),walk(n.slice)
                # C++ indices never wrap to the end as Python negative indices do.
                if not isinstance(index,int) or index<0: raise IndexError(index)
                return array[index]
            except (IndexError, TypeError) as e: raise Unsupported('array index') from e
        if isinstance(n,ast.Attribute) and n.attr in ('x','y','z'):
            value=walk(n.value);index=('x','y','z').index(n.attr)
            if isinstance(value,list) and index<len(value) and all(isinstance(v,int) for v in value): return value[index]
        if isinstance(n, ast.BinOp) and type(n.op) in BIN:
            left,right=walk(n.left),walk(n.right)
            # C++ array-to-pointer offset, used by the second diagonal-brake row.
            if isinstance(n.op,ast.Add) and isinstance(left,list) and isinstance(right,int):
                if not 0<=right<=len(left): raise Unsupported('array pointer offset')
                return left[right:]
            try: return BIN[type(n.op)](left,right)
            except TypeError as e: raise Unsupported('non-scalar arithmetic') from e
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
    while values and values[0]=='(' and values[-1]==')': values=values[1:-1]
    text = ''.join(values)
    if '?' in values:
        question=values.index('?'); colon=values.index(':',question)
        return image_value(values[question+1:colon] if evaluate(values[:question],env) else values[colon+1:],env)
    if text == 'session.TrackColours': return ImageValue(0, 0)
    if text == 'session.SupportColours': return ImageValue(0, 1)
    if text == 'WoodenRCGetRailsColour(session)': return ImageValue(0, 0)
    if text == 'GetStationColourScheme(session,trackElement)': return ImageValue(0, 2)
    if text == 'GetTrackColour(session)': return ImageValue(0, 3)
    if text in ('WoodenRCGetTrackColour<false>(session)','WoodenRCGetTrackColour<true>(session)'):
        return ImageValue(0, 0 if '<true>' in text else 1)
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
    if len(values) == 2 and values[0] in ('ImageId','TunnelSubType','int16_t','int32_t','uint16_t','uint32_t'):
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
    if any(token in rhs for token in ('TrackColours', 'SupportColours', 'WithIndex', 'WithIndexOffset',
                                    'WoodenRCGetTrackColour','WoodenRCGetRailsColour','GetTrackColour',
                                    'GetStationColourScheme')) or (
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
                 r'TrackPaintUtil(?:DrawStation\w*|DrawSupports\w*|OnridePhotoPaint\w*|'
                 r'(?:Left|Right)QuarterTurn\w*Tunnel|RightVerticalLoopSegments|LeftCorkscrewUpSupports)|'
                 r'DrawSBend(?:Left|Right)Supports|DrawSupportsSideBySide)$')


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
    if node[0] == 'if':
        if any(v in STATION_CALLS for v in node[1]): return False
        return auxiliary(node[2]) and auxiliary(node[3])
    if node[0] == 'switch': return all(auxiliary(n) for _, body in node[2] for n in body)
    values = node[1]
    # Control flow is never auxiliary, even inside a block containing only
    # omitted support calls: break/return can prevent later rail emission.
    invoked=call(values)
    if invoked and invoked[0] in STATION_CALLS: return False
    return not values or bool(invoked and AUX.fullmatch(invoked[0]))


def auxiliary_local_block(node):
    """A support-only lexical block may also declare its own unused scalar locals.

    Never discard assignments to outer variables, control flow, or graphics calls.
    """
    if node is None: return True
    if node[0] != 'block': return auxiliary(node)
    for item in node[1]:
        if auxiliary(item): continue
        if item[0]!='expr': return False
        value=item[1]
        if not (len(value)>3 and value[0] in ('uint8_t','uint16_t','uint32_t','int8_t','int16_t','int32_t')
                and re.fullmatch(r'[A-Za-z_]\w*',value[1]) and value[2]=='='): return False
        if call(value[3:]) or any(v.startswith('Paint') for v in value[3:]): return False
    return True


class Source:
    def __init__(self, path, text):
        self.path, self.text, self.functions, self.globals = path, clean(text), {}, {}
        namespace=re.search(r'namespace\s+(OpenRCT2::\w+)\s*\{', self.text)
        self.namespace=namespace[1] if namespace else None
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
            junior=re.search(r'template\s*<\s*JuniorRCSubType\s+(\w+)\s*>\s*(?:static\s+)?$',self.text[max(0,match.start()-100):match.start()])
            if junior:
                variants=[(match[1]+'<JuniorRCSubType::'+v+'>',tokens(' '.join(values).replace(junior[1],'JuniorRCSubType::'+v)))
                          for v in ('junior','waterCoaster')]
            for name, body in variants:
                try: self.functions[name] = Parser(body).statement()
                except Unsupported as e: self.errors[name] = str(e)
        # Numeric constexpr tables (including sequence remaps). Other C++
        # declarations remain unavailable and reject the affected graphics rule.
        for match in re.finditer(r'(?:static\s+)?(?:constexpr|const)\s+(?:u?int\d+_t|ImageIndex|CoordsXY|CoordsXYZ|SpriteBb|SpriteBoundBox2|auto)\s+(\w+)\s*(?:\[[^;=]*\])?\s*=\s*([^;]+);', self.text):
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
        extra=['src/openrct2/paint/track_pieces/QuarterHelix.h','src/openrct2/paint/track_pieces/QuarterHelix.cpp',
               'src/openrct2/paint/Boundbox.h','src/openrct2/paint/track/TrackPaintGeneric.h',
               'src/openrct2/ride/TrackData.cpp','src/openrct2/world/MapLimits.h','src/openrct2/paint/tile_element/Segment.h']
        self.hashes = {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in inputs}
        self.hashes.update({p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in extra})
        self.enum(root/inputs[0]); self.enum(root/inputs[1]); self.type_count = self.constants['count']
        self.enum(root/inputs[2]); self.constants['kNumOrthogonalDirections'] = 4
        segment_text=clean((root/extra[-1]).read_text())
        segment_body=re.search(r'enum class PaintSegment[^\{]*\{(.*?)\}',segment_text,re.S)[1]
        for name,value in re.findall(r'(\w+)\s*=\s*(\d+)',segment_body): self.constants['PaintSegment_'+name]=int(value)
        tunnel_path='src/openrct2/paint/tile_element/Paint.Tunnel.h'
        self.hashes[tunnel_path]=hashlib.sha256((root/tunnel_path).read_bytes()).hexdigest()
        tunnel_body=re.search(r'enum class TunnelSubType[^\{]*\{(.*?)\}',clean((root/tunnel_path).read_text()),re.S)[1]
        for name,value in re.findall(r'(\w+)\s*=\s*(\d+)',tunnel_body): self.constants['TunnelSubType_'+name]=int(value)
        shared_sources=[]
        for path in inputs[4:7]:
            shared = Source(path, (root/path).read_text()); self.constants.update(shared.globals)
            shared_sources.append(shared)
        for path in inputs[7:]:
            source = Source(path, (root/path).read_text()); self.sources.append(source)
            for name in source.functions:
                if name not in self.by_name: self.by_name[name] = source
                else: self.by_name[name] = None  # ambiguous names require namespace support
                if source.namespace: self.by_name[source.namespace+'::'+name]=source
        for source in shared_sources:
            for name in source.functions:
                if name not in self.by_name: self.by_name[name]=source
        style_text = clean((root/'src/openrct2/ride/TrackStyle.cpp').read_text())
        body = re.search(r'kPaintFunctionMap\[\]\s*=\s*\{(.*?)\}', style_text, re.S)[1]
        self.getters = [v.strip() for v in body.split(',') if v.strip()][:81]
        self.load_helix_tables()

    def load_helix_tables(self):
        header=clean((self.root/'src/openrct2/paint/track_pieces/QuarterHelix.h').read_text())
        source=clean((self.root/'src/openrct2/paint/track_pieces/QuarterHelix.cpp').read_text())
        self.helix_maps={}
        for banked in ('','Banked'):
            name='kLeftQuarter'+banked+'HelixLargeUpSpriteMapArray'
            value=re.search(r'\b'+name+r'\s*\[[^=]*=\s*(.*?);',header,re.S)[1]
            array=evaluate(tokens(value),{})
            if len(array)!=4 or any(len(d)!=7 or any(len(s)!=2 for s in d) for d in array):
                raise Unsupported('helix sprite map shape')
            for right in (False,True):
                mask=0
                for d in range(4):
                    for s in range(7):
                        for i in range(2):
                            if array[d][s][i]: mask |= 1 << ((3-d if right else d)*14+s*2+i)
                self.helix_maps['k'+('Right' if right else 'Left')+'Quarter'+banked+'HelixLargeUpSpriteMap']=mask
        value=re.search(r'\bkLeftQuarterHelixLargeUpBoundingBoxes\s*=\s*(.*?);',source,re.S)[1]
        step=int(re.search(r'kCoordsZStep\s*=\s*(\d+)',(self.root/'src/openrct2/world/MapLimits.h').read_text())[1])
        array=evaluate(tokens(value),{'kCoordsZStep':step,'kBoundingBoxUnimplemented':[[0,0,0],[0,0,0]]})
        boxes=[]
        def flatten(value):
            if len(value)==2 and all(isinstance(v,list) and len(v)==3 and all(isinstance(n,int) for n in v) for v in value):
                boxes.append(value)
            else:
                for child in value: flatten(child)
        flatten(array)
        if len(boxes)!=56: raise Unsupported('helix bounding box shape')
        self.helix_bounds=[boxes,[[[v[1],v[0],v[2]] for v in boxes[(3-d)*14+s*2+i]] for d in range(4) for s in range(7) for i in range(2)]]
        ted=clean((self.root/'src/openrct2/ride/TrackData.cpp').read_text());self.helix_reverse={}
        for banked in ('','Banked'):
            for hand in ('Left','Right'):
                name=hand+'Quarter'+banked+'HelixLargeDown'
                seq=[]
                for i in range(7):
                    body=re.search(r'\bk'+name+'Seq'+str(i)+r'\s*=\s*\{(.*?)\n\s*\};',ted,re.S)[1]
                    seq.append(int(re.search(r'\.reversedTrackSequence\s*=\s*(\d+)',body)[1]))
                body=re.search(r'\bkTED'+name+r'\s*=\s*TrackElementDescriptor\{(.*?)\n\s*\};',ted,re.S)[1]
                rotation=int(re.search(r'\.reversedRotationOffset\s*=\s*(\d+)',body)[1])
                self.helix_reverse[name]=(seq,rotation)

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
        if result is None:
            candidates=[s for n,s in self.by_name.items() if n.endswith('::'+name) and s is not None]
            if len(candidates)==1: return candidates[0]
        if result is None: raise Unsupported('function ' + name)
        return result

    @staticmethod
    def local_name(source, name):
        if name in source.functions: return name
        if name.rsplit('::',1)[-1] in source.functions: return name.rsplit('::',1)[-1]
        return next(n for n in source.functions if n.endswith('::'+name))

    def getter(self, name, track_type, depth=0, state=0, dependencies=None):
        if depth > 8: raise Unsupported('getter cycle')
        source = self.find(name); node = source.functions[self.local_name(source,name)]
        env = ChainMap(dict(trackType=track_type,csgLoaded=bool(state&16),
                            _dependencies=dependencies if dependencies is not None else set()), source.globals,self.constants)
        result = self.execute(node, env, source, [], 0, getter=True)
        if not isinstance(result, tuple) or result[0] != 'return': raise Unsupported('getter without return')
        expression = result[1]
        invoked = call(expression)
        if invoked: return self.getter(invoked[0], evaluate(invoked[1][0], env), depth + 1,state,dependencies)
        function = ''.join(expression)
        if re.fullmatch(r'OpenRCT2::trackPaint(?:Left|Right)Quarter(?:Banked)?HelixLarge(?:Up|Down)<.*>',function):
            return source,function
        if not re.fullmatch(r'[\w:]+(?:<(?:true|false|JuniorRCSubType::(?:junior|waterCoaster))>)?', function): raise Unsupported('template getter')
        return self.find(function, source), function

    def execute(self, node, env, source, parts, depth, getter=False):
        kind = node[0]
        if not getter and auxiliary(node): return None
        if kind == 'block':
            for item in node[1]:
                flow = self.execute(item, env, source, parts, depth, getter)
                if flow: return flow
        elif kind == 'if':
            if (call(node[1]) and call(node[1])[0]=='TrackPaintUtilShouldPaintSupports'
                    and auxiliary_local_block(node[2]) and auxiliary_local_block(node[3])):
                return None
            negative=node[1][0]=='!'
            condition=node[1][1:] if negative else node[1]
            invoked=call(condition)
            if invoked and invoked[0] in STATION_CALLS:
                self.execute(('expr',condition),env,source,parts,depth)
                if auxiliary(node[2]) and auxiliary(node[3]): return None
                env['_dependencies'].add(64)
                branch=node[2] if env['hasPlatforms'] != negative else node[3]
                return self.execute(branch,env,source,parts,depth,getter) if branch else None
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
                if not getter and len(values) != 1:
                    invoked=call(values[1:])
                    if not invoked or len(invoked[1])!=7: raise Unsupported('value return in paint function')
                    self.find(invoked[0],source)
                    self.execute(('expr',values[1:]),env,source,parts,depth)
                return ('return', values[1:])
            if len(values)==3 and values[1:] in (['+','+'],['-','-']):
                env[values[0]]=evaluate([values[0]],env)+(1 if values[1]=='+' else -1);return None
            if len(values)>=3 and values[1] in ('+=','-='):
                old=evaluate([values[0]],env);rhs=evaluate(values[2:],env)
                env[values[0]]=old+rhs if values[1]=='+=' else old-rhs;return None
            if len(values)>=4 and values[1:3]==['&','=']:
                env[values[0]]=evaluate([values[0]],env)&evaluate(values[3:],env);return None
            if '=' in values:
                equal = values.index('='); lhs, rhs = values[:equal], values[equal + 1:]
                invoked=call(rhs)
                if invoked and invoked[0] in STATION_CALLS:
                    self.execute(('expr',rhs),env,source,parts,depth)
                    env['_dependencies'].add(64)
                    env[lhs[-1]]=env['hasPlatforms']
                    return None
                # This exact support bookkeeping assignment still executes its
                # RHS rail draw. Never treat an image-producing RHS as lazy data.
                if ''.join(lhs) == 'session.WoodenSupportsPrependTo':
                    invoked = call(rhs)
                    if not invoked or invoked[0] not in ('PaintAddImageAsParent','PaintAddImageAsParentRotated',
                                                         'TrackPaint<false>','TrackPaint<true>'):
                        raise Unsupported('support prepend pointer assignment')
                    self.execute(('expr',list(rhs)),env,source,parts,depth)
                    return None
            if declaration(values, env): return None
            invoked = call(values)
            if not invoked: raise Unsupported('statement ' + ' '.join(values[:12]))
            name, args = invoked
            if name in STATION_CALLS:
                # Preserve an authored station call in the immutable recipe.
                # GPU station rules consume current shared station/entrance facts.
                height=evaluate(args[5 if name=='TrackPaintUtilDrawPier' else 3],env)
                base=2; base_z=0; fence_a=5; fence_b=7; variant=0
                if name=='TrackPaintUtilDrawPier':
                    base=0;variant=5;fence_a=0;fence_b=2
                elif name=='TrackPaintUtilDrawNarrowStationPlatform':
                    variant=4;fence_a=evaluate(args[4],env);fence_b=fence_a+2
                    bases={'StationBaseType::none':0,'StationBaseType::a':1,'StationBaseType::b':2,'StationBaseType::c':3}
                    if len(args)>6: base=bases[''.join(args[6])]
                    if len(args)>7: base_z=evaluate(args[7],env)
                elif name=='TrackPaintUtilDrawStationInverted':
                    base=3; variant=1
                    if len(args)>5: variant=1+evaluate(args[5],env)
                else:
                    if len(args)>5:
                        bases={'StationBaseType::none':0,'StationBaseType::a':1,'StationBaseType::b':2,'StationBaseType::c':3}
                        base=bases[''.join(args[5])]
                    if len(args)>6: base_z=evaluate(args[6],env)
                    if len(args)>7: fence_a=evaluate(args[7],env)
                    if len(args)>8: fence_b=evaluate(args[8],env)
                parts.append((STATION_PART,0,0,height,base,base_z,fence_a,fence_b,variant,0,0,-1))
                return None
            if name in ('TrackPaintUtilOnridePhotoPlatformPaintBase','TrackPaintUtilOnridePhotoPlatformPaint'):
                height=evaluate(args[1 if name.endswith('Base') else 2],env)
                parts.append((22432,0,0,height,0,0,height,32,32,1,2,-1))
                return None
            curve_maps={'TrackPaintUtilRightQuarterTurn5TilesPaint':'right_quarter_turn_5_tiles_sprite_map',
                        'TrackPaintUtilRightQuarterTurn3TilesPaint':'kRightQuarterTurn3TilesSpriteMap',
                        'TrackPaintUtilRightHelixUpSmallQuarterTilesPaint':'right_helix_up_small_quarter_tiles_sprite_map',
                        'TrackPaintUtilRightHelixUpLargeQuarterTilesPaint':'right_helix_up_large_quarter_sprite_map'}
            if name in curve_maps:
                if len(args)!=10: raise Unsupported('legacy curve helper signature')
                thickness,height,direction,sequence=[evaluate(a,env) for a in args[1:5]]
                index=evaluate(tokens(curve_maps[name]+'['+str(sequence)+']'),env)
                if index<0: return None
                role=image_value(args[5],env).role
                def entry(arg,sprite,zero_tail=False):
                    try:
                        value=evaluate(arg,env)[direction][index]
                        if zero_tail and sprite==1 and len(value)==1: return 0
                        return value[sprite] if sprite is not None else value
                    except IndexError as e: raise Unsupported('legacy curve array index') from e
                for sprite in (range(2) if 'Helix' in name else (None,)):
                    image=entry(args[6],sprite,True)
                    if sprite is not None and image==0: continue
                    offset=[0,0] if tuple(args[7])==('nullptr',) else entry(args[7],sprite)
                    length=entry(args[8],sprite)
                    bounds=offset+[0] if tuple(args[9])==('nullptr',) else entry(args[9],sprite)
                    if name=='TrackPaintUtilRightQuarterTurn5TilesPaint': offset=[(v+128)%256-128 for v in offset]
                    parts.append(tuple([image]+offset+[height,bounds[0],bounds[1],height+bounds[2]]+length+
                                       [thickness[sprite] if sprite is not None else thickness,role,-1]))
                return None
            if name in ('TrackPaintUtilDiagTilesPaint','TrackPaintUtilDiagTilesPaintExtra'):
                thickness,height,direction,sequence=[evaluate(a,env) for a in args[1:5]]
                # kDiagSpriteMap: one owning direction per sequence.
                if sequence>=4 or direction!=(3,0,2,1)[sequence]: return None
                sprites=evaluate(args[5],env)
                extra=name.endswith('Extra')
                offset=[-16,-16] if extra else ([0,0] if tuple(args[6])==('nullptr',) else evaluate(args[6],env)[direction])
                length=[32,32] if extra else evaluate(args[7],env)[direction]
                z=0 if extra or len(args)<10 else evaluate(args[9],env)
                bound=list(offset)+[z] if extra or len(args)<9 or tuple(args[8])==('nullptr',) else evaluate(args[8],env)[direction]
                role=0 if extra or len(args)<11 else image_value(args[10],env).role
                parts.append(tuple([sprites[direction]]+list(offset)+[height]+[bound[0],bound[1],height+bound[2]]+list(length)+[thickness,role,-1]))
                return None
            if name in ('WoodenRCTrackPaintBb<false>','WoodenRCTrackPaintBb<true>'):
                value=evaluate(args[1][1:] if args[1][0]=='&' else args[1],env)
                height=evaluate(args[2],env)
                if len(value)!=4: raise Unsupported('wooden bounding box shape')
                first,second,offset,box=value
                if first==0: return None
                parent=len(parts)
                parts.append(tuple([first,offset[0],offset[1],height+offset[2],box[0][0],box[0][1],height+box[0][2]]+box[1]+[0 if '<true>' in name else 1,-1]))
                if second!=0:
                    parts.append(tuple([second,offset[0],offset[1],height+offset[2],box[0][0],box[0][1],height+box[0][2]]+box[1]+[0,parent]))
                return None
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
                           env['state'], parts, depth + 1,env['trackType'],env['_dependencies'])
        return None

    def paint(self, source, name, sequence, direction, height, state, parts, depth=0, track_type=0, dependencies=None):
        if depth > 16: raise Unsupported('call depth')
        env = ChainMap({}, source.globals, self.constants)
        env.update(trackSequence=sequence, direction=direction, height=height, state=state,
                   chain=bool(state & 1), inverted=bool(state & 2), brakeClosed=bool(state & 4),
                   cable=bool(state&8),csgLoaded=bool(state&16),greenLight=bool(state&32),hasPlatforms=bool(state&64),trackType=track_type,
                   _dependencies=dependencies if dependencies is not None else set())
        helix=re.fullmatch(r'OpenRCT2::trackPaint((?:Left|Right)Quarter(?:Banked)?HelixLarge(?:Up|Down))<(.*)>',name)
        if helix:
            if sequence>=7: return
            args=split_top(tokens(helix[2]))
            if len(args)!=7: raise Unsupported('quarter helix template signature')
            base=evaluate(args[0],env);map_name=''.join(args[1]).removeprefix('OpenRCT2::')
            sprite_map=self.helix_maps[map_name]
            right=helix[1].startswith('Right');down=helix[1].endswith('Down')
            if down:
                remap,rotation=self.helix_reverse[helix[1]];sequence=remap[sequence];direction=(direction+rotation)&3
            boxes=self.helix_bounds[int(right != down)]
            role=3 if evaluate(args[6],env) else 0
            for sprite in range(2 if 'Banked' in helix[1] else 1):
                index=direction*14+sequence*2+sprite
                if sprite_map&(1<<index):
                    image=base+bin(sprite_map&((1<<index)-1)).count('1');box=boxes[index]
                    parts.append(tuple([image,0,0,height,box[0][0],box[0][1],height+box[0][2]]+box[1]+[role,-1]))
            return
        self.execute(source.functions[self.local_name(source,name)], env, source, parts, depth)


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
                key = (getter, track_type)
                if key not in cache:
                    sampled={}; dependencies=set(); mask=0; errors=[]
                    while True:
                        for state in range(128):
                            if state & ~mask or state in sampled: continue
                            source,name=translator.getter(getter,track_type,state=state,dependencies=dependencies)
                            states=[]
                            for sequence in range(16):
                                directions=[]
                                for direction in range(4):
                                    result=[]
                                    try:
                                        translator.paint(source,name,sequence,direction,0,state,result,
                                                         track_type=track_type,dependencies=dependencies)
                                        markers=sum(p[0]==STATION_PART for p in result)
                                        if len(result)+markers*8>16 or sum(p[-1]==-1 for p in result)+markers*6>12:
                                            raise Unsupported('expanded station/component capacity')
                                        directions.append(tuple(result))
                                    except (Unsupported,ZeroDivisionError,RecursionError) as e:
                                        directions.append(None); errors.append(str(e))
                                states.append(directions)
                            sampled[state]=states
                        next_mask=sum(dependencies)
                        if next_mask==mask: break
                        mask=next_mask
                    variants=[sampled[state&mask] for state in range(128)]
                    maximum = max((seq for state in variants for seq, directions in enumerate(state) if any(directions)), default=-1)
                    if maximum < 0: cache[key] = Unsupported(errors[0] if errors else 'no rail graphics')
                    elif any(row is None for state in variants for directions in state[:maximum + 1] for row in directions):
                        cache[key] = Unsupported(errors[0])
                    else:
                        mask = next(mask for mask in range(128) if all(variants[state] == variants[state & mask] for state in range(128)))
                        cache[key] = (variants, maximum + 1, mask)
                value = cache[key]
                if isinstance(value, Unsupported): raise value
                variants, sequences, mask = value
                packed_rows = []
                for state in range(128):
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
                  omittedFamilies=['supports','tunnels','photo cameras','vehicles'],
                  scope='Static source translation; runtime row selection executes on GPU; no raster qualification')
    (args.output/'coverage.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('sourceSha256','styles')}))
    if not report['supportedStyleTypes']: raise Unsupported('no supported rules')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args())
