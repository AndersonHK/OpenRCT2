// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// One constant depth per authored sprite. No painter arrangement or pixel-depth plane.
#ifndef OPENRCT2_WORLD_PARENT_CAPTURE
#define OPENRCT2_WORLD_PARENT_CAPTURE
#include "world_component_depth.glsl"
const uint WORLD_COMPONENT_DEPTH_VALID=32u;
uint worldParentRoot=0xffffffffu,worldParentFlags=0u;
// Distinct hard-failure bits. reserved records the first write-pass tile index+1.
// Count-pass status is reset by the global prefix pass; never leave stale diagnostics there.
void worldReportComponentFailure(uint bit,uvec2 tile,bool writeRecords)
{
    if(!writeRecords) return;
    atomicOr(uStatus.overflow,bit);
    atomicCompSwap(uStatus.reserved,0u,tile.y*uScene.width+tile.x+1u);
}
uint worldComponentRootLayer=0u;
bool worldComponentAnchorOverride=false;
int worldAuthoredComponentDepth=0;

// Bounds are retained at authored call sites for compatibility; only explicit
// parent/child ownership is consumed here. Bounds never infer a depth role.
void worldSetPaintBounds(uvec2 tile,ivec3 offset,ivec3 size,uint flags)
{
    worldParentFlags=flags;worldComponentAnchorOverride=false;
    if(flags==0u) { worldParentRoot=0xffffffffu;worldComponentRootLayer=0u; }
}
// Explicit surface/placed-art layer, not inferred from bounds or pixel position.
void worldSetCoplanarSurfaceLayer() { worldComponentRootLayer=1u; }
// Explicit component anchor authored independently from its raster offset.
// XY is camera-relative to the tile's facing origin; Z is absolute world height.
// Only named recipes opt in. Never infer this anchor from generic bounding boxes.
void worldSetComponentDepthScalar(int depth)
{
    worldAuthoredComponentDepth=depth;worldComponentAnchorOverride=true;
}
void worldSetComponentDepthAnchor(uvec2 tile,ivec3 anchor)
{
    ivec2 origin=terrainRotateXY(terrainPaintTileOrigin(ivec2(tile*32u),uScene.rotation),uScene.rotation);
    worldAuthoredComponentDepth=origin.x+origin.y+anchor.x+anchor.y+anchor.z;
    worldComponentAnchorOverride=true;
}
void worldSetAttachment(uint parent)
{
    worldParentRoot=parent;worldParentFlags=2u;worldComponentAnchorOverride=false;
}
bool worldHasPaintOwner() { return worldParentFlags!=2u || worldParentRoot!=0xffffffffu; }

// Each component uses its own authored XYZ. Parent ownership supplies only a
// deterministic local tie layer; it never replaces a child's distinct anchor.
// reserved.y counts children/attachments of this root, not inter-object priority.
void worldCapturePaint(uint component,uint sprite,uvec2 tile,bool writeRecords,inout OutputRecord record)
{
    if(worldParentRoot==0xffffffffu) worldParentRoot=component;
    if(!writeRecords) return;
    uint layer=worldComponentRootLayer;
    ivec2 anchor=terrainPaintTileOrigin(record.world.xy,uScene.rotation);
    int componentDepth=worldComponentDepth(anchor.x,anchor.y,record.world.z,int(uScene.rotation));
    if(worldComponentAnchorOverride) componentDepth=worldAuthoredComponentDepth;
    if(worldParentRoot!=component) {
        if(worldParentRoot>=uScene.outputCapacity) {
            worldReportComponentFailure(4096u,tile,true);record.valid=0;return;
        }
        OutputRecord parent=uOutputs.records[worldParentRoot];
        uint child=uint(parent.reserved.y)+1u;
        layer=((uint(parent.depth)>>4u)&255u)+child;
        if(layer>uint(WORLD_COMPONENT_LAYER_MAX)) { worldReportComponentFailure(32u,tile,true);record.valid=0;return; }
        uOutputs.records[worldParentRoot].reserved.y=int(child);
    }
    if(!worldComponentDepthValid(componentDepth,int(layer))) {
        worldReportComponentFailure(64u,tile,true);record.valid=0;return;
    }
    record.depth=int(layer<<4u);
    record.reserved=ivec2(componentDepth,0);
    record.valid|=int(WORLD_COMPONENT_DEPTH_VALID);
}
void worldCaptureTile(uvec2 tile,uint first,uint count) { }
#endif
