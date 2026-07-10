/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SpatialAudio.h"

#include "../interface/Viewport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace OpenRCT2::Audio
{
    namespace
    {
        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
        constexpr float kDopplerVelocitySmoothingSeconds = 0.12f;
        constexpr float kDopplerFactorSmoothingSeconds = 0.06f;
        constexpr float kMaxDopplerSampleSeconds = 0.5f;
        constexpr float kMaxDopplerDistanceStep = kSpatialSpeedOfSound * kMaxDopplerSampleSeconds;

        struct Speaker
        {
            float Azimuth;
            uint8_t Channel;
        };

        float WrapAngle(float angle)
        {
            angle = std::fmod(angle, kTwoPi);
            return angle < 0.0f ? angle + kTwoPi : angle;
        }

        template<size_t N>
        std::array<float, kMaxOutputChannels> PanBetweenSpeakers(float azimuth, const std::array<Speaker, N>& speakers)
        {
            std::array<float, kMaxOutputChannels> gains{};
            const auto wrappedAzimuth = WrapAngle(azimuth);

            for (size_t i = 0; i < speakers.size(); i++)
            {
                const auto& left = speakers[i];
                const auto& right = speakers[(i + 1) % speakers.size()];
                const auto leftAngle = left.Azimuth;
                auto rightAngle = right.Azimuth;
                auto sourceAngle = wrappedAzimuth;
                if (i + 1 == speakers.size())
                {
                    rightAngle += kTwoPi;
                    if (sourceAngle < leftAngle)
                    {
                        sourceAngle += kTwoPi;
                    }
                }

                if (sourceAngle >= leftAngle && sourceAngle <= rightAngle)
                {
                    const auto arc = rightAngle - leftAngle;
                    const auto blend = arc > 0.0f ? (sourceAngle - leftAngle) / arc : 0.0f;
                    gains[left.Channel] = std::cos(blend * std::numbers::pi_v<float> / 2.0f);
                    gains[right.Channel] = std::sin(blend * std::numbers::pi_v<float> / 2.0f);
                    break;
                }
            }
            return gains;
        }
    } // namespace

    std::optional<SpatialAudioListener> GetSpatialAudioListener()
    {
        const Viewport* listeningViewport = gMusicTrackingViewport;
        if (listeningViewport == nullptr)
        {
            for (const auto& viewport : GetAllViewports())
            {
                if (viewport.flags & VIEWPORT_FLAG_SOUND_ON)
                {
                    listeningViewport = &viewport;
                    break;
                }
            }
        }

        if (listeningViewport == nullptr)
        {
            return std::nullopt;
        }

        const auto viewCentre = listeningViewport->viewPos
            + ScreenCoordsXY{ listeningViewport->ViewWidth() / 2, listeningViewport->ViewHeight() / 2 };
        const auto focus = ViewportAdjustForMapHeight(viewCentre, listeningViewport->rotation);
        auto listener = CalculateIsometricListener(
            focus, listeningViewport->rotation, listeningViewport->ViewWidth(), listeningViewport->ViewHeight());
        const auto baseViewWidth = listeningViewport->zoom.ApplyInversedTo(listeningViewport->ViewWidth());
        const auto baseViewHeight = listeningViewport->zoom.ApplyInversedTo(listeningViewport->ViewHeight());
        const auto nearGroundListener = CalculateIsometricListener(
            focus, listeningViewport->rotation, ZoomLevel::min().ApplyTo(baseViewWidth),
            ZoomLevel::min().ApplyTo(baseViewHeight));
        listener.NearGroundAltitude = nearGroundListener.Altitude;
        return listener;
    }

    SpatialAudioListener CalculateIsometricListener(
        const CoordsXYZ& focus, uint8_t rotation, int32_t projectedViewWidth, int32_t projectedViewHeight)
    {
        // On level ground the projection is x' = y - x and y' = (x + y) / 2. These
        // spans therefore estimate the sideways and forward radii of the visible ground plane.
        // Orthographic projection has no literal camera distance. Treat its visible ground radius
        // as a very-wide (150 degree) virtual acoustic frustum to obtain a smooth equivalent height.
        constexpr float kVirtualAcousticHalfFov = 75.0f * std::numbers::pi_v<float> / 180.0f;
        const auto sidewaysRadius = std::abs(static_cast<float>(projectedViewWidth))
            / (2.0f * std::numbers::sqrt2_v<float>);
        const auto forwardRadius = std::abs(static_cast<float>(projectedViewHeight))
            / std::numbers::sqrt2_v<float>;
        const auto visibleRadius = std::min(sidewaysRadius, forwardRadius);
        const auto altitude = visibleRadius / std::tan(kVirtualAcousticHalfFov);
        const auto offset = static_cast<int32_t>(std::lround(altitude));

        // Keep the acoustic x/y position on the area being viewed. The real renderer is orthographic,
        // so there is no unique perspective-camera distance; the visible ground radius provides the
        // stable height estimate while the focus remains the intuitive closest horizontal point.
        return SpatialAudioListener{ { focus.x, focus.y, focus.z + offset }, focus, rotation, altitude, altitude };
    }

    SpatialAudioParams CalculateSpatialAudioParams(
        const SpatialAudioListener& listener, const CoordsXYZ& source, float occlusion, SpatialAudioRolloff rolloff)
    {
        const auto dx = static_cast<float>(source.x - listener.Position.x);
        const auto dy = static_cast<float>(source.y - listener.Position.y);
        const auto dz = static_cast<float>(source.z - listener.Position.z);
        const auto distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));

        SpatialAudioParams result{};
        result.Distance = distance;
        result.Audible = occlusion > 0.0f;

        const auto propagationDistance = std::max(distance, kSpatialReferenceDistance);
        auto rolloffExponent = kSpatialDistanceRolloff;
        switch (rolloff)
        {
            case SpatialAudioRolloff::vehicle:
                rolloffExponent = kVehicleSpatialDistanceRolloff;
                break;
            case SpatialAudioRolloff::rideMusic:
                rolloffExponent = kRideMusicSpatialDistanceRolloff;
                break;
            case SpatialAudioRolloff::world:
                break;
        }
        const auto distanceGain = std::pow(kSpatialReferenceDistance / propagationDistance, rolloffExponent);
        result.Gain = std::clamp(occlusion, 0.0f, 1.0f) * distanceGain;

        const auto rotated = CoordsXY{ source.x - listener.Position.x, source.y - listener.Position.y }.Rotate(
            listener.Rotation);
        constexpr auto kDiagonalScale = std::numbers::sqrt2_v<float> / 2.0f;
        const auto right = static_cast<float>(rotated.y - rotated.x) * kDiagonalScale;
        const auto forward = -static_cast<float>(rotated.x + rotated.y) * kDiagonalScale;
        const auto horizontalDistance = std::sqrt((right * right) + (forward * forward));
        result.Azimuth = horizontalDistance > 0.0f ? std::atan2(right, forward) : 0.0f;
        result.Elevation = std::atan2(dz, horizontalDistance);
        return result;
    }

    float CalculateDopplerFactor(float relativeRadialVelocity)
    {
        // Positive range rate means the emitter and listener are separating. Sampling the relative
        // range makes this work for a moving source, a moving listener, or both.
        const auto denominator = std::max(kSpatialSpeedOfSound * 0.1f, kSpatialSpeedOfSound + relativeRadialVelocity);
        return std::clamp(kSpatialSpeedOfSound / denominator, kMinDopplerFactor, kMaxDopplerFactor);
    }

    float UpdateDopplerMotion(DopplerMotionState& state, float distance, float elapsedSeconds)
    {
        if (!state.Initialised || elapsedSeconds <= 0.0f || elapsedSeconds > kMaxDopplerSampleSeconds
            || std::abs(distance - state.PreviousDistance) > kMaxDopplerDistanceStep)
        {
            state.PreviousDistance = distance;
            state.SmoothedRadialVelocity = 0.0f;
            state.Factor = 1.0f;
            state.Initialised = true;
            return state.Factor;
        }

        const auto radialVelocity = (distance - state.PreviousDistance) / elapsedSeconds;
        const auto velocityBlend = 1.0f - std::exp(-elapsedSeconds / kDopplerVelocitySmoothingSeconds);
        state.SmoothedRadialVelocity = std::lerp(state.SmoothedRadialVelocity, radialVelocity, velocityBlend);

        const auto targetFactor = CalculateDopplerFactor(state.SmoothedRadialVelocity);
        const auto factorBlend = 1.0f - std::exp(-elapsedSeconds / kDopplerFactorSmoothingSeconds);
        state.Factor = std::lerp(state.Factor, targetFactor, factorBlend);
        state.PreviousDistance = distance;
        return state.Factor;
    }

    float CalculateRainHeightGain(const SpatialAudioListener& listener)
    {
        // Rain is an area ambience rather than a point emitter. Anchor its previous loudness one
        // camera-height step above the second-closest view, then apply one continuous height curve
        // in both directions. Zoom -1 therefore receives the former closest-view gain while zoom -2
        // rises by one additional step, without keeping rain foregrounded high above the park.
        const auto nearGroundAltitude = std::max(listener.NearGroundAltitude, 1.0f);
        const auto referenceAltitude = nearGroundAltitude * 4.0f;
        const auto listenerAltitude = std::max(listener.Altitude, 1.0f);
        return std::clamp(std::pow(referenceAltitude / listenerAltitude, kRainHeightRolloff), 0.1f, 3.0f);
    }

    int32_t SpatialGainToDSEnvelope(float gain)
    {
        if (gain <= 0.0f)
        {
            return -10000;
        }
        return static_cast<int32_t>(std::lround(2000.0f * std::log10(std::min(gain, 1.0f))));
    }

    std::array<float, kMaxOutputChannels> CalculateSpeakerGains(float azimuth, uint8_t channelCount)
    {
        using namespace std::numbers;
        switch (channelCount)
        {
            case 1:
            {
                std::array<float, kMaxOutputChannels> gains{};
                gains[0] = 1.0f;
                return gains;
            }
            case 2:
            case 3:
            {
                const auto pan = (std::sin(azimuth) + 1.0f) / 2.0f;
                std::array<float, kMaxOutputChannels> gains{};
                gains[0] = std::cos(pan * pi_v<float> / 2.0f);
                gains[1] = std::sin(pan * pi_v<float> / 2.0f);
                return gains;
            }
            case 4:
                return PanBetweenSpeakers(
                    azimuth,
                    std::array{ Speaker{ pi_v<float> / 4.0f, 1 }, Speaker{ 3.0f * pi_v<float> / 4.0f, 3 },
                                Speaker{ 5.0f * pi_v<float> / 4.0f, 2 }, Speaker{ 7.0f * pi_v<float> / 4.0f, 0 } });
            case 5:
                return PanBetweenSpeakers(
                    azimuth,
                    std::array{ Speaker{ pi_v<float> / 4.0f, 1 }, Speaker{ 3.0f * pi_v<float> / 4.0f, 4 },
                                Speaker{ 5.0f * pi_v<float> / 4.0f, 3 }, Speaker{ 7.0f * pi_v<float> / 4.0f, 0 } });
            case 6:
                return PanBetweenSpeakers(
                    azimuth,
                    std::array{ Speaker{ 0.0f, 2 }, Speaker{ pi_v<float> / 6.0f, 1 }, Speaker{ 11.0f * pi_v<float> / 18.0f, 5 },
                                Speaker{ 25.0f * pi_v<float> / 18.0f, 4 }, Speaker{ 11.0f * pi_v<float> / 6.0f, 0 } });
            case 7:
                return PanBetweenSpeakers(
                    azimuth,
                    std::array{ Speaker{ 0.0f, 2 }, Speaker{ pi_v<float> / 6.0f, 1 }, Speaker{ pi_v<float> / 2.0f, 6 },
                                Speaker{ pi_v<float>, 4 }, Speaker{ 3.0f * pi_v<float> / 2.0f, 5 },
                                Speaker{ 11.0f * pi_v<float> / 6.0f, 0 } });
            default:
                // SDL 7.1 order: FL, FR, FC, LFE, BL, BR, SL, SR. LFE remains available for device bass
                // management; full-range positional audio is not routed to it without a low-pass filter. World
                // effects use a phantom front centre across FL/FR rather than relying on a virtual-headset centre
                // channel, while still retaining distinct side and rear positions.
                return PanBetweenSpeakers(
                    azimuth,
                    std::array{ Speaker{ pi_v<float> / 6.0f, 1 }, Speaker{ pi_v<float> / 2.0f, 7 },
                                Speaker{ 5.0f * pi_v<float> / 6.0f, 5 }, Speaker{ 7.0f * pi_v<float> / 6.0f, 4 },
                                Speaker{ 3.0f * pi_v<float> / 2.0f, 6 }, Speaker{ 11.0f * pi_v<float> / 6.0f, 0 } });
        }
    }

    float CalculateNextLimiterGain(float currentGain, float peak, size_t frames, uint32_t sampleRate)
    {
        constexpr float kLimiterCeiling = 0.95f;
        constexpr float kLimiterReleaseSeconds = 0.75f;
        const auto targetGain = peak > kLimiterCeiling ? kLimiterCeiling / peak : 1.0f;
        if (targetGain < currentGain)
        {
            return targetGain;
        }
        if (sampleRate == 0)
        {
            return currentGain;
        }
        const auto release = 1.0f
            - std::exp(-static_cast<float>(frames) / (static_cast<float>(sampleRate) * kLimiterReleaseSeconds));
        return std::lerp(currentGain, targetGain, release);
    }
} // namespace OpenRCT2::Audio
