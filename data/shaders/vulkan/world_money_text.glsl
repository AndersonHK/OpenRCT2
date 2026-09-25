// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_MONEY_TEXT
#define OPENRCT2_WORLD_MONEY_TEXT
layout(std430,set=0,binding=28) readonly buffer WorldMoney { uint words[]; } uMoney;
const uint WORLD_MONEY_TEXT_EFFECT=1u<<12;
bool worldMoneyValid()
{
    return uMoney.words.length()>=16 && uMoney.words[0]==0x574D4F4Eu && uMoney.words[1]==1u
        && uMoney.words[2]<=65536u && uMoney.words[3]<=uint(uMoney.words.length());
}
bool worldMoneyRange(uint offset,uint count)
{
    return offset<=uMoney.words[3] && count<=uMoney.words[3]-offset;
}
// A scalar sentinel keeps transparent coverage distinct from valid index-zero
// ink without nested bool/out-parameter control flow in the fragment compiler.
const uint WORLD_MONEY_NO_PIXEL=0xffffffffu;
uint worldMoneyPixel(uint glyph,ivec2 pixel)
{
    if(!worldMoneyValid()) return WORLD_MONEY_NO_PIXEL;
    if(glyph<786448u || !worldMoneyRange(glyph,12u)) return WORLD_MONEY_NO_PIXEL;
    uint width=uMoney.words[glyph+2u],height=uMoney.words[glyph+3u];
    if(pixel.x<0 || pixel.y<0 || uint(pixel.x)>=width || uint(pixel.y)>=height) return WORLD_MONEY_NO_PIXEL;
    uint sizeHigh,size;
    umulExtended(width,height,sizeHigh,size);
    if(sizeHigh!=0u) return WORLD_MONEY_NO_PIXEL;
    uint offset=uMoney.words[glyph+4u];
    if(!worldMoneyRange(offset,(size>>2u)+uint((size&3u)!=0u))) return WORLD_MONEY_NO_PIXEL;
    uint p=uint(pixel.y)*width+uint(pixel.x);
    uint coverage=(uMoney.words[offset+p/4u]>>((p&3u)*8u))&255u;
    if(coverage==0u) return WORLD_MONEY_NO_PIXEL;
    if(uMoney.words[glyph+6u]==0u) return coverage;
    uint ink=uMoney.words[glyph+7u],hint=uMoney.words[glyph+8u];
    if(ink>255u || hint>255u) return WORLD_MONEY_NO_PIXEL;
    if(hint==0u) return ink;
    bool solid=coverage>180u;
    if(!solid && coverage<=hint) return WORLD_MONEY_NO_PIXEL;
    if(ink==0u) return solid?0x0103u:0x0102u;
    return (ink<<8u)+(solid?1u:0u);
}
#endif
