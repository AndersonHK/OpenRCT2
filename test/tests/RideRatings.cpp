/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/Date.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/core/File.h>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/RideRatings.h>
#include <cstdlib>
#include <string>
#include <string_view>

using namespace OpenRCT2;

class RideRatings : public testing::Test
{
protected:
    void CalculateRatingsForAllRides()
    {
        auto& gameState = getGameState();
        for (const auto& ride : RideManager(gameState))
        {
            RideRating::UpdateRide(ride);
        }
    }

    void DumpRatings()
    {
        auto& gameState = getGameState();
        for (const auto& ride : RideManager(gameState))
        {
            std::string line = FormatRatings(ride);
            printf("%s\n", line.c_str());
        }
    }

    std::string FormatRatings(const Ride& ride)
    {
        RideRating::Tuple ratings = ride.ratings;
        auto name = std::string(ride.getRideTypeDescriptor().Name);
        std::string line = String::stdFormat(
            "%s: (%d, %d, %d)", name.c_str(), static_cast<int>(ratings.excitement), static_cast<int>(ratings.intensity),
            static_cast<int>(ratings.nausea));
        return line;
    }

    bool ShouldUpdateExpectedRatings()
    {
        auto* value = std::getenv("OPENRCT2_UPDATE_RIDE_RATINGS");
        return value != nullptr && std::string_view(value) == "1";
    }

    std::vector<u8string> GetRatingsForAllRides()
    {
        std::vector<u8string> ratings;
        auto& gameState = getGameState();
        for (const auto& ride : RideManager(gameState))
        {
            ratings.push_back(FormatRatings(ride));
        }
        return ratings;
    }

    void WriteRatings(const u8string& path, const std::vector<u8string>& ratings)
    {
        std::string data;
        for (size_t i = 0; i < ratings.size(); i++)
        {
            if (i != 0)
            {
                data += '\n';
            }
            data += ratings[i];
        }
        File::WriteAllBytes(path, data.data(), data.size());
    }

    void TestRatings(const u8string& parkFile, uint16_t expectedRideCount)
    {
        const auto parkFilePath = TestData::GetParkPath(parkFile);
        const auto ratingsDataPath = Path::Combine(TestData::GetBasePath(), u8"ratings", parkFile + u8".txt");

        // Load expected ratings
        const auto updateExpectedRatings = ShouldUpdateExpectedRatings();
        std::vector<u8string> expectedRatings;
        if (!updateExpectedRatings)
        {
            expectedRatings = File::ReadAllLines(ratingsDataPath);
            ASSERT_FALSE(expectedRatings.empty());
        }

        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;

        auto context = CreateContext();
        bool initialised = context->Initialise();
        ASSERT_TRUE(initialised);

        GetContext()->LoadParkFromFile(parkFilePath);

        // Check ride count to check load was successful
        ASSERT_EQ(RideGetCount(), expectedRideCount);

        CalculateRatingsForAllRides();

        auto actualRatings = GetRatingsForAllRides();
        if (updateExpectedRatings)
        {
            WriteRatings(ratingsDataPath, actualRatings);
            return;
        }

        // Check ride ratings
        ASSERT_EQ(actualRatings.size(), expectedRatings.size());
        for (size_t i = 0; i < actualRatings.size(); i++)
        {
            auto actual = actualRatings[i];
            auto expected = expectedRatings[i];
            ASSERT_STREQ(actual.c_str(), expected.c_str());
        }
    }
};

TEST_F(RideRatings, bpb)
{
    TestRatings("bpb.sv6", 134);
}

TEST_F(RideRatings, BigMap)
{
    TestRatings("BigMapTest.sv6", 100);
}

TEST_F(RideRatings, NewRideValueBonusUsesMultiplier)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    GetContext()->LoadParkFromFile(TestData::GetParkPath("small_park_with_ferris_wheel.sv6"));

    auto& gameState = getGameState();
    auto rideManager = RideManager(gameState);
    auto it = std::find_if(
        rideManager.begin(), rideManager.end(), [](const auto& ride) { return ride.type == RIDE_TYPE_FERRIS_WHEEL; });
    ASSERT_NE(it, rideManager.end());

    Ride& ferrisWheel = *it;
    ferrisWheel.status = RideStatus::open;
    gameState.cheats.disableRideValueAging = false;

    const auto currentMonth = static_cast<int32_t>(GetDate().GetMonthsElapsed());
    ferrisWheel.buildDate = currentMonth - 13;
    RideRating::UpdateRide(ferrisWheel);
    const auto baseValue = ferrisWheel.value;
    ASSERT_GT(baseValue, 0.00_GBP);
    const auto legacyBaseValue = static_cast<money32>(baseValue / 10);

    ferrisWheel.buildDate = currentMonth;
    RideRating::UpdateRide(ferrisWheel);
    EXPECT_EQ(ferrisWheel.value, ToMoney64(static_cast<money32>(legacyBaseValue * 3 / 2)));

    ferrisWheel.buildDate = currentMonth - 5;
    RideRating::UpdateRide(ferrisWheel);
    EXPECT_EQ(ferrisWheel.value, ToMoney64(static_cast<money32>(legacyBaseValue * 6 / 5)));
}

TEST_F(RideRatings, EverythingPark)
{
    TestRatings("EverythingPark.park", 529);
}
