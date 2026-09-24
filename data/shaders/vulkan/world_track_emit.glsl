// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Include after world_prop_emit.glsl (shared raw WorldObjectRecord / uObjects).
#ifndef OPENRCT2_WORLD_TRACK_EMIT
#define OPENRCT2_WORLD_TRACK_EMIT
layout(std430,set=0,binding=14) readonly buffer TrackCatalog { uint words[]; } uTracks;
#define WORLD_TRACK_WORD(index) uTracks.words[uTracks.words[2u]+(index)]
#include "world_track_rules.glsl"
#undef WORLD_TRACK_WORD

uint worldTrackImage(uint image)
{
    uint begin=0u,end=uTracks.words[10u],base=uTracks.words[9u];
    [[dont_unroll]]
    while(begin<end) {
        uint middle=begin+(end-begin)/2u;
        if(uTracks.words[base+middle*2u]<image) begin=middle+1u;
        else end=middle;
    }
    if(begin>=uTracks.words[10u] || uTracks.words[base+begin*2u]!=image) return 0xffffffffu;
    return uTracks.words[base+begin*2u+1u];
}
#include "world_track_station.glsl"

void visitTrack(uint index,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    WorldObjectRecord object=uObjects.records[index];
    if(object.kind!=4u || (object.flags&2u)!=0u) return;
    if(visitStaticRide(index,tile,destination,writeRecords,count)) return;
    if(uTracks.words[0u]!=0x5754524bu || (uTracks.words[1u]&255u)!=1u) return;
    uint ride=object.rideIdAndMazeEntry&65535u;
    uint type=object.trackTypeAndRideType&65535u,rideType=object.trackTypeAndRideType>>16u;
    if(ride>=uTracks.words[4u] || rideType>=uTracks.words[6u] || type>=uTracks.words[8u]) return;
    uint rideOffset=uTracks.words[3u]+ride*8u;
    if(uTracks.words[rideOffset]==0u) return;
    uint mapped=uTracks.words[uTracks.words[7u]+type];
    uint variant=((object.flags>>8u)&1u)|((mapped>>15u)&2u);
    uint style=uTracks.words[uTracks.words[5u]+rideType*4u+variant];
    uint state=((object.flags>>6u)&1u)|((object.flags>>7u)&2u)|((object.flags>>7u)&4u);
    state|=((object.flags>>4u)&8u)|((uTracks.words[1u]>>4u)&16u)|((object.flags>>5u)&32u);
    if(worldStationHasPlatforms(ride)) state|=64u;
    uvec2 recipe;
    if(!worldTrackLookup(style,mapped&65535u,object.sequence,(object.direction+uScene.rotation)&3u,state,recipe)) return;
    uint scheme=object.trackData0&255u;
    if(scheme>=4u) return;
    uint colours=uTracks.words[rideOffset+4u+scheme];
    bool ghost=(object.flags&1u)!=0u;
    worldTrackDrawParts.count=0u;
    int recipeToPart[16];
    [[dont_unroll]]
    for(uint i=0u;i<recipe.y;i++) {
        WorldTrackPart part=worldTrackPart(recipe.x+i);
        recipeToPart[i]=int(worldTrackDrawParts.count);
        if(part.image==0xfffffffeu) {
            worldTrackStation(part,object,tile,colours);
            continue;
        }
        uint sprite=worldTrackImage(part.image);
        if(sprite==0xffffffffu) continue;
        uint primary=part.colourRole==1u?((colours>>16u)&255u):(colours&255u);
        uint secondary=part.colourRole==3u?((colours>>16u)&255u):((colours>>8u)&255u);
        uint palettes=ghost?uCatalog.reserved:(part.colourRole==2u?1u:((primary+1u)|((secondary+1u)<<8u)));
        if(part.parent>=0) part.parent=recipeToPart[part.parent];
        worldTrackAppend(part,sprite,palettes,ghost||part.colourRole==2u?1u:2u);
    }
    // Arrange only this owner's authored parents, preserving every child with
    // its parent. This does not claim a general cross-tile/object ordering fix.
    WorldPathPart parents[12];int parentParts[12];int parentCount=0;
    [[dont_unroll]]
    for(uint i=0u;i<worldTrackDrawParts.count;i++) {
        WorldTrackPart p=worldTrackDrawParts.parts[i].geometry;
        if(p.parent>=0) continue;
        parents[parentCount]=worldPathPart(0,p.offset.x,p.offset.y,p.offset.z,
            p.bounds.x,p.bounds.y,p.bounds.z,p.size.x,p.size.y,p.size.z);
        parentParts[parentCount++]=int(i);
    }
    WorldPathOrder order;order.count=0;
    if(writeRecords && parentCount>1) order=worldPathOrder(parents,parentCount,int(uScene.rotation));
    [[dont_unroll]]
    for(int ordinal=0;ordinal<parentCount;ordinal++) {
        int parent=parentParts[order.count==parentCount?order.indices[ordinal]:ordinal];
        [[dont_unroll]]
        for(uint i=uint(parent);i<worldTrackDrawParts.count;i++) {
            WorldTrackDrawPart p=worldTrackDrawParts.parts[i];
            if(int(i)!=parent && p.geometry.parent!=parent) continue;
            emitObjectSprite(tile,object.baseZ+p.geometry.offset.z,p.geometry.offset.xy,p.sprite,p.palettes,p.effects,
                destination,writeRecords,count);
        }
    }
}
#endif
