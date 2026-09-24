// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_ENTRANCE_EMIT
#define OPENRCT2_WORLD_ENTRANCE_EMIT
#include "world_entrance_rules.glsl"
layout(std430,set=0,binding=16) readonly buffer EntranceCatalog { uint words[]; } uEntrances;
uint worldEntranceRide(uint rideId)
{
    if(uEntrances.words.length()<8 || uEntrances.words[7u]!=1u || rideId>=uEntrances.words[5u]) return 0u;
    uint address=uEntrances.words[4u]+rideId*8u;
    return uEntrances.words[address]!=0u?address:0u;
}
uint worldEntranceStation(uint rideId)
{
    uint ride=worldEntranceRide(rideId);
    if(ride==0u) return 0u;
    uint slot=uEntrances.words[ride+1u];
    if(slot>=uEntrances.words[1u]) return 0u;
    uint address=uEntrances.words[0u]+slot*16u;
    return uEntrances.words[address]!=0u?address:0u;
}
bool worldStationHasPlatforms(uint rideId)
{
    uint station=worldEntranceStation(rideId);
    // Missing style preserves the legacy default platforms; explicit noPlatforms suppresses them.
    return station==0u || (uEntrances.words[station+1u]&8u)==0u;
}
void visitEntrance(uint index,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    WorldObjectRecord object=uObjects.records[index];
    if(object.kind!=5u || (object.flags&2u)!=0u || uEntrances.words.length()<8 || uEntrances.words[7u]!=1u) return;
    if((uScene.viewFlags&(1u<<18))!=0u) return;
    uint type=object.reserved&255u;
    if(type>2u) return;
    int direction=int((object.direction+uScene.rotation)&3u);
    uint station=0u,ride=0u,park=0u,flags=0u,colours=0u;
    WorldPropParts parts=worldEmptyPropParts();
    if(type==2u) {
        if(object.sequence>2u) return;
        if(object.objectSlot<uEntrances.words[3u]) park=uEntrances.words[2u]+object.objectSlot*4u;
        parts=worldParkEntranceParts(direction,int(object.sequence));
    } else {
        ride=worldEntranceRide(object.rideIdAndMazeEntry&65535u);
        station=worldEntranceStation(object.rideIdAndMazeEntry&65535u);
        if(ride==0u || station==0u || uEntrances.words[station+4u]==0xffffffffu) return;
        flags=uEntrances.words[station+1u];
        colours=((flags&1u)!=0u?uEntrances.words[ride+2u]:0u)|(((flags&2u)!=0u?uEntrances.words[ride+3u]:0u)<<8u);
        parts=worldRideEntranceParts(direction,type==1u,int(flags));
    }
    int recipe[2]; int parentCount=0;
    for(int i=0;i<parts.count;i++) {
        WorldPropPart p=parts.parts[i];
        if(p.child!=0) continue;
        if(parentCount==2) return; // Reject a future recipe exceeding this visitor's explicit bound.
        recipe[parentCount++]=i;
    }
    bool ghost=(object.flags&1u)!=0u;
    for(int ordinal=0;ordinal<parentCount;ordinal++) {
        int selected=ordinal;
        int begin=recipe[selected],end=begin+1;
        while(end<parts.count && parts.parts[end].child!=0) end++;
        for(int i=begin;i<end;i++) {
            WorldPropPart part=parts.parts[i];
            uint sprite=0xffffffffu;
            if(type==2u) {
                if(part.imageOffset<0) {
                    uint slot=(object.reserved>>8u)&255u;
                    if(slot<255u) {
                        PathMaterial surface=uCatalog.paths[slot+((object.flags&4096u)!=0u?255u:0u)];
                        uint offset=5u*(1u+(uint(direction)&1u));
                        if(offset<surface.surfaceCount) sprite=surface.surfaceBase+offset;
                    }
                } else if(park!=0u && uint(part.imageOffset)<uEntrances.words[park+1u]) sprite=uEntrances.words[park]+uint(part.imageOffset);
            } else {
                uint base=uEntrances.words[station+4u+uint(part.imageOffset)];
                if(base!=0xffffffffu) sprite=base+uint(direction);
            }
            if(sprite==0xffffffffu) continue;
            uint effects=ghost?1u:uint(part.colourMode);
            uint palettes=worldObjectPalette(colours,part.colourMode,ghost);
            // The original transparent entrance glass keeps its colour filter even on a ghost entrance.
            if(part.colourMode==4) { effects=1024u; palettes=uEntrances.words[6u]+uEntrances.words[ride+2u]; }
            worldSetPaintBounds(tile,ivec3(part.boundsX,part.boundsY,object.baseZ+part.boundsZ),
                ivec3(part.sizeX,part.sizeY,part.sizeZ),i==begin?0u:1u);
            worldSetCoplanarSurfaceLayer();
            if(type!=2u) {
                WorldEntranceDepthAnchor anchor=worldRideEntranceDepthAnchor(ordinal,object.baseZ);
                worldSetComponentDepthAnchor(tile,ivec3(anchor.x,anchor.y,anchor.z));
            }
            emitObjectSprite(tile,object.baseZ+part.z,ivec2(part.x,part.y),sprite,palettes,effects,destination,writeRecords,count);
        }
    }
    if(type==0u && station!=0u) {
        uint rideId=object.rideIdAndMazeEntry&65535u;
        uint rideFlags=0u;
        if(uRidePoses.words.length()>=4 && uRidePoses.words[1]==20u
            && rideId<uRidePoses.words[0] && rideId<(uint(uRidePoses.words.length())-4u)/20u)
            rideFlags=uRidePoses.words[4u+rideId*20u];
        WorldEntranceText text=worldRideEntranceText(false,ghost,int(uEntrances.words[station+3u]),
            int(uEntrances.words[station+2u]),object.baseZ,int(rideFlags));
        uint descriptor=worldBannerTextDescriptor(true,rideId);
        if(text.mode>=0 && descriptor!=0u) {
            if(text.closed!=0) descriptor=uBannerTexts.words[6];
            // PaintRideEntranceExitScrollingText authors this component's own
            // bounds at (2,2,baseZ+stationHeight), above the front frame's +30.
            worldEmitBannerText(tile,text.rasterZ,ivec3(2,2,text.rasterZ),descriptor,uint(text.mode),
                destination,writeRecords,count);
        }
    }
    worldSupportEntrance(worldSupportState,object.baseZ,int(type));
}
#endif
