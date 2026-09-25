// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Include after makeRecord/emitObjectSprite and the scene constants.
#ifndef OPENRCT2_WORLD_PROP_EMIT
#define OPENRCT2_WORLD_PROP_EMIT
#include "world_prop_rules.glsl"
layout(std430,set=0,binding=12) readonly buffer WorldObjects { WorldObjectRecord records[]; } uObjects;
layout(std430,set=0,binding=13) readonly buffer PropCatalog { uint words[]; } uProps;
#include "world_large_text_emit.glsl"

uint worldObjectPalette(uint colours,int count,bool ghost)
{
    if(ghost) return uCatalog.reserved;
    uint primary=(colours&255u)+1u;
    uint secondary=((colours>>8u)&255u)+1u;
    uint tertiary=((colours>>16u)&255u)+1u;
    return primary | (secondary<<8u) | (tertiary<<16u);
}
uint worldPropRemapColours(WorldObjectRecord object,uint flags)
{
    bool primary=false,secondary=false,tertiary=false;
    if(object.kind==0u) {
        primary=(flags&(1u<<10u))!=0u;
        secondary=primary && (flags&(1u<<19u))!=0u;
        tertiary=(flags&(1u<<29u))!=0u;
    } else if(object.kind==1u) {
        primary=(flags&1u)!=0u; secondary=(flags&2u)!=0u; tertiary=(flags&(1u<<6u))!=0u;
    } else if(object.kind==2u) {
        primary=(flags&1u)!=0u; secondary=(flags&(1u<<6u))!=0u; tertiary=(flags&(1u<<7u))!=0u;
    } else primary=true;
    uint mask=(primary?255u:0u)|(secondary?0xff00u:0u)|(tertiary?0xff0000u:0u);
    return object.colours&mask;
}
void visitProp(uint index,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    WorldObjectRecord object=uObjects.records[index];
    if(object.kind>=4u || (object.flags&2u)!=0u || object.objectSlot>=uProps.words[4u+object.kind]) return;
    if(object.kind<2u && (uScene.viewFlags&(1u<<18))!=0u) return;
    uint material=uProps.words[object.kind]+object.objectSlot*16u;
    uint spriteBase=uProps.words[material],spriteCount=uProps.words[material+1u];
    if(spriteCount==0u) return;
    uint flags=uProps.words[material+2u];
    int direction=int((object.direction+uScene.rotation)&3u);
    int ticks=int(uScene.sourceTick&65535u);
    WorldPropParts parts=worldEmptyPropParts();
    if(object.kind==0u) {
        ivec2 position=ivec2(tile*32u);
        if(uScene.rotation==1u) position.x+=32;
        else if(uScene.rotation==2u) position+=ivec2(32);
        else if(uScene.rotation==3u) position.y+=32;
        int phase=position.x/4+position.y/4;
        uint frame=uScene.sourceTick;
        if((flags&(1u<<22u))==0u) frame+=uint(phase)+(((object.data0>>8u)&255u)<<2u);
        uint delay=uProps.words[material+5u]&255u;
        frame=(delay<32u?frame>>delay:0u)&uProps.words[material+6u];
        uint frameOffset=frame<uProps.words[material+8u]?uProps.words[uProps.words[material+7u]+frame]:0u;
        parts=worldSmallPropParts(int(flags),int(uProps.words[material+4u]),direction,
            int((((object.data0>>8u)&255u)+uScene.rotation)&3u),int(object.data0&255u),
            ticks,phase,int(frameOffset),uScene.zoom,int(uScene.clockMinute),int(uScene.clockHour));
    } else if(object.kind==1u) {
        if(object.sequence>=uProps.words[material+10u]) return;
        uint tileMetadata=uProps.words[material+9u]+object.sequence*4u;
        parts=worldLargePropParts(int(flags),int(object.sequence),direction,int(uProps.words[tileMetadata]),
            int(uProps.words[tileMetadata+1u]),int(uProps.words[tileMetadata+2u]));
    } else if(object.kind==2u) {
        parts=worldWallPropParts(int(flags),int(uProps.words[material+3u]),int(uProps.words[material+4u]),
            direction,int((object.data0>>16u)&255u),int(object.data1&255u),(object.flags&16u)!=0u,ticks);
    } else {
        // Banner direction combines element direction and its edge position.
        parts=worldBannerPropParts((direction+int(object.data0>>24u))&3,uScene.zoom);
    }
    bool ghost=(object.flags&1u)!=0u;
    uint colours=worldPropRemapColours(object,flags);
    // Parent order is local to this object's finite recipe. Children stay directly after their parent.
    int recipe[12]; int parentCount=0;
    for(int i=0;i<parts.count;i++) {
        if(parts.parts[i].colourMode==4 && ghost) continue;
        if(parts.parts[i].child!=0 && parentCount!=0) continue;
        recipe[parentCount++]=i;
    }
    for(int ordinal=0;ordinal<parentCount;ordinal++) {
        int selected=ordinal;
        int begin=recipe[selected];
        int end=begin+1;
        while(end<parts.count && parts.parts[end].child!=0) end++;
        for(int i=begin;i<end;i++) {
            WorldPropPart part=parts.parts[i];
            if(part.imageOffset<0 || uint(part.imageOffset)>=spriteCount || (part.colourMode==4 && ghost)) continue;
            uint effects=ghost?1u:uint(part.colourMode);
            uint palettes=worldObjectPalette(colours,part.colourMode,ghost);
            if(part.colourMode==4) {
                effects=1024u;
                palettes=uProps.words[8u]+(object.kind==0u?(object.colours&255u):(colours&255u));
            }
            worldSetPaintBounds(tile,ivec3(part.boundsX,part.boundsY,object.baseZ+part.boundsZ),
                ivec3(part.sizeX,part.sizeY,part.sizeZ),i==begin?0u:1u);
            worldSetCoplanarSurfaceLayer();
            // Banner posts/front panel have separate authored anchors although
            // both bitmaps are positioned at the tile origin, sixteen units low.
            if(object.kind==3u)
                worldSetComponentDepthAnchor(tile,ivec3(part.boundsX,part.boundsY,object.baseZ+part.boundsZ));
            emitObjectSprite(tile,object.baseZ+part.z,ivec2(part.x,part.y),spriteBase+uint(part.imageOffset),
                palettes,effects,destination,writeRecords,count);
        }
    }
    if(object.kind==3u && parts.count!=0)
        worldEmitOrdinaryBannerText(tile,object.baseZ,(direction+int(object.data0>>24u))&3,ghost,
            object.reserved>>16u,uProps.words[material+11u],destination,writeRecords,count);
    if(object.kind==1u && parts.count!=0)
        worldEmitLargeObjectText(object,tile,material,direction,destination,writeRecords,count);
    if((object.kind==1u || object.kind==2u) && parts.count!=0) {
        int mode=worldPropScrollingMode(int(object.kind),int(flags),int(object.sequence),direction,
            int(uProps.words[material+11u]),uScene.zoom);
        if(mode>=0) {
            WorldPropPart body=parts.parts[0];
            worldEmitColouredBannerText(tile,object.baseZ+(object.kind==1u?25:8),
                ivec3(body.boundsX,body.boundsY,object.baseZ+body.boundsZ),object.reserved>>16u,uint(mode),
                ghost?1u:((object.colours>>8u)&255u),direction!=0,destination,writeRecords,count);
        }
    }
    if(object.kind==0u)
        worldSupportSmallScenery(worldSupportState,object.baseZ,int(uProps.words[material+4u]),int(flags),
            int((object.data0>>8u)&255u),int(uScene.rotation));
    else if(object.kind==1u) {
        uint supportFlags=uProps.words[uProps.words[material+9u]+object.sequence*4u+3u];
        worldSupportLargeScenery(worldSupportState,object.clearanceZ,(supportFlags&1u)!=0u,(supportFlags&2u)!=0u);
    }
}
#endif
