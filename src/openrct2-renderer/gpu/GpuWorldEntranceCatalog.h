// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <openrct2/world/WorldObjectPresentation.h>
#include <span>
#include <stdexcept>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    struct WorldEntranceCatalog
    {
        std::vector<uint32_t> words;
    };
    inline void ValidateWorldEntranceCatalog(std::span<const uint32_t> words, uint32_t spriteCount)
    {
        const auto span = [&](uint32_t offset, uint32_t count, uint32_t stride) {
            if (uint64_t(offset) + uint64_t(count) * stride > words.size())
                throw std::invalid_argument("GPU entrance catalog table exceeds its owned words");
        };
        const auto sprites = [&](uint32_t base, uint32_t count) {
            if ((base == UINT32_MAX && count != 0) || (base != UINT32_MAX && uint64_t(base) + count > spriteCount))
                throw std::invalid_argument("GPU entrance catalog exceeds its owned sprites");
        };
        if (words.size() < 8 || words[7] != 1)
            throw std::invalid_argument("Invalid GPU entrance catalog header");
        span(words[0], words[1], 16);
        span(words[2], words[3], 4);
        span(words[4], words[5], 8);
        for (uint32_t slot = 0; slot < words[1]; ++slot)
        {
            const auto entry = words[0] + slot * 16;
            if (words[entry] == 0)
                continue;
            for (uint32_t part = 0; part < 8; ++part)
            {
                const auto base = words[entry + 4 + part];
                sprites(base, base == UINT32_MAX ? 0 : 4);
            }
            sprites(words[entry + 12], words[entry + 13]);
            sprites(words[entry + 14], words[entry + 15]);
        }
        for (uint32_t slot = 0; slot < words[3]; ++slot)
        {
            const auto entry = words[2] + slot * 4;
            sprites(words[entry], words[entry + 1]);
        }
        for (uint32_t id = 0; id < words[5]; ++id)
        {
            const auto entry = words[4] + id * 8;
            if (words[entry] == 0)
                continue;
            span(words[entry + 4], words[entry + 5], 10);
        }
    }
    // Header8: station offset/count, park offset/count, ride offset/count, glass palette row, version.
    // Station16: present,flags,height,scrolling, eight four-direction sprite bases, shelter base/count, glass base/count.
    // Park4: base,count,scrolling,textHeight. Ride8: present,style,primary,secondary,stations offset/count,0,0.
    // Station facts10: validity bits(start/entrance/exit), startXYZ(world), entranceXYZ(tile), exitXYZ(tile).
    template<typename AppendImage>
    WorldEntranceCatalog BuildWorldEntranceCatalog(
        const WorldObjectPresentationMaterials& objects, const WorldRidePresentationMaterials& rides,
        const WorldObjectPresentationUsage* usage, uint32_t glassPaletteRow, AppendImage&& appendImage)
    {
        WorldEntranceCatalog result;
        auto& words = result.words;
        const auto stationCount = static_cast<uint32_t>(objects.stations.size());
        const auto parkCount = static_cast<uint32_t>(objects.parkEntrances.size());
        const auto rideCount = static_cast<uint32_t>(rides.rides.size());
        words.resize(8 + stationCount * 16 + parkCount * 4 + rideCount * 8);
        words[0] = 8;
        words[1] = stationCount;
        words[2] = 8 + stationCount * 16;
        words[3] = parkCount;
        words[4] = words[2] + parkCount * 4;
        words[5] = rideCount;
        words[6] = glassPaletteRow;
        words[7] = 1;
        std::vector<bool> usedStations(stationCount);
        for (uint32_t id = 0; id < rideCount; ++id)
        {
            const auto& ride = rides.rides[id];
            if (!ride.present || (usage && !usage->ContainsRide(id)))
                continue;
            const auto entry = words[4] + id * 8;
            words[entry] = 1;
            words[entry + 1] = ride.stationStyle;
            words[entry + 2] = ride.trackColours[0].main;
            words[entry + 3] = ride.trackColours[0].additional;
            if (ride.stationStyle < stationCount)
                usedStations[ride.stationStyle] = true;
            words[entry + 4] = static_cast<uint32_t>(words.size());
            words[entry + 5] = static_cast<uint32_t>(ride.stations.size());
            for (const auto& station : ride.stations)
            {
                words.push_back(
                    uint32_t(station.startValid) | (uint32_t(station.entranceValid) << 1) | (uint32_t(station.exitValid) << 2));
                for (int32_t value : { station.startX, station.startY, station.startZ, station.entranceX, station.entranceY,
                                       station.entranceZ, station.exitX, station.exitY, station.exitZ })
                    words.push_back(static_cast<uint32_t>(value));
            }
        }
        const auto range = [&](const auto& object, uint32_t image, uint32_t count) {
            if (image == UINT32_MAX)
                return UINT32_MAX;
            if (image < object.imageBase || uint64_t(image) + count > uint64_t(object.imageBase) + object.imageCount
                || uint64_t(image) + count > UINT32_MAX)
                throw std::runtime_error("GPU entrance images exceed owning allocation");
            uint32_t base = UINT32_MAX;
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto index = appendImage(image + i);
                if (i == 0)
                    base = index;
                else if (uint64_t(base) + i != index)
                    throw std::runtime_error("GPU entrance sprite table must be contiguous");
            }
            return base;
        };
        for (uint32_t slot = 0; slot < stationCount; ++slot)
        {
            const auto& object = objects.stations[slot];
            if (!object.present || (usage && !usedStations[slot]))
                continue;
            const auto entry = words[0] + slot * 16;
            words[entry] = 1;
            words[entry + 1] = object.flags;
            words[entry + 2] = static_cast<uint32_t>(object.height);
            words[entry + 3] = object.scrollingMode;
            const uint32_t images[] = { object.entranceBack,  object.entranceFront,     object.exitBack,
                                        object.exitFront,     object.entranceBackGlass, object.entranceFrontGlass,
                                        object.exitBackGlass, object.exitFrontGlass };
            for (uint32_t i = 0; i < 8; ++i)
                words[entry + 4 + i] = range(object, images[i], 4);
            for (uint32_t i = 0; i < 2; ++i)
            {
                const auto image = i == 0 ? object.shelter : object.shelterGlass;
                uint32_t count = 0;
                if (image != UINT32_MAX)
                {
                    if (image < object.imageBase || uint64_t(image) >= uint64_t(object.imageBase) + object.imageCount)
                        throw std::runtime_error("GPU station shelter exceeds owning allocation");
                    count = static_cast<uint32_t>(
                        std::min<uint64_t>(12, uint64_t(object.imageBase) + object.imageCount - image));
                }
                words[entry + 12 + i * 2] = range(object, image, count);
                words[entry + 13 + i * 2] = count;
            }
        }
        for (uint32_t slot = 0; slot < parkCount; ++slot)
        {
            const auto& object = objects.parkEntrances[slot];
            if (!object.present || (usage && !usage->Contains(5, slot)))
                continue;
            const auto entry = words[2] + slot * 4;
            words[entry] = range(object, object.image, 12);
            words[entry + 1] = 12;
            words[entry + 2] = object.scrollingMode;
            words[entry + 3] = object.textHeight;
        }
        return result;
    }
} // namespace OpenRCT2::Ui::Gpu
