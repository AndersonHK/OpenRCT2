/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <memory>
#include <openrct2/audio/AudioChannel.h>
#include <openrct2/audio/AudioSource.h>
#include <string>

struct SDL_RWops;

namespace OpenRCT2::Audio
{
    struct AudioFormat;
    struct IAudioContext;

    struct ISDLAudioChannel : public IAudioChannel
    {
        [[nodiscard]] virtual AudioFormat GetFormat() const = 0;
        [[nodiscard]] virtual float GetFadeLevel() const = 0;
        virtual float AdvanceFade(size_t frames, uint32_t sampleRate) = 0;
        [[nodiscard]] virtual double GetResampleRemainder() const = 0;
        virtual void SetResampleRemainder(double value) = 0;
        [[nodiscard]] virtual float GetLowPassState() const = 0;
        virtual void SetLowPassState(float value) = 0;
        [[nodiscard]] virtual bool IsLowPassInitialised() const = 0;
        virtual void SetLowPassInitialised(bool value) = 0;
        virtual size_t ReadForResampling(void* dst, size_t framesToConsume, size_t lookaheadFrames) = 0;
    };

    namespace AudioChannel
    {
        ISDLAudioChannel* Create();
    }

    [[nodiscard]] std::unique_ptr<IAudioContext> CreateAudioContext();

} // namespace OpenRCT2::Audio
