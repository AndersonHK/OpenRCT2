/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GpuTransparencyDepth.h"

#include <algorithm>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    namespace
    {
        struct Event
        {
            int32_t x;
            int32_t top;
            int32_t bottom;
            int32_t delta;
        };

        class RangeMaximum final
        {
        private:
            std::vector<int32_t> _maximum;
            std::vector<int32_t> _lazy;
            size_t _segmentCount = 0;

            void Add(size_t node, size_t left, size_t right, size_t queryLeft, size_t queryRight, int32_t delta)
            {
                if (queryLeft <= left && right <= queryRight)
                {
                    _maximum[node] += delta;
                    _lazy[node] += delta;
                    return;
                }

                const size_t middle = (left + right) / 2;
                if (queryLeft <= middle)
                    Add(node * 2, left, middle, queryLeft, queryRight, delta);
                if (queryRight > middle)
                    Add(node * 2 + 1, middle + 1, right, queryLeft, queryRight, delta);
                _maximum[node] = _lazy[node] + std::max(_maximum[node * 2], _maximum[node * 2 + 1]);
            }

        public:
            void Reset(size_t segmentCount)
            {
                _segmentCount = segmentCount;
                _maximum.resize(segmentCount * 4);
                _lazy.resize(segmentCount * 4);
                std::fill(_maximum.begin(), _maximum.end(), 0);
                std::fill(_lazy.begin(), _lazy.end(), 0);
            }

            void Add(size_t left, size_t right, int32_t delta)
            {
                Add(1, 0, _segmentCount - 1, left, right, delta);
            }

            [[nodiscard]] int32_t Maximum() const noexcept
            {
                return _maximum[1];
            }
        };

        struct Scratch
        {
            std::vector<Event> events;
            std::vector<int32_t> yCoordinates;
            RangeMaximum active;
        };
    } // namespace

    uint32_t MaxTransparencyDepth(const CommandBatch<RectCommand>& commands)
    {
        // Transparency is evaluated on the render worker. Thread-local scratch
        // retains the EverythingPark-sized allocations without shared state.
        thread_local Scratch scratch;
        auto& events = scratch.events;
        auto& yCoordinates = scratch.yCoordinates;
        events.clear();
        yCoordinates.clear();
        events.reserve(commands.size() * 2);
        yCoordinates.reserve(commands.size() * 2);

        for (const auto& command : commands)
        {
            const int32_t left = std::max(command.bounds.x, command.clip.x);
            const int32_t top = std::max(command.bounds.y, command.clip.y);
            const int32_t right = std::min(command.bounds.z, command.clip.z);
            const int32_t bottom = std::min(command.bounds.w, command.clip.w);
            if (left >= right || top >= bottom)
                continue;

            events.push_back({ left, top, bottom, 1 });
            events.push_back({ right, top, bottom, -1 });
            yCoordinates.push_back(top);
            yCoordinates.push_back(bottom);
        }

        if (events.empty())
            return 1;

        std::sort(yCoordinates.begin(), yCoordinates.end());
        yCoordinates.erase(std::unique(yCoordinates.begin(), yCoordinates.end()), yCoordinates.end());
        std::sort(events.begin(), events.end(), [](const Event& left, const Event& right) { return left.x < right.x; });

        auto& active = scratch.active;
        active.Reset(yCoordinates.size() - 1);
        int32_t maximum = 1;
        for (size_t first = 0; first < events.size();)
        {
            size_t last = first;
            while (last < events.size() && events[last].x == events[first].x)
            {
                const auto& event = events[last];
                const size_t top = std::lower_bound(yCoordinates.begin(), yCoordinates.end(), event.top) - yCoordinates.begin();
                const size_t bottom = std::lower_bound(yCoordinates.begin(), yCoordinates.end(), event.bottom)
                    - yCoordinates.begin();
                active.Add(top, bottom - 1, event.delta);
                ++last;
            }
            maximum = std::max(maximum, active.Maximum());
            first = last;
        }
        return static_cast<uint32_t>(maximum);
    }
} // namespace OpenRCT2::Ui::Gpu
