/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../world/Location.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace OpenRCT2::Audio
{
    constexpr size_t kMaxOutputChannels = 8;

    // Sprite coordinates use 32 horizontal units per tile. Beyond the reference distance, sound
    // pressure follows a mildly compressed inverse-distance law. The exponent keeps the spatial
    // ordering of sources while retaining more useful park ambience at long range.
    constexpr float kSpatialReferenceDistance = 2.0f * kCoordsXYStep;
    constexpr float kSpatialDistanceRolloff = 0.8f;
    constexpr float kVehicleSpatialDistanceRolloff = 1.1f;
    constexpr float kRideMusicSpatialDistanceRolloff = 0.6f;
    constexpr float kRainHeightRolloff = 0.75f;
    constexpr float kSpatialSpeedOfSound = 96.0f * kCoordsXYStep;
    constexpr float kMinDopplerFactor = 0.75f;
    constexpr float kMaxDopplerFactor = 4.0f / 3.0f;

    struct SpatialAudioListener
    {
        CoordsXYZ Position{};
        CoordsXYZ FocusPosition{};
        uint8_t Rotation{};
        float Altitude{};
        float NearGroundAltitude{};
    };

    struct SpatialAudioParams
    {
        bool Audible{};
        float Distance{};
        float Gain{};
        float Azimuth{};
        float Elevation{};
    };

    enum class SpatialAudioRolloff : uint8_t
    {
        world,
        vehicle,
        rideMusic,
    };

    struct DopplerMotionState
    {
        float PreviousDistance{};
        float SmoothedRadialVelocity{};
        float Factor{ 1.0f };
        bool Initialised{};
    };

    std::optional<SpatialAudioListener> GetSpatialAudioListener();
    SpatialAudioListener CalculateIsometricListener(
        const CoordsXYZ& focus, uint8_t rotation, int32_t projectedViewWidth, int32_t projectedViewHeight);
    SpatialAudioParams CalculateSpatialAudioParams(
        const SpatialAudioListener& listener, const CoordsXYZ& source, float occlusion = 1.0f,
        SpatialAudioRolloff rolloff = SpatialAudioRolloff::world);
    float CalculateDopplerFactor(float relativeRadialVelocity);
    // Stateful smoothing assumes regular mixer samples; long gaps and discontinuous jumps deliberately reset to unity.
    float UpdateDopplerMotion(DopplerMotionState& state, float distance, float elapsedSeconds);
    float CalculateRainHeightGain(const SpatialAudioListener& listener);
    int32_t SpatialGainToDSEnvelope(float gain);
    // Produces equal-power gains in SDL's native channel order. Layouts without a usable centre or LFE leave those channels
    // silent rather than feeding full-range world audio into speaker roles that require device-side filtering.
    std::array<float, kMaxOutputChannels> CalculateSpeakerGains(float azimuth, uint8_t channelCount);
    // Limiter attack is immediate to prevent clipping; release is frame-count/sample-rate based and therefore buffer-size neutral.
    float CalculateNextLimiterGain(float currentGain, float peak, size_t frames, uint32_t sampleRate);
} // namespace OpenRCT2::Audio
