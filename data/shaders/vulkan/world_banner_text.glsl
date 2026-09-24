// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Immutable glyph columns, sampled directly by the GPU. No rendered text bitmap.
#ifndef OPENRCT2_WORLD_BANNER_TEXT
#define OPENRCT2_WORLD_BANNER_TEXT
layout(std430,set=0,binding=20) readonly buffer WorldBannerTexts { uint words[]; } uBannerTexts;
const uint WORLD_BANNER_TEXT_EFFECT=1u<<11;
bool worldBannerTextValid()
{
    return uBannerTexts.words.length()>=16 && uBannerTexts.words[0]==0x57425458u
        && uBannerTexts.words[1]==1u && uBannerTexts.words[8]>=16u
        && uBannerTexts.words[8]<=uint(uBannerTexts.words.length());
}
bool worldBannerTextRange(uint offset,uint count)
{
    uint total=uBannerTexts.words[8];
    return offset<=total && count<=total-offset;
}
uint worldBannerTextDescriptor(bool queue,uint id)
{
    if(!worldBannerTextValid()) return 0u;
    uint table=queue?4u:2u;
    if(id>=uBannerTexts.words[table+1u] || uBannerTexts.words[table+1u]>uBannerTexts.words[8]/4u) return 0u;
    uint base=uBannerTexts.words[table];
    if(!worldBannerTextRange(base,uBannerTexts.words[table+1u]*4u)) return 0u;
    uint descriptor=base+id*4u;
    return base>=16u && uBannerTexts.words[descriptor]>=16u?descriptor:0u;
}
uint worldBannerTextPixel(uint descriptor,uint mode,uint halfTick,ivec2 pixel)
{
    if(!worldBannerTextValid() || mode>=38u || descriptor<16u
        || !worldBannerTextRange(descriptor,4u) || pixel.x<0 || pixel.x>=64 || pixel.y<0 || pixel.y>=40) return 0u;
    uint modeBase=uBannerTexts.words[7];
    if(!worldBannerTextRange(modeBase,38u*64u)) return 0u;
    uint mapping=uBannerTexts.words[modeBase+mode*64u+uint(pixel.x)];
    uint source=mapping&65535u;
    int row=pixel.y-int((mapping>>16u)&255u);
    if(source==65535u || row<0 || row>=8) return 0u;
    uint columns=uBannerTexts.words[descriptor];
    uint phaseWidth=uBannerTexts.words[descriptor+1u];
    uint count=uBannerTexts.words[descriptor+2u];
    uint repeat=uBannerTexts.words[descriptor+3u];
    if(count==0u || count>uBannerTexts.words[8]/2u || !worldBannerTextRange(columns,count*2u)) return 0u;
    uint phase=phaseWidth==0u?0u:halfTick%phaseWidth;
    if(source>0xffffffffu-phase) return 0u;
    source+=phase;
    if(repeat!=0u) source%=count;
    else if(source>=count) return 0u;
    uint packed=uBannerTexts.words[columns+source*2u+uint(row/4)];
    return (packed>>uint((row&3)*8))&255u;
}
#endif
