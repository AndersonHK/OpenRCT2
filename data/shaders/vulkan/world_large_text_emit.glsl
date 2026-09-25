// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_LARGE_TEXT_EMIT
#define OPENRCT2_WORLD_LARGE_TEXT_EMIT
int worldLargeCodepoint(int text,int index) { return int(uBannerTexts.words[uint(text+index)]); }
int worldLargeGlyph(int font,int cp) { return int(uProps.words[uint(font)+8u+uint(cp>=0 && cp<256?cp:32)]); }
#include "world_large_text_rules.glsl"
void worldEmitLargeObjectText(WorldObjectRecord object,uvec2 tile,uint material,int direction,
    uint destination,bool writeRecords,inout uint count)
{
    if(!worldLargeTextVisible(direction,int(object.sequence),int(uProps.words[material+10u]),uScene.zoom,
        int(uProps.words[material+11u])) || (uProps.words[material+2u]&4u)==0u || !worldBannerTextValid()) return;
    uint font=uProps.words[material+12u],banner=object.reserved>>16u;
    uint table=uBannerTexts.words[15];
    if(font==0u || font>uint(uProps.words.length()) || 264u>uint(uProps.words.length())-font
        || table<16u || banner>=uBannerTexts.words[3] || !worldBannerTextRange(table,uBannerTexts.words[3]*2u)) return;
    uint text=uBannerTexts.words[table+banner*2u],length=uBannerTexts.words[table+banner*2u+1u];
    if(text<16u || length>255u || !worldBannerTextRange(text,length) || worldParentRoot==0xffffffffu) return;
    WorldLargeTextLayout textLayout=worldLargeLayout(int(font),int(text),int(length),int(uProps.words[font+2u]),
        int(uProps.words[font+3u]),int(uProps.words[font+5u+uint(direction&1)*2u]),direction);
    uint first=count;
    count+=uint(textLayout.total);
    if(!writeRecords) return;
    if(worldParentRoot>=uScene.outputCapacity || destination>uScene.outputCapacity
        || first>uScene.outputCapacity-destination || uint(textLayout.total)>uScene.outputCapacity-destination-first) {
        atomicOr(uStatus.overflow,16384u);
        atomicCompSwap(uStatus.reserved,0u,tile.y*uScene.width+tile.x+1u);
        return;
    }
    OutputRecord parent=uOutputs.records[worldParentRoot];
    uint layer=((uint(parent.depth)>>4u)&255u)+1u;
    if(!worldComponentDepthValid(parent.reserved.x,int(layer))) {
        atomicOr(uStatus.overflow,32768u);
        atomicCompSwap(uStatus.reserved,0u,tile.y*uScene.width+tile.x+1u);
        return;
    }
    int ordinal=0,verticalY=textLayout.y0;
    for(int line=0;line<2;line++) {
        int begin=line==0?textLayout.first0:textLayout.first1,end=line==0?textLayout.end0:textLayout.end1;
        int y=line==0?textLayout.y0:textLayout.y1;
        int width=worldLargeMeasure(int(font),int(text),begin,end,false),advance=0;
        for(int i=begin;i<end;i++) {
            int glyph=worldLargeGlyph(int(font),worldLargeCodepoint(int(text),i));
            WorldLargeTextGlyph pose=worldLargeGlyphPosition(glyph,direction,textLayout.vertical!=0,
                int(uProps.words[font+4u+uint(direction&1)*2u]),textLayout.vertical!=0?verticalY:y,width,
                textLayout.vertical!=0?0:advance);
            uint index=destination+first+uint(worldLargeTextSubmissionOrdinal(ordinal,textLayout.total,direction,textLayout.vertical!=0));
            OutputRecord record;
            // Attachments share the parent's screen origin. All glyphs are siblings;
            // Reverse opaque submission preserves original last-painted ownership
            // with LESS depth, without changing phrase positions or child layers.
            bool valid=uint(pose.image)<uProps.words[font+1u]
                && makeRecord(tile,object.baseZ,ivec2(0),ivec2(pose.x,pose.y),uProps.words[font]+uint(pose.image),record);
            if(valid) {
                record.valid|=int(WORLD_COMPONENT_DEPTH_VALID);
                record.depth=int(layer<<4u);record.reserved=ivec2(parent.reserved.x,0);
                record.palettes=((object.flags&1u)!=0u?1u:((object.colours>>8u)&255u))+1u;
                record.effects=1u;
            } else { record.valid=0; }
            uOutputs.records[index]=record;
            advance+=(glyph>>8)&255;verticalY+=((glyph>>16)&255)*2;ordinal++;
        }
    }
}
#endif
