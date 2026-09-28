// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Included after the shared station append/fence/cover helpers.
#ifndef OPENRCT2_WORLD_CHAIRLIFT_STATION_EMIT
#define OPENRCT2_WORLD_CHAIRLIFT_STATION_EMIT
#include "world_chairlift_station_rules.glsl"
bool worldChairliftNeighbour(ivec2 tile,uint ride,int z)
{
    if(any(lessThan(tile,ivec2(0))) || any(greaterThanEqual(tile,ivec2(uScene.width,uScene.height)))) return false;
    SourceRecord source=uSources.records[uint(tile.y)*uScene.width+uint(tile.x)];
    [[dont_unroll]]
    for(uint i=0u;i<source.objectCount;i++) {
        WorldObjectRecord other=uObjects.records[source.objectFirst+i];
        if(worldChairliftNeighbourMatches(int(ride),z,int(other.kind),int(other.rideIdAndMazeEntry&65535u),other.baseZ)) return true;
    }
    return false;
}
void worldChairliftStation(WorldTrackPart marker,WorldObjectRecord object,uvec2 tile,uint colours)
{
    const ivec2 delta[4]=ivec2[4](ivec2(-1,0),ivec2(0,1),ivec2(1,0),ivec2(0,-1));
    uint ride=object.rideIdAndMazeEntry&65535u,type=object.trackTypeAndRideType&65535u;
    uint direction=(object.direction+uScene.rotation)&3u,stationIndex=(object.trackData0>>8u)&255u;
    bool first=type==2u && !worldChairliftNeighbour(ivec2(tile)-delta[object.direction&3u],ride,object.baseZ);
    bool last=type==1u && !worldChairliftNeighbour(ivec2(tile)+delta[object.direction&3u],ride,object.baseZ);
    bool odd=(direction&1u)!=0u,ghost=(object.flags&1u)!=0u;
    bool backFence=worldStationFence(ride,stationIndex,tile,odd?0u:3u);
    bool frontFence=worldStationFence(ride,stationIndex,tile,odd?2u:1u);
    uint frame=0u;
    if(uRidePoses.words.length()>=4 && uRidePoses.words[1]==20u && ride<uRidePoses.words[0]
        && ride<(uint(uRidePoses.words.length())-4u)/20u)
        frame=(uRidePoses.words[4u+ride*20u+3u]>>14u)&3u;
    int lastParent=-1;
    [[dont_unroll]]
    for(int event=0;event<12;event++) {
        WorldChairliftStationPart p=worldChairliftStationPart(event,int(direction),first,last,backFence,frontFence,int(frame));
        if(p.cover>=0) {
            uint begin=worldTrackDrawParts.count;
            if(begin>14u) {atomicOr(uStatus.overflow,8u);return;}
            if(worldSupportPassedSurface())
                worldStationCover(ride,uint(p.cover),event==3?backFence:frontFence,marker.offset.z,0u,0u,ghost);
            // GetStationColourScheme is the station-black single remap. Glass
            // retains its filter operation and the same station-black colour.
            for(uint i=begin;i<worldTrackDrawParts.count;i++) {
                if(worldTrackDrawParts.parts[i].geometry.parent<0) {
                    lastParent=int(i);
                    worldTrackDrawParts.parts[i].palettes=ghost?uCatalog.reserved:1u;
                    worldTrackDrawParts.parts[i].effects=1u;
                }
            }
            continue;
        }
        if(p.image==0) continue;
        uint sprite=worldTrackImage(uint(p.image));
        if(sprite==0xffffffffu) {atomicOr(uStatus.overflow,8u);continue;}
        if(worldTrackDrawParts.count>=16u) {atomicOr(uStatus.overflow,8u);return;}
        WorldTrackPart part=worldStationPart(uint(p.image),ivec3(p.x,p.y,p.z+marker.offset.z),
            ivec3(p.bx,p.by,p.bz+marker.offset.z),ivec3(p.sx,p.sy,p.sz),p.support!=0?1u:0u);
        if(p.child!=0) part.parent=lastParent;
        else lastParent=int(worldTrackDrawParts.count);
        uint primary=p.support!=0?((colours>>16u)&255u):(colours&255u);
        uint palette=ghost?uCatalog.reserved:((primary+1u)|((((colours>>8u)&255u)+1u)<<8u));
        worldTrackAppend(part,sprite,palette,ghost?1u:2u);
    }
}
#endif
