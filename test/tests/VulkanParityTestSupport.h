/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <openrct2-renderer/gpu/GpuBackend.h>
#include <openrct2/core/Imaging.h>
#include <openrct2/core/Json.hpp>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace VulkanParitySupport
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    inline uint64_t HashFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Cannot hash fixture input: " + path.string());
        uint64_t hash = 14695981039346656037ULL;
        char value;
        while (stream.get(value))
            hash = (hash ^ static_cast<uint8_t>(value)) * 1099511628211ULL;
        return hash;
    }

    inline std::vector<std::byte> Expand(std::span<const std::byte> indices, const std::array<std::byte, 1024>& palette)
    {
        std::vector<std::byte> rgba(indices.size() * 4);
        for (size_t i = 0; i < indices.size(); i++)
            std::copy_n(palette.begin() + std::to_integer<uint8_t>(indices[i]) * 4, 4, rgba.begin() + i * 4);
        return rgba;
    }

    inline void SaveRgba(const std::filesystem::path& path, std::span<const std::byte> rgba, Gpu::Extent extent)
    {
        Image image{ .Width = extent.width, .Height = extent.height, .Depth = 32 };
        image.Stride = extent.width * 4;
        image.Pixels.resize(rgba.size());
        std::transform(
            rgba.begin(), rgba.end(), image.Pixels.begin(), [](std::byte value) { return std::to_integer<uint8_t>(value); });
        OpenRCT2::Imaging::WriteToFile(path.string(), image, ImageFormat::png);
    }

    inline size_t CompareAndReport(
        const std::filesystem::path& directory, std::string_view fixture, std::string_view layer,
        std::span<const std::byte> expected, std::span<const std::byte> actual, size_t channels,
        const std::array<std::byte, 1024>& palette, json_t metadata, Gpu::Extent extent)
    {
        if ((channels != 1 && channels != 4) || expected.size() != actual.size()
            || expected.size() != static_cast<size_t>(extent.width) * extent.height * channels)
            throw std::invalid_argument("Pixel comparison dimensions and byte lengths disagree");
        size_t count = 0;
        size_t first = 0;
        uint32_t left = extent.width, top = extent.height, right = 0, bottom = 0;
        std::array<uint8_t, 4> maxError{};
        std::vector<std::byte> difference((static_cast<size_t>(extent.width) * extent.height) * 4, std::byte{ 0 });
        for (size_t i = 0; i < (static_cast<size_t>(extent.width) * extent.height); i++)
        {
            bool different = false;
            for (size_t c = 0; c < channels; c++)
            {
                const auto error = static_cast<uint8_t>(std::abs(
                    std::to_integer<int>(expected[i * channels + c]) - std::to_integer<int>(actual[i * channels + c])));
                maxError[c] = std::max(maxError[c], error);
                different |= error != 0;
            }
            difference[i * 4 + 3] = std::byte{ 255 };
            if (!different)
                continue;
            if (count++ == 0)
                first = i;
            const auto x = static_cast<uint32_t>(i % extent.width);
            const auto y = static_cast<uint32_t>(i / extent.width);
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
            difference[i * 4] = difference[i * 4 + 2] = std::byte{ 255 };
        }
        if (!directory.empty())
        {
            const auto output = directory / fixture / layer;
            std::filesystem::create_directories(output);
            const auto expectedRgba = channels == 1 ? Expand(expected, palette) : std::vector(expected.begin(), expected.end());
            const auto actualRgba = channels == 1 ? Expand(actual, palette) : std::vector(actual.begin(), actual.end());
            const auto referenceLabel = metadata.value("referenceImageLabel", std::string("software"));
            const auto resultLabel = metadata.value("resultImageLabel", std::string("vulkan"));
            SaveRgba(output / (referenceLabel + ".png"), expectedRgba, extent);
            SaveRgba(output / (resultLabel + ".png"), actualRgba, extent);
            SaveRgba(output / "diff.png", difference, extent);
            for (const auto& entry :
                 { std::pair{ referenceLabel + ".bin", expected }, std::pair{ resultLabel + ".bin", actual } })
            {
                std::ofstream bytes(output / entry.first, std::ios::binary);
                bytes.exceptions(std::ios::badbit | std::ios::failbit);
                bytes.write(reinterpret_cast<const char*>(entry.second.data()), entry.second.size());
            }
            metadata["fixture"] = fixture;
            metadata["fixtureVersion"] = metadata.value("fixtureVersion", 1);
            metadata["layer"] = layer;
            metadata["width"] = extent.width;
            metadata["height"] = extent.height;
            metadata["differingPixels"] = count;
            metadata["firstMismatch"] = count == 0 ? json_t(nullptr) : json_t{ first % extent.width, first / extent.width };
            metadata["boundsInclusive"] = count == 0 ? json_t(nullptr) : json_t{ left, top, right, bottom };
            metadata["maxChannelError"] = maxError;
            metadata["acceptedExceptions"] = json_t::array();
            metadata["visualReview"] = count == 0 ? "not-required" : "required";
            OpenRCT2::Json::WriteToFile((output / "report.json").string(), metadata);
        }
        return count;
    }

} // namespace VulkanParitySupport
