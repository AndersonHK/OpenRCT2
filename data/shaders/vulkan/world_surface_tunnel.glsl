// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_SURFACE_TUNNEL
#define OPENRCT2_WORLD_SURFACE_TUNNEL
#include "world_tunnel_rules.glsl"

// Stream source requests in tile-element order. There is no private request
// array or retained CPU paint list. The 65-request limit is the original
// PaintSession side capacity, including requests rejected by cliff clearance.
struct WorldTunnelCursor { int current; int corner1; int corner2; int requests; };
void worldEmitCliffStrip(uvec2 tile, int side, int tinyZ, int offset, uvec2 material,
    uint destination, bool writeRecords, inout uint count, int boundsHeight)
{
    uint image=uint(side*5+offset+((uScene.viewFlags&1u)!=0u?20:0));
    if(image >= (material.y&0x7fffffffu)) return;
    worldSetPaintBounds(tile,ivec3(side==0?30:0,side==0?0:30,tinyZ*16),
        ivec3(side==0?0:30,side==0?30:0,boundsHeight),0u);
    emitSprite(tile,tinyZ*16,side==0?ivec2(30,0):ivec2(0,30),ivec2(0),
        material.x+image,destination,writeRecords,count);
}
void worldEmitTunnelRequest(uvec2 tile,int side,int worldZ,int type,uvec2 material,
    uint destination,bool writeRecords,inout WorldTunnelCursor cursor,inout uint count)
{
    if(cursor.requests>=65) return;
    cursor.requests++;
    int height=worldTunnelTinyZ(worldZ);
    if(height<cursor.current || height>=min(cursor.corner1,cursor.corner2)) return;
    int selected=worldTunnelResolveType(type,height,cursor.corner1,cursor.corner2);
    int image=worldTunnelImageOffset(selected,side,(material.y&0x80000000u)!=0u);
    // An incomplete edge asset cannot create a hole with no matching portal.
    if(image<0 || uint(image+1)>=(material.y&0x7fffffffu)) return;
    [[dont_unroll]]
    while(cursor.current<height) {
        worldEmitCliffStrip(tile,side,cursor.current,0,material,destination,writeRecords,count,
            cursor.current==height-1?worldTunnelLowerBounds(type):15);
        cursor.current++;
    }
    ivec2 offset=side==0?ivec2(30,0):ivec2(0,30);
    int length=(worldTunnelBoundsLength(type)+worldTunnelHeight(selected)-worldTunnelHeight(type))*16;
    int beginZ=height*16+worldTunnelBoundsOffset(type),firstLength=length;
    if(beginZ<16) { beginZ+=16;firstLength-=16; }
    ivec2 size=side==0?ivec2(32,1):ivec2(1,32);
    worldSetPaintBounds(tile,ivec3(0,0,beginZ),ivec3(size,firstLength-1),0u);
    // Paint.Surface.cpp authors distinct back/front component origins even
    // though both images use the same raster offset. Preserve that separation
    // as one constant anchor per sprite, without recovering a depth plane.
    worldSetComponentDepthAnchor(tile,ivec3(0,0,beginZ));
    emitSprite(tile,height*16,offset,ivec2(0),material.x+uint(image),destination,writeRecords,count);
    beginZ=height*16+worldTunnelBoundsOffset(type);
    if(beginZ==0) { beginZ+=16;length-=16; }
    worldSetPaintBounds(tile,ivec3(side==0?0:31,side==0?31:0,beginZ),ivec3(size,length-1),0u);
    worldSetComponentDepthAnchor(tile,ivec3(side==0?0:31,side==0?31:0,beginZ));
    emitSprite(tile,height*16,offset,ivec2(0),material.x+uint(image+1),destination,writeRecords,count);
    cursor.current+=worldTunnelHeight(selected);
}
// The flat-family catalogue owns these styles, so their tunnel metadata must
// follow the same dispatch rather than consulting the ordinary track table.
int worldStaticTunnelFamily(WorldObjectRecord item, out bool section)
{
    section=false;
    if(uFlatRides.words[0]!=0x57464c54u || uFlatRides.words[1]!=1u || uFlatRides.words[6]<8u) return 0;
    uint id=item.rideIdAndMazeEntry&65535u;
    if(id>=uFlatRides.words[3]) return 0;
    uint ride=uFlatRides.words[2]+id*20u,type=item.trackTypeAndRideType&65535u;
    if(type!=uFlatRides.words[ride+2u] && type!=uFlatRides.words[ride+3u]) return 0;
    section=type==uFlatRides.words[ride+3u];
    return int(uFlatRides.words[ride]);
}
bool worldSurfaceVerticalTunnel(SourceRecord source)
{
    bool opened=false;
    [[dont_unroll]]
    for(uint o=0u;o<source.objectCount;o++) {
        WorldObjectRecord item=uObjects.records[source.objectFirst+o];
        if(item.baseZ>=source.baseZ) break;
        if(item.kind!=4u || (item.flags&2u)!=0u) continue;
        bool section;
        int family=worldStaticTunnelFamily(item,section);
        if(family!=0) {
            int offset=worldStaticRideVerticalTunnelOffset(family,int(item.sequence),section);
            if(offset>=0) opened=worldTunnelTinyZ(item.baseZ+offset)*16==source.baseZ;
            continue;
        }
        uvec2 recipe;
        if(!worldTrackObjectRecipe(item,recipe)) continue;
        [[dont_unroll]]
        for(uint i=0u;i<recipe.y;i++) {
            WorldTrackPart request=worldTrackPart(recipe.x+i);
            if(request.image==0xfffffffdu && request.offset.x==2)
                opened=worldTunnelTinyZ(item.baseZ+request.offset.z)*16==source.baseZ;
        }
    }
    return opened;
}
void worldEmitTerrainEdge(uvec2 tile,SourceRecord source,SourceRecord other,bool valid,int edge,
    uvec2 material,uint destination,bool writeRecords,inout uint count,bool water)
{
    if(!worldSurfaceInputValid(source.baseZ,int(source.slope),int(uScene.rotation))
        || (valid && !worldSurfaceInputValid(other.baseZ,int(other.slope),int(uScene.rotation)))) return;
    int slope=terrainRelativeSlope(int(source.slope),int(uScene.rotation))|int(source.slope&16u);
    int neighbourSlope=terrainRelativeSlope(int(other.slope),int(uScene.rotation))|int(other.slope&16u);
    int c1=worldSurfaceCornerHeight(source.baseZ,slope,edge==0?3:(edge==1?1:0));
    int c2=worldSurfaceCornerHeight(source.baseZ,slope,edge<2?2:(edge==2?3:1));
    int n1=valid?worldSurfaceCornerHeight(other.baseZ,neighbourSlope,edge<2?0:(edge==2?1:3)):1;
    int n2=valid?worldSurfaceCornerHeight(other.baseZ,neighbourSlope,edge==0?1:(edge==1?3:2)):1;
    if(water && valid) {
        if(other.waterHeight/16==source.waterHeight/16) return;
        if(edge<2) { int low=min(n1,n2);n1=max(c1,low);n2=max(c2,low); }
        else { n1=max(c1,n1);n2=max(c2,n2); }
        c1=source.waterHeight/16;c2=c1;
    }
    if(c1<=n1 && c2<=n2) return;
    bool underground=(uScene.viewFlags&1u)!=0u;
    if(edge>=2 && !water && !underground) {
        uint image=uint((edge==2?33:30)+c2-c1+1);
        if(image<(material.y&0x7fffffffu)) {
            emitSprite(tile,source.baseZ,ivec2(0),ivec2(0,source.baseZ-c1*16),material.x+image,destination,writeRecords,count);
        }
        return;
    }
    if(edge>=2) {
        // Rear underground land and rear water use complete strips, not a
        // surface attachment. Their image anchor and paint bounds are distinct.
        uint bank=uint((underground?20:10)+(edge==2?5:0));
        ivec2 offset=water?ivec2(0):(edge==2?ivec2(0,-2):ivec2(-2,0));
        ivec2 size=edge==2?ivec2(30,0):ivec2(0,30);
        int current=min(n1,n2),lower=n1!=n2 && current!=c1 && current!=c2?1:0;
        int total=lower+max(0,min(c1,c2)-(current+lower));
        if(current+total<c1 || current+total<c2) total++;
        [[dont_unroll]] for(int i=0;i<total;i++) {
            int image=0;
            if(i==0 && lower!=0) image=n2>n1?4:3;
            else if(current>=min(c1,c2)) image=current>=c1?2:1;
            if(bank+uint(image)<(material.y&0x7fffffffu)) {
                worldSetPaintBounds(tile,ivec3(offset,current*16),ivec3(size,15),0u);
                emitSprite(tile,current*16,offset,ivec2(0),material.x+bank+uint(image),destination,writeRecords,count);
            }
            current++;
        }
        return;
    }
    WorldTunnelCursor cursor;
    cursor.current=min(n1,n2);cursor.corner1=c1;cursor.corner2=c2;cursor.requests=0;
    if(n1!=n2 && cursor.current!=c1 && cursor.current!=c2) {
        worldEmitCliffStrip(tile,edge,cursor.current,n2>=n1?4:3,material,destination,writeRecords,count,15);
        cursor.current++;
    }
    uint p=0u,o=0u;
    [[dont_unroll]]
    while(p<source.pathCount || o<source.objectCount) {
        bool path=p<source.pathCount;
        if(path && o<source.objectCount) {
            PathRecord a=uPaths.records[source.pathFirst+p];
            WorldObjectRecord b=uObjects.records[source.objectFirst+o];
            path=a.baseZ<b.baseZ || (a.baseZ==b.baseZ && a.elementOrdinal<b.ordinal);
        }
        int nextZ=path?uPaths.records[source.pathFirst+p].baseZ:uObjects.records[source.objectFirst+o].baseZ;
        if(nextZ>=source.baseZ) break;
        if(path) {
            PathRecord item=uPaths.records[source.pathFirst+p++];
            if(item.baseZ>=source.baseZ || (item.flags&(1u<<9))!=0u) continue;
            int edges=worldPathRotateMask(int(item.edgesAndCorners&15u),int(uScene.rotation));
            int direction=int((item.slopeDirection+uScene.rotation)&3u);
            bool sloped=(item.flags&1u)!=0u;
            int type=worldPathTunnelType(edges,direction,sloped,edge);
            if(type>=0) worldEmitTunnelRequest(tile,edge,item.baseZ+worldPathTunnelZOffset(direction,sloped,edge),type,
                material,destination,writeRecords,cursor,count);
        } else {
            WorldObjectRecord item=uObjects.records[source.objectFirst+o++];
            if(item.baseZ>=source.baseZ || item.kind!=4u || (item.flags&2u)!=0u) continue;
            bool section;
            int family=worldStaticTunnelFamily(item,section);
            if(family!=0) {
                int type=worldStaticRideTunnelType(family,int((item.direction+uScene.rotation)&3u),edge);
                if(type>=0) worldEmitTunnelRequest(tile,edge,item.baseZ,type,material,destination,writeRecords,cursor,count);
                continue;
            }
            uvec2 recipe;
            if(!worldTrackObjectRecipe(item,recipe)) continue;
            [[dont_unroll]]
            for(uint i=0u;i<recipe.y;i++) {
                WorldTrackPart request=worldTrackPart(recipe.x+i);
                if(request.image==0xfffffffdu && request.offset.x==edge)
                    worldEmitTunnelRequest(tile,edge,item.baseZ+request.offset.z,
                        worldTunnelDoorType(request.offset.y,int((item.trackData1>>8u)&255u),int((item.trackData1>>16u)&255u)),
                        material,destination,writeRecords,cursor,count);
            }
        }
    }
    [[dont_unroll]]
    while(cursor.current<min(cursor.corner1,cursor.corner2)) {
        worldEmitCliffStrip(tile,edge,cursor.current,0,material,destination,writeRecords,count,15);
        cursor.current++;
    }
    if(cursor.current<max(cursor.corner1,cursor.corner2))
        worldEmitCliffStrip(tile,edge,cursor.current,cursor.current>=cursor.corner1?2:1,material,destination,writeRecords,count,15);
}
#endif
