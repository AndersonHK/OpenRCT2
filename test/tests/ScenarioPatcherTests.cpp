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
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/rct12/ScenarioPatcher.h>
#include <openrct2/ride/Ride.h>

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
