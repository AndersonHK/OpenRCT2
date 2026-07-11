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

        struct PlatformBoardingSeatRange
        {
            uint8_t firstSeat{};
            uint8_t seatCount{};
        };

        struct TrainBoardingSeat
        {
            uint32_t slotIndex{};
            uint8_t carIndex{};
            uint8_t seatIndex{};
        };

        struct TrainBoardingSeatPlan
        {
            std::array<TrainBoardingSeat, Limits::kMaxCarsPerTrain * kMaxPassengerCount> seats{};
            uint16_t seatCount{};
        };

        PlatformBoardingSeatRange GetPlatformBoardingSeatRange(const Vehicle& vehicle);
        TrainBoardingSeatPlan BuildTrainBoardingSeatPlan(const TrainSeatSummary& train);

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
