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
#include <openrct2/world/Map.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <openrct2/world/tile_element/TrackElement.h>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

using namespace OpenRCT2;

class RideRatings : public testing::Test
{
protected:
    TileCoordsXY FindGrassSurfaceTile(int32_t minX, int32_t minY)
    {
        const auto& gameState = getGameState();
        for (int32_t y = minY; y < gameState.mapSize.y - 1; y++)
        {
            for (int32_t x = minX; x < gameState.mapSize.x - 1; x++)
            {
                const auto tile = TileCoordsXY{ x, y };
                const auto* surfaceElement = MapGetSurfaceElementAt(tile);
                if (surfaceElement != nullptr && surfaceElement->CanGrassGrow())
                {
                    return tile;
                }
            }
        }

        return {};
    }

    void SetSurfaceZ(const TileCoordsXY& tile, int32_t z)
    {
        auto* surfaceElement = MapGetSurfaceElementAt(tile);
        ASSERT_NE(surfaceElement, nullptr);

        surfaceElement->setBaseZ(z);
        surfaceElement->setClearanceZ(z);
        MapInvalidateTileFull(tile.ToCoordsXY());
    }

    void PlaceSmallScenery(const TileCoordsXY& tile, int32_t baseZ, int32_t clearanceZ)
    {
        auto* sceneryElement = TileElementInsert<SmallSceneryElement>({ tile.ToCoordsXY(), baseZ }, 0);
        ASSERT_NE(sceneryElement, nullptr);

        sceneryElement->setClearanceZ(clearanceZ);
        MapInvalidateTileFull(tile.ToCoordsXY());
    }

    void PlaceMazeTrack(const TileCoordsXY& tile, int32_t baseZ, int32_t clearanceZ, RideId rideId)
    {
        auto* mazeElement = TileElementInsert<TrackElement>({ tile.ToCoordsXY(), baseZ }, 0);
        ASSERT_NE(mazeElement, nullptr);

        mazeElement->setClearanceZ(clearanceZ);
        mazeElement->SetTrackType(TrackElemType::maze);
        mazeElement->SetRideType(RIDE_TYPE_MAZE);
        mazeElement->SetRideIndex(rideId);
        MapInvalidateTileFull(tile.ToCoordsXY());
    }

    void PlaceFlatTrack(const TileCoordsXY& tile, int32_t baseZ, int32_t clearanceZ, RideId rideId)
    {
        auto* trackElement = TileElementInsert<TrackElement>({ tile.ToCoordsXY(), baseZ }, 0);
        ASSERT_NE(trackElement, nullptr);

        trackElement->setClearanceZ(clearanceZ);
        trackElement->SetTrackType(TrackElemType::flatTrack1x4A);
        trackElement->SetRideType(RIDE_TYPE_MINIATURE_RAILWAY);
        trackElement->SetRideIndex(rideId);
        MapInvalidateTileFull(tile.ToCoordsXY());
    }

    void PlacePath(const TileCoordsXY& tile, int32_t baseZ, int32_t clearanceZ)
    {
        auto* pathElement = TileElementInsert<PathElement>({ tile.ToCoordsXY(), baseZ }, 0);
        ASSERT_NE(pathElement, nullptr);

        pathElement->setClearanceZ(clearanceZ);
        MapInvalidateTileFull(tile.ToCoordsXY());
    }

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

TEST_F(RideRatings, RideEntryMultipliersApplyToRawAggregateBeforeGeometricFinalRating)
{
    RideObjectEntry entry{};
    entry.excitement_multiplier = 32;
    entry.intensity_multiplier = 0;
    entry.nausea_multiplier = 64;

    const auto score = RideRating::ApplyRideEntryMultipliers({ 128, 256, 512 }, entry);

    EXPECT_EQ(score.excitement, 160);
    EXPECT_EQ(score.intensity, 256);
    EXPECT_EQ(score.nausea, 768);
}

TEST_F(RideRatings, LocalContextSceneryUsesUncappedSqrtDiminishingReturns)
{
    EXPECT_EQ(RideRating::ScoreSceneryForLocalContext(0), 0);
    EXPECT_EQ(RideRating::ScoreSceneryForLocalContext(300), 9);
    EXPECT_EQ(RideRating::ScoreSceneryForLocalContext(1200), 18);
    EXPECT_GT(RideRating::ScoreSceneryForLocalContext(4800), 18);
}

TEST_F(RideRatings, SceneryVisibilityMultiplierUsesRideTypePolicy)
{
    Ride ride{};

    ride.type = RIDE_TYPE_CIRCUS;
    EXPECT_EQ(RideRating::GetSceneryVisibilityMultiplier(ride), std::make_pair(0, 1));

    ride.type = RIDE_TYPE_HAUNTED_HOUSE;
    EXPECT_EQ(RideRating::GetSceneryVisibilityMultiplier(ride), std::make_pair(1, 2));

    ride.type = RIDE_TYPE_MAZE;
    EXPECT_EQ(RideRating::GetSceneryVisibilityMultiplier(ride), std::make_pair(1, 1));

    ride.type = RIDE_TYPE_DODGEMS;
    EXPECT_EQ(RideRating::GetSceneryVisibilityMultiplier(ride), std::make_pair(1, 4));

    ride.type = RIDE_TYPE_OBSERVATION_TOWER;
    EXPECT_EQ(RideRating::GetSceneryVisibilityMultiplier(ride), std::make_pair(1, 1));
}

TEST_F(RideRatings, LocalContextSceneryScalesWithVehicleSpeed)
{
    const RideRating::LocalContextScore contextScore = {
        .excitement = 46,
        .intensity = 7,
        .nausea = 3,
        .scenery = 18,
    };

    const auto fullSpeed = RideRating::ScoreLocalContextForVehicleTick(contextScore, 90);
    const auto halfSpeed = RideRating::ScoreLocalContextForVehicleTick(contextScore, 45);
    const auto thirdSpeed = RideRating::ScoreLocalContextForVehicleTick(contextScore, 30);

    EXPECT_EQ(fullSpeed.excitement, 46);
    EXPECT_EQ(halfSpeed.excitement, 28);
    EXPECT_EQ(thirdSpeed.excitement, 22);
    EXPECT_EQ(halfSpeed.intensity, contextScore.intensity);
    EXPECT_EQ(halfSpeed.nausea, contextScore.nausea);
}

TEST_F(RideRatings, BoatHireFreeRoamAddsGuidedTurnStatDistribution)
{
    const auto firstTick = RideRating::ScoreBoatHireFreeRoamForTick(0);
    const auto secondTick = RideRating::ScoreBoatHireFreeRoamForTick(1);

    EXPECT_EQ(firstTick.excitement + secondTick.excitement, 1);
    EXPECT_EQ(firstTick.intensity + secondTick.intensity, 2);
    EXPECT_EQ(firstTick.nausea + secondTick.nausea, 2);
}

TEST_F(RideRatings, LocalContextHeightExtendsSceneryRange)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto sceneryTile = TileCoordsXY{ 14, 10 };
    auto* sceneryElement = TileElementInsert<SmallSceneryElement>({ sceneryTile.ToCoordsXY(), 14 * kCoordsZStep }, 0);
    ASSERT_NE(sceneryElement, nullptr);
    sceneryElement->setClearanceZ(18 * kCoordsZStep);
    MapInvalidateTileFull(sceneryTile.ToCoordsXY());

    const auto groundScore = RideRating::GetLocalContextScore(
        { originTile.ToCoordsXY().ToTileCentre(), 14 * kCoordsZStep }, RideId::FromUnderlying(1));
    const auto highScore = RideRating::GetLocalContextScore(
        { originTile.ToCoordsXY().ToTileCentre(), 18 * kCoordsZStep }, RideId::FromUnderlying(1));

    EXPECT_EQ(groundScore.scenery, 0);
    EXPECT_GT(highScore.scenery, groundScore.scenery);
    EXPECT_GT(highScore.excitement, groundScore.excitement);
}

TEST_F(RideRatings, LocalContextRangeUsesHeightAboveLocalGround)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    constexpr int32_t plateauZ = 24 * kCoordsZStep;
    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto sceneryTile = TileCoordsXY{ 14, 10 };

    auto* originSurface = MapGetSurfaceElementAt(originTile);
    ASSERT_NE(originSurface, nullptr);
    originSurface->setBaseZ(plateauZ);
    originSurface->setClearanceZ(plateauZ);

    auto* scenerySurface = MapGetSurfaceElementAt(sceneryTile);
    ASSERT_NE(scenerySurface, nullptr);
    scenerySurface->setBaseZ(plateauZ);
    scenerySurface->setClearanceZ(plateauZ);

    auto* sceneryElement = TileElementInsert<SmallSceneryElement>({ sceneryTile.ToCoordsXY(), plateauZ }, 0);
    ASSERT_NE(sceneryElement, nullptr);
    sceneryElement->setClearanceZ(plateauZ + (4 * kCoordsZStep));
    MapInvalidateTileFull(sceneryTile.ToCoordsXY());

    const auto plateauScore = RideRating::GetLocalContextScore(
        { originTile.ToCoordsXY().ToTileCentre(), plateauZ }, RideId::FromUnderlying(1));
    const auto abovePlateauScore = RideRating::GetLocalContextScore(
        { originTile.ToCoordsXY().ToTileCentre(), plateauZ + (4 * kCoordsZStep) }, RideId::FromUnderlying(1));

    EXPECT_EQ(plateauScore.scenery, 0);
    EXPECT_GT(abovePlateauScore.scenery, plateauScore.scenery);
}

TEST_F(RideRatings, LocalContextRangeCountsTerrainHeightBelowOrigin)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    constexpr int32_t lowGroundZ = 14 * kCoordsZStep;
    constexpr int32_t hillGroundZ = lowGroundZ + (6 * kCoordsZStep);
    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto sceneryTile = TileCoordsXY{ 15, 10 };

    SetSurfaceZ(originTile, hillGroundZ);
    SetSurfaceZ(sceneryTile, lowGroundZ);
    PlaceSmallScenery(sceneryTile, lowGroundZ, lowGroundZ + (4 * kCoordsZStep));

    const auto score = RideRating::GetLocalContextScore(
        { originTile.ToCoordsXY().ToTileCentre(), hillGroundZ + (3 * kCoordsZStep) }, RideId::FromUnderlying(1));

    EXPECT_GT(score.scenery, 0);
    EXPECT_GT(score.excitement, 0);
}

TEST_F(RideRatings, LocalContextRangeCountsTerrainHeightAboveOrigin)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    constexpr int32_t pitGroundZ = 14 * kCoordsZStep;
    constexpr int32_t surroundingGroundZ = pitGroundZ + (9 * kCoordsZStep);
    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto sceneryTile = TileCoordsXY{ 15, 10 };

    for (int32_t x = originTile.x + 1; x <= sceneryTile.x; x++)
    {
        SetSurfaceZ({ x, originTile.y }, surroundingGroundZ);
    }
    SetSurfaceZ(originTile, pitGroundZ);
    PlaceSmallScenery(sceneryTile, surroundingGroundZ, surroundingGroundZ + (4 * kCoordsZStep));

    const auto score = RideRating::GetLocalContextScore(
        { originTile.ToCoordsXY().ToTileCentre(), pitGroundZ + (12 * kCoordsZStep) }, RideId::FromUnderlying(1));

    EXPECT_EQ(score.scenery, 0);
    EXPECT_EQ(score.excitement, 0);
}

TEST_F(RideRatings, LocalContextDoesNotSeeSceneryOnCliffAboveOrigin)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    constexpr int32_t groundZ = 14 * kCoordsZStep;
    constexpr int32_t cliffZ = groundZ + (21 * kCoordsZStep);
    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto sceneryTile = TileCoordsXY{ 12, 10 };

    SetSurfaceZ(originTile, groundZ);
    SetSurfaceZ(sceneryTile, cliffZ);
    PlaceSmallScenery(sceneryTile, cliffZ, cliffZ + (4 * kCoordsZStep));

    const auto score = RideRating::GetLocalContextScore(
        { originTile.ToCoordsXY().ToTileCentre(), groundZ + (12 * kCoordsZStep) }, RideId::FromUnderlying(1));

    EXPECT_EQ(score.scenery, 0);
    EXPECT_EQ(score.excitement, 0);
}

TEST_F(RideRatings, LocalContextInvalidatesWhenMapTileChanges)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("tile-element-tests.sv6"));
    GameLoadInit();

    const auto sceneryTile = FindGrassSurfaceTile(8, 8);
    ASSERT_GE(sceneryTile.x, 1);
    const auto originTile = TileCoordsXY{ sceneryTile.x - 1, sceneryTile.y };
    const auto* originSurface = MapGetSurfaceElementAt(originTile);
    ASSERT_NE(originSurface, nullptr);
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), originSurface->getBaseZ() };
    const auto rideId = RideId::FromUnderlying(1);

    auto* scenerySurface = MapGetSurfaceElementAt(sceneryTile);
    ASSERT_NE(scenerySurface, nullptr);
    scenerySurface->SetGrassLength(GRASS_LENGTH_CLEAR_0);
    MapInvalidateTileFull(sceneryTile.ToCoordsXY());

    const auto before = RideRating::GetLocalContextScore(origin, rideId);

    scenerySurface->SetGrassLength(GRASS_LENGTH_MOWED);
    MapInvalidateTileFull(sceneryTile.ToCoordsXY());

    const auto after = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_GT(after.scenery, before.scenery);
    EXPECT_GT(after.excitement, before.excitement);
}

TEST_F(RideRatings, LocalContextScoresSameTileVerticalInteractionsStrongly)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    const auto foreignRideId = RideId::FromUnderlying(2);
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), 14 * kCoordsZStep };

    auto* foreignTrack = TileElementInsert<TrackElement>({ originTile.ToCoordsXY(), 18 * kCoordsZStep }, 0);
    ASSERT_NE(foreignTrack, nullptr);
    foreignTrack->SetRideIndex(foreignRideId);
    foreignTrack->setClearanceZ(20 * kCoordsZStep);
    MapInvalidateTileFull(originTile.ToCoordsXY());

    const auto score = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_GT(score.verticalInteraction, score.foreignTrackProximity);
    EXPECT_GT(score.excitement, 0);
    EXPECT_GT(score.intensity, 0);
}

TEST_F(RideRatings, FixedRideContextOriginUsesRideSpecificEyeHeight)
{
    const auto stationTile = TileCoordsXY{ 10, 10 };
    constexpr int32_t baseZ = 14 * kCoordsZStep;

    Ride groundRide{};
    groundRide.id = RideId::FromUnderlying(1);
    groundRide.type = RIDE_TYPE_MERRY_GO_ROUND;
    groundRide.numStations = 1;
    groundRide.getStation().Start = stationTile.ToCoordsXY();
    groundRide.getStation().SetBaseZ(baseZ);

    Ride ferrisRide{};
    ferrisRide.id = RideId::FromUnderlying(2);
    ferrisRide.type = RIDE_TYPE_FERRIS_WHEEL;
    ferrisRide.numStations = 1;
    ferrisRide.getStation().Start = stationTile.ToCoordsXY();
    ferrisRide.getStation().SetBaseZ(baseZ);

    Ride indoorRide{};
    indoorRide.id = RideId::FromUnderlying(3);
    indoorRide.type = RIDE_TYPE_HAUNTED_HOUSE;
    indoorRide.numStations = 1;
    indoorRide.getStation().Start = stationTile.ToCoordsXY();
    indoorRide.getStation().SetBaseZ(baseZ);

    Ride towerRide{};
    towerRide.id = RideId::FromUnderlying(4);
    towerRide.type = RIDE_TYPE_OBSERVATION_TOWER;
    towerRide.numStations = 1;
    towerRide.getStation().Start = stationTile.ToCoordsXY();
    towerRide.getStation().SetBaseZ(baseZ);
    towerRide.getStation().SegmentLength = 12 << 16;

    const auto groundOrigin = RideRating::GetFixedRideLocalContextOrigin(groundRide);
    const auto ferrisOrigin = RideRating::GetFixedRideLocalContextOrigin(ferrisRide);
    const auto indoorOrigin = RideRating::GetFixedRideLocalContextOrigin(indoorRide);
    const auto towerOrigin = RideRating::GetFixedRideLocalContextOrigin(towerRide);

    EXPECT_EQ(groundOrigin.z, baseZ + (2 * kCoordsZStep));
    EXPECT_EQ(ferrisOrigin.z, baseZ + (6 * kCoordsZStep));
    EXPECT_EQ(indoorOrigin.z, groundOrigin.z);
    EXPECT_GT(ferrisOrigin.z, groundOrigin.z);
    EXPECT_GE(towerOrigin.z, baseZ + (12 * kCoordsZStep));
    EXPECT_GT(towerOrigin.z, ferrisOrigin.z);
}

TEST_F(RideRatings, FixedRideContextOriginUsesDescriptorFootprintCentre)
{
    const auto stationTile = TileCoordsXY{ 10, 10 };
    constexpr int32_t baseZ = 14 * kCoordsZStep;

    Ride groundRide{};
    groundRide.id = RideId::FromUnderlying(1);
    groundRide.type = RIDE_TYPE_MERRY_GO_ROUND;
    groundRide.numStations = 1;
    groundRide.getStation().Start = stationTile.ToCoordsXY();
    groundRide.getStation().SetBaseZ(baseZ);

    Ride ferrisRide{};
    ferrisRide.id = RideId::FromUnderlying(2);
    ferrisRide.type = RIDE_TYPE_FERRIS_WHEEL;
    ferrisRide.numStations = 1;
    ferrisRide.getStation().Start = stationTile.ToCoordsXY();
    ferrisRide.getStation().SetBaseZ(baseZ);

    const auto stationStart = stationTile.ToCoordsXY();
    const auto groundOrigin = RideRating::GetFixedRideLocalContextOrigin(groundRide);
    const auto ferrisOrigin = RideRating::GetFixedRideLocalContextOrigin(ferrisRide);

    EXPECT_EQ(groundOrigin.x, stationStart.x + kCoordsXYHalfTile);
    EXPECT_EQ(groundOrigin.y, stationStart.y + kCoordsXYHalfTile);
    EXPECT_EQ(ferrisOrigin.x, stationStart.x);
    EXPECT_EQ(ferrisOrigin.y, stationStart.y + kCoordsXYHalfTile);
}

TEST_F(RideRatings, LocalContextMazeTrackBlocksLineOfSight)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto blockerTile = TileCoordsXY{ 12, 10 };
    const auto sceneryTile = TileCoordsXY{ 14, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    constexpr int32_t originZ = groundZ + (4 * kCoordsZStep);
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), originZ };

    SetSurfaceZ(originTile, groundZ);
    SetSurfaceZ(blockerTile, groundZ);
    SetSurfaceZ(sceneryTile, groundZ);
    PlaceSmallScenery(sceneryTile, groundZ, groundZ + (3 * kCoordsZStep));

    const auto before = RideRating::GetLocalContextScore(origin, rideId);
    ASSERT_GT(before.scenery, 0);

    auto* mazeElement = TileElementInsert<TrackElement>({ blockerTile.ToCoordsXY(), groundZ }, 0);
    ASSERT_NE(mazeElement, nullptr);
    mazeElement->setClearanceZ(originZ + kCoordsZStep);
    mazeElement->SetTrackType(TrackElemType::maze);
    mazeElement->SetRideType(RIDE_TYPE_MAZE);
    mazeElement->SetRideIndex(RideId::FromUnderlying(2));
    MapInvalidateTileFull(blockerTile.ToCoordsXY());
    RideRating::ClearLocalContextCache();

    const auto after = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_EQ(after.scenery, 0);
}

TEST_F(RideRatings, LocalContextOwnMazeTrackBlocksLineOfSightToSameHeightForeignMazeTrack)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto blockerTile = TileCoordsXY{ 11, 10 };
    const auto foreignMazeTile = TileCoordsXY{ 12, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    const auto foreignRideId = RideId::FromUnderlying(2);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    constexpr int32_t clearanceZ = groundZ + (3 * kCoordsZStep);
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };

    SetSurfaceZ(originTile, groundZ);
    SetSurfaceZ(blockerTile, groundZ);
    SetSurfaceZ(foreignMazeTile, groundZ);
    PlaceMazeTrack(foreignMazeTile, groundZ, clearanceZ, foreignRideId);

    const auto before = RideRating::GetLocalContextScore(origin, rideId);
    ASSERT_GT(before.foreignTrackProximity, 0);

    PlaceMazeTrack(blockerTile, groundZ, clearanceZ, rideId);
    RideRating::ClearLocalContextCache();

    const auto after = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_EQ(after.foreignTrackProximity, 0);
}

TEST_F(RideRatings, LocalContextSeesTallSceneryOverMazeTrack)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto blockerTile = TileCoordsXY{ 12, 10 };
    const auto sceneryTile = TileCoordsXY{ 14, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    constexpr int32_t mazeTopZ = groundZ + (3 * kCoordsZStep);
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), groundZ + (4 * kCoordsZStep) };

    SetSurfaceZ(originTile, groundZ);
    SetSurfaceZ(blockerTile, groundZ);
    SetSurfaceZ(sceneryTile, groundZ);
    PlaceMazeTrack(blockerTile, groundZ, mazeTopZ, RideId::FromUnderlying(2));
    PlaceSmallScenery(sceneryTile, groundZ, groundZ + (8 * kCoordsZStep));

    const auto score = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_GT(score.scenery, 0);
}

TEST_F(RideRatings, LocalContextSeesElevatedForeignTrackOverMazeTrack)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto blockerTile = TileCoordsXY{ 11, 10 };
    const auto foreignTrackTile = TileCoordsXY{ 12, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    const auto foreignRideId = RideId::FromUnderlying(2);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };

    SetSurfaceZ(originTile, groundZ);
    SetSurfaceZ(blockerTile, groundZ);
    SetSurfaceZ(foreignTrackTile, groundZ);
    PlaceMazeTrack(blockerTile, groundZ, groundZ + (3 * kCoordsZStep), rideId);
    PlaceFlatTrack(foreignTrackTile, groundZ + (6 * kCoordsZStep), groundZ + (8 * kCoordsZStep), foreignRideId);

    const auto score = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_GT(score.foreignTrackProximity, 0);
}

TEST_F(RideRatings, LocalContextOriginMazeTileBlocksLowSceneryButSeesOverWalls)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto gardenTile = TileCoordsXY{ 11, 10 };
    const auto tallTreeTile = TileCoordsXY{ 9, 10 };
    const auto elevatedPathTile = TileCoordsXY{ 10, 11 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    constexpr int32_t mazeTopZ = groundZ + (3 * kCoordsZStep);
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };

    SetSurfaceZ(originTile, groundZ);
    SetSurfaceZ(gardenTile, groundZ);
    SetSurfaceZ(tallTreeTile, groundZ);
    SetSurfaceZ(elevatedPathTile, groundZ);
    PlaceMazeTrack(originTile, groundZ, mazeTopZ, rideId);
    PlaceSmallScenery(gardenTile, groundZ, groundZ + (2 * kCoordsZStep));

    const auto gardenOnly = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_EQ(gardenOnly.scenery, 0);

    PlaceSmallScenery(tallTreeTile, groundZ, groundZ + (8 * kCoordsZStep));
    PlacePath(elevatedPathTile, groundZ + (6 * kCoordsZStep), groundZ + (7 * kCoordsZStep));
    RideRating::ClearLocalContextCache();

    const auto overWall = RideRating::GetLocalContextScore(origin, rideId);
    EXPECT_GT(overWall.scenery, 0);
    EXPECT_GT(overWall.pathProximity, 0);
}

TEST_F(RideRatings, AggregateRatingsPreserveLoadedRatingUntilSamplesExist)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("bpb.sv6"));

    Ride* target = nullptr;
    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        const auto& rtd = ride.getRideTypeDescriptor();
        if (rtd.RatingsData.Type == RatingsCalculationType::Normal)
        {
            target = &ride;
            break;
        }
    }
    ASSERT_NE(target, nullptr);

    const RideRating::Tuple savedRatings = {
        .excitement = RideRating::make(4, 20),
        .intensity = RideRating::make(5, 10),
        .nausea = RideRating::make(2, 80),
    };
    target->status = RideStatus::open;
    target->flags.set(RideFlag::tested);
    target->ratings = savedRatings;
    target->ratingAccumulator.clear();
    RideClearRiderRatingSamples(*target);

    RideRating::UpdateRide(*target);

    EXPECT_EQ(target->ratings, savedRatings);
}

TEST_F(RideRatings, AggregateRatingsIgnoreInProgressTestAccumulator)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("bpb.sv6"));

    Ride* target = nullptr;
    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        const auto& rtd = ride.getRideTypeDescriptor();
        if (rtd.RatingsData.Type == RatingsCalculationType::Normal)
        {
            target = &ride;
            break;
        }
    }
    ASSERT_NE(target, nullptr);

    const RideRating::Tuple savedRatings = {
        .excitement = RideRating::make(4, 20),
        .intensity = RideRating::make(5, 10),
        .nausea = RideRating::make(2, 80),
    };
    target->status = RideStatus::testing;
    target->flags.set(RideFlag::tested, RideFlag::testInProgress);
    target->ratings = savedRatings;
    RideClearRiderRatingSamples(*target);
    target->ratingAccumulator.excitement = 900000;
    target->ratingAccumulator.intensity = 800000;
    target->ratingAccumulator.nausea = 700000;
    target->ratingAccumulator.ticks = 500;

    RideRating::UpdateRide(*target);

    EXPECT_EQ(target->ratings, savedRatings);
}

TEST_F(RideRatings, EverythingPark)
{
    TestRatings("EverythingPark.park", 529);
}
