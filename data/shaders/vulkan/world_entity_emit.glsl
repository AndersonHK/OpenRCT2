// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "peep_rules.glsl"
#include "world_component_depth.glsl"
layout(std430,set=0,binding=21) readonly buffer PeepFieldsBuffer { uint words[]; } uPeepFields;
layout(std430,set=0,binding=26) readonly buffer PeepCatalogBuffer { uint words[]; } uPeepCatalog;
struct WorldBalloon { int x; int y; int z; uint id; uint generation; uint frame; uint popped; uint colour; uint width; uint heightMin; uint heightMax; uint present; };
layout(std430,set=0,binding=25) readonly buffer WorldBalloons { WorldBalloon records[]; } uBalloons;
const uint WORLD_ENTITY_CAPACITY=65535u;

bool worldEntityClip(ivec3 position,inout uint palettes,inout uint effects)
{
    if((uScene.viewFlags&(1u<<15))==0u) return true;
    ivec2 first=ivec2(uSelection.words[12],uSelection.words[13]);
    ivec2 last=ivec2(uSelection.words[14],uSelection.words[15])+ivec2(31);
    if(any(lessThan(position.xy,first)) || any(greaterThan(position.xy,last))) return false;
    if(position.z>int(uSelection.words[11])) {
        if((uScene.viewFlags&(1u<<17))==0u) return false;
        palettes=uCatalog.viewPalettes.x;effects=1u<<10;
    }
    return true;
}

void worldEmitEntitySpriteAt(ivec3 position,ivec3 clipPosition,uint spriteIndex,uint palettes,uint effects,uint layer,
    uint destination,bool writeRecords,inout uint count)
{
    // EntityPaintSetup uses the technical map domain, not the saved park extent;
    // flying ducks and particles can legitimately be outside the latter.
    if(position.x<0 || position.y<0 || position.x>=32032 || position.y>=32032) return;
    if(!worldEntityClip(clipPosition,palettes,effects)) return;
    if(spriteIndex>=uScene.spriteSetCount) return;
    SpriteVariant sprite=uSpriteSets.records[spriteIndex].variants[clamp(uScene.zoom+2,0,5)];
    if((sprite.valid&1)==0) return;
    // Actual entity XYZ is not a tile paint origin. Cancel the tile-facing-corner
    // correction in the shared vertex projector without changing depth or raster.
    ivec3 raster=position;
    if(uScene.rotation==1u || uScene.rotation==2u) raster.x-=32;
    if(uScene.rotation==2u || uScene.rotation==3u) raster.y-=32;
    int depth=worldComponentDepth(position.x,position.y,position.z,int(uScene.rotation));
    if(!worldComponentDepthValid(depth,int(layer))) { if(writeRecords) atomicOr(uStatus.overflow,16u); return; }
    OutputRecord record=OutputRecord(raster,sprite.valid|4|16|32,sprite.spriteSize,sprite.spriteOffset,
        sprite.asset,palettes,effects,int(layer<<4u),sprite.zoom,sprite.coordinateShift,ivec2(depth,0));
    // Conservative point+sprite rectangle cull; authoritative sampling/clipping is in the vertex shader.
    ivec2 xy=position.xy;
    if(uScene.rotation==1u) xy=ivec2(position.y,-position.x);
    else if(uScene.rotation==2u) xy=-position.xy;
    else if(uScene.rotation==3u) xy=ivec2(-position.y,position.x);
    ivec2 projected=ivec2(xy.y-xy.x,((xy.x+xy.y)>>1)-position.z);
    ivec2 screen=ivec2(inverseZoom(projected.x,uScene.zoom),inverseZoom(projected.y,uScene.zoom))-uScene.view+uScene.clip.xy;
    int margin=max(sprite.spriteSize.x+abs(sprite.spriteOffset.x),sprite.spriteSize.y+abs(sprite.spriteOffset.y));
    margin=abs(inverseZoom(margin,sprite.zoom))+8;
    if(screen.x+margin<uScene.clip.x || screen.y+margin<uScene.clip.y || screen.x-margin>=uScene.clip.z || screen.y-margin>=uScene.clip.w) return;
    if(writeRecords && destination+count<uScene.outputCapacity) uOutputs.records[destination+count]=record;
    count++;
}
void worldEmitEntitySprite(ivec3 position,uint spriteIndex,uint palettes,uint effects,uint layer,
    uint destination,bool writeRecords,inout uint count)
{
    worldEmitEntitySpriteAt(position,position,spriteIndex,palettes,effects,layer,destination,writeRecords,count);
}
void visitWorldPeep(uint id,uint destination,bool writeRecords,inout uint count)
{
    if(uScene.zoom>2 || id>=WORLD_ENTITY_CAPACITY) return;
    uint life=id*2u;
    if((uPeepFields.words[life+1u]&1u)==0u) return;
    uint m=WORLD_ENTITY_CAPACITY*2u+id*10u;
    uint a=WORLD_ENTITY_CAPACITY*12u+id*4u;
    uint n=WORLD_ENTITY_CAPACITY*16u+id*9u;
    PeepRaw raw=PeepRaw(int(uPeepFields.words[m]),int(uPeepFields.words[m+1u]),int(uPeepFields.words[m+2u]),id,
        int(uPeepFields.words[m+4u]),int(uPeepFields.words[m+5u]),int(uPeepFields.words[m+6u]),uPeepFields.words[life],
        uPeepFields.words[a],uPeepFields.words[a+1u],uPeepFields.words[m+7u],uPeepFields.words[m+8u],
        uPeepFields.words[m+3u],uPeepFields.words[n],uPeepFields.words[n+1u],uPeepFields.words[n+2u],uPeepFields.words[n+3u],
        uPeepFields.words[n+4u],uPeepFields.words[a+2u],uPeepFields.words[a+3u],uPeepFields.words[n+5u],uPeepFields.words[n+6u],uPeepFields.words[n+7u],
        uPeepFields.words[life+1u]|uPeepFields.words[m+9u]|uPeepFields.words[n+8u]);
    if(raw.x==-32768 || ((raw.flags>>8u)&255u)==3u || raw.objectIndex>=uPeepCatalog.words[0]) return;
    bool staff=(raw.flags&2u)!=0u;
    if((uScene.viewFlags&(1u<<18))!=0u && (!staff || (raw.accessoryColours>>24u)!=0u)) return;
    if((uScene.viewFlags&((1u<<14)|(staff?(1u<<23):(1u<<11))))!=0u) return;
    uint d=4u+raw.objectIndex*8u;
    PeepAnimationDescriptor descriptor=PeepAnimationDescriptor(uPeepCatalog.words[d],uPeepCatalog.words[d+1u],uPeepCatalog.words[d+2u],uPeepCatalog.words[d+3u],uPeepCatalog.words[d+4u],uPeepCatalog.words[d+5u],0u,0u);
    uint fact=peepFactAddress(raw,descriptor,uPeepCatalog.words[1]);
    if(fact==0xffffffffu) return;
    uint f=4u+uPeepCatalog.words[0]*8u+fact*4u;
    PeepSelection selected=peepSelect(raw,descriptor,PeepAnimationFact(uPeepCatalog.words[f],uPeepCatalog.words[f+1u],0u,0u),uScene.rotation,uPeepCatalog.words[1]);
    if(selected.error!=0u) return;
    // Source colours are enum values; texture palette row zero is identity.
    uint palettes=(selected.parentPrimary+1u)|((selected.parentSecondary+1u)<<8u)|(selected.parentRemapCount<<24u);
    worldEmitEntitySprite(ivec3(raw.x,raw.y,raw.z),selected.parentImage,palettes,selected.parentRemapCount,0u,destination,writeRecords,count);
    if(selected.childPresent!=0u) {
        uint base=raw.animationGroup==15u?10749u:(raw.animationGroup==5u?10813u:11229u);
        uint bank=raw.animationGroup==15u?0u:(raw.animationGroup==5u?32u:64u);
        worldEmitEntitySprite(ivec3(raw.x,raw.y,raw.z),uPeepCatalog.words[2]+bank+selected.childImage-base,
            (selected.childPrimary+1u)|(1u<<24u),1u,1u,destination,writeRecords,count);
    }
}
void visitWorldBalloon(uint id,uint destination,bool writeRecords,inout uint count)
{
    if(uScene.zoom>2 || id>=WORLD_ENTITY_CAPACITY || (uScene.viewFlags&(1u<<18))!=0u) return;
    WorldBalloon b=uBalloons.records[id];
    if(b.present==0u || b.x==-32768 || (uScene.viewFlags&(1u<<14))!=0u) return;
    uint frame=(b.frame&7u)+(b.popped!=0u?8u:0u);
    if(frame>=13u) return;
    worldEmitEntitySprite(ivec3(b.x,b.y,b.z),uPeepCatalog.words[3]+frame,(b.colour+1u)|(1u<<24u),1u,0u,destination,writeRecords,count);
}
