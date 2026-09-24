// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "GpuCommandStream.h"
#include <openrct2/drawing/SelectedVehicleSnapshot.h>
#include <stdexcept>

namespace OpenRCT2::Ui::Gpu
{
    constexpr uint32_t kSelectedVehiclePaintMagic = 0x56504831;
    constexpr uint32_t kSelectedVehiclePaintVersion = 1;
    constexpr uint32_t kSelectedVehiclePaintHeaderWords = 16;
    constexpr uint32_t kSelectedVehicleMaximumCars = 255;
    constexpr uint32_t kSelectedVehicleMaximumComponents = 16384;
    struct SelectedVehicleCarRecord
    {
        uint32_t entityId{}, generation{}, firstComponent{}, componentCount{};
        uint32_t tileIndex{}, reserved0{}, reserved1{}, reserved2{};
        Int4 coarseCull{};
    };
    struct SelectedVehicleParentRecord
    {
        int32_t x{}, y{}, z{}, xe{}, ye{}, ze{};
        uint32_t parentComponent{}, spriteSetIndex{}, flags{}, tileIndex{}, reserved0{}, reserved1{};
    };
    static_assert(sizeof(SelectedVehicleCarRecord) == 48);
    static_assert(sizeof(SelectedVehicleParentRecord) == 48);
    struct SelectedVehiclePaintPacket
    {
        // Header16, cars12 words each, parent metadata12 each, components16 each.
        // Counts/word offsets/source identities are explicit; no borrowed pointers enter GPU storage.
        std::vector<uint32_t> words;
        std::shared_ptr<const Drawing::SelectedVehicleSnapshot> source;
        std::shared_ptr<const AtlasAssetLease> residency;
    };

    inline void ValidateSelectedVehiclePaintPacket(const SelectedVehiclePaintPacket& packet)
    {
        const auto& w = packet.words;
        if (!packet.source || w.size() < 16 || w[0] != kSelectedVehiclePaintMagic
            || w[1] != kSelectedVehiclePaintVersion || w[2] > kSelectedVehicleMaximumCars
            || w[3] > kSelectedVehicleMaximumComponents || w[4] != 16 || w[5] != 16 + w[2] * 12
            || w[6] != w[5] + w[3] * 12 || w[7] != w[6] + w[3] * 16 || w[7] != w.size()
            || w[8] != packet.source->sourceTick
            || (uint64_t(w[9]) | (uint64_t(w[10]) << 32)) != packet.source->worldEpoch
            || (uint64_t(w[11]) | (uint64_t(w[12]) << 32)) != packet.source->entityEpoch
            || w[13] != 0 || w[14] != 0 || w[15] != 0)
            throw std::invalid_argument("Selected vehicle packet header/source identity is invalid");
        uint32_t next = 0;
        for (uint32_t car = 0; car < w[2]; ++car)
        {
            const auto c = 16 + car * 12;
            const auto first = w[c + 2], count = w[c + 3];
            if (w[c + 1] == 0 || first != next || count > w[3] - next
                || (car != 0 && w[c] <= w[c - 12]) || w[c + 5] != 0 || w[c + 6] != 0 || w[c + 7] != 0)
                throw std::invalid_argument("Selected vehicle car ranges/identity order are invalid");
            uint32_t currentRoot = first;
            for (uint32_t i = first; i < first + count; ++i)
            {
                const auto p = w[5] + i * 12;
                const auto root = w[p + 6], flags = w[p + 8];
                if (flags == 256)
                    currentRoot = i;
                if ((flags != 256 && flags != 257 && flags != 259) || w[p + 9] != w[c + 4]
                    || root != currentRoot || root < first || root > i || w[w[5] + root * 12 + 6] != root
                    || w[w[5] + root * 12 + 8] != 256 || ((flags == 256) != (root == i)))
                    throw std::invalid_argument("Selected vehicle parent links/flags are invalid");
            }
            next += count;
        }
        if (next != w[3])
            throw std::invalid_argument("Selected vehicle components have no owning car");
    }
}
