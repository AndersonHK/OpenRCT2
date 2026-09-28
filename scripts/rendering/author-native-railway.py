from pathlib import Path
import importlib.util, json, re, struct
# Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
# Source-text authoring only. This tool never builds or invokes a painter.
import argparse, subprocess, tarfile, io
cli=argparse.ArgumentParser()
cli.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
cli.add_argument('--work-dir',type=Path)
cli.add_argument('--output-root',type=Path)
args=cli.parse_args()
ROOT=args.root.resolve();P=(args.work_dir or ROOT/'obj/railway-authoring').resolve()
OUTPUT=(args.output_root or ROOT).resolve()
(P/'reference').mkdir(parents=True,exist_ok=True);(P/'baseline').mkdir(parents=True,exist_ok=True)
BASELINE='68b3880700'
inputs=['src/openrct2/paint','src/openrct2/ride','src/openrct2/SpriteIds.h',
        'src/openrct2/world/MapLimits.h','src/openrct2/drawing/ImageIndexType.h']
archive=subprocess.check_output(['git','-C',str(ROOT),'archive',BASELINE,*inputs])
with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
 for member in tar.getmembers():
  target=(P/'reference'/member.name).resolve()
  if P/'reference' not in target.parents or member.issym() or member.islnk():
   raise ValueError('invalid archive member')
 tar.extractall(P/'reference')
(P/'baseline/extract-native-track-recipes.py').write_bytes(subprocess.check_output(
 ['git','-C',str(ROOT),'show',BASELINE+':scripts/rendering/extract-native-track-recipes.py']))
s=importlib.util.spec_from_file_location('base',P/'baseline/extract-native-track-recipes.py'); b=importlib.util.module_from_spec(s); s.loader.exec_module(b)
b.CAPTURE_TUNNELS=True
original_evaluate=b.evaluate
def evaluate(values,env):
 c=b.call(values)
 if c and c[0]=='EnumValue':return evaluate(c[1][0],env)
 if c and c[0] in ('CoordsXY','CoordsXYZ'):
  out=[]
  for a in c[1]:
   v=evaluate(a,env);out.extend(v if isinstance(v,list) else [v])
  return out
 text=''.join(values)
 if re.fullmatch(r'\w+\+CoordsXYZ\{[^{}]*\}',text):
  lhs,rhs=text.split('+CoordsXYZ{');a=evaluate(b.tokens(lhs),env);z=evaluate(b.tokens('{'+rhs),env)
  return [x+y for x,y in zip(a,z)]
 return original_evaluate(values,env)
b.evaluate=evaluate
SUPPORT=0xfffffffb
STATE=0xfffffffa
class Railway(b.WoodenSupportTranslator):
 def __init__(self):
  super().__init__(P/'reference')
  self.result=False
  h=(P/'reference/src/openrct2/ride/TrackPaint.h').read_text()
  self.constants['kDiagSpriteMap']=evaluate(b.tokens(re.search(r'kDiagSpriteMap[^=]*=\s*(\{.*?\});',h,re.S)[1]),{})
  self.src=self.find('GetTrackPaintFunctionMiniatureRailway')
  text=self.src.text.replace('session.PathElementOnSameHeight != nullptr','false')
  # Structure layouts are authoring data, not runtime instances.
  self.src=b.Source(self.src.path,text)
  floor=re.search(r'kFloors\[\]\s*=\s*(\{.*?\});',text,re.S)[1]
  arr=b.evaluate(b.tokens(floor),self.constants)
  self.src.globals['kFloors']=[dict(zip(('image_id','bound_size','bound_offset'),v)) for v in arr]
  for name in self.src.functions: self.by_name[name]=self.src
 def append_support(self,*args,**kwargs):
  super().append_support(*args,**kwargs)
  op=self.support_ops[-1]
  # Opcode plus complete immutable support payload in the common 12-word record.
  args[1].append(tuple([SUPPORT if op[0] in (5,6) else STATE]+list(op[:8])+[op[11],op[9]]+[-1]))
 def execute(self,node,env,source,parts,depth,getter=False):
  if node and node[0]=='expr':
   v=node[1]
   if len(v)>3 and v[0] in ('CoordsXY','CoordsXYZ') and v[2]=='(':
    env[v[1]]=evaluate([v[0]]+v[2:],env);return
   if '=' in v:
    k=v.index('='); c=b.call(v[k+1:])
    if c and re.fullmatch(r'Wooden[AB]SupportsPaintSetup(Rotated)?',c[0]):
     super().execute(('expr',v[k+1:]),env,source,parts,depth,getter)
     env[v[k-1]]=self.result;return
  if node and node[0]=='if':
   c=b.call(node[1])
   if c and re.fullmatch(r'Wooden[AB]SupportsPaintSetup(Rotated)?',c[0]):
    super().execute(('expr',node[1]),env,source,parts,depth,getter)
    branch=node[2] if self.result else node[3]
    return self.execute(branch,env,source,parts,depth,getter) if branch else None
  return super().execute(node,env,source,parts,depth,getter)

def main():
 t=Railway(); rows=[]; types=[]
 names=re.findall(r'case TrackElemType::(\w+)',t.src.text[t.src.text.index('GetTrackPaintFunctionMiniatureRailway'):])
 for name in names:
  typ=t.constants[name]; source,fn=t.getter('GetTrackPaintFunctionMiniatureRailway',typ)
  n=7 if 'QuarterTurn5' in fn else 5 if 'Eighth' in fn else 4 if ('QuarterTurn3' in fn or 'SBend' in fn or 'Diag' in fn) else 1
  start=len(rows)
  for seq in range(n):
   for d in range(4):
    pair=[]
    for result in (False,True):
     t.result=result;t.support_ops=[]; parts=[]
     try:t.paint(source,fn,seq,d,0,64,parts,track_type=typ)
     except Exception as e:raise RuntimeError((name,seq,d,result)) from e
     assert sum(x[0]==SUPPORT for x in parts)<=1
     pair.append(parts)
    rows.append(pair)
  types.append(dict(name=name,type=typ,sequences=n,first=start))
 (P/'authored.json').write_text(json.dumps(dict(types=types,rows=rows),indent=2))
 # Dense raw TrackElemType directory, dual spans per camera-relative row.
 directory=[0]*(t.type_count*2); spans=[]; parts=[]; cache={}
 for entry in types:directory[entry['type']*2:entry['type']*2+2]=[entry['first'],entry['sequences']]
 for pair in rows:
  support=-1
  for variant in pair:
   key=tuple(variant)
   if key not in cache:cache[key]=len(parts);parts.extend(variant)
   first=cache[key];spans.extend((first,len(variant)))
   for i,p in enumerate(variant):
    if p[0]==SUPPORT:support=first+i
  spans.append(support)
 words=[0x5241494c,1,t.type_count,8,8+len(directory),8+len(directory)+len(spans),len(parts),t.constants['miniatureRailway']]+directory+spans+[v&0xffffffff for p in parts for v in p]
 assets=sorted({p[0] for p in parts if p[0]<0x7ffff}|{t.constants[n] for n in t.constants if n.startswith('SPR_TRACKS_MINIATURE_RAILWAY_')})
 out=OUTPUT/'src/openrct2-renderer/gpu/NativeRailwayData.h';out.parent.mkdir(parents=True,exist_ok=True)
 def array(name,values):return 'inline constexpr uint32_t '+name+'[] = {\n'+''.join('    '+','.join(str(v&0xffffffff)+'u' for v in values[i:i+12])+',\n' for i in range(0,len(values),12))+'};\n'
 out.write_text('// Generated by scripts/rendering/author-native-railway.py from commit 68b3880700. GPL-3.0-or-later.\n#pragma once\n#include <cstdint>\nnamespace OpenRCT2::Ui::Gpu {\n'+array('kNativeRailwayWords',words)+array('kNativeRailwayImages',assets)+'}\n')
 (P/'asset-constants.json').write_text(json.dumps({k:v for k,v in t.constants.items() if k.startswith('SPR_TRACKS_MINIATURE_RAILWAY_') or k=='miniatureRailway'},indent=2))
 print('types',len(types),'rows',len(rows),'unique records',len(parts),'words',len(words),'assets',len(assets))
if __name__=='__main__':main()
