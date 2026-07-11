/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <array>
#include <gtest/gtest.h>
#include <openrct2/ride/Ride.h>

TEST(TrainStationAssignment, TwoStationsReceiveOneTrainEachInPhysicalRingOrder)
{
    constexpr std::array stationOrder = {
        StationIndex::FromUnderlying(2),
        StationIndex::FromUnderlying(0),
    };
    constexpr std::array<uint8_t, 2> stationLengths = { 1, 1 };
    const auto assignments = RideBuildTrainStationAssignments(stationOrder, stationLengths, 80'000, 1, 2);

    ASSERT_EQ(assignments.size(), 2u);
    EXPECT_EQ(assignments[0], stationOrder[0]);
    EXPECT_EQ(assignments[1], stationOrder[1]);
}

TEST(TrainStationAssignment, AdditionalTrainsRemainGroupedByStationForTheVehicleRing)
{
    constexpr std::array stationOrder = {
        StationIndex::FromUnderlying(2),
        StationIndex::FromUnderlying(0),
    };
    constexpr std::array<uint8_t, 2> stationLengths = { 1, 1 };
    const auto assignments = RideBuildTrainStationAssignments(stationOrder, stationLengths, 80'000, 1, 4);

    ASSERT_EQ(assignments.size(), 4u);
    EXPECT_EQ(assignments[0], stationOrder[0]);
    EXPECT_EQ(assignments[1], stationOrder[0]);
    EXPECT_EQ(assignments[2], stationOrder[1]);
    EXPECT_EQ(assignments[3], stationOrder[1]);
}

TEST(TrainStationAssignment, LongerStationReceivesOnlyTheAdditionalConsistsItCanPhysicallyFit)
{
    constexpr std::array stationOrder = {
        StationIndex::FromUnderlying(0),
        StationIndex::FromUnderlying(1),
    };
    constexpr std::array<uint8_t, 2> stationLengths = { 1, 2 };
    const auto assignments = RideBuildTrainStationAssignments(stationOrder, stationLengths, 150'000, 2, 4);

    ASSERT_EQ(assignments.size(), 4u);
    EXPECT_EQ(assignments[0], stationOrder[0]);
    EXPECT_EQ(assignments[1], stationOrder[1]);
    EXPECT_EQ(assignments[2], stationOrder[1]);
    EXPECT_EQ(assignments[3], stationOrder[1]);
}

TEST(TrainStationAssignment, SingleStationKeepsDeterministicCreationOrder)
{
    constexpr std::array stationOrder = { StationIndex::FromUnderlying(3) };
    constexpr std::array<uint8_t, 1> stationLengths = { 1 };
    const auto assignments = RideBuildTrainStationAssignments(stationOrder, stationLengths, 80'000, 2, 2);

    ASSERT_EQ(assignments.size(), 2u);
    EXPECT_EQ(assignments[0], stationOrder[0]);
    EXPECT_EQ(assignments[1], stationOrder[0]);
}

TEST(TrainStationAssignment, AssignmentStopsWhenPhysicalStationCapacityIsExhausted)
{
    constexpr std::array stationOrder = {
        StationIndex::FromUnderlying(0),
        StationIndex::FromUnderlying(1),
    };
    constexpr std::array<uint8_t, 2> stationLengths = { 1, 1 };
    const auto assignments = RideBuildTrainStationAssignments(stationOrder, stationLengths, 150'000, 2, 3);

    ASSERT_EQ(assignments.size(), 2u);
    EXPECT_EQ(assignments[0], stationOrder[0]);
    EXPECT_EQ(assignments[1], stationOrder[1]);
}
