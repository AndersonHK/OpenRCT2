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
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <limits>
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
#include <openrct2/ride/Vehicle.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <openrct2/world/tile_element/TrackElement.h>
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

    TileCoordsXY OffsetTile(const TileCoordsXY& tile, Direction direction, int32_t steps = 1)
    {
        auto coords = tile.ToCoordsXY();
        for (int32_t i = 0; i < steps; i++)
        {
            coords += CoordsDirectionDelta[direction];
        }
        return TileCoordsXY{ coords };
    }

    uint8_t PathEdgeMask(Direction direction)
    {
        return 1 << direction;
    }

    uint8_t PathEdgeMask(Direction first, Direction second)
    {
        return PathEdgeMask(first) | PathEdgeMask(second);
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

    void PlaceFlatTrack(
        const TileCoordsXY& tile, int32_t baseZ, int32_t clearanceZ, RideId rideId,
        TrackElemType trackType = TrackElemType::flatTrack1x4A, Direction direction = 0)
    {
        auto* trackElement = TileElementInsert<TrackElement>({ tile.ToCoordsXY(), baseZ }, 0);
        ASSERT_NE(trackElement, nullptr);

        trackElement->setClearanceZ(clearanceZ);
        trackElement->SetTrackType(trackType);
        trackElement->setDirection(direction);
        trackElement->SetRideType(RIDE_TYPE_MINIATURE_RAILWAY);
        trackElement->SetRideIndex(rideId);
        MapInvalidateTileFull(tile.ToCoordsXY());
    }

    void PlacePath(const TileCoordsXY& tile, int32_t baseZ, int32_t clearanceZ, uint8_t edges = 0)
    {
        auto* pathElement = TileElementInsert<PathElement>({ tile.ToCoordsXY(), baseZ }, 0);
        ASSERT_NE(pathElement, nullptr);

        pathElement->setClearanceZ(clearanceZ);
        pathElement->SetEdges(edges);
        MapInvalidateTileFull(tile.ToCoordsXY());
    }

    void PlaceBridgeLine(const TileCoordsXY& centreTile, int32_t baseZ, int32_t clearanceZ, Direction axis)
    {
        const auto reverseAxis = DirectionReverse(axis);
        PlacePath(centreTile, baseZ, clearanceZ, PathEdgeMask(axis, reverseAxis));
        PlacePath(OffsetTile(centreTile, axis), baseZ, clearanceZ, PathEdgeMask(reverseAxis));
        PlacePath(OffsetTile(centreTile, reverseAxis), baseZ, clearanceZ, PathEdgeMask(axis));
    }

    void PlaceTwoWideBridgeLine(const TileCoordsXY& centreTile, int32_t baseZ, int32_t clearanceZ, Direction axis)
    {
        PlaceBridgeLine(centreTile, baseZ, clearanceZ, axis);
        PlaceBridgeLine(OffsetTile(centreTile, static_cast<Direction>((axis + 1) & 3)), baseZ, clearanceZ, axis);
    }

    void PlacePathPlaza(const TileCoordsXY& centreTile, int32_t baseZ, int32_t clearanceZ)
    {
        PlacePath(centreTile, baseZ, clearanceZ, 0b1111);
        for (Direction direction : kAllDirections)
        {
            PlacePath(OffsetTile(centreTile, direction), baseZ, clearanceZ, PathEdgeMask(DirectionReverse(direction)));
        }
    }

    void SetVehicleSideSurfaces(const TileCoordsXY& tile, Direction trackDirection, int32_t firstSideZ, int32_t secondSideZ)
    {
        SetSurfaceZ(OffsetTile(tile, static_cast<Direction>((trackDirection + 1) & 3)), firstSideZ);
        SetSurfaceZ(OffsetTile(tile, static_cast<Direction>((trackDirection - 1) & 3)), secondSideZ);
    }

    bool IsAggregateSummaryStatGate(RatingsModifierType type)
    {
        switch (type)
        {
            case RatingsModifierType::RequirementLength:
            case RatingsModifierType::RequirementDropHeight:
            case RatingsModifierType::RequirementMaxSpeed:
            case RatingsModifierType::RequirementNumDrops:
            case RatingsModifierType::RequirementNegativeGs:
            case RatingsModifierType::RequirementLateralGs:
            case RatingsModifierType::RequirementInversions:
            case RatingsModifierType::RequirementUnsheltered:
            case RatingsModifierType::RequirementReversals:
            case RatingsModifierType::RequirementHoles:
            case RatingsModifierType::RequirementStations:
            case RatingsModifierType::RequirementSplashdown:
            case RatingsModifierType::PenaltyLateralGs:
                return true;
            default:
                return false;
        }
    }

    bool RideHasAggregateSummaryStatGate(const Ride& ride)
    {
        for (const auto& modifier : ride.getRideTypeDescriptor().RatingsData.Modifiers)
        {
            if (IsAggregateSummaryStatGate(modifier.type))
            {
                return true;
            }
        }
        return false;
    }

    Ride* FindNormalAggregateRideWithSummaryStatGate()
    {
        auto& gameState = getGameState();
        for (auto& ride : RideManager(gameState))
        {
            const auto& rtd = ride.getRideTypeDescriptor();
            if (rtd.RatingsData.Type == RatingsCalculationType::Normal && RideHasAggregateSummaryStatGate(ride))
            {
                return &ride;
            }
        }
        return nullptr;
    }

    Ride* FindMazeRide()
    {
        auto& gameState = getGameState();
        for (auto& ride : RideManager(gameState))
        {
            if (ride.type == RIDE_TYPE_MAZE)
            {
                return &ride;
            }
        }
        return nullptr;
    }

    void AddStableRecentRatingSample(Ride& ride)
    {
        RideClearRiderRatingSamples(ride);
        ride.ratingAccumulator.clear();

        RideRatingAccumulator sample{};
        sample.excitement = 100000 * RideRating::kRideRatingAccumulatorRawScale;
        sample.intensity = 90000 * RideRating::kRideRatingAccumulatorRawScale;
        sample.nausea = 80000 * RideRating::kRideRatingAccumulatorRawScale;
        sample.ticks = 100;
        RideAddRecentRatingSample(ride, sample);
    }

    void SetLegacySummaryStatsPoor(Ride& ride)
    {
        ride.getStation().SegmentLength = 0;
        ride.highestDropHeight = 0;
        ride.maxSpeed = 0;
        ride.numDrops = 0;
        ride.maxNegativeVerticalG = MakeFixed16_2dp(0, 00);
        ride.maxLateralG = MakeFixed16_2dp(4, 00);
        ride.numInversions = 0;
        ride.numHoles = 0;
        ride.shelteredLength = std::numeric_limits<int32_t>::max();
        ride.specialTrackElements.clearAll();
    }

    void SetLegacySummaryStatsStrong(Ride& ride)
    {
        ride.getStation().SegmentLength = std::numeric_limits<int32_t>::max() / 4;
        ride.highestDropHeight = std::numeric_limits<uint8_t>::max();
        ride.maxSpeed = std::numeric_limits<int32_t>::max() / 4;
        ride.numDrops = std::numeric_limits<uint8_t>::max();
        ride.maxNegativeVerticalG = MakeFixed16_2dp(-4, 00);
        ride.maxLateralG = MakeFixed16_2dp(1, 00);
        ride.numInversions = std::numeric_limits<uint8_t>::max();
        ride.numHoles = std::numeric_limits<uint8_t>::max();
        ride.shelteredLength = 0;
        ride.specialTrackElements.set(SpecialElement::splash);
    }

    void PrepareAggregateRideWithStableSample(Ride& ride)
    {
        ride.status = RideStatus::open;
        ride.flags.set(RideFlag::tested);
        ride.flags.unset(RideFlag::testInProgress);
        AddStableRecentRatingSample(ride);
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

TEST_F(RideRatings, InProgressAverageSpeedDisplaysPartialAverage)
{
    Ride ride{};
    ride.numStations = 1;
    ride.averageSpeed = 30 << 16;
    ride.getStation().SegmentTime = 2;
    ride.flags.set(RideFlag::testInProgress);

    EXPECT_EQ(ride.getDisplayAverageSpeed(), 15 << 16);

    ride.flags.unset(RideFlag::testInProgress);
    EXPECT_EQ(ride.getDisplayAverageSpeed(), 30 << 16);
}

TEST_F(RideRatings, InProgressAverageSpeedDisplaysZeroBeforeFirstSample)
{
    Ride ride{};
    ride.numStations = 1;
    ride.averageSpeed = 30 << 16;
    ride.flags.set(RideFlag::testInProgress);

    EXPECT_EQ(ride.getDisplayAverageSpeed(), 0);
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
    std::fill(std::begin(ferrisWheel.numSecondaryItemsSoldHistory), std::end(ferrisWheel.numSecondaryItemsSoldHistory), 0);
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

TEST_F(RideRatings, ActiveRatingSamplesGrowBeyondLegacyTrainLimit)
{
    Ride ride{};
    constexpr size_t sampleCount = kRideRatingLegacyActiveSampleCount + 4;

    for (size_t i = 0; i < sampleCount; i++)
    {
        const auto sampleEntity = EntityId::FromUnderlying(static_cast<EntityId::UnderlyingType>(i + 1));
        auto* sample = RideGetOrCreateActiveRatingSample(ride, sampleEntity);

        ASSERT_NE(sample, nullptr);
        sample->excitement = static_cast<int64_t>(i + 1);
        sample->ticks = 1;
    }

    EXPECT_GE(ride.activeRatingSamples.size(), sampleCount);
    for (size_t i = 0; i < sampleCount; i++)
    {
        const auto sampleEntity = EntityId::FromUnderlying(static_cast<EntityId::UnderlyingType>(i + 1));
        auto* sample = RideFindActiveRatingSample(ride, sampleEntity);

        ASSERT_NE(sample, nullptr);
        EXPECT_EQ(sample->sampleEntity, sampleEntity);
        EXPECT_TRUE(sample->hasSamples());
    }
}

TEST_F(RideRatings, ActiveRiderTrainSampleCombinesCompletedVehicleLapsAtPublish)
{
    Ride ride{};
    std::array<EntityId, 3> sampleEntities = {
        EntityId::FromUnderlying(1),
        EntityId::FromUnderlying(2),
        EntityId::FromUnderlying(3),
    };

    auto* firstVehicleSample = RideGetOrCreateActiveRatingSample(ride, sampleEntities[0]);
    ASSERT_NE(firstVehicleSample, nullptr);
    firstVehicleSample->excitement = 1000;
    firstVehicleSample->intensity = 2000;
    firstVehicleSample->nausea = 3000;
    firstVehicleSample->ticks = 10;

    auto* secondVehicleSample = RideGetOrCreateActiveRatingSample(ride, sampleEntities[1]);
    ASSERT_NE(secondVehicleSample, nullptr);
    secondVehicleSample->excitement = 5000;
    secondVehicleSample->intensity = 7000;
    secondVehicleSample->nausea = 9000;
    secondVehicleSample->ticks = 20;

    EXPECT_TRUE(RideRating::RecordActiveRiderSamples(ride, sampleEntities));
    ASSERT_EQ(ride.recentRatingSampleCount, 1);

    const auto& storedSample = ride.recentRatingSamples[0];
    EXPECT_EQ(storedSample.excitement, 3000);
    EXPECT_EQ(storedSample.intensity, 4500);
    EXPECT_EQ(storedSample.nausea, 6000);
    EXPECT_EQ(storedSample.ticks, 15u);
    EXPECT_TRUE(storedSample.sampleComplete);
    EXPECT_TRUE(storedSample.sampleEntity.IsNull());

    EXPECT_EQ(RideFindActiveRatingSample(ride, sampleEntities[0]), nullptr);
    EXPECT_EQ(RideFindActiveRatingSample(ride, sampleEntities[1]), nullptr);
}

TEST_F(RideRatings, VehicleGForcesCanUseSharedTrainVelocityForTrailerCars)
{
    Vehicle vehicle{};
    vehicle.SetTrackType(TrackElemType::leftCorkscrewUp);
    vehicle.pitch = VehiclePitch::flat;
    vehicle.roll = VehicleRoll::unbanked;
    vehicle.track_progress = 128;
    vehicle.velocity = 0;

    const auto storedVelocityGForces = vehicle.GetGForces();
    const auto trainVelocityGForces = vehicle.GetGForces(20 << 16);

    EXPECT_EQ(storedVelocityGForces.lateralG, 0);
    EXPECT_GT(std::abs(trainVelocityGForces.lateralG), 0);
}

TEST_F(RideRatings, LongitudinalGUsesSignedChangeInTrainSpeedMagnitude)
{
    constexpr int32_t oneGAcceleration = (642000 * 21) / 512;
    constexpr int32_t baseSpeed = 10 << 16;

    EXPECT_EQ(CalculateLongitudinalG(baseSpeed, baseSpeed), 0);
    EXPECT_EQ(CalculateLongitudinalG(baseSpeed, baseSpeed + oneGAcceleration), 100);
    EXPECT_EQ(CalculateLongitudinalG(baseSpeed, baseSpeed - oneGAcceleration), -100);
    EXPECT_EQ(CalculateLongitudinalG(-baseSpeed, -baseSpeed - oneGAcceleration), 100);
    EXPECT_EQ(CalculateLongitudinalG(-baseSpeed, -baseSpeed + oneGAcceleration), -100);
}

TEST_F(RideRatings, LongitudinalGDoesNotDuplicateConstantSpeedCurveForces)
{
    Vehicle vehicle{};
    vehicle.SetTrackType(TrackElemType::leftCorkscrewUp);
    vehicle.pitch = VehiclePitch::flat;
    vehicle.roll = VehicleRoll::unbanked;
    vehicle.track_progress = 128;

    auto gForces = vehicle.GetGForces(20 << 16);
    gForces.longitudinalG = CalculateLongitudinalG(20 << 16, 20 << 16);
    EXPECT_GT(std::abs(gForces.lateralG), 0);
    EXPECT_EQ(gForces.longitudinalG, 0);
}

TEST_F(RideRatings, GForceTickScoringTreatsAirtimeAsExciting)
{
    const auto neutral = RideRating::ScoreGForcesForTick(100, 0);
    const auto partialAirtime = RideRating::ScoreGForcesForTick(50, 0);
    const auto airtime = RideRating::ScoreGForcesForTick(0, 0);

    EXPECT_EQ(neutral.excitement, 0);
    EXPECT_EQ(neutral.intensity, 0);
    EXPECT_EQ(neutral.nausea, 0);
    EXPECT_GT(partialAirtime.excitement, 0);
    EXPECT_GT(airtime.excitement, partialAirtime.excitement * 4);
    EXPECT_GT(airtime.excitement, airtime.intensity);
    EXPECT_GT(airtime.intensity, airtime.nausea);
}

TEST_F(RideRatings, GForceTickScoringMakesNegativeVerticalGNastierThanAirtime)
{
    const auto airtime = RideRating::ScoreGForcesForTick(0, 0);
    const auto negative = RideRating::ScoreGForcesForTick(-100, 0);

    EXPECT_GT(negative.excitement, airtime.excitement);
    EXPECT_GT(negative.excitement * 2, airtime.excitement * 3);
    EXPECT_GT(negative.intensity, airtime.intensity * 5);
    EXPECT_GT(negative.intensity, negative.excitement);
}

TEST_F(RideRatings, GForceTickScoringRewardsNormalPositiveVerticalGAndPunishesExcess)
{
    const auto mild = RideRating::ScoreGForcesForTick(200, 0);
    const auto strong = RideRating::ScoreGForcesForTick(300, 0);
    const auto excessive = RideRating::ScoreGForcesForTick(450, 0);

    EXPECT_GT(mild.excitement, 0);
    EXPECT_GT(mild.excitement, mild.intensity);
    EXPECT_GT(strong.excitement, mild.excitement * 3);
    EXPECT_GT(strong.intensity, mild.intensity * 3);
    EXPECT_GT(strong.intensity, strong.excitement);
    EXPECT_GT(excessive.intensity, excessive.excitement);
    EXPECT_GT(excessive.intensity, strong.intensity);
}

TEST_F(RideRatings, GForceTickScoringMakesLateralGSuperlinear)
{
    const auto oneG = RideRating::ScoreGForcesForTick(100, 100);
    const auto twoG = RideRating::ScoreGForcesForTick(100, 200);
    const auto severe = RideRating::ScoreGForcesForTick(100, 310);

    EXPECT_GT(twoG.excitement, oneG.excitement * 3);
    EXPECT_GT(twoG.intensity, oneG.intensity * 4);
    EXPECT_GT(twoG.intensity, twoG.excitement);
    EXPECT_GT(severe.intensity, twoG.intensity * 5);
    EXPECT_LT(severe.excitement, twoG.excitement);
}

TEST_F(RideRatings, LongitudinalGScoringDistinguishesAccelerationFromBraking)
{
    const auto acceleration = RideRating::ScoreLongitudinalGForTick(100);
    const auto braking = RideRating::ScoreLongitudinalGForTick(-100);
    const auto lateral = RideRating::ScoreLateralGForTick(100);
    const auto vertical = RideRating::ScorePositiveVerticalGForTick(200);

    EXPECT_GT(acceleration.excitement, lateral.excitement);
    EXPECT_LT(acceleration.excitement, vertical.excitement);
    EXPECT_LT(acceleration.intensity, lateral.intensity);
    EXPECT_LT(acceleration.nausea, lateral.nausea);
    EXPECT_LT(braking.excitement, acceleration.excitement);
    EXPECT_GT(braking.intensity, acceleration.intensity);
    EXPECT_GT(braking.nausea, acceleration.nausea);
    EXPECT_LT(braking.intensity, lateral.intensity);
    EXPECT_LT(braking.nausea, lateral.nausea);
}

TEST_F(RideRatings, VehicleSpeedTickScoringUsesPowerOnePointFiveForExcitement)
{
    const auto stopped = RideRating::ScoreVehicleSpeedForTick(0);
    const auto halfSpeed = RideRating::ScoreVehicleSpeedForTick(RideRating::kVehicleRatingBaselineSpeed / 2);
    const auto normalSpeed = RideRating::ScoreVehicleSpeedForTick(RideRating::kVehicleRatingBaselineSpeed);
    const auto doubleSpeed = RideRating::ScoreVehicleSpeedForTick(RideRating::kVehicleRatingBaselineSpeed * 2);

    EXPECT_EQ(stopped.excitement, 0);
    EXPECT_EQ(stopped.intensity, 0);
    EXPECT_EQ(stopped.nausea, 0);
    EXPECT_EQ(normalSpeed.excitement, (90 * RideRating::kRideRatingAccumulatorRawScale) / 5);
    EXPECT_EQ(normalSpeed.intensity, (90 * RideRating::kRideRatingAccumulatorRawScale) / 4);
    EXPECT_EQ(normalSpeed.nausea, (90 * RideRating::kRideRatingAccumulatorRawScale) / 8);
    EXPECT_LT(halfSpeed.excitement * 2, normalSpeed.excitement);
    EXPECT_GT(doubleSpeed.excitement, normalSpeed.excitement * 2);
    EXPECT_NEAR(halfSpeed.excitement, normalSpeed.excitement * std::pow(0.5, 1.5), 100);
    EXPECT_NEAR(doubleSpeed.excitement, normalSpeed.excitement * std::pow(2.0, 1.5), 100);
    EXPECT_EQ(doubleSpeed.intensity, normalSpeed.intensity * 2);
    EXPECT_EQ(doubleSpeed.nausea, normalSpeed.nausea * 2);
}

TEST_F(RideRatings, TransportQualityIsDistanceWeightedAndGForcesReduceComfort)
{
    const RideRating::LocalContextScore plainContext{};
    const RideRating::LocalContextScore decoratedContext = { .scenery = 50 };
    const auto smooth = RideRating::ScoreTransportQualityForVehicleTick(100, 0, 0, 90, plainContext);
    const auto rough = RideRating::ScoreTransportQualityForVehicleTick(130, 100, 50, 90, decoratedContext);

    EXPECT_EQ(smooth.distance, 90);
    EXPECT_EQ(smooth.comfort / smooth.distance, 1000);
    EXPECT_EQ(smooth.decoration / smooth.distance, 1000);
    EXPECT_LT(rough.comfort / rough.distance, smooth.comfort / smooth.distance);
    EXPECT_EQ(rough.decoration / rough.distance, 1250);
}

TEST_F(RideRatings, TransportFareValueIsLedByDistanceAndModifiedByQuality)
{
    Ride ride{};
    ride.type = RIDE_TYPE_MONORAIL;
    ride.numStations = 2;
    ride.stableStats.valid = true;
    ride.stableStats.averageSpeed = 18 * 29127;
    ride.stableStats.maxSpeed = ride.stableStats.averageSpeed;
    ride.stableStats.stations[0].SegmentLength = 400 << 16;
    ride.recentRatingSampleCount = 1;
    ride.recentRatingSamples[0].ticks = 1;
    ride.recentRatingSamples[0].transportDistance = 100;
    ride.recentRatingSamples[0].transportComfort = 900 * 100;
    ride.recentRatingSamples[0].transportDecoration = 1100 * 100;

    EXPECT_TRUE(RideUsesTargetPricing(ride));
    const auto quality = RideGetTransportQuality(ride);
    EXPECT_TRUE(quality.hasMeasurements);
    EXPECT_EQ(quality.comfortPermille, 900);
    EXPECT_EQ(quality.decorationPermille, 1100);

    ride.stableStats.stations[0].SegmentTime = 75;
    const auto measuredSegment = RideGetTransportSegment(ride, StationIndex::FromUnderlying(0), quality);
    EXPECT_EQ(measuredSegment.travelTimeMilliseconds, 75'000);
    EXPECT_EQ(measuredSegment.distanceMetres, 400);

    const auto baseValue = RideGetTransportSegment(ride, StationIndex::FromUnderlying(0)).fareValue;
    ride.stableStats.stations[0].SegmentLength = 800 << 16;
    const auto longerValue = RideGetTransportSegment(ride, StationIndex::FromUnderlying(0)).fareValue;
    ride.recentRatingSamples[0].transportComfort = 500 * 100;
    const auto uncomfortableValue = RideGetTransportSegment(ride, StationIndex::FromUnderlying(0)).fareValue;

    EXPECT_GT(baseValue, 0.20_GBP);
    EXPECT_GT(longerValue, baseValue);
    EXPECT_LT(uncomfortableValue, longerValue);
}

TEST_F(RideRatings, TransportJourneyAccumulatesSegmentsAndUsesExactFareBuckets)
{
    Ride ride{};
    ride.type = RIDE_TYPE_MONORAIL;
    ride.numStations = 3;
    ride.stableStats.valid = true;
    ride.stableStats.averageSpeed = 18 * 29127;
    ride.stableStats.maxSpeed = ride.stableStats.averageSpeed;
    ride.stableStats.stations[0].SegmentLength = 400 << 16;
    ride.stableStats.stations[0].SegmentTime = 30;
    ride.stableStats.stations[1].SegmentLength = 600 << 16;
    ride.stableStats.stations[1].SegmentTime = 45;

    const auto journey = RideGetTransportJourney(ride, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(2));
    ASSERT_EQ(journey.destinationStation, StationIndex::FromUnderlying(2));
    EXPECT_EQ(journey.segmentCount, 2);
    EXPECT_EQ(journey.distanceMetres, 1000);
    EXPECT_EQ(journey.travelTimeMilliseconds, 75'000);

    const auto expectedValue = RideGetTransportSegment(ride, StationIndex::FromUnderlying(0)).fareValue
        + RideGetTransportSegment(ride, StationIndex::FromUnderlying(1)).fareValue;
    EXPECT_EQ(journey.fareValue, expectedValue);

    ride.priceTarget = RidePriceTarget::free;
    EXPECT_EQ(RideGetTransportFare(ride, journey), 0.00_GBP);
    ride.priceTarget = RidePriceTarget::goodValue;
    EXPECT_EQ(RideGetTransportFare(ride, journey), std::clamp(journey.fareValue / 2, kRideMinPrice, kRideMaxPrice));
    ride.priceTarget = RidePriceTarget::neutral;
    EXPECT_EQ(RideGetTransportFare(ride, journey), std::clamp(journey.fareValue, kRideMinPrice, kRideMaxPrice));
    ride.priceTarget = RidePriceTarget::badValue;
    EXPECT_EQ(RideGetTransportFare(ride, journey), std::clamp(journey.fareValue * 2, kRideMinPrice, kRideMaxPrice));
}

TEST_F(RideRatings, TransportServiceCacheBuildsEveryDirectedJourneyAndRefreshesOncePerTick)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    auto& gameState = getGameState();
    RideInitAll();
    gameState.currentTicks = 100;

    const auto rideId = RideId::FromUnderlying(10);
    auto* ride = RideAllocateAtIndex(rideId);
    ASSERT_NE(ride, nullptr);
    ride->type = RIDE_TYPE_MONORAIL;
    ride->status = RideStatus::open;
    ride->numStations = 3;
    ride->stableStats.valid = true;
    ride->stableStats.averageSpeed = 18 * 29127;
    ride->stableStats.maxSpeed = ride->stableStats.averageSpeed;

    constexpr std::array<int32_t, 3> segmentLengthsMetres{ 400, 600, 800 };
    constexpr std::array<uint16_t, 3> segmentTimesSeconds{ 30, 45, 60 };
    for (size_t stationIndex = 0; stationIndex < ride->numStations; stationIndex++)
    {
        const auto index = StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(stationIndex));
        auto& station = ride->getStation(index);
        station.Start = { static_cast<int32_t>(stationIndex * 10), static_cast<int32_t>(stationIndex * 20) };
        station.Entrance = { static_cast<int32_t>(10 + stationIndex), 20, 2, 0 };
        station.Exit = { static_cast<int32_t>(30 + stationIndex), 40, 2, 2 };
        ride->stableStats.stations[stationIndex].SegmentLength = segmentLengthsMetres[stationIndex] << 16;
        ride->stableStats.stations[stationIndex].SegmentTime = segmentTimesSeconds[stationIndex];
    }

    RideRatingAccumulator qualitySample{};
    qualitySample.ticks = 1;
    qualitySample.transportDistance = 100;
    qualitySample.transportComfort = 900 * qualitySample.transportDistance;
    qualitySample.transportDecoration = 1100 * qualitySample.transportDistance;
    RideAddRecentRatingSample(*ride, qualitySample);

    const auto rideIds = RideGetTransportServiceRideIds();
    ASSERT_EQ(rideIds.size(), 1u);
    EXPECT_EQ(rideIds.front(), rideId);

    const auto service = RideGetTransportService(rideId);
    ASSERT_TRUE(service.isAvailable());
    ASSERT_EQ(service.stations.size(), 3u);
    ASSERT_EQ(service.journeys.size(), 6u);
    EXPECT_EQ(service.quality.comfortPermille, 900);
    EXPECT_EQ(service.quality.decorationPermille, 1100);
    EXPECT_EQ(service.stations[0].entrance, ride->getStation(StationIndex::FromUnderlying(0)).Entrance);
    EXPECT_EQ(service.stations[2].exit, ride->getStation(StationIndex::FromUnderlying(2)).Exit);

    size_t journeyIndex = 0;
    for (size_t boardingIndex = 0; boardingIndex < ride->numStations; boardingIndex++)
    {
        const auto boarding = StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(boardingIndex));
        for (size_t destinationIndex = 0; destinationIndex < ride->numStations; destinationIndex++)
        {
            if (destinationIndex == boardingIndex)
            {
                continue;
            }
            const auto destination =
                StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(destinationIndex));
            const auto expected = RideGetTransportJourney(*ride, boarding, destination, service.quality);
            const auto& cached = service.journeys[journeyIndex++];
            EXPECT_EQ(cached.boardingStation, boarding);
            EXPECT_EQ(cached.journey.destinationStation, destination);
            EXPECT_EQ(cached.journey.segmentCount, expected.segmentCount);
            EXPECT_EQ(cached.journey.distanceMetres, expected.distanceMetres);
            EXPECT_EQ(cached.journey.travelTimeMilliseconds, expected.travelTimeMilliseconds);
            EXPECT_EQ(cached.journey.fareValue, expected.fareValue);
            EXPECT_EQ(service.getJourney(boarding, destination), &cached);
        }
    }

    const auto initialSignature = service.freshnessSignature;
    const auto* initialJourneyStorage = service.journeys.data();
    ride->getStation().QueueTime = 12;
    ride->getStation().QueueLength = 30;
    ride->getStation().QueueFull = true;
    ride->priceTarget = RidePriceTarget::badValue;
    gameState.currentTicks++;

    const auto afterLiveOverlays = RideGetTransportService(rideId);
    EXPECT_EQ(afterLiveOverlays.freshnessSignature, initialSignature);
    EXPECT_EQ(afterLiveOverlays.journeys.data(), initialJourneyStorage);

    ride->stableStats.stations[0].SegmentTime = 90;
    ride->getStation().Entrance = { 50, 60, 3, 1 };
    const auto unchangedWithinTick = RideGetTransportService(rideId);
    EXPECT_EQ(unchangedWithinTick.freshnessSignature, initialSignature);
    EXPECT_EQ(unchangedWithinTick.stations[0].entrance, service.stations[0].entrance);

    RideInvalidateTransportServiceCache(rideId);
    const auto afterMeasuredStats = RideGetTransportService(rideId);
    EXPECT_NE(afterMeasuredStats.freshnessSignature, initialSignature);
    EXPECT_EQ(afterMeasuredStats.stations[0].entrance, ride->getStation().Entrance);
    ASSERT_NE(afterMeasuredStats.getJourney(StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1)), nullptr);
    EXPECT_EQ(
        afterMeasuredStats.getJourney(StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1))
            ->journey.travelTimeMilliseconds,
        90'000);

    const auto measuredStatsSignature = afterMeasuredStats.freshnessSignature;
    const auto measuredStatsValue = afterMeasuredStats.journeys.front().journey.fareValue;
    ride->recentRatingSamples[0].transportComfort = 500 * ride->recentRatingSamples[0].transportDistance;
    gameState.currentTicks++;
    const auto afterQuality = RideGetTransportService(rideId);
    EXPECT_NE(afterQuality.freshnessSignature, measuredStatsSignature);
    EXPECT_EQ(afterQuality.quality.comfortPermille, 500);
    EXPECT_LT(afterQuality.journeys.front().journey.fareValue, measuredStatsValue);
}

TEST_F(RideRatings, TransportServiceCacheTracksAvailabilityDeletionReuseAndReset)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    auto& gameState = getGameState();
    RideInitAll();
    gameState.currentTicks = 200;

    const auto rideId = RideId::FromUnderlying(7);
    auto initialiseRide = [&](int32_t entranceX) {
        auto* ride = RideAllocateAtIndex(rideId);
        EXPECT_NE(ride, nullptr);
        if (ride == nullptr)
        {
            return static_cast<Ride*>(nullptr);
        }
        ride->type = RIDE_TYPE_MONORAIL;
        ride->status = RideStatus::open;
        ride->numStations = 2;
        ride->stableStats.valid = true;
        ride->stableStats.averageSpeed = 18 * 29127;
        ride->stableStats.maxSpeed = ride->stableStats.averageSpeed;
        ride->stableStats.stations[0].SegmentLength = 400 << 16;
        ride->stableStats.stations[1].SegmentLength = 600 << 16;
        ride->getStation(StationIndex::FromUnderlying(0)).Entrance = { entranceX, 20, 2, 0 };
        ride->getStation(StationIndex::FromUnderlying(0)).Exit = { 30, 20, 2, 2 };
        ride->getStation(StationIndex::FromUnderlying(1)).Entrance = { 40, 20, 2, 0 };
        ride->getStation(StationIndex::FromUnderlying(1)).Exit = { 50, 20, 2, 2 };
        return ride;
    };

    auto* ride = initialiseRide(10);
    ASSERT_NE(ride, nullptr);
    ASSERT_TRUE(RideGetTransportService(rideId).isAvailable());

    ride->flags.set(RideFlag::brokenDown);
    RideInvalidateTransportServiceCache(rideId);
    EXPECT_TRUE(RideGetTransportServiceRideIds().empty());
    EXPECT_FALSE(RideGetTransportService(rideId).isAvailable());

    ride->flags.unset(RideFlag::brokenDown);
    RideInvalidateTransportServiceCache(rideId);
    EXPECT_TRUE(RideGetTransportService(rideId).isAvailable());

    ride->status = RideStatus::closed;
    RideInvalidateTransportServiceCache(rideId);
    EXPECT_FALSE(RideGetTransportService(rideId).isAvailable());

    ride->status = RideStatus::open;
    RideInvalidateTransportServiceCache(rideId);
    const auto beforeDeletion = RideGetTransportService(rideId);
    ASSERT_TRUE(beforeDeletion.isAvailable());

    RideDelete(rideId);
    EXPECT_FALSE(RideGetTransportService(rideId).isAvailable());
    EXPECT_TRUE(RideGetTransportServiceRideIds().empty());

    auto* replacement = initialiseRide(70);
    ASSERT_NE(replacement, nullptr);
    const auto afterReuse = RideGetTransportService(rideId);
    ASSERT_TRUE(afterReuse.isAvailable());
    EXPECT_EQ(afterReuse.stations[0].entrance, replacement->getStation().Entrance);

    RideInitAll();
    EXPECT_TRUE(RideGetTransportServiceRideIds().empty());
    EXPECT_FALSE(RideGetTransportService(rideId).isAvailable());
}

TEST_F(RideRatings, TransportServiceSpatialQueriesAreStableAcrossBoundariesMovesAndReuse)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());

    auto& gameState = getGameState();
    RideInitAll();
    gameState.currentTicks = 300;

    auto initialiseRide = [](RideId rideId, const TileCoordsXY& firstEntrance, const TileCoordsXY& secondEntrance) {
        auto* ride = RideAllocateAtIndex(rideId);
        EXPECT_NE(ride, nullptr);
        if (ride == nullptr)
        {
            return static_cast<Ride*>(nullptr);
        }
        ride->type = RIDE_TYPE_MONORAIL;
        ride->status = RideStatus::open;
        ride->numStations = 2;
        ride->stableStats.valid = true;
        ride->stableStats.averageSpeed = 18 * 29127;
        ride->stableStats.maxSpeed = ride->stableStats.averageSpeed;
        ride->stableStats.stations[0].SegmentLength = 400 << 16;
        ride->stableStats.stations[1].SegmentLength = 600 << 16;
        ride->getStation(StationIndex::FromUnderlying(0)).Entrance = { firstEntrance, 2, 0 };
        ride->getStation(StationIndex::FromUnderlying(0)).Exit = { firstEntrance + TileCoordsXY{ 100, 0 }, 2, 2 };
        ride->getStation(StationIndex::FromUnderlying(1)).Entrance = { secondEntrance, 2, 0 };
        ride->getStation(StationIndex::FromUnderlying(1)).Exit = { secondEntrance + TileCoordsXY{ 100, 0 }, 2, 2 };
        return ride;
    };

    // Warm an empty passive validation first. Allocation is an explicit
    // invalidation and must make the new service visible in this same tick.
    EXPECT_TRUE(RideGetTransportServiceRideIds().empty());
    const auto highRideId = RideId::FromUnderlying(20);
    auto* highRide = initialiseRide(highRideId, { 15, 31 }, { 16, 32 });
    ASSERT_NE(highRide, nullptr);

    std::vector<TransportRideServiceStationRef> highRideBuffer;
    const auto highRideCandidates = RideQueryTransportServiceStationsInBounds(
        TransportRideServiceEndpoint::boardingEntrance, { 15, 31 }, { 16, 32 }, highRideBuffer);
    ASSERT_EQ(highRideCandidates.stations.size(), 2u);
    EXPECT_EQ(highRideCandidates.stations[0], (TransportRideServiceStationRef{ highRideId, StationIndex::FromUnderlying(0) }));
    EXPECT_EQ(highRideCandidates.stations[1], (TransportRideServiceStationRef{ highRideId, StationIndex::FromUnderlying(1) }));
    const auto highServiceBeforeOtherRide = RideGetTransportService(highRideId);
    const auto* highJourneyStorage = highServiceBeforeOtherRide.journeys.data();

    // Insert a lower ride id after the buckets have already been populated.
    // Candidate ordering remains ride id then station id, not insertion or chunk order.
    const auto lowRideId = RideId::FromUnderlying(5);
    auto* lowRide = initialiseRide(lowRideId, { 16, 31 }, { 48, 48 });
    ASSERT_NE(lowRide, nullptr);

    std::vector<TransportRideServiceStationRef> orderedBuffer;
    const auto ordered = RideQueryTransportServiceStationsInBounds(
        TransportRideServiceEndpoint::boardingEntrance, { 15, 31 }, { 16, 32 }, orderedBuffer);
    ASSERT_EQ(ordered.stations.size(), 3u);
    EXPECT_EQ(ordered.stations[0], (TransportRideServiceStationRef{ lowRideId, StationIndex::FromUnderlying(0) }));
    EXPECT_EQ(ordered.stations[1], (TransportRideServiceStationRef{ highRideId, StationIndex::FromUnderlying(0) }));
    EXPECT_EQ(ordered.stations[2], (TransportRideServiceStationRef{ highRideId, StationIndex::FromUnderlying(1) }));
    EXPECT_EQ(RideGetTransportService(highRideId).journeys.data(), highJourneyStorage);
    EXPECT_EQ(highServiceBeforeOtherRide.journeys.data(), highJourneyStorage);

    std::vector<TransportRideServiceStationRef> radiusBuffer;
    const auto radius = RideQueryTransportServiceStationsInRadius(
        TransportRideServiceEndpoint::boardingEntrance, { 15, 31 }, 1, radiusBuffer);
    ASSERT_EQ(radius.stations.size(), 2u);
    EXPECT_EQ(radius.stations[0].ride, lowRideId);
    EXPECT_EQ(radius.stations[1], (TransportRideServiceStationRef{ highRideId, StationIndex::FromUnderlying(0) }));

    std::vector<TransportRideServiceStationRef> exitBuffer;
    const auto exits = RideQueryTransportServiceStationsInBounds(
        TransportRideServiceEndpoint::destinationExit, { 115, 31 }, { 116, 32 }, exitBuffer);
    ASSERT_EQ(exits.stations.size(), 3u);
    EXPECT_EQ(exits.stations[0].ride, lowRideId);

    std::vector<TransportRideServiceStationRef> fallbackBuffer;
    const auto fallback = RideQueryTransportServiceStationsInBounds(
        TransportRideServiceEndpoint::boardingEntrance, { 900, 900 }, { 901, 901 }, fallbackBuffer,
        TransportRideServiceQueryFallback::allServicesWhenEmpty);
    EXPECT_TRUE(fallback.usedAllServicesFallback);
    ASSERT_EQ(fallback.stations.size(), 4u);
    EXPECT_TRUE(std::is_sorted(fallback.stations.begin(), fallback.stations.end()));
    const auto warmedCapacity = fallbackBuffer.capacity();
    const auto allStations = RideCollectAllTransportServiceStations(
        TransportRideServiceEndpoint::boardingEntrance, fallbackBuffer);
    EXPECT_EQ(allStations.stations.size(), 4u);
    EXPECT_EQ(fallbackBuffer.capacity(), warmedCapacity);

    // A candidate result owns copied references in its caller buffer, so a
    // different buffer remains valid while this ride is moved and rebuilt.
    const auto retainedCandidate = highRideCandidates.stations.front();
    highRide->getStation(StationIndex::FromUnderlying(0)).Entrance = { 64, 64, 2, 0 };
    RideInvalidateTransportServiceCache(highRideId);
    std::vector<TransportRideServiceStationRef> movedBuffer;
    const auto moved = RideQueryTransportServiceStationsInBounds(
        TransportRideServiceEndpoint::boardingEntrance, { 64, 64 }, { 64, 64 }, movedBuffer);
    ASSERT_EQ(moved.stations.size(), 1u);
    EXPECT_EQ(moved.stations.front(), retainedCandidate);
    EXPECT_EQ(highRideCandidates.stations.front(), retainedCandidate);

    RideDelete(lowRideId);
    std::vector<TransportRideServiceStationRef> deletedBuffer;
    const auto afterDelete = RideQueryTransportServiceStationsInBounds(
        TransportRideServiceEndpoint::boardingEntrance, { 16, 31 }, { 16, 31 }, deletedBuffer);
    EXPECT_TRUE(afterDelete.stations.empty());

    auto* replacement = initialiseRide(lowRideId, { 80, 80 }, { 81, 80 });
    ASSERT_NE(replacement, nullptr);
    std::vector<TransportRideServiceStationRef> reuseBuffer;
    const auto afterReuse = RideQueryTransportServiceStationsInBounds(
        TransportRideServiceEndpoint::boardingEntrance, { 80, 80 }, { 81, 80 }, reuseBuffer);
    ASSERT_EQ(afterReuse.stations.size(), 2u);
    EXPECT_EQ(afterReuse.stations[0].ride, lowRideId);
    EXPECT_EQ(afterReuse.stations[1].ride, lowRideId);
}

TEST_F(RideRatings, PlatformCapacityUsesActualConsistAndSafeLegacyFallback)
{
    auto& entities = getGameState().entities;
    entities.ResetAllEntities();

    Ride ride{};
    ride.type = RIDE_TYPE_MONORAIL;
    ride.numStations = 1;
    ride.getStation().Length = 3;
    EXPECT_EQ(RideGetTransportStationPlatformCapacity(ride, StationIndex::FromUnderlying(0)), 0);

    auto* head = entities.CreateEntity<Vehicle>();
    auto* tail = entities.CreateEntity<Vehicle>();
    ASSERT_NE(head, nullptr);
    ASSERT_NE(tail, nullptr);
    head->SubType = Vehicle::Type::head;
    head->num_seats = 14;
    head->next_vehicle_on_train = tail->id;
    tail->SubType = Vehicle::Type::tail;
    tail->num_seats = 8;
    tail->next_vehicle_on_train = EntityId::GetNull();
    ride.numTrains = 1;
    ride.vehicles[0] = head->id;
    EXPECT_EQ(RideGetTransportStationPlatformCapacity(ride, StationIndex::FromUnderlying(0)), 22);

    ride.vehicles[0] = EntityId::FromUnderlying(
        static_cast<EntityId::UnderlyingType>(std::numeric_limits<EntityId::UnderlyingType>::max() - 1));
    EXPECT_EQ(RideGetTransportStationPlatformCapacity(ride, StationIndex::FromUnderlying(0)), 0);

    ride.type = RIDE_TYPE_CHAIRLIFT;
    EXPECT_EQ(RideGetTransportStationPlatformCapacity(ride, StationIndex::FromUnderlying(0)), 0);
    head->next_vehicle_on_train = EntityId::GetNull();
    head->num_seats = 2;
    ride.vehicles[0] = head->id;
    EXPECT_EQ(RideGetTransportStationPlatformCapacity(ride, StationIndex::FromUnderlying(0)), 2);

    ride.type = RIDE_TYPE_LIFT;
    EXPECT_EQ(RideGetTransportStationPlatformCapacity(ride, StationIndex::FromUnderlying(0)), 6);

    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    EXPECT_EQ(RideGetTransportStationPlatformCapacity(ride, StationIndex::FromUnderlying(0)), 0);
}

TEST_F(RideRatings, PlatformPreQueueAdaptersOptInChairliftButNotLiftOrCoasters)
{
    Ride ride{};
    ride.type = RIDE_TYPE_CHAIRLIFT;
    EXPECT_TRUE(RideSupportsStationPlatformPreQueue(ride));

    ride.type = RIDE_TYPE_LIFT;
    EXPECT_FALSE(RideSupportsStationPlatformPreQueue(ride));

    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    EXPECT_FALSE(RideSupportsStationPlatformPreQueue(ride));
}

TEST_F(RideRatings, MultiStationSamplesPublishAuthoritativeLegsAndConservativeCompatibility)
{
    Ride ride{};
    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    ride.numStations = 3;

    RideRatingAccumulator first{};
    first.originStation = StationIndex::FromUnderlying(0);
    first.destinationStation = StationIndex::FromUnderlying(2);
    first.excitement = 900'000;
    first.intensity = 300'000;
    first.nausea = 200'000;
    first.ticks = 80;
    first.sampledDistance = static_cast<int64_t>(2'000) << 16;
    first.totalSpeed = 80LL * 0x90000;
    first.maxSpeed = 0xA0000;
    first.maxPositiveVerticalG = 250;
    first.maxNegativeVerticalG = -80;
    first.maxLateralG = 120;
    ride.ratings = { RideRating::make(4, 00), RideRating::make(5, 00), RideRating::make(3, 00) };
    const auto priorCompatibility = ride.ratings;
    RideRating::RecordRiderSample(ride, first);

    const auto* firstLeg = RideGetRatingLeg(ride, first.originStation, first.destinationStation);
    ASSERT_NE(firstLeg, nullptr);
    EXPECT_EQ(firstLeg->destinationStation, first.destinationStation);
    EXPECT_EQ(firstLeg->recentSampleCount, 1);
    const auto firstRatings = firstLeg->ratings;
    const auto firstMeasurements = RideGetRatingLegMeasurements(*firstLeg);
    EXPECT_EQ(firstMeasurements.distanceMetres, ToHumanReadableRideLength(static_cast<int32_t>(first.sampledDistance)));
    EXPECT_EQ(firstMeasurements.durationTicks, first.ticks);
    EXPECT_EQ(firstMeasurements.maxSpeed, first.maxSpeed);
    EXPECT_EQ(firstMeasurements.averageSpeed, 0x90000);
    EXPECT_EQ(firstMeasurements.maxNegativeVerticalG, -80);

    RideRatingAccumulator second = first;
    second.originStation = StationIndex::FromUnderlying(2);
    second.destinationStation = StationIndex::FromUnderlying(1);
    second.excitement = 300'000;
    second.intensity = 800'000;
    second.nausea = 700'000;
    RideRating::RecordRiderSample(ride, second);

    const auto* secondLeg = RideGetRatingLeg(ride, second.originStation, second.destinationStation);
    ASSERT_NE(secondLeg, nullptr);
    EXPECT_EQ(secondLeg->destinationStation, second.destinationStation);
    const auto secondRatings = secondLeg->ratings;
    EXPECT_EQ(ride.ratings, priorCompatibility);

    RideRatingAccumulator third = first;
    third.originStation = StationIndex::FromUnderlying(1);
    third.destinationStation = StationIndex::FromUnderlying(0);
    third.excitement = 600'000;
    third.intensity = 500'000;
    third.nausea = 400'000;
    RideRating::RecordRiderSample(ride, third);
    const auto* thirdLeg = RideGetRatingLeg(ride, third.originStation, third.destinationStation);
    ASSERT_NE(thirdLeg, nullptr);
    EXPECT_EQ(
        ride.ratings.excitement,
        std::min({ firstRatings.excitement, secondRatings.excitement, thirdLeg->ratings.excitement }));
    EXPECT_EQ(
        ride.ratings.intensity,
        std::max({ firstRatings.intensity, secondRatings.intensity, thirdLeg->ratings.intensity }));
    EXPECT_EQ(
        ride.ratings.nausea,
        std::max({ firstRatings.nausea, secondRatings.nausea, thirdLeg->ratings.nausea }));
    EXPECT_EQ(RideGetRatingsForStation(ride, first.originStation), firstRatings);
    EXPECT_EQ(RideGetRatingsForStation(ride, StationIndex::FromUnderlying(1)), thirdLeg->ratings);
}

TEST_F(RideRatings, SingleStationSamplesRetainRideWideHistoryWithoutLegAllocation)
{
    Ride ride{};
    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    ride.numStations = 1;

    RideRatingAccumulator sample{};
    sample.excitement = 500'000;
    sample.intensity = 400'000;
    sample.nausea = 300'000;
    sample.ticks = 40;
    RideRating::RecordRiderSample(ride, sample);

    EXPECT_TRUE(ride.ratingLegs.empty());
    EXPECT_EQ(ride.recentRatingSampleCount, 1);
    EXPECT_FALSE(ride.ratings.isNull());
}

TEST_F(RideRatings, ThroughRiderCarsPublishOneLegThenStartCleanForTheNextLeg)
{
    Ride ride{};
    ride.id = RideId::FromUnderlying(7);
    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    ride.numStations = 3;
    const std::array sampleEntities = { EntityId::FromUnderlying(100), EntityId::FromUnderlying(101) };

    for (const auto entity : sampleEntities)
    {
        auto* sample = RideGetOrCreateActiveRatingSample(ride, entity);
        ASSERT_NE(sample, nullptr);
        sample->originStation = StationIndex::FromUnderlying(0);
        sample->destinationStation = StationIndex::FromUnderlying(2);
        sample->excitement = 200'000;
        sample->intensity = 100'000;
        sample->nausea = 50'000;
        sample->ticks = 20;
    }

    EXPECT_TRUE(RideRating::RecordActiveRiderSamples(ride, sampleEntities));
    const auto* completedLeg = RideGetRatingLeg(
        ride, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(2));
    ASSERT_NE(completedLeg, nullptr);
    EXPECT_EQ(completedLeg->destinationStation, StationIndex::FromUnderlying(2));
    for (const auto& cleared : ride.activeRatingSamples)
    {
        EXPECT_FALSE(cleared.hasSamples());
        EXPECT_TRUE(cleared.originStation.IsNull());
        EXPECT_TRUE(cleared.destinationStation.IsNull());
    }

    for (const auto entity : sampleEntities)
    {
        auto* nextLeg = RideGetOrCreateActiveRatingSample(ride, entity);
        ASSERT_NE(nextLeg, nullptr);
        nextLeg->originStation = StationIndex::FromUnderlying(2);
        nextLeg->destinationStation = StationIndex::FromUnderlying(1);
        nextLeg->excitement = 150'000;
        nextLeg->intensity = 75'000;
        nextLeg->nausea = 25'000;
        nextLeg->ticks = 15;
    }
    EXPECT_TRUE(RideRating::RecordActiveRiderSamples(ride, sampleEntities));
    EXPECT_NE(
        RideGetRatingLeg(ride, StationIndex::FromUnderlying(2), StationIndex::FromUnderlying(1)), nullptr);
}

TEST_F(RideRatings, DirectedLegStorageAllowsTwoDestinationsFromOneShuttleStation)
{
    Ride ride{};
    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    ride.numStations = 3;

    RideRatingAccumulator towardStart{};
    towardStart.originStation = StationIndex::FromUnderlying(1);
    towardStart.destinationStation = StationIndex::FromUnderlying(0);
    towardStart.excitement = 300'000;
    towardStart.intensity = 700'000;
    towardStart.nausea = 600'000;
    towardStart.ticks = 20;
    RideRating::RecordRiderSample(ride, towardStart);

    auto towardEnd = towardStart;
    towardEnd.destinationStation = StationIndex::FromUnderlying(2);
    towardEnd.excitement = 800'000;
    towardEnd.intensity = 200'000;
    towardEnd.nausea = 100'000;
    RideRating::RecordRiderSample(ride, towardEnd);

    const auto* firstEdge = RideGetRatingLeg(ride, towardStart.originStation, towardStart.destinationStation);
    const auto* secondEdge = RideGetRatingLeg(ride, towardEnd.originStation, towardEnd.destinationStation);
    ASSERT_NE(firstEdge, nullptr);
    ASSERT_NE(secondEdge, nullptr);
    EXPECT_EQ(ride.ratingLegs.size(), 2);
    EXPECT_EQ(RideGetUniqueOutboundRatingLeg(ride, towardStart.originStation), nullptr);
    const auto stationRatings = RideGetRatingsForStation(ride, towardStart.originStation);
    EXPECT_EQ(stationRatings.excitement, std::min(firstEdge->ratings.excitement, secondEdge->ratings.excitement));
    EXPECT_EQ(stationRatings.intensity, std::max(firstEdge->ratings.intensity, secondEdge->ratings.intensity));
    EXPECT_EQ(stationRatings.nausea, std::max(firstEdge->ratings.nausea, secondEdge->ratings.nausea));
}

TEST_F(RideRatings, RatingLegDisplaySelectionReachesLaterEdgesInStableEndpointOrder)
{
    Ride ride{};
    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    ride.numStations = 3;

    const auto addLeg = [&ride](uint8_t origin, uint8_t destination) {
        RideRatingAccumulator sample{};
        sample.originStation = StationIndex::FromUnderlying(origin);
        sample.destinationStation = StationIndex::FromUnderlying(destination);
        sample.excitement = 100'000 + origin * 10'000 + destination;
        sample.intensity = 100'000;
        sample.nausea = 100'000;
        sample.ticks = 20;
        RideRating::RecordRiderSample(ride, sample);
    };
    addLeg(2, 1);
    addLeg(0, 2);
    addLeg(1, 2);
    addLeg(0, 1);
    addLeg(1, 0);

    const std::array expected = {
        std::pair{ uint8_t{ 0 }, uint8_t{ 1 } },
        std::pair{ uint8_t{ 0 }, uint8_t{ 2 } },
        std::pair{ uint8_t{ 1 }, uint8_t{ 0 } },
        std::pair{ uint8_t{ 1 }, uint8_t{ 2 } },
        std::pair{ uint8_t{ 2 }, uint8_t{ 1 } },
    };
    for (size_t index = 0; index < expected.size(); index++)
    {
        const auto* leg = RideGetRatingLegByDisplayIndex(ride, index);
        ASSERT_NE(leg, nullptr);
        EXPECT_EQ(leg->originStation.ToUnderlying(), expected[index].first);
        EXPECT_EQ(leg->destinationStation.ToUnderlying(), expected[index].second);
    }
    ASSERT_NE(RideGetRatingLegByDisplayIndex(ride, 4), nullptr);
    EXPECT_EQ(RideGetRatingLegByDisplayIndex(ride, expected.size()), nullptr);

    // Updating an existing early edge must not move the selected fifth edge.
    addLeg(0, 1);
    const auto* fifth = RideGetRatingLegByDisplayIndex(ride, 4);
    ASSERT_NE(fifth, nullptr);
    EXPECT_EQ(fifth->originStation, StationIndex::FromUnderlying(2));
    EXPECT_EQ(fifth->destinationStation, StationIndex::FromUnderlying(1));
}

TEST_F(RideRatings, IncompleteDirectedCoveragePreservesNullCompatibility)
{
    Ride ride{};
    ride.type = RIDE_TYPE_WOODEN_ROLLER_COASTER;
    ride.numStations = 3;
    ride.ratings.setNull();
    ASSERT_TRUE(ride.ratings.isNull());

    RideRatingAccumulator sample{};
    sample.originStation = StationIndex::FromUnderlying(0);
    sample.destinationStation = StationIndex::FromUnderlying(2);
    sample.excitement = 200'000;
    sample.intensity = 100'000;
    sample.nausea = 50'000;
    sample.ticks = 20;
    RideRating::RecordRiderSample(ride, sample);

    EXPECT_TRUE(ride.ratings.isNull());
}

TEST_F(RideRatings, TransportJourneyFollowsAndComposesMeasuredPhysicalLegs)
{
    Ride ride{};
    ride.type = RIDE_TYPE_MONORAIL;
    ride.numStations = 3;
    ride.stableStats.valid = true;
    ride.stableStats.stations[0].SegmentLength = 1'000 << 16;
    ride.stableStats.stations[2].SegmentLength = 1'500 << 16;
    ride.stableStats.stations[0].SegmentTime = 20;
    ride.stableStats.stations[2].SegmentTime = 30;

    const auto addLeg = [&ride](uint8_t origin, uint8_t destination, int32_t distance, uint32_t ticks, int32_t speed) {
        RideRatingAccumulator sample{};
        sample.originStation = StationIndex::FromUnderlying(origin);
        sample.destinationStation = StationIndex::FromUnderlying(destination);
        sample.excitement = 100'000;
        sample.intensity = 100'000;
        sample.nausea = 100'000;
        sample.transportDistance = 1'000;
        sample.transportComfort = 900'000;
        sample.transportDecoration = 1'000'000;
        sample.sampledDistance = static_cast<int64_t>(distance) << 16;
        sample.totalSpeed = static_cast<int64_t>(speed) * ticks;
        sample.maxSpeed = speed + 0x10000;
        sample.ticks = ticks;
        RideAddRecentRatingSample(ride, sample);
    };
    addLeg(0, 2, 4'000, 40, 0x80000);
    addLeg(2, 1, 5'000, 60, 0xA0000);
    addLeg(1, 0, 6'000, 80, 0x70000);

    const TransportRideQuality quality{ .comfortPermille = 900, .decorationPermille = 1000, .hasMeasurements = true };
    const auto first = RideGetTransportSegment(ride, StationIndex::FromUnderlying(0), quality);
    const auto second = RideGetTransportSegment(ride, StationIndex::FromUnderlying(2), quality);
    const auto journey = RideGetTransportJourney(
        ride, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(1), quality);
    EXPECT_EQ(first.destinationStation, StationIndex::FromUnderlying(2));
    EXPECT_EQ(second.destinationStation, StationIndex::FromUnderlying(1));
    EXPECT_EQ(first.distanceMetres, 4'000);
    EXPECT_EQ(second.distanceMetres, 5'000);
    EXPECT_EQ(first.travelTimeMilliseconds, 1'000);
    EXPECT_EQ(second.travelTimeMilliseconds, 1'500);
    EXPECT_EQ(journey.destinationStation, StationIndex::FromUnderlying(1));
    EXPECT_EQ(journey.segmentCount, 2);
    EXPECT_EQ(journey.distanceMetres, first.distanceMetres + second.distanceMetres);
    EXPECT_EQ(journey.travelTimeMilliseconds, first.travelTimeMilliseconds + second.travelTimeMilliseconds);
    EXPECT_EQ(journey.fareValue, first.fareValue + second.fareValue);
}

TEST_F(RideRatings, TransportJourneyKeepsBothMeasuredShuttleDepartures)
{
    Ride ride{};
    ride.type = RIDE_TYPE_MONORAIL;
    ride.numStations = 3;

    const auto addLeg = [&ride](uint8_t origin, uint8_t destination, int32_t distance) {
        RideRatingAccumulator sample{};
        sample.originStation = StationIndex::FromUnderlying(origin);
        sample.destinationStation = StationIndex::FromUnderlying(destination);
        sample.excitement = 100'000;
        sample.intensity = 100'000;
        sample.nausea = 100'000;
        sample.transportDistance = 1'000;
        sample.transportComfort = 900'000;
        sample.transportDecoration = 1'000'000;
        sample.sampledDistance = static_cast<int64_t>(distance) << 16;
        sample.totalSpeed = 40LL * 0x80000;
        sample.maxSpeed = 0x90000;
        sample.ticks = 40;
        RideAddRecentRatingSample(ride, sample);
    };
    addLeg(1, 0, 2'000);
    addLeg(1, 2, 3'000);
    addLeg(0, 1, 2'000);
    addLeg(2, 1, 3'000);

    const TransportRideQuality quality{ .comfortPermille = 900, .decorationPermille = 1000, .hasMeasurements = true };
    EXPECT_TRUE(RideGetTransportSegment(ride, StationIndex::FromUnderlying(1), quality).destinationStation.IsNull());
    const auto towardStart = RideGetTransportSegment(
        ride, StationIndex::FromUnderlying(1), StationIndex::FromUnderlying(0), quality);
    const auto towardEnd = RideGetTransportSegment(
        ride, StationIndex::FromUnderlying(1), StationIndex::FromUnderlying(2), quality);
    EXPECT_EQ(towardStart.destinationStation, StationIndex::FromUnderlying(0));
    EXPECT_EQ(towardEnd.destinationStation, StationIndex::FromUnderlying(2));
    EXPECT_NE(towardStart.distanceMetres, towardEnd.distanceMetres);
    const auto* measuredStartLeg = RideGetRatingLeg(
        ride, StationIndex::FromUnderlying(1), StationIndex::FromUnderlying(0));
    ASSERT_NE(measuredStartLeg, nullptr);
    const auto measuredStartQuality = RideGetTransportLegQuality(*measuredStartLeg, {});
    EXPECT_EQ(measuredStartQuality.comfortPermille, 900);
    EXPECT_EQ(measuredStartQuality.decorationPermille, 1000);
    EXPECT_TRUE(measuredStartQuality.hasMeasurements);

    const auto startJourney = RideGetTransportJourney(
        ride, StationIndex::FromUnderlying(1), StationIndex::FromUnderlying(0), quality);
    const auto endJourney = RideGetTransportJourney(
        ride, StationIndex::FromUnderlying(1), StationIndex::FromUnderlying(2), quality);
    EXPECT_EQ(startJourney.segmentCount, 1);
    EXPECT_EQ(endJourney.segmentCount, 1);
    EXPECT_EQ(startJourney.distanceMetres, towardStart.distanceMetres);
    EXPECT_EQ(endJourney.distanceMetres, towardEnd.distanceMetres);
}

TEST_F(RideRatings, SampledRatingProfilesApplyInitialRideTypeMultipliers)
{
    const auto& woodenProfile = GetRideTypeDescriptor(RIDE_TYPE_WOODEN_ROLLER_COASTER).SampledRatings;
    const auto& goKartsRtd = GetRideTypeDescriptor(RIDE_TYPE_GO_KARTS);
    const auto& goKartsProfile = goKartsRtd.SampledRatings;

    EXPECT_EQ(woodenProfile.VerticalG, 2000);
    EXPECT_EQ(woodenProfile.LateralG, 500);
    EXPECT_EQ(woodenProfile.Speed, 300);
    EXPECT_EQ(woodenProfile.SpeedGCoupling, 500);
    EXPECT_EQ(goKartsProfile.Speed, 3000);
    EXPECT_EQ(goKartsProfile.LateralG, 3000);
    EXPECT_TRUE(goKartsRtd.flags.has(RtdFlag::hasGForces));
    EXPECT_TRUE(goKartsRtd.flags.has(RtdFlag::hasDataLogging));

    const auto defaultSpeed = RideRating::ScoreVehicleSpeedForTick(90, kSampledRideRatingProfileScale);
    const auto kartSpeed = RideRating::ScoreVehicleSpeedForTick(90, goKartsProfile.Speed);
    EXPECT_EQ(kartSpeed.excitement, defaultSpeed.excitement * 3);
    EXPECT_EQ(kartSpeed.intensity, defaultSpeed.intensity * 3);
    EXPECT_EQ(kartSpeed.nausea, defaultSpeed.nausea * 3);
}

TEST_F(RideRatings, SampledRatingProfileScalesEveryNonSpeedChannel)
{
    SampledRideRatingProfile profile{};
    profile.VerticalG = 2000;
    profile.LateralG = 500;
    profile.LongitudinalG = 3000;
    profile.Airtime = 4000;
    profile.SpeedGCoupling = 0;

    SampledRideRatingProfile defaultProfile{};
    defaultProfile.SpeedGCoupling = 0;
    const auto defaultVertical = RideRating::ScoreGForcesForVehicleTick(200, 0, 0, 90, defaultProfile);
    const auto scaledVertical = RideRating::ScoreGForcesForVehicleTick(200, 0, 0, 90, profile);
    const auto defaultLateral = RideRating::ScoreGForcesForVehicleTick(100, 100, 0, 90, defaultProfile);
    const auto scaledLateral = RideRating::ScoreGForcesForVehicleTick(100, 100, 0, 90, profile);
    const auto defaultLongitudinal = RideRating::ScoreGForcesForVehicleTick(100, 0, 100, 90, defaultProfile);
    const auto scaledLongitudinal = RideRating::ScoreGForcesForVehicleTick(100, 0, 100, 90, profile);
    const auto defaultAirtime = RideRating::ScoreGForcesForVehicleTick(0, 0, 0, 90, defaultProfile);
    const auto scaledAirtime = RideRating::ScoreGForcesForVehicleTick(0, 0, 0, 90, profile);

    EXPECT_EQ(scaledVertical.excitement, defaultVertical.excitement * 2);
    EXPECT_EQ(scaledLateral.excitement * 2, defaultLateral.excitement);
    EXPECT_EQ(scaledLongitudinal.excitement, defaultLongitudinal.excitement * 3);
    EXPECT_EQ(scaledAirtime.excitement, defaultAirtime.excitement * 4);

    const RideRating::LocalContextScore contextScore = { .excitement = 10, .intensity = 6, .nausea = 4 };
    const auto fullContext = RideRating::ScoreLocalContextForVehicleTick(contextScore, 90, 1000);
    const auto halfContext = RideRating::ScoreLocalContextForVehicleTick(contextScore, 90, 500);
    const auto clampedContext = RideRating::ScoreLocalContextForVehicleTick(contextScore, 90, 2000);
    EXPECT_EQ(halfContext.excitement * 2, fullContext.excitement);
    EXPECT_EQ(clampedContext.excitement, fullContext.excitement);
}

TEST_F(RideRatings, SpeedGCouplingRewardsEqualForcesAtHigherSpeed)
{
    SampledRideRatingProfile profile{};
    profile.SpeedGCoupling = 1000;
    const auto halfSpeed = RideRating::ScoreGForcesForVehicleTick(100, 100, 0, 45, profile);
    const auto baseline = RideRating::ScoreGForcesForVehicleTick(100, 100, 0, 90, profile);
    const auto doubleSpeed = RideRating::ScoreGForcesForVehicleTick(100, 100, 0, 180, profile);

    EXPECT_NEAR(halfSpeed.excitement * 4, baseline.excitement, 4);
    EXPECT_EQ(doubleSpeed.excitement, baseline.excitement * 4);
    EXPECT_NEAR(halfSpeed.excitement * 2, baseline.excitement / 2, 2);
    EXPECT_EQ(doubleSpeed.excitement / 2, baseline.excitement * 2);
}

TEST_F(RideRatings, StableStatsPublishLongitudinalGExtrema)
{
    Ride ride{};
    ride.maxPositiveLongitudinalG = 125;
    ride.maxNegativeLongitudinalG = -85;
    ride.publishCurrentStatsAsStable();
    ride.maxPositiveLongitudinalG = 0;
    ride.maxNegativeLongitudinalG = 0;

    EXPECT_EQ(ride.getDisplayMaxPositiveLongitudinalG(), 125);
    EXPECT_EQ(ride.getDisplayMaxNegativeLongitudinalG(), -85);
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
        .excitement = 77,
        .intensity = 16,
        .nausea = 4,
        .scenery = 18,
        .pathBridge = 6,
        .pathNearMiss = 4,
        .pathLoop = 3,
        .trackVerticalInteraction = 2,
        .ownTrackVerticalInteraction = 2,
        .trackHeightExposure = 4,
    };

    const auto normalSpeed = RideRating::ScoreLocalContextForVehicleTick(contextScore, RideRating::kVehicleRatingBaselineSpeed);
    const auto slowerSpeed = RideRating::ScoreLocalContextForVehicleTick(contextScore, 41);
    const auto slowestSpeed = RideRating::ScoreLocalContextForVehicleTick(contextScore, 17);
    EXPECT_EQ(normalSpeed.excitement, 41200);
    EXPECT_EQ(normalSpeed.intensity, 12400);
    EXPECT_EQ(normalSpeed.nausea, 2000);
    EXPECT_EQ(slowerSpeed.excitement, 18768);
    EXPECT_EQ(slowestSpeed.excitement, 7782);
    EXPECT_EQ(slowerSpeed.intensity, 5648);
    EXPECT_EQ(slowerSpeed.nausea, 911);
}

TEST_F(RideRatings, LocalContextVehicleTickScoringSpeedNormalizesPathProximity)
{
    const RideRating::LocalContextScore contextScore = {
        .excitement = 5,
        .pathProximity = 5,
    };

    const auto stopped = RideRating::ScoreLocalContextForVehicleTick(contextScore, 0);
    const auto moving = RideRating::ScoreLocalContextForVehicleTick(contextScore, 37);

    EXPECT_EQ(stopped.excitement, 0);
    EXPECT_EQ(
        moving.excitement,
        (5 * ((RideRating::kRideRatingAccumulatorRawScale * 2) / 5) * 37) / RideRating::kVehicleRatingBaselineSpeed);
}

TEST_F(RideRatings, LocalContextVehicleTickScoringPreservesFractionalRawNausea)
{
    const RideRating::LocalContextScore contextScore = {
        .excitement = 2,
        .intensity = 1,
        .nausea = 0,
        .pathNearMiss = 1,
    };

    const auto score = RideRating::ScoreLocalContextForVehicleTick(contextScore, RideRating::kVehicleRatingBaselineSpeed);

    EXPECT_EQ(score.excitement, 1600);
    EXPECT_EQ(score.intensity, 800);
    EXPECT_EQ(score.nausea, 133);
}

TEST_F(RideRatings, VehicleLocalContextRuntimeCacheTracksNearbyMapChanges)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    // Straddle the local-context generation chunk boundary as well as testing
    // ordinary nearby invalidation.
    const auto originTile = TileCoordsXY{ 15, 10 };
    const auto sceneryTile = TileCoordsXY{ 16, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };
    RideRating::VehicleLocalContextCache runtimeCache{};

    SetSurfaceZ(originTile, groundZ);
    SetSurfaceZ(sceneryTile, groundZ);
    const auto before = RideRating::GetVehicleRatingEnvironment(origin, rideId, TrackElemType::flatTrack1x4A, 0, runtimeCache);
    EXPECT_FALSE(before.isSheltered);

    PlaceSmallScenery(sceneryTile, groundZ, groundZ + (3 * kCoordsZStep));
    const auto decorated = RideRating::GetVehicleRatingEnvironment(
        origin, rideId, TrackElemType::flatTrack1x4A, 0, runtimeCache);
    EXPECT_GT(decorated.context.scenery, before.context.scenery);

    PlacePath(originTile, groundZ + (4 * kCoordsZStep), groundZ + (5 * kCoordsZStep));
    const auto sheltered = RideRating::GetVehicleRatingEnvironment(
        origin, rideId, TrackElemType::flatTrack1x4A, 0, runtimeCache);
    EXPECT_TRUE(sheltered.isSheltered);
}

TEST_F(RideRatings, BoatHireFreeRoamAddsGuidedTurnStatDistribution)
{
    const auto firstTick = RideRating::ScoreBoatHireFreeRoamForTick(0);
    const auto secondTick = RideRating::ScoreBoatHireFreeRoamForTick(1);

    EXPECT_EQ(firstTick.excitement + secondTick.excitement, (8 * RideRating::kRideRatingAccumulatorRawScale) / 5);
    EXPECT_EQ(firstTick.intensity + secondTick.intensity, 2 * RideRating::kRideRatingAccumulatorRawScale);
    EXPECT_EQ(firstTick.nausea + secondTick.nausea, 2 * RideRating::kRideRatingAccumulatorRawScale);
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

TEST_F(RideRatings, LocalContextFlatVehicleOnLevelSidesAddsNoHeightExposure)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr Direction trackDirection = 0;
    constexpr int32_t trackZ = 20 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    SetSurfaceZ(originTile, trackZ);
    SetVehicleSideSurfaces(originTile, trackDirection, trackZ, trackZ);
    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, trackDirection);
    EXPECT_EQ(score.trackHeightExposure, 0);
    EXPECT_EQ(score.verticalInteraction, 0);
    EXPECT_EQ(score.excitement, 0);
    EXPECT_EQ(score.intensity, 0);
    EXPECT_EQ(score.nausea, 0);
}

TEST_F(RideRatings, LocalContextHighVehicleAboveTwoSideSurfacesAddsHeightExposure)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr Direction trackDirection = 0;
    constexpr int32_t lowGroundZ = 14 * kCoordsZStep;
    constexpr int32_t trackZ = 26 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    SetSurfaceZ(originTile, trackZ);
    SetVehicleSideSurfaces(originTile, trackDirection, lowGroundZ, lowGroundZ);
    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, trackDirection);
    EXPECT_EQ(score.trackHeightExposure, 3);
    EXPECT_EQ(score.verticalInteraction, score.trackHeightExposure);
    EXPECT_EQ(score.excitement, 3);
    EXPECT_EQ(score.intensity, 3);
    EXPECT_EQ(score.nausea, 1);
}

TEST_F(RideRatings, LocalContextOneExposedSideScoresLessThanTwo)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto twoSideTile = TileCoordsXY{ 10, 10 };
    const auto oneSideTile = TileCoordsXY{ 16, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr Direction trackDirection = 0;
    constexpr int32_t lowGroundZ = 14 * kCoordsZStep;
    constexpr int32_t trackZ = 26 * kCoordsZStep;

    SetSurfaceZ(twoSideTile, trackZ);
    SetVehicleSideSurfaces(twoSideTile, trackDirection, lowGroundZ, lowGroundZ);
    PlaceFlatTrack(twoSideTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);

    SetSurfaceZ(oneSideTile, trackZ);
    SetVehicleSideSurfaces(oneSideTile, trackDirection, lowGroundZ, trackZ);
    PlaceFlatTrack(oneSideTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);

    const auto twoSideScore = RideRating::GetVehicleLocalContextScore(
        { twoSideTile.ToCoordsXY().ToTileCentre(), trackZ }, rideId, TrackElemType::flatTrack1x4A, trackDirection);
    const auto oneSideScore = RideRating::GetVehicleLocalContextScore(
        { oneSideTile.ToCoordsXY().ToTileCentre(), trackZ }, rideId, TrackElemType::flatTrack1x4A, trackDirection);

    EXPECT_GT(oneSideScore.trackHeightExposure, 0);
    EXPECT_LT(oneSideScore.trackHeightExposure, twoSideScore.trackHeightExposure);
}

TEST_F(RideRatings, LocalContextHeightExposureCapsAtSix)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr Direction trackDirection = 0;
    constexpr int32_t lowGroundZ = 14 * kCoordsZStep;
    constexpr int32_t trackZ = 40 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    SetSurfaceZ(originTile, trackZ);
    SetVehicleSideSurfaces(originTile, trackDirection, lowGroundZ, lowGroundZ);
    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, trackDirection);
    EXPECT_EQ(score.trackHeightExposure, 6);
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

    SetSurfaceZ(originTile, origin.z);
    SetVehicleSideSurfaces(originTile, 0, origin.z, origin.z);
    auto* foreignTrack = TileElementInsert<TrackElement>({ originTile.ToCoordsXY(), 18 * kCoordsZStep }, 0);
    ASSERT_NE(foreignTrack, nullptr);
    foreignTrack->SetRideIndex(foreignRideId);
    foreignTrack->setClearanceZ(20 * kCoordsZStep);
    MapInvalidateTileFull(originTile.ToCoordsXY());

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, 0);
    EXPECT_GT(score.trackVerticalInteraction, score.foreignTrackProximity);
    EXPECT_EQ(score.ownTrackVerticalInteraction, 0);
    EXPECT_EQ(score.trackHeightExposure, 0);
    EXPECT_EQ(score.verticalInteraction, score.trackVerticalInteraction);
    EXPECT_GT(score.excitement, 0);
    EXPECT_GT(score.intensity, 0);

    const auto normalSpeed = RideRating::ScoreLocalContextForVehicleTick(score, RideRating::kVehicleRatingBaselineSpeed);
    const auto slowerSpeed = RideRating::ScoreLocalContextForVehicleTick(score, 41);
    EXPECT_LT(slowerSpeed.excitement, normalSpeed.excitement);
    EXPECT_LT(slowerSpeed.intensity, normalSpeed.intensity);
    EXPECT_LT(slowerSpeed.nausea, normalSpeed.nausea);
}

TEST_F(RideRatings, LocalContextSameRideVerticalTrackScoresBelowForeignTrack)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto foreignTile = TileCoordsXY{ 10, 10 };
    const auto ownTile = TileCoordsXY{ 16, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    const auto foreignRideId = RideId::FromUnderlying(2);
    constexpr Direction trackDirection = 0;
    constexpr int32_t trackZ = 18 * kCoordsZStep;
    constexpr int32_t upperTrackZ = trackZ + (4 * kCoordsZStep);

    SetSurfaceZ(foreignTile, trackZ);
    SetVehicleSideSurfaces(foreignTile, trackDirection, trackZ, trackZ);
    PlaceFlatTrack(foreignTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);
    PlaceFlatTrack(
        foreignTile, upperTrackZ, upperTrackZ + (2 * kCoordsZStep), foreignRideId, TrackElemType::flatTrack1x4A,
        trackDirection);

    SetSurfaceZ(ownTile, trackZ);
    SetVehicleSideSurfaces(ownTile, trackDirection, trackZ, trackZ);
    PlaceFlatTrack(ownTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);
    PlaceFlatTrack(
        ownTile, upperTrackZ, upperTrackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);

    const auto foreignScore = RideRating::GetVehicleLocalContextScore(
        { foreignTile.ToCoordsXY().ToTileCentre(), trackZ }, rideId, TrackElemType::flatTrack1x4A, trackDirection);
    const auto ownScore = RideRating::GetVehicleLocalContextScore(
        { ownTile.ToCoordsXY().ToTileCentre(), trackZ }, rideId, TrackElemType::flatTrack1x4A, trackDirection);

    EXPECT_GT(foreignScore.trackVerticalInteraction, ownScore.ownTrackVerticalInteraction);
    EXPECT_EQ(foreignScore.ownTrackVerticalInteraction, 0);
    EXPECT_GT(ownScore.ownTrackVerticalInteraction, 0);
    EXPECT_EQ(ownScore.trackVerticalInteraction, 0);
    EXPECT_GT(ownScore.excitement, 0);
    EXPECT_GT(ownScore.intensity, 0);
    EXPECT_GT(ownScore.nausea, 0);
}

TEST_F(RideRatings, LocalContextSameRideSameHeightTrackAddsNoVerticalBonus)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr Direction trackDirection = 0;
    constexpr int32_t trackZ = 18 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    SetSurfaceZ(originTile, trackZ);
    SetVehicleSideSurfaces(originTile, trackDirection, trackZ, trackZ);
    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, trackDirection);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, trackDirection);
    EXPECT_EQ(score.trackVerticalInteraction, 0);
    EXPECT_EQ(score.ownTrackVerticalInteraction, 0);
    EXPECT_EQ(score.trackHeightExposure, 0);
    EXPECT_EQ(score.verticalInteraction, 0);
    EXPECT_EQ(score.excitement, 0);
}

TEST_F(RideRatings, LocalContextLonePathAboveFlatVehicleAddsNoBridgeBonus)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId);
    PlacePath(originTile, trackZ + (4 * kCoordsZStep), trackZ + (5 * kCoordsZStep));

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, 0);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_EQ(score.pathNearMiss, 0);
    EXPECT_EQ(score.pathLoop, 0);
    EXPECT_EQ(score.excitement, 0);
}

TEST_F(RideRatings, LocalContextPathPlazaAboveFlatVehicleAddsNoBridgeBonus)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto plazaTile = TileCoordsXY{ 10, 11 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId);
    PlacePathPlaza(plazaTile, trackZ + (4 * kCoordsZStep), trackZ + (5 * kCoordsZStep));

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, 0);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_EQ(score.pathNearMiss, 0);
    EXPECT_EQ(score.pathLoop, 0);
}

TEST_F(RideRatings, LocalContextOneWideBridgeAdjacentToFlatVehicleAddsExcitementOnly)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto bridgeTile = TileCoordsXY{ 10, 11 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, 1);
    PlaceBridgeLine(bridgeTile, trackZ + (4 * kCoordsZStep), trackZ + (5 * kCoordsZStep), 0);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, 1);
    EXPECT_GT(score.pathBridge, 0);
    EXPECT_EQ(score.pathNearMiss, 0);
    EXPECT_EQ(score.pathLoop, 0);
    EXPECT_EQ(score.trackVerticalInteraction, 0);
    EXPECT_EQ(score.excitement, score.pathBridge);
    EXPECT_EQ(score.intensity, 0);
    EXPECT_EQ(score.nausea, 0);
}

TEST_F(RideRatings, LocalContextTwoWideBridgeAdjacentToFlatVehicleAddsExcitementOnly)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto bridgeTile = TileCoordsXY{ 10, 11 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, 1);
    PlaceTwoWideBridgeLine(bridgeTile, trackZ + (4 * kCoordsZStep), trackZ + (5 * kCoordsZStep), 0);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, 1);
    EXPECT_GT(score.pathBridge, 0);
    EXPECT_EQ(score.intensity, 0);
    EXPECT_EQ(score.nausea, 0);
}

TEST_F(RideRatings, LocalContextDirectlyUnderBridgeGetsNoNormalBridgeBonus)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, 0);
    PlaceBridgeLine(originTile, trackZ + (4 * kCoordsZStep), trackZ + (5 * kCoordsZStep), 1);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, 0);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_EQ(score.pathNearMiss, 0);
    EXPECT_EQ(score.pathLoop, 0);
}

TEST_F(RideRatings, LocalContextBridgeAboveGradientVehicleAddsNearMissThrill)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatToUp25, 0);
    PlaceBridgeLine(originTile, trackZ + (4 * kCoordsZStep), trackZ + (5 * kCoordsZStep), 1);

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatToUp25, 0);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_GT(score.pathNearMiss, 0);
    EXPECT_EQ(score.pathLoop, 0);
    EXPECT_GT(score.intensity, 0);
    EXPECT_GT(score.nausea, 0);
}

TEST_F(RideRatings, LocalContextPathBelowFlatVehicleAddsNoBonus)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 18 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    SetSurfaceZ(originTile, trackZ);
    SetVehicleSideSurfaces(originTile, 0, trackZ, trackZ);
    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::flatTrack1x4A, 0);
    PlacePath(originTile, trackZ - (3 * kCoordsZStep), trackZ - (2 * kCoordsZStep));

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::flatTrack1x4A, 0);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_EQ(score.pathNearMiss, 0);
    EXPECT_EQ(score.pathLoop, 0);
    EXPECT_EQ(score.excitement, 0);
}

TEST_F(RideRatings, LocalContextPathBelowVerticalLoopAddsLoopThrill)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t trackZ = 18 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), trackZ };

    SetSurfaceZ(originTile, trackZ);
    SetVehicleSideSurfaces(originTile, 0, trackZ, trackZ);
    PlaceFlatTrack(originTile, trackZ, trackZ + (2 * kCoordsZStep), rideId, TrackElemType::leftVerticalLoop, 0);
    PlacePath(originTile, trackZ - (3 * kCoordsZStep), trackZ - (2 * kCoordsZStep));

    const auto score = RideRating::GetVehicleLocalContextScore(origin, rideId, TrackElemType::leftVerticalLoop, 0);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_EQ(score.pathNearMiss, 0);
    EXPECT_GT(score.pathLoop, 0);
    EXPECT_GT(score.intensity, 0);
    EXPECT_GT(score.nausea, 0);
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

TEST_F(RideRatings, LocalContextLonePathAboveMazeAddsNoBonus)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 9, 10 };
    const auto pathTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };

    PlaceMazeTrack(originTile, groundZ, groundZ + (3 * kCoordsZStep), rideId);
    PlacePath(pathTile, groundZ + (6 * kCoordsZStep), groundZ + (7 * kCoordsZStep));

    const auto score = RideRating::GetMazeLocalContextScore(origin, rideId);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_EQ(score.pathProximity, 0);
    EXPECT_EQ(score.excitement, 0);
}

TEST_F(RideRatings, LocalContextPathPlazaAboveMazeAddsNoBonus)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto originTile = TileCoordsXY{ 9, 10 };
    const auto plazaTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    const auto origin = CoordsXYZ{ originTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };

    PlaceMazeTrack(originTile, groundZ, groundZ + (3 * kCoordsZStep), rideId);
    PlacePathPlaza(plazaTile, groundZ + (6 * kCoordsZStep), groundZ + (7 * kCoordsZStep));

    const auto score = RideRating::GetMazeLocalContextScore(origin, rideId);
    EXPECT_EQ(score.pathBridge, 0);
    EXPECT_EQ(score.pathProximity, 0);
}

TEST_F(RideRatings, LocalContextBridgeAboveMazeAddsExcitementToAdjacentTilesOnly)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 30, 30 });

    const auto bridgeTile = TileCoordsXY{ 10, 10 };
    const auto adjacentMazeTile = TileCoordsXY{ 9, 10 };
    const auto underBridgeTile = TileCoordsXY{ 10, 10 };
    const auto rideId = RideId::FromUnderlying(1);
    constexpr int32_t groundZ = 14 * kCoordsZStep;
    constexpr int32_t mazeTopZ = groundZ + (3 * kCoordsZStep);
    const auto adjacentOrigin = CoordsXYZ{ adjacentMazeTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };
    const auto underBridgeOrigin = CoordsXYZ{ underBridgeTile.ToCoordsXY().ToTileCentre(), groundZ + kCoordsZStep };

    PlaceMazeTrack(adjacentMazeTile, groundZ, mazeTopZ, rideId);
    PlaceMazeTrack(underBridgeTile, groundZ, mazeTopZ, rideId);
    PlaceBridgeLine(bridgeTile, groundZ + (6 * kCoordsZStep), groundZ + (7 * kCoordsZStep), 1);

    const auto adjacentScore = RideRating::GetMazeLocalContextScore(adjacentOrigin, rideId);
    EXPECT_GT(adjacentScore.pathBridge, 0);
    EXPECT_EQ(adjacentScore.excitement, adjacentScore.pathBridge);
    EXPECT_EQ(adjacentScore.intensity, 0);
    EXPECT_EQ(adjacentScore.nausea, 0);

    const auto underBridgeScore = RideRating::GetMazeLocalContextScore(underBridgeOrigin, rideId);
    EXPECT_EQ(underBridgeScore.pathBridge, 0);
    EXPECT_EQ(underBridgeScore.pathNearMiss, 0);
    EXPECT_EQ(underBridgeScore.pathLoop, 0);
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
    PlaceBridgeLine(elevatedPathTile, groundZ + (6 * kCoordsZStep), groundZ + (7 * kCoordsZStep), 0);
    RideRating::ClearLocalContextCache();

    const auto overWall = RideRating::GetMazeLocalContextScore(origin, rideId);
    EXPECT_GT(overWall.scenery, 0);
    EXPECT_GT(overWall.pathBridge, 0);
    EXPECT_EQ(overWall.intensity, 0);
    EXPECT_EQ(overWall.nausea, 0);
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

TEST_F(RideRatings, CompletedAggregateSampleMarksRideTestedAndPublishesRatings)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("bpb.sv6"));

    auto* target = FindNormalAggregateRideWithSummaryStatGate();
    ASSERT_NE(target, nullptr);

    target->status = RideStatus::open;
    target->flags.unset(RideFlag::tested, RideFlag::testInProgress);
    target->ratings.setNull();
    target->unreliabilityFactor = 0x5A;
    RideClearRiderRatingSamples(*target);

    RideRatingAccumulator sample{};
    sample.excitement = 100000 * RideRating::kRideRatingAccumulatorRawScale;
    sample.intensity = 80000 * RideRating::kRideRatingAccumulatorRawScale;
    sample.nausea = 60000 * RideRating::kRideRatingAccumulatorRawScale;
    sample.ticks = 100;

    RideRating::RecordRiderSample(*target, sample);

    EXPECT_TRUE(target->flags.has(RideFlag::tested));
    EXPECT_EQ(target->recentRatingSampleCount, 1);
    EXPECT_FALSE(target->ratings.isNull());
    EXPECT_EQ(target->unreliabilityFactor, 0x5A);
}

TEST_F(RideRatings, MazeCompletedSamplesRemainAveragedOverRecentTwenty)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));

    auto* target = FindMazeRide();
    ASSERT_NE(target, nullptr);

    target->status = RideStatus::open;
    target->flags.unset(RideFlag::tested, RideFlag::testInProgress);
    target->ratings.setNull();
    RideClearRiderRatingSamples(*target);

    for (size_t i = 0; i < kRideRatingRecentSampleCount + 5; i++)
    {
        const auto value = static_cast<int64_t>(i + 1);
        RideRatingAccumulator sample{};
        sample.excitement = value * 2;
        sample.intensity = value * 4;
        sample.nausea = value * 6;
        sample.ticks = 1;

        RideRating::RecordRiderSample(*target, sample);
    }

    EXPECT_TRUE(target->flags.has(RideFlag::tested));
    EXPECT_EQ(target->recentRatingSampleCount, kRideRatingRecentSampleCount);

    const auto accumulator = RideGetRecentRatingAccumulator(*target);
    EXPECT_EQ(accumulator.excitement, 31);
    EXPECT_EQ(accumulator.intensity, 62);
    EXPECT_EQ(accumulator.nausea, 93);
    EXPECT_EQ(accumulator.ticks, 1u);
    EXPECT_FALSE(target->ratings.isNull());
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

TEST_F(RideRatings, AggregateVehicleRatingsIgnoreLegacySummaryStatGates)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("bpb.sv6"));

    auto* target = FindNormalAggregateRideWithSummaryStatGate();
    ASSERT_NE(target, nullptr);
    PrepareAggregateRideWithStableSample(*target);

    SetLegacySummaryStatsPoor(*target);
    RideRating::UpdateRide(*target);
    const auto poorStatsRatings = target->ratings;

    SetLegacySummaryStatsStrong(*target);
    RideRating::UpdateRide(*target);
    const auto strongStatsRatings = target->ratings;

    EXPECT_EQ(strongStatsRatings, poorStatsRatings);
}

TEST_F(RideRatings, AggregateMazeRatingsIgnoreLegacySummaryStatGates)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    GetContext()->LoadParkFromFile(TestData::GetParkPath("EverythingPark.park"));

    auto* target = FindMazeRide();
    ASSERT_NE(target, nullptr);
    PrepareAggregateRideWithStableSample(*target);

    SetLegacySummaryStatsPoor(*target);
    RideRating::UpdateRide(*target);
    const auto poorStatsRatings = target->ratings;

    SetLegacySummaryStatsStrong(*target);
    RideRating::UpdateRide(*target);
    const auto strongStatsRatings = target->ratings;

    EXPECT_EQ(strongStatsRatings, poorStatsRatings);
}

TEST_F(RideRatings, EverythingPark)
{
    TestRatings("EverythingPark.park", 529);
}
