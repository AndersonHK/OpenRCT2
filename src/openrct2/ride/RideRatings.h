/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../Identifiers.h"
#include "../core/FixedPoint.hpp"
#include "../world/Location.hpp"

#include <array>
#include <cstdint>

struct Ride;
struct RideObjectEntry;
struct RideRatingAccumulator;

namespace OpenRCT2
{
    enum class TrackElemType : uint16_t;
    using RideRating_t = fixed16_2dp;
    namespace RideRating
    {
        // Convenience function for writing ride ratings. The result is a 16 bit signed
        // integer. To create the ride rating 3.65 type MakeRideRating(3, 65).
        constexpr RideRating_t make(int16_t whole, uint8_t fraction)
        {
            return MakeFixed2dp<RideRating_t>(whole, fraction);
        }

        constexpr RideRating_t kUndefined = 0xFFFFu;

#pragma pack(push, 1)

        // Used for return values, for functions that modify all three.
        struct Tuple
        {
            RideRating_t excitement{};
            RideRating_t intensity{};
            RideRating_t nausea{};

            bool isNull() const;
            void setNull();

            bool operator==(const Tuple& rhs) const = default;
        };
        static_assert(sizeof(Tuple) == 6);

#pragma pack(pop)

        struct TickScore
        {
            int64_t excitement{};
            int64_t intensity{};
            int64_t nausea{};
        };

        struct LocalContextScore
        {
            int32_t excitement{};
            int32_t intensity{};
            int32_t nausea{};
            int32_t scenery{};
            int32_t pathProximity{};
            int32_t foreignTrackProximity{};
            int32_t verticalInteraction{};
        };

        struct UpdateState
        {
            CoordsXYZ Proximity;
            CoordsXYZ ProximityStart;
            RideId CurrentRide;
            uint8_t State;
            TrackElemType ProximityTrackType;
            uint8_t ProximityBaseHeight;
            uint16_t ProximityTotal;
            uint16_t ProximityScores[26];
            uint16_t AmountOfBrakes;
            uint16_t amountOfBoosters;
            uint16_t AmountOfReversers;
            uint16_t StationFlags;
        };

        static constexpr size_t kMaxUpdateStates = 4;
        using UpdateStates = std::array<UpdateState, kMaxUpdateStates>;

        TickScore ScoreAirtimeGForTick(int32_t verticalG);
        TickScore ScoreNegativeVerticalGForTick(int32_t verticalG);
        TickScore ScorePositiveVerticalGForTick(int32_t verticalG);
        TickScore ScoreLateralGForTick(int32_t lateralG);
        TickScore ScoreGForcesForTick(int32_t verticalG, int32_t lateralG);
        TickScore ApplyRideEntryMultipliers(TickScore score, const RideObjectEntry& rideEntry);
        int32_t ScoreSceneryForLocalContext(int32_t rawScenery);
        LocalContextScore GetLocalContextScore(const CoordsXYZ& origin, RideId rideId);
        CoordsXYZ GetFixedRideLocalContextOrigin(const Ride& ride);
        void InvalidateLocalContextCacheAround(const CoordsXY& location);
        void ClearLocalContextCache();

        void ResetUpdateStates();
        void UpdateRide(const Ride& ride);
        void RecordRiderSample(Ride& ride, const RideRatingAccumulator& sample);
        bool RecordActiveRiderSample(Ride& ride, EntityId sampleEntity);
        void UpdateAll();
    } // namespace RideRating
} // namespace OpenRCT2

// Special Track Element Adjustment functions for RTDs
void SpecialTrackElementRatingsAjustment_Default(const Ride& ride, int32_t& excitement, int32_t& intensity, int32_t& nausea);
void SpecialTrackElementRatingsAjustment_GhostTrain(const Ride& ride, int32_t& excitement, int32_t& intensity, int32_t& nausea);
void SpecialTrackElementRatingsAjustment_LogFlume(const Ride& ride, int32_t& excitement, int32_t& intensity, int32_t& nausea);
