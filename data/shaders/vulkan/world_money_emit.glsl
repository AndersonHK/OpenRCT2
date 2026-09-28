// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_MONEY_EMIT
#define OPENRCT2_WORLD_MONEY_EMIT
#include "world_money_text.glsl"
void visitWorldMoney(uint index,uint destination,bool writeRecords,inout uint count)
{
    if(uScene.zoom>0 || (uScene.viewFlags&((1u<<14)|(1u<<18)))!=0u
        || !worldMoneyValid() || index>=uMoney.words[2]) return;
    uint r=16u+index*12u;
    if(!worldMoneyRange(r,12u)) return;
    ivec3 position=ivec3(int(uMoney.words[r]),int(uMoney.words[r+1u]),int(uMoney.words[r+2u]));
    if(position.x<0 || position.y<0 || position.x>=32032 || position.y>=32032) return;
    uint clipPalette=0u,clipEffects=0u;
    if(!worldEntityClip(position,clipPalette,clipEffects)) return;
    // EntityPaintSetup clips before adding floating strings, but their later
    // annotation pass does not apply PaintStruct see-through remapping.
    uint first=uMoney.words[r+8u],length=uMoney.words[r+9u];
    if(length>256u || first<786448u || !worldMoneyRange(first,length*12u)) return;
    ivec3 raster=position;
    if(uScene.rotation==1u || uScene.rotation==2u) raster.x-=32;
    if(uScene.rotation==2u || uScene.rotation==3u) raster.y-=32;
    const int wave[22]=int[22](0,1,2,2,3,3,3,3,2,2,1,0,-1,-2,-2,-3,-3,-3,-3,-2,-2,-1);
    int depth=worldComponentDepth(position.x,position.y,position.z,int(uScene.rotation));
    if(!worldComponentDepthValid(depth,0)) { if(writeRecords) atomicOr(uStatus.overflow,16u);return; }
    for(uint i=0u;i<length;i++) {
        uint g=first+i*12u;
        ivec2 size=ivec2(int(uMoney.words[g+2u]),int(uMoney.words[g+3u]));
        if(any(lessThanEqual(size,ivec2(0)))) continue;
        uint ordinal=uMoney.words[g+5u];
        int wiggle=ordinal==0xffffffffu?0:wave[(uMoney.words[r+5u]%22u+ordinal%22u)%22u];
        ivec2 offset=ivec2(int(uMoney.words[r+6u])+int(uMoney.words[g]),int(uMoney.words[g+1u])+wiggle);
        uint effects=WORLD_MONEY_TEXT_EFFECT;
        if(uMoney.words[g+6u]!=0u && uMoney.words[g+8u]!=0u) effects|=0x100u;
        // Compacted output order preserves original glyph/outline overdraw.
        // Every glyph still has one constant depth, independent of texels.
        OutputRecord record=OutputRecord(raster,1|4|32|128,size,offset,g,0u,effects,int(destination+count),uScene.zoom,0,ivec2(depth,0));
        if(writeRecords && destination+count<uScene.outputCapacity) uOutputs.records[destination+count]=record;
        count++;
    }
}
#endif
