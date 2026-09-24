// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_SELECTION_EMIT
#define OPENRCT2_WORLD_SELECTION_EMIT
layout(std430,set=0,binding=18) readonly buffer Selection { uint words[]; } uSelection;

bool worldConstructionTile(uvec2 tile)
{
    uint key=(tile.y<<16)|tile.x, first=0u, last=uSelection.words[10];
    [[dont_unroll]]
    for(int iteration=0;iteration<19 && first<last;iteration++) {
        uint middle=first+(last-first)/2u;
        uint candidate=uSelection.words[16u+middle];
        if(candidate<key) first=middle+1u; else last=middle;
    }
    return first<uSelection.words[10] && uSelection.words[16u+first]==key;
}

int worldSelectionShape(int slope)
{
    const int offsets[32]=int[](0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15,0,0,0,0,0,0,0,17,0,0,0,16,0,18,15,0);
    return offsets[slope&31];
}

void worldEmitSelection(uvec2 tile, SourceRecord source, bool aboveWater,
    uint destination, bool writeRecords, inout uint count)
{
    uint flags=uSelection.words[0];
    if((flags&3u)==0u || source.present==0u || source.kind!=1u) return;
    ivec2 position=ivec2(tile*32u);
    bool rectangular=(flags&1u)!=0u && all(greaterThanEqual(position,ivec2(uSelection.words[2],uSelection.words[3])))
        && all(lessThanEqual(position,ivec2(uSelection.words[4],uSelection.words[5])));
    bool construction=(flags&2u)!=0u && worldConstructionTile(tile);
    if(!rectangular && !construction) return;
    int slope=terrainRelativeSlope(int(source.slope),int(uScene.rotation))|int(source.slope&16u);
    int height=source.baseZ, waterSlope=slope, waterHeight=height;
    if(source.waterHeight>height) {
        waterHeight=height+16;
        if(source.waterHeight!=waterHeight || (slope&16)==0) { waterHeight=source.waterHeight;waterSlope=0; }
        else { int low=(slope^15)&15;waterSlope=((low>>2)|(low<<2))&15; }
    }
    if(aboveWater && waterHeight==height && waterSlope==slope) return;
    if(aboveWater) { height=waterHeight;slope=waterSlope; }
    if(rectangular) {
        uint type=uSelection.words[1], family=0u, palette=4u;
        bool draw=true;
        if(type>=11u) { family=2u;palette=(type-11u+1u+uScene.rotation)&3u;draw=!aboveWater; }
        else if(type>=7u) { family=1u;palette=6u+((type-7u+uScene.rotation)&3u);draw=!aboveWater; }
        else if(type<=4u) { palette=type==4u?4u:(type+uScene.rotation)&3u;draw=!aboveWater; }
        else if(type==5u) { palette=5u;draw=aboveWater || (waterHeight==source.baseZ && waterSlope==slope); }
        // type6 selects both land and water using the scenery-ground palette.
        if(draw) {
            uint backup=worldParentRoot;
            if(aboveWater || type==5u) worldSetPaintBounds(tile,ivec3(0,0,height),ivec3(32,32,1),0u);
            else worldSetAttachment(backup);
            emitObjectSprite(tile,height,ivec2(0),uCatalog.selectionSprites[family*19u+uint(worldSelectionShape(slope))],
            uCatalog.selectionPalettes[palette],1u,destination,writeRecords,count);
            worldParentRoot=backup;
        }
    }
    if(construction) {
        uint backup=worldParentRoot;
        if(aboveWater) worldSetPaintBounds(tile,ivec3(0,0,height),ivec3(32,32,0),0u);
        else worldSetAttachment(backup);
        emitObjectSprite(tile,height,ivec2(0),uCatalog.selectionSprites[uint(worldSelectionShape(slope))],
        uCatalog.selectionPalettes[(flags&8u)!=0u?10u:4u],1u,destination,writeRecords,count);
        worldParentRoot=backup;
    }
}

bool worldHasSelectionArrow(uvec2 tile)
{
    return (uSelection.words[0]&4u)!=0u && all(equal(ivec2(tile*32u),ivec2(uSelection.words[6],uSelection.words[7])));
}
void worldEmitSelectionArrow(uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    if(!worldHasSelectionArrow(tile)) return;
    uint direction=uSelection.words[9];
    uint image=57u+((direction+uScene.rotation)&3u)+(direction&4u);
    worldSetPaintBounds(tile,ivec3(0,0,int(uSelection.words[8])+18),ivec3(32,32,-1),0u);
    emitObjectSprite(tile,int(uSelection.words[8]),ivec2(0),uCatalog.selectionSprites[image],
        uCatalog.selectionPalettes[11],1u,destination,writeRecords,count);
}
#endif
