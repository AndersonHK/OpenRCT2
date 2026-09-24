// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Original flat mechanism rules. Inputs are captured simulation fields, never elapsed render time.
#ifndef OPENRCT2_WORLD_FLAT_RIDE_ANIMATION
#define OPENRCT2_WORLD_FLAT_RIDE_ANIMATION
#include "world_flat_ride_rules.glsl"
#ifdef __cplusplus
#define POSE_FN constexpr
#else
#define POSE_FN
#endif
struct WorldFlatPose {
    int present; int onTrack; int frame; int secondary; int orientation; int restraints;
    int currentTime; int breakdownFlags; int breakdownReason; int breakdownModifier;
    int slideInUse; int slideProgress; int slideColour;
};
POSE_FN WorldFlatPose worldFlatEmptyPose()
{
    return WorldFlatPose(0,0,0,0,0,0,0,0,0,0,0,0,0);
}
POSE_FN WorldFlatPart worldFlatAnimatePart(WorldFlatPart p,int family,int direction,int rotation,WorldFlatPose pose)
{
    bool isOperating=pose.present!=0 && pose.onTrack!=0;
    int frame=isOperating?pose.frame:0;
    int signedFrame=frame>=128?frame-256:frame;
    if(family==8 && p.bank==1 && p.colour==2 && pose.present!=0) {
        p.image=(pose.frame+(((pose.orientation>>3)+rotation)<<5))&31;
        if(isOperating && (pose.breakdownFlags&12)!=0 && pose.breakdownReason==7 && pose.breakdownModifier>=128) {
            int phase=(pose.currentTime>>1)&7;
            int vibration=phase<=4?phase:8-phase;
            p.z+=vibration;p.bz+=vibration;
        }
    }
    if(family==9 && p.bank==1 && pose.present!=0) p.image+=pose.frame%8;
    if(family==10 && p.bank==1 && isOperating) p.image+=signedFrame*4;
    if(family==11 && p.bank==1 && p.colour==2 && isOperating)
        p.image=(((direction*88)%216)+((pose.orientation>>3)<<4)+frame)%216%24;
    if(family==12 && p.bank==1 && isOperating) p.image=frame*4+(((pose.orientation>>3)+rotation)&3);
    if(family==13 && p.bank==1 && isOperating && signedFrame!=0) {
        int swing=(direction&2)!=0?-signedFrame:signedFrame;
        if(swing<0) swing=-swing+9;
        p.image+=swing*18;
    }
    if(family==14 && p.bank==1 && isOperating && signedFrame!=0) {
        int swing=(direction&2)!=0?-signedFrame:signedFrame;
        if(swing<0) swing+=72;
        p.image=32+(direction&1)+(swing-1)*2;
    }
    if(family==17 && p.bank==1 && isOperating)
        p.image+=(pose.restraints>=64?(pose.restraints>>6):frame)*4;
    if(family==15 && frame<32) {
#ifdef __cplusplus
        const int z[32]={-2,-1,1,5,10,16,23,30,37,45,52,59,65,70,74,76,77,76,74,70,65,59,52,45,37,30,23,16,10,5,1,-1};
        const int xy[32]={0,6,12,18,23,27,30,31,32,31,30,27,23,18,12,6,0,-5,-11,-17,-22,-26,-29,-30,-31,-30,-29,-26,-22,-17,-11,-5};
#else
        const int z[32]=int[32](-2,-1,1,5,10,16,23,30,37,45,52,59,65,70,74,76,77,76,74,70,65,59,52,45,37,30,23,16,10,5,1,-1);
        const int xy[32]=int[32](0,6,12,18,23,27,30,31,32,31,30,27,23,18,12,6,0,-5,-11,-17,-22,-26,-29,-30,-31,-30,-29,-26,-22,-17,-11,-5);
#endif
        if(p.bank==0 && (p.image==22006+(direction&1)*32 || p.image==22070+(direction&1)*32))
            p.image+=(direction&2)!=0?(-frame)&31:frame;
        if(p.bank==1) {
            p.z+=z[frame]+2;
            if(direction==0) p.x-=xy[frame];
            if(direction==1) p.y+=xy[frame];
            if(direction==2) p.x+=xy[frame];
            if(direction==3) p.y-=xy[frame];
        }
    }
    if(family==16 && p.bank==1) {
        int arm=(direction&2)!=0 && frame!=0?48-frame:frame;
        if(p.colour==1) p.image+=arm;
        if(p.colour==2) {
            if(frame>=48) { p.image=-1;return p; }
#ifdef __cplusplus
            const int z[48]={-10,-10,-9,-7,-4,-1,2,6,11,16,21,26,31,37,42,47,52,57,61,64,67,70,72,73,73,73,72,70,67,64,61,57,52,47,42,37,31,26,21,16,11,6,2,-1,-4,-7,-9,-10};
            const int xy[48]={0,4,9,13,17,21,24,27,29,31,33,34,34,34,33,31,29,27,24,21,17,13,9,4,0,-3,-8,-12,-16,-20,-23,-26,-28,-30,-32,-33,-33,-33,-32,-30,-28,-26,-23,-20,-16,-12,-8,-3};
#else
            const int z[48]=int[48](-10,-10,-9,-7,-4,-1,2,6,11,16,21,26,31,37,42,47,52,57,61,64,67,70,72,73,73,73,72,70,67,64,61,57,52,47,42,37,31,26,21,16,11,6,2,-1,-4,-7,-9,-10);
            const int xy[48]=int[48](0,4,9,13,17,21,24,27,29,31,33,34,34,34,33,31,29,27,24,21,17,13,9,4,0,-3,-8,-12,-16,-20,-23,-26,-28,-30,-32,-33,-33,-33,-32,-30,-28,-26,-23,-20,-16,-12,-8,-3);
#endif
            p.image=pose.present!=0 && pose.restraints>=64?64+((pose.restraints-64)>>6)+direction*3
                :direction*16+(isOperating?pose.secondary:0);
            p.z+=z[frame]+10;
            if(direction==0) p.x-=xy[frame];
            if(direction==1) p.y+=xy[frame];
            if(direction==2) p.x+=xy[frame];
            if(direction==3) p.y-=xy[frame];
        }
    }
    return p;
}
// The original mechanism helper changes InteractionType to entity for these parts.
// PaintDrawStruct snaps that projected origin before zoom-linked sprite resolution.
POSE_FN bool worldFlatEntityPart(WorldFlatPart p,int family,WorldFlatPose pose)
{
    if(pose.present==0 || pose.onTrack==0) return false;
    if(p.bank==1 && (family==4 || (family>=8 && family<=17))) return true;
    if(p.bank!=0) return false;
    if(family==9) return p.image>=22150 && p.image<=22153;
    if(family==13) return p.image>=21994 && p.image<=21997;
    if(family==14) return p.image>=21998 && p.image<=22001;
    if(family==15) return p.image>=22002 && p.image<=22133;
    if(family==17) return p.image>=22154 && p.image<=22161;
    return false;
}
// At most one extra child. The body recipe keeps the parent and front/behind relationship.
POSE_FN WorldFlatPart worldFlatAnimationOverlay(WorldFlatPart p,int family,int direction,int zoom,WorldFlatPose pose)
{
    if(zoom==0 && family==4 && p.bank==1 && pose.present!=0 && pose.onTrack!=0 && pose.frame>0 && pose.frame<=18) {
        p.image=3+direction*18+pose.frame;p.child=1;return p;
    }
    if(zoom==0 && family==5 && p.bank==1 && p.image==direction*3+1 && pose.slideInUse!=0) {
        int progress=pose.slideProgress;
        if(progress>0) --progress;
        if(progress==46) --progress;
        if(progress<46) { p.image=20+direction*46+progress;p.colour=7;p.child=1;return p; }
    }
    p.image=-1;return p;
}
#undef POSE_FN
#endif
