// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// TrackPaint.cpp station base/platform/fence and shelter rules. All instance
// decisions use raw station type, direction, light and owner entrance locations.
#ifndef OPENRCT2_WORLD_TRACK_STATION
#define OPENRCT2_WORLD_TRACK_STATION
struct WorldTrackDrawPart { WorldTrackPart geometry; uint sprite; uint palettes; uint effects; };
struct WorldTrackDrawParts { uint count; WorldTrackDrawPart parts[16]; };
// One private owner per compute invocation, reset before each track visit.
// Passing this array through GLSL inout parameters creates whole-array copies
// at every helper call; helpers instead mutate this single bounded scratch.
WorldTrackDrawParts worldTrackDrawParts;
void worldTrackAppend(WorldTrackPart geometry,uint sprite,uint palettes,uint effects)
{
    if(sprite==0xffffffffu) return;
    WorldTrackDrawPart p; p.geometry=geometry;p.sprite=sprite;p.palettes=palettes;p.effects=effects;
    worldTrackDrawParts.parts[worldTrackDrawParts.count++]=p;
}
WorldTrackPart worldStationPart(uint image,ivec3 offset,ivec3 bounds,ivec3 size,uint role)
{
    WorldTrackPart p;p.image=image;p.offset=offset;p.bounds=bounds;p.size=size;p.colourRole=role;p.parent=-1;return p;
}
bool worldStationFence(uint ride,uint stationIndex,uvec2 tile,uint edge)
{
    uint owner=worldEntranceRide(ride);
    if(owner==0u || stationIndex>=uEntrances.words[owner+5u]) return true;
    uint station=uEntrances.words[owner+4u]+stationIndex*10u;
    const ivec2 offsets[4]=ivec2[4](ivec2(-1,0),ivec2(0,-1),ivec2(1,0),ivec2(0,1));
    // NE/SE/SW/NW are indices0/1/2/3; matches EntranceOffsetEdgeNE/NW.
    uint turn=edge==0u?0u:(edge==1u?3u:(edge==2u?2u:1u));
    ivec2 neighbour=ivec2(tile)+offsets[(uScene.rotation+turn)&3u];
    uint flags=uEntrances.words[station];
    if((flags&2u)!=0u && neighbour==ivec2(uEntrances.words[station+4u],uEntrances.words[station+5u])) return false;
    if((flags&4u)!=0u && neighbour==ivec2(uEntrances.words[station+7u],uEntrances.words[station+8u])) return false;
    return true;
}
void worldStationImage(uint image,ivec3 offset,ivec3 bounds,ivec3 size,
    uint colours,bool ghost,bool base)
{
    uint palettes=ghost?uCatalog.reserved:(base?1u:(((colours>>16u)&255u)+1u)|((((colours>>8u)&255u)+1u)<<8u));
    worldTrackAppend(worldStationPart(image,offset,bounds,size,base?2u:1u),worldTrackImage(image),palettes,
        ghost||base?1u:2u);
}
// Front rails/post components opt into the same authored contact anchor as path fences.
// The role flag changes ordering only; raster offset and palette selection remain intact.
void worldStationFrontFence(uint image,ivec3 offset,ivec3 size,int contactZ,uint colours,bool ghost)
{
    uint first=worldTrackDrawParts.count;
    worldStationImage(image,offset,offset,size,colours,ghost,false);
    if(worldTrackDrawParts.count>first) {
        // Internal station role; static track recipe bit3 keeps its own authored
        // edge endpoint. The station rail and shelter instead share one contact.
        worldTrackDrawParts.parts[first].geometry.colourRole|=16u;
        worldTrackDrawParts.parts[first].geometry.bounds.z=contactZ;
    }
}
void worldStationCover(uint ride,uint edge,bool fence,int height,uint variant,
    uint colours,bool ghost)
{
    uint station=worldEntranceStation(ride);
    if(station==0u) return;
    uint offset=edge==0u?(fence?4u:3u):(edge==1u?2u:(edge==2u?5u:(fence?1u:0u)));
    if(variant==2u) offset+=6u;
    uint base=uEntrances.words[station+12u],length=uEntrances.words[station+13u];
    if(base==0xffffffffu || offset>=length) return;
    int h=variant==0u?22:(variant==1u?30:46);
    ivec3 bounds=ivec3(0,0,height+1+h),size=ivec3(32,32,0);
    if(edge==0u) {bounds=ivec3(0,1,height+1);size=ivec3(1,30,h);}
    if(edge==3u) {bounds=ivec3(1,0,height+1);size=ivec3(30,1,h);}
    uint palette=ghost?uCatalog.reserved:((colours&255u)+1u)|((((colours>>8u)&255u)+1u)<<8u);
    WorldTrackPart p=worldStationPart(uint(worldTrackStationCoverMarker(int(edge),int(variant))),
        ivec3(0,0,height),bounds,size,0u);
    uint parent=worldTrackDrawParts.count;
    worldTrackAppend(p,base+offset,palette,ghost?1u:2u);
    uint glass=uEntrances.words[station+14u];
    if(!ghost && (uEntrances.words[station+1u]&4u)!=0u && glass!=0xffffffffu && offset<uEntrances.words[station+15u]) {
        p.parent=int(parent);
        worldTrackAppend(p,glass+offset,uEntrances.words[6u]+(colours&255u),1024u);
    }
}
#include "world_chairlift_station_emit.glsl"
void worldTrackStation(WorldTrackPart marker,WorldObjectRecord object,uvec2 tile,uint colours)
{
    if(marker.size.y==8) {worldChairliftStation(marker,object,tile,colours);return;}
    uint ride=object.rideIdAndMazeEntry&65535u;
    if(marker.size.y==7) {
        // Direct source cover calls do not test StationObject::noPlatforms.
        // Their edge and height are authored; only entrance ownership is dynamic.
        if(!worldSupportPassedSurface()) return;
        uint edge=uint(marker.offset.x),stationIndex=(object.trackData0>>8u)&255u;
        worldStationCover(ride,edge,worldStationFence(ride,stationIndex,tile,edge),
            marker.offset.z,uint(marker.offset.y),colours,(object.flags&1u)!=0u);
        return;
    }
    if(!worldStationHasPlatforms(ride)) return;
    uint direction=(object.direction+uScene.rotation)&3u,axis=direction&1u;
    uint type=object.trackTypeAndRideType&65535u,stationIndex=(object.trackData0>>8u)&255u;
    if(marker.size.y==6) {
        // MultiDimensionRCTrackStation authors two covers only, no deck or fences.
        // A missing station object emits no covers (worldStationCover returns).
        uint stationIndex=(object.trackData0>>8u)&255u;
        uint backEdge=axis==0u?3u:0u,frontEdge=axis==0u?1u:2u;
        bool ghost=(object.flags&1u)!=0u;
        worldStationCover(ride,backEdge,worldStationFence(ride,stationIndex,tile,backEdge),
            marker.offset.z,0u,colours,ghost);
        worldStationCover(ride,frontEdge,worldStationFence(ride,stationIndex,tile,frontEdge),
            marker.offset.z,0u,colours,ghost);
        return;
    }
    if(marker.size.y>=4) {
        // TrackPaintUtilDrawNarrowStationPlatform / DrawPier. The narrow
        // base's bounding-axis swap is authored and intentionally preserved.
        bool pier=marker.size.y==5,ghost=(object.flags&1u)!=0u;
        int height=marker.offset.z,z=height+marker.bounds.z;
        if(!pier && marker.bounds.x!=0)
            worldStationImage(22424u+uint(marker.bounds.x)*2u+axis,ivec3(0,0,height+marker.bounds.y),
                axis==0u?ivec3(2,0,height):ivec3(0,2,height),axis==0u?ivec3(28,32,1):ivec3(32,28,1),colours,ghost,true);
        uint backEdge=axis==0u?3u:0u,frontEdge=axis==0u?1u:2u;
        bool backFence=worldStationFence(ride,stationIndex,tile,backEdge);
        bool frontFence=worldStationFence(ride,stationIndex,tile,frontEdge);
        uint back=pier?(backFence?22408u:22406u)+axis:(backFence?22414u:22416u)+axis;
        ivec3 size=axis==0u?ivec3(32,8,1):ivec3(8,32,1),bound=ivec3(0,0,z);
        ivec3 backSize=size;
        if(pier) {bound=axis==0u?ivec3(0,2,z):ivec3(2,0,z);backSize=axis==0u?ivec3(32,6,1):ivec3(6,32,1);}
        worldStationImage(back,ivec3(0,0,z),bound,backSize,colours,ghost,false);
        worldStationCover(ride,backEdge,backFence,height,0u,colours,ghost);
        ivec3 frontOffset=axis==0u?ivec3(0,24,z):ivec3(24,0,z);
        worldStationImage((pier?22404u:22412u)+axis,frontOffset,frontOffset,size,colours,ghost,false);
        if(frontFence) {
            ivec3 offset=axis==0u?ivec3(0,31,z+2):ivec3(31,0,z+2);
            worldStationFrontFence((pier?22410u:22370u)+axis,offset,
                axis==0u?ivec3(32,1,7):ivec3(1,32,7),height,colours,ghost);
        }
        worldStationCover(ride,frontEdge,frontFence,height,0u,colours,ghost);
        return;
    }
    bool ghost=(object.flags&1u)!=0u,green=(object.flags&(1u<<10u))!=0u,inverted=marker.size.y!=0;
    int height=marker.offset.z,a=inverted?6:marker.bounds.z,b=inverted?8:marker.size.x;
    uint variant=inverted?uint(marker.size.y-1):0u;
    if(marker.bounds.x!=0) {
        uint image=22424u+uint(marker.bounds.x)*2u+axis;
        worldStationImage(image,ivec3(0,0,height+marker.bounds.y),axis==0u?ivec3(0,2,height):ivec3(2,0,height),
            axis==0u?ivec3(32,28,1):ivec3(28,32,1),colours,ghost,true);
    }
    uint backEdge=axis==0u?3u:0u,frontEdge=axis==0u?1u:2u;
    bool backFence=worldStationFence(ride,stationIndex,tile,backEdge);
    bool frontFence=worldStationFence(ride,stationIndex,tile,frontEdge);
    bool lightFront=type==1u && direction==(axis==0u?2u:1u);
    bool beginFront=type==2u && direction==(axis==0u?0u:3u);
    bool lightBack=type==1u && direction==(axis==0u?0u:3u);
    bool beginBack=type==2u && direction==(axis==0u?2u:1u);
    uint back=22362u+axis+(backFence?2u:0u);
    if(beginBack) back=(backFence?22366u:22368u)+axis;
    if(lightBack) back=(backFence?22380u:22388u)+axis+(green?2u:0u);
    ivec3 platformSize=axis==0u?ivec3(32,8,1):ivec3(8,32,1);
    worldStationImage(back,ivec3(0,0,height+a),ivec3(0,0,height+a),platformSize,colours,ghost,false);
    worldStationCover(ride,backEdge,backFence,height,variant,colours,ghost);
    uint front=beginBack?22368u+axis:(lightBack?22388u+axis+(green?2u:0u):22362u+axis);
    ivec3 frontOffset=axis==0u?ivec3(0,24,height+a):ivec3(24,0,height+a);
    worldStationImage(front,frontOffset,frontOffset,platformSize,colours,ghost,false);
    if(frontFence) {
        uint image=(inverted?22392u:22370u)+axis;
        if(beginFront) image=(inverted?22394u:22372u)+axis;
        if(lightFront) image=(inverted?22396u:22386u)+axis;
        ivec3 offset=axis==0u?ivec3(0,31,height+b):ivec3(31,0,height+b);
        worldStationFrontFence(image,offset,axis==0u?ivec3(32,1,7):ivec3(1,32,7),height,colours,ghost);
    } else if(beginFront||lightFront) {
        uint image=(beginFront?22374u:22384u)+axis;
        ivec3 offset=axis==0u?ivec3(31,23,height+b):ivec3(23,31,height+b);
        worldStationFrontFence(image,offset,axis==0u?ivec3(1,8,7):ivec3(8,1,7),height,colours,ghost);
    }
    worldStationCover(ride,frontEdge,frontFence,height,variant,colours,ghost);
    if(beginFront||lightFront) {
        uint image=(beginFront?22374u:22384u)+axis;
        ivec3 offset=axis==0u?ivec3(31,0,height+b):ivec3(0,31,height+b);
        worldStationImage(image,offset,offset,axis==0u?ivec3(1,8,7):ivec3(8,1,7),colours,ghost,false);
    }
}
#endif
