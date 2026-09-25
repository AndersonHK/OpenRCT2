// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <openrct2/SpriteIds.h>
#include <openrct2/core/Console.hpp>
#include <openrct2/drawing/VehiclePresentation.h>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    constexpr uint32_t kWorldVehicleCarWords = 96;
    inline constexpr uint32_t kWorldVehicleSelector[] = {
#include "../../openrct2/drawing/NativeVehicleSelector.inc"
    };
    struct WorldVehicleCatalog
    {
        std::vector<uint32_t> words;
    };
    inline uint64_t WorldVehicleImageBankCount(const Drawing::VehiclePresentationCar& car)
    {
        uint64_t count = uint64_t(car.carImages) * (1 + car.seatingRows);
        // Specialized painters use authored layouts, not the generic sprite-group
        // stride calculated by RideObject::Load. Include their finite body/rider
        // ranges even when numCarImages only describes a 32-frame flat group.
        uint32_t specialized = 0;
        switch (car.paintStyle)
        {
            case 2:
                specialized = 21;
                break; // Freefall: 9 body + four 3-image rider groups.
            case 3:
                specialized = 36;
                break; // Observation: final restraint pair is 34/35.
            case 4:
            case 15:
                specialized = 5 * 72;
                break; // Rapids/reel: body + four rider banks.
            case 9:
                specialized = 20 + 48 + 64 + 3 + 1;
                break; // RotoDrop's last visible open-restraint rider.
            case 17:
                specialized = 3 * 183;
                break; // Classic spinner body + two rider-facing banks.
        }
        return std::max(count, uint64_t(specialized));
    }
    inline void ValidateWorldVehicleCatalog(std::span<const uint32_t> w)
    {
        if (w.empty())
            return;
        if (w.size() < 16 || w[0] != 0x56534331 || w[1] != 1 || w[3] != 16 || w[2] > 8192
            || w[4] != 16 + w[2] * kWorldVehicleCarWords || w[5] != std::size(kWorldVehicleSelector) || w[6] != w[4] + w[5]
            || w[7] > (w.size() - std::min<size_t>(w[6], w.size())) / 2 || w[6] + uint64_t(w[7]) * 2 != w.size()
            || w[8] != w.size())
            throw std::invalid_argument("Native vehicle catalog layout is invalid");
        if (!std::equal(std::begin(kWorldVehicleSelector), std::end(kWorldVehicleSelector), w.begin() + w[4]))
            throw std::invalid_argument("Native vehicle selector identity is invalid");
    }
    template<typename Append>
    WorldVehicleCatalog BuildWorldVehicleCatalog(
        const Drawing::VehiclePresentationCatalog& source, std::span<const uint32_t> usedCars, uint32_t ghostRow,
        Append&& appendImage)
    {
        if (source.cars.size() > 8192)
            throw std::length_error("Native vehicle car catalog capacity exceeded");
        WorldVehicleCatalog out;
        auto& w = out.words;
        w.resize(16 + source.cars.size() * kWorldVehicleCarWords);
        w[0] = 0x56534331;
        w[1] = 1;
        w[2] = static_cast<uint32_t>(source.cars.size());
        w[3] = 16;
        w[4] = static_cast<uint32_t>(w.size());
        w[5] = static_cast<uint32_t>(std::size(kWorldVehicleSelector));
        w[9] = ghostRow;
        w[10] = SPR_WATER_PARTICLES_DENSE_0;
        std::set<uint32_t> images;
        uint64_t bankImageCount = 0;
        size_t admittedCars = 0;
        const auto admitBank = [&](const Drawing::VehiclePresentationCar& car, uint64_t count) {
            if (car.baseImage < car.imageBase || count > car.imageCount
                || uint64_t(car.baseImage) + count > uint64_t(car.imageBase) + car.imageCount)
                throw std::invalid_argument("Native vehicle image bank exceeds owning object allocation");
            for (uint32_t i = 0; i < count; i++)
                images.insert(car.baseImage + i);
            bankImageCount += count;
        };
        for (const auto slot : usedCars)
        {
            if (slot >= source.cars.size())
                throw std::invalid_argument("Native vehicle car slot is invalid");
            const auto& c = source.cars[slot];
            if (!c.present || c.paintStyle == 1)
                continue;
            auto p = 16 + slot * kWorldVehicleCarWords;
            w[p] = 1;
            w[p + 1] = c.paintStyle;
            w[p + 2] = c.baseImage;
            w[p + 3] = c.baseFrames;
            w[p + 4] = c.carImages;
            w[p + 5] = c.seatingRows;
            w[p + 6] = static_cast<uint32_t>(c.flags);
            w[p + 7] = static_cast<uint32_t>(c.flags >> 32);
            w[p + 8] = c.spinningFrames;
            w[p + 9] = c.effectVisual;
            w[p + 10] = c.drawOrder;
            w[p + 11] = c.imageBase;
            w[p + 12] = c.imageCount;
            for (uint32_t g = 0; g < Drawing::kVehiclePresentationGroups; g++)
            {
                w[p + 16 + g * 2] = c.groups[g][0];
                w[p + 17 + g * 2] = c.groups[g][1];
            }
            admitBank(c, slot == source.cableCar ? c.imageCount : WorldVehicleImageBankCount(c));
            if (c.paintStyle == 5 || c.paintStyle == 6)
            {
                // Both MiniGolf painters address Cars[0], including the player's
                // 37 four-direction frames after the single ball image.
                admitBank(source.cars[(slot / 4) * 4], 1 + 37 * 4);
            }
            ++admittedCars;
        }
        if (!usedCars.empty())
        {
            for (uint32_t i = 0; i < 8; i++)
                images.insert(SPR_WATER_PARTICLES_DENSE_0 + i);
            for (uint32_t i = 0; i < 32; i++)
            {
                images.insert(SPR_SPLASH_EFFECT_1_NE_0 + i);
                images.insert(SPR_SPLASH_EFFECT_3_NE_0 + i);
                images.insert(SPR_SPLASH_EFFECT_5_NE_0 + i);
            }
        }
        w.insert(w.end(), std::begin(kWorldVehicleSelector), std::end(kWorldVehicleSelector));
        w[6] = static_cast<uint32_t>(w.size());
        w[7] = static_cast<uint32_t>(images.size());
        // Report the complete immutable union before any resident sprite allocation can fail.
        // This is catalog-build work only, never a per-frame or per-vehicle image selection.
        Console::WriteLine(
            "Vulkan vehicle admission: usedSlots=%zu admittedBanks=%zu bankImages=%llu uniqueImagesIncludingEffects=%zu",
            usedCars.size(), admittedCars, static_cast<unsigned long long>(bankImageCount), images.size());
        for (auto image : images)
        {
            w.push_back(image);
            w.push_back(appendImage(image));
        }
        w[8] = static_cast<uint32_t>(w.size());
        ValidateWorldVehicleCatalog(w);
        return out;
    }
} // namespace OpenRCT2::Ui::Gpu
