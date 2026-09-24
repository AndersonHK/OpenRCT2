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
                if (sequences > 16 || mask >= 8
                    || uint64_t(words[d]) + (uint64_t(1) << std::popcount(mask)) * sequences * 4 > rows)
                    throw std::runtime_error("Native track definition descriptor is invalid");
            }
            for (size_t row = words[5]; row < words[6]; row += 2)
            {
                const auto first = words[row], count = words[row + 1];
                if (count > 64 || uint64_t(first) + count > parts)
                    throw std::runtime_error("Native track definition row is invalid");
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto p = words[6] + (first + i) * 12;
                    const auto parent = static_cast<int32_t>(words[p + 11]);
                    if (words[p] >= 0x7ffffu || words[p + 10] > 1
                        || (parent != -1 && (parent < 0 || static_cast<uint32_t>(parent) >= i)))
                        throw std::runtime_error("Native track definition component is invalid");
                }
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
                result.push_back(words[index]);
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }();
        return images;
    }
} // namespace OpenRCT2::Drawing
