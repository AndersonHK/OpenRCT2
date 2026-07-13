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
#include <cassert>
#include <cstdint>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    struct DamageSnapshot
    {
        uint64_t serial{};
        bool fullRedraw{};
        std::vector<Int4> rectangles;
    };

    /**
     * Tracks screen damage until a render-worker submission has incorporated it into the retained canvas.
     * Latest-frame delivery may discard packets, so traversal deliberately does not clear cells; only an
     * acknowledgement for an applied serial retires damage.
     */
    class DamageTracker final
    {
    private:
        uint32_t _width{};
        uint32_t _height{};
        uint32_t _blockWidth{ 64 };
        uint32_t _blockHeight{ 64 };
        uint32_t _columns{};
        uint32_t _rows{};
        uint64_t _currentSerial{};
        uint64_t _appliedSerial{};
        uint64_t _fullRedrawSerial{};
        size_t _dirtyCellCount{};
        std::vector<uint64_t> _cellSerials;

    public:
        void Reset(uint32_t width, uint32_t height, uint32_t blockWidth = 64, uint32_t blockHeight = 64)
        {
            assert(blockWidth != 0 && blockHeight != 0);
            _width = width;
            _height = height;
            _blockWidth = blockWidth;
            _blockHeight = blockHeight;
            _columns = width == 0 ? 0 : (width + blockWidth - 1) / blockWidth;
            _rows = height == 0 ? 0 : (height + blockHeight - 1) / blockHeight;
            _cellSerials.assign(static_cast<size_t>(_columns) * _rows, 0);
            _dirtyCellCount = 0;
            ForceFullRedraw();
        }

        void Invalidate(int32_t left, int32_t top, int32_t right, int32_t bottom)
        {
            left = std::clamp(left, 0, static_cast<int32_t>(_width));
            top = std::clamp(top, 0, static_cast<int32_t>(_height));
            right = std::clamp(right, 0, static_cast<int32_t>(_width));
            bottom = std::clamp(bottom, 0, static_cast<int32_t>(_height));
            if (left >= right || top >= bottom || _columns == 0 || _rows == 0)
                return;

            const auto serial = NextSerial();
            const uint32_t firstColumn = static_cast<uint32_t>(left) / _blockWidth;
            const uint32_t lastColumn = static_cast<uint32_t>(right - 1) / _blockWidth;
            const uint32_t firstRow = static_cast<uint32_t>(top) / _blockHeight;
            const uint32_t lastRow = static_cast<uint32_t>(bottom - 1) / _blockHeight;
            for (uint32_t row = firstRow; row <= lastRow; row++)
            {
                for (uint32_t column = firstColumn; column <= lastColumn; column++)
                {
                    auto& cellSerial = _cellSerials[static_cast<size_t>(row) * _columns + column];
                    if (cellSerial <= _appliedSerial)
                        _dirtyCellCount++;
                    cellSerial = serial;
                }
            }
        }

        void ForceFullRedraw()
        {
            const auto serial = NextSerial();
            std::fill(_cellSerials.begin(), _cellSerials.end(), serial);
            _dirtyCellCount = _cellSerials.size();
            _fullRedrawSerial = serial;
        }

        void Acknowledge(uint64_t serial) noexcept
        {
            _appliedSerial = std::max(_appliedSerial, std::min(serial, _currentSerial));
            if (!IsFullRedrawPending())
            {
                _dirtyCellCount = static_cast<size_t>(std::count_if(
                    _cellSerials.begin(), _cellSerials.end(), [this](uint64_t cellSerial) {
                        return cellSerial > _appliedSerial;
                    }));
            }
        }

        [[nodiscard]] bool IsFullRedrawPending() const noexcept
        {
            return _fullRedrawSerial > _appliedSerial;
        }

        bool CoalesceFullRedrawInvalidation() noexcept
        {
            if (IsFullRedrawPending())
            {
                _fullRedrawSerial = NextSerial();
                return true;
            }
            if (_dirtyCellCount * 2 <= _cellSerials.size())
                return false;

            // A packet may already have snapshotted the prior serial. Advance the full-redraw serial so acknowledging that
            // packet cannot retire mutations which happened while it was waiting in the render mailbox. Dense sparse damage
            // is promoted only when this call actually elides a newer rectangle, preserving exact partial acknowledgements.
            ForceFullRedraw();
            return true;
        }

        [[nodiscard]] DamageSnapshot Snapshot() const
        {
            DamageSnapshot result{ _currentSerial, _fullRedrawSerial > _appliedSerial, {} };
            if (_columns == 0 || _rows == 0)
                return result;

            if (result.fullRedraw || _dirtyCellCount * 2 > _cellSerials.size())
            {
                // Re-entering the viewport painter for many dirty islands costs more than rebuilding one dense scene.
                // Keep sparse invalidation retained, but cross over before fragmented traversal dominates the main thread.
                result.fullRedraw = true;
                result.rectangles.push_back(
                    { 0, 0, static_cast<int32_t>(_width), static_cast<int32_t>(_height) });
                return result;
            }

            std::vector<uint8_t> consumed(_cellSerials.size());
            for (uint32_t row = 0; row < _rows; row++)
            {
                for (uint32_t column = 0; column < _columns;)
                {
                    const auto index = static_cast<size_t>(row) * _columns + column;
                    if (consumed[index] || _cellSerials[index] <= _appliedSerial)
                    {
                        column++;
                        continue;
                    }

                    uint32_t width = 1;
                    while (column + width < _columns)
                    {
                        const auto next = index + width;
                        if (consumed[next] || _cellSerials[next] <= _appliedSerial)
                            break;
                        width++;
                    }

                    uint32_t height = 1;
                    while (row + height < _rows)
                    {
                        bool completeRow = true;
                        for (uint32_t x = 0; x < width; x++)
                        {
                            const auto next = static_cast<size_t>(row + height) * _columns + column + x;
                            if (consumed[next] || _cellSerials[next] <= _appliedSerial)
                            {
                                completeRow = false;
                                break;
                            }
                        }
                        if (!completeRow)
                            break;
                        height++;
                    }

                    for (uint32_t y = 0; y < height; y++)
                    {
                        std::fill_n(
                            consumed.begin() + static_cast<size_t>(row + y) * _columns + column, width, uint8_t{ 1 });
                    }
                    result.rectangles.push_back({
                        static_cast<int32_t>(column * _blockWidth),
                        static_cast<int32_t>(row * _blockHeight),
                        static_cast<int32_t>(std::min((column + width) * _blockWidth, _width)),
                        static_cast<int32_t>(std::min((row + height) * _blockHeight, _height)),
                    });
                    column += width;
                }
            }
            return result;
        }

        [[nodiscard]] uint64_t GetAppliedSerial() const noexcept
        {
            return _appliedSerial;
        }

    private:
        [[nodiscard]] uint64_t NextSerial() noexcept
        {
            if (++_currentSerial == 0)
            {
                // A practical impossibility, but preserving the ordering invariant is cheaper than allowing ambiguity.
                _currentSerial = 1;
                _appliedSerial = 0;
                std::fill(_cellSerials.begin(), _cellSerials.end(), _currentSerial);
                _fullRedrawSerial = _currentSerial;
            }
            return _currentSerial;
        }
    };
} // namespace OpenRCT2::Ui::Gpu
