// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Original PathBox/PathPole support stepping, shared by GPU and CPU contracts.
// One emitted component at a time: no height-dependent private sprite array.
#ifndef OPENRCT2_WORLD_PATH_SUPPORT_RULES
#define OPENRCT2_WORLD_PATH_SUPPORT_RULES
#include "world_path_rules.glsl"
#ifdef __cplusplus
#define PATH_SUPPORT_FN constexpr
#define PATH_SUPPORT_INOUT(T,N) T& N
#else
#define PATH_SUPPORT_FN
#define PATH_SUPPORT_INOUT(T,N) inout T N
#endif

PATH_SUPPORT_FN int worldPathWoodSlopeOffset(int slope)
{
    slope&=31;
    if(slope>=2 && slope<=14) return slope-1;
    if(slope==23) return 14;
    if(slope==27) return 17;
    if(slope==29) return 16;
    if(slope==30) return 15;
    return 0;
}
PATH_SUPPORT_FN int worldPathPoleSlopeOffset(int slope)
{
    slope&=31;
    if(slope<=14) return slope;
    if(slope==23) return 15;
    if(slope==27) return 16;
    if(slope==29) return 17;
    if(slope==30) return 18;
    return 0;
}
PATH_SUPPORT_FN WorldPathPart worldPathNoSupport()
{
    return worldPathPart(-1,0,0,0,0,0,0,0,0,0);
}
// These recipes exclusively own under-deck art. A side pole's raster XY
// (notably 16,28 or 28,16) must not move its footing above its own path deck.
// Keep one constant scalar for the complete sprite and retain the original
// anchor whenever it is already below the deck. Never move the deck itself.
PATH_SUPPORT_FN int worldPathSupportDepth(WorldPathPart part,int deckZ)
{
    int authored=part.x+part.y+part.z;
    return authored<deckZ?authored:deckZ-1;
}
struct WorldPathBoxCursor { int z; int steps; int phase; int orientation; int slope; int water; int transition; };
PATH_SUPPORT_FN WorldPathBoxCursor worldPathBoxBegin(int supportZ,int supportSlope,int height,int water,
    int orientation,bool sloped,int slopeDirection,bool passedSurface)
{
    WorldPathBoxCursor c;
    c.z=(supportZ+15)&~15; c.steps=(height-c.z)/16; c.phase=0;
    c.orientation=orientation!=0?24:0; c.slope=worldPathWoodSlopeOffset(supportSlope);
    c.water=water; c.transition=sloped?55+(slopeDirection&3):-1;
    if(!passedSurface || height<c.z) { c.phase=-1; return c; }
    if((supportSlope&32)!=0) c.phase=3;
    else if((supportSlope&16)!=0) { c.steps-=2; c.phase=2; }
    else if((supportSlope&15)!=0) { c.steps--; c.phase=1; }
    if(c.steps<0) c.phase=-1;
    return c;
}
PATH_SUPPORT_FN WorldPathPart worldPathBoxNext(PATH_SUPPORT_INOUT(WorldPathBoxCursor,c))
{
    if(c.phase<0) return worldPathNoSupport();
    int z=c.z;
    if(c.phase==2 || c.phase==5 || c.phase==1) {
        int image=c.orientation+c.slope+(c.phase==5?4:0);
        c.z+=16; c.phase=c.phase==2?5:0;
        return worldPathPart(image,0,0,z,0,0,z+2,32,32,11);
    }
    if(c.phase==3) {
        c.phase=0;
        return worldPathPart(48,0,0,z-2,0,0,z-2,32,32,0);
    }
    if(c.steps>0) {
        bool halfSection=(z&16)!=0 || c.steps==1 || z+16==c.water;
        int extent=halfSection?(c.steps==1?7:12):(c.steps==2?23:28);
        c.z+=halfSection?16:32; c.steps-=halfSection?1:2;
        return worldPathPart(c.orientation+(halfSection?23:22),0,0,z,0,0,z,32,32,extent);
    }
    c.phase=-1;
    if(c.transition>=0) return worldPathPart(c.transition,0,0,z,0,0,z,1,1,4);
    return worldPathNoSupport();
}

// Edge NE,SE,SW,NW -> original MetalSupportPlace side slots.
PATH_SUPPORT_FN int worldPathPolePlace(int edge)
{
    return edge==0?6:(edge==1?8:(edge==2?7:5));
}
PATH_SUPPORT_FN int worldPathPoleX(int edge) { return edge==0?4:(edge==2?28:16); }
PATH_SUPPORT_FN int worldPathPoleY(int edge) { return edge==1?28:(edge==3?4:16); }
// Bit positions are MetalSupportPlace / PaintSession.SupportSegments slots.
PATH_SUPPORT_FN int worldPathBlockedSupportSlots(int rawEdgesAndCorners,int rotatedEdges,bool queue,bool hasSupports)
{
    if(queue || (rawEdgesAndCorners!=255 && hasSupports)) return 511;
    if(rawEdgesAndCorners==255) return 480;
    int slots=16;
    for(int edge=0;edge<4;edge++)
        if((rotatedEdges&(1<<edge))!=0) slots|=1<<worldPathPolePlace(edge);
    return slots;
}
struct WorldPathPoleCursor { int z; int endZ; int phase; int ordinal; int slope; int edge; int sloped; };
PATH_SUPPORT_FN WorldPathPoleCursor worldPathPoleBegin(int supportZ,int supportSlope,int height,
    int edge,bool sloped,bool hasBase,bool passedSurface)
{
    WorldPathPoleCursor c;
    c.z=supportZ; c.endZ=height; c.ordinal=0; c.edge=edge; c.sloped=sloped?1:0;
    c.slope=worldPathPoleSlopeOffset(supportSlope);
    c.phase=hasBase && (supportSlope&32)==0 && height-supportZ>=6?0:1;
    if(!passedSurface || height<supportZ) c.phase=-1;
    return c;
}
PATH_SUPPORT_FN WorldPathPart worldPathPoleNext(PATH_SUPPORT_INOUT(WorldPathPoleCursor,c))
{
    if(c.phase<0) return worldPathNoSupport();
    int x=worldPathPoleX(c.edge),y=worldPathPoleY(c.edge),z=c.z;
    if(c.phase==0) {
        c.z+=6; c.phase=1;
        return worldPathPart(37+c.slope,x,y,z,x,y,z,0,0,5);
    }
    if(c.phase==1) {
        c.phase=2;
        int nextZ=((z+16)&~15);
        if(nextZ>c.endZ) nextZ=c.endZ;
        int length=nextZ-z;
        if(length>0) {
            c.z=nextZ;
            return worldPathPart(20+length-1,x,y,z,x,y,z,0,0,length-1);
        }
    }
    if(c.z<c.endZ) {
        int length=c.endZ-c.z;
        if(length>16) length=16;
        int image=20+length-1+((c.ordinal&3)==3 && length==16?1:0);
        c.z+=length; c.ordinal++;
        return worldPathPart(image,x,y,z,x,y,z,0,0,length-1);
    }
    c.phase=-1;
    if(c.sloped!=0) return worldPathPart(27,x,y,z,x,y,z,0,0,0);
    return worldPathNoSupport();
}
#undef PATH_SUPPORT_FN
#undef PATH_SUPPORT_INOUT
#endif
