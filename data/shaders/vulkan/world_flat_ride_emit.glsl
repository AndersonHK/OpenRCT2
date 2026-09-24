// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Included after raw object declarations, emitObjectSprite and world_path_order.
#ifndef OPENRCT2_WORLD_FLAT_RIDE_EMIT
#define OPENRCT2_WORLD_FLAT_RIDE_EMIT
#include "world_maze_order.glsl"
#include "world_flat_ride_animation.glsl"
layout(std430,set=0,binding=15) readonly buffer FlatRideCatalog { uint words[]; } uFlatRides;
layout(std430,set=0,binding=17) readonly buffer RidePoseBuffer { uint words[]; } uRidePoses;

WorldFlatPose worldFlatReadPose(uint id,uint vehicle)
{
    WorldFlatPose pose=worldFlatEmptyPose();
    if(id>=uRidePoses.words[0] || uRidePoses.words[1]!=20u || vehicle>=4u) return pose;
    uint base=4u+id*20u,source=base+4u+vehicle*4u;
    if((uRidePoses.words[base]&1u)==0u) return pose;
    uint state=uRidePoses.words[source+2u];
    pose.present=uRidePoses.words[source]!=0xffffffffu?1:0;
    pose.onTrack=(uRidePoses.words[base]&2u)!=0u?1:0;
    pose.frame=int(state&255u);pose.secondary=int((state>>8u)&255u);
    pose.orientation=int((state>>16u)&255u);pose.restraints=int(state>>24u);
    pose.currentTime=int(uRidePoses.words[source+3u]<<16u)>>16;
    pose.breakdownFlags=int(uRidePoses.words[base]);
    pose.breakdownReason=int(uRidePoses.words[base+2u]&255u);
    pose.breakdownModifier=int((uRidePoses.words[base+2u]>>8u)&255u);
    pose.slideInUse=int(uRidePoses.words[base+3u]&255u);
    pose.slideProgress=int((uRidePoses.words[base+3u]>>8u)&255u);
    pose.slideColour=int((uRidePoses.words[base+3u]>>16u)&255u);
    return pose;
}

uint worldFlatImage(uint image)
{
    uint begin=0u,end=uFlatRides.words[5u],base=uFlatRides.words[4u];
    while(begin<end) {
        uint middle=begin+(end-begin)/2u;
        if(uFlatRides.words[base+middle*2u]<image) begin=middle+1u;
        else end=middle;
    }
    if(begin>=uFlatRides.words[5u] || uFlatRides.words[base+begin*2u]!=image) return 0xffffffffu;
    return uFlatRides.words[base+begin*2u+1u];
}
int worldFlatFenceMask(uint ride,uint stationIndex,uvec2 tile)
{
    if(stationIndex>=uFlatRides.words[ride+17u]) return 15;
    uint station=uFlatRides.words[ride+16u]+stationIndex*5u;
    uint valid=uFlatRides.words[station];
    ivec2 entrance=ivec2(uFlatRides.words[station+1u],uFlatRides.words[station+2u]);
    ivec2 exitTile=ivec2(uFlatRides.words[station+3u],uFlatRides.words[station+4u]);
    int result=15;
    for(int edge=0;edge<4;edge++) {
        ivec2 offset=edge==0?ivec2(-1,0):(edge==1?ivec2(0,1):(edge==2?ivec2(1,0):ivec2(0,-1)));
        for(uint r=0u;r<uScene.rotation;r++) offset=ivec2(-offset.y,offset.x);
        ivec2 position=ivec2(tile)+offset;
        if(((valid&1u)!=0u && position==entrance) || ((valid&2u)!=0u && position==exitTile)) result&=~(1<<edge);
    }
    return result;
}
bool visitStaticRide(uint index,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    // Old diagnostic packets bind an eight-word zero catalogue; do not touch variable data.
    if(uFlatRides.words[0u]!=0x57464c54u || uFlatRides.words[1u]!=1u || uFlatRides.words[6u]<8u) return false;
    WorldObjectRecord object=uObjects.records[index];
    if(object.kind!=4u || (object.flags&2u)!=0u) return false;
    uint id=object.rideIdAndMazeEntry&65535u;
    if(id>=uFlatRides.words[3u]) return false;
    uint ride=uFlatRides.words[2u]+id*20u;
    int family=int(uFlatRides.words[ride]);
    if(family<1 || family>23) return false;
    uint type=object.trackTypeAndRideType&65535u;
    if(type!=uFlatRides.words[ride+2u] && type!=uFlatRides.words[ride+3u]) return false;
    int direction=int((object.direction+uScene.rotation)&3u);
    uint stationFlags=uFlatRides.words[ride+4u];
    int fenceMask=worldFlatFenceMask(ride,(object.trackData0>>8u)&255u,tile);
    WorldFlatParts parts;parts.count=0;
    if(family>=20 && family<=22) {
        bool section=type==uFlatRides.words[ride+3u];
        bool cap=(object.flags&(1u<<(family==22?14u:13u)))==0u;
        parts=worldTowerParts(family,int(object.sequence),direction,section,cap,(stationFlags&2u)!=0u,fenceMask);
    } else if(family!=23) {
        parts=worldFlatParts(family,int(object.sequence),direction,(stationFlags&1u)!=0u,
            (stationFlags&2u)!=0u,fenceMask,object.clearanceZ-object.baseZ,int(uFlatRides.words[ride+6u]),int(uFlatRides.words[ride+7u]));
    }
    uint scheme=object.trackData0&255u;
    if(scheme>=4u) return true;
    uint track=uFlatRides.words[ride+8u+scheme];
    uint vehicleIndex=0u;
    if(family==10) {
        int sequence=worldFlatSequence(family,int(object.sequence),direction);
        int segment=sequence==0?0:(sequence==5?1:(sequence==7?2:3));
        vehicleIndex=uint((segment-direction)&3);
    }
    WorldFlatPose pose=worldFlatReadPose(id,vehicleIndex);
    uint vehicle=uFlatRides.words[ride+12u+(uFlatRides.words[ride+5u]!=0u?vehicleIndex:0u)];
    bool ghost=(object.flags&1u)!=0u;
    // Preserve the finite family parent/child structure. The common world traversal
    // provides cross-tile order; arbitrary inter-family paint-bound arrangement remains separate.
    WorldMazeOrder mazeOrder;mazeOrder.count=0;
    // Common GPU columns arrange original creation order.
    int partCount=family==23?(mazeOrder.count>0?mazeOrder.count:26):parts.count;
    [[dont_unroll]] for(int i=0;i<partCount;i++) {
        WorldFlatPart part;
        if(family==23) part=worldMazePart(mazeOrder.count>0?mazeOrder.indices[i]:i,int(object.rideIdAndMazeEntry>>16u),direction,int((uFlatRides.words[ride+8u]>>16u)&255u));
        else part=worldFlatAnimatePart(parts.parts[i],family,direction,int(uScene.rotation),pose);
        if(part.image<0) continue;
        uint image=uint(part.image)+(part.bank!=0?uFlatRides.words[ride+1u]:0u);
        uint sprite=worldFlatImage(image);
        if(sprite==0xffffffffu) continue; // Host catalog preflight owns all reachable static images.
        // Top Spin's mechanical body explicitly uses scheme zero in the original painter.
        uint partTrack=family==16 && part.bank==1 && (part.colour==1 || part.colour==4)
            ?uFlatRides.words[ride+8u]:track;
        uint colours=part.colour==2?vehicle:partTrack;
        uint remaps=2u;
        if(part.colour==0) { colours=0u;remaps=1u; }
        if(part.colour==3) colours=(partTrack>>16u)|(partTrack&0xff00u);
        if(part.colour==4) colours=(partTrack&255u)|((partTrack>>8u)&0xff00u);
        if(part.colour==6) remaps=1u;
        uint palettes=worldObjectPalette(colours,int(remaps),ghost),effects=ghost?1u:remaps;
        if(part.colour==5) { palettes=uFlatRides.words[7u];effects=1024u; }
        worldSetPaintBounds(tile,ivec3(part.bx,part.by,object.baseZ+part.bz),
            ivec3(part.sx,part.sy,part.sz),part.child!=0?1u:0u);
        emitObjectSpriteWithFlags(tile,object.baseZ+part.z,ivec2(part.x,part.y),sprite,palettes,effects,
            worldFlatEntityPart(part,family,pose)?16u:0u,destination,writeRecords,count);
        WorldFlatPart overlay=worldFlatAnimationOverlay(part,family,direction,int(uScene.zoom),pose);
        if(overlay.image>=0) {
            uint overlaySprite=worldFlatImage(uint(overlay.image)+uFlatRides.words[ride+1u]);
            uint overlayColours=overlay.colour==7?uint(pose.slideColour)|(1u<<8u):colours;
            // The original slide rider uses its shirt/grey image directly, including on a ghost track.
            bool overlayGhost=ghost && overlay.colour!=7;
            uint overlayPalettes=worldObjectPalette(overlayColours,overlay.colour==7?2:int(remaps),overlayGhost);
            if(overlaySprite!=0xffffffffu) {
                worldSetPaintBounds(tile,ivec3(overlay.bx,overlay.by,object.baseZ+overlay.bz),
                    ivec3(overlay.sx,overlay.sy,overlay.sz),overlay.child!=0?1u:0u);
                emitObjectSpriteWithFlags(tile,object.baseZ+overlay.z,ivec2(overlay.x,overlay.y),overlaySprite,
                    overlayPalettes,overlayGhost?1u:(overlay.colour==7?2u:remaps),
                    worldFlatEntityPart(overlay,family,pose)?16u:0u,destination,writeRecords,count);
            }
        }
    }
    return true;
}
#endif
