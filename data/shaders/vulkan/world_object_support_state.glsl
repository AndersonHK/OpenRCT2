// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Post-paint support ownership from SmallScenery, LargeScenery and Entrance.
// Run for admitted, visible tile elements even when their image is culled,
// ghosted, or supports are hidden. Highlight-path-issues skips these painters.
#ifndef OPENRCT2_WORLD_OBJECT_SUPPORT_STATE
#define OPENRCT2_WORLD_OBJECT_SUPPORT_STATE
#include "world_support_state.glsl"

SUPPORT_FN void worldSupportSmallScenery(SUPPORT_INOUT(WorldSupportState,s),int baseZ,int objectHeight,
    int flags,int quadrant,int rotation)
{
    int height=baseZ+objectHeight;
    worldSupportRaiseGeneralState(s,(height+7)&~7);
    bool full=(flags&1)!=0,centre=(flags&2)!=0,above=(flags&(1<<23))!=0;
    if(above){
        if(full){
            worldSupportSetSegments(s,256,height,32);
            if(centre)worldSupportSetSegments(s,255,height,32);
        }else if(centre){
            worldSupportSetSegments(s,worldSupportRotatedMask(131,(quadrant+rotation)&3),height,32);
        }
    }else if(full||(flags&(1<<27))!=0){
        worldSupportSetSegments(s,256,65535,0);
        if(centre)worldSupportSetSegments(s,255,65535,0);
    }else if(centre){
        worldSupportSetSegments(s,worldSupportRotatedMask(131,(quadrant+rotation)&3),65535,0);
    }
}
SUPPORT_FN void worldSupportLargeScenery(SUPPORT_INOUT(WorldSupportState,s),int clearanceZ,
    bool hasSupports,bool allowSupportsAbove)
{
    if(!hasSupports)return;
    // Original ceil2(clearanceZ + 15,16), deliberately not ceil2(clearanceZ,16).
    int clearanceHeight=(clearanceZ+30)&~15;
    worldSupportSetSegments(s,511,allowSupportsAbove?clearanceHeight:65535,allowSupportsAbove?32:0);
    worldSupportRaiseGeneralState(s,clearanceHeight);
}
SUPPORT_FN void worldSupportEntrance(SUPPORT_INOUT(WorldSupportState,s),int baseZ,int entranceType)
{
    worldSupportSetSegments(s,511,65535,0);
    worldSupportRaiseGeneralState(s,baseZ+(entranceType==2?80:(entranceType==1?40:56)));
}
#endif
