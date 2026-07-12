/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../Limits.h"
#include "RideTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct Vehicle;
struct Ride;

namespace OpenRCT2
{
    class EntityRegistry;
    struct Guest;

    namespace RideVehicle::StationDetail
    {
        constexpr size_t kMaxPassengerCount = 32;
        constexpr uint16_t kGoKartRaceStartDelayMinTicks = 1;
        constexpr uint16_t kGoKartRaceStartDelayMaxTicks = 40;

        [[nodiscard]] constexpr uint16_t CalculateGoKartRaceStartDelay(uint32_t randomValue) noexcept
        {
            return kGoKartRaceStartDelayMinTicks
                + (randomValue % (kGoKartRaceStartDelayMaxTicks - kGoKartRaceStartDelayMinTicks + 1));
        }

        [[nodiscard]] bool ConsumeGoKartRaceStartDelay(bool raceStartActive, uint16_t& ticksRemaining) noexcept;

        struct TrainSeatSummary
        {
            std::array<const Vehicle*, Limits::kMaxCarsPerTrain> cars{};
            uint16_t carCount{};
            uint32_t capacity{};
            uint32_t currentPeeps{};
            uint32_t reservedSeats{};

            std::span<const Vehicle* const> GetCars() const
            {
                return { cars.data(), carCount };
            }
        };

        struct StationLoadingPolicy
        {
            bool incomingTrain{};
            bool initialDwellPending{};
            bool emptyTrainMustWait{};
            bool minimumWaitPending{};
            bool maximumWaitElapsed{};
            bool waitForLoad{};
            uint32_t loadTarget{};
        };

        [[nodiscard]] constexpr bool ShouldStopBoarding(
            const TrainSeatSummary& train, const StationLoadingPolicy& policy) noexcept
        {
            if (policy.incomingTrain)
                return true;
            if (policy.initialDwellPending || policy.emptyTrainMustWait || policy.minimumWaitPending)
                return false;
            return policy.maximumWaitElapsed || !policy.waitForLoad || train.currentPeeps >= policy.loadTarget;
        }

        TrainSeatSummary BuildTrainSeatSummary(const Vehicle& head);

        struct PlatformBoardingSeatRange
        {
            uint8_t firstSeat{};
            uint8_t seatCount{};
        };

        PlatformBoardingSeatRange GetPlatformBoardingSeatRange(const Vehicle& vehicle);

        struct PassengerUnloadPlan
        {
            std::array<uint8_t, kMaxPassengerCount> sourceIndices{};
            uint8_t continuingCount{};
        };

        PassengerUnloadPlan BuildPassengerUnloadPlan(std::span<const bool> shouldAlight);
        PassengerUnloadPlan BuildTransportPassengerUnloadPlan(
            const Ride& ride, StationIndex stationIndex, std::span<Guest* const> passengers);
        void ApplyTransportPassengerUnload(
            Vehicle& vehicle, std::span<Guest* const> originalPassengers, const PassengerUnloadPlan& plan);
        void ApplyOrdinaryPassengerUnload(Vehicle& vehicle, EntityRegistry& entities);

        bool BindPlatformGuestToSeat(Guest& guest, Vehicle& vehicle, uint8_t seatIndex);
    } // namespace RideVehicle::StationDetail
} // namespace OpenRCT2
