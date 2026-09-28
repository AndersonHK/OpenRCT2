// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Paint.Surface support seeds and PaintUtil mutation semantics, shared with CPU tests.
#ifndef OPENRCT2_WORLD_SUPPORT_STATE
#define OPENRCT2_WORLD_SUPPORT_STATE
#ifdef __cplusplus
#define SUPPORT_FN constexpr
#define SUPPORT_INOUT(T,N) T& N
#define SUPPORT_TABLE_BEGIN(N,S) constexpr int N[S]={
#define SUPPORT_TABLE_END };
#else
#define SUPPORT_FN
#define SUPPORT_INOUT(T,N) inout T N
#define SUPPORT_TABLE_BEGIN(N,S) const int N[S]=int[S](
#define SUPPORT_TABLE_END );
#endif
struct WorldSupportState
{
    int heights[9]; int slopes[9];
    int generalHeight; int generalSlope; int passedSurface; int waterHeight;
};
SUPPORT_TABLE_BEGIN(worldSupportTerrainHeights,288)
    0,0,0,0,0,0,0,0,0,
    0,0,0,12,0,0,0,6,6,
    0,12,0,0,0,6,0,6,0,
    2,14,2,14,8,8,2,14,8,
    12,0,0,0,0,6,6,0,0,
    12,0,0,12,0,6,6,6,6,
    14,14,2,2,8,14,8,8,2,
    16,16,4,16,16,16,10,16,10,
    0,0,12,0,0,0,6,0,6,
    2,2,14,14,8,2,8,8,14,
    0,12,12,0,0,6,6,6,6,
    4,16,16,16,16,10,10,16,16,
    14,2,14,2,8,8,14,2,8,
    16,4,16,16,16,10,16,10,16,
    16,16,16,4,16,16,16,10,10,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    16,28,4,16,16,22,10,22,10,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    4,16,16,28,16,10,10,22,22,
    0,0,0,0,0,0,0,0,0,
    16,4,28,16,16,10,22,10,22,
    28,16,16,4,16,22,22,10,10,
    0,0,0,0,0,0,0,0,0 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldSupportTerrainSlopes,288)
    0,0,0,0,0,0,0,0,0,
    0,1,1,27,1,0,0,27,27,
    2,23,0,2,2,23,0,23,0,
    3,3,3,3,3,3,3,3,3,
    30,4,4,0,4,30,30,0,0,
    30,5,5,27,5,30,30,27,27,
    6,6,6,6,6,6,6,6,6,
    7,0,23,7,7,0,23,0,23,
    8,0,29,8,8,0,29,0,29,
    9,9,9,9,9,9,9,9,9,
    10,23,29,10,10,23,29,23,29,
    27,11,11,0,11,27,27,0,0,
    12,12,12,12,12,12,12,12,12,
    13,29,0,13,13,29,0,29,0,
    0,14,14,30,14,0,0,30,30,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    23,23,23,23,23,23,23,23,23,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    27,27,27,27,27,27,27,27,27,
    0,0,0,0,0,0,0,0,0,
    29,29,29,29,29,29,29,29,29,
    30,30,30,30,30,30,30,30,30,
    0,0,0,0,0,0,0,0,0 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldSupportTerrainGeneral,32)
    0,1,2,3,4,5,6,7,8,
    9,10,11,12,13,14,0,0,0,
    0,0,0,0,0,23,0,0,0,
    27,0,29,30,0 SUPPORT_TABLE_END
SUPPORT_FN void worldSupportInitialise(SUPPORT_INOUT(WorldSupportState,s))
{
    for(int i=0;i<9;i++){s.heights[i]=65535;s.slopes[i]=0;}
    s.generalHeight=65535;s.generalSlope=0;s.passedSurface=0;s.waterHeight=65535;
}
SUPPORT_FN void worldSupportSeedTerrain(SUPPORT_INOUT(WorldSupportState,s),int baseZ,int relativeSlope,int waterHeight)
{
    int shape=relativeSlope&31;
    for(int i=0;i<9;i++){s.heights[i]=(baseZ+worldSupportTerrainHeights[shape*9+i])&65535;s.slopes[i]=worldSupportTerrainSlopes[shape*9+i];}
    s.generalHeight=baseZ&65535;s.generalSlope=worldSupportTerrainGeneral[shape];
    s.passedSurface=1;s.waterHeight=waterHeight;
}
SUPPORT_FN int worldSupportRotatedMask(int mask,int rotation)
{
    int shift=(rotation&3)*2;
    int outer=mask&255;
    return (((outer<<shift)|(outer>>(8-shift)))&255)|(mask&256);
}
SUPPORT_FN void worldSupportSetSegments(SUPPORT_INOUT(WorldSupportState,s),int mask,int height,int slope)
{
    for(int i=0;i<9;i++){
        int bit=i==0?1:(i==1?64:(i==2?4:(i==3?16:(i==4?256:(i==5?128:(i==6?2:(i==7?32:8)))))));
        if((mask&bit)!=0){s.heights[i]=height&65535;if(height!=65535)s.slopes[i]=slope&255;}
    }
}
SUPPORT_FN void worldSupportRaiseGeneralState(SUPPORT_INOUT(WorldSupportState,s),int height)
{
    if(s.generalHeight<height){s.generalHeight=height&65535;s.generalSlope=32;}
}
#ifndef __cplusplus
WorldSupportState worldSupportState;
bool worldSupportPassedSurface(){return worldSupportState.passedSurface!=0;}
int worldSupportGeneralHeight(){return worldSupportState.generalHeight;}
int worldSupportGeneralSlope(){return worldSupportState.generalSlope;}
int worldSupportWaterHeight(){return worldSupportState.waterHeight;}
int worldSupportSegmentHeight(int slot){return worldSupportState.heights[slot];}
int worldSupportSegmentSlope(int slot){return worldSupportState.slopes[slot];}
void worldSupportSetSegment(int slot,int height,int slope){worldSupportState.heights[slot]=height;worldSupportState.slopes[slot]=slope;}
void worldSupportRaiseGeneral(int height){worldSupportRaiseGeneralState(worldSupportState,height);}
#endif
#endif
