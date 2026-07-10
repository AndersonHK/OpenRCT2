/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuCommandStream.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    constexpr int32_t kAtlasDimension = 2048;
    constexpr int32_t kSmallestAtlasSlot = 32;
    constexpr uint32_t kAtlasLayers = 64;

    struct TextureBinding
    {
        uint32_t index = 0;
        Float4 coords{};
    };

    struct TextureLocation : TextureBinding
    {
        uint32_t slot = 0;
        Int4 bounds{};
        uint32_t image = 0;
        uint32_t generation = 0;
    };

    /**
     * One layer of the indexed sprite texture array. Each layer stores one
     * power-of-two slot class so allocation and invalidation remain O(1), and
     * the same metadata can address OpenGL or Vulkan array images.
     */
    class AtlasPage final
    {
    private:
        uint32_t _index = 0;
        int32_t _imageSize = 0;
        int32_t _width = 0;
        int32_t _height = 0;
        int32_t _columns = 0;
        std::vector<uint32_t> _freeSlots;

    public:
        AtlasPage(uint32_t index, int32_t imageSize)
            : _index(index)
            , _imageSize(imageSize)
        {
        }

        void Initialise(int32_t width, int32_t height)
        {
            _width = width;
            _height = height;
            _columns = std::max(1, width / _imageSize);
            const int32_t rows = std::max(1, height / _imageSize);
            _freeSlots.resize(static_cast<size_t>(_columns) * rows);
            for (size_t i = 0; i < _freeSlots.size(); i++)
            {
                _freeSlots[i] = static_cast<uint32_t>(i);
            }
        }

        [[nodiscard]] TextureLocation Allocate(int32_t actualWidth, int32_t actualHeight)
        {
            assert(!_freeSlots.empty());
            const uint32_t slot = _freeSlots.back();
            _freeSlots.pop_back();
            const auto bounds = GetSlotCoordinates(slot, actualWidth, actualHeight);

            TextureLocation result{};
            result.index = _index;
            result.slot = slot;
            result.bounds = bounds;
            result.coords = {
                static_cast<float>(bounds.x),
                static_cast<float>(bounds.y),
                static_cast<float>(_width),
                static_cast<float>(_height),
            };
            return result;
        }

        void Free(const TextureLocation& location)
        {
            assert(_index == location.index);
            _freeSlots.push_back(location.slot);
        }

        [[nodiscard]] bool IsImageSuitable(int32_t actualWidth, int32_t actualHeight) const
        {
            return CalculateImageSizeOrder(actualWidth, actualHeight) == std::countr_zero(static_cast<uint32_t>(_imageSize));
        }

        [[nodiscard]] int32_t GetFreeSlots() const noexcept
        {
            return static_cast<int32_t>(_freeSlots.size());
        }

        [[nodiscard]] static int32_t CalculateImageSizeOrder(int32_t actualWidth, int32_t actualHeight)
        {
            const auto actualSize = static_cast<uint32_t>(std::max(kSmallestAtlasSlot, std::max(actualWidth, actualHeight)));
            return static_cast<int32_t>(std::bit_width(actualSize - 1));
        }

    private:
        [[nodiscard]] Int4 GetSlotCoordinates(uint32_t slot, int32_t actualWidth, int32_t actualHeight) const
        {
            const int32_t row = static_cast<int32_t>(slot) / _columns;
            const int32_t column = static_cast<int32_t>(slot) % _columns;
            return {
                _imageSize * column,
                _imageSize * row,
                _imageSize * column + actualWidth,
                _imageSize * row + actualHeight,
            };
        }
    };
} // namespace OpenRCT2::Ui::Gpu
