// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_RAILWAY_EMIT
#define OPENRCT2_WORLD_RAILWAY_EMIT
// Support record fields: offset=opcode/type/subtype, bounds=direction/height/transition,
// size=segmentMask/slope/flags. Role and all geometry remain source authored.
WorldWoodenCursor worldRailwaySupport(WorldTrackPart p,WorldObjectRecord object,uint supportType)
{
    return worldWoodenBegin(worldSupportState,p.offset.y==255?int(supportType):p.offset.y,
        p.offset.z,p.bounds.x,object.baseZ+p.bounds.y,p.bounds.z,p.offset.x==6,(p.size.z&2)!=0,false);
}
bool worldRailwayHasFloor(WorldWoodenCursor c)
{
    // Exact final return, derived before streaming any support art. A transition
    // overwrites hasSupports even when base art exists; type-B flat caps differ.
    return c.accepted && (c.transition!=255?c.subtype<2:(c.phase<4 || c.steps>0));
}
void worldRailwayDraw(WorldTrackPart p,WorldObjectRecord object,uvec2 tile,uint colours,
    uint destination,bool writeRecords,inout uint count,inout uint owner)
{
    if(p.parent>=0 && owner==0xffffffffu) return;
    uint sprite=worldTrackImage(p.image);
    if(sprite==0xffffffffu) {
        worldReportComponentFailure(256u,tile,writeRecords);
        if(p.parent<0) owner=0xffffffffu;
        return;
    }
    bool ghost=(object.flags&1u)!=0u;
    uint role=p.colourRole&7u;
    uint palettes=ghost?uCatalog.reserved:role==2u?1u:
        (((role==1u?colours>>16u:colours)&255u)+1u)|((((colours>>8u)&255u)+1u)<<8u);
    uint effects=ghost||role==2u?1u:2u;
    // The source crossing helpers construct fresh unremapped ImageIds.
    if(p.image>=32151u && p.image<=32154u) { palettes=1u;effects=1u; }
    if(p.image>=32155u && p.image<=32162u) { palettes=uCatalog.viewPalettes.y;effects=1u<<10; }
    worldParentRoot=owner;
    worldSetPaintBounds(tile,p.bounds+ivec3(0,0,object.baseZ),p.size,p.parent<0?0u:1u);
    worldComponentRootLayer=uint(WORLD_TRACK_RAIL_LAYER);
    // Explicit authored child ownership, including floor-owned diagonal rails.
    if(p.parent>=0 && writeRecords && worldParentRoot<uScene.outputCapacity) {
        worldSetComponentDepthAnchor(tile,p.offset+ivec3(0,0,object.baseZ));
        worldSetComponentDepthScalar(max(worldAuthoredComponentDepth,uOutputs.records[worldParentRoot].reserved.x));
    }
    if(p.image>=32153u && p.image<=32162u) {
        worldSetComponentDepthAnchor(tile,p.offset+ivec3(0,0,object.baseZ+2));
        worldComponentRootLayer=p.image<=32154u?3u:4u;
    }
    uint before=count;
    emitObjectSprite(tile,object.baseZ+p.offset.z,p.offset.xy,sprite,palettes,effects,destination,writeRecords,count);
    if(p.parent<0) owner=count>before?destination+before:0xffffffffu;
}
// Paint.TileElement selects the last later element in the same-height group.
// Raw path flags/edges stay on the GPU; no crossing-art selector is published.
int worldRailwayCrossingEdges(WorldObjectRecord object,uvec2 tile)
{
    SourceRecord source=uSources.records[tile.y*uScene.width+tile.x];
    uint first=object.ordinal;bool invisible=false;int edges=-1;
    if(source.present!=0u && source.baseZ==object.baseZ && source.surfaceOrdinal<first) first=source.surfaceOrdinal;
    [[dont_unroll]]
    for(uint i=0u;i<source.objectCount;i++) {
        WorldObjectRecord item=uObjects.records[source.objectFirst+i];
        if(item.baseZ==object.baseZ && item.ordinal<first) { first=item.ordinal;invisible=(item.flags&2u)!=0u; }
    }
    [[dont_unroll]]
    for(uint i=0u;i<source.pathCount;i++) {
        PathRecord path=uPaths.records[source.pathFirst+i];
        if(path.baseZ==object.baseZ && path.elementOrdinal<first) { first=path.elementOrdinal;invisible=(path.flags&(1u<<9))!=0u; }
    }
    if(invisible) return -1;
    [[dont_unroll]]
    for(uint i=0u;i<source.pathCount;i++) {
        PathRecord path=uPaths.records[source.pathFirst+i];
        if(path.baseZ==object.baseZ && path.elementOrdinal>first) edges=int(path.edgesAndCorners&15u);
    }
    return edges<0?-1:((edges<<int(uScene.rotation))|(edges>>(4-int(uScene.rotation))))&15;
}
void worldRailwayCrossing(WorldTrackPart rail,int edges,uint direction,WorldObjectRecord object,uvec2 tile,
    uint colours,uint destination,bool writeRecords,inout uint count,inout uint owner)
{
    rail.image=32151u+(direction&1u);worldRailwayDraw(rail,object,tile,colours,destination,writeRecords,count,owner);
    rail.image+=2u;rail.bounds.z+=2;worldRailwayDraw(rail,object,tile,colours,destination,writeRecords,count,owner);
    bool odd=(direction&1u)!=0u;
    bool first=(edges&(odd?2:1))!=0,last=(edges&(odd?8:4))!=0;
    rail.image=first?(last?(odd?32156u:32155u):(odd?32159u:32160u)):
        (last?(odd?32158u:32157u):(odd?32162u:32161u));
    worldRailwayDraw(rail,object,tile,colours,destination,writeRecords,count,owner);
}
void worldRailwayStation(WorldTrackPart marker,WorldObjectRecord object,uvec2 tile,uint colours,
    uint destination,bool writeRecords,inout uint count)
{
    worldTrackDrawParts.count=0u;worldTrackStation(marker,object,tile,colours);
    uint owner=0xffffffffu;
    [[dont_unroll]]
    for(uint j=0u;j<worldTrackDrawParts.count;j++) {
        WorldTrackDrawPart p=worldTrackDrawParts.parts[j];
        if(p.geometry.parent>=0 && owner==0xffffffffu) continue;
        worldParentRoot=owner;
        worldSetPaintBounds(tile,p.geometry.bounds+ivec3(0,0,object.baseZ),p.geometry.size,p.geometry.parent<0?0u:1u);
        worldComponentRootLayer=uint(WORLD_TRACK_RAIL_LAYER);
        if((p.geometry.colourRole&16u)!=0u)
            worldSetForegroundTileContact(tile,object.baseZ+p.geometry.bounds.z,WORLD_FOREGROUND_RAIL_LAYER);
        WorldTrackStationCoverAnchor cover=worldTrackStationCoverAnchor(int(p.geometry.image));
        if(cover.valid) {
            worldSetComponentDepthAnchor(tile,ivec3(cover.x,cover.y,object.baseZ+p.geometry.offset.z+cover.z));
            worldComponentRootLayer=uint(WORLD_FOREGROUND_SHELL_LAYER);
        }
        uint before=count;
        emitObjectSprite(tile,object.baseZ+p.geometry.offset.z,p.geometry.offset.xy,p.sprite,p.palettes,p.effects,
            destination,writeRecords,count);
        if(p.geometry.parent<0) owner=count>before?destination+before:0xffffffffu;
    }
}
bool visitRailway(WorldObjectRecord object,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    uint type=object.trackTypeAndRideType&65535u,rideType=object.trackTypeAndRideType>>16u;
    uint ride=object.rideIdAndMazeEntry&65535u;
    if(rideType>=uTracks.words[6u] || ride>=uTracks.words[4u]) return false;
    uint styleAndSupport=uTracks.words[uTracks.words[5u]+rideType*4u];
    if(!worldRailwayStyle(styleAndSupport&65535u)) return false;
    uint row;uint direction=(object.direction+uScene.rotation)&3u;
    if(!worldRailwayLookup(type,object.sequence,direction,row)) return true;
    uint rideOffset=uTracks.words[3u]+ride*8u,scheme=object.trackData0&255u;
    if(uTracks.words[rideOffset]==0u || scheme>=4u) return true;
    uint colours=uTracks.words[rideOffset+4u+scheme],supportType=(styleAndSupport>>16u)&255u;
    bool hide=(uScene.viewFlags&(1u<<3))!=0u,invisible=hide&&(uScene.viewFlags&(1u<<29))!=0u;
    uint support=worldRailwayWord(row+4u);bool floor=false;
    if(support!=0xffffffffu && !invisible) floor=worldRailwayHasFloor(worldRailwaySupport(worldRailwayPart(support),object,supportType));
    uint first=worldRailwayWord(row+(floor?2u:0u)),n=worldRailwayWord(row+(floor?3u:1u));
    uint owner=0xffffffffu;
    int edges=type==0u?worldRailwayCrossingEdges(object,tile):-1;
    // Compute the existing element-local support cap from authored rail origins.
    // It is independent of support height, screen pixels and projection.
    worldTrackRailDepth=2147483647;
    [[dont_unroll]]
    for(uint i=0u;i<n;i++) {
        WorldTrackPart p=worldRailwayPart(first+i);
        if(p.image>=23341u && p.image<=23452u) {
            worldSetComponentDepthAnchor(tile,p.offset+ivec3(0,0,object.baseZ));
            int depth=worldAuthoredComponentDepth;
            if(p.parent>=0) {
                WorldTrackPart owner=worldRailwayPart(first+uint(p.parent));
                if(owner.image<0x7ffffu) {
                    worldSetComponentDepthAnchor(tile,owner.offset+ivec3(0,0,object.baseZ));
                    depth=max(depth,worldAuthoredComponentDepth);
                }
            }
            worldTrackRailDepth=min(worldTrackRailDepth,depth);
        }
    }
    [[dont_unroll]]
    for(uint i=0u;i<n;i++) {
        WorldTrackPart p=worldRailwayPart(first+i);
        if(p.image==0xfffffffdu) continue; // Terrain owns these same immutable tunnel requests.
        if(p.image==0xfffffffau) {
            if(p.offset.x==3) worldSupportSetSegments(worldSupportState,p.size.x,
                p.bounds.y==65535?65535:object.baseZ+p.bounds.y,p.size.y);
            else worldSupportRaiseGeneralState(worldSupportState,object.baseZ+p.bounds.y);
        } else if(p.image==0xfffffffbu) {
            owner=0xffffffffu;
            if(invisible) continue;
            WorldWoodenCursor cursor=worldRailwaySupport(p,object,supportType);
            [[dont_unroll]]
            while(cursor.phase>=0) {
                WorldWoodenPart s=worldWoodenNext(cursor);if(s.imageOffset<0) continue;
                uint sprite=worldTrackImage(uint(s.imageOffset));
                if(sprite==0xffffffffu) { owner=0xffffffffu;worldReportComponentFailure(512u,tile,writeRecords);continue; }
                bool ghost=(object.flags&1u)!=0u;
                uint palettes=ghost?uCatalog.reserved:(((colours>>16u)&255u)+1u)|((((colours>>8u)&255u)+1u)<<8u);
                uint effects=ghost?1u:2u;if(hide) { palettes=uCatalog.viewPalettes.x;effects=1u<<10; }
                worldSetPaintBounds(tile,ivec3(s.boundsX,s.boundsY,s.boundsZ),ivec3(s.sizeX,s.sizeY,s.sizeZ),0u);
                worldSetCoplanarSurfaceLayer();worldTrackSupportDepth(tile,ivec3(s.x,s.y,s.z));
                uint before=count;
                emitObjectSprite(tile,s.z,ivec2(s.x,s.y),sprite,palettes,effects,destination,writeRecords,count);
                owner=count>before?destination+before:0xffffffffu;
            }
        } else if(p.image==0xfffffffeu) worldRailwayStation(p,object,tile,colours,destination,writeRecords,count);
        else if(edges>=0 && (p.image==23341u || p.image==23342u))
            worldRailwayCrossing(p,edges,direction,object,tile,colours,destination,writeRecords,count,owner);
        else worldRailwayDraw(p,object,tile,colours,destination,writeRecords,count,owner);
    }
    return true;
}
#endif
