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
#include <chrono>
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
        constexpr float kListenerPositionSmoothingSeconds = 0.08f;

        struct ListenerMotionState
        {
            const Viewport* ViewportIdentity{};
            SpatialAudioListener Listener{};
            std::chrono::steady_clock::time_point LastUpdate{};
            bool Initialised{};
        };

        ListenerMotionState _listenerMotion{};

        constexpr SpatialAudioVector operator+(const SpatialAudioVector& lhs, const SpatialAudioVector& rhs)
        {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        constexpr SpatialAudioVector operator-(const SpatialAudioVector& lhs, const SpatialAudioVector& rhs)
        {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
        }

        constexpr SpatialAudioVector operator*(const SpatialAudioVector& value, float scale)
        {
            return { value.x * scale, value.y * scale, value.z * scale };
        }

        constexpr float Dot(const SpatialAudioVector& lhs, const SpatialAudioVector& rhs)
        {
            return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
        }

        constexpr SpatialAudioVector Cross(const SpatialAudioVector& lhs, const SpatialAudioVector& rhs)
        {
            return {
                (lhs.y * rhs.z) - (lhs.z * rhs.y),
                (lhs.z * rhs.x) - (lhs.x * rhs.z),
                (lhs.x * rhs.y) - (lhs.y * rhs.x),
            };
        }

        float Length(const SpatialAudioVector& value)
        {
            return std::sqrt(Dot(value, value));
        }

        SpatialAudioVector Normalise(const SpatialAudioVector& value)
        {
            const auto length = Length(value);
            return length > 0.0f ? value * (1.0f / length) : SpatialAudioVector{};
        }

        SpatialAudioVector ToVector(const CoordsXYZ& value)
        {
            return { static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z) };
        }

        SpatialAudioVector Lerp(const SpatialAudioVector& lhs, const SpatialAudioVector& rhs, float amount)
        {
            return {
                std::lerp(lhs.x, rhs.x, amount),
                std::lerp(lhs.y, rhs.y, amount),
                std::lerp(lhs.z, rhs.z, amount),
            };
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
        auto targetListener = CalculateIsometricListener(
            focus, listeningViewport->rotation, listeningViewport->ViewWidth(), listeningViewport->ViewHeight());
        const auto baseViewWidth = listeningViewport->zoom.ApplyInversedTo(listeningViewport->ViewWidth());
        const auto baseViewHeight = listeningViewport->zoom.ApplyInversedTo(listeningViewport->ViewHeight());
        const auto nearGroundListener = CalculateIsometricListener(
            focus, listeningViewport->rotation, ZoomLevel::min().ApplyTo(baseViewWidth),
            ZoomLevel::min().ApplyTo(baseViewHeight));
        targetListener.NearGroundAltitude = nearGroundListener.Altitude;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = _listenerMotion.Initialised
            ? std::chrono::duration<float>(now - _listenerMotion.LastUpdate).count()
            : 0.0f;
        const auto displacement = _listenerMotion.Initialised
            ? Length(targetListener.Position - _listenerMotion.Listener.Position)
            : 0.0f;
        const auto discontinuous = !_listenerMotion.Initialised || _listenerMotion.ViewportIdentity != listeningViewport
            || _listenerMotion.Listener.Rotation != targetListener.Rotation || elapsed <= 0.0f
            || elapsed > kMaxDopplerSampleSeconds || displacement > kMaxDopplerDistanceStep;

        if (discontinuous)
        {
            targetListener.Velocity = {};
            targetListener.Discontinuous = true;
            _listenerMotion.Listener = targetListener;
        }
        else if (elapsed >= 0.004f)
        {
            const auto previousPosition = _listenerMotion.Listener.Position;
            const auto positionBlend = 1.0f - std::exp(-elapsed / kListenerPositionSmoothingSeconds);
            targetListener.Position = Lerp(previousPosition, targetListener.Position, positionBlend);
            const auto rawVelocity = (targetListener.Position - previousPosition) * (1.0f / elapsed);
            const auto velocityBlend = 1.0f - std::exp(-elapsed / kDopplerVelocitySmoothingSeconds);
            targetListener.Velocity = Lerp(_listenerMotion.Listener.Velocity, rawVelocity, velocityBlend);
            targetListener.Discontinuous = false;
            _listenerMotion.Listener = targetListener;
        }
        else
        {
            return _listenerMotion.Listener;
        }

        _listenerMotion.ViewportIdentity = listeningViewport;
        _listenerMotion.LastUpdate = now;
        _listenerMotion.Initialised = true;
        return _listenerMotion.Listener;
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
        const auto cameraDistance = visibleRadius / std::tan(kVirtualAcousticHalfFov);

        // Translate3DTo2DWithZ has a (1, 1, 1) null direction before view rotation. Positioning the
        // listener on that ray makes the rendered centre ray and the acoustic forward ray agree.
        const auto cameraGroundOffset = CoordsXY{ 1, 1 }.Rotate((4 - rotation) & 3);
        const auto cameraOffset = Normalise(SpatialAudioVector{
            static_cast<float>(cameraGroundOffset.x), static_cast<float>(cameraGroundOffset.y), 1.0f });
        const auto forward = cameraOffset * -1.0f;
        const auto screenRightGround = CoordsXY{ -1, 1 }.Rotate((4 - rotation) & 3);
        const auto right = Normalise(SpatialAudioVector{
            static_cast<float>(screenRightGround.x), static_cast<float>(screenRightGround.y), 0.0f });
        const auto up = Normalise(Cross(right, forward));
        const auto focusVector = ToVector(focus);
        const auto position = focusVector + (cameraOffset * cameraDistance);
        const auto altitude = position.z - static_cast<float>(focus.z);

        SpatialAudioListener result{};
        result.Position = position;
        result.FocusPosition = focus;
        result.Rotation = rotation;
        result.Altitude = altitude;
        result.NearGroundAltitude = altitude;
        result.Forward = forward;
        result.Right = right;
        result.Up = up;
        return result;
    }

    SpatialAudioParams CalculateSpatialAudioParams(
        const SpatialAudioListener& listener, const CoordsXYZ& source, float occlusion, SpatialAudioRolloff rolloff)
    {
        const auto relative = ToVector(source) - listener.Position;
        const auto distance = Length(relative);

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
        result.LowPassCutoff = CalculateDistanceLowPassCutoff(distance, occlusion, rolloff);

        const auto localRight = Dot(relative, listener.Right);
        const auto localForward = Dot(relative, listener.Forward);
        const auto localUp = Dot(relative, listener.Up);
        const auto forwardPlaneDistance = std::sqrt((localRight * localRight) + (localForward * localForward));
        result.Azimuth = forwardPlaneDistance > 0.0f ? std::atan2(localRight, localForward) : 0.0f;
        result.Elevation = std::atan2(localUp, forwardPlaneDistance);
        return result;
    }

    float CalculateDopplerFactor(float sourceRadialVelocity, float listenerRadialVelocity, float cameraStrength)
    {
        const auto numerator = std::max(
            kSpatialSpeedOfSound * 0.1f,
            kSpatialSpeedOfSound + (std::clamp(cameraStrength, 0.0f, 1.0f) * listenerRadialVelocity));
        const auto denominator = std::max(kSpatialSpeedOfSound * 0.1f, kSpatialSpeedOfSound + sourceRadialVelocity);
        return std::clamp(numerator / denominator, kMinDopplerFactor, kMaxDopplerFactor);
    }

    float UpdateDopplerMotion(
        DopplerMotionState& state, const SpatialAudioListener& listener, const CoordsXYZ& sourcePosition,
        float elapsedSeconds, bool sourceMoves, bool sourceDiscontinuity)
    {
        const auto source = ToVector(sourcePosition);
        const auto sourceStep = state.Initialised ? Length(source - state.PreviousSourcePosition) : 0.0f;
        if (!state.Initialised || sourceDiscontinuity || listener.Discontinuous || elapsedSeconds <= 0.0f
            || elapsedSeconds > kMaxDopplerSampleSeconds || (sourceMoves && sourceStep > kMaxDopplerDistanceStep))
        {
            state.PreviousSourcePosition = source;
            state.SmoothedSourceRadialVelocity = 0.0f;
            state.SmoothedListenerRadialVelocity = 0.0f;
            state.Factor = 1.0f;
            state.Initialised = true;
            return state.Factor;
        }

        const auto ray = Normalise(source - listener.Position);
        const auto sourceVelocity = sourceMoves
            ? (source - state.PreviousSourcePosition) * (1.0f / elapsedSeconds)
            : SpatialAudioVector{};
        const auto sourceRadialVelocity = Dot(sourceVelocity, ray);
        const auto listenerRadialVelocity = Dot(listener.Velocity, ray);
        const auto velocityBlend = 1.0f - std::exp(-elapsedSeconds / kDopplerVelocitySmoothingSeconds);
        state.SmoothedSourceRadialVelocity = std::lerp(
            state.SmoothedSourceRadialVelocity, sourceRadialVelocity, velocityBlend);
        state.SmoothedListenerRadialVelocity = std::lerp(
            state.SmoothedListenerRadialVelocity, listenerRadialVelocity, velocityBlend);

        const auto targetFactor = CalculateDopplerFactor(
            state.SmoothedSourceRadialVelocity, state.SmoothedListenerRadialVelocity);
        const auto factorBlend = 1.0f - std::exp(-elapsedSeconds / kDopplerFactorSmoothingSeconds);
        state.Factor = std::lerp(state.Factor, targetFactor, factorBlend);
        state.PreviousSourcePosition = source;
        return state.Factor;
    }

    float CalculateDistanceLowPassCutoff(float distance, float occlusion, SpatialAudioRolloff rolloff)
    {
        const auto clampedOcclusion = std::clamp(occlusion, 0.0f, 1.0f);
        const auto distanceRatio = std::max(1.0f, distance / kSpatialReferenceDistance);
        if (distanceRatio <= 1.0f && clampedOcclusion >= 1.0f)
        {
            return kSpatialFilterBypassCutoff;
        }

        auto distanceStrength = 1.0f;
        switch (rolloff)
        {
            case SpatialAudioRolloff::vehicle:
                distanceStrength = 1.1f;
                break;
            case SpatialAudioRolloff::rideMusic:
                distanceStrength = 0.55f;
                break;
            case SpatialAudioRolloff::world:
                break;
        }
        const auto distanceOctaves = std::log2(distanceRatio);
        const auto airCutoff = 20000.0f * std::pow(0.78f, distanceOctaves * distanceStrength);
        const auto occlusionMultiplier = 0.2f + (0.8f * clampedOcclusion);
        return std::clamp(airCutoff * occlusionMultiplier, 1200.0f, 20000.0f);
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
        struct Speaker
        {
            float azimuth;
            uint8_t channel;
        };
        const auto panBetweenSpeakers = [azimuth]<size_t N>(const std::array<Speaker, N>& speakers) {
            std::array<float, kMaxOutputChannels> gains{};
            auto sourceAngle = std::fmod(azimuth, kTwoPi);
            if (sourceAngle < 0.0f)
                sourceAngle += kTwoPi;

            for (size_t i = 0; i < speakers.size(); i++)
            {
                const auto& left = speakers[i];
                const auto& right = speakers[(i + 1) % speakers.size()];
                auto rightAngle = right.azimuth;
                auto adjustedSource = sourceAngle;
                if (i + 1 == speakers.size())
                {
                    rightAngle += kTwoPi;
                    if (adjustedSource < left.azimuth)
                        adjustedSource += kTwoPi;
                }
                if (adjustedSource < left.azimuth || adjustedSource > rightAngle)
                    continue;

                const auto blend = (adjustedSource - left.azimuth) / (rightAngle - left.azimuth);
                gains[left.channel] = std::cos(blend * pi_v<float> / 2.0f);
                gains[right.channel] = std::sin(blend * pi_v<float> / 2.0f);
                break;
            }
            return gains;
        };

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
                return panBetweenSpeakers(
                    std::array{ Speaker{ pi_v<float> / 4.0f, 1 }, Speaker{ 3.0f * pi_v<float> / 4.0f, 3 },
                                Speaker{ 5.0f * pi_v<float> / 4.0f, 2 }, Speaker{ 7.0f * pi_v<float> / 4.0f, 0 } });
            case 5:
                return panBetweenSpeakers(
                    std::array{ Speaker{ pi_v<float> / 4.0f, 1 }, Speaker{ 3.0f * pi_v<float> / 4.0f, 4 },
                                Speaker{ 5.0f * pi_v<float> / 4.0f, 3 }, Speaker{ 7.0f * pi_v<float> / 4.0f, 0 } });
            case 6:
                return panBetweenSpeakers(
                    std::array{ Speaker{ 0.0f, 2 }, Speaker{ pi_v<float> / 6.0f, 1 }, Speaker{ 11.0f * pi_v<float> / 18.0f, 5 },
                                Speaker{ 25.0f * pi_v<float> / 18.0f, 4 }, Speaker{ 11.0f * pi_v<float> / 6.0f, 0 } });
            case 7:
                return panBetweenSpeakers(
                    std::array{ Speaker{ 0.0f, 2 }, Speaker{ pi_v<float> / 6.0f, 1 }, Speaker{ pi_v<float> / 2.0f, 6 },
                                Speaker{ pi_v<float>, 4 }, Speaker{ 3.0f * pi_v<float> / 2.0f, 5 },
                                Speaker{ 11.0f * pi_v<float> / 6.0f, 0 } });
            default:
                // SDL 7.1 order: FL, FR, FC, LFE, BL, BR, SL, SR. LFE remains available for device bass
                // management; full-range positional audio is not routed to it without a low-pass filter. World
                // effects use a phantom front centre across FL/FR rather than relying on a virtual-headset centre
                // channel, while still retaining distinct side and rear positions.
                return panBetweenSpeakers(
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
