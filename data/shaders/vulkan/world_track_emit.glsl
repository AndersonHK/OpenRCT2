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
    while(begin<end) {
        uint middle=begin+(end-begin)/2u;
        if(uTracks.words[base+middle*2u]<image) begin=middle+1u;
        else end=middle;
    }
    if(begin>=uTracks.words[10u] || uTracks.words[base+begin*2u]!=image) return 0xffffffffu;
    return uTracks.words[base+begin*2u+1u];
}

void visitTrack(uint index,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    WorldObjectRecord object=uObjects.records[index];
    if(object.kind!=4u || (object.flags&2u)!=0u) return;
    if(uTracks.words[0u]!=0x5754524bu || uTracks.words[1u]!=1u) return;
    uint ride=object.rideIdAndMazeEntry&65535u;
    uint type=object.trackTypeAndRideType&65535u,rideType=object.trackTypeAndRideType>>16u;
    if(ride>=uTracks.words[4u] || rideType>=uTracks.words[6u] || type>=uTracks.words[8u]) return;
    uint rideOffset=uTracks.words[3u]+ride*8u;
    if(uTracks.words[rideOffset]==0u) return;
    uint mapped=uTracks.words[uTracks.words[7u]+type];
    uint variant=((object.flags>>8u)&1u)|((mapped>>15u)&2u);
    uint style=uTracks.words[uTracks.words[5u]+rideType*4u+variant];
    uint state=((object.flags>>6u)&1u)|((object.flags>>7u)&2u)|((object.flags>>7u)&4u);
    uvec2 recipe;
    if(!worldTrackLookup(style,mapped&65535u,object.sequence,(object.direction+uScene.rotation)&3u,state,recipe)) return;
    uint scheme=object.trackData0&255u;
    if(scheme>=4u) return;
    uint colours=uTracks.words[rideOffset+4u+scheme];
    bool ghost=(object.flags&1u)!=0u;
    // Source-authored parent/child emission order. Cross-object occlusion and
    // support/tunnel/station/vehicle families remain separate native work.
    for(uint i=0u;i<recipe.y;i++) {
        WorldTrackPart part=worldTrackPart(recipe.x+i);
        uint sprite=worldTrackImage(part.image);
        if(sprite==0xffffffffu) continue;
        uint primary=part.colourRole==1u?((colours>>16u)&255u):(colours&255u);
        uint palettes=ghost?uCatalog.reserved:((primary+1u)|((((colours>>8u)&255u)+1u)<<8u));
        emitObjectSprite(tile,object.baseZ+part.offset.z,part.offset.xy,sprite,palettes,ghost?1u:2u,
            destination,writeRecords,count);
    }
}
#endif
