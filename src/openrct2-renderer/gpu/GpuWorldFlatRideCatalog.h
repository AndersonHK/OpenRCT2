// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <limits>
#include <openrct2/ride/TrackStyle.h>
#include <openrct2/ride/ted/TrackElemType.h>
#include <openrct2/world/WorldObjectPresentation.h>
#include <span>
#include <stdexcept>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    namespace FlatRideRules
    {
#include "../../../data/shaders/vulkan/world_flat_ride_animation.glsl"
#include "../../../data/shaders/vulkan/world_maze_order.glsl"
    } // namespace FlatRideRules

    struct WorldFlatRideCatalog
    {
        // Header: magic,version,rideOffset,rideCount,imageMapOffset,imageMapCount,totalWords,darkenRow.
        // Ride stride20: family,carBase,typeA,typeB,stationFlags,perTrain,numStations,numTrains,
        // trackColours[4],vehicleColours[4],stationOffset,stationCount,reserved[2].
        // Station stride5: entrance/exit valid flags,entranceX/Y,exitX/Y (tile coordinates).
        // Sorted image map: original image ID,resident sprite-table index.
        std::vector<uint32_t> words;
    };
    constexpr uint32_t kWorldFlatRideCatalogMagic = 0x57464c54;
    constexpr uint32_t kWorldFlatRideWords = 20;

    inline void ValidateWorldFlatRideCatalog(std::span<const uint32_t> words, size_t spriteCount)
    {
        if (words.empty())
            return;
        if (words.size() < 8 || words[0] != kWorldFlatRideCatalogMagic || words[1] != 1 || words[6] != words.size())
            throw std::invalid_argument("GPU flat ride catalogue header is invalid");
        const auto range = [&](uint32_t start, uint32_t count, uint32_t stride) {
            return start <= words.size() && uint64_t(count) * stride <= words.size() - start;
        };
        if (words[2] < 8 || !range(words[2], words[3], kWorldFlatRideWords)
            || words[4] < uint64_t(words[2]) + uint64_t(words[3]) * kWorldFlatRideWords || !range(words[4], words[5], 2)
            || uint64_t(words[4]) + uint64_t(words[5]) * 2 != words.size())
            throw std::invalid_argument("GPU flat ride catalogue range is invalid");
        for (uint32_t i = 0; i < words[3]; i++)
        {
            const auto entry = words[2] + i * kWorldFlatRideWords;
            if (words[entry] == 0)
                continue;
            if (words[entry] > 23 || words[entry + 17] > 255 || !range(words[entry + 16], words[entry + 17], 5)
                || words[entry + 16] < uint64_t(words[2]) + uint64_t(words[3]) * kWorldFlatRideWords
                || uint64_t(words[entry + 16]) + uint64_t(words[entry + 17]) * 5 > words[4])
                throw std::invalid_argument("GPU flat ride station range is invalid");
        }
        for (uint32_t i = 0; i < words[5]; i++)
        {
            const auto entry = words[4] + i * 2;
            if (words[entry + 1] >= spriteCount || (i != 0 && words[entry - 2] >= words[entry]))
                throw std::invalid_argument("GPU flat ride image map is invalid");
        }
    }

    constexpr uint32_t WorldFlatRideFamily(TrackStyle style)
    {
        switch (style)
        {
            case TrackStyle::_3DCinema:
                return 1;
            case TrackStyle::circus:
                return 2;
            case TrackStyle::crookedHouse:
                return 3;
            case TrackStyle::hauntedHouse:
                return 4;
            case TrackStyle::spiralSlide:
                return 5;
            case TrackStyle::dodgems:
                return 6;
            case TrackStyle::flyingSaucers:
                return 7;
            case TrackStyle::merryGoRound:
                return 8;
            case TrackStyle::ferrisWheel:
                return 9;
            case TrackStyle::spaceRings:
                return 10;
            case TrackStyle::twist:
                return 11;
            case TrackStyle::enterprise:
                return 12;
            case TrackStyle::swingingShip:
                return 13;
            case TrackStyle::swingingInverterShip:
                return 14;
            case TrackStyle::magicCarpet:
                return 15;
            case TrackStyle::topSpin:
                return 16;
            case TrackStyle::motionSimulator:
                return 17;
            case TrackStyle::shop:
                return 18;
            case TrackStyle::facility:
                return 19;
            case TrackStyle::observationTower:
                return 20;
            case TrackStyle::launchedFreefall:
                return 21;
            case TrackStyle::rotoDrop:
                return 22;
            case TrackStyle::maze:
                return 23;
            default:
                return 0;
        }
    }
    constexpr TrackElemType WorldFlatRideTrackType(uint32_t family)
    {
        if (family >= 20 && family <= 22)
            return TrackElemType::towerBase;
        if (family == 23)
            return TrackElemType::maze;
        if (family == 5 || family == 17)
            return TrackElemType::flatTrack2x2;
        if (family == 6 || family == 7 || family == 12)
            return TrackElemType::flatTrack4x4;
        if (family == 9)
            return TrackElemType::flatTrack1x4C;
        if (family == 13)
            return TrackElemType::flatTrack1x5;
        if (family == 14)
            return TrackElemType::flatTrack1x4B;
        if (family == 15)
            return TrackElemType::flatTrack1x4A;
        if (family == 18 || family == 19)
            return TrackElemType::flatTrack1x1A;
        return TrackElemType::flatTrack3x3;
    }

    template<typename AppendImage>
    WorldFlatRideCatalog BuildWorldFlatRideCatalog(
        const WorldObjectPresentationMaterials& objects, const WorldRidePresentationMaterials& source,
        const WorldObjectPresentationUsage* usage, uint32_t darkenRow, AppendImage&& appendImage)
    {
        WorldFlatRideCatalog result;
        auto& words = result.words;
        if (source.rides.size() > (std::numeric_limits<uint32_t>::max() - 8) / kWorldFlatRideWords)
            throw std::overflow_error("GPU flat ride catalogue exceeds address space");
        words.resize(8 + source.rides.size() * kWorldFlatRideWords);
        words[0] = kWorldFlatRideCatalogMagic;
        words[1] = 1;
        words[2] = 8;
        words[3] = static_cast<uint32_t>(source.rides.size());
        words[7] = darkenRow;
        std::vector<uint32_t> images;
        for (uint32_t id = 0; id < source.rides.size(); id++)
        {
            const auto& ride = source.rides[id];
            if (!ride.present || (usage && !usage->Contains(4, id)))
                continue;
            const auto family = WorldFlatRideFamily(static_cast<TrackStyle>(ride.regularStyle));
            if (family == 0)
                continue;
            const auto entry = 8 + id * kWorldFlatRideWords;
            const bool needsObject = family < 20 && family != 6 && family != 7;
            const auto* object = ride.objectSlot < objects.rideObjects.size() ? &objects.rideObjects[ride.objectSlot] : nullptr;
            if (needsObject && (!object || !object->present))
                throw std::runtime_error("GPU flat ride references a missing ride object");
            words[entry] = family;
            words[entry + 1] = object && object->present ? object->carBaseImage : 0;
            words[entry + 2] = static_cast<uint32_t>(WorldFlatRideTrackType(family));
            words[entry + 3] = family == 18 ? static_cast<uint32_t>(TrackElemType::flatTrack1x1B) : words[entry + 2];
            if (family >= 20 && family <= 22)
                words[entry + 3] = static_cast<uint32_t>(TrackElemType::towerSection);
            if (ride.stationStyle < objects.stations.size() && objects.stations[ride.stationStyle].present)
                words[entry + 4] = 1u | ((objects.stations[ride.stationStyle].flags & (1u << 3)) != 0 ? 2u : 0u);
            words[entry + 5] = ride.vehicleColourSettings == 1 ? 1u : 0u;
            words[entry + 6] = ride.numStations;
            words[entry + 7] = ride.numTrains;
            for (uint32_t i = 0; i < 4; i++)
            {
                const auto& track = ride.trackColours[i];
                const auto& vehicle = ride.vehicleColours[i];
                words[entry + 8 + i] = uint32_t(track.main) | (uint32_t(track.additional) << 8)
                    | (uint32_t(track.supports) << 16);
                words[entry + 12 + i] = uint32_t(vehicle.body) | (uint32_t(vehicle.trim) << 8)
                    | (uint32_t(vehicle.tertiary) << 16);
            }
            words[entry + 16] = static_cast<uint32_t>(words.size());
            words[entry + 17] = static_cast<uint32_t>(ride.stations.size());
            for (const auto& station : ride.stations)
            {
                words.push_back((station.entranceValid ? 1u : 0u) | (station.exitValid ? 2u : 0u));
                words.push_back(static_cast<uint32_t>(station.entranceX));
                words.push_back(static_cast<uint32_t>(station.entranceY));
                words.push_back(static_cast<uint32_t>(station.exitX));
                words.push_back(static_cast<uint32_t>(station.exitY));
            }
            // Enumerate shared static asset recipes, never placed-instance image choices.
            const auto collect = [&](const FlatRideRules::WorldFlatPart& part) {
                if (part.image < 0)
                    return;
                uint64_t image = static_cast<uint32_t>(part.image);
                if (part.bank != 0)
                {
                    if (!object || !object->present)
                        throw std::runtime_error("GPU flat ride body lacks owned art");
                    image += object->carBaseImage;
                    if (image < object->imageBase || image >= uint64_t(object->imageBase) + object->imageCount)
                        throw std::runtime_error("GPU flat ride body image exceeds its owning allocation");
                }
                if (image > std::numeric_limits<uint32_t>::max())
                    throw std::overflow_error("GPU flat ride image overflow");
                images.push_back(static_cast<uint32_t>(image));
            };
            if (family == 23)
            {
                for (int style = 0; style < 5; style++)
                    for (int part = 0; part < 26; part++)
                        collect(FlatRideRules::worldMazePart(part, 65535, 0, style));
                continue;
            }
            for (int direction = 0; direction < 4; direction++)
                for (int sequence = 0; sequence < FlatRideRules::worldFlatSize(static_cast<int>(family)); sequence++)
                    for (int fenceMask : { 0, 15 })
                    {
                        const auto parts = family >= 20
                            ? FlatRideRules::worldTowerParts(
                                  static_cast<int>(family), sequence, direction, false, false, false, fenceMask)
                            : FlatRideRules::worldFlatParts(
                                  static_cast<int>(family), sequence, direction, true, false, fenceMask, 128, 0, 4);
                        for (int i = 0; i < parts.count; i++)
                            collect(parts.parts[i]);
                    }
            if (family >= 20)
            {
                const auto parts = FlatRideRules::worldTowerParts(static_cast<int>(family), 0, 0, true, true, false, 15);
                for (int i = 0; i < parts.count; i++)
                    collect(parts.parts[i]);
            }
            // Resident body sequences are object facts, independent of the current pose.
            // Rider overlays have their own future entity stream and are not pinned here.
            const auto collectRange = [&](int first, int end, int bank = 1) {
                for (int image = first; image < end; ++image)
                {
                    FlatRideRules::WorldFlatPart part{};
                    part.image = image;
                    part.bank = bank;
                    collect(part);
                }
            };
            switch (family)
            {
                case 4:
                    collectRange(4, 76);
                    break;
                case 5:
                    collectRange(20, 204);
                    break;
                case 8:
                    collectRange(0, 32);
                    break;
                case 9:
                    collectRange(0, 32);
                    break;
                case 10:
                    collectRange(0, 352);
                    break;
                case 11:
                    collectRange(0, 24);
                    break;
                case 12:
                    collectRange(0, 196);
                    break;
                case 13:
                    for (int swing = 0; swing <= 18; ++swing)
                        for (int side = 0; side < 2; ++side)
                            collectRange(swing * 18 + side * 9, swing * 18 + side * 9 + 1);
                    break;
                case 14:
                    collectRange(32, 174);
                    break;
                case 15:
                    collectRange(22006, 22134, 0);
                    break;
                case 16:
                    collectRange(0, 76);
                    collectRange(380, 576);
                    break;
                case 17:
                    collectRange(0, 140);
                    break;
                default:
                    break;
            }
        }
        std::sort(images.begin(), images.end());
        images.erase(std::unique(images.begin(), images.end()), images.end());
        words[4] = static_cast<uint32_t>(words.size());
        words[5] = static_cast<uint32_t>(images.size());
        for (const auto image : images)
        {
            words.push_back(image);
            words.push_back(appendImage(image));
        }
        if (words.size() > std::numeric_limits<uint32_t>::max())
            throw std::overflow_error("GPU flat ride catalogue overflow");
        words[6] = static_cast<uint32_t>(words.size());
        return result;
    }
} // namespace OpenRCT2::Ui::Gpu
