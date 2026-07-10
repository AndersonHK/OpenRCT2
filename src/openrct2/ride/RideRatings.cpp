/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "RideRatings.h"

#include "../Cheats.h"
#include "../Context.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../core/Guard.hpp"
#include "../core/Money.hpp"
#include "../core/UnitConversion.h"
#include "../profiling/Profiling.h"
#include "../scripting/ScriptEngine.h"
#include "../ui/WindowManager.h"
#include "../world/Map.h"
#include "../world/MapLimits.h"
#include "../world/Scenery.h"
#include "../world/tile_element/PathElement.h"
#include "../world/tile_element/SurfaceElement.h"
#include "../world/tile_element/TileElement.h"
#include "../world/tile_element/TrackElement.h"
#include "Ride.h"
#include "RideData.h"
#include "RideManager.hpp"
#include "Station.h"
#include "Track.h"
#include "TrackData.h"
#include "TrackIteration.h"
#include "ted/TrackElementDescriptor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>

using namespace OpenRCT2;
using namespace OpenRCT2::Scripting;
using namespace OpenRCT2::TrackMetadata;

enum
{
    RIDE_RATINGS_STATE_FIND_NEXT_RIDE,
    RIDE_RATINGS_STATE_INITIALISE,
    RIDE_RATINGS_STATE_2,
    RIDE_RATINGS_STATE_CALCULATE,
    RIDE_RATINGS_STATE_4,
    RIDE_RATINGS_STATE_5
};

enum
{
    RIDE_RATING_STATION_FLAG_NO_ENTRANCE = 1 << 0
};

enum
{
    PROXIMITY_WATER_OVER,                   // 0x0138B596
    PROXIMITY_WATER_TOUCH,                  // 0x0138B598
    PROXIMITY_WATER_LOW,                    // 0x0138B59A
    PROXIMITY_WATER_HIGH,                   // 0x0138B59C
    PROXIMITY_SURFACE_TOUCH,                // 0x0138B59E
    PROXIMITY_QUEUE_PATH_OVER,              // 0x0138B5A0
    PROXIMITY_QUEUE_PATH_TOUCH_ABOVE,       // 0x0138B5A2
    PROXIMITY_QUEUE_PATH_TOUCH_UNDER,       // 0x0138B5A4
    PROXIMITY_PATH_TOUCH_ABOVE,             // 0x0138B5A6
    PROXIMITY_PATH_TOUCH_UNDER,             // 0x0138B5A8
    PROXIMITY_OWN_TRACK_TOUCH_ABOVE,        // 0x0138B5AA
    PROXIMITY_OWN_TRACK_CLOSE_ABOVE,        // 0x0138B5AC
    PROXIMITY_FOREIGN_TRACK_ABOVE_OR_BELOW, // 0x0138B5AE
    PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE,    // 0x0138B5B0
    PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE,    // 0x0138B5B2
    PROXIMITY_SCENERY_SIDE_BELOW,           // 0x0138B5B4
    PROXIMITY_SCENERY_SIDE_ABOVE,           // 0x0138B5B6
    PROXIMITY_OWN_STATION_TOUCH_ABOVE,      // 0x0138B5B8
    PROXIMITY_OWN_STATION_CLOSE_ABOVE,      // 0x0138B5BA
    PROXIMITY_TRACK_THROUGH_VERTICAL_LOOP,  // 0x0138B5BC
    PROXIMITY_PATH_TROUGH_VERTICAL_LOOP,    // 0x0138B5BE
    PROXIMITY_INTERSECTING_VERTICAL_LOOP,   // 0x0138B5C0
    PROXIMITY_THROUGH_VERTICAL_LOOP,        // 0x0138B5C2
    PROXIMITY_PATH_SIDE_CLOSE,              // 0x0138B5C4
    PROXIMITY_FOREIGN_TRACK_SIDE_CLOSE,     // 0x0138B5C6
    PROXIMITY_SURFACE_SIDE_CLOSE,           // 0x0138B5C8
    PROXIMITY_COUNT
};

struct ShelteredEights
{
    uint8_t TrackShelteredEighths;
    uint8_t TotalShelteredEighths;
};

// Amount of updates allowed per updating state on the current tick.
// The total amount would be MaxRideRatingSubSteps * RideRating::kMaxUpdateStates which
// would be currently 80, this is the worst case of sub-steps and may break out earlier.
static constexpr size_t MaxRideRatingUpdateSubSteps = 20;
static constexpr int64_t kAggregatedRideRatingDivisor = 1000;
static constexpr int32_t kRideRatingContextBaseRadius = 2;
static constexpr int32_t kRideRatingContextMaxRadius = 7;
static constexpr int32_t kRideRatingContextHeightBandStep = 2 * kCoordsZStep;
static constexpr int32_t kRideRatingContextGroundRideEyeHeight = 2 * kCoordsZStep;
static constexpr int32_t kRideRatingContextMazeEyeHeight = 1 * kCoordsZStep;
static constexpr int32_t kRideRatingContextTowerRideMinEyeHeight = 4 * kCoordsZStep;
static constexpr int32_t kRideRatingContextTowerRideMaxEyeHeight = 14 * kCoordsZStep;
static constexpr int32_t kRideRatingContextSceneryFormerCap = 18;
static constexpr int32_t kRideRatingContextSceneryRawAtFormerCap = 1200;
static constexpr int32_t kRideRatingContextWeightScale = 256;
static constexpr int64_t kTrackedRideRawPerLocalContextPoint = 400;
static constexpr int64_t kTrackedRideRawPerVerticalContextPoint = 800;
static constexpr int64_t kTrackedRideRawPerForeignTrackIntensityPoint = 200;
static constexpr int64_t kTrackedRideVerticalContextNauseaNumerator = 2;
static constexpr int64_t kTrackedRideVerticalContextNauseaDenominator = 15;
static constexpr int32_t kRideRatingPathBridgeRaw = 120;
static constexpr int32_t kRideRatingPathNearMissRaw = 180;
static constexpr int32_t kRideRatingPathLoopRaw = 260;
static constexpr int32_t kRideRatingForeignTrackVerticalRaw = 220;
static constexpr int32_t kRideRatingOwnTrackVerticalRaw = 160;
static constexpr int32_t kRideRatingBridgeNearMissMaxGap = 6 * kCoordsZStep;
static constexpr int32_t kRideRatingTrackHeightExposureMax = 6;
static constexpr int32_t kRideRatingContextGenerationChunkSize = 8;
static constexpr size_t kRideRatingContextInitialCacheCapacity = 16384;
static constexpr size_t kRideRatingContextGenerationChunksPerAxis = (kMaximumMapSizeTechnical
                                                                     + kRideRatingContextGenerationChunkSize - 1)
    / kRideRatingContextGenerationChunkSize;
static constexpr int32_t kRideRatingContextGenerationNeighbourRadius =
    (kRideRatingContextMaxRadius + kRideRatingContextGenerationChunkSize - 1) / kRideRatingContextGenerationChunkSize;
static constexpr std::array<int32_t, kRideRatingContextMaxRadius + 1> kRideRatingContextDistanceWeights = { 256, 218, 154, 90,
                                                                                                            46,  28,  18,  12 };

enum class RideRatingLocalContextSampleKind : uint8_t
{
    generic,
    vehicle,
    maze,
};

struct RideRatingLocalContextQuery
{
    RideId rideId{};
    RideRatingLocalContextSampleKind sampleKind = RideRatingLocalContextSampleKind::generic;
    TrackElemType trackType = TrackElemType::none;
    Direction trackDirection = kInvalidDirection;
};

struct RideRatingLocalContextKey
{
    int16_t x{};
    int16_t y{};
    int16_t z{};
    uint16_t rideId{};
    uint16_t trackType{};
    uint8_t trackDirection{};
    RideRatingLocalContextSampleKind sampleKind{};

    bool operator==(const RideRatingLocalContextKey& rhs) const = default;
};

struct RideRatingLocalContextKeyHash
{
    size_t operator()(const RideRatingLocalContextKey& key) const noexcept
    {
        auto hash = static_cast<size_t>(static_cast<uint16_t>(key.x));
        hash = (hash << 16) ^ static_cast<uint16_t>(key.y);
        hash = (hash << 10) ^ static_cast<uint16_t>(key.z);
        hash = (hash << 16) ^ key.rideId;
        hash = (hash << 16) ^ key.trackType;
        hash = (hash << 8) ^ key.trackDirection;
        hash = (hash << 8) ^ static_cast<uint8_t>(key.sampleKind);
        return hash;
    }
};

struct RideRatingLocalContextCacheEntry
{
    RideRating::VehicleRatingEnvironment environment{};
    uint64_t spatialGeneration{};
};

using RideRatingLocalContextCache = std::unordered_map<
    RideRatingLocalContextKey, RideRatingLocalContextCacheEntry, RideRatingLocalContextKeyHash>;

static RideRatingLocalContextCache _rideRatingLocalContextCache = []() {
    RideRatingLocalContextCache result;
    result.reserve(kRideRatingContextInitialCacheCapacity);
    return result;
}();
static std::array<uint64_t, kRideRatingContextGenerationChunksPerAxis * kRideRatingContextGenerationChunksPerAxis>
    _rideRatingLocalContextOriginGenerations{};
static uint64_t _rideRatingLocalContextInvalidationGeneration{};

struct RideRatingLocalContextRaw
{
    int32_t scenery{};
    int32_t pathProximity{};
    int32_t foreignTrackProximity{};
    int32_t pathBridge{};
    int32_t pathNearMiss{};
    int32_t pathLoop{};
    int32_t trackVerticalInteraction{};
    int32_t ownTrackVerticalInteraction{};
    int32_t trackHeightExposure{};
    bool hasSameTilePathBridge{};
};

struct RideRatingLocalContextVisibilityTarget
{
    TileCoordsXY tileLocation{};
    int32_t rangeBaseZ{};
    int32_t lowerVisibleZ{};
    int32_t upperVisibleZ{};
};

struct RideRatingFixedRideLocalContextBase
{
    CoordsXY location{};
    int32_t baseZ{};
    Direction direction{};
    TrackElemType trackType = TrackElemType::none;
};

struct RawRideRating
{
    int64_t excitement{};
    int64_t intensity{};
    int64_t nausea{};
};
static_assert(std::is_same_v<decltype(RawRideRating::excitement), int64_t>);
static_assert(std::is_same_v<decltype(RawRideRating::intensity), int64_t>);
static_assert(std::is_same_v<decltype(RawRideRating::nausea), int64_t>);

static int64_t RideRatingCurveScore(int32_t hundredthsOfG, double coefficient, double exponent)
{
    if (hundredthsOfG <= 0)
    {
        return 0;
    }

    const auto g = static_cast<double>(hundredthsOfG) / 100.0;
    const auto score = coefficient * std::pow(g, exponent);
    if (!std::isfinite(score) || score <= 0.0)
    {
        return 0;
    }
    return static_cast<int64_t>(std::llround(score * RideRating::kRideRatingAccumulatorRawScale));
}

static void RideRatingAddTickScore(RideRating::TickScore& total, const RideRating::TickScore& value)
{
    total.excitement += value.excitement;
    total.intensity += value.intensity;
    total.nausea += value.nausea;
}

static RideRating::TickScore RideRatingScaleTickScore(RideRating::TickScore score, int32_t coefficient)
{
    const auto scale = static_cast<int64_t>(std::max(coefficient, 0));
    score.excitement = (score.excitement * scale) / kSampledRideRatingProfileScale;
    score.intensity = (score.intensity * scale) / kSampledRideRatingProfileScale;
    score.nausea = (score.nausea * scale) / kSampledRideRatingProfileScale;
    return score;
}

static RideRating::TickScore RideRatingApplySpeedGCoupling(RideRating::TickScore score, int32_t speed, int32_t coupling)
{
    const auto normalisedSpeed = static_cast<int64_t>(std::max(speed, 0));
    const auto normalisedCoupling = static_cast<int64_t>(std::max(coupling, 0));
    const auto couplingDenominator = static_cast<int64_t>(RideRating::kVehicleRatingBaselineSpeed)
        * kSampledRideRatingProfileScale;
    const auto couplingNumerator = std::max<int64_t>(
        0, couplingDenominator + normalisedCoupling * (normalisedSpeed - RideRating::kVehicleRatingBaselineSpeed));

    score.excitement = ((score.excitement * normalisedSpeed) / RideRating::kVehicleRatingBaselineSpeed) * couplingNumerator
        / couplingDenominator;
    score.intensity = ((score.intensity * normalisedSpeed) / RideRating::kVehicleRatingBaselineSpeed) * couplingNumerator
        / couplingDenominator;
    score.nausea = ((score.nausea * normalisedSpeed) / RideRating::kVehicleRatingBaselineSpeed) * couplingNumerator
        / couplingDenominator;
    return score;
}

static int32_t RideRatingDiminishLocalContext(int32_t raw, int32_t cap, int32_t divisor)
{
    if (raw <= 0)
    {
        return 0;
    }

    return std::max(1, (cap * raw) / (raw + divisor));
}

static RideRatingLocalContextQuery RideRatingNormaliseLocalContextQuery(RideRatingLocalContextQuery query)
{
    if (query.sampleKind != RideRatingLocalContextSampleKind::vehicle)
    {
        query.trackType = TrackElemType::none;
        query.trackDirection = kInvalidDirection;
    }
    else if (!DirectionValid(query.trackDirection))
    {
        query.trackDirection = kInvalidDirection;
    }

    return query;
}

static bool RideRatingTrackTypeIsVerticalLoop(TrackElemType trackType)
{
    return trackType == TrackElemType::leftVerticalLoop || trackType == TrackElemType::rightVerticalLoop;
}

static bool RideRatingTrackTypeCanPathNearMiss(TrackElemType trackType)
{
    if (trackType == TrackElemType::none)
    {
        return false;
    }

    const auto& descriptor = GetTrackElementDescriptor(trackType);
    return descriptor.flags.hasAny(
        TrackElementFlag::up, TrackElementFlag::down, TrackElementFlag::turnSloped, TrackElementFlag::normalToInversion,
        TrackElementFlag::inversionToNormal);
}

static int32_t RideRatingGetPathEdgeZ(const PathElement& pathElement, Direction direction)
{
    if (pathElement.IsSloped() && pathElement.GetSlopeDirection() == direction)
    {
        return pathElement.getBaseZ() + kLandHeightStep;
    }

    return pathElement.getBaseZ();
}

static bool RideRatingPathConnects(const TileCoordsXY& tile, const PathElement& pathElement, Direction direction)
{
    if (!DirectionValid(direction) || (pathElement.GetEdges() & (1 << direction)) == 0)
    {
        return false;
    }

    const auto adjacentCoords = tile.ToCoordsXY() + CoordsDirectionDelta[direction];
    if (!MapIsLocationValid(adjacentCoords))
    {
        return false;
    }

    const auto edgeZ = RideRatingGetPathEdgeZ(pathElement, direction);
    const auto reverseDirection = DirectionReverse(direction);
    auto* tileElement = MapGetFirstElementAt(TileCoordsXY{ adjacentCoords });
    if (tileElement == nullptr)
    {
        return false;
    }

    do
    {
        if (tileElement->isGhost() || tileElement->getType() != TileElementType::Path)
        {
            continue;
        }

        const auto* adjacentPath = tileElement->asPath();
        if (adjacentPath == nullptr || (adjacentPath->GetEdges() & (1 << reverseDirection)) == 0)
        {
            continue;
        }

        if (RideRatingGetPathEdgeZ(*adjacentPath, reverseDirection) == edgeZ)
        {
            return true;
        }
    } while (!(tileElement++)->isLastForTile());

    return false;
}

static bool RideRatingPathAxisQualifiesAsBridge(const TileCoordsXY& tile, const PathElement& pathElement, Direction axis)
{
    if (!DirectionValid(axis))
    {
        return false;
    }

    const auto reverseAxis = DirectionReverse(axis);
    const auto connectsForward = RideRatingPathConnects(tile, pathElement, axis);
    const auto connectsBack = RideRatingPathConnects(tile, pathElement, reverseAxis);
    if (!connectsForward && !connectsBack)
    {
        return false;
    }

    const auto perpendicular = static_cast<Direction>((axis + 1) & 3);
    const auto reversePerpendicular = DirectionReverse(perpendicular);
    return !(
        RideRatingPathConnects(tile, pathElement, perpendicular)
        && RideRatingPathConnects(tile, pathElement, reversePerpendicular));
}

static bool RideRatingPathQualifiesAsBridge(
    const TileCoordsXY& tile, const PathElement& pathElement, const RideRatingLocalContextQuery& query)
{
    if (query.sampleKind == RideRatingLocalContextSampleKind::vehicle && DirectionValid(query.trackDirection))
    {
        return RideRatingPathAxisQualifiesAsBridge(tile, pathElement, static_cast<Direction>((query.trackDirection + 1) & 3));
    }

    return RideRatingPathAxisQualifiesAsBridge(tile, pathElement, 0)
        || RideRatingPathAxisQualifiesAsBridge(tile, pathElement, 1);
}

int32_t RideRating::ScoreSceneryForLocalContext(int32_t rawScenery)
{
    if (rawScenery <= 0)
    {
        return 0;
    }

    const auto score = static_cast<double>(kRideRatingContextSceneryFormerCap)
        * std::sqrt(static_cast<double>(rawScenery) / kRideRatingContextSceneryRawAtFormerCap);
    return std::max<int32_t>(1, static_cast<int32_t>(std::llround(score)));
}

std::pair<int32_t, int32_t> RideRating::GetSceneryVisibilityMultiplier(const Ride& ride)
{
    switch (ride.type)
    {
        case RIDE_TYPE_3D_CINEMA:
        case RIDE_TYPE_MOTION_SIMULATOR:
        case RIDE_TYPE_CIRCUS:
            return { 0, 1 };
        case RIDE_TYPE_HAUNTED_HOUSE:
        case RIDE_TYPE_FLYING_SAUCERS:
            return { 1, 2 };
        case RIDE_TYPE_CROOKED_HOUSE:
        case RIDE_TYPE_DODGEMS:
            return { 1, 4 };
        default:
            return { 1, 1 };
    }
}

static int32_t RideRatingApplySceneryVisibilityMultiplier(int32_t scenery, const Ride& ride)
{
    const auto multiplier = RideRating::GetSceneryVisibilityMultiplier(ride);
    if (scenery <= 0 || multiplier.first <= 0)
    {
        return 0;
    }

    return (scenery * multiplier.first) / multiplier.second;
}

RideRating::TickScore RideRating::ScoreLocalContextForVehicleTick(
    const LocalContextScore& contextScore, int32_t speed, int32_t coefficient)
{
    const auto verticalIntensity = static_cast<int64_t>(contextScore.pathNearMiss) + contextScore.pathLoop
        + contextScore.trackVerticalInteraction + contextScore.ownTrackVerticalInteraction;
    const auto heightExposureIntensity = static_cast<int64_t>(contextScore.trackHeightExposure);
    const auto foreignTrackProximityIntensity = static_cast<int64_t>(contextScore.foreignTrackProximity) / 2;
    const auto nonForeignTrackProximityIntensity = static_cast<int64_t>(contextScore.intensity) - verticalIntensity
        - heightExposureIntensity - foreignTrackProximityIntensity;
    const auto verticalNausea = verticalIntensity / 3;
    const auto heightExposureNausea = heightExposureIntensity / 3;
    const auto nonVerticalNausea = static_cast<int64_t>(contextScore.nausea) - verticalNausea - heightExposureNausea;
    const auto verticalExcitement = (static_cast<int64_t>(contextScore.pathNearMiss) * 2)
        + (static_cast<int64_t>(contextScore.pathLoop) * 2) + (static_cast<int64_t>(contextScore.trackVerticalInteraction) * 2)
        + (static_cast<int64_t>(contextScore.ownTrackVerticalInteraction) * 2) + heightExposureIntensity;
    const auto nonVerticalExcitement = static_cast<int64_t>(contextScore.excitement) - verticalExcitement;
    const auto excitementRaw = (nonVerticalExcitement * kTrackedRideRawPerLocalContextPoint)
        + (verticalExcitement * kTrackedRideRawPerVerticalContextPoint);
    const auto foreignTrackProximityIntensityRaw = static_cast<int64_t>(contextScore.foreignTrackProximity)
        * kTrackedRideRawPerForeignTrackIntensityPoint;
    const auto intensityRaw = (nonForeignTrackProximityIntensity * kTrackedRideRawPerLocalContextPoint)
        + foreignTrackProximityIntensityRaw
        + ((verticalIntensity + heightExposureIntensity) * kTrackedRideRawPerVerticalContextPoint);
    const auto nauseaRaw = (nonVerticalNausea * kTrackedRideRawPerLocalContextPoint)
        + (((verticalIntensity + heightExposureIntensity) * kRideRatingAccumulatorRawScale
            * kTrackedRideVerticalContextNauseaNumerator)
           / kTrackedRideVerticalContextNauseaDenominator);
    const auto normalisedSpeed = std::max<int64_t>(speed, 0);

    auto score = RideRatingScaleTickScore(
        {
            .excitement = excitementRaw,
            .intensity = intensityRaw,
            .nausea = nauseaRaw,
        },
        std::clamp(coefficient, 0, kSampledRideRatingProfileScale));
    score.excitement = (score.excitement * normalisedSpeed) / kVehicleRatingBaselineSpeed;
    score.intensity = (score.intensity * normalisedSpeed) / kVehicleRatingBaselineSpeed;
    score.nausea = (score.nausea * normalisedSpeed) / kVehicleRatingBaselineSpeed;
    return score;
}

RideRating::TickScore RideRating::ScoreBoatHireLocalContextForVehicleTick(
    const LocalContextScore& contextScore, int32_t speed, int32_t coefficient)
{
    constexpr int64_t rawScale = kRideRatingAccumulatorRawScale;
    const auto verticalIntensity = static_cast<int64_t>(contextScore.pathNearMiss) + contextScore.pathLoop
        + contextScore.trackVerticalInteraction + contextScore.ownTrackVerticalInteraction;
    const auto heightExposureIntensity = static_cast<int64_t>(contextScore.trackHeightExposure);
    const auto foreignTrackProximityIntensity = static_cast<int64_t>(contextScore.foreignTrackProximity) / 2;
    const auto nonForeignTrackProximityIntensity = static_cast<int64_t>(contextScore.intensity) - verticalIntensity
        - heightExposureIntensity - foreignTrackProximityIntensity;
    const auto verticalNausea = verticalIntensity / 3;
    const auto heightExposureNausea = heightExposureIntensity / 3;
    const auto nonVerticalNausea = static_cast<int64_t>(contextScore.nausea) - verticalNausea - heightExposureNausea;
    const auto excitementRaw = static_cast<int64_t>(contextScore.excitement) * rawScale;
    const auto foreignTrackProximityIntensityRaw = (static_cast<int64_t>(contextScore.foreignTrackProximity) * rawScale) / 2;
    const auto intensityRaw = (nonForeignTrackProximityIntensity * rawScale) + foreignTrackProximityIntensityRaw
        + ((verticalIntensity + heightExposureIntensity) * rawScale);
    const auto nauseaRaw = (nonVerticalNausea * rawScale) + (((verticalIntensity + heightExposureIntensity) * rawScale) / 3);
    const auto normalisedSpeed = std::max<int64_t>(speed, 0);

    auto score = RideRatingScaleTickScore(
        {
            .excitement = excitementRaw,
            .intensity = intensityRaw,
            .nausea = nauseaRaw,
        },
        std::clamp(coefficient, 0, kSampledRideRatingProfileScale));
    score.excitement = (score.excitement * normalisedSpeed) / kVehicleRatingBaselineSpeed;
    score.intensity = (score.intensity * normalisedSpeed) / kVehicleRatingBaselineSpeed;
    score.nausea = (score.nausea * normalisedSpeed) / kVehicleRatingBaselineSpeed;
    return score;
}

RideRating::TickScore RideRating::ScoreBoatHireFreeRoamForTick(uint32_t tickIndex)
{
    // Boat Hire should feel gently eventful without the coaster-style unbanked turn weight.
    return {
        .excitement = (tickIndex % 2) == 0 ? (8 * kRideRatingAccumulatorRawScale) / 5 : 0,
        .intensity = kRideRatingAccumulatorRawScale,
        .nausea = kRideRatingAccumulatorRawScale,
    };
}

static int32_t RideRatingGetLocalContextGroundZ(const TileCoordsXY& tileLocation)
{
    const auto* surfaceElement = MapGetSurfaceElementAt(tileLocation);
    return surfaceElement != nullptr ? surfaceElement->getBaseZ() : 0;
}

static int32_t RideRatingGetVehicleHeightExposure(const TileCoordsXY& originTile, int32_t originZ, Direction trackDirection)
{
    if (!DirectionValid(trackDirection))
    {
        return 0;
    }

    int32_t totalBands = 0;
    int32_t validSides = 0;
    const std::array<Direction, 2> sideDirections = {
        static_cast<Direction>((trackDirection + 1) & 3),
        static_cast<Direction>((trackDirection - 1) & 3),
    };

    for (const auto sideDirection : sideDirections)
    {
        const auto sideCoords = originTile.ToCoordsXY() + CoordsDirectionDelta[sideDirection];
        if (!MapIsLocationValid(sideCoords))
        {
            continue;
        }

        const auto* sideSurface = MapGetSurfaceElementAt(TileCoordsXY{ sideCoords });
        if (sideSurface == nullptr)
        {
            continue;
        }

        validSides++;
        totalBands += std::max(0, (originZ - sideSurface->getBaseZ()) / kRideRatingContextHeightBandStep);
    }

    if (validSides == 0)
    {
        return 0;
    }

    const auto averageBands = totalBands / validSides;
    return std::clamp((averageBands + 1) / 2, 0, kRideRatingTrackHeightExposureMax);
}

static int32_t RideRatingGetLocalContextSightZ(const CoordsXYZ& origin, int32_t distance)
{
    const auto falloffDistance = std::max(0, distance - kRideRatingContextBaseRadius);
    return origin.z - (falloffDistance * kRideRatingContextHeightBandStep);
}

static bool RideRatingLocalContextTileIsInRange(const CoordsXYZ& origin, int32_t distance, int32_t rangeBaseZ)
{
    return distance <= kRideRatingContextMaxRadius && rangeBaseZ <= RideRatingGetLocalContextSightZ(origin, distance);
}

static int32_t RideRatingGetLocalContextDistanceWeight(int32_t distance)
{
    return kRideRatingContextDistanceWeights[std::clamp(distance, 0, kRideRatingContextMaxRadius)];
}

static int32_t RideRatingGetLocalContextHeightWeight(const CoordsXYZ& origin, int32_t targetTopZ)
{
    if (origin.z <= targetTopZ)
    {
        return kRideRatingContextWeightScale;
    }

    const auto heightBandsAbove = (origin.z - targetTopZ) / kRideRatingContextHeightBandStep;
    return std::max(160, kRideRatingContextWeightScale - (heightBandsAbove * 12));
}

static bool RideRatingContextElementIsSolidOccluder(const TileElement& tileElement)
{
    switch (tileElement.getType())
    {
        case TileElementType::Track:
        {
            const auto* trackElement = tileElement.asTrack();
            return trackElement != nullptr && trackElement->GetTrackType() == TrackElemType::maze;
        }
        case TileElementType::Wall:
        case TileElementType::Entrance:
        case TileElementType::SmallScenery:
        case TileElementType::LargeScenery:
            return true;
        default:
            return false;
    }
}

static RideRatingLocalContextVisibilityTarget RideRatingBuildLocalContextVisibilityTarget(
    const TileCoordsXY& tileLocation, const TileElement& tileElement, int32_t rangeBaseZ)
{
    const auto lowerVisibleZ = tileElement.getBaseZ();
    const auto upperVisibleZ = std::max(lowerVisibleZ, static_cast<int32_t>(tileElement.getClearanceZ()));
    return {
        .tileLocation = tileLocation,
        .rangeBaseZ = rangeBaseZ,
        .lowerVisibleZ = lowerVisibleZ,
        .upperVisibleZ = upperVisibleZ,
    };
}

static bool RideRatingContextRayBlockedByOriginTileMaze(
    const CoordsXYZ& origin, const TileCoordsXY& originTile, const TileCoordsXY& targetTile, int32_t targetZ)
{
    if (originTile == targetTile)
    {
        return false;
    }

    auto* tileElement = MapGetFirstElementAt(originTile);
    if (tileElement == nullptr)
    {
        return false;
    }

    const auto exitZ = origin.z + ((targetZ - origin.z) / 2);
    do
    {
        if (tileElement->isGhost())
        {
            continue;
        }

        if (tileElement->getType() != TileElementType::Track)
        {
            continue;
        }

        const auto* trackElement = tileElement->asTrack();
        if (trackElement == nullptr || trackElement->GetTrackType() != TrackElemType::maze)
        {
            continue;
        }

        if (exitZ >= tileElement->getBaseZ() && exitZ < tileElement->getClearanceZ())
        {
            return true;
        }
    } while (!(tileElement++)->isLastForTile());

    return false;
}

static bool RideRatingContextRayHasLineOfSight(
    const CoordsXYZ& origin, const TileCoordsXY& originTile, const TileCoordsXY& targetTile, int32_t targetZ)
{
    const auto dxTiles = targetTile.x - originTile.x;
    const auto dyTiles = targetTile.y - originTile.y;
    const auto steps = std::max(std::abs(dxTiles), std::abs(dyTiles));
    if (RideRatingContextRayBlockedByOriginTileMaze(origin, originTile, targetTile, targetZ))
    {
        return false;
    }

    if (steps <= 1)
    {
        return true;
    }

    const auto targetCentre = targetTile.ToCoordsXY().ToTileCentre();
    for (int32_t step = 1; step < steps; step++)
    {
        const CoordsXYZ sample{
            origin.x + (((targetCentre.x - origin.x) * step) / steps),
            origin.y + (((targetCentre.y - origin.y) * step) / steps),
            origin.z + (((targetZ - origin.z) * step) / steps),
        };
        const auto sampleTile = TileCoordsXY{ CoordsXY{ sample.x, sample.y } };
        if (sampleTile == originTile || sampleTile == targetTile)
        {
            continue;
        }

        auto* tileElement = MapGetFirstElementAt(sampleTile);
        if (tileElement == nullptr)
        {
            continue;
        }

        do
        {
            if (tileElement->isGhost())
            {
                continue;
            }

            if (tileElement->getType() == TileElementType::Surface && sample.z < tileElement->getBaseZ())
            {
                return false;
            }

            if (!RideRatingContextElementIsSolidOccluder(*tileElement))
            {
                continue;
            }

            if (sample.z >= tileElement->getBaseZ() && sample.z < tileElement->getClearanceZ())
            {
                return false;
            }
        } while (!(tileElement++)->isLastForTile());
    }

    return true;
}

static bool RideRatingContextHasLineOfSight(
    const CoordsXYZ& origin, const TileCoordsXY& originTile, const RideRatingLocalContextVisibilityTarget& target)
{
    const auto dxTiles = target.tileLocation.x - originTile.x;
    const auto dyTiles = target.tileLocation.y - originTile.y;
    const auto steps = std::max(std::abs(dxTiles), std::abs(dyTiles));
    if (!RideRatingLocalContextTileIsInRange(origin, steps, target.rangeBaseZ))
    {
        return false;
    }

    if (RideRatingContextRayHasLineOfSight(origin, originTile, target.tileLocation, target.lowerVisibleZ))
    {
        return true;
    }

    return target.upperVisibleZ != target.lowerVisibleZ
        && RideRatingContextRayHasLineOfSight(origin, originTile, target.tileLocation, target.upperVisibleZ);
}

static int32_t RideRatingGetLocalContextWeightedValue(
    const CoordsXYZ& origin, const TileElement& tileElement, int32_t distance, bool applyHeightPenalty)
{
    auto weight = RideRatingGetLocalContextDistanceWeight(distance);
    if (applyHeightPenalty)
    {
        weight = (weight * RideRatingGetLocalContextHeightWeight(origin, tileElement.getClearanceZ()))
            / kRideRatingContextWeightScale;
    }
    return weight;
}

static void RideRatingAccumulateLocalContextElement(
    RideRatingLocalContextRaw& raw, const CoordsXYZ& origin, const TileCoordsXY& originTile, const TileCoordsXY& candidateTile,
    int32_t candidateGroundZ, const TileElement& tileElement, const RideRatingLocalContextQuery& query)
{
    if (tileElement.isGhost())
    {
        return;
    }

    const auto distance = std::max(std::abs(candidateTile.x - originTile.x), std::abs(candidateTile.y - originTile.y));
    const auto isSameTile = distance == 0;
    const auto isOrthogonallyAdjacent = distance == 1 && (candidateTile.x == originTile.x || candidateTile.y == originTile.y);
    const auto visibilityTarget = RideRatingBuildLocalContextVisibilityTarget(candidateTile, tileElement, candidateGroundZ);
    if (!isSameTile && !RideRatingLocalContextTileIsInRange(origin, distance, visibilityTarget.rangeBaseZ))
    {
        return;
    }

    bool lineOfSightCalculated = isSameTile;
    bool lineOfSight = isSameTile;
    const auto hasLineOfSight = [&]() {
        if (!lineOfSightCalculated)
        {
            lineOfSight = RideRatingContextHasLineOfSight(origin, originTile, visibilityTarget);
            lineOfSightCalculated = true;
        }
        return lineOfSight;
    };

    switch (tileElement.getType())
    {
        case TileElementType::Path:
        {
            const auto* pathElement = tileElement.asPath();
            if (pathElement == nullptr)
            {
                break;
            }

            const auto isPathAboveOrigin = tileElement.getBaseZ() > origin.z;
            const auto isPathBelowOrigin = tileElement.getClearanceZ() <= origin.z;
            const auto isVerticalPath = isPathAboveOrigin || isPathBelowOrigin;
            const auto isBridge = isPathAboveOrigin && RideRatingPathQualifiesAsBridge(candidateTile, *pathElement, query);
            if (isSameTile && isBridge)
            {
                raw.hasSameTilePathBridge = true;
            }
            if (query.sampleKind == RideRatingLocalContextSampleKind::vehicle)
            {
                if (isSameTile && isBridge && tileElement.getBaseZ() - origin.z <= kRideRatingBridgeNearMissMaxGap
                    && RideRatingTrackTypeCanPathNearMiss(query.trackType))
                {
                    raw.pathNearMiss += kRideRatingPathNearMissRaw;
                }
                else if (isSameTile && isPathBelowOrigin && RideRatingTrackTypeIsVerticalLoop(query.trackType))
                {
                    raw.pathLoop += kRideRatingPathLoopRaw;
                }
                else if (isOrthogonallyAdjacent && isBridge)
                {
                    raw.pathBridge += (kRideRatingPathBridgeRaw
                                       * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, false))
                        / kRideRatingContextWeightScale;
                }
                else if (!isVerticalPath && hasLineOfSight())
                {
                    raw.pathProximity += (45 * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, false))
                        / kRideRatingContextWeightScale;
                }
                break;
            }

            if (query.sampleKind == RideRatingLocalContextSampleKind::maze)
            {
                if (isOrthogonallyAdjacent && isBridge)
                {
                    raw.pathBridge += (kRideRatingPathBridgeRaw
                                       * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, false))
                        / kRideRatingContextWeightScale;
                }
                else if (!isVerticalPath && hasLineOfSight())
                {
                    raw.pathProximity += (45 * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, false))
                        / kRideRatingContextWeightScale;
                }
                break;
            }

            if (!isVerticalPath && hasLineOfSight())
            {
                raw.pathProximity += (45 * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, false))
                    / kRideRatingContextWeightScale;
            }
            break;
        }
        case TileElementType::Track:
        {
            const auto* trackElement = tileElement.asTrack();
            if (trackElement == nullptr)
            {
                break;
            }

            const auto isSameRide = trackElement->GetRideIndex() == query.rideId;
            if (isSameTile && std::abs(tileElement.getBaseZ() - origin.z) >= kCoordsZStep)
            {
                if (isSameRide)
                {
                    if (query.sampleKind == RideRatingLocalContextSampleKind::vehicle)
                    {
                        raw.ownTrackVerticalInteraction += kRideRatingOwnTrackVerticalRaw;
                    }
                }
                else
                {
                    raw.trackVerticalInteraction += kRideRatingForeignTrackVerticalRaw;
                }
            }
            else if (!isSameRide && hasLineOfSight())
            {
                raw.foreignTrackProximity += (75 * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, false))
                    / kRideRatingContextWeightScale;
            }
            break;
        }
        case TileElementType::Wall:
            if (hasLineOfSight())
            {
                raw.pathProximity += (24 * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, false))
                    / kRideRatingContextWeightScale;
            }
            break;
        default:
        {
            const auto decorationScore = TileElementGetDecorationScore(tileElement);
            if (decorationScore > 0 && hasLineOfSight())
            {
                raw.scenery += (decorationScore * RideRatingGetLocalContextWeightedValue(origin, tileElement, distance, true))
                    / kRideRatingContextWeightScale;
            }
            break;
        }
    }
}

static RideRating::LocalContextScore RideRatingBuildLocalContextScore(
    const CoordsXYZ& origin, const RideRatingLocalContextQuery& query)
{
    PROFILED_FUNCTION();

    RideRatingLocalContextRaw raw{};
    const auto originTile = TileCoordsXY{ CoordsXY{ origin.x, origin.y } };
    auto& gameState = getGameState();

    for (int32_t yy = std::max(originTile.y - kRideRatingContextMaxRadius, 0);
         yy <= std::min(originTile.y + kRideRatingContextMaxRadius, gameState.mapSize.y - 1); yy++)
    {
        for (int32_t xx = std::max(originTile.x - kRideRatingContextMaxRadius, 0);
             xx <= std::min(originTile.x + kRideRatingContextMaxRadius, gameState.mapSize.x - 1); xx++)
        {
            const auto candidateTile = TileCoordsXY{ xx, yy };
            auto* tileElement = MapGetFirstElementAt(candidateTile);
            if (tileElement == nullptr)
            {
                continue;
            }

            const auto candidateGroundZ = RideRatingGetLocalContextGroundZ(candidateTile);
            do
            {
                RideRatingAccumulateLocalContextElement(
                    raw, origin, originTile, candidateTile, candidateGroundZ, *tileElement, query);
            } while (!(tileElement++)->isLastForTile());
        }
    }
    if (query.sampleKind == RideRatingLocalContextSampleKind::vehicle)
    {
        raw.trackHeightExposure = RideRatingGetVehicleHeightExposure(originTile, origin.z, query.trackDirection);
    }

    RideRating::LocalContextScore result{};
    result.scenery = RideRating::ScoreSceneryForLocalContext(raw.scenery);
    if (auto* ride = GetRide(query.rideId); ride != nullptr)
    {
        result.scenery = RideRatingApplySceneryVisibilityMultiplier(result.scenery, *ride);
    }
    result.pathProximity = RideRatingDiminishLocalContext(raw.pathProximity, 10, 220);
    result.foreignTrackProximity = RideRatingDiminishLocalContext(raw.foreignTrackProximity, 14, 220);
    result.pathBridge = RideRatingDiminishLocalContext(raw.hasSameTilePathBridge ? 0 : raw.pathBridge, 10, 180);
    result.pathNearMiss = RideRatingDiminishLocalContext(raw.pathNearMiss, 14, 160);
    result.pathLoop = RideRatingDiminishLocalContext(raw.pathLoop, 16, 160);
    result.trackVerticalInteraction = RideRatingDiminishLocalContext(raw.trackVerticalInteraction, 16, 180);
    result.ownTrackVerticalInteraction = RideRatingDiminishLocalContext(raw.ownTrackVerticalInteraction, 16, 180);
    result.trackHeightExposure = raw.trackHeightExposure;
    result.verticalInteraction = result.pathNearMiss + result.pathLoop + result.trackVerticalInteraction
        + result.ownTrackVerticalInteraction + result.trackHeightExposure;
    result.excitement = (result.scenery * 2) + result.pathProximity + (result.foreignTrackProximity * 2) + result.pathBridge
        + (result.pathNearMiss * 2) + (result.pathLoop * 2) + (result.trackVerticalInteraction * 2)
        + (result.ownTrackVerticalInteraction * 2) + result.trackHeightExposure;
    result.intensity = (result.foreignTrackProximity / 2) + result.pathNearMiss + result.pathLoop
        + result.trackVerticalInteraction + result.ownTrackVerticalInteraction + result.trackHeightExposure;
    result.nausea = ((result.pathNearMiss + result.pathLoop + result.trackVerticalInteraction
                      + result.ownTrackVerticalInteraction)
                     / 3)
        + (result.trackHeightExposure / 3);
    return result;
}

static size_t RideRatingGetLocalContextGenerationIndex(int32_t chunkX, int32_t chunkY)
{
    return static_cast<size_t>(chunkY) * kRideRatingContextGenerationChunksPerAxis + static_cast<size_t>(chunkX);
}

static uint64_t RideRatingGetLocalContextSpatialGeneration(const TileCoordsXY& originTile)
{
    const auto chunkX = std::clamp(originTile.x, 0, kMaximumMapSizeTechnical - 1)
        / kRideRatingContextGenerationChunkSize;
    const auto chunkY = std::clamp(originTile.y, 0, kMaximumMapSizeTechnical - 1)
        / kRideRatingContextGenerationChunkSize;
    return _rideRatingLocalContextOriginGenerations[RideRatingGetLocalContextGenerationIndex(chunkX, chunkY)];
}

static bool RideRatingLocalContextTickIsSheltered(const CoordsXYZ& location)
{
    auto* surfaceElement = MapGetSurfaceElementAt(CoordsXY{ location.x, location.y });
    if (surfaceElement != nullptr && surfaceElement->getBaseZ() <= location.z)
    {
        return TrackGetIsSheltered(location);
    }
    return true;
}

static bool RideRatingRuntimeContextCacheMatches(
    const RideRating::VehicleLocalContextCache& runtimeCache, const RideRatingLocalContextKey& key)
{
    return runtimeCache.valid && runtimeCache.originTile.x == key.x && runtimeCache.originTile.y == key.y
        && runtimeCache.originTile.z == key.z && runtimeCache.rideId.ToUnderlying() == key.rideId
        && static_cast<uint16_t>(runtimeCache.trackType) == key.trackType && runtimeCache.trackDirection == key.trackDirection;
}

static void RideRatingUpdateRuntimeContextCache(
    RideRating::VehicleLocalContextCache& runtimeCache, const RideRatingLocalContextKey& key,
    const RideRatingLocalContextCacheEntry& entry)
{
    runtimeCache.originTile = TileCoordsXYZ{ key.x, key.y, key.z };
    runtimeCache.rideId = RideId::FromUnderlying(key.rideId);
    runtimeCache.trackType = static_cast<TrackElemType>(key.trackType);
    runtimeCache.trackDirection = key.trackDirection;
    runtimeCache.spatialGeneration = entry.spatialGeneration;
    runtimeCache.observedInvalidationGeneration = _rideRatingLocalContextInvalidationGeneration;
    runtimeCache.environment = entry.environment;
    runtimeCache.valid = true;
}

static RideRating::VehicleRatingEnvironment RideRatingGetLocalContextEnvironment(
    const CoordsXYZ& origin, RideRatingLocalContextQuery query, RideRating::VehicleLocalContextCache* runtimeCache = nullptr)
{
    if (origin.x == kLocationNull)
    {
        return {};
    }

    query = RideRatingNormaliseLocalContextQuery(query);
    const auto originTile = TileCoordsXYZ{ origin };
    const auto key = RideRatingLocalContextKey{
        static_cast<int16_t>(originTile.x),
        static_cast<int16_t>(originTile.y),
        static_cast<int16_t>(originTile.z),
        query.rideId.ToUnderlying(),
        static_cast<uint16_t>(query.trackType),
        query.trackDirection,
        query.sampleKind,
    };

    uint64_t spatialGeneration = 0;
    bool hasSpatialGeneration = false;
    const auto getSpatialGeneration = [&]() {
        if (!hasSpatialGeneration)
        {
            spatialGeneration = RideRatingGetLocalContextSpatialGeneration(originTile);
            hasSpatialGeneration = true;
        }
        return spatialGeneration;
    };

    if (runtimeCache != nullptr && RideRatingRuntimeContextCacheMatches(*runtimeCache, key))
    {
        if (runtimeCache->observedInvalidationGeneration == _rideRatingLocalContextInvalidationGeneration)
        {
            return runtimeCache->environment;
        }

        if (runtimeCache->spatialGeneration == getSpatialGeneration())
        {
            runtimeCache->observedInvalidationGeneration = _rideRatingLocalContextInvalidationGeneration;
            return runtimeCache->environment;
        }
    }

    spatialGeneration = getSpatialGeneration();
    auto it = _rideRatingLocalContextCache.find(key);
    if (it != _rideRatingLocalContextCache.end() && it->second.spatialGeneration == spatialGeneration)
    {
        if (runtimeCache != nullptr)
        {
            RideRatingUpdateRuntimeContextCache(*runtimeCache, key, it->second);
        }
        return it->second.environment;
    }

    const auto centredOrigin = CoordsXYZ{ origin.ToTileCentre(), origin.z };
    RideRatingLocalContextCacheEntry entry{
        .environment = {
            .context = RideRatingBuildLocalContextScore(centredOrigin, query),
            .isSheltered = query.sampleKind == RideRatingLocalContextSampleKind::vehicle
                && RideRatingLocalContextTickIsSheltered(centredOrigin),
        },
        .spatialGeneration = spatialGeneration,
    };
    if (it == _rideRatingLocalContextCache.end())
    {
        it = _rideRatingLocalContextCache.emplace(key, entry).first;
    }
    else
    {
        it->second = entry;
    }
    if (runtimeCache != nullptr)
    {
        RideRatingUpdateRuntimeContextCache(*runtimeCache, key, it->second);
    }
    return entry.environment;
}

RideRating::LocalContextScore RideRating::GetLocalContextScore(const CoordsXYZ& origin, RideId rideId)
{
    return RideRatingGetLocalContextEnvironment(
               origin,
               {
                   .rideId = rideId,
                   .sampleKind = RideRatingLocalContextSampleKind::generic,
               })
        .context;
}

RideRating::LocalContextScore RideRating::GetVehicleLocalContextScore(
    const CoordsXYZ& origin, RideId rideId, TrackElemType trackType, uint8_t trackDirection)
{
    return RideRatingGetLocalContextEnvironment(
               origin,
               {
                   .rideId = rideId,
                   .sampleKind = RideRatingLocalContextSampleKind::vehicle,
                   .trackType = trackType,
                   .trackDirection = trackDirection,
               })
        .context;
}

RideRating::VehicleRatingEnvironment RideRating::GetVehicleRatingEnvironment(
    const CoordsXYZ& origin, RideId rideId, TrackElemType trackType, uint8_t trackDirection,
    VehicleLocalContextCache& runtimeCache)
{
    return RideRatingGetLocalContextEnvironment(
        origin,
        {
            .rideId = rideId,
            .sampleKind = RideRatingLocalContextSampleKind::vehicle,
            .trackType = trackType,
            .trackDirection = trackDirection,
        },
        &runtimeCache);
}

RideRating::LocalContextScore RideRating::GetMazeLocalContextScore(const CoordsXYZ& origin, RideId rideId)
{
    return RideRatingGetLocalContextEnvironment(
               origin,
               {
                   .rideId = rideId,
                   .sampleKind = RideRatingLocalContextSampleKind::maze,
               })
        .context;
}

static int32_t RideRatingGetFixedRideDescriptorEyeHeight(const RideTypeDescriptor& rtd)
{
    if (rtd.flags.has(RtdFlag::describeAsInside))
    {
        return kRideRatingContextGroundRideEyeHeight;
    }

    const auto clearanceHeightUnits = static_cast<int32_t>(rtd.Heights.ClearanceHeight) / kCoordsZStep;
    const auto eyeHeightUnits = std::clamp(
        (clearanceHeightUnits * 3) / 10, kRideRatingContextGroundRideEyeHeight / kCoordsZStep,
        kRideRatingContextTowerRideMaxEyeHeight / kCoordsZStep);
    return eyeHeightUnits * kCoordsZStep;
}

static int32_t RideRatingGetFixedRideEyeHeight(const Ride& ride, const RideTypeDescriptor& rtd)
{
    if (rtd.specialType == RtdSpecialType::maze)
    {
        return kRideRatingContextMazeEyeHeight;
    }

    switch (ride.type)
    {
        case RIDE_TYPE_OBSERVATION_TOWER:
        case RIDE_TYPE_ROTO_DROP:
        case RIDE_TYPE_LAUNCHED_FREEFALL:
        case RIDE_TYPE_LIFT:
        {
            const auto towerHeight = std::clamp(
                ToHumanReadableRideLength(ride.getTotalLength()), kRideRatingContextTowerRideMinEyeHeight / kCoordsZStep,
                kRideRatingContextTowerRideMaxEyeHeight / kCoordsZStep);
            return towerHeight * kCoordsZStep;
        }
        case RIDE_TYPE_CHAIRLIFT:
            return kRideRatingContextTowerRideMaxEyeHeight;
        default:
            return RideRatingGetFixedRideDescriptorEyeHeight(rtd);
    }
}

static CoordsXY RideRatingGetFixedRideFootprintCentre(const RideRatingFixedRideLocalContextBase& base)
{
    if (base.trackType == TrackElemType::none)
    {
        return base.location.ToTileCentre();
    }

    const auto& ted = GetTrackElementDescriptor(base.trackType);
    if (ted.sequenceData.numSequences == 0)
    {
        return base.location.ToTileCentre();
    }

    auto minX = std::numeric_limits<int32_t>::max();
    auto minY = std::numeric_limits<int32_t>::max();
    auto maxX = std::numeric_limits<int32_t>::min();
    auto maxY = std::numeric_limits<int32_t>::min();

    for (uint8_t sequenceIndex = 0; sequenceIndex < ted.sequenceData.numSequences; sequenceIndex++)
    {
        const auto& trackBlock = ted.sequenceData.sequences[sequenceIndex].clearance;
        const auto rotatedOffset = CoordsXY{ trackBlock.x, trackBlock.y }.Rotate(base.direction);
        minX = std::min(minX, rotatedOffset.x);
        minY = std::min(minY, rotatedOffset.y);
        maxX = std::max(maxX, rotatedOffset.x + kCoordsXYStep);
        maxY = std::max(maxY, rotatedOffset.y + kCoordsXYStep);
    }

    return base.location + CoordsXY{ (minX + maxX) / 2, (minY + maxY) / 2 };
}

static bool RideRatingGetFixedRideLocalContextBase(const Ride& ride, RideRatingFixedRideLocalContextBase& base)
{
    const auto stationIndex = RideGetFirstValidStationStart(ride);
    if (stationIndex.IsNull())
    {
        return false;
    }

    const auto& rtd = ride.getRideTypeDescriptor();
    if (rtd.specialType == RtdSpecialType::maze)
    {
        base.location = ride.getStation().Entrance.ToCoordsXY();
        base.baseZ = ride.getStation().GetBaseZ();
        return true;
    }

    const auto& station = ride.getStation(stationIndex);
    base.location = station.Start;
    base.baseZ = station.GetBaseZ();
    base.trackType = rtd.StartTrackPiece;

    if (MapIsLocationValid(station.GetStart()))
    {
        auto* stationTrackElement = RideGetStationStartTrackElement(ride, stationIndex);
        auto* trackElement = stationTrackElement != nullptr ? stationTrackElement->asTrack() : nullptr;
        if (trackElement != nullptr)
        {
            base.direction = trackElement->getDirection();
            base.trackType = trackElement->GetTrackType();

            if (const auto origin = GetTrackSegmentOrigin({ station.GetStart(), stationTrackElement }))
            {
                base.location = *origin;
                base.baseZ = origin->z;
            }
        }
    }

    return true;
}

static CoordsXYZ RideRatingBuildFixedRideLocalContextOrigin(const Ride& ride, const RideRatingFixedRideLocalContextBase& base)
{
    return {
        RideRatingGetFixedRideFootprintCentre(base),
        base.baseZ + RideRatingGetFixedRideEyeHeight(ride, ride.getRideTypeDescriptor()),
    };
}

CoordsXYZ RideRating::GetFixedRideLocalContextOrigin(const Ride& ride)
{
    RideRatingFixedRideLocalContextBase base;
    if (!RideRatingGetFixedRideLocalContextBase(ride, base))
    {
        return { kLocationNull, kLocationNull, 0 };
    }

    return RideRatingBuildFixedRideLocalContextOrigin(ride, base);
}

void RideRating::InvalidateLocalContextCacheAround(const CoordsXY& location)
{
    if (location.x == kLocationNull)
    {
        return;
    }

    const auto tileLocation = TileCoordsXY{ location };
    if (tileLocation.x < 0 || tileLocation.y < 0 || tileLocation.x >= kMaximumMapSizeTechnical
        || tileLocation.y >= kMaximumMapSizeTechnical)
    {
        return;
    }

    const auto invalidatedChunkX = tileLocation.x / kRideRatingContextGenerationChunkSize;
    const auto invalidatedChunkY = tileLocation.y / kRideRatingContextGenerationChunkSize;
    const auto minOriginChunkX = std::max(invalidatedChunkX - kRideRatingContextGenerationNeighbourRadius, 0);
    const auto minOriginChunkY = std::max(invalidatedChunkY - kRideRatingContextGenerationNeighbourRadius, 0);
    const auto maxOriginChunkX = std::min(
        invalidatedChunkX + kRideRatingContextGenerationNeighbourRadius,
        static_cast<int32_t>(kRideRatingContextGenerationChunksPerAxis) - 1);
    const auto maxOriginChunkY = std::min(
        invalidatedChunkY + kRideRatingContextGenerationNeighbourRadius,
        static_cast<int32_t>(kRideRatingContextGenerationChunksPerAxis) - 1);
    const auto generation = ++_rideRatingLocalContextInvalidationGeneration;

    // Charge the small neighbourhood update to the infrequent invalidation so
    // the per-vehicle cache-hit path needs only one generation lookup.
    for (auto originChunkY = minOriginChunkY; originChunkY <= maxOriginChunkY; originChunkY++)
    {
        for (auto originChunkX = minOriginChunkX; originChunkX <= maxOriginChunkX; originChunkX++)
        {
            _rideRatingLocalContextOriginGenerations[
                RideRatingGetLocalContextGenerationIndex(originChunkX, originChunkY)] = generation;
        }
    }
}

void RideRating::ClearLocalContextCache()
{
    _rideRatingLocalContextCache.clear();
    _rideRatingLocalContextOriginGenerations.fill(++_rideRatingLocalContextInvalidationGeneration);
}

RideRating::TickScore RideRating::ScoreAirtimeGForTick(int32_t verticalG)
{
    const auto airtimeG = std::clamp(100 - verticalG, 0, 100);
    return {
        .excitement = RideRatingCurveScore(airtimeG, 42.0, 2.15),
        .intensity = RideRatingCurveScore(airtimeG, 11.5, 2.25),
        .nausea = RideRatingCurveScore(airtimeG, 3.4, 1.85),
    };
}

RideRating::TickScore RideRating::ScoreNegativeVerticalGForTick(int32_t verticalG)
{
    const auto negativeG = std::max(-verticalG, 0);
    const auto excessiveNegativeG = std::max(negativeG - 150, 0);
    return {
        .excitement = RideRatingCurveScore(negativeG, 28.0, 2.50),
        .intensity = RideRatingCurveScore(negativeG, 64.0, 2.80) + RideRatingCurveScore(excessiveNegativeG, 160.0, 3.00),
        .nausea = RideRatingCurveScore(negativeG, 18.0, 2.50),
    };
}

RideRating::TickScore RideRating::ScorePositiveVerticalGForTick(int32_t verticalG)
{
    const auto positiveG = std::max(verticalG - 100, 0);
    const auto normalPositiveG = std::min(positiveG, 200);
    const auto excessivePositiveG = std::max(positiveG - 200, 0);
    return {
        .excitement = RideRatingCurveScore(normalPositiveG, 20.0, 4.25) + RideRatingCurveScore(excessivePositiveG, 7.5, 2.75),
        .intensity = RideRatingCurveScore(normalPositiveG, 16.0, 4.75) + RideRatingCurveScore(excessivePositiveG, 84.0, 3.15),
        .nausea = RideRatingCurveScore(positiveG, 7.0, 3.50),
    };
}

RideRating::TickScore RideRating::ScoreLateralGForTick(int32_t lateralG)
{
    const auto sidewaysG = std::abs(lateralG);
    const auto excitementG = std::min(sidewaysG, 200);
    auto excitement = RideRatingCurveScore(excitementG, 10.5, 3.25);

    // The old code had hard penalties around 2.8G and 3.1G. Smoothly taper fun before those landmarks
    // while continuing to compound intensity and nausea.
    if (sidewaysG > 260)
    {
        const auto g = static_cast<double>(sidewaysG) / 100.0;
        const auto excitementScale = std::max(0.35, 1.0 - ((g - 2.60) * 0.35));
        excitement = static_cast<int64_t>(std::llround(static_cast<double>(excitement) * excitementScale));
    }

    const auto severeSidewaysG = std::max(sidewaysG - 200, 0);
    return {
        .excitement = excitement,
        .intensity = RideRatingCurveScore(sidewaysG, 7.5, 4.25) + RideRatingCurveScore(severeSidewaysG, 112.0, 3.15),
        .nausea = RideRatingCurveScore(sidewaysG, 9.0, 3.40) + RideRatingCurveScore(severeSidewaysG, 34.0, 2.80),
    };
}

RideRating::TickScore RideRating::ScoreLongitudinalGForTick(int32_t longitudinalG)
{
    if (longitudinalG >= 0)
    {
        return {
            .excitement = RideRatingCurveScore(longitudinalG, 16.0, 2.20),
            .intensity = RideRatingCurveScore(longitudinalG, 5.5, 2.35),
            .nausea = RideRatingCurveScore(longitudinalG, 4.5, 2.45),
        };
    }

    const auto brakingG = -longitudinalG;
    return {
        .excitement = RideRatingCurveScore(brakingG, 7.0, 2.15),
        .intensity = RideRatingCurveScore(brakingG, 7.0, 2.50),
        .nausea = RideRatingCurveScore(brakingG, 8.0, 2.60),
    };
}

RideRating::TickScore RideRating::ScoreGForcesForTick(int32_t verticalG, int32_t lateralG, int32_t longitudinalG)
{
    TickScore result{};
    RideRatingAddTickScore(result, ScoreAirtimeGForTick(verticalG));
    RideRatingAddTickScore(result, ScoreNegativeVerticalGForTick(verticalG));
    RideRatingAddTickScore(result, ScorePositiveVerticalGForTick(verticalG));
    RideRatingAddTickScore(result, ScoreLateralGForTick(lateralG));
    RideRatingAddTickScore(result, ScoreLongitudinalGForTick(longitudinalG));
    return result;
}

RideRating::TickScore RideRating::ScoreGForcesForVehicleTick(
    int32_t verticalG, int32_t lateralG, int32_t longitudinalG, int32_t speed, const SampledRideRatingProfile& profile)
{
    TickScore result{};

    auto verticalScore = ScoreNegativeVerticalGForTick(verticalG);
    RideRatingAddTickScore(verticalScore, ScorePositiveVerticalGForTick(verticalG));
    RideRatingAddTickScore(result, RideRatingScaleTickScore(verticalScore, profile.VerticalG));
    RideRatingAddTickScore(result, RideRatingScaleTickScore(ScoreAirtimeGForTick(verticalG), profile.Airtime));
    RideRatingAddTickScore(result, RideRatingScaleTickScore(ScoreLateralGForTick(lateralG), profile.LateralG));
    RideRatingAddTickScore(result, RideRatingScaleTickScore(ScoreLongitudinalGForTick(longitudinalG), profile.LongitudinalG));

    return RideRatingApplySpeedGCoupling(result, speed, profile.SpeedGCoupling);
}

RideRating::TickScore RideRating::ScoreVehicleSpeedForTick(int32_t speed, int32_t coefficient)
{
    const auto normalisedSpeed = std::max<int64_t>(speed, 0);
    const auto speedRatio = static_cast<double>(normalisedSpeed) / kVehicleRatingBaselineSpeed;
    const auto excitementSpeed = static_cast<int64_t>(std::llround(normalisedSpeed * std::sqrt(speedRatio)));
    return RideRatingScaleTickScore(
        {
            .excitement = (excitementSpeed * kRideRatingAccumulatorRawScale) / 5,
            .intensity = (normalisedSpeed * kRideRatingAccumulatorRawScale) / 4,
            .nausea = (normalisedSpeed * kRideRatingAccumulatorRawScale) / 8,
        },
        coefficient);
}

RideRating::TransportQualityScore RideRating::ScoreTransportQualityForVehicleTick(
    int32_t verticalG, int32_t lateralG, int32_t longitudinalG, int32_t speed, const LocalContextScore& contextScore)
{
    const auto distance = std::max<int64_t>(speed, 0);
    const auto verticalDeviation = std::abs(verticalG - 100);
    const auto forcePenalty = (verticalDeviation * 2) + (std::abs(lateralG) * 3) + (std::abs(longitudinalG) * 3);
    const auto comfortFactor = std::clamp<int64_t>(1000 - forcePenalty, 100, 1000);
    const auto decorationFactor = 1000 + std::clamp<int64_t>(static_cast<int64_t>(contextScore.scenery) * 5, 0, 500);

    return {
        .comfort = distance * comfortFactor,
        .decoration = distance * decorationFactor,
        .distance = distance,
    };
}

RideRating::TickScore RideRating::ApplyRideEntryMultipliers(TickScore score, const RideObjectEntry& rideEntry)
{
    score.excitement += (score.excitement * rideEntry.excitement_multiplier) >> 7;
    score.intensity += (score.intensity * rideEntry.intensity_multiplier) >> 7;
    score.nausea += (score.nausea * rideEntry.nausea_multiplier) >> 7;
    return score;
}

static void ride_ratings_update_state(RideRating::UpdateState& state);
static void ride_ratings_update_state_0(RideRating::UpdateState& state);
static void ride_ratings_update_state_1(RideRating::UpdateState& state);
static void ride_ratings_update_state_2(RideRating::UpdateState& state);
static void ride_ratings_update_state_3(RideRating::UpdateState& state);
static void ride_ratings_update_state_4(RideRating::UpdateState& state);
static void ride_ratings_update_state_5(RideRating::UpdateState& state);
static void ride_ratings_begin_proximity_loop(RideRating::UpdateState& state);
static void RideRatingsCalculate(RideRating::UpdateState& state, Ride& ride);
static void RideRatingsCalculateValue(Ride& ride);
static void ride_ratings_score_close_proximity(RideRating::UpdateState& state, TileElement* inputTileElement);
static void RideRatingsAdd(RideRating::Tuple& ratings, int32_t excitement, int32_t intensity, int32_t nausea);
static RideRating::Tuple RideRatingsCalculateAggregated(const Ride& ride, const RideRatingAccumulator& accumulator);
static RideRating::Tuple RideRatingsCalculateLegCompatibility(const Ride& ride);
static bool RideRatingsHaveCompleteLegCoverage(const Ride& ride);

static ShelteredEights GetNumOfShelteredEighths(const Ride& ride);
static money64 RideComputeUpkeep(RideRating::UpdateState& state, const Ride& ride);
static void SetUnreliabilityFactor(Ride& ride);

static void RideRatingsApplyAdjustments(const Ride& ride, RideRating::Tuple& ratings);
static void RideRatingsApplyIntensityPenalty(RideRating::Tuple& ratings);

static void RideRatingsApplyBonusLength(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusSynchronisation(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusTrainLength(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusMaxSpeed(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusAverageSpeed(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusDuration(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusGForces(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusTurns(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusDrops(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusSheltered(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusRotations(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusOperationOption(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusReversedTrains(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusGoKartRace(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusTowerRide(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusRotoDrop(RideRating::Tuple& ratings, const Ride& ride);
static void RideRatingsApplyBonusMazeSize(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusBoatHireNoCircuit(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusSlideUnlimitedRides(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusMotionSimulatorMode(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonus3DCinemaMode(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusTopSpinMode(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusReversals(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier);
static void RideRatingsApplyBonusHoles(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusNumTrains(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusDownwardLaunch(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyBonusLaunchedFreefallSpecial(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier);
static void RideRatingsApplyBonusProximity(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier);
static void RideRatingsApplyBonusScenery(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementLength(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementDropHeight(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementMaxSpeed(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementNumDrops(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementNegativeGs(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementLateralGs(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementInversions(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementUnsheltered(
    RideRating::Tuple& ratings, const Ride& ride, uint8_t shelteredEighths, RatingsModifier modifier);
static void RideRatingsApplyRequirementReversals(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier);
static void RideRatingsApplyRequirementHoles(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementStations(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyRequirementSplashdown(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);
static void RideRatingsApplyPenaltyLateralGs(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier);

static bool RideRatingsUsesAggregateSamples(const Ride& ride)
{
    const auto& rtd = ride.getRideTypeDescriptor();
    return rtd.RatingsData.Type == RatingsCalculationType::Normal || rtd.specialType == RtdSpecialType::maze;
}

void RideRating::ResetUpdateStates()
{
    UpdateState nullState{};
    nullState.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;

    auto& updateStates = getGameState().rideRatingUpdateStates;
    std::fill(updateStates.begin(), updateStates.end(), nullState);
}

/**
 * This is a small hack function to keep calling the ride rating processor until
 * the given ride's ratings have been calculated. Whatever is currently being
 * processed will be overwritten.
 * Only purpose of this function currently is for testing.
 */
void RideRating::UpdateRide(const Ride& ride)
{
    if (ride.status != RideStatus::closed)
    {
        UpdateState state;
        state.CurrentRide = ride.id;
        state.State = RIDE_RATINGS_STATE_INITIALISE;
        while (state.State != RIDE_RATINGS_STATE_FIND_NEXT_RIDE)
        {
            ride_ratings_update_state(state);
        }
    }
}

void RideRating::RecordRiderSample(Ride& ride, const RideRatingAccumulator& sample)
{
    if (!sample.hasSamples() || ride.flags.has(RideFlag::fixedRatings))
    {
        return;
    }

    RideAddRecentRatingSample(ride, sample);
    if (RideRatingsUsesAggregateSamples(ride))
    {
        ride.flags.set(RideFlag::tested);
        if (ride.numStations > 1)
        {
            if (!sample.originStation.IsNull() && !sample.destinationStation.IsNull()
                && sample.originStation != sample.destinationStation)
            {
                auto* leg = RideGetRatingLeg(ride, sample.originStation, sample.destinationStation);
                if (leg != nullptr)
                {
                    const auto legAccumulator = RideGetRecentRatingAccumulator(*leg);
                    if (legAccumulator.hasSamples())
                    {
                        leg->ratings = RideRatingsCalculateAggregated(ride, legAccumulator);
                    }
                }
            }
            if (RideRatingsHaveCompleteLegCoverage(ride))
            {
                const auto ratings = RideRatingsCalculateLegCompatibility(ride);
                if (ride.ratings != ratings)
                {
                    ride.ratings = ratings;
                    ride.windowInvalidateFlags.set(RideInvalidateFlag::ratings);
                }
            }
        }
        else
        {
            const auto recentAccumulator = RideGetRecentRatingAccumulator(ride);
            if (recentAccumulator.hasSamples())
            {
                const auto ratings = RideRatingsCalculateAggregated(ride, recentAccumulator);
                if (ride.ratings != ratings)
                {
                    ride.ratings = ratings;
                    ride.windowInvalidateFlags.set(RideInvalidateFlag::ratings);
                }
            }
        }
    }
    else
    {
        // Non-aggregate rating types still depend on the legacy track-wide state.
        UpdateRide(ride);
    }

    // Track proximity, shelter, upkeep, and scripting hooks remain on UpdateAll's bounded
    // incremental state machine. Running an entire track scan synchronously every time a train
    // unloads can monopolise the title/main thread in ride-heavy parks while audio keeps playing.
    ride.windowInvalidateFlags.set(RideInvalidateFlag::ratings);
}

bool RideRating::RecordActiveRiderSample(Ride& ride, EntityId sampleEntity)
{
    std::array<EntityId, 1> sampleEntities = { sampleEntity };
    return RecordActiveRiderSamples(ride, sampleEntities);
}

bool RideRating::RecordActiveRiderSamples(Ride& ride, std::span<const EntityId> sampleEntities)
{
    RideRatingAccumulator combinedSample{};
    size_t completedSampleCount = 0;

    for (const auto sampleEntity : sampleEntities)
    {
        auto* accumulator = RideFindActiveRatingSample(ride, sampleEntity);
        if (accumulator == nullptr || !accumulator->hasSamples())
        {
            continue;
        }

        if (completedSampleCount == 0)
        {
            combinedSample.originStation = accumulator->originStation;
            combinedSample.destinationStation = accumulator->destinationStation;
        }
        else if (combinedSample.originStation != accumulator->originStation
            || combinedSample.destinationStation != accumulator->destinationStation)
        {
            continue;
        }
        combinedSample.excitement += accumulator->excitement;
        combinedSample.intensity += accumulator->intensity;
        combinedSample.nausea += accumulator->nausea;
        combinedSample.transportComfort += accumulator->transportComfort;
        combinedSample.transportDecoration += accumulator->transportDecoration;
        combinedSample.transportDistance += accumulator->transportDistance;
        combinedSample.sampledDistance += accumulator->sampledDistance;
        combinedSample.totalSpeed += accumulator->totalSpeed;
        combinedSample.maxSpeed = std::max(combinedSample.maxSpeed, accumulator->maxSpeed);
        combinedSample.maxPositiveVerticalG = std::max(
            combinedSample.maxPositiveVerticalG, accumulator->maxPositiveVerticalG);
        combinedSample.maxNegativeVerticalG = std::min(
            combinedSample.maxNegativeVerticalG, accumulator->maxNegativeVerticalG);
        combinedSample.maxLateralG = std::max(combinedSample.maxLateralG, accumulator->maxLateralG);
        combinedSample.maxPositiveLongitudinalG = std::max(
            combinedSample.maxPositiveLongitudinalG, accumulator->maxPositiveLongitudinalG);
        combinedSample.maxNegativeLongitudinalG = std::min(
            combinedSample.maxNegativeLongitudinalG, accumulator->maxNegativeLongitudinalG);
        combinedSample.ticks += accumulator->ticks;
        completedSampleCount++;
    }

    if (completedSampleCount == 0)
    {
        return false;
    }

    combinedSample.excitement /= static_cast<int64_t>(completedSampleCount);
    combinedSample.intensity /= static_cast<int64_t>(completedSampleCount);
    combinedSample.nausea /= static_cast<int64_t>(completedSampleCount);
    combinedSample.transportComfort /= static_cast<int64_t>(completedSampleCount);
    combinedSample.transportDecoration /= static_cast<int64_t>(completedSampleCount);
    combinedSample.transportDistance /= static_cast<int64_t>(completedSampleCount);
    combinedSample.sampledDistance /= static_cast<int64_t>(completedSampleCount);
    combinedSample.totalSpeed /= static_cast<int64_t>(completedSampleCount);
    combinedSample.ticks = std::max<uint32_t>(1, combinedSample.ticks / static_cast<uint32_t>(completedSampleCount));

    RecordRiderSample(ride, combinedSample);

    for (const auto sampleEntity : sampleEntities)
    {
        if (auto* accumulator = RideFindActiveRatingSample(ride, sampleEntity); accumulator != nullptr)
        {
            accumulator->clear();
        }
    }

    return true;
}

/**
 *
 *  rct2: 0x006B5A2A
 */
void RideRating::UpdateAll()
{
    PROFILED_FUNCTION();

    if (gLegacyScene == LegacyScene::scenarioEditor)
        return;

    for (auto& updateState : getGameState().rideRatingUpdateStates)
    {
        for (size_t i = 0; i < MaxRideRatingUpdateSubSteps; ++i)
        {
            ride_ratings_update_state(updateState);

            // We need to abort the loop if the state machine requested to find the next ride.
            if (updateState.State == RIDE_RATINGS_STATE_FIND_NEXT_RIDE)
                break;
        }
    }
}

static void ride_ratings_update_state(RideRating::UpdateState& state)
{
    switch (state.State)
    {
        case RIDE_RATINGS_STATE_FIND_NEXT_RIDE:
            ride_ratings_update_state_0(state);
            break;
        case RIDE_RATINGS_STATE_INITIALISE:
            ride_ratings_update_state_1(state);
            break;
        case RIDE_RATINGS_STATE_2:
            ride_ratings_update_state_2(state);
            break;
        case RIDE_RATINGS_STATE_CALCULATE:
            ride_ratings_update_state_3(state);
            break;
        case RIDE_RATINGS_STATE_4:
            ride_ratings_update_state_4(state);
            break;
        case RIDE_RATINGS_STATE_5:
            ride_ratings_update_state_5(state);
            break;
    }
}

static bool RideRatingIsUpdatingRide(RideId id)
{
    const auto& updateStates = getGameState().rideRatingUpdateStates;
    return std::any_of(updateStates.begin(), updateStates.end(), [id](auto& state) {
        return state.CurrentRide == id && state.State != RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
    });
}

static bool ShouldSkipRatingCalculation(const Ride& ride)
{
    // Skip rides that are closed.
    if (ride.status == RideStatus::closed)
    {
        return true;
    }

    // Skip anything that is already updating.
    if (RideRatingIsUpdatingRide(ride.id))
    {
        return true;
    }

    // Skip rides that have a fixed rating.
    if (ride.flags.has(RideFlag::fixedRatings))
    {
        return true;
    }

    return false;
}

static RideId GetNextRideToUpdate(RideId currentRide)
{
    auto& gameState = getGameState();
    auto rm = RideManager(gameState);
    if (rm.size() == 0)
    {
        return RideId::GetNull();
    }

    auto it = rm.get(currentRide);
    if (it == rm.end())
    {
        // Start at the beginning, ride is missing.
        it = rm.begin();
    }
    else
    {
        it = std::next(it);
    }

    // Filter out rides to avoid wasting a tick to find the next ride.
    while (it != rm.end() && ShouldSkipRatingCalculation(*it))
    {
        it++;
    }

    // If we reached the end of the list we start over,
    // in case the next ride doesn't pass the filter function it will
    // look for the next matching ride in the next tick.
    if (it == rm.end())
        it = rm.begin();

    return (*it).id;
}

/**
 *
 *  rct2: 0x006B5A5C
 */
static void ride_ratings_update_state_0(RideRating::UpdateState& state)
{
    // It is possible that the current ride being calculated has
    // been removed or due to import invalid. For both, reset
    // ratings and start check at the start
    if (GetRide(state.CurrentRide) == nullptr)
    {
        state.CurrentRide = {};
    }

    const auto nextRideId = GetNextRideToUpdate(state.CurrentRide);
    const auto* nextRide = GetRide(nextRideId);
    if (nextRide != nullptr && !ShouldSkipRatingCalculation(*nextRide))
    {
        Guard::Assert(!RideRatingIsUpdatingRide(nextRideId));
        state.State = RIDE_RATINGS_STATE_INITIALISE;
    }
    state.CurrentRide = nextRideId;
}

/**
 *
 *  rct2: 0x006B5A94
 */
static void ride_ratings_update_state_1(RideRating::UpdateState& state)
{
    state.ProximityTotal = 0;
    for (int32_t i = 0; i < PROXIMITY_COUNT; i++)
    {
        state.ProximityScores[i] = 0;
    }
    state.AmountOfBrakes = 0;
    state.amountOfBoosters = 0;
    state.AmountOfReversers = 0;
    state.State = RIDE_RATINGS_STATE_2;
    state.StationFlags = 0;
    ride_ratings_begin_proximity_loop(state);
}

/**
 *
 *  rct2: 0x006B5C66
 */
static void ride_ratings_update_state_2(RideRating::UpdateState& state)
{
    const RideId rideIndex = state.CurrentRide;
    auto ride = GetRide(rideIndex);
    if (ride == nullptr || ride->status == RideStatus::closed || ride->type >= RIDE_TYPE_COUNT)
    {
        state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    auto loc = state.Proximity;
    TrackElemType trackType = state.ProximityTrackType;

    TileElement* tileElement = MapGetFirstElementAt(loc);
    if (tileElement == nullptr)
    {
        state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }
    do
    {
        if (tileElement->isGhost())
            continue;
        if (tileElement->getType() != TileElementType::Track)
            continue;
        if (tileElement->getBaseZ() != loc.z)
            continue;
        if (tileElement->asTrack()->GetRideIndex() != ride->id)
        {
            // Only check that the track belongs to the same ride if ride does not have buildable track
            if (!ride->getRideTypeDescriptor().flags.has(RtdFlag::hasTrack))
                continue;
        }

        if (trackType == TrackElemType::none
            || (tileElement->asTrack()->GetSequenceIndex() == 0 && trackType == tileElement->asTrack()->GetTrackType()))
        {
            if (trackType == TrackElemType::endStation)
            {
                auto entranceIndex = tileElement->asTrack()->GetStationIndex();
                state.StationFlags &= ~RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
                if (ride->getStation(entranceIndex).Entrance.IsNull())
                {
                    state.StationFlags |= RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
                }
            }

            ride_ratings_score_close_proximity(state, tileElement);

            CoordsXYE trackElement = { state.Proximity, tileElement };
            CoordsXYE nextTrackElement;
            if (!trackBlockGetNext(&trackElement, &nextTrackElement, nullptr, nullptr))
            {
                state.State = RIDE_RATINGS_STATE_4;
                return;
            }

            loc = { nextTrackElement, nextTrackElement.element->getBaseZ() };
            tileElement = nextTrackElement.element;
            if (loc == state.ProximityStart)
            {
                state.State = RIDE_RATINGS_STATE_CALCULATE;
                return;
            }
            state.Proximity = loc;
            state.ProximityTrackType = tileElement->asTrack()->GetTrackType();
            return;
        }
    } while (!(tileElement++)->isLastForTile());

    state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

/**
 *
 *  rct2: 0x006B5E4D
 */
static void ride_ratings_update_state_3(RideRating::UpdateState& state)
{
    auto ride = GetRide(state.CurrentRide);
    if (ride == nullptr || ride->status == RideStatus::closed)
    {
        state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    RideRatingsCalculate(state, *ride);
    RideRatingsCalculateValue(*ride);

    state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

/**
 *
 *  rct2: 0x006B5BAB
 */
static void ride_ratings_update_state_4(RideRating::UpdateState& state)
{
    state.State = RIDE_RATINGS_STATE_5;
    ride_ratings_begin_proximity_loop(state);
}

/**
 *
 *  rct2: 0x006B5D72
 */
static void ride_ratings_update_state_5(RideRating::UpdateState& state)
{
    auto ride = GetRide(state.CurrentRide);
    if (ride == nullptr || ride->status == RideStatus::closed)
    {
        state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    auto loc = state.Proximity;
    TrackElemType trackType = state.ProximityTrackType;

    TileElement* tileElement = MapGetFirstElementAt(loc);
    if (tileElement == nullptr)
    {
        state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }
    do
    {
        if (tileElement->isGhost())
            continue;
        if (tileElement->getType() != TileElementType::Track)
            continue;
        if (tileElement->getBaseZ() != loc.z)
            continue;
        if (tileElement->asTrack()->GetRideIndex() != ride->id)
        {
            // Only check that the track belongs to the same ride if ride does not have buildable track
            if (!ride->getRideTypeDescriptor().flags.has(RtdFlag::hasTrack))
                continue;
        }

        if (trackType == TrackElemType::none || trackType == tileElement->asTrack()->GetTrackType())
        {
            ride_ratings_score_close_proximity(state, tileElement);

            TrackBeginEnd trackBeginEnd;
            if (!trackBlockGetPrevious({ state.Proximity, tileElement }, &trackBeginEnd))
            {
                state.State = RIDE_RATINGS_STATE_CALCULATE;
                return;
            }

            loc.x = trackBeginEnd.begin_x;
            loc.y = trackBeginEnd.begin_y;
            loc.z = trackBeginEnd.begin_z;
            if (loc == state.ProximityStart)
            {
                state.State = RIDE_RATINGS_STATE_CALCULATE;
                return;
            }
            state.Proximity = loc;
            state.ProximityTrackType = trackBeginEnd.begin_element->asTrack()->GetTrackType();
            return;
        }
    } while (!(tileElement++)->isLastForTile());

    state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

/**
 *
 *  rct2: 0x006B5BB2
 */
static void ride_ratings_begin_proximity_loop(RideRating::UpdateState& state)
{
    auto ride = GetRide(state.CurrentRide);
    if (ride == nullptr || ride->status == RideStatus::closed)
    {
        state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    const auto& rtd = ride->getRideTypeDescriptor();
    if (rtd.specialType == RtdSpecialType::maze)
    {
        state.State = RIDE_RATINGS_STATE_CALCULATE;
        return;
    }

    for (auto& station : ride->getStations())
    {
        if (!station.Start.IsNull())
        {
            state.StationFlags &= ~RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
            if (station.Entrance.IsNull())
            {
                state.StationFlags |= RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
            }

            auto location = station.GetStart();
            state.Proximity = location;
            state.ProximityTrackType = TrackElemType::none;
            state.ProximityStart = location;
            return;
        }
    }

    state.State = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

static void proximity_score_increment(RideRating::UpdateState& state, int32_t type)
{
    state.ProximityScores[type]++;
}

/**
 *
 *  rct2: 0x006B6207
 */
static void ride_ratings_score_close_proximity_in_direction(
    RideRating::UpdateState& state, TileElement* inputTileElement, int32_t direction)
{
    auto scorePos = CoordsXY{ CoordsXY{ state.Proximity } + CoordsDirectionDelta[direction] };
    if (!MapIsLocationValid(scorePos))
        return;

    TileElement* tileElement = MapGetFirstElementAt(scorePos);
    if (tileElement == nullptr)
        return;
    do
    {
        if (tileElement->isGhost())
            continue;

        switch (tileElement->getType())
        {
            case TileElementType::Surface:
                if (TileElementCountsAsDecoration(*tileElement))
                {
                    proximity_score_increment(state, PROXIMITY_SCENERY_SIDE_BELOW);
                }
                if (state.ProximityBaseHeight <= inputTileElement->baseHeight)
                {
                    if (inputTileElement->clearanceHeight <= tileElement->baseHeight)
                    {
                        proximity_score_increment(state, PROXIMITY_SURFACE_SIDE_CLOSE);
                    }
                }
                break;
            case TileElementType::Path:
            {
                const auto* pathElement = tileElement->asPath();
                const RideRatingLocalContextQuery query{
                    .rideId = inputTileElement->asTrack()->GetRideIndex(),
                    .sampleKind = RideRatingLocalContextSampleKind::vehicle,
                    .trackType = inputTileElement->asTrack()->GetTrackType(),
                    .trackDirection = inputTileElement->getDirection(),
                };
                if (pathElement != nullptr && tileElement->getBaseZ() >= inputTileElement->getClearanceZ()
                    && RideRatingPathQualifiesAsBridge(TileCoordsXY{ scorePos }, *pathElement, query))
                {
                    proximity_score_increment(state, PROXIMITY_PATH_TOUCH_ABOVE);
                }
                else if (abs(inputTileElement->getBaseZ() - tileElement->getBaseZ()) <= 2 * kCoordsZStep)
                {
                    proximity_score_increment(state, PROXIMITY_PATH_SIDE_CLOSE);
                }
                break;
            }
            case TileElementType::Track:
                if (inputTileElement->asTrack()->GetRideIndex() != tileElement->asTrack()->GetRideIndex())
                {
                    if (abs(inputTileElement->getBaseZ() - tileElement->getBaseZ()) <= 2 * kCoordsZStep)
                    {
                        proximity_score_increment(state, PROXIMITY_FOREIGN_TRACK_SIDE_CLOSE);
                    }
                }
                break;
            case TileElementType::SmallScenery:
            case TileElementType::LargeScenery:
                if (tileElement->getBaseZ() < inputTileElement->getClearanceZ())
                {
                    if (inputTileElement->getBaseZ() > tileElement->getClearanceZ())
                    {
                        proximity_score_increment(state, PROXIMITY_SCENERY_SIDE_ABOVE);
                    }
                    else
                    {
                        proximity_score_increment(state, PROXIMITY_SCENERY_SIDE_BELOW);
                    }
                }
                break;
            default:
                break;
        }
    } while (!(tileElement++)->isLastForTile());
}

static void ride_ratings_score_close_proximity_loops_helper(RideRating::UpdateState& state, const CoordsXYE& coordsElement)
{
    TileElement* tileElement = MapGetFirstElementAt(coordsElement);
    if (tileElement == nullptr)
        return;
    do
    {
        if (tileElement->isGhost())
            continue;

        auto type = tileElement->getType();
        if (type == TileElementType::Path)
        {
            int32_t zDiff = static_cast<int32_t>(tileElement->baseHeight)
                - static_cast<int32_t>(coordsElement.element->baseHeight);
            if (zDiff >= 0 && zDiff <= 16)
            {
                proximity_score_increment(state, PROXIMITY_PATH_TROUGH_VERTICAL_LOOP);
            }
        }
        else if (type == TileElementType::Track)
        {
            bool elementsAreAt90DegAngle = ((tileElement->getDirection() ^ coordsElement.element->getDirection()) & 1) != 0;
            if (elementsAreAt90DegAngle)
            {
                int32_t zDiff = static_cast<int32_t>(tileElement->baseHeight)
                    - static_cast<int32_t>(coordsElement.element->baseHeight);
                if (zDiff >= 0 && zDiff <= 16)
                {
                    proximity_score_increment(state, PROXIMITY_TRACK_THROUGH_VERTICAL_LOOP);
                    if (tileElement->asTrack()->GetTrackType() == TrackElemType::leftVerticalLoop
                        || tileElement->asTrack()->GetTrackType() == TrackElemType::rightVerticalLoop)
                    {
                        proximity_score_increment(state, PROXIMITY_INTERSECTING_VERTICAL_LOOP);
                    }
                }
            }
        }
    } while (!(tileElement++)->isLastForTile());
}

/**
 *
 *  rct2: 0x006B62DA
 */
static void ride_ratings_score_close_proximity_loops(RideRating::UpdateState& state, TileElement* inputTileElement)
{
    auto trackType = inputTileElement->asTrack()->GetTrackType();
    if (trackType == TrackElemType::leftVerticalLoop || trackType == TrackElemType::rightVerticalLoop)
    {
        ride_ratings_score_close_proximity_loops_helper(state, { state.Proximity, inputTileElement });

        int32_t direction = inputTileElement->getDirection();
        ride_ratings_score_close_proximity_loops_helper(
            state, { CoordsXY{ state.Proximity } + CoordsDirectionDelta[direction], inputTileElement });
    }
}

/**
 *
 *  rct2: 0x006B5F9D
 */
static void ride_ratings_score_close_proximity(RideRating::UpdateState& state, TileElement* inputTileElement)
{
    if (state.StationFlags & RIDE_RATING_STATION_FLAG_NO_ENTRANCE)
    {
        return;
    }

    state.ProximityTotal++;
    TileElement* tileElement = MapGetFirstElementAt(state.Proximity);
    if (tileElement == nullptr)
        return;
    do
    {
        if (tileElement->isGhost())
            continue;

        int32_t waterHeight;
        switch (tileElement->getType())
        {
            case TileElementType::Surface:
                state.ProximityBaseHeight = tileElement->baseHeight;
                if (tileElement->getBaseZ() == state.Proximity.z)
                {
                    proximity_score_increment(state, PROXIMITY_SURFACE_TOUCH);
                }
                waterHeight = tileElement->asSurface()->GetWaterHeight();
                if (waterHeight != 0)
                {
                    auto z = waterHeight;
                    if (z <= state.Proximity.z)
                    {
                        proximity_score_increment(state, PROXIMITY_WATER_OVER);
                        if (z == state.Proximity.z)
                        {
                            proximity_score_increment(state, PROXIMITY_WATER_TOUCH);
                        }
                        z += 16;
                        if (z == state.Proximity.z)
                        {
                            proximity_score_increment(state, PROXIMITY_WATER_LOW);
                        }
                        z += 112;
                        if (z <= state.Proximity.z)
                        {
                            proximity_score_increment(state, PROXIMITY_WATER_HIGH);
                        }
                    }
                }
                break;
            case TileElementType::Path:
                break;
            case TileElementType::Track:
            {
                auto trackType = tileElement->asTrack()->GetTrackType();
                if (trackType == TrackElemType::leftVerticalLoop || trackType == TrackElemType::rightVerticalLoop)
                {
                    int32_t sequence = tileElement->asTrack()->GetSequenceIndex();
                    if (sequence == 3 || sequence == 6)
                    {
                        if (tileElement->baseHeight - inputTileElement->clearanceHeight <= 10)
                        {
                            proximity_score_increment(state, PROXIMITY_THROUGH_VERTICAL_LOOP);
                        }
                    }
                }
                if (inputTileElement->asTrack()->GetRideIndex() != tileElement->asTrack()->GetRideIndex())
                {
                    proximity_score_increment(state, PROXIMITY_FOREIGN_TRACK_ABOVE_OR_BELOW);
                    if (tileElement->getClearanceZ() == inputTileElement->getBaseZ())
                    {
                        proximity_score_increment(state, PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE);
                    }
                    if (tileElement->clearanceHeight + 2 <= inputTileElement->baseHeight)
                    {
                        if (tileElement->clearanceHeight + 10 >= inputTileElement->baseHeight)
                        {
                            proximity_score_increment(state, PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE);
                        }
                    }
                    if (inputTileElement->clearanceHeight == tileElement->baseHeight)
                    {
                        proximity_score_increment(state, PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE);
                    }
                    if (inputTileElement->clearanceHeight + 2 == tileElement->baseHeight)
                    {
                        if (static_cast<uint8_t>(inputTileElement->clearanceHeight + 10) >= tileElement->baseHeight)
                        {
                            proximity_score_increment(state, PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE);
                        }
                    }
                }
                else
                {
                    bool isStation = tileElement->asTrack()->IsStation();
                    if (tileElement->clearanceHeight == inputTileElement->baseHeight)
                    {
                        proximity_score_increment(state, PROXIMITY_OWN_TRACK_TOUCH_ABOVE);
                        if (isStation)
                        {
                            proximity_score_increment(state, PROXIMITY_OWN_STATION_TOUCH_ABOVE);
                        }
                    }
                    if (tileElement->clearanceHeight + 2 <= inputTileElement->baseHeight)
                    {
                        if (tileElement->clearanceHeight + 10 >= inputTileElement->baseHeight)
                        {
                            proximity_score_increment(state, PROXIMITY_OWN_TRACK_CLOSE_ABOVE);
                            if (isStation)
                            {
                                proximity_score_increment(state, PROXIMITY_OWN_STATION_CLOSE_ABOVE);
                            }
                        }
                    }

                    if (inputTileElement->getClearanceZ() == tileElement->getBaseZ())
                    {
                        proximity_score_increment(state, PROXIMITY_OWN_TRACK_TOUCH_ABOVE);
                        if (isStation)
                        {
                            proximity_score_increment(state, PROXIMITY_OWN_STATION_TOUCH_ABOVE);
                        }
                    }
                    if (inputTileElement->clearanceHeight + 2 <= tileElement->baseHeight)
                    {
                        if (inputTileElement->clearanceHeight + 10 >= tileElement->baseHeight)
                        {
                            proximity_score_increment(state, PROXIMITY_OWN_TRACK_CLOSE_ABOVE);
                            if (isStation)
                            {
                                proximity_score_increment(state, PROXIMITY_OWN_STATION_CLOSE_ABOVE);
                            }
                        }
                    }
                }
                break;
            }
            default:
                break;
        } // switch tileElement->getType
    } while (!(tileElement++)->isLastForTile());

    uint8_t direction = inputTileElement->getDirection();
    ride_ratings_score_close_proximity_in_direction(state, inputTileElement, (direction + 1) & 3);
    ride_ratings_score_close_proximity_in_direction(state, inputTileElement, (direction - 1) & 3);
    ride_ratings_score_close_proximity_loops(state, inputTileElement);

    if (trackTypeIsBrakes(state.ProximityTrackType))
        state.AmountOfBrakes++;
    else if (trackTypeIsBooster(state.ProximityTrackType))
        state.amountOfBoosters++;
    else if (trackTypeIsReverser(state.ProximityTrackType))
        state.AmountOfReversers++;
}

static void RideRatingsCalculate(RideRating::UpdateState& state, Ride& ride)
{
    const auto& rtd = ride.getRideTypeDescriptor();
    const auto& rrd = rtd.RatingsData;

    switch (rrd.Type)
    {
        case RatingsCalculationType::Normal:
            if (!ride.flags.has(RideFlag::tested))
                return;
            break;
        case RatingsCalculationType::FlatRide:
            ride.flags.set(RideFlag::tested, RideFlag::noRawStats);
            break;
        case RatingsCalculationType::Stall:
            ride.upkeepCost = RideComputeUpkeep(state, ride);
            ride.windowInvalidateFlags.set(RideInvalidateFlag::income);
            // Exit ratings
            return;
    }

    ride.unreliabilityFactor = rrd.Unreliability;
    SetUnreliabilityFactor(ride);

    const auto shelteredEighths = GetNumOfShelteredEighths(ride);
    ride.shelteredEighths = (rrd.RideShelter == kDynamicRideShelterRating) ? shelteredEighths.TotalShelteredEighths
                                                                           : rrd.RideShelter;

    RideRating::Tuple ratings{};
    const auto recentAccumulator = RideGetRecentRatingAccumulator(ride);
    const RideRatingAccumulator* aggregateAccumulator = nullptr;
    if (recentAccumulator.hasSamples())
    {
        aggregateAccumulator = &recentAccumulator;
    }

    const bool aggregateRatingType = RideRatingsUsesAggregateSamples(ride);
    if (aggregateRatingType && ride.numStations > 1)
    {
        ratings = RideRatingsHaveCompleteLegCoverage(ride) ? RideRatingsCalculateLegCompatibility(ride) : ride.ratings;
    }
    else if (aggregateRatingType && aggregateAccumulator != nullptr)
    {
        ratings = RideRatingsCalculateAggregated(ride, *aggregateAccumulator);
    }
    else if (aggregateRatingType)
    {
        ratings = ride.flags.has(RideFlag::tested) ? ride.ratings : RideRating::Tuple{};
    }
    else
    {
        ratings = rrd.BaseRatings;

        // Apply Modifiers
        for (const auto& modifier : rrd.Modifiers)
        {
            switch (modifier.type)
            {
                case RatingsModifierType::BonusLength:
                    RideRatingsApplyBonusLength(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusSynchronisation:
                    RideRatingsApplyBonusSynchronisation(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusTrainLength:
                    RideRatingsApplyBonusTrainLength(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusMaxSpeed:
                    RideRatingsApplyBonusMaxSpeed(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusAverageSpeed:
                    RideRatingsApplyBonusAverageSpeed(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusDuration:
                    RideRatingsApplyBonusDuration(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusGForces:
                    RideRatingsApplyBonusGForces(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusTurns:
                    RideRatingsApplyBonusTurns(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusDrops:
                    RideRatingsApplyBonusDrops(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusSheltered:
                    RideRatingsApplyBonusSheltered(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusProximity:
                    RideRatingsApplyBonusProximity(ratings, ride, state, modifier);
                    break;
                case RatingsModifierType::BonusScenery:
                    RideRatingsApplyBonusScenery(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusRotations:
                    RideRatingsApplyBonusRotations(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusOperationOption:
                    RideRatingsApplyBonusOperationOption(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusReversedTrains:
                    RideRatingsApplyBonusReversedTrains(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusGoKartRace:
                    RideRatingsApplyBonusGoKartRace(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusTowerRide:
                    RideRatingsApplyBonusTowerRide(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusRotoDrop:
                    RideRatingsApplyBonusRotoDrop(ratings, ride);
                    break;
                case RatingsModifierType::BonusMazeSize:
                    RideRatingsApplyBonusMazeSize(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusBoatHireNoCircuit:
                    RideRatingsApplyBonusBoatHireNoCircuit(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusSlideUnlimitedRides:
                    RideRatingsApplyBonusSlideUnlimitedRides(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusMotionSimulatorMode:
                    RideRatingsApplyBonusMotionSimulatorMode(ratings, ride, modifier);
                    break;
                case RatingsModifierType::Bonus3DCinemaMode:
                    RideRatingsApplyBonus3DCinemaMode(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusTopSpinMode:
                    RideRatingsApplyBonusTopSpinMode(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusReversals:
                    RideRatingsApplyBonusReversals(ratings, ride, state, modifier);
                    break;
                case RatingsModifierType::BonusHoles:
                    RideRatingsApplyBonusHoles(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusNumTrains:
                    RideRatingsApplyBonusNumTrains(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusDownwardLaunch:
                    RideRatingsApplyBonusDownwardLaunch(ratings, ride, modifier);
                    break;
                case RatingsModifierType::BonusLaunchedFreefallSpecial:
                    RideRatingsApplyBonusLaunchedFreefallSpecial(ratings, ride, state, modifier);
                    break;
                case RatingsModifierType::RequirementLength:
                    RideRatingsApplyRequirementLength(ratings, ride, modifier);
                    break;
                case RatingsModifierType::RequirementMaxSpeed:
                    RideRatingsApplyRequirementMaxSpeed(ratings, ride, modifier);
                    break;
                case RatingsModifierType::RequirementLateralGs:
                    RideRatingsApplyRequirementLateralGs(ratings, ride, modifier);
                    break;
                case RatingsModifierType::RequirementInversions:
                    RideRatingsApplyRequirementInversions(ratings, ride, modifier);
                    break;
                case RatingsModifierType::RequirementUnsheltered:
                    RideRatingsApplyRequirementUnsheltered(ratings, ride, shelteredEighths.TrackShelteredEighths, modifier);
                    break;
                case RatingsModifierType::RequirementReversals:
                    RideRatingsApplyRequirementReversals(ratings, ride, state, modifier);
                    break;
                case RatingsModifierType::RequirementHoles:
                    RideRatingsApplyRequirementHoles(ratings, ride, modifier);
                    break;
                case RatingsModifierType::RequirementStations:
                    RideRatingsApplyRequirementStations(ratings, ride, modifier);
                    break;
                case RatingsModifierType::RequirementSplashdown:
                    RideRatingsApplyRequirementSplashdown(ratings, ride, modifier);
                    break;
                case RatingsModifierType::PenaltyLateralGs:
                    RideRatingsApplyPenaltyLateralGs(ratings, ride, modifier);
                    break;
                default:
                    break;
            }

            // Requirements that may be ignored if the ride has inversions
            if (ride.numInversions == 0 || !rrd.RelaxRequirementsIfInversions)
            {
                switch (modifier.type)
                {
                    case RatingsModifierType::RequirementDropHeight:
                        RideRatingsApplyRequirementDropHeight(ratings, ride, modifier);
                        break;
                    case RatingsModifierType::RequirementNumDrops:
                        RideRatingsApplyRequirementNumDrops(ratings, ride, modifier);
                        break;
                    case RatingsModifierType::RequirementNegativeGs:
                        RideRatingsApplyRequirementNegativeGs(ratings, ride, modifier);
                        break;
                    default:
                        break;
                }
            }
        }
        // Universl ratings adjustments
        RideRatingsApplyIntensityPenalty(ratings);
        RideRatingsApplyAdjustments(ride, ratings);
    }
    if (ride.ratings != ratings)
    {
        ride.ratings = ratings;
        ride.windowInvalidateFlags.set(RideInvalidateFlag::ratings);
    }

    ride.upkeepCost = RideComputeUpkeep(state, ride);
    ride.windowInvalidateFlags.set(RideInvalidateFlag::income);

#ifdef ORIGINAL_RATINGS
    if (!ride.ratings.isNull())
    {
        // Address underflows allowed by original RCT2 code
        ride.ratings.excitement = std::max<uint16_t>(0, ride.ratings.excitement);
        ride.ratings.intensity = std::max<uint16_t>(0, ride.ratings.intensity);
        ride.ratings.nausea = std::max<uint16_t>(0, ride.ratings.nausea);
    }
#endif

#ifdef ENABLE_SCRIPTING
    // Only call the 'ride.ratings.calculate' API hook if testing of the ride is complete
    if (ride.flags.has(RideFlag::tested))
    {
        auto& hookEngine = GetContext()->GetScriptEngine().GetHookEngine();
        if (hookEngine.HasSubscriptions(HookType::rideRatingsCalculate))
        {
            auto ctx = GetContext()->GetScriptEngine().GetContext();
            auto originalRatings = ride.ratings;

            // Create event args object
            JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "rideId", JS_NewInt32(ctx, ride.id.ToUnderlying()));
            JS_SetPropertyStr(ctx, obj, "excitement", JS_NewInt32(ctx, originalRatings.excitement));
            JS_SetPropertyStr(ctx, obj, "intensity", JS_NewInt32(ctx, originalRatings.intensity));
            JS_SetPropertyStr(ctx, obj, "nausea", JS_NewInt32(ctx, originalRatings.nausea));

            // Call the subscriptions
            hookEngine.Call(HookType::rideRatingsCalculate, obj, true, true);

            auto scriptExcitement = AsOrDefault(ctx, obj, "excitement", static_cast<int32_t>(originalRatings.excitement));
            auto scriptIntensity = AsOrDefault(ctx, obj, "intensity", static_cast<int32_t>(originalRatings.intensity));
            auto scriptNausea = AsOrDefault(ctx, obj, "nausea", static_cast<int32_t>(originalRatings.nausea));
            JS_FreeValue(ctx, obj);

            ride.ratings.excitement = std::clamp<int32_t>(scriptExcitement, 0, INT16_MAX);
            ride.ratings.intensity = std::clamp<int32_t>(scriptIntensity, 0, INT16_MAX);
            ride.ratings.nausea = std::clamp<int32_t>(scriptNausea, 0, INT16_MAX);
        }
    }
#endif
}

static void RideRatingsCalculateValue(Ride& ride)
{
    struct Row
    {
        int32_t months, multiplier, divisor, summand;
    };
    static constexpr auto kAgeTable = std::to_array<Row>({
        { 5, 3, 2, 0 },       // 1.5x
        { 13, 6, 5, 0 },      // 1.2x
        { 40, 1, 1, 0 },      // 1x
        { 64, 3, 4, 0 },      // 0.75x
        { 88, 9, 16, 0 },     // 0.56x
        { 104, 27, 64, 0 },   // 0.42x
        { 120, 81, 256, 0 },  // 0.32x
        { 128, 81, 512, 0 },  // 0.16x
        { 200, 81, 1024, 0 }, // 0.08x
        { 200, 9, 16, 0 },    // 0.56x "easter egg"
    });

    if (!RideHasRatings(ride))
    {
        return;
    }

    // Start with the base ratings, multiplied by the ride type specific weights for excitement, intensity and nausea.
    const auto& ratingsMultipliers = ride.getRideTypeDescriptor().RatingsMultipliers;
    money32 value = (((ride.ratings.excitement * ratingsMultipliers.excitement) * 32) >> 15)
        + (((ride.ratings.intensity * ratingsMultipliers.intensity) * 32) >> 15)
        + (((ride.ratings.nausea * ratingsMultipliers.nausea) * 32) >> 15);

    int32_t monthsOld = 0;
    if (!getGameState().cheats.disableRideValueAging)
    {
        monthsOld = ride.getAge();
    }

    Row lastRow = kAgeTable[kAgeTable.size() - 1];

    // Ride is older than oldest age in the table?
    if (monthsOld >= lastRow.months)
    {
        value = (value * lastRow.multiplier) / lastRow.divisor + lastRow.summand;
    }
    else
    {
        // Find the first hit in the table that matches this ride's age
        for (const Row& curr : kAgeTable)
        {
            if (monthsOld < curr.months)
            {
                value = (value * curr.multiplier) / curr.divisor + curr.summand;
                break;
            }
        }
    }

    // Other ride of same type penalty
    const auto& gameState = getGameState();
    const auto& rideManager = RideManager(gameState);
    auto rideType = ride.type;
    auto otherRidesOfSameType = std::count_if(rideManager.begin(), rideManager.end(), [rideType](const Ride& r) {
        return r.status == RideStatus::open && r.type == rideType;
    });
    if (otherRidesOfSameType > 1)
        value -= value / 4;

    ride.value = ToMoney64(std::max<money32>(0, value));
    RideUpdateTargetPrice(ride);
}

/**
 * I think this function computes ride upkeep? Though it is weird that the
 *  rct2: Sub65E621
 * inputs
 * - edi: ride ptr
 */
static money64 RideComputeUpkeep(RideRating::UpdateState& state, const Ride& ride)
{
    // data stored at 0x0057E3A8, incrementing 18 bytes at a time
    auto upkeep = ride.getRideTypeDescriptor().UpkeepCosts.BaseCost;

    auto trackCost = ride.getRideTypeDescriptor().UpkeepCosts.CostPerTrackPiece;
    upkeep += trackCost * ride.numPoweredLifts;

    uint32_t totalLength = ToHumanReadableRideLength(ride.getTotalLength());

    // The data originally here was 20's and 0's. The 20's all represented
    // rides that had tracks. The 0's were fixed rides like crooked house or
    // dodgems.
    // Data source is 0x0097E3AC
    totalLength *= ride.getRideTypeDescriptor().UpkeepCosts.TrackLengthMultiplier;
    upkeep += static_cast<uint16_t>(totalLength >> 10);

    if (ride.flags.has(RideFlag::onRidePhoto))
    {
        // The original code read from a table starting at 0x0097E3AE and
        // incrementing by 0x12 bytes between values. However, all of these
        // values were 40. I have replaced the table lookup with the constant
        // 40 in this case.
        upkeep += 40;
    }

    // Add maintenance cost for reverser track pieces
    upkeep += 10 * state.AmountOfReversers;

    // Add maintenance cost for brake track pieces
    upkeep += 20 * state.AmountOfBrakes;

    // Add maintenance cost for booster track pieces
    upkeep += 80 * state.amountOfBoosters;

    // these seem to be adhoc adjustments to a ride's upkeep/cost, times
    // various variables set on the ride itself.

    // https://gist.github.com/kevinburke/e19b803cd2769d96c540
    upkeep += ride.getRideTypeDescriptor().UpkeepCosts.CostPerTrain * ride.numTrains;
    upkeep += ride.getRideTypeDescriptor().UpkeepCosts.CostPerCar * ride.numCarsPerTrain;

    // slight upkeep boosts for some rides - 5 for mini railway, 10 for log
    // flume/rapids, 10 for roller coaster, 28 for giga coaster
    upkeep += ride.getRideTypeDescriptor().UpkeepCosts.CostPerStation * ride.numStations;

    if (ride.mode == RideMode::reverseInclineLaunchedShuttle)
    {
        upkeep += 30;
    }
    else if (ride.mode == RideMode::poweredLaunchPasstrough)
    {
        upkeep += 160;
    }
    else if (ride.mode == RideMode::limPoweredLaunch)
    {
        upkeep += 320;
    }
    else if (ride.mode == RideMode::poweredLaunch || ride.mode == RideMode::poweredLaunchBlockSectioned)
    {
        upkeep += 220;
    }

    // multiply by 5/8
    upkeep *= 10;
    upkeep >>= 4;
    return ToMoney64(static_cast<money32>(upkeep));
}

/**
 *
 *  rct2: 0x0065E7FB
 *
 * inputs
 * - bx: excitement
 * - cx: intensity
 * - bp: nausea
 * - edi: ride ptr
 */
static void RideRatingsApplyAdjustments(const Ride& ride, RideRating::Tuple& ratings)
{
    const auto* rideEntry = GetRideEntryByIndex(ride.subtype);

    if (rideEntry == nullptr)
    {
        return;
    }

    // Apply ride entry multipliers
    RideRatingsAdd(
        ratings, ((static_cast<int32_t>(ratings.excitement) * rideEntry->excitement_multiplier) >> 7),
        ((static_cast<int32_t>(ratings.intensity) * rideEntry->intensity_multiplier) >> 7),
        ((static_cast<int32_t>(ratings.nausea) * rideEntry->nausea_multiplier) >> 7));

    // Apply total air time
#ifdef ORIGINAL_RATINGS
    if (ride.getRideTypeDescriptor().flags.has(RtdFlag::hasAirTime))
    {
        uint16_t totalAirTime = ride.totalAirTime;
        if (rideEntry->flags.has(RideEntryFlag::limitAirTimeBonus))
        {
            if (totalAirTime >= 96)
            {
                totalAirTime -= 96;
                ratings.excitement -= totalAirTime / 8;
                ratings.nausea += totalAirTime / 16;
            }
        }
        else
        {
            ratings.excitement += totalAirTime / 8;
            ratings.nausea += totalAirTime / 16;
        }
    }
#else
    if (ride.getRideTypeDescriptor().flags.has(RtdFlag::hasAirTime))
    {
        int32_t excitementModifier;
        int32_t totalAirTime = ride.totalAirTime;
        if (rideEntry->flags.has(RideEntryFlag::limitAirTimeBonus))
        {
            totalAirTime = std::max(0, totalAirTime - 96);
            excitementModifier = -1 * (std::min<uint16_t>(totalAirTime, 200) / 8);
        }
        else
        {
            excitementModifier = std::min<uint16_t>(totalAirTime, 200) / 8;
        }
        int32_t nauseaModifier = totalAirTime / 16;

        RideRatingsAdd(ratings, excitementModifier, 0, nauseaModifier);
    }
#endif
}

/**
 * Lowers excitement, the higher the intensity.
 *  rct2: 0x0065E7A3
 */
static void RideRatingsApplyIntensityPenalty(RideRating::Tuple& ratings)
{
    static constexpr RideRating_t intensityBounds[] = { 1000, 1100, 1200, 1320, 1450 };
    RideRating_t excitement = ratings.excitement;
    for (auto intensityBound : intensityBounds)
    {
        if (ratings.intensity >= intensityBound)
        {
            excitement -= excitement / 4;
        }
    }
    ratings.excitement = excitement;
}

/**
 *
 *  rct2: 0x00655FD6
 */
static void SetUnreliabilityFactor(Ride& ride)
{
    const auto& rtd = ride.getRideTypeDescriptor();
    // Special unreliability for a few ride types
    if (rtd.flags.has(RtdFlag::reverseInclineLaunchAffectsReliability) && ride.mode == RideMode::reverseInclineLaunchedShuttle)
    {
        ride.unreliabilityFactor += 10;
    }
    else if (rtd.flags.has(RtdFlag::poweredLaunchAffectsReliability) && ride.isPoweredLaunched())
    {
        ride.unreliabilityFactor += 5;
    }
    else if (rtd.flags.has(RtdFlag::runningSpeedAffectsReliability))
    {
        ride.unreliabilityFactor += (ride.speed * 2);
    }
    // The bigger the difference in lift speed and minimum the higher the unreliability
    uint8_t minLiftSpeed = ride.getRideTypeDescriptor().LiftData.minimum_speed;
    ride.unreliabilityFactor += (ride.liftHillSpeed - minLiftSpeed) * 2;
}

static uint32_t get_proximity_score_helper_1(uint16_t x, uint16_t max, uint32_t multiplier)
{
    return (std::min(x, max) * multiplier) >> 16;
}

static uint32_t get_proximity_score_helper_2(uint16_t x, uint16_t additionIfNotZero, uint16_t max, uint32_t multiplier)
{
    uint32_t result = x;
    if (result != 0)
        result += additionIfNotZero;
    return (std::min<int32_t>(result, max) * multiplier) >> 16;
}

static uint32_t get_proximity_score_helper_3(uint16_t x, uint16_t resultIfNotZero)
{
    return x == 0 ? 0 : resultIfNotZero;
}

/**
 *
 *  rct2: 0x0065E277
 */
static uint32_t ride_ratings_get_proximity_score(RideRating::UpdateState& state)
{
    const uint16_t* scores = state.ProximityScores;

    uint32_t result = 0;
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_OVER], 60, 0x00AAAA);
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_TOUCH], 22, 0x0245D1);
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_LOW], 10, 0x020000);
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_HIGH], 40, 0x00A000);
    result += get_proximity_score_helper_1(scores[PROXIMITY_SURFACE_TOUCH], 70, 0x01B6DB);
    result += get_proximity_score_helper_1(scores[PROXIMITY_QUEUE_PATH_OVER] + 8, 12, 0x064000);
    result += get_proximity_score_helper_3(scores[PROXIMITY_QUEUE_PATH_TOUCH_ABOVE], 40);
    result += get_proximity_score_helper_3(scores[PROXIMITY_QUEUE_PATH_TOUCH_UNDER], 45);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_TOUCH_ABOVE], 10, 20, 0x03C000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_TOUCH_UNDER], 10, 20, 0x044000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_OWN_TRACK_TOUCH_ABOVE], 10, 15, 0x035555);
    result += get_proximity_score_helper_1(scores[PROXIMITY_OWN_TRACK_CLOSE_ABOVE], 5, 0x060000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_FOREIGN_TRACK_ABOVE_OR_BELOW], 10, 15, 0x02AAAA);
    result += get_proximity_score_helper_2(scores[PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE], 10, 15, 0x04AAAA);
    result += get_proximity_score_helper_1(scores[PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE], 5, 0x090000);
    result += get_proximity_score_helper_1(scores[PROXIMITY_SCENERY_SIDE_BELOW], 35, 0x016DB6);
    result += get_proximity_score_helper_1(scores[PROXIMITY_SCENERY_SIDE_ABOVE], 35, 0x00DB6D);
    result += get_proximity_score_helper_3(scores[PROXIMITY_OWN_STATION_TOUCH_ABOVE], 55);
    result += get_proximity_score_helper_3(scores[PROXIMITY_OWN_STATION_CLOSE_ABOVE], 25);
    result += get_proximity_score_helper_2(scores[PROXIMITY_TRACK_THROUGH_VERTICAL_LOOP], 4, 6, 0x140000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_TROUGH_VERTICAL_LOOP], 4, 6, 0x0F0000);
    result += get_proximity_score_helper_3(scores[PROXIMITY_INTERSECTING_VERTICAL_LOOP], 100);
    result += get_proximity_score_helper_2(scores[PROXIMITY_THROUGH_VERTICAL_LOOP], 4, 6, 0x0A0000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_SIDE_CLOSE], 10, 20, 0x01C000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_FOREIGN_TRACK_SIDE_CLOSE], 10, 20, 0x024000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_SURFACE_SIDE_CLOSE], 10, 20, 0x028000);
    return result;
}

/**
 * Calculates how much of the track is sheltered in eighths.
 *  rct2: 0x0065E72D
 */
static ShelteredEights GetNumOfShelteredEighths(const Ride& ride)
{
    int32_t totalLength = ride.getTotalLength();
    int32_t shelteredLength = ride.shelteredLength;
    int32_t lengthEighth = totalLength / 8;
    int32_t lengthCounter = lengthEighth;
    uint8_t numShelteredEighths = 0;
    for (int32_t i = 0; i < 7; i++)
    {
        if (shelteredLength >= lengthCounter)
        {
            lengthCounter += lengthEighth;
            numShelteredEighths++;
        }
    }

    uint8_t trackShelteredEighths = numShelteredEighths;
    const auto* rideType = GetRideEntryByIndex(ride.subtype);
    if (rideType == nullptr)
    {
        return { 0, 0 };
    }
    if (rideType->flags.has(RideEntryFlag::isACoveredRide))
        numShelteredEighths = 7;

    return { trackShelteredEighths, numShelteredEighths };
}

static RideRating::Tuple get_flat_turns_rating(const Ride& ride)
{
    int32_t num3PlusTurns = GetTurnCount3Elements(ride, 0);
    int32_t num2Turns = GetTurnCount2Elements(ride, 0);
    int32_t num1Turns = GetTurnCount1Element(ride, 0);

    RideRating::Tuple rating;
    rating.excitement = (num3PlusTurns * 0x28000) >> 16;
    rating.excitement += (num2Turns * 0x30000) >> 16;
    rating.excitement += (num1Turns * 63421) >> 16;

    rating.intensity = (num3PlusTurns * 81920) >> 16;
    rating.intensity += (num2Turns * 49152) >> 16;
    rating.intensity += (num1Turns * 21140) >> 16;

    rating.nausea = (num3PlusTurns * 0x50000) >> 16;
    rating.nausea += (num2Turns * 0x32000) >> 16;
    rating.nausea += (num1Turns * 42281) >> 16;

    return rating;
}

/**
 *
 *  rct2: 0x0065DF72
 */
static RideRating::Tuple get_banked_turns_rating(const Ride& ride)
{
    int32_t num3PlusTurns = GetTurnCount3Elements(ride, 1);
    int32_t num2Turns = GetTurnCount2Elements(ride, 1);
    int32_t num1Turns = GetTurnCount1Element(ride, 1);

    RideRating::Tuple rating;
    rating.excitement = (num3PlusTurns * 0x3C000) >> 16;
    rating.excitement += (num2Turns * 0x3C000) >> 16;
    rating.excitement += (num1Turns * 73992) >> 16;

    rating.intensity = (num3PlusTurns * 0x14000) >> 16;
    rating.intensity += (num2Turns * 49152) >> 16;
    rating.intensity += (num1Turns * 21140) >> 16;

    rating.nausea = (num3PlusTurns * 0x50000) >> 16;
    rating.nausea += (num2Turns * 0x32000) >> 16;
    rating.nausea += (num1Turns * 48623) >> 16;

    return rating;
}

/**
 *
 *  rct2: 0x0065E047
 */
static RideRating::Tuple get_sloped_turns_rating(const Ride& ride)
{
    RideRating::Tuple rating;

    int32_t num4PlusTurns = GetTurnCount4PlusElements(ride, 2);
    int32_t num3Turns = GetTurnCount3Elements(ride, 2);
    int32_t num2Turns = GetTurnCount2Elements(ride, 2);
    int32_t num1Turns = GetTurnCount1Element(ride, 2);

    rating.excitement = (std::min(num4PlusTurns, 4) * 0x78000) >> 16;
    rating.excitement += (std::min(num3Turns, 6) * 273066) >> 16;
    rating.excitement += (std::min(num2Turns, 6) * 0x3AAAA) >> 16;
    rating.excitement += (std::min(num1Turns, 7) * 187245) >> 16;
    rating.intensity = 0;
    rating.nausea = (std::min(num4PlusTurns, 8) * 0x78000) >> 16;

    return rating;
}

/**
 *
 *  rct2: 0x0065E0F2
 */
static RideRating::Tuple getInversionsRatings(uint16_t inversions)
{
    RideRating::Tuple rating;

    rating.excitement = (std::min<int32_t>(inversions, 6) * 0x1AAAAA) >> 16;
    rating.intensity = (inversions * 0x320000) >> 16;
    rating.nausea = (inversions * 0x15AAAA) >> 16;

    return rating;
}

void SpecialTrackElementRatingsAjustment_Default(const Ride& ride, int32_t& excitement, int32_t& intensity, int32_t& nausea)
{
    if (ride.hasWaterSplash())
    {
        excitement += 50;
        intensity += 30;
        nausea += 20;
    }
    if (ride.hasWaterfall())
    {
        excitement += 55;
        intensity += 30;
    }
    if (ride.hasWhirlpool())
    {
        excitement += 35;
        intensity += 20;
        nausea += 23;
    }
}

void SpecialTrackElementRatingsAjustment_GhostTrain(const Ride& ride, int32_t& excitement, int32_t& intensity, int32_t& nausea)
{
    if (ride.hasSpinningTunnel())
    {
        excitement += 40;
        intensity += 25;
        nausea += 55;
    }
}

void SpecialTrackElementRatingsAjustment_LogFlume(const Ride& ride, int32_t& excitement, int32_t& intensity, int32_t& nausea)
{
    if (ride.hasLogReverser())
    {
        excitement += 48;
        intensity += 55;
        nausea += 65;
    }
}

static RideRating::Tuple GetSpecialTrackElementsRating(uint8_t type, const Ride& ride)
{
    int32_t excitement = 0, intensity = 0, nausea = 0;
    const auto& rtd = ride.getRideTypeDescriptor();
    rtd.SpecialElementRatingAdjustment(ride, excitement, intensity, nausea);

    auto helixSections = ride.numHelices;

    int32_t helixesUpTo9 = std::min<int32_t>(helixSections, 9);
    excitement += (helixesUpTo9 * 254862) >> 16;

    int32_t helixesUpTo11 = std::min<int32_t>(helixSections, 11);
    intensity += (helixesUpTo11 * 148945) >> 16;

    int32_t helixesOver5UpTo10 = std::clamp<int32_t>(helixSections - 5, 0, 10);
    nausea += (helixesOver5UpTo10 * 0x140000) >> 16;

    RideRating::Tuple rating = { static_cast<RideRating_t>(excitement), static_cast<RideRating_t>(intensity),
                                 static_cast<RideRating_t>(nausea) };
    return rating;
}

/**
 *
 *  rct2: 0x0065DDD1
 */
static RideRating::Tuple ride_ratings_get_turns_ratings(const Ride& ride)
{
    int32_t excitement = 0, intensity = 0, nausea = 0;

    RideRating::Tuple specialTrackElementsRating = GetSpecialTrackElementsRating(ride.type, ride);
    excitement += specialTrackElementsRating.excitement;
    intensity += specialTrackElementsRating.intensity;
    nausea += specialTrackElementsRating.nausea;

    RideRating::Tuple flatTurnsRating = get_flat_turns_rating(ride);
    excitement += flatTurnsRating.excitement;
    intensity += flatTurnsRating.intensity;
    nausea += flatTurnsRating.nausea;

    RideRating::Tuple bankedTurnsRating = get_banked_turns_rating(ride);
    excitement += bankedTurnsRating.excitement;
    intensity += bankedTurnsRating.intensity;
    nausea += bankedTurnsRating.nausea;

    RideRating::Tuple slopedTurnsRating = get_sloped_turns_rating(ride);
    excitement += slopedTurnsRating.excitement;
    intensity += slopedTurnsRating.intensity;
    nausea += slopedTurnsRating.nausea;

    RideRating::Tuple inversionsRating = getInversionsRatings(ride.numInversions);
    excitement += inversionsRating.excitement;
    intensity += inversionsRating.intensity;
    nausea += inversionsRating.nausea;

    RideRating::Tuple rating = { static_cast<RideRating_t>(excitement), static_cast<RideRating_t>(intensity),
                                 static_cast<RideRating_t>(nausea) };
    return rating;
}

/**
 *
 *  rct2: 0x0065E1C2
 */
static RideRating::Tuple ride_ratings_get_sheltered_ratings(const Ride& ride)
{
    int32_t shelteredLengthShifted = (ride.shelteredLength) >> 16;

    uint32_t shelteredLengthUpTo1000 = std::min(shelteredLengthShifted, 1000);
    uint32_t shelteredLengthUpTo2000 = std::min(shelteredLengthShifted, 2000);

    int32_t excitement = (shelteredLengthUpTo1000 * 9175) >> 16;
    int32_t intensity = (shelteredLengthUpTo2000 * 0x2666) >> 16;
    int32_t nausea = (shelteredLengthUpTo1000 * 0x4000) >> 16;

    /*eax = (ride.var11C * 30340) >> 16;*/
    /*nausea += eax;*/

    if (ride.numShelteredSections & ShelteredSectionsBits::kBankingWhileSheltered)
    {
        excitement += 20;
        nausea += 15;
    }

    if (ride.numShelteredSections & ShelteredSectionsBits::kRotatingWhileSheltered)
    {
        excitement += 20;
        nausea += 15;
    }

    uint8_t lowerVal = ride.getNumShelteredSections();
    lowerVal = std::min<uint8_t>(lowerVal, 11);
    excitement += (lowerVal * 774516) >> 16;

    RideRating::Tuple rating = { static_cast<RideRating_t>(excitement), static_cast<RideRating_t>(intensity),
                                 static_cast<RideRating_t>(nausea) };
    return rating;
}

/**
 *
 *  rct2: 0x0065DCDC
 */
static RideRating::Tuple ride_ratings_get_gforce_ratings(const Ride& ride)
{
    RideRating::Tuple result = {
        .excitement = 0,
        .intensity = 0,
        .nausea = 0,
    };

    // Apply maximum positive G force factor
    result.excitement += (ride.maxPositiveVerticalG * 5242) >> 16;
    result.intensity += (ride.maxPositiveVerticalG * 52428) >> 16;
    result.nausea += (ride.maxPositiveVerticalG * 17039) >> 16;

    // Apply maximum negative G force factor
    fixed16_2dp gforce = ride.maxNegativeVerticalG;
    result.excitement += (std::clamp<fixed16_2dp>(gforce, -RideRating::make(2, 50), RideRating::make(0, 00)) * -15728) >> 16;
    result.intensity += ((gforce - RideRating::make(1, 00)) * -52428) >> 16;
    result.nausea += ((gforce - RideRating::make(1, 00)) * -14563) >> 16;

    // Apply lateral G force factor
    result.excitement += (std::min<fixed16_2dp>(RideRating::make(1, 50), ride.maxLateralG) * 26214) >> 16;
    result.intensity += ride.maxLateralG;
    result.nausea += (ride.maxLateralG * 21845) >> 16;

// Very high lateral G force penalty
#ifdef ORIGINAL_RATINGS
    if (ride.maxLateralG > MakeFixed16_2dp(2, 80))
    {
        result.intensity += RideRating::make(3, 75);
        result.nausea += RideRating::make(2, 00);
    }
    if (ride.maxLateralG > MakeFixed16_2dp(3, 10))
    {
        result.excitement /= 2;
        result.intensity += RideRating::make(8, 50);
        result.nausea += RideRating::make(4, 00);
    }
#endif

    return result;
}

/**
 *
 *  rct2: 0x0065E139
 */
static RideRating::Tuple ride_ratings_get_drop_ratings(const Ride& ride)
{
    RideRating::Tuple result = {
        /* .excitement = */ 0,
        /* .intensity = */ 0,
        /* .nausea = */ 0,
    };

    // Apply number of drops factor
    int32_t drops = ride.numDrops;
    result.excitement += (std::min(9, drops) * 728177) >> 16;
    result.intensity += (drops * 928426) >> 16;
    result.nausea += (drops * 655360) >> 16;

    // Apply highest drop factor
    RideRatingsAdd(
        result, ((ride.highestDropHeight * 2) * 16000) >> 16, ((ride.highestDropHeight * 2) * 32000) >> 16,
        ((ride.highestDropHeight * 2) * 10240) >> 16);

    return result;
}

/**
 * Calculates a score based on the surrounding scenery.
 *  rct2: 0x0065E557
 */
static int32_t ride_ratings_get_scenery_score(const Ride& ride)
{
    RideRatingFixedRideLocalContextBase base;
    if (!RideRatingGetFixedRideLocalContextBase(ride, base))
    {
        return 0;
    }

    int32_t z = TileElementHeight(base.location);

    // Check if station is underground, returns a fixed mediocre score since you can't have scenery underground
    if (z > base.baseZ)
    {
        return 40;
    }

    const auto contextScore = RideRating::GetLocalContextScore(RideRatingBuildFixedRideLocalContextOrigin(ride, base), ride.id);
    return contextScore.scenery * 5;
}

#pragma region Ride rating calculation helpers

static void RideRatingsSet(RideRating::Tuple& ratings, int32_t excitement, int32_t intensity, int32_t nausea)
{
    ratings.excitement = 0;
    ratings.intensity = 0;
    ratings.nausea = 0;
    RideRatingsAdd(ratings, excitement, intensity, nausea);
}

/**
 * Add to a ride rating with overflow protection.
 */
static void RideRatingsAdd(RideRating::Tuple& ratings, int32_t excitement, int32_t intensity, int32_t nausea)
{
    int32_t newExcitement = ratings.excitement + excitement;
    int32_t newIntensity = ratings.intensity + intensity;
    int32_t newNausea = ratings.nausea + nausea;
    ratings.excitement = std::clamp<int32_t>(newExcitement, 0, INT16_MAX);
    ratings.intensity = std::clamp<int32_t>(newIntensity, 0, INT16_MAX);
    ratings.nausea = std::clamp<int32_t>(newNausea, 0, INT16_MAX);
}

static RideRating_t RideRatingsRawToRating(int64_t raw)
{
    if (raw <= 0)
    {
        return 0;
    }

    const auto unscaledRaw = static_cast<double>(raw) / static_cast<double>(RideRating::kRideRatingAccumulatorRawScale);
    const double value = std::sqrt(unscaledRaw / static_cast<double>(kAggregatedRideRatingDivisor)) * 100.0;
    return static_cast<RideRating_t>(std::clamp<int64_t>(static_cast<int64_t>(std::llround(value)), 0, INT16_MAX));
}

static void RideRatingsRawApplyRideEntryMultipliers(RawRideRating& raw, const Ride& ride)
{
    const auto* rideEntry = GetRideEntryByIndex(ride.subtype);
    if (rideEntry == nullptr)
    {
        return;
    }

    const auto score = RideRating::ApplyRideEntryMultipliers({ raw.excitement, raw.intensity, raw.nausea }, *rideEntry);
    raw.excitement = score.excitement;
    raw.intensity = score.intensity;
    raw.nausea = score.nausea;
}

static void RideRatingsRawApplyMazeCapacityMode(RawRideRating& raw, const Ride& ride)
{
    const auto [numerator, denominator] = ride.getMazeRatingAccumulatorScale();
    if (numerator == denominator)
    {
        return;
    }

    raw.excitement = (raw.excitement * numerator) / denominator;
    raw.intensity = (raw.intensity * numerator) / denominator;
    raw.nausea = (raw.nausea * numerator) / denominator;
}

static bool RideRatingsModifierIsAggregateSummaryStatGate(RatingsModifierType type)
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

static void RideRatingsRawApplyModifiers(RawRideRating& raw, const Ride& ride)
{
    const auto& descriptor = ride.getRideTypeDescriptor().RatingsData;
    for (const auto& modifier : descriptor.Modifiers)
    {
        // Aggregate ratings must start from sampled ticks only. Legacy summary-stat gates are skipped here because sampled
        // momentum, track context, and maze movement now own those effects directly.
        if (RideRatingsModifierIsAggregateSummaryStatGate(modifier.type))
        {
            continue;
        }

        switch (modifier.type)
        {
            case RatingsModifierType::BonusReversedTrains:
                if (ride.flags.has(RideFlag::reversedTrains))
                {
                    raw.excitement += (raw.excitement * modifier.excitement) >> 7;
                    raw.intensity += (raw.intensity * modifier.intensity) >> 7;
                    raw.nausea += (raw.nausea * modifier.nausea) >> 7;
                }
                break;
            default:
                break;
        }
    }
}

static RideRating::Tuple RideRatingsCalculateAggregated(const Ride& ride, const RideRatingAccumulator& accumulator)
{
    RawRideRating raw = {
        .excitement = accumulator.excitement,
        .intensity = accumulator.intensity,
        .nausea = accumulator.nausea,
    };

    RideRatingsRawApplyMazeCapacityMode(raw, ride);
    RideRatingsRawApplyModifiers(raw, ride);
    RideRatingsRawApplyRideEntryMultipliers(raw, ride);

    return {
        .excitement = RideRatingsRawToRating(raw.excitement),
        .intensity = RideRatingsRawToRating(raw.intensity),
        .nausea = RideRatingsRawToRating(raw.nausea),
    };
}

static RideRating::Tuple RideRatingsCalculateLegCompatibility(const Ride& ride)
{
    RideRating::Tuple result{};
    bool hasRatings = false;
    for (const auto& leg : ride.ratingLegs)
    {
        if (!leg.hasSamples())
        {
            continue;
        }
        if (!hasRatings)
        {
            result = leg.ratings;
            hasRatings = true;
            continue;
        }

        // Legacy/list/value consumers require one tuple. Use the least exciting
        // measured leg and the worst measured intensity/nausea so that the
        // compatibility value never advertises a better trip than every leg.
        result.excitement = std::min(result.excitement, leg.ratings.excitement);
        result.intensity = std::max(result.intensity, leg.ratings.intensity);
        result.nausea = std::max(result.nausea, leg.ratings.nausea);
    }
    return hasRatings ? result : ride.ratings;
}

static bool RideRatingsHaveCompleteLegCoverage(const Ride& ride)
{
    if (ride.numStations <= 1)
    {
        return true;
    }
    std::array<bool, Limits::kMaxStationsPerRide> hasOutboundLeg{};
    for (const auto& leg : ride.ratingLegs)
    {
        if (leg.hasSamples() && !leg.originStation.IsNull() && leg.originStation.ToUnderlying() < ride.numStations)
        {
            hasOutboundLeg[leg.originStation.ToUnderlying()] = true;
        }
    }
    return std::all_of(hasOutboundLeg.begin(), hasOutboundLeg.begin() + ride.numStations, [](bool covered) {
        return covered;
    });
}

static void RideRatingsApplyBonusLength(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRatingsAdd(
        ratings, (std::min(ToHumanReadableRideLength(ride.getTotalLength()), modifier.threshold) * modifier.excitement) >> 16,
        0, 0);
}

static void RideRatingsApplyBonusSynchronisation(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if ((ride.departFlags & RIDE_DEPART_SYNCHRONISE_WITH_ADJACENT_STATIONS) && RideHasAdjacentStation(ride))
    {
        RideRatingsAdd(ratings, modifier.excitement, modifier.intensity, modifier.nausea);
    }
}

static void RideRatingsApplyBonusTrainLength(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRatingsAdd(ratings, ((ride.numCarsPerTrain - 1) * modifier.excitement) >> 16, 0, 0);
}

static void RideRatingsApplyBonusMaxSpeed(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    int32_t maxSpeedMod = ride.maxSpeed >> 16;
    RideRatingsAdd(
        ratings, (maxSpeedMod * modifier.excitement) >> 16, (maxSpeedMod * modifier.intensity) >> 16,
        (maxSpeedMod * modifier.nausea) >> 16);
}

static void RideRatingsApplyBonusAverageSpeed(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    int32_t avgSpeedMod = ride.averageSpeed >> 16;
    RideRatingsAdd(ratings, (avgSpeedMod * modifier.excitement) >> 16, (avgSpeedMod * modifier.intensity) >> 16, 0);
}

static void RideRatingsApplyBonusDuration(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRatingsAdd(ratings, (std::min(ride.getTotalTime(), modifier.threshold) * modifier.excitement) >> 16, 0, 0);
}

static void RideRatingsApplyBonusGForces(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRating::Tuple subRating = ride_ratings_get_gforce_ratings(ride);
    RideRatingsAdd(
        ratings, (subRating.excitement * modifier.excitement) >> 16, (subRating.intensity * modifier.intensity) >> 16,
        (subRating.nausea * modifier.nausea) >> 16);
}

static void RideRatingsApplyBonusTurns(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRating::Tuple subRating = ride_ratings_get_turns_ratings(ride);
    RideRatingsAdd(
        ratings, (subRating.excitement * modifier.excitement) >> 16, (subRating.intensity * modifier.intensity) >> 16,
        (subRating.nausea * modifier.nausea) >> 16);
}

static void RideRatingsApplyBonusDrops(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRating::Tuple subRating = ride_ratings_get_drop_ratings(ride);
    RideRatingsAdd(
        ratings, (subRating.excitement * modifier.excitement) >> 16, (subRating.intensity * modifier.intensity) >> 16,
        (subRating.nausea * modifier.nausea) >> 16);
}

static void RideRatingsApplyBonusSheltered(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRating::Tuple subRating = ride_ratings_get_sheltered_ratings(ride);
    RideRatingsAdd(
        ratings, (subRating.excitement * modifier.excitement) >> 16, (subRating.intensity * modifier.intensity) >> 16,
        (subRating.nausea * modifier.nausea) >> 16);
}

static void RideRatingsApplyBonusRotations(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRatingsAdd(
        ratings, ride.rotations * modifier.excitement, ride.rotations * modifier.intensity, ride.rotations * modifier.nausea);
}

static void RideRatingsApplyBonusOperationOption(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    int32_t intensity = (modifier.intensity >= 0) ? (ride.operationOption * modifier.intensity)
                                                  : (ride.operationOption / std::abs(modifier.intensity));
    RideRatingsAdd(ratings, ride.operationOption * modifier.excitement, intensity, ride.operationOption * modifier.nausea);
}

static void RideRatingsApplyBonusReversedTrains(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.flags.has(RideFlag::reversedTrains))
    {
        RideRatingsAdd(
            ratings, ((ratings.excitement * modifier.excitement) >> 7), (ratings.intensity * modifier.intensity) >> 7,
            (ratings.nausea * modifier.nausea) >> 7);
    }
}

static void RideRatingsApplyBonusGoKartRace(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.mode == RideMode::race && ride.numTrains >= modifier.threshold)
    {
        RideRatingsAdd(ratings, modifier.excitement, modifier.intensity, modifier.nausea);

        int32_t lapsFactor = (ride.numLaps - 1) * 30;
        RideRatingsAdd(ratings, lapsFactor, lapsFactor / 2, 0);
    }
}

static void RideRatingsApplyBonusTowerRide(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    int32_t lengthFactor = ToHumanReadableRideLength(ride.getTotalLength());
    RideRatingsAdd(
        ratings, (lengthFactor * modifier.excitement) >> 16, (lengthFactor * modifier.intensity) >> 16,
        (lengthFactor * modifier.nausea) >> 16);
}

static void RideRatingsApplyBonusRotoDrop(RideRating::Tuple& ratings, const Ride& ride)
{
    int32_t lengthFactor = (ToHumanReadableRideLength(ride.getTotalLength()) * 209715) >> 16;
    RideRatingsAdd(ratings, lengthFactor, lengthFactor * 2, lengthFactor * 2);
}

static void RideRatingsApplyBonusMazeSize(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    int32_t size = std::min<uint16_t>(ride.mazeTiles, modifier.threshold);
    RideRatingsAdd(ratings, size * modifier.excitement, size * modifier.intensity, size * modifier.nausea);
}

static void RideRatingsApplyBonusBoatHireNoCircuit(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    // Most likely checking if the ride has does not have a circuit
    if (!ride.flags.has(RideFlag::tested))
    {
        RideRatingsAdd(ratings, modifier.excitement, modifier.intensity, modifier.nausea);
    }
}

static void RideRatingsApplyBonusSlideUnlimitedRides(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.mode == RideMode::unlimitedRidesPerAdmission)
    {
        RideRatingsAdd(ratings, modifier.excitement, modifier.intensity, modifier.nausea);
    }
}

static void RideRatingsApplyBonusMotionSimulatorMode(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    // Hardcoded until ride mode refactor
    if (ride.mode == RideMode::filmThrillRiders)
    {
        RideRatingsSet(ratings, RideRating::make(3, 25), RideRating::make(4, 10), RideRating::make(3, 30));
    }
    else
    {
        RideRatingsSet(ratings, RideRating::make(2, 90), RideRating::make(3, 50), RideRating::make(3, 00));
    }
}

static void RideRatingsApplyBonus3DCinemaMode(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    // Hardcoded until ride mode refactor
    switch (ride.mode)
    {
        default:
        case RideMode::mouseTails3DFilm:
            RideRatingsSet(ratings, RideRating::make(3, 50), RideRating::make(2, 40), RideRating::make(1, 40));
            break;
        case RideMode::stormChasers3DFilm:
            RideRatingsSet(ratings, RideRating::make(4, 00), RideRating::make(2, 65), RideRating::make(1, 55));
            break;
        case RideMode::spaceRaiders3DFilm:
            RideRatingsSet(ratings, RideRating::make(4, 20), RideRating::make(2, 60), RideRating::make(1, 48));
            break;
    }
}

static void RideRatingsApplyBonusTopSpinMode(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    // Hardcoded until ride mode refactor
    switch (ride.mode)
    {
        default:
        case RideMode::beginners:
            RideRatingsSet(ratings, RideRating::make(2, 00), RideRating::make(4, 80), RideRating::make(5, 74));
            break;
        case RideMode::intense:
            RideRatingsSet(ratings, RideRating::make(3, 00), RideRating::make(5, 75), RideRating::make(6, 64));
            break;
        case RideMode::berserk:
            RideRatingsSet(ratings, RideRating::make(3, 20), RideRating::make(6, 80), RideRating::make(7, 94));
            break;
    }
}

static void RideRatingsApplyBonusReversals(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier)
{
    int32_t numReversers = std::min<uint16_t>(state.AmountOfReversers, modifier.threshold);
    RideRatingsAdd(
        ratings, numReversers * modifier.excitement, numReversers * modifier.intensity, numReversers * modifier.nausea);
}

static void RideRatingsApplyBonusHoles(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    auto bonusHoles = std::min<uint8_t>(modifier.threshold, ride.numHoles);
    RideRatingsAdd(ratings, bonusHoles * modifier.excitement, bonusHoles * modifier.intensity, bonusHoles * modifier.nausea);
}

static void RideRatingsApplyBonusNumTrains(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    // For some reason the original code ran this twice, before and after the operation option bonus
    // Has been changed to call once with double value
    if (ride.numTrains >= modifier.threshold)
    {
        RideRatingsAdd(ratings, modifier.excitement, modifier.intensity, modifier.nausea);
    }
}

static void RideRatingsApplyBonusDownwardLaunch(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.mode == RideMode::downwardLaunch)
    {
        RideRatingsAdd(ratings, modifier.excitement, modifier.intensity, modifier.nausea);
    }
}

static void RideRatingsApplyBonusOperationOptionFreefall(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRatingsAdd(
        ratings, (ride.operationOption * modifier.excitement) >> 16, (ride.operationOption * modifier.intensity) >> 16,
        (ride.operationOption * modifier.nausea) >> 16);
}

static void RideRatingsApplyBonusLaunchedFreefallSpecial(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier)
{
    int32_t excitement = (ToHumanReadableRideLength(ride.getTotalLength()) * 32768) >> 16;
    RideRatingsAdd(ratings, excitement, 0, 0);

#ifdef ORIGINAL_RATINGS
    RideRatingsApplyBonusOperationOptionFreefall(ratings, ride, modifier);
#else
    // Only apply "launch speed" effects when the setting can be modified
    if (ride.mode == RideMode::upwardLaunch)
    {
        RideRatingsApplyBonusOperationOptionFreefall(ratings, ride, modifier);
    }
    else
    {
        // Fix #3282: When the ride mode is in downward launch mode, the intensity and
        //            nausea were fixed regardless of how high the ride is. The following
        //            calculation is based on roto-drop which is a similar mechanic.
        int32_t lengthFactor = (ToHumanReadableRideLength(ride.getTotalLength()) * 209715) >> 16;
        RideRatingsAdd(ratings, lengthFactor, lengthFactor * 2, lengthFactor * 2);
    }
#endif
}

static void RideRatingsApplyBonusProximity(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier)
{
    RideRatingsAdd(ratings, (ride_ratings_get_proximity_score(state) * modifier.excitement) >> 16, 0, 0);
}

static void RideRatingsApplyBonusScenery(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    RideRatingsAdd(ratings, (ride_ratings_get_scenery_score(ride) * modifier.excitement) >> 16, 0, 0);
}

static void RideRatingsApplyRequirementLength(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.getStation().SegmentLength < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementDropHeight(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.highestDropHeight < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementMaxSpeed(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.maxSpeed < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementNumDrops(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.numDrops < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementNegativeGs(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.maxNegativeVerticalG >= modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementLateralGs(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.maxLateralG < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementInversions(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.numInversions < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementUnsheltered(
    RideRating::Tuple& ratings, const Ride& ride, uint8_t shelteredEighths, RatingsModifier modifier)
{
    if (shelteredEighths >= modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementReversals(
    RideRating::Tuple& ratings, const Ride& ride, RideRating::UpdateState& state, RatingsModifier modifier)
{
    if (state.AmountOfReversers < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementHoles(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.numHoles < modifier.threshold)
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementStations(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (ride.numStations <= modifier.threshold)
    {
        // Excitement is set to 0 in original code - this could be changed for consistency
        ratings.excitement = 0;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

static void RideRatingsApplyRequirementSplashdown(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
    if (!ride.specialTrackElements.has(SpecialElement::splash))
    {
        ratings.excitement /= modifier.excitement;
        ratings.intensity /= modifier.intensity;
        ratings.nausea /= modifier.nausea;
    }
}

#ifndef ORIGINAL_RATINGS
static RideRating::Tuple ride_ratings_get_excessive_lateral_g_penalty(const Ride& ride)
{
    RideRating::Tuple result{};
    if (ride.maxLateralG > MakeFixed16_2dp(2, 80))
    {
        result.intensity = RideRating::make(3, 75);
        result.nausea = RideRating::make(2, 00);
    }

    if (ride.maxLateralG > MakeFixed16_2dp(3, 10))
    {
        // Remove half of the ride_ratings_get_gforce_ratings
        result.excitement = (ride.maxPositiveVerticalG * 5242) >> 16;

        // Apply maximum negative G force factor
        fixed16_2dp gforce = ride.maxNegativeVerticalG;
        result.excitement += (std::clamp<fixed16_2dp>(gforce, -RideRating::make(2, 50), RideRating::make(0, 00)) * -15728)
            >> 16;

        // Apply lateral G force factor
        result.excitement += (std::min<fixed16_2dp>(RideRating::make(1, 50), ride.maxLateralG) * 26214) >> 16;

        // Remove half of the ride_ratings_get_gforce_ratings
        result.excitement /= 2;
        result.excitement *= -1;
        result.intensity = RideRating::make(12, 25);
        result.nausea = RideRating::make(6, 00);
    }
    return result;
}
#endif

static void RideRatingsApplyPenaltyLateralGs(RideRating::Tuple& ratings, const Ride& ride, RatingsModifier modifier)
{
#ifndef ORIGINAL_RATINGS
    RideRating::Tuple subRating = ride_ratings_get_excessive_lateral_g_penalty(ride);
    RideRatingsAdd(
        ratings, (subRating.excitement * modifier.excitement) >> 16, (subRating.intensity * modifier.intensity) >> 16,
        (subRating.nausea * modifier.nausea) >> 16);
#endif
}

#pragma endregion

bool RideRating::Tuple::isNull() const
{
    return excitement == kUndefined;
}

void RideRating::Tuple::setNull()
{
    excitement = kUndefined;
}
