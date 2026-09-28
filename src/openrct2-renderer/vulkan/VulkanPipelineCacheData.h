/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <openrct2/core/Crypt.h>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

namespace OpenRCT2::Ui::Vulkan
{
    constexpr size_t kMaximumPipelineCacheBytes = 64 * 1024 * 1024;
    struct PipelineCacheFileHeader
    {
        uint64_t magic = 0x3145484341434B56; // VKCACHE1, also rejects different byte order.
        uint32_t driverVersion{};
        uint32_t bytes{};
        std::array<uint8_t, 32> digest{};
    };
    static_assert(sizeof(PipelineCacheFileHeader) == 48);

    inline bool PipelineCacheMatchesDevice(std::span<const std::byte> bytes, const VkPhysicalDeviceProperties& device)
    {
        if (bytes.size() < sizeof(VkPipelineCacheHeaderVersionOne) || bytes.size() > kMaximumPipelineCacheBytes)
            return false;
        VkPipelineCacheHeaderVersionOne header{};
        std::memcpy(&header, bytes.data(), sizeof(header));
        return header.headerSize == sizeof(header) && header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
            && header.vendorID == device.vendorID && header.deviceID == device.deviceID
            && std::equal(std::begin(header.pipelineCacheUUID), std::end(header.pipelineCacheUUID), device.pipelineCacheUUID);
    }

    inline std::span<const std::byte> DecodePipelineCacheFile(
        std::span<const std::byte> bytes, const VkPhysicalDeviceProperties& device)
    {
        if (bytes.size() < sizeof(PipelineCacheFileHeader))
            return {};
        PipelineCacheFileHeader header{};
        std::memcpy(&header, bytes.data(), sizeof(header));
        auto payload = bytes.subspan(sizeof(header));
        if (header.magic != PipelineCacheFileHeader{}.magic || header.driverVersion != device.driverVersion
            || header.bytes != payload.size() || !PipelineCacheMatchesDevice(payload, device)
            || header.digest != Crypt::SHA256(payload.data(), payload.size()))
            return {};
        return payload;
    }

    inline std::vector<std::byte> EncodePipelineCacheFile(
        std::span<const std::byte> payload, const VkPhysicalDeviceProperties& device)
    {
        if (!PipelineCacheMatchesDevice(payload, device))
            return {};
        PipelineCacheFileHeader header{};
        header.driverVersion = device.driverVersion;
        header.bytes = static_cast<uint32_t>(payload.size());
        header.digest = Crypt::SHA256(payload.data(), payload.size());
        std::vector<std::byte> result(sizeof(header) + payload.size());
        std::memcpy(result.data(), &header, sizeof(header));
        std::memcpy(result.data() + sizeof(header), payload.data(), payload.size());
        return result;
    }
} // namespace OpenRCT2::Ui::Vulkan
