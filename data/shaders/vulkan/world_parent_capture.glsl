// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Physical-depth prototype. No painter bounds, quadrant lists or ordering SSBO.
#ifndef OPENRCT2_WORLD_PARENT_CAPTURE
#define OPENRCT2_WORLD_PARENT_CAPTURE
#include "world_terrain_depth.glsl"
const uint WORLD_DEPTH_CONSTANT=0u,WORLD_DEPTH_HORIZONTAL=1u,WORLD_DEPTH_UPRIGHT=2u,
    WORLD_DEPTH_FIXED_X=3u,WORLD_DEPTH_FIXED_Y=4u,WORLD_DEPTH_TERRAIN_PLANE=5u;
const uint WORLD_DEPTH_VALID=32u;
uint worldParentRoot=0xffffffffu,worldParentFlags=0u;
uint worldPhysicalRole=WORLD_DEPTH_UPRIGHT,worldPhysicalRootLayer=0u;
bool worldPhysicalOverride=false,worldPhysicalFace=false;
int worldPhysicalFaceIntercept=0;
uint worldPhysicalPlaneBits=0u;

// Keep finite recipe parent/child ownership; bounds no longer produce priorities.
// Nonclassified art starts as an upright drawing-anchor plane, not a family rank.
void worldSetPaintBounds(uvec2 tile,ivec3 offset,ivec3 size,uint flags)
{
    worldParentFlags=flags;worldPhysicalOverride=false;worldPhysicalFace=false;worldPhysicalPlaneBits=0u;
    if(flags==0u) { worldParentRoot=0xffffffffu;worldPhysicalRole=WORLD_DEPTH_UPRIGHT;worldPhysicalRootLayer=0u; }
}
void worldSetPhysicalRole(uint role)
{
    worldPhysicalRole=role;worldPhysicalOverride=true;worldPhysicalFace=false;worldPhysicalPlaneBits=0u;
}
// Surface art/decal overlay; this is a local coplanar layer, never an owner/family rank.
void worldSetCoplanarSurfaceLayer() { worldPhysicalRootLayer=1u; }
// Exact physical face of this tile in camera-rotated world coordinates.
// Edge0/1 are X/Y at +32; edge2/3 are Y/X at0. Art offsets30/-2
// position original pixels but are not the physical boundary of a32-unit tile.
void worldSetTerrainFace(uvec2 tile,int edge)
{
    ivec2 origin=terrainRotateXY(terrainPaintTileOrigin(ivec2(tile*32u),uScene.rotation),uScene.rotation);
    bool fixedX=edge==0 || edge==3;
    worldPhysicalRole=fixedX?WORLD_DEPTH_FIXED_X:WORLD_DEPTH_FIXED_Y;
    worldPhysicalFaceIntercept=6*((fixedX?origin.x:origin.y)+(edge<2?32:0));
    worldPhysicalOverride=true;worldPhysicalFace=true;
}
// Use owned raw corners only when they define one non-edge-on physical plane.
// Nonplanar/edge-on forms retain the explicit prototype horizontal approximation.
void worldSetTerrainPlane(uvec2 tile,int baseZ,int relativeSlope)
{
    worldSetPhysicalRole(WORLD_DEPTH_HORIZONTAL);
    ivec2 origin=terrainRotateXY(terrainPaintTileOrigin(ivec2(tile*32u),uScene.rotation),uScene.rotation);
    WorldTerrainDepthPlane p=worldTerrainDepthPlane(
        16*worldSurfaceCornerHeight(baseZ,relativeSlope,0),
        16*worldSurfaceCornerHeight(baseZ,relativeSlope,1),
        16*worldSurfaceCornerHeight(baseZ,relativeSlope,2),
        16*worldSurfaceCornerHeight(baseZ,relativeSlope,3),origin.x,origin.y);
    if(p.valid==0) return;
    worldPhysicalRole=WORLD_DEPTH_TERRAIN_PLANE;
    worldPhysicalPlaneBits=(uint(p.dx+2)<<12u)|(uint(p.dy+2)<<15u);
    worldPhysicalFaceIntercept=p.twiceIntercept;
    worldPhysicalFace=true;
}
// Transitional classifier for track/static-ride recipes without authored roles.
// This is approximate: legacy zero-thickness bounds are not physical mesh data.
void worldSetPrototypeBoundsRole(ivec3 size)
{
    if(worldParentFlags==0u && size.x>0 && size.y>0 && size.z<=min(size.x,size.y)) {
        worldSetPhysicalRole(WORLD_DEPTH_HORIZONTAL);
        worldSetCoplanarSurfaceLayer();
    }
}
void worldSetAttachment(uint parent)
{
    worldParentRoot=parent;worldParentFlags=2u;worldPhysicalOverride=false;worldPhysicalFace=false;worldPhysicalPlaneBits=0u;
}
bool worldHasPaintOwner() { return worldParentFlags!=2u || worldParentRoot!=0xffffffffu; }

// Only the invocation owning this tile reads/writes its recipe parents. reserved.y
// counts local children/attachments; it is never an inter-object depth priority.
void worldCapturePaint(uint component,uint sprite,uvec2 tile,bool writeRecords,inout OutputRecord record)
{
    if(worldParentRoot==0xffffffffu) worldParentRoot=component;
    if(!writeRecords) return;
    uint role=worldPhysicalRole,layer=worldPhysicalRootLayer,planeBits=worldPhysicalPlaneBits;
    ivec2 anchor=terrainPaintTileOrigin(record.world.xy,uScene.rotation);
    anchor=terrainRotateXY(anchor,uScene.rotation);
    int sum=anchor.x+anchor.y;
    int twiceIntercept=role==WORLD_DEPTH_HORIZONTAL?6*record.world.z:
        (role==WORLD_DEPTH_UPRIGHT?3*sum:2*(sum+record.world.z));
    if(worldPhysicalFace) twiceIntercept=worldPhysicalFaceIntercept;
    if(worldParentRoot!=component) {
        OutputRecord parent=uOutputs.records[worldParentRoot];
        uint child=uint(parent.reserved.y)+1u;
        layer=((uint(parent.depth)>>4u)&255u)+child;
        if(layer>255u) { atomicOr(uStatus.overflow,8u);record.valid=0;return; }
        uOutputs.records[worldParentRoot].reserved.y=int(child);
        if(!worldPhysicalOverride) { role=uint(parent.depth)&15u;twiceIntercept=parent.reserved.x;planeBits=uint(parent.depth)&0x3f000u; }
    }
    record.depth=int(role|(layer<<4u)|planeBits);
    record.reserved=ivec2(twiceIntercept,0);
    record.valid|=int(WORLD_DEPTH_VALID);
}
void worldCaptureTile(uvec2 tile,uint first,uint count) { }
#endif
