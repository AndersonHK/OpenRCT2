// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_STATIC_TOWER_MAZE_RULES
#define OPENRCT2_WORLD_STATIC_TOWER_MAZE_RULES
#include "world_flat_ride_rules.glsl"
#include "world_tower_assembly.glsl"
#ifdef __cplusplus
#define STATIC_FN constexpr
#else
#define STATIC_FN
#endif
STATIC_FN WorldFlatParts worldTowerParts(int family,int sequence,int direction,bool section,bool cap,
    bool noPlatforms,int fenceMask)
{
    WorldFlatParts r;r.count=0;
#ifdef __cplusplus
    for(int i=0;i<WORLD_FLAT_PART_CAPACITY;i++) r.parts[i]=WorldFlatPart(0,0,0,0,0,0,0,0,0,0,0,0,0,0);
#endif
    // Lift.cpp authors two independent cage parents per height segment. Keep
    // their back/front bounds separate: a vehicle can pass between the halves.
    if(family==24) {
        if(direction<0 || direction>3 || sequence<0 || sequence>=9) return r;
        if(section && sequence==1) return r;
        int s=worldFlatSequence(24,sequence,direction);
        if(section || s==0) {
            int tiers=section?1:3;
            for(int tier=0;tier<tiers;tier++) {
                int image=14994+(!section && tier==0?2+direction*2:0);
                int z=tier*32;
                worldFlatAdd(r,image,0,1,0,0,0,z,2,2,z,2,2,30);
                worldFlatAdd(r,image+1,0,1,0,0,0,z,28,28,z,2,2,30);
            }
        } else {
            // Lift flooring is unconditional in the source, including stations
            // whose object disables the ordinary tower platform artwork.
            worldFlatAdd(r,14989,0,3,0,0,0,0,0,0,0,32,32,1);
            worldFlatFences(r,worldFlatEdges(24,s,direction)&fenceMask,14990,0,1,0);
        }
        return r;
    }
    if(family<20 || family>22) return r;
    int segment=family==20?14987:(family==21?14565:14558);
    if(section) {
        if(sequence==1) return r;
        worldFlatAdd(r,segment,0,1,0,0,0,0,8,8,0,2,2,30);
        if(cap) worldFlatAdd(r,segment+1,0,1,1,0,0,0,8,8,0,2,2,30);
        for(int i=0;i<r.count;i++) r.parts[i].depthAnchor=WORLD_TOWER_SHAFT_ANCHOR;
        return r;
    }
    int s=worldFlatSequence(1,sequence,direction);
    if(s<0) return r;
    if(!noPlatforms) {
        worldFlatAdd(r,family==21?14567:14989,0,3,0,0,0,0,0,0,0,32,32,1);
        worldFlatFences(r,worldFlatEdges(1,s,direction)&fenceMask,family==21?14568:14990,0,1,0);
    }
    if(s==0) {
        int base=family==20?14986:(family==21?14564:14560+(direction&1)*2);
        int baseSegment=family==22?base+1:segment;
        int shaftFirst=r.count;
        worldFlatAdd(r,base,0,1,0,0,0,0,8,8,3,2,2,27);
        worldFlatAdd(r,baseSegment,0,1,0,0,0,32,8,8,32,2,2,30);
        worldFlatAdd(r,baseSegment,0,1,0,0,0,64,8,8,64,2,2,30);
        for(int i=shaftFirst;i<r.count;i++) r.parts[i].depthAnchor=WORLD_TOWER_SHAFT_ANCHOR;
    }
    return r;
}
// Stream one of the 26 authored maze parents instead of allocating a large
// per-invocation parts array. image=-1 means the raw wall bits omit this part.
STATIC_FN WorldFlatPart worldMazePart(int index,int rawEntry,int direction,int wallStyle)
{
    WorldFlatPart p=WorldFlatPart(-1,0,0,0,0,0,0,0,0,2,1,1,9,0);
    if(index<0 || index>=26 || direction<0 || direction>3) return p;
    if(index==0) {
        p.image=2485;p.bz=0;p.sx=32;p.sy=32;p.sz=0;
        return p;
    }
    int shift=direction*4;
    int entry=((rawEntry<<shift)|(rawEntry>>(16-shift)))&65535;
    int base=wallStyle==0?21951:(wallStyle==1?21938:(wallStyle==2?21964:(wallStyle==3?21977:0)));
    int mask=0,offset=0;
    if(index<=4) {
        int q=index-1;mask=1<<(3+q*4);
        p.x=q>=2?18:2;p.y=q==1 || q==2?18:2;
        p.bx=p.x+1;p.by=p.y+1;p.sx=10;p.sy=10;
    } else if(index<=8) {
        int q=index-5;
        mask=1<<(q==0?0:(q==1?13:(q==2?5:8)));
        offset=q<2?3:5;p.x=2+(q&1)*16;p.y=q<2?0:30;
        p.bx=p.x+1;p.by=q<2?1:30;p.sx=10;
    } else if(index<=12) {
        int q=index-9;
        mask=1<<(q==0?1:(q==1?4:(q==2?12:9)));
        offset=q<2?4:6;p.x=q<2?0:30;p.y=2+(q&1)*16;
        p.bx=q<2?1:30;p.by=p.y+1;p.sy=10;
    } else if(index<=14) {
        mask=1<<(index==13?2:10);offset=1;
        p.x=index==13?2:18;p.y=14;p.bx=p.x+1;p.by=14;p.sx=10;p.sy=4;
    } else if(index<=16) {
        mask=1<<(index==15?14:6);offset=2;
        p.x=14;p.y=index==15?2:18;p.bx=14;p.by=p.y+1;p.sx=4;p.sy=10;
    } else if(index<=20) {
        int q=index-17;mask=3<<(q*4);offset=12;
        p.x=q>=2?30:0;p.y=q==1 || q==2?30:0;
        p.bx=p.x==0?1:30;p.by=p.y==0?1:30;
    } else if(index==21 || index==22) {
        mask=index==21?((1<<0)|(1<<13)|(1<<14)):((1<<5)|(1<<6)|(1<<8));
        offset=index==21?9:11;p.x=14;p.y=index==21?0:30;
        p.bx=15;p.by=index==21?1:30;p.sx=2;
    } else if(index==23 || index==24) {
        mask=index==23?((1<<1)|(1<<2)|(1<<4)):((1<<9)|(1<<10)|(1<<12));
        offset=index==23?8:10;p.x=index==23?0:30;p.y=14;
        p.bx=index==23?1:30;p.by=15;p.sy=2;
    } else {
        mask=(1<<2)|(1<<6)|(1<<10)|(1<<14);offset=7;
        p.x=14;p.y=14;p.bx=15;p.by=15;p.sx=2;p.sy=2;p.sz=8;
    }
    if((entry&mask)!=0) p.image=base+offset;
    return p;
}
#undef STATIC_FN
#endif
