"""Artifact-only diagnostic: no game, compiler or device execution."""
import argparse, hashlib, json, struct
from collections import Counter
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--corpus',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();root=args.corpus.resolve(strict=True);ARTIFACTS=args.output.resolve();ARTIFACTS.mkdir(parents=True,exist_ok=True)
manifest=json.loads((root/'corpus.json').read_text())
assets={a['image']:a for a in manifest['assets']}
statics=list(struct.iter_unpack('<3i5I',(root/'statics.bin').read_bytes()))
defs=(root/'definitions.bin').read_bytes()
descs={d[0]:d for d in struct.iter_unpack('<8I',(root/'descriptors.bin').read_bytes())}
facts=list(struct.iter_unpack('<4I',(root/'facts.bin').read_bytes()))
remap=(root/'remap.bin').read_bytes()
def rot(x,y,r): return [(x,y),(y,-x),(-x,-y),(-y,x)][r]
def project(x,y,z,r):
    x,y=rot(x,y,r)
    return (y-x,((x+y)>>1)-z)
def key(p): return (p['image'],tuple(p['screen']),tuple(p['bounds']))
def visible(k,target):
    image,(x,y),_=k
    if image not in assets:return None
    a=assets[image]; tx,ty,w,h=target
    x+=a['xOffset'];y+=a['yOffset']
    return x<tx+w and x+a['width']>tx and y<ty+h and y+a['height']>ty
def draw(canvas,k,rule,target,left,right,tags=None):
    image,(x,y),_=k
    if image not in assets:return
    a=assets[image];pixels=bytes.fromhex(a['decodedIndexedHex']);tx,ty,w,h=target
    x+=a['xOffset'];y+=a['yOffset']
    count=rule.get('remapCount',rule.get('effects',0)&3)
    primary=rule.get('primary',-1)+1 if 'primary' in rule else rule.get('palettes',0)&255
    secondary=rule.get('secondary',-1)+1 if 'secondary' in rule else (rule.get('palettes',0)>>8)&255
    for py in range(max(ty,y),min(ty+h,y+a['height'])):
        for px in range(max(tx,left,x),min(tx+w,right,x+a['width'])):
            value=pixels[(py-y)*a['width']+px-x]
            if not value:continue
            if count>=2 and 0xca<=value<0xd6:value=remap[secondary*256+value+0x29]
            elif count>=1:value=remap[primary*256+value]
            if value:
                offset=(py-ty)*w+px-tx
                canvas[offset]=value
                if tags is not None:tags[offset]=(rule['family'],rule.get('owner',202+rule.get('raw',0)),rule['part'],image)
results=[]
predicted_root=ARTIFACTS/'mixed-structural-predictions'
predicted_root.mkdir(exist_ok=True)
for case in manifest['cases']:
    r=case['rotation']; target=case['target']; expected={}; details=[]
    for owner,s in enumerate(statics):
        x,y,z,identity,base,obj,gen,present=s
        off=(base+r)*272; _,_,_,count=struct.unpack_from('<4I',defs,off)
        parts=[struct.unpack_from('<4I10i2I',defs,off+16+i*64) for i in range(count)]
        for i,p in enumerate(parts):
            image,palettes,effects,parent,ox,oy,oz,attached,bx,by,bz,sx,sy,sz,_,_=p
            if parent != 0xffffffff: raise RuntimeError('Diagnostic needs attachment branch')
            tx=x+(32 if r in (1,2) else 0);ty=y+(32 if r in (2,3) else 0)
            dx,dy=rot(ox,oy,(r*3)&3); screen=project(tx+dx,ty+dy,z+oz,r)
            dx,dy=rot(bx,by,(r*3)&3); begin=(tx+dx,ty+dy,z+bz)
            if r==0:sx-=1;sy-=1
            elif r==1:sx-=1
            elif r==3:sy-=1
            dx,dy=rot(sx,sy,(r*3)&3);bounds=begin+(begin[0]+dx,begin[1]+dy,begin[2]+sz)
            k=(image,screen,bounds);expected[k]={'family':'terrain' if owner<196 else 'path' if owner<200 else 'tree' if owner==200 else 'track/support','owner':owner,'part':i,'palettes':palettes,'effects':effects}
    raw=list(struct.iter_unpack('<24I',(root/case['peeps']).read_bytes()))
    for i,p in enumerate(raw):
        x,y,z=p[:3];obj=p[8];direction=((r*8+p[12])&31)>>3
        action,group,atype,ntype,frame,colours,access=p[13:20]
        atype=ntype if action==254 else atype;frame=0 if action==254 else frame
        d=descs[obj]; f=facts[d[3]+group*37+atype]
        image=f[0]+(frame if atype==11 else direction+frame*4)
        bounds=(x,y,z+5,x-(r in (1,2)),y-(r in (2,3)),z+16)
        screen=project(x,y,z,r);k=(image,screen,bounds)
        expected[k]={'family':'peep','raw':i,'part':0,'primary':colours&255,'secondary':0 if p[23]&2 else (colours>>8)&255,'remapCount':1 if p[23]&2 else 2}
        if not p[23]&2 and action not in (11,26,8) and group in (15,5,7):
            base,colour={15:(10749,access&255),5:(10813,(access>>8)&255),7:(11229,(access>>16)&255)}[group]
            frame=6 if atype==2 else 7 if atype==7 else frame%6
            k=(base+direction+frame*4,screen,bounds)
            expected[k]={'family':'accessory','raw':i,'part':1,'primary':colour,'remapCount':1}
    trace=json.loads((root/(case['name']+'.paint.json')).read_text())
    observed={key(p) for c in trace['columns'] for parent in c['orderedParents'] for p in parent}
    missing=[{'component':k,'rule':v} for k,v in expected.items() if visible(k,target) and k not in observed]
    unexpected=[{'component':k,'assetPresent':k[0] in assets,'visible':visible(k,target)} for k in observed if k not in expected and visible(k,target) is not False]
    replay=bytearray(target[2]*target[3]);scalar=bytearray(len(replay));replay_tags=[None]*len(replay);scalar_tags=[None]*len(replay)
    for column in trace['columns']:
        for parent in column['orderedParents']:
            for p in parent:
                k=key(p)
                if k in expected:draw(replay,k,expected[k],target,column['column'],column['column']+32,replay_tags)
    def depth(item):
        k,v=item;x,y,z=k[2][:3];rx,ry=rot(x,y,r)
        owner=v.get('owner',202+v.get('raw',0))
        return (rx+ry+z+2048)*1024+owner*4+v['part']
    for k,v in sorted(expected.items(),key=depth):draw(scalar,k,v,target,target[0],target[0]+target[2],scalar_tags)
    frozen=(root/case['expected']).read_bytes()
    (predicted_root/(case['name']+'.scalar.indexed.bin')).write_bytes(scalar)
    (predicted_root/(case['name']+'.original-order.indexed.bin')).write_bytes(replay)
    replay_diff=sum(a!=b for a,b in zip(replay,frozen));scalar_diff=sum(a!=b for a,b in zip(scalar,frozen))
    pairs=Counter((replay_tags[i],scalar_tags[i]) for i in range(len(replay)) if replay[i]!=scalar[i])
    def candidate_key(item,method):
        k,v=item; image,screen,b=k; x,y,z,xe,ye,ze=b
        cx,cy,cz=(x+xe)/2,(y+ye)/2,(z+ze)/2
        rx,ry=rot(cx,cy,r); horizontal=rx+ry
        owner=v.get('owner',202+v.get('raw',0));part=v['part']
        if method=='bounds-centre-sum':score=(horizontal+cz,)
        elif method=='bounds-centre-bottom':score=(horizontal+min(z,ze),)
        elif method=='bounds-centre-height-tie':score=(horizontal,cz)
        elif method=='bounds-front-bottom':score=(horizontal+abs(xe-x)/2+abs(ye-y)/2+min(z,ze),)
        else:
            # Physical XY anchors specified by the owner's authored shape: tile
            # centre for surfaces/track, trunk for tree, axis for supports,
            # raw location for peeps. No family depth bias or fitted coefficient.
            if v['family'] in ('peep','accessory'):ax,ay,ground=raw[v['raw']][:3]
            elif v['family']=='tree' or (owner==201 and part>0):ax,ay=x,y;ground=statics[owner][2]
            else:ax,ay,ground=statics[owner][:3];ax+=16;ay+=16
            arx,ary=rot(ax,ay,r);horizontal=arx+ary
            if method=='semantic-centre-sum':score=(horizontal+cz,)
            elif method=='semantic-bottom-sum':score=(horizontal+min(z,ze),)
            elif method=='semantic-height-tie':score=(horizontal+ground,cz)
            elif method=='semantic-project-height':
                # Screen Y identifies authored image anchor height independently
                # of compatibility paint bounds. X/Y above are physical anchors.
                if owner>=202:height=raw[owner-202][2]
                else:
                    s=statics[owner];poff=(s[4]+r)*272+16+part*64
                    height=s[2]+struct.unpack_from('<i',defs,poff+24)[0]
                score=(horizontal+height,)
        return score+(owner*4+part,)
    candidates={}
    for method in ('bounds-centre-sum','bounds-centre-bottom','bounds-centre-height-tie','bounds-front-bottom',
                   'semantic-centre-sum','semantic-bottom-sum','semantic-height-tie','semantic-project-height'):
        pixels=bytearray(len(replay));tags=[None]*len(replay)
        for k,v in sorted(expected.items(),key=lambda item:candidate_key(item,method)):
            draw(pixels,k,v,target,target[0],target[0]+target[2],tags)
        conflicts=Counter((replay_tags[i],tags[i]) for i in range(len(replay)) if replay[i]!=pixels[i])
        candidates[method]={'differingPixels':sum(conflicts.values()),'pairs':[{'oracle':a,'candidate':b,'pixels':count} for (a,b),count in conflicts.most_common()]}
    # Keep all unique non-terrain component facts for independent inspection.
    details=[{'component':k,'rule':v,'seen':k in observed,'visible':visible(k,target)} for k,v in expected.items() if v['family']!='terrain']
    results.append({'name':case['name'],'uniqueOracleComponents':len(observed),'predictedVisible':sum(visible(k,target) for k in expected),
        'missing':missing,'unexpected':unexpected,'orderedArtifactReplayDifferingPixels':replay_diff,
        'scalarArtifactReplayDifferingPixels':scalar_diff,
        'scalarMismatchOwnerPairs':[{'oracle':a,'scalar':b,'pixels':count} for (a,b),count in pairs.most_common()],
        'geometricCandidates':candidates,
        'nonTerrain':details})
out=ARTIFACTS/'mixed-structural-review.json'
inputs={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.iterdir()) if p.is_file()}
out.write_text(json.dumps({'scope':'Artifact structural comparison/replay only; traces omit remap flags. No GPU claim.',
                          'corpusSha256':inputs,'parserSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'cases':results},indent=2)+'\n')
print(json.dumps([{k:c[k] for k in ('name','uniqueOracleComponents','predictedVisible','orderedArtifactReplayDifferingPixels','scalarArtifactReplayDifferingPixels')}|{'missing':len(c['missing']),'unexpected':len(c['unexpected'])} for c in results],indent=2))
for c in results:
    if c['missing'] or c['unexpected']:print(c['name'],json.dumps({'missing':c['missing'],'unexpected':c['unexpected']}))
for c in results:
    if c['name'].endswith('legacy'):print(c['name'],json.dumps(c['scalarMismatchOwnerPairs']))
for method in results[0]['geometricCandidates']:
    print(method,[c['geometricCandidates'][method]['differingPixels'] for c in results if c['name'].endswith('legacy')])
