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
#include <openrct2/Diagnostic.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/GameStateSnapshots.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/audio/AudioContext.h>
#include <openrct2/core/Crypt.h>
#include <openrct2/core/MemoryStream.h>
#include <openrct2/core/String.hpp>
#include <openrct2/core/UnitConversion.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/park/ParkFile.h>
#include <openrct2/rct2/RCT2.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/world/MapAnimation.h>
#include <limits>
#include <string>

using namespace OpenRCT2;

static bool LoadFileToBuffer(MemoryStream& stream, const std::string& filePath)
{
    FILE* fp = fopen(filePath.c_str(), "rb");
    EXPECT_NE(fp, nullptr);
    if (fp == nullptr)
        return false;

    uint8_t buf[1024];
    size_t bytesRead = fread(buf, 1, sizeof(buf), fp);
    while (bytesRead > 0)
    {
        stream.Write(buf, bytesRead);
        bytesRead = fread(buf, 1, sizeof(buf), fp);
    }
    fclose(fp);

    return true;
}

static void GameInit(bool retainSpatialIndices)
{
    auto& gameState = getGameState();
    if (!retainSpatialIndices)
        gameState.entities.ResetEntitySpatialIndices();

    ResetAllSpriteQuadrantPlacements();
    LoadPalette();
    EntityTweener::Get().Reset();
    MapAnimations::MarkAllTiles();
    FixInvalidVehicleSpriteSizes();

    gGameSpeed = 1;
}

static bool ImportS6(MemoryStream& stream, std::unique_ptr<IContext>& context, bool retainSpatialIndices)
{
    stream.SetPosition(0);

    auto& objManager = context->GetObjectManager();

    auto importer = ParkImporter::CreateS6(context->GetObjectRepository());
    auto loadResult = importer->LoadFromStream(&stream, false);
    objManager.LoadObjects(loadResult.RequiredObjects);

    MapAnimations::ClearAll();
    // TODO: Have a separate GameState and exchange once loaded.
    auto& gameState = getGameState();
    importer->Import(gameState);

    GameInit(retainSpatialIndices);

    return true;
}

static bool ImportPark(MemoryStream& stream, std::unique_ptr<IContext>& context, bool retainSpatialIndices)
{
    stream.SetPosition(0);

    auto& objManager = context->GetObjectManager();

    auto importer = ParkImporter::CreateParkFile(context->GetObjectRepository());
    auto loadResult = importer->LoadFromStream(&stream, false);
    objManager.LoadObjects(loadResult.RequiredObjects);

    // TODO: Have a separate GameState and exchange once loaded.
    auto& gameState = getGameState();
    importer->Import(gameState);

    GameInit(retainSpatialIndices);

    return true;
}

static bool ExportSave(
    MemoryStream& stream, std::unique_ptr<IContext>& context, uint32_t targetVersion = kParkFileCurrentVersion)
{
    auto& objManager = context->GetObjectManager();

    auto exporter = std::make_unique<ParkFileExporter>();
    exporter->ExportObjectsList = objManager.GetPackableObjects();
    exporter->TargetVersion = targetVersion;

    auto& gameState = getGameState();
    exporter->Export(gameState, stream, kParkFileSaveCompressionLevel);

    return true;
}

static std::unique_ptr<IContext> ImportBigMap()
{
    auto context = CreateContext();
    MemoryStream stream;
    if (!context->Initialise()
        || !LoadFileToBuffer(stream, TestData::GetParkPath("BigMapTest.sv6")) || !ImportS6(stream, context, false))
        context.reset();
    return context;
}

static std::unique_ptr<IContext> ImportParkVersion(MemoryStream& stream)
{
    auto context = CreateContext();
    if (!context->Initialise() || !ImportPark(stream, context, true))
        context.reset();
    return context;
}

static Ride* GetFirstRide()
{
    for (auto& ride : getGameState().rides)
    {
        if (ride.id != RideId::GetNull())
        {
            return &ride;
        }
    }
    return nullptr;
}

static void RecordGameStateSnapshot(std::unique_ptr<IContext>& context, MemoryStream& snapshotStream)
{
    auto* snapshots = context->GetGameStateSnapshots();

    auto& snapshot = snapshots->CreateSnapshot();
    snapshots->Capture(snapshot);
    snapshots->LinkSnapshot(snapshot, getGameState().currentTicks, ScenarioRandState().s0);
    DataSerialiser snapShotDs(true, snapshotStream);
    snapshots->SerialiseSnapshot(snapshot, snapShotDs);
}

static void AdvanceGameTicks(uint32_t ticks, std::unique_ptr<IContext>& context)
{
    for (uint32_t i = 0; i < ticks; i++)
    {
        gameStateUpdateLogic();
    }
}

static void CompareStates(MemoryStream& importBuffer, MemoryStream& exportBuffer, MemoryStream& snapshotStream)
{
    if (importBuffer.GetLength() != exportBuffer.GetLength())
    {
        LOG_WARNING(
            "Inconsistent export size! Import Size: %llu bytes, Export Size: %llu bytes",
            static_cast<unsigned long long>(importBuffer.GetLength()),
            static_cast<unsigned long long>(exportBuffer.GetLength()));
    }

    std::unique_ptr<IContext> context = CreateContext();
    EXPECT_NE(context, nullptr);
    bool initialised = context->Initialise();
    ASSERT_TRUE(initialised);

    DataSerialiser ds(false, snapshotStream);
    IGameStateSnapshots* snapshots = GetContext()->GetGameStateSnapshots();

    GameStateSnapshot_t& importSnapshot = snapshots->CreateSnapshot();
    snapshots->SerialiseSnapshot(importSnapshot, ds);

    GameStateSnapshot_t& exportSnapshot = snapshots->CreateSnapshot();
    snapshots->SerialiseSnapshot(exportSnapshot, ds);

    try
    {
        GameStateCompareData cmpData = snapshots->Compare(importSnapshot, exportSnapshot);

        // Find out if there are any differences between the two states
        auto res = std::find_if(
            cmpData.spriteChanges.begin(), cmpData.spriteChanges.end(),
            [](const GameStateSpriteChange& diff) { return diff.changeType != GameStateSpriteChange::EQUAL; });

        if (res != cmpData.spriteChanges.end())
        {
            LOG_WARNING("Snapshot data differences. %s", snapshots->GetCompareDataText(cmpData).c_str());
            FAIL();
        }
    }
    catch (const std::runtime_error& err)
    {
        LOG_WARNING("Snapshot data failed to be read. Snapshot not compared. %s", err.what());
        FAIL();
    }
}

TEST(S6ImportExportBasic, all)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    MemoryStream importBuffer;
    MemoryStream exportBuffer;
    MemoryStream snapshotStream;

    // Load initial park data.
    {
        std::unique_ptr<IContext> context = CreateContext();
        EXPECT_NE(context, nullptr);

        bool initialised = context->Initialise();
        ASSERT_TRUE(initialised);

        std::string testParkPath = TestData::GetParkPath("BigMapTest.sv6");
        ASSERT_TRUE(LoadFileToBuffer(importBuffer, testParkPath));
        ASSERT_TRUE(ImportS6(importBuffer, context, false));
        RecordGameStateSnapshot(context, snapshotStream);

        ASSERT_TRUE(ExportSave(exportBuffer, context));
    }

    // Import the exported version.
    {
        auto context = ImportParkVersion(exportBuffer);
        EXPECT_NE(context, nullptr);

        RecordGameStateSnapshot(context, snapshotStream);
    }

    snapshotStream.SetPosition(0);
    CompareStates(importBuffer, exportBuffer, snapshotStream);

    SUCCEED();
}

TEST(ParkFileMigration, LegacyRideLengthsScaleOnce)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    MemoryStream oldVersionPark;
    MemoryStream currentVersionPark;

    {
        auto context = ImportBigMap();
        ASSERT_NE(context, nullptr);

        auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        ride->getStation().SegmentLength = static_cast<int32_t>(static_cast<int64_t>(426) << 16);
        ride->shelteredLength = static_cast<int32_t>(static_cast<int64_t>(213) << 16);

        ASSERT_TRUE(ExportSave(oldVersionPark, context, kRideItemSalesHistoryVersion));
    }

    {
        auto context = ImportParkVersion(oldVersionPark);
        ASSERT_NE(context, nullptr);

        auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_EQ(ToHumanReadableRideLength(ride->getStation().SegmentLength), 316);
        EXPECT_EQ(ToHumanReadableRideLength(ride->shelteredLength), 158);

        ASSERT_TRUE(ExportSave(currentVersionPark, context));
    }

    {
        auto context = ImportParkVersion(currentVersionPark);
        ASSERT_NE(context, nullptr);

        auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_EQ(ToHumanReadableRideLength(ride->getStation().SegmentLength), 316);
        EXPECT_EQ(ToHumanReadableRideLength(ride->shelteredLength), 158);
    }
}

TEST(ParkFileMigration, LongitudinalGStatsRoundTripAndDefaultForPreviousVersion)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    MemoryStream currentVersionPark;
    MemoryStream longitudinalStatsVersionPark;
    MemoryStream previousVersionPark;

    {
        auto context = ImportBigMap();
        ASSERT_NE(context, nullptr);

        auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        ride->maxPositiveLongitudinalG = 135;
        ride->maxNegativeLongitudinalG = -95;
        ride->previousLongitudinalG = 42;
        ride->previousLongitudinalVelocity = 123456;
        ride->hasPreviousLongitudinalVelocity = true;
        ride->ratingAccumulator.previousTrainVelocity = 654321;
        ride->ratingAccumulator.hasPreviousTrainVelocity = true;
        ride->stableStats.valid = true;
        ride->stableStats.maxPositiveLongitudinalG = 125;
        ride->stableStats.maxNegativeLongitudinalG = -85;
        ride->measurement = std::make_unique<RideMeasurement>();
        ride->measurement->num_items = 2;
        ride->measurement->previousVelocity = 345678;
        ride->measurement->hasPreviousVelocity = true;
        ride->measurement->longitudinal[0] = 17;
        ride->measurement->longitudinal[1] = -23;

        ASSERT_TRUE(ExportSave(currentVersionPark, context));
        ASSERT_TRUE(ExportSave(longitudinalStatsVersionPark, context, kLongitudinalGStatsVersion));
        ASSERT_TRUE(ExportSave(previousVersionPark, context, kRideRatingActiveSampleVectorVersion));
    }

    {
        auto context = ImportParkVersion(currentVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_EQ(ride->maxPositiveLongitudinalG, 135);
        EXPECT_EQ(ride->maxNegativeLongitudinalG, -95);
        EXPECT_EQ(ride->previousLongitudinalG, 42);
        EXPECT_EQ(ride->previousLongitudinalVelocity, 123456);
        EXPECT_TRUE(ride->hasPreviousLongitudinalVelocity);
        EXPECT_EQ(ride->ratingAccumulator.previousTrainVelocity, 654321);
        EXPECT_TRUE(ride->ratingAccumulator.hasPreviousTrainVelocity);
        EXPECT_EQ(ride->stableStats.maxPositiveLongitudinalG, 125);
        EXPECT_EQ(ride->stableStats.maxNegativeLongitudinalG, -85);
        ASSERT_NE(ride->measurement, nullptr);
        EXPECT_EQ(ride->measurement->previousVelocity, 345678);
        EXPECT_TRUE(ride->measurement->hasPreviousVelocity);
        EXPECT_EQ(ride->measurement->longitudinal[0], 17);
        EXPECT_EQ(ride->measurement->longitudinal[1], -23);
    }

    {
        auto context = ImportParkVersion(longitudinalStatsVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_EQ(ride->maxPositiveLongitudinalG, 135);
        EXPECT_EQ(ride->maxNegativeLongitudinalG, -95);
        EXPECT_EQ(ride->previousLongitudinalG, 42);
        EXPECT_EQ(ride->previousLongitudinalVelocity, 0);
        EXPECT_FALSE(ride->hasPreviousLongitudinalVelocity);
        EXPECT_EQ(ride->ratingAccumulator.previousTrainVelocity, 0);
        EXPECT_FALSE(ride->ratingAccumulator.hasPreviousTrainVelocity);
        EXPECT_EQ(ride->stableStats.maxPositiveLongitudinalG, 125);
        EXPECT_EQ(ride->stableStats.maxNegativeLongitudinalG, -85);
        ASSERT_NE(ride->measurement, nullptr);
        EXPECT_EQ(ride->measurement->previousVelocity, 0);
        EXPECT_FALSE(ride->measurement->hasPreviousVelocity);
        EXPECT_EQ(ride->measurement->longitudinal[0], 17);
        EXPECT_EQ(ride->measurement->longitudinal[1], -23);
    }

    {
        auto context = ImportParkVersion(previousVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_EQ(ride->maxPositiveLongitudinalG, 0);
        EXPECT_EQ(ride->maxNegativeLongitudinalG, 0);
        EXPECT_EQ(ride->previousLongitudinalG, 0);
        EXPECT_EQ(ride->previousLongitudinalVelocity, 0);
        EXPECT_FALSE(ride->hasPreviousLongitudinalVelocity);
        EXPECT_EQ(ride->ratingAccumulator.previousTrainVelocity, 0);
        EXPECT_FALSE(ride->ratingAccumulator.hasPreviousTrainVelocity);
        EXPECT_EQ(ride->stableStats.maxPositiveLongitudinalG, 0);
        EXPECT_EQ(ride->stableStats.maxNegativeLongitudinalG, 0);
        ASSERT_NE(ride->measurement, nullptr);
        EXPECT_EQ(ride->measurement->previousVelocity, 0);
        EXPECT_FALSE(ride->measurement->hasPreviousVelocity);
        EXPECT_EQ(ride->measurement->longitudinal[0], 0);
        EXPECT_EQ(ride->measurement->longitudinal[1], 0);
    }
}

TEST(ParkFileMigration, TransportDestinationRoundTripsAndIsRemovedFromOlderTargets)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    MemoryStream currentVersionPark;
    MemoryStream previousVersionPark;
    EntityId guestId = EntityId::GetNull();
    RideId transportRide = RideId::GetNull();

    {
        auto context = ImportBigMap();
        ASSERT_NE(context, nullptr);

        auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        ride->type = RIDE_TYPE_MONORAIL;
        ride->priceTarget = RidePriceTarget::free;
        transportRide = ride->id;
        auto* guest = Guest::generate({ 0, 0, 0 });
        ASSERT_NE(guest, nullptr);
        guestId = guest->id;
        guest->setTransportRoute(transportRide, StationIndex::FromUnderlying(0), StationIndex::FromUnderlying(2), true);

        ASSERT_TRUE(ExportSave(currentVersionPark, context));
        ASSERT_TRUE(ExportSave(previousVersionPark, context, kTransportRideStatsVersion));
    }

    {
        auto context = ImportParkVersion(currentVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_EQ(ride->priceTarget, RidePriceTarget::free);
        const auto* guest = getGameState().entities.GetEntity<Guest>(guestId);
        ASSERT_NE(guest, nullptr);
        EXPECT_TRUE(guest->hasTransportRoute());
        EXPECT_EQ(guest->previousRide, transportRide);
        EXPECT_EQ(guest->CurrentRideStation, StationIndex::FromUnderlying(0));
        EXPECT_EQ(guest->transportDestinationStation, StationIndex::FromUnderlying(2));
        EXPECT_TRUE(guest->transportRouteWasExtortive);
    }

    {
        auto context = ImportParkVersion(previousVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_EQ(ride->priceTarget, RidePriceTarget::neutral);
        const auto* guest = getGameState().entities.GetEntity<Guest>(guestId);
        ASSERT_NE(guest, nullptr);
        EXPECT_FALSE(guest->hasTransportRoute());
        EXPECT_EQ(guest->previousRide, transportRide);
        EXPECT_EQ(guest->previousRideTimeOut, 0);
        EXPECT_TRUE(guest->transportDestinationStation.IsNull());
        EXPECT_FALSE(guest->transportRouteWasExtortive);
    }
}

TEST(ParkFileMigration, RideRatingLegsRoundTripAndOlderTargetKeepsLiveState)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    MemoryStream currentVersionPark;
    MemoryStream previousShelterVersionPark;
    MemoryStream previousVersionPark;
    MemoryStream legacyActivePark;

    {
        auto context = ImportBigMap();
        ASSERT_NE(context, nullptr);

        auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        ride->type = RIDE_TYPE_MONORAIL;
        ride->numStations = 3;
        auto* legacyActive = RideGetOrCreateActiveRatingSample(*ride, EntityId::FromUnderlying(321));
        ASSERT_NE(legacyActive, nullptr);
        legacyActive->excitement = 99'000;
        legacyActive->ticks = 10;
        ASSERT_TRUE(ExportSave(legacyActivePark, context, kStationPlatformPreQueueVersion));

        RideRatingAccumulator sample{};
        sample.originStation = StationIndex::FromUnderlying(0);
        sample.destinationStation = StationIndex::FromUnderlying(2);
        sample.excitement = 123'000;
        sample.intensity = 234'000;
        sample.nausea = 345'000;
        sample.transportDistance = 1'000;
        sample.transportComfort = 900'000;
        sample.transportDecoration = 1'000'000;
        sample.transportShelteredDistance = 625;
        sample.sampledDistance = static_cast<int64_t>(1'234) << 16;
        sample.totalSpeed = 40LL * 0x80000;
        sample.maxSpeed = 0x90000;
        sample.maxPositiveVerticalG = 175;
        sample.maxNegativeVerticalG = -65;
        sample.ticks = 40;
        RideAddRecentRatingSample(*ride, sample);
        ASSERT_EQ(ride->ratingLegs.size(), 1);

        ASSERT_TRUE(ExportSave(currentVersionPark, context));
        ASSERT_TRUE(ExportSave(previousShelterVersionPark, context, kRideRatingLegsVersion));
        ASSERT_TRUE(ExportSave(previousVersionPark, context, kStationPlatformPreQueueVersion));
        ASSERT_EQ(ride->ratingLegs.size(), 1);
        EXPECT_EQ(ride->ratingLegs[0].destinationStation, sample.destinationStation);
    }

    {
        auto context = ImportParkVersion(currentVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        ASSERT_EQ(ride->ratingLegs.size(), 1);
        const auto& leg = ride->ratingLegs[0];
        EXPECT_EQ(leg.originStation, StationIndex::FromUnderlying(0));
        EXPECT_EQ(leg.destinationStation, StationIndex::FromUnderlying(2));
        EXPECT_EQ(leg.recentSampleCount, 1);
        EXPECT_EQ(leg.recentSampleNext, 1);
        EXPECT_EQ(leg.recentSamples[0].originStation, leg.originStation);
        EXPECT_EQ(leg.recentSamples[0].destinationStation, leg.destinationStation);
        EXPECT_TRUE(leg.recentSamples[0].sampleEntity.IsNull());
        EXPECT_TRUE(leg.recentSamples[0].sampleComplete);
        const auto sample = RideGetRecentRatingAccumulator(leg);
        EXPECT_EQ(sample.sampledDistance, static_cast<int64_t>(1'234) << 16);
        EXPECT_EQ(sample.maxSpeed, 0x90000);
        EXPECT_EQ(sample.maxPositiveVerticalG, 175);
        EXPECT_EQ(sample.maxNegativeVerticalG, -65);
        EXPECT_EQ(sample.transportShelteredDistance, 625);
    }

    {
        auto context = ImportParkVersion(previousShelterVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        ASSERT_EQ(ride->ratingLegs.size(), 1);
        EXPECT_EQ(RideGetRecentRatingAccumulator(ride->ratingLegs[0]).transportShelteredDistance, 0);
    }

    {
        auto context = ImportParkVersion(previousVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_TRUE(ride->ratingLegs.empty());
        EXPECT_EQ(ride->recentRatingSampleCount, 0);
    }

    {
        auto context = ImportParkVersion(legacyActivePark);
        ASSERT_NE(context, nullptr);

        const auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        EXPECT_TRUE(ride->activeRatingSamples.empty());
    }
}

TEST(ParkFileMigration, PlatformGuestRoundTripsAndOlderTargetUsesStationExitRecovery)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    MemoryStream currentVersionPark;
    MemoryStream previousVersionPark;
    EntityId guestId = EntityId::GetNull();
    TileCoordsXYZD exit{};

    {
        auto context = ImportBigMap();
        ASSERT_NE(context, nullptr);

        auto* ride = GetFirstRide();
        ASSERT_NE(ride, nullptr);
        ride->type = RIDE_TYPE_MONORAIL;
        ride->numStations = std::max<uint8_t>(ride->numStations, 1);
        auto& station = ride->getStation(StationIndex::FromUnderlying(0));
        station.Entrance = { 10, 10, 2, 0 };
        station.Exit = { 12, 10, 2, 2 };
        exit = station.Exit;

        auto* train = getGameState().entities.CreateEntity<Vehicle>();
        ASSERT_NE(train, nullptr);
        train->SubType = Vehicle::Type::head;
        train->num_seats = 2;
        train->next_vehicle_on_train = EntityId::GetNull();
        ride->numTrains = 1;
        ride->vehicles[0] = train->id;

        auto* guest = Guest::generate({ 10 * kCoordsXYStep, 10 * kCoordsXYStep, station.GetBaseZ() });
        ASSERT_NE(guest, nullptr);
        guestId = guest->id;
        guest->CurrentRide = ride->id;
        guest->CurrentRideStation = StationIndex::FromUnderlying(0);
        guest->CurrentTrain = RideStation::kNoTrain;
        guest->CurrentCar = 0;
        guest->CurrentSeat = 0;
        guest->State = PeepState::enteringRide;
        guest->RideSubState = PeepRideSubState::waitingOnPlatform;
        guest->SetDestination({ 11 * kCoordsXYStep, 10 * kCoordsXYStep }, 2);

        ASSERT_TRUE(ExportSave(currentVersionPark, context));
        ASSERT_TRUE(ExportSave(previousVersionPark, context, kTransportJourneyRoutingVersion));
    }

    {
        auto context = ImportParkVersion(currentVersionPark);
        ASSERT_NE(context, nullptr);

        auto* guest = getGameState().entities.GetEntity<Guest>(guestId);
        ASSERT_NE(guest, nullptr);
        EXPECT_EQ(guest->State, PeepState::enteringRide);
        EXPECT_EQ(guest->RideSubState, PeepRideSubState::waitingOnPlatform);
        EXPECT_EQ(guest->CurrentTrain, RideStation::kNoTrain);
        auto* ride = GetRide(guest->CurrentRide);
        ASSERT_NE(ride, nullptr);
        EXPECT_TRUE(RideStationPlatformPreQueueIsActive(*ride, guest->CurrentRideStation));
        EXPECT_EQ(RideGetTransportStationPlatformOccupancy(*ride, guest->CurrentRideStation), 1);
        EXPECT_EQ(RideGetTransportStationPlatformCapacity(*ride, guest->CurrentRideStation), 2);

        auto* train = getGameState().entities.GetEntity<Vehicle>(ride->vehicles[0]);
        ASSERT_NE(train, nullptr);
        train->num_peeps = 1;
        train->next_free_seat = 1;
        train->peep[0] = EntityId::FromUnderlying(900);
        train->peep[1] = EntityId::GetNull();
        // A through-rider still occupies the guest's exact platform seat. The
        // saved assignment must remain unchanged for the next train.
        EXPECT_EQ(guest->CurrentCar, 0u);
        EXPECT_EQ(guest->CurrentSeat, 0u);
        ride->status = RideStatus::open;
        ride->getStation(guest->CurrentRideStation).TrainAtStation = 0;
        train->status = Vehicle::Status::waitingForPassengers;
        guest->StepProgress = std::numeric_limits<uint8_t>::max();
        guest->update();

        EXPECT_EQ(guest->State, PeepState::enteringRide);
        EXPECT_EQ(guest->RideSubState, PeepRideSubState::waitingOnPlatform);
        EXPECT_EQ(guest->CurrentTrain, RideStation::kNoTrain);
        EXPECT_EQ(guest->CurrentCar, 0u);
        EXPECT_EQ(guest->CurrentSeat, 0u);
        EXPECT_EQ(train->peep[0], EntityId::FromUnderlying(900));
        EXPECT_TRUE(train->peep[1].IsNull());
    }

    {
        auto context = ImportParkVersion(previousVersionPark);
        ASSERT_NE(context, nullptr);

        const auto* guest = getGameState().entities.GetEntity<Guest>(guestId);
        ASSERT_NE(guest, nullptr);
        EXPECT_EQ(guest->State, PeepState::leavingRide);
        EXPECT_EQ(guest->RideSubState, PeepRideSubState::approachExit);
        EXPECT_EQ(
            guest->GetDestination().x,
            exit.x * kCoordsXYStep + kCoordsXYHalfTile - DirectionOffsets[exit.direction].x * 20);
        EXPECT_EQ(
            guest->GetDestination().y,
            exit.y * kCoordsXYStep + kCoordsXYHalfTile - DirectionOffsets[exit.direction].y * 20);
    }
}

TEST(S6ImportExportAdvanceTicks, all)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;

    MemoryStream importBuffer;
    MemoryStream exportBuffer;
    MemoryStream snapshotStream;

    // Load initial park data.
    {
        std::unique_ptr<IContext> context = CreateContext();
        EXPECT_NE(context, nullptr);

        bool initialised = context->Initialise();
        ASSERT_TRUE(initialised);

        std::string testParkPath = TestData::GetParkPath("BigMapTest.sv6");
        ASSERT_TRUE(LoadFileToBuffer(importBuffer, testParkPath));
        ASSERT_TRUE(ImportS6(importBuffer, context, false));
        AdvanceGameTicks(1000, context);
        ASSERT_TRUE(ExportSave(exportBuffer, context));

        RecordGameStateSnapshot(context, snapshotStream);
    }

    // Import the exported version.
    {
        auto context = ImportParkVersion(exportBuffer);
        EXPECT_NE(context, nullptr);

        RecordGameStateSnapshot(context, snapshotStream);
    }

    snapshotStream.SetPosition(0);
    CompareStates(importBuffer, exportBuffer, snapshotStream);

    SUCCEED();
}

TEST(SeaDecrypt, DecryptSea)
{
    auto path = TestData::GetParkPath("volcania.sea");
    auto decrypted = DecryptSea(path);
    auto sha1 = Crypt::SHA1(decrypted.data(), decrypted.size());
    std::array<uint8_t, 20> expected = {
        0x1B, 0x85, 0xFC, 0xC0, 0xE8, 0x9B, 0xBE, 0x72, 0xD9, 0x1F, 0x6E, 0xC8, 0xB1, 0xFF, 0xEC, 0x70, 0x2A, 0x72, 0x05, 0xBB,
    };
    ASSERT_EQ(sha1, expected);
}
