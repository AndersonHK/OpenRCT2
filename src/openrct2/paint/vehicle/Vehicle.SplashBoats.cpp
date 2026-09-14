/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../../ride/Vehicle.h"

#include "../../GameState.h"
#include "../../entity/EntityRegistry.h"
#include "../../ride/Ride.h"
#include "../../ride/RideEntry.h"
#include "../Paint.h"
#include "../entity/Paint.Vehicle.h"
#include "VehiclePaint.h"

#include <cstdint>

namespace OpenRCT2
{
    namespace
    {
        struct SplashPaintCall;
        thread_local const SplashPaintCall* activeSplashPaintCall = nullptr;

        struct SplashPaintCall
        {
            const PaintSession* session;
            const Vehicle* vehicle;
            const SplashPaintCall* previous = activeSplashPaintCall;

            SplashPaintCall(const PaintSession& paintSession, const Vehicle& paintVehicle)
                : session(&paintSession)
                , vehicle(&paintVehicle)
            {
                activeSplashPaintCall = this;
            }

            ~SplashPaintCall()
            {
                activeSplashPaintCall = previous;
            }
        };
    }

    /**
     *
     *  rct2: 0x006D4295
     */
    void VehicleVisualSplashBoatsOrWaterCoaster(
        PaintSession& session, int32_t x, int32_t imageDirection, int32_t y, int32_t z, const Vehicle* vehicle,
        const CarEntry* carEntry)
    {
        // A published vehicle chain can predate a live train-size edit. Guard the actual paint chain as well as the count.
        for (auto* call = activeSplashPaintCall; call != nullptr; call = call->previous)
        {
            if (call->session == &session && call->vehicle == vehicle)
                return;
        }
        const SplashPaintCall call(session, *vehicle);

        // Prevent infinite paint recursion for 0 or -1 cars per train.
        const auto* ride = GetRide(vehicle->ride);
        if (ride == nullptr)
            return;
        const auto* rideEntry = ride->getRideEntry();
        if (rideEntry == nullptr || ride->numCarsPerTrain - rideEntry->zero_cars < 1)
            return;

        auto* vehicleToPaint = vehicle->IsHead() ? GetEntityForPresentation<Vehicle>(vehicle->next_vehicle_on_ride)
                                                 : GetEntityForPresentation<Vehicle>(vehicle->prev_vehicle_on_ride);
        if (vehicleToPaint == nullptr)
        {
            return;
        }

        session.CurrentlyDrawnEntity = vehicleToPaint;
        imageDirection = Entity::Yaw::Add(Entity::Yaw::YawFrom4(session.CurrentRotation), vehicleToPaint->orientation);
        session.SpritePosition.x = vehicleToPaint->x;
        session.SpritePosition.y = vehicleToPaint->y;
        PaintVehicle(session, *vehicleToPaint, imageDirection);
    }
} // namespace OpenRCT2
