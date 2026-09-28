// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <openrct2/Limits.h>
#include <openrct2/world/WorldObjectPresentation.h>
#include <span>
#include <stdexcept>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    inline void AppendWorldTrackRideWords(std::vector<uint32_t>& words, const WorldRidePresentationMaterials& source)
    {
        if (source.rides.size() > Limits::kMaxRidesInPark)
            throw std::invalid_argument("Track ride publication exceeds the ride domain");
        for (const auto& ride : source.rides)
        {
            words.push_back(ride.present ? 1u : 0u);
            words.push_back(ride.rideType);
            words.push_back(uint32_t(ride.objectSlot) | (uint32_t(ride.stationStyle) << 16));
            words.push_back(0);
            for (const auto& colour : ride.trackColours)
                words.push_back(uint32_t(colour.main) | (uint32_t(colour.additional) << 8) | (uint32_t(colour.supports) << 16));
        }
    }

    // The shader's track buffer retains the same ABI, but its immutable recipe
    // and image banks precede a trailing variable ride table. Only these two
    // bounded regions belong to an instance publication.
    struct WorldTrackInstanceData
    {
        std::array<uint32_t, 16> header{};
        std::vector<uint32_t> rides;

        [[nodiscard]] size_t UploadBytes() const
        {
            return sizeof(header) + rides.size() * sizeof(uint32_t);
        }
        [[nodiscard]] uint32_t RideWordOffset() const
        {
            return header[3];
        }

        void Validate(std::span<const uint32_t> resident) const
        {
            if (resident.empty() && rides.empty()
                && std::all_of(header.begin(), header.end(), [](uint32_t value) { return value == 0; }))
                return;
            if (resident.size() < header.size() || resident[0] != 0x5754524b || resident[11] != resident.size()
                || resident[3] < header.size() || uint64_t(resident[3]) + uint64_t(resident[4]) * 8 != resident.size()
                || rides.size() > Limits::kMaxRidesInPark * 8u || rides.size() != uint64_t(header[4]) * 8
                || uint64_t(header[3]) + rides.size() != header[11])
                throw std::invalid_argument("Track instance publication has an invalid trailing ride table");
            for (size_t i = 0; i < header.size(); ++i)
                if (i != 4 && i != 11 && header[i] != resident[i])
                    throw std::invalid_argument("Track instance publication changed the immutable recipe bank");
        }

        WorldTrackInstanceData() = default;
        WorldTrackInstanceData(const WorldRidePresentationMaterials& source, std::span<const uint32_t> resident)
        {
            if (resident.empty() && source.rides.empty())
                return;
            if (resident.size() < header.size())
                throw std::invalid_argument("Track instance publication has no resident header");
            std::copy_n(resident.begin(), header.size(), header.begin());
            rides.reserve(source.rides.size() * 8);
            AppendWorldTrackRideWords(rides, source);
            header[4] = static_cast<uint32_t>(source.rides.size());
            header[11] = header[3] + static_cast<uint32_t>(rides.size());
            Validate(resident);
        }
    };
} // namespace OpenRCT2::Ui::Gpu
