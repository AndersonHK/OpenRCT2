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
#include <memory>
#include <numbers>
#include <numeric>
#ifndef OPENRCT2_TEST_NO_UI_AUDIO
    #include <openrct2-ui/audio/AudioContext.h>
    #include <openrct2-ui/audio/AudioFormat.h>
    #include <openrct2-ui/audio/AudioMixer.h>
    #include <openrct2-ui/audio/SDLAudioSource.h>
#endif
#include <openrct2/Context.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/audio/Audio.h>
#include <openrct2/audio/SpatialAudio.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/RideObject.h>
#include <openrct2/ride/CarEntry.h>
#include <openrct2/ride/RideAudio.h>

using namespace OpenRCT2::Audio;

#ifndef OPENRCT2_TEST_NO_UI_AUDIO
namespace
{
    class TestAudioSource final : public SDLAudioSource
    {
    public:
        AudioFormat GetFormat() const override
        {
            return { 22050, AUDIO_S16SYS, 1 };
        }
        uint64_t GetLength() const override
        {
            return _samples.size() * sizeof(int16_t);
        }

        size_t Read(void* destination, uint64_t offset, size_t length) override
        {
            const auto available = GetLength() - std::min(offset, GetLength());
            const auto readLength = std::min<uint64_t>(length, available);
            std::memcpy(destination, reinterpret_cast<const uint8_t*>(_samples.data()) + offset, readLength);
            return readLength;
        }

    protected:
        void Unload() override
        {
        }

    private:
        std::array<int16_t, 4> _samples{ 1, 2, 3, 4 };
    };
} // namespace

TEST(AudioChannel, NonLoopingSourceCompletionOwnsChannelLifetimeState)
{
    TestAudioSource source;
    std::unique_ptr<ISDLAudioChannel> channel(AudioChannel::Create());
    channel->Play(&source, kMixerLoopNone);

    std::array<int16_t, 4> output{};
    EXPECT_EQ(channel->Read(output.data(), sizeof(output)), sizeof(output));
    EXPECT_TRUE(channel->IsDone());
}

TEST(AudioMixer, RepeatedSampleVoicesRetainIndependentPlayback)
{
    TestAudioSource source;
    AudioMixer mixer;
    std::vector<std::shared_ptr<IAudioChannel>> voices;
    // A busy park reuses the same engine/rumble sample across hundreds of trains.
    // Admission must not deduplicate that source or restore a small legacy voice limit.
    for (size_t i = 0; i < 1024; i++)
    {
        auto voice = mixer.Play(&source, kMixerLoopInfinite);
        ASSERT_NE(voice, nullptr) << "Repeated sample rejected at voice " << i;
        EXPECT_EQ(voice->GetSource(), &source);
        EXPECT_FALSE(voice->IsDone());
        voices.push_back(std::move(voice));
    }

    voices.front()->SetOffset(sizeof(int16_t));
    auto* first = static_cast<ISDLAudioChannel*>(voices.front().get());
    auto* last = static_cast<ISDLAudioChannel*>(voices.back().get());
    int16_t output{};
    ASSERT_EQ(first->Read(&output, sizeof(output)), sizeof(output));
    EXPECT_EQ(output, 2);
    ASSERT_EQ(last->Read(&output, sizeof(output)), sizeof(output));
    EXPECT_EQ(output, 1);

    voices.front()->SetDone(true);
    auto replacement = mixer.Play(&source, kMixerLoopInfinite);
    ASSERT_NE(replacement, nullptr);
    EXPECT_FALSE(voices.back()->IsDone());
    ASSERT_EQ(last->Read(&output, sizeof(output)), sizeof(output));
    EXPECT_EQ(output, 2);
}

#endif

TEST(SpatialAudio, DirectionOnlyPreservesAxesAndCoincidentSource)
{
    SpatialAudioListener listener{};
    listener.Position = { 64, 96, 32 };
    listener.Right = { 1, 0, 0 };
    listener.Forward = { 0, 1, 0 };
    listener.Up = { 0, 0, 1 };
    struct Sample
    {
        CoordsXYZ source;
        float azimuth;
        float elevation;
    };
    constexpr float halfPi = std::numbers::pi_v<float> / 2.0f;
    const std::array samples{
        Sample{ { 64, 96, 32 }, 0, 0 },
        Sample{ { 64, 128, 32 }, 0, 0 },
        Sample{ { 96, 96, 32 }, halfPi, 0 },
        Sample{ { 32, 96, 32 }, -halfPi, 0 },
        Sample{ { 64, 64, 32 }, std::numbers::pi_v<float>, 0 },
        Sample{ { 64, 96, 64 }, 0, halfPi },
        Sample{ { 64, 96, 0 }, 0, -halfPi },
    };
    for (const auto& sample : samples)
    {
        const auto angles = CalculateSpatialAudioAngles(listener, sample.source);
        EXPECT_FLOAT_EQ(angles.Azimuth, sample.azimuth);
        EXPECT_FLOAT_EQ(angles.Elevation, sample.elevation);
    }
}

TEST(SpatialAudio, DirectionOnlyPreservesOriginalCrowdSectorsAndElevation)
{
    // Reference is the angular calculation before extraction. Compare the actual crowd consumers
    // (rounded sector and weighted elevation), including all camera rotations and fractional listener positions.
    constexpr float sectorAngle = 2.0f * std::numbers::pi_v<float> / 8.0f;
    constexpr std::array offsets{ -8192, -1024, -32, -1, 0, 1, 32, 1024, 8192 };
    for (uint8_t rotation = 0; rotation < 4; ++rotation)
    {
        auto listener = CalculateIsometricListener({ 1024, 2048, 96 }, rotation, 3840 * 4, 2160 * 4);
        listener.Position.x += 0.25f;
        listener.Position.y -= 0.75f;
        std::array<float, 8> expectedElevation{}, actualElevation{};
        std::array<int, 8> expectedWeights{}, actualWeights{};
        for (const auto x : offsets)
        {
            for (const auto y : offsets)
            {
                for (const auto z : { -64, 0, 256 })
                {
                    const CoordsXYZ source{ 1024 + x, 2048 + y, 96 + z };
                    const SpatialAudioVector relative{
                        static_cast<float>(source.x) - listener.Position.x,
                        static_cast<float>(source.y) - listener.Position.y,
                        static_cast<float>(source.z) - listener.Position.z,
                    };
                    const auto dot = [&relative](const SpatialAudioVector& axis) {
                        return (relative.x * axis.x) + (relative.y * axis.y) + (relative.z * axis.z);
                    };
                    const float right = dot(listener.Right);
                    const float forward = dot(listener.Forward);
                    const float up = dot(listener.Up);
                    const float planeDistance = std::sqrt((right * right) + (forward * forward));
                    const float azimuth = planeDistance > 0.0f ? std::atan2(right, forward) : 0.0f;
                    const float elevation = std::atan2(up, planeDistance);
                    const auto actual = CalculateSpatialAudioAngles(listener, source);
                    EXPECT_EQ(actual.Azimuth, azimuth);
                    EXPECT_EQ(actual.Elevation, elevation);
                    const auto sector = [](float angle) {
                        auto index = std::lround(angle / sectorAngle) % 8;
                        return static_cast<size_t>(index < 0 ? index + 8 : index);
                    };
                    const int weight = z == 0 ? 1 : 2;
                    expectedWeights[sector(azimuth)] += weight;
                    actualWeights[sector(actual.Azimuth)] += weight;
                    expectedElevation[sector(azimuth)] += elevation * static_cast<float>(weight);
                    actualElevation[sector(actual.Azimuth)] += actual.Elevation * static_cast<float>(weight);
                    for (const auto rolloff :
                         { SpatialAudioRolloff::world, SpatialAudioRolloff::vehicle, SpatialAudioRolloff::rideMusic })
                    {
                        const auto full = CalculateSpatialAudioParams(listener, source, 0.5f, rolloff);
                        EXPECT_EQ(full.Azimuth, actual.Azimuth);
                        EXPECT_EQ(full.Elevation, actual.Elevation);
                    }
                }
            }
        }
        EXPECT_EQ(actualWeights, expectedWeights);
        EXPECT_EQ(actualElevation, expectedElevation);
    }
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
    EXPECT_NEAR(sixteenTiles.Gain / eightTiles.Gain, std::pow(0.5f, kSpatialDistanceRolloff), 0.000001f);
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
    EXPECT_FLOAT_EQ(CalculateSpatialAudioParams(listener, referenceSource, 1.0f, SpatialAudioRolloff::vehicle).Gain, 1.0f);
    EXPECT_FLOAT_EQ(CalculateSpatialAudioParams(listener, referenceSource, 1.0f, SpatialAudioRolloff::rideMusic).Gain, 1.0f);
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
    const auto elevated = CalculateSpatialAudioParams(listener, { 0, 0, static_cast<int32_t>(std::lround(listener.Altitude)) });

    EXPECT_GT(ground.Distance, elevated.Distance);
    EXPECT_GT(elevated.Gain, ground.Gain);
    EXPECT_LT(ground.Elevation, elevated.Elevation);
}

TEST(SpatialAudio, IsometricCentreRayIsCameraForwardAtEveryRotation)
{
    const CoordsXYZ focus{ 4096, 4096, 64 };
    for (uint8_t rotation = 0; rotation < 4; rotation++)
    {
        const auto listener = CalculateIsometricListener(focus, rotation, 1280, 720);
        const auto ground = CalculateSpatialAudioParams(listener, focus);
        const auto rayGround = CoordsXY{ 1, 1 }.rotate((4 - rotation) & 3);
        const auto elevation = std::max(1, static_cast<int32_t>(std::lround(listener.Altitude * 0.5f)));
        const CoordsXYZ elevatedOnRay{
            focus.x + (rayGround.x * elevation),
            focus.y + (rayGround.y * elevation),
            focus.z + elevation,
        };
        const auto elevated = CalculateSpatialAudioParams(listener, elevatedOnRay);

        EXPECT_NEAR(ground.Azimuth, 0.0f, 0.0001f);
        EXPECT_NEAR(ground.Elevation, 0.0f, 0.0001f);
        EXPECT_NEAR(elevated.Azimuth, 0.0f, 0.0001f);
        EXPECT_NEAR(elevated.Elevation, 0.0f, 0.0001f);
        EXPECT_LT(elevated.Distance, ground.Distance);
        EXPECT_GT(elevated.Gain, ground.Gain);
    }
}

TEST(SpatialAudio, IsometricScreenRightMapsToPositiveCameraAzimuth)
{
    const CoordsXYZ focus{ 4096, 4096, 64 };
    for (uint8_t rotation = 0; rotation < 4; rotation++)
    {
        const auto listener = CalculateIsometricListener(focus, rotation, 1280, 720);
        const auto rightGround = CoordsXY{ -1, 1 }.rotate((4 - rotation) & 3);
        const CoordsXYZ source{
            focus.x + (rightGround.x * 128),
            focus.y + (rightGround.y * 128),
            focus.z,
        };
        EXPECT_GT(CalculateSpatialAudioParams(listener, source).Azimuth, 0.0f);
    }
}

TEST(SpatialAudio, ScreenVerticalDisplacementRemainsFrontBiased)
{
    const CoordsXYZ focus{ 4096, 4096, 256 };
    const auto listener = CalculateIsometricListener(focus, 0, 1280, 720);
    constexpr float displacement = 128.0f;
    const CoordsXYZ upperCentre{
        focus.x + static_cast<int32_t>(std::lround(listener.Up.x * displacement)),
        focus.y + static_cast<int32_t>(std::lround(listener.Up.y * displacement)),
        focus.z + static_cast<int32_t>(std::lround(listener.Up.z * displacement)),
    };
    const CoordsXYZ lowerCentre{
        focus.x - static_cast<int32_t>(std::lround(listener.Up.x * displacement)),
        focus.y - static_cast<int32_t>(std::lround(listener.Up.y * displacement)),
        focus.z - static_cast<int32_t>(std::lround(listener.Up.z * displacement)),
    };

    const auto upper = CalculateSpatialAudioParams(listener, upperCentre);
    const auto lower = CalculateSpatialAudioParams(listener, lowerCentre);
    EXPECT_NEAR(upper.Azimuth, 0.0f, 0.01f);
    EXPECT_NEAR(lower.Azimuth, 0.0f, 0.01f);
    EXPECT_GT(upper.Elevation, 0.0f);
    EXPECT_LT(lower.Elevation, 0.0f);
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
    const CoordsXYZ focus{ 4096, 4096, 64 };
    const auto unrotatedListener = CalculateIsometricListener(focus, 0, 1280, 720);
    const auto rotatedListener = CalculateIsometricListener(focus, 1, 1280, 720);
    const CoordsXYZ source{ focus.x, focus.y - 512, focus.z };
    const auto unrotated = CalculateSpatialAudioParams(unrotatedListener, source);
    const auto rotated = CalculateSpatialAudioParams(rotatedListener, source);

    // Rotating the view also moves the canonical camera around its focus, so a finite source does
    // not remain at exactly the same range while its bearing changes. It must still move by roughly
    // a quadrant rather than remaining pinned to the old speaker direction.
    const auto bearingChange = std::abs(rotated.Azimuth - unrotated.Azimuth);
    EXPECT_GT(bearingChange, std::numbers::pi_v<float> * 0.4f);
    EXPECT_LT(bearingChange, std::numbers::pi_v<float> * 0.6f);
}

TEST(SpatialAudio, DopplerSeparatesPhysicalSourceAndReducedCameraMotion)
{
    const auto speed = kSpatialSpeedOfSound * 0.2f;
    const auto sourceApproaching = CalculateDopplerFactor(-speed, 0.0f);
    const auto sourceReceding = CalculateDopplerFactor(speed, 0.0f);
    const auto cameraApproaching = CalculateDopplerFactor(0.0f, speed);
    const auto fullCameraApproaching = CalculateDopplerFactor(0.0f, speed, 1.0f);

    EXPECT_GT(sourceApproaching, 1.0f);
    EXPECT_LT(sourceReceding, 1.0f);
    EXPECT_GT(cameraApproaching, 1.0f);
    EXPECT_LT(cameraApproaching - 1.0f, fullCameraApproaching - 1.0f);
    EXPECT_FLOAT_EQ(CalculateDopplerFactor(0.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(CalculateDopplerFactor(-kSpatialSpeedOfSound, 0.0f), kMaxDopplerFactor);
    EXPECT_FLOAT_EQ(CalculateDopplerFactor(kSpatialSpeedOfSound, 0.0f), kMinDopplerFactor);
}

TEST(SpatialAudio, DopplerMotionIsSmoothedAndRejectsTeleports)
{
    SpatialAudioListener listener{};
    listener.Position = { 0.0f, 0.0f, 0.0f };
    listener.Discontinuous = false;
    DopplerMotionState state{};
    EXPECT_FLOAT_EQ(UpdateDopplerMotion(state, listener, { 1000, 0, 0 }, 1.0f / 60.0f, true), 1.0f);
    const auto approaching = UpdateDopplerMotion(state, listener, { 980, 0, 0 }, 1.0f / 60.0f, true);
    EXPECT_GT(approaching, 1.0f);
    EXPECT_LT(approaching, kMaxDopplerFactor);
    EXPECT_FLOAT_EQ(UpdateDopplerMotion(state, listener, { 100000, 0, 0 }, 1.0f / 60.0f, true), 1.0f);
}

TEST(SpatialAudio, StaticSourceUsesReducedListenerDoppler)
{
    SpatialAudioListener listener{};
    listener.Position = { 0.0f, 0.0f, 0.0f };
    listener.Discontinuous = false;
    const CoordsXYZ source{ 1000, 0, 0 };
    DopplerMotionState state{};
    EXPECT_FLOAT_EQ(UpdateDopplerMotion(state, listener, source, 1.0f / 60.0f, false), 1.0f);

    listener.Velocity = { kSpatialSpeedOfSound * 0.2f, 0.0f, 0.0f };
    const auto cameraOnly = UpdateDopplerMotion(state, listener, source, 1.0f / 60.0f, false);
    EXPECT_GT(cameraOnly, 1.0f);
    EXPECT_LT(cameraOnly, CalculateDopplerFactor(0.0f, kSpatialSpeedOfSound * 0.2f, 1.0f));
}

TEST(SpatialAudio, DistanceAndOcclusionReduceLowPassCutoffContinuously)
{
    const auto clearNear = CalculateDistanceLowPassCutoff(kSpatialReferenceDistance, 1.0f);
    const auto clearFar = CalculateDistanceLowPassCutoff(kSpatialReferenceDistance * 32.0f, 1.0f);
    const auto occludedNear = CalculateDistanceLowPassCutoff(kSpatialReferenceDistance, 0.25f);
    const auto vehicleFar = CalculateDistanceLowPassCutoff(
        kSpatialReferenceDistance * 32.0f, 1.0f, SpatialAudioRolloff::vehicle);
    const auto musicFar = CalculateDistanceLowPassCutoff(
        kSpatialReferenceDistance * 32.0f, 1.0f, SpatialAudioRolloff::rideMusic);

    EXPECT_FLOAT_EQ(clearNear, kSpatialFilterBypassCutoff);
    EXPECT_LT(clearFar, clearNear);
    EXPECT_LT(occludedNear, clearNear);
    EXPECT_LT(vehicleFar, clearFar);
    EXPECT_GT(musicFar, clearFar);
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

TEST(SpatialAudio, RideDefinitionDecibelGainUsesAmplitudeConvention)
{
    CarEntry legacyCar{};
    EXPECT_FLOAT_EQ(legacyCar.friction_sound_gain, 1.0f);
    EXPECT_NEAR(DecibelsToLinearGain(-6.0f), 0.501187f, 0.000001f);
    EXPECT_FLOAT_EQ(DecibelsToLinearGain(0.0f), 1.0f);
}

#ifndef OPENRCT2_TEST_NO_UI_AUDIO
TEST(SpatialAudio, NewSpatialChannelStartsFromConfiguredState)
{
    std::unique_ptr<IAudioChannel> channel(AudioChannel::Create());
    channel->SetVolume(64);
    InitialiseSpatialChannel(*channel, 0.125f, 1.25f, -0.25f, 3200.0f);

    EXPECT_FLOAT_EQ(channel->GetGain(), 0.125f);
    EXPECT_FLOAT_EQ(channel->GetOldGain(), 0.125f);
    EXPECT_FLOAT_EQ(channel->GetAzimuth(), 1.25f);
    EXPECT_FLOAT_EQ(channel->GetOldAzimuth(), 1.25f);
    EXPECT_FLOAT_EQ(channel->GetLowPassCutoff(), 3200.0f);
    EXPECT_FLOAT_EQ(channel->GetOldLowPassCutoff(), 3200.0f);
    EXPECT_EQ(channel->GetVolume(), 64);
    EXPECT_EQ(channel->GetOldVolume(), 64);
}

#endif

TEST(SpatialAudio, ShippedKartDefinitionProvidesFrictionSoundGain)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = OpenRCT2::CreateContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->Initialise());

    auto* object = context->GetObjectManager().LoadObject("rct2.ride.kart1");
    auto* rideObject = dynamic_cast<OpenRCT2::RideObject*>(object);
    ASSERT_NE(rideObject, nullptr);
    EXPECT_NEAR(rideObject->GetEntry().Cars[0].friction_sound_gain, DecibelsToLinearGain(-6.0f), 0.000001f);
}

#ifndef OPENRCT2_TEST_NO_UI_AUDIO
TEST(AudioChannel, FloatingPointGainPreservesValuesAboveUnity)
{
    std::unique_ptr<ISDLAudioChannel> channel(AudioChannel::Create());
    channel->SetGain(4.5f);
    EXPECT_FLOAT_EQ(channel->GetGain(), 4.5f);
    channel->UpdateOldVolume();
    EXPECT_FLOAT_EQ(channel->GetOldGain(), 4.5f);
}

#endif

TEST(SpatialAudio, LimiterUsesImmediateAttackAndGradualRelease)
{
    const auto attacked = CalculateNextLimiterGain(1.0f, 1.9f, 1024, 48000);
    EXPECT_FLOAT_EQ(attacked, 0.5f);

    const auto released = CalculateNextLimiterGain(attacked, 0.1f, 1024, 48000);
    EXPECT_GT(released, attacked);
    EXPECT_LT(released, 1.0f);
}
