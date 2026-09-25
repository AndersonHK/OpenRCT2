// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <openrct2-renderer/gpu/GpuWorldTrackCatalog.h>
namespace ChairliftRules
{
#include "../../data/shaders/vulkan/world_chairlift_station_rules.glsl"
}

TEST(WorldChairliftStationRulesTest, EndpointLookupUsesRideAndFuzzyHeightWithoutTypeOrVisibilityInference)
{
    using ChairliftRules::worldChairliftNeighbourMatches;
    EXPECT_TRUE(worldChairliftNeighbourMatches(513,336,4,513,336));
    EXPECT_TRUE(worldChairliftNeighbourMatches(513,336,4,513,328));
    EXPECT_FALSE(worldChairliftNeighbourMatches(513,336,4,513,344));
    EXPECT_FALSE(worldChairliftNeighbourMatches(513,336,4,513,320));
    EXPECT_FALSE(worldChairliftNeighbourMatches(513,336,4,512,336));
    EXPECT_FALSE(worldChairliftNeighbourMatches(513,336,0,513,336));
}

TEST(WorldChairliftStationRulesTest, EndCapsAndColumnsFollowSourceEndpointDirection)
{
    using ChairliftRules::worldChairliftStationPart;
    // Direct ChairliftPaintStationNeSw/SeNw branches, not a rotated image guess.
    const std::array<int,4> beginCaps{20544,20545,20546,20547};
    const std::array<int,4> endCaps{20546,20547,20544,20545};
    for(int direction=0;direction<4;direction++) {
        SCOPED_TRACE(direction);
        EXPECT_EQ(worldChairliftStationPart(9,direction,true,false,true,true,0).image,beginCaps[direction]);
        EXPECT_EQ(worldChairliftStationPart(9,direction,false,true,true,true,0).image,endCaps[direction]);
        EXPECT_EQ(worldChairliftStationPart(0,direction,false,false,true,true,0).image,20502+direction);
        EXPECT_EQ(worldChairliftStationPart(0,direction,true,false,true,true,0).image,0);
        for(int frame=0;frame<4;frame++)
            EXPECT_EQ(worldChairliftStationPart(8,direction,true,false,true,true,frame).image,20540+frame);
        const auto back=worldChairliftStationPart(10,direction,true,false,true,true,0);
        const auto front=worldChairliftStationPart(11,direction,true,false,true,true,0);
        EXPECT_NE(back.image!=0,front.image!=0);
        if(front.image!=0) {
            // Original front column's unusual bounds are retained independently
            // of its actual raster position; no footprint-based substitution.
            EXPECT_EQ(direction&1?front.y:front.x,30);
            EXPECT_EQ(direction&1?front.by:front.bx,1);
        }
    }
}

TEST(WorldChairliftStationRulesTest, EveryFenceEndpointAndWheelVariantFitsSharedScratch)
{
    using ChairliftRules::worldChairliftStationPart;
    int maximum=0;
    for(int direction=0;direction<4;direction++)
        for(int first=0;first<2;first++) for(int last=0;last<2;last++)
            for(int back=0;back<2;back++) for(int front=0;front<2;front++)
                for(int frame=0;frame<4;frame++) {
                    int components=0,parents=0;
                    for(int event=0;event<12;event++) {
                        auto p=worldChairliftStationPart(event,direction,first,last,back,front,frame);
                        components+=p.cover>=0?2:(p.image!=0?1:0);
                        parents+=p.cover>=0?1:(p.image!=0&&p.child==0?1:0);
                        if(p.image!=0&&p.child!=0) EXPECT_GT(parents,0);
                    }
                    maximum=std::max(maximum,components);
                    EXPECT_LE(components,16);
                    EXPECT_LE(parents,12);
                }
    EXPECT_EQ(maximum,11);
}

TEST(WorldChairliftStationRulesTest, CatalogAdmitsCompleteStationAndBullwheelArtForPresentRide)
{
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    rides.rides[0].present = true;
    rides.rides[0].rideType = OpenRCT2::RIDE_TYPE_CHAIRLIFT;
    std::vector<uint32_t> images;
    const auto catalogue = OpenRCT2::Ui::Gpu::BuildWorldTrackCatalog(rides, [&](uint32_t image) {
        images.push_back(image);
        return static_cast<uint32_t>(images.size() - 1);
    });
    OpenRCT2::Ui::Gpu::ValidateWorldTrackCatalog(catalogue.words);
    for (const auto range : { std::pair{14567u,14571u}, std::pair{20502u,20507u}, std::pair{20540u,20547u} })
        for (uint32_t image = range.first; image <= range.second; ++image)
            EXPECT_NE(std::find(images.begin(), images.end(), image), images.end()) << image;
}
