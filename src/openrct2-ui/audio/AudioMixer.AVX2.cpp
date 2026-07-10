/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AudioMixer.SIMD.h"

#include <cmath>

#ifdef __AVX2__
    #include <immintrin.h>
#endif

namespace OpenRCT2::Audio
{
    void MixSpatialSpeakerAVX2(
        float* destination, const int16_t* samples, size_t mixedFrames, size_t interpolationFrames, float oldVolume,
        float newVolume, float startFade, float endFade, float oldSpeakerGain, float speakerGain)
    {
        size_t frame = 0;
#ifdef __AVX2__
        const auto inverseFrames = interpolationFrames > 1 ? 1.0f / static_cast<float>(interpolationFrames - 1) : 0.0f;
        const auto oldVolumeVector = _mm256_set1_ps(oldVolume);
        const auto volumeDeltaVector = _mm256_set1_ps(newVolume - oldVolume);
        const auto startFadeVector = _mm256_set1_ps(startFade);
        const auto fadeDeltaVector = _mm256_set1_ps(endFade - startFade);
        const auto oldSpeakerVector = _mm256_set1_ps(oldSpeakerGain);
        const auto speakerDeltaVector = _mm256_set1_ps(speakerGain - oldSpeakerGain);
        const auto inverseFramesVector = _mm256_set1_ps(inverseFrames);
        const auto sampleScale = _mm256_set1_ps(1.0f / 32768.0f);
        const auto one = _mm256_set1_ps(1.0f);

        for (; frame + 8 <= mixedFrames; frame += 8)
        {
            const auto indices = _mm256_setr_ps(
                static_cast<float>(frame), static_cast<float>(frame + 1), static_cast<float>(frame + 2),
                static_cast<float>(frame + 3), static_cast<float>(frame + 4), static_cast<float>(frame + 5),
                static_cast<float>(frame + 6), static_cast<float>(frame + 7));
            const auto t = interpolationFrames > 1 ? _mm256_mul_ps(indices, inverseFramesVector) : one;
            const auto volume = _mm256_add_ps(oldVolumeVector, _mm256_mul_ps(volumeDeltaVector, t));
            const auto fade = _mm256_add_ps(startFadeVector, _mm256_mul_ps(fadeDeltaVector, t));
            const auto speaker = _mm256_add_ps(oldSpeakerVector, _mm256_mul_ps(speakerDeltaVector, t));

            const auto packedSamples = _mm_loadu_si128(reinterpret_cast<const __m128i*>(samples + frame));
            const auto sampleIntegers = _mm256_cvtepi16_epi32(packedSamples);
            const auto sampleFloats = _mm256_mul_ps(_mm256_cvtepi32_ps(sampleIntegers), sampleScale);
            const auto contribution = _mm256_mul_ps(sampleFloats, _mm256_mul_ps(volume, _mm256_mul_ps(fade, speaker)));
            const auto current = _mm256_loadu_ps(destination + frame);
            _mm256_storeu_ps(destination + frame, _mm256_add_ps(current, contribution));
        }
        _mm256_zeroupper();
#endif
        for (; frame < mixedFrames; frame++)
        {
            const auto t = interpolationFrames > 1
                ? static_cast<float>(frame) / static_cast<float>(interpolationFrames - 1)
                : 1.0f;
            const auto volume = std::lerp(oldVolume, newVolume, t) * std::lerp(startFade, endFade, t);
            destination[frame] += static_cast<float>(samples[frame]) / 32768.0f * volume
                * std::lerp(oldSpeakerGain, speakerGain, t);
        }
    }
} // namespace OpenRCT2::Audio
