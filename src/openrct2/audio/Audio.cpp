/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Audio.h"

#include "../Context.h"
#include "../OpenRCT2.h"
#include "../PlatformEnvironment.h"
#include "../config/Config.h"
#include "../core/EnumUtils.hpp"
#include "../core/File.h"
#include "../core/FileStream.h"
#include "../core/String.hpp"
#include "../entity/Peep.h"
#include "../localisation/Language.h"
#include "../localisation/StringIds.h"
#include "../object/AudioObject.h"
#include "../object/ObjectManager.h"
#include "../ride/Ride.h"
#include "../ride/RideAudio.h"
#include "../scenes/intro/IntroScene.h"
#include "../ui/WindowManager.h"
#include "../util/Util.h"
#include "../world/Map.h"
#include "../world/Weather.h"
#include "../world/tile_element/SurfaceElement.h"
#include "AudioChannel.h"
#include "AudioContext.h"
#include "AudioMixer.h"
#include "SpatialAudio.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

namespace OpenRCT2::Audio
{
    struct AudioParams
    {
        bool inRange;
        int32_t volume;
        float azimuth;
        float elevation;
        float distance;
        float occlusion;
        int32_t baseVolume;
        float spatialGain;
        float lowPassCutoff;
    };

    struct ActiveSpatialSound
    {
        std::shared_ptr<IAudioChannel> Channel;
        CoordsXYZ Location;
        int32_t BaseVolume{};
        float Occlusion{ 1.0f };
        DopplerMotionState Doppler{};
        std::chrono::steady_clock::time_point LastUpdate{};
    };

    static std::vector<std::string> _audioDevices;
    static int32_t _currentAudioDevice = -1;
    static ObjectEntryIndex _soundsAudioObjectEntryIndex = kObjectEntryIndexNull;
    static ObjectEntryIndex _soundsAdditionalAudioObjectEntryIndex = kObjectEntryIndexNull;
    static ObjectEntryIndex _titleAudioObjectEntryIndex = kObjectEntryIndexNull;
    static std::vector<ActiveSpatialSound> _activeSpatialSounds;
    static std::chrono::steady_clock::time_point _lastSpatialSoundUpdate{};

    bool gGameSoundsOff = false;

    static std::shared_ptr<IAudioChannel> _titleMusicChannel = nullptr;

    VehicleSound gVehicleSoundList[kMaxVehicleSounds];

    bool IsAvailable()
    {
        if (_currentAudioDevice == -1)
            return false;
        if (gGameSoundsOff)
            return false;
        if (!Config::Get().sound.soundEnabled)
            return false;
        if (gOpenRCT2Headless)
            return false;
        return true;
    }

    void Init()
    {
        auto& audioContext = GetContext()->GetAudioContext();
        if (Config::Get().sound.device.empty())
        {
            audioContext.SetOutputDevice("");
            _currentAudioDevice = 0;
        }
        else
        {
            audioContext.SetOutputDevice(Config::Get().sound.device);

            PopulateDevices();
            for (int32_t i = 0; i < GetDeviceCount(); i++)
            {
                if (_audioDevices[i] == Config::Get().sound.device)
                {
                    _currentAudioDevice = i;
                }
            }
        }
    }

    void LoadAudioObjects()
    {
        auto& objManager = GetContext()->GetObjectManager();

        Object* baseAudio = objManager.LoadObject(AudioObjectIdentifiers::kRCT2);
        if (baseAudio != nullptr)
        {
            _soundsAudioObjectEntryIndex = objManager.GetLoadedObjectEntryIndex(baseAudio);
        }

        objManager.LoadObject(AudioObjectIdentifiers::kOpenRCT2Additional);
        _soundsAdditionalAudioObjectEntryIndex = objManager.GetLoadedObjectEntryIndex(
            AudioObjectIdentifiers::kOpenRCT2Additional);
        objManager.LoadObject(AudioObjectIdentifiers::kRCT2Circus);
    }

    void PopulateDevices()
    {
        auto& audioContext = GetContext()->GetAudioContext();
        std::vector<std::string> devices = audioContext.GetOutputDevices();

        // Replace blanks with localised unknown string
        for (auto& device : devices)
        {
            if (device.empty())
            {
                device = LanguageGetString(STR_OPTIONS_SOUND_VALUE_DEFAULT);
            }
        }

        // The first device is always system default
        std::string defaultDevice = LanguageGetString(STR_OPTIONS_SOUND_VALUE_DEFAULT);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
        devices.insert(devices.begin(), defaultDevice);
#pragma GCC diagnostic pop

        _audioDevices = devices;
    }

    /**
     * Returns the audio parameters to use when playing the specified sound at a virtual location.
     * @param soundId The sound effect to be played.
     * @param location The location at which the sound effect is to be played.
     * @return The audio parameters to be used when playing this sound effect.
     */
    static AudioParams GetParametersFromLocation(
        SoundId soundId, AudioObject* obj, uint32_t sampleIndex, const CoordsXYZ& location)
    {
        float occlusion = 1.0f;
        auto element = MapGetSurfaceElementAt(location);
        if (element != nullptr && (element->getBaseZ()) - 5 > location.z)
        {
            occlusion = 0.25f;
        }

        const auto listener = GetSpatialAudioListener();
        if (!listener.has_value())
        {
            return {};
        }

        const auto spatial = CalculateSpatialAudioParams(*listener, location, occlusion);
        // The purchase/cash-register sample is short and was being masked by sustained park ambience.
        // Keep the correction local to that cue instead of raising every positional one-shot.
        constexpr int32_t kPurchaseSourceBoost = 400;
        const auto baseVolume = obj->GetSampleModifier(sampleIndex)
            + (soundId == SoundId::purchase ? kPurchaseSourceBoost : 0);
        return AudioParams{
            spatial.Audible,
            baseVolume,
            spatial.Azimuth,
            spatial.Elevation,
            spatial.Distance,
            occlusion,
            baseVolume,
            spatial.Gain,
            spatial.LowPassCutoff,
        };
    }

    static std::tuple<AudioObject*, uint32_t> GetAudioObjectAndSampleIndex(SoundId id)
    {
        auto& objManager = GetContext()->GetObjectManager();
        AudioObject* audioObject{};
        uint32_t sampleIndex = EnumValue(id);
        if (id >= SoundId::liftRMC)
        {
            audioObject = objManager.GetLoadedObject<AudioObject>(_soundsAdditionalAudioObjectEntryIndex);
            sampleIndex -= EnumValue(SoundId::liftRMC);
        }
        else
        {
            audioObject = objManager.GetLoadedObject<AudioObject>(_soundsAudioObjectEntryIndex);
        }
        return std::make_tuple(audioObject, sampleIndex);
    }

    static void Play(IAudioSource* audioSource, int32_t volume, int32_t pan)
    {
        int32_t mixerPan = 0;
        if (pan != kAudioPlayAtCentre)
        {
            int32_t x2 = pan << 16;
            uint16_t screenWidth = std::max<int32_t>(64, ContextGetWidth());
            mixerPan = ((x2 / screenWidth) - 0x8000) >> 4;
        }

        CreateAudioChannel(audioSource, MixerGroup::Sound, false, DStoMixerVolume(volume), DStoMixerPan(mixerPan));
    }

    static void PlaySpatial(IAudioSource* audioSource, const AudioParams& params, const CoordsXYZ& location)
    {
        auto channel = CreateAudioChannel(audioSource, MixerGroup::Sound, false, DStoMixerVolume(params.volume));
        if (channel != nullptr)
        {
            channel->SetSpatial(params.azimuth, params.elevation);
            channel->SetGain(params.spatialGain);
            channel->SetLowPassCutoff(params.lowPassCutoff);
            ActiveSpatialSound active{ channel, location, params.baseVolume, params.occlusion };
            if (const auto listener = GetSpatialAudioListener(); listener.has_value())
            {
                UpdateDopplerMotion(active.Doppler, *listener, location, 0.0f, false);
            }
            active.LastUpdate = std::chrono::steady_clock::now();
            _activeSpatialSounds.push_back(std::move(active));
        }
    }

    void Play3D(SoundId soundId, const CoordsXYZ& loc)
    {
        if (!IsAvailable())
            return;

        // Get sound from base object
        auto [baseAudioObject, sampleIndex] = GetAudioObjectAndSampleIndex(soundId);
        if (baseAudioObject != nullptr)
        {
            auto params = GetParametersFromLocation(soundId, baseAudioObject, sampleIndex, loc);
            if (params.inRange)
            {
                auto source = baseAudioObject->GetSample(sampleIndex);
                if (source != nullptr)
                {
                    PlaySpatial(source, params, loc);
                }
            }
        }
    }

    void UpdateSpatialSounds()
    {
        const auto listener = GetSpatialAudioListener();
        if (!listener.has_value() || _activeSpatialSounds.empty())
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (_lastSpatialSoundUpdate != std::chrono::steady_clock::time_point{}
            && now - _lastSpatialSoundUpdate < std::chrono::milliseconds(4))
        {
            return;
        }
        _lastSpatialSoundUpdate = now;

        _activeSpatialSounds.erase(
            std::remove_if(
                _activeSpatialSounds.begin(), _activeSpatialSounds.end(), [&](auto& active) {
                    if (active.Channel == nullptr || !active.Channel->IsPlaying())
                    {
                        return true;
                    }

                    const auto spatial = CalculateSpatialAudioParams(*listener, active.Location, active.Occlusion);
                    const auto elapsed = std::chrono::duration<float>(now - active.LastUpdate).count();
                    const auto doppler = UpdateDopplerMotion(
                        active.Doppler, *listener, active.Location, elapsed, false);
                    active.LastUpdate = now;
                    active.Channel->SetVolume(DStoMixerVolume(active.BaseVolume));
                    active.Channel->SetGain(spatial.Gain);
                    active.Channel->SetSpatial(spatial.Azimuth, spatial.Elevation);
                    active.Channel->SetLowPassCutoff(spatial.LowPassCutoff);
                    active.Channel->SetRate(doppler);
                    return false;
                }),
            _activeSpatialSounds.end());
    }

    void Play(SoundId soundId, int32_t volume, int32_t pan)
    {
        if (!IsAvailable())
            return;

        // Get sound from base object
        auto [baseAudioObject, sampleIndex] = GetAudioObjectAndSampleIndex(soundId);
        if (baseAudioObject != nullptr)
        {
            auto source = baseAudioObject->GetSample(sampleIndex);
            if (source != nullptr)
            {
                Play(source, volume, pan);
            }
        }
    }

    static bool IsRCT1TitleMusicAvailable()
    {
        auto& env = GetContext()->GetPlatformEnvironment();
        auto rct1path = env.GetDirectoryPath(DirBase::rct1);
        return !rct1path.empty();
    }

    static std::map<TitleMusicKind, std::string_view> GetAvailableMusicMap()
    {
        auto musicMap = std::map<TitleMusicKind, std::string_view>{
            { TitleMusicKind::OpenRCT2, AudioObjectIdentifiers::kOpenRCT2Title },
            { TitleMusicKind::RCT2, AudioObjectIdentifiers::kRCT2Title },
        };

        if (IsRCT1TitleMusicAvailable())
        {
            musicMap.emplace(TitleMusicKind::RCT1, AudioObjectIdentifiers::kRCT1Title);
        }

        return musicMap;
    }

    static ObjectEntryDescriptor GetTitleMusicDescriptor(TitleMusicKind musicKind)
    {
        auto musicMap = GetAvailableMusicMap();
        auto it = musicMap.find(musicKind);
        if (musicKind == TitleMusicKind::random)
        {
            it = std::next(musicMap.begin(), UtilRand() % musicMap.size());
        }

        if (it != musicMap.end())
        {
            return ObjectEntryDescriptor(ObjectType::audio, it->second);
        }

        // No music descriptor for the current setting, intentional for TitleMusicKind::none
        return {};
    }

    void PlayTitleMusic()
    {
        if (gGameSoundsOff || gLegacyScene != LegacyScene::titleSequence || IntroIsPlaying())
        {
            StopTitleMusic();
            return;
        }

        if (_titleMusicChannel != nullptr && !_titleMusicChannel->IsDone())
        {
            return;
        }

        // Load title sequence audio object
        auto descriptor = GetTitleMusicDescriptor(Config::Get().sound.titleMusic);
        auto& objManager = GetContext()->GetObjectManager();
        auto* audioObject = static_cast<AudioObject*>(objManager.LoadObject(descriptor));
        if (audioObject != nullptr)
        {
            _titleAudioObjectEntryIndex = objManager.GetLoadedObjectEntryIndex(audioObject);

            // Play first sample from object
            auto source = audioObject->GetSample(0);
            if (source != nullptr)
            {
                _titleMusicChannel = CreateAudioChannel(source, MixerGroup::TitleMusic, true);
            }
        }
    }

    void StopSFX()
    {
        StopVehicleSounds();
        for (auto& active : _activeSpatialSounds)
        {
            if (active.Channel != nullptr)
            {
                active.Channel->Stop();
            }
        }
        _activeSpatialSounds.clear();
        PeepStopCrowdNoise();
        Weather::stopWeatherSound();
    }

    void StopAll()
    {
        StopSFX();
        StopTitleMusic();
        RideAudio::StopAllChannels();
    }

    int32_t GetDeviceCount()
    {
        return static_cast<int32_t>(_audioDevices.size());
    }

    const std::string& GetDeviceName(int32_t index)
    {
        if (index < 0 || index >= GetDeviceCount())
        {
            static std::string InvalidDevice = "Invalid Device";
            return InvalidDevice;
        }
        return _audioDevices[index];
    }

    int32_t GetCurrentDeviceIndex()
    {
        return _currentAudioDevice;
    }

    void StopTitleMusic()
    {
        if (_titleMusicChannel != nullptr)
        {
            _titleMusicChannel->Stop();
            _titleMusicChannel = nullptr;
        }

        // Unload the audio object
        if (_titleAudioObjectEntryIndex != kObjectEntryIndexNull)
        {
            auto& objManager = GetContext()->GetObjectManager();
            auto* obj = objManager.GetLoadedObject<AudioObject>(_titleAudioObjectEntryIndex);
            if (obj != nullptr)
            {
                objManager.UnloadObjects({ obj->GetDescriptor() });
            }
            _titleAudioObjectEntryIndex = kObjectEntryIndexNull;
        }
    }

    static void ResetRideSounds(int32_t device)
    {
        Close();
        for (auto& vehicleSound : gVehicleSoundList)
        {
            vehicleSound.id = kSoundIdNull;
        }

        _currentAudioDevice = device;
    }

    void InitRideSoundsAndInfo(bool persistDeviceSelection)
    {
        ResetRideSounds(0);
        if (persistDeviceSelection)
        {
            Config::Save();
        }
    }

    void InitRideSounds(int32_t device)
    {
        ResetRideSounds(device);
        Config::Save();
    }

    void Close()
    {
        PeepStopCrowdNoise();
        StopTitleMusic();
        RideAudio::StopAllChannels();
        Weather::stopWeatherSound();
        _currentAudioDevice = -1;
    }

    void ToggleAllSounds()
    {
        Config::Get().sound.masterSoundEnabled = !Config::Get().sound.masterSoundEnabled;
        if (Config::Get().sound.masterSoundEnabled)
        {
            Resume();
        }
        else
        {
            Pause();
        }

        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->InvalidateByClass(WindowClass::options);
    }

    void Pause()
    {
        gGameSoundsOff = true;
        StopAll();
    }

    void Resume()
    {
        gGameSoundsOff = !Config::Get().sound.masterSoundEnabled;
        PlayTitleMusic();
    }

    void StopVehicleSounds()
    {
        if (!IsAvailable())
            return;

        for (auto& vehicleSound : gVehicleSoundList)
        {
            if (vehicleSound.id != kSoundIdNull)
            {
                vehicleSound.id = kSoundIdNull;
                if (vehicleSound.trackSound.id != SoundId::null)
                {
                    vehicleSound.trackSound.channel->Stop();
                }
                if (vehicleSound.otherSound.id != SoundId::null)
                {
                    vehicleSound.otherSound.channel->Stop();
                }
            }
        }
    }

    static IAudioMixer* GetMixer()
    {
        auto& audioContext = GetContext()->GetAudioContext();
        return audioContext.GetMixer();
    }

    std::shared_ptr<IAudioChannel> CreateAudioChannel(
        SoundId id, bool loop, int32_t volume, float pan, double rate)
    {
        return CreateAudioChannel(id, MixerGroup::Sound, loop, volume, pan, rate);
    }

    std::shared_ptr<IAudioChannel> CreateAudioChannel(
        SoundId id, MixerGroup group, bool loop, int32_t volume, float pan, double rate)
    {
        // Get sound from base object
        auto [baseAudioObject, sampleIndex] = GetAudioObjectAndSampleIndex(id);
        if (baseAudioObject != nullptr)
        {
            auto source = baseAudioObject->GetSample(sampleIndex);
            if (source != nullptr)
            {
                return CreateAudioChannel(source, group, loop, volume, pan, rate);
            }
        }
        return nullptr;
    }

    std::shared_ptr<IAudioChannel> CreateAudioChannel(
        IAudioSource* source, MixerGroup group, bool loop, int32_t volume, float pan, double rate)
    {
        auto* mixer = GetMixer();
        if (mixer == nullptr)
        {
            return nullptr;
        }

        mixer->Lock();
        auto channel = mixer->Play(source, loop ? kMixerLoopInfinite : kMixerLoopNone);
        if (channel != nullptr)
        {
            channel->SetGroup(group);
            channel->SetVolume(volume);
            channel->SetPan(pan);
            channel->SetRate(rate);
            channel->UpdateOldVolume();
        }
        mixer->Unlock();
        return channel;
    }

    int32_t DStoMixerVolume(int32_t volume)
    {
        return static_cast<int32_t>(kMixerVolumeMax * (std::pow(10.0f, static_cast<float>(volume) / 2000)));
    }

    float DStoMixerPan(int32_t pan)
    {
        constexpr int32_t kDSBPanLeft = -10000;
        constexpr int32_t kDSBPanRight = 10000;
        return ((static_cast<float>(pan) + -kDSBPanLeft) / kDSBPanRight) / 2;
    }

    double DStoMixerRate(int32_t frequency)
    {
        return static_cast<double>(frequency) / 22050;
    }

} // namespace OpenRCT2::Audio
