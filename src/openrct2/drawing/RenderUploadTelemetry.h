/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace OpenRCT2::Drawing
{
    // Exact requested API payload bytes, never physical memory-bus traffic.
    enum class UploadCategory : size_t
    {
        palette,
        lookup,
        atlas,
        lightFx,
        world,
        commands,
        count
    };
    enum class UploadMetric : size_t
    {
        hostWritten,
        bufferTransfer,
        imageTransfer,
        directVertex,
        directStorage,
        count
    };
    constexpr size_t kUploadCategoryCount = static_cast<size_t>(UploadCategory::count);
    constexpr size_t kUploadMetricCount = static_cast<size_t>(UploadMetric::count);
    constexpr std::array<const char*, kUploadCategoryCount> kUploadCategoryNames{ "palette", "lookup", "atlas",
                                                                                  "lightFx", "world",  "commands" };
    using UploadByteMatrix = std::array<std::array<uint64_t, kUploadMetricCount>, kUploadCategoryCount>;

    struct RenderUploadTelemetry
    {
        UploadByteMatrix bytes{};
        uint64_t allocatedBytes{};
        uint64_t alignmentBytes{};
        uint64_t allocationFailures{};
        uint64_t captureRequests{};
        uint64_t readbackRequests{};
        uint64_t readbackBytes{};
        uint64_t lostSamples{};
        bool submitted{};
        bool auxiliary{};
        bool overflow{};

        void Add(uint64_t& target, uint64_t value) noexcept
        {
            if (value > std::numeric_limits<uint64_t>::max() - target)
            {
                target = std::numeric_limits<uint64_t>::max();
                overflow = true;
            }
            else
                target += value;
        }
        void Add(UploadCategory category, UploadMetric metric, uint64_t value) noexcept
        {
            Add(bytes[static_cast<size_t>(category)][static_cast<size_t>(metric)], value);
        }
    };

    struct RenderUploadTotals
    {
        RenderUploadTelemetry attempted;
        UploadByteMatrix submittedBytes{};
        uint64_t attemptedFrames{};
        uint64_t submittedFrames{};
        uint64_t auxiliarySamples{};

        void Include(const RenderUploadTelemetry& sample) noexcept
        {
            attempted.Add(sample.auxiliary ? auxiliarySamples : attemptedFrames, 1);
            if (sample.submitted)
                attempted.Add(submittedFrames, 1);
            for (size_t c = 0; c < kUploadCategoryCount; ++c)
                for (size_t m = 0; m < kUploadMetricCount; ++m)
                {
                    attempted.Add(attempted.bytes[c][m], sample.bytes[c][m]);
                    if (sample.submitted)
                        attempted.Add(submittedBytes[c][m], sample.bytes[c][m]);
                }
            attempted.Add(attempted.allocatedBytes, sample.allocatedBytes);
            attempted.Add(attempted.alignmentBytes, sample.alignmentBytes);
            attempted.Add(attempted.allocationFailures, sample.allocationFailures);
            attempted.Add(attempted.captureRequests, sample.captureRequests);
            attempted.Add(attempted.readbackRequests, sample.readbackRequests);
            attempted.Add(attempted.readbackBytes, sample.readbackBytes);
            attempted.Add(attempted.lostSamples, sample.lostSamples);
            attempted.overflow |= sample.overflow;
        }
    };
} // namespace OpenRCT2::Drawing
