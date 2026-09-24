// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// GPU-authored painter metadata. No CPU draw list or inferred depth plane.
#ifndef OPENRCT2_WORLD_PARENT_CAPTURE
#define OPENRCT2_WORLD_PARENT_CAPTURE
const uint WORLD_PARENT_CAPACITY=1048576u;
const uint WORLD_TILE_RANGE_OFFSET=WORLD_PARENT_CAPACITY*12u;
layout(std430,set=0,binding=19) buffer ParentMetadata { uint words[]; } uParentMetadata;
ivec3 worldParentBegin,worldParentEnd;
uint worldParentRoot=0xffffffffu,worldParentFlags=0u;

// 0 parent; 1 normal child (promoted if its parent is culled); 2 attached
// (never promoted, prepended on its owning parent's attachment chain).
void worldSetPaintBounds(uvec2 tile,ivec3 offset,ivec3 size,uint flags)
{
    terrainParentPaintBounds(ivec2(tile*32u),offset,size,uScene.rotation,worldParentBegin,worldParentEnd);
    worldParentFlags=flags;
    if(flags==0u) worldParentRoot=0xffffffffu;
}
void worldSetAttachment(uint parent)
{
    worldParentRoot=parent;
    worldParentFlags=2u;
}
bool worldHasPaintOwner() { return worldParentFlags!=2u || worldParentRoot!=0xffffffffu; }
void worldCapturePaint(uint component,uint sprite,uvec2 tile,bool writeRecords)
{
    if(worldParentRoot==0xffffffffu) worldParentRoot=component;
    if(!writeRecords) return;
    uint base=component*12u;
    uParentMetadata.words[base]=uint(worldParentBegin.x);
    uParentMetadata.words[base+1u]=uint(worldParentBegin.y);
    uParentMetadata.words[base+2u]=uint(worldParentBegin.z);
    uParentMetadata.words[base+3u]=uint(worldParentEnd.x);
    uParentMetadata.words[base+4u]=uint(worldParentEnd.y);
    uParentMetadata.words[base+5u]=uint(worldParentEnd.z);
    uParentMetadata.words[base+6u]=worldParentRoot;
    uParentMetadata.words[base+7u]=sprite;
    uParentMetadata.words[base+8u]=worldParentFlags;
    uParentMetadata.words[base+9u]=tile.y*uScene.width+tile.x;
    uint previous=worldParentRoot==component?component:uParentMetadata.words[worldParentRoot*12u+10u];
    uParentMetadata.words[base+10u]=previous;
    uParentMetadata.words[base+11u]=0xffffffffu;
    if(worldParentRoot!=component) {
        uParentMetadata.words[previous*12u+11u]=component;
        uParentMetadata.words[worldParentRoot*12u+10u]=component;
    }
}
void worldCaptureTile(uvec2 tile,uint first,uint count)
{
    uint base=WORLD_TILE_RANGE_OFFSET+(tile.y*uScene.width+tile.x)*2u;
    uParentMetadata.words[base]=first;
    uParentMetadata.words[base+1u]=count;
}
#endif
