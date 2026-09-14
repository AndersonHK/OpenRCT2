/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/core/FileSystem.hpp>
#include <openrct2/core/Guard.hpp>
#include <openrct2/core/Json.hpp>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/rct12/ScenarioPatcher.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/tile_element/SurfaceElement.h>

/* Test that all JSONs are with the expected formatting, otherwise the fetcher will abort
    NOTE: This will *not* test that it actually applies the patch, due to the scenarios
          not being available on the CI environment.
*/
TEST(FetchAndApplyScenarioPatch, expected_json_format)
{
    // Needs to be headless and without graphics not to prompt for RCT2 path
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = OpenRCT2::CreateContext();
    bool initialised = context->Initialise();
    ASSERT_TRUE(initialised);

    auto& env = context->GetPlatformEnvironment();
    auto scenarioPatches = env.GetDirectoryPath(OpenRCT2::DirBase::openrct2, OpenRCT2::DirId::scenarioPatches);

    std::error_code ec;
    OpenRCT2::RCT12::SetDryRun(true);
    OpenRCT2::Guard::SetAssertBehaviour(AssertBehaviour::abort);
    static const u8string dummySHA;
    for (const fs::directory_entry& entry : fs::directory_iterator(scenarioPatches, ec))
    {
        auto path = entry.path().u8string();
        if (OpenRCT2::String::endsWith(path, ".parkpatch"))
        {
            OpenRCT2::RCT12::ApplyScenarioPatch(path, dummySHA);
        }
    }
    OpenRCT2::RCT12::SetDryRun(false);
    SUCCEED();
}

TEST(FetchAndApplyScenarioPatch, RideNameOperationPreservesRideStateAndHonoursDryRun)
{
    using namespace OpenRCT2;
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    auto* ride = RideAllocateAtIndex(RideId::FromUnderlying(24));
    auto* untouched = RideAllocateAtIndex(RideId::FromUnderlying(25));
    ASSERT_NE(ride, nullptr);
    ASSERT_NE(untouched, nullptr);
    ride->type = RIDE_TYPE_SPLASH_BOATS;
    untouched->type = RIDE_TYPE_SPLASH_BOATS;
    ride->customName = "Before";
    untouched->customName = "Unchanged";
    ride->numCarsPerTrain = 3;
    ride->ratingAccumulator.ticks = 123;
    const auto status = ride->status;
    const auto patch = Path::Combine(TestData::GetBasePath(), "scenario-ride-name.parkpatch");
    RCT12::SetDryRun(true);
    RCT12::ApplyScenarioPatch(patch, "fixture-sha");
    RCT12::SetDryRun(false);
    EXPECT_EQ(ride->customName, "Before");
    RCT12::ApplyScenarioPatch(patch, "fixture-sha");
    EXPECT_EQ(ride->customName, u8"Test – Schiffschaukel");
    EXPECT_EQ(untouched->customName, "Unchanged");
    EXPECT_EQ(ride->numCarsPerTrain, 3);
    EXPECT_EQ(ride->ratingAccumulator.ticks, 123u);
    EXPECT_EQ(ride->status, status);
}

TEST(FetchAndApplyScenarioPatch, RideIdArraysApplyInOrderAndHonourDryRun)
{
    using namespace OpenRCT2;
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    for (uint16_t id = 24; id <= 27; ++id)
    {
        auto* ride = RideAllocateAtIndex(RideId::FromUnderlying(id));
        ASSERT_NE(ride, nullptr);
        ride->type = RIDE_TYPE_SPLASH_BOATS;
        ride->customName = "Before";
        ride->numCarsPerTrain = 3;
        ride->ratingAccumulator.ticks = 123;
    }
    const auto patch = Path::Combine(TestData::GetBasePath(), "scenario-ride-arrays.parkpatch");
    struct ResetDryRun
    {
        ~ResetDryRun() { RCT12::SetDryRun(false); }
    } resetDryRun;
    RCT12::SetDryRun(true);
    RCT12::ApplyScenarioPatch(patch, "fixture-arrays-sha");
    for (uint16_t id = 24; id <= 27; ++id)
        EXPECT_EQ(GetRide(RideId::FromUnderlying(id))->customName, "Before");
    RCT12::SetDryRun(false);
    RCT12::ApplyScenarioPatch(patch, "fixture-arrays-sha");
    EXPECT_EQ(GetRide(RideId::FromUnderlying(24))->customName, u8"Shared – name");
    EXPECT_EQ(GetRide(RideId::FromUnderlying(25))->customName, "Before");
    EXPECT_TRUE(GetRide(RideId::FromUnderlying(26))->customName.empty());
    EXPECT_EQ(GetRide(RideId::FromUnderlying(27))->customName, "Scalar after array");
    for (uint16_t id = 24; id <= 27; ++id)
    {
        const auto* ride = GetRide(RideId::FromUnderlying(id));
        EXPECT_EQ(ride->numCarsPerTrain, 3);
        EXPECT_EQ(ride->ratingAccumulator.ticks, 123u);
        EXPECT_EQ(ride->status, RideStatus::closed);
    }
}

TEST(FetchAndApplyScenarioPatch, OkinawaCdPatchAppliesApprovedStartingOwnership)
{
    using namespace OpenRCT2;
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 128, 128 });
    auto* ride = RideAllocateAtIndex(RideId::FromUnderlying(18));
    ASSERT_NE(ride, nullptr);
    ride->type = RIDE_TYPE_SPLASH_BOATS;
    ride->customName = "Bullet Coaster 1";
    ride->numCarsPerTrain = 3;
    ride->ratingAccumulator.ticks = 123;
    const auto untouched = MapGetSurfaceElementAt(TileCoordsXY{ 50, 50 })->getOwnership();
    const auto directory = context->GetPlatformEnvironment().GetDirectoryPath(DirBase::openrct2, DirId::scenarioPatches);
    const auto path = Path::Combine(directory, "b2eed35.parkpatch");
    RCT12::SetDryRun(true);
    RCT12::ApplyScenarioPatch(path, "b2eed35919d3992139041b68eb3fbbfa5e3fd06e2cb9058e8f50c5d9f15974bf");
    RCT12::SetDryRun(false);
    EXPECT_EQ(ride->customName, "Bullet Coaster 1");
    RCT12::SetDryRun(false);
    RCT12::ApplyScenarioPatch(path, "b2eed35919d3992139041b68eb3fbbfa5e3fd06e2cb9058e8f50c5d9f15974bf");
    const auto patch = Json::ReadFromFile(path);
    const std::array expected = {
        std::pair{ "owned", OwnershipFlag::landOwned },
        std::pair{ "construction_rights_owned", OwnershipFlag::constructionRightsOwned },
        std::pair{ "construction_rights_available", OwnershipFlag::constructionRightsForSale },
        std::pair{ "available", OwnershipFlag::landForSale },
    };
    size_t checked = 0;
    for (const auto& [key, flag] : expected)
    {
        for (const auto& coordinates : patch["land_ownership"][key]["coordinates"])
        {
            const TileCoordsXY tile{ coordinates[0].get<int32_t>(), coordinates[1].get<int32_t>() };
            ASSERT_NE(MapGetSurfaceElementAt(tile), nullptr);
            EXPECT_EQ(MapGetSurfaceElementAt(tile)->getOwnership(), OwnershipFlags{ flag });
            checked++;
        }
    }
    EXPECT_EQ(checked, 214u);
    EXPECT_EQ(MapGetSurfaceElementAt(TileCoordsXY{ 50, 50 })->getOwnership(), untouched);
    EXPECT_TRUE(ride->customName.empty());
    EXPECT_EQ(ride->numCarsPerTrain, 3);
    EXPECT_EQ(ride->ratingAccumulator.ticks, 123u);
}
