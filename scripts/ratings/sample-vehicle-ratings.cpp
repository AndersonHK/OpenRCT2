/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Experimental kart perception model: NOT used by the game. Baselines call production helpers.
// See docs/archive/ratings/go-kart-experience-calibration.md and kart-calibration.py.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/RideRatings.h>

using namespace OpenRCT2;
using namespace OpenRCT2::RideRating;

enum class CalibrationModel
{
    generic,
    goKart,
    sustainedGoKart,
    sustainedWithFatigue
};
struct CalibrationProfile : SampledRideRatingProfile
{
    CalibrationModel Model{};
};
static constexpr int64_t kTrackedRideRawPerLocalContextPoint = 400;
static constexpr int64_t kTrackedRideRawPerVerticalContextPoint = 800;
static TickScore RideRatingScaleTickScore(TickScore value, int32_t coefficient)
{
    auto scale = std::max(coefficient, 0);
    return { value.excitement * scale / 1000, value.intensity * scale / 1000, value.nausea * scale / 1000 };
}
static void RideRatingAddTickScore(TickScore& target, const TickScore& value)
{
    target.excitement += value.excitement;
    target.intensity += value.intensity;
    target.nausea += value.nausea;
}

static int32_t UpdateSustainedAcceleration(int32_t& history, int32_t measured)
{
    // A 20-tick (0.5-second) signed low-pass filter rejects rapid throttle/brake cancellation.
    // Fractional hundredths of G retain precision; all updates are integer and deterministic.
    const auto target = std::clamp(measured, -1000, 1000) * 1024;
    history += (target - history) / 20;
    const auto filtered = history / 1024;
    if ((filtered > 0) != (measured > 0))
        return 0;
    return std::clamp(filtered, std::min(measured, 0), std::max(measured, 0));
}

RideRating::TickScore ScoreKartContext(
    VehicleLocalContextCache& runtimeCache, int64_t normalisedSpeed, const CalibrationProfile& profile, bool isBoatHire)
{
    if (profile.Model == CalibrationModel::generic)
        return ScoreCachedLocalContextForVehicleTick(runtimeCache, normalisedSpeed, profile.LocalContext, isBoatHire);

    // Kart scenery provides atmosphere; height and near misses are not driving rewards. Bound the combined
    // atmosphere source so decoration density cannot replace an interesting circuit.
    const auto& context = runtimeCache.environment.context;
    const auto atmosphere = std::clamp<int64_t>(
        static_cast<int64_t>(context.scenery) * 2 + context.pathProximity + context.foreignTrackProximity * 2
            + context.pathBridge,
        0, 36);
    const auto speed = std::clamp<int64_t>(normalisedSpeed, 0, 12);
    const auto height = std::max<int64_t>(context.trackHeightExposure, 0);
    return RideRatingScaleTickScore(
        {
            .excitement = atmosphere * kTrackedRideRawPerLocalContextPoint * speed / kVehicleRatingBaselineSpeed,
            .intensity = height * kTrackedRideRawPerVerticalContextPoint * speed / kVehicleRatingBaselineSpeed,
            .nausea = height * kRideRatingAccumulatorRawScale * speed / (kVehicleRatingBaselineSpeed * 10),
        },
        profile.LocalContext);
}

static RideRating::TickScore ScoreKartForces(
    int32_t verticalG, int32_t lateralG, int32_t longitudinalG, int32_t sustainedG, const CalibrationProfile& profile,
    const RideRating::VehicleGForceSpeedContext& speedContext)
{
    using RideRating::TickScore;
    const auto toRaw = [](double value) {
        return static_cast<int64_t>(std::llround(value * RideRating::kRideRatingAccumulatorRawScale));
    };
    const auto engagement = std::clamp(static_cast<double>(speedContext.normalisedSpeed) / 4.0, 0.0, 1.0);
    const auto lateral = std::abs(static_cast<double>(lateralG)) / 100.0;
    const auto acceleration = std::abs(static_cast<double>(longitudinalG)) / 100.0;
    const bool braking = longitudinalG < 0;
    const auto sustained = std::abs(static_cast<double>(sustainedG)) / 100.0;
    const auto enjoyment = profile.Model >= CalibrationModel::sustainedGoKart
        ? sustained * sustained / (sustained * sustained + 0.08 * 0.08)
            * std::exp(-std::pow(sustained / (braking ? 0.65 : 0.4), 4.0))
        : acceleration / (acceleration + 0.07) * std::exp(-std::pow(acceleration / (braking ? 0.65 : 0.4), 4.0));
    const auto harshLateral = std::max(lateral - 0.8, 0.0);
    const auto harshLongitudinal = std::max(acceleration - (braking ? 0.5 : 0.4), 0.0);

    // Reward manageable cornering and acceleration out of corners. Scalar-only tuning of the generic
    // power curves rewards violent throttle/brake oscillation; these enjoyment curves instead taper.
    TickScore result = RideRatingScaleTickScore(
        {
            .excitement = toRaw(engagement * 4.0 * lateral / (lateral + 0.25) * std::exp(-std::pow(lateral / 0.85, 4.0))),
            .intensity = toRaw(lateral * lateral + 4.0 * harshLateral * harshLateral),
            .nausea = toRaw(0.4 * lateral * lateral + 2.0 * harshLateral * harshLateral),
        },
        profile.LateralG);
    RideRatingAddTickScore(
        result,
        RideRatingScaleTickScore(
            {
                .excitement = toRaw(engagement * (braking ? 6.6 : 12.0) * enjoyment),
                .intensity = toRaw(
                    (braking ? 3.0 : 2.0) * acceleration * acceleration
                    + (braking ? 18.0 : 16.0) * harshLongitudinal * harshLongitudinal),
                .nausea = toRaw(
                    (braking ? 0.8 : 0.4) * acceleration * acceleration
                    + (braking ? 10.0 : 8.0) * harshLongitudinal * harshLongitudinal),
            },
            profile.LongitudinalG));

    const auto lightness = std::clamp(1.0 - static_cast<double>(verticalG) / 100.0, 0.0, 1.0);
    const auto negative = std::max(-static_cast<double>(verticalG) / 100.0, 0.0);
    const auto compression = std::max(static_cast<double>(verticalG) / 100.0 - 1.0, 0.0);
    const auto hardLanding = std::max(compression - 0.5, 0.0);
    // In a kart, loss of wheel contact and hard landings are discomfort, not coaster airtime excitement.
    RideRatingAddTickScore(
        result,
        RideRatingScaleTickScore(
            { 0, toRaw(6.0 * lightness * lightness), toRaw(2.0 * lightness * lightness) }, profile.Airtime));
    RideRatingAddTickScore(
        result,
        RideRatingScaleTickScore(
            { 0, toRaw(12.0 * negative * negative + 3.0 * compression * compression + 4.0 * hardLanding * hardLanding),
              toRaw(6.0 * negative * negative + 1.5 * compression * compression + 3.0 * hardLanding * hardLanding) },
            profile.VerticalG));
    return result;
}

RideRating::TickScore ScoreKartSpeed(int32_t speed, const CalibrationProfile& profile)
{
    if (profile.Model == CalibrationModel::generic)
        return ScoreVehicleSpeedForTick(speed, profile.Speed);

    // 12 internal speed units correspond to 27 displayed mph. Enjoyment approaches a ceiling,
    // whereas high speed continues to add intensity. Steady ordinary cruising adds little nausea.
    const auto ratio = static_cast<double>(std::max(speed, 0)) / 12.0;
    return RideRatingScaleTickScore(
        {
            .excitement = static_cast<int64_t>(std::llround(4.0 * ratio / (1.0 + ratio) * kRideRatingAccumulatorRawScale)),
            .intensity = static_cast<int64_t>(std::llround(0.4 * ratio * ratio * kRideRatingAccumulatorRawScale)),
            .nausea = static_cast<int64_t>(std::llround(0.03 * ratio * ratio * kRideRatingAccumulatorRawScale)),
        },
        profile.Speed);
}

RideRating::TickScore ApplyKartComfort(
    TickScore score, int32_t verticalG, int32_t lateralG, int32_t longitudinalG, const CalibrationProfile& profile)
{
    if (profile.Model == CalibrationModel::generic)
        return score;

    // Discomfort suppresses the enjoyment of every source in this tick, including speed, scenery and
    // descriptor bonuses. Keep intensity/nausea intact, so a violent tick cannot become harmless.
    const auto vertical = std::clamp<int64_t>(100 - std::abs(static_cast<int64_t>(verticalG) - 100), 0, 100);
    const auto lateral = std::clamp<int64_t>(200 - std::abs(static_cast<int64_t>(lateralG)), 0, 120);
    const auto longitudinal = std::clamp<int64_t>(150 - std::abs(static_cast<int64_t>(longitudinalG)), 0, 100);
    score.excitement = score.excitement * vertical / 100 * lateral / 120 * longitudinal / 100;
    return score;
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1]);
    std::ofstream output(argv[2]);
    if (!input || !output)
        return 3;
    int id, speed, vertical, lateral, longitudinal, scenery, height, model, turn, down, history, tick;
    CalibrationProfile profile{};
    while (input >> id >> profile.LocalContext >> profile.Speed >> profile.LongitudinalG >> profile.VerticalG
           >> profile.LateralG >> profile.Airtime >> profile.SpeedGCoupling >> model >> speed >> vertical >> lateral
           >> longitudinal >> scenery >> height >> turn >> down >> history >> tick)
    {
        profile.Model = static_cast<CalibrationModel>(model);
        const auto sustained = UpdateSustainedAcceleration(history, longitudinal);
        auto speedScore = ScoreKartSpeed(speed, profile);
        auto forces = model == 0
            ? RideRating::ScoreGForcesForVehicleTick(vertical, lateral, longitudinal, speed, profile)
            : ScoreKartForces(
                  vertical, lateral, longitudinal, sustained, profile, RideRating::PrepareVehicleGForceSpeedContext(speed, 0));
        RideRating::VehicleLocalContextCache cache{};
        cache.environment.context = {
            .excitement = scenery * 2 + height,
            .intensity = height,
            .nausea = height / 3,
            .scenery = scenery,
            .trackHeightExposure = height,
        };
        auto context = ScoreKartContext(cache, speed, profile, false);
        // Descriptor constants from Vehicle.cpp for the synthetic unbanked-turn/down cases.
        RideRating::TickScore features{ (800LL * turn + 2000LL * down) * speed / 90,
                                        (1600LL * turn + 1200LL * down) * speed / 90, 1600LL * turn * speed / 90 };
        RideRating::TickScore total{ speedScore.excitement + forces.excitement + context.excitement + features.excitement,
                                     speedScore.intensity + forces.intensity + context.intensity + features.intensity,
                                     speedScore.nausea + forces.nausea + context.nausea + features.nausea };
        total = ApplyKartComfort(total, vertical, lateral, longitudinal, profile);
        if (profile.Model == CalibrationModel::sustainedWithFatigue && tick > 2400)
        {
            // After one minute, each extra excitement tick contributes (60 seconds / elapsed)^2.
            // This bounds constant-stimulus enjoyment at two minutes' worth, without hiding discomfort.
            total.excitement = total.excitement * 2400 * 2400 / (static_cast<int64_t>(tick) * tick);
        }
        output << id;
        for (const auto& score : { speedScore, forces, context, features, total })
            output << ' ' << score.excitement << ' ' << score.intensity << ' ' << score.nausea;
        output << ' ' << history << '\n';
    }
    if (!input.eof() || !output)
        return 4;
    return 0;
}
