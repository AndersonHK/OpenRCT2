"""Translate a restricted, explicit subset of track source rules into immutable GPU data.

This authoring tool parses source text; it never loads the game, calls a painter,
or captures a draw stream. Unknown graphics expressions reject a style/type.
Supports and vehicles remain separate omitted families; tunnel requests and station markers are authored metadata.
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
PHOTO_PART = 0xfffffffc
TUNNEL_PART = 0xfffffffd
STATION_PART = 0xfffffffe
COVER_CALLS = ('TrackPaintUtilDrawStationCovers', 'TrackPaintUtilDrawStationCovers2')
CAPTURE_TUNNELS = False
AUTHORING_TICK_OVERRIDE = None
TUNNEL_CALL = re.compile(r"(?:PaintUtil(?:PushTunnel\w*|SetVerticalTunnel)|TrackPaintUtil(?:(?:Left|Right)QuarterTurn\w*Tunnel|DrawStationTunnel(?:Tall)?))$")
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
    text = re.sub(r'\b(TrackElemType|TrackStyle|JuniorRCSubType|BlockedSegments)\s*::\s*', '', text)
    text = re.sub(r'(MetalSupportType|MetalSupportPlace|WoodenSupportType|WoodenSupportSubType|WoodenSupportTransitionType)\s*::\s*', r'\1_', text)
    text=re.sub(r'PaintSegment\s*::\s*','PaintSegment_',text)
    for enum in ('TunnelSubType','TunnelGroup','TunnelType'):
        text=re.sub(enum+r'\s*::\s*',enum+'_',text)
    text = re.sub(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b', r'\1', text)
    text = re.sub(r'\b0([0-7]+)\b',lambda m:str(int(m[0],8)),text)
    text = text.replace('->','.')
    text = text.replace('&&', ' and ').replace('||', ' or ')
    text = re.sub(r'!(?!=)', ' not ', text).replace('/', '//').strip()
    try: return ast.parse(text, mode='eval').body
    except SyntaxError as e: raise Unsupported('expression ' + text) from e


def evaluate(values, env):
    # Strip only parentheses enclosing the complete expression. C++ conditional
    # expressions are otherwise hidden from the top-level ternary parser.
    while values and values[0]=='(' and values[-1]==')':
        depth=0;wrapped=True
        for i,value in enumerate(values):
            if value=='(': depth+=1
            elif value==')': depth-=1
            if depth==0 and i<len(values)-1: wrapped=False;break
        if not wrapped: break
        values=values[1:-1]
    if not values: raise Unsupported('empty expression')
    clock=re.fullmatch(r'\(getGameState\(\).currentTicks(/|>>)(\d+)\)([&%])(\d+)', ''.join(values))
    if clock:
        period=(1<<int(clock[2])) if clock[1]=='>>' and int(clock[2])<=3 else int(clock[2]) if clock[1]=='/' else 0
        if period not in (1,2,4,8): raise Unsupported('animated image tick period')
        frames=int(clock[4])+(clock[3]=='&')
        if frames not in (2,4,8,16): raise Unsupported('animated image frame count')
        if AUTHORING_TICK_OVERRIDE is not None:
            return (AUTHORING_TICK_OVERRIDE//period)%frames
        return AnimatedImageIndex(0,period.bit_length()-1,frames.bit_length()-1)

    if values[0]=='&': return evaluate(values[1:],env)
    if ''.join(values)=='stationObj!=nullptr&&stationObj->Flags.has(StationObjectFlag::noPlatforms)':
        env['_dependencies'].add(64);return not env['hasPlatforms']
    if ''.join(values)=='imageId.GetIndex()':
        value=env['imageId']
        return image_value(value.value,env).image if isinstance(value,Deferred) else value.image
    door=re.fullmatch(r'kDoor(FlatTo25Deg)?Opening(Inwards|Outwards)ToImage\[trackElement\.getDoor([AB])State\(\)\]', ''.join(values))
    if door:
        return 256 | int(door[3]=='B') | (int(door[2]=='Inwards')<<1) | (int(bool(door[1]))<<2)
    straight=re.fullmatch(r'GetTunnelDoorsImageStraightFlat\(trackElement,(.*)\)', ''.join(values))
    if straight: return 258 if evaluate(tokens(straight[1]),env) in (0,3) else 257
    junior=re.fullmatch(r'JuniorRCGetSubTypeOffset<JuniorRCSubType::(junior|waterCoaster)>\(trackElement\)', ''.join(values))
    if junior: return (1 if junior[1]=='junior' else 2) if evaluate(['chain'],env) else 0
    if len(values)>2 and values[0] in ('CoordsXY','CoordsXYZ','BoundBoxXYZ') and values[1]=='{':
        return evaluate(values[1:],env)
    constructor=call(values)
    if constructor and constructor[0]=='flipTrackSequenceBoundBoxesXAxis':
        if len(constructor[1])!=1: raise Unsupported('bound box mirror signature')
        boxes=evaluate(constructor[1][0],env)
        if len(boxes)!=4: raise Unsupported('bound box mirror views')
        def flip(box): return [[box[0][1],box[0][0],box[0][2]],[box[1][1],box[1][0],box[1][2]]]
        return [[[flip(box) for box in sequence] for sequence in view] for view in reversed(boxes)]
    if constructor and constructor[0] in ('BoundBoxXYZ','CoordsXY','CoordsXYZ'):
        name,args=constructor
        if name=='BoundBoxXYZ':
            if len(args)!=2: raise Unsupported('bounding box constructor')
            result=[evaluate(a,env) for a in args]
            return [v if v else [0,0,0] for v in result]
        if not args or args==[[]]: return [0]*(2 if name=='CoordsXY' else 3)
        result=[evaluate(a,env) for a in args]
        if name=='CoordsXYZ' and len(result)==2 and isinstance(result[0],list): return result[0]+[result[1]]
        return result
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
    # A conditional inside an array subscript is still scalar C++ syntax.
    # Evaluate only that subexpression; never reinterpret the surrounding call.
    stack=[]
    for i,value in enumerate(values):
        if value=='[': stack.append(i)
        elif value==']' and stack:
            begin=stack.pop()
            if '?' in values[begin+1:i]:
                index=evaluate(values[begin+1:i],env)
                if not isinstance(index,int): raise Unsupported('conditional array index')
                return evaluate(list(values[:begin+1])+[str(index)]+list(values[i:]),env)
    node = scalar_ast(tuple(values))

    def walk(n):
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, bool)): return n.value
        if isinstance(n, ast.Name):
            if n.id in ('true', 'false'): return n.id == 'true'
            if n.id not in env: raise Unsupported('identifier ' + n.id)
            if n.id in STATE_BITS and '_dependencies' in env: env['_dependencies'].add(STATE_BITS[n.id])
            value = env[n.id]
            if isinstance(value, Deferred): value=value.resolve(env)
            if value is None: raise Unsupported('uninitialised local '+n.id)
            return value
        if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='HasSecondary' and not n.args:
            value=walk(n.func.value)
            if isinstance(value,ImageValue): return value.role in (0,1,3)
        if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='GetIndex' and not n.args:
            value=walk(n.func.value)
            if isinstance(value,ImageValue): return value.image
        if isinstance(n, ast.Subscript):
            try:
                array,index=walk(n.value),walk(n.slice)
                if isinstance(index,AnimatedImageIndex):
                    frames=1<<index.frame_bits
                    if (index.base!=0 or not isinstance(array,list) or len(array)!=frames
                        or not all(isinstance(x,int) for x in array)
                        or any(x!=array[0]+i for i,x in enumerate(array))):
                        raise Unsupported('nonlinear animated image table')
                    return AnimatedImageIndex(array[0],index.shift,index.frame_bits)
                # C++ indices never wrap to the end as Python negative indices do.
                if not isinstance(index,int) or index<0: raise IndexError(index)
                return array[index]
            except (IndexError, TypeError) as e: raise Unsupported('array index') from e
        if isinstance(n,ast.Attribute):
            value=walk(n.value)
            if isinstance(value,dict) and n.attr in value:
                result=value[n.attr]
                if result is None: raise Unsupported('uninitialised member '+n.attr)
                return result
            if n.attr in ('offset','length') and isinstance(value,list) and len(value)==2:
                return value[0 if n.attr=='offset' else 1]
            if n.attr in ('sprite_id','offset','bb_offset','bb_size') and isinstance(value,list) and len(value)==4:
                return value[('sprite_id','offset','bb_offset','bb_size').index(n.attr)]
            if n.attr in ('track','handrail','frontTrack','frontHandrail') and isinstance(value,list):
                index=('track','handrail','frontTrack','frontHandrail').index(n.attr)
                return FrontTrackImage(value[index]) if index>=2 and index<len(value) else value[index] if index<len(value) else 0xffffffff
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


class ArrayDeferred(Deferred):
    def __init__(self,value,dimensions,box=False,record=False):
        super().__init__(value);self.dimensions=dimensions;self.box=box;self.record=record
    def resolve(self,env):
        dimensions=[evaluate(tokens(d),env) for d in self.dimensions]
        def zero(depth):
            if depth==len(dimensions):
                if self.record: return [0,0,[0,0,0],[[0,0,0],[0,0,0]]]
                return [[0,0,0],[0,0,0]] if self.box else 0
            return [zero(depth+1) for _ in range(dimensions[depth])]
        def normalise(value,depth):
            if depth==len(dimensions): return zero(depth) if (self.record or self.box) and not value else value
            def box_shape(v):
                return (isinstance(v,list) and len(v)==2 and all(isinstance(n,list) and len(n)==3
                    and all(isinstance(x,int) for x in n) for n in v))
            size=dimensions[depth]
            while isinstance(value,list) and len(value)==1 and isinstance(value[0],list) and size!=1:
                # One initialized BoundBox is an array element, not an extra
                # std::array brace wrapper; retain zero-initialized siblings.
                if self.box and depth==len(dimensions)-1 and box_shape(value[0]): break
                value=value[0]
            if not isinstance(value,list) or len(value)>size: raise Unsupported('std::array initializer shape')
            value=value+[zero(depth+1) for _ in range(size-len(value))]
            return [normalise(child,depth+1) for child in value]
        value=evaluate(self.value,env)
        if isinstance(value,list) and all(isinstance(v,int) for v in value) and len(dimensions)>1:
            def reshape(depth,start):
                if depth==len(dimensions): return value[start],start+1
                output=[]
                for _ in range(dimensions[depth]):
                    child,start=reshape(depth+1,start);output.append(child)
                return output,start
            size=1
            for dimension in dimensions: size*=dimension
            if len(value)!=size: raise Unsupported('flat std::array initializer shape')
            return reshape(0,0)[0]
        return normalise(value,0)


class SpriteBbCArrayDeferred(Deferred):
    """Normalize C aggregate brace elision without inventing sprite geometry."""
    def __init__(self,value,dimensions):
        super().__init__(value);self.dimensions=dimensions
    def resolve(self,env):
        value=evaluate(self.value,env)
        dimensions=[evaluate(tokens(d),env) for d in self.dimensions]
        def row(items,depth):
            if depth==len(dimensions)-1:
                # SpriteBb has four authored fields: image, offset, bbOffset, size.
                if items and isinstance(items[0],int):
                    if len(items)%4: raise Unsupported('SpriteBb elided initializer fields')
                    items=[items[i:i+4] for i in range(0,len(items),4)]
                if len(items)!=dimensions[depth]: raise Unsupported('SpriteBb row capacity')
                for item in items:
                    if len(item)!=4 or not isinstance(item[0],int) or any(
                        not isinstance(v,list) or len(v)!=3 or not all(isinstance(x,int) for x in v)
                        for v in item[1:]): raise Unsupported('SpriteBb initializer shape')
                return items
            if len(items)!=dimensions[depth]: raise Unsupported('SpriteBb array capacity')
            return [row(child,depth+1) for child in items]
        return row(value,0)


class NumericCArrayDeferred(Deferred):
    """Explicitly braced numeric C arrays retain declared zero-initialized tails."""
    def __init__(self,value,dimensions):
        super().__init__(value);self.dimensions=dimensions
    def resolve(self,env):
        value=evaluate(self.value,env)
        dimensions=[evaluate(tokens(d),env) if d else None for d in self.dimensions]
        if dimensions[0] is None: dimensions[0]=len(value)
        if any(d is None or d<1 for d in dimensions): raise Unsupported('numeric C array dimensions')
        def fill(items,depth):
            if depth==len(dimensions):
                if items==[]: return 0
                if not isinstance(items,int): raise Unsupported('numeric C array scalar initializer')
                return items
            if not isinstance(items,list) or len(items)>dimensions[depth]:
                raise Unsupported('numeric C array explicit initializer shape')
            return [fill(items[i] if i<len(items) else [],depth+1) for i in range(dimensions[depth])]
        return fill(value,0)


class StationFenceValue:
    def __init__(self,edge): self.edge=edge
    def __bool__(self): raise Unsupported('station fence controls ordinary geometry')


def station_edge(values):
    edges={'EDGE_NE':0,'EDGE_SE':1,'EDGE_SW':2,'EDGE_NW':3}
    text=''.join(values)
    if text not in edges: raise Unsupported('station cover edge')
    return edges[text]


class PaintHandle:
    def __init__(self,index): self.index=index


class FunctionValue:
    def __init__(self,source,name): self.source,self.name=source,name


class AnimatedImageIndex:
    """Restricted base + unsigned tick-frame expression, never dynamic geometry."""
    def __init__(self,base,shift,frame_bits):
        self.base,self.shift,self.frame_bits=base,shift,frame_bits
    def __add__(self,value):
        if not isinstance(value,int): raise Unsupported('nonlinear animated image expression')
        return AnimatedImageIndex(self.base+value,self.shift,self.frame_bits)
    __radd__=__add__
    def __bool__(self): raise Unsupported('animated branch condition')
    def encode(self):
        if not 0<=self.base or self.base+(1<<self.frame_bits)>0x7ffff:
            raise Unsupported('animated image range')
        return 0x80000000 | (self.shift<<19) | (self.frame_bits<<22) | self.base


class FrontTrackImage(int):
    """Image provenance from an authored frontTrack/frontHandrail field."""
    pass


class ImageValue:
    def __init__(self, image, role): self.image, self.role = image, role


def image_value(values, env):
    while values and values[0]=='(' and values[-1]==')':
        depth=0;wrapped=True
        for i,value in enumerate(values):
            if value=='(': depth+=1
            elif value==')': depth-=1
            if depth==0 and i<len(values)-1: wrapped=False;break
        if not wrapped: break
        values=values[1:-1]
    text = ''.join(values)
    if text=='ImageId(SPR_WATER_MASK).WithRemap(FilterPaletteID::paletteWater).WithBlended(true)':
        return ImageValue(0,4)  # Shared water mask/filter; never an atlas image0.
    if text=='ImageId(transparent?EnumValue(SPR_WATER_OVERLAY):EnumValue(SPR_G2_OPAQUE_WATER_OVERLAY))':
        return ImageValue(0,5)  # GPU selects current transparency and viewport mode.
    depth=0;question=None;nested=0
    for i,value in enumerate(values):
        if value in ('(','[','{'): depth+=1
        elif value in (')',']','}'): depth-=1
        elif depth==0 and value=='?':
            if question is None: question=i
            else: nested+=1
        elif depth==0 and value==':' and question is not None:
            if nested: nested-=1
            else: return image_value(values[question+1:i] if evaluate(values[:question],env) else values[i+1:],env)
    if re.fullmatch(r'ImageId\(\d+\)',text): return ImageValue(int(text[8:-1]),0)
    if text=='ImageId(SPR_STATION_BASE_BORDERLESS,OpenRCT2::Drawing::Colour::black)':
        return ImageValue(evaluate(['SPR_STATION_BASE_BORDERLESS'],env),2)
    if text == 'session.TrackColours': return ImageValue(0, 0)
    if text == 'session.SupportColours': return ImageValue(0, 1)
    if text == 'WoodenRCGetRailsColour(session)': return ImageValue(0, 0)
    if text == 'GetStationColourScheme(session,trackElement)': return ImageValue(0, 2)
    if text == 'GetTrackColour(session)': return ImageValue(0, 3)
    if text in ('WoodenRCGetTrackColour<false>(session)','WoodenRCGetTrackColour<true>(session)'):
        return ImageValue(0, 0 if '<true>' in text else 1)
    secondary=re.fullmatch(r'(.+)\.WithSecondary\((.+)\.GetSecondary\(\)\)',text)
    if secondary:
        base=image_value(tokens(secondary[1]),env);other=image_value(tokens(secondary[2]),env)
        if base.role==1 and other.role==0: return ImageValue(base.image,1)
        raise Unsupported('unrepresented secondary colour role')
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
    if values and values[0] in ('ImageId','CoordsXY','CoordsXYZ','bool','int8_t','uint8_t','int16_t','int32_t','uint16_t','uint32_t') and '=' not in values:
        names=split_top(values[1:])
        if all(len(n)==1 and re.fullmatch(r'[A-Za-z_]\w*',n[0]) for n in names):
            for name in names: env[name[0]]=None
            return True
    if len(values)==3 and values[:2]==['PaintStruct','*']:
        env[values[2]]=None;return True
    if len(values)==2 and values[0]=='BoundBoxXY':
        env[values[1]]={'offset':None,'length':None};return True
    if values and values[0]=='TunnelType' and '=' not in values:
        names=split_top(values[1:])
        if all(len(n)==1 for n in names):
            for name in names: env[name[0]]=None
            return True
    if len(values) == 2 and values[0] in ('ImageId','CoordsXY','CoordsXYZ','TunnelSubType','int16_t','int32_t','uint16_t','uint32_t'):
        env[values[1]] = None  # Reading before a branch assigns it is rejected.
        return True
    if '=' not in values: return False
    index = values.index('='); lhs, rhs = values[:index], values[index + 1:]
    if any(v.startswith('PaintAddImage') or v == 'TrackPaint' for v in rhs):
        raise Unsupported('draw-valued declaration requires explicit ownership')
    if 'SpriteBoundBox2' in lhs and '[' in lhs:
        dimensions=re.findall(r'\[([^\]]+)\]',''.join(lhs))
        env[lhs[lhs.index('[')-1]]=ArrayDeferred(rhs,dimensions,record=True);return True
    pointer=re.fullmatch(r'(?:const)?(?:u?int\d+_t|ImageIndex)\(\*(\w+)\)(?:\[\d+\])+',''.join(lhs))
    if pointer:
        env[pointer[1]]=evaluate(rhs,env);return True
    # First bracket belongs to the declared array, never to its initializer.
    if '[' in lhs: lhs = lhs[:lhs.index('[')]
    if len(lhs)==3 and lhs[1]=='.' and lhs[2] in ('offset','length'):
        value=env.get(lhs[0])
        if isinstance(value,Deferred): value=value.resolve(env)
        if isinstance(value,list) and len(value)==2 and all(isinstance(v,list) and len(v)==3 for v in value):
            replacement=evaluate(rhs,env)
            if not isinstance(replacement,list) or len(replacement)!=3: raise Unsupported('bounding member shape')
            value=[list(v) for v in value];value[0 if lhs[2]=='offset' else 1]=replacement
            env[lhs[0]]=value;return True
    if len(lhs)==3 and lhs[1]=='.' and isinstance(env.get(lhs[0]),dict):
        if lhs[2] not in env[lhs[0]]: raise Unsupported('unknown member assignment')
        env[lhs[0]][lhs[2]]=evaluate(rhs,env);return True
    if not lhs or not re.fullmatch(r'[A-Za-z_]\w*', lhs[-1]): return False
    name = lhs[-1]
    if ''.join(rhs)=='trackElement.getTrackType()':
        env[name]=env['trackType'];return True
    if any(token in rhs for token in ('TrackColours', 'SupportColours', 'WithIndex', 'WithIndexOffset', 'WithSecondary',
                                    'WoodenRCGetTrackColour','WoodenRCGetRailsColour','GetTrackColour',
                                    'GetStationColourScheme','ImageId')) or (
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
                 r'WoodenSupportsPrependTo|ChairliftPaintUtilDrawSupports|DrawSupportForSequence[AB]<[^>]+>|'
                 r'PaintUtil(?:SetSegmentSupportHeight|SetGeneralSupportHeight|SetVerticalTunnel|PushTunnel\w*)|'
                 r'TrackPaintUtil(?:DrawStation\w*|DrawSupports\w*|'
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
    key = (id(node), CAPTURE_TUNNELS)
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
    if invoked and (invoked[0] in STATION_CALLS+COVER_CALLS or (CAPTURE_TUNNELS and TUNNEL_CALL.fullmatch(invoked[0]))): return False
    return not values or bool(invoked and AUX.fullmatch(invoked[0]))


def auxiliary_local_block(node):
    """A support-only lexical block may also declare its own unused scalar locals.

    Never discard assignments to outer variables, control flow, or graphics calls.
    """
    if node is None: return True
    if node[0]=='switch':
        # Break is local to this discarded switch. Returns and writes to outer
        # variables are still rejected; no later rail control flow is removed.
        return all(auxiliary_local_block(('block',[n for n in body if n != ('expr',['break'])]))
                   for _,body in node[2])
    if node[0]=='if': return auxiliary_local_block(node[2]) and auxiliary_local_block(node[3])
    if node[0] != 'block': return auxiliary(node)
    for item in node[1]:
        if auxiliary(item): continue
        if item[0] in ('switch','if') and auxiliary_local_block(item): continue
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
        self.errors = {}; self.templates = {}; self.parameters = {}
        pattern = r'\b(?:void|TrackPaintFunction)\s+([\w:]+)\s*\(([^{};]*)\)\s*\{'
        for match in re.finditer(pattern, self.text):
            start = match.end() - 1; end = start + 1; depth = 1
            while depth and end < len(self.text):
                if self.text[end] == '{': depth += 1
                elif self.text[end] == '}': depth -= 1
                end += 1
            values = tokens(self.text[start:end])
            parameters=[]
            for parameter in split_top(tokens(match[2])):
                array=parameter.index('[') if '[' in parameter and parameter[0]!='[' else len(parameter)
                names=[x for x in parameter[:array] if re.fullmatch(r'[A-Za-z_]\w*',x)]
                parameters.append(names[-1] if names else '')
            header=re.search(r'template\s*<(.*?)>\s*(?:static\s+)?(?:inline\s+)?$',self.text[max(0,match.start()-500):match.start()],re.S)
            if header and ',' in header[1]:
                # Non-type immutable parameters only; reject arbitrary C++ templates.
                params=re.findall(r'(?:bool|(?:std::array<.*>))\s+(\w+)\s*(?:,|$)',header[1])
                if len(params)==2 and params[0]=='isClassic':
                    self.templates[match[1]]=(params,values,parameters)
                image_params=re.findall(r'ImageIndex\s+(\w+)',header[1])
                if len(image_params)==4:
                    self.templates[match[1]]=(image_params,values,parameters)
            template = re.search(r'template\s*<\s*bool\s+(\w+)\s*>\s*(?:static\s+)?$', self.text[max(0,match.start()-100):match.start()])
            variants = ((match[1]+'<'+v+'>',[v if t==template[1] else t for t in values]) for v in ('false','true')) if template else [(match[1],values)]
            junior=re.search(r'template\s*<\s*JuniorRCSubType\s+(\w+)\s*>\s*(?:static\s+)?$',self.text[max(0,match.start()-100):match.start()])
            if junior:
                variants=[(match[1]+'<JuniorRCSubType::'+v+'>',tokens(' '.join(values).replace(junior[1],'JuniorRCSubType::'+v)))
                          for v in ('junior','waterCoaster')]
            for name, body in variants:
                try:
                    self.functions[name] = Parser(body).statement();self.parameters[name]=parameters
                except Unsupported as e: self.errors[name] = str(e)
        # Numeric constexpr tables (including sequence remaps). Other C++
        # declarations remain unavailable and reject the affected graphics rule.
        for match in re.finditer(r'(?:static\s+)?(?:constexpr|const)\s+(?:u?int\d+_t|ImageIndex|CoordsXY|CoordsXYZ|BoundBoxXY|BoundBoxXYZ|SpriteBb|SpriteBoundBox2|TunnelGroup|WoodenSupportSubType|WoodenSupportTransitionType|auto)\s+(\w+)\s*(?:\[[^;=]*\])?\s*=\s*([^;]+);', self.text):
            self.globals[match[1]] = Deferred(tokens(match[2]))
        for match in re.finditer(r'(?:static\s+)?(?:constexpr|const)\s+SpriteBb\s+(\w+)\s*((?:\[[^\]]+\]\s*)+)\s*=\s*([^;]+);',self.text):
            self.globals[match[1]]=SpriteBbCArrayDeferred(tokens(match[3]),re.findall(r'\[([^\]]+)\]',match[2]))
        for match in re.finditer(r'(?:static\s+)?(?:constexpr|const)\s+(?:u?int\d+_t|ImageIndex)\s+(\w+)\s*((?:\[[^\]]*\]\s*)+)\s*=\s*([^;]+);',self.text):
            dimensions=re.findall(r'\[([^\]]*)\]',match[2])
            self.globals[match[1]]=NumericCArrayDeferred(tokens(match[3]),dimensions)
        for match in re.finditer(r'(?:static\s+)?constexpr\s+(std::array<[^;=]+>)\s+(\w+)\s*=\s*([^;]+);',self.text):
            dimensions=re.findall(r',\s*([A-Za-z_]\w*|\d+)\s*>',match[1])[::-1]
            if dimensions: self.globals[match[2]]=ArrayDeferred(tokens(match[3]),dimensions,'BoundBoxXYZ' in match[1])
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
        self.constants['kImageIndexUndefined']=0xffffffff
        self.hashes['src/openrct2/drawing/ImageIndexType.h']=hashlib.sha256((root/'src/openrct2/drawing/ImageIndexType.h').read_bytes()).hexdigest()
        segment_text=clean((root/extra[-1]).read_text())
        segment_body=re.search(r'enum class PaintSegment[^\{]*\{(.*?)\}',segment_text,re.S)[1]
        for name,value in re.findall(r'(\w+)\s*=\s*(\d+)',segment_body): self.constants['PaintSegment_'+name]=int(value)
        self.constants.update(Source(extra[-1],segment_text).globals)
        tunnel_path='src/openrct2/paint/tile_element/Paint.Tunnel.h'
        self.hashes[tunnel_path]=hashlib.sha256((root/tunnel_path).read_bytes()).hexdigest()
        for enum in ('TunnelSubType','TunnelGroup','TunnelType'):
            tunnel_body=re.search(r'enum class '+enum+r'[^\{]*\{(.*?)\}',clean((root/tunnel_path).read_text()),re.S)[1]
            for name,value in re.findall(r'(\w+)\s*=\s*(\d+)',tunnel_body): self.constants[enum+'_'+name]=int(value)
        self.hashes['src/openrct2/paint/tile_element/Paint.Tunnel.cpp']=hashlib.sha256((root/'src/openrct2/paint/tile_element/Paint.Tunnel.cpp').read_bytes()).hexdigest()
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
        self.shared_sources=shared_sources
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
        template=re.fullmatch(r'([\w:]+)<((?:[A-Za-z_]\w*|true|false)(?:,(?:[A-Za-z_]\w*|true|false))+)>' ,name)
        if template and source:
            candidates=[s for s in self.sources if template[1] in s.templates]
            candidates += [s for s in getattr(self,'shared_sources',[]) if template[1] in s.templates]
            if len(candidates)==1:
                original=candidates[0];params,body,parameters=original.templates[template[1]]
                specialised=Source(original.path,'')
                arguments=template[2].split(',')
                if len(arguments)!=len(params): raise Unsupported('template argument count')
                replacements=dict(zip(params,arguments))
                specialised.functions[name]=Parser([replacements.get(v,v) for v in body]).statement()
                specialised.globals=dict(original.globals);specialised.globals.update(source.globals)
                source.functions[name]=specialised.functions[name];source.parameters[name]=parameters
                source.globals.update({k:v for k,v in original.globals.items() if k not in source.globals})
                return source
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
        if name=='getTrackPaintFunctionMultiDimensionRCInverted':
            source=self.find(name)
            entries=re.findall(r'fns\[EnumValue\((\w+)\)\]\s*=\s*(\w+)\s*;',source.text)
            if not entries: raise Unsupported('missing inverted function table')
            for type_name,function in entries:
                if self.constants[type_name]==track_type: return source,function
            return self.find('TrackPaintFunctionDummy'), 'TrackPaintFunctionDummy'
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
        if not re.fullmatch(r'[\w:]+(?:<(?:true|false|JuniorRCSubType::(?:junior|waterCoaster))(?:,[A-Za-z_]\w*)?>)?', function): raise Unsupported('template getter')
        return self.find(function, source), function

    def execute(self, node, env, source, parts, depth, getter=False):
        kind = node[0]
        if not getter and not getattr(self, 'capture_supports', False) and auxiliary(node): return None
        if kind == 'block':
            for item in node[1]:
                flow = self.execute(item, env, source, parts, depth, getter)
                if flow: return flow
        elif kind == 'if':
            if (env.get('_functionName')=='MultiDimensionRCTrackStation' and ''.join(node[1])==
                    'stationObj!=nullptr&&!stationObj->Flags.has(StationObjectFlag::noPlatforms)'):
                # This source block authors only the two station covers, with
                # fence-dependent variants. Keep their selection on the GPU.
                def cover_only(n):
                    if n is None: return True
                    if n[0]=='block': return all(cover_only(c) for c in n[1])
                    if n[0]=='if': return cover_only(n[2]) and cover_only(n[3])
                    if n[0]!='expr': return False
                    v=n[1];rhs=v[v.index('=')+1:] if '=' in v else v
                    invoked=call(rhs)
                    return (v==['bool','hasFence'] or bool(invoked and invoked[0] in
                        ('DrawSupportsSideBySide','GetStationColourScheme','TrackPaintUtilHasFence','TrackPaintUtilDrawStationCovers')))
                if not cover_only(node[2]) or not auxiliary_local_block(node[3]):
                    raise Unsupported('multidimension station cover block changed')
                env['_dependencies'].add(64)
                if env['hasPlatforms']:
                    parts.append((STATION_PART,0,0,env['height'],0,0,0,0,6,0,0,-1))
                return None
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
                if invoked and invoked[0]=='TrackPaintUtilHasFence':
                    args=invoked[1]
                    if len(args)!=5 or [''.join(a) for a in args[1:]]!=[
                            'session.MapPosition','trackElement','ride','session.CurrentRotation']:
                        raise Unsupported('station fence owner')
                    env[lhs[-1]]=StationFenceValue(station_edge(args[0]));return None
                if invoked and invoked[0].startswith('GetTrackPaintFunction') and len(invoked[1])==1:
                    target,function=self.getter(invoked[0],evaluate(invoked[1][0],env),state=env['state'],dependencies=env['_dependencies'])
                    env[lhs[-1]]=FunctionValue(target,function);return None
                if invoked and invoked[0] in ('PaintAddImageAsParent','PaintAddImageAsParentRotated') and ''.join(lhs)!='session.WoodenSupportsPrependTo':
                    if not (len(lhs)==1 or '*' in lhs): raise Unsupported('draw-valued declaration must be a local handle')
                    start=len(parts);self.execute(('expr',list(rhs)),env,source,parts,depth)
                    env[lhs[-1]]=PaintHandle(start) if len(parts)>start else None
                    return None
                if invoked and invoked[0] in STATION_CALLS:
                    self.execute(('expr',rhs),env,source,parts,depth)
                    env['_dependencies'].add(64)
                    env[lhs[-1]]=env['hasPlatforms']
                    return None
                # This exact support bookkeeping assignment still executes its
                # RHS rail draw. Never treat an image-producing RHS as lazy data.
                if ''.join(lhs) == 'session.WoodenSupportsPrependTo':
                    if len(rhs)==1 and (isinstance(env.get(rhs[0]),PaintHandle) or (rhs[0] in env and env[rhs[0]] is None)):
                        return None  # Only omitted wooden support ordering consumes this handle.
                    invoked = call(rhs)
                    if not invoked or invoked[0] not in ('PaintAddImageAsParent','PaintAddImageAsParentRotated','PaintAddImageAsParentHeight',
                                                         'TrackPaint<false>','TrackPaint<true>'):
                        raise Unsupported('support prepend pointer assignment')
                    self.execute(('expr',list(rhs)),env,source,parts,depth)
                    return None
            if declaration(values, env): return None
            invoked = call(values)
            if not invoked: raise Unsupported('statement ' + ' '.join(values[:12]))
            name, args = invoked
            if isinstance(env.get(name),FunctionValue):
                target=env[name]
                if len(args)!=7: raise Unsupported('function alias signature')
                self.paint(target.source,target.name,evaluate(args[2],env),evaluate(args[3],env),evaluate(args[4],env),
                           env['state'],parts,depth+1,env['trackType'],env['_dependencies'])
                return None
            if CAPTURE_TUNNELS and TUNNEL_CALL.fullmatch(name):
                self.tunnel(name,args,env,parts)
                return None
            if name in COVER_CALLS:
                if len(args)!=(6 if name==COVER_CALLS[0] else 7): raise Unsupported('station cover signature')
                edge=station_edge(args[1]);fence=evaluate(args[2],env)
                if not isinstance(fence,StationFenceValue) or fence.edge!=edge:
                    raise Unsupported('station cover fence ownership')
                station=env.get(''.join(args[3]))
                if not isinstance(station,Deferred) or ''.join(station.value)!='ride.getStationObject()':
                    raise Unsupported('station cover object ownership')
                if image_value(args[-1],env).role!=2: raise Unsupported('station cover colour ownership')
                variant=0 if name==COVER_CALLS[0] else evaluate(args[5],env)
                if variant not in (0,1,2): raise Unsupported('station cover variant')
                height=evaluate(args[4],env)
                # One ordered opaque parent plus optional glass child. Variant7
                # is a single cover, never a synthesized entire station.
                parts.append((STATION_PART,edge,variant,height,0,0,0,0,7,0,0,-1))
                return None
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
            if name in ('TrackPaintUtilOnridePhotoPaint','TrackPaintUtilOnridePhotoSmallPaint','TrackPaintUtilOnridePhotoPaint2'):
                direction=evaluate(args[1],env)
                if name.endswith('Paint2'):
                    # The source overload has trackElement before height. The old
                    # undeclared-definition overload is not silently guessed.
                    if ''.join(args[2])!='trackElement': raise Unsupported('photo helper signature')
                    base=evaluate(args[3],env)
                    height=base+(evaluate(args[5],env) if len(args)>5 else 3)
                else:
                    height=evaluate(args[2],env)
                if not 0<=direction<4: raise Unsupported('photo direction')
                parts.append((PHOTO_PART,direction,int(name.endswith('SmallPaint')),height,0,0,0,0,0,0,2,-1))
                if name.endswith('Paint2') and CAPTURE_TUNNELS:
                    parts.append((TUNNEL_PART,direction&1,6,base,0,0,0,0,0,0,0,-1))
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
            if name in ('TrackPaintUtilRightQuarterTurn3TilesPaint3','TrackPaintUtilRightQuarterTurn3TilesPaint4'):
                if len(args)!=6: raise Unsupported('SpriteBb curve signature')
                height,direction,sequence=[evaluate(a,env) for a in args[1:4]]
                index=evaluate(tokens('kRightQuarterTurn3TilesSpriteMap['+str(sequence)+']'),env)
                if index<0: return None
                image,offset,bound,size=evaluate(args[5],env)[direction][index]
                offset=offset[:2]+[offset[2]+height]
                bound=offset if name.endswith('4') else bound[:2]+[bound[2]+height]
                parts.append(tuple([image]+offset+bound+size+[image_value(args[4],env).role,-1]))
                return None
            if name=='TrackPaintUtilEighthToDiagTilesPaint':
                if len(args)!=10: raise Unsupported('eighth curve signature')
                height,direction,sequence=[evaluate(a,env) for a in args[2:5]]
                index=evaluate(tokens('eighth_to_diag_sprite_map['+str(sequence)+']'),env)
                if index<0: return None
                thickness=evaluate(args[1],env)[direction][index]
                image=evaluate(args[6],env)[direction][index]
                offset=[0,0] if tuple(args[7])==('nullptr',) else evaluate(args[7],env)[direction][index]
                length=evaluate(args[8],env)[direction][index]
                bound=offset+[0] if tuple(args[9])==('nullptr',) else evaluate(args[9],env)[direction][index]
                role=image_value(args[5],env).role
                parts.append(tuple([image]+offset+[height]+bound[:2]+[height+bound[2]]+length+[thickness,role,-1]))
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
            if name in ('PaintAddImageAsParent', 'PaintAddImageAsChild', 'PaintAddImageAsParentRotated', 'PaintAddImageAsChildRotated', 'PaintAddImageAsParentHeight'):
                extra_height=evaluate(args[2],env) if name.endswith('Height') else 0
                if name.endswith('Height'): args=args[:2]+args[3:]
                rotated = name.endswith('Rotated'); shift = int(rotated)
                direction = evaluate(args[1], env) if rotated else 0
                selected = image_value(args[1 + shift], env)
                image, role = selected.image, selected.role
                offset = list(evaluate(args[2 + shift], env)); box = evaluate(args[3 + shift], env)
                if len(box) == 3 and all(isinstance(v, int) for v in box): box = [list(offset), box]
                if (len(offset)!=3 or not all(isinstance(v,int) for v in offset) or len(box)!=2
                    or any(not isinstance(v,list) or len(v)!=3 or not all(isinstance(x,int) for x in v) for v in box)):
                    raise Unsupported('bounds shape or animated geometry')
                box = [list(v) for v in box]
                offset[2]+=extra_height;box[0][2]+=extra_height
                if direction & 1:
                    offset[0], offset[1] = offset[1], offset[0]
                    for v in box: v[0], v[1] = v[1], v[0]
                if isinstance(image,AnimatedImageIndex): image=image.encode()
                elif image==0xffffffff: return None
                elif not 0 <= image < 0x7ffff: raise Unsupported('image range')
                parent = -1
                if 'Child' in name:
                    parent = next((i for i in range(len(parts)-1, -1, -1) if parts[i][-1] == -1 and parts[i][0]!=TUNNEL_PART), -1)
                    if parent < 0: raise Unsupported('child without parent')
                parts.append(tuple([image] + offset + box[0] + box[1] + [role, parent]))
                if len(parts) > 64: raise Unsupported('component capacity')
            else:
                target = self.find(name, source)
                generic=(name in ('TrackPaintUtilSpinningTunnelPaint','CompactInvertedRCTrackDiagFlatBase','InvertedRCTrackDiagFlatBase',
                    'TrackPaintUtilLeftQuarterTurn3TilesPaintWithHeightOffset',
                    'TrackPaintUtilRightQuarterTurn3TilesPaint2WithHeightOffset','TrackPaintUtilRightQuarterTurn3TilesPaint2',
                    'TrackPaintUtilLeftQuarterTurn3TilesPaint','TrackPaintUtilLeftQuarterTurn1TilePaint',
                    'TrackPaintUtilRightQuarterTurn5TilesPaint2','TrackPaintUtilRightQuarterTurn5TilesPaint3',
                    'PaintRiverRapidsTrack25Deg','PaintRiverRapidsTrack25DegToFlatA','PaintRiverRapidsTrack25DegToFlatB') or name.startswith('classicStandUpRCTrackDiag'))
                if generic:
                    parameters=target.parameters[self.local_name(target,name)]
                    if len(parameters)!=len(args): raise Unsupported('named helper signature '+name)
                    local=ChainMap(dict(env.maps[0]),target.globals,self.constants)
                    for parameter,arg in zip(parameters,args):
                        if parameter in ('session','ride','trackElement','supportType'): continue
                        local[parameter]=image_value(arg,env) if parameter=='colourFlags' else evaluate(arg,env)
                    self.execute(target.functions[self.local_name(target,name)],local,target,parts,depth+1)
                    return None
                if name.startswith('TrackStraightBankTrack<') and len(args)==3:
                    self.paint(target,name,env['trackSequence'],evaluate(args[1],env),evaluate(args[2],env),
                               env['state'],parts,depth+1,env['trackType'],env['_dependencies'])
                    return None
                if len(args) != 7: raise Unsupported('helper signature ' + name)
                self.paint(target, name, evaluate(args[2], env), evaluate(args[3], env), evaluate(args[4], env),
                           env['state'], parts, depth + 1,env['trackType'],env['_dependencies'])
        return None

    def tunnel(self,name,args,env,parts):
        values=[evaluate(a,env) for a in args[1:]]
        group_map=((0,1,2,12,3),(6,7,8,14,9),(3,4,5,13,3))
        def kind(v):
            if len(v)==1:
                if not (0<=v[0]<26 or 256<=v[0]<264): raise Unsupported('tunnel type range')
                return v[0]
            if len(v)!=2 or not 0<=v[0]<3 or not 0<=v[1]<5: raise Unsupported('tunnel group range')
            return group_map[v[0]][v[1]]
        def emit(side,z,t):
            parts.append((TUNNEL_PART,side,t,z,0,0,0,0,0,0,0,-1))
        if name in ('TrackPaintUtilDrawStationTunnel','TrackPaintUtilDrawStationTunnelTall'):
            emit(values[0]&1,values[1],9 if name.endswith('Tall') else 6);return
        if name=='PaintUtilSetVerticalTunnel': emit(2,values[0],0);return
        if name=='PaintUtilPushTunnelRotated': emit(values[0]&1,values[1],kind(values[2:]));return
        if name in ('PaintUtilPushTunnelLeft','PaintUtilPushTunnelRight'):
            emit(int(name.endswith('Right')),values[0],kind(values[1:]));return
        if 'QuarterTurn1TileTunnel' in name:
            if len(values)==6: direction,height,start,first,end,last=values
            else:
                group,direction,height,start,first,end,last=values
                first=kind([group,first]);last=kind([group,last])
            if name.startswith('TrackPaintUtilRight'):
                direction=(direction+3)&3;start,end=end,start;first,last=last,first
            if direction==0: emit(0,height+start,first)
            elif direction==2: emit(1,height+end,last)
            elif direction==3: emit(1,height+start,first);emit(0,height+end,last)
            return
        if '25Deg' in name:
            group,height,direction,sequence,first,last=values
            first=kind([group,first]);last=kind([group,last]);delta=8 if 'Up' in name else -8
            if sequence==0 and direction in (0,3): emit(0 if direction==0 else 1,height-delta,first)
            if sequence==3 and direction in (0,1): emit(1 if direction==0 else 0,height+delta,last)
            return
        if len(values)==4: height,direction,sequence,t=values
        else: group,sub,height,direction,sequence=values;t=kind([group,sub])
        end=6 if '5Tiles' in name else 3
        if name.startswith('TrackPaintUtilLeft'):
            if sequence==0 and direction in (0,3): emit(0 if direction==0 else 1,height,t)
            if sequence==end and direction in (2,3): emit(1 if direction==2 else 0,height,t)
        else:
            if sequence==0 and direction in (0,3): emit(0 if direction==0 else 1,height,t)
            if sequence==end and direction in (0,1): emit(1 if direction==0 else 0,height,t)

    def paint(self, source, name, sequence, direction, height, state, parts, depth=0, track_type=0, dependencies=None):
        if depth > 16: raise Unsupported('call depth')
        # Chairlift station endpoints depend on neighboring raw map state. Keep
        # that decision on the GPU; the immutable marker owns the exact source
        # station family, not a guessed generic station layout.
        if name == 'ChairliftPaintStation':
            parts.append((STATION_PART,0,0,height,0,0,0,0,8,0,0,-1))
            if CAPTURE_TUNNELS:
                parts.append((TUNNEL_PART,direction&1,6,height,0,0,0,0,0,0,0,-1))
            return
        env = ChainMap({}, source.globals, self.constants)
        env.update(_functionName=name,trackSequence=sequence, direction=direction, height=height, state=state,
                   chain=bool(state & 1), inverted=bool(state & 2), brakeClosed=bool(state & 4),
                   cable=bool(state&8),csgLoaded=bool(state&16),greenLight=bool(state&32),hasPlatforms=bool(state&64),trackType=track_type,
                   _dependencies=dependencies if dependencies is not None else set())
        helix=re.fullmatch(r'OpenRCT2::trackPaint((?:Left|Right)Quarter(?:Banked)?HelixLarge(?:Up|Down))<(.*)>',name)
        if helix:
            if sequence>=7: return
            if CAPTURE_TUNNELS: raise Unsupported('generic TED tunnel metadata not yet authored')
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
        if depth==0:
            for i,part in enumerate(parts):
                if isinstance(part[0],FrontTrackImage):
                    if part[10]>3: raise Unsupported('front-track metadata on non-track component')
                    item=list(part);item[0]=int(item[0]);item[10]|=8;parts[i]=tuple(item)


class SupportTranslator(Translator):
    """Separate source-authored support program. Rail tables are never rewritten."""
    capture_supports = True

    def __init__(self, root):
        super().__init__(root)
        self.support_ops = []
        self.support_predicate = 0
        self.support_gaps = set()
        self.station_no_support = set()
        path = 'src/openrct2/paint/support/MetalSupports.h'
        text = clean((root/path).read_text())
        self.hashes[path] = hashlib.sha256((root/path).read_bytes()).hexdigest()
        for enum in ('MetalSupportType', 'MetalSupportPlace'):
            body = re.search(r'enum class '+enum+r'[^\{]*\{(.*?)\}', text, re.S)[1]
            for name,value in re.findall(r'(\w+)\s*=\s*(\d+)',body):
                self.constants[enum+'_'+name] = int(value)
        path='src/openrct2/paint/track/Segment.h'
        self.hashes[path]=hashlib.sha256((root/path).read_bytes()).hexdigest()
        segment_globals=Source(path,(root/path).read_text()).globals
        self.constants.update(segment_globals)
        self.constants['diagBlockedSegments']=segment_globals['kDiagStraightFlat']
        path='src/openrct2/paint/track/Support.h'
        self.hashes[path]=hashlib.sha256((root/path).read_bytes()).hexdigest()
        self.constants.update(Source(path,(root/path).read_text()).globals)
        text=clean((root/'src/openrct2/ride/TrackPaint.h').read_text())
        body=re.search(r'constexpr MetalSupportPlace kDiagSupportPlacement\[\]\s*=\s*(\{.*?\});',text,re.S)[1]
        self.constants['kDiagSupportPlacement']=Deferred(tokens(body))

    def value(self, values, env):
        text = ''.join(values)
        if text.startswith('BlockedSegments::'):
            return self.value(tokens(text.split('::',1)[1]),env)
        if text == 'supportType.metal':
            return env.get('_metalType', 255)
        if text == 'supportType' and isinstance(env.get('supportType'), int):
            return env['supportType']
        invoked = call(values)
        if invoked and invoked[0] in ('PaintUtilRotateSegments', 'paintSegmentsRotate'):
            mask, rotation = [self.value(a,env) for a in invoked[1]]
            shift=(rotation&3)*2; outer=mask&255
            return (mask&256)|(((outer<<shift)|(outer>>(8-shift)))&255)
        if invoked and invoked[0] == 'EnumToFlag':
            return 1 << self.value(invoked[1][0],env)
        text = re.sub(r'(MetalSupportType|MetalSupportPlace)::',r'\1_',text)
        return evaluate(tokens(text),env)

    def append_support(self, opcode, parts, **values):
        fields = dict(type=0,placement=0,rotation=4,height=0,extra=0,mask=0,slope=0,
                      predicate=self.support_predicate,colour=1,railOrdinal=len(parts),reserved=0)
        fields.update(values)
        self.support_ops.append(tuple([opcode]+list(fields.values())))
        if len(self.support_ops)>64: raise Unsupported('support program operation capacity')

    def execute(self,node,env,source,parts,depth,getter=False):
        if getter: return super().execute(node,env,source,parts,depth,getter)
        if node[0]=='if':
            condition=''.join(node[1]); negate=condition.startswith('!')
            plain=condition[1:] if negate else condition
            if plain=='TrackPaintUtilShouldPaintSupports(session.MapPosition)':
                if not auxiliary_local_block(node[2]) or not auxiliary_local_block(node[3]):
                    raise Unsupported('support checker controls non-support statements')
                previous=self.support_predicate
                try:
                    for branch,predicate in ((node[2],2 if negate else 1),(node[3],1 if negate else 2)):
                        if branch is None or (previous!=0 and previous!=predicate): continue
                        self.support_predicate=predicate
                        self.execute(branch,env,source,parts,depth)
                finally: self.support_predicate=previous
                return None
        if node[0]=='expr':
            invoked=call(node[1])
            if invoked:
                name,args=invoked
                metal=re.fullmatch(r'Metal([AB])SupportsPaintSetup(Rotated)?',name)
                if metal:
                    rotated=bool(metal[2]); expected=7 if rotated else 6
                    if len(args)!=expected: raise Unsupported('metal support call signature')
                    role=image_value(args[-1],env).role
                    if role>3: raise Unsupported('metal support colour role')
                    self.append_support(1 if metal[1]=='A' else 2,parts,type=self.value(args[1],env),
                        placement=self.value(args[2],env),rotation=self.value(args[3],env) if rotated else 4,
                        extra=self.value(args[4 if rotated else 3],env),height=self.value(args[5 if rotated else 4],env),colour=role)
                    return None
                if name=='PaintUtilSetSegmentSupportHeight':
                    if len(args)!=4: raise Unsupported('segment support signature')
                    self.append_support(3,parts,mask=self.value(args[1],env),height=self.value(args[2],env),slope=self.value(args[3],env))
                    return None
                if name=='PaintUtilSetGeneralSupportHeight':
                    if len(args) not in (2,3): raise Unsupported('general support signature')
                    self.append_support(4,parts,height=self.value(args[1],env),slope=self.value(args[2],env) if len(args)==3 else 0x20)
                    return None
                if name=='TrackPaintUtilOnridePhotoPlatformPaint':
                    if len(args)!=4: raise Unsupported('photo platform support signature')
                    result=super().execute(node,env,source,parts,depth,getter)
                    direction=self.value(args[1],env);height=self.value(args[2],env)
                    for placement in ((6,7) if direction&1 else (5,8)):
                        self.append_support(1,parts,type=self.value(args[3],env),placement=placement,
                            rotation=direction,height=height,reserved=1)
                    return result
                if name=='TrackPaintUtilOnridePhotoPaint2':
                    if not 4<=len(args)<=6 or ''.join(args[2])!='trackElement':
                        raise Unsupported('photo2 support signature')
                    result=super().execute(node,env,source,parts,depth,getter)
                    height=self.value(args[3],env)
                    extra=self.value(args[4],env) if len(args)>4 else self.value(tokens('kGeneralSupportHeightOnRidePhoto'),env)
                    self.append_support(3,parts,mask=511,height=65535,slope=0)
                    self.append_support(4,parts,height=height+extra,slope=32)
                    return result
                if name=='TrackPaintUtilDiagTilesPaintExtra':
                    if len(args)!=7: raise Unsupported('diagonal extra support helper signature')
                    result=super().execute(node,env,source,parts,depth,getter)
                    direction=self.value(args[3],env);sequence=self.value(args[4],env)
                    if not 0<=sequence<4: raise Unsupported('diagonal extra support sequence')
                    if sequence==3:
                        self.append_support(1,parts,type=self.value(args[6],env),placement=self.constants['MetalSupportPlace_leftCorner'],
                            rotation=direction,height=self.value(args[2],env),colour=1)
                    mask=self.value(tokens('diagBlockedSegments['+str(sequence)+']'),env)
                    mask=self.value(tokens('PaintUtilRotateSegments('+str(mask)+','+str(direction)+')'),env)
                    self.append_support(3,parts,mask=mask,height=65535,slope=0)
                    self.append_support(4,parts,height=self.value(args[2],env)+32,slope=32)
                    return result
                if name=='DrawSupportsSideBySide':
                    if len(args) not in (5,6): raise Unsupported('side by side support signature')
                    direction=self.value(args[1],env)
                    for placement in ((6,7) if direction&1 else (5,8)):
                        self.append_support(1,parts,type=self.value(args[4],env),placement=placement,rotation=direction,
                            height=self.value(args[2],env),extra=self.value(args[5],env) if len(args)==6 else 0,
                            colour=image_value(args[3],env).role)
                    # Placements above are already rotated; only graphic rotation remains.
                    for i in (-1,-2):
                        op=list(self.support_ops[i]);op[11]=1;self.support_ops[i]=tuple(op)
                    return None
                if name in ('DrawSBendLeftSupports','DrawSBendRightSupports',
                            'TrackPaintUtilLeftCorkscrewUpSupports','TrackPaintUtilRightVerticalLoopSegments'):
                    target=self.find(name,source);parameters=target.parameters[self.local_name(target,name)]
                    if len(parameters)!=len(args): raise Unsupported('support helper signature '+name)
                    local=ChainMap(dict(env.maps[0]),target.globals,self.constants)
                    for parameter,arg in zip(parameters,args):
                        if parameter=='session': continue
                        local[parameter]=self.value(arg,env)
                    return self.execute(target.functions[self.local_name(target,name)],local,target,parts,depth+1)
                if name in STATION_CALLS+COVER_CALLS:
                    # These helpers paint platform/fence/shelter art, already
                    # represented by the station marker; they have no support setup.
                    helper='TrackPaintUtilDrawStationImpl' if name in ('TrackPaintUtilDrawStation','TrackPaintUtilDrawStation2') else name
                    if helper not in self.station_no_support:
                        text=clean((self.root/'src/openrct2/ride/TrackPaint.cpp').read_text())
                        match=re.search(r'\b'+helper+r'\([^;{]*\)\s*\{',text)
                        if not match: raise Unsupported('station helper definition missing '+helper)
                        depth_count=1;end=match.end()
                        while depth_count and end<len(text):
                            depth_count+=(text[end]=='{')-(text[end]=='}');end+=1
                        body=text[match.end():end-1]
                        if depth_count or 'SupportsPaintSetup' in body or 'DrawSupports' in body:
                            raise Unsupported('station helper acquired support operations')
                        self.station_no_support.add(helper)
                if AUX.fullmatch(name) and not TUNNEL_CALL.fullmatch(name) and name not in STATION_CALLS+COVER_CALLS:
                    self.support_gaps.add(name)
                    return None
        return super().execute(node,env,source,parts,depth,getter)

    def paint(self,source,name,*args,**kwargs):
        if name.startswith('OpenRCT2::trackPaint'):
            self.support_gaps.add('generic TED support helper: '+name)
        return super().paint(source,name,*args,**kwargs)


class WoodenSupportTranslator(SupportTranslator):
    """DRAFT: preserve wooden calls as operations; never execute their terrain-dependent return value."""

    def __init__(self, root):
        super().__init__(root)
        self.wooden_prepend = None
        path='src/openrct2/paint/support/WoodenSupports.h'
        header=clean((root/path).read_text())
        self.hashes[path]=hashlib.sha256((root/path).read_bytes()).hexdigest()
        for enum in ('WoodenSupportType','WoodenSupportSubType','WoodenSupportTransitionType'):
            body=re.search(r'enum class '+enum+r'[^\{]*\{(.*?)\}',header,re.S)[1]
            previous=-1
            for member in split_top(tokens(body)):
                if not member: continue
                previous=evaluate(member[2:],self.constants) if len(member)>1 else previous+1
                self.constants[enum+'_'+member[0]]=previous
        self.constants['kQuarterHelixSequenceCount']=7
        map_limits=(root/'src/openrct2/world/MapLimits.h').read_text()
        for constant in ('kCoordsZStep','kLandHeightStep'):
            self.constants[constant]=evaluate(tokens(re.search(r'\b'+constant+r'\s*=\s*([^;]+)',map_limits)[1]),self.constants)
        generic='src/openrct2/paint/track/TrackPaintGeneric.h'
        self.hashes[generic]=hashlib.sha256((root/generic).read_bytes()).hexdigest()
        self.wooden_sequences={};self.wooden_descriptors={}
        paths=[root/'src/openrct2/ride/TrackData.cpp']+sorted((root/'src/openrct2/ride/ted').glob('TED.*.h'))
        for path in paths:
            text=clean(path.read_text())
            self.hashes[str(path.relative_to(root)).replace('\\','/')]=hashlib.sha256(path.read_bytes()).hexdigest()
            for match in re.finditer(r'constexpr\s+SequenceDescriptor\s+(\w+)\s*=\s*\{',text):
                self.wooden_sequences[match[1]]=self.brace_body(text,match.end()-1)
            for match in re.finditer(r'constexpr\s+auto\s+(\w+)\s*=\s*TrackElementDescriptor\s*\{',text):
                self.wooden_descriptors[match[1]]=self.brace_body(text,match.end()-1)
        source=clean((root/'src/openrct2/ride/TrackData.cpp').read_text())
        table=re.search(r'kTrackElementDescriptors\s*=\s*std::to_array<TrackElementDescriptor>\(\{(.*?)\}\)',source,re.S)
        if not table: raise Unsupported('wooden TED directory missing')
        self.wooden_types=[x.strip() for x in table[1].split(',') if x.strip()]
        for path in ('src/openrct2/paint/support/WoodenSupports.cpp','src/openrct2/paint/support/WoodenSupports.hpp',
                     'src/openrct2/ride/ted/TrackElementDescriptor.h'):
            self.hashes[path]=hashlib.sha256((root/path).read_bytes()).hexdigest()

    @staticmethod
    def brace_body(text,start):
        depth=1;end=start+1
        while depth and end<len(text):
            if text[end]=='{': depth+=1
            elif text[end]=='}': depth-=1
            end+=1
        if depth: raise Unsupported('unterminated TED initializer')
        return text[start+1:end-1]

    def value(self, values, env):
        text=''.join(values)
        if text=='supportType.wooden': return env.get('_woodenType',255)
        text=re.sub(r'(WoodenSupportType|WoodenSupportSubType|WoodenSupportTransitionType)::',r'\1_',text)
        text=text.replace('TrackElemType::','')
        return super().value(tokens(text),env)

    def ted_fields(self,name):
        if name not in self.wooden_sequences: raise Unsupported('TED sequence initializer unavailable '+name)
        result={}
        for field in split_top(tokens(self.wooden_sequences[name])):
            if len(field)>=4 and field[0]=='.' and field[2]=='=': result[field[1]]=field[3:]
            elif field: raise Unsupported('TED designated sequence fields changed')
        return result

    def ted_field(self,name,field,default=None,depth=0):
        if depth>16: raise Unsupported('TED field alias depth')
        value=self.ted_fields(name).get(field,default)
        if value is None: raise Unsupported('TED missing field '+field)
        if len(value)==3 and value[1]=='.': return self.ted_field(value[0],value[2],default,depth+1)
        return value

    def ted_segments(self,name,index,depth=0):
        if depth>16: raise Unsupported('TED segment alias depth')
        value=self.ted_field(name,'blockedSegments',['{','{','0',',','0',',','0','}','}'])
        invoked=call(value)
        if invoked and invoked[0]=='blockedSegmentsFlipXAxis':
            member=invoked[1][0]
            if len(member)!=3 or tuple(member[1:])!=('.','blockedSegments'):
                raise Unsupported('TED segment flip member')
            mask=self.ted_segments(member[0],index,depth+1)
            outer=int(format(mask&255,'08b')[::-1],2)
            return (mask&256)|(((outer<<3)|(outer>>5))&255)
        if len(value)==3 and value[1]=='.': return self.ted_segments(value[0],index,depth+1)
        values=self.value(tokens(''.join(value).replace('PS::','PaintSegment::')),self.constants)
        while len(values)==1 and isinstance(values[0],list): values=values[0]
        if len(values)!=3: raise Unsupported('TED blocked segment type count')
        return values[index]

    def ted_general_height(self,name):
        value=self.ted_field(name,'generalSupportHeight',['-32768'])
        invoked=call(value)
        if not invoked: return self.value(value,self.constants)
        if invoked[0]!='calculateGeneralSupportHeight' or len(invoked[1])!=3:
            raise Unsupported('TED general support expression')
        member=invoked[1][0]
        if len(member)!=3 or tuple(member[1:])!=('.','clearance'): raise Unsupported('TED clearance member')
        clearance=self.ted_field(member[0],'clearance')
        if clearance[0]!='{' or clearance[-1]!='}': raise Unsupported('TED clearance initializer')
        fields=split_top(clearance[1:-1])
        z=self.value(fields[2],self.constants);clearance_z=self.value(fields[3],self.constants)
        offset=self.value(invoked[1][1],self.constants);half=self.value(invoked[1][2],self.constants)
        step=self.constants['kCoordsZStep'];land=self.constants['kLandHeightStep']
        half=(z+int(half)*step)%land==step
        rounded=((clearance_z+(step if half else land)-1)//(step if half else land))*(step if half else land)
        if half: rounded+=(rounded+step)%land
        return rounded+offset*land

    def helix_supports(self,source,name,sequence,direction,height,parts,track_type):
        match=re.fullmatch(r'OpenRCT2::trackPaint((?:Left|Right)Quarter(?:Banked)?HelixLarge(?:Up|Down))<(.*)>',name)
        if not match: return False
        if sequence>=7: return True
        args=split_top(tokens(match[2]))
        if len(args)!=7: raise Unsupported('quarter helix support template signature')
        descriptor=self.wooden_descriptors[self.wooden_types[track_type]]
        table=re.search(r'\.sequenceData\s*=\s*\{\s*(\d+)\s*,\s*\{(.*?)\}\s*\}',descriptor,re.S)
        names=[x.strip() for x in table[2].split(',') if x.strip()] if table else []
        if len(names)!=7: raise Unsupported('quarter helix TED sequence count')
        seq_name=names[sequence];fields=self.ted_fields(seq_name)
        env=ChainMap({},source.globals,self.constants)
        modified_sequence=sequence;modified_direction=direction
        if match[1].endswith('Down'):
            modified_sequence=self.value(self.ted_field(seq_name,'reversedTrackSequence'),env)
            rotation=re.search(r'\.reversedRotationOffset\s*=\s*([^,}]+)',descriptor)
            if not rotation: raise Unsupported('quarter helix TED reversed rotation')
            modified_direction=(direction+self.value(tokens(rotation[1]),env))&3
        extra_rotation=self.value(self.ted_field(seq_name,'extraSupportRotation',['0']),env)
        support_rotation=(direction+extra_rotation)&3
        if self.value(args[2],env):
            value=self.ted_field(seq_name,'woodenSupports',['{','WoodenSupportSubType::null','}'])
            values=split_top(value[1:-1]);subtype=self.value(values[0],env)
            transition=self.value(values[1],env) if len(values)>1 else 255
            support_height=self.value(values[2],env) if len(values)>2 else 0
            self.wooden_op(parts,'A',255,subtype,support_rotation,height+support_height,1,transition,True)
        else:
            value=self.ted_field(seq_name,'metalSupports',['{','MetalSupportPlace::none','}'])
            values=split_top(value[1:-1]);place=self.value(values[0],env)
            support_height=self.value(values[2],env) if len(values)>2 else 0
            if place!=self.constants['MetalSupportPlace_none']:
                extras=self.value(args[3],env)
                self.append_support(1,parts,type=255,placement=place,rotation=support_rotation,
                    height=height+support_height,extra=extras[modified_sequence][modified_direction])
        blocked=''.join(args[4]).split('::')[-1]
        if blocked not in ('narrow','inverted','wide'): raise Unsupported('quarter helix blocked segment type')
        mask=self.ted_segments(seq_name,('narrow','inverted','wide').index(blocked))
        rotated=self.value(tokens('PaintUtilRotateSegments('+str(mask)+','+str(direction)+')'),env)
        self.append_support(3,parts,mask=rotated,height=65535,slope=0)
        general=self.ted_general_height(seq_name)
        self.append_support(4,parts,height=height+32+general if general!=-32768 else 0,slope=32)
        return True

    def wooden_sequence(self,track_type,sequence):
        try: descriptor=self.wooden_descriptors[self.wooden_types[track_type]]
        except (KeyError,IndexError): raise Unsupported('wooden TED descriptor unavailable')
        table=re.search(r'\.sequenceData\s*=\s*\{\s*(\d+)\s*,\s*\{(.*?)\}\s*\}',descriptor,re.S)
        if not table: raise Unsupported('wooden TED sequence directory unavailable')
        names=[x.strip() for x in table[2].split(',') if x.strip()]
        if len(names)!=int(table[1]) or sequence<0 or sequence>=16: raise Unsupported('wooden TED sequence range')
        # TED owns a fixed 16-entry value-initialized array. The rail baseline may
        # include ignored sequence values; their wooden descriptor is the null default.
        if sequence>=len(names): return 6,255,0
        try: body=self.wooden_sequences[names[sequence]]
        except KeyError: raise Unsupported('wooden TED sequence initializer unavailable')
        support=re.search(r'\.woodenSupports\s*=\s*\{(.*?)\}',body,re.S)
        values=split_top(tokens(support[1])) if support else []
        if len(values)>3: raise Unsupported('wooden TED support metadata changed')
        subtype=self.value(values[0],self.constants) if values else 6
        transition=self.value(values[1],self.constants) if len(values)>1 else 255
        rotation=re.search(r'\.extraSupportRotation\s*=\s*([^,}]+)',body)
        return subtype,transition,self.value(tokens(rotation[1]),self.constants) if rotation else 0

    def wooden_op(self,parts,family,support_type,subtype,direction,height,role,transition,rotated):
        if subtype==6: return
        if support_type not in (0,1,255) or subtype not in range(6) or direction not in range(4):
            raise Unsupported('wooden support type/subtype/direction range')
        if transition!=255 and transition not in range(21): raise Unsupported('wooden transition range')
        flags=(2 if rotated else 0)|(4 if self.wooden_prepend is not None else 0)
        self.append_support(5 if family=='A' else 6,parts,type=support_type,placement=subtype,
            rotation=direction,height=height,extra=transition,colour=role,
            mask=self.wooden_prepend if self.wooden_prepend is not None else 0,reserved=flags)

    def execute(self,node,env,source,parts,depth,getter=False):
        if getter: return super().execute(node,env,source,parts,depth,getter)
        if node[0]=='expr':
            values=node[1]
            if '=' in values and ''.join(values[:values.index('=')])=='session.WoodenSupportsPrependTo':
                rhs=values[values.index('=')+1:]
                if len(rhs)==1 and isinstance(env.get(rhs[0]),PaintHandle):
                    self.wooden_prepend=env[rhs[0]].index;return None
                if rhs==['nullptr'] or (len(rhs)==1 and rhs[0] in env and env[rhs[0]] is None):
                    self.wooden_prepend=None;return None
                before=len(parts)
                result=super().execute(node,env,source,parts,depth)
                self.wooden_prepend=before if len(parts)>before else None
                return result
            invoked=call(values)
            if invoked:
                name,args=invoked
                direct=re.fullmatch(r'Wooden([AB])SupportsPaintSetup(Rotated)?',name)
                if direct:
                    rotated=bool(direct[2])
                    if len(args) not in ((6,7) if rotated else (5,6,7)):
                        raise Unsupported('wooden support call signature')
                    colourIndex=5 if rotated else 4
                    transition=self.value(args[colourIndex+1],env) if len(args)>colourIndex+1 else 255
                    direction=self.value(args[3],env) if rotated else self.value(args[6],env) if len(args)==7 else 0
                    self.wooden_op(parts,direct[1],self.value(args[1],env),self.value(args[2],env),direction,
                        self.value(args[4 if rotated else 3],env),image_value(args[colourIndex],env).role,transition,rotated)
                    return None
                helper=re.fullmatch(r'DrawSupportForSequence([AB])(?:<(.*)>)?',name)
                if helper:
                    templated=helper[2] is not None
                    if len(args)!=(6 if templated else 7): raise Unsupported('wooden sequence call signature')
                    track_type=self.value(tokens(helper[2]),env) if templated else self.value(args[2],env)
                    sequenceIndex=2 if templated else 3
                    subtype,transition,extraRotation=self.wooden_sequence(track_type,self.value(args[sequenceIndex],env))
                    self.wooden_op(parts,helper[1],self.value(args[1],env),subtype,
                        (self.value(args[sequenceIndex+1],env)+extraRotation)&3,
                        self.value(args[sequenceIndex+2],env),image_value(args[sequenceIndex+3],env).role,transition,True)
                    return None
        return super().execute(node,env,source,parts,depth,getter)

    def paint(self,source,name,sequence,direction,height,state,parts,depth=0,track_type=0,dependencies=None):
        if depth==0: self.wooden_prepend=None
        if name == 'ChairliftPaintStation':
            # Both source axis functions unconditionally author these operations;
            # endpoint/fence decisions alter only the marker's GPU station art.
            self.wooden_op(parts,'A',self.constants['WoodenSupportType_truss'],
                self.constants['WoodenSupportSubType_neSw'],direction,height,2,255,True)
            Translator.paint(self,source,name,sequence,direction,height,state,parts,depth,track_type,dependencies)
            self.append_support(3,parts,mask=self.value(['kSegmentsAll'],self.constants),height=65535,slope=0)
            self.append_support(4,parts,height=height+self.value(['kDefaultGeneralSupportHeight'],self.constants),slope=0x20)
            return
        if name.startswith('OpenRCT2::trackPaint'):
            # Emit the existing rail recipe first, then its source TED support
            # operations. Reversed image indexing does not reverse support ownership.
            result=Translator.paint(self,source,name,sequence,direction,height,state,parts,depth,track_type,dependencies)
            if not self.helix_supports(source,name,sequence,direction,height,parts,track_type):
                self.support_gaps.add('generic TED support helper: '+name)
            return result
        return super().paint(source,name,sequence,direction,height,state,parts,depth,track_type,dependencies)


def qualify_wooden_prepend(ops, source_parts, baseline_parts):
    if not any(op[0] in (5,6) and op[11]&4 for op in ops): return ops
    baseline=[(i,tuple(x&0xffffffff for x in part)) for i,part in enumerate(baseline_parts) if part[0]!=TUNNEL_PART]
    source=[tuple(x&0xffffffff for x in part) for part in source_parts]
    if source!=[part for _,part in baseline]: raise Unsupported('wooden prepend rail baseline identity mismatch')
    result=[]
    for original in ops:
        op=list(original)
        if op[0] in (5,6) and op[11]&4:
            if op[6]>=len(baseline): raise Unsupported('wooden prepend rail ordinal range')
            op[6]=baseline[op[6]][0]
        result.append(tuple(op))
    return result


def build_supports(args):
    """Read the already-qualified rail table; emit support coverage separately."""
    baseline=args.support_baseline.read_bytes()
    words=struct.unpack('<'+'I'*(len(baseline)//4),baseline)
    if words[:5]!=(0x5452434b,1,81,350,8): raise Unsupported('support baseline header')
    translator=WoodenSupportTranslator(args.root)
    descriptors=[0]*(81*350*3);rows=[];ops=[];row_sets={};op_sets={};cache={};reports=[]
    for style,getter in enumerate(translator.getters):
        print('Authoring metal/wooden supports style %d/81 %s'%(style+1,getter),flush=True)
        report=dict(style=style,supported=[],supportRejected={},omittedCalls={})
        for track_type in range(350):
            offset=(style*350+track_type)*3;sequences=words[8+offset+1]
            if sequences==0:continue
            key=(getter,track_type,sequences)
            if key not in cache:
                try:
                    sampled={};dependencies=set();mask=words[8+offset+2];gaps=set()
                    while True:
                        for state in range(128):
                            if state&~mask or state in sampled:continue
                            source,name=translator.getter(getter,track_type,state=state,dependencies=dependencies)
                            program=[]
                            for sequence in range(sequences):
                                for direction in range(4):
                                    translator.support_ops=[];translator.support_gaps=set();translator.support_predicate=0
                                    source_parts=[]
                                    translator.paint(source,name,sequence,direction,0,state,source_parts,track_type=track_type,dependencies=dependencies)
                                    baseline_mask=words[8+offset+2]
                                    packed_state=sum(((state>>bit)&1)<<sum(bool(baseline_mask&(1<<j)) for j in range(bit))
                                                     for bit in range(7) if baseline_mask&(1<<bit))
                                    row=words[8+offset]+(packed_state*sequences+sequence)*4+direction
                                    first,count=words[words[5]+row*2:words[5]+row*2+2]
                                    baseline_parts=[words[words[6]+(first+i)*12:words[6]+(first+i+1)*12] for i in range(count)]
                                    program.append(tuple(qualify_wooden_prepend(translator.support_ops,source_parts,baseline_parts)))
                                    gaps.update(translator.support_gaps)
                            sampled[state]=tuple(program)
                        next_mask=mask|sum(dependencies)
                        if next_mask==mask:break
                        mask=next_mask
                    variants=[sampled[state&mask] for state in range(128)]
                    mask=next(m for m in range(128) if all(variants[s]==variants[s&m] for s in range(128)))
                    cache[key]=(variants,mask,gaps)
                except (Unsupported,KeyError,ZeroDivisionError,RecursionError) as error:cache[key]=str(error)
            value=cache[key]
            if isinstance(value,str):report['supportRejected'][str(track_type)]=value;continue
            variants,mask,gaps=value;packed=[]
            for state in range(128):
                if state&~mask:continue
                for program in variants[state]:
                    if program not in op_sets:
                        op_sets[program]=len(ops)//12
                        for op in program:ops.extend(x&0xffffffff for x in op)
                    packed.extend((op_sets[program],len(program)))
            row_key=tuple(packed)
            if row_key not in row_sets:row_sets[row_key]=len(rows)//2;rows.extend(packed)
            descriptors[offset:offset+3]=[row_sets[row_key],sequences,mask];report['supported'].append(track_type)
            if gaps:report['omittedCalls'][str(track_type)]=sorted(gaps)
        reports.append(report)
    result=[0x54535054,1,81,350,8,8+len(descriptors),8+len(descriptors)+len(rows),0]+descriptors+rows+ops
    result[7]=len(result)
    if len(result)>16*1024*1024:raise Unsupported('support word capacity')
    binary=struct.pack('<'+'I'*len(result),*result);compressed=zlib.compress(binary,9)
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'native-track-supports.bin').write_bytes(binary)
    lines=['// Generated by extract-native-track-recipes.py --support-baseline; do not edit.',
           'static constexpr uint32_t kNativeTrackSupportWordCount = %du;'%len(result),
           'static constexpr uint8_t kNativeTrackSupportCompressed[] = {']
    lines+=['    '+','.join('0x%02x'%v for v in compressed[i:i+24])+',' for i in range(0,len(compressed),24)]
    (args.output/'NativeTrackSupportData.inc').write_text('\n'.join(lines+['};','']),encoding='utf-8')
    report=dict(schema=1,baselineSha256=hashlib.sha256(baseline).hexdigest(),sourceSha256=translator.hashes,
        authoringScriptSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        supportSha256=hashlib.sha256(binary).hexdigest(),wordCount=len(result),styles=reports,
        supportedStyleTypes=sum(len(s['supported']) for s in reports),
        scope='DRAFT source-authored metal/wooden support programs; omitted calls and rejected support programs remain explicit. Rail baseline unchanged.')
    (args.output/'support-coverage.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Supported metal/wooden program style/types:',report['supportedStyleTypes'])


def build(args):
    global CAPTURE_TUNNELS
    translator = Translator(args.root)
    descriptors = [0] * (81 * translator.type_count * 3)
    rows, parts = [], []; part_sets = {}; row_sets = {}; coverage = []
    # Cache source functions shared by multiple styles/types, without runtime objects.
    cache = {}
    for style, getter in enumerate(translator.getters):
        print('Translating style %d/%d %s' % (style+1,len(translator.getters),getter), flush=True)
        style_report = dict(style=style, getter=getter, supported=[], rejected={}, tunnelRejected={})
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
                                        CAPTURE_TUNNELS=True
                                        try:
                                            translator.paint(source,name,sequence,direction,0,state,result,
                                                             track_type=track_type,dependencies=dependencies)
                                        except Unsupported as tunnel_error:
                                            # Retain existing rail coverage when a dynamic tunnel rule is
                                            # not yet expressible. Record the missing tunnel rule explicitly.
                                            CAPTURE_TUNNELS=False;result=[]
                                            translator.paint(source,name,sequence,direction,0,state,result,
                                                             track_type=track_type,dependencies=dependencies)
                                            style_report['tunnelRejected'][str(track_type)]=str(tunnel_error)
                                        finally: CAPTURE_TUNNELS=False
                                        markers=sum(p[0]==STATION_PART and p[8] not in (7,8) for p in result)
                                        chairlifts=sum(p[0]==STATION_PART and p[8]==8 for p in result)
                                        covers=sum(p[0]==STATION_PART and p[8]==7 for p in result)
                                        photos=sum(p[0]==PHOTO_PART for p in result)
                                        graphics=[p for p in result if p[0]!=TUNNEL_PART]
                                        if len(result)>16 or len(graphics)+markers*8+chairlifts*10+covers+photos*2>16 or sum(p[-1]==-1 for p in graphics)+markers*6+chairlifts*7+photos*2>12:
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
                  omittedFamilies=['supports','unsupported tunnel requests listed per style','vehicles'],
                  scope='Static source translation; runtime row selection executes on GPU; no raster qualification')
    (args.output/'coverage.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('sourceSha256','styles')}))
    if not report['supportedStyleTypes']: raise Unsupported('no supported rules')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--support-baseline',type=Path,help='Author support sidecar for this immutable rail binary without modifying it')
    args=parser.parse_args()
    if args.support_baseline: build_supports(args)
    else: build(args)
