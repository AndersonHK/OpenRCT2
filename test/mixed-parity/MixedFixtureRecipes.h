// STAGED test fixture only. Finite hand-encoded painter recipes; never call
// painter expansion, inspect PaintStruct, or select dynamic entity images here.
#pragma once
#include "MixedFixture.h"
#include <openrct2/object/SmallSceneryEntry.h>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>
namespace MixedFixtureRecipes
{
    namespace Mixed=OpenRCT2::Ui::Gpu::MixedFixture;
    using Definitions=std::array<Mixed::StaticDefinition,4>;
    inline Definitions Empty(uint32_t key,uint32_t generation)
    {
        if(generation==0) throw std::invalid_argument("Fixture object generation must be nonzero");
        Definitions result{};
        for(uint32_t rotation=0;rotation<4;++rotation)
        { result[rotation].objectKey=key; result[rotation].objectGeneration=generation; result[rotation].rotation=rotation; }
        return result;
    }
    inline Mixed::StaticPart Part(uint32_t image,int32_t x,int32_t y,int32_t z,
        int32_t bx,int32_t by,int32_t bz,int32_t sx,int32_t sy,int32_t sz,uint32_t palettes=0,uint32_t effects=0)
    {
        return {image,palettes,effects,UINT32_MAX,x,y,z,0,bx,by,bz,sx,sy,sz,0,0};
    }
    // Four images are material's immutable flat, no-grid, grass1 selectors at
    // object load, not a captured per-tile painter result. Fixture material must
    // have no map-position variation and borders must be outside the capture.
    inline Definitions FlatTerrain(uint32_t key,uint32_t generation,const std::array<uint32_t,4>& images)
    {
        auto result=Empty(key,generation);
        for(uint32_t r=0;r<4;++r) { result[r].partCount=1; result[r].parts[0]=Part(images[r],0,0,0,0,0,0,32,32,-1); }
        return result;
    }
    // Paint.Path.cpp: corners=0, flat ordinary on-ground path, no additions,
    // queue, banners, rails or same-height track. Straight raw edges5 or10 only.
    inline Definitions StraightPath(uint32_t key,uint32_t generation,uint32_t surfaceBase,uint32_t edges)
    {
        if(edges!=5 && edges!=10) throw std::invalid_argument("Fixture path must be straight edges5/10");
        auto result=Empty(key,generation);
        for(uint32_t r=0;r<4;++r)
        {
            const uint32_t rotated=((edges<<r)|(edges>>(4-r)))&15u;
            const bool horizontal=rotated==5;
            result[r].partCount=1;
            result[r].parts[0]=Part(surfaceBase+rotated,0,0,0,horizontal?0:3,horizontal?3:0,1,horizontal?32:26,horizontal?26:32,0);
        }
        return result;
    }
    // Paint.SmallScenery.cpp: immutable base body, all authored sprite offsets
    // remain in G1 metadata. No atlas trim/recentering or artificial billboards.
    inline Definitions Tree(uint32_t key,uint32_t generation,const OpenRCT2::SmallSceneryEntry& entry,
        uint32_t direction,uint32_t quadrant)
    {
        using F=OpenRCT2::SmallSceneryFlag;
        if(direction>=4 || quadrant>=4 || entry.flags.hasAny(F::isAnimated,F::canWither,F::hasGlass,F::isVisibleWhenZoomed,
            F::hasPrimaryColour,F::hasSecondaryColour,F::hasTertiaryColour,F::occupiesHalfTile,F::occupiesThreeQuarters))
            throw std::invalid_argument("Tree fixture only admits an ordinary uncoloured static body");
        auto result=Empty(key,generation); int32_t sz=int32_t(entry.height)-4; if(sz>128 || sz<0) sz=128; --sz;
        constexpr std::array<std::array<int32_t,2>,4> quadrants={{{7,7},{7,23},{23,23},{23,7}}};
        for(uint32_t r=0;r<4;++r)
        {
            int32_t x,y,sx=2,sy=2;
            if(entry.flags.has(F::occupiesFullTile))
            {
                x=y=15;
                if(entry.flags.has(F::vOffsetCentre))
                { x=y=3; sx=sy=26; if(entry.flags.has(F::prohibitWalls)) { x=y=1; sx=sy=30; } }
            }
            else { x=quadrants[(quadrant+r)&3][0]; y=quadrants[(quadrant+r)&3][1]; }
            result[r].partCount=1; result[r].parts[0]=Part(entry.image+((direction+r)&3),x,y,0,x,y,0,sx,sy,sz);
        }
        return result;
    }
    // TwisterRCTrackFlat + MetalSupportsPaintSetupCommon<false>. The fixture
    // ground must be a multiple of16, track exactly32 above it, clear flat
    // support centre, tubes, no chain and TrackPaintUtilShouldPaintSupports true.
    // Those conditions yield exactly track+base+10-unit beam+16-unit beam.
    // This is NOT a generic supports recipe; arbitrary support scene predicates
    // require semantic neighbour/support state in the retained world owner.
    inline Definitions RaisedTwisterFlat(uint32_t key,uint32_t generation,uint32_t direction,
        uint32_t trackPalettes,uint32_t trackEffects,uint32_t supportPalettes,uint32_t supportEffects)
    {
        if(direction>=4 || trackEffects>2 || supportEffects>2) throw std::invalid_argument("Unsupported track fixture colour/direction");
        auto result=Empty(key,generation);
        for(uint32_t r=0;r<4;++r)
        {
            const bool odd=((direction+r)&1)!=0; auto& d=result[r]; d.partCount=4;
            d.parts[0]=Part(odd?17147:17146,0,0,32,odd?6:0,odd?0:6,32,odd?20:32,odd?32:20,3,trackPalettes,trackEffects);
            d.parts[1]=Part(3243,16,16,0,16,16,0,0,0,5,supportPalettes,supportEffects);
            d.parts[2]=Part(3218,16,16,6,16,16,6,0,0,9,supportPalettes,supportEffects);
            d.parts[3]=Part(3224,16,16,16,16,16,16,0,0,15,supportPalettes,supportEffects);
        }
        return result;
    }
    inline std::set<uint32_t> StaticImages(std::span<const Mixed::StaticDefinition> definitions)
    {
        std::set<uint32_t> images;
        for(const auto& d:definitions)
        {
            if(d.partCount>Mixed::kPartsPerOwner) throw std::invalid_argument("Fixture must never truncate components");
            for(uint32_t i=0;i<d.partCount;++i) images.insert(d.parts[i].image);
        }
        return images;
    }
}
