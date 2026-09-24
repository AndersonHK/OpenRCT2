// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_ENTRANCE_RULES
#define OPENRCT2_WORLD_ENTRANCE_RULES
#include "world_prop_rules.glsl"
#include "world_path_order.glsl"
#ifdef __cplusplus
#define ENTRANCE_FN constexpr
#else
#define ENTRANCE_FN
#endif
// imageOffset indexes the station's eight directional image groups; direction is applied by the GPU visitor.
ENTRANCE_FN WorldPropParts worldRideEntranceParts(int direction,bool isExit,int flags)
{
    WorldPropParts result=worldEmptyPropParts();
    int colours=(flags&2)!=0?2:((flags&1)!=0?1:0);
    int group=isExit?2:0;
    WorldPropPart back=worldPropPart(group,0,0,0,2,2,0,(direction&1)!=0?8:28,(direction&1)!=0?28:8,30,colours,0);
    result.parts[result.count++]=back;
    if((flags&4)!=0) { back.imageOffset+=4; back.colourMode=4; back.child=1; result.parts[result.count++]=back; }
    WorldPropPart front=worldPropPart(group+1,0,0,0,2,2,30,28,28,isExit?1:17,colours,0);
    result.parts[result.count++]=front;
    if((flags&4)!=0) { front.imageOffset+=4; front.colourMode=4; front.child=1; result.parts[result.count++]=front; }
    return result;
}
ENTRANCE_FN WorldPropParts worldParkEntranceParts(int direction,int sequence)
{
    WorldPropParts result=worldEmptyPropParts();
    if(sequence<0 || sequence>2) return result;
    if(sequence==0) {
        result.parts[result.count++]=worldPropPart(-1,0,0,0,0,2,0,32,28,0,0,0);
        result.parts[result.count++]=worldPropPart(direction*3,0,0,0,2,2,32,28,28,47,0,0);
    } else result.parts[result.count++]=worldPropPart(direction*3+sequence,0,0,0,3,3,0,26,26,79,0,0);
    return result;
}
// Entrances have at most two parents. Preserve quadrant prepend and the two
// possible comparator moves without allocating the general linked-list sorter.
// Children are deliberately absent: the visitor emits them with their parent.
ENTRANCE_FN int worldEntranceFirstParent(WorldPropPart a,WorldPropPart b,int rotation)
{
    if(rotation<0 || rotation>3 || a.boundsX<0 || a.boundsX>32 || a.boundsY<0 || a.boundsY>32
        || b.boundsX<0 || b.boundsX>32 || b.boundsY<0 || b.boundsY>32) return 0;
    int qa=(a.boundsX+a.boundsY)/32;
    int qb=(b.boundsX+b.boundsY)/32;
    int first=qa<qb?0:1;
    if(qa-qb>1 || qb-qa>1) return first;
    WorldPathBounds ba=worldPathOrderBounds(worldPathPart(a.imageOffset,a.x,a.y,a.z,
        a.boundsX,a.boundsY,a.boundsZ,a.sizeX,a.sizeY,a.sizeZ),rotation);
    WorldPathBounds bb=worldPathOrderBounds(worldPathPart(b.imageOffset,b.x,b.y,b.z,
        b.boundsX,b.boundsY,b.boundsZ,b.sizeX,b.sizeY,b.sizeZ),rotation);
    bool ab=worldPathOrderIntersects(ba,bb,rotation);
    bool baIntersects=worldPathOrderIntersects(bb,ba,rotation);
    // If both comparisons move a node, the second restores the initial order.
    if(first==0 ? (ab && !baIntersects) : (baIntersects && !ab)) first=1-first;
    return first;
}
#undef ENTRANCE_FN
#endif
