/*****************************************************************************
 * Copyright (c) 2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

    #include "ScRide.hpp"

    #include "../../../Context.h"
    #include "../../../core/EnumMap.hpp"
    #include "../../../core/UnitConversion.h"
    #include "../../../ride/Ride.h"
    #include "../../../ride/RideBreakdownMap.h"
    #include "../../../ride/RideData.h"
    #include "../../ScriptEngine.h"
    #include "../object/ScObject.hpp"

namespace OpenRCT2::Scripting
{
    void ScRide::Register(JSContext* ctx)
    {
        static constexpr JSCFunctionListEntry funcs[] = {
            JS_CGETSET_DEF("id", ScRide::id_get, nullptr),
            JS_CGETSET_DEF("object", ScRide::object_get, nullptr),
            JS_CGETSET_DEF("type", ScRide::type_get, nullptr),
            JS_CGETSET_DEF("classification", ScRide::classification_get, nullptr),
            JS_CGETSET_DEF("name", ScRide::name_get, ScRide::name_set),
            JS_CGETSET_DEF("status", ScRide::status_get, nullptr),
            JS_CGETSET_DEF("lifecycleFlags", ScRide::flags_get, ScRide::flags_set),
            JS_CGETSET_DEF("flags", ScRide::flags_get, ScRide::flags_set),
            JS_CGETSET_DEF("mode", ScRide::mode_get, ScRide::mode_set),
            JS_CGETSET_DEF("departFlags", ScRide::departFlags_get, ScRide::departFlags_set),
            JS_CGETSET_DEF("minimumWaitingTime", ScRide::minimumWaitingTime_get, ScRide::minimumWaitingTime_set),
            JS_CGETSET_DEF("maximumWaitingTime", ScRide::maximumWaitingTime_get, ScRide::maximumWaitingTime_set),
            JS_CGETSET_DEF("vehicles", ScRide::vehicles_get, nullptr),
            JS_CGETSET_DEF("vehicleColours", ScRide::vehicleColours_get, ScRide::vehicleColours_set),
            JS_CGETSET_DEF("colourSchemes", ScRide::colourSchemes_get, ScRide::colourSchemes_set),
            JS_CGETSET_DEF("stationStyle", ScRide::stationStyle_get, ScRide::stationStyle_set),
            JS_CGETSET_DEF("music", ScRide::music_get, ScRide::music_set),
            JS_CGETSET_DEF("stations", ScRide::stations_get, nullptr),
            JS_CGETSET_DEF("price", ScRide::price_get, ScRide::price_set),
            JS_CGETSET_DEF("excitement", ScRide::excitement_get, ScRide::excitement_set),
            JS_CGETSET_DEF("intensity", ScRide::intensity_get, ScRide::intensity_set),
            JS_CGETSET_DEF("nausea", ScRide::nausea_get, ScRide::nausea_set),
            JS_CGETSET_DEF("totalCustomers", ScRide::totalCustomers_get, ScRide::totalCustomers_set),
            JS_CGETSET_DEF("buildDate", ScRide::buildDate_get, ScRide::buildDate_set),
            JS_CGETSET_DEF("age", ScRide::age_get, nullptr),
            JS_CGETSET_DEF("runningCost", ScRide::runningCost_get, ScRide::runningCost_set),
            JS_CGETSET_DEF("totalProfit", ScRide::totalProfit_get, ScRide::totalProfit_set),
            JS_CGETSET_DEF("inspectionInterval", ScRide::inspectionInterval_get, ScRide::inspectionInterval_set),
            JS_CGETSET_DEF("value", ScRide::value_get, ScRide::value_set),
            JS_CGETSET_DEF("downtime", ScRide::downtime_get, nullptr),
            JS_CGETSET_DEF("liftHillSpeed", ScRide::liftHillSpeed_get, ScRide::liftHillSpeed_set),
            JS_CGETSET_DEF("maxLiftHillSpeed", ScRide::maxLiftHillSpeed_get, nullptr),
            JS_CGETSET_DEF("minLiftHillSpeed", ScRide::minLiftHillSpeed_get, nullptr),
            JS_CGETSET_DEF("satisfaction", ScRide::satisfaction_get, nullptr),
            JS_CGETSET_DEF("maxSpeed", ScRide::maxSpeed_get, nullptr),
            JS_CGETSET_DEF("averageSpeed", ScRide::averageSpeed_get, nullptr),
            JS_CGETSET_DEF("rideTime", ScRide::rideTime_get, nullptr),
            JS_CGETSET_DEF("rideLength", ScRide::rideLength_get, nullptr),
            JS_CGETSET_DEF("maxPositiveVerticalGs", ScRide::maxPositiveVerticalGs_get, nullptr),
            JS_CGETSET_DEF("maxNegativeVerticalGs", ScRide::maxNegativeVerticalGs_get, nullptr),
            JS_CGETSET_DEF("maxLateralGs", ScRide::maxLateralGs_get, nullptr),
            JS_CGETSET_DEF("maxPositiveLongitudinalGs", ScRide::maxPositiveLongitudinalGs_get, nullptr),
            JS_CGETSET_DEF("maxNegativeLongitudinalGs", ScRide::maxNegativeLongitudinalGs_get, nullptr),
            JS_CGETSET_DEF("totalAirTime", ScRide::totalAirTime_get, nullptr),
            JS_CGETSET_DEF("numDrops", ScRide::numDrops_get, nullptr),
            JS_CGETSET_DEF("numLiftHills", ScRide::numLiftHills_get, nullptr),
            JS_CGETSET_DEF("highestDropHeight", ScRide::highestDropHeight_get, nullptr),
            JS_CGETSET_DEF("breakdown", ScRide::breakdown_get, nullptr),
            JS_CFUNC_DEF("setBreakdown", 1, ScRide::setBreakdown),
            JS_CFUNC_DEF("fixBreakdown", 0, ScRide::fixBreakdown),
        };
        RegisterBase(ctx, "Ride", Finalize, funcs);
    }

    JSValue ScRide::New(JSContext* ctx, RideId rideId)
    {
        return MakeWithOpaque(ctx, new RideData{ rideId });
    }

    void ScRide::Finalize(JSRuntime* rt, JSValue thisVal)
    {
        RideData* data = GetRideData(thisVal);
        if (data)
            delete data;
    }

    ScRide::RideData* ScRide::GetRideData(JSValue thisVal)
    {
        return gScRide.GetOpaque<RideData*>(thisVal);
    }

    Ride* ScRide::GetRide(JSValue thisVal)
    {
        RideData* data = GetRideData(thisVal);
        return ::GetRide(data->_rideId);
    }

    JSValue ScRide::id_get(JSContext* ctx, JSValue thisVal)
    {
        RideData* data = GetRideData(thisVal);
        return JS_NewInt32(ctx, data->_rideId.ToUnderlying());
    }

    JSValue ScRide::object_get(JSContext* ctx, JSValue thisVal)
    {
        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            auto rideObject = GetContext()->GetObjectManager().GetLoadedObject<RideObject>(ride->subtype);
            if (rideObject != nullptr)
            {
                return ScRideObject::New(ctx, ObjectType::ride, ride->subtype);
            }
        }
        return JS_NULL;
    }

    #define DEFINE_RIDE_NUMBER_GETTER(name, constructor, expression)                                                           \
        JSValue ScRide::name(JSContext* ctx, JSValue thisVal)                                                                  \
        {                                                                                                                      \
            const auto* ride = GetRide(thisVal);                                                                               \
            return constructor(ctx, ride != nullptr ? (expression) : 0);                                                       \
        }

    DEFINE_RIDE_NUMBER_GETTER(type_get, JS_NewInt32, ride->type)

    JSValue ScRide::classification_get(JSContext* ctx, JSValue thisVal)
    {
        auto ride = GetRide(thisVal);
        std::string str = "";
        if (ride != nullptr)
        {
            switch (ride->getClassification())
            {
                case RideClassification::ride:
                    str = "ride";
                    break;
                case RideClassification::shopOrStall:
                    str = "stall";
                    break;
                case RideClassification::kioskOrFacility:
                    str = "facility";
                    break;
            }
        }
        return JSFromStdString(ctx, str);
    }

    JSValue ScRide::name_get(JSContext* ctx, JSValue thisVal)
    {
        auto ride = GetRide(thisVal);
        return JSFromStdString(ctx, ride != nullptr ? ride->getName() : std::string());
    }

    JSValue ScRide::name_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_STR(valueStr, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->customName = std::move(valueStr);
        }
        return JS_UNDEFINED;
    }

    JSValue ScRide::status_get(JSContext* ctx, JSValue thisVal)
    {
        auto ride = GetRide(thisVal);
        std::string str = "";

        if (ride != nullptr)
        {
            switch (ride->status)
            {
                case RideStatus::closed:
                    str = "closed";
                    break;
                case RideStatus::open:
                    str = "open";
                    break;
                case RideStatus::testing:
                    str = "testing";
                    break;
                case RideStatus::simulating:
                    str = "simulating";
                    break;
                case RideStatus::count:
                    str = "count";
                    break;
            }
        }
        return JSFromStdString(ctx, str);
    }

    DEFINE_RIDE_NUMBER_GETTER(flags_get, JS_NewUint32, ride->flags.holder)

    JSValue ScRide::flags_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->flags.holder = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(mode_get, JS_NewUint32, static_cast<uint8_t>(ride->mode))

    JSValue ScRide::mode_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->mode = static_cast<RideMode>(valueInt);
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(departFlags_get, JS_NewUint32, ride->departFlags)

    JSValue ScRide::departFlags_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->departFlags = static_cast<uint8_t>(valueInt);
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(minimumWaitingTime_get, JS_NewUint32, ride->minWaitingTime)

    JSValue ScRide::minimumWaitingTime_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->minWaitingTime = static_cast<uint8_t>(valueInt);
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(maximumWaitingTime_get, JS_NewUint32, ride->maxWaitingTime)

    JSValue ScRide::maximumWaitingTime_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->maxWaitingTime = static_cast<uint8_t>(valueInt);
        }
        return JS_UNDEFINED;
    }

    JSValue ScRide::vehicles_get(JSContext* ctx, JSValue thisVal)
    {
        JSValue result = JS_NewArray(ctx);
        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            int64_t index = 0;
            std::for_each(std::begin(ride->vehicles), std::begin(ride->vehicles) + ride->numTrains, [&](auto& veh) {
                JS_SetPropertyInt64(ctx, result, index++, JS_NewUint32(ctx, veh.ToUnderlying()));
            });
        }
        return result;
    }

    JSValue ScRide::vehicleColours_get(JSContext* ctx, JSValue thisVal)
    {
        JSValue result = JS_NewArray(ctx);
        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            int64_t index = 0;
            for (const auto& vehicleColour : ride->vehicleColours)
            {
                JS_SetPropertyInt64(ctx, result, index++, ToJSValue(ctx, vehicleColour));
            }
        }
        return result;
    }

    JSValue ScRide::vehicleColours_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr && JS_IsArray(value))
        {
            int64_t length;
            JS_GetLength(ctx, value, &length);

            auto count = std::min(static_cast<size_t>(length), std::size(ride->vehicleColours));
            for (size_t i = 0; i < count; i++)
            {
                JSValue item = JS_GetPropertyInt64(ctx, value, static_cast<int64_t>(i));
                ride->vehicleColours[i] = JSToVehicleColours(ctx, item);
                JS_FreeValue(ctx, item);
            }
        }
        return JS_UNDEFINED;
    }

    JSValue ScRide::colourSchemes_get(JSContext* ctx, JSValue thisVal)
    {
        JSValue result = JS_NewArray(ctx);
        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            int64_t index = 0;
            for (const auto& trackColour : ride->trackColours)
            {
                JS_SetPropertyInt64(ctx, result, index++, ToJSValue(ctx, trackColour));
            }
        }
        return result;
    }

    JSValue ScRide::colourSchemes_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr && JS_IsArray(value))
        {
            int64_t length;
            JS_GetLength(ctx, value, &length);

            auto count = std::min(static_cast<size_t>(length), std::size(ride->trackColours));
            for (size_t i = 0; i < count; i++)
            {
                JSValue item = JS_GetPropertyInt64(ctx, value, static_cast<int64_t>(i));
                ride->trackColours[i] = JSToTrackColour(ctx, item);
                JS_FreeValue(ctx, item);
            }
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(stationStyle_get, JS_NewUint32, ride->entranceStyle)

    JSValue ScRide::stationStyle_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->entranceStyle = static_cast<ObjectEntryIndex>(valueInt);
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(music_get, JS_NewUint32, ride->music)

    JSValue ScRide::music_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->music = static_cast<ObjectEntryIndex>(valueInt);
        }
        return JS_UNDEFINED;
    }

    JSValue ScRide::stations_get(JSContext* ctx, JSValue thisVal)
    {
        JSValue result = JS_NewArray(ctx);
        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            int64_t index = 0;
            for (const auto& station : ride->getStations())
            {
                JS_SetPropertyInt64(ctx, result, index++, gScRideStation.New(ctx, ride->id, ride->getStationIndex(&station)));
            }
        }
        return result;
    }

    JSValue ScRide::price_get(JSContext* ctx, JSValue thisVal)
    {
        JSValue result = JS_NewArray(ctx);
        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            auto numPrices = ride->getNumPrices();
            for (size_t i = 0; i < numPrices; i++)
            {
                JS_SetPropertyInt64(ctx, result, static_cast<int64_t>(i), JS_NewInt64(ctx, ride->price[i]));
            }
        }
        return result;
    }

    JSValue ScRide::price_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr && JS_IsArray(value))
        {
            int64_t length;
            JS_GetLength(ctx, value, &length);

            auto numPrices = std::min(static_cast<size_t>(length), ride->getNumPrices());
            for (size_t i = 0; i < numPrices; i++)
            {
                JSValue item = JS_GetPropertyInt64(ctx, value, static_cast<uint32_t>(i));
                JS_UNPACK_MONEY64(money, ctx, item);
                ride->price[i] = std::clamp<money64>(money, kRideMinPrice, kRideMaxPrice);
            }
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(excitement_get, JS_NewInt32, ride->ratings.excitement)

    JSValue ScRide::excitement_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_INT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->ratings.excitement = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(intensity_get, JS_NewInt32, ride->ratings.intensity)

    JSValue ScRide::intensity_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_INT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->ratings.intensity = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(nausea_get, JS_NewInt32, ride->ratings.nausea)

    JSValue ScRide::nausea_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_INT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->ratings.nausea = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(totalCustomers_get, JS_NewInt32, ride->totalCustomers)

    JSValue ScRide::totalCustomers_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_INT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->totalCustomers = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(buildDate_get, JS_NewInt32, ride->buildDate)

    JSValue ScRide::buildDate_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_INT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->buildDate = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(age_get, JS_NewInt32, ride->getAge())

    DEFINE_RIDE_NUMBER_GETTER(runningCost_get, JS_NewInt64, ride->upkeepCost)

    JSValue ScRide::runningCost_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_MONEY64(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->upkeepCost = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(totalProfit_get, JS_NewInt64, ride->totalProfit)

    JSValue ScRide::totalProfit_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_MONEY64(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->totalProfit = valueInt;
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(inspectionInterval_get, JS_NewUint32, EnumValue(ride->inspectionInterval))

    JSValue ScRide::inspectionInterval_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            auto clamped = std::clamp<uint8_t>(
                static_cast<uint8_t>(valueInt), EnumValue(RideInspection::every10Minutes), EnumValue(RideInspection::never));
            ride->inspectionInterval = static_cast<RideInspection>(clamped);
        }
        return JS_UNDEFINED;
    }

    JSValue ScRide::value_get(JSContext* ctx, JSValue thisVal)
    {
        auto ride = GetRide(thisVal);
        if (ride != nullptr && ride->value != kRideValueUndefined)
        {
            return JS_NewInt64(ctx, ride->value);
        }
        return JS_NULL;
    }

    JSValue ScRide::value_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            if (JS_IsNumber(value))
            {
                JS_UNPACK_MONEY64(valueInt, ctx, value);
                ride->value = valueInt;
            }
            else
            {
                ride->value = kRideValueUndefined;
            }
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(downtime_get, JS_NewUint32, ride->downtime)

    DEFINE_RIDE_NUMBER_GETTER(liftHillSpeed_get, JS_NewUint32, ride->liftHillSpeed)

    JSValue ScRide::liftHillSpeed_set(JSContext* ctx, JSValue thisVal, JSValue value)
    {
        JS_UNPACK_UINT32(valueInt, ctx, value);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            ride->liftHillSpeed = static_cast<uint8_t>(valueInt);
        }
        return JS_UNDEFINED;
    }

    DEFINE_RIDE_NUMBER_GETTER(maxLiftHillSpeed_get, JS_NewUint32, ride->getRideTypeDescriptor().LiftData.maximum_speed)

    DEFINE_RIDE_NUMBER_GETTER(minLiftHillSpeed_get, JS_NewUint32, ride->getRideTypeDescriptor().LiftData.minimum_speed)

    DEFINE_RIDE_NUMBER_GETTER(satisfaction_get, JS_NewUint32, ride->satisfaction * 5)

    DEFINE_RIDE_NUMBER_GETTER(maxSpeed_get, JS_NewFloat64, ToHumanReadableSpeed(ride->getDisplayMaxSpeed()))

    DEFINE_RIDE_NUMBER_GETTER(averageSpeed_get, JS_NewFloat64, ToHumanReadableSpeed(ride->getDisplayAverageSpeed()))

    DEFINE_RIDE_NUMBER_GETTER(rideTime_get, JS_NewInt32, ride->getDisplayTotalTime())

    DEFINE_RIDE_NUMBER_GETTER(rideLength_get, JS_NewFloat64, ToHumanReadableRideLength(ride->getDisplayTotalLength()))

    DEFINE_RIDE_NUMBER_GETTER(maxPositiveVerticalGs_get, JS_NewFloat64, ride->getDisplayMaxPositiveVerticalG() / 100.0)

    DEFINE_RIDE_NUMBER_GETTER(maxNegativeVerticalGs_get, JS_NewFloat64, ride->getDisplayMaxNegativeVerticalG() / 100.0)

    DEFINE_RIDE_NUMBER_GETTER(maxLateralGs_get, JS_NewFloat64, ride->getDisplayMaxLateralG() / 100.0)

    DEFINE_RIDE_NUMBER_GETTER(maxPositiveLongitudinalGs_get, JS_NewFloat64, ride->getDisplayMaxPositiveLongitudinalG() / 100.0)

    DEFINE_RIDE_NUMBER_GETTER(maxNegativeLongitudinalGs_get, JS_NewFloat64, ride->getDisplayMaxNegativeLongitudinalG() / 100.0)

    DEFINE_RIDE_NUMBER_GETTER(totalAirTime_get, JS_NewFloat64, ToHumanReadableAirTime(ride->getDisplayTotalAirTime()) / 100.0)

    DEFINE_RIDE_NUMBER_GETTER(numDrops_get, JS_NewUint32, ride->getDisplayNumDrops())

    DEFINE_RIDE_NUMBER_GETTER(numLiftHills_get, JS_NewUint32, ride->getDisplayNumPoweredLifts())

    DEFINE_RIDE_NUMBER_GETTER(highestDropHeight_get, JS_NewFloat64, ride->getDisplayHighestDropHeight())

    #undef DEFINE_RIDE_NUMBER_GETTER

    JSValue ScRide::breakdown_get(JSContext* ctx, JSValue thisVal)
    {
        auto ride = GetRide(thisVal);
        if (ride != nullptr)
        {
            if (!ride->flags.has(RideFlag::brokenDown))
            {
                return JSFromStdString(ctx, "none");
            }
            auto it = kBreakdownMap.find(ride->breakdownReason);
            if (it != kBreakdownMap.end())
                return JSFromStdString(ctx, std::string(it->first));
        }
        return JSFromStdString(ctx, "");
    }

    JSValue ScRide::setBreakdown(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
    {
        JS_UNPACK_STR(breakDown, ctx, argv[0]);
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr && ride->canBreakDown() && ride->status == RideStatus::open)
        {
            auto it = kBreakdownMap.find(breakDown);
            if (it != kBreakdownMap.end())
            {
                RidePrepareBreakdown(*ride, it->second);
            }
        }
        return JS_UNDEFINED;
    }

    JSValue ScRide::fixBreakdown(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();

        auto ride = GetRide(thisVal);
        if (ride != nullptr && ride->canBreakDown())
        {
            RideFixBreakdown(*ride, 0);
        }
        return JS_UNDEFINED;
    }

} // namespace OpenRCT2::Scripting

#endif
