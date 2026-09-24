// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Bounded auxiliary selected-train recipes. No world traversal or painter arrangement.
#ifndef OPENRCT2_WORLD_SELECTED_VEHICLE_EMIT
#define OPENRCT2_WORLD_SELECTED_VEHICLE_EMIT
layout(std430,set=0,binding=22) readonly buffer SelectedVehicle { uint words[]; } uSelected;

int worldSelectedZoomOut(int value)
{
    return uScene.zoom<0?value<<-uScene.zoom:value>>uScene.zoom;
}
OutputRecord worldSelectedComponent(uint index)
{
    uint b=uSelected.words[6u]+index*16u;
    return OutputRecord(ivec3(uSelected.words[b],uSelected.words[b+1u],uSelected.words[b+2u]),int(uSelected.words[b+3u]),
        ivec2(uSelected.words[b+4u],uSelected.words[b+5u]),ivec2(uSelected.words[b+6u],uSelected.words[b+7u]),
        uSelected.words[b+8u],uSelected.words[b+9u],uSelected.words[b+10u],0,
        int(uSelected.words[b+12u]),int(uSelected.words[b+13u]),ivec2(0));
}
bool worldSelectedImageInView(uint metadata,OutputRecord record)
{
    uint dimensions=uSelected.words[metadata+10u],offset=uSelected.words[metadata+11u];
    ivec2 size=ivec2(dimensions&65535u,dimensions>>16u);
    ivec2 originalOffset=ivec2(int(offset<<16u)>>16,int(offset)>>16);
    ivec2 p=terrainRotateXY(terrainPaintTileOrigin(record.world.xy,uScene.rotation),uScene.rotation);
    int x=p.y-p.x+originalOffset.x;
    int left=uScene.view.x,right=left+uScene.clip.z-uScene.clip.x;
    return size.x>0 && size.y>0 && worldSelectedZoomOut(x+size.x)>left && worldSelectedZoomOut(x)<right;
}
void worldEmitSelected(uint workIndex,uint destination,bool writeRecords,inout uint count)
{
    if(uScene.zoom>2 || uScene.recordCount==0u || uSelected.words[0u]!=0x56504831u || uSelected.words[1u]!=2u) return;
    uint carCount=uSelected.words[2u];
    // Host validates all counts, identities, contiguous groups and packet offsets.
    // Independent tile invocations share this bounded car work without scanning
    // all cars for every tile; every car is emitted exactly once, even on a tiny map.
    [[dont_unroll]] for(uint car=workIndex;car<carCount;car+=uScene.recordCount) {
        uint address=uSelected.words[4u]+car*12u;
        ivec4 coarse=ivec4(uSelected.words[address+8u],uSelected.words[address+9u],
            uSelected.words[address+10u],uSelected.words[address+11u]);
        for(int i=0;i<4;i++) coarse[i]=worldSelectedZoomOut(coarse[i]);
        ivec2 end=uScene.view+uScene.clip.zw-uScene.clip.xy;
        if(coarse.z<=uScene.view.x || coarse.x>=end.x || coarse.w<=uScene.view.y || coarse.y>=end.y) continue;
        uint tileIndex=uSelected.words[address+4u];
        uvec2 tile=uvec2(tileIndex%uScene.width,tileIndex/uScene.width);
        // The rendered image anchor intentionally remains the original exact
        // inverse projection. Actual car XYZ is independent retained source data.
        int componentDepth=worldComponentDepth(int(uSelected.words[address+5u]),
            int(uSelected.words[address+6u]),int(uSelected.words[address+7u]),int(uScene.rotation));
        uint first=uSelected.words[address+2u],limit=first+uSelected.words[address+3u];
        // Source owns actual XYZ per car, not per sprite. Keep that limitation
        // explicit and distinguish authored components through bounded local layers.
        if(limit-first>255u) { atomicOr(uStatus.overflow,8u);return; }
        bool admitted=false;
        worldParentRoot=0xffffffffu;worldParentFlags=0u;
        [[dont_unroll]] for(uint c=first;c<limit;c++) {
            uint metadata=uSelected.words[5u]+c*12u;
            uint kind=uSelected.words[metadata+8u]&3u;
            if(kind==0u) { admitted=false;worldParentRoot=0xffffffffu; }
            OutputRecord record=worldSelectedComponent(c);
            if(kind>=2u) {
                if(!admitted) continue;
                worldSetAttachment(worldParentRoot);
            } else {
                if(!worldSelectedImageInView(metadata,record)) continue;
                worldSetPaintBounds(tile,ivec3(0),ivec3(0),admitted?1u:0u);
            }
            bool root=worldParentRoot==0xffffffffu;
            if(writeRecords) {
                if(destination+count>=uScene.outputCapacity) { atomicOr(uStatus.overflow,1u);return; }
                // The owned car anchor includes true Z; inverse-projected raster
                // coordinates are never used for selected component depth.
                worldCapturePaint(destination+count,0u,tile,true,record);
                record.reserved.x=componentDepth;
                record.depth=int((c-first+1u)<<4u);
                uOutputs.records[destination+count]=record;
            } else if(root) worldParentRoot=destination+count;
            admitted=true;
            count++;
        }
    }
}
#endif
