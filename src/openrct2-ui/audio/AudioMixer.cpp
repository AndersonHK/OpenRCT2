/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AudioMixer.h"
#include "AudioMixer.SIMD.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <openrct2/Diagnostic.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/audio/SpatialAudio.h>
#include <openrct2/config/Config.h>
#include <openrct2/platform/Platform.h>

using namespace OpenRCT2::Audio;

namespace
{
    constexpr size_t kMaxMixedChannels = 8192;

    void MixSpatialSpeakerScalar(
        float* destination, const int16_t* samples, size_t mixedFrames, size_t interpolationFrames, float oldVolume,
        float newVolume, float startFade, float endFade, float oldSpeakerGain, float speakerGain)
    {
        for (size_t frame = 0; frame < mixedFrames; frame++)
        {
            const auto t = interpolationFrames > 1
                ? static_cast<float>(frame) / static_cast<float>(interpolationFrames - 1)
                : 1.0f;
            const auto volume = std::lerp(oldVolume, newVolume, t) * std::lerp(startFade, endFade, t);
            const auto sample = static_cast<float>(samples[frame]) / 32768.0f;
            destination[frame] += sample * volume * std::lerp(oldSpeakerGain, speakerGain, t);
        }
    }
}

AudioMixer::~AudioMixer()
{
    Close();
}

void AudioMixer::Init(const char* device)
{
    Close();

    SDL_AudioSpec want = {};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 8;
    want.samples = 1024;
    want.callback = [](void* arg, uint8_t* dst, int32_t length) -> void {
        auto* mixer = static_cast<AudioMixer*>(arg);
        mixer->GetNextAudioChunk(dst, static_cast<size_t>(length));
        mixer->RemoveReleasedSources();
    };
    want.userdata = this;

    SDL_AudioSpec have{};
    constexpr auto kAllowedChanges = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
    _deviceId = SDL_OpenAudioDevice(device, 0, &want, &have, kAllowedChanges);
    if (_deviceId == 0)
    {
        LOG_WARNING("Unable to open 7.1 audio output, falling back to stereo: %s", SDL_GetError());
        want.channels = 2;
        have = {};
        _deviceId = SDL_OpenAudioDevice(device, 0, &want, &have, kAllowedChanges);
    }
    if (_deviceId == 0)
    {
        LOG_ERROR("Unable to open audio output: %s", SDL_GetError());
        return;
    }
    _outputFormat.format = have.format;
    _outputFormat.channels = have.channels;
    _outputFormat.freq = have.freq;
    // Keep the legacy 22.05 kHz stereo source representation so saved byte offsets and CSS track lengths remain stable.
    // The callback converts to the negotiated 48 kHz output format before spatial routing.
    _sourceFormat = AudioFormat{ 22050, AUDIO_S16SYS, 2 };
    _limiterGain = 1.0f;
    _mixSpatialSpeaker = Platform::AVX2Available() ? MixSpatialSpeakerAVX2 : MixSpatialSpeakerScalar;
    _channels.reserve(kMaxMixedChannels);
    LOG_VERBOSE("Audio mixer spatial kernel: %s planar", Platform::AVX2Available() ? "AVX2" : "scalar");
    LOG_INFO(
        "Opened %s audio output at %d Hz with %d channels and %d-frame callbacks",
        SDL_GetCurrentAudioDriver() == nullptr ? "unknown" : SDL_GetCurrentAudioDriver(), have.freq, have.channels,
        have.samples);

    SDL_PauseAudioDevice(_deviceId, 0);
}

void AudioMixer::Close()
{
    // Free channels
    Lock();
    _channels.clear();
    Unlock();

    if (_deviceId != 0)
    {
        SDL_CloseAudioDevice(_deviceId);
        _deviceId = 0;
    }

    // Free buffers
    _channelBuffer.clear();
    _convertBuffer.clear();
    _effectBuffer.clear();
    _mixBuffer.clear();
}

void AudioMixer::Lock()
{
    if (_deviceId != 0)
    {
        SDL_LockAudioDevice(_deviceId);
    }
}

void AudioMixer::Unlock()
{
    if (_deviceId != 0)
    {
        SDL_UnlockAudioDevice(_deviceId);
    }
}

std::shared_ptr<IAudioChannel> AudioMixer::Play(IAudioSource* source, int32_t loop)
{
    std::erase_if(_channels, [](const auto& channel) { return channel->IsDone(); });
    if (_channels.size() >= kMaxMixedChannels)
    {
        return nullptr;
    }
    auto channel = std::shared_ptr<ISDLAudioChannel>(AudioChannel::Create());
    channel->Play(source, loop);
    _channels.push_back(channel);
    return channel;
}

void AudioMixer::SetVolume(float volume)
{
    _volume = volume;
}

SDLAudioSource* AudioMixer::AddSource(std::unique_ptr<SDLAudioSource> source)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _sources.push_back(std::move(source));
    return _sources.back().get();
}

void AudioMixer::RemoveReleasedSources()
{
    std::lock_guard<std::mutex> guard(_mutex);
    std::erase_if(_sources, [](const auto& source) { return source->IsReleased(); });
}

const AudioFormat& AudioMixer::GetFormat() const
{
    return _sourceFormat;
}

void AudioMixer::GetNextAudioChunk(uint8_t* dst, size_t length)
{
    const auto& soundConfig = Config::Get().sound;
    const auto updateVolume = [](uint8_t configured, uint8_t& cached, float& adjusted) {
        if (cached != configured)
        {
            cached = configured;
            adjusted = std::pow(static_cast<float>(configured) / 100.0f, 10.0f / 6.0f);
        }
    };
    updateVolume(soundConfig.soundVolume, _settingSoundVolume, _adjustSoundVolume);
    updateVolume(soundConfig.rideMusicVolume, _settingMusicVolume, _adjustMusicVolume);

    const auto frameBytes = static_cast<size_t>(_outputFormat.GetByteRate());
    const auto frames = length / frameBytes;
    _mixBuffer.assign(frames * static_cast<size_t>(_outputFormat.channels), 0.0f);

    const auto masterGain = soundConfig.masterSoundEnabled ? static_cast<float>(soundConfig.masterVolume) / 100.0f : 0.0f;
    std::erase_if(_channels, [&](const auto& channel) {
        const auto* source = channel->GetSource();
        if (source == nullptr || source->IsReleased() || channel->IsDone())
        {
            channel->SetDone(true);
            return true;
        }

        const auto group = channel->GetGroup();
        const auto isSoundEffect = group == MixerGroup::Sound || group == MixerGroup::Vehicle;
        if ((!isSoundEffect || soundConfig.soundEnabled) && masterGain > 0.0f)
        {
            MixChannel(channel.get(), frames, masterGain);
        }
        return channel->IsDone();
    });

    WriteOutput(dst, frames);
    const auto mixedLength = frames * frameBytes;
    if (mixedLength < length)
    {
        std::fill(dst + mixedLength, dst + length, 0);
    }

}

// TODO: investigate replacing this with OpenAL (#26035)
void AudioMixer::MixChannel(ISDLAudioChannel* channel, size_t frames, float masterGain)
{
    const auto rate = channel->GetRate();
    const auto streamFormat = channel->GetFormat();
    AudioFormat mixFormat{ _outputFormat.freq, AUDIO_S16SYS, channel->IsSpatial() ? 1 : std::min(streamFormat.channels, 2) };

    const void* buffer = nullptr;
    size_t availableFrames = 0;
    const auto canUseDirectSpatialPath = channel->IsSpatial() && streamFormat.format == AUDIO_S16SYS
        && streamFormat.channels == 2 && streamFormat.freq > 0;
    if (canUseDirectSpatialPath)
    {
        availableFrames = PrepareSpatialSamples(channel, streamFormat, frames, rate);
        buffer = static_cast<const void*>(_effectBuffer.data());
    }
    else
    {
        bool mustConvert = false;
        SDL_AudioCVT cvt{};
        cvt.len_ratio = 1;
        if (streamFormat != mixFormat)
        {
            if (SDL_BuildAudioCVT(
                    &cvt, streamFormat.format, static_cast<uint8_t>(streamFormat.channels), streamFormat.freq, mixFormat.format,
                    static_cast<uint8_t>(mixFormat.channels), mixFormat.freq)
                == -1)
            {
                return;
            }
            mustConvert = true;
        }

        const auto requiredMixFrames = static_cast<size_t>(std::ceil(static_cast<double>(frames) * rate)) + 2;
        const auto requiredMixBytes = requiredMixFrames * static_cast<size_t>(mixFormat.GetByteRate());
        const auto sourceFrameBytes = static_cast<size_t>(std::max(1, streamFormat.GetByteRate()));
        auto readLength = mustConvert ? static_cast<size_t>(std::ceil(static_cast<double>(requiredMixBytes) / cvt.len_ratio))
                                      : requiredMixBytes;
        readLength = ((readLength + sourceFrameBytes - 1) / sourceFrameBytes) * sourceFrameBytes;
        _channelBuffer.resize(readLength);
        const auto bytesRead = channel->Read(_channelBuffer.data(), readLength);

        size_t bufferLen = 0;
        if (mustConvert)
        {
            if (Convert(&cvt, _channelBuffer.data(), bytesRead))
            {
                buffer = static_cast<const void*>(cvt.buf);
                bufferLen = cvt.len_cvt;
            }
            else
            {
                return;
            }
        }
        else
        {
            buffer = static_cast<const void*>(_channelBuffer.data());
            bufferLen = bytesRead;
        }

        availableFrames = bufferLen / static_cast<size_t>(mixFormat.GetByteRate());
        if (rate != 1)
        {
            availableFrames = ApplyResample(buffer, availableFrames, frames, mixFormat.channels, rate);
            buffer = static_cast<const void*>(_effectBuffer.data());
        }
    }

    const auto mixedFrames = std::min(frames, availableFrames);
    const auto* samples = static_cast<const int16_t*>(buffer);
    const auto outputChannels = static_cast<size_t>(_outputFormat.channels);
    const auto volumeAdjust = GetVolumeAdjust(channel, masterGain) / static_cast<float>(kMixerVolumeMax);
    const auto oldVolume = static_cast<float>(channel->GetOldVolume()) * volumeAdjust;
    const auto newVolume = static_cast<float>(channel->GetVolume()) * volumeAdjust;
    const auto startFade = channel->GetFadeLevel();
    const auto endFade = channel->AdvanceFade(frames, static_cast<uint32_t>(_outputFormat.freq));

    if (!channel->IsSpatial())
    {
        const auto sourceChannels = static_cast<size_t>(mixFormat.channels);
        for (size_t frame = 0; frame < mixedFrames; frame++)
        {
            const auto t = frames > 1 ? static_cast<float>(frame) / static_cast<float>(frames - 1) : 1.0f;
            const auto volume = std::lerp(oldVolume, newVolume, t) * std::lerp(startFade, endFade, t);
            const auto sourceOffset = frame * sourceChannels;
            _mixBuffer[frame] += static_cast<float>(samples[sourceOffset]) / 32768.0f * volume * channel->GetVolumeL();
            if (outputChannels > 1)
            {
                const auto rightOffset = sourceOffset + (sourceChannels > 1 ? 1 : 0);
                _mixBuffer[frames + frame] += static_cast<float>(samples[rightOffset]) / 32768.0f * volume
                    * channel->GetVolumeR();
            }
        }
    }
    else
    {
        const auto speakerGains = CalculateSpeakerGains(
            channel->GetAzimuth(), static_cast<uint8_t>(_outputFormat.channels));
        const auto oldSpeakerGains = CalculateSpeakerGains(
            channel->GetOldAzimuth(), static_cast<uint8_t>(_outputFormat.channels));
        for (size_t outputChannel = 0; outputChannel < outputChannels; outputChannel++)
        {
            if (speakerGains[outputChannel] != 0.0f || oldSpeakerGains[outputChannel] != 0.0f)
            {
                _mixSpatialSpeaker(
                    _mixBuffer.data() + (outputChannel * frames), samples, mixedFrames, frames, oldVolume, newVolume,
                    startFade, endFade, oldSpeakerGains[outputChannel], speakerGains[outputChannel]);
            }
        }
    }

    channel->UpdateOldVolume();
}

void AudioMixer::WriteOutput(uint8_t* dst, size_t frames)
{
    constexpr float kHeadroom = 0.5f;

    float peak = 0.0f;
    for (const auto sample : _mixBuffer)
    {
        peak = std::max(peak, std::abs(sample * kHeadroom));
    }

    _limiterGain = CalculateNextLimiterGain(_limiterGain, peak, frames, static_cast<uint32_t>(_outputFormat.freq));

    auto* output = reinterpret_cast<int16_t*>(dst);
    const auto outputChannels = static_cast<size_t>(_outputFormat.channels);
    for (size_t frame = 0; frame < frames; frame++)
    {
        for (size_t channel = 0; channel < outputChannels; channel++)
        {
            const auto sample = std::clamp(
                _mixBuffer[(channel * frames) + frame] * kHeadroom * _limiterGain, -1.0f, 1.0f);
            output[(frame * outputChannels) + channel] = static_cast<int16_t>(
                std::lround(sample * static_cast<float>(std::numeric_limits<int16_t>::max())));
        }
    }
}

size_t AudioMixer::ApplyResample(const void* srcBuffer, size_t srcFrames, size_t dstFrames, int32_t channels, double rate)
{
    if (srcFrames < 2 || channels <= 0)
        return 0;

    const int16_t* src = static_cast<const int16_t*>(srcBuffer);
    _effectBuffer.resize(dstFrames * static_cast<size_t>(channels) * sizeof(int16_t));
    int16_t* dst = reinterpret_cast<int16_t*>(_effectBuffer.data());

    size_t producedFrames = 0;
    for (; producedFrames < dstFrames; producedFrames++)
    {
        const auto srcPos = static_cast<double>(producedFrames) * rate;
        const auto index = static_cast<size_t>(srcPos);
        if (index + 1 >= srcFrames)
        {
            break;
        }
        const auto fraction = srcPos - static_cast<double>(index);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto baseIndex = (index * static_cast<size_t>(channels)) + static_cast<size_t>(ch);
            const auto sample = std::lerp(
                static_cast<double>(src[baseIndex]), static_cast<double>(src[baseIndex + channels]), fraction);
            dst[(producedFrames * static_cast<size_t>(channels)) + static_cast<size_t>(ch)] = static_cast<int16_t>(
                std::clamp(sample, -32768.0, 32767.0));
        }
    }

    return producedFrames;
}

size_t AudioMixer::PrepareSpatialSamples(
    ISDLAudioChannel* channel, const AudioFormat& streamFormat, size_t frames, double rate)
{
    const auto sourceStep = rate * static_cast<double>(streamFormat.freq) / static_cast<double>(_outputFormat.freq);
    const auto oldRemainder = channel->GetResampleRemainder();
    const auto exactAdvance = oldRemainder + (static_cast<double>(frames) * sourceStep);
    const auto framesToConsume = static_cast<size_t>(std::floor(exactAdvance));
    channel->SetResampleRemainder(exactAdvance - static_cast<double>(framesToConsume));

    constexpr size_t kLookaheadFrames = 2;
    const auto sourceFrameBytes = static_cast<size_t>(streamFormat.GetByteRate());
    _channelBuffer.resize((framesToConsume + kLookaheadFrames) * sourceFrameBytes);
    const auto availableSourceFrames = channel->ReadForResampling(
        _channelBuffer.data(), framesToConsume, kLookaheadFrames);
    if (availableSourceFrames < 2)
    {
        return 0;
    }

    _effectBuffer.resize(frames * sizeof(int16_t));
    const auto* source = reinterpret_cast<const int16_t*>(_channelBuffer.data());
    auto* destination = reinterpret_cast<int16_t*>(_effectBuffer.data());
    size_t producedFrames = 0;
    for (; producedFrames < frames; producedFrames++)
    {
        const auto sourcePosition = oldRemainder + (static_cast<double>(producedFrames) * sourceStep);
        const auto sourceIndex = static_cast<size_t>(sourcePosition);
        if (sourceIndex + 1 >= availableSourceFrames)
        {
            break;
        }
        const auto fraction = sourcePosition - static_cast<double>(sourceIndex);
        const auto firstOffset = sourceIndex * 2;
        const auto secondOffset = firstOffset + 2;
        const auto first = (static_cast<double>(source[firstOffset]) + static_cast<double>(source[firstOffset + 1])) * 0.5;
        const auto second = (static_cast<double>(source[secondOffset]) + static_cast<double>(source[secondOffset + 1])) * 0.5;
        destination[producedFrames] = static_cast<int16_t>(std::clamp(std::lerp(first, second, fraction), -32768.0, 32767.0));
    }
    return producedFrames;
}

float AudioMixer::GetVolumeAdjust(const IAudioChannel* channel, float masterGain) const
{
    float volumeAdjust = _volume * masterGain;

    switch (channel->GetGroup())
    {
        case MixerGroup::Sound:
        case MixerGroup::Vehicle:
            volumeAdjust *= _adjustSoundVolume;

            // Cap sound volume on title screen so music is more audible
            if (gLegacyScene == LegacyScene::titleSequence)
            {
                volumeAdjust = std::min(volumeAdjust, 0.75f);
            }
            break;
        case MixerGroup::RideMusic:
        case MixerGroup::TitleMusic:
            volumeAdjust *= _adjustMusicVolume;
            break;
    }
    return volumeAdjust;
}

bool AudioMixer::Convert(SDL_AudioCVT* cvt, const void* src, size_t len)
{
    // tofix: there seems to be an issue with converting audio using SDL_ConvertAudio in the callback vs preconverted,
    // can cause pops and static depending on sample rate and channels
    if (len == 0 || cvt->len_mult == 0)
    {
        return false;
    }
    _convertBuffer.resize(len * cvt->len_mult);
    std::copy_n(static_cast<const uint8_t*>(src), len, _convertBuffer.data());
    cvt->len = static_cast<int32_t>(len);
    cvt->buf = _convertBuffer.data();
    return SDL_ConvertAudio(cvt) >= 0;
}
