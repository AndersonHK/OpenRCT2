// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "NativeTrackRecipes.h"

#include <algorithm>
#include <bit>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <zlib.h>

namespace OpenRCT2::Drawing
{
    namespace
    {
#include "NativeTrackRecipeData.inc"
#include "NativeTrackSupportData.inc"
        static_assert(kNativeTrackRecipeWordCount >= 8 && kNativeTrackRecipeWordCount <= 16 * 1024 * 1024);

        std::vector<uint32_t> LoadRecipes()
        {
            std::vector<uint32_t> words(kNativeTrackRecipeWordCount);
            uLongf size = static_cast<uLongf>(words.size() * sizeof(uint32_t));
            uLong compressedSize = static_cast<uLong>(std::size(kNativeTrackRecipeCompressed));
            const auto status = uncompress2(
                reinterpret_cast<Bytef*>(words.data()), &size, kNativeTrackRecipeCompressed, &compressedSize);
            if (status != Z_OK || size != words.size() * sizeof(uint32_t)
                || compressedSize != std::size(kNativeTrackRecipeCompressed))
                throw std::runtime_error("Native track definitions failed bounded decompression");
            if constexpr (std::endian::native == std::endian::big)
                for (auto& word : words)
                    word = (word >> 24) | ((word >> 8) & 0xff00u) | ((word << 8) & 0xff0000u) | (word << 24);
            if (words[0] != 0x5452434bu || words[1] != 1 || words[2] != 81 || words[3] != 350 || words[4] != 8
                || words[5] != 8 + uint64_t(words[2]) * words[3] * 3 || words[5] > words[6] || words[6] > words.size()
                || words[7] != words.size() || (words[6] - words[5]) % 2 != 0 || (words.size() - words[6]) % 12 != 0)
                throw std::runtime_error("Native track definition header is invalid");
            const auto rows = (words[6] - words[5]) / 2;
            const auto parts = (words.size() - words[6]) / 12;
            for (size_t d = words[4]; d < words[5]; d += 3)
            {
                const auto sequences = words[d + 1], mask = words[d + 2];
                if (sequences > 16 || mask >= 128
                    || uint64_t(words[d]) + (uint64_t(1) << std::popcount(mask)) * sequences * 4 > rows)
                    throw std::runtime_error("Native track definition descriptor is invalid");
            }
            for (size_t row = words[5]; row < words[6]; row += 2)
            {
                const auto first = words[row], count = words[row + 1];
                if (count > 16 || uint64_t(first) + count > parts)
                    throw std::runtime_error("Native track definition row is invalid");
                uint32_t expandedCount = count, parentCount = 0;
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto p = words[6] + (first + i) * 12;
                    const auto parent = static_cast<int32_t>(words[p + 11]);
                    const auto colourRole = words[p + 10] & 7u;
                    const bool frontComponent = (words[p + 10] & 8u) != 0;
                    if ((words[p] >= 0x7ffffu && !IsNativeTrackAnimatedImage(words[p]) && words[p] != 0xfffffffeu
                         && words[p] != 0xfffffffdu && words[p] != 0xfffffffcu)
                        || (words[p + 10] & ~15u) != 0 || colourRole > 5
                        || (frontComponent && (colourRole >= 4 || words[p] >= 0xfffffffcu))
                        || (parent != -1 && (parent < 0 || static_cast<uint32_t>(parent) >= i)))
                        throw std::runtime_error("Native track definition component is invalid");
                    if (colourRole >= 4 && (words[p] != 0 || parent < 0))
                        throw std::runtime_error("Native track water component is invalid");
                    if (words[p] == 0xfffffffdu)
                    {
                        if (words[p + 1] > 2 || (words[p + 2] >= 26 && (words[p + 2] < 256 || words[p + 2] >= 264))
                            || parent != -1)
                            throw std::runtime_error("Native track tunnel request is invalid");
                        --expandedCount;
                        continue;
                    }
                    if (words[p] == 0xfffffffcu)
                    {
                        if (words[p + 1] >= 4 || words[p + 2] > 1 || words[p + 10] != 2 || parent != -1)
                            throw std::runtime_error("Native track photo request is invalid");
                        expandedCount += 2;
                        parentCount += 2;
                    }
                    parentCount += parent == -1;
                    if (words[p] == 0xfffffffeu)
                    {
                        if (words[p + 8] > 8 || parent != -1)
                            throw std::runtime_error("Native track station request is invalid");
                        if (words[p + 8] == 7)
                        {
                            if (words[p + 1] > 3 || words[p + 2] > 2 || words[p + 4] != 0 || words[p + 5] != 0
                                || words[p + 6] != 0 || words[p + 7] != 0 || words[p + 9] != 0 || words[p + 10] != 0)
                                throw std::runtime_error("Native track single cover request is invalid");
                            ++expandedCount; // Opaque parent plus optional glass child.
                        }
                        else if (words[p + 8] == 8)
                        {
                            if (words[p + 1] != 0 || words[p + 2] != 0 || words[p + 4] != 0 || words[p + 5] != 0
                                || words[p + 6] != 0 || words[p + 7] != 0 || words[p + 9] != 0 || words[p + 10] != 0)
                                throw std::runtime_error("Native chairlift station request is invalid");
                            expandedCount += 10;
                            parentCount += 7;
                        }
                        else
                        {
                            expandedCount += 8;
                            parentCount += 6;
                        }
                    }
                }
                if (expandedCount > 16 || parentCount > 12)
                    throw std::runtime_error("Native track expanded component capacity is invalid");
            }
            return words;
        }
    } // namespace

    std::span<const uint32_t> GetNativeTrackRecipeWords()
    {
        static const auto words = LoadRecipes();
        return words;
    }

    std::span<const uint32_t> GetNativeTrackRecipeImages()
    {
        static const auto images = [] {
            std::vector<uint32_t> result;
            const auto words = GetNativeTrackRecipeWords();
            for (size_t index = words[6]; index < words.size(); index += 12)
            {
                if ((words[index + 10] & 7u) >= 4)
                    continue; // Symbolic water uses the already-resident terrain banks.
                if (words[index] == 0xfffffffcu)
                {
                    const uint32_t first = words[index + 2] != 0 ? 23485u : 25615u;
                    for (uint32_t image = first; image < first + 12; ++image)
                        result.push_back(image);
                }
                if (words[index] != 0xfffffffeu && words[index] != 0xfffffffdu && words[index] != 0xfffffffcu)
                    for (uint32_t frame = 0; frame < GetNativeTrackImageFrameCount(words[index]); ++frame)
                        result.push_back(GetNativeTrackImageAtTick(words[index], 0) + frame);
                if (words[index] == 0xfffffffdu && words[index + 1] == 2)
                    for (uint32_t vertical = 1575; vertical <= 1578; ++vertical)
                        result.push_back(vertical);
            }
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }();
        return images;
    }

    std::span<const uint32_t> GetNativeTrackSupportWords()
    {
        static const auto words = [] {
            static_assert(kNativeTrackSupportWordCount >= 8 && kNativeTrackSupportWordCount <= 16 * 1024 * 1024);
            std::vector<uint32_t> result(kNativeTrackSupportWordCount);
            uLongf size = static_cast<uLongf>(result.size() * sizeof(uint32_t));
            uLong compressedSize = static_cast<uLong>(std::size(kNativeTrackSupportCompressed));
            if (uncompress2(reinterpret_cast<Bytef*>(result.data()), &size, kNativeTrackSupportCompressed, &compressedSize)
                    != Z_OK
                || size != result.size() * sizeof(uint32_t) || compressedSize != std::size(kNativeTrackSupportCompressed))
                throw std::runtime_error("Native track supports failed bounded decompression");
            if constexpr (std::endian::native == std::endian::big)
                for (auto& word : result)
                    word = (word >> 24) | ((word >> 8) & 0xff00u) | ((word << 8) & 0xff0000u) | (word << 24);
            if (result[0] != 0x54535054u || result[1] != 1 || result[2] != 81 || result[3] != 350 || result[4] != 8
                || result[5] != 8 + 81 * 350 * 3 || result[5] > result[6] || result[6] > result.size()
                || result[7] != result.size() || (result[6] - result[5]) % 2 != 0 || (result.size() - result[6]) % 12 != 0)
                throw std::runtime_error("Native track support header is invalid");
            const auto rows = (result[6] - result[5]) / 2;
            const auto operations = (result.size() - result[6]) / 12;
            for (size_t d = result[4]; d < result[5]; d += 3)
            {
                const auto sequences = result[d + 1], mask = result[d + 2];
                if (sequences > 16 || mask >= 128
                    || uint64_t(result[d]) + (uint64_t(1) << std::popcount(mask)) * sequences * 4 > rows)
                    throw std::runtime_error("Native track support descriptor is invalid");
            }
            for (size_t r = result[5]; r < result[6]; r += 2)
                if (result[r + 1] > 64 || uint64_t(result[r]) + result[r + 1] > operations)
                    throw std::runtime_error("Native track support row is invalid");
            for (size_t p = result[6]; p < result.size(); p += 12)
            {
                const auto opcode = result[p];
                const auto height = static_cast<int32_t>(result[p + 4]);
                const auto extra = static_cast<int32_t>(result[p + 5]);
                if (opcode < 1 || opcode > 6 || result[p + 8] > 2 || result[p + 9] > 3 || result[p + 10] > 64 || height < -65535
                    || height > 65535 || extra < -65535 || extra > 65535)
                    throw std::runtime_error("Native track support operation is invalid");
                if (opcode <= 2)
                {
                    if ((result[p + 1] >= 8 && result[p + 1] != 255) || result[p + 2] >= 9 || result[p + 3] > 4
                        || result[p + 6] != 0 || result[p + 7] != 0 || result[p + 11] > 1
                        || (result[p + 11] != 0 && result[p + 3] == 4))
                        throw std::runtime_error("Native track metal support operation is invalid");
                }
                else if (opcode >= 5)
                {
                    if ((result[p + 1] > 1 && result[p + 1] != 255) || result[p + 2] > 5 || result[p + 3] > 3
                        || (result[p + 5] > 20 && result[p + 5] != 255) || result[p + 7] != 0 || (result[p + 11] & ~6u) != 0
                        || result[p + 6] >= 64 || ((result[p + 11] & 4u) == 0 && result[p + 6] != 0))
                        throw std::runtime_error("Native track wooden support operation is invalid");
                }
                else if (
                    result[p + 1] != 0 || result[p + 2] != 0 || result[p + 3] != 4 || extra != 0 || result[p + 6] > 511
                    || result[p + 7] > 255 || result[p + 11] != 0 || (opcode == 4 && result[p + 6] != 0))
                    throw std::runtime_error("Native track support state operation is invalid");
            }
            return result;
        }();
        return words;
    }
} // namespace OpenRCT2::Drawing
