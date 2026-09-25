// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Include after world_prop_emit.glsl (shared raw WorldObjectRecord / uObjects).
#ifndef OPENRCT2_WORLD_TRACK_EMIT
#define OPENRCT2_WORLD_TRACK_EMIT
layout(std430,set=0,binding=14) readonly buffer TrackCatalog { uint words[]; } uTracks;
#define WORLD_TRACK_WORD(index) uTracks.words[uTracks.words[2u]+(index)]
#include "world_track_rules.glsl"
#undef WORLD_TRACK_WORD
#include "world_track_depth.glsl"

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
#include "world_track_photo.glsl"
#include "world_track_support_emit.glsl"

bool worldTrackObjectRecipe(WorldObjectRecord object, out uvec2 recipe)
{
    recipe=uvec2(0u);
    uint ride=object.rideIdAndMazeEntry&65535u;
    uint version=uTracks.words[1u]&255u;
    if(uTracks.words[0u]!=0x5754524bu || (version!=1u && version!=2u)) return false;
    uint type=object.trackTypeAndRideType&65535u,rideType=object.trackTypeAndRideType>>16u;
    if(ride>=uTracks.words[4u] || rideType>=uTracks.words[6u] || type>=uTracks.words[8u]) return false;
    uint rideOffset=uTracks.words[3u]+ride*8u;
    if(uTracks.words[rideOffset]==0u) return false;
    uint mapped=uTracks.words[uTracks.words[7u]+type];
    uint variant=((object.flags>>8u)&1u)|((mapped>>15u)&2u);
    uint style=uTracks.words[uTracks.words[5u]+rideType*4u+variant]&65535u;
    uint state=((object.flags>>6u)&1u)|((object.flags>>7u)&2u)|((object.flags>>7u)&4u);
    state|=((object.flags>>4u)&8u)|((uTracks.words[1u]>>4u)&16u)|((object.flags>>5u)&32u);
    if(worldStationHasPlatforms(ride)) state|=64u;
    if(!worldTrackLookup(style,mapped&65535u,object.sequence,(object.direction+uScene.rotation)&3u,state,recipe)) return false;
    return true;
}

void visitTrack(uint index,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    WorldObjectRecord object=uObjects.records[index];
    if(object.kind!=4u || (object.flags&2u)!=0u) return;
    if(visitStaticRide(index,tile,destination,writeRecords,count)) return;
    uint ride=object.rideIdAndMazeEntry&65535u;
    uint rideOffset=uTracks.words[3u]+ride*8u;
    uvec2 recipe;
    if(!worldTrackObjectRecipe(object,recipe)) return;
    uint scheme=object.trackData0&255u;
    if(scheme>=4u) return;
    uint colours=uTracks.words[rideOffset+4u+scheme];
    bool ghost=(object.flags&1u)!=0u;
    worldTrackDrawParts.count=0u;
    int recipeToPart[16];
    int partToQualifiedRail[16];
    worldTrackQualifiedRailCount=recipe.y;
    worldTrackRailDepth=2147483647;
    for(int i=0;i<16;i++) { partToQualifiedRail[i]=-1;worldTrackRailOwners[i]=0xffffffffu; }
    [[dont_unroll]]
    for(uint i=0u;i<recipe.y;i++) {
        WorldTrackPart part=worldTrackPart(recipe.x+i);
        recipeToPart[i]=int(worldTrackDrawParts.count);
        if(part.image==0xfffffffdu) continue;
        uint qualified=i;
        if(part.image==0xfffffffcu) {
            recipeToPart[i]=int(worldTrackDrawParts.count)+2; // Last authored parent is the camera.
            uint direction=uint(part.offset.x);
            [[dont_unroll]]
            for(uint photo=0u;photo<3u;photo++) {
                uint image=worldPhotoImage(direction,part.offset.y!=0,object.trackData0>>24u,photo);
                ivec3 offset=ivec3(worldPhotoX(direction,photo),worldPhotoY(direction,photo),
                    part.offset.z+worldPhotoZ(direction,photo));
                worldTrackAppend(worldStationPart(image,offset,offset,ivec3(1,1,19),2u),
                    worldTrackImage(image),ghost?uCatalog.reserved:1u,1u);
            }
            continue;
        }
        if(part.image==0xfffffffeu) {
            worldTrackStation(part,object,tile,colours);
            continue;
        }
        uint colourRole=part.colourRole&7u;
        if(colourRole>=4u) {
            uint sprite=colourRole==4u?uCatalog.waterMask[0]:
                ((uScene.transparentWater!=0u || (uScene.viewFlags&1u)!=0u)?uCatalog.waterOverlay[0]:uCatalog.waterOpaque[0]);
            if(part.parent>=0) part.parent=recipeToPart[part.parent];
            if(sprite<uScene.spriteSetCount)
                worldTrackAppend(part,sprite,uSpriteSets.records[sprite].palettes,uSpriteSets.records[sprite].effects);
            continue;
        }
        part.image=worldTrackImageAtTick(part.image,uScene.sourceTick);
        uint sprite=worldTrackImage(part.image);
        if(sprite==0xffffffffu) continue;
        if(part.parent<0 && worldTrackDrawParts.count<16u)
            partToQualifiedRail[worldTrackDrawParts.count]=int(qualified);
        uint primary=colourRole==1u?((colours>>16u)&255u):(colours&255u);
        uint secondary=colourRole==3u?((colours>>16u)&255u):((colours>>8u)&255u);
        uint palettes=ghost?uCatalog.reserved:(colourRole==2u?1u:((primary+1u)|((secondary+1u)<<8u)));
        if(part.parent>=0) part.parent=recipeToPart[part.parent];
        worldTrackAppend(part,sprite,palettes,ghost||colourRole==2u?1u:2u);
    }
    // Preserve recipe ownership for component-local layers; hardware depth resolves visibility.
    int parentParts[12];int parentCount=0;
    [[dont_unroll]]
    for(uint i=0u;i<worldTrackDrawParts.count;i++) {
        WorldTrackPart p=worldTrackDrawParts.parts[i].geometry;
        if(p.parent>=0) continue;
        parentParts[parentCount++]=int(i);
    }
    [[dont_unroll]]
    for(int ordinal=0;ordinal<parentCount;ordinal++) {
        int parent=parentParts[ordinal];
        [[dont_unroll]]
        for(uint i=uint(parent);i<worldTrackDrawParts.count;i++) {
            WorldTrackDrawPart p=worldTrackDrawParts.parts[i];
            if(int(i)!=parent && p.geometry.parent!=parent) continue;
            worldSetPaintBounds(tile,p.geometry.bounds+ivec3(0,0,object.baseZ),p.geometry.size,int(i)==parent?0u:1u);
            worldComponentRootLayer=uint(WORLD_TRACK_RAIL_LAYER);
            if((p.geometry.colourRole&8u)!=0u)
                worldSetComponentDepthAnchor(tile,ivec3(
                    worldForegroundContact(p.geometry.bounds.x,p.geometry.size.x),
                    worldForegroundContact(p.geometry.bounds.y,p.geometry.size.y),
                    object.baseZ+p.geometry.bounds.z));
            if((p.geometry.colourRole&16u)!=0u)
                worldSetForegroundTileContact(tile,object.baseZ+p.geometry.bounds.z,WORLD_FOREGROUND_RAIL_LAYER);
            WorldTrackStationCoverAnchor cover=worldTrackStationCoverAnchor(int(p.geometry.image));
            if(cover.valid) {
                worldSetComponentDepthAnchor(tile,ivec3(cover.x,cover.y,object.baseZ+p.geometry.offset.z+cover.z));
                worldComponentRootLayer=uint(WORLD_FOREGROUND_SHELL_LAYER);
            }
            uint first=count;
            emitObjectSprite(tile,object.baseZ+p.geometry.offset.z,p.geometry.offset.xy,p.sprite,p.palettes,p.effects,
                destination,writeRecords,count);
            if(count>first && partToQualifiedRail[i]>=0) {
                worldTrackRailOwners[partToQualifiedRail[i]]=destination+first;
                if(writeRecords && destination+first<uScene.outputCapacity && (uOutputs.records[destination+first].valid&1)!=0)
                    worldTrackRailDepth=min(worldTrackRailDepth,uOutputs.records[destination+first].reserved.x);
            }
        }
    }
    // Only support state, not rail geometry, depends on these operations. Run
    // their original ordered program after recording rail owners so steep
    // wooden transitions can bind the exact captured prepend target.
    worldEmitTrackSupports(object,tile,colours,destination,writeRecords,count);
}
#endif
