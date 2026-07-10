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

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct Vehicle;

namespace OpenRCT2
{
    class EntityRegistry;
    struct Guest;

    namespace RideVehicle::StationDetail
    {
        struct TrainSeatSummary
        {
            std::array<const Vehicle*, Limits::kMaxCarsPerTrain> cars{};
            uint16_t carCount{};
            uint32_t capacity{};
            uint32_t currentPeeps{};
            uint32_t reservedSeats{};

            bool HasRiders() const
            {
                return currentPeeps != 0;
            }

            std::span<const Vehicle* const> GetCars() const
            {
                return { cars.data(), carCount };
            }
        };

        TrainSeatSummary BuildTrainSeatSummary(const Vehicle& head);

        constexpr size_t kMaxPassengerCount = 32;

        struct PassengerUnloadPlan
        {
            std::array<uint8_t, kMaxPassengerCount> sourceIndices{};
            uint8_t passengerCount{};
            uint8_t continuingCount{};
        };

        PassengerUnloadPlan BuildPassengerUnloadPlan(std::span<const bool> shouldAlight);
        void ApplyTransportPassengerUnload(
            Vehicle& vehicle, std::span<Guest* const> originalPassengers, const PassengerUnloadPlan& plan);
        void ApplyOrdinaryPassengerUnload(Vehicle& vehicle, EntityRegistry& entities);
    } // namespace RideVehicle::StationDetail
} // namespace OpenRCT2
