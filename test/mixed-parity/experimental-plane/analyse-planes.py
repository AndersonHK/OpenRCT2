"""Pure artifact depth-plane analysis. Never launches compiler/game/test/GPU."""
import argparse,hashlib,json,struct
from pathlib import Path
from collections import Counter
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--corpus',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();root=args.corpus.resolve(strict=True);base=args.output.resolve();base.mkdir(parents=True,exist_ok=True)
output=base/'mixed-depth-plane-predictions';output.mkdir(exist_ok=True)
manifest=json.loads((root/'corpus.json').read_text())
structural=json.loads((base/'mixed-structural-review.json').read_text())
details={c['name']:c for c in structural['cases']}
statics=list(struct.iter_unpack('<3i5I',(root/'statics.bin').read_bytes()))
definitions=(root/'definitions.bin').read_bytes();remap=(root/'remap.bin').read_bytes()
assets={a['image']:{**a,'pixels':bytes.fromhex(a['decodedIndexedHex'])} for a in manifest['assets']}
def key(c):return c['image'],tuple(c['screen']),tuple(c['bounds'])
def rot(x,y,r):return ((x,y),(y,-x),(-x,-y),(-y,x))[r]
def pixels(k,v,target,left=None,right=None):
    image,(x,y),bounds=k
    if image not in assets:return
    a=assets[image];tx,ty,w,h=target;x+=a['xOffset'];y+=a['yOffset']
    count=v.get('remapCount',v.get('effects',0)&3)
    primary=v['primary']+1 if 'primary' in v else v.get('palettes',0)&255
    secondary=v['secondary']+1 if 'secondary' in v else (v.get('palettes',0)>>8)&255
    for py in range(max(ty,y),min(ty+h,y+a['height'])):
        for px in range(max(tx,x,left if left is not None else tx),min(tx+w,x+a['width'],right if right is not None else tx+w)):
            value=a['pixels'][(py-y)*a['width']+px-x]
            if not value:continue
            if count>=2 and 0xca<=value<0xd6:value=remap[secondary*256+value+0x29]
            elif count:value=remap[primary*256+value]
            if value:yield (py-ty)*w+px-tx,px,py,value
results=[]
for case in manifest['cases']:
    name=case['name'];r=case['rotation'];target=case['target'];frozen=(root/case['expected']).read_bytes()
    trace=json.loads((root/(name+'.paint.json')).read_text())
    raw=list(struct.iter_unpack('<24I',(root/case['peeps']).read_bytes()))
    nonterrain={tuple([c['component'][0],tuple(c['component'][1]),tuple(c['component'][2])]):c['rule'] for c in details[name]['nonTerrain']}
    observed={key(c) for column in trace['columns'] for parent in column['orderedParents'] for c in parent}
    components={}
    for k in observed:
        if k in nonterrain:v=dict(nonterrain[k])
        else:
            x=k[2][0]-(32 if r in (1,2) else 0);y=k[2][1]-(32 if r in (2,3) else 0)
            owner=(y//32-9)*14+(x//32-9)
            v={'family':'terrain','owner':owner,'part':0,'palettes':0,'effects':0}
        owner=v.get('owner',202+v.get('raw',0));v['owner']=owner
        if v['family'] in ('peep','accessory'):
            ax,ay,az=raw[v['raw']][:3];v['plane']='vertical';v['anchor']=(ax,ay,az)
        elif v['family']=='terrain':
            v['plane']='horizontal';v['height']=32
            x=k[2][0]-(32 if r in (1,2) else 0);y=k[2][1]-(32 if r in (2,3) else 0)
            v['footprint']=(x,y,x+32,y+32)
        else:
            sx,sy,sz,_,definition,*_=statics[owner]
            offset=(definition+r)*272+16+v['part']*64
            ox,oy,oz=struct.unpack_from('<3i',definitions,offset+16)
            if v['family']=='path' or (owner==201 and v['part']==0):
                v['plane']='horizontal';v['height']=sz+oz
                bx,by,bz,wx,wy=struct.unpack_from('<5i',definitions,offset+32)
                dx,dy=rot(bx,by,(3*r)&3);ex,ey=rot(wx,wy,(3*r)&3)
                x=sx+(32 if r in (1,2) else 0)+dx;y=sy+(32 if r in (2,3) else 0)+dy
                v['footprint']=(min(x,x+ex),min(y,y+ey),max(x,x+ex),max(y,y+ey))
            else:
                dx,dy=rot(ox,oy,(3*r)&3)
                v['plane']='vertical';v['anchor']=(sx+(32 if r in (1,2) else 0)+dx,sy+(32 if r in (2,3) else 0)+dy,sz+oz)
        v['covered']=list(pixels(k,v,target));components[k]=v
    oracle=bytearray(len(frozen));oracle_tags=[None]*len(frozen)
    def tag(k,v):return (v['family'],v['owner'],v['part'],k[0])
    for column in trace['columns']:
        for parent in column['orderedParents']:
            for part in parent:
                k=key(part);v=components[k]
                for index,px,py,value in pixels(k,v,target,column['column'],column['column']+32):oracle[index]=value;oracle_tags[index]=tag(k,v)
    if oracle!=frozen:raise RuntimeError('Original order replay changed: '+name)
    candidates={}
    for snapped in (False,True):
        for centre,envelope in ((c,e) for c in (False,True) for e in ('affine','floor','compatibility-slab','finite-support','bound-surface-finite-support','contact-footprint')):
            label=('snapped' if snapped else 'physical')+('-centre' if centre else '-integer')+'-'+envelope
            canvas=bytearray(len(frozen));depths=[None]*len(frozen);tags=[None]*len(frozen);winners=[None]*len(frozen)
            for k,v in components.items():
                tie=v['owner']*4+v['part']
                if v['plane']=='vertical':
                    ax,ay,az=v['anchor'];rx,ry=rot(ax,ay,r);s=rx+ry
                    if snapped:s=2*(s//2)
                for index,px,py,value in v['covered']:
                    # Four times D avoids fractions. D = X+Y+Z and projected
                    # ordinate y = (X+Y)/2-Z. All planes use the same D.
                    ordinate=4*py+(2 if centre else 0)
                    horizontal_height=k[2][2] if envelope=='bound-surface-finite-support' else v.get('height')
                    d=2*ordinate+12*horizontal_height if v['plane']=='horizontal' else 6*s-ordinate
                    if v['plane']=='vertical' and envelope!='affine':
                        d=max(d,2*ordinate+12*v['anchor'][2])
                        if envelope=='compatibility-slab' or (envelope in ('finite-support','bound-surface-finite-support','contact-footprint') and v['owner']==201):
                            # Original inclusive zEnd converted to the final
                            # occupied unit's centre; this is an explicit
                            # compatibility-box hypothesis, not art metadata.
                            ceiling=max(k[2][2],k[2][5])+0.5
                            d=min(d,2*ordinate+12*ceiling)
                    depth=(d,tie)
                    wins=depths[index] is None or depth>depths[index]
                    if envelope=='contact-footprint' and depths[index] is not None and d==depths[index][0]:
                        prior=components[winners[index]]
                        if v['plane']!=prior['plane']:
                            vertical,horizontal=(v,prior) if v['plane']=='vertical' else (prior,v)
                            # This is a pairwise contact relation, not a fitted
                            # scalar: an upright contact belongs above a surface
                            # only when its anchor is inside that surface.
                            if d==2*ordinate+12*vertical['anchor'][2]:
                                ax,ay,_=vertical['anchor'];l,t,rr,b=horizontal['footprint']
                                inside=l<=ax<rr and t<=ay<b
                                wins=inside if v['plane']=='vertical' else not inside
                    if wins:canvas[index]=value;depths[index]=depth;tags[index]=tag(k,v);winners[index]=k
            path=output/(name+'.'+label+'.indexed.bin');path.write_bytes(canvas)
            pairs=Counter((oracle_tags[i],tags[i]) for i in range(len(frozen)) if frozen[i]!=canvas[i])
            samples=[]
            if label=='physical-centre-finite-support':
                for index in range(len(frozen)):
                    if frozen[index]==canvas[index]:continue
                    px=index%target[2]+target[0];py=index//target[2]+target[1]
                    source=next((v for k,v in components.items() if tag(k,v)==oracle_tags[index]),None)
                    source_depth=2*(4*py+2)+12*source['height'] if source and source['plane']=='horizontal' else None
                    samples.append({'screen':[px,py],'oracle':oracle_tags[index],'candidate':tags[index],
                                    'candidateDepthTimes4':depths[index][0],'oracleHorizontalDepthTimes4':source_depth,
                                    'depthTie':depths[index][0]==source_depth})
            candidates[label]={'differingPixels':sum(pairs.values()),'sha256':hashlib.sha256(canvas).hexdigest(),
                               'pairs':[{'oracle':a,'plane':b,'pixels':n} for (a,b),n in pairs.most_common()],
                               'tieSamples':samples}
    results.append({'name':name,'originalReplayDifferingPixels':0,'candidates':candidates})
report={'scope':'Artifact-only affine planes; not GPU parity or runtime admission. No fitted depth offsets.',
        'equations':{'projection':'u=Y-X; v=(X+Y)/2-Z','commonDepth':'D=X+Y+Z',
                     'horizontal':'Z=h => D=2*v+3*h','vertical':'X+Y=s => D=1.5*s-v'},
        'corpusSha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.iterdir()) if p.is_file()},
        'parserSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'cases':results}
(base/'mixed-depth-plane-review.json').write_text(json.dumps(report,indent=2)+'\n')
for method in results[0]['candidates']:
    print(method,[c['candidates'][method]['differingPixels'] for c in results if c['name'].endswith('legacy')])
for c in results:
    if c['name'].endswith('legacy'):print(c['name'],json.dumps(c['candidates']['physical-integer-finite-support']['pairs']))
