/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <numbers>
#include <numeric>
#include <memory>
#include <openrct2-ui/audio/AudioContext.h>
#include <openrct2-ui/audio/AudioFormat.h>
#include <openrct2-ui/audio/SDLAudioSource.h>
#include <openrct2/audio/SpatialAudio.h>
#include <openrct2/ride/RideAudio.h>

using namespace OpenRCT2::Audio;

namespace
{
    class TestAudioSource final : public SDLAudioSource
    {
    public:
        AudioFormat GetFormat() const override { return { 22050, AUDIO_S16SYS, 1 }; }
        uint64_t GetLength() const override { return _samples.size() * sizeof(int16_t); }

        size_t Read(void* destination, uint64_t offset, size_t length) override
        {
            const auto available = GetLength() - std::min(offset, GetLength());
            const auto readLength = std::min<uint64_t>(length, available);
            std::memcpy(destination, reinterpret_cast<const uint8_t*>(_samples.data()) + offset, readLength);
            return readLength;
        }

    protected:
        void Unload() override {}

    private:
        std::array<int16_t, 4> _samples{ 1, 2, 3, 4 };
    };
}

TEST(AudioChannel, NonLoopingSourceCompletionOwnsChannelLifetimeState)
{
    TestAudioSource source;
    std::unique_ptr<ISDLAudioChannel> channel(AudioChannel::Create());
    channel->Play(&source, kMixerLoopNone);

    std::array<int16_t, 4> output{};
    EXPECT_EQ(channel->Read(output.data(), sizeof(output)), sizeof(output));
    EXPECT_TRUE(channel->IsDone());
}

TEST(SpatialAudio, DistanceAttenuationIsContinuousAndLongRange)
{
    const SpatialAudioListener listener{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0.0f };
    const auto nearSource = CalculateSpatialAudioParams(listener, { 32, 0, 0 });
    const auto mediumSource = CalculateSpatialAudioParams(listener, { 16 * 32, 0, 0 });
    const auto distantSource = CalculateSpatialAudioParams(listener, { 64 * 32, 0, 0 });

    EXPECT_TRUE(nearSource.Audible);
    EXPECT_TRUE(mediumSource.Audible);
    EXPECT_TRUE(distantSource.Audible);
    EXPECT_FLOAT_EQ(nearSource.Gain, 1.0f);
    EXPECT_NEAR(mediumSource.Gain, std::pow(0.125f, kSpatialDistanceRolloff), 0.000001f);
    EXPECT_NEAR(distantSource.Gain, std::pow(0.03125f, kSpatialDistanceRolloff), 0.000001f);
    EXPECT_GT(nearSource.Gain, mediumSource.Gain);
    EXPECT_GT(mediumSource.Gain, distantSource.Gain);
    EXPECT_GT(distantSource.Gain, 0.0f);
}

TEST(SpatialAudio, DistanceRolloffIsMildlyCompressedButMonotonic)
{
    const SpatialAudioListener listener{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0.0f };
    const auto eightTiles = CalculateSpatialAudioParams(listener, { 8 * 32, 0, 0 });
    const auto sixteenTiles = CalculateSpatialAudioParams(listener, { 16 * 32, 0, 0 });

    EXPECT_GT(eightTiles.Gain, sixteenTiles.Gain);
    EXPECT_GT(sixteenTiles.Gain / eightTiles.Gain, 0.5f);
    EXPECT_NEAR(
        sixteenTiles.Gain / eightTiles.Gain, std::pow(0.5f, kSpatialDistanceRolloff), 0.000001f);
}

TEST(SpatialAudio, SourceClassesUseDistinctContinuousRolloffCurves)
{
    const SpatialAudioListener listener{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0.0f };
    const CoordsXYZ source{ 64 * 32, 0, 0 };

    const auto world = CalculateSpatialAudioParams(listener, source);
    const auto vehicle = CalculateSpatialAudioParams(listener, source, 1.0f, SpatialAudioRolloff::vehicle);
    const auto music = CalculateSpatialAudioParams(listener, source, 1.0f, SpatialAudioRolloff::rideMusic);

    EXPECT_LT(vehicle.Gain, world.Gain);
    EXPECT_LT(world.Gain, music.Gain);
    EXPECT_NEAR(vehicle.Gain, std::pow(0.03125f, kVehicleSpatialDistanceRolloff), 0.000001f);
    EXPECT_NEAR(music.Gain, std::pow(0.03125f, kRideMusicSpatialDistanceRolloff), 0.000001f);

    const CoordsXYZ referenceSource{ static_cast<int32_t>(kSpatialReferenceDistance), 0, 0 };
    EXPECT_FLOAT_EQ(
        CalculateSpatialAudioParams(listener, referenceSource, 1.0f, SpatialAudioRolloff::vehicle).Gain, 1.0f);
    EXPECT_FLOAT_EQ(
        CalculateSpatialAudioParams(listener, referenceSource, 1.0f, SpatialAudioRolloff::rideMusic).Gain, 1.0f);
}

TEST(SpatialAudio, DistanceCurveHasNoTileBoundary)
{
    const SpatialAudioListener listener{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0.0f };
    const auto veryDistant = CalculateSpatialAudioParams(listener, { 1024 * 32, 0, 0 });
    EXPECT_TRUE(veryDistant.Audible);
    EXPECT_GT(veryDistant.Gain, 0.0f);
}

TEST(SpatialAudio, ZoomedInListenerEmphasisesTheFocusedArea)
{
    const CoordsXYZ focus{ 4096, 4096, 64 };
    const auto zoomedIn = CalculateIsometricListener(focus, 0, 640, 480);
    const auto zoomedOut = CalculateIsometricListener(focus, 0, 2560, 1920);
    const CoordsXYZ horizontallyRemote{ focus.x + (32 * 32), focus.y, focus.z };

    const auto nearIn = CalculateSpatialAudioParams(zoomedIn, focus);
    const auto farIn = CalculateSpatialAudioParams(zoomedIn, horizontallyRemote);
    const auto nearOut = CalculateSpatialAudioParams(zoomedOut, focus);
    const auto farOut = CalculateSpatialAudioParams(zoomedOut, horizontallyRemote);

    EXPECT_LT(zoomedIn.Altitude, zoomedOut.Altitude);
    EXPECT_GT(nearIn.Gain, nearOut.Gain);
    EXPECT_GT(nearIn.Gain, farIn.Gain);
    EXPECT_GT(nearIn.Gain / farIn.Gain, nearOut.Gain / farOut.Gain);
}

TEST(SpatialAudio, ObjectAndCameraHeightContributeToDistance)
{
    const auto listener = CalculateIsometricListener({ 0, 0, 0 }, 0, 1280, 720);
    const auto ground = CalculateSpatialAudioParams(listener, { 0, 0, 0 });
    const auto elevated = CalculateSpatialAudioParams(
        listener, { 0, 0, static_cast<int32_t>(std::lround(listener.Altitude)) });

    EXPECT_GT(ground.Distance, elevated.Distance);
    EXPECT_GT(elevated.Gain, ground.Gain);
    EXPECT_LT(ground.Elevation, elevated.Elevation);
}

TEST(SpatialAudio, RainUsesContinuousCameraHeightAttenuation)
{
    auto listener = CalculateIsometricListener({ 0, 0, 0 }, 0, 1280, 720);
    const auto nearGroundAltitude = listener.NearGroundAltitude;

    listener.Altitude = nearGroundAltitude;
    const auto closest = CalculateRainHeightGain(listener);
    listener.Altitude = nearGroundAltitude * 2.0f;
    const auto secondClosest = CalculateRainHeightGain(listener);
    listener.Altitude = nearGroundAltitude * 4.0f;
    const auto distant = CalculateRainHeightGain(listener);
    listener.Altitude = nearGroundAltitude * 32.0f;
    const auto highest = CalculateRainHeightGain(listener);

    EXPECT_GT(closest, secondClosest);
    EXPECT_GT(secondClosest, 1.0f);
    EXPECT_GT(secondClosest, distant);
    EXPECT_GT(distant, highest);
    EXPECT_FLOAT_EQ(distant, 1.0f);
    EXPECT_NEAR(closest, std::pow(4.0f, kRainHeightRolloff), 0.000001f);
    EXPECT_NEAR(secondClosest, std::pow(2.0f, kRainHeightRolloff), 0.000001f);
    EXPECT_NEAR(highest, std::pow(0.125f, kRainHeightRolloff), 0.000001f);
}

TEST(SpatialAudio, RideMusicPriorityRemainsDistanceOrderedWhenPlaybackGainSaturates)
{
    OpenRCT2::RideAudio::ViewportRideMusicInstance nearby{};
    nearby.Volume = 0;
    nearby.PriorityGain = 0.9f;
    OpenRCT2::RideAudio::ViewportRideMusicInstance distant{};
    distant.Volume = 0;
    distant.PriorityGain = 0.2f;

    EXPECT_TRUE(OpenRCT2::RideAudio::IsMusicInstanceHigherPriority(nearby, distant));
    EXPECT_FALSE(OpenRCT2::RideAudio::IsMusicInstanceHigherPriority(distant, nearby));
}

TEST(SpatialAudio, OcclusionReducesGainWithoutChangingDirection)
{
    const SpatialAudioListener listener{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0.0f };
    const auto clear = CalculateSpatialAudioParams(listener, { 512, 256, 64 });
    const auto occluded = CalculateSpatialAudioParams(listener, { 512, 256, 64 }, 0.25f);

    EXPECT_FLOAT_EQ(occluded.Gain, clear.Gain * 0.25f);
    EXPECT_FLOAT_EQ(occluded.Azimuth, clear.Azimuth);
    EXPECT_FLOAT_EQ(occluded.Elevation, clear.Elevation);
}

TEST(SpatialAudio, ListenerRotationRotatesTheSoundField)
{
    const auto unrotated = CalculateSpatialAudioParams({ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0.0f }, { 0, -512, 0 });
    const auto rotated = CalculateSpatialAudioParams({ { 0, 0, 0 }, { 0, 0, 0 }, 1, 0.0f }, { 0, -512, 0 });

    EXPECT_NEAR(std::abs(rotated.Azimuth - unrotated.Azimuth), std::numbers::pi_v<float> / 2.0f, 0.0001f);
}

TEST(SpatialAudio, DopplerRaisesApproachingAndLowersRecedingPitch)
{
    EXPECT_GT(CalculateDopplerFactor(-kSpatialSpeedOfSound * 0.2f), 1.0f);
    EXPECT_FLOAT_EQ(CalculateDopplerFactor(0.0f), 1.0f);
    EXPECT_LT(CalculateDopplerFactor(kSpatialSpeedOfSound * 0.2f), 1.0f);
    EXPECT_FLOAT_EQ(CalculateDopplerFactor(-kSpatialSpeedOfSound), kMaxDopplerFactor);
    EXPECT_FLOAT_EQ(CalculateDopplerFactor(kSpatialSpeedOfSound), kMinDopplerFactor);
}

TEST(SpatialAudio, DopplerMotionIsSmoothedAndRejectsTeleports)
{
    DopplerMotionState state{};
    EXPECT_FLOAT_EQ(UpdateDopplerMotion(state, 1000.0f, 1.0f / 60.0f), 1.0f);
    const auto approaching = UpdateDopplerMotion(state, 980.0f, 1.0f / 60.0f);
    EXPECT_GT(approaching, 1.0f);
    EXPECT_LT(approaching, kMaxDopplerFactor);
    EXPECT_FLOAT_EQ(UpdateDopplerMotion(state, 100000.0f, 1.0f / 60.0f), 1.0f);
}

TEST(SpatialAudio, SurroundPanningUsesConstantPower)
{
    for (int32_t degrees = -180; degrees <= 180; degrees += 5)
    {
        const auto gains = CalculateSpeakerGains(degrees * std::numbers::pi_v<float> / 180.0f, 8);
        const auto power = std::accumulate(
            gains.begin(), gains.end(), 0.0f, [](float sum, float gain) { return sum + (gain * gain); });
        EXPECT_NEAR(power, 1.0f, 0.0001f);
        EXPECT_FLOAT_EQ(gains[3], 0.0f);
    }
}

TEST(SpatialAudio, SevenPointOneRoutesFrontSideAndRearToDistinctSpeakers)
{
    const auto front = CalculateSpeakerGains(0.0f, 8);
    const auto right = CalculateSpeakerGains(std::numbers::pi_v<float> / 2.0f, 8);
    const auto rear = CalculateSpeakerGains(std::numbers::pi_v<float>, 8);

    EXPECT_NEAR(front[0], std::numbers::sqrt2_v<float> / 2.0f, 0.0001f);
    EXPECT_NEAR(front[1], std::numbers::sqrt2_v<float> / 2.0f, 0.0001f);
    EXPECT_FLOAT_EQ(front[2], 0.0f);
    EXPECT_FLOAT_EQ(right[7], 1.0f);
    EXPECT_NEAR(rear[4], std::numbers::sqrt2_v<float> / 2.0f, 0.0001f);
    EXPECT_NEAR(rear[5], std::numbers::sqrt2_v<float> / 2.0f, 0.0001f);
}

TEST(SpatialAudio, GainConversionMatchesMixerAmplitudeConvention)
{
    EXPECT_EQ(SpatialGainToDSEnvelope(1.0f), 0);
    EXPECT_NEAR(SpatialGainToDSEnvelope(0.5f), -602, 1);
    EXPECT_EQ(SpatialGainToDSEnvelope(0.0f), -10000);
}

TEST(SpatialAudio, LimiterUsesImmediateAttackAndGradualRelease)
{
    const auto attacked = CalculateNextLimiterGain(1.0f, 1.9f, 1024, 48000);
    EXPECT_FLOAT_EQ(attacked, 0.5f);

    const auto released = CalculateNextLimiterGain(attacked, 0.1f, 1024, 48000);
    EXPECT_GT(released, attacked);
    EXPECT_LT(released, 1.0f);
}
