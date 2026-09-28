// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_CHAIRLIFT_STATION_RULES
#define OPENRCT2_WORLD_CHAIRLIFT_STATION_RULES
#ifdef __cplusplus
#define CHAIR_FN constexpr
#else
#define CHAIR_FN
#endif
// Source ChairliftPaintUtilMapGetTrackElementAtFromRideFuzzy uses base-height
// units of eight, and deliberately includes ghost/invisible track elements.
CHAIR_FN bool worldChairliftNeighbourMatches(int ownRide,int ownZ,int kind,int ride,int z)
{
    return kind==4 && ride==ownRide && (z==ownZ || z==ownZ-8);
}
struct WorldChairliftStationPart {
    int image,x,y,z,bx,by,bz,sx,sy,sz,child,support,cover;
};
CHAIR_FN WorldChairliftStationPart worldChairliftPart(int image,int x,int y,int z,
    int bx,int by,int bz,int sx,int sy,int sz,int child,int support,int cover)
{
    WorldChairliftStationPart p;
    p.image=image;p.x=x;p.y=y;p.z=z;p.bx=bx;p.by=by;p.bz=bz;
    p.sx=sx;p.sy=sy;p.sz=sz;p.child=child;p.support=support;p.cover=cover;return p;
}
// Twelve source-order events; cover events expand to at most two components.
// No variable-height scratch array and no inferred endpoint from station.start.
CHAIR_FN WorldChairliftStationPart worldChairliftStationPart(int event,int direction,
    bool first,bool last,bool backFence,bool frontFence,int frame)
{
    bool odd=(direction&1)!=0;
    bool nearEnd=odd?((direction==3&&first)||(direction==1&&last)):
        ((direction==0&&first)||(direction==2&&last));
    bool farEnd=odd?((direction==1&&first)||(direction==3&&last)):
        ((direction==2&&first)||(direction==0&&last));
    if(event==0 && !first && !last)
        return worldChairliftPart(20502+direction,0,0,0,odd?13:0,odd?0:13,22,odd?6:32,odd?32:6,1,0,0,-1);
    if(event==1) return worldChairliftPart(14567,0,0,0,0,0,0,32,32,1,0,1,-1);
    if(event==2 && backFence)
        return worldChairliftPart(odd?14568:14571,0,0,0,odd?2:0,odd?0:2,2,odd?1:32,odd?32:1,7,1,0,-1);
    if(event==3) return worldChairliftPart(0,0,0,0,0,0,0,0,0,0,0,0,odd?0:3);
    if(event==4 && farEnd)
        return worldChairliftPart(odd?14571:14568,0,0,0,2,2,4,odd?28:1,odd?1:28,7,1,0,-1);
    if(event==5 && frontFence)
        return worldChairliftPart(odd?14570:14569,0,0,0,odd?30:0,odd?0:30,2,odd?1:32,odd?32:1,20,0,0,-1);
    if(event==6) return worldChairliftPart(0,0,0,0,0,0,0,0,0,0,0,0,odd?2:1);
    if(event==7 && nearEnd)
        return worldChairliftPart(odd?14569:14570,0,0,0,odd?2:30,odd?30:2,4,odd?28:1,odd?1:28,27,0,0,-1);
    if(event==8 && (nearEnd||farEnd))
        return worldChairliftPart(20540+(frame&3),0,0,0,14,14,4,4,4,19,0,0,-1);
    if(event==9 && (nearEnd||farEnd))
        return worldChairliftPart(odd?(nearEnd?20547:20545):(nearEnd?20544:20546),0,0,0,14,14,4,4,4,19,1,0,-1);
    if(event==10 && !farEnd)
        return worldChairliftPart(odd?20507:20506,odd?16:0,odd?0:16,2,odd?16:1,odd?1:16,2,1,1,7,0,0,-1);
    if(event==11 && !nearEnd)
        return worldChairliftPart(odd?20507:20506,odd?16:30,odd?30:16,2,odd?16:1,odd?1:16,2,1,1,7,0,0,-1);
    return worldChairliftPart(0,0,0,0,0,0,0,0,0,0,0,0,-1);
}
#undef CHAIR_FN
#endif
