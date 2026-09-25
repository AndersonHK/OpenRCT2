// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Shared immutable programs update nine tile-local support slots and stream
// columns directly. No instance recipes or height-sized temporary arrays.
#ifndef OPENRCT2_WORLD_TRACK_SUPPORT_EMIT
#define OPENRCT2_WORLD_TRACK_SUPPORT_EMIT
#include "world_metal_support_rules.glsl"
#include "world_wooden_support_rules.glsl"

// Qualified baseline recipe ordinals include tunnel request slots. Filled only from
// successfully emitted named rail parents, never inferred from nearby sprites.
uint worldTrackRailOwners[16];
uint worldTrackQualifiedRailCount=0u;
int worldTrackRailDepth=2147483647;
void worldTrackSupportDepth(uvec2 tile,ivec3 anchor)
{
    if(worldTrackRailDepth==2147483647) return; // No admitted owner rail at this zoom.
    worldSetComponentDepthAnchor(tile,anchor);
    worldSetComponentDepthScalar(worldTrackUnderRailDepth(worldAuthoredComponentDepth,worldTrackRailDepth));
}


void worldEmitTrackSupports(WorldObjectRecord object,uvec2 tile,uint colours,
    uint destination,bool writeRecords,inout uint count)
{
    if((uTracks.words[1u]&255u)<2u || uTracks.words[13u]<8u) return;
    uint base=uTracks.words[12u];
    if(uTracks.words[base]!=0x54535054u || uTracks.words[base+1u]!=1u) return;
    uint type=object.trackTypeAndRideType&65535u,rideType=object.trackTypeAndRideType>>16u;
    if(type>=uTracks.words[8u] || rideType>=uTracks.words[6u]) return;
    uint mapped=uTracks.words[uTracks.words[7u]+type];
    uint variant=((object.flags>>8u)&1u)|((mapped>>15u)&2u);
    uint styleAndSupport=uTracks.words[uTracks.words[5u]+rideType*4u+variant];
    uint style=styleAndSupport&65535u;
    type=mapped&65535u;
    if(style>=uTracks.words[base+2u] || type>=uTracks.words[base+3u]) return;
    uint descriptor=base+uTracks.words[base+4u]+(style*uTracks.words[base+3u]+type)*3u;
    uint first=uTracks.words[descriptor],sequences=uTracks.words[descriptor+1u],mask=uTracks.words[descriptor+2u];
    if(object.sequence>=sequences) return;
    uint state=((object.flags>>6u)&1u)|((object.flags>>7u)&2u)|((object.flags>>7u)&4u);
    state|=((object.flags>>4u)&8u)|((uTracks.words[1u]>>4u)&16u)|((object.flags>>5u)&32u);
    if(worldStationHasPlatforms(object.rideIdAndMazeEntry&65535u)) state|=64u;
    variant=0u;uint shift=0u;
    for(uint bit=0u;bit<7u;bit++)
        if((mask&(1u<<bit))!=0u) { variant|=((state>>bit)&1u)<<shift;shift++; }
    uint row=base+uTracks.words[base+5u]+(first+(variant*sequences+object.sequence)*4u+
        ((object.direction+uScene.rotation)&3u))*2u;
    uint firstOp=uTracks.words[row],ops=uTracks.words[row+1u];
    bool hide=(uScene.viewFlags&(1u<<3))!=0u;
    bool invisible=hide && (uScene.viewFlags&(1u<<29))!=0u;
    bool ghost=(object.flags&1u)!=0u;
    [[dont_unroll]]
    for(uint i=0u;i<ops;i++) {
        uint op=base+uTracks.words[base+6u]+(firstOp+i)*12u;
        uint predicate=uTracks.words[op+8u];
        bool parity=(tile.x&1u)==(tile.y&1u);
        if((predicate==1u && !parity) || (predicate==2u && parity)) continue;
        uint opcode=uTracks.words[op];
        int height=int(uTracks.words[op+4u]);
        if(opcode==3u) {
            worldSupportSetSegments(worldSupportState,int(uTracks.words[op+6u]),
                height==65535?65535:object.baseZ+height,int(uTracks.words[op+7u]));
        } else if(opcode==4u) {
            worldSupportRaiseGeneralState(worldSupportState,object.baseZ+height);
        } else if((opcode==1u || opcode==2u) && !invisible) {
            int metal=int(uTracks.words[op+1u]);
            if(metal==255) metal=int((styleAndSupport>>16u)&255u);
            WorldMetalCursor cursor=worldMetalBegin(worldSupportState,metal,int(uTracks.words[op+2u]),
                int(uTracks.words[op+3u]),int(uScene.rotation),object.baseZ+height,
                int(uTracks.words[op+5u]),opcode==2u,(uTracks.words[op+11u]&1u)!=0u);
            [[dont_unroll]]
            while(cursor.phase>=0) {
                WorldMetalPart part=worldMetalNext(cursor);
                if(part.imageOffset<0) continue;
                uint sprite=worldTrackImage(uint(part.imageOffset));
                if(sprite==0xffffffffu) { worldReportComponentFailure(256u,tile,writeRecords);continue; }
                uint role=uTracks.words[op+9u];
                uint primary=role==1u?((colours>>16u)&255u):(colours&255u);
                uint secondary=role==3u?((colours>>16u)&255u):((colours>>8u)&255u);
                uint palettes=ghost?uCatalog.reserved:(role==2u?1u:((primary+1u)|((secondary+1u)<<8u)));
                uint effects=ghost||role==2u?1u:2u;
                if(hide) { palettes=uCatalog.viewPalettes.x;effects=1u<<10; }
                worldSetPaintBounds(tile,ivec3(part.boundsX,part.boundsY,part.boundsZ),
                    ivec3(part.sizeX,part.sizeY,part.sizeZ),0u);
                // A footing may share the terrain's XYZ exactly. It is placed
                // art, so it must win that local tie without moving its anchor.
                worldSetCoplanarSurfaceLayer();
                if(writeRecords) worldTrackSupportDepth(tile,ivec3(part.x,part.y,part.z));
                emitObjectSprite(tile,part.z,ivec2(part.x,part.y),sprite,palettes,effects,
                    destination,writeRecords,count);
            }
        } else if((opcode==5u || opcode==6u) && !invisible) {
            int wooden=int(uTracks.words[op+1u]);
            if(wooden==255) wooden=int((styleAndSupport>>16u)&255u);
            uint flags=uTracks.words[op+11u];
            WorldWoodenCursor cursor=worldWoodenBegin(worldSupportState,wooden,int(uTracks.words[op+2u]),
                int(uTracks.words[op+3u]),object.baseZ+height,int(uTracks.words[op+5u]),
                opcode==6u,(flags&2u)!=0u,(flags&4u)!=0u);
            [[dont_unroll]]
            while(cursor.phase>=0) {
                WorldWoodenPart part=worldWoodenNext(cursor);
                if(part.imageOffset<0) continue;
                uint sprite=worldTrackImage(uint(part.imageOffset));
                if(sprite==0xffffffffu) { worldReportComponentFailure(512u,tile,writeRecords);continue; }
                uint owner=0xffffffffu;
                if(part.orphan!=0) {
                    uint ordinal=uTracks.words[op+6u];
                    if(ordinal<worldTrackQualifiedRailCount && ordinal<16u) owner=worldTrackRailOwners[ordinal];
                    // Original AsOrphan is an explicit named owner relation.
                    // Losing that owner must never turn the transition into a
                    // free-standing parent or alias an unrelated component.
                    if(owner==0xffffffffu) { worldReportComponentFailure(1024u,tile,writeRecords);continue; }
                    if(writeRecords && owner>=uScene.outputCapacity) { worldReportComponentFailure(2048u,tile,true);continue; }
                }
                uint role=uTracks.words[op+9u];
                uint primary=role==1u?((colours>>16u)&255u):(colours&255u);
                uint secondary=role==3u?((colours>>16u)&255u):((colours>>8u)&255u);
                uint palettes=ghost?uCatalog.reserved:(role==2u?1u:((primary+1u)|((secondary+1u)<<8u)));
                uint effects=ghost||role==2u?1u:2u;
                if(hide) { palettes=uCatalog.viewPalettes.x;effects=1u<<10; }
                worldSetPaintBounds(tile,ivec3(part.boundsX,part.boundsY,part.boundsZ),
                    ivec3(part.sizeX,part.sizeY,part.sizeZ),part.orphan!=0?1u:0u);
                if(part.orphan!=0) {
                    worldParentRoot=owner;
                    // Original WoodenSupports assigns this transition to the
                    // exact rail's Children: it overlays that rail, not all rails.
                    if(writeRecords) worldSetComponentDepthScalar(uOutputs.records[owner].reserved.x);
                } else {
                    // Whole wooden arch sprites start at ground Z. Layer zero
                    // ties the terrain and LESS clips their ground-contact art.
                    // Preserve the scalar and own-rail cap; only break that tie.
                    worldSetCoplanarSurfaceLayer();
                    if(writeRecords) worldTrackSupportDepth(tile,ivec3(part.x,part.y,part.z));
                }
                emitObjectSprite(tile,part.z,ivec2(part.x,part.y),sprite,palettes,effects,
                    destination,writeRecords,count);
            }
        }
    }
}
#endif
