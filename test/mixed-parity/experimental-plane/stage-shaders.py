"""Generate diagnostic overlay text only; never compiles or launches a device."""
import hashlib,json
from pathlib import Path
out=Path(__file__).resolve().parent
root=next(p for p in Path(__file__).resolve().parents if (p / 'scripts/rendering/run-mixed-parity.py').is_file() and (p / 'src/openrct2').is_dir())
source=root/'data/shaders/vulkan'
inputs={}
def read(name):
    p=(root/'test/mixed-parity/shaders'/name) if name=='mixed_fixture_emit.comp' else source/name;inputs[name]=hashlib.sha256(p.read_bytes()).hexdigest();return p.read_text()
def replace(text,old,new):
    if text.count(old)!=1:raise RuntimeError('Source drift: '+old[:70])
    return text.replace(old,new)
compute=read('mixed_fixture_emit.comp')
compute=replace(compute,'// Rejected initial scalar-depth hypothesis, retained for diagnostic comparisons.',
                '// Diagnostic finite plane envelope, derived from authored component geometry.')
start=compute.index('// Experimental common scalar')
end=compute.index('void emit(',start)
compute=compute[:start]+'''// DIAGNOSTIC OVERLAY ONLY: physical-centre-finite-support, zoom0.
// The normal60-byte SpriteCommand remains unchanged. Its unused high bytes
// carry semantic plane facts consumed only by the paired diagnostic shaders.
int planePayload(ivec3 anchor,uint owner,uint layer,uint kind,uint span,inout uint palettes,inout uint effects) {
    int relativeSum=terrainRotateXY(anchor.xy,camera.rotation).x
        +terrainRotateXY(anchor.xy,camera.rotation).y-2*(camera.y-camera.clipY);
    if(relativeSum < -1024 || relativeSum > 1023 || anchor.z<0 || anchor.z>127
        || kind>2u || span>63u || (kind==2u && (span==0u || anchor.z+int(span)>128))) {
        fail(4u); return 0;
    }
    palettes=(palettes&0x00ffffffu)|(uint(anchor.z)<<24u);
    effects=(effects&0x00ffffffu)|(kind<<24u)|(span<<26u);
    return (relativeSum+2048)*1024+int(owner*4u+layer);
}
''' +compute[end:]
compute=replace(compute,'ivec3 depthAnchor,uint palettes,uint effects) {','ivec3 semanticAnchor,uint planeKind,uint planeSpan,uint palettes,uint effects) {')
compute=replace(compute,'int depth=scalarDepth(depthAnchor,owner,part);','int depth=planePayload(semanticAnchor,owner,part,planeKind,planeSpan,palettes,effects);')
compute=replace(compute,'camera.height<=4096','camera.height<=256 && camera.width<=256 && camera.clipX==0 && camera.clipY==0')
compute=replace(compute,'        emit(owner,part,p.image,pos,begin,p.palettes,p.effects);','''        // Geometry-derived classification, bounded to this finite corpus:
        // broad flat boxes are horizontal surfaces; zero-width columns are
        // finite vertical segments; other upright authored parts are billboards.
        if(p.parentPart!=0xffffffffu) { fail(1u); return; }
        bool horizontal=anchor.sx>0 && anchor.sy>0 && anchor.sz<=min(anchor.sx,anchor.sy);
        bool column=anchor.sx==0 && anchor.sy==0 && anchor.sz>=0;
        uint kind=horizontal?0u:(column?2u:1u);
        uint span=column?uint(anchor.sz+1):0u;
        ivec2 semanticXY=terrainPaintTileOrigin(ivec2(raw.x,raw.y),camera.rotation)
            +terrainRotateXY(ivec2(anchor.ox,anchor.oy),(camera.rotation*3u)&3u);
        emit(owner,part,p.image,pos,ivec3(semanticXY,raw.z+anchor.oz),kind,span,p.palettes,p.effects);''')
compute=replace(compute,'ivec3(p.x,p.y,p.z),palette,s.parentRemapCount);','ivec3(raw.x,raw.y,raw.z),1u,0u,palette,s.parentRemapCount);')
compute=replace(compute,'ivec3(p.x,p.y,p.z),\n        (s.childPrimary+1u)','ivec3(raw.x,raw.y,raw.z),1u,0u,\n        (s.childPrimary+1u)')
(out/'mixed_fixture_emit.comp').write_text(compute)
for name in ('indexed_sprite.vert','indexed_rect.vert'):
    text=read(name)
    text=replace(text,'layout(location = 8) flat out int fTexMaskAtlas;','''layout(location = 8) flat out int fTexMaskAtlas;
// relative anchor sum, authored baseZ, plane kind/span, stable owner/part tie.
layout(location = 9) flat out ivec4 fPlane;''')
    if name=='indexed_sprite.vert':
        text=replace(text,'float depth = 1.0 - (float(vDepth) + 1.0) * DEPTH_INCREMENT;','float depth = 0.5; // Fragment diagnostic writes the geometric envelope depth.')
        text=replace(text,'    fPosition = vBounds.xy;','''    fPlane = ivec4((vDepth >> 10) - 2048, int(vPalettes >> 24u), int(vEffects >> 24u), vDepth & 1023);
    fPosition = vBounds.xy;''')
    else:
        text=replace(text,'    fPosition = vBounds.xy;','''    fPlane = ivec4(0, 0, 3, 0); // Ordinary rect pipeline is created but unused by this fixture.
    fPosition = vBounds.xy;''')
    (out/name).write_text(text)
fragment=read('indexed_rect.frag')
fragment=replace(fragment,'layout(location = 8) flat in int fTexMaskAtlas;','layout(location = 8) flat in int fTexMaskAtlas;\nlayout(location = 9) flat in ivec4 fPlane;')
fragment=replace(fragment,'    oColour = texel;','''    int kind=fPlane.z&3;
    if(kind==3) gl_FragDepth=gl_FragCoord.z;
    else {
        // D=X+Y+Z; subtracting the camera's2*worldY from all D preserves order.
        // Four times D keeps pixel centres and inclusive endpoint half-units exact.
        int ordinate=4*int(floor(gl_FragCoord.y))+2;
        int floorDepth=2*ordinate+12*fPlane.y;
        int d4=kind==0?floorDepth:max(6*fPlane.x-ordinate,floorDepth);
        if(kind==2) {
            int span=fPlane.z>>2;
            int ceilingDepth=2*ordinate+12*(fPlane.y+span)-6;
            d4=min(d4,ceilingDepth);
        }
        // Emitter gates viewport<=256, baseZ<=127, sum[-1024,1023]. With
        // floor/ceiling clamps these keep d4 within[-8192,8191]. 24-bit
        // integer encoding is exact in float/D32; tie never outranks1/4 world unit.
        int encoded=(d4+8192)*1024+fPlane.w;
        gl_FragDepth=1.0-(float(encoded)+1.0)*(1.0/16777216.0);
    }
    oColour = texel;''')
(out/'indexed_rect.frag').write_text(fragment)
(out/'overlay-sources.json').write_text(json.dumps({'schema':1,'model':'plane-envelope-v1',
    'baseShaderSha256':inputs,'overlaySha256':{n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in inputs},
    'generatorSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},indent=2)+'\n')
print('Staged4 diagnostic shader sources; no compilation performed.')
