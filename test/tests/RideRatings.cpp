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
#include <openrct2/core/GameTime.hpp>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/core/UnitConversion.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/RideEntry.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/RideRatings.h>
#include <openrct2/ride/ShopItem.h>
#include <cstdlib>
#include <limits>
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

TEST_F(RideRatings, RealTimeConversionsUseFortyTicksPerSecond)
{
    EXPECT_EQ(GameTime::SecondsToTicks(60), 2400u);
    EXPECT_EQ(GameTime::MinutesToTicks(60), 144000u);
    EXPECT_EQ(ToHumanReadableAirTime(40), 100);
}

TEST_F(RideRatings, LegacyRideLengthScalesToHorizontalTileLength)
{
    EXPECT_EQ(ScaleLegacyRideLengthToReal(426), 316);
    EXPECT_EQ(ToHumanReadableRideLength(ScaleLegacyRideLengthToReal(static_cast<int64_t>(426) << 16)), 316);
}

TEST_F(RideRatings, LegacyRideLengthScalingIsSignSafeAndClamped)
{
    EXPECT_EQ(ScaleLegacyRideLengthToReal(0), 0);
    EXPECT_EQ(ScaleLegacyRideLengthToReal(-426), -316);
    EXPECT_EQ(ScaleLegacyRideLengthToReal(std::numeric_limits<int64_t>::max()), std::numeric_limits<int32_t>::max());
    EXPECT_EQ(ScaleLegacyRideLengthToReal(std::numeric_limits<int64_t>::min()), std::numeric_limits<int32_t>::min());
}

TEST_F(RideRatings, ChainLiftSpeedKeepsOriginalRealWorldScale)
{
    constexpr int32_t chainLiftRawSpeed = 7 * 31079;
    EXPECT_EQ(ToHumanReadableSpeed(chainLiftRawSpeed), 7);
    EXPECT_EQ(MphToKmph(ToHumanReadableSpeed(chainLiftRawSpeed)), 11);
}

TEST_F(RideRatings, RunningCostPerHourScalesHalfMonthPaymentsToRealHour)
{
    Ride ride{};
    ride.upkeepCost = 0.64_GBP;

    EXPECT_EQ(RideGetUpkeepCostPerHour(ride), 11.25_GBP);
}

TEST_F(RideRatings, OnRidePhotoIncomeKeepsAdmissionRevenue)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    GetContext()->LoadParkFromFile(TestData::GetParkPath("small_park_with_ferris_wheel.sv6"));

    auto& gameState = getGameState();
    gameState.park.flags |= PARK_FLAGS_UNLOCK_ALL_PRICES;

    auto rideManager = RideManager(gameState);
    auto it = std::find_if(
        rideManager.begin(), rideManager.end(), [](const auto& ride) { return ride.type == RIDE_TYPE_FERRIS_WHEEL; });
    ASSERT_NE(it, rideManager.end());

    Ride& ferrisWheel = *it;
    ferrisWheel.flags.set(RideFlag::onRidePhoto);
    ferrisWheel.price[0] = 5.00_GBP;
    ferrisWheel.price[1] = 2.00_GBP;
    ferrisWheel.totalCustomers = 100;
    ferrisWheel.numSecondaryItemsSold = 25;
    std::fill(std::begin(ferrisWheel.numCustomers), std::end(ferrisWheel.numCustomers), 0);
    std::fill(
        std::begin(ferrisWheel.numSecondaryItemsSoldHistory), std::end(ferrisWheel.numSecondaryItemsSoldHistory), 0);
    ferrisWheel.numCustomers[0] = 10;
    ferrisWheel.numSecondaryItemsSoldHistory[0] = 3;

    const auto photoItem = ferrisWheel.getRideTypeDescriptor().PhotoItem;
    const auto photoProfit = ferrisWheel.price[1] - GetShopItemDescriptor(photoItem).Cost;
    const auto photoSalesPerHour = ferrisWheel.numSecondaryItemsSoldHistory[0] * 12;
    const auto expectedIncomePerHour = (RideCustomersPerHour(ferrisWheel) * ferrisWheel.price[0])
        + (photoSalesPerHour * photoProfit);

    EXPECT_EQ(ferrisWheel.calculateIncomePerHour(), expectedIncomePerHour);
}

TEST_F(RideRatings, DualItemStallIncomeUsesRecentItemSales)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    GetContext()->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));

    auto& gameState = getGameState();
    auto rideManager = RideManager(gameState);
    auto it = std::find_if(rideManager.begin(), rideManager.end(), [](const auto& ride) {
        const auto* entry = ride.getRideEntry();
        return entry != nullptr && entry->shop_item[0] != ShopItem::none && entry->shop_item[1] != ShopItem::none;
    });
    ASSERT_NE(it, rideManager.end());

    Ride& stall = *it;
    const auto* entry = stall.getRideEntry();
    ASSERT_NE(entry, nullptr);

    stall.price[0] = 3.00_GBP;
    stall.price[1] = 7.00_GBP;
    std::fill(std::begin(stall.numPrimaryItemsSoldHistory), std::end(stall.numPrimaryItemsSoldHistory), 0);
    std::fill(std::begin(stall.numSecondaryItemsSoldHistory), std::end(stall.numSecondaryItemsSoldHistory), 0);
    stall.numPrimaryItemsSoldHistory[0] = 4;
    stall.numSecondaryItemsSoldHistory[0] = 6;

    const auto primaryProfit = stall.price[0] - GetShopItemDescriptor(entry->shop_item[0]).Cost;
    const auto secondaryProfit = stall.price[1] - GetShopItemDescriptor(entry->shop_item[1]).Cost;
    const auto expectedIncomePerHour = (stall.numPrimaryItemsSoldHistory[0] * 12 * primaryProfit)
        + (stall.numSecondaryItemsSoldHistory[0] * 12 * secondaryProfit);

    EXPECT_EQ(stall.calculateIncomePerHour(), expectedIncomePerHour);
}

TEST_F(RideRatings, PreciseNewRideAgeWaitsForFullMonth)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    GetDate() = Date::FromYMD(1, MONTH_MARCH, 29);

    Ride ride{};
    ride.buildDate = RideGetCurrentBuildDate();

    GetDate() = Date::FromYMD(1, MONTH_APRIL, 0);
    EXPECT_EQ(ride.getAge(), 0);

    GetDate() = Date::FromYMD(1, MONTH_APRIL, 29);
    EXPECT_EQ(ride.getAge(), 1);
}

TEST_F(RideRatings, RecentAccumulatorAveragesLastTwentySamples)
{
    Ride ride{};

    for (size_t i = 0; i < kRideRatingRecentSampleCount + 5; i++)
    {
        const auto value = static_cast<int64_t>(i + 1);
        RideRatingAccumulator sample{};
        sample.excitement = value * 2;
        sample.intensity = value * 4;
        sample.nausea = value * 6;
        sample.ticks = 1;

        RideAddRecentRatingSample(ride, sample);
    }

    EXPECT_EQ(ride.recentRatingSampleCount, kRideRatingRecentSampleCount);

    const auto accumulator = RideGetRecentRatingAccumulator(ride);
    EXPECT_EQ(accumulator.excitement, 31);
    EXPECT_EQ(accumulator.intensity, 62);
    EXPECT_EQ(accumulator.nausea, 93);
    EXPECT_EQ(accumulator.ticks, 1u);
}

TEST_F(RideRatings, GForceTickScoringTreatsAirtimeAsExciting)
{
    const auto neutral = RideRating::ScoreGForcesForTick(100, 0);
    const auto airtime = RideRating::ScoreGForcesForTick(0, 0);

    EXPECT_EQ(neutral.excitement, 0);
    EXPECT_EQ(neutral.intensity, 0);
    EXPECT_EQ(neutral.nausea, 0);
    EXPECT_GT(airtime.excitement, 30);
    EXPECT_GT(airtime.excitement, airtime.intensity);
    EXPECT_GT(airtime.intensity, airtime.nausea);
}

TEST_F(RideRatings, GForceTickScoringMakesNegativeVerticalGNastierThanAirtime)
{
    const auto airtime = RideRating::ScoreGForcesForTick(0, 0);
    const auto negative = RideRating::ScoreGForcesForTick(-100, 0);

    EXPECT_GT(negative.excitement, airtime.excitement);
    EXPECT_GT(negative.intensity, airtime.intensity * 3);
    EXPECT_GT(negative.nausea, airtime.nausea * 5);
    EXPECT_GT(negative.intensity, negative.excitement);
}

TEST_F(RideRatings, GForceTickScoringWeightsPositiveVerticalGMostlyAsIntensity)
{
    const auto mild = RideRating::ScoreGForcesForTick(200, 0);
    const auto strong = RideRating::ScoreGForcesForTick(300, 0);

    EXPECT_GT(strong.intensity, mild.intensity * 2);
    EXPECT_GT(strong.intensity, strong.excitement);
    EXPECT_GT(strong.nausea, strong.excitement);
}

TEST_F(RideRatings, GForceTickScoringMakesLateralGSuperlinear)
{
    const auto oneG = RideRating::ScoreGForcesForTick(100, 100);
    const auto twoG = RideRating::ScoreGForcesForTick(100, 200);
    const auto severe = RideRating::ScoreGForcesForTick(100, 310);

    EXPECT_GT(twoG.intensity, oneG.intensity * 3);
    EXPECT_GT(twoG.nausea, oneG.nausea * 3);
    EXPECT_GT(severe.intensity, twoG.intensity * 3);
    EXPECT_GT(severe.nausea, twoG.nausea * 3);
    EXPECT_LT(severe.excitement, twoG.excitement);
}

TEST_F(RideRatings, EverythingPark)
{
    TestRatings("EverythingPark.park", 529);
}
