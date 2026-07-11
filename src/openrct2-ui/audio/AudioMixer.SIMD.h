/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

namespace OpenRCT2::Audio
{
    using MixSpatialSpeakerFunc = void (*)(
        float* destination, const int16_t* samples, size_t mixedFrames, size_t interpolationFrames, float oldVolume,
        float newVolume, float startFade, float endFade, float oldSpeakerGain, float speakerGain);

    void MixSpatialSpeakerAVX2(
        float* destination, const int16_t* samples, size_t mixedFrames, size_t interpolationFrames, float oldVolume,
        float newVolume, float startFade, float endFade, float oldSpeakerGain, float speakerGain);
}
