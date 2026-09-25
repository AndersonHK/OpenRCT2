// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "../world/Location.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace OpenRCT2::Drawing
{
    // Raw tool state only. Image, slope, palette and projection decisions belong to the GPU.
    constexpr uint32_t kWorldSelectionHeaderWords = 16;
    constexpr uint32_t kWorldSelectionMaximumTiles = 512 * 512;
    inline void ValidateWorldSelectionWords(std::span<const uint32_t> words)
    {
        if (words.size() < kWorldSelectionHeaderWords || words.size() > kWorldSelectionHeaderWords + kWorldSelectionMaximumTiles
            || words[0] > 15 || words[1] > 14 || ((words[0] & 4) != 0 && words[9] > 7)
            || words[10] != words.size() - kWorldSelectionHeaderWords)
            throw std::invalid_argument("Invalid world selection payload");
        // The final five header words own cutaway state independently of tool flags.
        if (words[11] > 255 * kCoordsZStep || static_cast<int32_t>(words[12]) > static_cast<int32_t>(words[14])
            || static_cast<int32_t>(words[13]) > static_cast<int32_t>(words[15]))
            throw std::invalid_argument("Invalid world clip state");
        for (size_t i = 12; i < kWorldSelectionHeaderWords; ++i)
            if (static_cast<int32_t>(words[i]) < INT16_MIN || static_cast<int32_t>(words[i]) > INT16_MAX)
                throw std::invalid_argument("World clip coordinate exceeds source range");
        for (size_t i = kWorldSelectionHeaderWords; i < words.size(); ++i)
            if ((words[i] & 65535) >= 512 || (words[i] >> 16) >= 512
                || (i > kWorldSelectionHeaderWords && words[i - 1] >= words[i]))
                throw std::invalid_argument("Invalid world construction tile list");
    }
    inline std::vector<uint32_t> MakeWorldSelectionWords(
        uint32_t flags, uint32_t type, CoordsXY first, CoordsXY last, CoordsXYZ arrow, uint32_t direction,
        std::span<const CoordsXY> selected, uint8_t clipHeight = 0, CoordsXY clipFirst = {}, CoordsXY clipLast = {})
    {
        if (flags > 15 || type > 14 || ((flags & 4) != 0 && direction > 7) || selected.size() > kWorldSelectionMaximumTiles)
            throw std::invalid_argument("Invalid world selection state");
        std::vector<uint32_t> result(kWorldSelectionHeaderWords);
        result[0] = flags;
        result[1] = type;
        result[2] = static_cast<uint32_t>(first.x);
        result[3] = static_cast<uint32_t>(first.y);
        result[4] = static_cast<uint32_t>(last.x);
        result[5] = static_cast<uint32_t>(last.y);
        result[6] = static_cast<uint32_t>(arrow.x);
        result[7] = static_cast<uint32_t>(arrow.y);
        result[8] = static_cast<uint32_t>(arrow.z);
        result[9] = direction;
        result[11] = static_cast<uint32_t>(clipHeight) * kCoordsZStep;
        result[12] = static_cast<uint32_t>(clipFirst.x);
        result[13] = static_cast<uint32_t>(clipFirst.y);
        result[14] = static_cast<uint32_t>(clipLast.x);
        result[15] = static_cast<uint32_t>(clipLast.y);
        if ((flags & 2) != 0)
        {
            result.reserve(kWorldSelectionHeaderWords + selected.size());
            for (const auto& tile : selected)
                if (tile.x >= 0 && tile.y >= 0 && tile.x < 512 * 32 && tile.y < 512 * 32)
                    result.push_back((static_cast<uint32_t>(tile.y / 32) << 16) | static_cast<uint32_t>(tile.x / 32));
            std::sort(result.begin() + kWorldSelectionHeaderWords, result.end());
            result.erase(std::unique(result.begin() + kWorldSelectionHeaderWords, result.end()), result.end());
        }
        result[10] = static_cast<uint32_t>(result.size() - kWorldSelectionHeaderWords);
        ValidateWorldSelectionWords(result);
        return result;
    }
} // namespace OpenRCT2::Drawing
