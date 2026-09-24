// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Include after emitSprite; caller owns the visible front parent.
#ifndef OPENRCT2_WORLD_BANNER_EMIT
#define OPENRCT2_WORLD_BANNER_EMIT
#include "world_banner_text.glsl"

ivec2 worldBannerFrontAnchor(int direction)
{
    if(direction==0) return ivec2(1,29);
    if(direction==1) return ivec2(29,32);
    if(direction==2) return ivec2(32,29);
    return ivec2(29,1);
}
void worldEmitBannerText(uvec2 tile,int rasterZ,ivec3 anchor,uint descriptor,uint mode,
    uint destination,bool writeRecords,inout uint count)
{
    if(!worldBannerTextValid() || descriptor<16u || !worldBannerTextRange(descriptor,4u) || mode>=38u
        || uScene.zoom>1 || worldParentRoot==0xffffffffu) return;
    worldSetPaintBounds(tile,anchor,ivec3(1,1,21),1u);
    worldSetComponentDepthAnchor(tile,anchor);
    if(uScene.zoom>0) {
        uint image=uBannerTexts.words[9];
        if(image>=uScene.spriteSetCount) { atomicOr(uStatus.overflow,8u);return; }
        emitSprite(tile,rasterZ,ivec2(0),ivec2(0),image,destination,writeRecords,count);
        return;
    }
    OutputRecord record=OutputRecord(ivec3(ivec2(tile*32u),rasterZ),1|4|64,
        ivec2(64,40),ivec2(-32,0),descriptor,mode,WORLD_BANNER_TEXT_EFFECT,0,uScene.zoom,0,ivec2(0));
    worldCapturePaint(destination+count,0u,tile,writeRecords,record);
    if(writeRecords && destination+count<uScene.outputCapacity)
        uOutputs.records[destination+count]=record;
    count++;
}

// Paint.Banner.cpp: ordinary banners raster sixteen units below element base;
// text raster is twenty-two units above that, sharing the front pole anchor.
void worldEmitOrdinaryBannerText(uvec2 tile,int baseZ,int direction,bool ghost,uint bannerId,uint scrollingMode,
    uint destination,bool writeRecords,inout uint count)
{
    int facing=((direction+2)&3)-1;
    if(ghost || facing<0 || facing>=2 || (uScene.viewFlags&(1u<<18))!=0u) return;
    uint descriptor=worldBannerTextDescriptor(false,bannerId);
    ivec2 anchor=worldBannerFrontAnchor(direction);
    worldEmitBannerText(tile,baseZ+6,ivec3(anchor,baseZ-14),descriptor,scrollingMode+uint(facing),
        destination,writeRecords,count);
}

// Paint.Path.cpp: visible queue faces1/2; a sloped endpoint raises both pole and
// text together. Queue open/broken flags are authoritative snapshot facts.
void worldEmitQueueBannerText(PathRecord path,uvec2 tile,uint scrollingMode,uint rideFlags,
    uint destination,bool writeRecords,inout uint count)
{
    int direction=int((path.queueBannerDirection+uScene.rotation)&3u);
    int facing=direction-1;
    if((path.flags&(1u<<4))!=0u || facing<0 || facing>=2) return;
    uint descriptor=worldBannerTextDescriptor(true,path.rideId);
    if(descriptor==0u) return;
    if((rideFlags&16u)==0u || (rideFlags&8u)!=0u) descriptor=uBannerTexts.words[6];
    int height=path.baseZ+((path.flags&1u)!=0u && path.slopeDirection==path.queueBannerDirection?16:0);
    ivec2 anchor=worldBannerFrontAnchor(direction);
    worldEmitBannerText(tile,height+7,ivec3(anchor,height+2),descriptor,scrollingMode+uint(facing),
        destination,writeRecords,count);
}
#endif
