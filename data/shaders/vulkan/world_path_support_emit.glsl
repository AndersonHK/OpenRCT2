// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Native path supports consume the same tile-local support state as track
// supports. Include after the shared support state and emitObjectSprite.
#ifndef OPENRCT2_WORLD_PATH_SUPPORT_EMIT
#define OPENRCT2_WORLD_PATH_SUPPORT_EMIT
#include "world_path_support_rules.glsl"

void worldEmitPathSupportPart(uvec2 tile,WorldPathPart part,int deckZ,PathMaterial railings,
    uint palettes,uint effects,uint destination,bool writeRecords,inout uint count)
{
    if(part.imageOffset<0) return;
    if(uint(part.imageOffset)>=railings.bridgeCount) {
        // An admitted path must retain its entire support allocation. Never
        // silently truncate a column when the immutable catalog is incomplete.
        atomicOr(uStatus.overflow,8u);return;
    }
    worldSetPaintBounds(tile,ivec3(part.boundsX,part.boundsY,part.boundsZ),
        ivec3(part.sizeX,part.sizeY,part.sizeZ),0u);
    worldSetComponentDepthAnchor(tile,ivec3(0,0,worldPathSupportDepth(part,deckZ)));
    emitObjectSprite(tile,part.z,ivec2(part.x,part.y),railings.bridgeBase+uint(part.imageOffset),
        palettes,effects,destination,writeRecords,count);
}

// Call once after the path's surface, additions, fences and sign. State changes
// also occur when support sprites are hidden, matching Paint.Path.cpp.
void worldEmitPathSupports(PathRecord path,uvec2 tile,PathMaterial railings,bool hasSupports,
    uint destination,bool writeRecords,inout uint count)
{
    bool sloped=(path.flags&1u)!=0u;
    bool queue=(path.flags&2u)!=0u;
    bool ghost=(path.flags&(1u<<4))!=0u;
    int edges=worldPathRotateMask(int(path.edgesAndCorners&15u),int(uScene.rotation));
    bool hide=(uScene.viewFlags&(1u<<3))!=0u;
    bool invisible=hide && (uScene.viewFlags&(1u<<29))!=0u;
    uint palettes=ghost?uCatalog.reserved:0u;
    uint effects=ghost?1u:0u;
    if(!ghost && railings.supportType==1u && railings.supportColour<56u) {
        palettes=railings.supportColour+1u;effects=1u;
    }
    if(hide) { palettes=uCatalog.viewPalettes.x;effects=1u<<10; }
    if(worldSupportPassedSurface() && !invisible) {
        if(railings.supportType!=1u) {
            WorldPathBoxCursor cursor=worldPathBoxBegin(worldSupportGeneralHeight(),worldSupportGeneralSlope(),
                path.baseZ,worldSupportWaterHeight(),worldPathBoxOrientation(edges),sloped,
                (int(path.slopeDirection)+int(uScene.rotation))&3,true);
            while(cursor.phase>=0) {
                WorldPathPart part=worldPathBoxNext(cursor);
                worldEmitPathSupportPart(tile,part,path.baseZ,railings,palettes,effects,destination,writeRecords,count);
            }
        } else {
            // Original NE/SE/SW/NW list is visited backwards.
            for(int edge=3;edge>=0;edge--) {
                if((edges&(1<<edge))!=0) continue;
                int place=worldPathPolePlace(edge);
                int supportZ=worldSupportSegmentHeight(place);
                if(path.baseZ<supportZ) continue;
                WorldPathPoleCursor cursor=worldPathPoleBegin(supportZ,worldSupportSegmentSlope(place),
                    path.baseZ,edge,sloped,(railings.flags&1u)!=0u,true);
                while(cursor.phase>=0) {
                    WorldPathPart part=worldPathPoleNext(cursor);
                    worldEmitPathSupportPart(tile,part,path.baseZ,railings,palettes,effects,destination,writeRecords,count);
                }
                worldSupportSetSegment(place,65535,32);
            }
        }
    }
    worldSupportRaiseGeneral(path.baseZ+32+(sloped?16:0));
    int blocked=worldPathBlockedSupportSlots(int(path.edgesAndCorners),edges,queue,hasSupports);
    for(int place=0;place<9;place++)
        if((blocked&(1<<place))!=0)
            worldSupportSetSegment(place,65535,worldSupportSegmentSlope(place));
}
#endif
