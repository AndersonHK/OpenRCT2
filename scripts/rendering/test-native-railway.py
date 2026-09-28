"""Offline contract checks; no Vulkan, compiler, painter or live world required."""
import json,itertools,re
from pathlib import Path
import argparse
cli=argparse.ArgumentParser();cli.add_argument('--work-dir',type=Path,required=True);cli.add_argument('--output-root',type=Path,required=True)
args=cli.parse_args();P=args.work_dir;OUTPUT=args.output_root
j=json.loads((P/'authored.json').read_text());TUNNEL=0xfffffffd;SUPPORT=0xfffffffb;STATE=0xfffffffa
assert len(j['types'])==27 and len(j['rows'])==352
assert sum(t['sequences']*4 for t in j['types'])==352
for false,true in j['rows']:
 assert [p for p in false if p[0] in (SUPPORT,STATE,TUNNEL)]==[p for p in true if p[0] in (SUPPORT,STATE,TUNNEL)]
 for row in (false,true):
  assert len(row)<=16
  supports=[p for p in row if p[0]==SUPPORT];assert len(supports)<=1
  if supports:
   ix=row.index(supports[0]);assert all(p[0]!=STATE for p in row[:ix])
  for i,p in enumerate(row):
   assert len(p)==12
   if p[-1]>=0:
    assert p[-1]<i
    assert row[p[-1]][0] not in (STATE,TUNNEL)
    assert not any(x[0] not in (STATE,TUNNEL) and x[-1]<0 for x in row[p[-1]+1:i])
# Authoring exposes the exact original exception, not a generic floor rule.
right=next(t for t in j['types'] if t['name']=='rightQuarterTurn5Tiles')
for d in range(4):
 row=j['rows'][right['first']+3*4+d][1]
 floors=[p for p in row if p[0]<0x7ffff and p[10]==1]
 assert bool(floors)==(d!=2)
# Station planks attach to the final support; rails start a separate root.
for typ in ('beginStation','middleStation','endStation'):
 t=next(t for t in j['types'] if t['name']==typ)
 for d in range(4):
  row=j['rows'][t['first']+d][1]
  assert row[1][-1]==0 and row[0][0]==SUPPORT
  assert row[2][0] in (23403,23404) and row[2][-1]==-1
# Directions without diagonal rails may still own an independently visible floor.
t=next(t for t in j['types'] if t['name']=='diagFlat')
assert any(any(p[10]==1 and p[0]<0x7ffff for p in j['rows'][t['first']+i][1]) and
 not any(23437<=p[0]<=23440 for p in j['rows'][t['first']+i][1]) for i in range(16))
# Independent direct source return vs cursor-derived scalar, including signed16
# wrap, type A/B flat-cap distinction, transition overwrite and impossible slopes.
def setup(slope,height,general,type_b,transition,subtype):
 z=((general+15)&~15)&65535;length=(height-z)&65535
 if length>=32768:length-=65536
 if length<0:return False
 steps=length//16;has=False
 if slope&32:has=not(type_b and steps==0)
 elif slope&16:
  steps-=2
  if steps<0:return False
  has=True
 elif slope&15:
  steps-=1
  if steps<0:return False
  has=True
 while steps>0:steps-=1;has=True
 return subtype<2 if transition!=255 else has

def scalar(slope,height,general,type_b,transition,subtype):
 z=((general+15)&~15)&65535;n=(height-z)&65535;n=n-65536 if n>=32768 else n
 if n<0:return False
 steps=n//16;phase=4
 if slope&32:phase=4 if type_b and steps==0 else 0
 elif slope&16:steps-=2;phase=1
 elif slope&15:steps-=1;phase=3
 if steps<0:return False
 return subtype<2 if transition!=255 else phase<4 or steps>0
cases=0
for slope,height,general,type_b,transition,subtype in itertools.product(range(64),[-8,0,8,16,24,32,48,128,32767,32768,65535],[-16,0,8,16,32,65520],range(2),[255,12,13,14],range(6)):
 assert setup(slope,height,general,type_b,transition,subtype)==scalar(slope,height,general,type_b,transition,subtype)
 cases+=1
# Full state-to-table addressing and compact binary round trip.
s=(OUTPUT/'src/openrct2-renderer/gpu/NativeRailwayData.h').read_text();body=s.split('kNativeRailwayWords[] = {')[1].split('};')[0]
assert not re.search(r'-\d+u',s), 'unsigned C++ words must not use unary minus'
w=[int(n) for n in re.findall(r'(\d+)u',body)]
for t in j['types']:
 assert w[w[3]+t['type']*2:w[3]+t['type']*2+2]==[t['first'],t['sequences']]
 for seq,d in itertools.product(range(t['sequences']),range(4)):
  row=w[4]+(t['first']+seq*4+d)*5
  for supported in range(2):
   first,n=w[row+supported*2:row+supported*2+2]
   data=[w[w[5]+(first+i)*12:w[5]+(first+i+1)*12] for i in range(n)]
   assert data==[[v&0xffffffff for v in p] for p in j['rows'][t['first']+seq*4+d][supported]]
report=dict(types=27,rows=352,variants=704,maxRecords=max(len(v) for pair in j['rows'] for v in pair),woodenReturnCases=cases,tableWords=len(w),status='passed',gpuValidation='not run; parent owns compiler/device')
(P/'offline-test-results.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))

# A failed or culled authored parent cannot alias an earlier element's root.
def logical_owner(parent, visible, prior):
 if parent:return 31 if visible else 0xffffffff
 return prior
assert logical_owner(True,False,7)==0xffffffff
assert logical_owner(True,True,7)==31
assert logical_owner(False,True,31)==31
emit=(OUTPUT/'data/shaders/vulkan/world_railway_emit.glsl').read_text()
assert 'if(p.parent>=0 && owner==0xffffffffu) return;' in emit
assert 'owner=count>before?destination+before:0xffffffffu' in emit
assert 'worldReportComponentFailure(256u' in emit and 'worldReportComponentFailure(512u' in emit
catalog=(OUTPUT/'src/openrct2-renderer/gpu/GpuWorldTrackCatalog.h').read_text()
assert 'ValidateWorldRailwayCatalog(words);' in catalog.split('inline void ValidateWorldTrackCatalog')[1].split('template<typename AppendImage>')[0]
