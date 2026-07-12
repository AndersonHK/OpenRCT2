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
    constexpr float kCameraDopplerStrength = 0.05f;
    constexpr float kSpatialFilterBypassCutoff = 24000.0f;

    struct SpatialAudioVector
    {
        float x{};
        float y{};
        float z{};
    };

    struct SpatialAudioListener
    {
        SpatialAudioVector Position{};
        CoordsXYZ FocusPosition{};
        uint8_t Rotation{};
        float Altitude{};
        float NearGroundAltitude{};
        SpatialAudioVector Forward{ -0.577350269f, -0.577350269f, -0.577350269f };
        SpatialAudioVector Right{ -0.707106781f, 0.707106781f, 0.0f };
        SpatialAudioVector Up{ -0.408248290f, -0.408248290f, 0.816496581f };
        SpatialAudioVector Velocity{};
        bool Discontinuous{};
    };

    struct SpatialAudioParams
    {
        bool Audible{};
        float Distance{};
        float Gain{};
        float Azimuth{};
        float Elevation{};
        float LowPassCutoff{ kSpatialFilterBypassCutoff };
    };

    enum class SpatialAudioRolloff : uint8_t
    {
        world,
        vehicle,
        rideMusic,
    };

    struct DopplerMotionState
    {
        SpatialAudioVector PreviousSourcePosition{};
        float SmoothedSourceRadialVelocity{};
        float SmoothedListenerRadialVelocity{};
        float Factor{ 1.0f };
        bool Initialised{};
    };

    std::optional<SpatialAudioListener> GetSpatialAudioListener();
    SpatialAudioListener CalculateIsometricListener(
        const CoordsXYZ& focus, uint8_t rotation, int32_t projectedViewWidth, int32_t projectedViewHeight);
    SpatialAudioParams CalculateSpatialAudioParams(
        const SpatialAudioListener& listener, const CoordsXYZ& source, float occlusion = 1.0f,
        SpatialAudioRolloff rolloff = SpatialAudioRolloff::world);
    float CalculateDopplerFactor(
        float sourceRadialVelocity, float listenerRadialVelocity,
        float cameraStrength = kCameraDopplerStrength);
    // Stateful smoothing assumes regular control-rate samples. Long gaps, camera discontinuities, and emitter-anchor changes
    // deliberately reset to unity rather than synthesising a pitch impulse.
    float UpdateDopplerMotion(
        DopplerMotionState& state, const SpatialAudioListener& listener, const CoordsXYZ& sourcePosition,
        float elapsedSeconds, bool sourceMoves, bool sourceDiscontinuity = false);
    float CalculateDistanceLowPassCutoff(
        float distance, float occlusion, SpatialAudioRolloff rolloff = SpatialAudioRolloff::world);
    float CalculateRainHeightGain(const SpatialAudioListener& listener);
    int32_t SpatialGainToDSEnvelope(float gain);
    // Produces equal-power gains in SDL's native channel order. Layouts without a usable centre or LFE leave those channels
    // silent rather than feeding full-range world audio into speaker roles that require device-side filtering.
    std::array<float, kMaxOutputChannels> CalculateSpeakerGains(float azimuth, uint8_t channelCount);
    // Limiter attack is immediate to prevent clipping; release is frame-count/sample-rate based and therefore buffer-size neutral.
    float CalculateNextLimiterGain(float currentGain, float peak, size_t frames, uint32_t sampleRate);
} // namespace OpenRCT2::Audio
