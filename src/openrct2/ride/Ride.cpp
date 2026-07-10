/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Ride.h"

#include "../Cheats.h"
#include "../Context.h"
#include "../Diagnostic.h"
#include "../Editor.h"
#include "../GameState.h"
#include "../Input.h"
#include "../OpenRCT2.h"
#include "../actions/GameActionRunner.h"
#include "../actions/ResultWithMessage.h"
#include "../actions/ride/RideSetSettingAction.h"
#include "../actions/ride/RideSetStatusAction.h"
#include "../actions/ride/RideSetVehicleAction.h"
#include "../audio/Audio.h"
#include "../config/Config.h"
#include "../core/BitSet.hpp"
#include "../core/EnumUtils.hpp"
#include "../core/GameTime.hpp"
#include "../core/Guard.hpp"
#include "../core/Numerics.hpp"
#include "../core/UnitConversion.h"
#include "../drawing/Drawing.h"
#include "../entity/EntityList.h"
#include "../entity/EntityRegistry.h"
#include "../entity/Guest.h"
#include "../entity/Peep.h"
#include "../entity/Staff.h"
#include "../interface/Viewport.h"
#include "../interface/WindowBase.h"
#include "../localisation/Formatter.h"
#include "../localisation/Formatting.h"
#include "../localisation/StringWithArgs.h"
#include "../management/NewsItem.h"
#include "../object/MusicObject.h"
#include "../object/ObjectList.h"
#include "../object/ObjectManager.h"
#include "../object/RideObject.h"
#include "../object/StationObject.h"
#include "../profiling/Profiling.h"
#include "../rct1/RCT1.h"
#include "../scenario/Scenario.h"
#include "../scripting/ScriptEngine.h"
#include "../ui/WindowManager.h"
#include "../util/Util.h"
#include "../windows/Intent.h"
#include "../world/Entrance.h"
#include "../world/Footpath.h"
#include "../world/Location.hpp"
#include "../world/Map.h"
#include "../world/MapTopology.h"
#include "../world/Park.h"
#include "../world/TileElementsView.h"
#include "../world/Weather.h"
#include "../world/tile_element/EntranceElement.h"
#include "../world/tile_element/PathElement.h"
#include "../world/tile_element/TrackElement.h"
#include "CableLift.h"
#include "RideAudio.h"
#include "RideBreakdownMap.h"
#include "RideConstruction.h"
#include "RideData.h"
#include "RideEntry.h"
#include "RideManager.hpp"
#include "ShopItem.h"
#include "Station.h"
#include "TrackData.h"
#include "TrackDesign.h"
#include "TrackIteration.h"
#include "Vehicle.h"
#include "Vehicle.Station.h"
#include "ted/TrackElementDescriptor.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>

using namespace OpenRCT2;
using namespace OpenRCT2::TrackMetadata;
using namespace OpenRCT2::Scripting;

struct ActiveRatingSampleReference
{
    RideId rideId{ RideId::GetNull() };
    uint32_t sampleIndex{ std::numeric_limits<uint32_t>::max() };
};

// A vehicle belongs to one ride, so its globally unique entity id is the cheapest
// runtime key for the active accumulator. References are always validated against
// the owning ride and sample entity, which also makes stale entries safe after a
// ride, vehicle, or park is replaced.
static std::array<ActiveRatingSampleReference, kMaxEntities> _activeRatingSampleReferences{};

static void RideTransportServiceCacheReset();

static void RideCacheActiveRatingSampleReference(const Ride& ride, EntityId sampleEntity, size_t sampleIndex)
{
    auto& reference = _activeRatingSampleReferences[sampleEntity.ToUnderlying()];
    reference.rideId = ride.id;
    reference.sampleIndex = static_cast<uint32_t>(sampleIndex);
}

static constexpr auto kRideModeBlockSectionedCounterpart = std::to_array({
    RideMode::normal,                          // RideMode::normal,
    RideMode::continuousCircuitBlockSectioned, // RideMode::continuousCircuit,
    RideMode::reverseInclineLaunchedShuttle,   // RideMode::reverseInclineLaunchedShuttle,
    RideMode::poweredLaunchBlockSectioned,     // RideMode::poweredLaunchPasstrough,
    RideMode::shuttle,                         // RideMode::shuttle,
    RideMode::boatHire,                        // RideMode::boatHire,
    RideMode::upwardLaunch,                    // RideMode::upwardLaunch,
    RideMode::rotatingLift,                    // RideMode::rotatingLift,
    RideMode::stationToStation,                // RideMode::stationToStation,
    RideMode::singleRidePerAdmission,          // RideMode::singleRidePerAdmission,
    RideMode::unlimitedRidesPerAdmission,      // RideMode::unlimitedRidesPerAdmission ,
    RideMode::maze,                            // RideMode::maze,
    RideMode::race,                            // RideMode::race,
    RideMode::dodgems,                         // RideMode::dodgems,
    RideMode::swing,                           // RideMode::swing,
    RideMode::shopStall,                       // RideMode::shopStall,
    RideMode::rotation,                        // RideMode::rotation,
    RideMode::forwardRotation,                 // RideMode::forwardRotation,
    RideMode::backwardRotation,                // RideMode::backwardRotation,
    RideMode::filmAvengingAviators,            // RideMode::filmAvengingAviators,
    RideMode::mouseTails3DFilm,                // RideMode::mouseTails3DFilm,
    RideMode::spaceRings,                      // RideMode::spaceRings,
    RideMode::beginners,                       // RideMode::beginners,
    RideMode::limPoweredLaunch,                // RideMode::limPoweredLaunch,
    RideMode::filmThrillRiders,                // RideMode::filmThrillRiders,
    RideMode::stormChasers3DFilm,              // RideMode::stormChasers3DFilm,
    RideMode::spaceRaiders3DFilm,              // RideMode::spaceRaiders3DFilm,
    RideMode::intense,                         // RideMode::intense,
    RideMode::berserk,                         // RideMode::berserk,
    RideMode::hauntedHouse,                    // RideMode::hauntedHouse,
    RideMode::circus,                          // RideMode::circus,
    RideMode::downwardLaunch,                  // RideMode::downwardLaunch,
    RideMode::crookedHouse,                    // RideMode::crookedHouse,
    RideMode::freefallDrop,                    // RideMode::freefallDrop,
    RideMode::continuousCircuitBlockSectioned, // RideMode::continuousCircuitBlockSectioned,
    RideMode::poweredLaunchBlockSectioned,     // RideMode::poweredLaunch,
    RideMode::poweredLaunchBlockSectioned,     // RideMode::poweredLaunchBlockSectioned,
});
static_assert(kRideModeBlockSectionedCounterpart.size() == EnumValue(RideMode::count));

RideMode& operator++(RideMode& d, int)
{
    return d = (d == RideMode::count) ? RideMode::normal : static_cast<RideMode>(static_cast<uint8_t>(d) + 1);
}

static constexpr int32_t RideInspectionInterval[] = {
    10, 20, 30, 45, 60, 120, 0, 0,
};

// clang-format off
const StringId kRideInspectionIntervalNames[] = {
    STR_EVERY_10_MINUTES,
    STR_EVERY_20_MINUTES,
    STR_EVERY_30_MINUTES,
    STR_EVERY_45_MINUTES,
    STR_EVERY_HOUR,
    STR_EVERY_2_HOURS,
    STR_NEVER,
};
// clang-format on

static constexpr int32_t kPreciseBuildDateMarker = 1'000'000;
static constexpr int32_t kBuildDateFractionsPerMonth = 256;

static int64_t GetCurrentBuildDateFraction()
{
    const auto& date = GetDate();
    return (static_cast<int64_t>(date.GetMonthsElapsed()) * kBuildDateFractionsPerMonth)
        + ((static_cast<int64_t>(date.GetMonthTicks()) * kBuildDateFractionsPerMonth) / kTicksPerMonth);
}

static int64_t DecodeBuildDateFraction(int32_t buildDate)
{
    if (buildDate >= kPreciseBuildDateMarker)
    {
        return buildDate - kPreciseBuildDateMarker;
    }

    return static_cast<int64_t>(buildDate) * kBuildDateFractionsPerMonth;
}

static money64 ScaleMoneyByRatio(money64 amount, uint32_t numerator, uint32_t denominator)
{
    return (amount * numerator + (denominator / 2)) / denominator;
}

static uint32_t SumRecentCounts(const uint16_t (&history)[OpenRCT2::Limits::kCustomerHistorySize])
{
    uint32_t sum = 0;
    for (const auto count : history)
    {
        sum += count;
    }
    return sum;
}

int32_t RideGetCurrentBuildDate()
{
    return kPreciseBuildDateMarker + static_cast<int32_t>(GetCurrentBuildDateFraction());
}

money64 RideGetUpkeepCostPerHour(const Ride& ride)
{
    if (ride.upkeepCost == kMoney64Undefined)
    {
        return kMoney64Undefined;
    }

    return ScaleMoneyByRatio(ride.upkeepCost, GameTime::kTicksPerHour, GameTime::kGameTicksPerCalendarHalfMonth);
}

// A special instance of Ride that is used to draw previews such as the track designs.
static Ride _previewRide{};

struct StationIndexWithMessage
{
    ::StationIndex StationIndex;
    StringId Message = kStringIdNone;
};

// Static function declarations
Staff* FindClosestMechanic(const CoordsXY& entrancePosition, int32_t forInspection);
static void RideBreakdownStatusUpdate(Ride& ride);
static void RideBreakdownUpdate(Ride& ride, uint32_t currentTicks);
static void RideCallClosestMechanic(Ride& ride);
static void RideCallMechanic(Ride& ride, Peep* mechanic, int32_t forInspection);
static void RideEntranceExitConnected(Ride& ride);
static Breakdown RideGetNewBreakdownProblem(const Ride& ride);
static void RideInspectionUpdate(Ride& ride);
static void RideMechanicStatusUpdate(Ride& ride, MechanicStatus mechanicStatus);
static void RideMusicUpdate(Ride& ride, const RideTypeDescriptor& rtd);
static void RideShopConnected(const Ride& ride);

RideId GetNextFreeRideId()
{
    auto& gameState = getGameState();
    for (RideId::UnderlyingType i = 0; i < gameState.rides.size(); i++)
    {
        if (gameState.rides[i].id.IsNull())
        {
            return RideId::FromUnderlying(i);
        }
    }
    return RideId::GetNull();
}

Ride* RideAllocateAtIndex(RideId index)
{
    const auto idx = index.ToUnderlying();

    auto& gameState = getGameState();
    gameState.ridesEndOfUsedRange = std::max<size_t>(idx + 1, gameState.ridesEndOfUsedRange);

    auto result = &gameState.rides[idx];
    assert(result->id == RideId::GetNull());

    // Initialize the ride to all the defaults.
    *result = Ride{};

    // Because it is default initialized to zero rather than the magic constant for Null, fill the array.
    std::fill(std::begin(result->vehicles), std::end(result->vehicles), EntityId::GetNull());

    result->id = index;
    RideInvalidateTransportServiceCache(index);
    return result;
}

Ride& RideGetTemporaryForPreview()
{
    return _previewRide;
}

static void RideReset(Ride& ride)
{
    ride.id = RideId::GetNull();
    ride.type = kRideTypeNull;
    ride.customName = {};
    ride.measurement = {};
}

void RideDelete(RideId id)
{
    auto& gameState = getGameState();
    const auto idx = id.ToUnderlying();

    assert(idx < gameState.rides.size());
    assert(gameState.rides[idx].type != kRideTypeNull);

    RideInvalidateTransportServiceCache(id);
    auto& ride = gameState.rides[idx];
    RideClearStationPlatformPreQueue(ride);
    RideReset(ride);

    // Shrink maximum ride size.
    while (gameState.ridesEndOfUsedRange > 0 && gameState.rides[gameState.ridesEndOfUsedRange - 1].id.IsNull())
    {
        gameState.ridesEndOfUsedRange--;
    }
}

Ride* GetRide(RideId index)
{
    if (index.IsNull())
    {
        return nullptr;
    }

    auto& gameState = getGameState();
    const auto idx = index.ToUnderlying();
    if (idx >= gameState.rides.size())
    {
        return nullptr;
    }

    auto& ride = gameState.rides[idx];
    if (ride.type != kRideTypeNull)
    {
        assert(ride.id == index);
        return &ride;
    }

    return nullptr;
}

const RideObjectEntry* GetRideEntryByIndex(ObjectEntryIndex index)
{
    auto* context = GetContext();
    if (context == nullptr)
    {
        return nullptr;
    }
    auto& objMgr = context->GetObjectManager();

    auto obj = objMgr.GetLoadedObject<RideObject>(index);
    if (obj == nullptr)
    {
        return nullptr;
    }

    return static_cast<RideObjectEntry*>(obj->GetLegacyData());
}

std::string_view GetRideEntryName(ObjectEntryIndex index)
{
    if (index >= getObjectEntryGroupCount(ObjectType::ride))
    {
        LOG_ERROR("invalid index %d for ride type", index);
        return {};
    }

    auto objectEntry = ObjectEntryGetObject(ObjectType::ride, index);
    if (objectEntry != nullptr)
    {
        return objectEntry->GetLegacyIdentifier();
    }
    return {};
}

const RideObjectEntry* Ride::getRideEntry() const
{
    return GetRideEntryByIndex(subtype);
}

int32_t RideGetCount()
{
    auto& gameState = getGameState();
    return static_cast<int32_t>(RideManager(gameState).size());
}

RideRatingAccumulator* RideFindActiveRatingSample(Ride& ride, EntityId sampleEntity)
{
    if (sampleEntity.IsNull())
    {
        return nullptr;
    }

    auto& reference = _activeRatingSampleReferences[sampleEntity.ToUnderlying()];
    if (reference.rideId == ride.id && reference.sampleIndex < ride.activeRatingSamples.size())
    {
        auto& sample = ride.activeRatingSamples[reference.sampleIndex];
        if (sample.sampleEntity == sampleEntity)
        {
            return &sample;
        }
    }

    for (size_t i = 0; i < ride.activeRatingSamples.size(); i++)
    {
        auto& sample = ride.activeRatingSamples[i];
        if (sample.sampleEntity == sampleEntity)
        {
            RideCacheActiveRatingSampleReference(ride, sampleEntity, i);
            return &sample;
        }
    }

    return nullptr;
}

RideRatingAccumulator* RideGetOrCreateActiveRatingSample(Ride& ride, EntityId sampleEntity)
{
    if (sampleEntity.IsNull())
    {
        return nullptr;
    }

    if (auto* existingSample = RideFindActiveRatingSample(ride, sampleEntity); existingSample != nullptr)
    {
        return existingSample;
    }

    for (size_t i = 0; i < ride.activeRatingSamples.size(); i++)
    {
        auto& sample = ride.activeRatingSamples[i];
        if (sample.sampleEntity.IsNull() && !sample.hasSamples())
        {
            sample.sampleEntity = sampleEntity;
            RideCacheActiveRatingSampleReference(ride, sampleEntity, i);
            return &sample;
        }
    }

    auto& sample = ride.activeRatingSamples.emplace_back();
    sample.sampleEntity = sampleEntity;
    RideCacheActiveRatingSampleReference(ride, sampleEntity, ride.activeRatingSamples.size() - 1);
    return &sample;
}

static RideRatingAccumulator RideAverageRecentRatingSamples(
    std::span<const RideRatingAccumulator> samples, size_t sampleCount)
{
    RideRatingAccumulator result{};
    sampleCount = std::min(sampleCount, samples.size());
    size_t validSamples = 0;

    for (size_t i = 0; i < sampleCount; i++)
    {
        const auto& sample = samples[i];
        if (!sample.hasSamples())
        {
            continue;
        }

        result.excitement += sample.excitement;
        result.intensity += sample.intensity;
        result.nausea += sample.nausea;
        result.transportComfort += sample.transportComfort;
        result.transportDecoration += sample.transportDecoration;
        result.transportDistance += sample.transportDistance;
        result.sampledDistance += sample.sampledDistance;
        result.totalSpeed += sample.totalSpeed;
        result.maxSpeed = std::max(result.maxSpeed, sample.maxSpeed);
        result.maxPositiveVerticalG = std::max(result.maxPositiveVerticalG, sample.maxPositiveVerticalG);
        result.maxNegativeVerticalG = std::min(result.maxNegativeVerticalG, sample.maxNegativeVerticalG);
        result.maxLateralG = std::max(result.maxLateralG, sample.maxLateralG);
        result.maxPositiveLongitudinalG = std::max(
            result.maxPositiveLongitudinalG, sample.maxPositiveLongitudinalG);
        result.maxNegativeLongitudinalG = std::min(
            result.maxNegativeLongitudinalG, sample.maxNegativeLongitudinalG);
        result.ticks += sample.ticks;
        validSamples++;
    }

    if (validSamples == 0)
    {
        return {};
    }

    result.excitement /= static_cast<int64_t>(validSamples);
    result.intensity /= static_cast<int64_t>(validSamples);
    result.nausea /= static_cast<int64_t>(validSamples);
    result.transportComfort /= static_cast<int64_t>(validSamples);
    result.transportDecoration /= static_cast<int64_t>(validSamples);
    result.transportDistance /= static_cast<int64_t>(validSamples);
    result.sampledDistance /= static_cast<int64_t>(validSamples);
    result.totalSpeed /= static_cast<int64_t>(validSamples);
    result.ticks = std::max<uint32_t>(1, result.ticks / static_cast<uint32_t>(validSamples));
    return result;
}

RideRatingAccumulator RideGetRecentRatingAccumulator(const Ride& ride)
{
    return RideAverageRecentRatingSamples(ride.recentRatingSamples, ride.recentRatingSampleCount);
}

RideRatingAccumulator RideGetRecentRatingAccumulator(const RideRatingLeg& leg)
{
    return RideAverageRecentRatingSamples(leg.recentSamples, leg.recentSampleCount);
}

template<typename TRide>
static auto* RideGetRatingLegImpl(TRide& ride, StationIndex originStation, StationIndex destinationStation)
{
    const auto key = std::pair{ originStation.ToUnderlying(), destinationStation.ToUnderlying() };
    const auto it = std::lower_bound(
        ride.ratingLegs.begin(), ride.ratingLegs.end(), key,
        [](const RideRatingLeg& leg, const auto& candidate) {
            return std::pair{ leg.originStation.ToUnderlying(), leg.destinationStation.ToUnderlying() } < candidate;
        });
    return it != ride.ratingLegs.end() && it->originStation == originStation
            && it->destinationStation == destinationStation
        ? &*it
        : nullptr;
}

RideRatingLeg* RideGetRatingLeg(Ride& ride, StationIndex originStation, StationIndex destinationStation)
{
    return RideGetRatingLegImpl(ride, originStation, destinationStation);
}

const RideRatingLeg* RideGetRatingLeg(
    const Ride& ride, StationIndex originStation, StationIndex destinationStation)
{
    return RideGetRatingLegImpl(ride, originStation, destinationStation);
}

const RideRatingLeg* RideGetUniqueOutboundRatingLeg(const Ride& ride, StationIndex originStation)
{
    const RideRatingLeg* result = nullptr;
    for (const auto& leg : ride.ratingLegs)
    {
        if (leg.originStation != originStation || !leg.hasSamples())
        {
            continue;
        }
        if (result != nullptr)
        {
            return nullptr;
        }
        result = &leg;
    }
    return result;
}

const RideRatingLeg* RideGetRatingLegByDisplayIndex(const Ride& ride, size_t displayIndex)
{
    for (const auto& leg : ride.ratingLegs)
    {
        if (!leg.hasSamples())
        {
            continue;
        }
        if (displayIndex == 0)
        {
            return &leg;
        }
        displayIndex--;
    }
    return nullptr;
}

RideRatingLegMeasurements RideGetRatingLegMeasurements(const RideRatingLeg& leg)
{
    const auto sample = RideGetRecentRatingAccumulator(leg);
    return {
        .distanceMetres = ToHumanReadableRideLength(static_cast<int32_t>(
            std::clamp<int64_t>(sample.sampledDistance, 0, std::numeric_limits<int32_t>::max()))),
        .durationTicks = sample.ticks,
        .maxSpeed = sample.maxSpeed,
        .averageSpeed = sample.ticks == 0 ? 0 : static_cast<int32_t>(sample.totalSpeed / sample.ticks),
        .maxPositiveVerticalG = sample.maxPositiveVerticalG,
        .maxNegativeVerticalG = sample.maxNegativeVerticalG,
        .maxLateralG = sample.maxLateralG,
        .maxPositiveLongitudinalG = sample.maxPositiveLongitudinalG,
        .maxNegativeLongitudinalG = sample.maxNegativeLongitudinalG,
    };
}

RideRating::Tuple RideGetRatingsForStation(const Ride& ride, StationIndex originStation)
{
    RideRating::Tuple result{};
    bool hasRatings = false;
    if (ride.numStations > 1 && !originStation.IsNull())
    {
        for (const auto& leg : ride.ratingLegs)
        {
            if (leg.originStation != originStation || !leg.hasSamples())
            {
                continue;
            }
            if (!hasRatings)
            {
                result = leg.ratings;
                hasRatings = true;
            }
            else
            {
                result.excitement = std::min(result.excitement, leg.ratings.excitement);
                result.intensity = std::max(result.intensity, leg.ratings.intensity);
                result.nausea = std::max(result.nausea, leg.ratings.nausea);
            }
        }
    }
    return hasRatings ? result : ride.ratings;
}

void RideAddRecentRatingSample(Ride& ride, const RideRatingAccumulator& sample)
{
    if (!sample.hasSamples())
    {
        return;
    }

    auto storedSample = sample;
    storedSample.sampleEntity = EntityId::GetNull();
    storedSample.sampleComplete = true;

    ride.recentRatingSamples[ride.recentRatingSampleNext] = storedSample;
    ride.recentRatingSampleNext = static_cast<uint8_t>((ride.recentRatingSampleNext + 1) % ride.recentRatingSamples.size());
    ride.recentRatingSampleCount = static_cast<uint8_t>(
        std::min<size_t>(ride.recentRatingSampleCount + 1, ride.recentRatingSamples.size()));

    if (ride.numStations > 1 && !sample.originStation.IsNull() && !sample.destinationStation.IsNull()
        && sample.originStation.ToUnderlying() < ride.numStations
        && sample.destinationStation.ToUnderlying() < ride.numStations && sample.originStation != sample.destinationStation)
    {
        const auto key = std::pair{ sample.originStation.ToUnderlying(), sample.destinationStation.ToUnderlying() };
        auto it = std::lower_bound(
            ride.ratingLegs.begin(), ride.ratingLegs.end(), key,
            [](const RideRatingLeg& leg, const auto& candidate) {
                return std::pair{ leg.originStation.ToUnderlying(), leg.destinationStation.ToUnderlying() } < candidate;
            });
        if (it == ride.ratingLegs.end() || it->originStation != sample.originStation
            || it->destinationStation != sample.destinationStation)
        {
            it = ride.ratingLegs.insert(
                it, RideRatingLeg{ .originStation = sample.originStation, .destinationStation = sample.destinationStation });
        }

        it->recentSamples[it->recentSampleNext] = storedSample;
        it->recentSampleNext = static_cast<uint8_t>((it->recentSampleNext + 1) % it->recentSamples.size());
        it->recentSampleCount = static_cast<uint8_t>(
            std::min<size_t>(it->recentSampleCount + 1, it->recentSamples.size()));
    }
    if (ride.type != kRideTypeNull && ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
    {
        RideInvalidateTransportServiceCache(ride.id);
    }
}

void RideClearRiderRatingSamples(Ride& ride)
{
    ride.activeRatingSamples.clear();
    for (auto& sample : ride.recentRatingSamples)
    {
        sample.clear();
    }
    ride.recentRatingSampleCount = 0;
    ride.recentRatingSampleNext = 0;
    ride.ratingLegs.clear();
    if (ride.type != kRideTypeNull && ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
    {
        RideInvalidateTransportServiceCache(ride.id);
    }
}

size_t Ride::getNumPrices() const
{
    size_t result = 0;
    const auto& rtd = getRideTypeDescriptor();
    if (rtd.specialType == RtdSpecialType::cashMachine || rtd.specialType == RtdSpecialType::firstAid)
    {
        result = 0;
    }
    else if (rtd.specialType == RtdSpecialType::toilet)
    {
        result = 1;
    }
    else
    {
        result = 1;

        auto rideEntry = getRideEntry();
        if (rideEntry != nullptr)
        {
            if (flags.has(RideFlag::onRidePhoto))
            {
                result++;
            }
            else if (rideEntry->shop_item[1] != ShopItem::none)
            {
                result++;
            }
        }
    }
    return result;
}

int32_t Ride::getAge() const
{
    const auto age = GetCurrentBuildDateFraction() - DecodeBuildDateFraction(buildDate);
    return static_cast<int32_t>(std::max<int64_t>(0, age) / kBuildDateFractionsPerMonth);
}

int32_t Ride::getTotalQueueLength() const
{
    int32_t queueLength = 0;
    for (const auto& station : stations)
        if (!station.Entrance.IsNull())
            queueLength += station.QueueLength;
    return queueLength;
}

int32_t Ride::getMaxQueueTime() const
{
    uint8_t queueTime = 0;
    for (const auto& station : stations)
        if (!station.Entrance.IsNull())
            queueTime = std::max(queueTime, station.QueueTime);
    return static_cast<int32_t>(queueTime);
}

Guest* Ride::getQueueHeadGuest(StationIndex stationIndex) const
{
    Guest* peep;
    Guest* result = nullptr;
    auto spriteIndex = getStation(stationIndex).LastPeepInQueue;
    while ((peep = getGameState().entities.TryGetEntity<Guest>(spriteIndex)) != nullptr)
    {
        spriteIndex = peep->guestNextInQueue;
        result = peep;
    }
    return result;
}

void Ride::updateQueueLength(StationIndex stationIndex)
{
    uint16_t count = 0;
    Guest* peep;
    auto& station = getStation(stationIndex);
    auto spriteIndex = station.LastPeepInQueue;
    while ((peep = getGameState().entities.TryGetEntity<Guest>(spriteIndex)) != nullptr)
    {
        spriteIndex = peep->guestNextInQueue;
        count++;
    }
    station.QueueLength = count;
}

void Ride::queueInsertGuestAtFront(StationIndex stationIndex, Guest* peep)
{
    assert(stationIndex.ToUnderlying() < OpenRCT2::Limits::kMaxStationsPerRide);
    assert(peep != nullptr);

    peep->guestNextInQueue = EntityId::GetNull();
    auto* queueHeadGuest = getQueueHeadGuest(peep->CurrentRideStation);
    if (queueHeadGuest == nullptr)
    {
        getStation(peep->CurrentRideStation).LastPeepInQueue = peep->id;
    }
    else
    {
        queueHeadGuest->guestNextInQueue = peep->id;
    }
    updateQueueLength(peep->CurrentRideStation);
}

/**
 *
 *  rct2: 0x006AC916
 */
void RideUpdateFavouritedStat()
{
    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
        ride.guestsFavourite = 0;

    for (auto peep : EntityList<Guest>())
    {
        if (!peep->favouriteRide.IsNull())
        {
            auto ride = GetRide(peep->favouriteRide);
            if (ride != nullptr)
            {
                ride->guestsFavourite = AddClamp(ride->guestsFavourite, 1u);
                ride->windowInvalidateFlags.set(RideInvalidateFlag::customers);
            }
        }
    }

    auto* windowMgr = Ui::GetWindowManager();
    windowMgr->InvalidateByClass(WindowClass::rideList);
}

/**
 *
 *  rct2: 0x006AC3AB
 */
money64 Ride::calculateIncomePerHour() const
{
    // Get entry by ride to provide better reporting
    const auto* entry = getRideEntry();
    if (entry == nullptr)
    {
        return 0;
    }
    const auto customersPerHour = RideCustomersPerHour(*this);
    const auto primaryItemsPerHour = SumRecentCounts(numPrimaryItemsSoldHistory) * 12;
    const auto secondaryItemsPerHour = SumRecentCounts(numSecondaryItemsSoldHistory) * 12;

    money64 primaryProfit = RideGetPrice(*this);
    ShopItem primaryShopItem = entry->shop_item[0];
    if (primaryShopItem != ShopItem::none)
    {
        primaryProfit -= GetShopItemDescriptor(primaryShopItem).Cost;
    }

    ShopItem secondaryShopItem = flags.has(RideFlag::onRidePhoto) ? getRideTypeDescriptor().PhotoItem : entry->shop_item[1];
    if (secondaryShopItem == ShopItem::none)
    {
        if (primaryShopItem != ShopItem::none && primaryItemsPerHour != 0)
        {
            return primaryItemsPerHour * primaryProfit;
        }
        return customersPerHour * primaryProfit;
    }

    const auto& secondaryShopItemDescriptor = GetShopItemDescriptor(secondaryShopItem);
    const money64 secondaryProfit = price[1] - secondaryShopItemDescriptor.Cost;
    if (secondaryShopItemDescriptor.IsPhoto())
    {
        return (customersPerHour * primaryProfit) + (secondaryItemsPerHour * secondaryProfit);
    }

    if (primaryItemsPerHour != 0 || secondaryItemsPerHour != 0)
    {
        return (primaryItemsPerHour * primaryProfit) + (secondaryItemsPerHour * secondaryProfit);
    }

    int64_t primarySales = primaryShopItem == ShopItem::none ? totalCustomers : numPrimaryItemsSold;
    int64_t secondarySales = numSecondaryItemsSold;
    const auto totalSales = primarySales + secondarySales;
    if (totalSales == 0)
    {
        return customersPerHour * primaryProfit;
    }

    const auto weightedProfit = ((primaryProfit * primarySales) + (secondaryProfit * secondarySales)) / totalSales;
    return customersPerHour * weightedProfit;
}

/**
 *
 *  rct2: 0x006CAF80
 * ax result x
 * bx result y
 * dl ride index
 * esi result map element
 */
bool RideTryGetOriginElement(const Ride& ride, CoordsXYE* output)
{
    TileElement* resultTileElement = nullptr;

    TileElementIterator it;
    TileElementIteratorBegin(&it);
    do
    {
        if (it.element->getType() != TileElementType::Track)
            continue;
        if (it.element->asTrack()->GetRideIndex() != ride.id)
            continue;

        // Found a track piece for target ride

        // Check if it's not the station or ??? (but allow end piece of station)
        const auto& ted = GetTrackElementDescriptor(it.element->asTrack()->GetTrackType());
        bool specialTrackPiece
            = (it.element->asTrack()->GetTrackType() != TrackElemType::beginStation
               && it.element->asTrack()->GetTrackType() != TrackElemType::middleStation
               && ted.sequenceData.sequences[0].flags.has(SequenceFlag::trackOrigin));

        // Set result tile to this track piece if first found track or a ???
        if (resultTileElement == nullptr || specialTrackPiece)
        {
            resultTileElement = it.element;

            if (output != nullptr)
            {
                output->element = resultTileElement;
                output->x = it.x * kCoordsXYStep;
                output->y = it.y * kCoordsXYStep;
            }
        }

        if (specialTrackPiece)
        {
            return true;
        }
    } while (TileElementIteratorNext(&it));

    return resultTileElement != nullptr;
}

void Ride::formatStatusTo(Formatter& ft) const
{
    if (flags.has(RideFlag::crashed))
    {
        ft.Add<StringId>(STR_CRASHED);
    }
    else if (flags.has(RideFlag::brokenDown))
    {
        ft.Add<StringId>(STR_BROKEN_DOWN);
    }
    else if (status == RideStatus::closed)
    {
        if (!getRideTypeDescriptor().flags.has(RtdFlag::isShopOrFacility))
        {
            if (numRiders != 0)
            {
                ft.Add<StringId>(numRiders == 1 ? STR_CLOSED_WITH_PERSON : STR_CLOSED_WITH_PEOPLE);
                ft.Add<uint16_t>(numRiders);
            }
            else
            {
                ft.Add<StringId>(STR_CLOSED);
            }
        }
        else
        {
            ft.Add<StringId>(STR_CLOSED);
        }
    }
    else if (status == RideStatus::simulating)
    {
        ft.Add<StringId>(STR_SIMULATING);
    }
    else if (status == RideStatus::testing)
    {
        ft.Add<StringId>(STR_TEST_RUN);
    }
    else if (mode == RideMode::race && !flags.has(RideFlag::passStationNoStopping) && !raceWinner.IsNull())
    {
        auto peep = getGameState().entities.GetEntity<Guest>(raceWinner);
        if (peep != nullptr)
        {
            ft.Add<StringId>(STR_RACE_WON_BY);
            peep->FormatNameTo(ft);
        }
        else
        {
            ft.Add<StringId>(STR_RACE_WON_BY);
            ft.Add<StringId>(kStringIdNone);
        }
    }
    else if (!getRideTypeDescriptor().flags.has(RtdFlag::isShopOrFacility))
    {
        ft.Add<StringId>(numRiders == 1 ? STR_PERSON_ON_RIDE : STR_PEOPLE_ON_RIDE);
        ft.Add<uint16_t>(numRiders);
    }
    else
    {
        ft.Add<StringId>(STR_OPEN);
    }
}

int32_t Ride::getTotalLength() const
{
    int32_t totalLength = 0;
    for (int32_t i = 0; i < numStations; i++)
        totalLength += stations[i].SegmentLength;
    return totalLength;
}

int32_t Ride::getTotalTime() const
{
    int32_t totalTime = 0;
    for (int32_t i = 0; i < numStations; i++)
        totalTime += stations[i].SegmentTime;
    return totalTime;
}

bool Ride::hasStableStats() const
{
    return stableStats.valid;
}

int32_t Ride::getDisplayMaxSpeed() const
{
    return hasStableStats() ? stableStats.maxSpeed : maxSpeed;
}

int32_t Ride::getDisplayTotalLength() const
{
    if (!hasStableStats())
    {
        return getTotalLength();
    }

    int32_t totalLength = 0;
    for (int32_t i = 0; i < numStations; i++)
    {
        totalLength += stableStats.stations[i].SegmentLength;
    }
    return totalLength;
}

int32_t Ride::getDisplayTotalTime() const
{
    if (!hasStableStats())
    {
        return getTotalTime();
    }

    int32_t totalTime = 0;
    for (int32_t i = 0; i < numStations; i++)
    {
        totalTime += stableStats.stations[i].SegmentTime;
    }
    return totalTime;
}

int32_t Ride::getDisplayStationSegmentLength(StationIndex stationIndex) const
{
    const auto index = stationIndex.ToUnderlying();
    return hasStableStats() ? stableStats.stations[index].SegmentLength : stations[index].SegmentLength;
}

uint16_t Ride::getDisplayStationSegmentTime(StationIndex stationIndex) const
{
    const auto index = stationIndex.ToUnderlying();
    return hasStableStats() ? stableStats.stations[index].SegmentTime : stations[index].SegmentTime;
}

int32_t Ride::getDisplayAverageSpeed() const
{
    if (hasStableStats() || !flags.has(RideFlag::testInProgress))
    {
        return hasStableStats() ? stableStats.averageSpeed : averageSpeed;
    }

    const auto totalTime = getTotalTime();
    return totalTime <= 0 ? 0 : averageSpeed / totalTime;
}

fixed16_2dp Ride::getDisplayMaxPositiveVerticalG() const
{
    return hasStableStats() ? stableStats.maxPositiveVerticalG : maxPositiveVerticalG;
}

fixed16_2dp Ride::getDisplayMaxNegativeVerticalG() const
{
    return hasStableStats() ? stableStats.maxNegativeVerticalG : maxNegativeVerticalG;
}

fixed16_2dp Ride::getDisplayMaxLateralG() const
{
    return hasStableStats() ? stableStats.maxLateralG : maxLateralG;
}

fixed16_2dp Ride::getDisplayMaxPositiveLongitudinalG() const
{
    return hasStableStats() ? stableStats.maxPositiveLongitudinalG : maxPositiveLongitudinalG;
}

fixed16_2dp Ride::getDisplayMaxNegativeLongitudinalG() const
{
    return hasStableStats() ? stableStats.maxNegativeLongitudinalG : maxNegativeLongitudinalG;
}

uint16_t Ride::getDisplayTotalAirTime() const
{
    return hasStableStats() ? stableStats.totalAirTime : totalAirTime;
}

uint8_t Ride::getDisplayNumDrops() const
{
    return hasStableStats() ? stableStats.numDrops : numDrops;
}

uint8_t Ride::getDisplayNumPoweredLifts() const
{
    return hasStableStats() ? stableStats.numPoweredLifts : numPoweredLifts;
}

uint8_t Ride::getDisplayNumInversions() const
{
    return hasStableStats() ? stableStats.numInversions : numInversions;
}

uint8_t Ride::getDisplayNumHoles() const
{
    return hasStableStats() ? stableStats.numHoles : numHoles;
}

uint8_t Ride::getDisplayHighestDropHeight() const
{
    return hasStableStats() ? stableStats.highestDropHeight : highestDropHeight;
}

void Ride::publishCurrentStatsAsStable()
{
    stableStats.valid = true;
    stableStats.maxSpeed = maxSpeed;
    stableStats.averageSpeed = averageSpeed;
    stableStats.maxPositiveVerticalG = maxPositiveVerticalG;
    stableStats.maxNegativeVerticalG = maxNegativeVerticalG;
    stableStats.maxLateralG = maxLateralG;
    stableStats.maxPositiveLongitudinalG = maxPositiveLongitudinalG;
    stableStats.maxNegativeLongitudinalG = maxNegativeLongitudinalG;
    stableStats.numDrops = numDrops;
    stableStats.numPoweredLifts = numPoweredLifts;
    stableStats.numInversions = numInversions;
    stableStats.numHoles = numHoles;
    stableStats.highestDropHeight = highestDropHeight;
    stableStats.totalAirTime = totalAirTime;

    for (size_t i = 0; i < stableStats.stations.size(); i++)
    {
        stableStats.stations[i].SegmentLength = stations[i].SegmentLength;
        stableStats.stations[i].SegmentTime = stations[i].SegmentTime;
    }
    if (type != kRideTypeNull && getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
    {
        RideInvalidateTransportServiceCache(id);
    }
}

bool Ride::canHaveMultipleCircuits() const
{
    if (!getRideTypeDescriptor().flags.has(RtdFlag::allowMultipleCircuits))
        return false;

    // Only allow circuit or launch modes
    if (mode != RideMode::continuousCircuit && mode != RideMode::reverseInclineLaunchedShuttle
        && mode != RideMode::poweredLaunchPasstrough)
    {
        return false;
    }

    // Must have no more than one vehicle and one station
    if (numTrains > 1 || numStations > 1)
        return false;

    return true;
}

bool Ride::supportsStatus(RideStatus s) const
{
    const auto& rtd = getRideTypeDescriptor();

    switch (s)
    {
        case RideStatus::closed:
        case RideStatus::open:
            return true;
        case RideStatus::simulating:
            return (!rtd.flags.has(RtdFlag::noTestMode) && rtd.flags.has(RtdFlag::hasTrack));
        case RideStatus::testing:
            return !rtd.flags.has(RtdFlag::noTestMode);
        case RideStatus::count: // Meaningless but necessary to satisfy -Wswitch
            return false;
    }
    // Unreachable
    return false;
}

bool Ride::hasFailingBrakes() const
{
    return flags.has(RideFlag::brokenDown) && breakdownReasonPending == Breakdown::brakesFailure
        && mechanicStatus != MechanicStatus::hasFixedStationBrakes;
}

#pragma region Initialisation functions

/**
 *
 *  rct2: 0x006ACA89
 */
void RideInitAll()
{
    auto& gameState = getGameState();
    RideTransportServiceCacheReset();
    RideClearAllStationPlatformPreQueues();
    std::for_each(std::begin(gameState.rides), std::end(gameState.rides), RideReset);
    gameState.ridesEndOfUsedRange = 0;
}

/**
 *
 *  rct2: 0x006B7A38
 */
void ResetAllRideBuildDates()
{
    auto& gameState = getGameState();
    const auto currentBuildDateFraction = GetCurrentBuildDateFraction();
    for (auto& ride : RideManager(gameState))
    {
        const auto age = std::max<int64_t>(0, currentBuildDateFraction - DecodeBuildDateFraction(ride.buildDate));
        ride.buildDate = -static_cast<int32_t>(age / kBuildDateFractionsPerMonth);
    }
}

#pragma endregion

#pragma region Construction

#pragma endregion

#pragma region Update functions

/**
 *
 *  rct2: 0x006ABE4C
 */
void Ride::updateAll()
{
    PROFILED_FUNCTION();

    auto& gameState = getGameState();

    // Remove all rides if scenario editor
    if (gLegacyScene == LegacyScene::scenarioEditor)
    {
        switch (getGameState().editorStep)
        {
            case Editor::Step::objectSelection:
            case Editor::Step::landscapeEditor:
            case Editor::Step::inventionsListSetUp:
            {
                for (auto& ride : RideManager(gameState))
                    ride.remove();
                break;
            }
            case Editor::Step::optionsSelection:
            case Editor::Step::objectiveSelection:
            case Editor::Step::scenarioDetails:
            case Editor::Step::saveScenario:
            case Editor::Step::rollerCoasterDesigner:
            case Editor::Step::designsManager:
            case Editor::Step::invalid:
                break;
        }
        return;
    }

    WindowUpdateViewportRideMusic();

    // Update rides
    const auto currentTicks = gameState.currentTicks;
    const bool wholeSecondTick = GameTime::IsWholeSecondTick(currentTicks);
    const bool breakdownTick = (currentTicks & 255) == 0;
    const bool inspectionTick = (currentTicks % GameTime::kTicksPerMinute) == 0;
    for (auto& ride : RideManager(gameState))
        ride.update(currentTicks, wholeSecondTick, breakdownTick, inspectionTick);

    RideAudio::UpdateMusicChannels();
}

std::unique_ptr<TrackDesign> Ride::saveToTrackDesign(TrackDesignState& tds) const
{
    if (!flags.has(RideFlag::tested))
    {
        ContextShowError(STR_CANT_SAVE_TRACK_DESIGN, kStringIdNone, {});
        return nullptr;
    }

    if (!RideHasRatings(*this))
    {
        ContextShowError(STR_CANT_SAVE_TRACK_DESIGN, kStringIdNone, {});
        return nullptr;
    }

    auto td = std::make_unique<TrackDesign>();
    auto errMessage = td->CreateTrackDesign(tds, *this);
    if (!errMessage.Successful)
    {
        ContextShowError(STR_CANT_SAVE_TRACK_DESIGN, errMessage.Message, {});
        return nullptr;
    }
    if (errMessage.HasMessage())
    {
        ContextShowError(errMessage.Message, kStringIdEmpty, {});
    }

    return td;
}

RideStation& Ride::getStation(StationIndex stationIndex)
{
    return stations[stationIndex.ToUnderlying()];
}

StationIndex::UnderlyingType Ride::getStationNumber(StationIndex in) const
{
    StationIndex::UnderlyingType nullStationsSeen{ 0 };
    for (size_t i = 0; i < in.ToUnderlying(); i++)
    {
        if (stations[i].Start.IsNull())
        {
            nullStationsSeen++;
        }
    }

    return in.ToUnderlying() - nullStationsSeen + 1;
}

const RideStation& Ride::getStation(StationIndex stationIndex) const
{
    return stations[stationIndex.ToUnderlying()];
}

std::span<RideStation> Ride::getStations()
{
    return stations;
}

std::span<const RideStation> Ride::getStations() const
{
    return stations;
}

StationIndex Ride::getStationIndex(const RideStation* station) const
{
    auto distance = std::distance(stations.data(), station);
    Guard::Assert(distance >= 0 && distance < int32_t(std::size(stations)));
    return StationIndex::FromUnderlying(distance);
}

/**
 *
 *  rct2: 0x006ABE73
 */
void Ride::update(uint32_t currentTicks, bool wholeSecondTick, bool breakdownTick, bool inspectionTick)
{
    if (vehicleChangeTimeout != 0)
        vehicleChangeTimeout--;

    const auto& rtd = getRideTypeDescriptor();
    RideMusicUpdate(*this, rtd);

    // Update stations
    if (rtd.specialType != RtdSpecialType::maze)
    {
        auto stationsRemaining = numStations;
        for (StationIndex::UnderlyingType i = 0; i < Limits::kMaxStationsPerRide && stationsRemaining != 0; i++)
        {
            if (stations[i].Start.IsNull())
            {
                continue;
            }
            RideUpdateStation(*this, StationIndex::FromUnderlying(i), currentTicks, wholeSecondTick);
            stationsRemaining--;
        }
    }

    // Update financial statistics
    numCustomersTimeout++;

    if (numCustomersTimeout >= GameTime::SecondsToTicks(30))
    {
        // This updates every 30 real seconds.
        numCustomersTimeout = 0;

        // Shift number of customers history, start of the array is the most recent one
        for (int32_t i = Limits::kCustomerHistorySize - 1; i > 0; i--)
        {
            numCustomers[i] = numCustomers[i - 1];
            numPrimaryItemsSoldHistory[i] = numPrimaryItemsSoldHistory[i - 1];
            numSecondaryItemsSoldHistory[i] = numSecondaryItemsSoldHistory[i - 1];
        }
        numCustomers[0] = curNumCustomers;
        numPrimaryItemsSoldHistory[0] = curNumPrimaryItemsSold;
        numSecondaryItemsSoldHistory[0] = curNumSecondaryItemsSold;

        curNumCustomers = 0;
        curNumPrimaryItemsSold = 0;
        curNumSecondaryItemsSold = 0;
        windowInvalidateFlags.set(RideInvalidateFlag::customers);

        incomePerHour = calculateIncomePerHour();
        windowInvalidateFlags.set(RideInvalidateFlag::income);

        if (upkeepCost != kMoney64Undefined)
            profit = incomePerHour - RideGetUpkeepCostPerHour(*this);
    }

    // Ride specific updates
    if (rtd.RideUpdate != nullptr)
        rtd.RideUpdate(*this);

    if (breakdownTick)
    {
        RideBreakdownUpdate(*this, currentTicks);
    }

    // Various things include news messages
    if (flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown, RideFlag::dueInspection))
    {
        // Breakdown updates originally were performed when (id == (gCurrentTicks / 2) & 0xFF)
        // with the increased MAX_RIDES the update is tied to the first byte of the id this allows
        // for identical balance with vanilla.
        const auto updatingRideByte = static_cast<uint8_t>((currentTicks / 2) & 0xFF);
        if (updatingRideByte == static_cast<uint8_t>(id.ToUnderlying()))
            RideBreakdownStatusUpdate(*this);
    }

    if (inspectionTick)
    {
        RideInspectionUpdate(*this);
    }

    // If ride is simulating but crashed, reset the vehicles
    if (status == RideStatus::simulating && flags.has(RideFlag::crashed))
    {
        // We require this to execute right away during the simulation, always ignore network and queue.
        auto gameAction = GameActions::RideSetStatusAction(id, RideStatus::simulating);
        GameActions::ExecuteNested(&gameAction, getGameState());
    }
}

/**
 *
 *  rct2: 0x006AC489
 */
void updateChairlift(Ride& ride)
{
    if (!ride.flags.has(RideFlag::onTrack))
        return;
    if (ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown, RideFlag::crashed)
        && ride.breakdownReasonPending == Breakdown::safetyCutOut)
        return;

    uint16_t oldChairliftBullwheelRotation = ride.chairliftBullwheelRotation >> 14;
    ride.chairliftBullwheelRotation += ride.speed * 2048;
    if (oldChairliftBullwheelRotation == ride.speed / 8)
        return;

    auto bullwheelLoc = ride.chairliftBullwheelLocation[0].ToCoordsXYZ();
    MapInvalidateTileZoom1({ bullwheelLoc, bullwheelLoc.z, bullwheelLoc.z + (4 * kCoordsZStep) });

    bullwheelLoc = ride.chairliftBullwheelLocation[1].ToCoordsXYZ();
    MapInvalidateTileZoom1({ bullwheelLoc, bullwheelLoc.z, bullwheelLoc.z + (4 * kCoordsZStep) });
}

/**
 *
 *  rct2: 0x0069A3A2
 * edi: ride (in code as bytes offset from start of rides list)
 * bl: happiness
 */
void Ride::updateSatisfaction(const uint8_t happiness)
{
    satisfactionNext += happiness;
    satisfactionTimeout++;
    if (satisfactionTimeout >= 20)
    {
        satisfaction = satisfactionNext >> 2;
        satisfactionNext = 0;
        satisfactionTimeout = 0;
        windowInvalidateFlags.set(RideInvalidateFlag::customers);
    }
}

/**
 *
 *  rct2: 0x0069A3D7
 * Updates the ride popularity
 * edi : ride
 * bl  : pop_amount
 * pop_amount can be zero if peep visited but did not purchase.
 */
void Ride::updatePopularity(const uint8_t pop_amount)
{
    popularityNext += pop_amount;
    popularityTimeout++;
    if (popularityTimeout < 25)
        return;

    popularity = popularityNext;
    popularityNext = 0;
    popularityTimeout = 0;
    windowInvalidateFlags.set(RideInvalidateFlag::customers);
}

/** rct2: 0x0098DDB8, 0x0098DDBA */
static constexpr CoordsXY ride_spiral_slide_main_tile_offset[][4] = {
    {
        { 32, 32 },
        { 0, 32 },
        { 0, 0 },
        { 32, 0 },
    },
    {
        { 32, 0 },
        { 0, 0 },
        { 0, -32 },
        { 32, -32 },
    },
    {
        { 0, 0 },
        { -32, 0 },
        { -32, -32 },
        { 0, -32 },
    },
    {
        { 0, 0 },
        { 0, 32 },
        { -32, 32 },
        { -32, 0 },
    },
};

/**
 *
 *  rct2: 0x006AC545
 */

void updateSpiralSlide(Ride& ride)
{
    if (getGameState().currentTicks & 3)
        return;
    if (!ride.slideInUse)
        return;

    ride.spiralSlideProgress++;
    if (ride.spiralSlideProgress >= 48)
    {
        ride.slideInUse = 0;

        auto* peep = getGameState().entities.GetEntity<Guest>(ride.slidePeep);
        if (peep != nullptr)
        {
            peep->spiralSlideSubstate = PeepSpiralSlideSubState::finishedSliding;
        }
    }

    const uint8_t current_rotation = GetCurrentRotation();
    // Invalidate something related to station start
    for (int32_t i = 0; i < Limits::kMaxStationsPerRide; i++)
    {
        if (ride.stations[i].Start.IsNull())
            continue;

        auto startLoc = ride.stations[i].Start;

        TileElement* tileElement = RideGetStationStartTrackElement(ride, StationIndex::FromUnderlying(i));
        if (tileElement == nullptr)
            continue;

        int32_t rotation = tileElement->getDirection();
        startLoc += ride_spiral_slide_main_tile_offset[rotation][current_rotation];

        MapInvalidateTileZoom0({ startLoc, tileElement->getBaseZ(), tileElement->getClearanceZ() });
    }
}

#pragma endregion

#pragma region Breakdown and inspection functions

static uint8_t _breakdownProblemProbabilities[] = {
    25, // Breakdown::safetyCutOut
    12, // Breakdown::restraintsStuckClosed
    10, // Breakdown::restraintsStuckOpen
    13, // Breakdown::doorsStuckClosed
    10, // Breakdown::doorsStuckOpen
    6,  // Breakdown::vehicleMalfunction
    0,  // Breakdown::brakesFailure
    3,  // Breakdown::controlFailure
};

/**
 *
 *  rct2: 0x006AC7C2
 */
static void RideInspectionUpdate(Ride& ride)
{
    if (gLegacyScene == LegacyScene::trackDesigner)
        return;

    ride.lastInspection = AddClamp<decltype(ride.lastInspection)>(ride.lastInspection, 1);
    ride.windowInvalidateFlags.set(RideInvalidateFlag::maintenance);

    int32_t inspectionIntervalMinutes = RideInspectionInterval[EnumValue(ride.inspectionInterval)];
    // An inspection interval of 0 minutes means the ride is set to never be inspected.
    if (inspectionIntervalMinutes == 0)
    {
        ride.flags.unset(RideFlag::dueInspection);
        return;
    }

    if (ride.getRideTypeDescriptor().availableBreakdowns.isEmpty())
        return;

    if (inspectionIntervalMinutes > ride.lastInspection)
        return;

    if (ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown, RideFlag::dueInspection, RideFlag::crashed))
        return;

    // Inspect the first station that has an exit
    ride.flags.set(RideFlag::dueInspection);
    ride.mechanicStatus = MechanicStatus::calling;

    auto stationIndex = RideGetFirstValidStationExit(ride);
    ride.inspectionStation = (!stationIndex.IsNull()) ? stationIndex : StationIndex::FromUnderlying(0);
}

static int32_t getAgePenalty(const Ride& ride)
{
    auto years = DateGetYear(ride.getAge());
    switch (years)
    {
        case 0:
            return 0;
        case 1:
            return ride.unreliabilityFactor / 8;
        case 2:
            return ride.unreliabilityFactor / 4;
        case 3:
        case 4:
            return ride.unreliabilityFactor / 2;
        case 5:
        case 6:
        case 7:
            return ride.unreliabilityFactor;
        default:
            return ride.unreliabilityFactor * 2;
    }
}

/**
 *
 *  rct2: 0x006AC622
 */
static void RideBreakdownUpdate(Ride& ride, uint32_t currentTicks)
{
    auto& gameState = getGameState();
    if (gLegacyScene == LegacyScene::trackDesigner)
        return;

    if (ride.flags.hasAny(RideFlag::brokenDown, RideFlag::crashed))
        ride.downtimeHistory[0]++;

    if (!(currentTicks & 8191))
    {
        int32_t totalDowntime = 0;

        for (int32_t i = 0; i < Limits::kDowntimeHistorySize; i++)
        {
            totalDowntime += ride.downtimeHistory[i];
        }

        ride.downtime = std::min(totalDowntime / 2, 100);

        for (int32_t i = Limits::kDowntimeHistorySize - 1; i > 0; i--)
        {
            ride.downtimeHistory[i] = ride.downtimeHistory[i - 1];
        }
        ride.downtimeHistory[0] = 0;

        ride.windowInvalidateFlags.set(RideInvalidateFlag::maintenance);
    }

    if (ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown, RideFlag::crashed))
        return;
    if (ride.status == RideStatus::closed || ride.status == RideStatus::simulating)
        return;

    if (!ride.canBreakDown())
    {
        ride.reliability = kRideInitialReliability;
        return;
    }

    // Calculate breakdown probability?
    int32_t unreliabilityAccumulator = ride.unreliabilityFactor + getAgePenalty(ride);
    ride.reliability = static_cast<uint16_t>(std::max(0, (ride.reliability - unreliabilityAccumulator)));
    ride.windowInvalidateFlags.set(RideInvalidateFlag::maintenance);

    // Random probability of a breakdown. Roughly this is 1 in
    //
    // (25000 - reliability) / 3 000 000
    //
    // a 0.8% chance, less the breakdown factor which accumulates as the game
    // continues.
    if ((ride.reliability == 0
         || static_cast<uint32_t>(ScenarioRand() & 0x2FFFFF) <= 1u + kRideInitialReliability - ride.reliability)
        && !gameState.cheats.disableAllBreakdowns)
    {
        auto breakdownReason = RideGetNewBreakdownProblem(ride);
        if (breakdownReason != Breakdown::none)
            RidePrepareBreakdown(ride, breakdownReason);
    }
}

/**
 *
 *  rct2: 0x006B7294
 */
static Breakdown RideGetNewBreakdownProblem(const Ride& ride)
{
    // Brake failure is more likely when it's raining or heavily snowing (HeavySnow and Blizzard)
    _breakdownProblemProbabilities[EnumValue(Breakdown::brakesFailure)] = Weather::isPrecipitating() ? 20 : 3;

    if (!ride.canBreakDown())
        return Breakdown::none;

    auto availableBreakdownProblems = ride.getRideTypeDescriptor().availableBreakdowns;

    // Calculate the total probability range for all possible breakdown problems
    int32_t totalProbability = 0;
    for (Breakdown breakdown : kAllBreakdownTypes)
    {
        if (availableBreakdownProblems.has(breakdown))
            totalProbability += _breakdownProblemProbabilities[EnumValue(breakdown)];
    }

    if (totalProbability == 0)
        return Breakdown::none;

    // Choose a random number within this range
    int32_t randomProbability = ScenarioRand() % totalProbability;

    // Find which problem range the random number lies
    auto breakdownProblem = Breakdown::none;
    for (Breakdown breakdown : kAllBreakdownTypes)
    {
        if (availableBreakdownProblems.has(breakdown))
        {
            breakdownProblem = breakdown;
            randomProbability -= _breakdownProblemProbabilities[EnumValue(breakdown)];
        }
        if (randomProbability < 0)
            break;
    }

    if (breakdownProblem != Breakdown::brakesFailure)
        return breakdownProblem;

    // Brakes failure can not happen if block brakes are used (so long as there is more than one vehicle)
    // However if this is the case, brake failure should be taken out the equation, otherwise block brake
    // rides have a lower probability to break down due to a random implementation reason.
    if (ride.isBlockSectioned())
        if (ride.numTrains != 1)
            return Breakdown::none;

    // If brakes failure is disabled, also take it out of the equation (see above comment why)
    if (getGameState().cheats.disableBrakesFailure)
        return Breakdown::none;

    auto monthsOld = ride.getAge();
    if (monthsOld < 16 || ride.reliabilityPercentage > 50)
        return Breakdown::none;

    return Breakdown::brakesFailure;
}

bool Ride::canBreakDown() const
{
    if (getRideTypeDescriptor().availableBreakdowns.isEmpty())
    {
        return false;
    }

    const auto* entry = getRideEntry();
    return entry != nullptr && !entry->flags.has(RideEntryFlag::cannotBreakDown);
}

static void ChooseRandomTrainToBreakdownSafe(Ride& ride)
{
    // Prevent integer division by zero in case of hacked ride.
    if (ride.numTrains == 0)
        return;

    ride.brokenTrain = ScenarioRand() % ride.numTrains;

    // Prevent crash caused by accessing SPRITE_INDEX_NULL on hacked rides.
    // This should probably be cleaned up on import instead.
    while (ride.vehicles[ride.brokenTrain].IsNull() && ride.brokenTrain != 0)
    {
        --ride.brokenTrain;
    }
}

/**
 *
 *  rct2: 0x006B7348
 */
void RidePrepareBreakdown(Ride& ride, Breakdown breakdownReason)
{
    StationIndex i;
    Vehicle* vehicle;

    if (ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown, RideFlag::crashed))
        return;

    ride.flags.set(RideFlag::breakdownPending);

    ride.breakdownReasonPending = breakdownReason;
    ride.breakdownSoundModifier = 0;
    ride.notFixedTimeout = 0;
    ride.inspectionStation = StationIndex::FromUnderlying(0); // ensure set to something.

    switch (breakdownReason)
    {
        case Breakdown::safetyCutOut:
        case Breakdown::controlFailure:
            // Inspect first station with an exit
            i = RideGetFirstValidStationExit(ride);
            if (!i.IsNull())
            {
                ride.inspectionStation = i;
            }

            break;
        case Breakdown::restraintsStuckClosed:
        case Breakdown::restraintsStuckOpen:
        case Breakdown::doorsStuckClosed:
        case Breakdown::doorsStuckOpen:
            // Choose a random train and car
            ChooseRandomTrainToBreakdownSafe(ride);
            if (ride.numCarsPerTrain != 0)
            {
                ride.brokenCar = ScenarioRand() % ride.numCarsPerTrain;

                // Set flag on broken car
                vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[ride.brokenTrain]);
                if (vehicle != nullptr)
                {
                    vehicle = vehicle->GetCar(ride.brokenCar);
                }
                if (vehicle != nullptr)
                {
                    vehicle->flags.set(VehicleFlag::carIsBroken);
                }
            }
            break;
        case Breakdown::vehicleMalfunction:
            // Choose a random train
            ChooseRandomTrainToBreakdownSafe(ride);
            ride.brokenCar = 0;

            // Set flag on broken train, first car
            vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[ride.brokenTrain]);
            if (vehicle != nullptr)
            {
                vehicle->flags.set(VehicleFlag::trainIsBroken);
            }
            break;
        case Breakdown::brakesFailure:
            // Original code generates a random number but does not use it
            // Unsure if this was supposed to choose a random station (or random station with an exit)
            i = RideGetFirstValidStationExit(ride);
            if (!i.IsNull())
            {
                ride.inspectionStation = i;
            }
            break;
        default:
            break;
    }
#ifdef ENABLE_SCRIPTING
    auto& hookEngine = GetContext()->GetScriptEngine().GetHookEngine();
    if (hookEngine.HasSubscriptions(HookType::rideBreakDown))
    {
        auto ctx = GetContext()->GetScriptEngine().GetContext();
        JSValue obj = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, obj, "rideId", JS_NewInt32(ctx, ride.id.ToUnderlying()));

        auto it = kBreakdownMap.find(breakdownReason);
        if (it != kBreakdownMap.end())
            JS_SetPropertyStr(ctx, obj, "breakdownReason", JS_NewString(ctx, it->first.data()));
        else
            JS_SetPropertyStr(ctx, obj, "breakdownReason", JS_NewString(ctx, "none"));

        hookEngine.Call(HookType::rideBreakDown, obj, true);
    }
#endif
}

/**
 *
 *  rct2: 0x006B74FA
 */
void RideBreakdownAddNewsItem(const Ride& ride)
{
    if (Config::Get().notifications.rideBrokenDown)
    {
        Formatter ft;
        ride.formatNameTo(ft);
        News::AddItemToQueue(News::ItemType::ride, STR_RIDE_IS_BROKEN_DOWN, ride.id.ToUnderlying(), ft);
    }
}

/**
 *
 *  rct2: 0x006B75C8
 */
static void RideBreakdownStatusUpdate(Ride& ride)
{
    // Warn player if ride hasn't been fixed for ages
    if (ride.flags.has(RideFlag::brokenDown))
    {
        ride.notFixedTimeout++;
        // When there has been a full 255 timeout ticks this
        // will force timeout ticks to keep issuing news every
        // 16 ticks. Note there is no reason to do this.
        if (ride.notFixedTimeout == 0)
            ride.notFixedTimeout -= 16;

        if (!(ride.notFixedTimeout & 15) && ride.mechanicStatus != MechanicStatus::fixing
            && ride.mechanicStatus != MechanicStatus::hasFixedStationBrakes)
        {
            if (Config::Get().notifications.rideWarnings)
            {
                Formatter ft;
                ride.formatNameTo(ft);
                News::AddItemToQueue(News::ItemType::ride, STR_RIDE_IS_STILL_NOT_FIXED, ride.id.ToUnderlying(), ft);
            }
        }
    }

    RideMechanicStatusUpdate(ride, ride.mechanicStatus);
}

/**
 *
 *  rct2: 0x006B762F
 */
static void RideMechanicStatusUpdate(Ride& ride, MechanicStatus mechanicStatus)
{
    // Turn a pending breakdown into a breakdown.
    if ((mechanicStatus == MechanicStatus::undefined || mechanicStatus == MechanicStatus::calling
         || mechanicStatus == MechanicStatus::heading)
        && ride.flags.has(RideFlag::breakdownPending) && !ride.flags.has(RideFlag::brokenDown))
    {
        auto breakdownReason = ride.breakdownReasonPending;
        if (breakdownReason == Breakdown::safetyCutOut || breakdownReason == Breakdown::brakesFailure
            || breakdownReason == Breakdown::controlFailure)
        {
            ride.flags.set(RideFlag::brokenDown);
            RideInvalidateTransportServiceCache(ride.id);
            ride.windowInvalidateFlags.set(RideInvalidateFlag::maintenance, RideInvalidateFlag::list, RideInvalidateFlag::main);
            ride.breakdownReason = breakdownReason;
            RideBreakdownAddNewsItem(ride);
        }
    }
    switch (mechanicStatus)
    {
        case MechanicStatus::undefined:
            if (ride.flags.has(RideFlag::brokenDown))
            {
                ride.mechanicStatus = MechanicStatus::calling;
            }
            break;
        case MechanicStatus::calling:
            if (ride.getRideTypeDescriptor().availableBreakdowns.isEmpty())
            {
                ride.flags.unset(RideFlag::breakdownPending, RideFlag::brokenDown, RideFlag::dueInspection);
                RideInvalidateTransportServiceCache(ride.id);
                break;
            }

            RideCallClosestMechanic(ride);
            break;
        case MechanicStatus::heading:
        {
            auto mechanic = RideGetMechanic(ride);
            bool rideNeedsRepair = ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown);
            if (mechanic == nullptr
                || (mechanic->State != PeepState::headingToInspection && mechanic->State != PeepState::answering)
                || mechanic->CurrentRide != ride.id)
            {
                ride.mechanicStatus = MechanicStatus::calling;
                ride.windowInvalidateFlags.set(RideInvalidateFlag::maintenance);
                RideMechanicStatusUpdate(ride, MechanicStatus::calling);
            }
            // if the ride is broken down, but a mechanic was heading for an inspection, update orders to fix
            else if (rideNeedsRepair && mechanic->State == PeepState::headingToInspection)
            {
                // updates orders for mechanic already heading to inspect ride
                // forInspection == false means start repair (goes to PeepState::answering)
                RideCallMechanic(ride, mechanic, false);
            }
            break;
        }
        case MechanicStatus::fixing:
        {
            auto mechanic = RideGetMechanic(ride);
            if (mechanic == nullptr
                || (mechanic->State != PeepState::headingToInspection && mechanic->State != PeepState::fixing
                    && mechanic->State != PeepState::inspecting && mechanic->State != PeepState::answering))
            {
                ride.mechanicStatus = MechanicStatus::calling;
                ride.windowInvalidateFlags.set(RideInvalidateFlag::maintenance);
                RideMechanicStatusUpdate(ride, MechanicStatus::calling);
            }
            break;
        }
        default:
            break;
    }
}

/**
 *
 *  rct2: 0x006B796C
 */
static void RideCallMechanic(Ride& ride, Peep* mechanic, int32_t forInspection)
{
    mechanic->SetState(forInspection ? PeepState::headingToInspection : PeepState::answering);
    mechanic->SubState = 0;
    ride.mechanicStatus = MechanicStatus::heading;
    ride.windowInvalidateFlags.set(RideInvalidateFlag::maintenance);
    ride.mechanic = mechanic->id;
    mechanic->CurrentRide = ride.id;
    mechanic->CurrentRideStation = ride.inspectionStation;
}

/**
 *
 *  rct2: 0x006B76AB
 */
static void RideCallClosestMechanic(Ride& ride)
{
    auto forInspection = !ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown);
    auto mechanic = RideFindClosestMechanic(ride, forInspection);
    if (mechanic != nullptr)
        RideCallMechanic(ride, mechanic, forInspection);
}

Staff* RideFindClosestMechanic(const Ride& ride, int32_t forInspection)
{
    const auto stationIndex = ride.inspectionStation.IsNull() ? RideGetFirstValidStationExit(ride) : ride.inspectionStation;
    if (stationIndex.IsNull())
        return nullptr;

    // Get either exit position or entrance position if there is no exit
    const auto& station = ride.getStation(stationIndex);
    TileCoordsXYZD location = station.Exit;
    if (location.IsNull())
    {
        location = station.Entrance;
        if (station.Entrance.IsNull())
            return nullptr;
    }

    // Get station start track element and position
    auto mapLocation = location.ToCoordsXYZ();
    TileElement* tileElement = RideGetStationExitElement(mapLocation);
    if (tileElement == nullptr)
        return nullptr;

    // Set x,y to centre of the station exit for the mechanic search.
    auto centreMapLocation = mapLocation.ToTileCentre();

    return FindClosestMechanic(centreMapLocation, forInspection);
}

/**
 *
 *  rct2: 0x006B774B (forInspection = 0)
 *  rct2: 0x006B78C3 (forInspection = 1)
 */
Staff* FindClosestMechanic(const CoordsXY& entrancePosition, int32_t forInspection)
{
    Staff* closestMechanic = nullptr;
    uint32_t closestDistance = std::numeric_limits<uint32_t>::max();

    for (auto peep : EntityList<Staff>())
    {
        if (!peep->isMechanic())
            continue;

        if (!forInspection)
        {
            if (peep->State == PeepState::headingToInspection)
            {
                if (peep->SubState >= 4)
                    continue;
            }
            else if (peep->State != PeepState::patrolling)
                continue;

            if (!(peep->staffOrders & STAFF_ORDERS_FIX_RIDES))
                continue;
        }
        else
        {
            if (peep->State != PeepState::patrolling || !(peep->staffOrders & STAFF_ORDERS_INSPECT_RIDES))
                continue;
        }

        auto location = entrancePosition.ToTileStart();
        if (MapIsLocationInPark(location))
            if (!peep->isLocationInPatrol(location))
                continue;

        if (peep->x == kLocationNull)
            continue;

        // Manhattan distance
        uint32_t distance = std::abs(peep->x - entrancePosition.x) + std::abs(peep->y - entrancePosition.y);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestMechanic = peep;
        }
    }

    return closestMechanic;
}

Staff* RideGetMechanic(const Ride& ride)
{
    auto staff = getGameState().entities.GetEntity<Staff>(ride.mechanic);
    if (staff != nullptr && staff->isMechanic())
    {
        return staff;
    }
    return nullptr;
}

Staff* RideGetAssignedMechanic(const Ride& ride)
{
    if (ride.flags.has(RideFlag::brokenDown))
    {
        if (ride.mechanicStatus == MechanicStatus::heading || ride.mechanicStatus == MechanicStatus::fixing
            || ride.mechanicStatus == MechanicStatus::hasFixedStationBrakes)
        {
            return RideGetMechanic(ride);
        }
    }

    return nullptr;
}

#pragma endregion

#pragma region Music functions

/**
 *
 *  Calculates the sample rate for ride music.
 */
static int32_t RideMusicSampleRate(const Ride& ride)
{
    int32_t sampleRate = 22050;

    // Alter sample rate for a power cut effect
    if (ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown))
    {
        sampleRate = ride.breakdownSoundModifier * 70;
        if (ride.breakdownReasonPending != Breakdown::controlFailure)
            sampleRate *= -1;
        sampleRate += 22050;
    }

    return sampleRate;
}

/**
 *
 *  Ride music slows down upon breaking. If it's completely broken, no music should play.
 */
static bool RideMusicBreakdownEffect(Ride& ride)
{
    // Oscillate parameters for a power cut effect when breaking down
    if (ride.flags.hasAny(RideFlag::breakdownPending, RideFlag::brokenDown))
    {
        if (ride.breakdownReasonPending == Breakdown::controlFailure)
        {
            if (!(getGameState().currentTicks & 7))
                if (ride.breakdownSoundModifier != 255)
                    ride.breakdownSoundModifier++;
        }
        else
        {
            if (ride.flags.has(RideFlag::brokenDown) || ride.breakdownReasonPending == Breakdown::brakesFailure
                || ride.breakdownReasonPending == Breakdown::controlFailure)
            {
                if (ride.breakdownSoundModifier != 255)
                    ride.breakdownSoundModifier++;
            }

            if (ride.breakdownSoundModifier == 255)
            {
                ride.musicTuneId = kTuneIDNull;
                return true;
            }
        }
    }
    return false;
}

/**
 *
 *  Circus music is a sound effect, rather than music. Needs separate processing.
 */
void CircusMusicUpdate(Ride& ride)
{
    Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[0]);
    if (vehicle == nullptr || vehicle->status != Vehicle::Status::doingCircusShow)
    {
        ride.musicPosition = 0;
        ride.musicTuneId = kTuneIDNull;
        return;
    }

    if (RideMusicBreakdownEffect(ride))
    {
        return;
    }

    CoordsXYZ rideCoords = ride.getStation().GetStart().ToTileCentre();

    const auto sampleRate = RideMusicSampleRate(ride);

    RideAudio::UpdateMusicInstance(ride, rideCoords, sampleRate);
}

/**
 *
 *  rct2: 0x006ABE85
 */
void DefaultMusicUpdate(Ride& ride)
{
    const auto finishCrashTrack = ride.flags.has(RideFlag::crashed) && ride.musicTuneId != kTuneIDNull;
    if ((ride.status != RideStatus::open && !finishCrashTrack) || !ride.flags.has(RideFlag::music))
    {
        ride.musicTuneId = kTuneIDNull;
        return;
    }

    if (RideMusicBreakdownEffect(ride))
    {
        return;
    }

    // Select random tune from available tunes for a music style (of course only merry-go-rounds have more than one tune)
    if (ride.musicTuneId == kTuneIDNull)
    {
        auto& objManager = GetContext()->GetObjectManager();
        auto musicObj = objManager.GetLoadedObject<MusicObject>(ride.music);
        if (musicObj != nullptr)
        {
            auto numTracks = musicObj->GetTrackCount();
            ride.musicTuneId = static_cast<uint8_t>(UtilRand() % numTracks);
            ride.musicPosition = 0;
        }
        return;
    }

    CoordsXYZ rideCoords = ride.getStation().GetStart().ToTileCentre();

    int32_t sampleRate = RideMusicSampleRate(ride);

    RideAudio::UpdateMusicInstance(ride, rideCoords, sampleRate);
}

static void RideMusicUpdate(Ride& ride, const RideTypeDescriptor& rtd)
{
    if (!rtd.flags.hasAny(RtdFlag::hasMusicByDefault, RtdFlag::allowMusic))
        return;
    rtd.MusicUpdateFunction(ride);
}

#pragma endregion

#pragma region Measurement functions

/**
 *
 *  rct2: 0x006B64F2
 */
static void RideMeasurementUpdate(Ride& ride, RideMeasurement& measurement)
{
    if (measurement.vehicle_index >= std::size(ride.vehicles))
        return;

    auto vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[measurement.vehicle_index]);
    if (vehicle == nullptr)
        return;

    if (measurement.flags.has(RideMeasurementFlag::unloading))
    {
        if (vehicle->status != Vehicle::Status::departing && vehicle->status != Vehicle::Status::travellingCableLift)
            return;

        measurement.flags.unset(RideMeasurementFlag::unloading);
        measurement.hasPreviousVelocity = false;
        if (measurement.current_station == vehicle->current_station)
            measurement.current_item = 0;
    }

    if (vehicle->status == Vehicle::Status::unloadingPassengers)
    {
        measurement.flags.set(RideMeasurementFlag::unloading);
        measurement.hasPreviousVelocity = false;
        return;
    }

    auto trackType = vehicle->GetTrackType();
    if (trackType == TrackElemType::blockBrakes || trackType == TrackElemType::cableLiftHill
        || trackType == TrackElemType::up25ToFlat || trackType == TrackElemType::up60ToFlat
        || trackType == TrackElemType::diagUp25ToFlat || trackType == TrackElemType::diagUp60ToFlat
        || trackType == TrackElemType::diagBlockBrakes)
        if (vehicle->velocity == 0)
            return;

    if (measurement.current_item >= RideMeasurement::kMaxItems)
        return;

    const auto currentTicks = getGameState().currentTicks;

    if (measurement.flags.has(RideMeasurementFlag::gForces))
    {
        auto gForces = vehicle->GetGForces();
        if (measurement.hasPreviousVelocity)
        {
            gForces.longitudinalG = CalculateLongitudinalG(measurement.previousVelocity, vehicle->velocity);
        }
        measurement.previousVelocity = vehicle->velocity;
        measurement.hasPreviousVelocity = true;
        gForces.verticalG = std::clamp(gForces.verticalG / 8, -127, 127);
        gForces.lateralG = std::clamp(gForces.lateralG / 8, -127, 127);
        gForces.longitudinalG = std::clamp(gForces.longitudinalG / 8, -127, 127);

        if (currentTicks & 1)
        {
            gForces.verticalG = (gForces.verticalG + measurement.vertical[measurement.current_item]) / 2;
            gForces.lateralG = (gForces.lateralG + measurement.lateral[measurement.current_item]) / 2;
            gForces.longitudinalG = (gForces.longitudinalG + measurement.longitudinal[measurement.current_item]) / 2;
        }

        measurement.vertical[measurement.current_item] = gForces.verticalG & 0xFF;
        measurement.lateral[measurement.current_item] = gForces.lateralG & 0xFF;
        measurement.longitudinal[measurement.current_item] = gForces.longitudinalG & 0xFF;
    }

    auto velocity = std::min(std::abs((vehicle->velocity * 5) >> 16), 255);
    auto altitude = std::min(vehicle->z / 8, 255);

    if (currentTicks & 1)
    {
        velocity = (velocity + measurement.velocity[measurement.current_item]) / 2;
        altitude = (altitude + measurement.altitude[measurement.current_item]) / 2;
    }

    measurement.velocity[measurement.current_item] = velocity & 0xFF;
    measurement.altitude[measurement.current_item] = altitude & 0xFF;

    if (currentTicks & 1)
    {
        measurement.current_item++;
        measurement.num_items = std::max(measurement.num_items, measurement.current_item);
    }
}

/**
 *
 *  rct2: 0x006B6456
 */
void RideMeasurementsUpdate()
{
    PROFILED_FUNCTION();

    if (gLegacyScene == LegacyScene::scenarioEditor)
        return;

    auto& gameState = getGameState();

    // For each ride measurement
    for (auto& ride : RideManager(gameState))
    {
        auto measurement = ride.measurement.get();
        if (measurement != nullptr && ride.flags.has(RideFlag::onTrack) && ride.status != RideStatus::simulating)
        {
            if (measurement->flags.has(RideMeasurementFlag::running))
            {
                RideMeasurementUpdate(ride, *measurement);
            }
            else
            {
                // For each vehicle
                for (int32_t j = 0; j < ride.numTrains; j++)
                {
                    auto vehicleSpriteIdx = ride.vehicles[j];
                    auto vehicle = gameState.entities.GetEntity<Vehicle>(vehicleSpriteIdx);
                    if (vehicle != nullptr)
                    {
                        if (vehicle->status == Vehicle::Status::departing
                            || vehicle->status == Vehicle::Status::travellingCableLift)
                        {
                            measurement->vehicle_index = j;
                            measurement->current_station = vehicle->current_station;
                            measurement->flags.set(RideMeasurementFlag::running);
                            measurement->flags.unset(RideMeasurementFlag::unloading);
                            measurement->hasPreviousVelocity = false;
                            RideMeasurementUpdate(ride, *measurement);
                            break;
                        }
                    }
                }
            }
        }
    }
}

/**
 * If there are more than the threshold of allowed ride measurements, free the non-LRU one.
 */
static void RideFreeOldMeasurements()
{
    size_t numRideMeasurements;
    do
    {
        Ride* lruRide{};
        numRideMeasurements = 0;

        auto& gameState = getGameState();
        for (auto& ride : RideManager(gameState))
        {
            if (ride.measurement != nullptr)
            {
                if (lruRide == nullptr || ride.measurement->last_use_tick > lruRide->measurement->last_use_tick)
                {
                    lruRide = &ride;
                }
                numRideMeasurements++;
            }
        }
        if (numRideMeasurements > kMaxRideMeasurements && lruRide != nullptr)
        {
            lruRide->measurement = {};
            numRideMeasurements--;
        }
    } while (numRideMeasurements > kMaxRideMeasurements);
}

std::pair<RideMeasurement*, StringWithArgs> Ride::getMeasurement()
{
    const auto& rtd = getRideTypeDescriptor();

    // Check if ride type supports data logging
    if (!rtd.flags.has(RtdFlag::hasDataLogging))
    {
        return { nullptr, { STR_DATA_LOGGING_NOT_AVAILABLE_FOR_THIS_TYPE_OF_RIDE, {} } };
    }

    // Check if a measurement already exists for this ride
    if (measurement == nullptr)
    {
        measurement = std::make_unique<RideMeasurement>();
        if (rtd.flags.has(RtdFlag::hasGForces))
        {
            measurement->flags.set(RideMeasurementFlag::gForces);
        }
        RideFreeOldMeasurements();
        assert(measurement != nullptr);
    }

    measurement->last_use_tick = getGameState().currentTicks;
    if (measurement->flags.has(RideMeasurementFlag::running))
    {
        return { measurement.get(), { kStringIdEmpty, {} } };
    }

    auto ft = Formatter();
    ft.Add<StringId>(GetRideComponentName(rtd.NameConvention.vehicle).singular);
    ft.Add<StringId>(GetRideComponentName(rtd.NameConvention.station).singular);
    return { nullptr, { STR_DATA_LOGGING_WILL_START_WHEN_NEXT_LEAVES, ft } };
}

#pragma endregion

#pragma region Colour functions

VehicleColour RideGetVehicleColour(const Ride& ride, int32_t vehicleIndex)
{
    // Prevent indexing array out of bounds
    vehicleIndex = std::clamp<int32_t>(vehicleIndex, 0, static_cast<int32_t>(std::size(ride.vehicleColours) - 1));
    return ride.vehicleColours[vehicleIndex];
}

static bool RideTypeVehicleColourExists(ObjectEntryIndex subType, const VehicleColour& vehicleColour)
{
    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        if (ride.subtype != subType)
            continue;
        if (ride.vehicleColours[0].Body != vehicleColour.Body)
            continue;
        return true;
    }
    return false;
}

int32_t RideGetUnusedPresetVehicleColour(ObjectEntryIndex subType, uint32_t randomValue)
{
    const auto* rideEntry = GetRideEntryByIndex(subType);
    if (rideEntry == nullptr)
        return 0;

    const auto* colourPresets = rideEntry->vehicle_preset_list;
    if (colourPresets == nullptr || colourPresets->count == 0)
        return 0;
    if (colourPresets->count == 255)
        return 255;

    // Find all the presets that haven't yet been used in the park for this ride type
    std::vector<uint8_t> unused;
    unused.reserve(colourPresets->count);
    for (uint8_t i = 0; i < colourPresets->count; i++)
    {
        const auto& preset = colourPresets->list[i];
        if (!RideTypeVehicleColourExists(subType, preset))
        {
            unused.push_back(i);
        }
    }

    // If all presets have been used, just go with a random preset
    if (unused.empty())
        return randomValue % colourPresets->count;

    // Choose a random preset from the list of unused presets
    auto unusedIndex = randomValue % unused.size();
    return unused[unusedIndex];
}

/**
 *
 *  rct2: 0x006DE52C
 */
void RideSetVehicleColoursToRandomPreset(Ride& ride, uint8_t preset_index)
{
    const auto* rideEntry = GetRideEntryByIndex(ride.subtype);
    const auto* presetList = rideEntry->vehicle_preset_list;

    if (presetList->count != 0 && presetList->count != 255)
    {
        assert(preset_index < presetList->count);

        ride.vehicleColourSettings = VehicleColourSettings::same;
        ride.vehicleColours[0] = presetList->list[preset_index];
    }
    else
    {
        ride.vehicleColourSettings = VehicleColourSettings::perTrain;
        for (uint32_t i = 0; i < presetList->count; i++)
        {
            const auto index = i % 32u;
            ride.vehicleColours[i] = presetList->list[index];
        }
    }
}

#pragma endregion

#pragma region Reachability

/**
 *
 *  rct2: 0x006B7A5E
 */
void RideCheckAllReachable()
{
    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        if (ride.connectedMessageThrottle != 0)
            ride.connectedMessageThrottle--;

        if (ride.status != RideStatus::open || ride.connectedMessageThrottle != 0)
            continue;

        if (ride.getRideTypeDescriptor().flags.has(RtdFlag::isShopOrFacility))
            RideShopConnected(ride);
        else
            RideEntranceExitConnected(ride);
    }
}

/**
 *
 *  rct2: 0x006B7C59
 * @return true if the coordinate is reachable or has no entrance, false otherwise
 */
static bool RideEntranceExitIsReachable(const TileCoordsXYZD& coordinates)
{
    if (coordinates.IsNull())
        return true;

    TileCoordsXYZ loc{ coordinates.x, coordinates.y, coordinates.z };
    loc -= TileDirectionDelta[coordinates.direction];

    return MapCoordIsConnected(loc, coordinates.direction);
}

static void RideEntranceExitConnected(Ride& ride)
{
    for (auto& station : ride.getStations())
    {
        auto station_start = station.Start;
        auto entrance = station.Entrance;
        auto exit = station.Exit;

        if (station_start.IsNull())
            continue;

        if (!entrance.IsNull() && !RideEntranceExitIsReachable(entrance))
        {
            // name of ride is parameter of the format string
            Formatter ft;
            ride.formatNameTo(ft);
            if (Config::Get().notifications.rideWarnings)
            {
                News::AddItemToQueue(News::ItemType::ride, STR_ENTRANCE_NOT_CONNECTED, ride.id.ToUnderlying(), ft);
            }
            ride.connectedMessageThrottle = 3;
        }

        if (!exit.IsNull() && !RideEntranceExitIsReachable(exit))
        {
            // name of ride is parameter of the format string
            Formatter ft;
            ride.formatNameTo(ft);
            if (Config::Get().notifications.rideWarnings)
            {
                News::AddItemToQueue(News::ItemType::ride, STR_EXIT_NOT_CONNECTED, ride.id.ToUnderlying(), ft);
            }
            ride.connectedMessageThrottle = 3;
        }
    }
}

static void RideShopConnected(const Ride& ride)
{
    auto shopLoc = TileCoordsXY(ride.getStation().Start);
    if (shopLoc.IsNull())
        return;

    TrackElement* trackElement = nullptr;
    TileElement* tileElement = MapGetFirstElementAt(shopLoc);
    do
    {
        if (tileElement == nullptr)
            break;
        if (tileElement->getType() == TileElementType::Track && tileElement->asTrack()->GetRideIndex() == ride.id)
        {
            trackElement = tileElement->asTrack();
            break;
        }
    } while (!(tileElement++)->isLastForTile());

    if (trackElement == nullptr)
        return;

    auto track_type = trackElement->GetTrackType();
    auto ride2 = GetRide(trackElement->GetRideIndex());
    if (ride2 == nullptr)
        return;

    const auto& ted = GetTrackElementDescriptor(track_type);
    uint8_t connectionSides = ted.sequenceData.sequences[0].getEntranceConnectionSides();
    uint8_t tile_direction = trackElement->getDirection();
    connectionSides = Numerics::rol4(connectionSides, tile_direction);

    // Now each bit in connectionSides stands for an entrance direction to check
    if (connectionSides == 0)
        return;

    for (auto count = 0; connectionSides != 0; count++)
    {
        if (!(connectionSides & 1))
        {
            connectionSides >>= 1;
            continue;
        }
        connectionSides >>= 1;

        // Flip direction north<->south, east<->west
        uint8_t face_direction = DirectionReverse(count);

        int32_t y2 = shopLoc.y - TileDirectionDelta[face_direction].y;
        int32_t x2 = shopLoc.x - TileDirectionDelta[face_direction].x;

        if (MapCoordIsConnected({ x2, y2, tileElement->baseHeight }, face_direction))
            return;
    }

    // Name of ride is parameter of the format string
    if (Config::Get().notifications.rideWarnings)
    {
        Formatter ft;
        ride2->formatNameTo(ft);
        News::AddItemToQueue(News::ItemType::ride, STR_ENTRANCE_NOT_CONNECTED, ride2->id.ToUnderlying(), ft);
    }

    ride2->connectedMessageThrottle = 3;
}

#pragma endregion

#pragma region Interface

static void RideTrackSetMapTooltip(const TrackElement& trackElement)
{
    auto rideIndex = trackElement.GetRideIndex();
    auto ride = GetRide(rideIndex);
    if (ride != nullptr)
    {
        auto ft = Formatter();
        ft.Add<StringId>(STR_RIDE_MAP_TIP);
        ride->formatNameTo(ft);
        ride->formatStatusTo(ft);
        auto intent = Intent(INTENT_ACTION_SET_MAP_TOOLTIP);
        intent.PutExtra(INTENT_EXTRA_FORMATTER, &ft);
        ContextBroadcastIntent(&intent);
    }
}

static void RideQueueBannerSetMapTooltip(const PathElement& pathElement)
{
    auto rideIndex = pathElement.GetRideIndex();
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return;

    auto ft = Formatter();
    ft.Add<StringId>(STR_RIDE_MAP_TIP);
    ride->formatNameTo(ft);
    ride->formatStatusTo(ft);
    auto intent = Intent(INTENT_ACTION_SET_MAP_TOOLTIP);
    intent.PutExtra(INTENT_EXTRA_FORMATTER, &ft);
    ContextBroadcastIntent(&intent);
}

static void RideStationSetMapTooltip(const TrackElement& trackElement)
{
    auto rideIndex = trackElement.GetRideIndex();
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return;

    const auto stationIndex = trackElement.GetStationIndex();
    const auto stationNumber = ride->getStationNumber(stationIndex);

    auto ft = Formatter();
    ft.Add<StringId>(STR_RIDE_MAP_TIP);
    ft.Add<StringId>(ride->numStations <= 1 ? STR_RIDE_STATION : STR_RIDE_STATION_X);
    ride->formatNameTo(ft);
    ft.Add<StringId>(GetRideComponentName(ride->getRideTypeDescriptor().NameConvention.station).capitalised);
    ft.Add<uint16_t>(stationNumber);
    ride->formatStatusTo(ft);
    auto intent = Intent(INTENT_ACTION_SET_MAP_TOOLTIP);
    intent.PutExtra(INTENT_EXTRA_FORMATTER, &ft);
    ContextBroadcastIntent(&intent);
}

static void RideEntranceSetMapTooltip(const EntranceElement& entranceElement)
{
    auto rideIndex = entranceElement.GetRideIndex();
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return;

    if (entranceElement.GetEntranceType() == ENTRANCE_TYPE_RIDE_ENTRANCE)
    {
        // Get the queue length
        int32_t queueLength = 0;
        const auto stationIndex = entranceElement.GetStationIndex();
        if (!ride->getStation(stationIndex).Entrance.IsNull())
        {
            queueLength = ride->getStation(stationIndex).QueueLength;
        }

        auto ft = Formatter();
        ft.Add<StringId>(STR_RIDE_MAP_TIP);
        ft.Add<StringId>(ride->numStations <= 1 ? STR_RIDE_ENTRANCE : STR_RIDE_STATION_X_ENTRANCE);
        ride->formatNameTo(ft);

        // String IDs have an extra pop16 for some reason
        ft.Increment(sizeof(uint16_t));

        const auto stationNumber = ride->getStationNumber(stationIndex);
        ft.Add<uint16_t>(stationNumber);

        switch (queueLength)
        {
            case 0:
                ft.Add<StringId>(STR_QUEUE_EMPTY);
                break;
            case 1:
                ft.Add<StringId>(STR_QUEUE_ONE_PERSON);
                break;
            default:
                ft.Add<StringId>(STR_QUEUE_PEOPLE);
                break;
        }
        ft.Add<uint16_t>(queueLength);

        auto intent = Intent(INTENT_ACTION_SET_MAP_TOOLTIP);
        intent.PutExtra(INTENT_EXTRA_FORMATTER, &ft);
        ContextBroadcastIntent(&intent);
    }
    else
    {
        auto ft = Formatter();
        ft.Add<StringId>(ride->numStations <= 1 ? STR_RIDE_EXIT : STR_RIDE_STATION_X_EXIT);
        ride->formatNameTo(ft);

        // String IDs have an extra pop16 for some reason
        ft.Increment(sizeof(uint16_t));

        const auto stationIndex = entranceElement.GetStationIndex();
        const auto stationNumber = ride->getStationNumber(stationIndex);
        ft.Add<uint16_t>(stationNumber);
        auto intent = Intent(INTENT_ACTION_SET_MAP_TOOLTIP);
        intent.PutExtra(INTENT_EXTRA_FORMATTER, &ft);
        ContextBroadcastIntent(&intent);
    }
}

void RideSetMapTooltip(const TileElement& tileElement)
{
    if (tileElement.getType() == TileElementType::Entrance)
    {
        RideEntranceSetMapTooltip(*tileElement.asEntrance());
    }
    else if (tileElement.getType() == TileElementType::Track)
    {
        const auto* trackElement = tileElement.asTrack();
        if (trackElement->IsStation())
        {
            RideStationSetMapTooltip(*trackElement);
        }
        else
        {
            RideTrackSetMapTooltip(*trackElement);
        }
    }
    else if (tileElement.getType() == TileElementType::Path)
    {
        RideQueueBannerSetMapTooltip(*tileElement.asPath());
    }
}

#pragma endregion

/**
 *
 *  rct2: 0x006B4CC1
 */
static ResultWithMessage RideModeCheckValidStationNumbers(const Ride& ride)
{
    uint16_t numStations = 0;
    for (const auto& station : ride.getStations())
    {
        if (!station.Start.IsNull())
        {
            numStations++;
        }
    }

    switch (ride.mode)
    {
        case RideMode::reverseInclineLaunchedShuttle:
        case RideMode::poweredLaunchPasstrough:
        case RideMode::poweredLaunch:
        case RideMode::limPoweredLaunch:
            if (numStations <= 1)
                return { true };
            return { false, STR_UNABLE_TO_OPERATE_WITH_MORE_THAN_ONE_STATION_IN_THIS_MODE };
        case RideMode::shuttle:
            if (numStations >= 2)
                return { true };
            return { false, STR_UNABLE_TO_OPERATE_WITH_LESS_THAN_TWO_STATIONS_IN_THIS_MODE };
        default:
        {
            // This is workaround for multiple compilation errors of type "enumeration value ‘RIDE_MODE_*' not handled
            // in switch [-Werror=switch]"
        }
    }

    const auto& rtd = ride.getRideTypeDescriptor();
    if (rtd.flags.has(RtdFlag::hasOneStation) && numStations > 1)
        return { false, STR_UNABLE_TO_OPERATE_WITH_MORE_THAN_ONE_STATION_IN_THIS_MODE };

    return { true };
}

/**
 * returns stationIndex of first station on success
 * STATION_INDEX_NULL on failure.
 */
static StationIndexWithMessage RideModeCheckStationPresent(const Ride& ride)
{
    auto stationIndex = RideGetFirstValidStationStart(ride);

    if (stationIndex.IsNull())
    {
        const auto& rtd = ride.getRideTypeDescriptor();
        if (!rtd.flags.has(RtdFlag::hasTrack))
            return { StationIndex::GetNull(), STR_NOT_YET_CONSTRUCTED };

        if (rtd.specialType == RtdSpecialType::maze)
            return { StationIndex::GetNull(), STR_NOT_YET_CONSTRUCTED };

        return { StationIndex::GetNull(), STR_REQUIRES_A_STATION_PLATFORM };
    }

    return { stationIndex };
}

/**
 *
 *  rct2: 0x006B5872
 */
static ResultWithMessage RideCheckForEntranceExit(RideId rideIndex)
{
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return { false };

    if (ride->getRideTypeDescriptor().flags.has(RtdFlag::isShopOrFacility))
        return { true };

    uint8_t entrance = 0;
    uint8_t exit = 0;
    for (const auto& station : ride->getStations())
    {
        if (station.Start.IsNull())
            continue;

        if (!station.Entrance.IsNull())
        {
            entrance = 1;
        }

        if (!station.Exit.IsNull())
        {
            exit = 1;
        }

        // If station start and no entrance/exit
        // Sets same error message as no entrance
        if (station.Exit.IsNull() && station.Entrance.IsNull())
        {
            entrance = 0;
            break;
        }
    }

    if (entrance == 0)
    {
        return { false, STR_ENTRANCE_NOT_YET_BUILT };
    }

    if (exit == 0)
    {
        return { false, STR_EXIT_NOT_YET_BUILT };
    }

    return { true };
}

/**
 * Calls FootpathChainRideQueue for all entrances of the ride
 *  rct2: 0x006B5952
 */
void Ride::chainQueues() const
{
    for (const auto& station : stations)
    {
        if (station.Entrance.IsNull())
            continue;

        auto mapLocation = station.Entrance.ToCoordsXYZ();

        // This will fire for every entrance on this x, y and z, regardless whether that actually belongs to
        // the ride or not.
        TileElement* tileElement = MapGetFirstElementAt(station.Entrance);
        if (tileElement != nullptr)
        {
            do
            {
                if (tileElement->getType() != TileElementType::Entrance)
                    continue;
                if (tileElement->getBaseZ() != mapLocation.z)
                    continue;

                int32_t direction = tileElement->getDirection();
                FootpathChainRideQueue(id, getStationIndex(&station), mapLocation, tileElement, DirectionReverse(direction));
            } while (!(tileElement++)->isLastForTile());
        }
    }
}

/**
 *
 *  rct2: 0x006D3319
 */
static ResultWithMessage RideCheckBlockBrakes(const CoordsXYE& input, CoordsXYE* output, bool shouldCheckCompleteCircuit)
{
    if (input.element == nullptr || input.element->getType() != TileElementType::Track)
        return { false };

    RideId rideIndex = input.element->asTrack()->GetRideIndex();

    auto* windowMgr = Ui::GetWindowManager();
    WindowBase* w = windowMgr->FindByClass(WindowClass::rideConstruction);
    if (w != nullptr && _rideConstructionState != RideConstructionState::State0 && _currentRideIndex == rideIndex)
        RideConstructionInvalidateCurrentTrack();

    TrackCircuitIterator it;
    trackCircuitIteratorBegin(&it, input);
    while (trackCircuitIteratorNext(&it))
    {
        if (trackTypeIsBlockBrakes(it.current.element->asTrack()->GetTrackType()))
        {
            auto type = it.last.element->asTrack()->GetTrackType();
            if (type == TrackElemType::endStation)
            {
                *output = it.current;
                return { false, STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_STATION };
            }
            if (trackTypeIsBlockBrakes(type))
            {
                *output = it.current;
                return { false, STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_EACH_OTHER };
            }
            if (it.last.element->asTrack()->HasChain() && type != TrackElemType::leftCurvedLiftHill
                && type != TrackElemType::rightCurvedLiftHill)
            {
                *output = it.current;
                return { false, STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_THE_TOP_OF_THIS_LIFT_HILL };
            }
        }
    }
    if (!it.looped && shouldCheckCompleteCircuit)
    {
        // Not sure why this is the case...
        *output = it.last;
        return { false, STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_STATION };
    }

    return { true };
}

/**
 * Iterates along the track until an inversion (loop, corkscrew, barrel roll etc.) track piece is reached.
 * @param input The start track element and position.
 * @param output The first track element and position which is classified as an inversion.
 * @returns true if an inversion track piece is found, otherwise false.
 *  rct2: 0x006CB149
 */
static bool RideCheckTrackContainsInversions(const CoordsXYE& input, CoordsXYE* output)
{
    if (input.element == nullptr)
        return false;

    const auto* trackElement = input.element->asTrack();
    if (trackElement == nullptr)
        return false;

    RideId rideIndex = trackElement->GetRideIndex();
    auto ride = GetRide(rideIndex);
    if (ride != nullptr)
    {
        const auto& rtd = ride->getRideTypeDescriptor();
        if (rtd.specialType == RtdSpecialType::maze)
            return true;
    }

    auto* windowMgr = Ui::GetWindowManager();
    WindowBase* w = windowMgr->FindByClass(WindowClass::rideConstruction);
    if (w != nullptr && _rideConstructionState != RideConstructionState::State0 && rideIndex == _currentRideIndex)
    {
        RideConstructionInvalidateCurrentTrack();
    }

    bool moveSlowIt = true;
    TrackCircuitIterator it, slowIt;
    trackCircuitIteratorBegin(&it, input);
    slowIt = it;

    while (trackCircuitIteratorNext(&it))
    {
        auto trackType = it.current.element->asTrack()->GetTrackType();
        const auto& ted = GetTrackElementDescriptor(trackType);
        if (ted.flags.has(TrackElementFlag::inversionToNormal))
        {
            *output = it.current;
            return true;
        }

        // Prevents infinite loops
        moveSlowIt = !moveSlowIt;
        if (moveSlowIt)
        {
            trackCircuitIteratorNext(&slowIt);
            if (trackCircuitIteratorsMatch(&it, &slowIt))
            {
                return false;
            }
        }
    }
    return false;
}

/**
 * Iterates along the track until a banked track piece is reached.
 * @param input The start track element and position.
 * @param output The first track element and position which is banked.
 * @returns true if a banked track piece is found, otherwise false.
 *  rct2: 0x006CB1D3
 */
static bool RideCheckTrackContainsBanked(const CoordsXYE& input, CoordsXYE* output)
{
    if (input.element == nullptr)
        return false;

    const auto* trackElement = input.element->asTrack();
    if (trackElement == nullptr)
        return false;

    auto rideIndex = trackElement->GetRideIndex();
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return false;

    const auto& rtd = ride->getRideTypeDescriptor();
    if (rtd.specialType == RtdSpecialType::maze)
        return true;

    auto* windowMgr = Ui::GetWindowManager();
    WindowBase* w = windowMgr->FindByClass(WindowClass::rideConstruction);
    if (w != nullptr && _rideConstructionState != RideConstructionState::State0 && rideIndex == _currentRideIndex)
    {
        RideConstructionInvalidateCurrentTrack();
    }

    bool moveSlowIt = true;
    TrackCircuitIterator it, slowIt;
    trackCircuitIteratorBegin(&it, input);
    slowIt = it;

    while (trackCircuitIteratorNext(&it))
    {
        auto trackType = it.current.element->asTrack()->GetTrackType();
        const auto& ted = GetTrackElementDescriptor(trackType);
        if (ted.flags.has(TrackElementFlag::banked))
        {
            *output = it.current;
            return true;
        }

        // Prevents infinite loops
        moveSlowIt = !moveSlowIt;
        if (moveSlowIt)
        {
            trackCircuitIteratorNext(&slowIt);
            if (trackCircuitIteratorsMatch(&it, &slowIt))
            {
                return false;
            }
        }
    }
    return false;
}

/**
 *
 *  rct2: 0x006CB25D
 */
static int32_t RideCheckStationLength(const CoordsXYE& input, CoordsXYE* output)
{
    auto* windowMgr = Ui::GetWindowManager();
    WindowBase* w = windowMgr->FindByClass(WindowClass::rideConstruction);
    if (w != nullptr && _rideConstructionState != RideConstructionState::State0
        && _currentRideIndex == input.element->asTrack()->GetRideIndex())
    {
        RideConstructionInvalidateCurrentTrack();
    }

    output->x = input.x;
    output->y = input.y;
    output->element = input.element;
    TrackBeginEnd trackBeginEnd;
    while (trackBlockGetPrevious(*output, &trackBeginEnd))
    {
        output->x = trackBeginEnd.begin_x;
        output->y = trackBeginEnd.begin_y;
        output->element = trackBeginEnd.begin_element;
    }

    int32_t num_station_elements = 0;
    CoordsXYE last_good_station = *output;

    do
    {
        const auto& ted = GetTrackElementDescriptor(output->element->asTrack()->GetTrackType());
        if (ted.sequenceData.sequences[0].flags.has(SequenceFlag::trackOrigin))
        {
            num_station_elements++;
            last_good_station = *output;
        }
        else
        {
            if (num_station_elements == 0)
                continue;
            if (num_station_elements == 1)
            {
                return 0;
            }
            num_station_elements = 0;
        }
    } while (trackBlockGetNext(output, output, nullptr, nullptr));

    // Prevent returning a pointer to a map element with no track.
    *output = last_good_station;
    if (num_station_elements == 1)
        return 0;

    return 1;
}

/**
 *
 *  rct2: 0x006CB2DA
 */
static bool RideCheckStartAndEndIsStation(const CoordsXYE& input)
{
    CoordsXYE trackBack, trackFront;

    RideId rideIndex = input.element->asTrack()->GetRideIndex();
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return false;

    auto* windowMgr = Ui::GetWindowManager();
    auto w = windowMgr->FindByClass(WindowClass::rideConstruction);
    if (w != nullptr && _rideConstructionState != RideConstructionState::State0 && rideIndex == _currentRideIndex)
    {
        RideConstructionInvalidateCurrentTrack();
    }

    // Check back of the track
    trackGetBack(input, &trackBack);
    auto trackType = trackBack.element->asTrack()->GetTrackType();
    const auto& tedBack = GetTrackElementDescriptor(trackType);
    if (!tedBack.sequenceData.sequences[0].flags.has(SequenceFlag::trackOrigin))
    {
        return false;
    }
    ride->chairliftBullwheelLocation[0] = TileCoordsXYZ{ CoordsXYZ{ trackBack.x, trackBack.y, trackBack.element->getBaseZ() } };

    // Check front of the track
    trackGetFront(input, &trackFront);
    trackType = trackFront.element->asTrack()->GetTrackType();
    const auto& tedFront = GetTrackElementDescriptor(trackType);
    if (!tedFront.sequenceData.sequences[0].flags.has(SequenceFlag::trackOrigin))
    {
        return false;
    }
    ride->chairliftBullwheelLocation[1] = TileCoordsXYZ{ CoordsXYZ{ trackFront.x, trackFront.y,
                                                                    trackFront.element->getBaseZ() } };
    return true;
}

/**
 * Sets the position and direction of the returning point on the track of a boat hire ride. This will either be the end of the
 * station or the last track piece from the end of the direction.
 *  rct2: 0x006B4D39
 */
static void RideSetBoatHireReturnPoint(Ride& ride, const CoordsXYE& startElement)
{
    auto trackType = TrackElemType::none;
    auto returnPos = startElement;
    int32_t startX = returnPos.x;
    int32_t startY = returnPos.y;
    TrackBeginEnd trackBeginEnd;
    while (trackBlockGetPrevious(returnPos, &trackBeginEnd))
    {
        // If previous track is back to the starting x, y, then break loop (otherwise possible infinite loop)
        if (trackType != TrackElemType::none && startX == trackBeginEnd.begin_x && startY == trackBeginEnd.begin_y)
            break;

        auto trackCoords = CoordsXYZ{ trackBeginEnd.begin_x, trackBeginEnd.begin_y, trackBeginEnd.begin_z };
        int32_t direction = trackBeginEnd.begin_direction;
        trackType = trackBeginEnd.begin_element->asTrack()->GetTrackType();
        auto newCoords = GetTrackElementOriginAndApplyChanges(
            { trackCoords, static_cast<Direction>(direction) }, trackType, 0, &returnPos.element, {});
        returnPos = newCoords.has_value() ? CoordsXYE{ newCoords.value(), returnPos.element }
                                          : CoordsXYE{ trackCoords, returnPos.element };
    };

    trackType = returnPos.element->asTrack()->GetTrackType();
    const auto& ted = GetTrackElementDescriptor(trackType);
    int32_t elementReturnDirection = ted.coordinates.rotationBegin;
    ride.boatHireReturnDirection = returnPos.element->getDirectionWithOffset(elementReturnDirection);
    ride.boatHireReturnPosition = TileCoordsXY{ returnPos };
}

/**
 *
 *  rct2: 0x006B4D39
 */
static void RideSetMazeEntranceExitPoints(Ride& ride)
{
    // Needs room for an entrance and an exit per station, plus one position for the list terminator.
    TileCoordsXYZD positions[(Limits::kMaxStationsPerRide * 2) + 1];

    // Create a list of all the entrance and exit positions
    TileCoordsXYZD* position = positions;
    for (const auto& station : ride.getStations())
    {
        if (!station.Entrance.IsNull())
        {
            *position++ = station.Entrance;
        }
        if (!station.Exit.IsNull())
        {
            *position++ = station.Exit;
        }
    }
    position->SetNull();

    // Enumerate entrance and exit positions
    for (position = positions; !position->IsNull(); position++)
    {
        auto entranceExitMapPos = position->ToCoordsXYZ();

        TileElement* tileElement = MapGetFirstElementAt(*position);
        do
        {
            if (tileElement == nullptr)
                break;
            if (tileElement->getType() != TileElementType::Entrance)
                continue;
            if (tileElement->asEntrance()->GetEntranceType() != ENTRANCE_TYPE_RIDE_ENTRANCE
                && tileElement->asEntrance()->GetEntranceType() != ENTRANCE_TYPE_RIDE_EXIT)
            {
                continue;
            }
            if (tileElement->getBaseZ() != entranceExitMapPos.z)
                continue;

            MazeEntranceHedgeRemoval({ entranceExitMapPos, tileElement });
        } while (!(tileElement++)->isLastForTile());
    }
}

void SetBrakeClosedMultiTile(TrackElement& trackElement, const CoordsXY& trackLocation, bool isClosed)
{
    switch (trackElement.GetTrackType())
    {
        case TrackElemType::diagUp25ToFlat:
        case TrackElemType::diagUp60ToFlat:
        case TrackElemType::cableLiftHill:
        case TrackElemType::diagBrakes:
        case TrackElemType::diagBlockBrakes:
            GetTrackElementOriginAndApplyChanges(
                { trackLocation, trackElement.getBaseZ(), trackElement.getDirection() }, trackElement.GetTrackType(), isClosed,
                nullptr, { TrackElementSetFlag::brakeClosed });
            break;
        default:
            trackElement.SetBrakeClosed(isClosed);
    }
}

/**
 * Opens all block brakes of a ride.
 *  rct2: 0x006B4E6B
 */
static void RideOpenBlockBrakes(const CoordsXYE& startElement)
{
    CoordsXYE currentElement = startElement;
    do
    {
        auto trackType = currentElement.element->asTrack()->GetTrackType();
        switch (trackType)
        {
            case TrackElemType::blockBrakes:
            case TrackElemType::diagBlockBrakes:
                BlockBrakeSetLinkedBrakesClosed(
                    CoordsXYZ(currentElement.x, currentElement.y, currentElement.element->getBaseZ()),
                    *currentElement.element->asTrack(), false);
                [[fallthrough]];
            case TrackElemType::diagUp25ToFlat:
            case TrackElemType::diagUp60ToFlat:
            case TrackElemType::cableLiftHill:
            case TrackElemType::endStation:
            case TrackElemType::up25ToFlat:
            case TrackElemType::up60ToFlat:
                SetBrakeClosedMultiTile(*currentElement.element->asTrack(), { currentElement.x, currentElement.y }, false);
                break;
            default:
                break;
        }
    } while (trackBlockGetNext(&currentElement, &currentElement, nullptr, nullptr)
             && currentElement.element != startElement.element);
}

/**
 * Set the open status of brakes adjacent to the block brake
 */
void BlockBrakeSetLinkedBrakesClosed(const CoordsXYZ& vehicleTrackLocation, TrackElement& trackElement, bool isClosed)
{
    uint8_t brakeSpeed = trackElement.GetBrakeBoosterSpeed();

    auto tileElement = reinterpret_cast<TileElement*>(&trackElement);
    auto location = vehicleTrackLocation;
    TrackBeginEnd trackBeginEnd, slowTrackBeginEnd;
    TileElement slowTileElement = *tileElement;
    bool counter = true;
    CoordsXY slowLocation = location;
    do
    {
        if (!trackBlockGetPrevious({ location, tileElement }, &trackBeginEnd))
        {
            return;
        }
        if (trackBeginEnd.begin_x == vehicleTrackLocation.x && trackBeginEnd.begin_y == vehicleTrackLocation.y
            && tileElement == trackBeginEnd.begin_element)
        {
            return;
        }

        location.x = trackBeginEnd.end_x;
        location.y = trackBeginEnd.end_y;
        location.z = trackBeginEnd.begin_z;
        tileElement = trackBeginEnd.begin_element;

        if (trackTypeIsBrakes(tileElement->asTrack()->GetTrackType()))
        {
            SetBrakeClosedMultiTile(
                *tileElement->asTrack(), { trackBeginEnd.begin_x, trackBeginEnd.begin_y },
                (tileElement->asTrack()->GetBrakeBoosterSpeed() >= brakeSpeed) || isClosed);
        }

        // prevent infinite loop
        counter = !counter;
        if (counter)
        {
            trackBlockGetPrevious({ slowLocation, &slowTileElement }, &slowTrackBeginEnd);
            slowLocation.x = slowTrackBeginEnd.end_x;
            slowLocation.y = slowTrackBeginEnd.end_y;
            slowTileElement = *(slowTrackBeginEnd.begin_element);
            if (slowLocation == location && slowTileElement.getBaseZ() == tileElement->getBaseZ()
                && slowTileElement.getType() == tileElement->getType()
                && slowTileElement.getDirection() == tileElement->getDirection())
            {
                return;
            }
        }
    } while (trackTypeIsBrakes(trackBeginEnd.begin_element->asTrack()->GetTrackType()));
}

/**
 *
 *  rct2: 0x006B4D26
 */
static void RideSetStartFinishPoints(RideId rideIndex, const CoordsXYE& startElement)
{
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return;

    const auto& rtd = ride->getRideTypeDescriptor();
    if (rtd.specialType == RtdSpecialType::maze)
        RideSetMazeEntranceExitPoints(*ride);
    else if (rtd.specialType == RtdSpecialType::boatHire)
        RideSetBoatHireReturnPoint(*ride, startElement);

    if (ride->isBlockSectioned() && !ride->flags.has(RideFlag::onTrack))
    {
        RideOpenBlockBrakes(startElement);
    }
}

/**
 *
 *  rct2: 0x0069ED9E
 */
static int32_t count_free_misc_sprite_slots()
{
    auto& gameState = getGameState();
    int32_t miscSpriteCount = gameState.entities.GetMiscEntityCount();
    int32_t remainingSpriteCount = gameState.entities.GetNumFreeEntities();
    return std::max(0, miscSpriteCount + remainingSpriteCount - 300);
}

static constexpr CoordsXY word_9A3AB4[4] = {
    { 0, 0 },
    { 0, -96 },
    { -96, -96 },
    { -96, 0 },
};

// clang-format off
static constexpr CoordsXY word_9A2A60[] = {
    { 0, 16 },
    { 16, 31 },
    { 31, 16 },
    { 16, 0 },
    { 16, 16 },
    { 64, 64 },
    { 64, -32 },
    { -32, -32 },
    { -32, 64 },
};
// clang-format on

/**
 *
 *  rct2: 0x006DD90D
 */
static Vehicle* VehicleCreateCar(
    Ride& ride, int32_t carEntryIndex, int32_t carIndex, int32_t vehicleIndex, const CoordsXYZ& carPosition,
    int32_t* remainingDistance, TrackElement* trackElement)
{
    if (trackElement == nullptr)
        return nullptr;

    auto rideEntry = ride.getRideEntry();
    if (rideEntry == nullptr)
        return nullptr;

    auto& carEntry = rideEntry->Cars[carEntryIndex];

    auto* vehicle = getGameState().entities.CreateEntity<Vehicle>();
    if (vehicle == nullptr)
        return nullptr;

    vehicle->ride = ride.id;
    vehicle->ride_subtype = ride.subtype;

    vehicle->vehicle_type = carEntryIndex;
    vehicle->SubType = carIndex == 0 ? Vehicle::Type::head : Vehicle::Type::tail;
    vehicle->var_44 = Numerics::ror32(carEntry.spacing, 10) & 0xFFFF;

    const auto halfSpacing = carEntry.spacing >> 1;
    *remainingDistance -= halfSpacing;
    vehicle->remaining_distance = *remainingDistance;

    if (!carEntry.flags.has(CarEntryFlag::isGoKart))
    {
        *remainingDistance -= halfSpacing;
    }

    // Loc6DD9A5:
    vehicle->spriteData.width = carEntry.spriteWidth;
    vehicle->spriteData.heightMin = carEntry.spriteHeightNegative;
    vehicle->spriteData.heightMax = carEntry.spriteHeightPositive;
    vehicle->mass = carEntry.car_mass;
    vehicle->num_seats = carEntry.num_seats;
    vehicle->speed = carEntry.powered_max_speed;
    vehicle->powered_acceleration = carEntry.powered_acceleration;
    vehicle->velocity = 0;
    vehicle->acceleration = 0;
    vehicle->SwingSprite = 0;
    vehicle->SwingPosition = 0;
    vehicle->SwingSpeed = 0;
    vehicle->restraints_position = 0;
    vehicle->spin_sprite = 0;
    vehicle->spin_speed = 0;
    vehicle->sound2_flags = 0;
    vehicle->sound1_id = Audio::SoundId::null;
    vehicle->sound2_id = Audio::SoundId::null;
    vehicle->next_vehicle_on_train = EntityId::GetNull();
    vehicle->CollisionDetectionTimer = 0;
    vehicle->animation_frame = 0;
    vehicle->animationState = 0;
    vehicle->scream_sound_id = Audio::SoundId::null;
    vehicle->pitch = VehiclePitch::flat;
    vehicle->roll = VehicleRoll::unbanked;
    vehicle->target_seat_rotation = 4;
    vehicle->seat_rotation = 4;
    for (size_t i = 0; i < std::size(vehicle->peep); i++)
    {
        vehicle->peep[i] = EntityId::GetNull();
    }

    const auto& rtd = ride.getRideTypeDescriptor();
    if (carEntry.flags.has(CarEntryFlag::useDodgemCarPlacement))
    {
        // Loc6DDCA4:
        vehicle->TrackSubposition = VehicleTrackSubposition::Default;
        int32_t direction = trackElement->getDirection();
        auto dodgemPos = carPosition + CoordsXYZ{ word_9A3AB4[direction], 0 };
        vehicle->TrackLocation = dodgemPos;
        vehicle->current_station = trackElement->GetStationIndex();

        dodgemPos.z += rtd.Heights.VehicleZOffset;

        vehicle->SetTrackDirection(0);
        vehicle->SetTrackType(trackElement->GetTrackType());
        vehicle->track_progress = 0;
        vehicle->SetState(Vehicle::Status::movingToEndOfStation);
        vehicle->flags.clearAll();

        CoordsXY chosenLoc;
        auto numAttempts = 0;
        // Loc6DDD26:
        do
        {
            numAttempts++;
            // This can happen when trying to spawn dozens of cars in a tiny area.
            if (numAttempts > 10000)
                return nullptr;

            vehicle->orientation = ScenarioRand() & 0x1E;
            chosenLoc.y = dodgemPos.y + (ScenarioRand() & 0xFF);
            chosenLoc.x = dodgemPos.x + (ScenarioRand() & 0xFF);
        } while (vehicle->DodgemsCarWouldCollideAt(chosenLoc).has_value());

        vehicle->moveToAndUpdateSpatialIndex({ chosenLoc, dodgemPos.z });
    }
    else
    {
        VehicleTrackSubposition subposition = VehicleTrackSubposition::Default;
        if (carEntry.flags.has(CarEntryFlag::isChairlift))
        {
            subposition = VehicleTrackSubposition::ChairliftGoingOut;
        }

        if (carEntry.flags.has(CarEntryFlag::isGoKart))
        {
            // Choose which lane Go Kart should start in
            subposition = VehicleTrackSubposition::GoKartsLeftLane;
            if (vehicleIndex & 1)
            {
                subposition = VehicleTrackSubposition::GoKartsRightLane;
            }
        }
        if (carEntry.flags.has(CarEntryFlag::isMiniGolf))
        {
            subposition = VehicleTrackSubposition::MiniGolfStart9;
            vehicle->var_D3 = 0;
            vehicle->mini_golf_current_animation = MiniGolfAnimation::Walk;
            vehicle->miniGolfFlags.clearAll();
        }
        if (carEntry.flags.has(CarEntryFlag::isReverserCoasterBogie))
        {
            if (vehicle->IsHead())
            {
                subposition = VehicleTrackSubposition::ReverserRCFrontBogie;
            }
        }
        if (carEntry.flags.has(CarEntryFlag::isReverserCoasterPassengerCar))
        {
            subposition = VehicleTrackSubposition::ReverserRCRearBogie;
        }
        vehicle->TrackSubposition = subposition;

        auto chosenLoc = carPosition;
        vehicle->TrackLocation = chosenLoc;

        int32_t direction = trackElement->getDirection();
        vehicle->orientation = direction << 3;

        if (ride.getRideTypeDescriptor().specialType == RtdSpecialType::spaceRings)
        {
            direction = 4;
        }
        else
        {
            if (rtd.flags.has(RtdFlag::vehicleIsIntegral))
            {
                if (rtd.StartTrackPiece != TrackElemType::flatTrack1x4B)
                {
                    if (rtd.StartTrackPiece != TrackElemType::flatTrack1x4A)
                    {
                        if (ride.getRideTypeDescriptor().specialType == RtdSpecialType::enterprise)
                        {
                            direction += 5;
                        }
                        else
                        {
                            direction = 4;
                        }
                    }
                }
            }
        }

        chosenLoc += CoordsXYZ{ word_9A2A60[direction], rtd.Heights.VehicleZOffset };

        vehicle->current_station = trackElement->GetStationIndex();

        vehicle->moveTo(chosenLoc);
        vehicle->SetTrackType(trackElement->GetTrackType());
        vehicle->SetTrackDirection(vehicle->orientation >> 3);
        vehicle->track_progress = 31;
        if (carEntry.flags.has(CarEntryFlag::isMiniGolf))
        {
            vehicle->track_progress = 15;
        }
        vehicle->flags = { VehicleFlag::collisionDisabled };
        if (carEntry.flags.has(CarEntryFlag::hasInvertedSpriteSet))
        {
            if (trackElement->IsInverted())
            {
                vehicle->flags.set(VehicleFlag::carIsInverted);
            }
        }
        vehicle->SetState(Vehicle::Status::movingToEndOfStation);

        if (ride.flags.has(RideFlag::reversedTrains))
        {
            vehicle->SubType = carIndex == (ride.numCarsPerTrain - 1) ? Vehicle::Type::head : Vehicle::Type::tail;
            vehicle->flags.set(VehicleFlag::carIsReversed);
        }
    }

    // Loc6DDD5E:
    vehicle->num_peeps = 0;
    vehicle->next_free_seat = 0;
    vehicle->BoatLocation.SetNull();
    return vehicle;
}

/**
 *
 *  rct2: 0x006DD84C
 */
static TrainReference VehicleCreateTrain(
    Ride& ride, const CoordsXYZ& trainPos, int32_t vehicleIndex, int32_t* remainingDistance, TrackElement* trackElement)
{
    TrainReference train = { nullptr, nullptr };
    bool isReversed = ride.flags.has(RideFlag::reversedTrains);

    for (int32_t carIndex = 0; carIndex < ride.numCarsPerTrain; carIndex++)
    {
        auto carSpawnIndex = (isReversed) ? (ride.numCarsPerTrain - 1) - carIndex : carIndex;

        auto vehicle = RideEntryGetVehicleAtPosition(ride.subtype, ride.numCarsPerTrain, carSpawnIndex);
        auto car = VehicleCreateCar(ride, vehicle, carSpawnIndex, vehicleIndex, trainPos, remainingDistance, trackElement);
        if (car == nullptr)
            break;

        if (carIndex == 0)
        {
            train.head = car;
        }
        else
        {
            // Link the previous car with this car
            train.tail->next_vehicle_on_train = car->id;
            train.tail->next_vehicle_on_ride = car->id;
            car->prev_vehicle_on_ride = train.tail->id;
        }
        train.tail = car;
    }

    return train;
}

static bool VehicleCreateTrains(Ride& ride, const CoordsXYZ& trainsPos, TrackElement* trackElement, int16_t numberOfTrains)
{
    TrainReference firstTrain = {};
    TrainReference lastTrain = {};
    int32_t remainingDistance = 0;
    bool allTrainsCreated = true;

    for (int32_t vehicleIndex = 0; vehicleIndex < numberOfTrains; vehicleIndex++)
    {
        if (ride.isBlockSectioned())
        {
            remainingDistance = 0;
        }
        TrainReference train = VehicleCreateTrain(ride, trainsPos, vehicleIndex, &remainingDistance, trackElement);
        if (train.head == nullptr || train.tail == nullptr)
        {
            allTrainsCreated = false;
            continue;
        }

        if (vehicleIndex == 0)
        {
            firstTrain = train;
        }
        else
        {
            // Link the end of the previous train with the front of this train
            lastTrain.tail->next_vehicle_on_ride = train.head->id;
            train.head->prev_vehicle_on_ride = lastTrain.tail->id;
        }
        lastTrain = train;

        for (int32_t i = 0; i <= Limits::kMaxTrainsPerRide; i++)
        {
            if (ride.vehicles[i].IsNull())
            {
                ride.vehicles[i] = train.head->id;
                break;
            }
        }
    }

    // Link the first train and last train together. Nullptr checks are there to keep Clang happy.
    if (lastTrain.tail != nullptr)
        firstTrain.head->prev_vehicle_on_ride = lastTrain.tail->id;
    if (firstTrain.head != nullptr)
        lastTrain.tail->next_vehicle_on_ride = firstTrain.head->id;

    return allTrainsCreated;
}

/**
 *
 *  rct2: 0x006DDE9E
 */
static void RidecreateVehiclesFindFirstBlock(const Ride& ride, CoordsXYE* outXYElement)
{
    Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[0]);
    if (vehicle == nullptr)
        return;

    auto curTrackPos = vehicle->TrackLocation;
    auto curTrackElement = MapGetTrackElementAt(curTrackPos);

    assert(curTrackElement != nullptr);

    CoordsXY trackPos = curTrackPos;
    auto trackElement = curTrackElement;
    TrackBeginEnd trackBeginEnd;
    while (trackBlockGetPrevious({ trackPos, reinterpret_cast<TileElement*>(trackElement) }, &trackBeginEnd))
    {
        trackPos = { trackBeginEnd.end_x, trackBeginEnd.end_y };
        trackElement = trackBeginEnd.begin_element->asTrack();
        if (trackPos == curTrackPos && trackElement == curTrackElement)
        {
            break;
        }

        auto trackType = trackElement->GetTrackType();
        switch (trackType)
        {
            case TrackElemType::diagUp25ToFlat:
            case TrackElemType::diagUp60ToFlat:
                if (!trackElement->HasChain())
                {
                    break;
                }
                [[fallthrough]];
            case TrackElemType::diagBlockBrakes:
            {
                TileElement* tileElement = MapGetTrackElementAtOfTypeSeq(
                    { trackBeginEnd.begin_x, trackBeginEnd.begin_y, trackBeginEnd.begin_z }, trackType, 0);

                if (tileElement != nullptr)
                {
                    outXYElement->x = trackBeginEnd.begin_x;
                    outXYElement->y = trackBeginEnd.begin_y;
                    outXYElement->element = tileElement;
                    return;
                }
                break;
            }
            case TrackElemType::up25ToFlat:
            case TrackElemType::up60ToFlat:
                if (!trackElement->HasChain())
                {
                    break;
                }
                [[fallthrough]];
            case TrackElemType::endStation:
            case TrackElemType::blockBrakes:
                *outXYElement = { trackPos, reinterpret_cast<TileElement*>(trackElement) };
                return;
            default:
                break;
        }
    }

    outXYElement->x = curTrackPos.x;
    outXYElement->y = curTrackPos.y;
    outXYElement->element = reinterpret_cast<TileElement*>(curTrackElement);
}

/**
 * Create and place the rides vehicles
 *  rct2: 0x006DD84C
 */
ResultWithMessage Ride::createVehicles(const CoordsXYE& element, bool isApplying, bool isSimulating)
{
    updateMaxVehicles();
    if (subtype == kObjectEntryIndexNull)
    {
        return { true };
    }

    // Check if there are enough free sprite slots for all the vehicles
    int32_t numberOfTrains = numTrains;
    if (isBlockSectioned() && isSimulating)
    {
        numberOfTrains = 1;
    }
    int32_t totalCars = numberOfTrains * numCarsPerTrain;
    if (totalCars > count_free_misc_sprite_slots())
    {
        return { false, STR_UNABLE_TO_CREATE_ENOUGH_VEHICLES };
    }

    if (!isApplying)
    {
        return { true };
    }

    auto* trackElement = element.element->asTrack();
    auto vehiclePos = CoordsXYZ{ element, element.element->getBaseZ() };
    int32_t direction = trackElement->getDirection();

    //
    if (mode == RideMode::stationToStation)
    {
        vehiclePos -= CoordsXYZ{ CoordsDirectionDelta[direction], 0 };

        trackElement = MapGetTrackElementAt(vehiclePos);

        vehiclePos.z = trackElement->getBaseZ();
    }

    if (!VehicleCreateTrains(*this, vehiclePos, trackElement, numberOfTrains))
    {
        // This flag is needed for Ride::removeVehicles()
        flags.set(RideFlag::onTrack);
        removeVehicles();
        return { false, STR_UNABLE_TO_CREATE_ENOUGH_VEHICLES };
    }
    // return true;

    // Initialise station departs
    // 006DDDD0:
    flags.set(RideFlag::onTrack);
    for (int32_t i = 0; i < Limits::kMaxStationsPerRide; i++)
    {
        stations[i].Depart = (stations[i].Depart & kStationDepartFlag) | 1;
    }

    const auto& rtd = getRideTypeDescriptor();
    if (rtd.specialType != RtdSpecialType::spaceRings && !rtd.flags.has(RtdFlag::vehicleIsIntegral))
    {
        if (isBlockSectioned() && !isSimulating)
        {
            CoordsXYE firstBlock{};
            RidecreateVehiclesFindFirstBlock(*this, &firstBlock);
            moveTrainsToBlockBrakes(
                { firstBlock.x, firstBlock.y, firstBlock.element->getBaseZ() }, *firstBlock.element->asTrack());
        }
        else
        {
            for (int32_t i = 0; i < numTrains; i++)
            {
                Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(vehicles[i]);
                if (vehicle == nullptr)
                {
                    continue;
                }

                auto carEntry = vehicle->Entry();

                if (!carEntry->flags.has(CarEntryFlag::useDodgemCarPlacement))
                {
                    vehicle->UpdateTrackMotion(nullptr);
                }

                vehicle->EnableCollisionsForTrain();
            }
        }
    }
    RideUpdateVehicleColours(*this);
    return { true };
}

/**
 * Move all the trains so each one will be placed at the block brake of a different block.
 * The first vehicle will placed into the first block and all other vehicles in the blocks
 * preceding that block.
 *  rct2: 0x006DDF9C
 */
void Ride::moveTrainsToBlockBrakes(const CoordsXYZ& firstBlockPosition, TrackElement& firstBlock)
{
    // If the ride has a cable lift, we don't want to fetch the cable lift element and the block preceding it
    TrackElement* cableLiftTileElement = nullptr;
    TrackElement* cableLiftPreviousBlock = nullptr;
    if (flags.has(RideFlag::cableLiftHillComponentUsed))
    {
        cableLiftTileElement = MapGetTrackElementAt(cableLiftLoc);
        if (cableLiftTileElement != nullptr)
        {
            CoordsXYZ location = cableLiftLoc;
            cableLiftPreviousBlock = trackGetPreviousBlock(location, reinterpret_cast<TileElement*>(cableLiftTileElement));
        }
    }

    for (int32_t i = 0; i < numTrains; i++)
    {
        auto train = getGameState().entities.GetEntity<Vehicle>(vehicles[i]);
        if (train == nullptr)
            continue;

        // At this point, all vehicles have state of MovingToEndOfStation, which slowly moves forward at a constant speed
        // regardless of incline. The first vehicle stops at the station immediately, while all other vehicles seek forward
        // until they reach a closed block brake. The block brake directly before the station is set to closed every frame
        // because the trains will open the block brake when the tail leaves the station. Brakes have no effect at this time, so
        // do not set linked brakes when closing the first block.
        train->UpdateTrackMotion(nullptr);

        if (i == 0)
        {
            train->EnableCollisionsForTrain();
            continue;
        }

        size_t numIterations = 0;
        do
        {
            // Fixes both freezing issues in #15503.
            // TODO: refactor the code so a tortoise-and-hare algorithm can be used.
            if (numIterations++ > 1000000)
            {
                break;
            }

            // Setting the first block before the cable lift to the same state as the cable lift ensures that any train which
            // would be placed on the cable lift will instead stop on the block before it. As there can only be one cable lift
            // per ride and there must always be at least one block left free, there will be enough blocks remaining. This fixes
            // the bug in #1122.
            if (cableLiftTileElement != nullptr && cableLiftPreviousBlock != nullptr)
            {
                cableLiftPreviousBlock->SetBrakeClosed(cableLiftTileElement->IsBrakeClosed());
            }
            firstBlock.SetBrakeClosed(true);
            for (Vehicle* car = train; car != nullptr;
                 car = getGameState().entities.GetEntity<Vehicle>(car->next_vehicle_on_train))
            {
                car->velocity = 0;
                car->acceleration = 0;
                car->SwingSprite = 0;
                car->remaining_distance += 13962;
            }
        } while (!(train->UpdateTrackMotion(nullptr) & VEHICLE_UPDATE_MOTION_TRACK_FLAG_VEHICLE_AT_BLOCK_BRAKE));

        // All vehicles are in position, set the block brake directly before the station one last time and make sure the brakes
        // are set appropriately
        SetBrakeClosedMultiTile(firstBlock, firstBlockPosition, true);
        if (trackTypeIsBlockBrakes(firstBlock.GetTrackType()))
        {
            BlockBrakeSetLinkedBrakesClosed(firstBlockPosition, firstBlock, true);
        }
        for (Vehicle* car = train; car != nullptr; car = getGameState().entities.GetEntity<Vehicle>(car->next_vehicle_on_train))
        {
            car->flags.unset(VehicleFlag::collisionDisabled);
            car->SetState(Vehicle::Status::travelling, car->sub_state);
            if ((car->GetTrackType()) == TrackElemType::endStation)
            {
                car->SetState(Vehicle::Status::movingToEndOfStation, car->sub_state);
            }
        }
    }

    // After all trains are in position, set the block preceding the cable lift to open.
    if (cableLiftPreviousBlock != nullptr)
    {
        cableLiftPreviousBlock->SetBrakeClosed(false);
    }
}

static bool RideGetStationTile(const Ride& ride, CoordsXYE* output)
{
    for (const auto& station : ride.getStations())
    {
        CoordsXYZ trackStart = station.GetStart();
        if (trackStart.IsNull())
            continue;

        TileElement* tileElement = MapGetTrackElementAtOfType(trackStart, TrackElemType::endStation);
        if (tileElement == nullptr)
            continue;

        *output = { trackStart.x, trackStart.y, tileElement };
        return true;
    }
    return false;
}

/**
 * Checks and initialises the cable lift track returns false if unable to find
 * appropriate track.
 *  rct2: 0x006D31A6
 */
static ResultWithMessage RideInitialiseCableLiftTrack(const Ride& ride, bool isApplying)
{
    // Despawn existing cable lift tiles
    CoordsXYE stationTile;
    if (!RideGetStationTile(ride, &stationTile))
        return { false, STR_CABLE_LIFT_HILL_MUST_START_IMMEDIATELY_AFTER_STATION_OR_BLOCK_BRAKE };

    if (isApplying)
    {
        // In case circuit is incomplete, find the start of the track in order to ensure all tiles connected
        // to the station are cleared
        RideGetStartOfTrack(&stationTile);

        TrackCircuitIterator it;
        trackCircuitIteratorBegin(&it, stationTile);
        while (trackCircuitIteratorNext(&it))
        {
            TileElement* tileElement = it.current.element;
            GetTrackElementOriginAndApplyChanges(
                { { it.current, tileElement->getBaseZ() }, tileElement->getDirection() },
                tileElement->asTrack()->GetTrackType(), 0, &tileElement, { TrackElementSetFlag::cableLiftOff });
        }
    }

    // Spawn new cable lift tiles
    auto cableLiftTileElement = MapGetTrackElementAt(ride.cableLiftLoc);
    CoordsXYE cableLiftCoords = { ride.cableLiftLoc, reinterpret_cast<TileElement*>(cableLiftTileElement) };
    if (cableLiftTileElement == nullptr)
        return { false };

    TrackCircuitIterator it;
    trackCircuitIteratorBegin(&it, cableLiftCoords);
    while (trackCircuitIteratorPrevious(&it))
    {
        TileElement* tileElement = it.current.element;
        auto trackType = tileElement->asTrack()->GetTrackType();
        switch (trackType)
        {
            case TrackElemType::up25:
            case TrackElemType::up60:
            case TrackElemType::flatToUp25:
            case TrackElemType::up25ToFlat:
            case TrackElemType::up25ToUp60:
            case TrackElemType::up60ToUp25:
            case TrackElemType::flatToUp60LongBase:
            case TrackElemType::flat:
                if (isApplying)
                {
                    GetTrackElementOriginAndApplyChanges(
                        { { it.current, tileElement->getBaseZ() }, tileElement->getDirection() }, trackType, 0, &tileElement,
                        { TrackElementSetFlag::cableLiftOn });
                }
                break;
            case TrackElemType::endStation:
            case TrackElemType::blockBrakes:
                return { true };
            default:
                return { false, STR_CABLE_LIFT_HILL_MUST_START_IMMEDIATELY_AFTER_STATION_OR_BLOCK_BRAKE };
        }
    }
    return { false, STR_CABLE_LIFT_HILL_MUST_START_IMMEDIATELY_AFTER_STATION_OR_BLOCK_BRAKE };
}

/**
 *
 *  rct2: 0x006DF4D4
 */
static ResultWithMessage RideCreateCableLift(RideId rideIndex, bool isApplying)
{
    auto ride = GetRide(rideIndex);
    if (ride == nullptr)
        return { false };

    if (ride->mode != RideMode::continuousCircuitBlockSectioned && ride->mode != RideMode::continuousCircuit)
    {
        return { false, STR_CABLE_LIFT_UNABLE_TO_WORK_IN_THIS_OPERATING_MODE };
    }

    if (ride->numCircuits > 1)
    {
        return { false, STR_MULTICIRCUIT_NOT_POSSIBLE_WITH_CABLE_LIFT_HILL };
    }

    if (count_free_misc_sprite_slots() <= 5)
    {
        return { false, STR_UNABLE_TO_CREATE_ENOUGH_VEHICLES };
    }

    auto cableLiftInitialiseResult = RideInitialiseCableLiftTrack(*ride, isApplying);
    if (!cableLiftInitialiseResult.Successful)
    {
        return { false, cableLiftInitialiseResult.Message };
    }

    if (!isApplying)
    {
        return { true };
    }

    auto cableLiftLoc = ride->cableLiftLoc;
    auto tileElement = MapGetTrackElementAt(cableLiftLoc);
    int32_t direction = tileElement->getDirection();

    Vehicle* head = nullptr;
    Vehicle* tail = nullptr;
    uint32_t ebx = 0;
    for (int32_t i = 0; i < 5; i++)
    {
        uint32_t edx = Numerics::ror32(0x15478, 10);
        uint16_t var_44 = edx & 0xFFFF;
        edx = Numerics::rol32(edx, 10) >> 1;
        ebx -= edx;
        int32_t remaining_distance = ebx;
        ebx -= edx;

        Vehicle* current = CableLiftSegmentCreate(
            *ride, cableLiftLoc.x, cableLiftLoc.y, cableLiftLoc.z / 8, direction, var_44, remaining_distance, i == 0);
        current->next_vehicle_on_train = EntityId::GetNull();
        if (i == 0)
        {
            head = current;
        }
        else
        {
            tail->next_vehicle_on_train = current->id;
            tail->next_vehicle_on_ride = current->id;
            current->prev_vehicle_on_ride = tail->id;
        }
        tail = current;
    }
    head->prev_vehicle_on_ride = tail->id;
    tail->next_vehicle_on_ride = head->id;

    ride->flags.set(RideFlag::cableLift);
    head->CableLiftUpdateTrackMotion();
    return { true };
}

/**
 * Opens the construction window prompting to construct a missing entrance or exit.
 * This will also move the screen to the first station missing the entrance or exit.
 *  rct2: 0x006B51C0
 */
void Ride::constructMissingEntranceOrExit() const
{
    auto* w = WindowGetMain();
    if (w == nullptr)
        return;

    int8_t entranceOrExit = -1;
    const RideStation* incompleteStation = nullptr;
    for (const auto& station : stations)
    {
        if (station.Start.IsNull())
            continue;

        if (station.Entrance.IsNull())
        {
            entranceOrExit = WC_RIDE_CONSTRUCTION__WIDX_ENTRANCE;
            incompleteStation = &station;
            break;
        }

        if (station.Exit.IsNull())
        {
            entranceOrExit = WC_RIDE_CONSTRUCTION__WIDX_EXIT;
            incompleteStation = &station;
            break;
        }
    }

    if (incompleteStation == nullptr)
    { // No station with a missing entrance or exit was found
        return;
    }

    const auto& rtd = getRideTypeDescriptor();
    if (rtd.specialType != RtdSpecialType::maze)
    {
        auto location = incompleteStation->GetStart();
        WindowScrollToLocation(*w, location);

        CoordsXYE trackElement;
        RideTryGetOriginElement(*this, &trackElement);
        findTrackGap(*this, trackElement, &trackElement);
        int32_t ok = RideModify(trackElement);
        if (ok == 0)
        {
            return;
        }

        auto* windowMgr = Ui::GetWindowManager();
        w = windowMgr->FindByClass(WindowClass::rideConstruction);
        if (w != nullptr)
            w->onMouseUp(entranceOrExit);
    }
}

/**
 *
 *  rct2: 0x006B528A
 */
static void RideScrollToTrackError(const CoordsXYE& trackElement)
{
    if (trackElement.element == nullptr)
        return;

    auto* w = WindowGetMain();
    if (w != nullptr)
    {
        WindowScrollToLocation(*w, { trackElement, trackElement.element->getBaseZ() });
        RideModify(trackElement);
    }
}

/**
 *
 *  rct2: 0x006B4F6B
 */
TrackElement* Ride::getOriginElement(StationIndex stationIndex) const
{
    auto stationLoc = getStation(stationIndex).Start;
    TileElement* tileElement = MapGetFirstElementAt(stationLoc);
    if (tileElement == nullptr)
        return nullptr;
    do
    {
        if (tileElement->getType() != TileElementType::Track)
            continue;

        auto* trackElement = tileElement->asTrack();
        const auto& ted = GetTrackElementDescriptor(trackElement->GetTrackType());
        if (!ted.sequenceData.sequences[0].flags.has(SequenceFlag::trackOrigin))
            continue;

        if (trackElement->GetRideIndex() == id)
            return trackElement;
    } while (!(tileElement++)->isLastForTile());

    return nullptr;
}

ResultWithMessage Ride::test(bool isApplying)
{
    if (type == kRideTypeNull)
    {
        LOG_WARNING("Invalid ride type for ride %u", id.ToUnderlying());
        return { false };
    }

    auto* windowMgr = Ui::GetWindowManager();
    windowMgr->CloseByNumber(WindowClass::rideConstruction, id.ToUnderlying());

    StationIndex stationIndex = {};
    auto message = changeStatusDoStationChecks(stationIndex);
    if (!message.Successful)
    {
        return message;
    }

    auto entranceExitCheck = RideCheckForEntranceExit(id);
    if (!entranceExitCheck.Successful)
    {
        constructMissingEntranceOrExit();
        return { false, entranceExitCheck.Message };
    }

    CoordsXYE trackElement = {};
    message = changeStatusGetStartElement(stationIndex, trackElement);
    if (!message.Successful)
    {
        return message;
    }

    message = changeStatusCheckCompleteCircuit(trackElement);
    if (!message.Successful)
    {
        return message;
    }

    message = changeStatusCheckTrackValidity(trackElement, false);
    if (!message.Successful)
    {
        return message;
    }

    return changeStatusCreateVehicles(isApplying, trackElement, false);
}

ResultWithMessage Ride::simulate(bool isApplying)
{
    CoordsXYE trackElement = {};
    if (type == kRideTypeNull)
    {
        LOG_WARNING("Invalid ride type for ride %u", id.ToUnderlying());
        return { false };
    }

    StationIndex stationIndex = {};
    auto message = changeStatusDoStationChecks(stationIndex);
    if (!message.Successful)
    {
        return message;
    }

    message = changeStatusGetStartElement(stationIndex, trackElement);
    if (!message.Successful)
    {
        return message;
    }

    message = changeStatusCheckTrackValidity(trackElement, true);
    if (!message.Successful)
    {
        return message;
    }

    return changeStatusCreateVehicles(isApplying, trackElement, true);
}

/**
 *
 *  rct2: 0x006B4EEA
 */
ResultWithMessage Ride::open(bool isApplying)
{
    // Check to see if construction tool is in use. If it is close the construction window
    // to set the track to its final state and clean up ghosts.
    // We can't just call close as it would cause a stack overflow during shop creation
    // with auto open on.
    if (isToolActive(WindowClass::rideConstruction, static_cast<WindowNumber>(id.ToUnderlying())))
    {
        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->CloseByNumber(WindowClass::rideConstruction, id.ToUnderlying());
    }

    StationIndex stationIndex = {};
    auto message = changeStatusDoStationChecks(stationIndex);
    if (!message.Successful)
    {
        return message;
    }

    auto entranceExitCheck = RideCheckForEntranceExit(id);
    if (!entranceExitCheck.Successful)
    {
        constructMissingEntranceOrExit();
        return { false, entranceExitCheck.Message };
    }

    if (isApplying)
    {
        chainQueues();
        flags.set(RideFlag::everBeenOpened);
    }

    CoordsXYE trackElement = {};
    message = changeStatusGetStartElement(stationIndex, trackElement);
    if (!message.Successful)
    {
        return message;
    }

    message = changeStatusCheckCompleteCircuit(trackElement);
    if (!message.Successful)
    {
        return message;
    }

    message = changeStatusCheckTrackValidity(trackElement, false);
    if (!message.Successful)
    {
        return message;
    }

    return changeStatusCreateVehicles(isApplying, trackElement, false);
}

/**
 * Given a track element of the ride, find the start of the track.
 * It has to do this as a backwards loop in case this is an incomplete track.
 */
void RideGetStartOfTrack(CoordsXYE* output)
{
    TrackBeginEnd trackBeginEnd;
    CoordsXYE trackElement = *output;
    if (trackBlockGetPrevious(trackElement, &trackBeginEnd))
    {
        TileElement* initial_map = trackElement.element;
        TrackBeginEnd slowIt = trackBeginEnd;
        bool moveSlowIt = true;
        do
        {
            // Because we are working backwards, begin_element is the section at the end of a piece of track, whereas
            // begin_x and begin_y are the coordinates at the start of a piece of track, so we need to pass end_x and
            // end_y
            CoordsXYE lastGood = {
                /* .x = */ trackBeginEnd.end_x,
                /* .y = */ trackBeginEnd.end_y,
                /* .element = */ trackBeginEnd.begin_element,
            };

            if (!trackBlockGetPrevious(
                    { trackBeginEnd.end_x, trackBeginEnd.end_y, trackBeginEnd.begin_element }, &trackBeginEnd))
            {
                trackElement = lastGood;
                break;
            }

            moveSlowIt = !moveSlowIt;
            if (moveSlowIt)
            {
                if (!trackBlockGetPrevious({ slowIt.end_x, slowIt.end_y, slowIt.begin_element }, &slowIt)
                    || slowIt.begin_element == trackBeginEnd.begin_element)
                {
                    break;
                }
            }
        } while (initial_map != trackBeginEnd.begin_element);
    }
    *output = trackElement;
}

/**
 *
 *  rct2: 0x00696707
 */
void Ride::stopGuestsQueuing()
{
    for (auto peep : EntityList<Guest>())
    {
        if (peep->CurrentRide != id)
            continue;

        if (peep->State == PeepState::queuing)
        {
            peep->removeFromQueue();
            peep->SetState(PeepState::falling);
        }
        else if (RideGetStationPlatformReservation(*this, peep->CurrentRideStation, peep->id).has_value())
        {
            peep->recoverFromStationPlatform(*this);
        }
    }
    RideClearStationPlatformPreQueue(*this);
}

RideMode Ride::getDefaultMode() const
{
    return getRideTypeDescriptor().DefaultMode;
}

static bool RideTypeWithTrackColoursExists(ride_type_t rideType, const TrackColour& colours)
{
    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        if (ride.type != rideType)
            continue;
        if (ride.trackColours[0].main != colours.main)
            continue;
        if (ride.trackColours[0].additional != colours.additional)
            continue;
        if (ride.trackColours[0].supports != colours.supports)
            continue;

        return true;
    }
    return false;
}

bool Ride::nameExists(std::string_view name, RideId excludeRideId)
{
    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        if (ride.id != excludeRideId)
        {
            Formatter ft;
            ride.formatNameTo(ft);

            char buffer[256]{};
            FormatStringLegacy(buffer, 256, STR_STRINGID, ft.Data());
            if (name == buffer && RideHasAnyTrackElements(ride))
            {
                return true;
            }
        }
    }
    return false;
}

int32_t RideGetRandomColourPresetIndex(ride_type_t rideType)
{
    if (rideType >= std::size(kRideTypeDescriptors))
    {
        return 0;
    }

    // Find all the presets that haven't yet been used in the park for this ride type
    const auto& colourPresets = GetRideTypeDescriptor(rideType).ColourPresets;
    std::vector<uint8_t> unused;
    unused.reserve(colourPresets.count);
    for (uint8_t i = 0; i < colourPresets.count; i++)
    {
        const auto& colours = colourPresets.list[i];
        if (!RideTypeWithTrackColoursExists(rideType, colours))
        {
            unused.push_back(static_cast<uint8_t>(i));
        }
    }

    // If all presets have been used, just go with a random preset
    if (unused.empty())
        return UtilRand() % colourPresets.count;

    // Choose a random preset from the list of unused presets
    auto unusedIndex = UtilRand() % unused.size();
    return unused[unusedIndex];
}

/**
 *
 *  Based on rct2: 0x006B4776
 */
void Ride::setColourPreset(uint8_t trackColourPreset, uint8_t vehicleColourPreset)
{
    const TrackColourPresetList* colourPresets = &getRideTypeDescriptor().ColourPresets;
    TrackColour colours = { OpenRCT2::Drawing::Colour::black, OpenRCT2::Drawing::Colour::black,
                            OpenRCT2::Drawing::Colour::black };
    // Stalls save their default colour in the vehicle settings (since they share a common ride type)
    if (!isRide())
    {
        const auto* rideEntry = GetRideEntryByIndex(subtype);
        if (rideEntry != nullptr && rideEntry->vehicle_preset_list->count > 0)
        {
            if (vehicleColourPreset < rideEntry->vehicle_preset_list->count)
            {
                auto list = rideEntry->vehicle_preset_list->list[vehicleColourPreset];
                colours = { list.Body, list.Trim, list.Tertiary };
            }
        }
    }
    else if (trackColourPreset < colourPresets->count)
    {
        colours = colourPresets->list[trackColourPreset];
    }
    for (size_t i = 0; i < std::size(trackColours); i++)
    {
        trackColours[i].main = colours.main;
        trackColours[i].additional = colours.additional;
        trackColours[i].supports = colours.supports;
    }
    vehicleColourSettings = VehicleColourSettings::same;
}

money64 RideGetCommonPrice(const Ride& forRide)
{
    auto& gameState = getGameState();
    for (const auto& ride : RideManager(gameState))
    {
        if (ride.type == forRide.type && ride.id != forRide.id)
        {
            return ride.price[0];
        }
    }

    return kMoney64Undefined;
}

void Ride::setNameToDefault()
{
    char rideNameBuffer[256]{};

    // Increment default name number until we find a unique name
    customName = {};
    defaultNameNumber = 0;
    do
    {
        defaultNameNumber++;
        Formatter ft;
        formatNameTo(ft);
        FormatStringLegacy(rideNameBuffer, 256, STR_STRINGID, ft.Data());
    } while (nameExists(rideNameBuffer, id));
}

/**
 * This will return the name of the ride, as seen in the New Ride window.
 */
RideNaming GetRideNaming(const ride_type_t rideType, const RideObjectEntry* rideEntry)
{
    const auto& rtd = GetRideTypeDescriptor(rideType);
    if (rtd.flags.has(RtdFlag::listVehiclesSeparately) && rideEntry != nullptr)
    {
        return rideEntry->naming;
    }

    return rtd.Naming;
}

/*
 * The next eight functions are helpers to access ride data at the offset 10E &
 * 110. Known as the turn counts. There are 3 different types (default, banked, sloped)
 * and there are 4 counts as follows:
 *
 * 1 element turns: low 5 bits
 * 2 element turns: bits 6-8
 * 3 element turns: bits 9-11
 * 4 element or more turns: bits 12-15
 *
 * 4 plus elements only possible on sloped type. Falls back to 3 element
 * if by some miracle you manage 4 element none sloped.
 */

void IncrementTurnCount1Element(Ride& ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride.turnCountDefault;
            break;
        case 1:
            turn_count = &ride.turnCountBanked;
            break;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & kTurnMask1Element) + 1;
    *turn_count &= ~kTurnMask1Element;

    if (value > kTurnMask1Element)
        value = kTurnMask1Element;
    *turn_count |= value;
}

void IncrementTurnCount2Elements(Ride& ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride.turnCountDefault;
            break;
        case 1:
            turn_count = &ride.turnCountBanked;
            break;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & kTurnMask2Elements) + 0x20;
    *turn_count &= ~kTurnMask2Elements;

    if (value > kTurnMask2Elements)
        value = kTurnMask2Elements;
    *turn_count |= value;
}

void IncrementTurnCount3Elements(Ride& ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride.turnCountDefault;
            break;
        case 1:
            turn_count = &ride.turnCountBanked;
            break;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & kTurnMask3Elements) + 0x100;
    *turn_count &= ~kTurnMask3Elements;

    if (value > kTurnMask3Elements)
        value = kTurnMask3Elements;
    *turn_count |= value;
}

void IncrementTurnCount4PlusElements(Ride& ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
        case 1:
            // Just in case fallback to 3 element turn
            IncrementTurnCount3Elements(ride, type);
            return;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & kTurnMask4PlusElements) + 0x800;
    *turn_count &= ~kTurnMask4PlusElements;

    if (value > kTurnMask4PlusElements)
        value = kTurnMask4PlusElements;
    *turn_count |= value;
}

int32_t GetTurnCount1Element(const Ride& ride, uint8_t type)
{
    const uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride.turnCountDefault;
            break;
        case 1:
            turn_count = &ride.turnCountBanked;
            break;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return 0;
    }

    return (*turn_count) & kTurnMask1Element;
}

int32_t GetTurnCount2Elements(const Ride& ride, uint8_t type)
{
    const uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride.turnCountDefault;
            break;
        case 1:
            turn_count = &ride.turnCountBanked;
            break;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return 0;
    }

    return ((*turn_count) & kTurnMask2Elements) >> 5;
}

int32_t GetTurnCount3Elements(const Ride& ride, uint8_t type)
{
    const uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride.turnCountDefault;
            break;
        case 1:
            turn_count = &ride.turnCountBanked;
            break;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return 0;
    }

    return ((*turn_count) & kTurnMask3Elements) >> 8;
}

int32_t GetTurnCount4PlusElements(const Ride& ride, uint8_t type)
{
    const uint16_t* turn_count;
    switch (type)
    {
        case 0:
        case 1:
            return 0;
        case 2:
            turn_count = &ride.turnCountSloped;
            break;
        default:
            return 0;
    }

    return ((*turn_count) & kTurnMask4PlusElements) >> 11;
}

bool Ride::hasSpinningTunnel() const
{
    return specialTrackElements.has(SpecialElement::spinningTunnel);
}

bool Ride::hasWaterSplash() const
{
    return specialTrackElements.has(SpecialElement::splash);
}

bool Ride::hasRapids() const
{
    return specialTrackElements.has(SpecialElement::rapids);
}

bool Ride::hasLogReverser() const
{
    return specialTrackElements.has(SpecialElement::reverser);
}

bool Ride::hasWaterfall() const
{
    return specialTrackElements.has(SpecialElement::waterfall);
}

bool Ride::hasWhirlpool() const
{
    return specialTrackElements.has(SpecialElement::whirlpool);
}

bool Ride::isPoweredLaunched() const
{
    return mode == RideMode::poweredLaunchPasstrough || mode == RideMode::poweredLaunch
        || mode == RideMode::poweredLaunchBlockSectioned;
}

bool Ride::isBlockSectioned() const
{
    return mode == RideMode::continuousCircuitBlockSectioned || mode == RideMode::poweredLaunchBlockSectioned;
}

bool RideHasAnyTrackElements(const Ride& ride)
{
    TileElementIterator it;

    TileElementIteratorBegin(&it);
    while (TileElementIteratorNext(&it))
    {
        if (it.element->getType() != TileElementType::Track)
            continue;
        if (it.element->asTrack()->GetRideIndex() != ride.id)
            continue;
        if (it.element->isGhost())
            continue;

        return true;
    }

    return false;
}

/**
 *
 *  rct2: 0x006B59C6
 */
void InvalidateTestResults(Ride& ride)
{
    ride.measurement = {};
    ride.ratings.setNull();
    ride.ratingAccumulator.clear();
    RideClearRiderRatingSamples(ride);
    ride.flags.unset(RideFlag::tested, RideFlag::testInProgress);
    ride.currentTestVehicle = EntityId::GetNull();
    if (ride.flags.has(RideFlag::onTrack))
    {
        for (int32_t i = 0; i < ride.numTrains; i++)
        {
            Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[i]);
            if (vehicle != nullptr)
            {
                vehicle->flags.unset(VehicleFlag::testing);
            }
        }
    }

    auto* windowMgr = Ui::GetWindowManager();
    windowMgr->InvalidateByNumber(WindowClass::ride, ride.id.ToUnderlying());
}

/**
 *
 *  rct2: 0x006B7481
 *
 * @param rideIndex (dl)
 * @param reliabilityIncreaseFactor (ax)
 */
void RideFixBreakdown(Ride& ride, int32_t reliabilityIncreaseFactor)
{
    ride.flags.unset(RideFlag::breakdownPending, RideFlag::brokenDown, RideFlag::dueInspection);
    RideInvalidateTransportServiceCache(ride.id);
    ride.windowInvalidateFlags.set(RideInvalidateFlag::main, RideInvalidateFlag::list, RideInvalidateFlag::maintenance);

    if (ride.flags.has(RideFlag::onTrack))
    {
        for (int32_t i = 0; i < ride.numTrains; i++)
        {
            for (Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[i]); vehicle != nullptr;
                 vehicle = getGameState().entities.GetEntity<Vehicle>(vehicle->next_vehicle_on_train))
            {
                vehicle->flags.unset(VehicleFlag::stoppedBySafetyCutout, VehicleFlag::carIsBroken, VehicleFlag::trainIsBroken);
            }
        }
    }

    uint8_t unreliability = 100 - ride.reliabilityPercentage;
    ride.reliability += reliabilityIncreaseFactor * (unreliability / 2);
}

/**
 *
 *  rct2: 0x006DE102
 */
void RideUpdateVehicleColours(const Ride& ride)
{
    const auto& rtd = ride.getRideTypeDescriptor();
    if (rtd.specialType == RtdSpecialType::spaceRings || rtd.flags.has(RtdFlag::vehicleIsIntegral))
    {
        GfxInvalidateScreen();
    }

    auto& entities = getGameState().entities;
    for (int32_t i = 0; i <= Limits::kMaxTrainsPerRide; i++)
    {
        int32_t carIndex = 0;
        for (Vehicle* vehicle = entities.GetEntity<Vehicle>(ride.vehicles[i]); vehicle != nullptr;
             vehicle = entities.GetEntity<Vehicle>(vehicle->next_vehicle_on_train))
        {
            VehicleColour colours = {};
            switch (ride.vehicleColourSettings)
            {
                case VehicleColourSettings::same:
                    colours = ride.vehicleColours[0];
                    break;
                case VehicleColourSettings::perTrain:
                    colours = ride.vehicleColours[i];
                    break;
                case VehicleColourSettings::perCar:
                {
                    const bool isReversed = vehicle->flags.has(VehicleFlag::carIsReversed);
                    const auto colourIndex = isReversed ? (ride.numCarsPerTrain - 1) - carIndex : carIndex;
                    colours = ride.vehicleColours[std::min(colourIndex, Limits::kMaxCarsPerTrain - 1)];
                    break;
                }
            }

            vehicle->colours = colours;
            vehicle->invalidate();
            carIndex++;
        }
    }
}

uint8_t RideEntryGetVehicleAtPosition(int32_t rideEntryIndex, int32_t numCarsPerTrain, int32_t position)
{
    const auto* rideEntry = GetRideEntryByIndex(rideEntryIndex);
    if (position == 0 && rideEntry->FrontCar != 255)
    {
        return rideEntry->FrontCar;
    }
    if (position == 1 && rideEntry->SecondCar != 255)
    {
        return rideEntry->SecondCar;
    }
    if (position == 2 && rideEntry->ThirdCar != 255)
    {
        return rideEntry->ThirdCar;
    }
    if (position == numCarsPerTrain - 1 && rideEntry->RearCar != 255)
    {
        return rideEntry->RearCar;
    }

    return rideEntry->DefaultCar;
}

using namespace OpenRCT2::Entity::Yaw;

struct NecessarySpriteGroup
{
    SpriteGroupType VehicleSpriteGroup;
    SpritePrecision MinPrecision;
};

// Finds track pieces that a given ride entry has sprites for
BitSet<EnumValue(TrackGroup::count)> RideEntryGetSupportedTrackPieces(const RideObjectEntry& rideEntry)
{
    // TODO: Use a std::span when C++20 available as 6 is due to jagged array
    static const std::array<NecessarySpriteGroup, 9> trackPieceRequiredSprites[] = {
        { SpriteGroupType::SlopeFlat, SpritePrecision::none },     // TrackGroup::flat
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 }, // TrackGroup::straight
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 }, // TrackGroup::stationEnd
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4 },  // TrackGroup::liftHill
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60,
          SpritePrecision::sprites4 },                             // TrackGroup::liftHillSteep
        { SpriteGroupType::Slopes25, SpritePrecision::sprites16 }, // TrackGroup::liftHillCurve
        { SpriteGroupType::FlatBanked22, SpritePrecision::sprites4, SpriteGroupType::FlatBanked45,
          SpritePrecision::sprites16 }, // TrackGroup::flatRollBanking
        { SpriteGroupType::Slopes60, SpritePrecision::sprites4, SpriteGroupType::Slopes75, SpritePrecision::sprites4,
          SpriteGroupType::Slopes90, SpritePrecision::sprites4, SpriteGroupType::SlopesLoop, SpritePrecision::sprites4,
          SpriteGroupType::SlopeInverted, SpritePrecision::sprites4 }, // TrackGroup::verticalLoop
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4 },      // TrackGroup::slope
        { SpriteGroupType::Slopes60, SpritePrecision::sprites4 },      // TrackGroup::slopeSteepDown
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60,
          SpritePrecision::sprites4 },                              // TrackGroup::flatToSteepSlope
        { SpriteGroupType::Slopes25, SpritePrecision::sprites16 },  // TrackGroup::slopeCurve
        { SpriteGroupType::Slopes60, SpritePrecision::sprites16 },  // TrackGroup::slopeCurveSteep
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 }, // TrackGroup::sBend
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 }, // TrackGroup::curveVerySmall
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 }, // TrackGroup::curveSmall
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 }, // TrackGroup::curve
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 }, // TrackGroup::curveLarge
        { SpriteGroupType::FlatBanked22, SpritePrecision::sprites4, SpriteGroupType::FlatBanked45, SpritePrecision::sprites4,
          SpriteGroupType::FlatBanked67, SpritePrecision::sprites4, SpriteGroupType::FlatBanked90, SpritePrecision::sprites4,
          SpriteGroupType::InlineTwists, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::twist
        { SpriteGroupType::Slopes60, SpritePrecision::sprites4, SpriteGroupType::Slopes75, SpritePrecision::sprites4,
          SpriteGroupType::Slopes90, SpritePrecision::sprites4, SpriteGroupType::SlopesLoop, SpritePrecision::sprites4,
          SpriteGroupType::SlopeInverted, SpritePrecision::sprites4 }, // TrackGroup::halfLoop
        { SpriteGroupType::Corkscrews, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 },                                 // TrackGroup::corkscrew
        { SpriteGroupType::SlopeFlat, SpritePrecision::none },         // TrackGroup::tower
        { SpriteGroupType::FlatBanked45, SpritePrecision::sprites16 }, // TrackGroup::helixUpBankedHalf
        { SpriteGroupType::FlatBanked45, SpritePrecision::sprites16 }, // TrackGroup::helixDownBankedHalf
        { SpriteGroupType::FlatBanked45, SpritePrecision::sprites16 }, // TrackGroup::helixUpBankedQuarter
        { SpriteGroupType::FlatBanked45, SpritePrecision::sprites16 }, // TrackGroup::helixDownBankedQuarter
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 },    // TrackGroup::helixUpUnbankedQuarter
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 },    // TrackGroup::helixDownUnbankedQuarter
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 },     // TrackGroup::brakes
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 },     // TrackGroup::onridePhoto
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4, SpriteGroupType::Slopes12,
          SpritePrecision::sprites4 }, // TrackGroup::waterSplash
        { SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 }, // TrackGroup::slopeVertical
        { SpriteGroupType::FlatBanked22, SpritePrecision::sprites4, SpriteGroupType::FlatBanked45, SpritePrecision::sprites4,
          SpriteGroupType::InlineTwists, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 },                            // TrackGroup::barrelRoll
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4 }, // TrackGroup::poweredLift
        { SpriteGroupType::Slopes60, SpritePrecision::sprites4, SpriteGroupType::Slopes75, SpritePrecision::sprites4,
          SpriteGroupType::Slopes90, SpritePrecision::sprites4, SpriteGroupType::SlopesLoop, SpritePrecision::sprites4,
          SpriteGroupType::SlopeInverted, SpritePrecision::sprites4 },     // TrackGroup::halfLoopLarge
        { SpriteGroupType::Slopes12Banked22, SpritePrecision::sprites16 }, // TrackGroup::slopeCurveBanked
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 },        // TrackGroup::logFlumeReverser
        { SpriteGroupType::FlatBanked22, SpritePrecision::sprites4, SpriteGroupType::FlatBanked45, SpritePrecision::sprites4,
          SpriteGroupType::InlineTwists, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 },                              // TrackGroup::heartlineRoll
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites16 }, // TrackGroup::reverser
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4, SpriteGroupType::Slopes25, SpritePrecision::sprites4,
          SpriteGroupType::Slopes60, SpritePrecision::sprites4, SpriteGroupType::Slopes75, SpritePrecision::sprites4,
          SpriteGroupType::Slopes90, SpritePrecision::sprites4 }, // TrackGroup::reverseFreefall
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4, SpriteGroupType::Slopes25, SpritePrecision::sprites4,
          SpriteGroupType::Slopes60, SpritePrecision::sprites4, SpriteGroupType::Slopes75, SpritePrecision::sprites4,
          SpriteGroupType::Slopes90, SpritePrecision::sprites4 },         // TrackGroup::slopeToFlat
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 },        // TrackGroup::blockBrakes
        { SpriteGroupType::Slopes25Banked22, SpritePrecision::sprites4 }, // TrackGroup::slopeRollBanking
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60,
          SpritePrecision::sprites4 },                             // TrackGroup::slopeSteepLong
        { SpriteGroupType::Slopes90, SpritePrecision::sprites16 }, // TrackGroup::curveVertical
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60,
          SpritePrecision::sprites4 },                                     // TrackGroup::liftHillCable
        { SpriteGroupType::CurvedLiftHillUp, SpritePrecision::sprites16 }, // TrackGroup::liftHillCurved
        { SpriteGroupType::Slopes90, SpritePrecision::sprites4, SpriteGroupType::SlopesLoop, SpritePrecision::sprites4,
          SpriteGroupType::SlopeInverted, SpritePrecision::sprites4 }, // TrackGroup::quarterLoop
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 },     // TrackGroup::spinningTunnel
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 },     // TrackGroup::booster
        { SpriteGroupType::FlatBanked22, SpritePrecision::sprites4, SpriteGroupType::FlatBanked45, SpritePrecision::sprites4,
          SpriteGroupType::FlatBanked67, SpritePrecision::sprites4, SpriteGroupType::FlatBanked90, SpritePrecision::sprites4,
          SpriteGroupType::InlineTwists, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::inlineTwistUninverted
        { SpriteGroupType::FlatBanked22, SpritePrecision::sprites4, SpriteGroupType::FlatBanked45, SpritePrecision::sprites4,
          SpriteGroupType::FlatBanked67, SpritePrecision::sprites4, SpriteGroupType::FlatBanked90, SpritePrecision::sprites4,
          SpriteGroupType::InlineTwists, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::inlineTwistInverted
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 }, // TrackGroup::quarterLoopUninvertedUp
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 }, // TrackGroup::quarterLoopUninvertedDown
        { SpriteGroupType::Slopes90, SpritePrecision::sprites4, SpriteGroupType::SlopesLoop, SpritePrecision::sprites4,
          SpriteGroupType::SlopeInverted, SpritePrecision::sprites4 }, // TrackGroup::quarterLoopInvertedUp
        { SpriteGroupType::Slopes90, SpritePrecision::sprites4, SpriteGroupType::SlopesLoop, SpritePrecision::sprites4,
          SpriteGroupType::SlopeInverted, SpritePrecision::sprites4 }, // TrackGroup::quarterLoopInvertedDown
        { SpriteGroupType::Slopes12, SpritePrecision::sprites4 },      // TrackGroup::rapids
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 }, // TrackGroup::flyingHalfLoopUninvertedUp
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90, SpritePrecision::sprites4,
          SpriteGroupType::SlopesLoop, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 },                             // TrackGroup::flyingHalfLoopInvertedDown
        {},                                                        // TrackGroup::flatRideBase
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 }, // TrackGroup::waterfall
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 }, // TrackGroup::whirlpool
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60,
          SpritePrecision::sprites4 }, // TrackGroup::brakeForDrop
        { SpriteGroupType::Corkscrews, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::corkscrewUninverted
        { SpriteGroupType::Corkscrews, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::corkscrewInverted
        { SpriteGroupType::Slopes12, SpritePrecision::sprites4, SpriteGroupType::Slopes25,
          SpritePrecision::sprites4 },                             // TrackGroup::heartlineTransfer
        {},                                                        // TrackGroup::miniGolfHole
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites4 }, // TrackGroup::rotationControlToggle
        { SpriteGroupType::Slopes60, SpritePrecision::sprites4 },  // TrackGroup::slopeSteepUp
        { SpriteGroupType::Corkscrews, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::corkscrewLarge
        { SpriteGroupType::Slopes60, SpritePrecision::sprites4, SpriteGroupType::Slopes75, SpritePrecision::sprites4,
          SpriteGroupType::Slopes90, SpritePrecision::sprites4, SpriteGroupType::SlopesLoop, SpritePrecision::sprites4,
          SpriteGroupType::SlopeInverted, SpritePrecision::sprites4 }, // TrackGroup::halfLoopMedium
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes12Banked22, SpritePrecision::sprites4,
          SpriteGroupType::Slopes25Banked22, SpritePrecision::sprites4, SpriteGroupType::Slopes25Banked45,
          SpritePrecision::sprites4, SpriteGroupType::InlineTwists, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::zeroGRoll
        { SpriteGroupType::Slopes42Banked22, SpritePrecision::sprites4, SpriteGroupType::Slopes42Banked45,
          SpritePrecision::sprites4, SpriteGroupType::Slopes42Banked67, SpritePrecision::sprites4,
          SpriteGroupType::Slopes42Banked90, SpritePrecision::sprites4, SpriteGroupType::Slopes60Banked22,
          SpritePrecision::sprites4 }, // TrackGroup::zeroGRollLarge
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 }, // TrackGroup::flyingLargeHalfLoopUninvertedUp
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90, SpritePrecision::sprites4,
          SpriteGroupType::SlopesLoop, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::flyingLargeHalfLoopInvertedDown
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90, SpritePrecision::sprites4,
          SpriteGroupType::SlopesLoop, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::flyingLargeHalfLoopUninvertedDown
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 }, // TrackGroup::flyingLargeHalfLoopInvertedUp
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 }, // TrackGroup::flyingHalfLoopInvertedUp
        { SpriteGroupType::Slopes25, SpritePrecision::sprites4, SpriteGroupType::Slopes60, SpritePrecision::sprites4,
          SpriteGroupType::Slopes75, SpritePrecision::sprites4, SpriteGroupType::Slopes90,
          SpritePrecision::sprites4 },                                     // TrackGroup::flyingHalfLoopUninvertedDown
        { SpriteGroupType::Slopes25, SpritePrecision::sprites16 },         // TrackGroup::slopeCurveLarge
        { SpriteGroupType::Slopes25Banked45, SpritePrecision::sprites16 }, // TrackGroup::slopeCurveLargeBanked
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites8 },         // TrackGroup::diagBrakes
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites8 },         // TrackGroup::diagBlockBrakes
        { SpriteGroupType::Slopes25, SpritePrecision::sprites8 },          // TrackGroup::inclinedBrakes
        { SpriteGroupType::SlopeFlat, SpritePrecision::sprites8 },         // TrackGroup::diagBooster
        { SpriteGroupType::Slopes8, SpritePrecision::sprites4, SpriteGroupType::Slopes16, SpritePrecision::sprites4,
          SpriteGroupType::Slopes25, SpritePrecision::sprites8, SpriteGroupType::Slopes42, SpritePrecision::sprites8,
          SpriteGroupType::Slopes50, SpritePrecision::sprites4 }, // TrackGroup::slopeSteepLong
        { SpriteGroupType::Slopes50, SpritePrecision::sprites4, SpriteGroupType::Slopes60Banked22, SpritePrecision::sprites8,
          SpriteGroupType::Slopes50Banked45, SpritePrecision::sprites8, SpriteGroupType::Slopes50Banked67,
          SpritePrecision::sprites8, SpriteGroupType::Slopes50Banked90, SpritePrecision::sprites8, SpriteGroupType::Corkscrews,
          SpritePrecision::sprites4, SpriteGroupType::Slopes25InlineTwists, SpritePrecision::sprites4,
          SpriteGroupType::SlopesLoop, SpritePrecision::sprites4, SpriteGroupType::SlopeInverted,
          SpritePrecision::sprites4 }, // TrackGroup::diveLoop
        { SpriteGroupType::Slopes8, SpritePrecision::sprites4, SpriteGroupType::Slopes16,
          SpritePrecision::sprites4 }, // TrackGroup::diagSlope
        { SpriteGroupType::Slopes25, SpritePrecision::sprites8, SpriteGroupType::Slopes42, SpritePrecision::sprites8,
          SpriteGroupType::Slopes50, SpritePrecision::sprites4 }, // TrackGroup::diagSlopeSteepUp
        { SpriteGroupType::Slopes25, SpritePrecision::sprites8, SpriteGroupType::Slopes42, SpritePrecision::sprites8,
          SpriteGroupType::Slopes50, SpritePrecision::sprites4 }, // TrackGroup::diagSlopeSteepDown
    };

    static_assert(std::size(trackPieceRequiredSprites) == EnumValue(TrackGroup::count));

    // Only check default vehicle; it's assumed the others will have correct sprites if this one does (I've yet to find an
    // exception, at least)
    auto supportedPieces = OpenRCT2::BitSet<EnumValue(TrackGroup::count)>();
    supportedPieces.flip();
    auto defaultVehicle = rideEntry.GetDefaultCar();
    if (defaultVehicle != nullptr)
    {
        for (size_t i = 0; i < std::size(trackPieceRequiredSprites); i++)
        {
            for (auto& group : trackPieceRequiredSprites[i])
            {
                auto precision = defaultVehicle->SpriteGroups[EnumValue(group.VehicleSpriteGroup)].spritePrecision;
                if (precision < group.MinPrecision)
                    supportedPieces.set(i, false);
            }
        }
    }
    return supportedPieces;
}

static std::optional<int32_t> RideGetSmallestStationLength(const Ride& ride)
{
    std::optional<int32_t> result;
    for (const auto& station : ride.getStations())
    {
        if (!station.Start.IsNull())
        {
            if (!result.has_value() || station.Length < result.value())
            {
                result = station.Length;
            }
        }
    }
    return result;
}

/**
 *
 *  rct2: 0x006CB3AA
 */
static int32_t RideGetTrackLength(const Ride& ride)
{
    TileElement* tileElement = nullptr;
    TrackElemType trackType;
    CoordsXYZ trackStart;
    bool foundTrack = false;

    for (const auto& station : ride.getStations())
    {
        trackStart = station.GetStart();
        if (trackStart.IsNull())
            continue;

        tileElement = MapGetFirstElementAt(trackStart);
        if (tileElement == nullptr)
            continue;
        do
        {
            if (tileElement->getType() != TileElementType::Track)
                continue;

            trackType = tileElement->asTrack()->GetTrackType();
            const auto& ted = GetTrackElementDescriptor(trackType);
            if (!ted.sequenceData.sequences[0].flags.has(SequenceFlag::trackOrigin))
                continue;

            if (tileElement->getBaseZ() != trackStart.z)
                continue;

            foundTrack = true;
        } while (!foundTrack && !(tileElement++)->isLastForTile());

        if (foundTrack)
            break;
    }

    if (!foundTrack)
        return 0;

    RideId rideIndex = tileElement->asTrack()->GetRideIndex();

    auto* windowMgr = Ui::GetWindowManager();
    WindowBase* w = windowMgr->FindByClass(WindowClass::rideConstruction);
    if (w != nullptr && _rideConstructionState != RideConstructionState::State0 && _currentRideIndex == rideIndex)
    {
        RideConstructionInvalidateCurrentTrack();
    }

    bool moveSlowIt = true;
    int32_t result = 0;

    TrackCircuitIterator it;
    trackCircuitIteratorBegin(&it, { trackStart.x, trackStart.y, tileElement });

    TrackCircuitIterator slowIt = it;
    while (trackCircuitIteratorNext(&it))
    {
        trackType = it.current.element->asTrack()->GetTrackType();
        const auto& ted = GetTrackElementDescriptor(trackType);
        result += ted.pieceLength;

        moveSlowIt = !moveSlowIt;
        if (moveSlowIt)
        {
            trackCircuitIteratorNext(&slowIt);
            if (trackCircuitIteratorsMatch(&it, &slowIt))
            {
                return 0;
            }
        }
    }
    return result;
}

uint8_t Ride::getMazeMaximumCapacity() const
{
    if (type != RIDE_TYPE_MAZE)
        return 0;

    return getMazeCapacityForMode(MazeCapacityMode::overcrowded);
}

MazeCapacityMode RideNormaliseMazeCapacityMode(uint8_t operationOption)
{
    switch (static_cast<MazeCapacityMode>(operationOption))
    {
        case MazeCapacityMode::sparse:
        case MazeCapacityMode::normal:
        case MazeCapacityMode::overcrowded:
            return static_cast<MazeCapacityMode>(operationOption);
    }

    return MazeCapacityMode::normal;
}

uint8_t Ride::getMazeCapacityForMode(MazeCapacityMode capacityMode) const
{
    if (type != RIDE_TYPE_MAZE || mazeTiles == 0)
        return 0;

    uint32_t capacity = mazeTiles;
    switch (capacityMode)
    {
        case MazeCapacityMode::sparse:
            capacity = std::max<uint32_t>(1, capacity / 2);
            break;
        case MazeCapacityMode::normal:
            break;
        case MazeCapacityMode::overcrowded:
            capacity *= 2;
            break;
    }

    return static_cast<uint8_t>(std::min<uint32_t>(capacity, std::numeric_limits<uint8_t>::max()));
}

MazeCapacityMode Ride::getMazeCapacityMode() const
{
    if (type != RIDE_TYPE_MAZE)
        return MazeCapacityMode::normal;

    return RideNormaliseMazeCapacityMode(operationOption);
}

MazeCapacityMode Ride::getClosestMazeCapacityModeForCapacity(uint8_t capacity) const
{
    if (type != RIDE_TYPE_MAZE || mazeTiles == 0)
        return MazeCapacityMode::normal;

    auto capacityDifference = [this, capacity](MazeCapacityMode mode) {
        return std::abs(static_cast<int32_t>(capacity) - getMazeCapacityForMode(mode));
    };

    auto bestMode = MazeCapacityMode::normal;
    auto bestDifference = capacityDifference(bestMode);
    auto considerMode = [&bestMode, &bestDifference, capacityDifference](MazeCapacityMode mode) {
        auto difference = capacityDifference(mode);
        if (difference < bestDifference)
        {
            bestMode = mode;
            bestDifference = difference;
        }
    };

    considerMode(MazeCapacityMode::sparse);
    considerMode(MazeCapacityMode::overcrowded);
    return bestMode;
}

std::pair<int32_t, int32_t> Ride::getMazeRatingAccumulatorScale() const
{
    if (type != RIDE_TYPE_MAZE)
        return { 1, 1 };

    switch (getMazeCapacityMode())
    {
        case MazeCapacityMode::sparse:
            return { 2, 1 };
        case MazeCapacityMode::normal:
            return { 1, 1 };
        case MazeCapacityMode::overcrowded:
            return { 1, 2 };
    }

    return { 1, 1 };
}

uint8_t Ride::getOperationOptionMinimum(bool unlockOperatingLimits) const
{
    if (type == RIDE_TYPE_MAZE)
        return static_cast<uint8_t>(MazeCapacityMode::sparse);

    if (unlockOperatingLimits)
        return 0;

    return getRideTypeDescriptor().OperatingSettings.MinValue;
}

uint8_t Ride::getOperationOptionMaximum(bool unlockOperatingLimits) const
{
    if (type == RIDE_TYPE_MAZE)
        return static_cast<uint8_t>(MazeCapacityMode::overcrowded);

    if (unlockOperatingLimits)
        return Limits::kCheatsMaxOperatingLimit;

    return getRideTypeDescriptor().OperatingSettings.MaxValue;
}

uint8_t Ride::getDefaultOperationOption() const
{
    if (type == RIDE_TYPE_MAZE)
        return static_cast<uint8_t>(MazeCapacityMode::normal);

    const auto& operatingSettings = getRideTypeDescriptor().OperatingSettings;
    return (operatingSettings.MinValue * 3 + operatingSettings.MaxValue) / 4;
}

uint8_t Ride::getStoredOperationOption() const
{
    if (type == RIDE_TYPE_MAZE)
        return static_cast<uint8_t>(getMazeCapacityMode());

    return operationOption;
}

uint8_t Ride::getEffectiveOperationOption() const
{
    if (type == RIDE_TYPE_MAZE)
        return getMazeCapacityForMode(getMazeCapacityMode());

    return operationOption;
}

void Ride::normaliseMazeCapacityMode()
{
    if (type != RIDE_TYPE_MAZE)
        return;

    operationOption = getStoredOperationOption();
}

void Ride::updateMazeCapacityForConstruction()
{
    normaliseMazeCapacityMode();
    if (type != RIDE_TYPE_MAZE)
        return;

    windowInvalidateFlags.set(RideInvalidateFlag::operatingSettings);
}

/**
 *
 *  rct2: 0x006DD57D
 */
void Ride::updateMaxVehicles()
{
    if (subtype == kObjectEntryIndexNull)
        return;

    const auto* rideEntry = GetRideEntryByIndex(subtype);
    if (rideEntry == nullptr)
    {
        return;
    }

    uint8_t newNumCarsPerTrain;
    int32_t maxNumTrains;

    const auto& rtd = getRideTypeDescriptor();
    if (rideEntry->cars_per_flat_ride == kNoFlatRideCars)
    {
        newNumCarsPerTrain = std::max(rideEntry->min_cars_in_train, numCarsPerTrain);
        minCarsPerTrain = rideEntry->min_cars_in_train;
        maxCarsPerTrain = rideEntry->max_cars_in_train;

        // Calculate maximum train length based on smallest station length
        auto stationNumTiles = RideGetSmallestStationLength(*this);
        if (!stationNumTiles.has_value())
            return;

        auto stationLength = (stationNumTiles.value() * 0x44180) - 0x16B2A;
        int32_t maxMass = rtd.MaxMass << 8;
        int32_t newMaxCarsPerTrain = 1;
        for (int32_t numCars = rideEntry->max_cars_in_train; numCars > 0; numCars--)
        {
            int32_t trainLength = 0;
            int32_t totalMass = 0;
            for (int32_t i = 0; i < numCars; i++)
            {
                const auto& carEntry = rideEntry->Cars[RideEntryGetVehicleAtPosition(subtype, numCars, i)];
                trainLength += carEntry.spacing;
                totalMass += carEntry.car_mass;
            }

            if (trainLength <= stationLength && totalMass <= maxMass)
            {
                newMaxCarsPerTrain = numCars;
                break;
            }
        }
        int32_t newCarsPerTrain = std::max(proposedNumCarsPerTrain, rideEntry->min_cars_in_train);
        newMaxCarsPerTrain = std::max(newMaxCarsPerTrain, static_cast<int32_t>(rideEntry->min_cars_in_train));
        if (!getGameState().cheats.disableTrainLengthLimit)
        {
            newCarsPerTrain = std::min(newMaxCarsPerTrain, newCarsPerTrain);
        }
        maxCarsPerTrain = newMaxCarsPerTrain;
        minCarsPerTrain = rideEntry->min_cars_in_train;

        switch (mode)
        {
            case RideMode::continuousCircuitBlockSectioned:
            case RideMode::poweredLaunchBlockSectioned:
                maxNumTrains = std::clamp<int32_t>(numStations + numBlockBrakes - 1, 1, Limits::kMaxTrainsPerRide);
                break;
            case RideMode::reverseInclineLaunchedShuttle:
            case RideMode::poweredLaunchPasstrough:
            case RideMode::shuttle:
            case RideMode::limPoweredLaunch:
            case RideMode::poweredLaunch:
                maxNumTrains = 1;
                break;
            default:
                // Calculate maximum number of trains
                int32_t trainLength = 0;
                for (int32_t i = 0; i < newCarsPerTrain; i++)
                {
                    const auto& carEntry = rideEntry->Cars[RideEntryGetVehicleAtPosition(subtype, newCarsPerTrain, i)];
                    trainLength += carEntry.spacing;
                }

                int32_t totalLength = trainLength / 2;
                if (newCarsPerTrain != 1)
                    totalLength /= 2;

                maxNumTrains = 0;
                do
                {
                    maxNumTrains++;
                    totalLength += trainLength;
                } while (totalLength <= stationLength);

                if ((mode != RideMode::stationToStation && mode != RideMode::continuousCircuit)
                    || !rtd.flags.has(RtdFlag::allowMoreVehiclesThanStationFits))
                {
                    maxNumTrains = std::min(maxNumTrains, int32_t(Limits::kMaxTrainsPerRide));
                }
                else
                {
                    const auto& firstCarEntry = rideEntry->Cars[RideEntryGetVehicleAtPosition(subtype, newCarsPerTrain, 0)];
                    int32_t poweredMaxSpeed = firstCarEntry.powered_max_speed;

                    int32_t totalSpacing = 0;
                    for (int32_t i = 0; i < newCarsPerTrain; i++)
                    {
                        const auto& carEntry = rideEntry->Cars[RideEntryGetVehicleAtPosition(subtype, newCarsPerTrain, i)];
                        totalSpacing += carEntry.spacing;
                    }

                    totalSpacing >>= 13;
                    int32_t trackLength = RideGetTrackLength(*this) / 4;
                    if (poweredMaxSpeed > 10)
                        trackLength = (trackLength * 3) / 4;
                    if (poweredMaxSpeed > 25)
                        trackLength = (trackLength * 3) / 4;
                    if (poweredMaxSpeed > 40)
                        trackLength = (trackLength * 3) / 4;

                    maxNumTrains = 0;
                    int32_t length = 0;
                    do
                    {
                        maxNumTrains++;
                        length += totalSpacing;
                    } while (maxNumTrains < Limits::kMaxTrainsPerRide && length < trackLength);
                }
                break;
        }
        maxTrains = maxNumTrains;

        newNumCarsPerTrain = std::min(proposedNumCarsPerTrain, static_cast<uint8_t>(newCarsPerTrain));
    }
    else
    {
        maxTrains = rideEntry->cars_per_flat_ride;
        minCarsPerTrain = rideEntry->min_cars_in_train;
        maxCarsPerTrain = rideEntry->max_cars_in_train;
        newNumCarsPerTrain = rideEntry->max_cars_in_train;
        maxNumTrains = rideEntry->cars_per_flat_ride;
    }

    if (getGameState().cheats.disableTrainLengthLimit)
    {
        maxNumTrains = Limits::kMaxTrainsPerRide;
    }
    auto newNumTrains = std::min(proposedNumTrains, static_cast<uint8_t>(maxNumTrains));

    // Refresh new current num vehicles / num cars per vehicle
    if (newNumTrains != numTrains || newNumCarsPerTrain != numCarsPerTrain)
    {
        numCarsPerTrain = newNumCarsPerTrain;
        numTrains = newNumTrains;

        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->InvalidateByNumber(WindowClass::ride, id.ToUnderlying());
    }
}

void Ride::updateNumberOfCircuits()
{
    if (!canHaveMultipleCircuits())
    {
        numCircuits = 1;
    }
}

void Ride::setRideEntry(ObjectEntryIndex entryIndex)
{
    auto colour = RideGetUnusedPresetVehicleColour(entryIndex, UtilRand());
    auto rideSetVehicleAction = GameActions::RideSetVehicleAction(
        id, GameActions::RideSetVehicleType::rideEntry, entryIndex, colour);
    GameActions::Execute(&rideSetVehicleAction, getGameState());
}

void Ride::setNumTrains(int32_t newNumTrains)
{
    auto rideSetVehicleAction = GameActions::RideSetVehicleAction(id, GameActions::RideSetVehicleType::numTrains, newNumTrains);
    GameActions::Execute(&rideSetVehicleAction, getGameState());
}

void Ride::setNumCarsPerTrain(int32_t numCarsPerVehicle)
{
    auto rideSetVehicleAction = GameActions::RideSetVehicleAction(
        id, GameActions::RideSetVehicleType::numCarsPerTrain, numCarsPerVehicle);
    GameActions::Execute(&rideSetVehicleAction, getGameState());
}

void Ride::setReversedTrains(bool reverseTrains)
{
    auto rideSetVehicleAction = GameActions::RideSetVehicleAction(
        id, GameActions::RideSetVehicleType::trainsReversed, reverseTrains);
    GameActions::Execute(&rideSetVehicleAction, getGameState());
}

/**
 *
 *  rct2: 0x006B752C
 */
void Ride::crash(uint8_t vehicleIndex)
{
    Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(vehicles[vehicleIndex]);

    if (gLegacyScene != LegacyScene::titleSequence && vehicle != nullptr)
    {
        // Open ride window for crashed vehicle
        auto intent = Intent(WindowDetail::vehicle);
        intent.PutExtra(INTENT_EXTRA_VEHICLE, vehicle);
        WindowBase* w = ContextOpenIntent(&intent);

        Viewport* viewport = WindowGetViewport(w);
        if (w != nullptr && viewport != nullptr)
        {
            viewport->flags |= VIEWPORT_FLAG_SOUND_ON;
        }
    }

    if (Config::Get().notifications.rideCrashed)
    {
        Formatter ft;
        formatNameTo(ft);
        News::AddItemToQueue(News::ItemType::ride, STR_RIDE_HAS_CRASHED, id.ToUnderlying(), ft);
    }
}

// Gets the approximate value of customers per hour for this ride. Multiplies ride_customers_in_last_5_minutes() by 12.
uint32_t RideCustomersPerHour(const Ride& ride)
{
    return RideCustomersInLast5Minutes(ride) * 12;
}

// Calculates the number of customers for this ride in the last 5 real minutes.
uint32_t RideCustomersInLast5Minutes(const Ride& ride)
{
    uint32_t sum = 0;

    for (int32_t i = 0; i < Limits::kCustomerHistorySize; i++)
    {
        sum += ride.numCustomers[i];
    }

    return sum;
}

Vehicle* RideGetBrokenVehicle(const Ride& ride)
{
    auto vehicleIndex = ride.vehicles[ride.brokenTrain];
    Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(vehicleIndex);
    if (vehicle != nullptr)
    {
        return vehicle->GetCar(ride.brokenCar);
    }
    return nullptr;
}

/**
 *
 *  rct2: 0x006D235B
 */
void Ride::remove()
{
    RideDelete(id);
}

void Ride::renew()
{
    // Set build date to current date (so the ride is brand new)
    buildDate = RideGetCurrentBuildDate();
    reliability = kRideInitialReliability;
    std::fill(std::begin(downtimeHistory), std::end(downtimeHistory), 0);
    downtime = 0;
}

RideClassification Ride::getClassification() const
{
    const auto& rtd = getRideTypeDescriptor();
    return rtd.Classification;
}

bool Ride::isRide() const
{
    return getClassification() == RideClassification::ride;
}

namespace
{
    constexpr money64 kRidePriceGoodValueMinMargin = 0.05_GBP;
    constexpr money64 kRidePriceNeutralMinMargin = 0.05_GBP;
    constexpr money64 kRidePriceBadValueMinMargin = 0.05_GBP;
    constexpr int64_t kRideTargetPriceScaleNumerator = 7;
    constexpr int64_t kRideTargetPriceScaleDenominator = 10;

    money64 RideGetGuestFacingValue(const Ride& ride)
    {
        auto value = ride.value;
        const auto& park = getGameState().park;
        if ((park.flags & PARK_FLAGS_UNLOCK_ALL_PRICES) && Park::GetEntranceFee(park) > 0
            && !(park.flags & PARK_FLAGS_PARK_FREE_ENTRY))
        {
            value /= 4;
        }
        return std::max(0.00_GBP, value);
    }

    money64 RidePriceBelowBoundary(money64 boundary, money64 minMargin, int32_t marginDivisor = 10)
    {
        if (boundary <= 0.00_GBP)
        {
            return 0.00_GBP;
        }

        const auto margin = std::max(minMargin, boundary / marginDivisor);
        return boundary > margin ? boundary - margin : 0.00_GBP;
    }

    money64 RideClampAdmissionPrice(money64 price)
    {
        return std::clamp(price, kRideMinPrice, kRideMaxPrice);
    }

    money64 RideApplyTargetPriceScale(money64 price)
    {
        return (price * kRideTargetPriceScaleNumerator) / kRideTargetPriceScaleDenominator;
    }
} // namespace

bool RideUsesTargetPricing(const Ride& ride)
{
    if (!ride.isRide())
    {
        return false;
    }

    const auto& rtd = ride.getRideTypeDescriptor();
    if (rtd.flags.has(RtdFlag::isTransportRide))
    {
        return true;
    }
    if (rtd.flags.has(RtdFlag::isShopOrFacility) || rtd.specialType == RtdSpecialType::toilet)
    {
        return false;
    }

    auto rideEntry = ride.getRideEntry();
    if (rideEntry == nullptr)
    {
        return false;
    }

    return rideEntry->shop_item[0] == ShopItem::none;
}

money64 RideGetTargetPrice(const Ride& ride, RidePriceTarget target)
{
    if (target == RidePriceTarget::free)
    {
        return 0.00_GBP;
    }
    if (ride.value == kRideValueUndefined)
    {
        return ride.price[0];
    }

    const auto value = RideGetGuestFacingValue(ride);
    money64 price = 0.00_GBP;
    switch (target)
    {
        case RidePriceTarget::goodValue:
            price = RidePriceBelowBoundary(value / 2, kRidePriceGoodValueMinMargin);
            break;
        case RidePriceTarget::neutral:
            price = RidePriceBelowBoundary(value, kRidePriceNeutralMinMargin);
            break;
        case RidePriceTarget::badValue:
            price = RidePriceBelowBoundary(value * 2, kRidePriceBadValueMinMargin, 20);
            break;
        case RidePriceTarget::free:
            break;
    }

    return RideClampAdmissionPrice(RideApplyTargetPriceScale(price));
}

void RideUpdateTargetPrice(Ride& ride)
{
    if (!RideUsesTargetPricing(ride))
    {
        return;
    }

    // Transport admission is calculated for the selected directed station
    // journey. There is intentionally no single ride-wide ticket price to set.
    if (ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
    {
        ride.windowInvalidateFlags.set(RideInvalidateFlag::income);
        return;
    }

    const auto& park = getGameState().park;
    money64 price = ride.price[0];
    if ((park.flags & PARK_FLAGS_NO_MONEY) || !Park::RidePricesUnlocked(park))
    {
        price = 0.00_GBP;
    }
    else if (ride.value != kRideValueUndefined)
    {
        price = RideGetTargetPrice(ride, ride.priceTarget);
    }

    if (ride.price[0] != price)
    {
        ride.price[0] = price;
        ride.windowInvalidateFlags.set(RideInvalidateFlag::income);
    }
}

money64 RideGetPrice(const Ride& ride)
{
    auto& park = getGameState().park;
    if (park.flags & PARK_FLAGS_NO_MONEY)
        return 0;
    if (ride.isRide())
    {
        if (!Park::RidePricesUnlocked(park))
        {
            return 0;
        }
    }
    if (ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide) && ride.numStations >= 2)
    {
        const auto boarding = StationIndex::FromUnderlying(0);
        const auto destination = StationIndex::FromUnderlying(1);
        return RideGetTransportFare(ride, RideGetTransportJourney(ride, boarding, destination));
    }
    return ride.price[0];
}

money64 RideGetTransportFare(const Ride& ride, const TransportRideJourney& journey)
{
    if (journey.destinationStation.IsNull() || journey.segmentCount == 0)
    {
        return 0.00_GBP;
    }

    switch (ride.priceTarget)
    {
        case RidePriceTarget::free:
            return 0.00_GBP;
        case RidePriceTarget::goodValue:
            return std::clamp<money64>(journey.fareValue / 2, kRideMinPrice, kRideMaxPrice);
        case RidePriceTarget::neutral:
            return std::clamp<money64>(journey.fareValue, kRideMinPrice, kRideMaxPrice);
        case RidePriceTarget::badValue:
            return std::clamp<money64>(journey.fareValue * 2, kRideMinPrice, kRideMaxPrice);
    }
    return 0.00_GBP;
}

TransportRideQuality RideGetTransportQuality(const Ride& ride)
{
    const auto sample = RideGetRecentRatingAccumulator(ride);
    if (sample.transportDistance <= 0)
    {
        return {};
    }

    return {
        .comfortPermille = static_cast<int32_t>(
            std::clamp<int64_t>(sample.transportComfort / sample.transportDistance, 100, 1000)),
        .decorationPermille = static_cast<int32_t>(
            std::clamp<int64_t>(sample.transportDecoration / sample.transportDistance, 1000, 1500)),
        .hasMeasurements = true,
    };
}

TransportRideQuality RideGetTransportLegQuality(
    const RideRatingLeg& leg, const TransportRideQuality& fallback)
{
    const auto sample = RideGetRecentRatingAccumulator(leg);
    if (sample.transportDistance <= 0)
    {
        return fallback;
    }
    return {
        .comfortPermille = static_cast<int32_t>(
            std::clamp<int64_t>(sample.transportComfort / sample.transportDistance, 100, 1000)),
        .decorationPermille = static_cast<int32_t>(
            std::clamp<int64_t>(sample.transportDecoration / sample.transportDistance, 1000, 1500)),
        .hasMeasurements = true,
    };
}

TransportRideSegment RideGetTransportSegment(const Ride& ride, StationIndex boardingStation)
{
    return RideGetTransportSegment(ride, boardingStation, RideGetTransportQuality(ride));
}

static TransportRideSegment RideGetTransportSegmentForEdge(
    const Ride& ride, StationIndex boardingStation, const RideRatingLeg* measuredLeg,
    const TransportRideQuality& quality)
{
    TransportRideSegment result{};
    if (ride.numStations < 2 || boardingStation.IsNull() || boardingStation.ToUnderlying() >= ride.numStations)
    {
        return result;
    }
    const auto legMeasurements = measuredLeg == nullptr ? RideRatingLegMeasurements{}
                                                        : RideGetRatingLegMeasurements(*measuredLeg);
    if (measuredLeg != nullptr)
    {
        result.destinationStation = measuredLeg->destinationStation;
    }
    else
    {
        result.destinationStation = StationIndex::FromUnderlying((boardingStation.ToUnderlying() + 1) % ride.numStations);
    }

    int64_t distanceMetres = legMeasurements.distanceMetres;
    if (distanceMetres <= 0)
    {
        distanceMetres = ToHumanReadableRideLength(ride.getDisplayStationSegmentLength(boardingStation));
    }
    if (distanceMetres <= 0)
    {
        const auto& start = ride.getStation(boardingStation).Start;
        const auto& end = ride.getStation(result.destinationStation).Start;
        if (!start.IsNull() && !end.IsNull())
        {
            const auto approximateTiles = std::abs(start.x - end.x) + std::abs(start.y - end.y);
            distanceMetres = std::max<int64_t>(1, approximateTiles * 4);
        }
        else
        {
            distanceMetres = 1;
        }
    }
    result.distanceMetres = static_cast<int32_t>(std::min<int64_t>(distanceMetres, INT32_MAX));

    auto speedMph = ToHumanReadableSpeed(legMeasurements.averageSpeed);
    if (speedMph <= 0)
    {
        speedMph = ToHumanReadableSpeed(ride.getDisplayAverageSpeed());
    }
    if (speedMph <= 0)
    {
        speedMph = ToHumanReadableSpeed(
            legMeasurements.maxSpeed > 0 ? legMeasurements.maxSpeed : ride.getDisplayMaxSpeed());
    }
    if (legMeasurements.durationTicks > 0)
    {
        result.travelTimeMilliseconds = static_cast<int64_t>(legMeasurements.durationTicks) * 1000
            / GameTime::kTicksPerSecond;
    }
    else if (const auto measuredTimeSeconds = ride.getDisplayStationSegmentTime(boardingStation); measuredTimeSeconds > 0)
    {
        result.travelTimeMilliseconds = static_cast<int64_t>(measuredTimeSeconds) * 1000;
    }
    else
    {
        // 1 mile = 1609 metres. A 3 mph floor gives untested services a
        // conservative estimate while retaining real time as the unit.
        constexpr int64_t kMillisecondsPerHour = 3'600'000;
        constexpr int64_t kMetresPerMile = 1609;
        result.travelTimeMilliseconds = (distanceMetres * kMillisecondsPerHour)
            / (std::max<int64_t>(speedMph, 3) * kMetresPerMile);
    }

    const auto speedFactor = std::clamp<int64_t>((std::max(speedMph, 1) * 1000) / 12, 600, 1800);
    const auto distanceValue = std::max<int64_t>(20, distanceMetres / 2);

    const auto segmentQuality = measuredLeg == nullptr ? quality : RideGetTransportLegQuality(*measuredLeg, quality);
    const auto value = (((distanceValue * speedFactor) / 1000) * segmentQuality.comfortPermille / 1000)
        * segmentQuality.decorationPermille / 1000;
    result.fareValue = std::clamp<money64>(static_cast<money64>(value), 0.20_GBP, kRideMaxPrice);
    return result;
}

TransportRideSegment RideGetTransportSegment(
    const Ride& ride, StationIndex boardingStation, const TransportRideQuality& quality)
{
    const auto* measuredLeg = RideGetUniqueOutboundRatingLeg(ride, boardingStation);
    if (measuredLeg == nullptr)
    {
        const auto hasAmbiguousMeasuredDeparture = std::any_of(
            ride.ratingLegs.begin(), ride.ratingLegs.end(), [boardingStation](const auto& leg) {
                return leg.originStation == boardingStation && leg.hasSamples();
            });
        if (hasAmbiguousMeasuredDeparture)
        {
            // A shuttle can leave the same station in either direction. There is no
            // honest single-segment answer without a requested destination.
            return {};
        }
    }
    return RideGetTransportSegmentForEdge(ride, boardingStation, measuredLeg, quality);
}

TransportRideSegment RideGetTransportSegment(
    const Ride& ride, StationIndex boardingStation, StationIndex destinationStation,
    const TransportRideQuality& quality)
{
    const auto* measuredLeg = RideGetRatingLeg(ride, boardingStation, destinationStation);
    return measuredLeg == nullptr || !measuredLeg->hasSamples()
        ? TransportRideSegment{}
        : RideGetTransportSegmentForEdge(ride, boardingStation, measuredLeg, quality);
}

using TransportRideSegmentGraph = std::vector<std::vector<TransportRideSegment>>;

static TransportRideSegmentGraph RideBuildTransportSegmentGraph(
    const Ride& ride, const TransportRideQuality& quality)
{
    TransportRideSegmentGraph graph(ride.numStations);
    for (StationIndex::UnderlyingType origin = 0; origin < ride.numStations; origin++)
    {
        const auto originStation = StationIndex::FromUnderlying(origin);
        for (const auto& leg : ride.ratingLegs)
        {
            if (leg.originStation == originStation && leg.hasSamples())
            {
                auto segment = RideGetTransportSegmentForEdge(ride, originStation, &leg, quality);
                if (!segment.destinationStation.IsNull())
                {
                    graph[origin].push_back(segment);
                }
            }
        }
        if (graph[origin].empty())
        {
            auto segment = RideGetTransportSegmentForEdge(ride, originStation, nullptr, quality);
            if (!segment.destinationStation.IsNull())
            {
                graph[origin].push_back(segment);
            }
        }
        std::sort(graph[origin].begin(), graph[origin].end(), [](const auto& left, const auto& right) {
            return left.destinationStation.ToUnderlying() < right.destinationStation.ToUnderlying();
        });
    }
    return graph;
}

static TransportRideJourney RideFindTransportJourney(
    const TransportRideSegmentGraph& graph, StationIndex boardingStation, StationIndex destinationStation)
{
    const auto stationCount = graph.size();
    if (boardingStation.IsNull() || destinationStation.IsNull() || boardingStation == destinationStation
        || boardingStation.ToUnderlying() >= stationCount || destinationStation.ToUnderlying() >= stationCount)
    {
        return {};
    }

    constexpr auto kUnreachable = std::numeric_limits<int64_t>::max();
    std::vector<int64_t> bestTime(stationCount, kUnreachable);
    std::vector<int32_t> bestDistance(stationCount);
    std::vector<money64> bestFare(stationCount);
    std::vector<uint8_t> bestSegments(stationCount);
    std::vector<bool> visited(stationCount);
    bestTime[boardingStation.ToUnderlying()] = 0;

    for (size_t iteration = 0; iteration < stationCount; iteration++)
    {
        size_t current = stationCount;
        for (size_t candidate = 0; candidate < stationCount; candidate++)
        {
            if (!visited[candidate] && bestTime[candidate] != kUnreachable
                && (current == stationCount || bestTime[candidate] < bestTime[current]
                    || (bestTime[candidate] == bestTime[current] && bestFare[candidate] < bestFare[current])))
            {
                current = candidate;
            }
        }
        if (current == stationCount)
        {
            break;
        }
        visited[current] = true;
        for (const auto& segment : graph[current])
        {
            const auto next = segment.destinationStation.ToUnderlying();
            if (next >= stationCount || visited[next])
            {
                continue;
            }
            const auto candidateTime = AddClamp<int64_t>(bestTime[current], segment.travelTimeMilliseconds);
            const auto candidateFare = AddClamp<money64>(bestFare[current], segment.fareValue);
            if (candidateTime < bestTime[next]
                || (candidateTime == bestTime[next] && candidateFare < bestFare[next]))
            {
                bestTime[next] = candidateTime;
                bestDistance[next] = AddClamp<int32_t>(bestDistance[current], segment.distanceMetres);
                bestFare[next] = candidateFare;
                bestSegments[next] = AddClamp<uint8_t>(bestSegments[current], static_cast<uint8_t>(1));
            }
        }
    }

    const auto destination = destinationStation.ToUnderlying();
    if (bestTime[destination] == kUnreachable || bestSegments[destination] == 0)
    {
        return {};
    }
    return {
        .destinationStation = destinationStation,
        .segmentCount = bestSegments[destination],
        .distanceMetres = bestDistance[destination],
        .travelTimeMilliseconds = bestTime[destination],
        .fareValue = bestFare[destination],
    };
}

TransportRideJourney RideGetTransportJourney(const Ride& ride, StationIndex boardingStation, StationIndex destinationStation)
{
    return RideGetTransportJourney(ride, boardingStation, destinationStation, RideGetTransportQuality(ride));
}

TransportRideJourney RideGetTransportJourney(
    const Ride& ride, StationIndex boardingStation, StationIndex destinationStation, const TransportRideQuality& quality)
{
    if (ride.numStations < 2 || boardingStation.IsNull() || destinationStation.IsNull()
        || boardingStation.ToUnderlying() >= ride.numStations || destinationStation.ToUnderlying() >= ride.numStations
        || boardingStation == destinationStation)
    {
        return {};
    }
    return RideFindTransportJourney(RideBuildTransportSegmentGraph(ride, quality), boardingStation, destinationStation);
}

namespace
{
    struct TransportRideServiceFreshness
    {
        uint64_t signature{};
        TransportRideQuality quality;
    };

    struct CachedTransportRideService
    {
        RideId ride{ RideId::GetNull() };
        bool available{};
        uint64_t freshnessSignature{};
        TransportRideQuality quality;
        std::vector<TransportRideServiceStation> stations;
        std::vector<TransportRideServiceJourney> journeys;
    };

    struct TransportRideServiceSpatialBucket
    {
        std::vector<TransportRideServiceStationRef> boardingEntrances;
        std::vector<TransportRideServiceStationRef> destinationExits;
    };

    constexpr size_t kTransportServiceSpatialBucketCount = static_cast<size_t>(MapTopology::kChunkCount)
        * MapTopology::kChunkCount;

    struct TransportRideServiceSpatialIndex
    {
        std::array<TransportRideServiceSpatialBucket, kTransportServiceSpatialBucketCount> buckets;
        std::vector<TransportRideServiceStationRef> allBoardingEntrances;
        std::vector<TransportRideServiceStationRef> allDestinationExits;
    };

    struct TransportRideServiceCache
    {
        std::array<CachedTransportRideService, OpenRCT2::Limits::kMaxRidesInPark> rides;
        std::vector<RideId> availableRideIds;
        TransportRideServiceSpatialIndex spatialIndex;
        uint32_t lastValidationTick{};
        size_t lastRideRange{};
        bool hasValidatedTick{};
        bool validationForced{};
    };

    TransportRideServiceCache _transportRideServiceCache;

    constexpr uint64_t kTransportServiceHashOffset = 14695981039346656037ULL;
    constexpr uint64_t kTransportServiceHashPrime = 1099511628211ULL;

    void TransportServiceHashAppend(uint64_t& hash, uint64_t value)
    {
        // FNV-1a over fixed-width values is deterministic, inexpensive, and
        // sufficient for detecting changes in this transient cache.
        hash ^= value;
        hash *= kTransportServiceHashPrime;
    }

    void TransportServiceHashAppendLocation(uint64_t& hash, const TileCoordsXYZD& location)
    {
        TransportServiceHashAppend(hash, static_cast<uint64_t>(static_cast<int64_t>(location.x)));
        TransportServiceHashAppend(hash, static_cast<uint64_t>(static_cast<int64_t>(location.y)));
        TransportServiceHashAppend(hash, static_cast<uint64_t>(static_cast<int64_t>(location.z)));
        TransportServiceHashAppend(hash, location.direction);
    }

    std::vector<TransportRideServiceStationRef>& TransportServiceGetEndpointRefs(
        TransportRideServiceSpatialBucket& bucket, TransportRideServiceEndpoint endpoint)
    {
        return endpoint == TransportRideServiceEndpoint::boardingEntrance ? bucket.boardingEntrances : bucket.destinationExits;
    }

    const std::vector<TransportRideServiceStationRef>& TransportServiceGetEndpointRefs(
        const TransportRideServiceSpatialBucket& bucket, TransportRideServiceEndpoint endpoint)
    {
        return endpoint == TransportRideServiceEndpoint::boardingEntrance ? bucket.boardingEntrances : bucket.destinationExits;
    }

    std::vector<TransportRideServiceStationRef>& TransportServiceGetAllEndpointRefs(
        TransportRideServiceSpatialIndex& spatialIndex, TransportRideServiceEndpoint endpoint)
    {
        return endpoint == TransportRideServiceEndpoint::boardingEntrance ? spatialIndex.allBoardingEntrances
                                                                          : spatialIndex.allDestinationExits;
    }

    const std::vector<TransportRideServiceStationRef>& TransportServiceGetAllEndpointRefs(
        const TransportRideServiceSpatialIndex& spatialIndex, TransportRideServiceEndpoint endpoint)
    {
        return endpoint == TransportRideServiceEndpoint::boardingEntrance ? spatialIndex.allBoardingEntrances
                                                                          : spatialIndex.allDestinationExits;
    }

    bool TransportServiceTryGetSpatialBucket(const TileCoordsXYZD& location, size_t& bucketIndex)
    {
        if (location.IsNull() || location.x < 0 || location.y < 0 || location.x >= kMaximumMapSizeTechnical
            || location.y >= kMaximumMapSizeTechnical)
        {
            return false;
        }

        const auto chunkX = location.x / MapTopology::kChunkSize;
        const auto chunkY = location.y / MapTopology::kChunkSize;
        bucketIndex = static_cast<size_t>(chunkY) * MapTopology::kChunkCount + chunkX;
        return true;
    }

    const TileCoordsXYZD* TransportServiceGetEndpointLocation(
        const CachedTransportRideService& cached, StationIndex station, TransportRideServiceEndpoint endpoint)
    {
        if (station.IsNull() || station.ToUnderlying() >= cached.stations.size())
        {
            return nullptr;
        }
        const auto& serviceStation = cached.stations[station.ToUnderlying()];
        return endpoint == TransportRideServiceEndpoint::boardingEntrance ? &serviceStation.entrance : &serviceStation.exit;
    }

    void TransportServiceInsertRef(std::vector<TransportRideServiceStationRef>& refs, const TransportRideServiceStationRef& ref)
    {
        const auto iterator = std::lower_bound(refs.begin(), refs.end(), ref);
        if (iterator == refs.end() || !(*iterator == ref))
        {
            refs.insert(iterator, ref);
        }
    }

    void TransportServiceEraseRef(std::vector<TransportRideServiceStationRef>& refs, const TransportRideServiceStationRef& ref)
    {
        const auto iterator = std::lower_bound(refs.begin(), refs.end(), ref);
        if (iterator != refs.end() && *iterator == ref)
        {
            refs.erase(iterator);
        }
    }

    void RideUpdateTransportServiceSpatialIndex(const CachedTransportRideService& cached, bool insert)
    {
        if (cached.ride.IsNull())
        {
            return;
        }

        auto& spatialIndex = _transportRideServiceCache.spatialIndex;
        for (size_t stationIndex = 0; stationIndex < cached.stations.size(); stationIndex++)
        {
            const auto station = StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(stationIndex));
            const TransportRideServiceStationRef ref{ .ride = cached.ride, .station = station };
            for (const auto endpoint :
                 { TransportRideServiceEndpoint::boardingEntrance, TransportRideServiceEndpoint::destinationExit })
            {
                const auto* location = TransportServiceGetEndpointLocation(cached, station, endpoint);
                size_t bucketIndex = 0;
                if (location == nullptr || !TransportServiceTryGetSpatialBucket(*location, bucketIndex))
                {
                    continue;
                }

                auto& bucketRefs = TransportServiceGetEndpointRefs(spatialIndex.buckets[bucketIndex], endpoint);
                auto& allRefs = TransportServiceGetAllEndpointRefs(spatialIndex, endpoint);
                if (insert)
                {
                    TransportServiceInsertRef(bucketRefs, ref);
                    TransportServiceInsertRef(allRefs, ref);
                }
                else
                {
                    TransportServiceEraseRef(bucketRefs, ref);
                    TransportServiceEraseRef(allRefs, ref);
                }
            }
        }
    }

    bool RideIsAvailableTransportService(const Ride& ride)
    {
        return ride.type != kRideTypeNull && ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide)
            && ride.status == RideStatus::open && !ride.flags.has(RideFlag::brokenDown) && ride.numStations >= 2;
    }

    TransportRideServiceFreshness RideGetTransportServiceFreshness(const Ride& ride)
    {
        TransportRideServiceFreshness result{ .signature = kTransportServiceHashOffset };
        result.quality = RideGetTransportQuality(ride);

        TransportServiceHashAppend(result.signature, ride.id.ToUnderlying());
        TransportServiceHashAppend(result.signature, ride.type);
        TransportServiceHashAppend(result.signature, static_cast<uint64_t>(ride.status));
        TransportServiceHashAppend(result.signature, ride.flags.has(RideFlag::brokenDown));
        TransportServiceHashAppend(result.signature, ride.numStations);
        TransportServiceHashAppend(
            result.signature, static_cast<uint64_t>(static_cast<int64_t>(ride.getDisplayAverageSpeed())));
        TransportServiceHashAppend(result.signature, static_cast<uint64_t>(static_cast<int64_t>(ride.getDisplayMaxSpeed())));
        TransportServiceHashAppend(
            result.signature, static_cast<uint64_t>(static_cast<int64_t>(result.quality.comfortPermille)));
        TransportServiceHashAppend(
            result.signature, static_cast<uint64_t>(static_cast<int64_t>(result.quality.decorationPermille)));
        TransportServiceHashAppend(result.signature, result.quality.hasMeasurements);

        for (size_t stationIndex = 0; stationIndex < ride.numStations; stationIndex++)
        {
            const auto index = StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(stationIndex));
            const auto& station = ride.getStation(index);
            TransportServiceHashAppend(result.signature, static_cast<uint64_t>(static_cast<int64_t>(station.Start.x)));
            TransportServiceHashAppend(result.signature, static_cast<uint64_t>(static_cast<int64_t>(station.Start.y)));
            TransportServiceHashAppendLocation(result.signature, station.Entrance);
            TransportServiceHashAppendLocation(result.signature, station.Exit);
            TransportServiceHashAppend(
                result.signature, static_cast<uint64_t>(static_cast<int64_t>(ride.getDisplayStationSegmentLength(index))));
            TransportServiceHashAppend(result.signature, ride.getDisplayStationSegmentTime(index));
        }
        TransportServiceHashAppend(result.signature, ride.ratingLegs.size());
        for (const auto& leg : ride.ratingLegs)
        {
            if (!leg.hasSamples())
            {
                continue;
            }
            const auto segment = RideGetTransportSegmentForEdge(ride, leg.originStation, &leg, result.quality);
            TransportServiceHashAppend(result.signature, leg.originStation.ToUnderlying());
            TransportServiceHashAppend(result.signature, leg.destinationStation.ToUnderlying());
            TransportServiceHashAppend(result.signature, leg.recentSampleCount);
            TransportServiceHashAppend(
                result.signature, static_cast<uint64_t>(static_cast<int64_t>(segment.distanceMetres)));
            TransportServiceHashAppend(
                result.signature, static_cast<uint64_t>(segment.travelTimeMilliseconds));
            TransportServiceHashAppend(result.signature, static_cast<uint64_t>(segment.fareValue));
        }
        return result;
    }

    void RideBuildTransportService(
        CachedTransportRideService& cached, const Ride& ride, const TransportRideServiceFreshness& freshness)
    {
        if (cached.available)
        {
            RideUpdateTransportServiceSpatialIndex(cached, false);
        }

        const auto stationCount = static_cast<size_t>(ride.numStations);
        const auto segmentGraph = RideBuildTransportSegmentGraph(ride, freshness.quality);

        cached.stations.resize(stationCount);
        for (size_t stationIndex = 0; stationIndex < stationCount; stationIndex++)
        {
            const auto index = StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(stationIndex));
            const auto& station = ride.getStation(index);
            cached.stations[stationIndex] = { .entrance = station.Entrance, .exit = station.Exit };
        }

        cached.journeys.clear();
        cached.journeys.reserve(stationCount * (stationCount - 1));
        for (size_t boardingIndex = 0; boardingIndex < stationCount; boardingIndex++)
        {
            const auto boardingStation = StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(boardingIndex));
            for (size_t destinationIndex = 0; destinationIndex < stationCount; destinationIndex++)
            {
                if (destinationIndex == boardingIndex)
                {
                    continue;
                }
                const auto destinationStation =
                    StationIndex::FromUnderlying(static_cast<StationIndex::UnderlyingType>(destinationIndex));
                const auto journey = RideFindTransportJourney(segmentGraph, boardingStation, destinationStation);
                if (journey.segmentCount == 0)
                {
                    continue;
                }
                cached.journeys.push_back({ .boardingStation = boardingStation, .journey = journey });
            }
        }

        cached.ride = ride.id;
        cached.available = true;
        cached.freshnessSignature = freshness.signature;
        cached.quality = freshness.quality;
        RideUpdateTransportServiceSpatialIndex(cached, true);
    }

    void RideDeactivateTransportService(CachedTransportRideService& cached)
    {
        if (cached.available)
        {
            RideUpdateTransportServiceSpatialIndex(cached, false);
        }
        cached.ride = RideId::GetNull();
        cached.available = false;
        cached.freshnessSignature = 0;
        cached.quality = {};
        cached.stations.clear();
        cached.journeys.clear();
    }

    void RideRebuildAvailableTransportServiceIds(size_t rideRange)
    {
        auto& cache = _transportRideServiceCache;
        cache.availableRideIds.clear();
        for (size_t rideIndex = 0; rideIndex < rideRange; rideIndex++)
        {
            if (cache.rides[rideIndex].available)
            {
                cache.availableRideIds.push_back(RideId::FromUnderlying(static_cast<RideId::UnderlyingType>(rideIndex)));
            }
        }
    }

    void RideValidateTransportServiceCache()
    {
        auto& cache = _transportRideServiceCache;
        const auto& gameState = getGameState();
        if (!cache.validationForced && cache.hasValidatedTick && cache.lastValidationTick == gameState.currentTicks)
        {
            return;
        }

        cache.validationForced = false;
        cache.hasValidatedTick = true;
        cache.lastValidationTick = gameState.currentTicks;
        const auto rideRange = gameState.ridesEndOfUsedRange;
        const auto validationRange = std::max(rideRange, cache.lastRideRange);
        bool availableRidesChanged = false;

        for (size_t rideIndex = 0; rideIndex < validationRange; rideIndex++)
        {
            auto& cached = cache.rides[rideIndex];
            const Ride* ride = rideIndex < rideRange ? &gameState.rides[rideIndex] : nullptr;
            const bool available = ride != nullptr && !ride->id.IsNull() && RideIsAvailableTransportService(*ride);
            if (!available)
            {
                if (cached.available)
                {
                    RideDeactivateTransportService(cached);
                    availableRidesChanged = true;
                }
                continue;
            }

            const auto freshness = RideGetTransportServiceFreshness(*ride);
            if (!cached.available || cached.freshnessSignature != freshness.signature)
            {
                availableRidesChanged = availableRidesChanged || !cached.available;
                RideBuildTransportService(cached, *ride, freshness);
            }
        }

        cache.lastRideRange = rideRange;
        if (availableRidesChanged)
        {
            RideRebuildAvailableTransportServiceIds(rideRange);
        }
    }

    bool TransportServiceLocationIsInQuery(
        const TileCoordsXYZD& location, int32_t minimumX, int32_t minimumY, int32_t maximumX, int32_t maximumY,
        const TileCoordsXY* radiusCentre, int64_t radiusSquared)
    {
        if (location.x < minimumX || location.y < minimumY || location.x > maximumX || location.y > maximumY)
        {
            return false;
        }
        if (radiusCentre == nullptr)
        {
            return true;
        }

        const auto deltaX = static_cast<int64_t>(location.x) - radiusCentre->x;
        const auto deltaY = static_cast<int64_t>(location.y) - radiusCentre->y;
        return deltaX * deltaX + deltaY * deltaY <= radiusSquared;
    }

    bool TransportServiceClampQueryBounds(
        int64_t rawMinimumX, int64_t rawMinimumY, int64_t rawMaximumX, int64_t rawMaximumY, int32_t& minimumX,
        int32_t& minimumY, int32_t& maximumX, int32_t& maximumY)
    {
        if (rawMaximumX < 0 || rawMaximumY < 0 || rawMinimumX >= kMaximumMapSizeTechnical
            || rawMinimumY >= kMaximumMapSizeTechnical)
        {
            return false;
        }

        minimumX = static_cast<int32_t>(std::clamp<int64_t>(rawMinimumX, 0, kMaximumMapSizeTechnical - 1));
        minimumY = static_cast<int32_t>(std::clamp<int64_t>(rawMinimumY, 0, kMaximumMapSizeTechnical - 1));
        maximumX = static_cast<int32_t>(std::clamp<int64_t>(rawMaximumX, 0, kMaximumMapSizeTechnical - 1));
        maximumY = static_cast<int32_t>(std::clamp<int64_t>(rawMaximumY, 0, kMaximumMapSizeTechnical - 1));
        return minimumX <= maximumX && minimumY <= maximumY;
    }

    TransportRideServiceStationQuery RideQueryTransportServiceStations(
        TransportRideServiceEndpoint endpoint, int64_t rawMinimumX, int64_t rawMinimumY, int64_t rawMaximumX,
        int64_t rawMaximumY, const TileCoordsXY* radiusCentre, int64_t radiusSquared,
        std::vector<TransportRideServiceStationRef>& reusableBuffer, TransportRideServiceQueryFallback fallback)
    {
        RideValidateTransportServiceCache();
        const auto& spatialIndex = _transportRideServiceCache.spatialIndex;
        const auto& allRefs = TransportServiceGetAllEndpointRefs(spatialIndex, endpoint);
        reusableBuffer.clear();
        if (reusableBuffer.capacity() < allRefs.size())
        {
            reusableBuffer.reserve(allRefs.size());
        }

        int32_t minimumX = 0;
        int32_t minimumY = 0;
        int32_t maximumX = 0;
        int32_t maximumY = 0;
        const bool hasMapIntersection = TransportServiceClampQueryBounds(
            rawMinimumX, rawMinimumY, rawMaximumX, rawMaximumY, minimumX, minimumY, maximumX, maximumY);
        if (hasMapIntersection)
        {
            const auto minimumChunkX = minimumX / MapTopology::kChunkSize;
            const auto minimumChunkY = minimumY / MapTopology::kChunkSize;
            const auto maximumChunkX = maximumX / MapTopology::kChunkSize;
            const auto maximumChunkY = maximumY / MapTopology::kChunkSize;
            for (auto chunkY = minimumChunkY; chunkY <= maximumChunkY; chunkY++)
            {
                for (auto chunkX = minimumChunkX; chunkX <= maximumChunkX; chunkX++)
                {
                    const auto bucketIndex = static_cast<size_t>(chunkY) * MapTopology::kChunkCount + chunkX;
                    const auto& refs = TransportServiceGetEndpointRefs(spatialIndex.buckets[bucketIndex], endpoint);
                    for (const auto& ref : refs)
                    {
                        const auto& cached = _transportRideServiceCache.rides[ref.ride.ToUnderlying()];
                        const auto* location = TransportServiceGetEndpointLocation(cached, ref.station, endpoint);
                        if (location != nullptr
                            && TransportServiceLocationIsInQuery(
                                *location, minimumX, minimumY, maximumX, maximumY, radiusCentre, radiusSquared))
                        {
                            reusableBuffer.push_back(ref);
                        }
                    }
                }
            }
            std::sort(reusableBuffer.begin(), reusableBuffer.end());
        }

        bool usedFallback = false;
        if (reusableBuffer.empty() && fallback == TransportRideServiceQueryFallback::allServicesWhenEmpty)
        {
            reusableBuffer.assign(allRefs.begin(), allRefs.end());
            usedFallback = true;
        }
        return { .stations = reusableBuffer, .usedAllServicesFallback = usedFallback };
    }
} // namespace

static void RideTransportServiceCacheReset()
{
    auto& cache = _transportRideServiceCache;
    for (auto& cached : cache.rides)
    {
        cached.ride = RideId::GetNull();
        cached.available = false;
        cached.freshnessSignature = 0;
        cached.quality = {};
        cached.stations.clear();
        cached.journeys.clear();
    }
    for (auto& bucket : cache.spatialIndex.buckets)
    {
        bucket.boardingEntrances.clear();
        bucket.destinationExits.clear();
    }
    cache.spatialIndex.allBoardingEntrances.clear();
    cache.spatialIndex.allDestinationExits.clear();
    cache.availableRideIds.clear();
    cache.lastValidationTick = 0;
    cache.lastRideRange = 0;
    cache.hasValidatedTick = false;
    cache.validationForced = false;
}

void RideInvalidateTransportServiceCache(RideId rideId)
{
    RideRating::InvalidateLiveSynchronisationCache(rideId);
    if (rideId.IsNull() || rideId.ToUnderlying() >= _transportRideServiceCache.rides.size())
    {
        return;
    }

    auto& cached = _transportRideServiceCache.rides[rideId.ToUnderlying()];
    const auto* ride = GetRide(rideId);
    if (!cached.available && ride != nullptr && ride->type != kRideTypeNull
        && !ride->getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
    {
        return;
    }
    RideDeactivateTransportService(cached);
    const auto iterator = std::lower_bound(
        _transportRideServiceCache.availableRideIds.begin(), _transportRideServiceCache.availableRideIds.end(), rideId);
    if (iterator != _transportRideServiceCache.availableRideIds.end() && *iterator == rideId)
    {
        _transportRideServiceCache.availableRideIds.erase(iterator);
    }
    _transportRideServiceCache.validationForced = true;
}

bool TransportRideServiceView::isAvailable() const
{
    return !ride.IsNull();
}

bool TransportRideServiceStationRef::operator==(const TransportRideServiceStationRef& other) const
{
    return ride == other.ride && station == other.station;
}

bool TransportRideServiceStationRef::operator<(const TransportRideServiceStationRef& other) const
{
    return ride == other.ride ? station.ToUnderlying() < other.station.ToUnderlying()
                              : ride.ToUnderlying() < other.ride.ToUnderlying();
}

const TransportRideServiceJourney* TransportRideServiceView::getJourney(
    StationIndex boardingStation, StationIndex destinationStation) const
{
    const auto stationCount = stations.size();
    if (stationCount < 2 || boardingStation.IsNull() || destinationStation.IsNull())
    {
        return nullptr;
    }

    const auto boardingIndex = static_cast<size_t>(boardingStation.ToUnderlying());
    const auto destinationIndex = static_cast<size_t>(destinationStation.ToUnderlying());
    if (boardingIndex >= stationCount || destinationIndex >= stationCount || boardingIndex == destinationIndex)
    {
        return nullptr;
    }

    const auto key = std::pair{ boardingStation.ToUnderlying(), destinationStation.ToUnderlying() };
    const auto result = std::lower_bound(journeys.begin(), journeys.end(), key, [](const auto& journey, const auto& candidate) {
        return std::pair{
                   journey.boardingStation.ToUnderlying(), journey.journey.destinationStation.ToUnderlying() }
            < candidate;
    });
    return result != journeys.end() && result->boardingStation == boardingStation
            && result->journey.destinationStation == destinationStation
        ? &*result
        : nullptr;
}

std::span<const RideId> RideGetTransportServiceRideIds()
{
    RideValidateTransportServiceCache();
    return _transportRideServiceCache.availableRideIds;
}

TransportRideServiceView RideGetTransportService(RideId rideId)
{
    RideValidateTransportServiceCache();
    if (rideId.IsNull() || rideId.ToUnderlying() >= _transportRideServiceCache.rides.size())
    {
        return {};
    }

    const auto& cached = _transportRideServiceCache.rides[rideId.ToUnderlying()];
    if (!cached.available)
    {
        return {};
    }
    return {
        .ride = rideId,
        .freshnessSignature = cached.freshnessSignature,
        .quality = cached.quality,
        .stations = cached.stations,
        .journeys = cached.journeys,
    };
}

TransportRideServiceStationQuery RideQueryTransportServiceStationsInBounds(
    TransportRideServiceEndpoint endpoint, const TileCoordsXY& minimum, const TileCoordsXY& maximum,
    std::vector<TransportRideServiceStationRef>& reusableBuffer, TransportRideServiceQueryFallback fallback)
{
    return RideQueryTransportServiceStations(
        endpoint, std::min<int64_t>(minimum.x, maximum.x), std::min<int64_t>(minimum.y, maximum.y),
        std::max<int64_t>(minimum.x, maximum.x), std::max<int64_t>(minimum.y, maximum.y), nullptr, 0, reusableBuffer, fallback);
}

TransportRideServiceStationQuery RideQueryTransportServiceStationsInRadius(
    TransportRideServiceEndpoint endpoint, const TileCoordsXY& centre, int32_t radiusTiles,
    std::vector<TransportRideServiceStationRef>& reusableBuffer, TransportRideServiceQueryFallback fallback)
{
    const auto radius = static_cast<int64_t>(std::max(radiusTiles, 0));
    return RideQueryTransportServiceStations(
        endpoint, static_cast<int64_t>(centre.x) - radius, static_cast<int64_t>(centre.y) - radius,
        static_cast<int64_t>(centre.x) + radius, static_cast<int64_t>(centre.y) + radius, &centre, radius * radius,
        reusableBuffer, fallback);
}

TransportRideServiceStationQuery RideCollectAllTransportServiceStations(
    TransportRideServiceEndpoint endpoint, std::vector<TransportRideServiceStationRef>& reusableBuffer)
{
    RideValidateTransportServiceCache();
    const auto& refs = TransportServiceGetAllEndpointRefs(_transportRideServiceCache.spatialIndex, endpoint);
    if (reusableBuffer.capacity() < refs.size())
    {
        reusableBuffer.reserve(refs.size());
    }
    reusableBuffer.assign(refs.begin(), refs.end());
    return { .stations = reusableBuffer };
}

namespace
{
    struct StationPlatformSlot
    {
        RideStationPlatformReservation reservation;
        EntityId guestId{ EntityId::GetNull() };
        uint64_t queueSequence{};
        bool hasWaitPosition{};
    };

    struct StationPlatformState
    {
        std::vector<StationPlatformSlot> slots;
        bool active{};
        uint64_t nextQueueSequence{};
    };

    std::unordered_map<uint32_t, StationPlatformState> _stationPlatformStates;

    uint32_t GetStationPlatformKey(const Ride& ride, StationIndex stationIndex)
    {
        return (static_cast<uint32_t>(ride.id.ToUnderlying()) << 8) | stationIndex.ToUnderlying();
    }

    StationPlatformState* GetStationPlatformState(const Ride& ride, StationIndex stationIndex)
    {
        const auto it = _stationPlatformStates.find(GetStationPlatformKey(ride, stationIndex));
        return it == _stationPlatformStates.end() ? nullptr : &it->second;
    }

    const StationPlatformState* GetStationPlatformStateConst(const Ride& ride, StationIndex stationIndex)
    {
        const auto it = _stationPlatformStates.find(GetStationPlatformKey(ride, stationIndex));
        return it == _stationPlatformStates.end() ? nullptr : &it->second;
    }

    bool IsSupportedTransportPlatformType(ride_type_t rideType)
    {
        // Lift is intentionally absent: its cabin uses loading waypoints and its
        // departing state lasts until the cabin reaches the tower top.
        return rideType == RIDE_TYPE_MINIATURE_RAILWAY || rideType == RIDE_TYPE_MONORAIL
            || rideType == RIDE_TYPE_SUSPENDED_MONORAIL || rideType == RIDE_TYPE_CHAIRLIFT;
    }

    std::optional<CoordsXYZ> GetPlatformWaitPosition(
        const Ride& ride, StationIndex stationIndex, const Vehicle& car, const CarEntry& carEntry, uint8_t seatIndex)
    {
        const auto& station = ride.getStation(stationIndex);
        if (station.Entrance.IsNull() || station.Exit.IsNull() || station.Exit.direction >= kNumOrthogonalDirections
            || car.orientation / 8 >= kNumOrthogonalDirections)
        {
            return std::nullopt;
        }

        auto position = station.Entrance.ToCoordsXYZD().ToTileCentre();
        const auto entranceDirection = station.Entrance.direction;
        if (entranceDirection >= kNumOrthogonalDirections)
        {
            return std::nullopt;
        }
        const int32_t entranceOffset = carEntry.flags.has(CarEntryFlag::isChairlift) ? 32 : 21;
        position.x += DirectionOffsets[entranceDirection].x * entranceOffset;
        position.y += DirectionOffsets[entranceDirection].y * entranceOffset;
        position.z = station.GetBaseZ() + ride.getRideTypeDescriptor().Heights.PlatformHeight;

        int32_t loadingPosition = 0;
        if (!carEntry.peep_loading_positions.empty())
        {
            const auto index = std::min<size_t>(seatIndex, carEntry.peep_loading_positions.size() - 1);
            loadingPosition = carEntry.peep_loading_positions[index];
        }
        if (car.flags.has(VehicleFlag::carIsReversed))
        {
            loadingPosition = -loadingPosition;
        }

        switch (car.orientation / 8)
        {
            case 0:
                position.x = car.x - loadingPosition;
                break;
            case 1:
                position.y = car.y + loadingPosition;
                break;
            case 2:
                position.x = car.x + loadingPosition;
                break;
            case 3:
                position.y = car.y - loadingPosition;
                break;
        }
        return position;
    }

    bool TrainSupportsStationPlatformTemplate(const RideVehicle::StationDetail::TrainSeatSummary& train)
    {
        for (const auto* car : train.GetCars())
        {
            const auto* rideEntry = car->GetRideEntry();
            if (rideEntry == nullptr || car->vehicle_type >= std::size(rideEntry->Cars)
                || rideEntry->Cars[car->vehicle_type].flags.has(CarEntryFlag::loadingWaypoints))
            {
                return false;
            }
        }
        return true;
    }
} // namespace

bool RideSupportsStationPlatformPreQueue(const Ride& ride)
{
    if (!IsSupportedTransportPlatformType(ride.type) || !ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide))
    {
        return false;
    }
    if (ride.entranceStyle == OpenRCT2::kObjectEntryIndexNull)
    {
        return true;
    }
    if (GetContext() == nullptr)
    {
        return false;
    }
    const auto* stationObject = ride.getStationObject();
    return stationObject == nullptr || !(stationObject->Flags & StationObjectFlags::noPlatforms);
}

bool RideCaptureStationPlatformTemplate(Ride& ride, StationIndex stationIndex, const Vehicle& trainHead)
{
    if (!RideSupportsStationPlatformPreQueue(ride) || stationIndex.IsNull()
        || stationIndex.ToUnderlying() >= ride.numStations)
    {
        return false;
    }

    const auto train = RideVehicle::StationDetail::BuildTrainSeatSummary(trainHead);
    if (train.capacity == 0 || train.capacity > std::numeric_limits<uint16_t>::max())
    {
        return false;
    }
    if (!TrainSupportsStationPlatformTemplate(train))
    {
        return false;
    }

    StationPlatformState replacement;
    replacement.slots.reserve(train.capacity);
    uint16_t slotIndex = 0;
    uint8_t carIndex = 0;
    for (const auto* car : train.GetCars())
    {
        const auto* rideEntry = car->GetRideEntry();
        if (rideEntry == nullptr || car->vehicle_type >= std::size(rideEntry->Cars))
        {
            return false;
        }
        const auto& carEntry = rideEntry->Cars[car->vehicle_type];

        const auto seatCount = car->num_seats & kVehicleSeatNumMask;
        for (uint8_t seatIndex = 0; seatIndex < seatCount; seatIndex++)
        {
            const auto waitPosition = GetPlatformWaitPosition(ride, stationIndex, *car, carEntry, seatIndex);
            if (!waitPosition.has_value())
            {
                return false;
            }
            replacement.slots.push_back(
                { { slotIndex++, carIndex, seatIndex, waitPosition.value() }, EntityId::GetNull(), 0, true });
        }
        carIndex++;
    }

    auto& state = _stationPlatformStates[GetStationPlatformKey(ride, stationIndex)];
    const auto hasOccupants = std::any_of(state.slots.begin(), state.slots.end(), [](const auto& slot) {
        return !slot.guestId.IsNull();
    });
    if (hasOccupants)
    {
        if (state.slots.size() != replacement.slots.size())
        {
            return false;
        }
        for (size_t i = 0; i < state.slots.size(); i++)
        {
            replacement.slots[i].guestId = state.slots[i].guestId;
            replacement.slots[i].queueSequence = state.slots[i].queueSequence;
        }
        replacement.active = state.active;
        replacement.nextQueueSequence = state.nextQueueSequence;
    }
    state = std::move(replacement);
    return true;
}

void RideActivateStationPlatformPreQueue(const Ride& ride, StationIndex stationIndex)
{
    if (auto* state = GetStationPlatformState(ride, stationIndex); state != nullptr && !state->slots.empty())
    {
        state->active = true;
    }
}

bool RideStationPlatformPreQueueIsActive(const Ride& ride, StationIndex stationIndex)
{
    const auto* state = GetStationPlatformStateConst(ride, stationIndex);
    return state != nullptr && state->active;
}

std::optional<RideStationPlatformReservation> RideReserveStationPlatformSlot(
    const Ride& ride, StationIndex stationIndex, EntityId guestId)
{
    auto* state = GetStationPlatformState(ride, stationIndex);
    if (state == nullptr || !state->active)
    {
        return std::nullopt;
    }
    for (auto& slot : state->slots)
    {
        if (slot.guestId.IsNull() && slot.hasWaitPosition)
        {
            slot.guestId = guestId;
            slot.queueSequence = ++state->nextQueueSequence;
            return slot.reservation;
        }
    }
    return std::nullopt;
}

std::optional<RideStationPlatformReservation> RideGetStationPlatformReservation(
    const Ride& ride, StationIndex stationIndex, EntityId guestId)
{
    const auto* state = GetStationPlatformStateConst(ride, stationIndex);
    if (state == nullptr)
    {
        return std::nullopt;
    }
    for (const auto& slot : state->slots)
    {
        if (slot.guestId == guestId)
        {
            return slot.reservation;
        }
    }
    return std::nullopt;
}

bool RideStationPlatformGuestIsFirst(const Ride& ride, StationIndex stationIndex, EntityId guestId)
{
    const auto* state = GetStationPlatformStateConst(ride, stationIndex);
    if (state == nullptr)
    {
        return false;
    }
    const StationPlatformSlot* first = nullptr;
    for (const auto& slot : state->slots)
    {
        if (!slot.guestId.IsNull() && (first == nullptr || slot.queueSequence < first->queueSequence))
        {
            first = &slot;
        }
    }
    return first != nullptr && first->guestId == guestId;
}

void RideReleaseStationPlatformSlot(const Ride& ride, StationIndex stationIndex, EntityId guestId)
{
    auto* state = GetStationPlatformState(ride, stationIndex);
    if (state == nullptr)
    {
        return;
    }
    for (auto& slot : state->slots)
    {
        if (slot.guestId == guestId)
        {
            slot.guestId = EntityId::GetNull();
            slot.queueSequence = 0;
            return;
        }
    }
}

void RideClearStationPlatformPreQueue(const Ride& ride)
{
    for (auto it = _stationPlatformStates.begin(); it != _stationPlatformStates.end();)
    {
        if ((it->first >> 8) == ride.id.ToUnderlying())
        {
            it = _stationPlatformStates.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void RideClearAllStationPlatformPreQueues()
{
    _stationPlatformStates.clear();
}

void RideRebuildStationPlatformPreQueues()
{
    RideClearAllStationPlatformPreQueues();
    for (auto* guest : EntityList<Guest>())
    {
        if (guest->State != PeepState::enteringRide || guest->CurrentTrain != RideStation::kNoTrain
            || (guest->RideSubState != PeepRideSubState::inEntrance
                && guest->RideSubState != PeepRideSubState::approachPlatformSlot
                && guest->RideSubState != PeepRideSubState::waitingOnPlatform))
        {
            continue;
        }
        auto* ride = GetRide(guest->CurrentRide);
        if (ride == nullptr)
        {
            guest->SetState(PeepState::falling);
            continue;
        }
        if (!RideSupportsStationPlatformPreQueue(*ride)
            || guest->CurrentRideStation.ToUnderlying() >= ride->numStations || ride->numTrains == 0)
        {
            guest->recoverFromStationPlatform(*ride);
            continue;
        }
        auto* head = getGameState().entities.GetEntity<Vehicle>(ride->vehicles[0]);
        if (head == nullptr)
        {
            guest->recoverFromStationPlatform(*ride);
            continue;
        }
        const auto train = RideVehicle::StationDetail::BuildTrainSeatSummary(*head);
        if (!TrainSupportsStationPlatformTemplate(train) || guest->CurrentCar >= train.carCount)
        {
            guest->recoverFromStationPlatform(*ride);
            continue;
        }

        uint32_t slotIndex = guest->CurrentSeat;
        for (uint8_t carIndex = 0; carIndex < guest->CurrentCar; carIndex++)
        {
            slotIndex += train.cars[carIndex]->num_seats & kVehicleSeatNumMask;
        }
        if (slotIndex >= train.capacity || train.capacity > std::numeric_limits<uint16_t>::max())
        {
            guest->recoverFromStationPlatform(*ride);
            continue;
        }

        auto& state = _stationPlatformStates[GetStationPlatformKey(*ride, guest->CurrentRideStation)];
        if (state.slots.empty())
        {
            state.slots.resize(train.capacity);
            uint16_t currentSlot = 0;
            uint8_t currentCar = 0;
            for (const auto* car : train.GetCars())
            {
                const auto seatCount = car->num_seats & kVehicleSeatNumMask;
                for (uint8_t seatIndex = 0; seatIndex < seatCount; seatIndex++)
                {
                    state.slots[currentSlot].reservation = { currentSlot, currentCar, seatIndex, {} };
                    currentSlot++;
                }
                currentCar++;
            }
        }
        auto& slot = state.slots[slotIndex];
        if (!slot.guestId.IsNull())
        {
            guest->recoverFromStationPlatform(*ride);
            continue;
        }
        const auto destination = guest->GetDestination();
        slot.reservation.waitPosition = { destination, guest->z };
        slot.guestId = guest->id;
        slot.hasWaitPosition = true;
        slot.queueSequence = (static_cast<uint64_t>(std::numeric_limits<uint16_t>::max() - guest->timeInQueue) << 16)
            | guest->id.ToUnderlying();
        state.nextQueueSequence = std::max(state.nextQueueSequence, slot.queueSequence);
        state.active = true;
    }
}

uint16_t RideGetTransportStationPlatformCapacity(const Ride& ride, StationIndex stationIndex)
{
    if (!ride.getRideTypeDescriptor().flags.has(RtdFlag::isTransportRide) || stationIndex.IsNull()
        || stationIndex.ToUnderlying() >= ride.numStations)
    {
        return 0;
    }

    if (const auto* state = GetStationPlatformStateConst(ride, stationIndex); state != nullptr && !state->slots.empty())
    {
        return static_cast<uint16_t>(state->slots.size());
    }

    if (RideSupportsStationPlatformPreQueue(ride))
    {
        for (uint8_t trainIndex = 0; trainIndex < ride.numTrains; trainIndex++)
        {
            auto* head = getGameState().entities.TryGetEntity<Vehicle>(ride.vehicles[trainIndex]);
            if (head == nullptr)
            {
                continue;
            }
            const auto train = RideVehicle::StationDetail::BuildTrainSeatSummary(*head);
            if (train.capacity != 0 && train.capacity <= std::numeric_limits<uint16_t>::max())
            {
                return static_cast<uint16_t>(train.capacity);
            }
        }
        return 0;
    }

    // Transport types without a platform adapter retain the conservative legacy estimate.
    constexpr uint16_t kGuestsPerStationTile = 2;
    return std::max<uint16_t>(kGuestsPerStationTile, ride.getStation(stationIndex).Length * kGuestsPerStationTile);
}

uint16_t RideGetTransportStationPlatformOccupancy(const Ride& ride, StationIndex stationIndex)
{
    if (RideGetTransportStationPlatformCapacity(ride, stationIndex) == 0)
    {
        return 0;
    }

    if (const auto* state = GetStationPlatformStateConst(ride, stationIndex); state != nullptr && state->active)
    {
        return static_cast<uint16_t>(std::count_if(state->slots.begin(), state->slots.end(), [](const auto& slot) {
            return !slot.guestId.IsNull();
        }));
    }

    const auto trainIndex = ride.getStation(stationIndex).TrainAtStation;
    if (trainIndex >= ride.numTrains)
    {
        return 0;
    }

    uint16_t occupancy = 0;
    for (auto* vehicle = getGameState().entities.GetEntity<Vehicle>(ride.vehicles[trainIndex]); vehicle != nullptr;
         vehicle = getGameState().entities.GetEntity<Vehicle>(vehicle->next_vehicle_on_train))
    {
        if (vehicle->status == Vehicle::Status::unloadingPassengers)
        {
            occupancy = AddClamp<uint16_t>(
                occupancy, vehicle->num_peeps - std::min(vehicle->num_peeps, vehicle->next_free_seat));
        }
        else if (vehicle->next_free_seat > vehicle->num_peeps)
        {
            occupancy = AddClamp<uint16_t>(occupancy, vehicle->next_free_seat - vehicle->num_peeps);
        }
    }
    return occupancy;
}

bool RideIsTransportStationOvercrowded(const Ride& ride, StationIndex stationIndex)
{
    const auto capacity = RideGetTransportStationPlatformCapacity(ride, stationIndex);
    return capacity > 0 && ride.getStation(stationIndex).QueueFull
        && RideGetTransportStationPlatformOccupancy(ride, stationIndex) >= capacity;
}

/**
 * Return the tile_element of an adjacent station at x,y,z(+-2).
 * Returns nullptr if no suitable tile_element is found.
 */
TileElement* GetStationPlatform(const CoordsXYRangedZ& coords)
{
    bool foundTileElement = false;
    TileElement* tileElement = MapGetFirstElementAt(coords);
    if (tileElement != nullptr)
    {
        do
        {
            if (tileElement->getType() != TileElementType::Track)
                continue;
            /* Check if tileElement is a station platform. */
            if (!tileElement->asTrack()->IsStation())
                continue;

            if (coords.baseZ > tileElement->getBaseZ() || coords.clearanceZ < tileElement->getBaseZ())
            {
                /* The base height of tileElement is not within
                 * the z tolerance. */
                continue;
            }

            foundTileElement = true;
            break;
        } while (!(tileElement++)->isLastForTile());
    }
    if (!foundTileElement)
    {
        return nullptr;
    }

    return tileElement;
}

/**
 * Check for an adjacent station to x,y,z in direction.
 */
static bool CheckForAdjacentStation(const CoordsXYZ& stationCoords, uint8_t direction)
{
    bool found = false;
    int32_t adjX = stationCoords.x;
    int32_t adjY = stationCoords.y;
    for (uint32_t i = 0; i <= kRideAdjacencyCheckDistance; i++)
    {
        adjX += CoordsDirectionDelta[direction].x;
        adjY += CoordsDirectionDelta[direction].y;
        TileElement* stationElement = GetStationPlatform(
            { { adjX, adjY, stationCoords.z - 2 * kCoordsZStep }, stationCoords.z + 2 * kCoordsZStep });
        if (stationElement != nullptr)
        {
            auto rideIndex = stationElement->asTrack()->GetRideIndex();
            auto ride = GetRide(rideIndex);
            if (ride != nullptr && (ride->departFlags & RIDE_DEPART_SYNCHRONISE_WITH_ADJACENT_STATIONS))
            {
                found = true;
            }
        }
    }
    return found;
}

/**
 * Return whether ride has at least one adjacent station to it.
 */
bool RideHasAdjacentStation(const Ride& ride)
{
    bool found = false;

    /* Loop through all of the ride stations, checking for an
     * adjacent station on either side. */
    for (const auto& station : ride.getStations())
    {
        auto stationStart = station.GetStart();
        if (!stationStart.IsNull())
        {
            /* Get the map element for the station start. */
            TileElement* stationElement = GetStationPlatform({ stationStart, stationStart.z + 0 });
            if (stationElement == nullptr)
            {
                continue;
            }
            /* Check the first side of the station */
            int32_t direction = stationElement->getDirectionWithOffset(1);
            found = CheckForAdjacentStation(stationStart, direction);
            if (found)
                break;
            /* Check the other side of the station */
            direction = DirectionReverse(direction);
            found = CheckForAdjacentStation(stationStart, direction);
            if (found)
                break;
        }
    }
    return found;
}

bool RideHasStationShelter(const Ride& ride)
{
    const auto* stationObj = ride.getStationObject();
    return stationObj != nullptr && (stationObj->Flags & StationObjectFlags::hasShelter);
}

bool RideHasRatings(const Ride& ride)
{
    return !ride.ratings.isNull();
}

int32_t GetUnifiedBoosterSpeed(ride_type_t rideType, int32_t relativeSpeed)
{
    return GetRideTypeDescriptor(rideType).GetUnifiedBoosterSpeed(relativeSpeed);
}

void FixInvalidVehicleSpriteSizes()
{
    auto& gameState = getGameState();
    for (const auto& ride : RideManager(gameState))
    {
        for (auto entityIndex : ride.vehicles)
        {
            for (Vehicle* vehicle = getGameState().entities.TryGetEntity<Vehicle>(entityIndex); vehicle != nullptr;
                 vehicle = getGameState().entities.TryGetEntity<Vehicle>(vehicle->next_vehicle_on_train))
            {
                auto carEntry = vehicle->Entry();
                if (carEntry == nullptr)
                {
                    break;
                }

                if (vehicle->spriteData.width == 0)
                {
                    vehicle->spriteData.width = carEntry->spriteWidth;
                }
                if (vehicle->spriteData.heightMin == 0)
                {
                    vehicle->spriteData.heightMin = carEntry->spriteHeightNegative;
                }
                if (vehicle->spriteData.heightMax == 0)
                {
                    vehicle->spriteData.heightMax = carEntry->spriteHeightPositive;
                }
            }
        }
    }
}

bool RideEntryHasCategory(const RideObjectEntry& rideEntry, RideCategory category)
{
    auto rideType = rideEntry.GetFirstNonNullRideType();
    return GetRideTypeDescriptor(rideType).Category == category;
}

ObjectEntryIndex RideGetEntryIndex(ride_type_t rideType, ObjectEntryIndex rideSubType)
{
    auto subType = rideSubType;

    if (subType == kObjectEntryIndexNull)
    {
        auto& objManager = GetContext()->GetObjectManager();
        auto& rideEntries = objManager.GetAllRideEntries(rideType);
        if (!rideEntries.empty())
        {
            subType = rideEntries[0];
            for (auto rideEntryIndex : rideEntries)
            {
                const auto* rideEntry = GetRideEntryByIndex(rideEntryIndex);
                if (rideEntry == nullptr)
                {
                    return kObjectEntryIndexNull;
                }

                // Can happen in select-by-track-type mode
                if (!RideEntryIsInvented(rideEntryIndex) && !getGameState().cheats.ignoreResearchStatus)
                {
                    continue;
                }

                if (!GetRideTypeDescriptor(rideType).flags.has(RtdFlag::listVehiclesSeparately))
                {
                    subType = rideEntryIndex;
                    break;
                }
            }
        }
    }

    return subType;
}

const StationObject* Ride::getStationObject() const
{
    auto& objManager = GetContext()->GetObjectManager();
    return objManager.GetLoadedObject<StationObject>(entranceStyle);
}

const MusicObject* Ride::getMusicObject() const
{
    auto& objManager = GetContext()->GetObjectManager();
    return objManager.GetLoadedObject<MusicObject>(music);
}

// Normally, a station has at most one entrance and one exit, which are at the same height
// as the station. But in hacked parks, neither can be taken for granted. This code ensures
// that the ride.entrances and ride.exits arrays will point to one of them. There is
// an ever-so-slight chance two entrances/exits for the same station reside on the same tile.
// In cases like this, the one at station height will be considered the "true" one.
// If none exists at that height, newer and higher placed ones take precedence.
void DetermineRideEntranceAndExitLocations()
{
    LOG_VERBOSE("Inspecting ride entrance / exit locations");

    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        for (auto& station : ride.getStations())
        {
            auto stationIndex = ride.getStationIndex(&station);
            TileCoordsXYZD entranceLoc = station.Entrance;
            TileCoordsXYZD exitLoc = station.Exit;
            bool fixEntrance = false;
            bool fixExit = false;

            // Skip if the station has no entrance
            if (!entranceLoc.IsNull())
            {
                const EntranceElement* entranceElement = MapGetRideEntranceElementAt(entranceLoc.ToCoordsXYZD(), false);

                if (entranceElement == nullptr || entranceElement->GetRideIndex() != ride.id
                    || entranceElement->GetStationIndex() != stationIndex)
                {
                    fixEntrance = true;
                }
                else
                {
                    station.Entrance.direction = entranceElement->getDirection();
                }
            }

            if (!exitLoc.IsNull())
            {
                const EntranceElement* entranceElement = MapGetRideExitElementAt(exitLoc.ToCoordsXYZD(), false);

                if (entranceElement == nullptr || entranceElement->GetRideIndex() != ride.id
                    || entranceElement->GetStationIndex() != stationIndex)
                {
                    fixExit = true;
                }
                else
                {
                    station.Exit.direction = entranceElement->getDirection();
                }
            }

            if (!fixEntrance && !fixExit)
            {
                continue;
            }

            // At this point, we know we have a disconnected entrance or exit.
            // Search the map to find it. Skip the outer ring of invisible tiles.
            bool alreadyFoundEntrance = false;
            bool alreadyFoundExit = false;
            for (int32_t y = 1; y < gameState.mapSize.y - 1; y++)
            {
                for (int32_t x = 1; x < gameState.mapSize.x - 1; x++)
                {
                    TileElement* tileElement = MapGetFirstElementAt(TileCoordsXY{ x, y });

                    if (tileElement != nullptr)
                    {
                        do
                        {
                            if (tileElement->getType() != TileElementType::Entrance)
                            {
                                continue;
                            }
                            const EntranceElement* entranceElement = tileElement->asEntrance();
                            if (entranceElement->GetRideIndex() != ride.id)
                            {
                                continue;
                            }
                            if (entranceElement->GetStationIndex() != stationIndex)
                            {
                                continue;
                            }

                            // The expected height is where entrances and exit reside in non-hacked parks.
                            const uint8_t expectedHeight = station.Height;

                            if (fixEntrance && entranceElement->GetEntranceType() == ENTRANCE_TYPE_RIDE_ENTRANCE)
                            {
                                if (alreadyFoundEntrance)
                                {
                                    if (station.Entrance.z == expectedHeight)
                                        continue;
                                    if (station.Entrance.z > entranceElement->baseHeight)
                                        continue;
                                }

                                // Found our entrance
                                station.Entrance = { x, y, entranceElement->baseHeight, entranceElement->getDirection() };
                                alreadyFoundEntrance = true;

                                LOG_VERBOSE(
                                    "Fixed disconnected entrance of ride %d, station %d to x = %d, y = %d and z = %d.", ride.id,
                                    stationIndex, x, y, entranceElement->baseHeight);
                            }
                            else if (fixExit && entranceElement->GetEntranceType() == ENTRANCE_TYPE_RIDE_EXIT)
                            {
                                if (alreadyFoundExit)
                                {
                                    if (station.Exit.z == expectedHeight)
                                        continue;
                                    if (station.Exit.z > entranceElement->baseHeight)
                                        continue;
                                }

                                // Found our exit
                                station.Exit = { x, y, entranceElement->baseHeight, entranceElement->getDirection() };
                                alreadyFoundExit = true;

                                LOG_VERBOSE(
                                    "Fixed disconnected exit of ride %d, station %d to x = %d, y = %d and z = %d.", ride.id,
                                    stationIndex, x, y, entranceElement->baseHeight);
                            }
                        } while (!(tileElement++)->isLastForTile());
                    }
                }
            }

            if (fixEntrance && !alreadyFoundEntrance)
            {
                station.Entrance.SetNull();
                LOG_VERBOSE("Cleared disconnected entrance of ride %d, station %d.", ride.id, stationIndex);
            }
            if (fixExit && !alreadyFoundExit)
            {
                station.Exit.SetNull();
                LOG_VERBOSE("Cleared disconnected exit of ride %d, station %d.", ride.id, stationIndex);
            }
        }
    }
}

void RideClearLeftoverEntrances(const Ride& ride)
{
    auto& gameState = getGameState();
    for (TileCoordsXY tilePos = {}; tilePos.x < gameState.mapSize.x; ++tilePos.x)
    {
        for (tilePos.y = 0; tilePos.y < gameState.mapSize.y; ++tilePos.y)
        {
            for (auto* entrance : TileElementsView<EntranceElement>(tilePos.ToCoordsXY()))
            {
                const bool isRideEntranceExit = entrance->GetEntranceType() == ENTRANCE_TYPE_RIDE_ENTRANCE
                    || entrance->GetEntranceType() == ENTRANCE_TYPE_RIDE_EXIT;
                if (!isRideEntranceExit)
                    continue;
                if (entrance->GetRideIndex() != ride.id)
                    continue;

                if (!entrance->isGhost())
                {
                    MapTopology::InvalidateTileAndNeighbours(tilePos);
                }
                TileElementRemove(entrance->as<TileElement>());
            }
        }
    }
}

std::string Ride::getName() const
{
    Formatter ft;
    formatNameTo(ft);
    return FormatStringIDLegacy(STR_STRINGID, reinterpret_cast<const void*>(ft.Data()));
}

void Ride::formatNameTo(Formatter& ft) const
{
    if (!customName.empty())
    {
        auto str = customName.c_str();
        ft.Add<StringId>(STR_STRING);
        ft.Add<const char*>(str);
    }
    else
    {
        const auto rideTypeName = getTypeNaming().Name;
        ft.Add<StringId>(1).Add<StringId>(rideTypeName).Add<uint16_t>(defaultNameNumber);
    }
}

uint64_t Ride::getAvailableModes() const
{
    if (getGameState().cheats.showAllOperatingModes)
        return kAllRideModesAvailable;

    return getRideTypeDescriptor().RideModes;
}

const RideTypeDescriptor& Ride::getRideTypeDescriptor() const
{
    return ::GetRideTypeDescriptor(type);
}

RideNaming Ride::getTypeNaming() const
{
    return GetRideNaming(type, getRideEntry());
}

uint8_t Ride::getNumShelteredSections() const
{
    return numShelteredSections & ShelteredSectionsBits::kNumShelteredSectionsMask;
}

void Ride::increaseNumShelteredSections()
{
    auto newNumShelteredSections = getNumShelteredSections();
    if (newNumShelteredSections != 0x1F)
        newNumShelteredSections++;
    numShelteredSections &= ~ShelteredSectionsBits::kNumShelteredSectionsMask;
    numShelteredSections |= newNumShelteredSections;
}

void Ride::updateRideTypeForAllPieces()
{
    auto& gameState = getGameState();
    for (int32_t y = 0; y < gameState.mapSize.y; y++)
    {
        for (int32_t x = 0; x < gameState.mapSize.x; x++)
        {
            auto* tileElement = MapGetFirstElementAt(TileCoordsXY(x, y));
            if (tileElement == nullptr)
                continue;

            do
            {
                if (tileElement->getType() != TileElementType::Track)
                    continue;

                auto* trackElement = tileElement->asTrack();
                if (trackElement->GetRideIndex() != id)
                    continue;

                trackElement->SetRideType(type);

            } while (!(tileElement++)->isLastForTile());
        }
    }
}

bool Ride::hasRecolourableShopItems() const
{
    const auto rideEntry = getRideEntry();
    if (rideEntry == nullptr)
        return false;

    for (size_t itemIndex = 0; itemIndex < std::size(rideEntry->shop_item); itemIndex++)
    {
        const ShopItem currentItem = rideEntry->shop_item[itemIndex];
        if (currentItem != ShopItem::none && GetShopItemDescriptor(currentItem).IsRecolourable())
        {
            return true;
        }
    }
    return false;
}

bool Ride::hasStation() const
{
    return numStations != 0;
}

std::vector<RideId> GetTracklessRides()
{
    // Iterate map and build list of seen ride IDs
    std::vector<bool> seen;
    seen.resize(256);
    TileElementIterator it;
    TileElementIteratorBegin(&it);
    while (TileElementIteratorNext(&it))
    {
        auto trackEl = it.element->asTrack();
        if (trackEl != nullptr && !trackEl->isGhost())
        {
            auto rideId = trackEl->GetRideIndex().ToUnderlying();
            if (rideId >= seen.size())
            {
                seen.resize(rideId + 1);
            }
            seen[rideId] = true;
        }
    }

    // Get all rides that did not get seen during map iteration
    auto& gameState = getGameState();
    const auto& rideManager = RideManager(gameState);
    std::vector<RideId> result;
    for (const auto& ride : rideManager)
    {
        const auto rideIndex = ride.id.ToUnderlying();
        if (seen.size() <= rideIndex || !seen[rideIndex])
        {
            result.push_back(ride.id);
        }
    }
    return result;
}

ResultWithMessage Ride::changeStatusDoStationChecks(StationIndex& stationIndex)
{
    auto stationIndexCheck = RideModeCheckStationPresent(*this);
    stationIndex = stationIndexCheck.StationIndex;
    if (stationIndex.IsNull())
        return { false, stationIndexCheck.Message };

    auto stationNumbersCheck = RideModeCheckValidStationNumbers(*this);
    if (!stationNumbersCheck.Successful)
        return { false, stationNumbersCheck.Message };

    return { true };
}

ResultWithMessage Ride::changeStatusGetStartElement(StationIndex stationIndex, CoordsXYE& trackElement)
{
    auto startLoc = getStation(stationIndex).Start;
    trackElement.x = startLoc.x;
    trackElement.y = startLoc.y;
    trackElement.element = reinterpret_cast<TileElement*>(getOriginElement(stationIndex));
    if (trackElement.element == nullptr)
    {
        // Maze is strange, station start is 0... investigation required
        const auto& rtd = getRideTypeDescriptor();
        if (rtd.specialType != RtdSpecialType::maze)
            return { false };
    }

    return { true };
}

ResultWithMessage Ride::changeStatusCheckCompleteCircuit(const CoordsXYE& trackElement)
{
    CoordsXYE problematicTrackElement = {};
    if (mode == RideMode::race || mode == RideMode::continuousCircuit || isBlockSectioned())
    {
        if (findTrackGap(*this, trackElement, &problematicTrackElement))
        {
            RideScrollToTrackError(problematicTrackElement);
            return { false, STR_TRACK_IS_NOT_A_COMPLETE_CIRCUIT };
        }
    }

    return { true };
}

ResultWithMessage Ride::changeStatusCheckTrackValidity(const CoordsXYE& trackElement, bool isSimulating)
{
    CoordsXYE problematicTrackElement = {};

    if (isBlockSectioned())
    {
        auto blockBrakeCheck = RideCheckBlockBrakes(trackElement, &problematicTrackElement, !isSimulating);
        if (!blockBrakeCheck.Successful)
        {
            RideScrollToTrackError(problematicTrackElement);
            return { false, blockBrakeCheck.Message };
        }
    }

    if (subtype != kObjectEntryIndexNull && !getGameState().cheats.enableAllDrawableTrackPieces)
    {
        const auto* rideEntry = GetRideEntryByIndex(subtype);
        if (rideEntry == nullptr)
        {
            return { false, STR_UNKNOWN_RIDE };
        }
        if (rideEntry->flags.has(RideEntryFlag::noInversions))
        {
            if (RideCheckTrackContainsInversions(trackElement, &problematicTrackElement))
            {
                RideScrollToTrackError(problematicTrackElement);
                return { false, STR_TRACK_UNSUITABLE_FOR_TYPE_OF_TRAIN };
            }
        }
        if (rideEntry->flags.has(RideEntryFlag::noBankedTrack))
        {
            if (RideCheckTrackContainsBanked(trackElement, &problematicTrackElement))
            {
                RideScrollToTrackError(problematicTrackElement);
                return { false, STR_TRACK_UNSUITABLE_FOR_TYPE_OF_TRAIN };
            }
        }
    }

    if (mode == RideMode::stationToStation)
    {
        if (!findTrackGap(*this, trackElement, &problematicTrackElement))
        {
            return { false, STR_RIDE_MUST_START_AND_END_WITH_STATIONS };
        }

        if (!RideCheckStationLength(trackElement, &problematicTrackElement))
        {
            RideScrollToTrackError(problematicTrackElement);
            return { false, STR_STATION_NOT_LONG_ENOUGH };
        }

        if (!RideCheckStartAndEndIsStation(trackElement))
        {
            RideScrollToTrackError(problematicTrackElement);
            return { false, STR_RIDE_MUST_START_AND_END_WITH_STATIONS };
        }
    }

    return { true };
}

ResultWithMessage Ride::changeStatusCreateVehicles(bool isApplying, const CoordsXYE& trackElement, bool isSimulating)
{
    if (isApplying)
        RideSetStartFinishPoints(id, trackElement);

    const auto& rtd = getRideTypeDescriptor();
    if (!rtd.flags.has(RtdFlag::noVehicles) && !flags.has(RideFlag::onTrack))
    {
        const auto createVehicleResult = createVehicles(trackElement, isApplying, isSimulating);
        if (!createVehicleResult.Successful)
        {
            return { false, createVehicleResult.Message };
        }
    }

    if (rtd.flags.has(RtdFlag::allowCableLiftHill) && flags.has(RideFlag::cableLiftHillComponentUsed)
        && !flags.has(RideFlag::cableLift))
    {
        const auto createCableLiftResult = RideCreateCableLift(id, isApplying);
        if (!createCableLiftResult.Successful)
            return { false, createCableLiftResult.Message };
    }

    return { true };
}

RideMode RideModeGetBlockSectionedCounterpart(RideMode originalMode)
{
    assert(originalMode < RideMode::count);
    return kRideModeBlockSectionedCounterpart[EnumValue(originalMode)];
}
