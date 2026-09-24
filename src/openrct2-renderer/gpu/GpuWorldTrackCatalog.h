// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <bit>
#include <limits>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/NativeTrackRecipes.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/ted/TrackElemType.h>
#include <openrct2/world/WorldObjectPresentation.h>
#include <stdexcept>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    struct WorldTrackCatalog
    {
        // Header (12 words): magic,version,recipeOffset,rideOffset,rideCount,
        // rideTypeOffset,rideTypeCount,trackTypeOffset,trackTypeCount,
        // imageMapOffset,imageMapCount,totalWords.
        // Ride entries: present,type,object/station,reserved, four raw colour triplets.
        // Ride type entries: regular,inverted,covered,covered-inverted styles.
        // Track type entries: uncovered type (low16), covered bit16.
        // Sorted image map entries: original image ID,resident sprite-table index.
        std::vector<uint32_t> words;
    };

    template<typename AppendImage>
    WorldTrackCatalog BuildWorldTrackCatalog(const WorldRidePresentationMaterials& source, AppendImage&& appendImage)
    {
        WorldTrackCatalog result;
        auto& words = result.words;
        const auto offset = [&]() {
            if (words.size() > std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("GPU track catalog exceeds address space");
            return static_cast<uint32_t>(words.size());
        };
        words.resize(12);
        words[0] = 0x5754524b;
        const bool csgLoaded = IsCsgLoaded();
        words[1] = 1u | (csgLoaded ? 1u << 8 : 0u);
        words[2] = offset();
        const auto definitions = Drawing::GetNativeTrackRecipeWords();
        words.insert(words.end(), definitions.begin(), definitions.end());
        words[3] = offset();
        words[4] = static_cast<uint32_t>(source.rides.size());
        for (const auto& ride : source.rides)
        {
            words.push_back(ride.present ? 1u : 0u);
            words.push_back(ride.rideType);
            words.push_back(uint32_t(ride.objectSlot) | (uint32_t(ride.stationStyle) << 16));
            words.push_back(0);
            for (const auto& colour : ride.trackColours)
                words.push_back(uint32_t(colour.main) | (uint32_t(colour.additional) << 8) | (uint32_t(colour.supports) << 16));
        }
        words[5] = offset();
        words[6] = RIDE_TYPE_COUNT;
        for (uint32_t type = 0; type < RIDE_TYPE_COUNT; ++type)
        {
            const auto& descriptor = GetRideTypeDescriptor(static_cast<ride_type_t>(type));
            const bool inverted = descriptor.flags.has(RtdFlag::hasInvertedVariant);
            for (const auto variant : { 0, 1, 2, 3 })
                words.push_back(static_cast<uint32_t>(
                    getTrackDrawerEntry(descriptor, inverted && (variant & 1) != 0, (variant & 2) != 0).trackStyle));
        }
        words[7] = offset();
        words[8] = static_cast<uint32_t>(TrackElemType::count);
        for (uint32_t type = 0; type < static_cast<uint32_t>(TrackElemType::count); ++type)
        {
            const auto track = static_cast<TrackElemType>(type);
            words.push_back(static_cast<uint32_t>(uncoverTrackType(track)) | (trackTypeIsCovered(track) ? 1u << 16 : 0u));
        }
        words[9] = offset();
        // Residency follows shared styles belonging to present rides, never
        // placed track instances. Use the same RTD variant table as the GPU.
        std::vector<bool> requiredStyles(definitions[2]);
        for (const auto& ride : source.rides)
        {
            if (!ride.present || ride.rideType >= words[6])
                continue;
            for (uint32_t variant = 0; variant < 4; ++variant)
            {
                const auto style = words[words[5] + ride.rideType * 4 + variant];
                if (style < requiredStyles.size())
                    requiredStyles[style] = true;
            }
        }
        std::vector<uint32_t> images;
        bool needsStations = false;
        for (uint32_t style = 0; style < requiredStyles.size(); ++style)
        {
            if (!requiredStyles[style])
                continue;
            for (uint32_t type = 0; type < definitions[3]; ++type)
            {
                const auto descriptor = definitions[4] + (style * definitions[3] + type) * 3;
                const auto sequences = definitions[descriptor + 1], mask = definitions[descriptor + 2];
                const auto count = (1u << std::popcount(mask)) * sequences * 4;
                for (uint32_t index = 0; index < count; ++index)
                {
                    const auto variant = index / (sequences * 4);
                    if ((mask & 16u) != 0u && bool((variant >> std::popcount(mask & 15u)) & 1u) != csgLoaded)
                        continue;
                    const auto row = definitions[5] + (definitions[descriptor] + index) * 2;
                    for (uint32_t part = 0; part < definitions[row + 1]; ++part)
                    {
                        const auto image = definitions[definitions[6] + (definitions[row] + part) * 12];
                        if (image != 0xfffffffeu)
                            images.push_back(image);
                        else
                            needsStations = true;
                    }
                }
            }
        }
        if (needsStations)
            for (uint32_t image = SPR_STATION_PLATFORM_SW_NE; image <= SPR_STATION_BASE_BORDERLESS; ++image)
                images.push_back(image);
        std::sort(images.begin(), images.end());
        images.erase(std::unique(images.begin(), images.end()), images.end());
        words[10] = static_cast<uint32_t>(images.size());
        // Every required image must resolve; allocation/asset failures propagate.
        for (const auto image : images)
        {
            words.push_back(image);
            words.push_back(appendImage(image));
        }
        words[11] = offset();
        return result;
    }
} // namespace OpenRCT2::Ui::Gpu
