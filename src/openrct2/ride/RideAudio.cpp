/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "RideAudio.h"

#include "../Context.h"
#include "../Diagnostic.h"
#include "../OpenRCT2.h"
#include "../audio/Audio.h"
#include "../audio/AudioChannel.h"
#include "../audio/AudioContext.h"
#include "../audio/AudioMixer.h"
#include "../audio/SpatialAudio.h"
#include "../config/Config.h"
#include "../interface/Viewport.h"
#include "../object/AudioObject.h"
#include "../object/MusicObject.h"
#include "../object/ObjectManager.h"
#include "Ride.h"
#include "RideData.h"

#include <algorithm>
#include <chrono>
#include <vector>

using namespace OpenRCT2;
using namespace OpenRCT2::Audio;

namespace OpenRCT2::RideAudio
{
    constexpr size_t kMaxRideMusicChannels = 64;
    constexpr float kRideMusicSourceGain = 5.0f;

    /**
     * Represents an audio channel to play a particular ride's music track.
     */
    struct RideMusicChannel
    {
        ::RideId RideId{};
        uint8_t TrackIndex{};

        size_t Offset{};
        int16_t Volume{};
        float Gain{};
        float Azimuth{};
        float Elevation{};
        float LowPassCutoff{ kSpatialFilterBypassCutoff };
        uint16_t Frequency{};
        DopplerMotionState Doppler{};
        std::chrono::steady_clock::time_point LastMotionUpdate{};

        std::shared_ptr<IAudioChannel> Channel{};
        IAudioSource* Source{};
        bool Stopping{};

        RideMusicChannel(
            const ViewportRideMusicInstance& instance, std::shared_ptr<IAudioChannel> channel, IAudioSource* source)
        {
            RideId = instance.RideId;
            TrackIndex = instance.TrackIndex;

            Offset = std::max<size_t>(0, instance.Offset - 10000);
            Volume = instance.Volume;
            Gain = instance.Gain;
            Azimuth = instance.Azimuth;
            Elevation = instance.Elevation;
            LowPassCutoff = instance.LowPassCutoff;
            Frequency = instance.Frequency;
            UpdateDopplerMotion(Doppler, instance.Listener, instance.SourcePosition, 0.0f, false);
            LastMotionUpdate = std::chrono::steady_clock::now();

            channel->SetOffset(Offset);
            channel->SetVolume(DStoMixerVolume(Volume));
            channel->SetRate(DStoMixerRate(Frequency));
            InitialiseSpatialChannel(*channel, Gain, Azimuth, Elevation, LowPassCutoff);
            Channel = std::move(channel);

            Source = source;
        }

        RideMusicChannel(const RideMusicChannel&) = delete;

        RideMusicChannel(RideMusicChannel&& src) noexcept
        {
            *this = std::move(src);
        }

        RideMusicChannel& operator=(RideMusicChannel&& src) noexcept
        {
            using std::swap;

            RideId = src.RideId;
            TrackIndex = src.TrackIndex;

            Offset = src.Offset;
            Volume = src.Volume;
            Gain = src.Gain;
            Azimuth = src.Azimuth;
            Elevation = src.Elevation;
            LowPassCutoff = src.LowPassCutoff;
            Frequency = src.Frequency;
            Doppler = src.Doppler;
            LastMotionUpdate = src.LastMotionUpdate;

            swap(Channel, src.Channel);
            swap(Source, src.Source);
            Stopping = src.Stopping;

            return *this;
        }

        ~RideMusicChannel()
        {
            if (Channel != nullptr)
            {
                Channel->Stop();
            }
            if (Source != nullptr)
            {
                Source->Release();
            }
        }

        bool IsPlaying() const
        {
            if (Channel != nullptr)
            {
                return Channel->IsPlaying();
            }
            return false;
        }

        void BeginStop()
        {
            if (!Stopping && Channel != nullptr)
            {
                Channel->Stop();
                Stopping = true;
            }
        }

        void Resume()
        {
            if (Stopping && Channel != nullptr)
            {
                Channel->SetStopping(false);
                Stopping = false;
            }
        }

        size_t GetOffset() const
        {
            if (Channel != nullptr)
            {
                return Channel->GetOffset();
            }
            return 0;
        }

        void Update(const ViewportRideMusicInstance& instance)
        {
            Resume();
            const auto ride = GetRide(instance.RideId);
            if (ride != nullptr && ride->flags.has(RideFlag::crashed) && Channel != nullptr)
            {
                Channel->SetLoop(kMixerLoopNone);
            }
            if (Volume != instance.Volume)
            {
                Volume = instance.Volume;
                if (Channel != nullptr)
                {
                    Channel->SetVolume(DStoMixerVolume(Volume));
                }
            }
            if (Gain != instance.Gain)
            {
                Gain = instance.Gain;
                if (Channel != nullptr)
                {
                    Channel->SetGain(Gain);
                }
            }
            if (Azimuth != instance.Azimuth || Elevation != instance.Elevation)
            {
                Azimuth = instance.Azimuth;
                Elevation = instance.Elevation;
                if (Channel != nullptr)
                {
                    Channel->SetSpatial(Azimuth, Elevation);
                }
            }
            if (LowPassCutoff != instance.LowPassCutoff)
            {
                LowPassCutoff = instance.LowPassCutoff;
                if (Channel != nullptr)
                {
                    Channel->SetLowPassCutoff(LowPassCutoff);
                }
            }
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration<float>(now - LastMotionUpdate).count();
            const auto doppler = UpdateDopplerMotion(
                Doppler, instance.Listener, instance.SourcePosition, elapsed, false);
            LastMotionUpdate = now;
            if (Frequency != instance.Frequency)
            {
                Frequency = instance.Frequency;
            }
            if (Channel != nullptr)
            {
                Channel->SetRate(DStoMixerRate(Frequency) * doppler);
            }
        }
    };

    static std::vector<ViewportRideMusicInstance> _musicInstances;
    static std::vector<RideMusicChannel> _musicChannels;
    static std::chrono::steady_clock::time_point _lastMusicAudioReport{};

    bool IsMusicInstanceHigherPriority(
        const ViewportRideMusicInstance& lhs, const ViewportRideMusicInstance& rhs)
    {
        return lhs.PriorityGain > rhs.PriorityGain;
    }

    void StopAllChannels()
    {
        _musicChannels.clear();
    }

    void ClearAllViewportInstances()
    {
        _musicInstances.clear();
    }

    void DefaultStartRideMusicChannel(const ViewportRideMusicInstance& instance)
    {
        auto& objManager = GetContext()->GetObjectManager();
        auto ride = GetRide(instance.RideId);
        auto musicObj = objManager.GetLoadedObject<MusicObject>(ride->music);
        if (musicObj != nullptr)
        {
            auto shouldLoop = musicObj->GetTrackCount() == 1 && !ride->flags.has(RideFlag::crashed);
            auto source = musicObj->GetTrackSample(instance.TrackIndex);
            if (source != nullptr)
            {
                auto channel = CreateAudioChannel(source, MixerGroup::RideMusic, shouldLoop, 0);
                if (channel != nullptr)
                {
                    _musicChannels.emplace_back(instance, channel, source);
                }
            }
        }
    }
    void CircusStartRideMusicChannel(const ViewportRideMusicInstance& instance)
    {
        auto& objManager = GetContext()->GetObjectManager();
        ObjectEntryDescriptor desc(ObjectType::audio, AudioObjectIdentifiers::kRCT2Circus);
        auto audioObj = static_cast<AudioObject*>(objManager.GetLoadedObject(desc));
        if (audioObj != nullptr)
        {
            auto source = audioObj->GetSample(0);
            if (source != nullptr)
            {
                auto channel = CreateAudioChannel(source, MixerGroup::Sound, false, 0);
                if (channel != nullptr)
                {
                    _musicChannels.emplace_back(instance, channel, nullptr);
                }
            }
        }
    }

    static void StartRideMusicChannel(const ViewportRideMusicInstance& instance)
    {
        // Create new music channel
        auto ride = GetRide(instance.RideId);
        const auto& rtd = ride->getRideTypeDescriptor();
        rtd.StartRideMusic(instance);
    }

    static void StopInactiveRideMusicChannels()
    {
        _musicChannels.erase(
            std::remove_if(
                _musicChannels.begin(), _musicChannels.end(),
                [](auto& channel) {
                    auto found = std::any_of(_musicInstances.begin(), _musicInstances.end(), [&channel](const auto& instance) {
                        return instance.RideId == channel.RideId && instance.TrackIndex == channel.TrackIndex;
                    });
                    if (!found)
                    {
                        channel.BeginStop();
                    }
                    if (!channel.IsPlaying())
                    {
                        return true;
                    }

                    return false;
                }),
            _musicChannels.end());
    }

    static void UpdateRideMusicChannelForMusicParams(
        const ViewportRideMusicInstance& instance, size_t& activeChannelCount, size_t channelBudget)
    {
        // Find existing music channel
        auto foundChannel = std::find_if(
            _musicChannels.begin(), _musicChannels.end(), [&instance](const RideMusicChannel& channel) {
                return channel.RideId == instance.RideId && channel.TrackIndex == instance.TrackIndex;
            });

        if (foundChannel != _musicChannels.end())
        {
            if (foundChannel->Stopping)
            {
                activeChannelCount++;
            }
            foundChannel->Update(instance);
        }
        else if (activeChannelCount < channelBudget)
        {
            const auto previousChannelCount = _musicChannels.size();
            StartRideMusicChannel(instance);
            if (_musicChannels.size() > previousChannelCount)
            {
                activeChannelCount++;
            }
        }
    }

    /**
     * Start, update and stop audio channels for each ride music instance that can be heard across all viewports.
     */
    void UpdateMusicChannels()
    {
        if (gLegacyScene == LegacyScene::scenarioEditor || gLegacyScene == LegacyScene::titleSequence)
            return;

        // TODO Allow circus music (CSS24) to play if ride music is disabled (that should be sound)
        if (gGameSoundsOff || !Config::Get().sound.rideMusicEnabled)
            return;

        constexpr auto channelBudget = kMaxRideMusicChannels;
        std::stable_sort(_musicInstances.begin(), _musicInstances.end(), IsMusicInstanceHigherPriority);
        const auto candidateCount = _musicInstances.size();
        if (_musicInstances.size() > channelBudget)
        {
            _musicInstances.resize(channelBudget);
        }
        StopInactiveRideMusicChannels();
        auto activeChannelCount = static_cast<size_t>(
            std::count_if(_musicChannels.begin(), _musicChannels.end(), [](const auto& channel) { return !channel.Stopping; }));
        for (const auto& instance : _musicInstances)
        {
            UpdateRideMusicChannelForMusicParams(instance, activeChannelCount, channelBudget);
        }

        const auto now = std::chrono::steady_clock::now();
        if (_lastMusicAudioReport == std::chrono::steady_clock::time_point{}
            || now - _lastMusicAudioReport >= std::chrono::seconds(5))
        {
            LOG_VERBOSE(
                "Spatial audio selected %zu ride-music emitters from %zu candidates with a %zu-voice budget",
                _musicInstances.size(), candidateCount, channelBudget);
            _lastMusicAudioReport = now;
        }
    }

    std::pair<size_t, size_t> RideMusicGetTrackOffsetLength_Circus(const Ride& ride)
    {
        return { 1378, 12427456 };
    }

    std::pair<size_t, size_t> RideMusicGetTrackOffsetLength_Default(const Ride& ride)
    {
        auto& objManager = GetContext()->GetObjectManager();
        auto musicObj = objManager.GetLoadedObject<MusicObject>(ride.music);
        if (musicObj != nullptr)
        {
            auto numTracks = musicObj->GetTrackCount();
            if (ride.musicTuneId < numTracks)
            {
                auto track = musicObj->GetTrack(ride.musicTuneId);
                return { track->BytesPerTick, track->Size };
            }
        }
        return { 0, 0 };
    }

    static std::pair<size_t, size_t> RideMusicGetTrackOffsetLength(const Ride& ride)
    {
        const auto& rtd = ride.getRideTypeDescriptor();
        return rtd.MusicTrackOffsetLength(ride);
    }

    static void RideUpdateMusicPosition(Ride& ride)
    {
        auto [trackOffset, trackLength] = RideMusicGetTrackOffsetLength(ride);
        auto position = ride.musicPosition + trackOffset;
        if (position < trackLength)
        {
            ride.musicPosition = position;
        }
        else
        {
            ride.musicTuneId = kTuneIDNull;
            ride.musicPosition = 0;
        }
    }

    void AdvanceMusicPosition(Ride& ride)
    {
        if (gLegacyScene != LegacyScene::scenarioEditor && !gGameSoundsOff)
            RideUpdateMusicPosition(ride);
    }

    /**
     * Register an instance of audible ride music for this presentation frame. This samples authoritative music state but
     * never writes it: display cadence and an audio channel's wall-clock cursor cannot affect simulation or multiplayer state.
     */
    void CollectMusicInstance(const Ride& ride, const CoordsXYZ& rideCoords, uint16_t sampleRate)
    {
        if (gLegacyScene != LegacyScene::scenarioEditor && !gGameSoundsOff)
        {
            const auto listener = GetSpatialAudioListener();
            if (!listener.has_value())
                return;

            const auto spatial = CalculateSpatialAudioParams(
                *listener, rideCoords, 1.0f, SpatialAudioRolloff::rideMusic);
            // Ride music is emitted by amplified park speakers rather than a point-sized mechanical source.
            // Its source calibration and compressed continuous rolloff keep a faint long-range bed.
            const auto musicGain = spatial.Gain * kRideMusicSourceGain;
            constexpr int16_t newVolume = 0;
            if (spatial.Audible && musicGain > 0.0f)
            {
                auto [trackOffset, trackLength] = RideMusicGetTrackOffsetLength(ride);
                auto foundChannel = std::find_if(_musicChannels.begin(), _musicChannels.end(), [&ride](const auto& channel) {
                    return channel.RideId == ride.id && channel.TrackIndex == ride.musicTuneId;
                });
                const auto offset = foundChannel != _musicChannels.end() && foundChannel->IsPlaying()
                    ? foundChannel->GetOffset()
                    : ride.musicPosition + trackOffset;
                if (offset < trackLength)
                {
                    auto& instance = _musicInstances.emplace_back();
                    instance.RideId = ride.id;
                    instance.TrackIndex = ride.musicTuneId;
                    instance.Offset = offset;
                    instance.Volume = newVolume;
                    instance.Gain = musicGain;
                    instance.PriorityGain = spatial.Gain;
                    instance.Azimuth = spatial.Azimuth;
                    instance.Elevation = spatial.Elevation;
                    instance.Distance = spatial.Distance;
                    instance.LowPassCutoff = spatial.LowPassCutoff;
                    instance.Listener = *listener;
                    instance.SourcePosition = rideCoords;
                    instance.Frequency = sampleRate;
                }
            }
        }
    }
} // namespace OpenRCT2::RideAudio
