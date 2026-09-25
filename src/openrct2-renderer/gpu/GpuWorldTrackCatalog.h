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
    namespace MetalSupportRules
    {
#include "../../../data/shaders/vulkan/world_metal_support_rules.glsl"
#include "../../../data/shaders/vulkan/world_wooden_support_rules.glsl"
    } // namespace MetalSupportRules
    struct WorldTrackCatalog
    {
        // Header (16 words): magic,version,recipeOffset,rideOffset,rideCount,
        // rideTypeOffset,rideTypeCount,trackTypeOffset,trackTypeCount,
        // imageMapOffset,imageMapCount,totalWords,supportProgramOffset,supportProgramWords,reserved[2].
        // Ride entries: present,type,object/station,reserved, four raw colour triplets.
        // Ride type entries: regular,inverted,covered,covered-inverted styles in
        // low16 bits, with the descriptor's support type in bits16..23.
        // Track type entries: uncovered type (low16), covered bit16.
        // Sorted image map entries: original image ID,resident sprite-table index.
        std::vector<uint32_t> words;
    };

    inline void ValidateWorldTrackCatalog(std::span<const uint32_t> words)
    {
        if (words.empty())
            return;
        if (words.size() < 12 || words[0] != 0x5754524b || words[11] != words.size())
            throw std::invalid_argument("GPU track catalog header is invalid");
        const auto version = words[1] & 255u;
        if (version != 1 && version != 2)
            throw std::invalid_argument("GPU track catalog version is unsupported");
        if (version == 2)
        {
            if (words.size() < 16 || words[12] < 16 || words[12] > words.size() || words[13] < 8
                || words[13] > words.size() - words[12])
                throw std::invalid_argument("GPU track support program range is invalid");
            const auto support = words.subspan(words[12], words[13]);
            if (support[0] != 0x54535054 || support[1] != 1 || support[7] != support.size() || support[4] < 8
                || support[4] > support[5] || support[5] > support[6] || support[6] > support[7]
                || uint64_t(support[2]) * support[3] * 3 != support[5] - support[4] || (support[6] - support[5]) % 2 != 0
                || (support[7] - support[6]) % 12 != 0)
                throw std::invalid_argument("GPU track support program header is invalid");
        }
    }

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
        words.resize(16);
        words[0] = 0x5754524b;
        const bool csgLoaded = IsCsgLoaded();
        words[1] = 2u | (csgLoaded ? 1u << 8 : 0u);
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
            {
                const auto& drawer = getTrackDrawerEntry(descriptor, inverted && (variant & 1) != 0, (variant & 2) != 0);
                words.push_back(static_cast<uint32_t>(drawer.trackStyle) | (uint32_t(drawer.supportType.generic) << 16));
            }
        }
        words[7] = offset();
        words[8] = static_cast<uint32_t>(TrackElemType::count);
        for (uint32_t type = 0; type < static_cast<uint32_t>(TrackElemType::count); ++type)
        {
            const auto track = static_cast<TrackElemType>(type);
            words.push_back(static_cast<uint32_t>(uncoverTrackType(track)) | (trackTypeIsCovered(track) ? 1u << 16 : 0u));
        }
        words[12] = offset();
        const auto supportDefinitions = Drawing::GetNativeTrackSupportWords();
        words[13] = static_cast<uint32_t>(supportDefinitions.size());
        words.insert(words.end(), supportDefinitions.begin(), supportDefinitions.end());
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
                const auto style = words[words[5] + ride.rideType * 4 + variant] & 65535u;
                if (style < requiredStyles.size())
                    requiredStyles[style] = true;
            }
        }
        std::vector<uint32_t> images;
        // The shared original-art map also serves park/ride entrance columns.
        // Their bounded truss domain must exist even in a park without tracks.
        for (int i = 0; i < MetalSupportRules::worldEntranceSupportAssetCount(); ++i)
            images.push_back(static_cast<uint32_t>(MetalSupportRules::worldEntranceSupportAssetImage(i)));
        bool needsMetal = false, needsWooden = false;
        for (uint32_t style = 0; style < requiredStyles.size() && !(needsMetal && needsWooden); ++style)
        {
            if (!requiredStyles[style] || style >= supportDefinitions[2])
                continue;
            for (uint32_t type = 0; type < supportDefinitions[3] && !(needsMetal && needsWooden); ++type)
            {
                const auto descriptor = supportDefinitions[4] + (style * supportDefinitions[3] + type) * 3;
                const auto count = (1u << std::popcount(supportDefinitions[descriptor + 2]))
                    * supportDefinitions[descriptor + 1] * 4;
                for (uint32_t i = 0; i < count && !(needsMetal && needsWooden); ++i)
                {
                    const auto row = supportDefinitions[5] + (supportDefinitions[descriptor] + i) * 2;
                    for (uint32_t op = 0; op < supportDefinitions[row + 1]; ++op)
                    {
                        const auto opcode = supportDefinitions[supportDefinitions[6] + (supportDefinitions[row] + op) * 12];
                        needsMetal |= opcode == 1 || opcode == 2;
                        needsWooden |= opcode == 5 || opcode == 6;
                    }
                }
            }
        }
        if (needsMetal)
            for (int i = 0; i < MetalSupportRules::worldMetalAssetCount(); ++i)
                images.push_back(static_cast<uint32_t>(MetalSupportRules::worldMetalAssetImage(i)));
        if (needsWooden)
            for (int i = 0; i < MetalSupportRules::worldWoodenAssetCount(); ++i)
                images.push_back(static_cast<uint32_t>(MetalSupportRules::worldWoodenAssetImage(i)));
        bool needsStations = false;
        bool needsChairliftStations = false;
        for (uint32_t style = 0; style < requiredStyles.size(); ++style)
        {
            if (!requiredStyles[style])
                continue;
            // Static tower bodies dispatch through the flat-family catalogue,
            // but their authored vertical openings use this shared G1 lookup.
            if (style == static_cast<uint32_t>(TrackStyle::observationTower)
                || style == static_cast<uint32_t>(TrackStyle::launchedFreefall)
                || style == static_cast<uint32_t>(TrackStyle::rotoDrop) || style == static_cast<uint32_t>(TrackStyle::lift))
                for (uint32_t image = 1575; image <= 1578; ++image)
                    images.push_back(image);
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
                        const auto partOffset = definitions[6] + (definitions[row] + part) * 12;
                        // Water components use the terrain catalog's resident mask/overlay banks.
                        if ((definitions[partOffset + 10] & 7u) >= 4)
                            continue;
                        const auto image = definitions[partOffset];
                        if (image == 0xfffffffdu)
                        {
                            if (definitions[definitions[6] + (definitions[row] + part) * 12 + 1] == 2)
                                for (uint32_t vertical = 1575; vertical <= 1578; ++vertical)
                                    images.push_back(vertical);
                            continue;
                        }
                        if (image == 0xfffffffcu)
                        {
                            const uint32_t first = definitions[definitions[6] + (definitions[row] + part) * 12 + 2] != 0
                                ? 23485u
                                : 25615u;
                            for (uint32_t photo = first; photo < first + 12; ++photo)
                                images.push_back(photo);
                            continue;
                        }
                        if (image != 0xfffffffeu)
                            for (uint32_t frame = 0; frame < Drawing::GetNativeTrackImageFrameCount(image); ++frame)
                                images.push_back(Drawing::GetNativeTrackImageAtTick(image, 0) + frame);
                        else
                        {
                            needsStations = true;
                            needsChairliftStations |= definitions[partOffset + 8] == 8;
                        }
                    }
                }
            }
        }
        if (needsStations)
            for (uint32_t image = SPR_STATION_PLATFORM_SW_NE; image <= SPR_STATION_BASE_BORDERLESS; ++image)
                images.push_back(image);
        if (needsChairliftStations)
        {
            for (uint32_t image = 14567; image <= 14571; ++image)
                images.push_back(image);
            for (uint32_t image = 20502; image <= 20507; ++image)
                images.push_back(image);
            for (uint32_t image = 20540; image <= 20547; ++image)
                images.push_back(image);
        }
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
