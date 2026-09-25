// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_LARGE_TEXT_RULES
#define OPENRCT2_WORLD_LARGE_TEXT_RULES
// The caller supplies immutable codepoints and the object's original 256 glyphs.
// All loops are bounded by the original 256-byte formatted string buffer.
bool worldLargeTextVisible(int direction,int sequence,int tileCount,int zoom,int scrollingMode)
{
    return zoom<=1 && (direction==0 || direction==3) && scrollingMode!=255
        && (tileCount==1 || ((sequence-1)&3)==direction);
}
int worldLargeTextOrdinal(int ordinal,int count,int direction,bool vertical)
{
    return direction==3 || vertical?count-1-ordinal:ordinal;
}
int worldLargeTextSubmissionOrdinal(int ordinal,int count,int direction,bool vertical)
{
    // LESS depth makes the first opaque sibling win. Submit in reverse of the
    // original attachment traversal, whose last painted glyph wins instead.
    return count-1-worldLargeTextOrdinal(ordinal,count,direction,vertical);
}
int worldLargeGlyphWidth(int font,int codepoint) { return (worldLargeGlyph(font,codepoint)>>8)&255; }
int worldLargeGlyphHeight(int font,int codepoint) { return (worldLargeGlyph(font,codepoint)>>16)&255; }
int worldLargeMeasure(int font,int text,int first,int end,bool height)
{
    int total=0;
    for(int i=first;i<end;i++) {
        int cp=worldLargeCodepoint(text,i);
        total+=height?worldLargeGlyphHeight(font,cp):worldLargeGlyphWidth(font,cp);
    }
    return total;
}
int worldLargeDisplayEnd(int font,int text,int first,int end,int maximum,bool height)
{
    int total=0,i=first;
    // Original CalculateDisplayText includes the first glyph exceeding maxWidth.
    while(i<end && total<=maximum) {
        int cp=worldLargeCodepoint(text,i++);
        total+=height?worldLargeGlyphHeight(font,cp):worldLargeGlyphWidth(font,cp);
    }
    return i;
}
struct WorldLargeTextLayout { int first0;int end0;int first1;int end1;int y0;int y1;int total;int vertical; };
WorldLargeTextLayout worldLargeLayout(int font,int text,int length,int flags,int maximum,int offsetY,int direction)
{
    WorldLargeTextLayout p;
    p.first0=0;p.end0=0;p.first1=0;p.end1=0;p.y0=offsetY*2;p.y1=0;p.total=0;p.vertical=flags&1;
    if(p.vertical!=0) {
        p.end0=worldLargeDisplayEnd(font,text,0,length,maximum,true);
        p.y0+=1-worldLargeMeasure(font,text,0,p.end0,true);
    } else {
        p.y0-=direction&1;
        if((flags&2)==0 || worldLargeMeasure(font,text,0,length,false)<=maximum)
            p.end0=worldLargeDisplayEnd(font,text,0,length,maximum,false);
        else {
            int lineHeight=worldLargeGlyphHeight(font,65)+1;
            p.y0-=lineHeight;p.y1=p.y0+lineHeight*2;
            int first=0,next=length;
            for(int line=0;line<2;line++) {
                int width=0,best=first,space=first,i=first;
                for(;i<length;i++) {
                    int cp=worldLargeCodepoint(text,i);
                    width+=worldLargeGlyphWidth(font,cp);
                    if(cp==32) space=i;
                    if(width>maximum) break;
                    best=i+1;next=i+1;
                }
                if(space!=first && i!=length) { best=space;next=space+1; }
                best=worldLargeDisplayEnd(font,text,first,best,maximum,false);
                if(line==0) { p.first0=first;p.end0=best; }
                else { p.first1=first;p.end1=best; }
                first=next;
            }
        }
    }
    p.total=p.end0-p.first0+p.end1-p.first1;
    return p;
}
struct WorldLargeTextGlyph { int image;int x;int y; };
WorldLargeTextGlyph worldLargeGlyphPosition(int glyph,int direction,bool vertical,int offsetX,int offsetY,int width,int advance)
{
    int acc=offsetY*((direction&1)!=0?-1:1);
    int x=offsetX;
    if(!vertical) { x-=width/2;acc-=width/2; }
    x+=advance;acc+=advance;
    int variant=direction&1;
    if(!vertical && (((direction&1)!=0 && (acc&1)==0) || ((direction&1)==0 && (acc&1)!=0))) variant+=2;
    WorldLargeTextGlyph p;
    p.image=(glyph&255)*(vertical?2:4)+variant;
    p.x=x;
    // GLSL signed modulus does not provide C++'s negative remainder test.
    // Spell out floor division so negative odd baselines keep the same phase.
    int halfOffset=acc/2-(acc<0 && (acc&1)!=0?1:0);
    p.y=direction==3?-halfOffset:halfOffset;
    return p;
}
#endif
