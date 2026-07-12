/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "AudioContext.h"
#include "AudioFormat.h"
#include "AudioMixer.SIMD.h"
#include "SDLAudioSource.h"

#include <SDL.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <openrct2/Context.h>
#include <openrct2/audio/Audio.h>
#include <openrct2/audio/AudioChannel.h>
#include <openrct2/audio/AudioMixer.h>
#include <openrct2/audio/AudioSource.h>
#include <openrct2/audio/SpatialAudio.h>
#include <vector>

namespace OpenRCT2::Audio
{
    class AudioMixer final : public IAudioMixer
    {
    private:
        std::vector<std::unique_ptr<SDLAudioSource>> _sources;

        SDL_AudioDeviceID _deviceId = 0;
        AudioFormat _outputFormat = {};
        AudioFormat _sourceFormat = {};
        std::vector<std::shared_ptr<ISDLAudioChannel>> _channels;
        float _volume = 1.0f;
        float _adjustSoundVolume = 0.0f;
        float _adjustMusicVolume = 0.0f;
        uint8_t _settingSoundVolume = 0xFF;
        uint8_t _settingMusicVolume = 0xFF;

        std::vector<uint8_t> _channelBuffer;
        std::vector<uint8_t> _convertBuffer;
        std::vector<uint8_t> _effectBuffer;
        std::vector<float> _mixBuffer;
        float _limiterGain = 1.0f;
        MixSpatialSpeakerFunc _mixSpatialSpeaker = nullptr;
        std::chrono::steady_clock::time_point _lastCallbackReport{};
        double _callbackTotalMilliseconds = 0.0;
        double _callbackWorstMilliseconds = 0.0;
        uint64_t _callbackCount = 0;

        std::mutex _mutex;

    public:
        ~AudioMixer() override;
        void Init(const char* device) override;
        void Close() override;
        void Lock() override;
        void Unlock() override;
        std::shared_ptr<IAudioChannel> Play(IAudioSource* source, int32_t loop) override;
        void SetVolume(float volume) override;
        SDLAudioSource* AddSource(std::unique_ptr<SDLAudioSource> source);

        const AudioFormat& GetFormat() const;

    private:
        void GetNextAudioChunk(uint8_t* dst, size_t length);
        void MixChannel(ISDLAudioChannel* channel, size_t frames, float masterGain);
        void WriteOutput(uint8_t* dst, size_t frames);
        void RemoveReleasedSources();

        /**
         * Resample the given buffer into _effectBuffer.
         * Assumes that srcBuffer is the same format as _outputFormat.
         */
        size_t ApplyResample(const void* srcBuffer, size_t srcFrames, size_t dstFrames, int32_t channels, double rate);
        size_t PrepareSpatialSamples(
            ISDLAudioChannel* channel, const AudioFormat& streamFormat, size_t frames, double rate);
        float GetVolumeAdjust(const IAudioChannel* channel, float masterGain) const;
        bool Convert(SDL_AudioCVT* cvt, const void* src, size_t len);
    };
} // namespace OpenRCT2::Audio
